/**
 * @file
 * @brief SETUP > WIRELESS' PRINTER entry and the print's frame-task side.
 *        See wireless_printer.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "wireless_printer.h"

#include <stdio.h>
#include <string.h>

#include "bleuart.h"
#include "servo_active.h"
#include "servo_screen.h"
#include "settings.h"
#include "settings_screen.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "wl_search.h"

_Static_assert(WLS_FOUND_MAX >= PRINT_FOUND_MAX, "SEARCH lists them all");
_Static_assert(WL_PRINTER_ROWS <= WL_ENTRY_ROWS_MAX,
               "the printer's rows fit an entry");

/** @brief What the tick saw last: the view's rev and jobs, the request
 *         acted on, the scans refused. */
static struct {
    bool     valid;    /**< a view was taken */
    uint32_t rev;      /**< its rev */
    uint16_t jobs;     /**< its jobs */
    uint16_t acted;    /**< the request whose end was acted on */
    uint16_t refused;  /**< its scan_refused */
} s_seen; /**< the one instance */

/** @brief The ids whose label printed this session, oldest first. */
static char     s_printed[WL_PRINTED_MAX][SERVO_ID_SIZE];
/** @brief The ids in s_printed. */
static unsigned s_printed_n;

/**
 * @brief Keep @p id as printed: moved to the newest place when held,
 *        else added, the oldest given up when the list is full.
 * @param id The id.
 */
static void printed_add(const char *id)
{
    unsigned i = 0u;
    while (i < s_printed_n && strcmp(s_printed[i], id) != 0) {
        ++i;
    }
    if (i == s_printed_n && s_printed_n < WL_PRINTED_MAX) {
        ++s_printed_n;
    } else if (i == s_printed_n) {
        i = 0u;
    }
    memmove(s_printed[i], s_printed[i + 1u],
            (s_printed_n - 1u - i) * sizeof(s_printed[0]));
    snprintf(s_printed[s_printed_n - 1u], sizeof(s_printed[0]), "%s", id);
}

bool wireless_printer_printed(const char *id)
{
    for (unsigned i = 0u; id != NULL && i < s_printed_n; ++i) {
        if (strcmp(s_printed[i], id) == 0) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Whether @p text is a printer's address: public, as the scan
 *        lists them.
 * @param text The address text.
 * @return true then.
 */
static bool printer_addr(const char *text)
{
    bleu_addr_t a;
    return bleu_addr_parse(text, &a) && a.type == BLEU_ADDR_PUBLIC;
}

bool wireless_printer_bound(void)
{
    return printer_addr(settings_text(SET_TEXT_PRN_ADDR));
}

/** @brief Whether the bench is ARMED or a SERVO run holds it. */
static bool unsafe(void)
{
    return ui_router_status()->armed || servo_screen_run_active();
}

void wireless_printer_name(char *out, size_t n)
{
    const char *name = settings_text(SET_TEXT_PRN_NAME);
    snprintf(out, n, "%s", wireless_printer_bound()
                               ? (name[0] != '\0'
                                      ? name
                                      : settings_text(SET_TEXT_PRN_ADDR))
                               : "");
}

const char *wireless_printer_job_word(const print_view_t *v, char *buf,
                                      size_t n)
{
    if (buf == NULL || n == 0u) {
        return "";
    }
    buf[0] = '\0';
    if (v == NULL || v->kind == PRINT_REQ_NONE) {
        return buf;
    }
    switch (v->phase) {
    case PRINT_CONNECTING:
        snprintf(buf, n, "%s", TR(PRN_PH_CONNECTING));
        break;
    case PRINT_READING:
        snprintf(buf, n, "%s", TR(PRN_PH_READING));
        break;
    case PRINT_BUILDING:
        snprintf(buf, n, "%s", TR(PRN_PH_BUILDING));
        break;
    case PRINT_SENDING:
        snprintf(buf, n, TR(PRN_PH_SENDING), (unsigned)v->frame,
                 (unsigned)v->frames);
        break;
    case PRINT_PRINTING:
        snprintf(buf, n, "%s", TR(PRN_PH_PRINTING));
        break;
    case PRINT_CHECKING:
        snprintf(buf, n, "%s", TR(PRN_PH_CHECKING));
        break;
    case PRINT_DONE:
        if (v->kind == PRINT_REQ_QUERY) {
            snprintf(buf, n, "%s", TR(PRN_PH_READ));
        } else if (v->left >= 0) {
            snprintf(buf, n, TR(PRN_PH_PRINTED_LEFT), (int)v->left);
        } else {
            snprintf(buf, n, "%s", TR(PRN_PH_PRINTED));
        }
        break;
    case PRINT_FAILED:
        snprintf(buf, n, "%s", ui_tr(print_err_text(v->err)));
        break;
    default:
        break;
    }
    return buf;
}

/**
 * @brief The printer's state in words: NOT BOUND, SCANNING, the request
 *        in work or its end, LINKED or IDLE.
 * @param buf The text.  @param n Its size.
 * @param ink Takes its colour; may be NULL.
 * @param idle_name An idle printer reads as its name, not IDLE.
 */
static void state_word(char *buf, size_t n, gfx_color_t *ink, bool idle_name)
{
    const print_view_t *v = print_ctl_view();
    gfx_color_t col = UI_TEXT_DIM;
    if (!wireless_printer_bound()) {
        snprintf(buf, n, "%s", TR(PRN_ST_NONE));
    } else if (v->scanning) {
        snprintf(buf, n, "%s", TR(IMU_ST_SCANNING));
    } else if (v->kind != PRINT_REQ_NONE && v->phase != PRINT_IDLE) {
        (void)wireless_printer_job_word(v, buf, n);
        col = v->phase == PRINT_FAILED ? UI_WARN
              : v->phase == PRINT_DONE ? UI_OK : UI_TEXT;
    } else if (v->linked) {
        snprintf(buf, n, "%s", TR(PRN_ST_LINKED));
        col = UI_OK;
    } else if (idle_name) {
        wireless_printer_name(buf, n);
    } else {
        snprintf(buf, n, "%s", TR(PRN_ST_IDLE));
    }
    if (ink != NULL) {
        *ink = col;
    }
}

/* ------------------------------------------------------------- SEARCH */

/** @brief A scan for SEARCH, refused with its text on the band while
 *         ARMED, while a job, a scan or an update runs. */
static void scan_tap(void)
{
    const print_view_t *v = print_ctl_view();
    if (unsafe()) {
        ui_router_set_alert(TR(PRN_SCAN_ARMED));
    } else if (print_view_busy(v)) {
        ui_router_set_alert(TR(PRN_SCAN_JOB));
    } else if (v->update) {
        ui_router_set_alert(TR(PRN_SCAN_UPD));
    } else if (v->scanning || !print_ctl_scan()) {
        ui_router_set_alert(TR(NODE_SCAN_RUNNING));
    }
}

/**
 * @brief Why the bind may not change now: a job or an update runs.
 * @return The band's text, NULL when it may.
 */
static const char *bind_refused(void)
{
    const print_view_t *v = print_ctl_view();
    if (print_view_busy(v)) {
        return TR(PRN_BIND_JOB);
    }
    if (v->update) {
        return TR(PRN_BIND_UPD);
    }
    return NULL;
}

/** @brief SEARCH's bind of found printer @p i: SETUP's address and name,
 *         a save asked.  @param i Its row. */
static void search_bind(uint8_t i)
{
    const char *why = bind_refused();
    const print_view_t *v = print_ctl_view();
    if (why != NULL) {
        ui_router_set_alert(why);
        return;
    }
    if (i >= v->found_n || !printer_addr(v->found[i].addr)) {
        ui_router_set_alert(TR(PRN_BIND_GONE));
        return;
    }
    settings_set_text(SET_TEXT_PRN_ADDR, v->found[i].addr);
    settings_set_text(SET_TEXT_PRN_NAME, v->found[i].name);
    settings_request_save();
    settings_screen_invalidate();
}

/** @brief SEARCH's UNBIND: SETUP's address and name cleared, a save
 *         asked. */
static void search_unbind(void)
{
    const char *why = bind_refused();
    if (why != NULL) {
        ui_router_set_alert(why);
        return;
    }
    settings_set_text(SET_TEXT_PRN_ADDR, "");
    settings_set_text(SET_TEXT_PRN_NAME, "");
    settings_request_save();
    settings_screen_invalidate();
}

/**
 * @brief SEARCH's view of the printer: the scan, the found printers, the
 *        bound one and the state.
 * @param out The view, zeroed.
 */
static void search_view(wls_view_t *out)
{
    const print_view_t *v = print_ctl_view();
    out->scanning = v->scanning;
    out->scan_left_s = v->scan_left_s;
    out->scan_done = v->scan_done;
    out->scan_cut = v->scan_cut;
    const unsigned n = v->found_n < PRINT_FOUND_MAX ? v->found_n
                                                    : PRINT_FOUND_MAX;
    out->found_n = (uint8_t)n;
    for (unsigned i = 0u; i < n; ++i) {
        wl_search_put(out, i, v->found[i].addr, v->found[i].name,
                      v->found[i].rssi);
    }
    if (wireless_printer_bound()) {
        snprintf(out->bound_addr, sizeof(out->bound_addr), "%s",
                 settings_text(SET_TEXT_PRN_ADDR));
        snprintf(out->bound_name, sizeof(out->bound_name), "%s",
                 settings_text(SET_TEXT_PRN_NAME));
    }
    state_word(out->word, sizeof(out->word), &out->word_ink, false);
}

/** @brief The printer's entry as SEARCH asks it. */
static const wls_device_t k_search = {
    .entry = WL_ENTRY_PRINTER,
    .kind = TX_PRN_ENTRY,
    .bind_scanning = TX_PRN_BIND_SCANNING,
    .bind_gone = TX_PRN_BIND_GONE,
    .view = search_view,
    .scan = scan_tap,
    .bind = search_bind,
    .unbind = search_unbind,
};

/* --------------------------------------------------------------- rows */

/** @brief Scan for printer's value: what the last scan did.
 *  @param out The text. @param n Its size. */
static void scan_value(char *out, size_t n)
{
    const print_view_t *v = print_ctl_view();
    if (v->scanning) {
        snprintf(out, n, "%s", TR(DIAL_SCAN_RUNS));
    } else if (!v->scan_done) {
        snprintf(out, n, "%s", TR(DIAL_SCAN_GO));
    } else if (v->scan_cut) {
        snprintf(out, n, "%s", TR(DIAL_SCAN_CUT));
    } else if (v->found_n == 0u) {
        snprintf(out, n, "%s", TR(DIAL_SCAN_NONE));
    } else {
        snprintf(out, n, TR(DIAL_SCAN_FOUND), (unsigned)v->found_n);
    }
}

/** @brief Scan for printer tapped: SEARCH opens and a scan is asked. */
static void scan_open(void)
{
    wl_search_open(&k_search, true);
}

/** @brief Bound printer tapped: SEARCH opens on the last scan's list. */
static void bind_open(void)
{
    wl_search_open(&k_search, false);
}

/** @brief Bound printer's value: its name, its address, or NONE.
 *  @param out The text. @param n Its size. */
static void bind_value(char *out, size_t n)
{
    wireless_printer_name(out, n);
    if (out[0] == '\0') {
        snprintf(out, n, "%s", TR(DIAL_BIND_NONE));
    }
}

/** @brief Address's value: the bound address, or --.
 *  @param out The text. @param n Its size. */
static void addr_value(char *out, size_t n)
{
    snprintf(out, n, "%s", wireless_printer_bound()
                               ? settings_text(SET_TEXT_PRN_ADDR) : "--");
}

/** @brief State's value. @param out The text. @param n Its size. */
static void state_value(char *out, size_t n)
{
    state_word(out, n, NULL, false);
}

/** @brief Roll's value: its size and gap, NO LABEL, or --.
 *  @param out The text. @param n Its size. */
static void roll_value(char *out, size_t n)
{
    const print_view_t *v = print_ctl_view();
    if (v->w_mm != 0u && v->h_mm != 0u) {
        snprintf(out, n, TR(PRN_ROLL), (unsigned)v->w_mm, (unsigned)v->h_mm,
                 (unsigned)v->gap_mm);
    } else if (v->phase == PRINT_FAILED && v->err == PRINT_ERR_NO_LABEL) {
        snprintf(out, n, "%s", TR(PRINT_ERR_NO_LABEL));
    } else {
        snprintf(out, n, "--");
    }
}

/** @brief Labels left's value: the printer's count, or --.
 *  @param out The text. @param n Its size. */
static void left_value(char *out, size_t n)
{
    const print_view_t *v = print_ctl_view();
    if (v->left >= 0) {
        snprintf(out, n, "%ld", (long)v->left);
    } else {
        snprintf(out, n, "--");
    }
}

/**
 * @brief Why Read printer takes no press: no printer bound, a job runs,
 *        an update runs.
 * @return The reason; NULL while it takes one.
 */
static const char *query_locked(void)
{
    const print_view_t *v = print_ctl_view();
    if (!wireless_printer_bound()) {
        return TR(PRN_LOCK_UNBOUND_HERE);
    }
    if (print_view_busy(v)) {
        return TR(PRN_LOCK_BUSY);
    }
    if (v->update) {
        return TR(PRN_LOCK_UPDATE);
    }
    return NULL;
}

/** @brief Why Test label takes no press: as Read printer, and ARMED or a
 *         run.  @return The reason; NULL while it takes one. */
static const char *test_locked(void)
{
    const char *why = query_locked();
    if (why == NULL && unsafe()) {
        why = TR(PRN_LOCK_SAFE);
    }
    return why;
}

/** @brief Read printer tapped: a QUERY job. */
static void query_tap(void)
{
    print_ctl_query();
    settings_screen_invalidate();
}

/** @brief Test label tapped: the test label. */
static void test_tap(void)
{
    (void)print_ctl_test();
    settings_screen_invalidate();
}

const ui_setlist_custom_t wireless_printer_scan_row = {
    .label = TX_PRN_ROW_SCAN, .help = TX_PRN_ROW_SCAN_HELP,
    .value_text = scan_value, .tap = scan_open,
};
const ui_setlist_custom_t wireless_printer_bind_row = {
    .label = TX_PRN_ROW_BIND, .help = TX_PRN_ROW_BIND_HELP,
    .value_text = bind_value, .tap = bind_open,
};
const ui_setlist_custom_t wireless_printer_addr_row = {
    .label = TX_PRN_ROW_ADDR, .help = TX_PRN_ROW_ADDR_HELP,
    .value_text = addr_value,
};
const ui_setlist_custom_t wireless_printer_state_row = {
    .label = TX_PRN_ROW_STATE, .help = TX_PRN_ROW_STATE_HELP,
    .value_text = state_value,
};
const ui_setlist_custom_t wireless_printer_roll_row = {
    .label = TX_PRN_ROW_ROLL, .help = TX_PRN_ROW_ROLL_HELP,
    .value_text = roll_value,
};
const ui_setlist_custom_t wireless_printer_left_row = {
    .label = TX_PRN_ROW_LEFT, .help = TX_PRN_ROW_LEFT_HELP,
    .value_text = left_value,
};
const ui_setlist_custom_t wireless_printer_query_row = {
    .label = TX_PRN_ROW_QUERY, .help = TX_PRN_ROW_QUERY_HELP,
    .value_text = wireless_printer_name, .tap = query_tap, .locked = query_locked,
};
const ui_setlist_custom_t wireless_printer_test_row = {
    .label = TX_PRN_ROW_TEST, .help = TX_PRN_ROW_TEST_HELP,
    .value_text = wireless_printer_name, .tap = test_tap, .locked = test_locked,
};

int wireless_printer_rows(wl_row_t *out, int max)
{
    static const ui_setlist_custom_t *const k_rows[WL_PRINTER_ROWS] = {
        &wireless_printer_scan_row,  &wireless_printer_bind_row,
        &wireless_printer_addr_row,  &wireless_printer_state_row,
        &wireless_printer_roll_row,  &wireless_printer_left_row,
        &wireless_printer_query_row, &wireless_printer_test_row,
    };
    for (int i = 0; i < WL_PRINTER_ROWS && out != NULL && i < max; ++i) {
        out[i].id = (setting_id_t)0;
        out[i].custom = k_rows[i];
    }
    return WL_PRINTER_ROWS;
}

const char *wireless_printer_state(char *buf, size_t n, gfx_color_t *ink)
{
    if (buf == NULL || n == 0u) {
        return "";
    }
    state_word(buf, n, ink, true);
    return buf;
}

void wireless_printer_opened(void)
{
    const print_view_t *v = print_ctl_view();
    if (wireless_printer_bound() && v->w_mm == 0u && !print_view_busy(v)
        && !v->scanning && !unsafe()) {
        print_ctl_query();
    }
}

/* --------------------------------------------------------------- tick */

/**
 * @brief A request that ended since the last tick: a label printed is
 *        kept in the session's list, and marks the active servo printed
 *        when it is that one's; an error goes to the band.
 * @param v The view.
 */
static void job_ended(const print_view_t *v)
{
    if (v->phase == PRINT_DONE && v->kind == PRINT_REQ_LABEL) {
        printed_add(v->id);
        const char *id = servo_active_id();
        if (id != NULL && strcmp(id, v->id) == 0) {
            servo_active_set_printed();
        }
    } else if (v->phase == PRINT_FAILED) {
        ui_router_set_alert(ui_tr(print_err_text(v->err)));
    }
}

void wireless_printer_tick(void)
{
    const print_view_t *v = print_ctl_view();
    if (s_seen.valid && v->rev == s_seen.rev && v->jobs == s_seen.jobs) {
        return;
    }
    const bool ended = v->phase == PRINT_DONE || v->phase == PRINT_FAILED;
    if (!s_seen.valid) {
        /* The start: an end already shown is not acted on again; a
         * request in work is, at its end. */
        s_seen.acted = ended ? v->jobs : (uint16_t)(v->jobs - 1u);
    } else {
        if (v->scan_refused != s_seen.refused) {
            ui_router_set_alert(v->update ? TR(PRN_SCAN_UPD)
                                          : TR(PRN_SCAN_OFF));
        }
        settings_screen_invalidate();
    }
    if (ended && v->jobs != s_seen.acted) {
        s_seen.acted = v->jobs;
        job_ended(v);
    }
    s_seen.valid = true;
    s_seen.rev = v->rev;
    s_seen.jobs = v->jobs;
    s_seen.refused = v->scan_refused;
}

void wireless_printer_reset(void)
{
    memset(&s_seen, 0, sizeof(s_seen));
    s_printed_n = 0u;
}
