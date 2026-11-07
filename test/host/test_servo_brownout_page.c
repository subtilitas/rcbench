/*
 * The brown-out walk's rows on TEST's SETTINGS list (servo_test_list.c):
 * the BROWN-OUT heading and its line, the walk a run takes on the supply
 * in force; its three rows -- BROWN-OUT on or off, LOWEST VOLTAGE and
 * STEP -- with the keypads' bounds; the settings kept over a restart; and
 * a run started from the screen that walks what they say.
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
#include "ui_setlist.h"
#include "ui_text.h"

#include "servo_bench_rig.h"
#include "servo_rig.h"

/** @brief The open list keypad's CANCEL. */
static void kp_cancel(void)
{
    const gfx_rect_t r =
        ui_keypad_key_rect(&servo_screen_list()->kp, UI_KEY_CANCEL);
    tap(r.x + r.w / 2, r.y + r.h / 2);
}

/**
 * @brief The BROWN-OUT heading's line on the open list, as it draws it.
 * @param out Takes it.
 * @param n   Its size.
 */
static void walk_line(char *out, size_t n)
{
    const ui_setlist_t *sl = servo_screen_list();
    const ui_setlist_row_t r = sl->host->row(TL_H_BO);
    out[0] = '\0';
    if (r.custom != NULL && r.custom->value_text != NULL) {
        r.custom->value_text(out, n);
    }
}

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

/* The list carries the walk under its own heading: BROWN-OUT's heading,
 * then BROWN-OUT, LOWEST VOLTAGE and STEP, each a setting's row. */
TEST_CASE(the_list_carries_the_walk_under_its_heading)
{
    fresh();
    open_run_settings(0);
    CHECK(servo_screen_list()->open);
    const ui_setlist_t *sl = servo_screen_list();
    const ui_setlist_row_t h = sl->host->row(TL_H_BO);
    CHECK_EQ(h.kind, (uint8_t)UI_SL_HEADING);
    CHECK_EQ(h.heading, TX_SV_ROW_BROWNOUT);
    CHECK_EQ(sl->host->row(TL_BROWNOUT).id, SET_SERVO_BROWNOUT);
    CHECK_EQ(sl->host->row(TL_BO_FLOOR).id, SET_SERVO_BO_FLOOR);
    CHECK_EQ(sl->host->row(TL_BO_STEP).id, SET_SERVO_BO_STEP);
}

/* The rows: BROWN-OUT switches and writes nothing before SAVE; LOWEST
 * VOLTAGE and STEP open the keypad with their titles, units and ranges; a
 * value past the range is refused and the setting keeps its value; OK sets
 * the value typed. */
TEST_CASE(the_rows_switch_and_take_their_ranges)
{
    fresh();
    open_run_settings(0);
    const bool on = settings_get_bool(SET_SERVO_BROWNOUT);
    sl_step(TL_BROWNOUT, +1);
    CHECK(settings_get_bool(SET_SERVO_BROWNOUT) != on);
    CHECK(!settings_save_asked());
    sl_step(TL_BROWNOUT, +1);
    CHECK(settings_get_bool(SET_SERVO_BROWNOUT) == on);

    static const struct {
        int row;
        setting_id_t id;
        float lo, hi, typed;
        const char *over, *under;
    } k[] = {
        { TL_BO_FLOOR, SET_SERVO_BO_FLOOR, 2.0f, 4.8f, 4.8f, "4.9", "1.9" },
        { TL_BO_STEP,  SET_SERVO_BO_STEP,  0.1f, 1.0f, 0.1f, "1.1", "0.05" },
    };
    const ui_keypad_t *kp = &servo_screen_list()->kp;
    for (size_t j = 0; j < sizeof(k) / sizeof(k[0]); ++j) {
        const float was = settings_get(k[j].id);
        sl_part(k[j].row, UI_SL_PART_VALUE);
        CHECK(kp->open);
        CHECK_STR_EQ(kp->title, ui_setting_label(k[j].id));
        CHECK_STR_EQ(kp->unit, "V");
        CHECK_NEAR(kp->min, k[j].lo, 1e-6f);
        CHECK_NEAR(kp->max, k[j].hi, 1e-6f);
        /* Past the top: refused, the keypad stays and nothing is set. */
        sl_keys(k[j].over);
        CHECK(kp->open);
        CHECK_NEAR(settings_get(k[j].id), was, 1e-6f);
        kp_cancel();
        CHECK(!kp->open);
        /* Under the bottom: refused too. */
        sl_part(k[j].row, UI_SL_PART_VALUE);
        sl_keys(k[j].under);
        CHECK(kp->open);
        kp_cancel();
        CHECK_NEAR(settings_get(k[j].id), was, 1e-6f);
        /* Each end is taken. */
        sl_part(k[j].row, UI_SL_PART_VALUE);
        char text[8];
        snprintf(text, sizeof(text), "%.1f", (double)k[j].typed);
        sl_keys(text);
        CHECK(!kp->open);
        CHECK_NEAR(settings_get(k[j].id), k[j].typed, 1e-4f);
    }
    /* A value between the 0.1V steps lands on the nearer. */
    sl_part(TL_BO_FLOOR, UI_SL_PART_VALUE);
    sl_keys("3.04");
    CHECK_NEAR(settings_get(SET_SERVO_BO_FLOOR), 3.0f, 1e-4f);
}

/* The heading's line names the walk the supply in force allows: the
 * model's lowest set point, 3.3V, ends the default walk there, 10
 * voltages; caps of 4.0 to 8.4V with LOWEST VOLTAGE 2.0V in 0.5V steps
 * walk 5.0V down to 4.0V, 3 voltages. */
TEST_CASE(the_heading_names_the_walk_on_the_supply_in_force)
{
    fresh();
    open_run_settings(0);
    char line[96];
    walk_line(line, sizeof(line));
    CHECK_STR_EQ(line, "From 5.00V down in 0.20V steps to 3.30V, "
                       "10 voltages");
    const supply_caps_t caps = { 4.0f, 8.4f, 0.02f, 0.5f, 3.0f, 0.05f };
    supply_screen_set_caps(&caps);
    sl_part(TL_BO_FLOOR, UI_SL_PART_VALUE);
    sl_keys("2");
    sl_part(TL_BO_STEP, UI_SL_PART_VALUE);
    sl_keys("0.5");
    walk_line(line, sizeof(line));
    CHECK_STR_EQ(line, "From 5.00V down in 0.50V steps to 4.00V, "
                       "3 voltages");
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
    RUN(the_list_carries_the_walk_under_its_heading);
    RUN(the_rows_switch_and_take_their_ranges);
    RUN(the_heading_names_the_walk_on_the_supply_in_force);
    RUN(floor_and_step_are_kept_over_a_restart);
    RUN(a_run_from_the_screen_walks_the_settings);
    return test_summary("servo_brownout_page");
}
