/**
 * @file
 * @brief The scan flow.  See scan_flow.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "scan_flow.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "servo_screen.h"
#include "settings.h"
#include "ui_text.h"

/** @brief Where the mailbox is: empty, being written, full, being read. */
enum {
    MAIL_EMPTY = 0,
    MAIL_WRITING,
    MAIL_FULL,
    MAIL_READING,
};

/** @brief The mailbox of one line, and the frame task's state. */
typedef struct {
    atomic_int        state;                          /**< MAIL_*            */
    char              line[SERVO_SCAN_LINE_MAX + 1u]; /**< the line waiting  */
    bool              too_long;                       /**< it was cut        */
    scan_flow_look_fn look;                           /**< the card's look-up */
    servo_scan_t      last;                           /**< the last result   */
} flow_t;

/** @brief The one instance. */
static flow_t s_flow = { .last = SERVO_SCAN_REPEAT };

void scan_flow_set_look(scan_flow_look_fn fn)
{
    s_flow.look = fn;
}

bool scan_flow_post(const char *line)
{
    int expect = MAIL_EMPTY;
    if (line == NULL
        || !atomic_compare_exchange_strong(&s_flow.state, &expect,
                                           MAIL_WRITING)) {
        return false;
    }
    s_flow.too_long = strlen(line) > SERVO_SCAN_LINE_MAX;
    (void)snprintf(s_flow.line, sizeof(s_flow.line), "%s", line);
    atomic_store(&s_flow.state, MAIL_FULL);
    return true;
}

void scan_flow_tick(uint32_t now_ms)
{
    int expect = MAIL_FULL;
    if (!atomic_compare_exchange_strong(&s_flow.state, &expect,
                                        MAIL_READING)) {
        return;
    }
    char line[SERVO_SCAN_LINE_MAX + 1u];
    memcpy(line, s_flow.line, sizeof(line));
    const bool too_long = s_flow.too_long;
    atomic_store(&s_flow.state, MAIL_EMPTY);
    (void)scan_flow_line(line, too_long, now_ms);
}

bool scan_flow_run_active(void)
{
    const ui_bench_status_t *st = ui_router_status();
    const bool servo_armed = st != NULL && st->armed
                             && (st->armed_roles & UI_ARMED_SERVO) != 0u;
    return servo_screen_testing() || servo_screen_limit_running()
           || servo_screen_rauto_running() || servo_armed;
}

/** @brief Whether the screen on top takes a scanned line. */
static bool screen_takes(void)
{
    switch (ui_router_current()) {
    case SCREEN_SPLASH:
    case SCREEN_UPDATE:
    case SCREEN_COPROC_UPDATE:
        return false;
    default:
        return true;
    }
}

/**
 * @brief The notice of a line that set the active servo, after the card's
 *        look-up; a known servo's name becomes DUT NAME.
 * @param id  The id.
 * @param out The notice.
 * @param n   Its size.
 */
static void set_notice(const char *id, char *out, size_t n)
{
    char name[SETTINGS_TEXT_MAX] = "";
    const scan_card_t card = (s_flow.look != NULL)
                                 ? s_flow.look(id, name, sizeof(name))
                                 : SCAN_CARD_UNKNOWN;
    name[sizeof(name) - 1u] = '\0';
    switch (card) {
    case SCAN_CARD_NONE:
        (void)snprintf(out, n, TR(SCAN_NO_CARD), id);
        break;
    case SCAN_CARD_NEW:
        (void)snprintf(out, n, TR(SCAN_NEW), id);
        break;
    case SCAN_CARD_KNOWN:
        if (name[0] != '\0') {
            settings_set_text(SET_TEXT_DUT_NAME, name);
            (void)snprintf(out, n, TR(SCAN_SET_NAME), id, name);
            break;
        }
        (void)snprintf(out, n, TR(SCAN_SET), id);
        break;
    default:
        (void)snprintf(out, n, TR(SCAN_SET), id);
        break;
    }
}

bool scan_flow_line(const char *line, bool too_long, uint32_t now_ms)
{
    if (!screen_takes()) {
        return false;
    }
    if (line == NULL) {
        line = "";
    }
    const servo_scan_t r = servo_scan_decide(line, too_long,
                                             scan_flow_run_active(), now_ms);
    s_flow.last = r;
    char notice[UI_ALERT_MAX];
    const char *id = servo_active_id();
    switch (r) {
    case SERVO_SCAN_SET:
        set_notice(id, notice, sizeof(notice));
        break;
    case SERVO_SCAN_SAME:
        (void)snprintf(notice, sizeof(notice), TR(SCAN_SAME), id);
        break;
    case SERVO_SCAN_REFUSED_RUN:
        if (id != NULL) {
            (void)snprintf(notice, sizeof(notice), TR(SCAN_RUN), id);
        } else {
            (void)snprintf(notice, sizeof(notice), "%s", TR(SCAN_RUN_NONE));
        }
        break;
    case SERVO_SCAN_MALFORMED:
        (void)snprintf(notice, sizeof(notice), TR(SCAN_MALFORMED), line);
        break;
    case SERVO_SCAN_TOO_LONG:
        (void)snprintf(notice, sizeof(notice), "%s", TR(SCAN_TOO_LONG));
        break;
    default:   /* a repeat, or the run's own servo: nobody needs telling */
        return true;
    }
    ui_router_set_notice(notice);
    return true;
}

servo_scan_t scan_flow_last(void)
{
    return s_flow.last;
}

void scan_flow_status(ui_bench_status_t *st)
{
    if (st == NULL) {
        return;
    }
    st->servo_id = servo_active_id();
    st->servo_from = (uint8_t)servo_active_from();
    st->servo_run = st->servo_id != NULL && scan_flow_run_active();
}

void scan_flow_reset(void)
{
    atomic_store(&s_flow.state, MAIL_EMPTY);
    s_flow.look = NULL;
    s_flow.last = SERVO_SCAN_REPEAT;
}
