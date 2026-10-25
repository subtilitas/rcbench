/**
 * @file panel_trial.c
 * @brief A new build's first start, kept once the panel works as a whole:
 *        see panel_trial.h and shared/fwupdate/fw_trial.h.
 *
 * bring_up() settles the check at the display and touch step: without
 * either, a trial start goes back at once.  With both up the trial stays
 * open through the card, the settings, the control and logger tasks and
 * the first frame, and the screen loop keeps the build from
 * panel_trial_frame().  A panic or reset before that starts the build
 * before it.
 *
 * Built with -D RCBENCH_TRIAL_CRASH=ON, a trial start aborts at the moment
 * it would be kept, before the check and before any line that says it was
 * kept, for a check on the bench that a start which fails late goes back.
 * A start that is no trial runs as any other.
 *
 * SPDX-License-Identifier: MIT
 */
#include "panel_trial.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

#include "fw_ota.h"
#include "fw_trial.h"
#include "panel.h"
#include "panel_main.h"
#include "rcbench_version.h"
#include "ui_screen.h"
#include "ui_text.h"

/** @brief The console tag the panel's start-up lines carry. */
static const char *TAG = "rcbench";

atomic_bool s_trial_control_ran;
atomic_bool s_trial_logger_ran;

/** @brief This start's trial; app_main's task only: bring_up(),
 *         app_main and the screen loop. */
static fw_trial_t s_trial;

/**
 * @brief The band's line for what the start check found.  On the splash
 *        it waits in s_boot_alert for the hand-over; after it, it shows
 *        at once.
 * @param r     The check's result.
 * @param other The written build's version, for FW_BOOT_ROLLED_BACK.
 */
static void report(fw_boot_t r, const char *other)
{
    char line[ALERT_MAX];
    switch (r) {
    case FW_BOOT_UPDATED:
        snprintf(line, sizeof(line), TR(ALERT_FW_UPDATED),
                 RCBENCH_VERSION_STRING);
        break;
    case FW_BOOT_ROLLED_BACK:
        snprintf(line, sizeof(line), TR(ALERT_FW_ROLLED_BACK), other,
                 RCBENCH_VERSION_STRING);
        break;
    case FW_BOOT_UNCONFIRMED:
        snprintf(line, sizeof(line), TR(ALERT_FW_UNCONFIRMED),
                 RCBENCH_VERSION_STRING);
        break;
    case FW_BOOT_NORMAL:
    default:
        return;
    }
    if (ui_router_current() == SCREEN_SPLASH) {
        snprintf(s_boot_alert, sizeof(s_boot_alert), "%s", line);
    } else {
        ui_router_set_alert(line);
    }
}

/**
 * @brief Run the start check and report it.
 * @param ui_up Whether the display and touch came up.
 * @return What the check found: FW_BOOT_UPDATED for a trial start it kept.
 */
static fw_boot_t check(bool ui_up)
{
    char other[FW_TEXT_MAX];
    const fw_boot_t r = fw_ota_boot_check(ui_up, other, sizeof(other));
    report(r, other);
    return r;
}

void panel_trial_begin(bool ui_up)
{
    /* The bound counts from the start of the panel, tick 0. */
    fw_trial_init(&s_trial, 0u);
    if (!ui_up || !fw_ota_trial_pending()) {
        /* No touch on a trial start: no STOP, back to the build before,
         * and this does not return.  No trial: nothing to keep. */
        fw_trial_settle(&s_trial);
        (void)check(ui_up);
        return;
    }
    ESP_LOGI(TAG, "%s on trial: kept once every task runs and a frame is "
                  "on the display, or at %us after the start with the "
                  "screen loop running",
             RCBENCH_VERSION_STRING, (unsigned)(FW_TRIAL_BOUND_MS / 1000u));
}

void panel_trial_mark(uint32_t steps)
{
    fw_trial_mark(&s_trial, steps);
}

/**
 * @brief The steps not seen, by name, for the console.
 * @param missing FW_TRIAL_* bits.
 * @param out     Where the names go.
 * @param cap     Bytes at @p out.
 */
static void name_missing(uint32_t missing, char *out, size_t cap)
{
    static const struct {
        uint32_t    bit;
        const char *name;
    } k_names[] = {
        { FW_TRIAL_CARD, "card" },
        { FW_TRIAL_SETTINGS, "settings" },
        { FW_TRIAL_CONTROL, "control task" },
        { FW_TRIAL_LOGGER, "logger task" },
        { FW_TRIAL_FRAME, "frame" },
        { FW_TRIAL_TICK, "tick" },
    };
    size_t at = 0u;
    out[0] = '\0';
    for (size_t i = 0; i < sizeof(k_names) / sizeof(k_names[0]); ++i) {
        if ((missing & k_names[i].bit) != 0u && at < cap) {
            const int n = snprintf(out + at, cap - at, "%s%s",
                                   at > 0u ? ", " : "", k_names[i].name);
            at = (n > 0) ? at + (size_t)n : at;
        }
    }
}

void panel_trial_frame(bool flushed, bool quiet)
{
    /* The tick of this pass counts for the frame of a pass before it:
     * marked first, it is ignored until a frame has reached the display. */
    fw_trial_mark(&s_trial, FW_TRIAL_TICK);
    uint32_t steps = 0u;
    if (flushed) {
        steps |= FW_TRIAL_FRAME;
    }
    if (atomic_load(&s_trial_control_ran)) {
        steps |= FW_TRIAL_CONTROL;
    }
    if (atomic_load(&s_trial_logger_ran)) {
        steps |= FW_TRIAL_LOGGER;
    }
    fw_trial_mark(&s_trial, steps);

    const uint32_t missing = fw_trial_missing(&s_trial);
    const uint32_t at = now_ms();
    const fw_trial_verdict_t v = fw_trial_poll(&s_trial, at, quiet);
    if (v != FW_TRIAL_KEEP && v != FW_TRIAL_KEEP_BOUND) {
        return;
    }
#ifdef RCBENCH_TRIAL_CRASH
    ESP_LOGE(TAG, "RCBENCH_TRIAL_CRASH: abort at the moment this build "
                  "would be kept");
    abort();
#endif
    /* Each line follows the check, so it says what the check did. */
    const fw_boot_t r = check(true);
    if (r == FW_BOOT_UNCONFIRMED) {
        ESP_LOGW(TAG, "%s not kept: otadata not written; the next start "
                      "goes back to the build before",
                 RCBENCH_VERSION_STRING);
    } else if (r != FW_BOOT_UPDATED) {
        return;                 /* no trial start: nothing was kept */
    } else if (v == FW_TRIAL_KEEP) {
        ESP_LOGI(TAG, "%s kept: every task runs and a frame is on the "
                      "display, %ums after the start",
                 RCBENCH_VERSION_STRING, (unsigned)at);
    } else {
        char names[64];
        name_missing(missing, names, sizeof(names));
        ESP_LOGW(TAG, "%s kept at the %us bound with the screen loop "
                      "running; not seen: %s",
                 RCBENCH_VERSION_STRING,
                 (unsigned)(FW_TRIAL_BOUND_MS / 1000u), names);
    }
}
