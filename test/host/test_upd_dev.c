/*
 * SETUP > UPDATES' wireless devices (upd_dev.c): the WIRELESS DEVICES
 * entry opening the page, BACK and the home tag closing it, the page
 * covering UPDATES; on the page, for DIAL FIRMWARE, POWER NODE FIRMWARE
 * and BALANCE NODE FIRMWARE each: the hold at 1,999ms and at 2,000ms,
 * once, for an update and for a reinstall; a short press's hint on the
 * band; a tap on the grey hold naming why on the band, for every grey
 * state; a state change under the finger ending the hold unfired; one
 * press at a time across the rows, and on UPDATES with SETTINGS and the
 * C6 row; the label of every phase of a running update, the node rows
 * asking for the BOOT hold; the entry's summary and label; CANCEL UPDATE
 * as a plain tap in every phase before END, grey with its reason from END
 * on, absent with no update; the line and the labels inside their fields
 * in both languages.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "devupd.h"
#include "settings.h"
#include "ui_footer.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "upd_c6.h"
#include "upd_dev.h"
#include "upd_screen.h"

#include "upd_spots.h"

/** @brief The entry's middle on UPDATES, body coordinates. @{ */
#define ENTRY_X (UPD_DEV_X + UPD_DEV_W / 2)
#define ENTRY_Y (UPD_DEV_BTN_Y + UPD_DEV_BTN_H / 2)
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

/** @brief Row @p id's hold's middle on the page. @{ */
static int row_x(upd_dev_id_t id)
{
    const gfx_rect_t r = upd_dev_rect(id);
    return r.x + r.w / 2;
}

static int row_y(upd_dev_id_t id)
{
    const gfx_rect_t r = upd_dev_rect(id);
    return r.y + r.h / 2;
}
/** @} */

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

static void tap(int x, int y)
{
    ev(TOUCH_EVENT_DOWN, 7, x, y);
    ev(TOUCH_EVENT_UP, 7, x, y);
}

/* UPDATES open, every device row READY, the C6 row grey; the page closed. */
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
    for (int i = 0; i < UPD_DEV_COUNT; ++i) {
        const upd_dev_view_t v = view_of(DEVUPD_ROW_READY);
        upd_dev_set((upd_dev_id_t)i, &v);
        (void)upd_dev_take((upd_dev_id_t)i);
    }
}

/* on_updates(), and the page opened by a tap on the entry. */
static void on_page(void)
{
    on_updates();
    tap(ENTRY_X, ENTRY_Y);
}

static void hold_for(upd_dev_id_t id, float seconds)
{
    ev(TOUCH_EVENT_DOWN, 1, row_x(id), row_y(id));
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

TEST_CASE(the_entry_opens_the_page_and_back_and_the_home_tag_close_it)
{
    on_updates();
    CHECK(!upd_dev_shown());
    CHECK(!upd()->covered());
    /* A hold's spot on the page is nothing on UPDATES. */
    hold_for(UPD_DEV_PNODE, UI_HOLD_S + 0.5f);
    ev(TOUCH_EVENT_UP, 1, row_x(UPD_DEV_PNODE), row_y(UPD_DEV_PNODE));
    CHECK(!upd_dev_take(UPD_DEV_PNODE));
    /* A press that slides off the entry opens nothing. */
    ev(TOUCH_EVENT_DOWN, 1, ENTRY_X, ENTRY_Y);
    CHECK(upd_dev_pressed());
    ev(TOUCH_EVENT_MOVE, 1, ENTRY_X, UPD_DEV_BTN_Y - 30);
    ev(TOUCH_EVENT_UP, 1, ENTRY_X, UPD_DEV_BTN_Y - 30);
    CHECK(!upd_dev_shown());
    tap(ENTRY_X, ENTRY_Y);
    CHECK(upd_dev_shown());
    CHECK(upd()->covered());
    /* BACK closes it. */
    const gfx_rect_t b = upd_dev_back_rect();
    tap(b.x + b.w / 2, b.y + b.h / 2);
    CHECK(!upd_dev_shown());
    /* The home tag closes it and stays on UPDATES; a hold under way ends
     * unfired. */
    tap(ENTRY_X, ENTRY_Y);
    hold_for(UPD_DEV_BAL, 1.0f);
    CHECK(upd()->back());
    CHECK(!upd_dev_shown());
    upd()->tick(2.0f);
    CHECK(!upd_dev_take(UPD_DEV_BAL));
    CHECK(!upd_dev_pressed());
    CHECK(!upd()->back());          /* closed: the tag leaves UPDATES */
    /* Leaving UPDATES closes it. */
    tap(ENTRY_X, ENTRY_Y);
    ui_router_goto(SCREEN_SETUP);
    CHECK(!upd_dev_shown());
}

TEST_CASE(each_hold_fires_at_2s_and_not_at_1999ms)
{
    for (int i = 0; i < UPD_DEV_COUNT; ++i) {
        const upd_dev_id_t id = (upd_dev_id_t)i;
        on_page();
        hold_for(id, UI_HOLD_S - 0.001f);
        CHECK(!upd_dev_take(id));
        upd()->tick(0.001f);
        CHECK(upd_dev_take(id));
        for (int k = 0; k < UPD_DEV_COUNT; ++k) {
            CHECK(!upd_dev_take((upd_dev_id_t)k));   /* only its own */
        }
        upd()->tick(1.0f);
        CHECK(!upd_dev_take(id));
        ev(TOUCH_EVENT_UP, 1, row_x(id), row_y(id));
        CHECK(!upd_dev_take(id));
        CHECK(!upd_dev_pressed());
        CHECK(upd_dev_shown());
    }
}

TEST_CASE(a_short_press_shows_each_rows_hint_and_its_reinstall_hint)
{
    static const ui_text_id_t k_hint[UPD_DEV_COUNT][2] = {
        { TX_HOLD_HINT_DIAL_UPDATE, TX_HOLD_HINT_DIAL_REINSTALL },
        { TX_HOLD_HINT_PNODE_UPDATE, TX_HOLD_HINT_PNODE_REINSTALL },
        { TX_HOLD_HINT_BAL_UPDATE, TX_HOLD_HINT_BAL_REINSTALL },
    };
    for (int i = 0; i < UPD_DEV_COUNT; ++i) {
        const upd_dev_id_t id = (upd_dev_id_t)i;
        on_page();
        hold_for(id, 0.5f);
        ev(TOUCH_EVENT_UP, 1, row_x(id), row_y(id));
        CHECK(!upd_dev_take(id));
        CHECK(band_says(ui_tr(k_hint[i][0])));
        upd_dev_view_t v = view_of(DEVUPD_ROW_REINSTALL);
        v.running = k_carried;
        upd_dev_set(id, &v);
        hold_for(id, 0.5f);
        ev(TOUCH_EVENT_UP, 1, row_x(id), row_y(id));
        CHECK(band_says(ui_tr(k_hint[i][1])));
        hold_for(id, UI_HOLD_S);
        CHECK(upd_dev_take(id));
        ev(TOUCH_EVENT_UP, 1, row_x(id), row_y(id));
    }
}

TEST_CASE(a_tap_on_a_grey_hold_says_why_on_the_band)
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
    for (int d = 0; d < UPD_DEV_COUNT; ++d) {
        const upd_dev_id_t id = (upd_dev_id_t)d;
        for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
            on_page();
            const upd_dev_view_t v = view_of(k[i].row);
            upd_dev_set(id, &v);
            ev(TOUCH_EVENT_DOWN, 1, row_x(id), row_y(id));
            upd()->tick(3.0f);
            ev(TOUCH_EVENT_UP, 1, row_x(id), row_y(id));
            CHECK(!upd_dev_take(id));
            CHECK(band_says(ui_tr(k[i].why)));
        }
    }
    const upd_dev_view_t ready = view_of(DEVUPD_ROW_READY);
    CHECK(upd_dev_reason(&ready) == NULL);
    CHECK(upd_dev_reason(NULL) == NULL);
}

TEST_CASE(a_state_change_under_the_finger_ends_the_hold_unfired)
{
    for (int d = 0; d < UPD_DEV_COUNT; ++d) {
        const upd_dev_id_t id = (upd_dev_id_t)d;
        on_page();
        hold_for(id, 1.5f);
        const upd_dev_view_t v = view_of(DEVUPD_ROW_ARMED);
        upd_dev_set(id, &v);
        upd()->tick(1.0f);
        CHECK(!upd_dev_take(id));
        CHECK(!upd_dev_pressed());
        /* Sliding off abandons it as well. */
        on_page();
        hold_for(id, 1.0f);
        ev(TOUCH_EVENT_MOVE, 1, row_x(id), upd_dev_rect(id).y - 20);
        upd()->tick(2.0f);
        CHECK(!upd_dev_take(id));
        CHECK(!upd_dev_pressed());
    }
}

TEST_CASE(one_press_at_a_time_on_the_page_and_on_updates)
{
    /* Two rows: the second finger is nobody's. */
    on_page();
    ev(TOUCH_EVENT_DOWN, 1, row_x(UPD_DEV_DIAL), row_y(UPD_DEV_DIAL));
    ev(TOUCH_EVENT_DOWN, 2, row_x(UPD_DEV_BAL), row_y(UPD_DEV_BAL));
    for (float t = 0.0f; t < UI_HOLD_S + 0.05f; t += 0.05f) {
        upd()->tick(0.05f);
    }
    CHECK(upd_dev_take(UPD_DEV_DIAL));
    CHECK(!upd_dev_take(UPD_DEV_BAL));
    ev(TOUCH_EVENT_UP, 2, row_x(UPD_DEV_BAL), row_y(UPD_DEV_BAL));
    ev(TOUCH_EVENT_UP, 1, row_x(UPD_DEV_DIAL), row_y(UPD_DEV_DIAL));
    /* A row's press keeps BACK from closing the page. */
    const gfx_rect_t b = upd_dev_back_rect();
    ev(TOUCH_EVENT_DOWN, 1, row_x(UPD_DEV_PNODE), row_y(UPD_DEV_PNODE));
    tap(b.x + b.w / 2, b.y + b.h / 2);
    CHECK(upd_dev_shown());
    ev(TOUCH_EVENT_UP, 1, row_x(UPD_DEV_PNODE), row_y(UPD_DEV_PNODE));
    /* On UPDATES: SETTINGS' press keeps the entry off. */
    on_updates();
    ev(TOUCH_EVENT_DOWN, 1, UP_SET_X, UP_SET_Y);
    ev(TOUCH_EVENT_DOWN, 2, ENTRY_X, ENTRY_Y);
    CHECK(!upd_dev_pressed());
    ev(TOUCH_EVENT_UP, 2, ENTRY_X, ENTRY_Y);
    CHECK(!upd_dev_shown());
    ev(TOUCH_EVENT_UP, 1, UP_SET_X, UP_SET_Y);
    /* The C6 row's press holds the entry off, and the other way. */
    on_updates();
    const upd_c6_view_t c6 = { .row = FW_C6_ROW_READY, .have_image = true };
    upd_c6_set(&c6);
    ev(TOUCH_EVENT_DOWN, 1, UP_C6_X, UP_C6_Y);
    ev(TOUCH_EVENT_DOWN, 2, ENTRY_X, ENTRY_Y);
    CHECK(upd_c6_pressed());
    CHECK(!upd_dev_pressed());
    ev(TOUCH_EVENT_UP, 1, UP_C6_X, UP_C6_Y);
    ev(TOUCH_EVENT_UP, 2, ENTRY_X, ENTRY_Y);
    CHECK(!upd_dev_shown());
    ev(TOUCH_EVENT_DOWN, 1, ENTRY_X, ENTRY_Y);
    ev(TOUCH_EVENT_DOWN, 2, UP_C6_X, UP_C6_Y);
    CHECK(upd_dev_pressed());
    CHECK(!upd_c6_pressed());
    ev(TOUCH_EVENT_UP, 2, UP_C6_X, UP_C6_Y);
    ev(TOUCH_EVENT_UP, 1, ENTRY_X, ENTRY_Y);
    CHECK(upd_dev_shown());
}

TEST_CASE(the_label_follows_the_update_phase)
{
    ui_text_set_language(UI_LANG_EN);
    char out[96];
    upd_dev_view_t v = view_of(DEVUPD_ROW_UPDATING);
    v.phase = DEVUPD_LINK;
    upd_dev_label(UPD_DEV_DIAL, &v, out, sizeof(out));
    CHECK_STR_EQ(out, "DIAL UPDATE: CONNECTING");
    upd_dev_label(UPD_DEV_PNODE, &v, out, sizeof(out));
    CHECK_STR_EQ(out, "POWER NODE UPDATE: CONNECTING");
    upd_dev_label(UPD_DEV_BAL, &v, out, sizeof(out));
    CHECK_STR_EQ(out, "BALANCE NODE UPDATE: CONNECTING");
    v.phase = DEVUPD_CONFIRM;
    v.secs = 27u;
    upd_dev_label(UPD_DEV_DIAL, &v, out, sizeof(out));
    CHECK_STR_EQ(out, "PRESS THE DIAL TO ACCEPT: 27s");
    upd_dev_label(UPD_DEV_PNODE, &v, out, sizeof(out));
    CHECK_STR_EQ(out, "HOLD BOOT 1s ON THE NODE: 27s");
    upd_dev_label(UPD_DEV_BAL, &v, out, sizeof(out));
    CHECK_STR_EQ(out, "HOLD BOOT 1s ON THE NODE: 27s");
    v.phase = DEVUPD_SEND;
    v.percent = 142u;
    upd_dev_label(UPD_DEV_DIAL, &v, out, sizeof(out));
    CHECK_STR_EQ(out, "DIAL UPDATE 100%");
    v.percent = 42u;
    upd_dev_label(UPD_DEV_PNODE, &v, out, sizeof(out));
    CHECK_STR_EQ(out, "POWER NODE UPDATE 42%");
    upd_dev_label(UPD_DEV_BAL, &v, out, sizeof(out));
    CHECK_STR_EQ(out, "BALANCE NODE UPDATE 42%");
    v.row = DEVUPD_ROW_REINSTALL;
    upd_dev_label(UPD_DEV_DIAL, &v, out, sizeof(out));
    CHECK_STR_EQ(out, "HOLD 2s TO REINSTALL THE DIAL FIRMWARE");
    upd_dev_label(UPD_DEV_PNODE, &v, out, sizeof(out));
    CHECK_STR_EQ(out, "HOLD 2s TO REINSTALL THE POWER NODE");
    v.row = DEVUPD_ROW_NOT_LINKED;
    v.known = false;
    upd_dev_label(UPD_DEV_DIAL, &v, out, sizeof(out));
    CHECK_STR_EQ(out, "HOLD 2s TO UPDATE THE DIAL");
    upd_dev_label(UPD_DEV_BAL, &v, out, sizeof(out));
    CHECK_STR_EQ(out, "HOLD 2s TO UPDATE THE BALANCE NODE");
    upd_dev_line(UPD_DEV_DIAL, &v, out, sizeof(out));
    CHECK_STR_EQ(out, "Dial: not connected. Included: 0.25.0.");
    upd_dev_line(UPD_DEV_PNODE, &v, out, sizeof(out));
    CHECK_STR_EQ(out, "Node: not connected. Included: 0.25.0.");
}

TEST_CASE(the_entry_counts_the_devices_and_shows_a_running_update)
{
    on_updates();
    char out[96];
    upd_dev_summary(out, sizeof(out));
    CHECK_STR_EQ(out, "Connected: 3 of 3. Another version: 3.");
    upd_dev_entry_label(out, sizeof(out));
    CHECK_STR_EQ(out, "WIRELESS DEVICE FIRMWARE");
    upd_dev_view_t v = view_of(DEVUPD_ROW_NOT_LINKED);
    v.known = false;
    upd_dev_set(UPD_DEV_DIAL, &v);
    v = view_of(DEVUPD_ROW_REINSTALL);
    v.running = k_carried;
    upd_dev_set(UPD_DEV_BAL, &v);
    upd_dev_summary(out, sizeof(out));
    CHECK_STR_EQ(out, "Connected: 2 of 3. Another version: 1.");
    v = view_of(DEVUPD_ROW_UPDATING);
    v.phase = DEVUPD_CONFIRM;
    v.secs = 12u;
    upd_dev_set(UPD_DEV_PNODE, &v);
    upd_dev_entry_label(out, sizeof(out));
    CHECK_STR_EQ(out, "HOLD BOOT 1s ON THE NODE: 12s");
}

static void fits(upd_dev_id_t id, const upd_dev_view_t *v)
{
    char line[96];
    upd_dev_line(id, v, line, sizeof(line));
    CHECK(gfx_text_width(&gfx_font_10x20, line, 1) <= UPD_DEVP_TEXT_W);
    upd_dev_label(id, v, line, sizeof(line));
    CHECK(gfx_text_width(&gfx_font_10x20, line, 1) <= UPD_DEVP_HOLD_W - 32);
    /* The entry shows a running row's label. */
    CHECK(gfx_text_width(&gfx_font_10x20, line, 1) <= UPD_DEV_W - 32
          || v->row != DEVUPD_ROW_UPDATING);
}

TEST_CASE(the_lines_and_the_labels_fit_in_both_languages)
{
    for (int lang = 0; lang < 2; ++lang) {
        ui_text_set_language(lang ? UI_LANG_DE : UI_LANG_EN);
        for (int d = 0; d < UPD_DEV_COUNT; ++d) {
            for (int row = DEVUPD_ROW_NO_IMAGE; row <= DEVUPD_ROW_CHECKING;
                 ++row) {
                for (int ph = DEVUPD_LINK; ph <= DEVUPD_RESTART; ++ph) {
                    upd_dev_view_t v = view_of((devupd_row_t)row);
                    v.phase = (uint8_t)ph;
                    v.running = (fw_version_t){ 255u, 255u, 255u };
                    v.carried = (fw_version_t){ 255u, 255u, 255u };
                    v.percent = 100u;
                    v.secs = 35u;
                    fits((upd_dev_id_t)d, &v);
                    v.known = false;
                    v.have_image = false;
                    fits((upd_dev_id_t)d, &v);
                }
            }
        }
        char line[96];
        upd_dev_summary(line, sizeof(line));
        CHECK(gfx_text_width(&gfx_font_10x20, line, 1) <= UPD_DEV_W);
        upd_dev_entry_label(line, sizeof(line));
        CHECK(gfx_text_width(&gfx_font_10x20, line, 1) <= UPD_DEV_W - 32);
    }
    ui_text_set_language(UI_LANG_EN);
}

/* CANCEL UPDATE stands while an update runs: a plain tap asks for the
 * cancel in every phase before END; from END on it is grey and the band
 * says the install runs; with no update it is not there. */
TEST_CASE(cancel_is_a_tap_before_end_and_grey_through_the_install)
{
    const gfx_rect_t c = upd_dev_cancel_rect();
    const int cx = c.x + c.w / 2, cy = c.y + c.h / 2;
    /* No update: nothing there. */
    on_page();
    tap(cx, cy);
    CHECK(!upd_dev_take_cancel());
    CHECK(upd_dev_shown());
    static const uint8_t k_live[] = {
        DEVUPD_IDLE, DEVUPD_LINK, DEVUPD_BEGIN, DEVUPD_CONFIRM, DEVUPD_SEND,
    };
    for (size_t i = 0; i < sizeof(k_live); ++i) {
        on_page();
        upd_dev_view_t v = view_of(DEVUPD_ROW_UPDATING);
        v.phase = k_live[i];
        upd_dev_set(UPD_DEV_PNODE, &v);
        ev(TOUCH_EVENT_DOWN, 1, cx, cy);
        CHECK(!upd_dev_take_cancel());        /* the release acts */
        ev(TOUCH_EVENT_UP, 1, cx, cy);
        CHECK(upd_dev_take_cancel());
        CHECK(!upd_dev_take_cancel());        /* once */
        CHECK(upd_dev_shown());
    }
    static const uint8_t k_late[] = { DEVUPD_END, DEVUPD_RESTART };
    for (size_t i = 0; i < sizeof(k_late); ++i) {
        on_page();
        upd_dev_view_t v = view_of(DEVUPD_ROW_UPDATING);
        v.phase = k_late[i];
        upd_dev_set(UPD_DEV_DIAL, &v);
        tap(cx, cy);
        CHECK(!upd_dev_take_cancel());
        CHECK(band_says(TR(DEV_WHY_INSTALLING)));
    }
    /* A row's press keeps the cancel off; sliding off asks nothing. */
    on_page();
    upd_dev_view_t v = view_of(DEVUPD_ROW_UPDATING);
    v.phase = DEVUPD_SEND;
    upd_dev_set(UPD_DEV_BAL, &v);
    ev(TOUCH_EVENT_DOWN, 1, row_x(UPD_DEV_DIAL), row_y(UPD_DEV_DIAL));
    tap(cx, cy);
    CHECK(!upd_dev_take_cancel());
    ev(TOUCH_EVENT_UP, 1, row_x(UPD_DEV_DIAL), row_y(UPD_DEV_DIAL));
    ev(TOUCH_EVENT_DOWN, 1, cx, cy);
    ev(TOUCH_EVENT_MOVE, 1, cx, c.y - 30);
    ev(TOUCH_EVENT_UP, 1, cx, c.y - 30);
    CHECK(!upd_dev_take_cancel());
}

int main(void)
{
    RUN(the_entry_opens_the_page_and_back_and_the_home_tag_close_it);
    RUN(each_hold_fires_at_2s_and_not_at_1999ms);
    RUN(a_short_press_shows_each_rows_hint_and_its_reinstall_hint);
    RUN(a_tap_on_a_grey_hold_says_why_on_the_band);
    RUN(a_state_change_under_the_finger_ends_the_hold_unfired);
    RUN(one_press_at_a_time_on_the_page_and_on_updates);
    RUN(the_label_follows_the_update_phase);
    RUN(the_entry_counts_the_devices_and_shows_a_running_update);
    RUN(the_lines_and_the_labels_fit_in_both_languages);
    RUN(cancel_is_a_tap_before_end_and_grey_through_the_install);
    return test_summary("upd_dev");
}
