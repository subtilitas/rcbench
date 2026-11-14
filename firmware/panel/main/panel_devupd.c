/**
 * @file panel_devupd.c
 * @brief The wireless devices' update on the panel (panel_devupd.h): the
 *        carried images, the `blec` task's update client, and the frame's
 *        DIAL FIRMWARE row.
 *
 * SPDX-License-Identifier: MIT
 */
#include "panel_devupd.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "devupd.h"
#include "devupd_client.h"
#include "fw_image.h"
#include "fw_sha256.h"
#include "panel.h"
#include "panel_ble.h"
#include "panel_blec.h"
#include "panel_dial.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "upd_dev.h"

/** @brief The console lines' tag. */
static const char *TAG = "devupd";

/** @brief From the hold to the Dial's link down, ms at most. */
#define HOLD_MS 5000u
/** @brief The console's progress line, every this many percent. */
#define LOG_STEP_PCT 10u

_Static_assert(PANEL_BLEC_UPD == 4u, "the update client is the hub's owner 4");
_Static_assert(PANEL_BLEC_UPD < BLEU_OWNER_MAX,
               "the update client's owner fits a token");

#ifdef RCBENCH_DIAL_IMAGE
/** @brief The carried Dial image (main/CMakeLists.txt). @{ */
extern const uint8_t _binary_rcbench_dial_bin_start[];
extern const uint8_t _binary_rcbench_dial_bin_end[];
/** @} */
#endif
#ifdef RCBENCH_NODE_PWR_IMAGE
/** @brief The carried power node image. @{ */
extern const uint8_t _binary_rcbench_node_pwr_bin_start[];
extern const uint8_t _binary_rcbench_node_pwr_bin_end[];
/** @} */
#endif
#ifdef RCBENCH_NODE_BAL_IMAGE
/** @brief The carried balance node image. @{ */
extern const uint8_t _binary_rcbench_node_bal_bin_start[];
extern const uint8_t _binary_rcbench_node_bal_bin_end[];
/** @} */
#endif

/** @brief A carried image's check. */
typedef enum {
    IMG_NONE = 0, /**< not carried */
    IMG_DUE,      /**< carried, not checked yet */
    IMG_READY,    /**< checked: version and SHA-256 known */
    IMG_BAD,      /**< failed its check */
} img_state_t;

/** @brief One carried image. */
typedef struct {
    const char     *word;    /**< its name on the console */
    const uint8_t  *p;       /**< its first byte */
    uint32_t        n;       /**< its length */
    uint8_t         kind;    /**< BLEOTA_KIND_* */
    uint16_t        chip;    /**< FW_CHIP_* it must name */
    const char     *project; /**< the project it must name */
    uint32_t        slot;    /**< the device's slot */
    uint8_t         st;      /**< img_state_t, written by `blec` */
    fw_err_t        err;     /**< IMG_BAD: why */
    fw_image_info_t info;    /**< IMG_READY: what it says */
    uint8_t         sha[32]; /**< IMG_READY: the SHA-256 of every byte */
} carried_t;

/** @brief The images, by BLEOTA_KIND_* - 1. */
static carried_t s_img[BLEOTA_KIND_MAX] = {
    { .word = "Dial", .kind = BLEOTA_KIND_DIAL, .chip = DEVUPD_CHIP_ESP32S3,
      .project = DEVUPD_PROJECT_DIAL, .slot = DEVUPD_SLOT_DIAL },
    { .word = "power node", .kind = BLEOTA_KIND_NODE_POWER,
      .chip = FW_CHIP_ESP32C6, .project = DEVUPD_PROJECT_NODE,
      .slot = DEVUPD_SLOT_NODE },
    { .word = "balance node", .kind = BLEOTA_KIND_NODE_BAL,
      .chip = FW_CHIP_ESP32C6, .project = DEVUPD_PROJECT_NODE,
      .slot = DEVUPD_SLOT_NODE },
};

/** @brief Where the `blec` task's update stands. */
typedef enum {
    STAGE_IDLE = 0, /**< none */
    STAGE_HOLD,     /**< the Dial's own link ends */
    STAGE_RUN,      /**< the client runs the transfer */
} stage_t;

/** @brief What the `blec` task publishes for the frame. */
typedef struct {
    bool         img_due;   /**< the Dial's image is not checked yet */
    bool         have_img;  /**< it is carried */
    bool         img_ok;    /**< it passed */
    fw_version_t carried;   /**< its version */
    bool         linked;    /**< the Dial is connected, INFO taken */
    fw_version_t running;   /**< the version its INFO named */
    uint8_t      phase;     /**< devupd_phase_t of the transfer */
    unsigned     percent;   /**< its share taken */
    unsigned     secs;      /**< DEVUPD_CONFIRM: seconds given */
    uint32_t     end_seq;   /**< updates ended, counted */
    bool         ok;        /**< the last one installed */
    uint8_t      err;       /**< otherwise devupd_err_t */
    uint8_t      code;      /**< and the device's BLEOTA_E_* */
} pub_t;

/** @brief The update client, the `blec` task's own. */
static devupd_client_t s_cli;
/** @brief The `blec` task's stage. */
static stage_t         s_stage;
/** @brief When the stage began. */
static uint32_t        s_stage_t;
/** @brief The image being sent. */
static devupd_image_t  s_im;
/** @brief The transfer's start, for its rate. */
static uint32_t        s_t0;
/** @brief The last progress line's share. */
static unsigned        s_logged_pct;
/** @brief The last phase logged. */
static uint8_t         s_logged_phase;
/** @brief The client was set up. */
static bool            s_started;
/** @brief The `blec` task's copy before it is published. */
static pub_t           s_next;
/** @brief Guards s_pub. */
static portMUX_TYPE    s_mux = portMUX_INITIALIZER_UNLOCKED;
/** @brief Published by `blec`, read by the frame. */
static pub_t           s_pub;
/** @brief The frame asked for a start. */
static atomic_bool     s_start_req;
/** @brief panel_devupd_busy(). */
static atomic_bool     s_busy;
/** @brief The frame task's last end_seq. */
static uint32_t        s_seen_end;
/** @brief The frame task's last phase, for the band's ask. */
static uint8_t         s_seen_phase;

bool panel_devupd_busy(void)
{
    return atomic_load(&s_busy);
}

void panel_devupd_start(void)
{
#ifdef RCBENCH_DIAL_IMAGE
    s_img[0].p = _binary_rcbench_dial_bin_start;
    /* cppcheck takes the two linker symbols for two arrays. */
    // cppcheck-suppress subtractPointers
    s_img[0].n = (uint32_t)(_binary_rcbench_dial_bin_end - _binary_rcbench_dial_bin_start);
#endif
#ifdef RCBENCH_NODE_PWR_IMAGE
    s_img[1].p = _binary_rcbench_node_pwr_bin_start;
    /* cppcheck takes the two linker symbols for two arrays. */
    // cppcheck-suppress subtractPointers
    s_img[1].n = (uint32_t)(_binary_rcbench_node_pwr_bin_end - _binary_rcbench_node_pwr_bin_start);
#endif
#ifdef RCBENCH_NODE_BAL_IMAGE
    s_img[2].p = _binary_rcbench_node_bal_bin_start;
    /* cppcheck takes the two linker symbols for two arrays. */
    // cppcheck-suppress subtractPointers
    s_img[2].n = (uint32_t)(_binary_rcbench_node_bal_bin_end - _binary_rcbench_node_bal_bin_start);
#endif
    for (unsigned i = 0; i < BLEOTA_KIND_MAX; ++i) {
        s_img[i].st = s_img[i].p != NULL ? IMG_DUE : IMG_NONE;
    }
    s_started = devupd_client_init(&s_cli, PANEL_BLEC_UPD);
    if (!s_started) {
        ESP_LOGE(TAG, "update client not set up; no device updates");
    }
}

/**
 * @brief Check the next image that waits for its check, one a poll: a poll
 *        that checks runs no longer than one image's two passes.
 */
static void check_next(void)
{
    for (unsigned i = 0; i < BLEOTA_KIND_MAX; ++i) {
        carried_t *c = &s_img[i];
        if (c->st != IMG_DUE) {
            continue;
        }
        c->err = fw_image_check_as(c->p, c->n, c->slot, c->chip, c->project,
                                   &c->info);
        if (c->err != FW_OK) {
            c->st = IMG_BAD;
            ESP_LOGE(TAG, "the carried %s image fails its check: %s",
                     c->word, fw_err_str(c->err));
            return;
        }
        fw_sha256(c->p, c->n, c->sha);
        c->st = IMG_READY;
        ESP_LOGI(TAG, "carries the %s image %s, %lu bytes, %lu%% of its "
                      "slot, SHA-256 %02x%02x%02x%02x", c->word,
                 c->info.version_text, (unsigned long)c->n,
                 (unsigned long)((uint64_t)c->n * 100u / c->slot),
                 c->sha[0], c->sha[1], c->sha[2], c->sha[3]);
        return;
    }
}

/**
 * @brief End the update: the console's line, the Dial's link back, the
 *        frame told.
 * @param ok   installed
 * @param err  devupd_err_t otherwise
 * @param code the device's BLEOTA_E_*
 * @param now  the clock
 */
static void finish(bool ok, uint8_t err, uint8_t code, uint32_t now)
{
    const uint32_t ms = now - s_t0;
    if (ok) {
        ESP_LOGI(TAG, "Dial update done: %u.%u.%u installed, %lu bytes in "
                      "%lums, %lu bytes/s, %lu rewinds, %lu polls; the Dial "
                      "restarts", (unsigned)s_im.version.major,
                 (unsigned)s_im.version.minor, (unsigned)s_im.version.patch,
                 (unsigned long)s_im.size, (unsigned long)ms,
                 (unsigned long)(ms > 0u ? (uint64_t)s_im.size * 1000u / ms
                                         : 0u),
                 (unsigned long)s_cli.e.rewinds,
                 (unsigned long)s_cli.e.polls_sent);
    } else {
        ESP_LOGW(TAG, "Dial update failed: error %u, device code %u, at %lu "
                      "of %lu bytes after %lums; the Dial keeps its firmware",
                 (unsigned)err, (unsigned)code,
                 (unsigned long)s_cli.e.next, (unsigned long)s_im.size,
                 (unsigned long)ms);
    }
    s_next.ok = ok;
    s_next.err = err;
    s_next.code = code;
    ++s_next.end_seq;
    s_stage = STAGE_IDLE;
    panel_dial_blec_hold(false, now);
}

/**
 * @brief The row's hold: hold the Dial's link off, if the Dial is there and
 *        its image ready.
 * @param bound  a Dial is bound
 * @param linked it is connected, its INFO taken
 * @param ver    the version INFO named
 * @param now    the clock
 */
static void begin(bool bound, bool linked, const uint8_t ver[3], uint32_t now)
{
    const carried_t *c = &s_img[0];
    if (!s_started || !bound || !linked || c->st != IMG_READY
        || s_stage != STAGE_IDLE) {
        ESP_LOGW(TAG, "Dial update refused: bound %d, linked %d, image %u",
                 bound, linked, (unsigned)c->st);
        s_t0 = now;
        finish(false, DEVUPD_E_LINK, 0u, now);
        return;
    }
    memset(&s_im, 0, sizeof(s_im));
    s_im.img = c->p;
    s_im.size = c->n;
    s_im.kind = c->kind;
    s_im.version = c->info.version;
    memcpy(s_im.sha, c->sha, sizeof(s_im.sha));
    const fw_version_t run = { ver[0], ver[1], ver[2] };
    s_im.reinstall = fw_version_cmp(run, s_im.version) == 0;
    ESP_LOGI(TAG, "Dial update%s: %s, %lu bytes; the Dial runs %u.%u.%u; "
                  "its link is held off", s_im.reinstall ? " (reinstall)" : "",
             c->info.version_text, (unsigned long)s_im.size,
             (unsigned)ver[0], (unsigned)ver[1], (unsigned)ver[2]);
    s_t0 = now;
    s_logged_pct = 0u;
    s_logged_phase = DEVUPD_IDLE;
    s_stage = STAGE_HOLD;
    s_stage_t = now;
    panel_dial_blec_hold(true, now);
}

/**
 * @brief The console's lines of the transfer's phases and progress.
 */
static void log_progress(void)
{
    const devupd_t *e = &s_cli.e;
    if (e->phase != s_logged_phase) {
        s_logged_phase = e->phase;
        if (e->phase == DEVUPD_BEGIN) {
            ESP_LOGI(TAG, "Dial update: link up, MTU %u, %u bytes a chunk",
                     (unsigned)s_cli.b.mtu, (unsigned)e->chunk);
        } else if (e->phase == DEVUPD_CONFIRM) {
            ESP_LOGI(TAG, "Dial update: the Dial asks for its press, %us",
                     (unsigned)e->secs);
        } else if (e->phase == DEVUPD_END) {
            ESP_LOGI(TAG, "Dial update: every byte taken, END sent");
        }
    }
    const unsigned pct = devupd_percent(e);
    if (e->phase == DEVUPD_SEND && pct >= s_logged_pct + LOG_STEP_PCT) {
        s_logged_pct = pct - pct % LOG_STEP_PCT;
        ESP_LOGI(TAG, "Dial update %u%%: %lu of %lu bytes, %lu rewinds, %lu "
                      "polls", pct, (unsigned long)e->next,
                 (unsigned long)e->im.size, (unsigned long)e->rewinds,
                 (unsigned long)e->polls_sent);
    }
}

void panel_devupd_blec_poll(uint32_t now)
{
    if (!s_started) {
        return;
    }
    check_next();
    bleu_addr_t addr;
    bool linked = false, down = false;
    uint8_t ver[3] = { 0u, 0u, 0u };
    const bool bound = panel_dial_blec_target(&addr, &linked, ver, &down);
    if (atomic_exchange(&s_start_req, false)) {
        begin(bound, linked, ver, now);
    }
    if (s_stage == STAGE_HOLD) {
        if (bound && down) {
            (void)devupd_client_start(&s_cli, &addr, &s_im, now);
            s_stage = STAGE_RUN;
        } else if (now - s_stage_t >= HOLD_MS) {
            ESP_LOGW(TAG, "Dial update: the Dial's link did not end in %ums",
                     (unsigned)HOLD_MS);
            finish(false, DEVUPD_E_LINK, 0u, now);
        }
    }
    if (s_stage == STAGE_RUN) {
        log_progress();
        if (!devupd_busy(&s_cli.e) && !s_cli.on) {
            finish(s_cli.e.phase == DEVUPD_DONE, s_cli.e.err,
                   s_cli.e.dev_code, now);
        }
    }
    const carried_t *c = &s_img[0];
    s_next.img_due = c->st == IMG_DUE;
    s_next.have_img = c->st != IMG_NONE;
    s_next.img_ok = c->st == IMG_READY;
    s_next.carried = c->info.version;
    s_next.linked = bound && linked;
    s_next.running = (fw_version_t){ ver[0], ver[1], ver[2] };
}

void panel_devupd_blec_event(const bleu_evt_t *e, uint32_t now)
{
    if (s_started) {
        devupd_client_event(&s_cli, e, now);
    }
}

bool panel_devupd_blec_step(uint32_t now, bleu_act_t *out)
{
    return s_started && devupd_client_step(&s_cli, now, out);
}

void panel_devupd_blec_chunks(uint32_t now)
{
    if (!s_started || s_stage != STAGE_RUN) {
        return;
    }
    devupd_out_t o;
    uint16_t conn = 0u, handle = 0u;
    for (int i = 0; i < PANEL_DEVUPD_CHUNKS
         && devupd_client_chunk(&s_cli, now, &o, &conn, &handle); ++i) {
        uint8_t hdr[BLEOTA_CHUNK_HDR];
        bleota_chunk_header(o.off, hdr);
        const int rc = panel_ble_write_chunk(conn, handle, hdr, sizeof(hdr),
                                             s_im.img + o.off, o.n);
        if (rc != 0) {
            devupd_client_unsend(&s_cli, &o);
            break;
        }
    }
}

void panel_devupd_blec_publish(uint32_t now)
{
    (void)now;
    if (!s_started) {
        return;
    }
    s_next.phase = s_stage == STAGE_HOLD ? (uint8_t)DEVUPD_LINK
                                         : s_cli.e.phase;
    s_next.percent = devupd_percent(&s_cli.e);
    s_next.secs = s_cli.e.secs;
    taskENTER_CRITICAL(&s_mux);
    s_pub = s_next;
    taskEXIT_CRITICAL(&s_mux);
    atomic_store(&s_busy, s_stage != STAGE_IDLE
                              || atomic_load(&s_start_req));
}

/**
 * @brief The band's reason for a failed update.
 * @param p   what was published
 * @param buf room for an "error %u" text
 * @param n   its bytes
 * @return the text
 */
static const char *fail_text(const pub_t *p, char *buf, size_t n)
{
    if (p->err != DEVUPD_E_DEVICE) {
        switch (p->err) {
        case DEVUPD_E_LINK:    return TR(DEV_FAIL_LINK);
        case DEVUPD_E_LOST:    return TR(DEV_FAIL_LOST);
        case DEVUPD_E_TIMEOUT: return TR(DEV_FAIL_TIMEOUT);
        case DEVUPD_E_WRITE:   return TR(DEV_FAIL_WRITE);
        default:
            snprintf(buf, n, TR(DEV_FAIL_OTHER), (unsigned)p->err);
            return buf;
        }
    }
    switch (p->code) {
    case BLEOTA_E_KIND:       return TR(DEV_FAIL_KIND);
    case BLEOTA_E_SIZE:       return TR(DEV_FAIL_SIZE);
    case BLEOTA_E_SAME:       return TR(DEV_FAIL_SAME);
    case BLEOTA_E_NO_CONFIRM: return TR(DEV_FAIL_CONFIRM);
    case BLEOTA_E_REFUSED:    return TR(DEV_FAIL_REFUSED);
    case BLEOTA_E_SHA:        return TR(DEV_FAIL_SHA);
    case BLEOTA_E_FLASH:      return TR(DEV_FAIL_FLASH);
    case BLEOTA_E_IMAGE:      return TR(DEV_FAIL_IMAGE);
    case BLEOTA_E_TIMEOUT:    return TR(DEV_FAIL_TIMEOUT);
    default:
        snprintf(buf, n, TR(DEV_FAIL_OTHER), (unsigned)p->code);
        return buf;
    }
}

void devupd_service(bool armed)
{
    pub_t p;
    taskENTER_CRITICAL(&s_mux);
    p = s_pub;
    taskEXIT_CRITICAL(&s_mux);
    const bool busy = atomic_load(&s_busy);
    const devupd_row_in_t in = {
        .have_image = p.have_img, .image_ok = p.img_ok || p.img_due,
        .linked = p.linked, .known = p.linked, .running = p.running,
        .carried = p.carried, .armed = armed,
        .other = panel_c6_busy() || panel_cu_busy() || panel_fw_busy(),
        .busy = busy,
    };
    const upd_dev_view_t v = {
        .row = devupd_row(&in), .known = p.linked, .running = p.running,
        .have_image = p.img_ok, .carried = p.carried, .phase = p.phase,
        .percent = p.percent, .secs = p.secs,
    };
    upd_dev_set(UPD_DEV_DIAL, &v);
    if (upd_dev_take(UPD_DEV_DIAL)) {
        if (v.row == DEVUPD_ROW_READY || v.row == DEVUPD_ROW_REINSTALL) {
            atomic_store(&s_busy, true);
            atomic_store(&s_start_req, true);
        } else {
            ui_router_set_notice(upd_dev_reason(&v));
        }
    }
    if (busy && p.phase == DEVUPD_CONFIRM && s_seen_phase != DEVUPD_CONFIRM) {
        ui_router_set_notice(TR(ALERT_DIAL_ASK));
    }
    s_seen_phase = p.phase;
    if (p.end_seq != s_seen_end) {
        s_seen_end = p.end_seq;
        char line[128], other[40];
        char ver[24];
        snprintf(ver, sizeof(ver), "%u.%u.%u", (unsigned)p.carried.major,
                 (unsigned)p.carried.minor, (unsigned)p.carried.patch);
        if (p.ok) {
            snprintf(line, sizeof(line), TR(ALERT_DEV_DONE), "Dial", ver);
        } else {
            snprintf(line, sizeof(line), TR(ALERT_DEV_FAILED), "Dial",
                     fail_text(&p, other, sizeof(other)));
        }
        control_alert(line);
    }
}
