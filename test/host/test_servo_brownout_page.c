/*
 * The brown-out walk's page on the SERVO screen: opened from the AUTOMATIC
 * TEST's BROWN-OUT row, its three rows -- BROWN-OUT on or off, LOWEST
 * VOLTAGE and STEP -- and CLOSE and the home tag back to the AUTOMATIC
 * TEST's page; the row's value, "ON 3.0/0.2V" or "OFF"; the keypads'
 * bounds; the settings kept over a restart; and a run started from the
 * screen that walks what they say.
 *
 * The screen and its fixtures are servo_rig.h, the bench
 * servo_bench_rig.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "greatest.h"
#include "touch_feed.h"

#include "servo_screen.h"
#include "settings.h"
#include "supply_screen.h"
#include "ui_keypad.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

#include "servo_bench_rig.h"
#include "servo_rig.h"

/** @brief servo_screen_run_settings()'s values: none, the AUTOMATIC
 *         TEST's, the LIMIT RUN's, the brown-out walk's. */
#define RL_AUTO_PAGE     1
#define RL_BROWNOUT_PAGE 3

/** @brief A run page's row @p r of column @p c, its value: 193 x 48px under
 *         a 21px label, 71px apart from y 112, columns 203px apart from
 *         x 18 (servo_screen_int.h's RL_*). */
static gfx_rect_t value_at(int c, int r)
{
    return (gfx_rect_t){ (int16_t)(UI_MARGIN + 10 + 203 * c),
                         (int16_t)(112 + 71 * r + 21), 193, 48 };
}

/**
 * @brief Buffer 0 drawn, and whether the value of row @p r in column @p c
 *        reads @p text, as an untouched row draws it.
 * @param c    Column, 0 to 2.
 * @param r    Row, 0 to 4.
 * @param text The value expected.
 * @return true when the value's pixels match.
 */
static bool value_reads(int c, int r, const char *text)
{
    scr->render(&cv, 0);
    want_fresh();
    const gfx_rect_t v = value_at(c, r);
    gfx_fill_rect(&want_cv, v.x, v.y, v.w, v.h, ui_theme_color(UI_C_PANEL));
    ui_button_font(&want_cv, v, text, ui_theme_color(UI_C_PANEL_SUNK), false,
                   true, &gfx_font_10x20);
    return same_in(fb, want_fb, v.x, v.y, v.w, v.h);
}

/** @brief The AUTOMATIC TEST's BROWN-OUT row: the middle column's last. */
static void tap_brownout_row(void) { tap(LROW_X(1), LROW_Y(4)); }

/* ------------------------------------------------------- memory store */

static float s_saved[SETTING_COUNT];
static bool  s_has_saved;

/** @brief The memory store's load: as the panel's NVS (non-volatile
 *         storage) store, a key it does not hold -- NAN here -- leaves the
 *         default.  @param values Out. @param count Size.
 *  @return true once something was saved. */
static bool mem_load(float *values, int count)
{
    if (!s_has_saved) {
        return false;
    }
    for (int i = 0; i < count && i < SETTING_COUNT; ++i) {
        if (!isnan(s_saved[i])) {
            values[i] = s_saved[i];
        }
    }
    return true;
}

/** @brief The memory store's save. @param values In. @param count Size.
 *  @return true: it takes every value. */
static bool mem_save(const float *values, int count)
{
    for (int i = 0; i < count && i < SETTING_COUNT; ++i) {
        s_saved[i] = values[i];
    }
    s_has_saved = true;
    return true;
}

static const settings_store_t s_mem_store = { mem_load, mem_save, NULL, NULL };

/* ------------------------------------------------------------------ tests */

/* BROWN-OUT on the AUTOMATIC TEST's page opens the walk's page, one level
 * down; CLOSE and the home tag climb back to the AUTOMATIC TEST's page, and
 * from there to TEST's cards. */
TEST_CASE(the_brownout_row_opens_its_page_and_close_climbs_back)
{
    fresh();
    open_run_settings(0);
    CHECK_EQ(servo_screen_run_settings(), RL_AUTO_PAGE);
    const bool on = settings_get_bool(SET_SERVO_BROWNOUT);
    tap_brownout_row();
    CHECK_EQ(servo_screen_run_settings(), RL_BROWNOUT_PAGE);
    CHECK_EQ(settings_get_bool(SET_SERVO_BROWNOUT), on);  /* opened only */
    tap(SV_RUN_CLOSE_X, SV_RUN_CLOSE_Y);
    CHECK_EQ(servo_screen_run_settings(), RL_AUTO_PAGE);

    tap_brownout_row();
    CHECK(home_tag());
    CHECK_EQ(servo_screen_run_settings(), RL_AUTO_PAGE);
    CHECK(home_tag());
    CHECK_EQ(servo_screen_run_settings(), 0);
    CHECK_EQ(servo_screen_task(), (int)UI_WB_TEST);

    /* LIVE and back: TEST shows the walk's page again, as it does the
     * AUTOMATIC TEST's. */
    open_run_settings(0);
    tap_brownout_row();
    close_settings();
    open_test();
    CHECK_EQ(servo_screen_run_settings(), RL_BROWNOUT_PAGE);
}

/* The row says the walk: "ON 3.0/0.2V" at the defaults, the two settings
 * as the walk's page sets them, and "OFF" alone with the walk off. */
TEST_CASE(the_brownout_row_reads_on_with_floor_and_step_or_off)
{
    fresh();
    open_run_settings(0);
    CHECK(settings_get_bool(SET_SERVO_BROWNOUT));
    CHECK(value_reads(1, 4, "ON 3.0/0.2V"));
    tap_brownout_row();
    tap(LROW_X(0), LROW_Y(1));                     /* LOWEST VOLTAGE */
    keys("2");
    tap(LROW_X(0), LROW_Y(2));                     /* STEP */
    keys("1");
    tap(SV_RUN_CLOSE_X, SV_RUN_CLOSE_Y);
    CHECK(value_reads(1, 4, "ON 2.0/1.0V"));
    tap_brownout_row();
    tap(LROW_X(0), LROW_Y(0));                     /* BROWN-OUT: off */
    tap(SV_RUN_CLOSE_X, SV_RUN_CLOSE_Y);
    CHECK(value_reads(1, 4, "OFF"));
}

/* The page's rows: BROWN-OUT switches, LOWEST VOLTAGE and STEP open the
 * keypad with their titles, units and ranges; a value past the range is
 * refused and the setting keeps its value; OK sets the value typed. */
TEST_CASE(the_pages_rows_switch_and_take_their_ranges)
{
    fresh();
    open_run_settings(0);
    tap_brownout_row();
    tap(LROW_X(0), LROW_Y(0));                     /* BROWN-OUT */
    CHECK(!settings_get_bool(SET_SERVO_BROWNOUT));
    CHECK(settings_save_asked());
    tap(LROW_X(0), LROW_Y(0));
    CHECK(settings_get_bool(SET_SERVO_BROWNOUT));

    static const struct {
        int row;
        setting_id_t id;
        ui_text_id_t title;
        float lo, hi, typed;
    } k[] = {
        { 1, SET_SERVO_BO_FLOOR, TX_SV_ROW_BO_FLOOR, 2.0f, 4.8f, 4.8f },
        { 2, SET_SERVO_BO_STEP,  TX_SV_ROW_BO_STEP,  0.1f, 1.0f, 0.1f },
    };
    for (size_t j = 0; j < sizeof(k) / sizeof(k[0]); ++j) {
        const float was = settings_get(k[j].id);
        tap(LROW_X(0), LROW_Y(k[j].row));
        const ui_keypad_t *kp = servo_screen_keypad();
        CHECK(kp->open);
        CHECK_STR_EQ(kp->title, ui_tr(k[j].title));
        CHECK_STR_EQ(kp->unit, "V");
        CHECK_NEAR(kp->min, k[j].lo, 1e-6f);
        CHECK_NEAR(kp->max, k[j].hi, 1e-6f);
        /* Past the top: refused, the keypad stays and nothing is set. */
        keys(k[j].row == 1 ? "4.9" : "1.1");
        CHECK(kp->open);
        CHECK_NEAR(settings_get(k[j].id), was, 1e-6f);
        key(UI_KEY_CANCEL);
        CHECK(!kp->open);
        /* Under the bottom: refused too. */
        tap(LROW_X(0), LROW_Y(k[j].row));
        keys(k[j].row == 1 ? "1.9" : "0.05");
        CHECK(kp->open);
        key(UI_KEY_CANCEL);
        CHECK_NEAR(settings_get(k[j].id), was, 1e-6f);
        /* Each end is taken. */
        tap(LROW_X(0), LROW_Y(k[j].row));
        char text[8];
        snprintf(text, sizeof(text), "%.1f", (double)k[j].typed);
        keys(text);
        CHECK_NEAR(settings_get(k[j].id), k[j].typed, 1e-4f);
    }
    /* A value between the 0.1V steps lands on the nearer. */
    tap(LROW_X(0), LROW_Y(1));
    keys("3.04");
    CHECK_NEAR(settings_get(SET_SERVO_BO_FLOOR), 3.0f, 1e-4f);
}

/* LOWEST VOLTAGE and STEP are kept over a restart, and a stored value
 * outside the ranges comes back held to them; a store that holds neither
 * gives the defaults, 3.0V and 0.2V. */
TEST_CASE(floor_and_step_are_kept_over_a_restart)
{
    fresh();
    CHECK_NEAR(settings_get(SET_SERVO_BO_FLOOR), 3.0f, 1e-6f);
    CHECK_NEAR(settings_get(SET_SERVO_BO_STEP), 0.2f, 1e-6f);
    CHECK_STR_EQ(settings_def(SET_SERVO_BO_FLOOR)->key, "srv_bo_floor");
    CHECK_STR_EQ(settings_def(SET_SERVO_BO_STEP)->key, "srv_bo_step");

    memset(s_saved, 0, sizeof(s_saved));
    s_has_saved = false;
    settings_set_store(&s_mem_store);
    settings_set(SET_SERVO_BO_FLOOR, 2.5f);
    settings_set(SET_SERVO_BO_STEP, 0.3f);
    CHECK(settings_save());
    settings_init();
    CHECK_NEAR(settings_get(SET_SERVO_BO_FLOOR), 2.5f, 1e-4f);
    CHECK_NEAR(settings_get(SET_SERVO_BO_STEP), 0.3f, 1e-4f);

    s_saved[SET_SERVO_BO_FLOOR] = 9.0f;
    s_saved[SET_SERVO_BO_STEP]  = 0.0f;
    settings_init();
    CHECK_NEAR(settings_get(SET_SERVO_BO_FLOOR), 4.8f, 1e-4f);
    CHECK_NEAR(settings_get(SET_SERVO_BO_STEP), 0.1f, 1e-4f);

    s_saved[SET_SERVO_BO_FLOOR] = NAN;
    s_saved[SET_SERVO_BO_STEP]  = NAN;
    settings_init();
    CHECK_NEAR(settings_get(SET_SERVO_BO_FLOOR), 3.0f, 1e-4f);
    CHECK_NEAR(settings_get(SET_SERVO_BO_STEP), 0.2f, 1e-4f);
    settings_set_store(NULL);
}

/* A run started on the screen walks the settings: no supply step, the walk
 * from 5.0V in 0.5V steps to 4.0V, three voltages, and the report says
 * so. */
TEST_CASE(a_run_from_the_screen_walks_the_settings)
{
    bench_fresh();
    short_runs();
    settings_set(SET_SERVO_STEP_48, 0.0f);
    settings_set(SET_SERVO_STEP_60, 0.0f);
    settings_set(SET_SERVO_BROWNOUT, 1.0f);
    settings_set(SET_SERVO_BO_FLOOR, 4.0f);
    settings_set(SET_SERVO_BO_STEP, 0.5f);
    hold_start(2.3f);
    CHECK(servo_screen_testing());
    for (int i = 0; i < 120 && servo_screen_testing(); ++i) {
        bench_frames(500u);
    }
    CHECK(!servo_screen_testing());
    bench_frames(1000u);
    CHECK(strstr(bench.report, "Brown-out:      from 5.00V down in 0.50V "
                               "steps to 4.00V") != NULL);
    CHECK(strstr(bench.report, "every step down to 4.00V") != NULL);
    CHECK(bench.set_v_max <= 5.0f + 1e-4f);
    /* The last reading's set point, the CSV's seventh cell: the floor. */
    CHECK(strstr(bench.csv_last, ";BROWN-OUT;") != NULL);
    CHECK(strstr(bench.csv_last, ";4.00;") != NULL);
}

int main(void)
{
    RUN(the_brownout_row_opens_its_page_and_close_climbs_back);
    RUN(the_brownout_row_reads_on_with_floor_and_step_or_off);
    RUN(the_pages_rows_switch_and_take_their_ranges);
    RUN(floor_and_step_are_kept_over_a_restart);
    RUN(a_run_from_the_screen_walks_the_settings);
    return test_summary("servo_brownout_page");
}
