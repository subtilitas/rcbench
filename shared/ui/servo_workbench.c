/**
 * @file
 * @brief The SERVO function screens' workbench (ui_workbench.h): the task
 * each function draws on, the programmers' sessions, TEST's run card and
 * the caps opener.
 *
 * Each function screen fixes one task of the strip, which is never drawn
 * (servo_fn.c): LIVE (the dial) for LIVE and SWEEP, TEST (the run card)
 * for TEST and LIMIT RUN, RUDDER for RUDDER and RUDDER AUTO, PROGRAM for
 * KST and HITEC, whose session shows over the left column.  A run card's
 * SETTINGS opens that run's settings in the page area, one level down: the
 * home tag (wb_back()) and the page's CLOSE climb from there back to the
 * card.  A session whose port is in programming asks for LEAVE first.
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
     * page under it stays.  From a run's settings it climbs to the run's
     * card, from the brown-out walk's to the automatic test's, from KST's
     * EXPERT MODE to the session. */
    if (modal_open()) {
        close_panels();
    } else if (s.run_list != RL_NONE) {
        s.run_list = (s.run_list == RL_BROWNOUT) ? RL_AUTO : RL_NONE;
    } else {
        return false;
    }
    ov_let_go();
    servo_invalidate();
    ++s.ctrl_rev;
    return true;
}

/**
 * @brief START LIMIT RUN, on the LIMIT RUN card: START TEST's place on its
 *        own card.
 * @return the button
 */
gfx_rect_t lim_start_rect(void)
{
    return (gfx_rect_t){ (int16_t)(RUN_CARD_X + 10),
                         (int16_t)(RUN_CARD_Y + RUN_BTN_DY), RUN_BTN_W,
                         RUN_BTN_H };
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

/**
 * @brief ANGLE SOURCE tapped in RUDDER's or RUDDER AUTO's list: SENSORS on
 *        the source's entry, the AS5600's or the IMU's.  The router leaves
 *        this screen for it, and the home tag there comes back here.
 */
void wb_open_encoder(void)
{
    const int e = (enc_src() == SERVO_ANGLE_IMU) ? SENS_ENTRY_IMU
                                                          : SENS_ENTRY_AS5600;
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
    return rudder_rest(evt) || rauto_rest(evt) || foot_rest(evt);
}

/* A press on TEST's or LIMIT RUN's page: its card's SETTINGS, its START,
 * on TEST the caps. */
void test_page_down(const touch_event_t *evt)
{
    const int x = evt->point.x, y = evt->point.y;
    const int k = run_card_shown();
    if (k == 0 && opener_down(evt, caps_open_rect(), OP_OPEN_CAPS)) {
        return;
    }
    if (gfx_rect_contains(run_set_rect(k), x, y)) {
        ov_take(evt, OP_RUN_SET, k);
        return;
    }
    if (k == 0 && gfx_rect_contains(test_start_rect(), x, y)) {
        ov_take(evt, OP_TEST_START, -1);
        /* A hold, unless a run is under way (a tap stops it), a step is
         * past 6.0V (a tap opens the HV warning, whose hold starts it), or
         * it cannot run now (a tap says why). */
        s.test_note = 0;
        if (!servo_test_running(&s.test) && test_blocked() == 0
            && !test_needs_hv()) {
            s.test_down = true;
            ui_hold_begin(&s.test_hold, TX_HOLD_HINT_TEST);
        }
        ++s.test_rev;
        return;
    }
    if (k == 1 && gfx_rect_contains(lim_start_rect(), x, y)) {
        ov_take(evt, OP_LIM_START, -1);
        /* A hold, unless the run is under way (a tap stops it) or it
         * cannot run now (a tap says why). */
        s.lim_note = NULL;
        if (!servo_limrun_running(&s.lim) && lim_blocked() == NULL) {
            s.lim_down = true;
            ui_hold_begin(&s.lim_hold, TX_HOLD_HINT_LIMIT_RUN);
        }
        ++s.test_rev;
    }
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
 * @brief TEST's run card and the caps, or LIMIT RUN's run card.
 * @param c the canvas
 */
void draw_test_page(gfx_canvas_t *c)
{
    const gfx_rect_t a = page_area();
    gfx_fill_rect(c, a.x + 1, a.y + 1, a.w - 2, a.h - 2,
                  ui_theme_color(UI_C_PANEL));
    static const ui_text_id_t k_title[2] = { TX_SV_AUTO_TEST,
                                             TX_SV_ROW_LIM_MODE };
    const int k = run_card_shown();
    {
        const gfx_rect_t r = run_card_rect(k);
        gfx_fill_round_rect(c, r.x, r.y, r.w, r.h, 6,
                            ui_theme_color(UI_C_PANEL_SUNK));
        gfx_draw_round_rect(c, r.x, r.y, r.w, r.h, 6,
                            ui_theme_color(UI_C_EDGE));
        gfx_text(c, r.x + 10, r.y + RUN_NAME_DY, ui_tr(k_title[k]),
                 SV_FONT_LABEL, ui_theme_color(UI_C_ACCENT), 1);
        ui_button_font(c, run_set_rect(k), TR(SUP_SETTINGS),
                       ui_theme_color(UI_C_PANEL_HI),
                       s.ov_have && s.ov_pressed == OP_RUN_SET
                           && s.ov_row == k,
                       true, SV_FONT_LABEL);
    }
    if (k == 0) {
        draw_test_start(c);
        draw_test_lines(c);
        draw_caps_button(c);
    } else {
        draw_lim_start(c);
        draw_lim_lines(c);
    }
    /* The run's box under the card: its lines while it runs and its
     * verdict after, STOP or CLOSE. */
    if (run_box_shown()) {
        if (k == 0) {
            draw_test_box(c);
        } else {
            draw_lim_box(c);
        }
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
    return s.arm_down;
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
