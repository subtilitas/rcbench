/*
 * The OUTPUTS screen's PROG PORT entry: the left column under the list
 * when a programming port is picked.  See outputs_screen.h.
 *
 * Every port's entry has the same rows:
 *
 *   PIN        a button with the port's pin; a tap opens PICK A PIN on
 *              PROG PORT n, where a tap on a pin asks the IOmcu for it.
 *   FUNCTION   SERVO PWM | NONE, the port's FUNC (protocol 4.26), asked
 *              of the IOmcu on a tap (port_view_ask()).
 *   WATCHED BY the INA3221 channel that watches the port, from the sensor
 *              map (outputs_screen_set_port_watch()), or none.
 *   SERVO TESTER  OUTPUTS | PORT n: SET_SERVO_TARGET, 0 or n.
 *   PORT PWM   ON | OFF: SET_SERVO_PORT_PWM, the target port's pulse.
 *
 * and one amber line that says why a row takes no press, or how the last
 * ask for this port stands.  The pin and the function are the IOmcu's,
 * kept in its flash; the entry asks and never writes a page itself.  The
 * two tester settings are the panel's; the control task takes them to
 * the IOmcu on a bench at rest.
 *
 * SPDX-License-Identifier: MIT
 */

#include "outputs_screen.h"

#include <stdio.h>
#include <string.h>

#include "link_pages.h"
#include "picker_screen.h"
#include "port_view.h"
#include "servo_screen.h"
#include "settings.h"
#include "ui_screen.h"
#include "ui_tabs.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

#include "outputs_screen_int.h"

/*
 * The column under the dropdown, 334 px to the band: the PIN button, each
 * segment under its label, the WATCHED BY line, and two lines of reason
 * above the band.
 */
#define PIN_Y        (DD_Y + DD_H + 8)
#define PIN_H        40
#define FN_Y         (PIN_Y + PIN_H + 8)
#define WATCH_Y      (FN_Y + SEG_DY + SEG_H + 8)
#define TESTER_Y     (WATCH_Y + 24)
#define PWM_Y        (TESTER_Y + SEG_DY + SEG_H + 8)
#define WHY_Y        (PWM_Y + SEG_DY + SEG_H + 8)
#define SEG_DY       20     /* label to segment */
#define SEG_H        36
#define WATCH_LEN    24

static struct {
    int       port;         /* the entry shown, or -1 for a protocol      */
    ui_tabs_t fn;           /* FUNCTION: SERVO PWM | NONE                 */
    ui_tabs_t tester;       /* SERVO TESTER: OUTPUTS | PORT n             */
    ui_tabs_t pwm;          /* PORT PWM: ON | OFF                         */
    char      tester_port[16];   /* PORT n, the tester's second tab */
    bool      pin_down;     /* the PIN button is pressed                  */
    int       pin_id;
    uint32_t  sig;          /* what the column last showed                */
    uint32_t  view_rev;     /* port_view_revision() last drawn            */
    uint32_t  rev;
} p = { .port = -1 };

/* The label of the channel that watches each port, "" for none. */
static char g_watch[LINK_PG_PORTS_MAX][WATCH_LEN];

void outputs_port_touched(void)
{
    ++p.rev;
}

uint32_t outputs_port_revision(void) { return p.rev; }

void outputs_screen_set_port_watch(uint8_t port, const char *label)
{
    if (port >= LINK_PG_PORTS_MAX) {
        return;
    }
    char next[WATCH_LEN];
    snprintf(next, sizeof(next), "%s", (label != NULL) ? label : "");
    if (strcmp(next, g_watch[port]) != 0) {
        memcpy(g_watch[port], next, sizeof(next));
        outputs_port_touched();
    }
}

static gfx_rect_t seg_row(int label_y)
{
    return gfx_rect_make(COL_X + 3, label_y + SEG_DY, COL_W - 6, SEG_H);
}

static gfx_rect_t pin_rect(void)
{
    return gfx_rect_make(COL_X, PIN_Y, COL_W, PIN_H);
}

void outputs_port_reset(void)
{
    static const ui_text_id_t fn[] = { TX_OUT_PORT_FN_PWM,
                                       TX_OUT_PORT_FN_NONE };
    static const ui_text_id_t pwm[] = { TX_ON, TX_OFF };
    p.port = -1;
    p.pin_down = false;
    ui_tabs_init_text(&p.fn, fn, 2, seg_row(FN_Y));
    ui_tabs_init_text(&p.pwm, pwm, 2, seg_row(PWM_Y));
    static const ui_text_id_t tester[] = { TX_OUT_TESTER_OUTPUTS,
                                           TX_OUT_TESTER_OUTPUTS };
    ui_tabs_init_text(&p.tester, tester, 2, seg_row(TESTER_Y));
    /* The second tab is PORT n, formatted for the entry shown. */
    p.tester_port[0] = '\0';
    p.tester.text[1]  = -1;
    p.tester.label[1] = p.tester_port;
    outputs_port_touched();
}

void outputs_port_cancel(void)
{
    ui_tabs_cancel(&p.fn);
    ui_tabs_cancel(&p.tester);
    ui_tabs_cancel(&p.pwm);
    p.pin_down = false;
    outputs_port_touched();
}

void outputs_port_hide(void)
{
    p.port = -1;
    outputs_port_cancel();
}

/* A PROG PORT entry picked: the pins as the overview shows them, the
 * port's own lit, and no pin takes a tick.  OFF underneath, picked, so a
 * reading keeps it. */
void outputs_screen_select_port(uint8_t port)
{
    if (port >= outputs_screen_port_entries()) {
        return;
    }
    outputs_screen_select(0u);
    p.port = (int)port;
}

int outputs_screen_port_shown(void) { return p.port; }

void outputs_port_name(uint8_t port, char *buf, size_t cap)
{
    if (buf == NULL || cap == 0u) {
        return;
    }
    if (outputs_screen_port_entries() == 1u) {
        snprintf(buf, cap, "%s", TR(OUT_PROG_PORT));
    } else {
        snprintf(buf, cap, TR(OUT_PROG_PORT_N), (unsigned)port + 1u);
    }
}

/* ------------------------------------------------------------- the rules */

static int target(void) { return settings_get_int(SET_SERVO_TARGET); }

static bool known(void) { return port_view_has((uint8_t)p.port); }

static bool held(void)
{
    return settings_held(SET_SERVO_TARGET)
           || settings_held(SET_SERVO_PORT_PWM);
}

/* The port's function is the servo pulse, or not known yet: an IOmcu
 * older than 4.26 runs every port's pin as SERVO PWM, and the tester's
 * rows take their settings before the ports are read, as they did before
 * there was a function. */
static bool fn_pwm(void)
{
    return port_view_func((uint8_t)p.port) != (uint8_t)LINK_PG_FN_NONE;
}

/* FUNCTION: not before the ports are read, not on an IOmcu without FUNC,
 * not while the programmer holds a port or the bench is ARMED or a test
 * or sweep runs, nor while the tester drives this port. */
static bool fn_locked(void)
{
    return !known() || !port_view_get()->has_func || held()
           || servo_screen_tester_target_locked()
           || target() == p.port + 1;
}

/* SERVO TESTER: under the SERVO screen's rules, and PORT n only on a
 * SERVO PWM port. */
static bool tester_locked(void)
{
    return servo_screen_tester_target_locked()
           || (!fn_pwm() && target() != p.port + 1);
}

/* PORT PWM: the target port's, so only on that port's entry. */
static bool pwm_locked(void)
{
    return servo_screen_port_pwm_locked() || target() != p.port + 1;
}

/* The segments follow the view and the settings, whoever set them. */
static void sync_tabs(void)
{
    const uint8_t fn = port_view_func((uint8_t)p.port);
    p.fn.selected = (fn == (uint8_t)LINK_PG_FN_SERVO_PWM) ? 0
                    : (fn == (uint8_t)LINK_PG_FN_NONE)    ? 1 : -1;
    snprintf(p.tester_port, sizeof(p.tester_port), TR(OUT_TESTER_PORT),
             (unsigned)p.port + 1u);
    p.tester.selected = (target() == 0)            ? 0
                        : (target() == p.port + 1) ? 1 : -1;
    p.pwm.selected = settings_get_bool(SET_SERVO_PORT_PWM) ? 0 : 1;
}

bool outputs_screen_port_reason(char *buf, size_t cap)
{
    if (buf == NULL || cap == 0u) {
        return false;
    }
    buf[0] = '\0';
    if (p.port < 0) {
        return false;
    }
    if (held()) {
        snprintf(buf, cap, "%s", TR(OUT_PORT_HELD));
        return true;
    }
    if (port_view_ask_line((uint8_t)p.port, buf, cap)) {
        return true;
    }
    if (!known()) {
        snprintf(buf, cap, "%s", TR(OUT_PORT_NOT_READ));
    } else if (servo_screen_tester_target_locked()) {
        snprintf(buf, cap, "%s", TR(OUT_PORT_LOCKED));
    } else if (target() > 0
               && (target() != p.port + 1
                   || (fn_pwm() && port_view_get()->has_func))) {
        /* Another port, or this one when neither reason below holds. */
        snprintf(buf, cap, TR(OUT_PORT_TESTER_ON), (unsigned)target());
    } else if (!fn_pwm()) {
        snprintf(buf, cap, "%s", TR(OUT_PORT_TESTER_PWM));
    } else if (!port_view_get()->has_func) {
        snprintf(buf, cap, "%s", TR(OUT_PORT_FN_OLD));
    } else if (pwm_locked()) {
        snprintf(buf, cap, "%s", TR(OUT_PORT_PWM_NEEDS));
    }
    return buf[0] != '\0';
}

/* What the column shows that the settings, the view or the SERVO screen
 * can change from outside: a change repaints it. */
static uint32_t signature(void)
{
    if (p.port < 0) {
        return 0u;
    }
    return 1u | ((uint32_t)(target() & 7) << 1)
           | (settings_get_bool(SET_SERVO_PORT_PWM) ? 16u : 0u)
           | (servo_screen_tester_target_locked() ? 32u : 0u)
           | (servo_screen_port_pwm_locked() ? 64u : 0u)
           | (held() ? 128u : 0u);
}

void outputs_port_tick(void)
{
    const uint32_t sig = signature();
    const uint32_t rev = port_view_revision();
    if (sig != p.sig || rev != p.view_rev) {
        p.sig = sig;
        p.view_rev = rev;
        outputs_port_touched();
    }
}

/* ----------------------------------------------------------------- input */

/*
 * A press on a segment: taken whether or not the row is locked, so nothing
 * under it sees it, and acted on only while it is not.  A row that locks
 * under a press under way drops it.  The index tapped, or -1 for none;
 * @p took says whether the event was the row's.
 */
static int seg_event(ui_tabs_t *t, const touch_event_t *evt, bool locked,
                     bool *took)
{
    *took = false;
    if (evt->type == TOUCH_EVENT_DOWN) {
        bool on = false;
        for (int i = 0; i < t->count; ++i) {
            on = on || gfx_rect_contains(t->rect[i], evt->point.x,
                                         evt->point.y);
        }
        if (!on) {
            return -1;
        }
        if (!locked && t->pressed < 0) {
            (void)ui_tabs_event(t, evt);
        }
        *took = true;
        outputs_port_touched();
        return -1;
    }
    if (t->pressed < 0 || evt->point.id != t->press_id) {
        return -1;
    }
    *took = true;
    outputs_port_touched();
    if (locked) {
        ui_tabs_cancel(t);
        return -1;
    }
    const int was = t->selected;
    /* A tab already selected is tapped as well: none may be selected. */
    t->selected = -1;
    const bool tapped = ui_tabs_event(t, evt);
    const int i = t->selected;
    t->selected = was;
    return tapped ? i : -1;
}

/* The PIN button: a tap opens PICK A PIN on this port. */
static bool pin_event(const touch_event_t *evt)
{
    const gfx_rect_t r = pin_rect();
    if (evt->type == TOUCH_EVENT_DOWN) {
        if (!gfx_rect_contains(r, evt->point.x, evt->point.y)) {
            return false;
        }
        p.pin_down = true;
        p.pin_id = evt->point.id;
        outputs_port_touched();
        return true;
    }
    if (!p.pin_down || evt->point.id != p.pin_id) {
        return false;
    }
    if (evt->type == TOUCH_EVENT_UP) {
        p.pin_down = false;
        outputs_port_touched();
        if (touch_event_is_tap_up(evt)
            && gfx_rect_contains(r, evt->point.x, evt->point.y)) {
            picker_screen_open_on_port((uint8_t)p.port);
            ui_router_goto(SCREEN_PICKER);
        }
    }
    return true;
}

bool outputs_port_event(const touch_event_t *evt)
{
    if (evt == NULL || p.port < 0) {
        return false;
    }
    sync_tabs();
    if (pin_event(evt)) {
        return true;
    }
    bool took;
    int i = seg_event(&p.fn, evt, fn_locked(), &took);
    if (took) {
        if (i >= 0 && i != p.fn.selected) {
            (void)port_view_ask((uint8_t)p.port, PROG_LINK_ASK_FUNC,
                                (i == 0) ? (uint16_t)LINK_PG_FN_SERVO_PWM
                                         : (uint16_t)LINK_PG_FN_NONE);
        }
        return true;
    }
    i = seg_event(&p.tester, evt, tester_locked(), &took);
    if (took) {
        if (i >= 0 && i != p.tester.selected) {
            settings_set(SET_SERVO_TARGET,
                         (i == 0) ? 0.0f : (float)(p.port + 1));
            settings_request_save();
        }
        return true;
    }
    i = seg_event(&p.pwm, evt, pwm_locked(), &took);
    if (took) {
        if (i >= 0 && i != p.pwm.selected) {
            settings_set(SET_SERVO_PORT_PWM, (i == 0) ? 1.0f : 0.0f);
            settings_request_save();
        }
        return true;
    }
    return false;
}

/* --------------------------------------------------------------- drawing */

/* The PIN button's label: the pin, the pin refused, none, or not known. */
static void pin_label(char *buf, size_t cap)
{
    const port_view_t *v = port_view_get();
    const uint8_t port = (uint8_t)p.port;
    if (!known()) {
        snprintf(buf, cap, "%s", TR(OUT_PORT_NO_PIN));
    } else if (v->pin[port] == PORT_VIEW_NO_PIN) {
        snprintf(buf, cap, "%s", TR(OUT_PORT_PIN_SET));
    } else if ((v->refused & (1u << port)) != 0u) {
        snprintf(buf, cap, TR(OUT_PORT_PIN_REFUSED), (unsigned)v->pin[port]);
    } else {
        snprintf(buf, cap, TR(OUT_PORT_PIN), (unsigned)v->pin[port]);
    }
}

/*
 * The column: the PIN button, FUNCTION, WATCHED BY, SERVO TESTER and PORT
 * PWM, the segments drawn locked under their rules, and the reason under
 * them.  No last write: the entry writes no binding.
 */
void outputs_port_draw(gfx_canvas_t *c, gfx_rect_t dd, bool pressed,
                       bool open)
{
    if (c == NULL || p.port < 0) {
        return;
    }
    char buf[96];
    gfx_text(c, COL_X, COL_Y, TR(OUT_PROTOCOL), UI_FONT_LABEL, UI_TEXT_FAINT,
             1);
    outputs_port_name((uint8_t)p.port, buf, sizeof(buf));
    ui_choice_face(c, dd, buf, pressed, open);
    sync_tabs();

    pin_label(buf, sizeof(buf));
    ui_button(c, pin_rect(), buf, UI_PANEL_HI, p.pin_down, true);

    gfx_text(c, COL_X, FN_Y, TR(OUT_PORT_FUNCTION), UI_FONT_LABEL,
             UI_TEXT_FAINT, 1);
    ui_tabs_render_locked(&p.fn, c, fn_locked());

    if (g_watch[p.port][0] != '\0') {
        snprintf(buf, sizeof(buf), TR(OUT_PORT_WATCHED), g_watch[p.port]);
    } else {
        snprintf(buf, sizeof(buf), "%s", TR(OUT_PORT_WATCH_NONE));
    }
    gfx_text(c, COL_X, WATCH_Y, buf, UI_FONT_LABEL, UI_TEXT_DIM, 1);

    gfx_text(c, COL_X, TESTER_Y, TR(OUT_PORT_TESTER), UI_FONT_LABEL,
             UI_TEXT_FAINT, 1);
    ui_tabs_render_locked(&p.tester, c, tester_locked());
    gfx_text(c, COL_X, PWM_Y, TR(OUT_PORT_PWM), UI_FONT_LABEL,
             UI_TEXT_FAINT, 1);
    ui_tabs_render_locked(&p.pwm, c, pwm_locked());
    if (outputs_screen_port_reason(buf, sizeof(buf))) {
        (void)outputs_wrap(c, gfx_rect_make(COL_X, WHY_Y, COL_W, 16), 18,
                           buf, 2, UI_FONT_LABEL, UI_WARN, false);
    }
}
