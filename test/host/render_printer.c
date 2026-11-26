/**
 * @file render_printer.c
 * @brief The label printer's views for tools/render_ui.py
 *        (render_printer.h).
 *
 * Each view but printer-none has the T50M Pro bound at A4:93:40:30:FA:71.
 *
 * - printer-none: SETUP > WIRELESS on PRINTER, nothing bound.
 * - printer-idle: the 40x30mm roll read, 293 labels left, the link ended.
 * - printer-search: SEARCH from Bound printer while a scan runs, 6s
 *   from its end, two printers found, the first bound.
 * - printer-printing: a servo's label in SENDING, 3 of 5 frames sent.
 * - printer-done: the label printed, 292 labels left.
 * - printer-error: the label ended with COVER OPEN.
 *
 * SPDX-License-Identifier: MIT
 */
#include "render_printer.h"

#include <stdio.h>
#include <string.h>

#include "print_ctl.h"
#include "settings.h"
#include "settings_screen.h"
#include "ui_screen.h"
#include "wireless_pane.h"
#include "wireless_printer.h"

/** @brief The bound printer. @{ */
#define ADDR "A4:93:40:30:FA:71 P"
#define NAME "T0205C2608247368"
/** @} */

bool render_printer_view(const char *view)
{
    return strncmp(view, "printer-", 8) == 0;
}

void render_printer_pose(const char *view)
{
    if (!render_printer_view(view)) {
        return;
    }
    const bool none = strcmp(view, "printer-none") == 0;
    settings_set_text(SET_TEXT_PRN_ADDR, none ? "" : ADDR);
    settings_set_text(SET_TEXT_PRN_NAME, none ? "" : NAME);
    print_view_t v;
    memset(&v, 0, sizeof(v));
    v.left = -1;
    if (!none) {
        v.bound = true;
        snprintf(v.addr, sizeof(v.addr), "%s", ADDR);
        snprintf(v.name, sizeof(v.name), "%s", NAME);
        snprintf(v.printer, sizeof(v.printer), "%s", NAME);
        v.kind = PRINT_REQ_QUERY;
        v.phase = PRINT_DONE;
        v.w_mm = 40u;
        v.h_mm = 30u;
        v.gap_mm = 3u;
        v.left = 293;
        v.jobs = 1u;
    }
    if (strcmp(view, "printer-search") == 0) {
        v.scanning = true;
        v.scan_left_s = 6u;
        v.found_n = 2u;
        snprintf(v.found[0].addr, sizeof(v.found[0].addr), "%s", ADDR);
        snprintf(v.found[0].name, sizeof(v.found[0].name), "%s", NAME);
        v.found[0].rssi = -55;
        snprintf(v.found[1].addr, sizeof(v.found[1].addr),
                 "A4:93:40:12:0B:3C P");
        snprintf(v.found[1].name, sizeof(v.found[1].name),
                 "T0205C2611350912");
        v.found[1].rssi = -71;
    } else if (strcmp(view, "printer-printing") == 0
               || strcmp(view, "printer-done") == 0
               || strcmp(view, "printer-error") == 0) {
        v.kind = PRINT_REQ_LABEL;
        v.linked = true;
        v.jobs = 2u;
        snprintf(v.id, sizeof(v.id), "KST_X10-00005");
        v.phase = PRINT_SENDING;
        v.frame = 3u;
        v.frames = 5u;
        if (strcmp(view, "printer-done") == 0) {
            v.phase = PRINT_DONE;
            v.left = 292;
        } else if (strcmp(view, "printer-error") == 0) {
            v.phase = PRINT_FAILED;
            v.err = PRINT_ERR_COVER;
        }
    }
    v.rev = 1u;
    print_ctl_show(&v);
    settings_screen_open_on(SET_CAT_WIRELESS, WL_ENTRY_PRINTER);
    print_ctl_reset();
    print_ctl_show(&v);
    if (strcmp(view, "printer-search") == 0) {
        wireless_printer_bind_row.tap();
        print_ctl_reset();
        print_ctl_show(&v);
    }
}
