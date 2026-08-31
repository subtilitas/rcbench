/*
 * The settings model and the screen that edits it.
 *
 * The screen cases were held back while it was re-cut and are here again.  The
 * layout they address moved up 40 px -- the router owns the band and the home
 * tag now -- so the geometry below follows it, and the taps are in panel
 * coordinates because ui_router_event strips the band on the way in.
 *
 * SPDX-License-Identifier: MIT
 */
#include "greatest.h"

#include <math.h>

#include "settings.h"
#include "settings_screen.h"
#include "ui_screen.h"
#include "ui_widgets.h"
#include <stdlib.h>
#include "ui_theme.h"

#define W 800
#define H 480

/* Geometry mirrored from settings_screen.c; if the layout moves these move
 * with it, and the test names say what they were aiming at. */
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
#define RESET_Y  (358 + UI_BAND_H)

static gfx_color_t *s_fb;
static gfx_canvas_t s_c;


#define H 480

/* Geometry mirrored from settings_screen.c; if the layout moves these move
 * with it, and the test names say what they were aiming at. */
#define CAT_X    16
#define CAT_Y    56
#define CAT_H    64
#define CAT_GAP  8
#define LIST_X   236
#define LIST_Y   56
#define ROW_PITCH 58
#define ROW_H    54
#define PLUS_X   726
#define MINUS_X  556
#define BTN_W    44

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

static void mem_save(const float *values, int count)
{
    for (int i = 0; i < count && i < SETTING_COUNT; ++i) {
        s_saved[i] = values[i];
    }
    s_has_saved = true;
    ++s_save_calls;
}

static const settings_store_t s_mem_store = { mem_load, mem_save };

static int s_observed;
static setting_id_t s_last_observed;

static void observer(setting_id_t id)
{
    ++s_observed;
    s_last_observed = id;
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
 * The table is the contract, and a bad row in it is the most likely way this
 * subsystem breaks: a default outside its own range, a step of zero that makes
 * the +/- keys inert, an enum with no options.  The suite asserted the keys and
 * the clamping but never the rows themselves, so any of those would have
 * shipped and shown up as a control that quietly does nothing.
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
    /* NVS keys are capped at 15 characters, and a collision would silently
     * make two settings the same one. */
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
    const setting_def_t *d = settings_def(SET_TELEM_SRC);
    CHECK(d->option_count >= 2);

    for (int i = 0; i < d->option_count; ++i) {
        CHECK_EQ(settings_get_int(SET_TELEM_SRC), i);
        settings_adjust(SET_TELEM_SRC, 1);
    }
    CHECK_EQ(settings_get_int(SET_TELEM_SRC), 0);      /* wrapped */

    settings_adjust(SET_TELEM_SRC, -1);
    CHECK_EQ(settings_get_int(SET_TELEM_SRC), d->option_count - 1);

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

    settings_set(SET_TELEM_SRC, 0);
    CHECK_STR_EQ(settings_value_text(SET_TELEM_SRC, buf, sizeof(buf)),
                 settings_def(SET_TELEM_SRC)->options[0]);

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

TEST_CASE(reset_restores_one_category_only)
{
    fresh_model();
    settings_set(SET_PACK_CELLS, 12);
    settings_set(SET_BRIGHTNESS, 40);

    /* A reset that does not notify leaves the hardware on the old value while
     * the screen shows the new one -- the observer is the only path a setting
     * has to motor_out and the backlight. */
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
    /* The predecessor initialised its bench screen here; ours is set up by
     * ui_router_init, which reset()s every screen. */
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

TEST_CASE(leaving_the_screen_saves)
{
    fresh_screen();
    settings_set_store(&s_mem_store);
    s_save_calls = 0;

    setting_id_t ids[64];
    settings_in_category(SET_CAT_ESC, ids, 64);
    tap(PLUS_X + BTN_W / 2, row_y(0));
    CHECK(settings_dirty());

    ui_router_goto(SCREEN_OVERVIEW);
    CHECK(!settings_dirty());
    CHECK_EQ(s_save_calls, 1);

    settings_set_store(NULL);
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
    RUN(reset_restores_one_category_only);
    RUN(bad_ids_are_survivable);
    RUN(theme_switch_changes_the_palette);
    RUN(brightness_and_contrast_are_clamped_and_monotonic);
    RUN(screen_switches_category_and_renders);
    RUN(plus_and_minus_change_the_setting_under_them);
    RUN(changing_the_theme_from_the_screen_takes_effect);
    RUN(a_drag_scrolls_instead_of_pressing);
    RUN(holding_a_key_repeats_with_acceleration);
    RUN(reset_button_restores_the_open_category);
    RUN(leaving_the_screen_saves);
    return test_summary("settings");
}
