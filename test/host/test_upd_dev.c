/*
 * SETUP > UPDATES' DIAL FIRMWARE row (upd_dev.c): the hold at 1,999ms and
 * at 2,000ms, once, for an update and for a reinstall; a short press's
 * hint on the band; a tap on the grey hold naming why on the band, for
 * every grey state; a state change under the finger ending the hold
 * unfired; one press at a time with SETTINGS and the C6 row; the
 * label of every phase of a running update; the line and the label inside
 * the row in both languages.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "devupd.h"
#include "settings.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "upd_c6.h"
#include "upd_dev.h"
#include "upd_screen.h"

#include "upd_spots.h"

/** @brief The hold's middle, body coordinates. @{ */
#define DEV_X (UPD_DEV_X + UPD_DEV_W / 2)
#define DEV_Y (UPD_DEV_BTN_Y + UPD_DEV_BTN_H / 2)
/** @} */

static const fw_version_t k_carried = { 0u, 25u, 0u };

static const ui_screen_t *upd(void) { return upd_screen(); }

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

static void ev(touch_event_type_t type, uint8_t id, int x, int y)
{
    touch_event_t e = { .type = type,
                        .point = { .id = id, .x = (int16_t)x,
                                   .y = (int16_t)y, .strength = 40 } };
    upd()->event(&e);
}

static upd_dev_view_t view_of(devupd_row_t row)
{
    upd_dev_view_t v;
    memset(&v, 0, sizeof(v));
    v.row = row;
    v.have_image = true;
    v.carried = k_carried;
    v.known = true;
    v.running = (fw_version_t){ 0u, 24u, 0u };
    return v;
}

/* UPDATES open, the Dial's row READY, the C6 row grey. */
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
    const upd_c6_view_t c6 = { .row = FW_C6_ROW_NO_IMAGE };
    upd_c6_set(&c6);
    const upd_dev_view_t v = view_of(DEVUPD_ROW_READY);
    upd_dev_set(UPD_DEV_DIAL, &v);
    (void)upd_dev_take(UPD_DEV_DIAL);
}

static void hold_for(float seconds)
{
    ev(TOUCH_EVENT_DOWN, 1, DEV_X, DEV_Y);
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

TEST_CASE(the_hold_fires_at_2s_and_not_at_1999ms)
{
    on_updates();
    hold_for(UI_HOLD_S - 0.001f);
    CHECK(!upd_dev_take(UPD_DEV_DIAL));
    upd()->tick(0.001f);
    CHECK(upd_dev_take(UPD_DEV_DIAL));
    upd()->tick(1.0f);
    CHECK(!upd_dev_take(UPD_DEV_DIAL));
    ev(TOUCH_EVENT_UP, 1, DEV_X, DEV_Y);
    CHECK(!upd_dev_take(UPD_DEV_DIAL));
    CHECK(!upd_dev_pressed());
}

TEST_CASE(a_short_press_shows_the_hint_and_the_reinstall_its_own)
{
    on_updates();
    hold_for(0.5f);
    ev(TOUCH_EVENT_UP, 1, DEV_X, DEV_Y);
    CHECK(!upd_dev_take(UPD_DEV_DIAL));
    CHECK(band_says(TR(HOLD_HINT_DIAL_UPDATE)));
    upd_dev_view_t v = view_of(DEVUPD_ROW_REINSTALL);
    v.running = k_carried;
    upd_dev_set(UPD_DEV_DIAL, &v);
    hold_for(0.5f);
    ev(TOUCH_EVENT_UP, 1, DEV_X, DEV_Y);
    CHECK(band_says(TR(HOLD_HINT_DIAL_REINSTALL)));
    hold_for(UI_HOLD_S);
    CHECK(upd_dev_take(UPD_DEV_DIAL));
    ev(TOUCH_EVENT_UP, 1, DEV_X, DEV_Y);
}

TEST_CASE(a_tap_on_the_grey_hold_says_why_on_the_band)
{
    static const struct {
        devupd_row_t row;
        ui_text_id_t why;
    } k[] = {
        { DEVUPD_ROW_NO_IMAGE,   TX_DEV_WHY_NO_IMAGE },
        { DEVUPD_ROW_BAD_IMAGE,  TX_DEV_WHY_BAD_IMAGE },
        { DEVUPD_ROW_CHECKING,   TX_DEV_WHY_CHECKING },
        { DEVUPD_ROW_NOT_LINKED, TX_DEV_WHY_NOT_LINKED },
        { DEVUPD_ROW_ARMED,      TX_DEV_WHY_ARMED },
        { DEVUPD_ROW_BUSY,       TX_DEV_WHY_BUSY },
        { DEVUPD_ROW_UPDATING,   TX_DEV_WHY_UPDATING },
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        on_updates();
        const upd_dev_view_t v = view_of(k[i].row);
        upd_dev_set(UPD_DEV_DIAL, &v);
        ev(TOUCH_EVENT_DOWN, 1, DEV_X, DEV_Y);
        upd()->tick(3.0f);
        ev(TOUCH_EVENT_UP, 1, DEV_X, DEV_Y);
        CHECK(!upd_dev_take(UPD_DEV_DIAL));
        CHECK(band_says(ui_tr(k[i].why)));
    }
    on_updates();
    const upd_dev_view_t ready = view_of(DEVUPD_ROW_READY);
    CHECK(upd_dev_reason(&ready) == NULL);
    CHECK(upd_dev_reason(NULL) == NULL);
}

TEST_CASE(a_state_change_under_the_finger_ends_the_hold_unfired)
{
    on_updates();
    hold_for(1.5f);
    const upd_dev_view_t v = view_of(DEVUPD_ROW_ARMED);
    upd_dev_set(UPD_DEV_DIAL, &v);
    upd()->tick(1.0f);
    CHECK(!upd_dev_take(UPD_DEV_DIAL));
    CHECK(!upd_dev_pressed());
    /* Sliding off abandons it as well. */
    on_updates();
    hold_for(1.0f);
    ev(TOUCH_EVENT_MOVE, 1, DEV_X, UPD_DEV_BTN_Y - 20);
    upd()->tick(2.0f);
    CHECK(!upd_dev_take(UPD_DEV_DIAL));
    CHECK(!upd_dev_pressed());
}

TEST_CASE(one_press_at_a_time_with_the_other_holds)
{
    on_updates();
    ev(TOUCH_EVENT_DOWN, 1, UP_SET_X, UP_SET_Y);
    ev(TOUCH_EVENT_DOWN, 2, DEV_X, DEV_Y);
    CHECK(!upd_dev_pressed());
    upd()->tick(3.0f);
    CHECK(!upd_dev_take(UPD_DEV_DIAL));
    ev(TOUCH_EVENT_UP, 1, UP_SET_X, UP_SET_Y);
    /* The C6 row's press holds the Dial's off, and the other way. */
    on_updates();
    const upd_c6_view_t c6 = { .row = FW_C6_ROW_READY, .have_image = true };
    upd_c6_set(&c6);
    ev(TOUCH_EVENT_DOWN, 1, UP_C6_X, UP_C6_Y);
    ev(TOUCH_EVENT_DOWN, 2, DEV_X, DEV_Y);
    CHECK(upd_c6_pressed());
    CHECK(!upd_dev_pressed());
    ev(TOUCH_EVENT_UP, 1, UP_C6_X, UP_C6_Y);
    ev(TOUCH_EVENT_UP, 2, DEV_X, DEV_Y);
    ev(TOUCH_EVENT_DOWN, 1, DEV_X, DEV_Y);
    ev(TOUCH_EVENT_DOWN, 2, UP_C6_X, UP_C6_Y);
    CHECK(upd_dev_pressed());
    CHECK(!upd_c6_pressed());
    ev(TOUCH_EVENT_UP, 1, DEV_X, DEV_Y);
    ev(TOUCH_EVENT_UP, 2, UP_C6_X, UP_C6_Y);
    on_updates();
    ev(TOUCH_EVENT_DOWN, 1, DEV_X, DEV_Y);
    ev(TOUCH_EVENT_DOWN, 2, UP_SET_X, UP_SET_Y);
    ev(TOUCH_EVENT_UP, 2, UP_SET_X, UP_SET_Y);
    CHECK(!upd()->covered());
    CHECK(upd_dev_pressed());
    ev(TOUCH_EVENT_UP, 1, DEV_X, DEV_Y);
}

TEST_CASE(the_label_follows_the_update_phase)
{
    ui_text_set_language(UI_LANG_EN);
    char out[96];
    upd_dev_view_t v = view_of(DEVUPD_ROW_UPDATING);
    v.phase = DEVUPD_LINK;
    upd_dev_label(UPD_DEV_DIAL, &v, out, sizeof(out));
    CHECK(strcmp(out, "DIAL UPDATE: CONNECTING") == 0);
    v.phase = DEVUPD_CONFIRM;
    v.secs = 27u;
    upd_dev_label(UPD_DEV_DIAL, &v, out, sizeof(out));
    CHECK(strcmp(out, "PRESS THE DIAL TO ACCEPT: 27s") == 0);
    v.phase = DEVUPD_SEND;
    v.percent = 142u;
    upd_dev_label(UPD_DEV_DIAL, &v, out, sizeof(out));
    CHECK(strcmp(out, "DIAL UPDATE 100%") == 0);
    v.row = DEVUPD_ROW_REINSTALL;
    upd_dev_label(UPD_DEV_DIAL, &v, out, sizeof(out));
    CHECK(strcmp(out, "HOLD 2s TO REINSTALL THE DIAL FIRMWARE") == 0);
    v.row = DEVUPD_ROW_NOT_LINKED;
    v.known = false;
    upd_dev_label(UPD_DEV_DIAL, &v, out, sizeof(out));
    CHECK(strcmp(out, "HOLD 2s TO UPDATE THE DIAL") == 0);
    upd_dev_line(UPD_DEV_DIAL, &v, out, sizeof(out));
    CHECK(strcmp(out, "Dial: not connected. Included: 0.25.0.") == 0);
}

static void fits(const upd_dev_view_t *v)
{
    char line[96];
    upd_dev_line(UPD_DEV_DIAL, v, line, sizeof(line));
    CHECK(gfx_text_width(&gfx_font_10x20, line, 1) <= UPD_DEV_W);
    upd_dev_label(UPD_DEV_DIAL, v, line, sizeof(line));
    CHECK(gfx_text_width(&gfx_font_10x20, line, 1) <= UPD_DEV_W - 32);
}

TEST_CASE(the_line_and_the_label_fit_in_both_languages)
{
    for (int lang = 0; lang < 2; ++lang) {
        ui_text_set_language(lang ? UI_LANG_DE : UI_LANG_EN);
        for (int row = DEVUPD_ROW_NO_IMAGE; row <= DEVUPD_ROW_CHECKING;
             ++row) {
            for (int ph = DEVUPD_LINK; ph <= DEVUPD_RESTART; ++ph) {
                upd_dev_view_t v = view_of((devupd_row_t)row);
                v.phase = (uint8_t)ph;
                v.running = (fw_version_t){ 255u, 255u, 255u };
                v.carried = (fw_version_t){ 255u, 255u, 255u };
                v.percent = 100u;
                v.secs = 35u;
                fits(&v);
                v.known = false;
                v.have_image = false;
                fits(&v);
            }
        }
    }
    ui_text_set_language(UI_LANG_EN);
}

int main(void)
{
    RUN(the_hold_fires_at_2s_and_not_at_1999ms);
    RUN(a_short_press_shows_the_hint_and_the_reinstall_its_own);
    RUN(a_tap_on_the_grey_hold_says_why_on_the_band);
    RUN(a_state_change_under_the_finger_ends_the_hold_unfired);
    RUN(one_press_at_a_time_with_the_other_holds);
    RUN(the_label_follows_the_update_phase);
    RUN(the_line_and_the_label_fit_in_both_languages);
    return test_summary("upd_dev");
}
