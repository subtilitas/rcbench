/*
 * SETUP > UPDATES' C6 RADIO row (upd_c6.c): the hold at 1,999ms and at
 * 2,000ms, once; a short press's hint on the band; a tap on the grey hold
 * naming why on the band, for every state and reason; a state change
 * under the finger ending the hold unfired; sliding off; one press at a
 * time with UPDATES' other holds; the versions' line and the button's
 * label in both languages inside the row, at every state.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "fw_c6.h"
#include "settings.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "upd_c6.h"
#include "upd_screen.h"

#include "upd_spots.h"

static const fw_version_t k_carried = { 2u, 13u, 0u };

static const ui_screen_t *upd(void) { return upd_screen(); }

/* ------------------------------------------------- a store, in memory */

static float g_kept[SETTING_COUNT];
static bool  g_have_kept;

static bool mem_load(float *values, int count)
{
    if (!g_have_kept) {
        return false;
    }
    memcpy(values, g_kept, (size_t)count * sizeof(float));
    return true;
}

static bool mem_save(const float *values, int count)
{
    memcpy(g_kept, values, (size_t)count * sizeof(float));
    g_have_kept = true;
    return true;
}

static const settings_store_t k_store = { mem_load, mem_save, NULL, NULL };

/* ------------------------------------------------------------- helpers */

static void ev(touch_event_type_t type, uint8_t id, int x, int y)
{
    touch_event_t e = { .type = type,
                        .point = { .id = id, .x = (int16_t)x,
                                   .y = (int16_t)y, .strength = 40 } };
    upd()->event(&e);
}

static upd_c6_view_t view_of(fw_c6_row_t row, fw_c6_why_t why)
{
    upd_c6_view_t v;
    memset(&v, 0, sizeof(v));
    v.row = row;
    v.why = why;
    v.have_image = true;
    v.carried = k_carried;
    return v;
}

/* UPDATES open, the row READY for a C6 that named no version. */
static void on_updates(void)
{
    ui_theme_set(UI_THEME_DARK);
    ui_text_set_language(UI_LANG_EN);
    g_have_kept = false;
    settings_set_store(&k_store);
    settings_init();
    ui_router_init();
    upd()->reset();
    ui_router_goto(SCREEN_UPDATES);
    ui_router_set_alert(NULL);
    const upd_c6_view_t v = view_of(FW_C6_ROW_READY, FW_C6_WHY_NONE);
    upd_c6_set(&v);
    (void)upd_c6_take();
}

/* The row's hold for @p seconds in steps of UI_HOLD_MAX_CREDIT_S and less,
 * not let go. */
static void hold_for(float seconds)
{
    ev(TOUCH_EVENT_DOWN, 1, UP_C6_X, UP_C6_Y);
    float left = seconds;
    while (left > 0.0f) {
        const float dt = (left > UI_HOLD_MAX_CREDIT_S) ? UI_HOLD_MAX_CREDIT_S
                                                       : left;
        upd()->tick(dt);
        left -= dt;
    }
}

static bool band_says(const char *text)
{
    const char *a = ui_router_alert();
    return a != NULL && text != NULL && strcmp(a, text) == 0;
}

/* ------------------------------------------------------------- the hold */

TEST_CASE(the_hold_fires_at_2s_and_not_at_1999ms)
{
    on_updates();
    hold_for(UI_HOLD_S - 0.001f);
    CHECK(!upd_c6_take());
    upd()->tick(0.001f);
    CHECK(upd_c6_take());
    /* Once: the finger still down asks nothing more. */
    upd()->tick(1.0f);
    CHECK(!upd_c6_take());
    ev(TOUCH_EVENT_UP, 1, UP_C6_X, UP_C6_Y);
    CHECK(!upd_c6_take());
    CHECK(!upd_c6_pressed());
}

TEST_CASE(a_short_press_shows_the_hint_and_asks_nothing)
{
    on_updates();
    hold_for(0.5f);
    ev(TOUCH_EVENT_UP, 1, UP_C6_X, UP_C6_Y);
    CHECK(!upd_c6_take());
    CHECK(band_says(TR(HOLD_HINT_C6_UPDATE)));
}

TEST_CASE(a_tap_on_the_grey_hold_says_why_on_the_band)
{
    static const struct {
        fw_c6_row_t row;
        fw_c6_why_t why;
        ui_text_id_t text;
    } k[] = {
        { FW_C6_ROW_NO_IMAGE,    FW_C6_WHY_NONE,    TX_C6_WHY_NO_IMAGE },
        { FW_C6_ROW_BAD_IMAGE,   FW_C6_WHY_NONE,    TX_C6_WHY_BAD_IMAGE },
        { FW_C6_ROW_UPDATING,    FW_C6_WHY_NONE,    TX_C6_WHY_UPDATING },
        { FW_C6_ROW_LINK_FAILED, FW_C6_WHY_NONE,    TX_C6_WHY_LINK },
        { FW_C6_ROW_SAME,        FW_C6_WHY_NONE,    TX_C6_WHY_SAME },
        { FW_C6_ROW_BLOCKED,     FW_C6_WHY_ARMED,   TX_C6_WHY_ARMED },
        { FW_C6_ROW_BLOCKED,     FW_C6_WHY_SUPPLY,  TX_C6_WHY_SUPPLY },
        { FW_C6_ROW_BLOCKED,     FW_C6_WHY_RUN,     TX_C6_WHY_RUN },
        { FW_C6_ROW_BLOCKED,     FW_C6_WHY_DRAWING, TX_C6_WHY_DRAWING },
        { FW_C6_ROW_BLOCKED,     FW_C6_WHY_IOMCU,   TX_C6_WHY_IOMCU },
        { FW_C6_ROW_BLOCKED,     FW_C6_WHY_INSTALL, TX_C6_WHY_INSTALL },
        { FW_C6_ROW_BLOCKED,     FW_C6_WHY_RADIO,   TX_C6_WHY_RADIO },
        { FW_C6_ROW_BLOCKED,     FW_C6_WHY_ENDING,  TX_C6_WHY_ENDING },
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        on_updates();
        const upd_c6_view_t v = view_of(k[i].row, k[i].why);
        upd_c6_set(&v);
        CHECK(upd_c6_reason(&v) == ui_tr(k[i].text));
        /* Held well past 2s: a grey hold fires nothing. */
        hold_for(3.0f);
        CHECK(!upd_c6_take());
        ev(TOUCH_EVENT_UP, 1, UP_C6_X, UP_C6_Y);
        CHECK(!upd_c6_take());
        CHECK(band_says(ui_tr(k[i].text)));
    }
    const upd_c6_view_t ready = view_of(FW_C6_ROW_READY, FW_C6_WHY_NONE);
    CHECK(upd_c6_reason(&ready) == NULL);
    CHECK(upd_c6_reason(NULL) == NULL);
}

TEST_CASE(a_release_off_the_grey_hold_says_nothing)
{
    on_updates();
    const upd_c6_view_t v = view_of(FW_C6_ROW_SAME, FW_C6_WHY_NONE);
    upd_c6_set(&v);
    ev(TOUCH_EVENT_DOWN, 1, UP_C6_X, UP_C6_Y);
    ev(TOUCH_EVENT_MOVE, 1, UP_C6_X, UP_C6_T - 40);
    ev(TOUCH_EVENT_UP, 1, UP_C6_X, UP_C6_T - 40);
    CHECK(ui_router_alert() == NULL);
}

TEST_CASE(a_state_change_under_the_finger_ends_the_hold_unfired)
{
    on_updates();
    hold_for(1.5f);
    const upd_c6_view_t v = view_of(FW_C6_ROW_BLOCKED, FW_C6_WHY_ARMED);
    upd_c6_set(&v);
    CHECK(!upd_c6_pressed());
    upd()->tick(1.0f);
    CHECK(!upd_c6_take());
    /* READY again under the same finger: no new hold from it. */
    const upd_c6_view_t r = view_of(FW_C6_ROW_READY, FW_C6_WHY_NONE);
    upd_c6_set(&r);
    upd()->tick(1.0f);
    upd()->tick(1.0f);
    CHECK(!upd_c6_take());
}

TEST_CASE(sliding_off_and_leaving_abandon_the_hold)
{
    on_updates();
    hold_for(1.5f);
    ev(TOUCH_EVENT_MOVE, 1, UP_C6_X, UP_C6_T - 1);
    CHECK(!upd_c6_pressed());
    upd()->tick(1.0f);
    CHECK(!upd_c6_take());
    ev(TOUCH_EVENT_UP, 1, UP_C6_X, UP_C6_T - 1);
    CHECK(ui_router_alert() == NULL);
    /* The screen's cancel and leave end it as well. */
    hold_for(1.5f);
    upd()->cancel();
    upd()->tick(1.0f);
    CHECK(!upd_c6_take());
    hold_for(1.5f);
    upd()->leave();
    upd()->tick(1.0f);
    CHECK(!upd_c6_take());
}

TEST_CASE(one_press_at_a_time_with_the_iomcu_hold)
{
    on_updates();
    /* A finger on REMINDER AT START first: the row takes no second one. */
    ev(TOUCH_EVENT_DOWN, 1, UP_BTN_X, UP_REMIND_Y);
    ev(TOUCH_EVENT_DOWN, 2, UP_C6_X, UP_C6_Y);
    CHECK(!upd_c6_pressed());
    upd()->tick(3.0f);
    CHECK(!upd_c6_take());
    ev(TOUCH_EVENT_UP, 1, UP_BTN_X, UP_REMIND_Y);
    /* And the row's press holds UPDATES' other buttons off. */
    on_updates();
    ev(TOUCH_EVENT_DOWN, 1, UP_C6_X, UP_C6_Y);
    const bool remind = settings_get_bool(SET_UPD_REMIND);
    ev(TOUCH_EVENT_DOWN, 2, UP_BTN_X, UP_REMIND_Y);
    ev(TOUCH_EVENT_UP, 2, UP_BTN_X, UP_REMIND_Y);
    CHECK_EQ(settings_get_bool(SET_UPD_REMIND), remind);
    CHECK(upd_c6_pressed());
}

/* --------------------------------------------------------- the texts */

static void fits(const upd_c6_view_t *v)
{
    char line[96];
    upd_c6_line(v, line, sizeof(line));
    CHECK(gfx_text_width(&gfx_font_10x20, line, 1) <= UPD_C6_W);
    upd_c6_label(v, line, sizeof(line));
    /* The button's label inside its 16px of padding each side. */
    CHECK(gfx_text_width(&gfx_font_10x20, line, 1) <= UPD_C6_W - 32);
}

TEST_CASE(the_line_and_the_label_fit_in_both_languages)
{
    for (int lang = 0; lang < 2; ++lang) {
        ui_text_set_language(lang ? UI_LANG_DE : UI_LANG_EN);
        for (int row = FW_C6_ROW_READY; row <= FW_C6_ROW_BLOCKED; ++row) {
            upd_c6_view_t v = view_of((fw_c6_row_t)row, FW_C6_WHY_RADIO);
            fits(&v);
            v.known = true;
            v.running = (fw_version_t){ 255u, 255u, 255u };
            v.percent = 100u;
            fits(&v);
            v.have_image = false;
            fits(&v);
        }
    }
    ui_text_set_language(UI_LANG_EN);
}

TEST_CASE(the_line_names_both_versions_or_says_not_reported)
{
    ui_text_set_language(UI_LANG_EN);
    upd_c6_view_t v = view_of(FW_C6_ROW_READY, FW_C6_WHY_NONE);
    char line[96];
    upd_c6_line(&v, line, sizeof(line));
    CHECK_STR_EQ(line, "C6 radio: not reported. Included: 2.13.0.");
    v.known = true;
    v.running = (fw_version_t){ 0u, 0u, 0u };
    v.have_image = false;
    upd_c6_line(&v, line, sizeof(line));
    CHECK_STR_EQ(line, "C6 radio: 0.0.0. Included: none.");
    v.row = FW_C6_ROW_UPDATING;
    v.percent = 37u;
    upd_c6_label(&v, line, sizeof(line));
    CHECK_STR_EQ(line, "C6 UPDATE 37%");
    v.percent = 140u;
    upd_c6_label(&v, line, sizeof(line));
    CHECK_STR_EQ(line, "C6 UPDATE 100%");
    v.row = FW_C6_ROW_READY;
    upd_c6_label(&v, line, sizeof(line));
    CHECK_STR_EQ(line, "HOLD 2s TO UPDATE THE C6 RADIO");
    ui_text_set_language(UI_LANG_DE);
    v.known = false;
    v.have_image = true;
    upd_c6_line(&v, line, sizeof(line));
    CHECK_STR_EQ(line, "C6-Funk: nicht gemeldet. Auf diesem Panel: 2.13.0.");
    ui_text_set_language(UI_LANG_EN);
}

TEST_CASE(the_rect_is_a_finger_tall_and_under_the_card)
{
    const gfx_rect_t r = upd_c6_rect();
    CHECK(r.h >= UI_TAP_MIN);
    CHECK_EQ(r.y, UP_C6_T);
    CHECK(UP_CARD_B + 12 <= UPD_C6_LINE_Y);
    CHECK(gfx_rect_contains(r, UP_C6_X, UP_C6_Y));
    CHECK(!gfx_rect_contains(r, UP_BTN_X, UP_CU_Y));
}

int main(void)
{
    RUN(the_hold_fires_at_2s_and_not_at_1999ms);
    RUN(a_short_press_shows_the_hint_and_asks_nothing);
    RUN(a_tap_on_the_grey_hold_says_why_on_the_band);
    RUN(a_release_off_the_grey_hold_says_nothing);
    RUN(a_state_change_under_the_finger_ends_the_hold_unfired);
    RUN(sliding_off_and_leaving_abandon_the_hold);
    RUN(one_press_at_a_time_with_the_iomcu_hold);
    RUN(the_line_and_the_label_fit_in_both_languages);
    RUN(the_line_names_both_versions_or_says_not_reported);
    RUN(the_rect_is_a_finger_tall_and_under_the_card);
    return test_summary("upd_c6");
}
