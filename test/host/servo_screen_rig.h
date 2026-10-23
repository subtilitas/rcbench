/**
 * @file servo_screen_rig.h
 * @brief The SERVO screen's cases as test_servo.c and test_servo_*.c share them:
 * the helpers that drive a sweep, a pause and the pins of the target row,
 * on top of servo_rig.h and servo_bench_rig.h.
 *
 * Only static inline helpers and the buffers and counters they use.
 *
 * Used by:
 * - test_servo.c
 * - test_servo_knob.c
 * - test_servo_dial_sweep.c
 * - test_servo_letgo.c
 * - test_servo_curve.c
 * - test_servo_supply.c
 * - test_servo_run.c
 * - test_servo_target.c
 * - test_servo_rail.c
 * - test_servo_meter.c
 * - test_servo_drag.c
 * - test_servo_repaint.c
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

#define SWEEP_X SV_SWEEP_X
#define BTN_Y   SV_BTN_Y

/** @brief Ticks the screen for @p secs seconds at the panel's 39 Hz frame rate. */
static inline void frames(float secs)
{
    for (int i = 0; i < (int)(secs * 39.0f + 0.5f); ++i) {
        scr->tick(1.0f / 39.0f);
    }
}

/** @brief SWEEP as the panel takes it, and the far end's start acknowledged now:
 * until then the curve is not drawn. */
static inline void sweep_go(void)
{
    tap(SWEEP_X, BTN_Y);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_SWEEP);
    acked(0u, SERVO_SWEEP_FROM_REST, 0u);
}

/** @brief PAUSE as the panel takes it: the HOLD, for its acknowledgement. */
static inline servo_cmd_t pause_go(void)
{
    tap(SWEEP_X, BTN_Y);
    const servo_cmd_t c = last_cmd();
    CHECK_EQ(c.kind, SERVO_CMD_HOLD);
    return c;
}

/**
 * @brief The panel's acknowledgement of @p hold, now: the phase the far end keeps
 * is the curve's now, the screen's own timing of the curve standing in for
 * the panel's, which the tests start in step with it.
 */
static inline void ack_hold(const servo_cmd_t *hold)
{
    servo_screen_sweep_held(hold->pause_seq, servo_screen_curve_ms());
}

/** @brief The panel's acknowledgement of a RESUME, @p ago_ms ago, of a far end that
 * kept @p kept_ms of its curve: the age of its phase 0, and of the resume. */
static inline void ack_resume(uint32_t kept_ms, uint32_t ago_ms)
{
    acked(kept_ms + ago_ms, SERVO_SWEEP_RESUMED, ago_ms);
}

/** @brief Pixels of @p col across the middle of the sweep button. */
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
 * The TARGET row of the output card, as the pixels of its box: the row at
 * y 308 to 356, between the SPEED slider, whose thumb and its shadow paint
 * down to y 303, and CENTRE (from y 366); the label at the output card's
 * column, SV_RC_X, and the value box 64 px further.
 */
#define TGT_X SV_RC_X
#define TGT_W SV_RC_W
#define TGT_VAL_DX UI_TARGET_LABEL_W

/* ------------------------------------------- the servo rail's meter */

/*
 * Mirrored from servo_screen_draw.c and servo_screen_int.h: the output
 * card's column, the line over the power plot with its 90 px name (PD
 * MINI) or 134 px name (INA 0x40 CH1), the line and plot together
 * (power_rect()), and LIVE's CURRENT reading, the numerals of the third
 * column along the foot of the left card.
 */
#define RAIL_X     SV_RC_X
#define RAIL_W     SV_RC_W
#define CUR_X      (UI_MARGIN + 12 + 2 * 198)
#define CUR_Y      476
#define CUR_W      194
#define CUR_H      44
#define LINE_Y     72
#define LINE_H     20
#define PWR_BOX_Y  70
#define PWR_BOX_H  96
#define LAB_SUPPLY 90
#define LAB_INA    134

/** @brief A reading's numerals as the screen draws them: in the 32 x 44
 *  numerals, or the 20 x 36 face, or the 10 x 20 one, the largest in which
 *  they fit the column. */
static inline void hero_want(const char *t, gfx_color_t col)
{
    const gfx_font_t *f = &gfx_font_num_32x44;
    if (gfx_text_width(f, t, 1) > CUR_W) {
        f = &gfx_font_20x36;
    }
    if (gfx_text_width(f, t, 1) > CUR_W) {
        f = &gfx_font_10x20;
    }
    gfx_text_in(&want_cv, (gfx_rect_t){ CUR_X, CUR_Y, CUR_W, CUR_H }, t, f,
                col, 1, GFX_ALIGN_LEFT);
}

/** @brief Buffer 0 drawn, and whether LIVE's CURRENT reading shows
 *  @p text, its unit left off, in @p col. */
static inline bool row_reads(const char *text, ui_color_id_t col)
{
    scr->render(&cv, 0);
    want_fresh();
    gfx_fill_rect(&want_cv, CUR_X, CUR_Y, CUR_W, CUR_H,
                  ui_theme_color(UI_C_PANEL));
    char num[24];
    snprintf(num, sizeof(num), "%s", text);
    char *sp = strchr(num, ' ');
    if (sp != NULL) {
        *sp = '\0';
    }
    hero_want(num, ui_theme_color(col));
    return same_in(fb, want_fb, CUR_X, CUR_Y, CUR_W, CUR_H);
}

/** @brief Buffer 0 drawn, and whether the line over the plot reads the name
 * @p label, @p lab_w px wide, and the three readings, the current in
 * @p a_col. */
static inline bool line_reads(const char *label, int lab_w, const char *v,
                       const char *a, ui_color_id_t a_col, const char *w)
{
    scr->render(&cv, 0);
    want_fresh();
    gfx_fill_rect(&want_cv, RAIL_X, LINE_Y, RAIL_W, LINE_H,
                  ui_theme_color(UI_C_PANEL));
    gfx_text(&want_cv, RAIL_X, LINE_Y, label, &gfx_font_10x20,
             ui_theme_color(UI_C_TEXT_DIM), 1);
    const char *txt[3] = { v, a, w };
    const ui_color_id_t col[3] = { UI_C_VOLT, a_col, UI_C_POWER };
    /* 88, 88 and 90 px beside PD MINI; 64, 84 and 74 px beside a channel. */
    static const int k_fw[2][3] = { { 88, 88, 90 }, { 64, 84, 74 } };
    const int *fw = k_fw[lab_w == LAB_INA ? 1 : 0];
    int x = RAIL_X + lab_w;
    bool fits = true;
    for (int k = 0; k < 3; ++k) {
        /* A reading wider than its field is cut at the left, and a cut
         * minus sign is a wrong reading. */
        fits = fits && (int)strlen(txt[k]) * 10 <= fw[k];
        gfx_text_in(&want_cv, (gfx_rect_t){ (int16_t)x, LINE_Y,
                                            (int16_t)fw[k], 20 },
                    txt[k], &gfx_font_10x20, ui_theme_color(col[k]), 1,
                    GFX_ALIGN_RIGHT);
        x += fw[k];
    }
    /* The name and the readings take the column's 356 px. */
    return fits && x == RAIL_X + RAIL_W
           && same_in(fb, want_fb, RAIL_X, LINE_Y, RAIL_W, LINE_H);
}

/** @brief True when the rail line reads the PD mini's voltage, current and power as given. */
static inline bool line_supply(const char *v, const char *a, const char *w)
{
    return line_reads("PD MINI", LAB_SUPPLY, v, a, UI_C_CURR, w);
}

/** @brief The meter's name over the plot: the channel watching the tester's
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

/** @brief True when the rail line reads the INA3221 window's voltage, current and power as given. */
static inline bool line_ina(const char *v, const char *a, const char *w)
{
    return line_reads(ina_label(), LAB_INA, v, a, UI_C_CURR, w);
}

/** @brief A supply sample that answers with both readings. */
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

/** @brief Hands the screen the INA3221 CH1 window of @p ma mA at @p mv mV. */
static inline void rail_window(int ma, unsigned mv)
{
    const sense_link_win_t w = rail_win(ma, mv);
    window(&w);
}
