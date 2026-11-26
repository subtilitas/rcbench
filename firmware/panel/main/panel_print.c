/**
 * @file panel_print.c
 * @brief The label printer on the panel (panel_print.h): the client of the
 *        `blec` task that runs print_ctl.h, and the frame task's glue.
 *
 * The `blec` task runs it every 5ms: the queue's requests and the printer's
 * address go to the controller, then the Bluetooth state, the bench and the
 * device update, then the controller's poll and its heap block
 * (panel_print_blec_poll()), then the host stack's events the hub routes to
 * it, then every action it hands out, then the view is published
 * (panel_print_blec_publish()).
 *
 * SPDX-License-Identifier: MIT
 */
#include "panel_print.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "panel.h"
#include "panel_ble.h"
#include "panel_blec.h"
#include "panel_devupd.h"
#include "panel_dial.h"
#include "print_ctl.h"
#include "rcbench_version.h"
#include "wireless_printer.h"

/** @brief The console lines' tag. */
static const char *TAG = "print";

/** @brief Messages the queue holds. */
#define PRINT_Q_LEN 4

_Static_assert(PANEL_BLEC_PRINT == PRINT_BLE_OWNER,
               "the controller's tokens carry the hub's owner");

/** @brief What the frame task posts: a request or SETUP's printer. */
typedef struct {
    bool        is_cfg;                    /**< addr and name, else req */
    print_req_t req;                       /**< the request */
    char        addr[BLEU_ADDR_TEXT_MAX];  /**< SETUP's address */
    char        name[PRINT_NAME_SIZE];     /**< SETUP's name */
} print_msg_t;

/** @brief The frame task's messages to the task; NULL before the start. */
static QueueHandle_t s_q;
/** @brief Guards s_pub and the console's line. */
static portMUX_TYPE  s_mux = portMUX_INITIALIZER_UNLOCKED;
/** @brief Written by the `blec` task under s_mux, read by the frame task. */
static print_view_t  s_pub = { .left = -1 };
/** @brief The `blec` task's own: too big for its stack. */
static print_ctl_t   s_ctl;
/** @brief The `blec` task's copy before it is published. */
static print_view_t  s_next;
/** @brief The phase last logged, the `blec` task's own. */
static uint8_t       s_logged = PRINT_IDLE;
/** @brief A request runs: panel_print_busy(). */
static atomic_bool   s_busy;
/** @brief The console's line for the frame task, "" none. */
static char          s_line[64];

/**
 * @brief SETUP's printer to the task; dropped while its queue is full.
 */
static void post_cfg(void)
{
    if (s_q == NULL) {
        return;
    }
    print_msg_t m;
    memset(&m, 0, sizeof(m));
    m.is_cfg = true;
    (void)snprintf(m.addr, sizeof(m.addr), "%s",
                   settings_text(SET_TEXT_PRN_ADDR));
    (void)snprintf(m.name, sizeof(m.name), "%s",
                   settings_text(SET_TEXT_PRN_NAME));
    if (xQueueSend(s_q, &m, 0) != pdTRUE) {
        ESP_LOGW(TAG, "queue full: printer address not handed over");
    }
}

/**
 * @brief The panel's Bluetooth state as the controller's.
 * @return The state.
 */
static imu_radio_t radio_now(void)
{
    switch (panel_ble_state()) {
    case PANEL_BLE_STARTING: return IMU_RADIO_STARTING;
    case PANEL_BLE_NO_LINK:  return IMU_RADIO_NO_LINK;
    case PANEL_BLE_NO_SYNC:  return IMU_RADIO_NO_SYNC;
    case PANEL_BLE_READY:    return IMU_RADIO_READY;
    case PANEL_BLE_OFF:
    default:                 return IMU_RADIO_OFF;
    }
}

/**
 * @brief The heap block the controller asks for, PSRAM first, and the one
 *        it hands back.
 * @param now The clock.
 */
static void block_service(uint32_t now)
{
    const size_t want = print_ctl_block_want(&s_ctl);
    if (want != 0u) {
        void *p = heap_caps_malloc(want, MALLOC_CAP_SPIRAM);
        if (p == NULL) {
            p = heap_caps_malloc(want, MALLOC_CAP_8BIT);
        }
        ESP_LOGI(TAG, "block of %u bytes %s", (unsigned)want,
                 p != NULL ? "taken" : "refused");
        print_ctl_block_give(&s_ctl, p, now);
    }
    void *back = print_ctl_block_back(&s_ctl);
    if (back != NULL) {
        heap_caps_free(back);
    }
}

void panel_print_blec_poll(uint32_t now)
{
    if (s_q == NULL) {
        return;
    }
    print_ctl_radio(&s_ctl, radio_now(), now);
    print_ctl_gate(&s_ctl, panel_dial_live(), panel_devupd_busy(), now);
    print_msg_t m;
    while (xQueueReceive(s_q, &m, 0) == pdTRUE) {
        if (m.is_cfg) {
            print_ctl_bind(&s_ctl, m.addr, m.name, now);
            continue;
        }
        if (m.req.kind != PRINT_REQ_ABORT
            && panel_ble_state() == PANEL_BLE_OFF) {
            ESP_LOGI(TAG, "request: starting Bluetooth LE");
            panel_ble_start();
        }
        const bool took = print_ctl_request(&s_ctl, &m.req, now);
        ESP_LOGI(TAG, "request %u %s%s%s", (unsigned)m.req.kind,
                 took ? "taken" : "refused: a job or a scan runs",
                 m.req.id[0] != '\0' ? " " : "", m.req.id);
    }
    print_ctl_poll(&s_ctl, now);
    block_service(now);
    atomic_store(&s_busy, print_ctl_busy(&s_ctl));
}

void panel_print_blec_event(const bleu_evt_t *e, uint32_t now)
{
    if (s_q != NULL) {
        print_ctl_event(&s_ctl, e, now);
    }
}

bool panel_print_blec_step(uint32_t now, bleu_act_t *out)
{
    return s_q != NULL && print_ctl_step(&s_ctl, now, out);
}

const uint8_t *panel_print_blec_ext(const bleu_act_t *a)
{
    return s_q != NULL ? print_ctl_ext(&s_ctl, a) : NULL;
}

/**
 * @brief The console's line for a change of phase: the link's times, the
 *        MTU, the build's time, the frames and the verdict.
 * @param now The clock.
 */
static void log_phase(uint32_t now)
{
    const print_view_t *v = &s_next;
    const bleu_t *b = &s_ctl.b;
    switch (v->phase) {
    case PRINT_READING:
        ESP_LOGI(TAG, "linked to %s: connect %ums, set-up %ums, MTU %u, "
                      "%u attempts", v->printer, (unsigned)b->t_connect,
                 (unsigned)b->t_setup, (unsigned)b->mtu,
                 (unsigned)b->attempts);
        break;
    case PRINT_BUILDING:
        ESP_LOGI(TAG, "roll %ux%umm gap %umm, %ld left: building",
                 (unsigned)v->w_mm, (unsigned)v->h_mm, (unsigned)v->gap_mm,
                 (long)v->left);
        break;
    case PRINT_SENDING:
        ESP_LOGI(TAG, "built in %ums: %u buffers, stream %u bytes",
                 (unsigned)s_ctl.build_ms, (unsigned)s_ctl.bufs,
                 (unsigned)s_ctl.zlen);
        break;
    case PRINT_PRINTING:
        ESP_LOGI(TAG, "%u frames sent, %ums after the request",
                 (unsigned)v->frames, (unsigned)(now - s_ctl.t_req));
        break;
    case PRINT_DONE:
        ESP_LOGI(TAG, "done in %ums: %ld left%s", (unsigned)(now - s_ctl.t_req),
                 (long)v->left, v->relinked ? ", after a reconnect" : "");
        break;
    case PRINT_FAILED:
        ESP_LOGW(TAG, "failed after %ums: error %u (%s)",
                 (unsigned)(now - s_ctl.t_req), (unsigned)v->err,
                 ui_tr_in(UI_LANG_EN, print_err_text(v->err)));
        break;
    default:
        ESP_LOGI(TAG, "phase %u", (unsigned)v->phase);
        break;
    }
}

void panel_print_blec_publish(uint32_t now)
{
    if (s_q == NULL) {
        return;
    }
    print_ctl_view_of(&s_ctl, &s_next);
    if (s_next.phase != s_logged) {
        s_logged = s_next.phase;
        log_phase(now);
    }
    taskENTER_CRITICAL(&s_mux);
    s_pub = s_next;
    taskEXIT_CRITICAL(&s_mux);
}

bool panel_print_busy(void)
{
    return atomic_load(&s_busy);
}

void panel_print_start(void)
{
    print_ctl_init(&s_ctl, RCBENCH_VERSION_STRING);
    s_q = xQueueCreate(PRINT_Q_LEN, sizeof(print_msg_t));
    if (s_q == NULL) {
        ESP_LOGE(TAG, "queue not created; label printer off");
        return;
    }
    post_cfg();
}

void panel_print_text_changed(setting_text_id_t id)
{
    if (id == SET_TEXT_PRN_ADDR || id == SET_TEXT_PRN_NAME) {
        post_cfg();
    }
}

/**
 * @brief The view on the console.
 * @param v The view.
 */
static void print_view_line(const print_view_t *v)
{
    printf("print: %s%s, phase %u, error %u, %s, roll %ux%umm, %ld left, "
           "MTU %u, frames %u/%u\n",
           v->bound ? "printer " : "no printer bound",
           v->bound ? v->printer : "", (unsigned)v->phase, (unsigned)v->err,
           v->linked ? "linked" : "not linked", (unsigned)v->w_mm,
           (unsigned)v->h_mm, (long)v->left, (unsigned)v->mtu,
           (unsigned)v->frame, (unsigned)v->frames);
    for (unsigned i = 0u; i < v->found_n && i < PRINT_FOUND_MAX; ++i) {
        printf("print: found %s %s %ddBm\n", v->found[i].addr,
               v->found[i].name, (int)v->found[i].rssi);
    }
}

/**
 * @brief Bind from the console: the address kept in SETUP, `P` added when
 *        no type follows it.
 * @param text The address.
 */
static void console_bind(const char *text)
{
    char addr[BLEU_ADDR_TEXT_MAX];
    const size_t n = strlen(text);
    bleu_addr_t a;
    if (n == 17u) {
        (void)snprintf(addr, sizeof(addr), "%.17s P", text);
    } else if (n < sizeof(addr)) {
        memcpy(addr, text, n + 1u);
    } else {
        addr[0] = '\0';
    }
    if (!bleu_addr_parse(addr, &a)) {
        printf("print: bind: not an address: %s\n", text);
        return;
    }
    settings_set_text(SET_TEXT_PRN_ADDR, addr);
    settings_set_text(SET_TEXT_PRN_NAME, "");
    settings_request_save();
    printf("print: bound %s\n", addr);
}

/**
 * @brief Run a console line on the frame task.
 * @param line The line, `print` and its words.
 */
static void console_run(const char *line)
{
    /* The console passes "print" or "print <words>"; arg is the words. */
    const char *arg = (line[5] == ' ') ? line + 6 : line + 5;
    if (strcmp(line, "print") == 0) {
        print_view_line(print_ctl_view());
    } else if (strcmp(arg, "test") == 0) {
        printf("print: test label %s\n",
               print_ctl_test() ? "asked" : "refused: a job runs");
    } else if (strncmp(arg, "id ", 3u) == 0) {
        printf("print: label %s %s\n", arg + 3,
               print_ctl_label(arg + 3, settings_text(SET_TEXT_DUT_NAME))
                   ? "asked" : "refused: a job runs or not a servo id");
    } else if (strcmp(arg, "query") == 0) {
        print_ctl_query();
        printf("print: query asked\n");
    } else if (strcmp(arg, "scan") == 0) {
        printf("print: scan %s\n",
               print_ctl_scan() ? "asked" : "refused: a job or a scan runs");
    } else if (strcmp(arg, "stop") == 0) {
        print_ctl_abort();
        printf("print: stop asked\n");
    } else if (strncmp(arg, "bind ", 5u) == 0) {
        console_bind(arg + 5);
    } else if (strcmp(arg, "unbind") == 0) {
        settings_set_text(SET_TEXT_PRN_ADDR, "");
        settings_set_text(SET_TEXT_PRN_NAME, "");
        settings_request_save();
        printf("print: unbound\n");
    } else {
        printf("print: commands: print, print test, print id ID, "
               "print query, print scan, print stop, print bind ADDRESS, "
               "print unbind\n");
    }
}

void panel_print_console(const char *line)
{
    if (line == NULL) {
        return;
    }
    taskENTER_CRITICAL(&s_mux);
    (void)snprintf(s_line, sizeof(s_line), "%s", line);
    taskEXIT_CRITICAL(&s_mux);
}

void panel_print_frame(void)
{
    char line[sizeof(s_line)];
    /* The frame task's own: too big for its stack with the scan's list. */
    static print_view_t v;
    taskENTER_CRITICAL(&s_mux);
    memcpy(line, s_line, sizeof(line));
    s_line[0] = '\0';
    v = s_pub;
    taskEXIT_CRITICAL(&s_mux);
    print_ctl_show(&v);
    wireless_printer_tick();
    if (line[0] != '\0') {
        console_run(line);
    }
    if (s_q == NULL) {
        return;
    }
    print_msg_t m;
    memset(&m, 0, sizeof(m));
    if (print_ctl_take(&m.req) && xQueueSend(s_q, &m, 0) != pdTRUE) {
        ESP_LOGW(TAG, "queue full: request %u dropped", (unsigned)m.req.kind);
    }
}
