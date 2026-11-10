/*
 * The SERVO screens as the servo suites drive them: a function's screen on
 * one canvas in the body's coordinates, a touch handed to its event hook,
 * and the places of the dial, the lists, ARM and SPEED and the supply's
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
#include "ui_setlist.h"
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

/* The function screen fresh() opens: LIVE, or SWEEP for the suites of
 * the sweep, which define it before they include the rig. */
#ifndef SERVO_RIG_FN
#define SERVO_RIG_FN SERVO_FN_LIVE
#endif

static gfx_color_t *fb;
static gfx_canvas_t cv;
static const ui_screen_t *scr;

/* The servo rail's meter as the panel hands it over: the meter, and the
 * count of its changes (servo_source_changes()).  The count is read only
 * by servo_bench_rig.h's meter() and window(). */
static servo_source_id_t meter_id = SERVO_SOURCE_MODEL;
static uint32_t meter_changes __attribute__((unused));
/* Whether open_run_settings() took the bench to SAFE for the page, and
 * the home tag that closes it arms it again. */
static bool g_rig_rearm __attribute__((unused));

static inline void fresh(void)
{
    meter_id = SERVO_SOURCE_MODEL;
    g_rig_rearm = false;
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
    scr = servo_fn_screen(SERVO_RIG_FN);
    scr->reset();
    servo_screen_show_fn(SERVO_RIG_FN);
    /* An IOmcu that reports its servo PWM bank: the KST tile is
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
 * The function screens, from servo_spots.h: the run's settings' rows (three
 * columns, 70px apart from y 64); then the panels over the left column,
 * mirrored from servo_settings_panel.c: the list a row opens (two columns
 * of five, 56px apart from y 64) and the warning's HOLD TO APPLY and
 * CANCEL, at the left column's floor at y 436.
 */
#define LROW_X(c) SV_LIST_X(c)
#define LROW_Y(r) SV_LIST_ROW_Y(r)
/* OV_X, OV_Y, OV_W and OV_H of servo_screen_int.h: the left column. */
#define OV_X0    UI_MARGIN
#define OV_Y0    UI_MARGIN
#define OV_W0    620
#define OV_H0    (436 - UI_MARGIN)
#define CH_X(i)  (165 + 305 * ((i) / 5))
#define CH_Y(i)  (OV_Y0 + 56 + 24 + 56 * ((i) % 5))
#define CH_CANCEL_X (OV_X0 + OV_W0 - 10 - 80)
#define CH_CANCEL_Y (436 - 8 - 56 + 8 + 24)
#define WARN_APPLY_X  (OV_X0 + 20 + 120)
#define WARN_CANCEL_X (OV_X0 + OV_W0 - 20 - 90)
#define WARN_Y        (436 - 8 - 56 + 28)

/* The hub's list, row by row: the profile under OUTPUT, then the device
 * under test. */
enum {
    HL_OUTPUT = 0, HL_TYPE, HL_RATE, HL_MIN, HL_CENTER, HL_MAX, HL_TRIM,
    HL_TRAVEL, HL_REVERSE, HL_HV, HL_DUT, HL_NAME, HL_REPORT,
};

/* Function @p fn's screen shown, as its enter() shows it, without a leave
 * of the one before: the bench, a run and the session stay. */
static inline void go(servo_fn_t fn)
{
    servo_screen_show_fn(fn);
    scr = servo_fn_screen(fn);
}

/* The footer's SETTINGS: the function's list or run page. */
static inline void tap_settings(void) { tap(SV_FOOT_SET_X, SV_FOOT_Y); }
/* The hub's SETTINGS list, the servo profile. */
static inline void open_settings(void)  { go(SERVO_FN_HUB); tap_settings(); }
static inline void close_settings(void) { go(SERVO_RIG_FN); }
static inline void open_test(void)      { go(SERVO_FN_TEST); }
static inline void open_limrun(void)    { go(SERVO_FN_LIMRUN); }
static inline void open_sweep(void)     { go(SERVO_FN_SWEEP); }
/* KST's session, and the home tag over it: the session's way back. */
static inline void open_kst(void)       { go(SERVO_FN_KST); }
static inline void close_kst(void)      { (void)scr->back(); }
/* TEST's SETTINGS list (servo_test_list.c), row by row: six headings
 * and the automatic test's 21 rows. */
enum {
    TL_H_STEPS = 0, TL_STEP_48, TL_STEP_60, TL_HV, TL_STEP_74, TL_STEP_84,
    TL_H_BO, TL_BROWNOUT, TL_BO_FLOOR, TL_BO_STEP,
    TL_H_LIMITS, TL_STALL, TL_IDLE, TL_HOLD, TL_TRAVEL,
    TL_H_LENGTH, TL_LEN_BY, TL_LEN_S, TL_MOVES, TL_DWELL, TL_SETTLE,
    TL_H_RANGE, TL_RANGE, TL_OUTPUT, TL_H_DUT, TL_NAME, TL_REPORT, TL_ROWS,
};


/* LIMIT RUN's list, row by row (servo_lists.c).  With SERVO at HITEC D
 * SERIES, D RECEIVE is row LL_RX and the rows from SERVO PULSES on move
 * down one. */
enum { LL_MODE = 0, LL_SERVO, LL_PULSES, LL_CURRENT, LL_WAIT, LL_NAME,
       LL_LOG, LL_ROWS };
enum { LL_RX = 2 };
/* RUDDER AUTO's list: its SERVO row, D RECEIVE under it with HITEC D
 * SERIES; without D RECEIVE CURRENT and PROG WAIT, then DUT NAME and USER
 * LOGS last. */
enum { RAL_SERVO = 4, RAL_RX = 5, RAL_CURRENT = 7, RAL_WAIT = 8,
       RAL_NAME = 9, RAL_LOG = 10, RAL_ROWS = 11 };
/* SWEEP's list: CURVE, SPEED, RANGE and DWELL TIME. */
enum { SWL_CURVE = 0, SWL_SPEED, SWL_RANGE, SWL_DWELL, SWL_ROWS };

/* LIMIT RUN's list, from the footer's SETTINGS.  It opens on a SAFE bench
 * only: on an armed one the rig switches to SAFE first, and the home tag
 * that closes the list arms it again.  A save asked for first, so the home
 * tag closes the list without the leave question. */
static inline void open_limrun_list(void)
{
    settings_request_save();
    go(SERVO_FN_LIMRUN);
    tap_settings();
    if (!servo_screen_list()->open) {
        servo_screen_set_armed(false);
        g_rig_rearm = true;
        tap_settings();
    }
}

/* A run's settings, one level down from its screen: 0 TEST's SETTINGS
 * list, 1 LIMIT RUN's list.  Each opens on a SAFE bench only: on an armed
 * one the operator switches to SAFE first, and the home tag that closes it
 * arms the bench again. */
static inline void open_run_settings(int k)
{
    if (k != 0) {
        open_limrun_list();
        return;
    }
    go(SERVO_FN_TEST);
    tap_settings();
    if (!servo_screen_list()->open) {
        servo_screen_set_armed(false);
        g_rig_rearm = true;
        tap_settings();
    }
}
/* The home tag, as the router asks the screen: whether it climbed. */
static inline bool home_tag(void)
{
    const bool climbed = scr->back != NULL && scr->back();
    if (g_rig_rearm && servo_screen_run_settings() == 0
        && !servo_screen_list()->open) {
        g_rig_rearm = false;
        servo_screen_set_armed(true);
    }
    return climbed;
}

/* The open list's row @p i scrolled into view: its rectangle. */
static inline gfx_rect_t sl_row(int i)
{
    ui_setlist_t *sl = (ui_setlist_t *)servo_screen_list();
    ui_setlist_scroll_to(sl, i);
    return ui_setlist_row_rect(sl, i);
}
/* A tap on the open list's row @p i, on its name: its tap or its choice. */
static inline void sl_tap(int i)
{
    const gfx_rect_t r = sl_row(i);
    tap(r.x + 60, r.y + r.h / 2);
}
/* A tap on a part of the open list's row @p i: a key or the value. */
static inline void sl_part(int i, ui_sl_part_t part)
{
    (void)sl_row(i);
    const gfx_rect_t r = ui_setlist_row_part(servo_screen_list(), i, part);
    tap(r.x + r.w / 2, r.y + r.h / 2);
}
/* Row @p i's choice list opened by a tap and its entry @p k tapped. */
static inline void sl_choose(int i, int k)
{
    sl_tap(i);
    const gfx_rect_t r = ui_setlist_choice_rect(servo_screen_list(), k);
    tap(r.x + r.w / 2, r.y + r.h / 2);
}
/* Entry @p k of the one-column list over the left column: PORT's, or the
 * SERVO list a READY card's SERVO row opens. */
static inline void ov_choose(int k)
{
    const gfx_rect_t r = servo_screen_port_choice_rect(k);
    tap(r.x + r.w / 2, r.y + r.h / 2);
}
/* One step of the open list's row @p i, @p dir -1 or +1. */
static inline void sl_step(int i, int dir)
{
    sl_part(i, (dir < 0) ? UI_SL_PART_MINUS : UI_SL_PART_PLUS);
}

/* HV SERVO on TEST's list switched, and the list closed as a new showing
 * of TEST closes it, past the leave warning a change not saved would
 * raise: TEST's card shows again, the bench as it was. */
static inline void test_hv_on(void)
{
    open_run_settings(0);
    sl_step(TL_HV, +1);
    go(SERVO_FN_TEST);
    if (g_rig_rearm) {
        g_rig_rearm = false;
        servo_screen_set_armed(true);
    }
}

/* @p text typed on the open list's keypad, then OK. */
static inline void sl_keys(const char *text)
{
    static const ui_key_t digit[10] = {
        UI_KEY_0, UI_KEY_1, UI_KEY_2, UI_KEY_3, UI_KEY_4,
        UI_KEY_5, UI_KEY_6, UI_KEY_7, UI_KEY_8, UI_KEY_9,
    };
    const ui_keypad_t *k = &servo_screen_list()->kp;
    for (const char *p = text; *p != '\0'; ++p) {
        const gfx_rect_t r = ui_keypad_key_rect(
            k, (*p == '.') ? UI_KEY_DOT : digit[*p - '0']);
        tap(r.x + r.w / 2, r.y + r.h / 2);
    }
    const gfx_rect_t ok = ui_keypad_key_rect(k, UI_KEY_OK);
    tap(ok.x + ok.w / 2, ok.y + ok.h / 2);
}
/* Key @p k of the open list's keyboard. */
static inline void sl_text_key(int k)
{
    const gfx_rect_t r = ui_textkey_key_rect(&servo_screen_list()->tk, k);
    tap(r.x + r.w / 2, r.y + r.h / 2);
}

/* Hold the warning's HOLD TO APPLY for @p s seconds and let go. */
static inline void hold_apply(float secs)
{
    ev(WARN_APPLY_X, WARN_Y, TOUCH_EVENT_DOWN, 1);
    for (int i = 0; i < (int)(secs * 39.0f + 0.5f); ++i) {
        scr->tick(1.0f / 39.0f);
    }
    ev(WARN_APPLY_X, WARN_Y, TOUCH_EVENT_UP, 1);
}

/* A type chosen from the hub's list: STANDARD PWM 0, NARROW 760 1, WIDE 2,
 * HELI CYCLIC 3, HELI TAIL 760 4.  A heli type is held through its
 * warning.  LIVE shows again after. */
static inline void choose_type(int t)
{
    open_settings();
    sl_tap(HL_TYPE);
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

/* The output card's inner column: CENTER, SWEEP and RELEASE, SPEED. */
#define COL_X SV_RC_X
#define COL_W SV_RC_W
/* ARM and DISARM in the footer's middle slot. */
/* The SPEED track, mirrored from the screen: x, w and y of the slider. */
#define SPEED_X_HALF (SV_RC_X + SV_RC_W / 2)
#define SPEED_Y SV_SPEED_Y
#define ARM_X SV_ARM_L
#define ARM_W SV_ARM_W
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
