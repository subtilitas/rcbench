/**
 * @file panel_update.c
 * @brief The updates: the IOmcu's over CAN (Controller Area Network) from
 * the image this panel carries, the panel's own from WI-FI or
 * from GitHub, and the IOmcu update's screen on the render side.
 * cu_service() runs on the control task; the rest runs on app_main.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "can_twai.h"
#include "coproc_update_screen.h"
#include "display.h"
#include "flash_gate.h"
#include "fw_install.h"
#include "fw_ota.h"
#include "fw_release.h"
#include "gfx.h"
#include "link_host.h"
#include "link_pages.h"
#include "link_port.h"
#include "link_update_host.h"
#include "panel.h"
#include "rcbench_version.h"
#include "settings.h"
#include "upd_screen.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "wa_fetch.h"
#include "wifi_access.h"
#include "wifi_screen.h"

static const char *TAG = "rcbench";

/*
 * The panel's own update.  An install about to write the flash asks the
 * control task to stand still (s_fw_quiet_req) and waits for it to say so
 * (s_fw_quiet_ack): no polls, no exchanges, and the heartbeat withheld on
 * purpose, so the IOmcu goes to failsafe as it does at any restart of
 * the panel.  A write that fails lets it go again.
 */
atomic_bool s_fw_quiet_req;
atomic_bool s_fw_quiet_ack;
/*
 * An install and the IOmcu's update exclude each other.  An install
 * says so from INSTALL's hold until its write has failed (s_fw_installing);
 * the control task says so for an update, and for the BUY of an image on
 * trial (s_cu_busy).  Each side stores its own flag before it reads the
 * other's, so of two starting at once at least one sees the other.
 */
static atomic_bool s_fw_installing;
static atomic_bool s_cu_busy;
/* False when wifi_access_init() failed: no call into wifi_access then. */
bool        s_wifi_ok;
/* Where PANEL UPDATE's CLOSE leads: to the screen that opened WI-FI, LOGS
 * or SETUP, after an upload's install, to UPDATES or OVERVIEW after a
 * download. */
static ui_screen_id_t s_update_back = SCREEN_LOGS;


/* -------------------------------------------- the IOmcu's update */

/*
 * The IOmcu image this panel carries: the release's own, embedded by
 * the build option RCBENCH_IOMCU_BIN (main/CMakeLists.txt).  A build without
 * it carries none and offers no update.
 */
#ifdef RCBENCH_IOMCU_IMAGE
extern const uint8_t _binary_rcbench_iomcu_bin_start[];
extern const uint8_t _binary_rcbench_iomcu_bin_end[];
#define CU_IMG _binary_rcbench_iomcu_bin_start
/* The image is two linker symbols, its start and its end.  cppcheck takes
 * them for two arrays and their difference for a subtraction across objects;
 * to the linker they are one object.  The suppression covers every use of
 * CU_LEN and nothing else in this file. */
// cppcheck-suppress-macro subtractPointers
#define CU_LEN ((uint32_t)(_binary_rcbench_iomcu_bin_end \
                           - _binary_rcbench_iomcu_bin_start))
#else
#define CU_IMG ((const uint8_t *)NULL)
#define CU_LEN 0u
#endif

/* Steps run back to back in one pass of the control task while an update
 * sends: a block is 9 exchanges, and the pass's own work and its 5ms
 * delay come between two slices. */
#define CU_SLICE_MS 20u

typedef enum {
    CU_IDLE = 0,
    CU_CHECKING,    /* does it run the carried build: link_uph_check()   */
    CU_OFFERED,     /* it does not; the question waits for START         */
    CU_DECLINED,    /* CANCEL, until the next link-up                    */
    CU_UPDATING,    /* link_uph_update(): the link is the update's       */
    CU_CONFIRMING,  /* an image found on trial is bought                 */
} cu_state_t;

/* Control task only. */
static link_uph_t s_cu;
static cu_state_t s_cu_state;
static cu_version_t s_cu_running;
static bool       s_cu_missed;     /* the update's last exchange went unanswered */
static bool       s_cu_took_down;  /* the update has taken the link down  */
static uint32_t   s_cu_starts_taken;
/* CANCEL holds for the version it was given for, until the panel restarts. */
static bool         s_cu_declined;
static cu_version_t s_cu_declined_for;
/* Render side -> control task: STARTs given on the question, and the offer
 * the question was declined for (its offer_gen; 0 none). */
static atomic_uint s_cu_starts;
static atomic_uint s_cu_declined_gen;
/* Control task: the offer_gen of the offer published last. */
static uint32_t    s_cu_offer_gen;

/* Control task: every change of s_cu_state, and with it s_cu_busy. */
static void cu_enter(cu_state_t st)
{
    s_cu_state = st;
    atomic_store(&s_cu_busy, st == CU_UPDATING || st == CU_CONFIRMING);
}

/* Control task: what UPDATES' IOmcu section reads of the last build
 * check. */
static void cu_publish_check(cu_check_t check)
{
    snap_lock();
    s_snap.cu.check = (uint8_t)check;
    snap_unlock();
}

/* Whether the update may start: see fw_coproc_may_start().  From any task. */
bool cu_may_start(void)
{
    return fw_coproc_may_start(!s_wifi_ok || wifi_access_down(),
                               atomic_load(&s_fw_installing));
}

static cu_version_t cu_carried(void)
{
    return (cu_version_t){ RCBENCH_VERSION_MAJOR, RCBENCH_VERSION_MINOR,
                           RCBENCH_VERSION_PATCH };
}

/*
 * The update's own wire: exchange() after the bus has been asked back.  An
 * IOmcu that restarts answers nothing for a few hundred milliseconds,
 * and a transmitter nobody acknowledges is off the bus in about 4ms and
 * does not come back on its own (probe_identity()).  Asked only after an
 * exchange went unanswered, so a block costs no status read.
 */
static bool cu_port_exchange(void *ctx, link_host_t *host,
                             const link_msg_t *req, link_msg_t *reply)
{
    (void)ctx;
    if (s_cu_missed && can_twai_recover() == CAN_TWAI_RECOVERING) {
        ++s_recoveries;
        ++s_recoveries_total;
    }
    s_cu_missed = !exchange(host, req, reply);
    return !s_cu_missed;
}

static const link_port_t k_cu_port = {
    cu_port_exchange, port_now, NULL, port_between,
};

static void cu_publish_offer(bool offered)
{
    snap_lock();
    s_snap.cu.offered = offered;
    s_snap.cu.running = s_cu_running;
    s_snap.cu.starts_taken = s_cu_starts_taken;
    if (offered) {
        ++s_snap.cu.offer_gen;
    }
    s_cu_offer_gen = s_snap.cu.offer_gen;
    snap_unlock();
}

static cu_step_t cu_step_of(uint8_t phase)
{
    switch (phase) {
    case LINK_UPH_BEGIN:
    case LINK_UPH_SEND:
    case LINK_UPH_RESYNC:
    case LINK_UPH_FINISH:      return CU_STEP_SEND;
    case LINK_UPH_VERIFY:
    case LINK_UPH_ACTIVATE:    return CU_STEP_VERIFY;
    case LINK_UPH_RESTARTING:  return CU_STEP_RESTART;
    case LINK_UPH_BUY:
    case LINK_UPH_DONE:        return CU_STEP_BUY;
    case LINK_UPH_PREPARE:
    default:                   return CU_STEP_PREPARE;
    }
}

/* An active update withdraws the offer in the same snapshot that shows its
 * START taken. */
static void cu_publish_progress(bool active)
{
    snap_lock();
    if (active) {
        s_snap.cu.offered = false;
    }
    s_snap.cu.active = active;
    s_snap.cu.step   = (uint8_t)cu_step_of(s_cu.phase);
    s_snap.cu.block  = s_cu.block;
    s_snap.cu.blocks = s_cu.blocks;
    s_snap.cu.starts_taken = s_cu_starts_taken;
    snap_unlock();
}

/*
 * An IOmcu answered its identity: a link-up, or a probe from an
 * IOmcu of another major.  Its build is checked against the carried
 * image, whatever was decided for the IOmcu before it.
 */
void cu_link_up(const link_msg_t *reply)
{
    if (CU_LEN == 0u || reply == NULL || s_cu_state == CU_UPDATING) {
        return;
    }
    s_cu_running = (cu_version_t){ reply->regs[LINK_ID_FIRMWARE_MAJOR],
                                   reply->regs[LINK_ID_FIRMWARE_MINOR],
                                   reply->regs[LINK_ID_FIRMWARE_PATCH] };
    cu_publish_offer(false);
    link_uph_check(&s_cu, CU_IMG, CU_LEN, now_ms());
    cu_enter(CU_CHECKING);
    cu_publish_check(CU_CHECK_RUNNING);
}

/* Why an update stopped, in the alert's words. */
static void cu_alert_failed(void)
{
    char why[48];
    switch (s_cu.result) {
    case LINK_UPH_NO_ANSWER:
        snprintf(why, sizeof(why), "%s", TR(CU_WHY_NO_ANSWER));
        break;
    case LINK_UPH_BAD_DIGEST:
        snprintf(why, sizeof(why), "%s", TR(CU_WHY_DIGEST));
        break;
    case LINK_UPH_BUSY:
        snprintf(why, sizeof(why), "%s", TR(CU_WHY_BUSY));
        break;
    case LINK_UPH_RESTARTED_OUT:
        snprintf(why, sizeof(why), "%s", TR(CU_WHY_RESTARTS));
        break;
    case LINK_UPH_NOT_STARTED:
        snprintf(why, sizeof(why), "%s", TR(CU_WHY_NOT_STARTED));
        break;
    case LINK_UPH_REFUSED:
    default:
        snprintf(why, sizeof(why), TR(CU_WHY_REFUSED),
                 (unsigned)s_cu.error);
        break;
    }
    char line[ALERT_MAX];
    snprintf(line, sizeof(line), TR(ALERT_CU_FAILED), why);
    control_alert(line);
}

/* The check has ended: what it found decides the offer. */
static void cu_checked(void)
{
    switch (s_cu.result) {
    case LINK_UPH_SAME:
        ESP_LOGI(TAG, "IOmcu runs the build this panel carries");
        cu_enter(CU_IDLE);
        cu_publish_check(CU_CHECK_SAME);
        break;
    case LINK_UPH_DIFFERENT:
        cu_publish_check(CU_CHECK_DIFFERENT);
        if (s_cu_declined
            && memcmp(&s_cu_declined_for, &s_cu_running,
                      sizeof(s_cu_running)) == 0) {
            ESP_LOGI(TAG, "IOmcu runs another build; its update was "
                          "declined");
            cu_enter(CU_DECLINED);
            break;
        }
        ESP_LOGI(TAG, "IOmcu %u.%u.%u runs another build; %u.%u.%u "
                      "offered", (unsigned)s_cu_running.major,
                 (unsigned)s_cu_running.minor, (unsigned)s_cu_running.patch,
                 (unsigned)RCBENCH_VERSION_MAJOR,
                 (unsigned)RCBENCH_VERSION_MINOR,
                 (unsigned)RCBENCH_VERSION_PATCH);
        cu_enter(CU_OFFERED);
        control_alert(TR(ALERT_CU_OFFER));
        cu_publish_offer(true);
        break;
    case LINK_UPH_TRIAL:
        /* A new image runs on trial, and answered: that is the condition
         * for keeping it (link_pages.h), whichever panel sent it. */
        ESP_LOGI(TAG, "IOmcu runs an image on trial; buying it");
        link_uph_confirm(&s_cu, now_ms());
        cu_enter(CU_CONFIRMING);
        cu_publish_check(CU_CHECK_UPDATING);
        break;
    case LINK_UPH_NEEDS_USB:
        ESP_LOGW(TAG, "IOmcu takes no update over CAN");
        control_alert(TR(ALERT_CU_USB));
        cu_enter(CU_IDLE);
        cu_publish_check(CU_CHECK_USB);
        break;
    default:
        ESP_LOGW(TAG, "IOmcu build check ended %u, error %u",
                 (unsigned)s_cu.result, (unsigned)s_cu.error);
        cu_enter(CU_IDLE);
        cu_publish_check(CU_CHECK_FAILED);
        break;
    }
}

/* The update has ended: the link goes back to the poll, which finds the
 * IOmcu again by its identity.  A frame of its own: inlined, its
 * alert buffers sit in cu_service()'s frame, above poll_far_end() on the
 * control task's deepest chain. */
static __attribute__((noinline)) void cu_ended(void)
{
    if (s_cu.result == LINK_UPH_UPDATED) {
        ESP_LOGI(TAG, "IOmcu updated to %u.%u.%u",
                 (unsigned)RCBENCH_VERSION_MAJOR,
                 (unsigned)RCBENCH_VERSION_MINOR,
                 (unsigned)RCBENCH_VERSION_PATCH);
        char line[ALERT_MAX];
        snprintf(line, sizeof(line), TR(ALERT_CU_DONE),
                 (unsigned)RCBENCH_VERSION_MAJOR,
                 (unsigned)RCBENCH_VERSION_MINOR,
                 (unsigned)RCBENCH_VERSION_PATCH);
        control_alert(line);
    } else if (s_cu.result == LINK_UPH_NEEDS_USB) {
        control_alert(TR(ALERT_CU_USB));
    } else {
        ESP_LOGW(TAG, "IOmcu update ended %u, error %u, block %u of %u",
                 (unsigned)s_cu.result, (unsigned)s_cu.error,
                 (unsigned)s_cu.block, (unsigned)s_cu.blocks);
        cu_alert_failed();
    }
    cu_enter(CU_IDLE);
    /* The link-up after the update checks the build again; an IOmcu of
     * another protocol major gives no link-up, and its next answer to the
     * probe checks it. */
    cu_publish_check(CU_CHECK_NONE);
    link_uph_mismatch_rearm(&s_mismatch);
    s_cu_took_down = false;
    s_cu_missed = false;
    /* The link-lost screen counts from here, not from the update's start:
     * the poll finds the IOmcu inside its LINK_LOST_SCREEN_MS. */
    if (atomic_load(&s_link_lost_ms) != 0u) {
        atomic_store(&s_link_lost_ms, now_ms());
    }
    cu_publish_progress(false);
}

/*
 * One pass of the update, before the poll.  Returns true while the update
 * holds the link, and the poll is then not run.
 *
 * The check and the BUY of an image on trial share the link with the poll,
 * one step a pass.  An update takes the link down first -- the poll's own
 * edge, with everything that ends there: the arm, the supply, the pages'
 * state -- and then runs alone for CU_SLICE_MS a pass.  The IOmcu
 * refuses ARM for the whole session, and the panel shows no band.
 */
bool cu_service(bool *link_up, bench_state_t *bench,
                uint32_t *last_poll, uint32_t *last_status)
{
    switch (s_cu_state) {
    case CU_CHECKING:
        if (link_uph_step(&s_cu, &s_host, &k_port) == LINK_UPH_DONE) {
            cu_checked();
        }
        return false;
    case CU_CONFIRMING:
        if (link_uph_step(&s_cu, &s_host, &k_port) == LINK_UPH_DONE) {
            if (s_cu.result == LINK_UPH_UPDATED) {
                control_alert(TR(ALERT_CU_KEPT));
                /* Bought, whichever panel sent it: whether it is the build
                 * this panel carries is checked again, as at a link-up. */
                link_uph_check(&s_cu, CU_IMG, CU_LEN, now_ms());
                cu_enter(CU_CHECKING);
                cu_publish_check(CU_CHECK_RUNNING);
            } else {
                ESP_LOGW(TAG, "the image on trial was not bought: %u, "
                              "error %u", (unsigned)s_cu.result,
                         (unsigned)s_cu.error);
                cu_enter(CU_IDLE);
                cu_publish_check(CU_CHECK_FAILED);
            }
        }
        return false;
    case CU_OFFERED:
    case CU_DECLINED:
        /* A decline counts for the offer it was given on only: one that
         * crossed a newer link-up's check is for an offer gone.  A declined
         * update still starts from SETUP > UPDATES. */
        if (s_cu_state == CU_OFFERED
            && atomic_load(&s_cu_declined_gen) == s_cu_offer_gen) {
            ESP_LOGI(TAG, "IOmcu update declined");
            cu_enter(CU_DECLINED);
            s_cu_declined = true;
            s_cu_declined_for = s_cu_running;
            cu_publish_offer(false);
            return false;
        }
        if (atomic_load(&s_cu_starts) == s_cu_starts_taken) {
            return false;
        }
        s_cu_starts_taken = atomic_load(&s_cu_starts);
        /* Busy first, then the install's flag: see s_fw_installing. */
        atomic_store(&s_cu_busy, true);
        /* A radio held for the clock's NTP answer goes off now, so the
         * offer made again below opens as soon as it is down. */
        if (s_wifi_ok) {
            wifi_access_end_linger();
        }
        if (!cu_may_start()) {
            /* The panel's radio is up or an install of the panel has
             * begun: the START is spent and the offer made again, so the
             * question opens once both have ended.  A declined one stays
             * declined: UPDATES says why its hold is grey. */
            ESP_LOGW(TAG, "IOmcu update refused: the radio is on or "
                          "the panel installs");
            atomic_store(&s_cu_busy, false);
            cu_publish_offer(s_cu_state == CU_OFFERED);
            return false;
        }
        ESP_LOGI(TAG, "IOmcu update: %lu bytes",
                 (unsigned long)CU_LEN);
        link_uph_update(&s_cu, CU_IMG, CU_LEN, now_ms());
        cu_enter(CU_UPDATING);
        s_cu_took_down = false;
        s_cu_missed = false;
        /* The START taken and the update running in one snapshot: a render
         * side that saw the one without the other would take it for spent. */
        cu_publish_progress(true);
        cu_publish_check(CU_CHECK_UPDATING);
        return true;
    case CU_UPDATING:
        break;
    case CU_IDLE:
    default:
        /* A START given while nothing is offered is spent. */
        if (atomic_load(&s_cu_starts) != s_cu_starts_taken) {
            s_cu_starts_taken = atomic_load(&s_cu_starts);
            cu_publish_offer(false);
        }
        return false;
    }

    if (!s_cu_took_down) {
        /* The poll's edge, once: s_link_quiet takes the link down at this
         * poll without a request. */
        if (*link_up) {
            s_link_quiet = true;
            (void)poll_far_end(link_up, bench, last_poll, last_status);
        }
        s_cu_took_down = true;
    }
    const uint32_t t0 = now_ms();
    do {
        if (link_uph_step(&s_cu, &s_host, &k_cu_port) == LINK_UPH_DONE) {
            cu_ended();
            return false;
        }
    } while ((uint32_t)(now_ms() - t0) < CU_SLICE_MS);
    cu_publish_progress(true);
    return true;
}


/* ------------------------------------------------- Wi-Fi and the update */

/* Whether a run's log, or a servo run's CSV or report, is open or about
 * to be: a servo run under way, an OPEN queued for the logger and not yet
 * answered, and the file from its open to its close, without a gap. */
static bool run_open_now(void)
{
    return atomic_load(&s_log_run_now) != 0u
           || atomic_load(&s_log_open_run) != 0u
           || atomic_load(&s_test_open_run) != 0u
           || atomic_load(&s_test_open_txt) != 0u
           || atomic_load(&s_servo_testing)
           || atomic_load(&s_test_opens_sent)
                  != atomic_load(&s_test_opens_done);
}

/* Whether flash may be written now, and if not why: see fw_gate(). */
static fw_gate_t fw_gate_now(bool armed)
{
    return fw_gate(armed, supply_live_or_coming(), run_open_now(),
                   s_artbusy || atomic_load(&s_keeping),
                   atomic_load(&s_cu_busy));
}

/* The update screen, drawn now: the install's loop runs no router tick. */
static void update_frame(update_state_t st, const char *version,
                         uint32_t written, uint32_t total)
{
    update_view_t v = { .state = st, .written = written, .total = total };
    snprintf(v.version, sizeof(v.version), "%s", version);
    update_screen_set(&v);
    gfx_canvas_t *c = panel_canvas();
    if (c != NULL) {
        ui_router_render(c, display_back_index());
        display_flip();
    }
}

/*
 * INSTALL: the 2s hold completed on the question.
 *
 * In order: the update screen, the radio off and waited for, the image
 * checked once more against the slot and the running build, the gate once
 * more, the control task stood still, then the write a piece at a time with
 * the bytes on screen, the check of what the slot holds, otadata, and the
 * restart.  Nothing touches the flash before the control task stands still
 * and the radio is off.  A failure before the first erase changes nothing;
 * one after it leaves the running build as the one that starts.
 */
static void fw_install_run(void)
{
    const uint8_t  *img = NULL;
    uint32_t        bytes = 0u;
    fw_image_info_t info;
    if (!wifi_access_take(&img, &bytes, &info)) {
        return;
    }
    /* Before the gate is read: see s_fw_installing. */
    atomic_store(&s_fw_installing, true);
    ui_router_goto(SCREEN_UPDATE);
    (void)wifi_screen_take();        /* the screen's leave: radio off */
    update_frame(UPDATE_WRITING, info.version_text, 0u, bytes);

    /* The radio goes first, and wholly: no request may be under way. */
    wifi_access_close(WA_OFF_INSTALL);
    bool down;
    const uint32_t t0 = now_ms();
    do {
        vTaskDelay(pdMS_TO_TICKS(20));
        down = wifi_access_down();
    } while (!down && (uint32_t)(now_ms() - t0) < 20000u);

    static fw_install_t in;
    fw_image_info_t running;
    (void)fw_ota_running(&running);
    bool armed;
    snap_lock();
    armed = s_snap.arm_wanted || s_snap.armed;
    snap_unlock();
    fw_err_t e = FW_ERR_BUSY;
    if (down && fw_gate_now(armed) == FW_GATE_OPEN) {
        e = fw_install_begin(&in, fw_ota_flash(), img, bytes,
                             fw_ota_slot_bytes(), &running);
    }
    fw_install_status_t st = FW_INSTALL_FAILED;
    if (e == FW_OK) {
        atomic_store(&s_fw_quiet_req, true);
        const uint32_t t1 = now_ms();
        while (!atomic_load(&s_fw_quiet_ack)
               && (uint32_t)(now_ms() - t1) < 1000u) {
            vTaskDelay(pdMS_TO_TICKS(CONTROL_PERIOD_MS));
        }
        const bool quiet = atomic_load(&s_fw_quiet_ack);
        ESP_LOGI(TAG, "writing %s, %lu bytes", info.version_text,
                 (unsigned long)bytes);
        do {
            st = fw_install_step(&in, quiet);
            update_frame(UPDATE_WRITING, info.version_text, in.done, bytes);
        } while (st == FW_INSTALL_RUNNING);
        if (st == FW_INSTALL_DONE) {
            update_frame(UPDATE_RESTARTING, info.version_text, bytes, bytes);
            fw_ota_note_restart(info.version_text);
            ESP_LOGI(TAG, "written and checked; restarting");
            vTaskDelay(pdMS_TO_TICKS(1500));
            esp_restart();
        }
    }
    ESP_LOGE(TAG, "install of %s failed: %s", info.version_text,
             fw_err_str((e != FW_OK) ? e : in.err));
    atomic_store(&s_fw_quiet_req, false);
    atomic_store(&s_fw_installing, false);
    wifi_access_release();
    update_frame(UPDATE_FAILED, info.version_text, in.done, bytes);
}

/*
 * The WI-FI screen without Wi-Fi: it says so and offers nothing, and
 * what it asks for is dropped.
 */
static void wifi_unavailable_service(void)
{
    (void)wifi_screen_take();
    if (ui_router_current() == SCREEN_WIFI) {
        wifi_screen_set_unavailable();
    }
}

/*
 * WI-FI, once a frame: the radio's gate, STOP, what the screen
 * asked for, and what it shows.
 *
 * The radio's gate is wa_gate(): SAFE, the supply off and no log run open.
 * STOP and ARM end the session and drop an image that waits; the supply
 * coming on or a run opening ends the session and leaves the image, which
 * the question then says cannot be installed yet.
 */
void wifi_service(bool armed, uint32_t stops_now)
{
    if (!s_wifi_ok) {
        wifi_unavailable_service();
        return;
    }
    static uint32_t seen_stops;
    static bool     started;
    if (!started) {
        seen_stops = stops_now;
        started = true;
    }
    const bool supply = supply_live_or_coming();
    const bool run = run_open_now();
    const wa_off_t gate = wa_gate(armed, supply, run);
    const bool stop = (stops_now != seen_stops);
    seen_stops = stops_now;
    const fw_gate_t fgate = fw_gate_now(armed);

    wifi_access_set_german(ui_text_language() == UI_LANG_DE);
    (void)wifi_access_tick(gate, stop,
                           fgate == FW_GATE_OPEN || fgate == FW_GATE_RUN);
    if (stop || gate == WA_OFF_ARMED) {
        wifi_access_discard();
    }

    const unsigned req = wifi_screen_take();
    if ((req & WIFI_REQ_OFF) != 0u) {
        wifi_access_close(WA_OFF_ASKED);
    }
    if ((req & WIFI_REQ_DISCARD) != 0u) {
        wifi_access_discard();
    }
    if ((req & WIFI_REQ_SAVE_NET) != 0u) {
        char ssid[WIFI_SSID_MAX + 1], pass[WIFI_PASS_MAX + 1];
        wifi_screen_net(ssid, pass);
        (void)wifi_access_save_net(ssid, pass);
        memset(pass, 0, sizeof(pass));
    }
    if ((req & WIFI_REQ_FORGET) != 0u) {
        wifi_access_forget();
    }
    if ((req & WIFI_REQ_CODE_ON) != 0u) {
        wifi_access_set_code(true);
    }
    if ((req & WIFI_REQ_CODE_OFF) != 0u) {
        wifi_access_set_code(false);
    }
    if ((req & (WIFI_REQ_AP | WIFI_REQ_JOIN)) != 0u) {
        /* The file API's rules keep the log folders in force; they change
         * only on SETUP, never while WI-FI is open. */
        wifi_access_set_log_dirs(log_viewer_tab_dir(LOG_TAB_USER),
                                 log_viewer_tab_dir(LOG_TAB_BENCH));
    }
    if ((req & WIFI_REQ_AP) != 0u) {
        (void)wifi_access_start(WA_MODE_AP, gate);
    }
    if ((req & WIFI_REQ_JOIN) != 0u) {
        (void)wifi_access_start(WA_MODE_JOIN, gate);
    }
    if ((req & WIFI_REQ_SCAN) != 0u) {
        (void)wifi_access_start(WA_MODE_SCAN, gate);
    }
    if ((req & WIFI_REQ_INSTALL) != 0u) {
        s_update_back = ui_router_parent(SCREEN_WIFI);
        fw_install_run();
    }
    if (update_screen_take_close()) {
        ui_router_goto(s_update_back);
    }

    if (ui_router_current() != SCREEN_WIFI) {
        return;
    }
    /* What the screen shows: the session's and the receiver's enums map
     * one to one onto the screen's, which test_wifi_screen holds. */
    /* 2kB of copies, in PSRAM: internal RAM is what the radio is short of. */
    static wifi_access_state_t *ap;
    static wifi_view_t         *vp;
    if (ap == NULL || vp == NULL) {
        /* A pair or neither: a half kept would be read as both next frame. */
        free(ap);
        free(vp);
        ap = heap_caps_malloc(sizeof(*ap), MALLOC_CAP_SPIRAM);
        vp = heap_caps_malloc(sizeof(*vp), MALLOC_CAP_SPIRAM);
        if (ap == NULL || vp == NULL) {
            free(ap);
            free(vp);
            ap = NULL;
            vp = NULL;
            return;
        }
    }
    wifi_access_state_t *const a = ap;
    wifi_view_t *const v = vp;
    wifi_access_get(a);
    memset(v, 0, sizeof(*v));
    v->block = (wifi_why_t)gate;
    v->have_net = a->conf.have_net;
    snprintf(v->net, sizeof(v->net), "%s", a->conf.ssid);
    v->code_on = a->conf.code_on;
    v->save_failed = a->save_failed;
    v->radio = (wifi_radio_t)a->w.state;
    v->off = (wifi_why_t)a->w.off;
    v->join = (wifi_join_t)a->w.join_err;
    snprintf(v->name, sizeof(v->name), "%s", a->name);
    snprintf(v->pass, sizeof(v->pass), "%s", a->w.pass);
    snprintf(v->code, sizeof(v->code), "%s", a->w.code);
    memcpy(v->ip, &a->w.ip, sizeof(v->ip));
    v->channel = a->channel;
    v->idle_left_s = a->idle_left_ms / 1000u;
    v->scanned = a->scanned;
    v->scan_n = (int)((a->scan.n < WIFI_SCAN_ROWS) ? a->scan.n : WIFI_SCAN_ROWS);
    for (int i = 0; i < v->scan_n; ++i) {
        snprintf(v->scan[i], sizeof(v->scan[i]), "%s", a->scan.net[i].ssid);
        v->rssi[i] = a->scan.net[i].rssi;
    }
    v->up = (wifi_up_t)a->up;
    v->received = a->received;
    v->total = a->total;
    v->offer = (wifi_offer_t)a->offer;
    v->gate = (wifi_gate_t)fgate;
    snprintf(v->new_ver, sizeof(v->new_ver), "%s", a->info.version_text);
    snprintf(v->run_ver, sizeof(v->run_ver), "%s", RCBENCH_VERSION_STRING);
    wifi_screen_set(v);
}

/* ------------------------------------------- GitHub: check and download */

/*
 * SETUP > UPDATES, the start-up check and the reminder (upd_screen.h), on
 * wifi_access's fetch jobs (wa_fetch.h).
 *
 * A check needs what the radio needs: SAFE, the supply off, no log run,
 * and neither a panel install nor the IOmcu's update.  A download
 * needs what an install needs (fw_gate_now()): it is one, from its hold
 * on, and says so in s_fw_installing before it reads the IOmcu's
 * flag.  Each frame a running job is ended for STOP, for WI-FI
 * opening, for CANCEL, and for its gate closing.
 */
static upd_driver_t s_upd;
static uint32_t     s_upd_boot_gen;   /* the start-up check's job; 0 none */
static bool         s_upd_dl;         /* a download, from its hold to its end */

static wa_why_t why_of_fw_gate(fw_gate_t g)
{
    switch (g) {
    case FW_GATE_ARMED:   return WA_WHY_ARMED;
    case FW_GATE_SUPPLY:  return WA_WHY_SUPPLY;
    case FW_GATE_RUN:     return WA_WHY_RUN;
    case FW_GATE_DRAWING: return WA_WHY_DRAWING;
    case FW_GATE_COPROC:  return WA_WHY_COPROC;
    case FW_GATE_OPEN:
    default:              return WA_WHY_NONE;
    }
}

static wa_why_t upd_check_gate(bool armed)
{
    return wa_fetch_gate(armed, supply_live_or_coming(), run_open_now(),
                         atomic_load(&s_cu_busy),
                         atomic_load(&s_fw_installing));
}

/* SETUP > UPDATES' RELEASES, now: 1 is TESTING. */
static wa_channel_t upd_channel(void)
{
    return (wa_channel_t)(settings_get_int(SET_UPD_CHANNEL) == 1);
}

/* @p unsafe: ARMED, an arm waiting, or the supply's output on or coming. */
static upd_feed_t upd_feed_now(bool armed, bool unsafe, const cu_snap_t *cs,
                               const wa_fetch_t *f, bool have_net)
{
    return (upd_feed_t){
        .now_ms     = now_ms(),
        .remind     = settings_get_bool(SET_UPD_REMIND),
        .available  = s_wifi_ok,
        .have_net   = have_net,
        .gate_open  = upd_check_gate(armed) == WA_WHY_NONE,
        .files_open = ui_router_current() == SCREEN_WIFI,
        .coproc     = cs->offered || cs->active || atomic_load(&s_cu_busy),
        .unsafe     = unsafe,
        .newer      = s_upd_boot_gen != 0u && f->gen == s_upd_boot_gen
                      && wa_fetch_offers(f, upd_channel()),
    };
}

/*
 * The reminder, before the IOmcu's question is served: it yields to
 * that question in the same frame the question may open.
 */
void upd_remind_service(bool armed, bool unsafe, const cu_snap_t *cs)
{
    if (!s_wifi_ok) {
        return;
    }
    wa_fetch_t f;
    wifi_access_fetch_get(&f);
    char ssid[WIFI_SSID_MAX + 1];
    const bool have_net = wifi_access_home(ssid, sizeof(ssid));
    const upd_feed_t feed = upd_feed_now(armed, unsafe, cs, &f, have_net);
    upd_remind_drive(&s_upd, &feed);
}

static void upd_ver_text(fw_version_t v, char *out, size_t n)
{
    char t[FW_REL_VER_TEXT];
    fw_release_version_text(v, t);
    snprintf(out, n, "%s", t);
}

/**
 * @brief Once a frame, after wifi_service(): the start-up check, what
 *        UPDATES and the reminder ask for, the job's aborts, the
 *        download's screen, and what UPDATES shows.  @p armed includes an
 *        arm waiting; panel.h names each parameter.
 */
void upd_service(bool armed, bool unsafe, uint32_t stops_now,
                 const cu_snap_t *cs)
{
    static uint32_t seen_stops;
    static bool     started;
    if (!started) {
        seen_stops = stops_now;
        started = true;
    }
    const bool stop = (stops_now != seen_stops);
    seen_stops = stops_now;

    static upd_view_t v;     /* off the stack: the loop's is the display's */
    memset(&v, 0, sizeof(v));
    snprintf(v.running, sizeof(v.running), "%s", RCBENCH_VERSION_STRING);
    v.line_capped = fw_release_line_capped();
    if (!s_wifi_ok) {
        (void)upd_screen_take();
        v.unavailable = true;
        upd_screen_set(&v);
        return;
    }

    v.have_net = wifi_access_home(v.net, sizeof(v.net));
    wa_fetch_t f;
    wifi_access_fetch_get(&f);
    const unsigned req = upd_screen_take();
    const fw_version_t none = { 0u, 0u, 0u };
    const wa_channel_t ch = upd_channel();

    /* The start-up check, once, at a moment the bench allows it. */
    const upd_feed_t feed = upd_feed_now(armed, unsafe, cs, &f, v.have_net);
    if (upd_startup_due(&s_upd, &feed)
        && wifi_access_fetch_start(WA_JOB_CHECK, none, ch,
                                   upd_check_gate(armed))
           == WA_GOT_NONE) {
        wifi_access_fetch_get(&f);
        s_upd_boot_gen = f.gen;
        ESP_LOGI(TAG, "GitHub: start-up check");
    }
    if ((req & UPD_REQ_CHECK) != 0u && !wa_fetch_busy(&f)) {
        (void)wifi_access_fetch_start(WA_JOB_CHECK, none, ch,
                                      upd_check_gate(armed));
        wifi_access_fetch_get(&f);
    }
    if ((req & UPD_REQ_DOWNLOAD) != 0u && !wa_fetch_busy(&f) && !s_upd_dl
        && wa_fetch_offers(&f, ch)) {
        /* Before the gate is read: see s_fw_installing. */
        atomic_store(&s_fw_installing, true);
        s_upd_dl = true;
        s_update_back = (ui_router_current() == SCREEN_UPDATES)
                            ? SCREEN_UPDATES : SCREEN_OVERVIEW;
        (void)wifi_access_fetch_start(WA_JOB_DOWNLOAD, f.found, ch,
                                      why_of_fw_gate(fw_gate_now(armed)));
        (void)update_screen_take_cancel();
        ui_router_goto(SCREEN_UPDATE);
        wifi_access_fetch_get(&f);
    }

    /* A job under way ends for any of these. */
    wa_why_t abort = WA_WHY_NONE;
    const bool cancel = (req & UPD_REQ_CANCEL) != 0u
                        || (s_upd_dl && update_screen_take_cancel());
    if (wa_fetch_busy(&f)) {
        if (stop) {
            abort = WA_WHY_STOP;
        } else if (ui_router_current() == SCREEN_WIFI) {
            abort = WA_WHY_FILES;
        } else if (cancel) {
            abort = WA_WHY_CANCEL;
        } else if (f.job == WA_JOB_DOWNLOAD) {
            abort = why_of_fw_gate(fw_gate_now(armed));
        } else {
            abort = upd_check_gate(armed);
        }
    }
    wifi_access_fetch_tick(abort);
    wifi_access_fetch_get(&f);

    /* What UPDATES and the download's screen show. */
    const bool busy = wa_fetch_busy(&f);
    v.block = (upd_why_t)upd_check_gate(armed);
    v.radio_free = wifi_access_down();
    if (s_upd_dl || busy) {
        v.dl_block = UPD_WHY_BUSY;
    } else {
        v.dl_block = (upd_why_t)why_of_fw_gate(fw_gate_now(armed));
        if (v.dl_block == UPD_WHY_NONE && atomic_load(&s_fw_installing)) {
            v.dl_block = UPD_WHY_INSTALL;
        }
    }
    /* A check on the other channel than RELEASES now shows as none. */
    if (!wa_fetch_stale(&f, ch)) {
        v.job = (upd_job_t)f.job;
        v.phase = (upd_phase_t)f.phase;
        v.got = (upd_got_t)f.got;
        v.why = (upd_why_t)f.why;
        v.join = (wifi_join_t)f.join;
        v.rel = (upd_rel_t)f.rel;
        v.status = f.status;
        v.budget_s = f.budget_ms / 1000u;
        if (wa_fetch_has_found(&f)) {
            upd_ver_text(f.found, v.found, sizeof(v.found));
        }
    }
    const uint32_t left_ms = wa_fetch_left(&f, now_ms());
    v.left_s = (left_ms + 999u) / 1000u;
    v.done = f.done;
    v.total = f.total;
    upd_screen_set(&v);

    if (!s_upd_dl) {
        return;
    }
    static update_view_t u;
    memset(&u, 0, sizeof(u));
    snprintf(u.version, sizeof(u.version), "%s", v.found);
    u.written = f.done;
    u.total = f.total;
    upd_result_text(&v, u.what, sizeof(u.what), u.why, sizeof(u.why));
    if (busy) {
        u.state = UPDATE_FETCHING;
        update_screen_set(&u);
        return;
    }
    s_upd_dl = false;
    if (f.got == WA_GOT_STAGED) {
        /* The installer's own flag from here: it sets it before it reads
         * the IOmcu's again. */
        atomic_store(&s_fw_installing, false);
        ESP_LOGI(TAG, "GitHub: %s downloaded and checked", v.found);
        fw_install_run();
        return;
    }
    atomic_store(&s_fw_installing, false);
    ESP_LOGW(TAG, "GitHub: the download of %s ended: %d", v.found,
             (int)f.got);
    u.state = UPDATE_FETCH_FAILED;
    update_screen_set(&u);
}

/*
 * The IOmcu update's screen, once a frame on the render side:
 * coproc_update_drive() moves the router and drains the screen's latches,
 * and its STARTs and decline go to the control task.
 * @p unsafe: armed, an arm waiting, or the supply's output on or coming.
 */
void cu_render_service(bool unsafe, bool link_up, const cu_snap_t *cs)
{
    static cu_driver_t driver;

    const cu_feed_t feed = {
        .offered      = cs->offered,
        .offer_gen    = cs->offer_gen,
        .active       = cs->active,
        .step         = (cu_step_t)cs->step,
        .block        = cs->block,
        .blocks       = cs->blocks,
        .starts_taken = cs->starts_taken,
        .running      = cs->running,
        .carried      = cu_carried(),
        .have_image   = CU_LEN != 0u,
        .reachable    = link_up || cs->mismatch,
        .check        = (cu_check_t)cs->check,
        .radio_up     = s_wifi_ok && !wifi_access_down(),
        .installing   = atomic_load(&s_fw_installing),
    };
    cu_hand_t hand;
    coproc_update_drive(&driver, &feed, unsafe, &hand);
    if (hand.starts > 0u) {
        atomic_fetch_add(&s_cu_starts, hand.starts);
    }
    if (hand.decline) {
        atomic_store(&s_cu_declined_gen, feed.offer_gen);
    }
}
