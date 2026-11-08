/**
 * @file
 * @brief The SERVO function screens' workbench (ui_workbench.h): the task
 * each function draws on, the programmers' sessions, TEST's run card and
 * the caps opener.
 *
 * Each function screen fixes one task of the strip, which is never drawn
 * (servo_fn.c): LIVE (the dial) for LIVE and SWEEP, TEST (the run card)
 * for TEST and LIMIT RUN, RUDDER for RUDDER and RUDDER AUTO, PROGRAM for
 * KST and HITEC, whose session shows over the left column.  TEST's card
 * has the run's lines, the caps button, the box and the rail's plot;
 * LIMIT RUN's left card is servo_limit_card.c's.  Both runs' holds and
 * lists are the footer's.  KST's SETTINGS opens EXPERT MODE's page, one
 * level down: the home tag (wb_back()) and the page's CLOSE climb from
 * there.  A session whose port is in programming asks for LEAVE first.
 *
 * The screen is servo_screen.c; the state and the layout the files share
 * are servo_screen_int.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include "esc_wire_panel.h"
#include "hitec_panel.h"
#include "kst_panel.h"
#include "outputs_screen.h"
#include "servo_limrun.h"
#include "prog_registry.h"
#include "sensors_pane.h"
#include "settings.h"
#include "settings_screen.h"
#include "settings_sensors.h"
#include "supply_screen.h"
#include "ui_target_row.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_screen.h"
#include "ui_widgets.h"
#include "ui_workbench.h"

/* Last: its layout macros have short names (W, H, PAD), which no header
 * included after it may meet. */
#include "servo_screen_int.h"

/* The tasks SERVO's function screens draw on. */
#define SERVO_TASKS (UI_WB_HAS(UI_WB_LIVE) | UI_WB_HAS(UI_WB_TEST) \
                     | UI_WB_HAS(UI_WB_PROGRAM) | UI_WB_HAS(UI_WB_RUDDER))

/* ---------------------------------------------------- PROGRAM's sessions */

/* The registry's SERVO sessions, each distinct page set once, whichever
 * rows open it (prog_reg_sets()).  Their count. */
static int servo_sets(const prog_page_set_t *out[PROG_LIST_MAX])
{
    return prog_reg_sets(PROG_DEV_SERVO, out);
}

const prog_page_set_t *prog_shown_set(void)
{
    const prog_page_set_t *set[PROG_LIST_MAX];
    const int k = servo_sets(set);
    for (int i = 0; i < k; ++i) {
        if (prog_ps_is_open(set[i])) {
            return set[i];
        }
    }
    return NULL;
}

bool prog_holds(void)
{
    const prog_page_set_t *set[PROG_LIST_MAX];
    const int k = servo_sets(set);
    for (int i = 0; i < k; ++i) {
        if (prog_ps_holds(set[i])) {
            return true;
        }
    }
    /* The ESC_WIRE page is no page set of the SERVO screen, and one pin held
     * holds the bank down all the same. */
    return esc_wire_panel_holds_port();
}

void prog_init(void)
{
    const prog_page_set_t *set[PROG_LIST_MAX];
    const int k = servo_sets(set);
    for (int i = 0; i < k; ++i) {
        prog_ps_init(set[i], overlay_area());
    }
}

void prog_entry(void)
{
    const prog_page_set_t *set[PROG_LIST_MAX];
    const int k = servo_sets(set);
    for (int i = 0; i < k; ++i) {
        prog_ps_entry(set[i]);
    }
}

void prog_leave(void)
{
    const prog_page_set_t *set[PROG_LIST_MAX];
    const int k = servo_sets(set);
    for (int i = 0; i < k; ++i) {
        prog_ps_leave(set[i]);
    }
}

void prog_cancel(void)
{
    const prog_page_set_t *set[PROG_LIST_MAX];
    const int k = servo_sets(set);
    for (int i = 0; i < k; ++i) {
        prog_ps_cancel(set[i]);
    }
}

void prog_armed(bool armed)
{
    const prog_page_set_t *set[PROG_LIST_MAX];
    const int k = servo_sets(set);
    for (int i = 0; i < k; ++i) {
        prog_ps_armed(set[i], armed);
    }
}

void prog_tick(float dt_s)
{
    const prog_page_set_t *set[PROG_LIST_MAX];
    const int k = servo_sets(set);
    for (int i = 0; i < k; ++i) {
        prog_ps_tick(set[i], dt_s);
    }
}

/* A session that holds its port and is not shown, with no limit run of
 * the screen's owning it, shows again on its own screen, KST's or
 * HITEC's: on another SERVO screen the router goes there, as PROGRAM's
 * task came up before.  The way back to PWM is asked there and nowhere
 * else. */
void prog_follow_hold(void)
{
    static const struct {
        const prog_page_set_t *set;
        servo_fn_t fn;
    } k_sets[] = {
        { &kst_panel_pages, SERVO_FN_KST },
        { &hitec_panel_pages, SERVO_FN_HITEC },
    };
    for (size_t i = 0; i < sizeof(k_sets) / sizeof(k_sets[0]); ++i) {
        if (!prog_ps_holds(k_sets[i].set) || prog_ps_is_open(k_sets[i].set)) {
            continue;
        }
        if (s.fn == (uint8_t)k_sets[i].fn) {
            prog_ps_open(k_sets[i].set);
        } else if (servo_screen_owns(ui_router_current())
                   && ui_router_current() == servo_fn_screen_id(
                          (servo_fn_t)s.fn)) {
            ui_router_goto(servo_fn_screen_id(k_sets[i].fn));
        }
        return;
    }
}

/* ------------------------------------------------------------ the tasks */

/** @brief The task strip at its start, LIVE, never drawn: the function
 *         screens fix its task (servo_screen_show_fn()). */
void wb_init(void)
{
    ui_wb_strip_init(&s.wb, SERVO_TASKS, UI_WB_STRIP_X, UI_WB_STRIP_Y);
    s.run_list = RL_NONE;
    rudder_init();
}

void wb_enter(void)
{
    ui_wb_strip_cancel(&s.wb);
    servo_invalidate();
}

void wb_leave(void)
{
    s.run_list = RL_NONE;
    rudder_cancel();
    rauto_cancel();
    ui_wb_strip_cancel(&s.wb);
    s.fp_pressed = FP_NONE;
    ++s.fp_rev;
}

void wb_cancel(void)
{
    rudder_cancel();
    rauto_cancel();
    ui_wb_strip_cancel(&s.wb);
    s.fp_pressed = FP_NONE;
    ++s.fp_rev;
}

bool svs_covered(void)
{
    return modal_open() || prog_shown();
}

bool wb_back(void)
{
    /* Over a panel the tag closes the panel, as its CANCEL does, and the
     * page under it stays.  From KST's EXPERT MODE it climbs to the
     * session. */
    if (modal_open()) {
        close_panels();
    } else if (s.run_list != RL_NONE) {
        s.run_list = RL_NONE;
    } else {
        return false;
    }
    ov_let_go();
    servo_invalidate();
    ++s.ctrl_rev;
    return true;
}

/**
 * @brief The caps button tapped: SUPPLY's LIMITS.  The router leaves this
 *        screen for it, and the home tag there comes back to TEST.
 */
void wb_open_caps(void)
{
    ui_router_goto(SCREEN_SUPPLY);
    supply_screen_open_limits();
}

int wb_meter_entry(void)
{
    /* The part of the channel watching the target, else the first part
     * fitted: settings_sensors_map()'s mirror part. */
    settings_sensors_map_t m;
    settings_sensors_map(&m);
    const int part = (m.mirror < SET_INA_PARTS) ? (int)m.mirror : 0;
    return SENS_ENTRY_INA3221_0 + part;
}

/**
 * @brief The red METER row tapped: SETUP > SENSORS on the INA3221 entry
 *        wb_meter_entry() names.  The router leaves this screen for it,
 *        and the home tag there comes back here.
 */
void wb_open_meter(void)
{
    const int e = wb_meter_entry();
    ui_router_goto(SCREEN_SETUP);
    settings_screen_open_on(SET_CAT_SENSORS, e);
}

/**
 * @brief The caps button's press, where @p r holds the point.  It opens
 *        another screen: leaving switches the bench to SAFE and ends a
 *        sweep or a test, so it takes no press under the TARGET setting's
 *        lock.
 * @param evt  the DOWN
 * @param r    the button
 * @param code the press's OP_ code
 * @return whether the point is on the button
 */
bool opener_down(const touch_event_t *evt, gfx_rect_t r, int code)
{
    if (!gfx_rect_contains(r, evt->point.x, evt->point.y)) {
        return false;
    }
    if (!target_locked_for(SET_SERVO_TARGET)) {
        ov_take(evt, code, -1);
    }
    return true;
}

bool wb_rest(const touch_event_t *evt)
{
    return rudder_rest(evt) || rauto_rest(evt) || ready_rest(evt)
           || foot_rest(evt);
}

/* A press on TEST's page: the caps.  TEST's hold is the footer's; LIMIT
 * RUN's left card takes none. */
void test_page_down(const touch_event_t *evt)
{
    if (s.fn != SERVO_FN_TEST) {
        return;
    }
    (void)opener_down(evt, caps_open_rect(), OP_OPEN_CAPS);
}

/* ----------------------------------------------------------------- drawing */

/**
 * @brief The caps button: SUPPLY's caps in force, which a run's steps and a
 *        set point stay inside, read-only here; a tap opens their home on
 *        SUPPLY.  Grey while the TARGET setting's lock holds.
 * @param c the canvas
 */
void draw_caps_button(gfx_canvas_t *c)
{
    const supply_caps_t caps = supply_screen_caps();
    char line[64];
    snprintf(line, sizeof(line), TR(SV_CAPS), (double)caps.v_max,
             (double)caps.i_max);
    ui_button_font(c, caps_open_rect(), line, ui_theme_color(UI_C_PANEL_HI),
                   s.ov_have && s.ov_pressed == OP_OPEN_CAPS,
                   !target_locked_for(SET_SERVO_TARGET), SV_FONT_LABEL);
}

/**
 * @brief TEST's run card, the caps and the rail's plot.
 * @param c the canvas
 */
void draw_test_page(gfx_canvas_t *c)
{
    const gfx_rect_t a = page_area();
    gfx_fill_rect(c, a.x + 1, a.y + 1, a.w - 2, a.h - 2,
                  ui_theme_color(UI_C_PANEL));
    {
        const gfx_rect_t r = test_card_rect();
        gfx_fill_round_rect(c, r.x, r.y, r.w, r.h, 6,
                            ui_theme_color(UI_C_PANEL_SUNK));
        gfx_draw_round_rect(c, r.x, r.y, r.w, r.h, 6,
                            ui_theme_color(UI_C_EDGE));
        gfx_text(c, r.x + 10, r.y + RUN_NAME_DY, TR(SV_AUTO_TEST),
                 SV_FONT_LABEL, ui_theme_color(UI_C_ACCENT), 1);
    }
    draw_test_lines(c);
    draw_caps_button(c);
    test_draw_plot(c);
    /* The run's box under the card: its lines while it runs and its
     * verdict after, STOP or CLOSE. */
    if (run_box_shown()) {
        draw_test_box(c);
    }
}

/* The protocol the SERVO PWM outputs are bound as: a name, the same in
 * every language. */
#define TARGET_PROTOCOL "SERVO PWM"
/** @brief Characters the target's line holds in the 10 x 20 face: 25, the
 *         PORT button's value and the footer's line under the name. */
#define TARGET_CELLS 25u

bool servo_target_line(char *out, size_t size, uint8_t target, uint8_t gpio,
                       bool pwm, uint64_t pins, bool known)
{
    if (out == NULL || size == 0u) {
        return false;
    }
    /* No link, or the IOmcu's page not known: the tester sends
     * nothing, and where it would go is not known. */
    if (target == SERVO_SCREEN_TARGET_NO_LINK) {
        snprintf(out, size, "%s", TR(SV_TARGET_NO_LINK));
        return true;
    }
    if (target == SERVO_SCREEN_TARGET_UNKNOWN) {
        snprintf(out, size, "%s", TR(SV_TARGET_UNKNOWN));
        return true;
    }
    /* The target the drive uses, not the setting: a change waits for a
     * quiet bench.  A port target is the port whether or not its pin is
     * known: the commands go there and do not reach the SERVO PWM outputs.
     * With its pulse off the port renders none, and the line says so. */
    if (target != 0u) {
        if (gpio == SERVO_SCREEN_NO_PIN) {
            snprintf(out, size, pwm ? TR(SV_TARGET_PN) : TR(SV_TARGET_PN_OFF),
                     (unsigned)target);
        } else {
            snprintf(out, size,
                     pwm ? TR(SV_TARGET_PN_PIN) : TR(SV_TARGET_PN_PIN_OFF),
                     (unsigned)target, (unsigned)gpio);
        }
        return true;
    }
    if (!known) {
        snprintf(out, size, "%s", TR(SV_TARGET_UNREAD));
        return true;
    }
    if (pins == 0u) {
        snprintf(out, size, "%s", TR(SV_TARGET_NONE));
        return true;
    }
    /* The protocol, then as many pins as the value holds and "+N". */
    (void)ui_target_text(out, size, TARGET_PROTOCOL, pins, TARGET_CELLS);
    return false;
}

/* ------------------------------------------------------------ for tests */

int servo_screen_task(void) { return (int)wb_task(); }

int servo_screen_run_settings(void) { return (int)s.run_list; }

bool servo_screen_arm_asked(void)
{
    return s.gate.arm_asked;
}

bool servo_screen_arm_held(void)
{
    return s.arm_down || svs_start_pending();
}

bool servo_screen_target_locked(void)
{
    return target_locked_for(SET_SERVO_TARGET);
}

/* On OUTPUTS this screen is left: a bench ARMED for the motor counts too,
 * which no screen but this one's own ARM could arm while it was shown.
 * PORT PWM goes off ARMED or not, as on this screen. */
bool servo_screen_tester_target_locked(void)
{
    return target_locked_for(SET_SERVO_TARGET) || s.bench_armed;
}

bool servo_screen_port_pwm_locked(void)
{
    return target_locked_for(SET_SERVO_PORT_PWM)
           || (!settings_get_bool(SET_SERVO_PORT_PWM) && s.bench_armed);
}
