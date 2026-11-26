/**
 * @file render_scanner.c
 * @brief The QR scanner's views for tools/render_ui.py (render_scanner.h).
 *
 * setup-scanner-ready: SETUP > SENSORS > SCANNER, the scanner ON and READY
 * at 9600 baud, 12 lines read, the last an id taken 3s ago and active.
 * setup-scanner-not-connected: the same entry with nothing on header P3:
 * NOT CONNECTED, no line yet.  setup-scanner-no-answer: a module that
 * idles high and answers at no rate.  setup-scanner-setup-code: SETUP CODE
 * over it in NO ANSWER, the code and the factory reset's hold.
 *
 * SPDX-License-Identifier: MIT
 */
#include "render_scanner.h"

#include <string.h>

#include "gm861.h"
#include "scan_flow.h"
#include "scanner_code.h"
#include "scanner_pane.h"
#include "sensors_pane.h"
#include "settings.h"
#include "settings_screen.h"
#include "ui_screen.h"

/** @brief The views' clock, ms. */
#define T_MS 600000u

bool render_scanner_view(const char *view)
{
    return strncmp(view, "setup-scanner-", 14) == 0;
}

void render_scanner_pose(const char *view)
{
    if (!render_scanner_view(view)) {
        return;
    }
    settings_set(SET_SCANNER_ENABLE, 1.0f);
    scanner_pane_reset();
    scanner_view_t v = { .on = true, .state = GM861_NOT_CONNECTED,
                         .bad_zone = GM861_NO_ZONE };
    if (strcmp(view, "setup-scanner-ready") == 0) {
        v.state = GM861_READY;
        v.baud = 9600u;
        v.lines = 12u;
        scanner_pane_set_view(&v, T_MS - 3000u);
        (void)scan_flow_line("KST_X10-0000A", false, T_MS - 3000u);
        ui_router_set_alert(NULL);
    } else if (strcmp(view, "setup-scanner-no-answer") == 0
               || strcmp(view, "setup-scanner-setup-code") == 0) {
        v.state = GM861_NO_ANSWER;
        v.baud = 1200u;
    }
    scanner_pane_set_view(&v, T_MS);
    settings_screen_open_on(SET_CAT_SENSORS, SENS_ENTRY_SCANNER);
    if (strcmp(view, "setup-scanner-setup-code") == 0) {
        scanner_code_open();
    }
}
