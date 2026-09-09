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

#include "link_pages.h"
#include "settings.h"
#include "settings_screen.h"
#include "ui_screen.h"
#include "ui_widgets.h"
#include <stdlib.h>
#include "ui_theme.h"

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

static const settings_store_t s_mem_store = { mem_load, mem_save };

/* A store that is asked and answers no.  It writes nothing, so a later load
 * returns what was there before the refused save. */
static bool refuse_save(const float *values, int count)
{
    (void)values;
    (void)count;
    ++s_save_calls;
    return false;
}

static const settings_store_t s_refusing_store = { mem_load, refuse_save };

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

    /* Press on the + key, then move well past the slop before releasing. */
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

    /* The press itself already applied one step; the drag must not add more. */
    CHECK_EQ(settings_get_int(ids[0]), after_press);
    CHECK(after_press != before);

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
    CHECK(after_press > start);

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

int main(void)
{
    RUN(defaults_come_from_the_schema);
    RUN(every_schema_row_is_internally_consistent);
    RUN(nvs_keys_are_unique_and_short_enough);
    RUN(values_are_clamped_to_the_schema);
    RUN(enums_and_booleans_cycle);
    RUN(steps_follow_the_schema);
    RUN(categories_partition_every_setting);
    RUN(value_text_renders_every_type);
    RUN(store_round_trips_and_coerces_stale_values);
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
    RUN(a_refused_save_keeps_the_values_dirty);
    RUN(a_missing_store_is_a_failed_save);
    RUN(a_successful_save_retires_an_earlier_failure);
    RUN(an_edit_retires_an_earlier_failure);
    RUN(the_idle_save_reports_a_refusal);
    RUN(a_reset_that_changes_nothing_keeps_the_failure);
    RUN(a_refused_pending_write_repaints_the_button);
    return test_summary("settings");
}
