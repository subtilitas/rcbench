/*
 * The settings model: schema, clamping, stores, observers and the theme.
 *
 * SPDX-License-Identifier: MIT
 */
#include "card_tree.h"
#include "settings_rig.h"

/* ------------------------------------------------------------------ model */

TEST_CASE(defaults_come_from_the_schema)
{
    fresh_model();
    for (int i = 0; i < SETTING_COUNT; ++i) {
        const setting_def_t *d = settings_def((setting_id_t)i);
        CHECK(d != NULL);
        CHECK(d->key != NULL && d->key[0] != '\0');
        CHECK(settings_label((setting_id_t)i)[0] != '\0');
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
        /* In no category: the four INA3221 keys of 0.21, kept. */
        const bool kept = i >= SET_INA3221_EN && i <= SET_INA3221_CH;
        if (d->cat < 0 || d->cat > SET_CAT_COUNT
            || (d->cat == SET_CAT_COUNT) != kept) {
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

/*
 * Keys an earlier build stored and this schema no longer has.  A panel
 * that held one keeps it in NVS: the firmware's store reads and writes
 * the schema's keys only (settings_nvs.c), so the old key is never read,
 * never written and never erased.  No setting may take one of these keys
 * again, or it would load the old setting's value.
 */
static const char *const k_retired_keys[] = {
    "backlight",        /* PANEL's Backlight, dropped after 0.21.1 */
};

/* NVS as the firmware's store addresses it: one value per key, in
 * milli-units, read through settings_def(i)->key and nothing else. */
static const struct { const char *key; int32_t milli; }
k_nvs_with_retired[] = {
    { "backlight",  0 },        /* the dropped row, stored OFF */
    { "brightness", 40000 },
    { "theme",      1000 },
    { "pack_cells", 4000 },
};

static int s_nvs_found;
static bool keyed_load(float *values, int count)
{
    s_nvs_found = 0;
    for (int i = 0; i < count; ++i) {
        const setting_def_t *d = settings_def((setting_id_t)i);
        if (d == NULL || d->key == NULL) {
            continue;
        }
        for (size_t k = 0; k < sizeof(k_nvs_with_retired)
                                   / sizeof(k_nvs_with_retired[0]); ++k) {
            if (strcmp(d->key, k_nvs_with_retired[k].key) == 0) {
                values[i] = (float)k_nvs_with_retired[k].milli / 1000.0f;
                ++s_nvs_found;
            }
        }
    }
    return s_nvs_found > 0;
}
static const settings_store_t s_keyed_store = { keyed_load, mem_save, NULL,
                                                NULL };

TEST_CASE(a_store_holding_a_retired_key_loads_and_ignores_it)
{
    for (size_t k = 0; k < sizeof(k_retired_keys) / sizeof(*k_retired_keys);
         ++k) {
        for (int i = 0; i < SETTING_COUNT; ++i) {
            CHECK(strcmp(settings_def((setting_id_t)i)->key, k_retired_keys[k])
                  != 0);
        }
    }

    /* What a panel with nothing stored starts on. */
    fresh_model();
    static float plain[SETTING_COUNT];
    for (int i = 0; i < SETTING_COUNT; ++i) {
        plain[i] = settings_get((setting_id_t)i);
    }
    settings_set_store(&s_keyed_store);
    settings_init();
    /* Three of the four keys are the schema's; the retired one is read by
     * no setting and moves nothing. */
    CHECK_EQ(s_nvs_found, 3);
    CHECK_EQ(settings_get_int(SET_BRIGHTNESS), 40);
    CHECK_EQ(settings_get_int(SET_THEME), 1);
    CHECK_EQ(settings_get_int(SET_PACK_CELLS), 4);
    CHECK(!settings_dirty());
    for (int i = 0; i < SETTING_COUNT; ++i) {
        const setting_id_t id = (setting_id_t)i;
        if (id == SET_BRIGHTNESS || id == SET_THEME || id == SET_PACK_CELLS) {
            continue;
        }
        /* Within 1e-5: coercion to the step can move a float's last bits. */
        if (fabsf(settings_get(id) - plain[i])
            > 1e-5f * fmaxf(1.0f, fabsf(plain[i]))) {
            T_FAIL("setting %s moved", settings_def(id)->key);
        }
    }
    /* A save writes the schema's keys: the store is handed SETTING_COUNT
     * values, none of them the retired one's. */
    settings_set(SET_BRIGHTNESS, 50);
    CHECK(settings_save());
    CHECK_EQ(s_saved[SET_BRIGHTNESS], 50.0f);
    settings_set_store(NULL);
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

    CHECK(!settings_get_bool(SET_PLOT_PAN));
    settings_adjust(SET_PLOT_PAN, 1);
    CHECK(settings_get_bool(SET_PLOT_PAN));
    settings_adjust(SET_PLOT_PAN, 1);
    CHECK(!settings_get_bool(SET_PLOT_PAN));
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
    /* And the four INA3221 keys of 0.21, in none (settings_sensors.h). */
    CHECK_EQ(total + 4, SETTING_COUNT);

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

    settings_set(SET_PLOT_PAN, 1);
    CHECK_STR_EQ(settings_value_text(SET_PLOT_PAN, buf, sizeof(buf)), "ON");
    settings_set(SET_PLOT_PAN, 0);
    CHECK_STR_EQ(settings_value_text(SET_PLOT_PAN, buf, sizeof(buf)), "OFF");

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
     * has to the outputs and the palette. */
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
    CHECK_STR_EQ(settings_label((setting_id_t)-1), "");
    CHECK_STR_EQ(settings_label((setting_id_t)SETTING_COUNT), "");
    CHECK_STR_EQ(settings_help((setting_id_t)-1), "");
    CHECK_STR_EQ(settings_help((setting_id_t)SETTING_COUNT), "");
    CHECK_EQ(settings_option_list((setting_id_t)-1), -1);
    CHECK_EQ(settings_option_list((setting_id_t)SETTING_COUNT), -1);
}

/** @brief An enum setting finds its list among ui_text.def's, which a
 * language's options follow; a number finds none.  Settings that show one
 * list find the same one. */
TEST_CASE(every_choice_finds_its_option_list)
{
    for (int i = 0; i < SETTING_COUNT; ++i) {
        const setting_def_t *d = settings_def((setting_id_t)i);
        const int list = settings_option_list((setting_id_t)i);
        if ((d->options != NULL) != (list >= 0)) {
            T_FAIL("%s: options %p, list %d", d->key,
                   (const void *)d->options, list);
        }
    }
    CHECK_EQ(settings_option_list(SET_PACK_CELLS), -1);
    CHECK_EQ(settings_option_list(SET_THEME), 0);
    CHECK(settings_option_list(SET_INA_W_01) >= 0);
    CHECK_EQ(settings_option_list(SET_INA_W_01),
             settings_option_list(SET_INA_W_33));
    CHECK(settings_option_list(SET_INA_W_01)
          != settings_option_list(SET_SERVO_TARGET));
    CHECK_STR_EQ(settings_label(SET_PACK_CELLS), "Cells");
    CHECK_STR_EQ(settings_help(SET_PACK_CELLS), "Series count in the pack");
    CHECK_STR_EQ(settings_label(SET_INA_SH_23), "0x42 CH3 shunt");
    CHECK_STR_EQ(settings_help(SET_THEME), "");
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


/*
 * AUTO LOGS and USER LOGS on SETUP > SD CARD: both ON by default, kept by
 * the store under their own keys, and ON after an update from a store that
 * has never held them.
 */
static float s_old[SETTING_COUNT];

static bool old_build_load(float *values, int count)
{
    /* An older build's store: every key but the two new ones. */
    for (int i = 0; i < count && i < SETTING_COUNT; ++i) {
        if (i != SET_LOG_AUTO && i != SET_LOG_USER) {
            values[i] = s_old[i];
        }
    }
    return true;
}

static const settings_store_t s_old_store = { old_build_load, mem_save,
                                              NULL, NULL };

TEST_CASE(the_card_switches_are_on_kept_and_on_after_an_update)
{
    fresh_model();
    CHECK(settings_get_bool(SET_LOG_AUTO));
    CHECK(settings_get_bool(SET_LOG_USER));
    CHECK_STR_EQ(settings_def(SET_LOG_AUTO)->key, "log_auto");
    CHECK_STR_EQ(settings_def(SET_LOG_USER)->key, "log_user");
    CHECK_EQ(settings_def(SET_LOG_AUTO)->type, SET_TYPE_BOOL);
    CHECK_EQ(settings_def(SET_LOG_USER)->type, SET_TYPE_BOOL);
    CHECK_EQ(settings_def(SET_LOG_AUTO)->cat, SET_CAT_CARD);
    CHECK_EQ(settings_def(SET_LOG_USER)->cat, SET_CAT_CARD);
    CHECK_STR_EQ(settings_category_name(SET_CAT_CARD), "SD CARD");
    setting_id_t ids[8];
    CHECK_EQ(settings_in_category(SET_CAT_CARD, ids, 8), 2);
    CHECK_EQ(ids[0], SET_LOG_AUTO);
    CHECK_EQ(ids[1], SET_LOG_USER);
    CHECK(SET_CAT_CARD < SET_CAT_SETUP_COUNT);   /* a SETUP tile */

    /* The round trip, each value both ways. */
    settings_set_store(&s_mem_store);
    settings_set(SET_LOG_AUTO, 0.0f);
    settings_save();
    settings_init();
    CHECK(!settings_get_bool(SET_LOG_AUTO));
    CHECK(settings_get_bool(SET_LOG_USER));
    settings_set(SET_LOG_AUTO, 1.0f);
    settings_set(SET_LOG_USER, 0.0f);
    settings_save();
    settings_init();
    CHECK(settings_get_bool(SET_LOG_AUTO));
    CHECK(!settings_get_bool(SET_LOG_USER));
    /* A toggle cycles; a stray stored word is pulled onto 0 or 1. */
    settings_adjust(SET_LOG_USER, 1);
    CHECK(settings_get_bool(SET_LOG_USER));
    s_saved[SET_LOG_AUTO] = 7.0f;
    settings_init();
    CHECK_EQ(settings_get(SET_LOG_AUTO), 1.0f);

    /* An older build's store: both ON, the rest as stored. */
    for (int i = 0; i < SETTING_COUNT; ++i) {
        s_old[i] = settings_def((setting_id_t)i)->def;
    }
    s_old[SET_PACK_CELLS] = 3.0f;
    settings_set_store(&s_old_store);
    settings_init();
    CHECK(settings_get_bool(SET_LOG_AUTO));
    CHECK(settings_get_bool(SET_LOG_USER));
    CHECK_EQ(settings_get_int(SET_PACK_CELLS), 3);
    /* A reset of the category puts them back ON. */
    settings_set(SET_LOG_AUTO, 0.0f);
    settings_set(SET_LOG_USER, 0.0f);
    settings_reset(SET_CAT_CARD);
    CHECK(settings_get_bool(SET_LOG_AUTO));
    CHECK(settings_get_bool(SET_LOG_USER));
    settings_set_store(NULL);
}

/*
 * AUTO LOG FOLDER and USER LOG FOLDER: two strings of SETUP > SD CARD, kept
 * with the numbers, their defaults the card's folders, the defaults again
 * after an update from a store that never held them, and every change told
 * to the text observer.
 */
static int s_text_seen[SETTING_TEXT_COUNT];

static void text_observer(setting_text_id_t id)
{
    if (id >= 0 && id < SETTING_TEXT_COUNT) {
        ++s_text_seen[id];
    }
}

static void text_seen_clear(void)
{
    memset(s_text_seen, 0, sizeof(s_text_seen));
}

/* An older build's strings: the DUT name and nothing else. */
static bool old_build_load_text(char (*texts)[SETTINGS_TEXT_MAX], int count)
{
    if (count > SET_TEXT_DUT_NAME) {
        snprintf(texts[SET_TEXT_DUT_NAME], SETTINGS_TEXT_MAX, "%s", "OLD");
    }
    return true;
}

static const settings_store_t s_old_text_store = {
    mem_load, mem_save, old_build_load_text, mem_save_text };

TEST_CASE(the_log_folders_are_strings_kept_reset_and_observed)
{
    fresh_model();
    CHECK_STR_EQ(settings_text(SET_TEXT_LOG_AUTO_DIR), CARD_AUTO_LOGS);
    CHECK_STR_EQ(settings_text(SET_TEXT_LOG_USER_DIR), CARD_USER_LOGS);
    CHECK_STR_EQ(settings_text_key(SET_TEXT_LOG_AUTO_DIR), "log_auto_dir");
    CHECK_STR_EQ(settings_text_key(SET_TEXT_LOG_USER_DIR), "log_user_dir");
    /* One NVS namespace holds the numbers and the strings: no key twice. */
    for (int t = 0; t < SETTING_TEXT_COUNT; ++t) {
        const char *k = settings_text_key((setting_text_id_t)t);
        CHECK(k != NULL && strlen(k) <= 15u);
        for (int u = t + 1; u < SETTING_TEXT_COUNT; ++u) {
            CHECK(strcmp(k, settings_text_key((setting_text_id_t)u)) != 0);
        }
        for (int i = 0; i < SETTING_COUNT; ++i) {
            if (strcmp(k, settings_def((setting_id_t)i)->key) == 0) {
                T_FAIL("string key %s is also a number's", k);
            }
        }
    }
    /* On SETUP > SD CARD, after its two switches; the DUT name is not on
     * SETUP. */
    CHECK_EQ(settings_text_category(SET_TEXT_LOG_AUTO_DIR), SET_CAT_CARD);
    CHECK_EQ(settings_text_category(SET_TEXT_LOG_USER_DIR), SET_CAT_CARD);
    CHECK_EQ(settings_text_category(SET_TEXT_DUT_NAME), SET_CAT_COUNT);
    CHECK_EQ(settings_text_category((setting_text_id_t)99), SET_CAT_COUNT);
    setting_text_id_t t[4];
    CHECK_EQ(settings_texts_in_category(SET_CAT_CARD, t, 4), 2);
    CHECK_EQ(t[0], SET_TEXT_LOG_AUTO_DIR);
    CHECK_EQ(t[1], SET_TEXT_LOG_USER_DIR);
    CHECK_EQ(settings_texts_in_category(SET_CAT_CARD, NULL, 0), 2);
    CHECK_EQ(settings_texts_in_category(SET_CAT_CARD, t, 1), 2);
    CHECK_EQ(settings_texts_in_category(SET_CAT_SERVO, t, 4), 0);
    CHECK_EQ(settings_texts_in_category(SET_CAT_COUNT, t, 4), 0);

    /* The round trip, and the start-up telling the observer each string. */
    s_has_text = false;
    settings_set_store(&s_text_store);
    settings_set_text_observer(text_observer);
    text_seen_clear();
    settings_init();
    for (int i = 0; i < SETTING_TEXT_COUNT; ++i) {
        CHECK_EQ(s_text_seen[i], 1);
    }
    text_seen_clear();
    settings_set_text(SET_TEXT_LOG_AUTO_DIR, "RUNS");
    settings_set_text(SET_TEXT_LOG_USER_DIR, "RUNS");
    CHECK_EQ(s_text_seen[SET_TEXT_LOG_AUTO_DIR], 1);
    CHECK_EQ(s_text_seen[SET_TEXT_LOG_USER_DIR], 1);
    CHECK_EQ(s_text_seen[SET_TEXT_DUT_NAME], 0);
    /* The same text again is no change and nothing to hear. */
    settings_set_text(SET_TEXT_LOG_AUTO_DIR, "RUNS");
    CHECK_EQ(s_text_seen[SET_TEXT_LOG_AUTO_DIR], 1);
    CHECK(settings_dirty());
    CHECK(settings_save());
    settings_init();
    CHECK_STR_EQ(settings_text(SET_TEXT_LOG_AUTO_DIR), "RUNS");
    CHECK_STR_EQ(settings_text(SET_TEXT_LOG_USER_DIR), "RUNS");

    /* DISCARD puts an unsaved folder back, and the observer hears it. */
    text_seen_clear();
    settings_set_text(SET_TEXT_LOG_USER_DIR, "A/B");
    CHECK_EQ(settings_changes(), 1);
    CHECK_EQ(settings_discard(), 0);
    CHECK_STR_EQ(settings_text(SET_TEXT_LOG_USER_DIR), "RUNS");
    CHECK_EQ(s_text_seen[SET_TEXT_LOG_USER_DIR], 2);
    CHECK(!settings_dirty());

    /* A reset of SD CARD puts both folders back and leaves the DUT name;
     * a reset of another category leaves the folders. */
    settings_set_text(SET_TEXT_DUT_NAME, "KEEP");
    settings_reset(SET_CAT_SERVO);
    CHECK_STR_EQ(settings_text(SET_TEXT_LOG_AUTO_DIR), "RUNS");
    text_seen_clear();
    settings_reset(SET_CAT_CARD);
    CHECK_STR_EQ(settings_text(SET_TEXT_LOG_AUTO_DIR), CARD_AUTO_LOGS);
    CHECK_STR_EQ(settings_text(SET_TEXT_LOG_USER_DIR), CARD_USER_LOGS);
    CHECK_STR_EQ(settings_text(SET_TEXT_DUT_NAME), "KEEP");
    CHECK_EQ(s_text_seen[SET_TEXT_LOG_AUTO_DIR], 1);
    CHECK_EQ(s_text_seen[SET_TEXT_LOG_USER_DIR], 1);
    CHECK_EQ(s_text_seen[SET_TEXT_DUT_NAME], 0);
    CHECK(settings_dirty());
    /* A reset that moves nothing is no edit. */
    CHECK(settings_save());
    settings_reset(SET_CAT_CARD);
    CHECK(!settings_dirty());

    /* An older build's store: the folders at their defaults. */
    settings_set_store(&s_old_text_store);
    settings_init();
    CHECK_STR_EQ(settings_text(SET_TEXT_DUT_NAME), "OLD");
    CHECK_STR_EQ(settings_text(SET_TEXT_LOG_AUTO_DIR), CARD_AUTO_LOGS);
    CHECK_STR_EQ(settings_text(SET_TEXT_LOG_USER_DIR), CARD_USER_LOGS);
    settings_set_text_observer(NULL);
    settings_set_store(NULL);
}

int main(void)
{
    RUN(defaults_come_from_the_schema);
    RUN(every_schema_row_is_internally_consistent);
    RUN(nvs_keys_are_unique_and_short_enough);
    RUN(a_store_holding_a_retired_key_loads_and_ignores_it);
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
    RUN(every_choice_finds_its_option_list);
    RUN(theme_switch_changes_the_palette);
    RUN(brightness_and_contrast_are_clamped_and_monotonic);
    RUN(the_card_switches_are_on_kept_and_on_after_an_update);
    RUN(the_log_folders_are_strings_kept_reset_and_observed);
    return test_summary("settings");
}
