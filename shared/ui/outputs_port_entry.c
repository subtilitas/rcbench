/**
 * @file outputs_port_entry.c
 * @brief The OUTPUTS screen's PROG PORT entry: the left column under the
 *        list when a programming port is picked.  See outputs_screen.h.
 *
 * Every port's entry has the same rows:
 *
 *   PIN        a button with the port's pin; a tap opens PICK A PIN on
 *              PROG PORT n, where a tap on a pin asks the IOmcu for it.
 *   FUNCTION   a choice face with the port's FUNC (protocol 4.26); a tap
 *              opens the list over the rows under it: SERVO PWM, NONE,
 *              and from 4.28 DSHOT150, DSHOT300, DSHOT600.  A tap on an
 *              entry asks the IOmcu for it (port_view_ask()).
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

/**
 * @brief The column under the dropdown, from body y 112 to OU_FOOT: the
 *        PIN button, each segment row under its label, the WATCHED BY
 *        line, and up to three lines of reason.
 *
 * The PIN button and the segment rows are 48 px (UI_TAP_MIN) tall; a
 * label is 10 x 20 with its segment row 22 px under its top.  The
 * FUNCTION list opens under its face: 5 rows of 48 px with its 4 px
 * margins end at body y 488, its shadow at OU_FOOT.
 */
#define PIN_Y        (DD_Y + DD_H + 4) /**< The PIN button's top. */
#define PIN_H        UI_TAP_MIN /**< Its height, px. */
#define FN_Y         (PIN_Y + PIN_H + 6) /**< FUNCTION's label. */
#define WATCH_Y      (FN_Y + SEG_DY + SEG_H + 8) /**< The WATCHED BY line. */
#define TESTER_Y     (WATCH_Y + 26) /**< SERVO TESTER's label. */
#define PWM_Y        (TESTER_Y + SEG_DY + SEG_H + 8) /**< PORT PWM's label. */
/** @brief The reason's first line. */
#define WHY_Y        (PWM_Y + SEG_DY + SEG_H + 8)
#define SEG_DY       22     /**< label to segment */
#define SEG_H        UI_TAP_MIN /**< A segment row's height, px. */
#define WATCH_LEN    24       /**< A watch label's bytes. */
/** @brief The FUNCTION list's rows under its face. */
#define FN_ROW_H     UI_TAP_MIN
#define FN_ROWS_MAX  5        /**< The functions the list can hold. */

_Static_assert(FN_Y + SEG_DY + SEG_H + 4 + FN_ROWS_MAX * FN_ROW_H + 8 + 4
                   <= OU_FOOT,
               "the FUNCTION list and its shadow end above the alert band");
_Static_assert(WHY_Y + 2 * OU_LINE + 20 <= OU_FOOT,
               "the reason's third line ends above the alert band");

static struct {
    int       port;         /**< the entry shown, or -1 for a protocol    */
    bool      fn_open;      /**< the FUNCTION list is open                */
    int       fn_hit;       /**< pressed: -1 none, 0 the face, 1 + row    */
    int       fn_id;        /**< the touch id of that press               */
    ui_tabs_t tester;       /**< SERVO TESTER: OUTPUTS | PORT n           */
    ui_tabs_t pwm;          /**< PORT PWM: ON | OFF                       */
    char      tester_port[16];   /**< PORT n, the tester's second tab */
    bool      pin_down;     /**< the PIN button is pressed                */
    int       pin_id;       /**< the touch id of that press               */
    uint32_t  sig;          /**< what the column last showed              */
    uint32_t  view_rev;     /**< port_view_revision() last drawn          */
    uint32_t  rev;          /**< bumped by every change it draws          */
} p = { .port = -1 };  /**< The entry's state. */

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

static gfx_rect_t fn_face(void) { return seg_row(FN_Y); }

/* The functions the list offers: SERVO PWM and NONE, and the DShot rates
 * from an IOmcu of 4.28. */
static int fn_rows(void)
{
    return port_view_get()->has_dshot ? (int)LINK_PG_FN_COUNT
                                      : (int)LINK_PG_FN_COUNT_V426;
}

static gfx_rect_t fn_list(void)
{
    const gfx_rect_t f = fn_face();
    return gfx_rect_make(COL_X, f.y + f.h + 4, COL_W,
                         fn_rows() * FN_ROW_H + 8);
}

static gfx_rect_t fn_row(int i)
{
    const gfx_rect_t l = fn_list();
    return gfx_rect_make(l.x + 4, l.y + 4 + i * FN_ROW_H, l.w - 8, FN_ROW_H);
}

_Static_assert(LINK_PG_FN_COUNT <= FN_ROWS_MAX,
               "the FUNCTION list holds every function");

static void fn_close(void)
{
    p.fn_open = false;
    p.fn_hit  = -1;
}

void outputs_port_reset(void)
{
    static const ui_text_id_t pwm[] = { TX_ON, TX_OFF };
    p.port = -1;
    p.pin_down = false;
    fn_close();
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
    fn_close();
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
    return port_view_pwm((uint8_t)p.port);
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
           | (held() ? 128u : 0u)
           | (fn_locked() ? 256u : 0u);
}

void outputs_port_tick(void)
{
    const uint32_t sig = signature();
    const uint32_t rev = port_view_revision();
    if (sig != p.sig || rev != p.view_rev) {
        p.sig = sig;
        p.view_rev = rev;
        /* A list opened on a row that has locked since shuts. */
        if (p.fn_open && fn_locked()) {
            fn_close();
        }
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

/*
 * FUNCTION: a tap on the face opens the list, a tap on an entry asks for
 * that function and shuts it, and a press anywhere else while it is open
 * shuts it and is taken, so nothing under the list sees it.
 */
static bool fn_event(const touch_event_t *evt)
{
    const int16_t x = evt->point.x, y = evt->point.y;
    if (evt->type == TOUCH_EVENT_DOWN) {
        if (p.fn_open) {
            p.fn_hit = -1;
            for (int i = 0; i < fn_rows(); ++i) {
                if (gfx_rect_contains(fn_row(i), x, y)) {
                    p.fn_hit = 1 + i;
                }
            }
            if (p.fn_hit < 0 && !gfx_rect_contains(fn_face(), x, y)) {
                fn_close();
            } else if (p.fn_hit < 0) {
                p.fn_hit = 0;
            }
            p.fn_id = evt->point.id;
            outputs_port_touched();
            return true;
        }
        if (!gfx_rect_contains(fn_face(), x, y)) {
            return false;
        }
        if (!fn_locked()) {
            p.fn_hit = 0;
            p.fn_id  = evt->point.id;
        }
        outputs_port_touched();
        return true;
    }
    if (p.fn_hit < 0 || evt->point.id != p.fn_id) {
        return false;
    }
    if (evt->type != TOUCH_EVENT_UP) {
        return true;
    }
    const int hit = p.fn_hit;
    p.fn_hit = -1;
    outputs_port_touched();
    if (!touch_event_is_tap_up(evt) || fn_locked()) {
        if (fn_locked()) {
            fn_close();
        }
        return true;
    }
    if (hit == 0 && gfx_rect_contains(fn_face(), x, y)) {
        p.fn_open = !p.fn_open;
    } else if (hit > 0 && gfx_rect_contains(fn_row(hit - 1), x, y)) {
        const uint8_t fn = (uint8_t)(hit - 1);
        if (fn != port_view_func((uint8_t)p.port)) {
            (void)port_view_ask((uint8_t)p.port, PROG_LINK_ASK_FUNC, fn);
        }
        fn_close();
    }
    return true;
}

bool outputs_port_event(const touch_event_t *evt)
{
    if (evt == NULL || p.port < 0) {
        return false;
    }
    sync_tabs();
    if (fn_event(evt)) {
        return true;
    }
    if (pin_event(evt)) {
        return true;
    }
    bool took;
    int i = seg_event(&p.tester, evt, tester_locked(), &took);
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
    gfx_text(c, COL_X, COL_Y, TR(OUT_PROTOCOL), OU_FONT_LABEL, UI_TEXT_FAINT,
             1);
    outputs_port_name((uint8_t)p.port, buf, sizeof(buf));
    ui_choice_face(c, dd, buf, pressed, open);
    sync_tabs();

    pin_label(buf, sizeof(buf));
    ui_button_font(c, pin_rect(), buf, UI_PANEL_HI, p.pin_down, true,
                   OU_FONT_LABEL);

    gfx_text(c, COL_X, FN_Y, TR(OUT_PORT_FUNCTION), OU_FONT_LABEL,
             UI_TEXT_FAINT, 1);
    const uint8_t fn = port_view_func((uint8_t)p.port);
    const char *fn_name = known() ? port_view_fn_name(fn) : "?";
    if (fn_locked()) {
        ui_choice_face_locked(c, fn_face(), fn_name);
    } else {
        ui_choice_face(c, fn_face(), fn_name, p.fn_hit == 0, p.fn_open);
    }

    if (g_watch[p.port][0] != '\0') {
        snprintf(buf, sizeof(buf), TR(OUT_PORT_WATCHED), g_watch[p.port]);
    } else {
        snprintf(buf, sizeof(buf), "%s", TR(OUT_PORT_WATCH_NONE));
    }
    /* In the label face where it fits the column, else in 8 x 16: "VON
     * KEINEM STROMKANAL ÜBERWACHT" is 31 cells. */
    const gfx_font_t *wf = (gfx_text_width(OU_FONT_LABEL, buf, 1) <= COL_W)
                               ? OU_FONT_LABEL : OU_FONT_FINE;
    gfx_text(c, COL_X, WATCH_Y + ((wf == OU_FONT_LABEL) ? 0 : 2), buf, wf,
             UI_TEXT_DIM, 1);

    gfx_text(c, COL_X, TESTER_Y, TR(OUT_PORT_TESTER), OU_FONT_LABEL,
             UI_TEXT_FAINT, 1);
    ui_tabs_render_locked(&p.tester, c, tester_locked());
    gfx_text(c, COL_X, PWM_Y, TR(OUT_PORT_PWM), OU_FONT_LABEL,
             UI_TEXT_FAINT, 1);
    ui_tabs_render_locked(&p.pwm, c, pwm_locked());
    if (outputs_screen_port_reason(buf, sizeof(buf))) {
        /* Three lines where it needs them: "GESPERRT: DER
         * SERVO-PROGRAMMIERER BELEGT DEN PORT" breaks into three of 30
         * cells. */
        (void)outputs_wrap(c, gfx_rect_make(COL_X, WHY_Y, COL_W, 20),
                           OU_LINE, buf, 3, OU_FONT_LABEL, UI_WARN, false);
    }
    /* Last: the open list lies over the rows under the face. */
    if (p.fn_open && !fn_locked()) {
        ui_choice_list(c, fn_list());
        for (int i = 0; i < fn_rows(); ++i) {
            ui_choice_row(c, fn_row(i), port_view_fn_name((uint8_t)i),
                          (uint8_t)i == fn, p.fn_hit == 1 + i);
        }
    }
}

bool outputs_port_fn_open(void) { return p.fn_open; }

bool outputs_port_back(void)
{
    if (!p.fn_open) {
        return false;
    }
    fn_close();
    outputs_port_touched();
    return true;
}
