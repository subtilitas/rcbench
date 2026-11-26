/**
 * @file
 * @brief SETUP > WIRELESS' PRINTER entry, the label printer's rows, and the
 *        frame task's side of a print that the screens share.
 *
 * | Row | Value | A press |
 * | --- | --- | --- |
 * | Scan for printer | SCAN, SCANNING, FOUND n, NONE FOUND, STOPPED BY ARM | SEARCH opens and scans 10s |
 * | Bound printer | its name, else its address, or NONE | SEARCH opens on the last scan's list |
 * | Address | the bound address, or -- | none |
 * | State | NOT BOUND, IDLE, LINKED, the job's phase or its last result | none |
 * | Roll | `40x30mm GAP 3mm`, NO LABEL, or -- | none |
 * | Labels left | the printer's count, or -- | none |
 * | Read printer | the bound printer's name | a QUERY job |
 * | Test label | the bound printer's name | the test label |
 *
 * The printer is bound by its public address and name in SETUP's text
 * settings `prn_ble` and `prn_name` (a random address binds nothing);
 * SEARCH's BIND writes them and asks a save,
 * its UNBIND (a 2s hold) clears them.  The printer advertises no service:
 * the scan lists the public addresses A4:93:40:xx:xx:xx (print_ctl.h).
 *
 * Refusals, each with its text on the band: a scan while ARMED, while a
 * job or a scan runs, while a device update runs, and when Bluetooth does
 * not come up within 10s; a bind or an unbind while a job or a device
 * update runs.  The update's start is refused while a job or a scan runs
 * (print_ctl_busy()).  Read printer and Test label are locked, the reason
 * on their help line, with no printer bound, while a job runs and while
 * an update runs; Test label also while ARMED.  The entry opened with a
 * printer bound and its roll unknown asks a QUERY.
 *
 * wireless_printer_tick() runs every frame on the frame task, whatever the
 * screen: a label of the active servo that printed marks it printed
 * (servo_active_set_printed()), a request that failed puts its error on
 * the band, a scan the controller refused says why, and SETUP repaints
 * when the view changes.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "gfx.h"
#include "print_ctl.h"
#include "wireless_pane.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The entry's rows, 8. */
#define WL_PRINTER_ROWS 8
/** @brief The ids the session keeps as printed, the oldest given up. */
#define WL_PRINTED_MAX 8u

/**
 * @brief The entry's rows, in the table's order.
 * @param out The rows, NULL to count.
 * @param max Their room.
 * @return The count, WL_PRINTER_ROWS.
 */
int wireless_printer_rows(wl_row_t *out, int max);

/**
 * @brief The entry's line on the left list: NOT BOUND, the job's phase,
 *        or the bound printer's name.
 * @param buf Takes the words.
 * @param n   Its size, bytes.
 * @param ink Takes their colour.
 * @return @p buf; "" with no buffer.
 */
const char *wireless_printer_state(char *buf, size_t n, gfx_color_t *ink);

/** @brief The entry was opened: a QUERY when a printer is bound, its roll
 *         unknown, no job runs and the bench is SAFE. */
void wireless_printer_opened(void);

/**
 * @brief A view's request in words: CONNECTING, READING ROLL, BUILDING
 *        LABEL, SENDING 3/5, PRINTING, CHECKING COUNT, PRINTED, 293 LEFT,
 *        ROLL READ, or the error's text.
 * @param v   The view.
 * @param buf Takes the words.
 * @param n   Its size, bytes.
 * @return @p buf; "" for no buffer, or for a view with no request.
 */
const char *wireless_printer_job_word(const print_view_t *v, char *buf,
                                      size_t n);

/**
 * @brief Whether a printer is bound: SETUP's address is one.
 * @return true then.
 */
bool wireless_printer_bound(void);

/**
 * @brief The bound printer's name, else its address; "" with none bound.
 * @param out Takes it.
 * @param n   Its size, bytes.
 */
void wireless_printer_name(char *out, size_t n);

/**
 * @brief Whether a label of @p id printed this session: one of the last
 *        WL_PRINTED_MAX ids whose label ended DONE, whichever servo was
 *        active then.
 * @param id The id; NULL is none.
 * @return true then.
 */
bool wireless_printer_printed(const char *id);

/** @brief Frame task, every frame: the printed mark, the band's notices
 *         and SETUP's repaint from the view's changes. */
void wireless_printer_tick(void);

/** @brief Forget what the tick saw and the printed ids (the tests). */
void wireless_printer_reset(void);

/** @brief Scan for printer; public for the tests. */
extern const ui_setlist_custom_t wireless_printer_scan_row;
/** @brief Bound printer; public for the tests. */
extern const ui_setlist_custom_t wireless_printer_bind_row;
/** @brief Address; public for the tests. */
extern const ui_setlist_custom_t wireless_printer_addr_row;
/** @brief State; public for the tests. */
extern const ui_setlist_custom_t wireless_printer_state_row;
/** @brief Roll; public for the tests. */
extern const ui_setlist_custom_t wireless_printer_roll_row;
/** @brief Labels left; public for the tests. */
extern const ui_setlist_custom_t wireless_printer_left_row;
/** @brief Read printer; public for the tests. */
extern const ui_setlist_custom_t wireless_printer_query_row;
/** @brief Test label; public for the tests. */
extern const ui_setlist_custom_t wireless_printer_test_row;

#ifdef __cplusplus
}
#endif
