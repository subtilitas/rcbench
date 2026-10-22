/*
 * The SERVO screen as the servo suites drive it: the screen on one canvas
 * in the body's coordinates, a touch handed to its event hook, and the
 * places of the dial, the SETTINGS overlay, ARM and SPEED and the supply's
 * SET row mirrored from servo_screen.c.  Taken by the suites of
 * servo_screen_rig.h, test_servo_settings.c, test_servo_test_panel.c and
 * test_servo_limit_panel.c.
 *
 * Only static inline helpers and the buffers and counters they use.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "gfx.h"
#include "kst_panel.h"
#include "link_pages.h"
#include "servo_screen.h"
#include "settings.h"
#include "supply_screen.h"
#include "ui_keypad.h"
#include "ui_screen.h"
#include "ui_theme.h"

#include "servo_spots.h"

#define W UI_PANEL_W
#define H UI_PANEL_H

/* Mirrored from servo_screen_core_int.h (ARC_R, the dial's radius) and
 * servo_spots.h; if the layout moves these move with it. */
#define BODY_H  UI_BODY_H
#define SHAFT_X SV_SHAFT_X
#define SHAFT_Y SV_SHAFT_Y
#define ARC_R   150

static gfx_color_t *fb;
static gfx_canvas_t cv;
static const ui_screen_t *scr;

/* The servo rail's meter as the panel hands it over: the meter, and the
 * count of its changes (servo_source_changes()).  The count is read only
 * by servo_bench_rig.h's meter() and window(). */
static servo_source_id_t meter_id = SERVO_SOURCE_MODEL;
static uint32_t meter_changes __attribute__((unused));

static inline void fresh(void)
{
    meter_id = SERVO_SOURCE_MODEL;
    if (fb == NULL) {
        fb = calloc((size_t)W * H, sizeof(gfx_color_t));
        REQUIRE_ALLOC(fb);
    }
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    gfx_canvas_init(&cv, fb, W, H, W);
    ui_theme_set(UI_THEME_DARK);
    settings_set_store(NULL);
    settings_init();
    supply_screen()->reset();
    supply_screen_settings_loaded();
    scr = servo_screen();
    scr->reset();
    /* An IOmcu that reports its servo PWM bank: PROGRAM's KST row is
     * live. */
    ui_bench_status_t st = *ui_router_status();
    st.capabilities = LINK_CAP_SERVO_PWM;
    ui_router_set_status(&st);
    servo_cmd_t junk;
    while (servo_screen_take(&junk)) { }
}

/* The same on an armed bench, with nothing waiting to be taken: a position
 * input is taken only there. */
static inline void fresh_armed(void)
{
    fresh();
    servo_screen_set_armed(true);
    servo_cmd_t junk;
    while (servo_screen_take(&junk)) { }
}

static inline void ev(int x, int y, touch_event_type_t t, uint8_t id)
{
    const touch_event_t e = { .type = t,
                              .point = { .id = id, .x = (int16_t)x,
                                         .y = (int16_t)y, .strength = 40 } };
    scr->event(&e);
}
static inline void tap(int x, int y) { ev(x, y, TOUCH_EVENT_DOWN, 1);
                                       ev(x, y, TOUCH_EVENT_UP, 1); }

/*
 * The workbench, from servo_spots.h: the strip's tabs, SETTINGS' page tabs
 * and rows (left column at x 18, right at x 323, 56 px apart from y 112),
 * TEST's run cards and a run's settings' rows (three columns, from y 116);
 * then the panels over the left column, mirrored from
 * servo_settings_panel.c: the list a row opens (two columns of five, 56 px
 * apart from y 64) and the warning's HOLD TO APPLY and CANCEL.
 */
#define TAB_X(i) SV_PAGE_X(i)
#define TAB_Y    SV_PAGE_TAB_Y
#define ROW_L_X  100
#define ROW_R_X  330
#define ROW_Y(r) SV_ROW_Y(r)
#define LROW_X(c) SV_LIST_X(c)
#define LROW_Y(r) SV_LIST_ROW_Y(r)
#define TRIM_DN_X 502
#define TRIM_UP_X 594
/* OV_X, OV_Y, OV_W and OV_H of servo_screen_int.h: the left column. */
#define OV_X0    UI_MARGIN
#define OV_Y0    UI_MARGIN
#define OV_W0    620
#define OV_H0    (UI_BODY_H - 2 * UI_MARGIN)
#define CH_X(i)  (165 + 305 * ((i) / 5))
#define CH_Y(i)  (OV_Y0 + 56 + 24 + 56 * ((i) % 5))
#define CH_CANCEL_X (OV_X0 + OV_W0 - 10 - 80)
#define CH_CANCEL_Y (UI_ALERT_Y - UI_BAND_H - 4 - 56 + 8 + 24)
#define WARN_APPLY_X  (OV_X0 + 20 + 120)
#define WARN_CANCEL_X (OV_X0 + OV_W0 - 20 - 90)
#define WARN_Y        (UI_ALERT_Y - UI_BAND_H - 4 - 56 + 28)
/* SETTINGS' pages. */
#define PAGE_OUTPUT 0
#define PAGE_SWEEP  1
#define PAGE_DUT    2

/* The strip: SETTINGS, LIVE, TEST. */
static inline void open_settings(void)  { tap(SV_TAB_SETTINGS_X, SV_TAB_Y); }
static inline void close_settings(void) { tap(SV_TAB_LIVE_X, SV_TAB_Y); }
static inline void open_test(void)      { tap(SV_TAB_TEST_X, SV_TAB_Y); }
static inline void open_program(void)   { tap(SV_TAB_PROGRAM_X, SV_TAB_Y); }
/* PROGRAM's KST and Chaservo row, and the session's CLOSE. */
static inline void open_kst(void)
{
    open_program();
    tap(SV_PROG_ROW_X, SV_PROG_ROW_Y(0));
}
static inline void close_kst(void)
{
    const gfx_rect_t r = kst_panel_close_rect();
    tap(r.x + r.w / 2, r.y + r.h / 2);
}
/* A run card's SETTINGS on TEST: the run's settings, one level down. */
static inline void open_run_settings(int k)
{
    open_test();
    tap(SV_RUN_SET_X, SV_RUN_SET_Y(k));
}
/* The home tag, as the router asks the screen: whether it climbed. */
static inline bool home_tag(void) { return scr->back != NULL && scr->back(); }

/* Hold the warning's HOLD TO APPLY for @p s seconds and let go. */
static inline void hold_apply(float secs)
{
    ev(WARN_APPLY_X, WARN_Y, TOUCH_EVENT_DOWN, 1);
    for (int i = 0; i < (int)(secs * 39.0f + 0.5f); ++i) {
        scr->tick(1.0f / 39.0f);
    }
    ev(WARN_APPLY_X, WARN_Y, TOUCH_EVENT_UP, 1);
}

/* A type chosen from the list on OUTPUT: STANDARD PWM 0, NARROW 760 1,
 * WIDE 2, HELI CYCLIC 3, HELI TAIL 760 4.  A heli type is held through its
 * warning.  The overlay is closed again after. */
static inline void choose_type(int t)
{
    open_settings();
    tap(ROW_L_X, ROW_Y(0));
    tap(CH_X(t), CH_Y(t));
    if (t >= 3) {
        hold_apply(2.3f);
    }
    close_settings();
}

/* A key of the keypad, where the screen opens it over the left card. */
static inline void key(ui_key_t k)
{
    ui_keypad_t probe;
    memset(&probe, 0, sizeof(probe));
    ui_keypad_open(&probe, SV_OV_AREA, "",
                   "", 0.0f, 0.0f, 1.0f, 2);
    const gfx_rect_t r = ui_keypad_key_rect(&probe, k);
    tap(r.x + r.w / 2, r.y + r.h / 2);
}

static inline void keys(const char *text)
{
    static const ui_key_t digit[10] = {
        UI_KEY_0, UI_KEY_1, UI_KEY_2, UI_KEY_3, UI_KEY_4,
        UI_KEY_5, UI_KEY_6, UI_KEY_7, UI_KEY_8, UI_KEY_9,
    };
    for (const char *p = text; *p != '\0'; ++p) {
        key((*p == '.') ? UI_KEY_DOT : digit[*p - '0']);
    }
    key(UI_KEY_OK);
}

/* A point on the dial at the given servo angle. */
static inline void dial_at(float deg, int r, int *x, int *y)
{
    const float k = 3.14159265358979f / 180.0f;
    *x = SHAFT_X + (int)((float)r * cosf(deg * k) + 0.5f);
    *y = SHAFT_Y - (int)((float)r * sinf(deg * k) + 0.5f);
}

/* The sweep command last taken, as the panel would give it back with the
 * acknowledgement of its write. */
static uint16_t g_sweep_seq;
/* And the pause last taken, as the panel gives it back when it lets the
 * pause go. */
static uint16_t g_hold_seq;

static inline servo_cmd_t last_cmd(void)
{
    servo_cmd_t c = { .kind = SERVO_CMD_NONE };
    servo_screen_take(&c);
    if (c.kind == SERVO_CMD_SWEEP) {
        g_sweep_seq = c.start_seq;
    }
    if (c.kind == SERVO_CMD_HOLD) {
        g_hold_seq = c.pause_seq;
    }
    return c;
}

/* The acknowledgement of the sweep command last taken. */
static inline void acked(uint32_t age_ms, servo_sweep_from_t from,
                         uint32_t since_ms)
{
    servo_screen_sweep_started(g_sweep_seq, age_ms, from, since_ms);
}

/* The ARM button, mirrored from servo_screen.c: full width of the right
 * card's inner column, under CENTRE and RELEASE. */
/* The SPEED track, mirrored from the screen: x, w and y of the slider. */
#define SPEED_X_HALF (SV_RC_X + SV_RC_W / 2)
#define SPEED_Y SV_SPEED_Y
#define ARM_X SV_RC_X
#define ARM_W SV_RC_W
#define ARM_Y SV_ARM_Y
#define ARM_H SV_ARM_H
static inline void arm_press(void)   { ev(ARM_X + 40, ARM_Y + 16, TOUCH_EVENT_DOWN, 1); }
static inline void arm_release(void) { ev(ARM_X + 40, ARM_Y + 16, TOUCH_EVENT_UP, 1); }
static inline void hold_for(float s_total)
{
    /* The frame rate the panel runs at, so the hold is fed the way it is fed
     * on the bench rather than in one jump. */
    for (int i = 0; i < (int)(s_total * 39.0f + 0.5f); ++i) {
        scr->tick(1.0f / 39.0f);
    }
}

/* A second buffer, and a full redraw to hold the first against. */
static gfx_color_t *fb1, *ref;
static gfx_canvas_t cv1, cref;
#define FB_BYTES ((size_t)W * H * sizeof(gfx_color_t))

static inline void two_buffers(void)
{
    if (fb1 == NULL) {
        fb1 = calloc((size_t)W * H, sizeof(gfx_color_t));
        ref = calloc((size_t)W * H, sizeof(gfx_color_t));
    }
    gfx_canvas_init(&cv1, fb1, W, H, W);
    gfx_canvas_init(&cref, ref, W, H, W);
    scr->render(&cv, 0);
    scr->render(&cv1, 1);
}

/* Both buffers drawn once more, each then held against one drawn whole. */
static inline bool both_whole(void)
{
    scr->render(&cv, 0);
    scr->render(&cv1, 1);
    const bool same = memcmp(fb, fb1, FB_BYTES) == 0;
    servo_invalidate();
    scr->render(&cref, 0);
    return same && memcmp(fb, ref, FB_BYTES) == 0;
}

/* The SET row on the right card: the voltage, the current limit, and the
 * output switch, in the body's coordinates. */
#define SUP_ROW_Y   SV_SUP_Y
#define SUP_V_X     SV_SUP_V_X
#define SUP_I_X     SV_SUP_I_X
#define SUP_OUT_X   SV_SUP_OUT_X

static inline bool supply_cmd(supply_cmd_t *out)
{
    supply_cmd_t junk;
    return supply_screen_poll_cmd(out != NULL ? out : &junk);
}
