/**
 * @file
 * @brief KST's and HITEC's READY card and the footer's WRITE n VALUES.
 *
 * On KST and HITEC the READY card takes the output card's place
 * (servo_ready_int.h) with three rows on KST, four on HITEC, and the rail
 * line:
 *
 *   - PORT: what the tester drives.  Red with OUTPUTS, which no programmer
 *     works on; a red dot while the session holds the port.  A tap opens
 *     the PORT list, which a held port locks with its reason in the alert
 *     band.
 *   - SERIES, HITEC only: D SERIES or CLASSIC, the series the next CONNECT
 *     enters (SET_HITEC_SERIES).  A tap opens the two over the left column;
 *     while the page holds the port it shows the session's series and a
 *     tap names why it takes no pick.
 *   - the supply the session's CONNECT switches: PROG 5V on the rbmod
 *     carrier, PD MINI where one answers, SUPPLY otherwise.  Amber `ON AT
 *     CONNECT` while it is off, a red dot `ON` while it is on, and then a
 *     tap switches it off as OUTPUT OFF and PROG 5V OFF do (the ports' 5V
 *     not while a port holds it); KST with no supply the bench switches
 *     reads `OFF, ON BY HAND` in amber.  The carrier's input supply low
 *     (prog5v_box_input_low_mv()) reads `INPUT 5.1V < 15V` in red, and
 *     CONNECT is refused with it.
 *   - SERVO: the servo read, its model or type; amber while the port is
 *     the session's and nothing is read, grey before CONNECT.
 *
 * Nothing on the card switches an output on: the session over the left
 * card does, with its own CONNECT, LEAVE and holds.  A tap on a row that
 * is not met names it in the alert band.
 *
 * The footer's middle slot is the session's WRITE n VALUES: the same label
 * and the same tap as the session's own button, from any of its tabs.  Its
 * page asks for ENABLE WRITE first while the switch is off, and the page's
 * 2s hold confirms every write.  Grey while the session takes no write; a
 * tap names why in the alert band.  Neither screen has ARM: programming
 * and an armed bench are exclusive.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include "hitec_panel.h"
#include "hitec_prog.h"
#include "hitec_wire.h"
#include "kst_panel.h"
#include "prog5v_box.h"
#include "prog_registry.h"
#include "settings.h"
#include "supply_screen.h"
#include "ui_footer.h"
#include "ui_text.h"
#include "ui_widgets.h"

/* Last: its layout macros have short names (W, H, PAD), which no header
 * included after it may meet. */
#include "servo_screen_core_int.h"

/** @brief The footer's WRITE as a press holds it. */
typedef struct {
    bool    down;  /**< a contact is on it */
    uint8_t id;    /**< its track id       */
} write_press_t;

/** @brief What the footer and the rows keep between calls. */
typedef struct {
    /** @brief The contact on the footer's WRITE, none while not pressed. */
    write_press_t write;
    /** @brief A why-text built from numbers, which the row points at. */
    char          why[96];
} prog_ready_t;

/** @brief The one instance. */
static prog_ready_t s_pr;

bool prog_ready_shown(void)
{
    return s.fn == SERVO_FN_KST || s.fn == SERVO_FN_HITEC;
}

/**
 * @brief The session of the screen shown.
 * @param out receives it; all zero on a screen with none
 */
static void session_now(prog_session_t *out)
{
    if (s.fn == SERVO_FN_KST) {
        kst_panel_session(out);
    } else if (s.fn == SERVO_FN_HITEC) {
        hitec_panel_session(out);
    } else {
        memset(out, 0, sizeof(*out));
    }
}

/** @brief Whether the PD mini's output is on, as OUTPUT OFF reads it.
 *  @return true then */
static bool pd_on(void)
{
    return supply_screen_output_on();
}

/**
 * @brief The supply row: what CONNECT switches, and whether it is on.
 * @param ps the session
 * @param r  the row, its label set here
 */
static void supply_row(const prog_session_t *ps, rdy_row_t *r)
{
    switch ((prog_ses_supply_t)ps->supply) {
    case PROG_SES_5V:
        r->label = "PROG 5V";
        if (prog5v_box_on()) {
            r->st = (uint8_t)RDY_LIVE;
            /* On from a low input: the input names itself, the tap stays
             * the OFF. */
            if (prog5v_box_input_tag(r->value, sizeof(r->value))[0] == '\0') {
                snprintf(r->value, sizeof(r->value), "%s", ui_on_off(1));
            }
        } else if (prog5v_box_input_why() != NULL) {
            r->st = (uint8_t)RDY_BAD;
            ready_set_why(r, prog5v_box_input_why());
        } else {
            r->st = (uint8_t)RDY_SEQ;
            r->why = TR(PRR_5V_WHY);
            snprintf(r->value, sizeof(r->value), "%s", TR(PRR_AT_CONNECT));
        }
        return;
    case PROG_SES_PD_MINI:
        r->label = "PD MINI";
        if (pd_on()) {
            r->st = (uint8_t)RDY_LIVE;
            snprintf(r->value, sizeof(r->value), "%s", ui_on_off(1));
        } else {
            r->st = (uint8_t)RDY_SEQ;
            snprintf(s_pr.why, sizeof(s_pr.why), TR(PRR_PD_WHY),
                     (unsigned)(ps->mv / 1000u),
                     (unsigned)(ps->mv % 1000u / 100u), (unsigned)ps->ma);
            r->why = s_pr.why;
            snprintf(r->value, sizeof(r->value), "%s", TR(PRR_AT_CONNECT));
        }
        return;
    default:
        r->label = TR(RDY_SUPPLY);
        if (ps->by_hand && !ps->held) {
            r->st = (uint8_t)RDY_SEQ;
            r->why = TR(PRR_HAND_WHY);
            snprintf(r->value, sizeof(r->value), "%s", TR(PRR_BY_HAND));
        } else {
            r->st = (uint8_t)RDY_OK;
            snprintf(r->value, sizeof(r->value), "%s", TR(PRR_AS_IS));
        }
        return;
    }
}

/** @brief HITEC's rows in the card's order: SERIES under PORT. */
static const uint8_t k_hitec_rows[] = { PRR_PORT, PRR_SERIES, PRR_SUPPLY,
                                        PRR_SERVO };

int prog_ready_count(void)
{
    if (!prog_ready_shown()) {
        return 0;
    }
    return s.fn == SERVO_FN_HITEC ? (int)sizeof(k_hitec_rows) : PRR_SERIES;
}

int prog_ready_at(prog_ready_row_t kind)
{
    for (int i = 0; i < prog_ready_count(); ++i) {
        if ((s.fn == SERVO_FN_HITEC ? (int)k_hitec_rows[i] : i) == (int)kind) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief The kind of row @p i on the card shown.
 * @param i the row, inside prog_ready_count()
 * @return a prog_ready_row_t
 */
static prog_ready_row_t kind_at(int i)
{
    return s.fn == SERVO_FN_HITEC ? (prog_ready_row_t)k_hitec_rows[i]
                                  : (prog_ready_row_t)i;
}

/**
 * @brief HITEC's SERIES: the series the next CONNECT enters, or the
 *        session's while the page holds the port, when a tap names why.
 * @param ps the session
 * @param r  the row
 */
static void series_row(const prog_session_t *ps, rdy_row_t *r)
{
    r->label = TR(HP_SERIES);
    r->st = (uint8_t)RDY_OK;
    const bool held = ps->held || ps->busy;
    const int series = held ? (int)hitec_prog_series(hitec_panel_model())
                            : settings_get_int(SET_HITEC_SERIES);
    snprintf(r->value, sizeof(r->value), "%s",
             ui_setting_option(SET_HITEC_SERIES,
                               series == (int)HITEC_CLASSIC ? 0 : 1));
    r->fix = !held;
    r->why = held ? TR(PRR_SERIES_HELD) : NULL;
}

bool prog_ready_row(int i, rdy_row_t *r)
{
    if (r == NULL || i < 0 || i >= prog_ready_count()) {
        return false;
    }
    memset(r, 0, sizeof(*r));
    prog_session_t ps;
    session_now(&ps);
    switch (kind_at(i)) {
    case PRR_PORT:
        r->label = TR(SVH_PORT);
        r->fix = true;
        if (s.drive_target == 0u) {
            r->st = (uint8_t)RDY_BAD;
            ready_set_why(r, TR(KP_NEED_TARGET_2));      /* NO PROG PORT */
        } else {
            r->st = (uint8_t)(ps.held ? RDY_LIVE : RDY_OK);
            (void)ready_port_line(r->value, sizeof(r->value));
        }
        return true;
    case PRR_SERIES:
        series_row(&ps, r);
        return true;
    case PRR_SUPPLY:
        if (s.drive_target == 0u) {
            r->label = TR(RDY_SUPPLY);
            r->st = (uint8_t)RDY_IDLE;
            snprintf(r->value, sizeof(r->value), "--");
            return true;
        }
        supply_row(&ps, r);
        return true;
    default:
        r->label = TR(SVH_SERVO);
        if (ps.read) {
            r->st = (uint8_t)RDY_OK;
            snprintf(r->value, sizeof(r->value), "%s",
                     ps.servo[0] != '\0' ? ps.servo : TR(PRR_READ));
        } else if (ps.busy) {
            r->st = (uint8_t)RDY_SEQ;
            snprintf(r->value, sizeof(r->value), "%s", TR(PRR_BUSY));
        } else if (ps.held) {
            r->st = (uint8_t)RDY_SEQ;
            r->why = TR(PRR_READ_WHY);
            snprintf(r->value, sizeof(r->value), "%s", TR(PRR_NOT_READ));
        } else {
            r->st = (uint8_t)RDY_IDLE;
            /* A tap for a servo the operator powers, else a hold. */
            r->why = ps.supply == (uint8_t)PROG_SES_AS_IS
                         ? TR(PRR_CONN_WHY) : TR(PRR_CONN_HOLD_WHY);
            snprintf(r->value, sizeof(r->value), "%s", TR(PRR_NOT_CONN));
        }
        return true;
    }
}

void prog_ready_tap(int i)
{
    rdy_row_t r;
    if (!prog_ready_row(i, &r)) {
        return;
    }
    const prog_ready_row_t kind = kind_at(i);
    if (kind == PRR_PORT) {
        /* The list, or the lock's reason while the session holds it. */
        port_list_open();
        return;
    }
    if (kind == PRR_SERIES && r.fix) {
        /* D SERIES and CLASSIC over the left column (servo_pick.c). */
        svs_series_open();
        return;
    }
    if (kind == PRR_SUPPLY && r.st == (uint8_t)RDY_LIVE) {
        /* A red dot is an output on: a tap switches it off, as OUTPUT OFF
         * and PROG 5V OFF do; the ports' 5V not while a port holds it. */
        prog_session_t ps;
        session_now(&ps);
        if (ps.supply != (uint8_t)PROG_SES_5V) {
            supply_screen_ask_off();
        } else if (prog5v_box_live()) {
            prog5v_box_ask_off(PROG5V_BY_SERVO);
        } else {
            ui_router_set_alert(TR(P5_LOCKED));
        }
        ++s.power_rev;
        return;
    }
    if ((r.st == (uint8_t)RDY_OK || r.st == (uint8_t)RDY_LIVE)
        && r.why == NULL) {
        return;
    }
    char line[128];
    snprintf(line, sizeof(line), TR(RDY_BAND), r.label,
             r.why != NULL ? r.why : r.value);
    ui_router_set_alert(line);
}

bool prog_ready_stops_at(int x, int y)
{
    rdy_row_t r;
    const int i = prog_ready_at(PRR_SUPPLY);
    return prog_ready_row(i, &r) && r.st == (uint8_t)RDY_LIVE
           && gfx_rect_contains(ready_row_rect(i), x, y);
}

/* ---------------------------------------------------------- the footer */

/**
 * @brief Why the footer's WRITE takes no tap now.
 * @param ps the session
 * @return the reason; NULL while it takes one
 */
static const char *write_why(const prog_session_t *ps)
{
    const int editor = (s.fn == SERVO_FN_KST) ? kst_panel_editor()
                                              : hitec_panel_editor();
    if (editor != 0 || modal_open() || rows_page() >= 0) {
        return TR(PRR_W_PAGE);
    }
    if (ps->write_live) {
        return NULL;
    }
    if (ps->busy) {
        return TR(PRR_W_BUSY);
    }
    return ps->read ? TR(PRR_W_NONE) : TR(PRR_W_CONNECT);
}

void prog_draw_write(gfx_canvas_t *c)
{
    prog_session_t ps;
    session_now(&ps);
    const bool live = write_why(&ps) == NULL;
    ui_button_font(c, s.arm_btn, ps.write[0] != '\0' ? ps.write : "--",
                   ui_action_fill(live ? UI_ACTION_AVAILABLE
                                       : UI_ACTION_REFUSED),
                   s_pr.write.down, live, SV_FONT_LABEL);
}

uint32_t prog_write_sig(void)
{
    if (!prog_ready_shown()) {
        return 0u;
    }
    prog_session_t ps;
    session_now(&ps);
    uint32_t h = 2166136261u;
    for (const char *p = ps.write; *p != '\0'; ++p) {
        h = (h ^ (uint8_t)*p) * 16777619u;
    }
    h = (h ^ (write_why(&ps) == NULL ? 1u : 0u)
         ^ (s_pr.write.down ? 2u : 0u)) * 16777619u;
    return h;
}

bool prog_write_pressed(void) { return s_pr.write.down; }

bool prog_write_down(const touch_event_t *evt)
{
    if (evt == NULL || !prog_ready_shown()
        || !gfx_rect_contains(s.arm_btn, evt->point.x, evt->point.y)) {
        return false;
    }
    /* One contact owns the button, from its DOWN to its UP. */
    if (!s_pr.write.down) {
        s_pr.write.down = true;
        s_pr.write.id = evt->point.id;
        ++s.fp_rev;
    }
    return true;
}

bool prog_write_rest(const touch_event_t *evt)
{
    if (evt == NULL || !s_pr.write.down || evt->point.id != s_pr.write.id) {
        return false;
    }
    if (evt->type != TOUCH_EVENT_UP) {
        return true;
    }
    s_pr.write.down = false;
    ++s.fp_rev;
    if (!touch_event_is_tap_up(evt) || !prog_ready_shown()
        || !gfx_rect_contains(s.arm_btn, evt->point.x, evt->point.y)) {
        return true;
    }
    /* The write page is an overlay: none opens under another finger. */
    if (svs_press_held()) {
        ui_action_refused(TR(FOOT_PRESS_HELD));
        return true;
    }
    prog_session_t ps;
    session_now(&ps);
    const char *why = write_why(&ps);
    if (why != NULL) {
        ui_action_refused(why);
        return true;
    }
    const bool took = (s.fn == SERVO_FN_KST) ? kst_panel_write_tap()
                                             : hitec_panel_write_tap();
    if (!took) {
        ui_action_refused(TR(PRR_W_PAGE));
    }
    ++s.ctrl_rev;
    servo_invalidate();
    return true;
}

void prog_ready_reset(void)
{
    if (s_pr.write.down) {
        s_pr.write.down = false;
        ++s.fp_rev;
    }
}
