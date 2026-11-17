/**
 * @file panel_updrun.c
 * @brief The update run after a restart: UPDATES opens by itself once per
 *        start, and the run ends (shared/ui's upd_run.h decides both).
 *
 * The run note (fw_ota.h, fw_run.h) is written before the two restarts of
 * an update run: by the install once the image is written whole and
 * checked, and by the C6 radio's update on success.  Each start reads it
 * once in fw_ota_boot_check() and spends one of its 2 openings.  This
 * file asks upd_run_drive() once a frame, before the IOmcu's question is
 * served, and ends the run (fw_ota_run_end(): the note is erased) when it
 * says so, once the bench is SAFE with no arm waiting and the supply off:
 * the erase is a flash write.  UPDATES' steps read the run flag from
 * upd_service().
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdbool.h>

#include "esp_log.h"

#include "fw_ota.h"
#include "panel.h"
#include "panel_devupd.h"
#include "upd_run.h"
#include "wifi_access.h"

/** @brief Log tag, the panel's. */
static const char *TAG = "rcbench";

/** @brief Every view UPDATES' steps read has been fed once in this start,
 *         with the device rows published: set by updrun_fed() after the
 *         frame's feeders ran, so a frame's updrun_service() sees the views
 *         the previous frame fed, never the zeroed ones of the start. */
static bool s_fed_known;

/** @brief The run flag the last feed gave UPDATES' steps.  A trial start
 *         settles the run at the end of a frame, after the feed: until a
 *         feed carries the run as it stands, the steps judge it wrongly. */
static bool s_fed_run;

void updrun_fed(bool dev_known)
{
    s_fed_known = s_fed_known || dev_known;
    s_fed_run = fw_ota_run_active();
}

void updrun_service(bool unsafe)
{
    static upd_run_t drv;     /* zeroed at the start: once per start */
    const upd_run_feed_t f = {
        .run         = fw_ota_run_active(),
        .unsafe      = unsafe,
        .radio_up    = s_wifi_ok && !wifi_access_down(),
        .installing  = panel_fw_busy(),
        .known       = s_fed_known && s_fed_run == fw_ota_run_active(),
        .restart_due = panel_c6_busy(),
    };
    const bool opened = drv.opened;
    const upd_run_end_t end = upd_run_drive(&drv, &f);
    if (drv.opened && !opened) {
        ESP_LOGI(TAG, "update run: UPDATES opened");
    }
    /* The end erases the note in NVS, and a flash write stalls both
     * cores, the control task's heartbeat, STOP and supply trips among
     * them (flash_gate.h).  An end decided on a bench that is ARMED, has an
     * arm waiting or a supply output on or coming waits for it to be SAFE
     * with the outputs off, as a settings save does. */
    static bool end_due;
    if (end != UPD_RUN_GOES_ON && !end_due) {
        end_due = true;
        ESP_LOGI(TAG, "update run: ends, %s%s",
                 end == UPD_RUN_END_LEFT ? "UPDATES left by the home tag"
                                         : "no step left",
                 unsafe ? "; the note waits for SAFE" : "");
    }
    if (!end_due || unsafe) {
        return;
    }
    end_due = false;
    fw_ota_run_end();
}
