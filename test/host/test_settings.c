/*
 * The settings model and the screen that edits it.
 *
 * Taps are in panel coordinates, because ui_router_event strips the band on
 * the way in; the geometry below adds UI_BAND_H to the screen's own rows.
 *
 * SPDX-License-Identifier: MIT
 */
#include "greatest.h"

#include <math.h>

#include "iomcu_pins.h"
#include "link_pages.h"
#include "out_bind.h"
#include "outputs.h"
#include "sense_link.h"
#include "sense_page.h"
#include "settings.h"
#include "settings_screen.h"
#include "tone_link.h"
#include "ui_screen.h"
#include "ui_widgets.h"
#include <stdlib.h>
#include "ui_theme.h"
#include "touch_feed.h"

#define W 800
#define H 480

/* Geometry mirrored from settings_screen.c; if the layout moves these move
 * with it, and the test names say what they aim at. */
#define CAT_X    16
#define CAT_Y    (16 + UI_BAND_H)
#define CAT_H    64
#define CAT_GAP  8
#define LIST_X   236
#define LIST_Y   (16 + UI_BAND_H)
#define ROW_PITCH 58
#define ROW_H    54
#define PLUS_X   726
#define MINUS_X  556
#define BTN_W    44
#define RESET_Y  (350 + UI_BAND_H)
#define RESET_H  36
#define SAVE_Y   (RESET_Y + RESET_H + 6)
#define SAVE_H   36

static gfx_color_t *s_fb;
static gfx_canvas_t s_c;



/* --------------------------------------------------------- memory store */

static float s_saved[SETTING_COUNT];
static bool  s_has_saved;
static int   s_save_calls;

static bool mem_load(float *values, int count)
{
    if (!s_has_saved) {
        return false;
    }
    for (int i = 0; i < count && i < SETTING_COUNT; ++i) {
        values[i] = s_saved[i];
    }
    return true;
}

/* True when the medium took every value; the refusing store below is what
 * a panel with unusable NVS (non-volatile storage) looks like. */
static bool mem_save(const float *values, int count)
{
    for (int i = 0; i < count && i < SETTING_COUNT; ++i) {
        s_saved[i] = values[i];
    }
    s_has_saved = true;
    ++s_save_calls;
    return true;
}

static const settings_store_t s_mem_store = { mem_load, mem_save, NULL, NULL };

/* A store that is asked and answers no.  It writes nothing, so a later load
 * returns what was there before the refused save. */
static bool refuse_save(const float *values, int count)
{
    (void)values;
    (void)count;
    ++s_save_calls;
    return false;
}

static const settings_store_t s_refusing_store = { mem_load, refuse_save, NULL, NULL };

/* A store that keeps the strings too. */
static char s_saved_text[SETTING_TEXT_COUNT][SETTINGS_TEXT_MAX];
static bool s_has_text;

static bool mem_load_text(char (*texts)[SETTINGS_TEXT_MAX], int count)
{
    if (!s_has_text) {
        return false;
    }
    for (int i = 0; i < count && i < SETTING_TEXT_COUNT; ++i) {
        memcpy(texts[i], s_saved_text[i], SETTINGS_TEXT_MAX);
    }
    return true;
}

static bool mem_save_text(const char (*texts)[SETTINGS_TEXT_MAX], int count)
{
    for (int i = 0; i < count && i < SETTING_TEXT_COUNT; ++i) {
        memcpy(s_saved_text[i], texts[i], SETTINGS_TEXT_MAX);
    }
    s_has_text = true;
    return true;
}

static const settings_store_t s_text_store = { mem_load, mem_save,
                                               mem_load_text, mem_save_text };

static int s_observed;
static setting_id_t s_last_observed;

static void observer(setting_id_t id)
{
    ++s_observed;
    s_last_observed = id;
}

/* The pole count is the one setting the coprocessor keeps a copy of, so a
 * notification for it is what the panel turns into a write.  Counted apart
 * from the rest, because settings_init() notifies every id in one sweep. */
static int s_poles_seen;
static int s_poles_value;

static void poles_observer(setting_id_t id)
{
    ++s_observed;
    s_last_observed = id;
    if (id == SET_MOTOR_POLES) {
        ++s_poles_seen;
        s_poles_value = settings_get_int(SET_MOTOR_POLES);
    }
}

static void fresh_model(void)
{
    s_has_saved = false;
    s_save_calls = 0;
    s_observed = 0;
    settings_set_store(NULL);
    settings_set_observer(NULL);
    settings_init();
}

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
            if (d->options == NULL || d->option_count <= 0) {
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

    settings_set_text(SET_TEXT_DUT_NAME, "SAVOX SH-0255");
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
 * A load tells the observer every setting, its defaults included: the
 * panel's copies for the control task -- the current monitors' set-up
 * among them -- are filled from that sweep, and a value left at its
 * default is a value the far end still has to be told.
 */
static int s_seen_ids[SETTING_COUNT];

static void every_id_observer(setting_id_t id)
{
    if (id >= 0 && id < SETTING_COUNT) {
        ++s_seen_ids[id];
    }
}

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
    /* Pins as far as the coprocessor's bank. */
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
 * The coprocessor holds a copy of this number and converts eRPM with it; the
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
 * A coprocessor that has just started holds zero in the register, so the
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
 * The coprocessor refuses an odd count and anything outside
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

static void fresh_screen(void)
{
    if (!s_fb) {
        s_fb = calloc((size_t)W * H, sizeof(gfx_color_t));
    }
    memset(s_fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    gfx_canvas_init(&s_c, s_fb, W, H, W);
    fresh_model();
    /* The bench screen is set up by ui_router_init, which reset()s every
     * screen. */
    ui_router_init();
    ui_router_goto(SCREEN_SETUP);
}

static void tap(int x, int y)
{
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = (int16_t)x, .y = (int16_t)y } };
    ui_router_event(&e);
    e.type = TOUCH_EVENT_UP;
    ui_router_event(&e);
}

static int row_y(int index)
{
    return LIST_Y + index * ROW_PITCH + ROW_H / 2;
}

/* ---------------------------------------------------------------- screen */

TEST_CASE(screen_switches_category_and_renders)
{
    fresh_screen();
    ui_router_render(&s_c, 0);
    CHECK(gfx_pixel_get(&s_c, 400, 100) != 0);

    for (int i = 0; i < SET_CAT_COUNT; ++i) {
        tap(CAT_X + 100, CAT_Y + i * (CAT_H + CAT_GAP) + CAT_H / 2);
        ui_router_render(&s_c, 0);
        CHECK(gfx_pixel_get(&s_c, 400, 100) != 0);
    }
}

TEST_CASE(plus_and_minus_change_the_setting_under_them)
{
    fresh_screen();
    setting_id_t ids[64];
    settings_in_category(SET_CAT_ESC, ids, 64);

    int before = settings_get_int(ids[0]);
    tap(PLUS_X + BTN_W / 2, row_y(0));
    CHECK_EQ(settings_get_int(ids[0]) - before,
             (long)settings_def(ids[0])->step);

    tap(MINUS_X + BTN_W / 2, row_y(0));
    CHECK_EQ(settings_get_int(ids[0]), before);

    /* The second row is a different setting, and only it moves. */
    int row1 = settings_get_int(ids[1]);
    tap(PLUS_X + BTN_W / 2, row_y(1));
    CHECK(settings_get_int(ids[1]) != row1);
    CHECK_EQ(settings_get_int(ids[0]), before);
}

/*
 * The operator's own path: the key under Motor poles on SETUP.
 *
 * The model, the screen and the observer are one chain, and the panel hangs
 * the write to the coprocessor off the far end of it.  A break anywhere in
 * that chain leaves the panel with nothing to hang the write on, which is
 * the defect this guards, seen from where the finger is.
 */
TEST_CASE(the_poles_key_notifies_once_per_press)
{
    fresh_screen();
    setting_id_t ids[64];
    int n = settings_in_category(SET_CAT_ESC, ids, 64);
    int poles_row = -1;
    for (int i = 0; i < n; ++i) {
        if (ids[i] == SET_MOTOR_POLES) { poles_row = i; }
    }
    CHECK(poles_row >= 0);

    s_poles_seen = 0;
    s_observed = 0;
    settings_set_observer(poles_observer);

    int before = settings_get_int(SET_MOTOR_POLES);
    tap(PLUS_X + BTN_W / 2, row_y(poles_row));
    CHECK_EQ(settings_get_int(SET_MOTOR_POLES) - before,
             (long)settings_def(SET_MOTOR_POLES)->step);
    CHECK_EQ(s_poles_seen, 1);
    CHECK_EQ(s_observed, 1);

    tap(MINUS_X + BTN_W / 2, row_y(poles_row));
    CHECK_EQ(settings_get_int(SET_MOTOR_POLES), before);
    CHECK_EQ(s_poles_seen, 2);

    settings_set_observer(NULL);
}

TEST_CASE(changing_the_theme_from_the_screen_takes_effect)
{
    fresh_screen();
    tap(CAT_X + 100, CAT_Y + 1 * (CAT_H + CAT_GAP) + CAT_H / 2);   /* APPLICATION */

    setting_id_t ids[64];
    int n = settings_in_category(SET_CAT_APP, ids, 64);
    int theme_row = -1;
    for (int i = 0; i < n; ++i) {
        if (ids[i] == SET_THEME) { theme_row = i; }
    }
    CHECK(theme_row >= 0);

    gfx_color_t before = ui_theme_color(UI_C_BG);
    tap(PLUS_X + BTN_W / 2, row_y(theme_row));
    CHECK_EQ(settings_get_int(SET_THEME), UI_THEME_LIGHT);
    CHECK(ui_theme_color(UI_C_BG) != before);

    tap(MINUS_X + BTN_W / 2, row_y(theme_row));
    CHECK_EQ(settings_get_int(SET_THEME), UI_THEME_DARK);
    CHECK_EQ(ui_theme_color(UI_C_BG), before);
}

TEST_CASE(a_drag_scrolls_instead_of_pressing)
{
    fresh_screen();
    setting_id_t ids[64];
    settings_in_category(SET_CAT_ESC, ids, 64);
    int before = settings_get_int(ids[0]);

    /* Press on the + key, then move well past the slop before releasing.
     * The press arms the key and steps nothing; the drag gives it up. */
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = PLUS_X + BTN_W / 2,
                                   .y = (int16_t)row_y(0) } };
    ui_router_event(&e);
    int after_press = settings_get_int(ids[0]);

    e.type = TOUCH_EVENT_MOVE;
    for (int i = 1; i <= 10; ++i) {
        e.point.y = (int16_t)(row_y(0) - i * 12);
        ui_router_event(&e);
    }
    e.type = TOUCH_EVENT_UP;
    ui_router_event(&e);

    CHECK_EQ(after_press, before);
    CHECK_EQ(settings_get_int(ids[0]), before);

    ui_router_render(&s_c, 0);
}

TEST_CASE(holding_a_key_repeats_with_acceleration)
{
    fresh_screen();
    setting_id_t ids[64];
    settings_in_category(SET_CAT_ESC, ids, 64);

    /* Row 1 is capacity: 0..30000 in steps of 100, which is why holding has
     * to work at all. */
    settings_set(ids[1], settings_def(ids[1])->def);
    int start = settings_get_int(ids[1]);

    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = PLUS_X + BTN_W / 2,
                                   .y = (int16_t)row_y(1) } };
    ui_router_event(&e);
    int after_press = settings_get_int(ids[1]);
    CHECK_EQ(after_press, start);       /* armed: the press steps nothing */

    /* Nothing repeats before the delay. */
    for (int i = 0; i < 5; ++i) {
        ui_router_tick(0.05f);
    }
    CHECK_EQ(settings_get_int(ids[1]), after_press);

    for (int i = 0; i < 20; ++i) {
        ui_router_tick(0.05f);
    }
    int slow = settings_get_int(ids[1]);
    CHECK(slow > after_press);

    /* And it speeds up rather than plodding through three hundred taps. */
    for (int i = 0; i < 40; ++i) {
        ui_router_tick(0.05f);
    }
    int fast = settings_get_int(ids[1]);
    CHECK((fast - slow) > (slow - after_press));

    e.type = TOUCH_EVENT_UP;
    ui_router_event(&e);
    int settled = settings_get_int(ids[1]);
    ui_router_tick(0.5f);
    CHECK_EQ(settings_get_int(ids[1]), settled);      /* release stops it */
}

TEST_CASE(reset_button_restores_the_open_category)
{
    fresh_screen();
    setting_id_t ids[64];
    settings_in_category(SET_CAT_ESC, ids, 64);

    settings_set(ids[0], settings_def(ids[0])->max);
    settings_set(SET_BRIGHTNESS, 50);

    tap(CAT_X + 100, RESET_Y + 20);
    CHECK_EQ(settings_get_int(ids[0]), (long)settings_def(ids[0])->def);
    CHECK_EQ(settings_get_int(SET_BRIGHTNESS), 50);   /* other category kept */
}

TEST_CASE(leaving_the_screen_no_longer_saves_on_its_own)
{
    /*
     * It used to. A save is a flash write, and on this board that stalls
     * both cores including the one that beats the safety line, so it does
     * not happen at a moment nobody chose.
     */
    fresh_screen();
    settings_set_store(&s_mem_store);
    s_save_calls = 0;

    setting_id_t ids[64];
    settings_in_category(SET_CAT_ESC, ids, 64);
    tap(PLUS_X + BTN_W / 2, row_y(0));
    CHECK(settings_dirty());

    ui_router_goto(SCREEN_OVERVIEW);
    CHECK(settings_dirty());          /* still unwritten */
    CHECK_EQ(s_save_calls, 0);

    settings_set_store(NULL);
}

TEST_CASE(the_save_button_asks_and_the_application_decides_when)
{
    /*
     * The press is a request, not a write. Nothing reaches the store until
     * the application says the bench can afford to stop for it.
     */
    fresh_screen();
    settings_set_store(&s_mem_store);
    s_save_calls = 0;

    setting_id_t ids[64];
    settings_in_category(SET_CAT_ESC, ids, 64);
    tap(PLUS_X + BTN_W / 2, row_y(0));
    CHECK(settings_dirty());
    CHECK(!settings_save_asked());

    tap(CAT_X + 100, SAVE_Y + SAVE_H / 2);
    CHECK(settings_save_asked());
    CHECK_EQ(s_save_calls, 0);        /* asked, and nothing written */
    CHECK(settings_dirty());

    /* Not while the bench is busy, however long that lasts. */
    for (int i = 0; i < 20; ++i) {
        CHECK(!settings_save_tick(false));
    }
    CHECK_EQ(s_save_calls, 0);
    CHECK(settings_save_asked());

    /* And then, once. */
    CHECK(settings_save_tick(true));
    CHECK_EQ(s_save_calls, 1);
    CHECK(!settings_dirty());
    CHECK(!settings_save_asked());
    CHECK(!settings_save_tick(true));  /* nothing left to take */
    CHECK_EQ(s_save_calls, 1);

    settings_set_store(NULL);
}

TEST_CASE(the_request_outlives_the_screen)
{
    /* The operator has said what they want kept; walking away does not
     * unsay it, and the save still lands when the bench is idle. */
    fresh_screen();
    settings_set_store(&s_mem_store);
    s_save_calls = 0;

    setting_id_t ids[64];
    settings_in_category(SET_CAT_ESC, ids, 64);
    tap(PLUS_X + BTN_W / 2, row_y(0));
    tap(CAT_X + 100, SAVE_Y + SAVE_H / 2);
    CHECK(settings_save_asked());

    ui_router_goto(SCREEN_OVERVIEW);
    CHECK(settings_save_asked());
    CHECK_EQ(s_save_calls, 0);

    CHECK(settings_save_tick(true));
    CHECK_EQ(s_save_calls, 1);
    settings_set_store(NULL);
}

/* The mean of the SAVE button's face, as the panel would show it. */
static unsigned save_face(void)
{
    ui_router_render(&s_c, 0);
    unsigned long sum = 0;
    for (int y = SAVE_Y + 6; y < SAVE_Y + SAVE_H - 6; ++y) {
        for (int x = CAT_X + 6; x < CAT_X + 200; ++x) {
            sum += (unsigned long)s_fb[y * W + x];
        }
    }
    return (unsigned)(sum & 0xffffffffUL);
}

TEST_CASE(a_press_while_the_save_is_pending_changes_nothing)
{
    /*
     * Once asked, the button reads WHEN IDLE and is drawn inert. A press
     * must not light it either: a highlight on a button that is doing
     * nothing is the same lie the old SAVED line told.
     */
    fresh_screen();
    settings_set_store(&s_mem_store);
    s_save_calls = 0;

    setting_id_t ids[64];
    settings_in_category(SET_CAT_ESC, ids, 64);
    tap(PLUS_X + BTN_W / 2, row_y(0));

    /* Offered: holding it down changes the face. */
    const unsigned offered = save_face();
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = CAT_X + 100,
                                   .y = (int16_t)(SAVE_Y + SAVE_H / 2),
                                   .strength = 40 } };
    ui_router_event(&e);
    CHECK(save_face() != offered);
    e.type = TOUCH_EVENT_UP;
    ui_router_event(&e);
    CHECK(settings_save_asked());

    /* Asked for: holding it down does not. */
    const unsigned pending = save_face();
    e.type = TOUCH_EVENT_DOWN;
    ui_router_event(&e);
    CHECK_EQ(save_face(), pending);
    e.type = TOUCH_EVENT_UP;
    ui_router_event(&e);

    CHECK_EQ(s_save_calls, 0);
    CHECK(settings_save_asked());
    settings_set_store(NULL);
}

TEST_CASE(asking_with_nothing_to_write_asks_for_nothing)
{
    /* A button that presses with nothing to save says a save happened. */
    fresh_screen();
    settings_set_store(&s_mem_store);
    s_save_calls = 0;
    CHECK(!settings_dirty());

    tap(CAT_X + 100, SAVE_Y + SAVE_H / 2);
    CHECK(!settings_save_asked());
    CHECK(!settings_save_tick(true));
    CHECK_EQ(s_save_calls, 0);

    settings_cancel_save();
    settings_set_store(NULL);
}



/*
 * A hit held open acts on its release and the plus and minus keys repeat on
 * the frame timer while it stands, so a lost event must not leave either
 * running: the GT911 reuses track ids, and a later contact that began
 * somewhere else would be taken for this one.
 */
/* Panel coordinates, like the other constants here: the screen's own
 * OUTPUTS_Y is 232 and the band sits above it. */
#define OUTPUTS_Y (232 + UI_BAND_H)
#define DOOR_H    42

TEST_CASE(a_cancelled_hit_presses_nothing)
{
    fresh_screen();
    CHECK_EQ(ui_router_current(), SCREEN_SETUP);

    /* Press the door to the outputs screen, then the events stop arriving. */
    const int dy = OUTPUTS_Y + DOOR_H / 2;
    touch_event_t d = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = CAT_X + 100,
                                   .y = (int16_t)dy } };
    ui_router_event(&d);

    ui_router_cancel_gestures();

    /*
     * The release the screen never saw arrives on a reused track id.  The
     * hit is gone, so it opens nothing: a navigation from a press nobody
     * made is the small version of what the same latch costs on the outputs
     * screen, where the release applies a binding change.
     */
    touch_event_t u = { .type = TOUCH_EVENT_UP,
                        .point = { .id = 1, .x = CAT_X + 100,
                                   .y = (int16_t)dy } };
    ui_router_event(&u);
    CHECK_EQ(ui_router_current(), SCREEN_SETUP);

    /* And a fresh press still opens it, so nothing is stuck. */
    ui_router_event(&d);
    ui_router_event(&u);
    CHECK_EQ(ui_router_current(), SCREEN_OUTPUTS);
}

/*
 * A store that refuses leaves the values dirty.  The screen's label is the
 * whole of the feedback, and it reads SAVED only when nothing is left to
 * write; reporting that against a store that wrote nothing would be a claim
 * the next boot contradicts.
 */
TEST_CASE(a_refused_save_keeps_the_values_dirty)
{
    fresh_model();
    settings_set_store(&s_refusing_store);
    settings_init();

    settings_set(SET_MOTOR_POLES, 12.0f);
    CHECK(settings_dirty());
    CHECK(!settings_save_failed());

    CHECK(!settings_save());
    CHECK(settings_dirty());          /* still to be written */
    CHECK(settings_save_failed());
    CHECK(!settings_save_asked());    /* the request is answered */
    CHECK_EQ(s_save_calls, 1);
}

/*
 * No store at all is a failed save and not a no-op.  settings_set_store(NULL)
 * is what a panel gets when NVS cannot be brought up, and every save it takes
 * for the rest of that session writes nothing.
 */
TEST_CASE(a_missing_store_is_a_failed_save)
{
    fresh_model();
    settings_set_store(NULL);
    settings_init();

    settings_set(SET_MOTOR_POLES, 12.0f);
    CHECK(!settings_save());
    CHECK(settings_dirty());
    CHECK(settings_save_failed());
}

/* A store that takes them clears both the dirt and the failure. */
TEST_CASE(a_successful_save_retires_an_earlier_failure)
{
    fresh_model();
    settings_set_store(&s_refusing_store);
    settings_init();
    settings_set(SET_MOTOR_POLES, 12.0f);
    CHECK(!settings_save());
    CHECK(settings_save_failed());

    settings_set_store(&s_mem_store);
    CHECK(settings_save());
    CHECK(!settings_dirty());
    CHECK(!settings_save_failed());
}

/*
 * And so does an edit: the failure described values these no longer are, and
 * a stale NOT SAVED beside a number the operator has just changed says the
 * wrong thing about the wrong value.
 */
TEST_CASE(an_edit_retires_an_earlier_failure)
{
    fresh_model();
    settings_set_store(&s_refusing_store);
    settings_init();
    settings_set(SET_MOTOR_POLES, 12.0f);
    CHECK(!settings_save());
    CHECK(settings_save_failed());

    settings_set(SET_MOTOR_POLES, 14.0f);
    CHECK(!settings_save_failed());
    CHECK(settings_dirty());
}

/* The idle write reports what the store did rather than that it was tried. */
TEST_CASE(the_idle_save_reports_a_refusal)
{
    fresh_model();
    settings_set_store(&s_refusing_store);
    settings_init();
    settings_set(SET_MOTOR_POLES, 12.0f);
    settings_request_save();
    CHECK(settings_save_asked());

    CHECK(!settings_save_tick(true));
    CHECK(settings_dirty());
    CHECK(settings_save_failed());
}

/*
 * A reset that changes nothing is not an edit.  A category already holding
 * its defaults is reset to what it has; treating that as an edit would drop
 * NOT SAVED while the values whose write was refused are still the ones in
 * memory and nothing has been written since.
 */
TEST_CASE(a_reset_that_changes_nothing_keeps_the_failure)
{
    fresh_model();
    settings_set_store(&s_refusing_store);
    settings_init();

    settings_set(SET_MOTOR_POLES, 12.0f);
    CHECK(!settings_save());
    CHECK(settings_save_failed());

    /* APP holds its defaults, so this moves nothing. */
    settings_reset(SET_CAT_APP);
    CHECK(settings_save_failed());

    /* ESC / BENCH holds the edited pole count, so this does move something
     * and is an edit like any other. */
    settings_reset(SET_CAT_ESC);
    CHECK(!settings_save_failed());
    CHECK(settings_dirty());
}

/*
 * The write is taken outside this screen, by settings_save_tick() in the
 * panel's render loop, so a pending write completing or being refused moves
 * the button's state with no touch to invalidate the cached framebuffers.
 * The screen has to notice that itself, or a refusal goes on drawing
 * WHEN IDLE until something else repaints.
 */
TEST_CASE(a_refused_pending_write_repaints_the_button)
{
    fresh_screen();
    settings_set_store(&s_refusing_store);
    settings_init();

    settings_set(SET_MOTOR_POLES, 12.0f);
    settings_request_save();
    CHECK(settings_save_asked());

    /* Drawn once as WHEN IDLE, with the chrome cached for this buffer. */
    ui_router_render(&s_c, 0);
    static gfx_color_t before[(size_t)W * H];
    memcpy(before, s_fb, sizeof(before));

    /* The write is taken and refused, away from any touch. */
    CHECK(!settings_save_tick(true));
    CHECK(settings_save_failed());

    ui_router_tick(0.05f);
    ui_router_render(&s_c, 0);
    CHECK(memcmp(before, s_fb, sizeof(before)) != 0);
}

/* The output encoder: off at the start, a centre inside the sensor's 12
 * bits, in INTERFACES beside the sensor bus. */
TEST_CASE(the_encoder_settings_start_off_with_a_12_bit_centre)
{
    settings_set_store(NULL);
    settings_init();
    CHECK_EQ(settings_get_int(SET_ENC_EN), 0);
    CHECK_EQ(settings_get_int(SET_ENC_CENTRE), 0);
    const setting_def_t *d = settings_def(SET_ENC_EN);
    CHECK_EQ(d->type, SET_TYPE_BOOL);
    CHECK_EQ(d->cat, SET_CAT_IFACE);
    CHECK_STR_EQ(d->label, "AS5600");
    d = settings_def(SET_ENC_CENTRE);
    CHECK_EQ(d->type, SET_TYPE_INT);
    CHECK_EQ(d->cat, SET_CAT_IFACE);
    CHECK_EQ((int)d->min, 0);
    CHECK_EQ((int)d->max, 4095);
    settings_set(SET_ENC_CENTRE, 5000.0f);
    CHECK_EQ(settings_get_int(SET_ENC_CENTRE), 4095);
    settings_set(SET_ENC_CENTRE, -3.0f);
    CHECK_EQ(settings_get_int(SET_ENC_CENTRE), 0);
}

/* -------------------------------------------------- gestures, tracker-fed */

/*
 * The cases below put finger frames through the tracker and the router
 * (touch_feed.h), one report every REPORT_S with the frame's tick after it,
 * and compare every setting before and after.  Coordinates are the
 * panel's.  XL is on a row's label, XM and XP are the centres of the "-"
 * and the "+" column.
 */
#define REPORT_S 0.010f
#define XL       400
#define XM       (MINUS_X + BTN_W / 2)
#define XP       (PLUS_X + BTN_W / 2)

static float s_before[SETTING_COUNT];

static void snap(void)
{
    for (int i = 0; i < SETTING_COUNT; ++i) {
        s_before[i] = settings_get((setting_id_t)i);
    }
}

/* How many settings differ from the last snap(). */
static int changed(void)
{
    int n = 0;
    for (int i = 0; i < SETTING_COUNT; ++i) {
        n += (settings_get((setting_id_t)i) != s_before[i]) ? 1 : 0;
    }
    return n;
}

/* Frames with nothing new on the glass. */
static void idle(float seconds)
{
    const int n = (int)lroundf(seconds / REPORT_S);
    for (int i = 0; i < n; ++i) {
        ui_router_tick(REPORT_S);
    }
}

/* SETUP on @p cat at its defaults, the list at its top. */
static void gesture_screen(setting_cat_t cat)
{
    fresh_screen();
    feed_reset();
    feed_tick_per_report(REPORT_S);
    if (cat != SET_CAT_ESC) {
        feed_tap(FEED_LONE, CAT_X + 100,
                 CAT_Y + (int)cat * (CAT_H + CAT_GAP) + CAT_H / 2);
    }
    snap();
}

static void swipe(int x0, int y0, int x1, int y1, int step)
{
    finger(FEED_LONE, x0, y0);
    glide(FEED_LONE, x1, y1, step);
    lift(FEED_LONE);
}

/* The row of @p id in its category, or -1. */
static int row_of(setting_id_t id)
{
    setting_id_t ids[64];
    const int n = settings_in_category(settings_def(id)->cat, ids, 64);
    for (int i = 0; i < n; ++i) {
        if (ids[i] == id) {
            return i;
        }
    }
    return -1;
}

/* Steps @p id stands from its default, in units of its own step. */
static int steps_off(setting_id_t id)
{
    const setting_def_t *d = settings_def(id);
    return (int)lroundf((settings_get(id) - d->def) / d->step);
}

/*
 * Swipes of 150 px that start on column x, from start points 29 px apart
 * down the list, up and down, at seven speeds, on every category.  The
 * upward ones from the upper rows run into the band.  A downward swipe
 * starts on a list scrolled 100 px, so it has somewhere to go.  Returns how
 * many changed a setting; *unscrolled counts those that stayed in the body,
 * moved as one contact and left the list where it was although it had room.
 */
static int swipe_matrix(const int *cols, int ncols, int *unscrolled,
                        int *total)
{
    static const int speeds[] = { 2, 4, 9, 20, 60, 119, 130 };
    int bad = 0;
    *unscrolled = 0;
    *total = 0;
    for (int cat = 0; cat < SET_CAT_SETUP_COUNT; ++cat) {
        for (int c = 0; c < ncols; ++c) {
            for (size_t sp = 0; sp < sizeof(speeds) / sizeof(speeds[0]);
                 ++sp) {
                for (int y0 = LIST_Y + 4; y0 < LIST_Y + 400; y0 += 29) {
                    for (int dir = -1; dir <= 1; dir += 2) {
                        gesture_screen((setting_cat_t)cat);
                        if (dir > 0) {
                            swipe(XL, LIST_Y + 300, XL, LIST_Y + 192, 4);
                            snap();
                        }
                        int max = 0;
                        const int from = settings_screen_scroll(&max);
                        int y1 = y0 + dir * 150;
                        if (y1 < 2)   { y1 = 2; }
                        if (y1 > 479) { y1 = 479; }
                        swipe(cols[c], y0, cols[c], y1, speeds[sp]);
                        idle(0.5f);
                        ++*total;
                        bad += (changed() != 0) ? 1 : 0;
                        const bool room = (dir < 0) ? from < max : from > 0;
                        if (speeds[sp] <= TOUCH_JUMP_PX && y1 >= UI_BAND_H
                            && room
                            && settings_screen_scroll(NULL) == from) {
                            ++*unscrolled;
                        }
                    }
                }
            }
        }
    }
    return bad;
}

/* The defect: a key stepped on its DOWN, so a scroll that started on a key
 * column had changed the setting under the finger before it scrolled. */
TEST_CASE(a_swipe_that_starts_on_a_key_column_scrolls_and_changes_no_value)
{
    static const int cols[] = { MINUS_X, XM, MINUS_X + BTN_W - 1,
                                PLUS_X, XP, PLUS_X + BTN_W - 1 };
    int unscrolled = 0, total = 0;
    const int bad = swipe_matrix(cols, 6, &unscrolled, &total);
    CHECK_EQ(total, 3 * 6 * 7 * 14 * 2);
    CHECK_EQ(bad, 0);
    CHECK_EQ(unscrolled, 0);
}

/* The same beside the columns, one px outside each edge among them. */
TEST_CASE(a_swipe_beside_the_key_columns_scrolls_and_changes_no_value)
{
    static const int cols[] = { 240, XL, MINUS_X - 11, MINUS_X - 1,
                                MINUS_X + BTN_W, 660, PLUS_X - 1,
                                PLUS_X + BTN_W, 783 };
    int unscrolled = 0, total = 0;
    const int bad = swipe_matrix(cols, 9, &unscrolled, &total);
    CHECK_EQ(total, 3 * 9 * 7 * 14 * 2);
    CHECK_EQ(bad, 0);
    CHECK_EQ(unscrolled, 0);
}

/* What a tester's panel showed: INA3221 shunt 99.9 mOhm and Sensor SDA 15
 * after the INTERFACES list was scrolled twice from its bottom row. */
TEST_CASE(two_scrolls_from_the_bottom_row_leave_the_shunt_and_the_pins)
{
    gesture_screen(SET_CAT_IFACE);
    CHECK_EQ(row_of(SET_INA3221_MOHM), 6);
    CHECK_EQ(row_of(SET_SENSE_SDA), 8);
    swipe(XM, LIST_Y + 376, XM, LIST_Y + 252, 4);
    CHECK_EQ(settings_screen_scroll(NULL), 116);
    swipe(XM, LIST_Y + 376, XM, LIST_Y + 252, 4);
    CHECK_EQ(settings_screen_scroll(NULL), 232);
    CHECK_NEAR(settings_get(SET_INA3221_MOHM), 100.0f, 1e-4);
    CHECK_EQ(settings_get_int(SET_SENSE_SDA), 16);
    CHECK_EQ(changed(), 0);
}

/* One tap, one step: "-" and "+" on a number, "<" and ">" on a switch and
 * on a choice.  The press alone steps nothing. */
TEST_CASE(a_tap_steps_each_kind_of_key_once)
{
    bool seen[4] = { false, false, false, false };
    for (int cat = 0; cat < SET_CAT_SETUP_COUNT; ++cat) {
        setting_id_t ids[64];
        const int n = settings_in_category((setting_cat_t)cat, ids, 64);
        for (int row = 0; row < n && row < 6; ++row) {
            const setting_def_t *d = settings_def(ids[row]);
            if (seen[d->type]) {
                continue;
            }
            seen[d->type] = true;
            for (int by = -1; by <= 1; by += 2) {
                gesture_screen((setting_cat_t)cat);
                /* Off both ends, so either key has a step to make. */
                if (d->type == SET_TYPE_INT || d->type == SET_TYPE_FLOAT) {
                    settings_set(ids[row], d->min + 3.0f * d->step);
                }
                snap();
                const float was = settings_get(ids[row]);
                settings_adjust(ids[row], by);
                const float want = settings_get(ids[row]);
                settings_set(ids[row], was);
                CHECK(want != was);

                finger(FEED_LONE, (by < 0) ? XM : XP, row_y(row));
                idle(0.20f);
                CHECK_EQ(changed(), 0);               /* armed, not stepped */
                lift(FEED_LONE);
                CHECK(settings_get(ids[row]) == want);
                CHECK_EQ(changed(), 1);
                idle(1.0f);
                CHECK(settings_get(ids[row]) == want);
            }
        }
    }
    CHECK(seen[SET_TYPE_INT]);
    CHECK(seen[SET_TYPE_FLOAT]);
    CHECK(seen[SET_TYPE_BOOL]);
    CHECK(seen[SET_TYPE_ENUM]);
}

/* A key held: nothing for 0.45 s, then 8 steps a second, 30 a second from
 * 2.25 s.  5 steps after 1.00 s and 38 after 3.00 s. */
TEST_CASE(a_held_key_repeats_five_times_in_one_second_and_38_in_three)
{
    gesture_screen(SET_CAT_IFACE);
    const int y = row_y(row_of(SET_INA3221_MOHM));
    finger(FEED_LONE, XM, y);                 /* its tick: 0.01 s held */
    idle(0.43f);
    CHECK_EQ(steps_off(SET_INA3221_MOHM), 0);
    idle(0.56f);
    CHECK_EQ(steps_off(SET_INA3221_MOHM), -5);
    idle(2.00f);
    CHECK_EQ(steps_off(SET_INA3221_MOHM), -38);
    lift(FEED_LONE);
    CHECK_EQ(steps_off(SET_INA3221_MOHM), -38);  /* the release adds none */
    idle(1.0f);
    CHECK_EQ(steps_off(SET_INA3221_MOHM), -38);
    CHECK_EQ(changed(), 1);
}

/* The hold's limit: 449 ms is a tap that has not stepped yet, 450 ms is the
 * first step of the repeat.  One frame of exactly that length; the timer
 * adds frame times and reads no tick counter, so it has no wrap. */
TEST_CASE(the_repeat_starts_at_450_ms)
{
    static const struct { float held; int while_down; } k[] = {
        { 0.449f, 0 }, { 0.450f, 1 }, { 0.451f, 1 },
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        gesture_screen(SET_CAT_IFACE);
        feed_tick_per_report(0.0f);
        const int y = row_y(row_of(SET_INA3221_MOHM));
        finger(FEED_LONE, XP, y);
        ui_router_tick(k[i].held);
        CHECK_EQ(steps_off(SET_INA3221_MOHM), k[i].while_down);
        lift(FEED_LONE);
        /* One step either way: the tap's, or the repeat's first. */
        CHECK_EQ(steps_off(SET_INA3221_MOHM), 1);
    }
    /* In 10 ms frames: 44 are short of it and 46 are past it. */
    for (int frames = 44; frames <= 46; frames += 2) {
        gesture_screen(SET_CAT_IFACE);
        feed_tick_per_report(0.0f);
        finger(FEED_LONE, XP, row_y(row_of(SET_INA3221_MOHM)));
        for (int f = 0; f < frames; ++f) {
            ui_router_tick(REPORT_S);
        }
        CHECK_EQ(steps_off(SET_INA3221_MOHM), (frames == 44) ? 0 : 1);
        /* A swipe from here leaves what the hold has stepped and adds
         * nothing. */
        glide(FEED_LONE, XP, row_y(row_of(SET_INA3221_MOHM)) - 100, 9);
        lift(FEED_LONE);
        CHECK_EQ(steps_off(SET_INA3221_MOHM), (frames == 44) ? 0 : 1);
    }
}

/* A held key that starts to scroll: it repeats while the finger is within
 * the 8 px, and stops at 9 px. */
TEST_CASE(a_hold_that_turns_into_a_swipe_stops_repeating_at_the_slop)
{
    for (int dir = -1; dir <= 1; dir += 2) {
        gesture_screen(SET_CAT_IFACE);
        const int y = row_y(row_of(SET_INA3221_MOHM));
        finger(FEED_LONE, XM, y);
        idle(0.99f);
        CHECK_EQ(steps_off(SET_INA3221_MOHM), -5);
        feed_tick_per_report(0.0f);
        glide(FEED_LONE, XM, y + dir * 8, 1);
        idle(0.50f);                          /* 1.50 s held: 4 more */
        CHECK_EQ(steps_off(SET_INA3221_MOHM), -9);
        CHECK_EQ(settings_screen_scroll(NULL), 0);
        glide(FEED_LONE, XM, y + dir * 9, 1);
        idle(1.00f);
        CHECK_EQ(steps_off(SET_INA3221_MOHM), -9);
        /* Up the glass scrolls the list on; down, it is at its top. */
        CHECK_EQ(settings_screen_scroll(NULL), (dir < 0) ? 1 : 0);
        glide(FEED_LONE, XM, y + dir * 20, 1);
        lift(FEED_LONE);
        idle(1.00f);
        CHECK_EQ(steps_off(SET_INA3221_MOHM), -9);
        CHECK_EQ(changed(), 1);
    }
}

/* The slop of a tap: 8 px from the press in x or in y is a tap, 9 px is
 * not. */
TEST_CASE(a_tap_may_move_8_px_and_not_9)
{
    static const struct { int dx, dy, steps; } k[] = {
        {  0,  0, 1 },
        {  8,  0, 1 }, {  9,  0, 0 }, { -8,  0, 1 }, { -9,  0, 0 },
        {  0,  8, 1 }, {  0,  9, 0 }, {  0, -8, 1 }, {  0, -9, 0 },
        {  8,  8, 1 }, {  8, -8, 1 }, {  9,  9, 0 },
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        for (int stride = 1; stride <= 9; stride += 8) {
            gesture_screen(SET_CAT_IFACE);
            const int y = row_y(row_of(SET_INA3221_MOHM));
            finger(FEED_LONE, XP, y);
            /* Px by px, and in one report. */
            glide(FEED_LONE, XP + k[i].dx, y + k[i].dy, stride);
            lift(FEED_LONE);
            idle(0.5f);
            CHECK_EQ(steps_off(SET_INA3221_MOHM), k[i].steps);
            CHECK_EQ(changed(), k[i].steps);
        }
    }
}

/* Sideways off a key gives it up: no step on the release, and no repeat
 * while the finger rests where it went. */
TEST_CASE(a_press_that_slides_off_a_key_sideways_steps_nothing)
{
    gesture_screen(SET_CAT_IFACE);
    const int y = row_y(row_of(SET_INA3221_MOHM));

    /* 300 px left onto the label, and held there. */
    finger(FEED_LONE, XM, y);
    glide(FEED_LONE, XM - 300, y, 5);
    idle(1.0f);
    CHECK_EQ(changed(), 0);
    lift(FEED_LONE);
    CHECK_EQ(changed(), 0);

    /* From "-" onto "+" of the same row. */
    finger(FEED_LONE, XM, y);
    glide(FEED_LONE, XP, y, 5);
    idle(1.0f);
    lift(FEED_LONE);
    CHECK_EQ(changed(), 0);

    /* Out of the key within the slop: 5 px left of its edge. */
    finger(FEED_LONE, MINUS_X, y);
    glide(FEED_LONE, MINUS_X - 5, y, 1);
    idle(1.0f);
    CHECK_EQ(changed(), 0);
    lift(FEED_LONE);
    CHECK_EQ(changed(), 0);

    /* And back onto it does not arm it again. */
    finger(FEED_LONE, MINUS_X, y);
    glide(FEED_LONE, MINUS_X - 5, y, 1);
    glide(FEED_LONE, MINUS_X + 2, y, 1);
    idle(1.0f);
    lift(FEED_LONE);
    CHECK_EQ(changed(), 0);
    CHECK_EQ(settings_screen_scroll(NULL), 0);
}

/* A key is its column over its own row.  The 4 px between two rows belong
 * to neither: a tap there steps nothing, and a press that leaves the row
 * into the gap is not a tap. */
TEST_CASE(the_gap_between_two_rows_is_no_key)
{
    gesture_screen(SET_CAT_IFACE);
    setting_id_t ids[64];
    settings_in_category(SET_CAT_IFACE, ids, 64);
    const int row = 2;
    const int top = LIST_Y + row * ROW_PITCH;
    for (int y = top + ROW_H; y < top + ROW_PITCH; ++y) {
        feed_tap(FEED_LONE, XM, y);
        feed_tap(FEED_LONE, XP, y);
    }
    CHECK_EQ(changed(), 0);

    /* The row's first and last px are the key. */
    feed_tap(FEED_LONE, XM, top);
    CHECK(settings_get(ids[row]) != s_before[ids[row]]);
    CHECK_EQ(changed(), 1);
    feed_tap(FEED_LONE, XP, top + ROW_H - 1);
    CHECK_EQ(changed(), 0);

    finger(FEED_LONE, XM, top + ROW_H - 1);
    glide(FEED_LONE, XM, top + ROW_H + 2, 1);
    lift(FEED_LONE);
    CHECK_EQ(changed(), 0);
}

/* One contact holds the screen.  A second one presses nothing and ends
 * nothing. */
TEST_CASE(a_second_finger_does_nothing_on_the_setup_screen)
{
    gesture_screen(SET_CAT_IFACE);
    const int y = row_y(row_of(SET_INA3221_MOHM));

    /* The first scrolls on the label; the second rests 2 s on a key. */
    finger(0, XL, LIST_Y + 300);
    glide(0, XL, LIST_Y + 260, 4);
    const int mid = settings_screen_scroll(NULL);
    CHECK(mid > 0);
    finger(1, XM, y);
    idle(2.0f);
    CHECK_EQ(changed(), 0);
    glide(0, XL, LIST_Y + 220, 4);            /* the first goes on scrolling */
    CHECK_EQ(settings_screen_scroll(NULL), mid + 40);
    lift(1);
    CHECK_EQ(changed(), 0);
    lift(0);
    CHECK_EQ(changed(), 0);

    /* The first rests on the label; the second taps a key, then holds it. */
    gesture_screen(SET_CAT_IFACE);
    finger(0, XL, y);
    finger(1, XM, y);
    lift(1);
    finger(1, XP, y);
    idle(2.0f);
    lift(1);
    CHECK_EQ(changed(), 0);
    lift(0);
    CHECK_EQ(changed(), 0);

    /* The first holds a key; the second's tap elsewhere does not end the
     * repeat and steps nothing of its own. */
    gesture_screen(SET_CAT_IFACE);
    finger(0, XM, y);
    idle(0.99f);
    CHECK_EQ(steps_off(SET_INA3221_MOHM), -5);
    feed_tick_per_report(0.0f);
    finger(1, XP, row_y(1));
    lift(1);
    finger(1, XL, row_y(2));
    lift(1);
    idle(0.50f);
    CHECK_EQ(steps_off(SET_INA3221_MOHM), -9);
    lift(0);
    CHECK_EQ(changed(), 1);

    /* With the glass clear the next tap is taken, whichever id it has. */
    feed_tap(3, XP, y);
    CHECK_EQ(steps_off(SET_INA3221_MOHM), -8);
}

/* A contact the controller leaves out of one report arrives as an UP and a
 * new DOWN.  Mid-swipe, the new DOWN travels on and is no tap. */
TEST_CASE(a_contact_that_drops_out_of_a_swipe_changes_no_value)
{
    /* On the label, and it returns on a key column. */
    gesture_screen(SET_CAT_IFACE);
    finger(FEED_LONE, XL, LIST_Y + 376);
    glide(FEED_LONE, XL, LIST_Y + 330, 9);
    lift(FEED_LONE);
    finger(FEED_LONE, XM, LIST_Y + 290);
    glide(FEED_LONE, XM, LIST_Y + 150, 9);
    lift(FEED_LONE);
    idle(0.5f);
    CHECK_EQ(changed(), 0);

    /* On a key column from the start. */
    gesture_screen(SET_CAT_IFACE);
    finger(FEED_LONE, XM, LIST_Y + 376);
    glide(FEED_LONE, XM, LIST_Y + 330, 9);
    lift(FEED_LONE);
    finger(FEED_LONE, XM, LIST_Y + 321);
    glide(FEED_LONE, XM, LIST_Y + 150, 9);
    lift(FEED_LONE);
    idle(0.5f);
    CHECK_EQ(changed(), 0);
    CHECK(settings_screen_scroll(NULL) > 100);
}

/* More than 120 px between two reports: the tracker ends the contact with a
 * flagged UP and starts one with a flagged DOWN.  Neither steps a key. */
TEST_CASE(a_flick_the_tracker_splits_changes_no_value)
{
    /* Down a key column, 140 px a report, lifted on the column. */
    gesture_screen(SET_CAT_IFACE);
    finger(FEED_LONE, XM, LIST_Y + 376);
    finger(FEED_LONE, XM, LIST_Y + 236);
    finger(FEED_LONE, XM, LIST_Y + 96);
    lift(FEED_LONE);
    idle(0.5f);
    CHECK_EQ(feed_downs, 4);                  /* the category's tap, and 3 */
    CHECK_EQ(changed(), 0);

    /* From the label onto a key in one report, then a swipe. */
    gesture_screen(SET_CAT_IFACE);
    finger(FEED_LONE, XM - 70, LIST_Y + 376);
    finger(FEED_LONE, XM, LIST_Y + 296);
    glide(FEED_LONE, XM, LIST_Y + 150, 9);
    lift(FEED_LONE);
    idle(0.5f);
    CHECK_EQ(changed(), 0);

    /* From the label onto a key in one report, lifted in the next: a DOWN
     * and an UP in one place, which is a tap but for the DOWN's flag. */
    gesture_screen(SET_CAT_IFACE);
    finger(FEED_LONE, XL, LIST_Y + 376);
    finger(FEED_LONE, XM, LIST_Y + 236);
    lift(FEED_LONE);
    idle(0.5f);
    CHECK_EQ(changed(), 0);
    /* It rests there instead: no repeat either. */
    finger(FEED_LONE, XL, LIST_Y + 376);
    finger(FEED_LONE, XM, LIST_Y + 236);
    idle(2.0f);
    lift(FEED_LONE);
    CHECK_EQ(changed(), 0);

    /* The tap after it is a tap. */
    feed_tap(FEED_LONE, XM, row_y(row_of(SET_INA3221_MOHM)));
    CHECK_EQ(steps_off(SET_INA3221_MOHM), -1);
}

/* A jump's DOWN is a press on every control that is not a key of the
 * scrolling list: a category pressed right after a touch elsewhere whose
 * release frame went missing is still chosen. */
TEST_CASE(a_category_takes_the_press_the_tracker_made_for_a_jump)
{
    gesture_screen(SET_CAT_ESC);
    setting_id_t app[64];
    settings_in_category(SET_CAT_APP, app, 64);
    finger(FEED_LONE, XL, LIST_Y + 300);
    finger(FEED_LONE, CAT_X + 100,
           CAT_Y + (int)SET_CAT_APP * (CAT_H + CAT_GAP) + CAT_H / 2);
    lift(FEED_LONE);
    snap();
    feed_tap(FEED_LONE, XP, row_y(0));
    CHECK(settings_get(app[0]) != s_before[app[0]]);
    CHECK_EQ(changed(), 1);
}

/* A key pressed and carried into the band: the router releases it for the
 * screen where the screen last saw it, which is on the key.  That release
 * is not the finger's and steps nothing; and nothing repeats afterwards
 * (v0.14.0 went on stepping with no finger on the glass). */
TEST_CASE(a_key_carried_into_the_band_steps_nothing_and_never_repeats)
{
    /* Straight into the band in one report: the release is at the press. */
    gesture_screen(SET_CAT_IFACE);
    finger(FEED_LONE, XM, LIST_Y + 4);
    finger(FEED_LONE, XM, UI_BAND_H - 8);
    CHECK_EQ(changed(), 0);
    idle(3.0f);
    CHECK_EQ(changed(), 0);
    lift(FEED_LONE);
    idle(3.0f);
    CHECK_EQ(changed(), 0);

    /* 8 px up inside the body first. */
    gesture_screen(SET_CAT_IFACE);
    finger(FEED_LONE, XM, LIST_Y + 12);
    finger(FEED_LONE, XM, LIST_Y + 4);
    finger(FEED_LONE, XM, UI_BAND_H - 8);
    idle(3.0f);
    lift(FEED_LONE);
    idle(3.0f);
    CHECK_EQ(changed(), 0);

    /* Held until it repeats, then into the band: the repeat ends there. */
    gesture_screen(SET_CAT_IFACE);
    finger(FEED_LONE, XP, row_y(2));
    idle(0.99f);
    setting_id_t ids[64];
    settings_in_category(SET_CAT_IFACE, ids, 64);
    const float held = settings_get(ids[2]);
    CHECK(held != s_before[ids[2]]);
    finger(FEED_LONE, XP, row_y(2) - 100);
    finger(FEED_LONE, XP, UI_BAND_H - 8);
    idle(3.0f);
    lift(FEED_LONE);
    idle(3.0f);
    CHECK(settings_get(ids[2]) == held);

    /* The same contact back in the body is the router's: it steps nothing.
     * The next one is a tap. */
    gesture_screen(SET_CAT_IFACE);
    finger(FEED_LONE, XM, LIST_Y + 4);
    finger(FEED_LONE, XM, UI_BAND_H - 8);
    finger(FEED_LONE, XM, LIST_Y + 4);
    idle(1.0f);
    lift(FEED_LONE);
    CHECK_EQ(changed(), 0);
    feed_tap(FEED_LONE, XM, LIST_Y + 4);
    CHECK_EQ(changed(), 1);
}

/* The same release on the buttons of the left column: a category, RESET
 * CATEGORY and the doors act on a release the finger made. */
TEST_CASE(a_button_carried_into_the_band_is_not_pressed)
{
    gesture_screen(SET_CAT_ESC);
    setting_id_t esc[64];
    settings_in_category(SET_CAT_ESC, esc, 64);

    /* The second category, 101 px under the band's edge, in one report. */
    finger(FEED_LONE, CAT_X + 100, CAT_Y + CAT_H + CAT_GAP + 5);
    finger(FEED_LONE, CAT_X + 100, UI_BAND_H - 8);
    lift(FEED_LONE);
    feed_tap(FEED_LONE, XP, row_y(0));
    CHECK(settings_get(esc[0]) != s_before[esc[0]]);  /* ESC still open */
    CHECK_EQ(changed(), 1);

    /* The tracker's own release, of a jump: RESET CATEGORY pressed, the
     * contact next reported 300 px away. */
    finger(FEED_LONE, CAT_X + 100, RESET_Y + RESET_H / 2);
    finger(FEED_LONE, XL, RESET_Y - 200);
    lift(FEED_LONE);
    CHECK_EQ(changed(), 1);                   /* not reset */
    CHECK_EQ(ui_router_current(), SCREEN_SETUP);

    /* And a tap on it resets. */
    feed_tap(FEED_LONE, CAT_X + 100, RESET_Y + RESET_H / 2);
    CHECK_EQ(changed(), 0);
}

/* The columns' outer edges, and the scroll bar 19 px right of "+". */
TEST_CASE(a_drag_at_the_edge_of_the_plus_column_and_on_the_scroll_bar)
{
    gesture_screen(SET_CAT_IFACE);
    swipe(PLUS_X + BTN_W - 1, LIST_Y + 376, PLUS_X + BTN_W - 1,
          LIST_Y + 176, 9);
    CHECK_EQ(changed(), 0);
    CHECK_EQ(settings_screen_scroll(NULL), 200);

    gesture_screen(SET_CAT_IFACE);
    swipe(789, LIST_Y + 376, 789, LIST_Y + 176, 9);
    CHECK_EQ(changed(), 0);
    CHECK_EQ(settings_screen_scroll(NULL), 0);
}

/* A press whose release is lost, or that the screen is left under, holds
 * nothing afterwards: no repeat, and the next touch is taken. */
TEST_CASE(a_key_press_that_loses_its_release_stops_and_holds_nothing)
{
    gesture_screen(SET_CAT_IFACE);
    const int y = row_y(row_of(SET_INA3221_MOHM));
    finger(0, XM, y);
    idle(0.99f);
    CHECK_EQ(steps_off(SET_INA3221_MOHM), -5);
    ui_router_cancel_gestures();
    idle(2.0f);
    CHECK_EQ(steps_off(SET_INA3221_MOHM), -5);
    lift(0);
    CHECK_EQ(steps_off(SET_INA3221_MOHM), -5);
    feed_tap(1, XM, y);
    CHECK_EQ(steps_off(SET_INA3221_MOHM), -6);

    /* Left under the finger and entered again. */
    finger(0, XM, y);
    ui_router_goto(SCREEN_OVERVIEW);
    ui_router_goto(SCREEN_SETUP);
    idle(2.0f);
    lift(0);
    CHECK_EQ(steps_off(SET_INA3221_MOHM), -6);
    feed_tap(1, XM, y);
    CHECK_EQ(steps_off(SET_INA3221_MOHM), -7);
}

/* ------------------------------------------------ the sensor bus's pins */

/* Whether the coprocessor's page, on a bank reserved as the firmware
 * reserves it at boot, takes a part enabled on @p sda and the GPIO after. */
static bool page_takes(unsigned sda)
{
    static outputs_t    o;
    static sense_page_t pg;
    outputs_init(&o, 0u);
    outputs_reserve_pins(&o, outbind_reserved_mask(IOMCU_BOARD_ID)
                                 | IOMCU_RESERVED_PINS);
    sense_page_init(&pg);
    const uint16_t bus[4] = { LINK_SN_EN_I3221, (uint16_t)sda,
                              (uint16_t)(sda + 1u), LINK_SN_KHZ_BUS };
    return sense_page_write(&pg, LINK_SN_ENABLE, 4u, bus, &o, 0u) == 0u;
}

/* The pins Sensor SDA steps through, in order, after -1. */
static const int k_sda[] = { 0, 4, 6, 14, 16, 18, 20, 26 };
#define N_SDA ((int)(sizeof(k_sda) / sizeof(k_sda[0])))

/* Both sides, for every GPIO: the setting takes a pin exactly when the
 * page takes the bus on it. */
TEST_CASE(sensor_sda_takes_the_pins_the_page_takes_and_no_other)
{
    int n = 0;
    for (unsigned gpio = 0u; gpio < 64u; ++gpio) {
        const bool page = page_takes(gpio);
        if (settings_sense_sda_valid((int)gpio) != page) {
            T_FAIL("GP%u: the setting %d, the page %d", gpio,
                   (int)settings_sense_sda_valid((int)gpio), (int)page);
        }
        if (page) {
            CHECK(n < N_SDA);
            CHECK_EQ((int)gpio, k_sda[n < N_SDA ? n : 0]);
            ++n;
        }
    }
    CHECK_EQ(n, N_SDA);
    /* The reserved pins and the ones the module does not bring out. */
    CHECK(!settings_sense_sda_valid(2));     /* GP3, the heartbeat */
    CHECK(!settings_sense_sda_valid(8));     /* GP8 to GP12, the CAN part */
    CHECK(!settings_sense_sda_valid(10));
    CHECK(!settings_sense_sda_valid(12));
    CHECK(!settings_sense_sda_valid(22));    /* GP23 */
    CHECK(!settings_sense_sda_valid(24));
    CHECK(!settings_sense_sda_valid(28));    /* GP29 */
    CHECK(!settings_sense_sda_valid(30));
    CHECK(!settings_sense_sda_valid(15));
    CHECK(!settings_sense_sda_valid(17));
    /* -1 is the pins not set; nothing else below 0 or past the bank. */
    CHECK(settings_sense_sda_valid(-1));
    CHECK(!settings_sense_sda_valid(-2));
    CHECK(!settings_sense_sda_valid(62));
    CHECK(!settings_sense_sda_valid(63));
    CHECK(!settings_sense_sda_valid(64));
    CHECK(!settings_sense_sda_valid(1000));
}

/* From every value, one step either way lands on the next value the bus
 * can have, and stops at -1 and at GP26.  SCL is the GPIO after, or -1. */
TEST_CASE(sensor_sda_steps_from_pin_to_pin_and_scl_follows)
{
    fresh_model();
    int seq[N_SDA + 1];
    seq[0] = -1;
    for (int i = 0; i < N_SDA; ++i) {
        seq[i + 1] = k_sda[i];
    }
    for (int i = 0; i <= N_SDA; ++i) {
        const int up   = seq[(i < N_SDA) ? i + 1 : N_SDA];
        const int down = seq[(i > 0) ? i - 1 : 0];
        settings_set(SET_SENSE_SDA, (float)seq[i]);
        CHECK_EQ(settings_get_int(SET_SENSE_SDA), seq[i]);
        CHECK_EQ(settings_get_int(SET_SENSE_SCL),
                 (seq[i] < 0) ? -1 : seq[i] + 1);
        settings_adjust(SET_SENSE_SDA, 1);
        CHECK_EQ(settings_get_int(SET_SENSE_SDA), up);
        CHECK_EQ(settings_get_int(SET_SENSE_SCL), (up < 0) ? -1 : up + 1);
        CHECK(settings_sense_sda_valid(settings_get_int(SET_SENSE_SDA)));
        settings_set(SET_SENSE_SDA, (float)seq[i]);
        settings_adjust(SET_SENSE_SDA, -1);
        CHECK_EQ(settings_get_int(SET_SENSE_SDA), down);
        CHECK_EQ(settings_get_int(SET_SENSE_SCL),
                 (down < 0) ? -1 : down + 1);
        CHECK(settings_sense_sda_valid(settings_get_int(SET_SENSE_SDA)));
    }
    /* Several steps at once, and more than there are. */
    settings_set(SET_SENSE_SDA, 16.0f);
    settings_adjust(SET_SENSE_SDA, 2);
    CHECK_EQ(settings_get_int(SET_SENSE_SDA), 20);
    settings_adjust(SET_SENSE_SDA, -3);
    CHECK_EQ(settings_get_int(SET_SENSE_SDA), 14);
    settings_adjust(SET_SENSE_SDA, 100);
    CHECK_EQ(settings_get_int(SET_SENSE_SDA), 26);
    CHECK_EQ(settings_get_int(SET_SENSE_SCL), 27);
    settings_adjust(SET_SENSE_SDA, -100);
    CHECK_EQ(settings_get_int(SET_SENSE_SDA), -1);
    CHECK_EQ(settings_get_int(SET_SENSE_SCL), -1);
    /* A value set that the bus cannot have is the default, as a stored
     * one is. */
    settings_set(SET_SENSE_SDA, 15.0f);
    CHECK_EQ(settings_get_int(SET_SENSE_SDA), 16);
    CHECK_EQ(settings_get_int(SET_SENSE_SCL), 17);
    settings_set(SET_SENSE_SDA, 26.0f);
    settings_set(SET_SENSE_SDA, 48.0f);
    CHECK_EQ(settings_get_int(SET_SENSE_SDA), 16);
    settings_set(SET_SENSE_SDA, -7.0f);
    CHECK_EQ(settings_get_int(SET_SENSE_SDA), 16);
}

/* Sensor SCL is not set: neither by a call nor by a step. */
TEST_CASE(sensor_scl_is_derived_and_takes_no_value_of_its_own)
{
    fresh_model();
    CHECK(settings_derived(SET_SENSE_SCL));
    CHECK(!settings_derived(SET_SENSE_SDA));
    CHECK(!settings_derived(SETTING_COUNT));
    int derived = 0;
    for (int i = 0; i < SETTING_COUNT; ++i) {
        derived += settings_derived((setting_id_t)i) ? 1 : 0;
    }
    CHECK_EQ(derived, 1);
    settings_set_observer(observer);
    s_observed = 0;
    settings_set(SET_SENSE_SCL, 5.0f);
    settings_set(SET_SENSE_SCL, -1.0f);
    settings_adjust(SET_SENSE_SCL, 1);
    settings_adjust(SET_SENSE_SCL, -4);
    CHECK_EQ(settings_get_int(SET_SENSE_SCL), 17);
    CHECK_EQ(s_observed, 0);
    CHECK(!settings_dirty());
    /* A step of SDA tells the observer both, SDA first: the panel
     * publishes the pair on either. */
    settings_adjust(SET_SENSE_SDA, 1);
    CHECK_EQ(s_observed, 2);
    CHECK_EQ(s_last_observed, SET_SENSE_SCL);
    CHECK_EQ(settings_get_int(SET_SENSE_SCL), 19);
    CHECK(settings_dirty());
    /* A step that stops at the end tells nobody. */
    settings_set(SET_SENSE_SDA, 26.0f);
    s_observed = 0;
    settings_adjust(SET_SENSE_SDA, 1);
    CHECK_EQ(s_observed, 0);
    /* RESET CATEGORY puts both back. */
    settings_reset(SET_CAT_IFACE);
    CHECK_EQ(settings_get_int(SET_SENSE_SDA), 16);
    CHECK_EQ(settings_get_int(SET_SENSE_SCL), 17);
    settings_set_observer(NULL);
}

/* What settings_nvs.c keeps and gives back: int32 milli-units a key. */
static float nvs_round_trip(float v)
{
    return (float)(int32_t)(v * 1000.0f) / 1000.0f;
}

/* A record as a panel build with two free pin settings saved it. */
static void load_pins(float sda, float scl)
{
    fresh_model();
    for (int i = 0; i < SETTING_COUNT; ++i) {
        s_saved[i] = nvs_round_trip(settings_def((setting_id_t)i)->def);
    }
    s_saved[SET_SENSE_SDA] = nvs_round_trip(sda);
    s_saved[SET_SENSE_SCL] = nvs_round_trip(scl);
    s_saved[SET_INA3221_MOHM] = nvs_round_trip(99.9f);
    s_has_saved = true;
    settings_set_store(&s_mem_store);
    settings_init();
}

/*
 * Records of 0.14.0 and 0.15.0, where SDA and SCL were two settings from
 * -1 to 47: the stored SCL is not read, and a stored SDA the bus cannot
 * have loads as the default.  No load is an edit or an error, and the
 * other keys load as stored.
 */
TEST_CASE(a_stored_pin_pair_of_an_older_build_loads_as_a_pair)
{
    static const struct { int sda, scl, want; } k[] = {
        { 16, 17, 16 },     /* the default pair */
        { 18, 19, 18 },     /* another pair */
        { 26, 27, 26 },     /* the last */
        {  0,  1,  0 },     /* the first */
        { 15, 17, 16 },     /* SDA odd: one stray "-" from the default */
        { 17, 18, 16 },
        { 16, 19, 16 },     /* SCL not the GPIO after: SCL is derived */
        { 18, 17, 18 },
        { 16, -1, 16 },     /* SCL -1: the bus was off for it */
        { -1, 17, -1 },     /* SDA -1 */
        { -1, -1, -1 },
        {  2,  3, 16 },     /* GP3 is reserved */
        { 22, 23, 16 },     /* GP23 is not brought out */
        { 46, 47, 16 },     /* past the module's pins */
        { 47, -1, 16 },
    };
    for (size_t i = 0u; i < sizeof(k) / sizeof(k[0]); ++i) {
        load_pins((float)k[i].sda, (float)k[i].scl);
        const int scl = (k[i].want < 0) ? -1 : k[i].want + 1;
        if (settings_get_int(SET_SENSE_SDA) != k[i].want
            || settings_get_int(SET_SENSE_SCL) != scl) {
            T_FAIL("stored %d/%d loads as %d/%d", k[i].sda, k[i].scl,
                   settings_get_int(SET_SENSE_SDA),
                   settings_get_int(SET_SENSE_SCL));
        }
        CHECK(settings_sense_sda_valid(settings_get_int(SET_SENSE_SDA)));
        CHECK(!settings_dirty());
        CHECK(!settings_save_failed());
        CHECK(!settings_save_asked());
        CHECK_NEAR(settings_get(SET_INA3221_MOHM), 99.9f, 1e-4);
        CHECK_EQ(settings_get_int(SET_INA228_ADDR), 5);
    }
    /* Values no build wrote. */
    load_pins(-5.0f, 300.0f);
    CHECK_EQ(settings_get_int(SET_SENSE_SDA), 16);
    CHECK_EQ(settings_get_int(SET_SENSE_SCL), 17);
    load_pins(1.0e9f, -1.0e9f);
    CHECK_EQ(settings_get_int(SET_SENSE_SDA), 16);
    CHECK_EQ(settings_get_int(SET_SENSE_SCL), 17);
}

/* The SCL key stays and holds the derived pin, so a build that reads both
 * keys reads a pair. */
TEST_CASE(the_scl_key_is_saved_with_the_derived_pin)
{
    CHECK_STR_EQ(settings_def(SET_SENSE_SDA)->key, "sns_sda");
    CHECK_STR_EQ(settings_def(SET_SENSE_SCL)->key, "sns_scl");
    load_pins(15.0f, 17.0f);
    settings_adjust(SET_SENSE_SDA, -1);
    CHECK(settings_save());
    CHECK_NEAR(s_saved[SET_SENSE_SDA], 14.0f, 1e-6);
    CHECK_NEAR(s_saved[SET_SENSE_SCL], 15.0f, 1e-6);
    settings_adjust(SET_SENSE_SDA, -100);
    CHECK(settings_save());
    CHECK_NEAR(s_saved[SET_SENSE_SDA], -1.0f, 1e-6);
    CHECK_NEAR(s_saved[SET_SENSE_SCL], -1.0f, 1e-6);
    settings_init();
    CHECK_EQ(settings_get_int(SET_SENSE_SDA), -1);
    CHECK_EQ(settings_get_int(SET_SENSE_SCL), -1);
}

/* INTERFACES scrolled so Sensor SDA and Sensor SCL are both on the glass. */
static void pins_screen(void)
{
    gesture_screen(SET_CAT_IFACE);
    swipe(XL, LIST_Y + 376, XL, LIST_Y + 252, 4);
    swipe(XL, LIST_Y + 376, XL, LIST_Y + 252, 4);
    CHECK_EQ(settings_screen_scroll(NULL), 232);
    snap();
}

static int scrolled_y(setting_id_t id)
{
    return row_y(row_of(id)) - settings_screen_scroll(NULL);
}

/* The keys of Sensor SDA, by taps as the panel makes them. */
TEST_CASE(a_tap_on_sensor_sda_steps_to_the_next_pin_and_scl_with_it)
{
    pins_screen();
    const int y = scrolled_y(SET_SENSE_SDA);
    feed_tap(FEED_LONE, XM, y);
    CHECK_EQ(settings_get_int(SET_SENSE_SDA), 14);
    CHECK_EQ(settings_get_int(SET_SENSE_SCL), 15);
    CHECK_EQ(changed(), 2);
    feed_tap(FEED_LONE, XP, y);
    feed_tap(FEED_LONE, XP, y);
    CHECK_EQ(settings_get_int(SET_SENSE_SDA), 18);
    CHECK_EQ(settings_get_int(SET_SENSE_SCL), 19);
    /* Held on "-": every step a pin the bus can have, to -1 and no
     * further. */
    finger(FEED_LONE, XM, y);
    for (int i = 0; i < 300; ++i) {
        idle(REPORT_S);
        if (!settings_sense_sda_valid(settings_get_int(SET_SENSE_SDA))) {
            T_FAIL("held at %d", settings_get_int(SET_SENSE_SDA));
        }
        const int sda = settings_get_int(SET_SENSE_SDA);
        CHECK_EQ(settings_get_int(SET_SENSE_SCL), (sda < 0) ? -1 : sda + 1);
    }
    lift(FEED_LONE);
    CHECK_EQ(settings_get_int(SET_SENSE_SDA), -1);
    CHECK_EQ(settings_get_int(SET_SENSE_SCL), -1);
    feed_tap(FEED_LONE, XM, y);
    CHECK_EQ(settings_get_int(SET_SENSE_SDA), -1);
    feed_tap(FEED_LONE, XP, y);
    CHECK_EQ(settings_get_int(SET_SENSE_SDA), 0);
    CHECK_EQ(settings_get_int(SET_SENSE_SCL), 1);
}

/* Sensor SCL's row has no keys: a tap or a hold where the other rows have
 * theirs changes nothing, and a drag from there scrolls the list. */
TEST_CASE(a_tap_on_sensor_scls_row_changes_nothing)
{
    pins_screen();
    const int y = scrolled_y(SET_SENSE_SCL);
    static const int xs[] = { MINUS_X, XM, MINUS_X + BTN_W - 1,
                              PLUS_X, XP, PLUS_X + BTN_W - 1, XL, 660 };
    for (size_t i = 0u; i < sizeof(xs) / sizeof(xs[0]); ++i) {
        feed_tap(FEED_LONE, xs[i], y);
        feed_tap(FEED_LONE, xs[i], y - ROW_H / 2 + 1);
        feed_tap(FEED_LONE, xs[i], y + ROW_H / 2 - 1);
        finger(FEED_LONE, xs[i], y);
        idle(3.0f);
        lift(FEED_LONE);
        idle(0.5f);
    }
    CHECK_EQ(changed(), 0);
    CHECK_EQ(settings_get_int(SET_SENSE_SDA), 16);
    CHECK_EQ(settings_get_int(SET_SENSE_SCL), 17);
    CHECK(!settings_dirty());
    /* The row above still steps, one px inside its lower edge. */
    feed_tap(FEED_LONE, XP, scrolled_y(SET_SENSE_SDA) + ROW_H / 2 - 1);
    CHECK_EQ(settings_get_int(SET_SENSE_SDA), 18);
    snap();
    /* And a drag from where a key would be scrolls. */
    const int from = settings_screen_scroll(NULL);
    swipe(XM, scrolled_y(SET_SENSE_SCL), XM,
          scrolled_y(SET_SENSE_SCL) - 60, 4);
    CHECK_EQ(settings_screen_scroll(NULL), from + 52);
    CHECK_EQ(changed(), 0);
}

/* The mark: each row of the sensor bus and the phase tap follows its own
 * bit, and no other row follows any. */
TEST_CASE(a_row_the_coprocessor_does_not_hold_is_marked)
{
    static const struct { setting_id_t id; uint16_t sense; uint8_t tone; }
        k[] = {
        { SET_INA228_EN,    SENSE_LINK_ROW_I228, 0u },
        { SET_INA228_ADDR,  SENSE_LINK_ROW_I228_ADDR, 0u },
        { SET_INA228_UOHM,  SENSE_LINK_ROW_I228_SHUNT, 0u },
        { SET_INA228_MAX_A, SENSE_LINK_ROW_I228_MAX, 0u },
        { SET_INA3221_EN,   SENSE_LINK_ROW_I3221, 0u },
        { SET_INA3221_ADDR, SENSE_LINK_ROW_I3221_ADDR, 0u },
        { SET_INA3221_MOHM, SENSE_LINK_ROW_I3221_SHUNT, 0u },
        { SET_INA3221_CH,   SENSE_LINK_ROW_I3221_CH, 0u },
        { SET_SENSE_SDA,    SENSE_LINK_ROW_PINS, 0u },
        { SET_SENSE_SCL,    SENSE_LINK_ROW_PINS, 0u },
        { SET_ENC_EN,       SENSE_LINK_ROW_ENC, 0u },
        { SET_TONE_EN,      0u, TONE_LINK_ROWS_TAP },
        { SET_TONE_PIN,     0u, TONE_LINK_ROWS_TAP },
        { SET_TONE_F_MIN,   0u, TONE_LINK_ROWS_TAP },
        { SET_TONE_F_MAX,   0u, TONE_LINK_ROWS_TAP },
        { SET_TONE_SPLIT,   0u, TONE_LINK_ROWS_BEEP },
        { SET_TONE_GAP,     0u, TONE_LINK_ROWS_BEEP },
        { SET_TONE_PERIODS, 0u, TONE_LINK_ROWS_BEEP },
    };
    const size_t n = sizeof(k) / sizeof(k[0]);
    fresh_screen();
    for (int id = 0; id < SETTING_COUNT; ++id) {
        CHECK(!settings_screen_unheld((setting_id_t)id));
    }
    /* Every bit there is, one at a time. */
    for (unsigned bit = 0u; bit < 24u; ++bit) {
        const uint16_t sense = (bit < 16u) ? (uint16_t)(1u << bit) : 0u;
        const uint8_t  tone  = (bit < 16u) ? 0u : (uint8_t)(1u << (bit - 16u));
        settings_screen_set_unheld(sense, tone);
        for (int id = 0; id < SETTING_COUNT; ++id) {
            bool want = false;
            for (size_t i = 0u; i < n; ++i) {
                if (k[i].id == (setting_id_t)id
                    && ((k[i].sense & sense) != 0u
                        || (k[i].tone & tone) != 0u)) {
                    want = true;
                }
            }
            if (settings_screen_unheld((setting_id_t)id) != want) {
                T_FAIL("bit %u, %s", bit,
                       settings_def((setting_id_t)id)->key);
            }
        }
    }
    settings_screen_set_unheld(0u, 0u);
    /* Every bit of both links names a row. */
    uint16_t sense_all = 0u;
    uint8_t  tone_all = 0u;
    for (size_t i = 0u; i < n; ++i) {
        sense_all |= k[i].sense;
        tone_all  |= k[i].tone;
    }
    CHECK_EQ(sense_all, 0x03FFu);
    CHECK_EQ(tone_all, TONE_LINK_ROWS_TAP | TONE_LINK_ROWS_BEEP);
}

/* The mark is drawn when it is set and gone when it is cleared, with no
 * touch in between: the control task sets it. */
TEST_CASE(the_mark_repaints_the_row_when_it_comes_and_when_it_goes)
{
    pins_screen();
    static gfx_color_t plain[W * H];
    static gfx_color_t marked[W * H];
    ui_router_tick(0.05f);
    ui_router_render(&s_c, 0);
    memcpy(plain, s_fb, sizeof(plain));

    settings_screen_set_unheld(SENSE_LINK_ROW_PINS, 0u);
    ui_router_tick(0.05f);
    ui_router_render(&s_c, 0);
    memcpy(marked, s_fb, sizeof(marked));
    /* The two pin rows differ, and no pixel outside them. */
    const int top = scrolled_y(SET_SENSE_SDA) - ROW_H / 2;
    const int bottom = scrolled_y(SET_SENSE_SCL) + ROW_H / 2;
    int inside = 0, outside = 0;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            if (plain[y * W + x] != marked[y * W + x]) {
                if (y >= top && y < bottom && x >= LIST_X) {
                    ++inside;
                } else {
                    ++outside;
                }
            }
        }
    }
    CHECK(inside > 200);
    CHECK_EQ(outside, 0);

    /* Set again the same: nothing to repaint.  Cleared: as before. */
    settings_screen_set_unheld(SENSE_LINK_ROW_PINS, 0u);
    ui_router_render(&s_c, 0);
    CHECK(memcmp(marked, s_fb, sizeof(marked)) == 0);
    settings_screen_set_unheld(0u, 0u);
    ui_router_tick(0.05f);
    ui_router_render(&s_c, 0);
    CHECK(memcmp(plain, s_fb, sizeof(plain)) == 0);
    /* A mark survives the screen being left and entered. */
    settings_screen_set_unheld(SENSE_LINK_ROW_PINS, 0u);
    ui_router_goto(SCREEN_OVERVIEW);
    ui_router_goto(SCREEN_SETUP);
    CHECK(settings_screen_unheld(SET_SENSE_SDA));
    settings_screen_set_unheld(0u, 0u);
}

int main(void)
{
    RUN(sensor_sda_takes_the_pins_the_page_takes_and_no_other);
    RUN(sensor_sda_steps_from_pin_to_pin_and_scl_follows);
    RUN(sensor_scl_is_derived_and_takes_no_value_of_its_own);
    RUN(a_stored_pin_pair_of_an_older_build_loads_as_a_pair);
    RUN(the_scl_key_is_saved_with_the_derived_pin);
    RUN(a_tap_on_sensor_sda_steps_to_the_next_pin_and_scl_with_it);
    RUN(a_tap_on_sensor_scls_row_changes_nothing);
    RUN(a_row_the_coprocessor_does_not_hold_is_marked);
    RUN(the_mark_repaints_the_row_when_it_comes_and_when_it_goes);
    RUN(the_encoder_settings_start_off_with_a_12_bit_centre);
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
    RUN(screen_switches_category_and_renders);
    RUN(plus_and_minus_change_the_setting_under_them);
    RUN(the_poles_key_notifies_once_per_press);
    RUN(changing_the_theme_from_the_screen_takes_effect);
    RUN(a_drag_scrolls_instead_of_pressing);
    RUN(holding_a_key_repeats_with_acceleration);
    RUN(reset_button_restores_the_open_category);
    RUN(leaving_the_screen_no_longer_saves_on_its_own);
    RUN(the_save_button_asks_and_the_application_decides_when);
    RUN(the_request_outlives_the_screen);
    RUN(a_press_while_the_save_is_pending_changes_nothing);
    RUN(asking_with_nothing_to_write_asks_for_nothing);
    RUN(a_cancelled_hit_presses_nothing);
    RUN(a_refused_save_keeps_the_values_dirty);
    RUN(a_missing_store_is_a_failed_save);
    RUN(a_successful_save_retires_an_earlier_failure);
    RUN(an_edit_retires_an_earlier_failure);
    RUN(the_idle_save_reports_a_refusal);
    RUN(a_reset_that_changes_nothing_keeps_the_failure);
    RUN(a_refused_pending_write_repaints_the_button);
    RUN(a_swipe_that_starts_on_a_key_column_scrolls_and_changes_no_value);
    RUN(a_swipe_beside_the_key_columns_scrolls_and_changes_no_value);
    RUN(two_scrolls_from_the_bottom_row_leave_the_shunt_and_the_pins);
    RUN(a_tap_steps_each_kind_of_key_once);
    RUN(a_held_key_repeats_five_times_in_one_second_and_38_in_three);
    RUN(the_repeat_starts_at_450_ms);
    RUN(a_hold_that_turns_into_a_swipe_stops_repeating_at_the_slop);
    RUN(a_tap_may_move_8_px_and_not_9);
    RUN(a_press_that_slides_off_a_key_sideways_steps_nothing);
    RUN(the_gap_between_two_rows_is_no_key);
    RUN(a_second_finger_does_nothing_on_the_setup_screen);
    RUN(a_contact_that_drops_out_of_a_swipe_changes_no_value);
    RUN(a_flick_the_tracker_splits_changes_no_value);
    RUN(a_category_takes_the_press_the_tracker_made_for_a_jump);
    RUN(a_key_carried_into_the_band_steps_nothing_and_never_repeats);
    RUN(a_button_carried_into_the_band_is_not_pressed);
    RUN(a_drag_at_the_edge_of_the_plus_column_and_on_the_scroll_bar);
    RUN(a_key_press_that_loses_its_release_stops_and_holds_nothing);
    return test_summary("settings");
}
