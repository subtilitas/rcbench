/*
 * The SERVO screen as a workbench (ui_workbench.h): the task strip under
 * the band, TEST's page of run cards, and the output card's TARGET row.
 *
 * The strip shows LIVE (the dial, SWEEP, CENTRE, RELEASE), TEST (the
 * AUTOMATIC TEST and LIMIT RUN cards), PROGRAM (the registry's SERVO rows,
 * prog_registry.h) and SETTINGS (OUTPUT, SWEEP, DUT).  A tile opens LIVE;
 * each task keeps its own state for the session -- SETTINGS its page, TEST
 * the run whose settings are open, PROGRAM its list's page and search --
 * and a tab switch is no navigation level.  A run card's SETTINGS opens
 * that run's settings in the page area, one level down: the home tag
 * climbs from there back to TEST (wb_back()), and from TEST to the menu.
 * A live row on PROGRAM opens its session over the left column, one level
 * down: the home tag and the session's CLOSE climb back to the list, and
 * a session whose port is in programming asks for LEAVE first.
 *
 * The TARGET row reads what the tester drives as `protocol pins` and is
 * the control that picks it, as MOTOR & ESC's is: a tap opens SETUP >
 * OUTPUTS on what the tester drives -- the PROG PORT entry with the port
 * as the TARGET setting, SERVO PWM otherwise -- where SERVO TESTER and
 * PORT PWM are set.  The row takes no tap under the TARGET setting's lock,
 * and the home tag there comes back here on the task the row was tapped
 * on.
 *
 * The screen is servo_screen.c; the state and the layout the files share
 * are servo_screen_int.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include "kst_panel.h"
#include "outputs_screen.h"
#include "servo_limrun.h"
#include "prog_registry.h"
#include "settings.h"
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

/* SERVO's tasks. */
#define SERVO_TASKS (UI_WB_HAS(UI_WB_LIVE) | UI_WB_HAS(UI_WB_TEST) \
                     | UI_WB_HAS(UI_WB_PROGRAM) | UI_WB_HAS(UI_WB_SETTINGS))

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
    return false;
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
    prog_view_cancel(&s.prog_view);
}

void prog_cancel(void)
{
    const prog_page_set_t *set[PROG_LIST_MAX];
    const int k = servo_sets(set);
    for (int i = 0; i < k; ++i) {
        prog_ps_cancel(set[i]);
    }
    prog_view_cancel(&s.prog_view);
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

/* The IOmcu's capabilities, which the rows' states follow. */
static uint16_t caps_now(void)
{
    return ui_router_status()->capabilities;
}

/* PROGRAM's list as it stands: SERVO's PROGRAM rows, the search applied. */
static void prog_list_now(prog_list_t *l)
{
    size_t n = 0;
    const prog_reg_row_t *rows = prog_registry(&n);
    prog_list_build(l, rows, n, PROG_DEV_SERVO, UI_WB_PROGRAM,
                    s.prog_view.query);
}

/* A session that holds its port and is not shown, with no limit run of
 * the screen's owning it, shows again: on PROGRAM, over the list.  The
 * way back to PWM is asked there and nowhere else. */
void prog_follow_hold(void)
{
    const prog_page_set_t *set[PROG_LIST_MAX];
    const int k = servo_sets(set);
    for (int i = 0; i < k; ++i) {
        if (prog_ps_holds(set[i]) && !prog_ps_is_open(set[i])) {
            if (ui_wb_strip_select(&s.wb, UI_WB_PROGRAM)) {
                ++s.strip_rev;
            }
            prog_ps_open(set[i]);
        }
    }
}

/* What PROGRAM's rows show, as one number: a change repaints the page. */
static uint32_t prog_signature(void)
{
    size_t n = 0;
    const prog_reg_row_t *rows = prog_registry(&n);
    const uint16_t caps = caps_now();
    uint32_t sig = (uint32_t)caps * 2654435761u;
    for (size_t i = 0; i < n; ++i) {
        const uint8_t pin = (rows[i].page_set != NULL)
                                ? prog_ps_pin(rows[i].page_set)
                                : PROG_REG_NO_PIN;
        sig = sig * 31u + (uint32_t)prog_reg_state(rows, n, i, caps) * 257u
              + pin;
    }
    return sig;
}

void prog_page_follow(void)
{
    if (wb_task() != UI_WB_PROGRAM || prog_shown()) {
        return;
    }
    const uint32_t sig = prog_signature();
    if (sig != s.prog_sig) {
        s.prog_sig = sig;
        ++s.ctrl_rev;
    }
}

/* Row @p i tapped: a live row's session opens over the left column, one
 * level down; a dimmed row opens nothing, its reason drawn beside it. */
static void prog_row_open(uint8_t i)
{
    size_t n = 0;
    const prog_reg_row_t *rows = prog_registry(&n);
    if (i >= n || prog_reg_state(rows, n, i, caps_now()) != PROG_ROW_LIVE) {
        return;
    }
    /* A run would go on behind the session, stepping the rail and the
     * position of the port CONNECT takes. */
    test_end_now(SERVO_TEST_AB_OPERATOR);
    close_panels();
    prog_ps_open(rows[i].page_set);
    servo_invalidate();
}

void prog_page_down(const touch_event_t *evt)
{
    prog_list_t l;
    prog_list_now(&l);
    (void)prog_view_event(&s.prog_view, &l, evt, NULL);
    ++s.ctrl_rev;
}

/* The rest of a press on PROGRAM's list. */
static bool prog_page_rest(const touch_event_t *evt)
{
    if (s.prog_view.pressed == PROG_VIEW_P_NONE
        || evt->point.id != s.prog_view.id) {
        return false;
    }
    prog_list_t l;
    prog_list_now(&l);
    uint8_t row = 0;
    const prog_view_ev_t r = prog_view_event(&s.prog_view, &l, evt, &row);
    if (r == PROG_VIEW_ROW && wb_task() == UI_WB_PROGRAM && !modal_open()
        && !prog_shown()) {
        prog_row_open(row);
    } else if (r == PROG_VIEW_SEARCH && wb_task() == UI_WB_PROGRAM) {
        ui_textkey_open_search(&s.tk, overlay_area(), TR(SP_FIND_TITLE),
                               s.prog_view.query, PROG_LIST_QUERY_MAX);
        servo_invalidate();
    }
    ++s.ctrl_rev;
    return true;
}

void prog_search_done(const char *text)
{
    snprintf(s.prog_view.query, sizeof(s.prog_view.query), "%s",
             (text != NULL) ? text : "");
    prog_view_top(&s.prog_view);
    servo_invalidate();
}

void draw_prog_page(gfx_canvas_t *c)
{
    const gfx_rect_t a = page_area();
    gfx_fill_rect(c, a.x + 1, a.y + 1, a.w - 2, a.h - 2,
                  ui_theme_color(UI_C_PANEL));
    size_t n = 0;
    const prog_reg_row_t *rows = prog_registry(&n);
    prog_list_t l;
    prog_list_now(&l);
    prog_view_render(&s.prog_view, &l, rows, n, caps_now(),
                     s.prog_view.query, c);
}

/* ------------------------------------------------------------ the strip */

/* ui_router_navigations() just after the TARGET row opened OUTPUTS, or 0:
 * the entry one navigation later is the way back. */
static uint32_t s_outputs_nav;

void wb_init(void)
{
    s_outputs_nav = 0u;
    ui_wb_strip_init(&s.wb, SERVO_TASKS, UI_WB_STRIP_X, UI_WB_STRIP_Y);
    s.run_list = RL_NONE;
    ++s.strip_rev;
    ui_target_row_init(&s.trow, (gfx_rect_t){ RC_X, TARGET_Y, RC_W,
                                              TARGET_H });
    prog_view_init(&s.prog_view, page_area());
}

void wb_enter(void)
{
    /* Back from OUTPUTS, which the TARGET row opened, with no screen in
     * between: the task it was opened from.  Any other entry is a tile's,
     * which opens LIVE. */
    const bool back = s_outputs_nav != 0u
                      && ui_router_navigations() == s_outputs_nav + 1u;
    s_outputs_nav = 0u;
    if (!back) {
        ui_wb_strip_open(&s.wb);
    }
    ui_wb_strip_cancel(&s.wb);
    ui_target_row_cancel(&s.trow);
    ++s.strip_rev;
    servo_invalidate();
}

void wb_leave(void)
{
    s.run_list = RL_NONE;
    prog_view_cancel(&s.prog_view);
    ui_wb_strip_cancel(&s.wb);
    ui_target_row_cancel(&s.trow);
    ++s.strip_rev;
}

void wb_cancel(void)
{
    ui_wb_strip_cancel(&s.wb);
    ui_target_row_cancel(&s.trow);
    ++s.strip_rev;
}

bool wb_back(void)
{
    /* Over a panel the tag closes the panel, as its CANCEL does, and the
     * page under it stays.  In a session it is the session's back: an
     * editor closes, the session closes to PROGRAM's list, or a port in
     * programming asks for LEAVE and the session stays.  From a run's
     * settings it climbs to TEST. */
    const prog_page_set_t *ps = prog_shown_set();
    if (modal_open()) {
        close_panels();
    } else if (ps != NULL) {
        prog_ps_back(ps);
    } else if (wb_task() == UI_WB_TEST && s.run_list != RL_NONE) {
        s.run_list = RL_NONE;
    } else {
        return false;
    }
    ov_let_go();
    servo_invalidate();
    ++s.ctrl_rev;
    return true;
}

gfx_rect_t strip_area(void)
{
    const gfx_rect_t r = ui_wb_strip_rect(&s.wb);
    return (gfx_rect_t){ (int16_t)(r.x - 3), (int16_t)(r.y - 3),
                         (int16_t)(r.w + 6), (int16_t)(r.h + 6) };
}

gfx_rect_t lim_start_rect(void)
{
    return (gfx_rect_t){ (int16_t)(RUN_CARD_X + 10),
                         (int16_t)(RUN_CARD_Y(1) + RUN_BTN_DY), 240,
                         OV_ROW_H + 2 };
}

/*
 * Another task is shown.  A press on the page that went goes with it:
 * START's hold above all, which must not complete on a page out of sight.
 * The page is repainted whole.
 */
static void task_changed(void)
{
    ov_let_go();
    prog_view_cancel(&s.prog_view);
    ui_hold_reset(&s.test_hold);
    s.test_down = false;
    ui_hold_reset(&s.lim_hold);
    s.lim_down = false;
    ++s.test_rev;
    servo_invalidate();
    ++s.ctrl_rev;
}

/*
 * The TARGET row tapped: SETUP > OUTPUTS, on what the row shows -- where
 * the IOmcu sends the tester's commands now (s.drive_target), not the
 * TARGET setting, which a write still pending or a port without a pin
 * leaves apart from it.  Programming port n: its PROG PORT entry, the
 * overview where the list has no such entry.  The standard outputs: SERVO
 * PWM.  A target not known or no link: the overview, as the row names
 * none.  The router leaves this screen for it, which switches the bench to
 * SAFE and ends a sweep or a test as any way out does (leave()); the row
 * takes the tap only under the TARGET setting's lock.
 */
static void open_outputs(void)
{
    const uint8_t target = s.drive_target;
    if (target == SERVO_SCREEN_TARGET_UNKNOWN
        || target == SERVO_SCREEN_TARGET_NO_LINK) {
        outputs_screen_open_on(0u);
    } else if (target != 0u) {
        outputs_screen_open_on_port((uint8_t)(target - 1u));
    } else {
        const outbind_proto_t *p = outbind_protos();
        for (uint8_t i = 0; i < OUTBIND_PROTOS; ++i) {
            if (p[i].driver == OUT_DRIVER_PWM
                && p[i].role == OUT_ROLE_SURFACE) {
                outputs_screen_open_on(i);
                break;
            }
        }
    }
    ui_router_goto(SCREEN_OUTPUTS);
    s_outputs_nav = ui_router_navigations();
}

bool wb_down(const touch_event_t *evt)
{
    const int x = evt->point.x, y = evt->point.y;
    /* The strip, unless a panel or a session covers it. */
    if (!modal_open() && !prog_shown()
        && gfx_rect_contains(strip_area(), x, y)) {
        if (!ui_wb_strip_pressed(&s.wb)) {
            (void)ui_wb_strip_event(&s.wb, evt);
            ++s.strip_rev;
        }
        return true;
    }
    if (ui_target_row_event(&s.trow, evt, target_locked_for(SET_SERVO_TARGET))
        != UI_TARGET_NONE) {
        ++s.ctrl_rev;
        return true;
    }
    return false;
}

bool wb_rest(const touch_event_t *evt)
{
    if (prog_page_rest(evt)) {
        return true;
    }
    if (ui_wb_strip_pressed(&s.wb) && evt->point.id == s.wb.tabs.press_id) {
        if (evt->type == TOUCH_EVENT_UP) {
            const bool changed = ui_wb_strip_event(&s.wb, evt);
            ++s.strip_rev;
            if (changed) {
                task_changed();
            }
        }
        return true;
    }
    const ui_target_ev_t r = ui_target_row_event(
        &s.trow, evt, target_locked_for(SET_SERVO_TARGET));
    if (r == UI_TARGET_NONE) {
        return false;
    }
    if (r == UI_TARGET_TAP) {
        open_outputs();
        return true;
    }
    ++s.ctrl_rev;
    return true;
}

/* A press on TEST's page: a card's SETTINGS, or a START. */
void test_page_down(const touch_event_t *evt)
{
    const int x = evt->point.x, y = evt->point.y;
    for (int k = 0; k < 2; ++k) {
        if (gfx_rect_contains(run_set_rect(k), x, y)) {
            ov_take(evt, OP_RUN_SET, k);
            return;
        }
    }
    if (gfx_rect_contains(test_start_rect(), x, y)) {
        ov_take(evt, OP_TEST_START, -1);
        /* A hold, unless a run is under way (a tap stops it), a step is
         * past 6.0 V (a tap opens the HV warning, whose hold starts it), or
         * it cannot run now (a tap says why). */
        s.test_note = 0;
        if (!servo_test_running(&s.test) && test_blocked() == 0
            && !test_needs_hv()) {
            s.test_down = true;
            ui_hold_begin(&s.test_hold);
        }
        ++s.test_rev;
        return;
    }
    if (gfx_rect_contains(lim_start_rect(), x, y)) {
        ov_take(evt, OP_LIM_START, -1);
        /* A hold, unless the run is under way (a tap stops it) or it
         * cannot run now (a tap says why). */
        s.lim_note = NULL;
        if (!servo_limrun_running(&s.lim) && lim_blocked() == NULL) {
            s.lim_down = true;
            ui_hold_begin(&s.lim_hold);
        }
        ++s.test_rev;
    }
}

/* ----------------------------------------------------------------- drawing */

void draw_strip(gfx_canvas_t *c)
{
    const gfx_rect_t a = strip_area();
    gfx_fill_rect(c, a.x, a.y, a.w, a.h, ui_theme_color(UI_C_BG));
    ui_wb_strip_render(&s.wb, c);
}

/* SUPPLY's caps in force, which a run's steps and a set point stay
 * inside, read-only here: their home is SUPPLY > SETTINGS. */
void draw_caps_line(gfx_canvas_t *c, int x, int y)
{
    const supply_caps_t caps = supply_screen_caps();
    char line[64];
    snprintf(line, sizeof(line), TR(SV_CAPS), (double)caps.v_max,
             (double)caps.i_max);
    gfx_text(c, x, y, line, &gfx_font_8x16, ui_theme_color(UI_C_TEXT_DIM), 1);
}

/* TEST: the two run cards, then the caps. */
void draw_test_page(gfx_canvas_t *c)
{
    const gfx_rect_t a = page_area();
    gfx_fill_rect(c, a.x + 1, a.y + 1, a.w - 2, a.h - 2,
                  ui_theme_color(UI_C_PANEL));
    static const ui_text_id_t k_title[2] = { TX_SV_AUTO_TEST,
                                             TX_SV_ROW_LIM_MODE };
    for (int k = 0; k < 2; ++k) {
        const gfx_rect_t r = run_card_rect(k);
        gfx_fill_round_rect(c, r.x, r.y, r.w, r.h, 6,
                            ui_theme_color(UI_C_PANEL_SUNK));
        gfx_draw_round_rect(c, r.x, r.y, r.w, r.h, 6,
                            ui_theme_color(UI_C_EDGE));
        gfx_text(c, r.x + 10, r.y + 11, ui_tr(k_title[k]), &gfx_font_8x16,
                 ui_theme_color(UI_C_ACCENT), 1);
        ui_button(c, run_set_rect(k), TR(SUP_SETTINGS),
                  ui_theme_color(UI_C_PANEL_HI),
                  s.ov_have && s.ov_pressed == OP_RUN_SET && s.ov_row == k,
                  true);
    }
    draw_test_start(c);
    draw_test_lines(c);
    draw_lim_start(c);
    draw_lim_lines(c);
    draw_caps_line(c, RUN_CARD_X,
                   RUN_CARD_Y(1) + RUN_CARD_H + 8);
}

/*
 * What the tester drives as `protocol pins`, on the output card, in sight
 * on every task and under every panel and the programmer.  The programming
 * port is the rare case and takes the warning colour: the tester's
 * commands then do not reach the SERVO PWM outputs, which an armed bank
 * holds at their rest.  No pin, a binding not read, a target not known and
 * no link take it too.
 */
void draw_target(gfx_canvas_t *c)
{
    char line[32];
    const bool warn = servo_target_line(line, sizeof(line), s.drive_target,
                                        s.drive_pin, s.drive_pwm, s.pins,
                                        s.pins_known);
    ui_target_row_render(&s.trow, c, TR(SV_TARGET), line, warn,
                         target_locked_for(SET_SERVO_TARGET));
}

/* The protocol the SERVO PWM outputs are bound as: a name, the same in
 * every language. */
#define TARGET_PROTOCOL "SERVO PWM"
/* Characters the TARGET row's value holds: its box less its padding. */
#define TARGET_CELLS 23u

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
            snprintf(out, size, "%s",
                     pwm ? TR(SV_TARGET_PORT) : TR(SV_TARGET_PORT_OFF));
        } else {
            snprintf(out, size,
                     pwm ? TR(SV_TARGET_PORT_PIN) : TR(SV_TARGET_PORT_PIN_OFF),
                     (unsigned)gpio);
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

int servo_screen_settings_page(void) { return (int)s.tabs.selected; }

int servo_screen_run_settings(void) { return (int)s.run_list; }

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
