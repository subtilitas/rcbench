/*
 * The SERVO screen's cases as test_servo.c and test_servo_*.c share them:
 * the helpers that drive a sweep, a pause and the pins of the target row,
 * on top of servo_rig.h and servo_bench_rig.h.
 *
 * Only static inline helpers and the buffers and counters they use.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"
#include "touch_feed.h"

#include "outputs_screen.h"
#include "servo_screen.h"
#include "servo_sim.h"
#include "servo_sweep.h"
#include "settings.h"
#include "supply_screen.h"
#include "ui_keypad.h"
#include "ui_screen.h"
#include "ui_target_row.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "outputs.h"

#include "servo_bench_rig.h"
#include "servo_rig.h"

/* ------------------------------------------------------------------ sweep */

#define SWEEP_X (ARM_X + ARM_W / 2)
#define BTN_Y   (350 + 16)

static inline void frames(float secs)
{
    for (int i = 0; i < (int)(secs * 39.0f + 0.5f); ++i) {
        scr->tick(1.0f / 39.0f);
    }
}

/* SWEEP as the panel takes it, and the far end's start acknowledged now:
 * until then the curve is not drawn. */
static inline void sweep_go(void)
{
    tap(SWEEP_X, BTN_Y);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_SWEEP);
    acked(0u, SERVO_SWEEP_FROM_REST, 0u);
}

/* PAUSE as the panel takes it: the HOLD, for its acknowledgement. */
static inline servo_cmd_t pause_go(void)
{
    tap(SWEEP_X, BTN_Y);
    const servo_cmd_t c = last_cmd();
    CHECK_EQ(c.kind, SERVO_CMD_HOLD);
    return c;
}

/*
 * The panel's acknowledgement of @p hold, now: the phase the far end keeps
 * is the curve's now, the screen's own timing of the curve standing in for
 * the panel's, which the tests start in step with it.
 */
static inline void ack_hold(const servo_cmd_t *hold)
{
    servo_screen_sweep_held(hold->pause_seq, servo_screen_curve_ms());
}

/* The panel's acknowledgement of a RESUME, @p ago_ms ago, of a far end that
 * kept @p kept_ms of its curve: the age of its phase 0, and of the resume. */
static inline void ack_resume(uint32_t kept_ms, uint32_t ago_ms)
{
    acked(kept_ms + ago_ms, SERVO_SWEEP_RESUMED, ago_ms);
}

/* Pixels of @p col across the middle of the sweep button. */
static inline int sweep_btn_pixels(gfx_color_t col)
{
    int n = 0;
    for (int y = BTN_Y - 12; y < BTN_Y + 12; ++y) {
        for (int x = SWEEP_X - 40; x < SWEEP_X + 40; ++x) {
            n += (fb[(size_t)y * W + x] == col) ? 1 : 0;
        }
    }
    return n;
}

/*
 * The TARGET row of the right card, as the pixels of its box: the row at
 * y 326 to 348, between the SPEED slider, whose thumb and its shadow paint
 * down to y 324, and CENTRE (from y 350); the label at x 514 and the value
 * box from x 578.
 */
#define TGT_X 514
#define TGT_W 268
#define TGT_VAL_DX UI_TARGET_LABEL_W

/* ------------------------------------------- the servo rail's meter */

/*
 * Mirrored from servo_screen.c and servo_screen_int.h: the right card's
 * column, the CURRENT row, the line over the power plot with its 64 px name
 * (PD MINI) or 88 px name (INA3221 CH1), and the line and plot together
 * (power_rect()).
 */
#define RAIL_X     514
#define RAIL_W     268
#define CUR_ROW_Y  92
#define CUR_ROW_H  24
#define LINE_Y     150
#define LINE_H     16
#define PWR_BOX_Y  148
#define PWR_BOX_H  70
#define LAB_SUPPLY 64
#define LAB_INA    96

/* Buffer 0 drawn, and whether its CURRENT row reads @p text in @p col. */
static inline bool row_reads(const char *text, ui_color_id_t col)
{
    scr->render(&cv, 0);
    want_fresh();
    gfx_fill_rect(&want_cv, RAIL_X, CUR_ROW_Y, RAIL_W, CUR_ROW_H,
                  ui_theme_color(UI_C_PANEL));
    gfx_text(&want_cv, RAIL_X, CUR_ROW_Y + 5, "CURRENT", UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_DIM), 1);
    gfx_text_in(&want_cv, (gfx_rect_t){ RAIL_X + 80, CUR_ROW_Y + 5,
                                        RAIL_W - 80, 16 },
                text, UI_FONT_LABEL, ui_theme_color(col), 1, GFX_ALIGN_RIGHT);
    return same_in(fb, want_fb, RAIL_X, CUR_ROW_Y, RAIL_W, CUR_ROW_H);
}

/* Buffer 0 drawn, and whether the line over the plot reads the name
 * @p label, @p lab_w px wide, and the three readings, the current in
 * @p a_col. */
static inline bool line_reads(const char *label, int lab_w, const char *v,
                       const char *a, ui_color_id_t a_col, const char *w)
{
    scr->render(&cv, 0);
    want_fresh();
    gfx_fill_rect(&want_cv, RAIL_X, LINE_Y, RAIL_W, LINE_H,
                  ui_theme_color(UI_C_PANEL));
    gfx_text(&want_cv, RAIL_X, LINE_Y, label, UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_DIM), 1);
    const char *txt[3] = { v, a, w };
    const ui_color_id_t col[3] = { UI_C_VOLT, a_col, UI_C_POWER };
    /* 68 px each beside PD MINI; 52, 64 and 56 px beside a channel. */
    static const int k_fw[2][3] = { { 68, 68, 68 }, { 52, 64, 56 } };
    const int *fw = k_fw[lab_w == LAB_INA ? 1 : 0];
    int x = RAIL_X + lab_w;
    bool fits = true;
    for (int k = 0; k < 3; ++k) {
        /* A reading wider than its field is cut at the left, and a cut
         * minus sign is a wrong reading. */
        fits = fits && (int)strlen(txt[k]) * 8 <= fw[k];
        gfx_text_in(&want_cv, (gfx_rect_t){ (int16_t)x, LINE_Y,
                                            (int16_t)fw[k], 16 },
                    txt[k], UI_FONT_LABEL, ui_theme_color(col[k]), 1,
                    GFX_ALIGN_RIGHT);
        x += fw[k];
    }
    return fits && x == RAIL_X + RAIL_W
           && same_in(fb, want_fb, RAIL_X, LINE_Y, RAIL_W, LINE_H);
}

static inline bool line_supply(const char *v, const char *a, const char *w)
{
    return line_reads("PD MINI", LAB_SUPPLY, v, a, UI_C_CURR, w);
}

/* The meter's name over the plot: the channel watching the tester's
 * target, "INA 0x40 CH1", or INA3221 with none fitted. */
static inline const char *ina_label(void)
{
    static char name[16];
    const uint8_t f = settings_sensors_fast();
    if (f >= SET_INA_CHANNELS) {
        return "INA3221";
    }
    snprintf(name, sizeof(name), "INA 0x%02X CH%u", 0x40u + f / 3u,
             f % 3u + 1u);
    return name;
}

static inline bool line_ina(const char *v, const char *a, const char *w)
{
    return line_reads(ina_label(), LAB_INA, v, a, UI_C_CURR, w);
}

/* A supply sample that answers with both readings. */
static inline void rail_supply(float v, float i)
{
    supply_state_t st;
    memset(&st, 0, sizeof(st));
    st.online = true;
    st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
    st.v = v;
    st.i = i;
    st.p = v * i;
    servo_screen_supply(&st);
}

static inline void rail_window(int ma, unsigned mv)
{
    const sense_link_win_t w = rail_win(ma, mv);
    window(&w);
}
