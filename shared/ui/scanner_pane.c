/**
 * @file
 * @brief SETUP > SENSORS > SCANNER's rows, TEST and the commands.  See
 *        scanner_pane.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "scanner_pane.h"

#include <stdio.h>
#include <string.h>

#include "gm861.h"
#include "scan_flow.h"
#include "scanner_code.h"
#include "servo_id.h"
#include "settings.h"
#include "settings_screen.h"
#include "ui_screen.h"
/* ui_text.h before ui_theme.h: ui_text.def ends by undefining UI_TEXT. */
#include "ui_text.h"
#include "ui_theme.h"

/** @brief Commands the queue holds. */
#define CMD_Q 4u

/** @brief The page's state. */
typedef struct {
    scanner_view_t  view;                         /**< the panel's view */
    uint32_t        now;                          /**< the frame's clock */
    char            last[SERVO_SCAN_LINE_MAX + 1u]; /**< the last line */
    uint32_t        last_at;                      /**< when it came */
    scanner_taken_t taken;                        /**< how it was taken */
    bool            testing;                      /**< TEST's window began */
    uint32_t        test_at;                      /**< when */
    uint32_t        shown_s;                      /**< a value in s shown */
    scanner_cmd_t   q[CMD_Q];                     /**< the commands */
    unsigned        q_n;                          /**< their count */
} scanner_page_t;

/** @brief The page, the frame task's. */
static scanner_page_t s;

/** @brief The state names, by gm861_state_t. */
static const ui_text_id_t k_states[] = {
    [GM861_OFF]           = TX_OFF,
    [GM861_NOT_CONNECTED] = TX_SCN_ST_NOT_CONNECTED,
    [GM861_NO_ANSWER]     = TX_SCN_ST_NO_ANSWER,
    [GM861_USB_OUTPUT]    = TX_SCN_ST_USB,
    [GM861_SETTING]       = TX_SCN_ST_SETTING,
    [GM861_READY]         = TX_SCN_ST_READY,
    [GM861_LOST]          = TX_SCN_ST_LOST,
};
_Static_assert(sizeof(k_states) / sizeof(k_states[0])
                   == (size_t)GM861_LOST + 1u,
               "a name for each scanner state");

/** @brief Taken as, by scanner_taken_t. */
static const ui_text_id_t k_taken[] = {
    [SCANNER_TAKEN_NONE]        = TX_SCN_V_NONE,
    [SCANNER_TAKEN_SERVO]       = TX_SCN_TAKEN_SERVO,
    [SCANNER_TAKEN_NOT_ID]      = TX_SCN_TAKEN_NOT_ID,
    [SCANNER_TAKEN_LONG]        = TX_SCN_TAKEN_LONG,
    [SCANNER_TAKEN_RUN]         = TX_SCN_TAKEN_RUN,
    [SCANNER_TAKEN_TEST_OK]     = TX_SCN_TAKEN_TEST_OK,
    [SCANNER_TAKEN_TEST_NOT_ID] = TX_SCN_TAKEN_TEST_NOT_ID,
    [SCANNER_TAKEN_TEST_LONG]   = TX_SCN_TAKEN_TEST_LONG,
    [SCANNER_TAKEN_TEST_RUN]    = TX_SCN_TAKEN_TEST_RUN,
};
_Static_assert(sizeof(k_taken) / sizeof(k_taken[0])
                   == (size_t)SCANNER_TAKEN_TEST_RUN + 1u,
               "a text for each way a line is taken");

const char *scanner_pane_state_name(uint8_t state)
{
    return (state < sizeof(k_states) / sizeof(k_states[0]))
               ? ui_tr(k_states[state]) : "";
}

/** @brief Seconds from @p at to the frame's clock, rounded down. */
static uint32_t age_s(uint32_t at)
{
    return (uint32_t)(s.now - at) / 1000u;
}

/** @brief TEST's seconds left, 0 when its window is shut. */
static uint32_t test_left_s(void)
{
    if (!scanner_pane_testing(s.now)) {
        return 0u;
    }
    return (SCANNER_TEST_MS - (uint32_t)(s.now - s.test_at) + 999u) / 1000u;
}

/** @brief Whether two views show the same. */
static bool same_view(const scanner_view_t *a, const scanner_view_t *b)
{
    return a->on == b->on && a->state == b->state && a->baud == b->baud
           && a->bad_zone == b->bad_zone && a->lines == b->lines
           && a->noise == b->noise && a->partial == b->partial
           && a->longs == b->longs && a->bad_crc == b->bad_crc;
}

void scanner_pane_set_view(const scanner_view_t *v, uint32_t now_ms)
{
    scanner_view_t next;
    if (v != NULL) {
        next = *v;
    } else {
        memset(&next, 0, sizeof(next));
    }
    s.now = now_ms;
    if (s.testing && !scanner_pane_testing(now_ms)) {
        s.testing = false;
        settings_screen_invalidate();
    }
    /* The ages and TEST's count move once a second. */
    const uint32_t shown = (s.last[0] != '\0' ? age_s(s.last_at) : 0u)
                           + test_left_s() * 1000u;
    if (!same_view(&next, &s.view) || shown != s.shown_s) {
        s.view = next;
        s.shown_s = shown;
        settings_screen_invalidate();
    }
}

const scanner_view_t *scanner_pane_view(void) { return &s.view; }

void scanner_pane_ask(scanner_cmd_t cmd)
{
    if (s.q_n < CMD_Q) {
        s.q[s.q_n++] = cmd;
    }
}

bool scanner_pane_take(scanner_cmd_t *out)
{
    if (out == NULL || s.q_n == 0u) {
        return false;
    }
    *out = s.q[0];
    --s.q_n;
    memmove(&s.q[0], &s.q[1], s.q_n * sizeof(s.q[0]));
    return true;
}

bool scanner_pane_testing(uint32_t now_ms)
{
    return s.testing && (uint32_t)(now_ms - s.test_at) <= SCANNER_TEST_MS;
}

/**
 * @brief Keep a line for Last scan.
 * @param line The line.  @param how How it was taken.  @param now The clock.
 */
static void keep(const char *line, scanner_taken_t how, uint32_t now)
{
    (void)snprintf(s.last, sizeof(s.last), "%s", line != NULL ? line : "");
    s.last_at = now;
    s.taken = how;
    settings_screen_invalidate();
}

void scanner_pane_test_line(const char *line, bool too_long, bool run_active,
                            uint32_t now_ms)
{
    servo_id_t p;
    scanner_taken_t how = SCANNER_TAKEN_TEST_OK;
    if (too_long) {
        how = SCANNER_TAKEN_TEST_LONG;
    } else if (line == NULL || !servo_id_parse(line, &p)
               || strcmp(p.text, line) != 0) {
        how = SCANNER_TAKEN_TEST_NOT_ID;
    } else if (run_active) {
        how = SCANNER_TAKEN_TEST_RUN;
    }
    keep(line, how, now_ms);
}

void scanner_pane_line(const char *line, servo_scan_t how, uint32_t now_ms)
{
    switch (how) {
    case SERVO_SCAN_SET:
    case SERVO_SCAN_SAME:
    case SERVO_SCAN_RUN_OWN:
        keep(line, SCANNER_TAKEN_SERVO, now_ms);
        break;
    case SERVO_SCAN_REFUSED_RUN:
        keep(line, SCANNER_TAKEN_RUN, now_ms);
        break;
    case SERVO_SCAN_MALFORMED:
        keep(line, SCANNER_TAKEN_NOT_ID, now_ms);
        break;
    case SERVO_SCAN_TOO_LONG:
        keep(line, SCANNER_TAKEN_LONG, now_ms);
        break;
    case SERVO_SCAN_REPEAT:
    default:
        break;                  /* the line shown already, dropped */
    }
}

const char *scanner_pane_last(uint32_t *at_ms)
{
    if (at_ms != NULL) {
        *at_ms = s.last_at;
    }
    return s.last;
}

scanner_taken_t scanner_pane_taken(void) { return s.taken; }

/** @brief Why a row that talks to the module takes no tap; NULL while it
 *         takes one. */
static const char *off_locked(void)
{
    return settings_get_bool(SET_SCANNER_ENABLE) ? NULL : TR(SCN_LOCK_OFF);
}

/** @brief State's value: the state and the rate, or the zone refused. */
static void state_value(char *out, size_t n)
{
    const char *name = scanner_pane_state_name(s.view.state);
    if (s.view.state == GM861_NO_ANSWER && s.view.bad_zone != GM861_NO_ZONE) {
        (void)snprintf(out, n, TR(SCN_V_ZONE), name,
                       (unsigned)s.view.bad_zone);
    } else if (s.view.baud != 0u && s.view.state >= GM861_USB_OUTPUT) {
        (void)snprintf(out, n, TR(SCN_V_BAUD), name, (unsigned)s.view.baud);
    } else {
        (void)snprintf(out, n, "%s", name);
    }
}

/** @brief Last scan's value: the line's first cells and its age. */
static void last_value(char *out, size_t n)
{
    if (s.last[0] == '\0') {
        (void)snprintf(out, n, "%s", TR(SCN_V_NONE));
        return;
    }
    (void)snprintf(out, n, TR(SCN_V_LAST), (int)SCANNER_LAST_CELLS, s.last,
                   (unsigned)age_s(s.last_at));
}

/** @brief Taken as's value. */
static void taken_value(char *out, size_t n)
{
    (void)snprintf(out, n, "%s", ui_tr(k_taken[s.taken]));
}

/** @brief Counts' value: lines, noise, partial, long, bad CRC. */
static void counts_value(char *out, size_t n)
{
    (void)snprintf(out, n, "%u %u %u %u %u", (unsigned)s.view.lines,
                   (unsigned)s.view.noise, (unsigned)s.view.partial,
                   (unsigned)s.view.longs, (unsigned)s.view.bad_crc);
}

/** @brief Active servo's value: the id, or NONE. */
static void active_value(char *out, size_t n)
{
    const char *id = servo_active_id();
    (void)snprintf(out, n, "%s", id != NULL ? id : TR(SCN_V_NO_SERVO));
}

/*
 * Active servo's choice list: NONE alone.  The SERVO LABEL rows' list of
 * ids (slice A4) takes its place here once it is on main.
 */
/** @brief The choices. @return 1. */
static int active_choices(void) { return 1; }
/** @brief Choice @p i's text. @param i The choice. @return It. */
static const char *active_choice(int i)
{
    (void)i;
    return TR(SCN_V_NO_SERVO);
}
/** @brief The choice in force: NONE while no servo is active. */
static int active_chosen(void) { return servo_active_id() == NULL ? 0 : -1; }

/** @brief NONE picked: the active servo cleared, refused during a run. */
static void active_pick(int i)
{
    (void)i;
    if (!servo_active_clear(scan_flow_run_active())) {
        ui_router_set_notice(TR(SCAN_CLEAR_RUN));
    }
    settings_screen_invalidate();
}

/** @brief Test's value: START, or the seconds its window has left. */
static void test_value(char *out, size_t n)
{
    const uint32_t left = test_left_s();
    if (left == 0u) {
        (void)snprintf(out, n, "%s", TR(SCN_V_TEST_GO));
    } else {
        (void)snprintf(out, n, TR(SCN_V_TEST_LEFT), (unsigned)left);
    }
}

/** @brief TEST tapped: its window opens, or opens again, now. */
static void test_tap(void)
{
    if (off_locked() != NULL) {
        return;
    }
    s.testing = true;
    s.test_at = s.now;
    settings_screen_invalidate();
}

/** @brief Check again tapped. */
static void recheck_tap(void)
{
    if (off_locked() == NULL) {
        scanner_pane_ask(SCANNER_CMD_RECHECK);
    }
}

/** @brief Show setup code and Factory reset: SETUP CODE opens. */
static void code_tap(void) { scanner_code_open(); }

/** @brief A row's ">" for one that opens a screen. */
static void opens_value(char *out, size_t n)
{
    (void)snprintf(out, n, ">");
}

/** @brief The line on typing: no value. */
static void no_value(char *out, size_t n)
{
    if (n > 0u) {
        out[0] = '\0';
    }
}

/** @brief Without a scanner: the servo id is typed. */
static const ui_setlist_custom_t k_note = {
    .label = TX_SCN_ROW_NOTE, .help = TX_SCN_ROW_NOTE_HELP,
    .value_text = no_value,
};
/** @brief State: the state and the rate. */
static const ui_setlist_custom_t k_state = {
    .label = TX_SCN_ROW_STATE, .help = TX_SCN_ROW_STATE_HELP,
    .value_text = state_value,
};
/** @brief Last scan: the line and its age. */
static const ui_setlist_custom_t k_last = {
    .label = TX_SCN_ROW_LAST, .help = TX_SCN_ROW_LAST_HELP,
    .value_text = last_value,
};
/** @brief Taken as: what the line did. */
static const ui_setlist_custom_t k_taken_row = {
    .label = TX_SCN_ROW_TAKEN, .help = TX_SCN_ROW_TAKEN_HELP,
    .value_text = taken_value,
};
/** @brief Counts: the framer's. */
static const ui_setlist_custom_t k_counts = {
    .label = TX_SCN_ROW_COUNTS, .help = TX_SCN_ROW_COUNTS_HELP,
    .value_text = counts_value,
};
/** @brief Active servo: the id; NONE clears it. */
static const ui_setlist_custom_t k_active = {
    .label = TX_SCN_ROW_ACTIVE, .help = TX_SCN_ROW_ACTIVE_HELP,
    .value_text = active_value, .choices = active_choices,
    .choice = active_choice, .chosen = active_chosen, .pick = active_pick,
};
/** @brief Test: the 30s window. */
static const ui_setlist_custom_t k_test = {
    .label = TX_SCN_ROW_TEST, .help = TX_SCN_ROW_TEST_HELP,
    .value_text = test_value, .tap = test_tap, .locked = off_locked,
};
/** @brief Check again: gm861_recheck(). */
static const ui_setlist_custom_t k_recheck = {
    .label = TX_SCN_ROW_RECHECK, .help = TX_SCN_ROW_RECHECK_HELP,
    .value_text = no_value, .tap = recheck_tap, .locked = off_locked,
};
/** @brief Show setup code: opens SETUP CODE. */
static const ui_setlist_custom_t k_code = {
    .label = TX_SCN_ROW_CODE, .help = TX_SCN_ROW_CODE_HELP,
    .value_text = opens_value, .tap = code_tap,
};
/** @brief Factory reset: opens SETUP CODE, whose hold sends it. */
static const ui_setlist_custom_t k_reset = {
    .label = TX_SCN_ROW_RESET, .help = TX_SCN_ROW_RESET_HELP,
    .value_text = opens_value, .tap = code_tap,
};

/** @brief The rows while ON, the line on typing first. */
static const ui_setlist_custom_t *const k_rows[] = {
    &k_note, &k_state, &k_last, &k_taken_row, &k_counts, &k_active,
    &k_test, &k_recheck, &k_code, &k_reset,
};
_Static_assert(sizeof(k_rows) / sizeof(k_rows[0]) == SCANNER_ROWS_MAX,
               "SCANNER_ROWS_MAX counts the rows");

int scanner_pane_rows(const ui_setlist_custom_t **out, int max)
{
    const int n = settings_get_bool(SET_SCANNER_ENABLE) ? SCANNER_ROWS_MAX
                                                         : 1;
    for (int i = 0; out != NULL && i < n && i < max; ++i) {
        out[i] = k_rows[i];
    }
    return n;
}

const char *scanner_pane_state(char *buf, size_t n, gfx_color_t *ink)
{
    if (buf == NULL || n == 0u || ink == NULL) {
        return "";
    }
    *ink = UI_TEXT_DIM;
    if (!settings_get_bool(SET_SCANNER_ENABLE)) {
        (void)snprintf(buf, n, "%s", TR(OFF));
        return buf;
    }
    (void)snprintf(buf, n, "%s", scanner_pane_state_name(s.view.state));
    if (s.view.state == GM861_READY) {
        *ink = UI_OK;
    }
    return buf;
}

void scanner_pane_reset(void)
{
    memset(&s, 0, sizeof(s));
    s.view.bad_zone = GM861_NO_ZONE;
}
