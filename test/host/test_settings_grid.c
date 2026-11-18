/**
 * @file test_settings_grid.c
 * @brief The number rows' grid: every row walked from end to end with the
 *        +/- keys lands on its max, its min and its default to the bit,
 *        and each step changes the text the row shows.
 *
 * SPDX-License-Identifier: MIT
 */
#include "settings_rig.h"

/*
 * Every number row walked from its bottom to its top and back with the
 * +/- keys.  The walk ends on max and on min to the bit, takes as many
 * steps up as down, each step changes the text the row shows, and the
 * row's default is a point of the walk, so a default saved and loaded
 * comes back as itself.  sup_v_max and sup_v_start (3.3 to 21.0 in 0.02)
 * are the rows float arithmetic missed: point 885 came out as 20.999998.
 */
TEST_CASE(every_number_row_walks_its_grid_from_end_to_end_and_back)
{
    for (int i = 0; i < SETTING_COUNT; ++i) {
        const setting_id_t id = (setting_id_t)i;
        const setting_def_t *d = settings_def(id);
        if ((d->type != SET_TYPE_INT && d->type != SET_TYPE_FLOAT)
            || settings_derived(id) || id == SET_SENSE_SDA
            || id == SET_PDMINI_TX || id == SET_PDMINI_RX
            || id == SET_TONE_PIN || !(d->step > 0.0f)) {
            continue;
        }
        fresh_model();
        /* The default is a fixed point of a store and a load. */
        settings_set(id, d->def);
        if (settings_get(id) != d->def) {
            T_FAIL("%s: default %.9g is kept as %.9g", d->key,
                   (double)d->def, (double)settings_get(id));
        }
        settings_set(id, d->min);
        if (settings_get(id) != d->min && settings_get(id) != 0.0f) {
            T_FAIL("%s: min %.9g is kept as %.9g", d->key, (double)d->min,
                   (double)settings_get(id));
        }
        const float bottom = settings_get(id);
        char was[32], now[32];
        bool def_met = bottom == d->def;
        long up = 0;
        for (; up < 200000; ++up) {
            const float before = settings_get(id);
            (void)settings_value_text(id, was, sizeof(was));
            settings_adjust(id, 1);
            if (settings_get(id) == before) {
                break;
            }
            def_met = def_met || settings_get(id) == d->def;
            (void)settings_value_text(id, now, sizeof(now));
            if (strcmp(was, now) == 0) {
                T_FAIL("%s: a step up from %.9g to %.9g reads '%s' twice",
                       d->key, (double)before, (double)settings_get(id), now);
                break;
            }
        }
        if (settings_get(id) != d->max) {
            T_FAIL("%s: the walk up ends on %.9g, max is %.9g", d->key,
                   (double)settings_get(id), (double)d->max);
        }
        if (!def_met) {
            T_FAIL("%s: the walk up never lands on the default %.9g",
                   d->key, (double)d->def);
        }
        long down = 0;
        for (; down < 200000; ++down) {
            const float before = settings_get(id);
            settings_adjust(id, -1);
            if (settings_get(id) == before) {
                break;
            }
        }
        if (settings_get(id) != bottom) {
            T_FAIL("%s: the walk down ends on %.9g, it began on %.9g",
                   d->key, (double)settings_get(id), (double)bottom);
        }
        if (up != down) {
            T_FAIL("%s: %ld steps up, %ld steps down", d->key, up, down);
        }
    }
}

/* The two rows in numbers: 885 steps of 0.02V from 3.3V end on 21.0V. */
TEST_CASE(supply_v_max_takes_885_steps_to_21_volts)
{
    static const setting_id_t rows[] = { SET_SUPPLY_V_MAX,
                                         SET_SUPPLY_V_START };
    for (unsigned r = 0; r < 2u; ++r) {
        fresh_model();
        settings_set(rows[r], 3.3f);
        for (int n = 0; n < 884; ++n) {
            settings_adjust(rows[r], 1);
        }
        CHECK(settings_get(rows[r]) < 21.0f);
        CHECK_NEAR(settings_get(rows[r]), 20.98f, 1e-4);
        settings_adjust(rows[r], 1);
        CHECK(settings_get(rows[r]) == 21.0f);
        settings_adjust(rows[r], 1);
        CHECK(settings_get(rows[r]) == 21.0f);
        settings_adjust(rows[r], -1);
        CHECK_NEAR(settings_get(rows[r]), 20.98f, 1e-4);
        /* A value between the last two points is one of them. */
        settings_set(rows[r], 20.996f);
        CHECK(settings_get(rows[r]) == 21.0f);
        settings_set(rows[r], 20.9849f);
        CHECK_NEAR(settings_get(rows[r]), 20.98f, 1e-4);
    }
}

int main(void)
{
    RUN(every_number_row_walks_its_grid_from_end_to_end_and_back);
    RUN(supply_v_max_takes_885_steps_to_21_volts);
    return test_summary("settings_grid");
}
