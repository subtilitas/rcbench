/*
 * The colours of an output-action button by state, in both themes
 * (ui_action_fill()), and the reason a tap on a refused one puts in the
 * alert band (ui_action_refused()).
 *
 * Available is the OK green, active the danger red, waiting the warning
 * amber, refused ui_button_off_fill(): four fills, all different, the same
 * tokens in the dark theme and the light one.  A label on each reads: the
 * ink ui_is_light() picks on a live fill, the faint label on the refused
 * one.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "settings.h"
#include "splash_screen.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

static const ui_theme_id_t k_themes[] = { UI_THEME_DARK, UI_THEME_LIGHT };

TEST_CASE(each_state_has_its_token_in_both_themes)
{
    for (size_t t = 0; t < sizeof(k_themes) / sizeof(k_themes[0]); ++t) {
        ui_theme_set(k_themes[t]);
        CHECK_EQ(ui_action_fill(UI_ACTION_AVAILABLE),
                 ui_theme_color(UI_C_OK));
        CHECK_EQ(ui_action_fill(UI_ACTION_ACTIVE),
                 ui_theme_color(UI_C_DANGER));
        CHECK_EQ(ui_action_fill(UI_ACTION_WAITING),
                 ui_theme_color(UI_C_WARN));
        CHECK_EQ(ui_action_fill(UI_ACTION_REFUSED), ui_button_off_fill());
        /* Outside the enum: refused, never a live colour. */
        CHECK_EQ(ui_action_fill(UI_ACTION_COUNT), ui_button_off_fill());
        CHECK_EQ(ui_action_fill((ui_action_t)-1), ui_button_off_fill());
    }
    ui_theme_set(UI_THEME_DARK);
}

TEST_CASE(the_four_states_differ_and_none_is_a_neutral_or_the_accent)
{
    for (size_t t = 0; t < sizeof(k_themes) / sizeof(k_themes[0]); ++t) {
        ui_theme_set(k_themes[t]);
        for (int a = 0; a < UI_ACTION_COUNT; ++a) {
            const gfx_color_t fa = ui_action_fill((ui_action_t)a);
            for (int b = a + 1; b < UI_ACTION_COUNT; ++b) {
                CHECK(fa != ui_action_fill((ui_action_t)b));
            }
            /* Edit and navigation are UI_C_PANEL_HI, a selected tab the
             * accent: an output action is neither. */
            CHECK(fa != ui_theme_color(UI_C_PANEL_HI));
            CHECK(fa != ui_theme_color(UI_C_ACCENT));
        }
    }
    ui_theme_set(UI_THEME_DARK);
}

TEST_CASE(a_label_reads_on_each_state_and_the_refused_one_is_faint)
{
    enum { W = 200, H = 60 };
    gfx_color_t *fb = calloc((size_t)W * H, sizeof(gfx_color_t));
    REQUIRE_ALLOC(fb);
    gfx_canvas_t cv;
    gfx_canvas_init(&cv, fb, W, H, W);
    const gfx_rect_t r = { 0, 0, W, H };
    for (size_t t = 0; t < sizeof(k_themes) / sizeof(k_themes[0]); ++t) {
        ui_theme_set(k_themes[t]);
        for (int a = 0; a < UI_ACTION_COUNT; ++a) {
            const gfx_color_t fill = ui_action_fill((ui_action_t)a);
            const bool refused = (a == UI_ACTION_REFUSED);
            memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
            ui_button(&cv, r, "START", fill, false, !refused);
            const gfx_color_t want = refused ? UI_TEXT_FAINT
                                   : ui_is_light(fill) ? UI_TEXT_ON_LIGHT
                                                       : UI_TEXT;
            unsigned ink = 0u;
            for (size_t i = 0; i < (size_t)W * H; ++i) {
                ink += (fb[i] == want);
            }
            CHECK(ink > 0u);
            CHECK_EQ(fb[6 * W + 12], fill);
        }
    }
    ui_theme_set(UI_THEME_DARK);
    free(fb);
}

/* The router on MOTOR & ESC, a screen with the band. */
static void router_on(void)
{
    ui_theme_set(UI_THEME_DARK);
    settings_set_store(NULL);
    settings_init();
    ui_text_set_language(UI_LANG_EN);
    ui_router_init();
    for (int i = 0; i < SPLASH_STEP_COUNT; ++i) {
        splash_screen_set((splash_step_t)i, SPLASH_OK, "");
    }
    ui_router_tick(2.0f);
    ui_router_goto(SCREEN_OVERVIEW);
    ui_router_goto(SCREEN_MOTOR);
    ui_router_set_alert(NULL);
}

TEST_CASE(a_tap_on_a_refused_button_puts_its_reason_in_the_band)
{
    router_on();
    ui_action_refused("ARM first.");
    CHECK(ui_router_alert() != NULL
          && strcmp(ui_router_alert(), "ARM first.") == 0);
    /* As a hold's hint: a notice, amber, not an alert. */
    CHECK(ui_router_alert_is_notice());
    /* No reason, nothing. */
    ui_router_set_alert(NULL);
    ui_action_refused(NULL);
    ui_action_refused("");
    CHECK(ui_router_alert() == NULL);
    /* An alert showing keeps the band. */
    ui_router_set_alert("Touch signal lost.");
    ui_action_refused("ARM first.");
    CHECK(strcmp(ui_router_alert(), "Touch signal lost.") == 0);
    CHECK(!ui_router_alert_is_notice());
}

int main(void)
{
    RUN(each_state_has_its_token_in_both_themes);
    RUN(the_four_states_differ_and_none_is_a_neutral_or_the_accent);
    RUN(a_label_reads_on_each_state_and_the_refused_one_is_faint);
    RUN(a_tap_on_a_refused_button_puts_its_reason_in_the_band);
    return test_summary("action_fill");
}
