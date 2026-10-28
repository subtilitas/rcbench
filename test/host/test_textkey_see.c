/**
 * @file test_textkey_see.c
 * @brief The text editor's see-through keyboard: SETUP > PANEL's Keyboard
 *        see-through, its three steps, its default, what a save and a
 *        restart keep and what a stored value off the list becomes; and
 *        the keyboard drawn see-through, the share of what is behind that
 *        shows through its keys and gaps, its labels opaque, its corners
 *        those of the opaque keys.
 *
 * Cases:
 * - the_steps_are_off_25_and_50_percent_and_clamp
 * - the_setting_is_off_by_default_and_on_panel
 * - the_setting_survives_a_save_and_a_restart_and_coerces_a_stray_value
 * - only_the_editor_keyboard_shows_through
 * - the_keys_and_gaps_show_the_share_of_the_text_behind
 * - the_labels_stay_opaque_and_the_corners_match_the_opaque_keys
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>
#include <string.h>

#include "greatest.h"
#include "settings.h"
#include "ui_text.h"
#include "ui_textkey.h"
#include "ui_theme.h"
#include "ui_widgets.h"

/** @brief The editor's keyboard area on FILES: under a panel of 8 rows. */
static const gfx_rect_t k_area = { 8, 236, 1008, 260 };

/** @brief The canvas: the body of the panel, 1024 x 540px. */
#define CW 1024
#define CH 540

static float s_stored[SETTING_COUNT];
static bool s_has_stored;

/**
 * @brief The memory store's load.
 * @param values Takes the values.
 * @param count  Their count.
 * @return Whether a save came first.
 */
static bool mem_load(float *values, int count)
{
    if (!s_has_stored) {
        return false;
    }
    memcpy(values, s_stored, (size_t)count * sizeof(float));
    return true;
}

/**
 * @brief The memory store's save.
 * @param values The values.
 * @param count  Their count.
 * @return True.
 */
static bool mem_save(const float *values, int count)
{
    memcpy(s_stored, values, (size_t)count * sizeof(float));
    s_has_stored = true;
    return true;
}

/** @brief A store in memory, as NVS keeps the values on the panel. */
static const settings_store_t k_store = { mem_load, mem_save, NULL, NULL };

TEST_CASE(the_steps_are_off_25_and_50_percent_and_clamp)
{
    CHECK_EQ(UI_TEXTKEY_SEE_STEPS, 3);
    CHECK_EQ(ui_textkey_see_pct(0), 0);
    CHECK_EQ(ui_textkey_see_pct(1), 25);
    CHECK_EQ(ui_textkey_see_pct(2), 50);
    CHECK_EQ(ui_textkey_see_pct(-1), 0);
    CHECK_EQ(ui_textkey_see_pct(3), 50);
    CHECK_EQ(ui_textkey_see_pct(1000), 50);
    ui_textkey_t k;
    memset(&k, 0, sizeof(k));
    ui_textkey_set_see(&k, 25);
    CHECK_EQ(k.see, 25);
    ui_textkey_set_see(&k, -5);
    CHECK_EQ(k.see, 0);
    ui_textkey_set_see(&k, 80);
    CHECK_EQ(k.see, UI_TEXTKEY_SEE_MAX);
    ui_textkey_set_see(NULL, 25);
}

TEST_CASE(the_setting_is_off_by_default_and_on_panel)
{
    settings_set_store(NULL);
    settings_init();
    const setting_def_t *d = settings_def(SET_KB_SEE);
    CHECK(d != NULL);
    if (d == NULL) {
        return;
    }
    CHECK_STR_EQ(d->key, "kb_see");
    CHECK_EQ(d->cat, SET_CAT_APP);
    CHECK_EQ(d->type, SET_TYPE_ENUM);
    CHECK_EQ(d->option_count, UI_TEXTKEY_SEE_STEPS);
    CHECK_STR_EQ(d->options[0], "OFF");
    CHECK_STR_EQ(d->options[1], "25%");
    CHECK_STR_EQ(d->options[2], "50%");
    CHECK_EQ(settings_get_int(SET_KB_SEE), 0);
    CHECK_EQ(ui_textkey_see_pct(settings_get_int(SET_KB_SEE)), 0);
    CHECK_STR_EQ(settings_label(SET_KB_SEE), "Keyboard see-through");
    ui_text_set_language(UI_LANG_DE);
    CHECK_STR_EQ(ui_setting_label(SET_KB_SEE), "Tastatur durchsichtig");
    ui_text_set_language(UI_LANG_EN);
}

TEST_CASE(the_setting_survives_a_save_and_a_restart_and_coerces_a_stray_value)
{
    s_has_stored = false;
    settings_set_store(&k_store);
    settings_init();
    settings_adjust(SET_KB_SEE, 1);
    settings_adjust(SET_KB_SEE, 1);
    CHECK_EQ(settings_get_int(SET_KB_SEE), 2);
    CHECK(settings_save());
    settings_init();                    /* the restart */
    CHECK_EQ(settings_get_int(SET_KB_SEE), 2);
    CHECK_EQ(ui_textkey_see_pct(settings_get_int(SET_KB_SEE)), 50);
    /* The step after 50% is OFF again. */
    settings_adjust(SET_KB_SEE, 1);
    CHECK_EQ(settings_get_int(SET_KB_SEE), 0);
    /* A stored word off the list is stale or corrupt: OFF, never past
     * 50%.  One near an option is that option. */
    s_stored[SET_KB_SEE] = 7.0f;
    settings_init();
    CHECK_EQ(settings_get_int(SET_KB_SEE), 0);
    s_stored[SET_KB_SEE] = -3.0f;
    settings_init();
    CHECK_EQ(settings_get_int(SET_KB_SEE), 0);
    s_stored[SET_KB_SEE] = 1.2f;
    settings_init();
    CHECK_EQ(settings_get_int(SET_KB_SEE), 1);
    /* RESET CATEGORY on PANEL gives OFF. */
    settings_reset(SET_CAT_APP);
    CHECK_EQ(settings_get_int(SET_KB_SEE), 0);
    settings_set_store(NULL);
    settings_init();
}

/**
 * @brief Draw @p k over a canvas of one colour.
 * @param k    The keyboard.
 * @param fb   The pixels, CW x CH.
 * @param back The colour under it.
 */
static void draw_over(const ui_textkey_t *k, gfx_color_t *fb,
                      gfx_color_t back)
{
    gfx_canvas_t c;
    gfx_canvas_init(&c, fb, CW, CH, CW);
    gfx_fill_rect(&c, 0, 0, CW, CH, back);
    ui_textkey_render(k, &c);
}

/** @brief The green of @p p, 0 to 63. */
static int green(gfx_color_t p)
{
    return (p >> 5) & 0x3F;
}

TEST_CASE(only_the_editor_keyboard_shows_through)
{
    gfx_color_t *black = calloc((size_t)CW * CH, sizeof(gfx_color_t));
    gfx_color_t *white = calloc((size_t)CW * CH, sizeof(gfx_color_t));
    REQUIRE_ALLOC(black);
    REQUIRE_ALLOC(white);
    ui_textkey_t k;
    memset(&k, 0, sizeof(k));
    ui_textkey_open_file(&k, k_area, "NAME", "");
    ui_textkey_set_see(&k, 50);
    draw_over(&k, black, GFX_BLACK);
    draw_over(&k, white, GFX_WHITE);
    int differ = 0;
    for (int y = k_area.y; y < k_area.y + k_area.h; ++y) {
        for (int x = k_area.x; x < k_area.x + k_area.w; ++x) {
            differ += (black[(size_t)y * CW + x] != white[(size_t)y * CW + x]);
        }
    }
    CHECK_EQ(differ, 0);
    /* The editor's at OFF covers its area too. */
    ui_textkey_open_editor(&k, k_area);
    ui_textkey_set_see(&k, 0);
    draw_over(&k, black, GFX_BLACK);
    draw_over(&k, white, GFX_WHITE);
    differ = 0;
    for (int y = k_area.y; y < k_area.y + k_area.h; ++y) {
        for (int x = k_area.x; x < k_area.x + k_area.w; ++x) {
            differ += (black[(size_t)y * CW + x] != white[(size_t)y * CW + x]);
        }
    }
    CHECK_EQ(differ, 0);
    free(black);
    free(white);
}

/*
 * Over black and over white, a pixel of a key's fill, one of a gap and
 * one of a blank of the marks differ by the see-through's share of white: 25% and 50% of the green's
 * 63 steps within one of the blend's 32 steps (2 green steps).  Nothing
 * outside the area changes.
 */
TEST_CASE(the_keys_and_gaps_show_the_share_of_the_text_behind)
{
    gfx_color_t *black = calloc((size_t)CW * CH, sizeof(gfx_color_t));
    gfx_color_t *white = calloc((size_t)CW * CH, sizeof(gfx_color_t));
    REQUIRE_ALLOC(black);
    REQUIRE_ALLOC(white);
    ui_textkey_t k;
    memset(&k, 0, sizeof(k));
    ui_textkey_open_editor(&k, k_area);
    const gfx_rect_t q = ui_textkey_key_rect(&k, 10);
    const gfx_rect_t w = ui_textkey_key_rect(&k, 11);
    /* A pixel of q's fill clear of its label and hairline, and one of the
     * gap between q and w. */
    const size_t key_px = (size_t)(q.y + q.h - 4) * CW + (size_t)(q.x + 10);
    const size_t gap_px = (size_t)(q.y + q.h / 2) * CW
                          + (size_t)(q.x + q.w + (w.x - q.x - q.w) / 2);
    for (int step = 1; step < UI_TEXTKEY_SEE_STEPS; ++step) {
        const int pct = ui_textkey_see_pct(step);
        ui_textkey_set_see(&k, pct);
        draw_over(&k, black, GFX_BLACK);
        draw_over(&k, white, GFX_WHITE);
        const int want = (63 * pct + 50) / 100;
        const int got_key = green(white[key_px]) - green(black[key_px]);
        const int got_gap = green(white[gap_px]) - green(black[gap_px]);
        CHECK(abs(got_key - want) <= 2);
        CHECK(abs(got_gap - want) <= 2);
        /* A blank of the second page of marks, the key left of DEL on
         * the middle row, shows through as a gap does. */
        ui_textkey_t more = k;
        more.layer = UI_TK_MORE;
        const gfx_rect_t blank = ui_textkey_key_rect(&more, 29);
        const size_t blank_px = (size_t)(blank.y + blank.h / 2) * CW
                                + (size_t)(blank.x + blank.w / 2);
        CHECK_STR_EQ(ui_textkey_key_label(&more, 29), "");
        draw_over(&more, black, GFX_BLACK);
        draw_over(&more, white, GFX_WHITE);
        CHECK(abs(green(white[blank_px]) - green(black[blank_px]) - want)
              <= 2);
        CHECK_EQ(black[blank_px], black[gap_px]);
        draw_over(&k, black, GFX_BLACK);
        draw_over(&k, white, GFX_WHITE);
        /* The colour over black is the fill's share of the key's. */
        const gfx_color_t sunk = ui_theme_color(UI_C_PANEL_SUNK);
        CHECK(green(black[key_px]) <= green(sunk));
        for (int y = 0; y < CH; ++y) {
            for (int x = 0; x < CW; ++x) {
                const bool in = x >= k_area.x && x < k_area.x + k_area.w
                                && y >= k_area.y
                                && y < k_area.y + k_area.h;
                if (!in && black[(size_t)y * CW + x] != GFX_BLACK) {
                    CHECK_EQ(x, -1);    /* names the pixel */
                    CHECK_EQ(y, -1);
                }
            }
        }
    }
    free(black);
    free(white);
}

/*
 * At 50%, the pixels of q that are the same over black and over white are
 * its label and its hairline: some, and fewer than a third of the key.
 * In each of q's first 8 rows the first pixel of the fill is where the
 * opaque key's starts, so the corners round alike.
 */
TEST_CASE(the_labels_stay_opaque_and_the_corners_match_the_opaque_keys)
{
    gfx_color_t *black = calloc((size_t)CW * CH, sizeof(gfx_color_t));
    gfx_color_t *white = calloc((size_t)CW * CH, sizeof(gfx_color_t));
    gfx_color_t *solid = calloc((size_t)CW * CH, sizeof(gfx_color_t));
    REQUIRE_ALLOC(black);
    REQUIRE_ALLOC(white);
    REQUIRE_ALLOC(solid);
    ui_textkey_t k;
    memset(&k, 0, sizeof(k));
    ui_textkey_open_editor(&k, k_area);
    draw_over(&k, solid, GFX_BLACK);
    ui_textkey_set_see(&k, 50);
    draw_over(&k, black, GFX_BLACK);
    draw_over(&k, white, GFX_WHITE);
    const gfx_rect_t q = ui_textkey_key_rect(&k, 10);
    int same = 0;
    for (int y = q.y + 2; y < q.y + q.h; ++y) {
        for (int x = q.x; x < q.x + q.w; ++x) {
            const size_t i = (size_t)y * CW + (size_t)x;
            if (black[i] == white[i]) {
                ++same;
                CHECK_EQ(black[i], solid[i]);   /* drawn as the opaque */
            }
        }
    }
    CHECK(same > 0);
    CHECK(same < q.w * q.h / 3);
    const gfx_color_t panel = ui_theme_color(UI_C_PANEL);
    for (int row = 0; row < 8; ++row) {
        const int y = q.y + row;
        const gfx_color_t gap = black[(size_t)y * CW + (size_t)(q.x - 3)];
        int opaque_in = -1;
        int glass_in = -1;
        for (int x = q.x; x < q.x + q.w / 2 && glass_in < 0; ++x) {
            if (opaque_in < 0 && solid[(size_t)y * CW + x] != panel) {
                opaque_in = x - q.x;
            }
            if (black[(size_t)y * CW + x] != gap) {
                glass_in = x - q.x;
            }
        }
        CHECK_EQ(glass_in, opaque_in);
    }
    free(black);
    free(white);
    free(solid);
}

int main(void)
{
    RUN(the_steps_are_off_25_and_50_percent_and_clamp);
    RUN(the_setting_is_off_by_default_and_on_panel);
    RUN(the_setting_survives_a_save_and_a_restart_and_coerces_a_stray_value);
    RUN(only_the_editor_keyboard_shows_through);
    RUN(the_keys_and_gaps_show_the_share_of_the_text_behind);
    RUN(the_labels_stay_opaque_and_the_corners_match_the_opaque_keys);
    return test_summary("textkey_see");
}
