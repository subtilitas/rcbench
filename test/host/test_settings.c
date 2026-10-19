/*
 * The settings model: schema, clamping, stores, observers and the theme.
 *
 * SPDX-License-Identifier: MIT
 */
#include "settings_rig.h"

/* ------------------------------------------------------------------ model */

TEST_CASE(defaults_come_from_the_schema)
{
    fresh_model();
    for (int i = 0; i < SETTING_COUNT; ++i) {
        const setting_def_t *d = settings_def((setting_id_t)i);
        CHECK(d != NULL);
        CHECK(d->key != NULL && d->key[0] != '\0');
        CHECK(d->label != NULL && d->label[0] != '\0');
        if (settings_get((setting_id_t)i) != d->def) {
            T_FAIL("%s: got %f, default %f", d->key,
                   (double)settings_get((setting_id_t)i), (double)d->def);
        }
    }
    CHECK(!settings_dirty());
}

/*
 * The table is the contract.  Every row is checked: a default inside its own
 * range, a non-zero step so the +/- keys move, and at least one option for an
 * enum.  A bad row shows up as a control that does nothing.
 */
TEST_CASE(every_schema_row_is_internally_consistent)
{
    for (int i = 0; i < SETTING_COUNT; ++i) {
        const setting_def_t *d = settings_def((setting_id_t)i);
        if (d == NULL) {
            T_FAIL("no definition for id %d", i);
            continue;
        }
        if (d->cat < 0 || d->cat >= SET_CAT_COUNT) {
            T_FAIL("%s: category %d out of range", d->key, (int)d->cat);
        }
        switch (d->type) {
        case SET_TYPE_ENUM:
            if (d->options == NULL || d->option_count == 0u) {
                T_FAIL("%s: enum with no options", d->key);
            } else {
                if (d->def < 0.0f || d->def >= (float)d->option_count) {
                    T_FAIL("%s: default %g is not one of %d options", d->key,
                           (double)d->def, d->option_count);
                }
                for (int o = 0; o < d->option_count; ++o) {
                    if (d->options[o] == NULL || d->options[o][0] == '\0') {
                        T_FAIL("%s: option %d is empty", d->key, o);
                    }
                }
            }
            break;
        case SET_TYPE_BOOL:
            if (d->def != 0.0f && d->def != 1.0f) {
                T_FAIL("%s: bool default %g", d->key, (double)d->def);
            }
            break;
        case SET_TYPE_INT:
        case SET_TYPE_FLOAT:
            if (!(d->max > d->min)) {
                T_FAIL("%s: range %g..%g", d->key, (double)d->min, (double)d->max);
            }
            if (d->def < d->min || d->def > d->max) {
                T_FAIL("%s: default %g outside %g..%g", d->key, (double)d->def,
                       (double)d->min, (double)d->max);
            }
            if (!(d->step > 0.0f)) {
                T_FAIL("%s: step %g makes the keys inert", d->key,
                       (double)d->step);
            } else {
                /* On the grid the +/- keys walk, or the first press jumps. */
                float steps = (d->def - d->min) / d->step;
                if (fabsf(steps - roundf(steps)) > 1e-3f) {
                    T_FAIL("%s: default %g is off the %g grid from %g", d->key,
                           (double)d->def, (double)d->step, (double)d->min);
                }
            }
            break;
        default:
            T_FAIL("%s: unknown type %d", d->key, (int)d->type);
            break;
        }
    }
}

TEST_CASE(nvs_keys_are_unique_and_short_enough)
{
    /* NVS (non-volatile storage) keys are capped at 15 characters, and a
     * collision makes two settings the same one. */
    for (int i = 0; i < SETTING_COUNT; ++i) {
        const char *a = settings_def((setting_id_t)i)->key;
        if (strlen(a) > 15) {
            T_FAIL("key %s is %u chars, NVS allows 15", a, (unsigned)strlen(a));
        }
        for (int j = i + 1; j < SETTING_COUNT; ++j) {
            if (!strcmp(a, settings_def((setting_id_t)j)->key)) {
                T_FAIL("duplicate key %s", a);
            }
        }
    }
}

TEST_CASE(a_string_setting_is_kept_cut_and_saved_with_the_numbers)
{
    s_has_saved = false;
    s_has_text  = false;
    settings_set_store(&s_text_store);
    settings_init();
    CHECK_STR_EQ(settings_text(SET_TEXT_DUT_NAME), "SERVO");
    CHECK(!settings_dirty());

    settings_set_text(SET_TEXT_DUT_NAME, "BENCH AB-0255");
    CHECK(settings_dirty());
    CHECK(settings_save());
    CHECK(!settings_dirty());

    /* Cut to what it holds, and the same text again is no edit. */
    settings_set_text(SET_TEXT_DUT_NAME, "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
    CHECK_EQ(strlen(settings_text(SET_TEXT_DUT_NAME)),
             (size_t)(SETTINGS_TEXT_MAX - 1));
    CHECK(settings_save());
    settings_set_text(SET_TEXT_DUT_NAME, "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
    CHECK(!settings_dirty());

    /* Back at the next start; the defaults when the store keeps none. */
    settings_init();
    CHECK_STR_EQ(settings_text(SET_TEXT_DUT_NAME), "ABCDEFGHIJKLMNOPQRSTUVW");
    settings_reset_all();
    CHECK_STR_EQ(settings_text(SET_TEXT_DUT_NAME), "SERVO");
    settings_set_store(&s_mem_store);
    settings_init();
    CHECK_STR_EQ(settings_text(SET_TEXT_DUT_NAME), "SERVO");

    /* Ids out of range are survivable. */
    CHECK_STR_EQ(settings_text((setting_text_id_t)99), "");
    CHECK(settings_text_key((setting_text_id_t)99) == NULL);
    CHECK(strlen(settings_text_key(SET_TEXT_DUT_NAME)) <= 15);
    settings_set_text((setting_text_id_t)99, "X");
    settings_set_text(SET_TEXT_DUT_NAME, NULL);
    settings_set_store(NULL);
}

TEST_CASE(values_are_clamped_to_the_schema)
{
    fresh_model();
    const setting_def_t *d = settings_def(SET_OUT_MIN_US);

    settings_set(SET_OUT_MIN_US, -10000.0f);
    CHECK_EQ(settings_get_int(SET_OUT_MIN_US), (long)d->min);
    settings_set(SET_OUT_MIN_US, 999999.0f);
    CHECK_EQ(settings_get_int(SET_OUT_MIN_US), (long)d->max);

    /* Stepping past an end stops there rather than wrapping. */
    for (int i = 0; i < 500; ++i) {
        settings_adjust(SET_OUT_MIN_US, 1);
    }
    CHECK_EQ(settings_get_int(SET_OUT_MIN_US), (long)d->max);
    for (int i = 0; i < 500; ++i) {
        settings_adjust(SET_OUT_MIN_US, -1);
    }
    CHECK_EQ(settings_get_int(SET_OUT_MIN_US), (long)d->min);
}

TEST_CASE(enums_and_booleans_cycle)
{
    fresh_model();
    const setting_def_t *d = settings_def(SET_LANGUAGE);
    CHECK(d->option_count >= 2);

    for (int i = 0; i < d->option_count; ++i) {
        CHECK_EQ(settings_get_int(SET_LANGUAGE), i);
        settings_adjust(SET_LANGUAGE, 1);
    }
    CHECK_EQ(settings_get_int(SET_LANGUAGE), 0);      /* wrapped */

    settings_adjust(SET_LANGUAGE, -1);
    CHECK_EQ(settings_get_int(SET_LANGUAGE), d->option_count - 1);

    CHECK(settings_get_bool(SET_BACKLIGHT));
    settings_adjust(SET_BACKLIGHT, 1);
    CHECK(!settings_get_bool(SET_BACKLIGHT));
    settings_adjust(SET_BACKLIGHT, 1);
    CHECK(settings_get_bool(SET_BACKLIGHT));
}

TEST_CASE(steps_follow_the_schema)
{
    fresh_model();
    int before = settings_get_int(SET_PACK_MAH);
    settings_adjust(SET_PACK_MAH, 1);
    CHECK_EQ(settings_get_int(SET_PACK_MAH) - before,
             (long)settings_def(SET_PACK_MAH)->step);

    /* Poles step by two: an odd pole count is not a motor. */
    settings_set(SET_MOTOR_POLES, 14);
    settings_adjust(SET_MOTOR_POLES, 1);
    CHECK_EQ(settings_get_int(SET_MOTOR_POLES), 16);
    CHECK_EQ(settings_get_int(SET_MOTOR_POLES) % 2, 0);
}

TEST_CASE(categories_partition_every_setting)
{
    int total = 0;
    for (int c = 0; c < SET_CAT_COUNT; ++c) {
        int n = settings_in_category((setting_cat_t)c, NULL, 0);
        CHECK(n > 0);
        CHECK(settings_category_name((setting_cat_t)c)[0] != '\0');
        total += n;
    }
    CHECK_EQ(total, SETTING_COUNT);

    setting_id_t ids[64];
    int n = settings_in_category(SET_CAT_APP, ids, 64);
    for (int i = 0; i < n; ++i) {
        CHECK_EQ(settings_def(ids[i])->cat, SET_CAT_APP);
    }

    /* A buffer smaller than the category still reports the true count. */
    setting_id_t small[2];
    CHECK_EQ(settings_in_category(SET_CAT_APP, small, 2), n);
}

TEST_CASE(value_text_renders_every_type)
{
    fresh_model();
    char buf[32];

    settings_set(SET_BACKLIGHT, 1);
    CHECK_STR_EQ(settings_value_text(SET_BACKLIGHT, buf, sizeof(buf)), "ON");
    settings_set(SET_BACKLIGHT, 0);
    CHECK_STR_EQ(settings_value_text(SET_BACKLIGHT, buf, sizeof(buf)), "OFF");

    settings_set(SET_LANGUAGE, 0);
    CHECK_STR_EQ(settings_value_text(SET_LANGUAGE, buf, sizeof(buf)),
                 settings_def(SET_LANGUAGE)->options[0]);

    settings_set(SET_PACK_CELLS, 6);
    CHECK_STR_EQ(settings_value_text(SET_PACK_CELLS, buf, sizeof(buf)), "6");

    settings_set(SET_PACK_MOHM, 18.0f);
    CHECK_STR_EQ(settings_value_text(SET_PACK_MOHM, buf, sizeof(buf)), "18.0");

    /* Degenerate arguments must not write anywhere. */
    CHECK(settings_value_text(SET_PACK_CELLS, NULL, 0) != NULL);
}

TEST_CASE(store_round_trips_and_coerces_stale_values)
{
    fresh_model();
    settings_set_store(&s_mem_store);

    settings_set(SET_PACK_CELLS, 12);
    settings_set(SET_MOTOR_KV, 2400);
    CHECK(settings_dirty());
    settings_save();
    CHECK(!settings_dirty());
    CHECK_EQ(s_save_calls, 1);

    settings_init();
    CHECK_EQ(settings_get_int(SET_PACK_CELLS), 12);
    CHECK_EQ(settings_get_int(SET_MOTOR_KV), 2400);
    CHECK(!settings_dirty());

    /* A value stored by an older build, outside today's range, must be
     * pulled back rather than trusted. */
    s_saved[SET_PACK_CELLS] = 9999.0f;
    s_saved[SET_OUT_MIN_US] = -5.0f;
    settings_init();
    CHECK_EQ(settings_get_int(SET_PACK_CELLS),
             (long)settings_def(SET_PACK_CELLS)->max);
    CHECK_EQ(settings_get_int(SET_OUT_MIN_US),
             (long)settings_def(SET_OUT_MIN_US)->min);

    settings_set_store(NULL);
}

/*
 * The RELEASES setting is stored as the option's index under the key
 * "upd_channel".  Index 0 was named USABLE and is named STABLE; a panel
 * that saved it keeps its setting.
 */

TEST_CASE(a_stored_usable_releases_setting_loads_as_stable)
{
    fresh_model();
    char buf[32];

    CHECK_STR_EQ(settings_def(SET_UPD_CHANNEL)->key, "upd_channel");
    CHECK_EQ(settings_def(SET_UPD_CHANNEL)->option_count, 2);

    /* Index 0 and 1 stored by a build that named them USABLE and TESTING. */
    for (int stored = 0; stored <= 1; ++stored) {
        fresh_model();
        settings_set_store(&s_mem_store);
        settings_set(SET_UPD_REMIND, 1);
        settings_save();
        s_saved[SET_UPD_CHANNEL] = (float)stored;
        settings_init();
        CHECK_EQ(settings_get_int(SET_UPD_CHANNEL), stored);
        CHECK_STR_EQ(settings_value_text(SET_UPD_CHANNEL, buf, sizeof(buf)),
                     stored == 0 ? "STABLE" : "TESTING");
        settings_set_store(NULL);
    }

    /* An index out of range falls back to the default, STABLE. */
    fresh_model();
    settings_set_store(&s_mem_store);
    settings_save();
    s_saved[SET_UPD_CHANNEL] = 7.0f;
    settings_init();
    CHECK_STR_EQ(settings_value_text(SET_UPD_CHANNEL, buf, sizeof(buf)), "STABLE");
    settings_set_store(NULL);
}

/*
 * A load tells the observer every setting, its defaults included: the
 * panel's copies for the control task -- the current monitors' set-up
 * among them -- are filled from that sweep, and a value left at its
 * default is a value the far end still has to be told.
 */

TEST_CASE(a_load_tells_the_observer_every_setting_at_its_default)
{
    memset(s_seen_ids, 0, sizeof(s_seen_ids));
    settings_set_store(NULL);
    settings_set_observer(every_id_observer);
    settings_init();
    for (int i = 0; i < SETTING_COUNT; ++i) {
        if (s_seen_ids[i] < 1) {
            T_FAIL("%s not told at the load", settings_def((setting_id_t)i)->key);
        }
    }
    CHECK_EQ(settings_get_int(SET_SENSE_SDA), 16);
    CHECK_EQ(settings_get_int(SET_SENSE_SCL), 17);
    CHECK_EQ(settings_get_int(SET_INA228_ADDR), 5);   /* 0x45 */
    settings_set_observer(NULL);
}

/*
 * The phase tap's rows are the TONE page's: every range inside what the
 * page takes and every default the page's own, so a tap the operator turns
 * on without touching anything else is the set-up the page holds at boot.
 */
TEST_CASE(the_phase_tap_settings_are_the_tone_pages)
{
    settings_set_store(NULL);
    settings_init();
    CHECK_EQ(settings_get_int(SET_TONE_EN), 0);
    CHECK_EQ(settings_get_int(SET_TONE_PIN), (int)LINK_TN_DEFAULT_PIN);
    CHECK_EQ(settings_get_int(SET_TONE_F_MIN), (int)LINK_TN_DEFAULT_F_MIN);
    CHECK_EQ(settings_get_int(SET_TONE_F_MAX), (int)LINK_TN_DEFAULT_F_MAX);
    CHECK_EQ(settings_get_int(SET_TONE_SPLIT), (int)LINK_TN_DEFAULT_SPLIT);
    CHECK_EQ(settings_get_int(SET_TONE_GAP), (int)LINK_TN_DEFAULT_GAP_MS);
    CHECK_EQ(settings_get_int(SET_TONE_PERIODS),
             (int)LINK_TN_DEFAULT_PERIODS);

    const setting_def_t *d = settings_def(SET_TONE_F_MIN);
    CHECK_EQ((int)d->min, (int)LINK_TN_F_MIN_LO);
    CHECK_EQ((int)d->max, (int)LINK_TN_F_MIN_HI);
    d = settings_def(SET_TONE_F_MAX);
    CHECK((int)d->min > (int)LINK_TN_F_MIN_LO);
    CHECK_EQ((int)d->max, (int)LINK_TN_F_MAX_HI);
    d = settings_def(SET_TONE_SPLIT);
    CHECK_EQ((int)d->min, 0);
    CHECK_EQ((int)d->max, (int)LINK_TN_SPLIT_MAX);
    d = settings_def(SET_TONE_GAP);
    CHECK_EQ((int)d->min, (int)LINK_TN_GAP_MS_MIN);
    CHECK_EQ((int)d->max, (int)LINK_TN_GAP_MS_MAX);
    d = settings_def(SET_TONE_PERIODS);
    CHECK_EQ((int)d->min, (int)LINK_TN_PERIODS_MIN);
    CHECK_EQ((int)d->max, (int)LINK_TN_PERIODS_MAX);
    /* The defaults lie on the grid a value is held to: a stored value is
     * coerced onto it, and a default off it would not read back. */
    d = settings_def(SET_TONE_F_MIN);
    CHECK_EQ((LINK_TN_DEFAULT_F_MIN - (int)d->min) % (int)d->step, 0);
    d = settings_def(SET_TONE_F_MAX);
    CHECK_EQ((LINK_TN_DEFAULT_F_MAX - (int)d->min) % (int)d->step, 0);
    /* Pins as far as the IOmcu's bank. */
    CHECK_EQ((int)settings_def(SET_TONE_PIN)->min, 0);
    CHECK_EQ((int)settings_def(SET_TONE_PIN)->max, 47);
}

TEST_CASE(observer_fires_only_on_real_changes)
{
    fresh_model();
    settings_set_observer(observer);

    s_observed = 0;
    settings_set(SET_PACK_CELLS, settings_get(SET_PACK_CELLS));
    CHECK_EQ(s_observed, 0);                 /* same value: nothing happened */

    settings_set(SET_PACK_CELLS, 8);
    CHECK_EQ(s_observed, 1);
    CHECK_EQ(s_last_observed, SET_PACK_CELLS);

    /* A clamp that lands on the current value is also not a change. */
    settings_set(SET_PACK_CELLS, 99999.0f);
    int after_clamp = s_observed;
    settings_set(SET_PACK_CELLS, 99999.0f);
    CHECK_EQ(s_observed, after_clamp);

    settings_set_observer(NULL);
}

/*
 * The pole count reaches the observer by every path an operator has to it.
 *
 * The IOmcu holds a copy of this number and converts eRPM with it; the
 * register is written and never refreshed, so the notification is the only
 * signal the panel gets that the copy is out of date.  A path that changes
 * the value without notifying leaves the far end converting with the old
 * count, and a stale count is in range, so the speed carries a valid bit.
 * A path that notifies without changing anything spends a link transaction
 * for nothing.
 */
TEST_CASE(a_pole_count_edit_reaches_the_observer)
{
    fresh_model();
    s_poles_seen = 0;
    settings_set_observer(poles_observer);

    /* Setting it to what it already holds is not an edit. */
    s_observed = 0;
    settings_set(SET_MOTOR_POLES, settings_get(SET_MOTOR_POLES));
    CHECK_EQ(s_observed, 0);
    CHECK_EQ(s_poles_seen, 0);

    settings_set(SET_MOTOR_POLES, 12);
    CHECK_EQ(settings_get_int(SET_MOTOR_POLES), 12);
    CHECK_EQ(s_observed, 1);
    CHECK_EQ(s_last_observed, SET_MOTOR_POLES);
    CHECK_EQ(s_poles_seen, 1);

    /* The screen's own key: one step of the schema's 2. */
    settings_adjust(SET_MOTOR_POLES, 1);
    CHECK_EQ(settings_get_int(SET_MOTOR_POLES), 14);
    CHECK_EQ(s_poles_seen, 2);

    /* A key press at the end of the range moves nothing and owes nothing. */
    settings_set(SET_MOTOR_POLES, settings_def(SET_MOTOR_POLES)->max);
    s_poles_seen = 0;
    settings_adjust(SET_MOTOR_POLES, 1);
    CHECK_EQ(settings_get_int(SET_MOTOR_POLES),
             (long)settings_def(SET_MOTOR_POLES)->max);
    CHECK_EQ(s_poles_seen, 0);

    /* RESET on the ESC category is an edit of the pole count as well. */
    settings_reset(SET_CAT_ESC);
    CHECK_EQ(settings_get_int(SET_MOTOR_POLES),
             (long)settings_def(SET_MOTOR_POLES)->def);
    CHECK_EQ(s_poles_seen, 1);

    settings_set_observer(NULL);
}

/*
 * And once at startup, with the value the store returned.
 *
 * An IOmcu that has just started holds zero in the register, so the
 * stored count has to go out like an edit rather than be assumed to be over
 * there already.  The load writes the values behind
 * the model's own setter, so the notification an observer acts on has to
 * come after it: a sweep that ran before the load would report the schema
 * default, which is the wrong number in the same way a stale one is.
 */
TEST_CASE(startup_delivers_the_stored_pole_count)
{
    fresh_model();
    settings_set_store(&s_mem_store);
    settings_set(SET_MOTOR_POLES, 10);
    settings_save();

    s_poles_seen = 0;
    s_poles_value = 0;
    settings_set_observer(poles_observer);
    settings_init();
    CHECK_EQ(settings_get_int(SET_MOTOR_POLES), 10);
    CHECK(s_poles_seen >= 1);
    CHECK_EQ(s_poles_value, 10);

    settings_set_observer(NULL);
    settings_set_store(NULL);
}

/*
 * Every count the schema can produce is one the CONTROL page takes.
 *
 * The IOmcu refuses an odd count and anything outside
 * LINK_POLES_MIN..LINK_POLES_MAX, and reads zero as "nobody has said".  A
 * value the far end refuses is a write that never lands, and a refusal is
 * not retried -- the same request refused once is refused every time -- so
 * the count already over there goes on converting eRPM until the next edit.
 * A schema that could reach zero would also put the panel's writes inside
 * the far end's no-speed guard, which covers a count never sent and not a
 * stale one.
 */
TEST_CASE(every_pole_count_the_schema_allows_is_one_the_link_takes)
{
    fresh_model();
    const setting_def_t *d = settings_def(SET_MOTOR_POLES);
    CHECK(d->step > 0.0f);

    for (float v = d->min; v <= d->max; v += d->step) {
        settings_set(SET_MOTOR_POLES, v);
        int p = settings_get_int(SET_MOTOR_POLES);
        CHECK(p >= (int)LINK_POLES_MIN);
        CHECK(p <= (int)LINK_POLES_MAX);
        CHECK_EQ(p % 2, 0);
    }

    /* And what a stale store or a wild write coerces to, for the same
     * reason: the panel sends whatever the model holds. */
    const float wild[] = { 0.0f, -5.0f, 1.0f, 15.0f, 43.0f, 9999.0f };
    for (size_t i = 0; i < sizeof(wild) / sizeof(wild[0]); ++i) {
        settings_set(SET_MOTOR_POLES, wild[i]);
        int p = settings_get_int(SET_MOTOR_POLES);
        CHECK(p >= (int)LINK_POLES_MIN);
        CHECK(p <= (int)LINK_POLES_MAX);
        CHECK_EQ(p % 2, 0);
    }
}

TEST_CASE(reset_restores_one_category_only)
{
    fresh_model();
    settings_set(SET_PACK_CELLS, 12);
    settings_set(SET_BRIGHTNESS, 40);

    /* A reset that does not notify leaves the hardware on the old value while
     * the screen shows the new one; the observer is the only path a setting
     * has to the outputs and the backlight. */
    settings_set_observer(observer);
    s_observed = 0;

    settings_reset(SET_CAT_APP);
    CHECK_EQ(settings_get_int(SET_BRIGHTNESS),
             (long)settings_def(SET_BRIGHTNESS)->def);
    CHECK_EQ(settings_get_int(SET_PACK_CELLS), 12);   /* untouched */
    CHECK_EQ(s_observed, 1);                          /* brightness only */

    s_observed = 0;
    settings_reset_all();
    CHECK_EQ(settings_get_int(SET_PACK_CELLS),
             (long)settings_def(SET_PACK_CELLS)->def);
    CHECK_EQ(s_observed, 1);                          /* cells only */

    settings_set_observer(NULL);
}

TEST_CASE(bad_ids_are_survivable)
{
    fresh_model();
    CHECK(settings_def((setting_id_t)-1) == NULL);
    CHECK(settings_def((setting_id_t)SETTING_COUNT) == NULL);
    CHECK_EQ((long)settings_get((setting_id_t)-1), 0);
    settings_set((setting_id_t)SETTING_COUNT, 5.0f);
    settings_adjust((setting_id_t)-1, 1);
    settings_adjust(SET_PACK_CELLS, 0);
    CHECK(settings_category_name((setting_cat_t)-1)[0] == '\0');
}

/* ----------------------------------------------------------------- theme */

TEST_CASE(theme_switch_changes_the_palette)
{
    fresh_model();
    ui_theme_set(UI_THEME_DARK);
    gfx_color_t dark_bg = ui_theme_color(UI_C_BG);
    gfx_color_t dark_text = ui_theme_color(UI_C_TEXT);

    ui_theme_set(UI_THEME_LIGHT);
    CHECK(ui_theme_color(UI_C_BG) != dark_bg);
    CHECK(ui_theme_color(UI_C_TEXT) != dark_text);

    /* Text must stay well clear of its background in both themes. */
    for (int th = 0; th < UI_THEME_COUNT; ++th) {
        ui_theme_set((ui_theme_id_t)th);
        uint8_t br, bg, bb, tr, tg, tb;
        gfx_unpack(ui_theme_color(UI_C_BG), &br, &bg, &bb);
        gfx_unpack(ui_theme_color(UI_C_TEXT), &tr, &tg, &tb);
        int bl = (br * 30 + bg * 59 + bb * 11) / 100;
        int tl = (tr * 30 + tg * 59 + tb * 11) / 100;
        int diff = (tl > bl) ? tl - bl : bl - tl;
        if (diff < 120) {
            T_FAIL("theme %d: text/background luminance differ by only %d",
                   th, diff);
        }
    }
    ui_theme_set(UI_THEME_DARK);
}

TEST_CASE(brightness_and_contrast_are_clamped_and_monotonic)
{
    ui_theme_set(UI_THEME_DARK);
    ui_theme_set_brightness(100);
    uint8_t r_full, g_full, b_full;
    gfx_unpack(ui_theme_color(UI_C_TEXT), &r_full, &g_full, &b_full);

    ui_theme_set_brightness(40);
    uint8_t r_dim, g_dim, b_dim;
    gfx_unpack(ui_theme_color(UI_C_TEXT), &r_dim, &g_dim, &b_dim);
    CHECK(r_dim < r_full);
    CHECK(g_dim < g_full);

    ui_theme_set_brightness(-100);
    CHECK_EQ(ui_theme_brightness(), 20);
    ui_theme_set_brightness(1000);
    CHECK_EQ(ui_theme_brightness(), 100);

    ui_theme_set_contrast(0);
    CHECK_EQ(ui_theme_contrast(), 60);
    ui_theme_set_contrast(1000);
    CHECK_EQ(ui_theme_contrast(), 140);

    ui_theme_set_contrast(100);
    ui_theme_set((ui_theme_id_t)-1);          /* ignored, not a crash */
    CHECK_EQ(ui_theme_get(), UI_THEME_DARK);
}

int main(void)
{
    RUN(defaults_come_from_the_schema);
    RUN(every_schema_row_is_internally_consistent);
    RUN(nvs_keys_are_unique_and_short_enough);
    RUN(a_string_setting_is_kept_cut_and_saved_with_the_numbers);
    RUN(values_are_clamped_to_the_schema);
    RUN(enums_and_booleans_cycle);
    RUN(steps_follow_the_schema);
    RUN(categories_partition_every_setting);
    RUN(value_text_renders_every_type);
    RUN(store_round_trips_and_coerces_stale_values);
    RUN(a_stored_usable_releases_setting_loads_as_stable);
    RUN(a_load_tells_the_observer_every_setting_at_its_default);
    RUN(the_phase_tap_settings_are_the_tone_pages);
    RUN(observer_fires_only_on_real_changes);
    RUN(a_pole_count_edit_reaches_the_observer);
    RUN(startup_delivers_the_stored_pole_count);
    RUN(every_pole_count_the_schema_allows_is_one_the_link_takes);
    RUN(reset_restores_one_category_only);
    RUN(bad_ids_are_survivable);
    RUN(theme_switch_changes_the_palette);
    RUN(brightness_and_contrast_are_clamped_and_monotonic);
    return test_summary("settings");
}
