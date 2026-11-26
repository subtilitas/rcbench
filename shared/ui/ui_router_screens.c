/**
 * @file
 * @brief The router's screens (ui_router_int.h): the table of each screen's
 *        ui_screen_t, the openers whose home tag returns to them, and the
 *        repaint of every screen's cached chrome.  ui_router.c holds the
 *        band, the alert band and the touches.
 *
 * SPDX-License-Identifier: MIT
 */

#include "ui_router_int.h"

#include <stddef.h>

#include "about_screen.h"
#include "analyser_screen.h"
#include "balance_screen.h"
#include "battery_screen.h"
#include "busfault_screen.h"
#include "coproc_update_screen.h"
#include "files_screen.h"
#include "imu_screen.h"
#include "layout_screen.h"
#include "log_viewer_screen.h"
#include "motor_screen.h"
#include "outputs_screen.h"
#include "overview_screen.h"
#include "picker_screen.h"
#include "sbk_screen.h"
#include "scanner_code.h"
#include "servo_screen.h"
#include "settings_screen.h"
#include "splash_screen.h"
#include "supply_screen.h"
#include "upd_screen.h"
#include "wifi_screen.h"
#include "wl_search.h"

/*
 * The openers, each on a row of its own: a control on one screen that opens
 * another, whose home tag then returns to the opener.  OUTPUTS opens from the
 * TARGET row of MOTOR & ESC, from SETUP's OUTPUTS tile and from the PORT
 * list's `OUTPUTS: NO PIN` entry on SERVO's LIVE, SWEEP, TEST, LIMIT RUN,
 * RUDDER and RUDDER AUTO; SUPPLY from the caps button of SERVO's TEST and
 * from the supply row of LIMIT RUN's and RUDDER AUTO's READY cards; SETUP
 * from the ANGLE SOURCE list's AS5600 entry on RUDDER, RUDDER AUTO, TEST
 * and LIMIT RUN, from the red METER row of LIMIT RUN's and RUDDER AUTO's
 * READY cards and from IMU's IMU SETTINGS; SETUP's SD
 * CARD list from the footer's SETTINGS on LOGS and FILES; WI-FI from
 * UPDATES' button and LOGS' WI-FI button; SEARCH from the scan and bound
 * rows of SETUP > WIRELESS; LOAD SETTINGS from SETUP > SD CARD's row of that
 * name; ABOUT from SETUP > PANEL's row About rcbench; LOGS from GRAPH on
 * the result cards of LIMIT RUN, RUDDER AUTO, TEST, MOTOR & ESC and SUPPLY
 * (graph_key.h).  SETUP's WI-FI tile needs no
 * row: WI-FI's parent is SETUP.
 * A screen with no row here, or entered from a screen not on its row, leaves
 * to its tile's parent (ui_router_parent()): a SERVO function screen to the
 * SERVO hub.
 */
static const struct {
    ui_screen_id_t screen;  /**< the screen that is opened */
    ui_screen_id_t opener;  /**< the screen that opens it */
} k_openers[] = {
    { SCREEN_OUTPUTS, SCREEN_MOTOR },
    { SCREEN_OUTPUTS, SCREEN_SETUP },
    { SCREEN_OUTPUTS, SCREEN_SERVO_LIVE },
    { SCREEN_OUTPUTS, SCREEN_SERVO_SWEEP },
    { SCREEN_OUTPUTS, SCREEN_SERVO_TEST },
    { SCREEN_OUTPUTS, SCREEN_SERVO_LIMRUN },
    { SCREEN_OUTPUTS, SCREEN_SERVO_RUDDER },
    { SCREEN_OUTPUTS, SCREEN_SERVO_RAUTO },
    { SCREEN_SUPPLY,  SCREEN_SERVO_TEST },
    { SCREEN_SUPPLY,  SCREEN_SERVO_LIMRUN },
    { SCREEN_SUPPLY,  SCREEN_SERVO_RAUTO },
    { SCREEN_SETUP,   SCREEN_SERVO_RUDDER },
    { SCREEN_SETUP,   SCREEN_SERVO_RAUTO },
    { SCREEN_SETUP,   SCREEN_SERVO_TEST },
    { SCREEN_SETUP,   SCREEN_SERVO_LIMRUN },
    { SCREEN_WIFI,    SCREEN_UPDATES },
    { SCREEN_WIFI,    SCREEN_LOGS },
    { SCREEN_SETUP,   SCREEN_IMU },
    { SCREEN_SETUP,   SCREEN_LOGS },
    { SCREEN_SETUP,   SCREEN_FILES },
    { SCREEN_WL_SEARCH, SCREEN_SETUP },
    { SCREEN_SET_LOAD, SCREEN_SETUP },
    { SCREEN_SCAN_CODE, SCREEN_SETUP },
    { SCREEN_ABOUT,   SCREEN_SETUP },
    { SCREEN_LOGS,    SCREEN_SERVO_LIMRUN },
    { SCREEN_LOGS,    SCREEN_SERVO_RAUTO },
    { SCREEN_LOGS,    SCREEN_SERVO_TEST },
    { SCREEN_LOGS,    SCREEN_MOTOR },
    { SCREEN_LOGS,    SCREEN_SUPPLY },
};  /**< which screen opens which: the home tag of the opened one returns to its opener */

bool ui_router_opens(ui_screen_id_t opener, ui_screen_id_t id)
{
    for (size_t i = 0; i < sizeof(k_openers) / sizeof(k_openers[0]); ++i) {
        if (k_openers[i].screen == id && k_openers[i].opener == opener) {
            return true;
        }
    }
    return false;
}

const ui_screen_t *ui_router_screen(ui_screen_id_t id)
{
    switch (id) {
    case SCREEN_SPLASH:   return splash_screen();
    case SCREEN_OVERVIEW: return overview_screen();
    case SCREEN_MOTOR:    return motor_screen();
    case SCREEN_SERVO:    return servo_screen();
    case SCREEN_ANALYSER: return analyser_screen();
    case SCREEN_BALANCE:  return balance_screen();
    case SCREEN_BATTERY:  return battery_screen();
    case SCREEN_LOGS:     return log_viewer_screen();
    case SCREEN_SETUP:    return settings_screen();
    case SCREEN_OUTPUTS:  return outputs_view_screen();
    case SCREEN_SUPPLY:   return supply_screen();
    case SCREEN_BUSFAULT: return busfault_screen();
    case SCREEN_WIFI:     return wifi_screen();
    case SCREEN_UPDATE:   return update_screen();
    case SCREEN_COPROC_UPDATE: return coproc_update_screen();
    case SCREEN_UPDATES:  return upd_screen();
    case SCREEN_UPD_REMIND: return upd_remind_screen();
    case SCREEN_FILES:    return files_screen();
    case SCREEN_LAYOUT:   return layout_screen();
    case SCREEN_IMU:      return imu_screen();
    case SCREEN_SERVO_LIVE:   return servo_fn_screen(SERVO_FN_LIVE);
    case SCREEN_SERVO_SWEEP:  return servo_fn_screen(SERVO_FN_SWEEP);
    case SCREEN_SERVO_TEST:   return servo_fn_screen(SERVO_FN_TEST);
    case SCREEN_SERVO_LIMRUN: return servo_fn_screen(SERVO_FN_LIMRUN);
    case SCREEN_SERVO_RUDDER: return servo_fn_screen(SERVO_FN_RUDDER);
    case SCREEN_SERVO_RAUTO:  return servo_fn_screen(SERVO_FN_RAUTO);
    case SCREEN_SERVO_KST:    return servo_fn_screen(SERVO_FN_KST);
    case SCREEN_SERVO_HITEC:  return servo_fn_screen(SERVO_FN_HITEC);
    case SCREEN_WL_SEARCH:    return wl_search_screen();
    case SCREEN_SET_LOAD:     return sbk_screen();
    case SCREEN_SCAN_CODE:    return scanner_code_screen();
    case SCREEN_ABOUT:        return about_screen();
    default:              return overview_screen();
    }
}

void ui_router_screens_invalidate(void)
{
    splash_invalidate();
    overview_invalidate();
    motor_invalidate();
    log_viewer_invalidate();
    settings_screen_invalidate();
    sbk_screen_invalidate();
    scanner_code_invalidate();
    about_screen_invalidate();
    analyser_invalidate();
    balance_invalidate();
    battery_invalidate();
    servo_invalidate();
    outputs_screen_invalidate();
    picker_screen_invalidate();
    busfault_screen_invalidate();
    coproc_update_screen_invalidate();
    supply_invalidate();
    wifi_screen_invalidate();
    update_screen_invalidate();
    upd_screen_invalidate();
    files_screen_invalidate();
    layout_screen_invalidate();
    imu_screen_invalidate();
    wl_search_invalidate();
}
