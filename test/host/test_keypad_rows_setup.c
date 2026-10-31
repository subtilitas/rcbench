/**
 * @file test_keypad_rows_setup.c
 * @brief Every number row of SETUP's lists (PANEL, SD CARD,
 *        SENSORS) held to keypad_rows_rig.h's four rules: a tap on its
 *        value opens the keypad with the row's label, unit, range and its
 *        step's digits; a value past the range is refused and the row
 *        keeps its value; CANCEL keeps it; OK sets the value typed.
 *
 * The rows are the schema's, read at run time: a number added to one of
 * these categories is held here without a line of this file changing.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "settings_rig.h"

#include "keypad_rows_rig.h"

/* The value between a row's "-" and "+" keys, in panel coordinates. */
#define XV ((MINUS_X + BTN_W + PLUS_X) / 2)

/** @brief SETUP's lists: the categories its tiles open. */
static const setting_cat_t k_cats[] = {
    SET_CAT_APP, SET_CAT_CARD, SET_CAT_SENSORS,
};
#define CAT_COUNT ((int)(sizeof(k_cats) / sizeof(k_cats[0])))

/** @brief The setting whose row the case holds. */
static setting_id_t s_id;

/**
 * @brief A drag up the list by about @p dy px, from the label column.
 * @param dy The drag, px.
 */
static void scroll_by(int dy)
{
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = XL,
                                   .y = (int16_t)(LIST_Y + LIST_H - 20) } };
    ui_router_event(&e);
    e.type = TOUCH_EVENT_MOVE;
    for (int moved = 6; moved <= dy; moved += 6) {
        e.point.y = (int16_t)(LIST_Y + LIST_H - 20 - moved);
        ui_router_event(&e);
    }
    e.type = TOUCH_EVENT_UP;
    ui_router_event(&e);
}

/**
 * @brief The row of @p id in the open list, scrolled into view, from the
 *        top down.
 * @param id The setting.
 * @return The row's middle y in panel coordinates; -1 for a row the list
 *         does not show.
 */
static int show(setting_id_t id)
{
    const setting_cat_t cat = settings_def(id)->cat;
    if (cat == SET_CAT_SENSORS) {
        open_sensor(id);
    }
    setting_id_t ids[64];
    const int n = settings_in_category(cat, ids, 64);
    int row = -1;
    for (int i = 0; i < n; ++i) {
        if (ids[i] == id) {
            row = settings_screen_row_of(cat, i);
        }
    }
    if (row < 0) {
        return -1;
    }
    for (int k = 0; k < 60; ++k) {
        const int top = LIST_Y + row * ROW_PITCH - settings_screen_scroll(NULL);
        if (top >= LIST_Y && top + ROW_H <= LIST_Y + LIST_H) {
            return top + ROW_H / 2;
        }
        scroll_by(96);
    }
    return -1;
}

/**
 * @brief SETUP on the list of @p cat, at the defaults with all four
 *        INA3221 parts FITTED, so SENSORS lists every channel's rows.
 * @param cat The category, opened from its tile.
 */
static void list_of(setting_cat_t cat)
{
    fresh_hub();
    for (int p = 0; p < 4; ++p) {
        settings_set((setting_id_t)(SET_INA_FIT_0 + p), 1.0f);
    }
    open_category(cat);
}

/** @brief The keypad SETUP holds. */
static const ui_keypad_t *kp(void) { return settings_screen_keypad(); }

/**
 * @brief A key of SETUP's keypad tapped, in panel coordinates.
 * @param k The key.
 */
static void kkey(ui_key_t k)
{
    const gfx_rect_t r = ui_keypad_key_rect(settings_screen_keypad(), k);
    tap(r.x + r.w / 2, UI_BAND_H + r.y + r.h / 2);
}

/**
 * @brief The keypad of s_id's row, by a tap on its value.
 * @param row Unused: the row is s_id's.
 */
static void open_row(int row)
{
    (void)row;
    const int y = show(s_id);
    if (y < 0) {
        T_FAIL("setting %d: not on its list", (int)s_id);
        return;
    }
    tap(XV, y);
}

/** @brief SETUP's keypad as keypad_rows_rig.h reaches it. */
static const kpr_screen_t k_setup = { kp, kkey, open_row };

/*
 * Every number row of SETUP's four lists the keypad is offered on: its
 * keypad opens with the row's label, unit and range and the digits of its
 * step, refuses a value past the range, keeps the row on CANCEL and sets
 * the value OK hands it.
 */
TEST_CASE(every_number_row_on_setup_takes_ok_and_refuses_and_cancels)
{
    int rows = 0;
    for (int c = 0; c < CAT_COUNT; ++c) {
        setting_id_t ids[64];
        const int n = settings_in_category(k_cats[c], ids, 64);
        for (int i = 0; i < n; ++i) {
            const setting_id_t id = ids[i];
            const setting_def_t *d = settings_def(id);
            if (d->type != SET_TYPE_INT && d->type != SET_TYPE_FLOAT) {
                continue;
            }
            /* The optical tacho's pin: its key stays, no entry shows it. */
            if (k_cats[c] == SET_CAT_SENSORS && sensors_pane_entry_of(id) < 0) {
                continue;
            }
            list_of(k_cats[c]);
            if (!ui_setting_keypad_offered(id)) {
                /* Sensor SCL: derived from SDA, no keys and no keypad. */
                CHECK(settings_derived(id));
                const int y = show(id);
                CHECK(y > 0);
                tap(XV, y);
                CHECK(!settings_screen_keypad()->open);
                continue;
            }
            s_id = id;
            const kpr_row_t e = kpr_setting(id, NULL);
            kpr_row(&k_setup, 0, &e);
            ++rows;
        }
    }
    /* PANEL 4, SENSORS 34: every number row an entry shows. */
    CHECK_EQ(rows, 38);
}

/*
 * A value inside the range the "-" and "+" keys never reach is refused
 * with its reason in place of the range, and the row keeps its value: a
 * 0x40 CH1 shunt of 3.0mΩ, between OFF and 5.0mΩ.
 */
TEST_CASE(a_value_the_steps_never_reach_is_refused_and_kept)
{
    list_of(SET_CAT_SENSORS);
    const setting_id_t id = SET_INA_SH_01;
    const float was = settings_get(id);
    s_id = id;
    open_row(0);
    kpr_type(&k_setup, "3");
    kkey(UI_KEY_OK);
    CHECK(kp()->open);
    CHECK(kp()->refused);
    char why[64];
    CHECK_STR_EQ(kp()->reason, ui_setting_keypad_why(id, 3.0f, why,
                                                     sizeof(why)));
    kkey(UI_KEY_CANCEL);
    CHECK_NEAR(settings_get(id), was, 1e-6f);
}

int main(void)
{
    RUN(every_number_row_on_setup_takes_ok_and_refuses_and_cancels);
    RUN(a_value_the_steps_never_reach_is_refused_and_kept);
    return test_summary("keypad_rows_setup");
}
