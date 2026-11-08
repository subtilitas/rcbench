/**
 * @file
 * @brief KST's and HITEC's READY card and the footer's WRITE n VALUES.
 *
 * On KST and HITEC the READY card takes the output card's place
 * (servo_ready_int.h) with three rows and the rail line:
 *
 *   - PORT: what the tester drives.  Red with OUTPUTS, which no programmer
 *     works on; a red dot while the session holds the port.  A tap opens
 *     the PORT list, which a held port locks with its reason in the alert
 *     band.
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
#include "kst_panel.h"
#include "prog5v_box.h"
#include "prog_registry.h"
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

/** @brief The contact on the footer's WRITE, none while not pressed. */
static write_press_t g_write;

/** @brief A why-text built from numbers, which the row points at. */
static char g_why[96];

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
            r->why = prog5v_box_input_why();
            (void)prog5v_box_input_tag(r->value, sizeof(r->value));
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
            snprintf(g_why, sizeof(g_why), TR(PRR_PD_WHY),
                     (unsigned)(ps->mv / 1000u),
                     (unsigned)(ps->mv % 1000u / 100u), (unsigned)ps->ma);
            r->why = g_why;
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

bool prog_ready_row(int i, rdy_row_t *r)
{
    if (r == NULL || i < 0 || i >= (int)PRR_COUNT || !prog_ready_shown()) {
        return false;
    }
    memset(r, 0, sizeof(*r));
    prog_session_t ps;
    session_now(&ps);
    switch ((prog_ready_row_t)i) {
    case PRR_PORT:
        r->label = TR(SVH_PORT);
        r->fix = true;
        if (s.drive_target == 0u) {
            r->st = (uint8_t)RDY_BAD;
            r->why = TR(KP_NEED_TARGET_2);
            snprintf(r->value, sizeof(r->value), "%s", TR(PRR_NO_PORT));
        } else {
            r->st = (uint8_t)(ps.held ? RDY_LIVE : RDY_OK);
            (void)svs_port_line(r->value, sizeof(r->value));
        }
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
    if (i == (int)PRR_PORT) {
        /* The list, or the lock's reason while the session holds it. */
        port_list_open();
        return;
    }
    if (i == (int)PRR_SUPPLY && r.st == (uint8_t)RDY_LIVE) {
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
    if (r.st == (uint8_t)RDY_OK || r.st == (uint8_t)RDY_LIVE) {
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
    return prog_ready_row((int)PRR_SUPPLY, &r) && r.st == (uint8_t)RDY_LIVE
           && gfx_rect_contains(ready_row_rect((int)PRR_SUPPLY), x, y);
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
                   g_write.down, live, SV_FONT_LABEL);
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
         ^ (g_write.down ? 2u : 0u)) * 16777619u;
    return h;
}

bool prog_write_pressed(void) { return g_write.down; }

bool prog_write_down(const touch_event_t *evt)
{
    if (evt == NULL || !prog_ready_shown()
        || !gfx_rect_contains(s.arm_btn, evt->point.x, evt->point.y)) {
        return false;
    }
    /* One contact owns the button, from its DOWN to its UP. */
    if (!g_write.down) {
        g_write.down = true;
        g_write.id = evt->point.id;
        ++s.fp_rev;
    }
    return true;
}

bool prog_write_rest(const touch_event_t *evt)
{
    if (evt == NULL || !g_write.down || evt->point.id != g_write.id) {
        return false;
    }
    if (evt->type != TOUCH_EVENT_UP) {
        return true;
    }
    g_write.down = false;
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
    if (g_write.down) {
        g_write.down = false;
        ++s.fp_rev;
    }
}
