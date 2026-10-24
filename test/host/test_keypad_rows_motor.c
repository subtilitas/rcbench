/**
 * @file test_keypad_rows_motor.c
 * @brief Every row of MOTOR & ESC > SETTINGS held to keypad_rows_rig.h's
 *        four rules on a SAFE bench: a tap on its value box opens the
 *        keypad with the row's label, unit, range and its step's digits; a
 *        value past the range is refused and the row keeps its value;
 *        CANCEL keeps it; OK sets the value typed.  The rows are every
 *        number of the schema's ESC / BENCH category.
 *
 * Driven through the router, in panel coordinates, as the panel drives it.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "touch_feed.h"

#include "gfx.h"
#include "keypad_rows_rig.h"
#include "motor_screen.h"
#include "settings.h"
#include "splash_screen.h"
#include "ui_band.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "motor_spots.h"

#define W UI_PANEL_W
#define H UI_PANEL_H
#define P(y) (UI_BAND_H + (y))

static gfx_color_t *fb;
static gfx_canvas_t cv;

/** @brief SETTINGS' rows, in their order. */
static setting_id_t s_ids[16];

/** @brief MOTOR & ESC on SETTINGS, opened from its tile on a SAFE bench
 *         with the settings at their defaults and no store. */
static void fresh(void)
{
    if (fb == NULL) {
        fb = calloc((size_t)W * H, sizeof(gfx_color_t));
        REQUIRE_ALLOC(fb);
    }
    gfx_canvas_init(&cv, fb, W, H, W);
    ui_theme_set(UI_THEME_DARK);
    ui_text_set_language(UI_LANG_EN);
    settings_set_store(NULL);
    settings_init();
    ui_router_init();
    for (int i = 0; i < SPLASH_STEP_COUNT; ++i) {
        splash_screen_set((splash_step_t)i, SPLASH_OK, "");
    }
    ui_router_tick(2.0f);
    ui_router_goto(SCREEN_OVERVIEW);
    ui_router_goto(SCREEN_MOTOR);
    feed_reset();
    motor_screen_set_armed(false);
    ui_router_tick(0.026f);
    ui_router_render(&cv, 0);
    feed_tap(FEED_LONE, MO_TAB_X(2), P(MO_TAB_Y));     /* SETTINGS */
}

/** @brief The keypad SETTINGS holds. */
static const ui_keypad_t *kp(void) { return motor_screen_keypad(); }

/**
 * @brief A key of SETTINGS' keypad tapped, in panel coordinates.
 * @param k The key.
 */
static void kkey(ui_key_t k)
{
    const gfx_rect_t r = ui_keypad_key_rect(motor_screen_keypad(), k);
    feed_tap(FEED_LONE, r.x + r.w / 2, P(r.y + r.h / 2));
}

/**
 * @brief The keypad of SETTINGS' row @p row, by a tap on its value box.
 * @param row The row.
 */
static void open_row(int row)
{
    const gfx_rect_t b = motor_settings_box(row);
    feed_tap(FEED_LONE, b.x + b.w / 2, P(b.y + b.h / 2));
}

/** @brief SETTINGS' keypad as keypad_rows_rig.h reaches it. */
static const kpr_screen_t k_settings = { kp, kkey, open_row };

/*
 * The rows are the ESC / BENCH category's numbers, each once; every one
 * opens its keypad with its label, unit, range and digits, refuses a value
 * past the range, keeps the row on CANCEL and sets the value OK hands it.
 */
TEST_CASE(every_settings_row_takes_ok_and_refuses_and_cancels)
{
    fresh();
    const int rows = motor_settings_rows(s_ids, 16);
    setting_id_t cat[64];
    const int n = settings_in_category(SET_CAT_ESC, cat, 64);
    int numbers = 0;
    for (int i = 0; i < n; ++i) {
        const setting_type_t t = settings_def(cat[i])->type;
        if (t != SET_TYPE_INT && t != SET_TYPE_FLOAT) {
            continue;
        }
        ++numbers;
        int seen = 0;
        for (int r = 0; r < rows; ++r) {
            seen += (s_ids[r] == cat[i]) ? 1 : 0;
        }
        CHECK_EQ(seen, 1);
    }
    CHECK_EQ(numbers, rows);
    CHECK_EQ(rows, 8);

    for (int r = 0; r < rows; ++r) {
        fresh();
        const kpr_row_t e = kpr_setting(s_ids[r], NULL);
        kpr_row(&k_settings, r, &e);
        CHECK_NEAR(settings_get(s_ids[r]), e.ok, 1e-4f);
    }
}

/*
 * Idle pulse and Full pulse take a keypad on a SAFE bench only: ARMED,
 * a tap on either value box opens nothing, and the value stays.
 */
TEST_CASE(the_pulse_rows_open_no_keypad_while_armed)
{
    int pulses = 0;
    for (int r = 0; r < motor_settings_rows(NULL, 0); ++r) {
        fresh();
        (void)motor_settings_rows(s_ids, 16);
        if (s_ids[r] != SET_OUT_MIN_US && s_ids[r] != SET_OUT_MAX_US) {
            continue;
        }
        motor_screen_set_armed(true);
        ui_router_tick(0.026f);
        const float was = settings_get(s_ids[r]);
        open_row(r);
        CHECK(!kp()->open);
        CHECK_NEAR(settings_get(s_ids[r]), was, 1e-6f);
        ++pulses;
    }
    CHECK_EQ(pulses, 2);
}

int main(void)
{
    RUN(every_settings_row_takes_ok_and_refuses_and_cancels);
    RUN(the_pulse_rows_open_no_keypad_while_armed);
    return test_summary("keypad_rows_motor");
}
