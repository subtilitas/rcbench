/**
 * @file test_keypad_rows_supply.c
 * @brief SUPPLY's keypads held to keypad_rows_rig.h's four rules: the two
 *        set points on LIVE and every number row of the SETTINGS list.  A tap opens
 *        the keypad with the row's title, unit, range and digits; a value
 *        past the range is refused and the row keeps its value; CANCEL
 *        keeps it; OK sets the value typed, and a cap or trip the row's
 *        rule moves onto another step shows what was stored.
 *
 * The supply reports caps of 4.00 to 20.00V in 0.02V steps and 1.00 to
 * 4.50A in 0.05A steps, inside the schema's 3.3 to 21.0V and 0.5 to 5.0A,
 * so a range that is the caps' tells itself from one that is the
 * schema's.  The set points, the caps and the start values take the
 * supply's caps; the trips and the trip time the schema's range.
 *
 * SPDX-License-Identifier: MIT
 */
#include "supply_screen_rig.h"

#include "keypad_rows_rig.h"
#include "ui_text.h"

/** @brief The caps the supply reports. */
static const supply_caps_t k_caps = { 4.0f, 20.0f, 0.02f, 1.0f, 4.5f, 0.05f };

/** @brief The SETTINGS list's number rows, in the order of k_rows below
 *         from its third row on. */
static const setting_id_t k_ids[] = {
    SET_SUPPLY_V_MAX, SET_SUPPLY_I_MAX, SET_SUPPLY_V_START,
    SET_SUPPLY_I_START, SET_SUPPLY_TRIP_I, SET_SUPPLY_TRIP_V,
    SET_SUPPLY_TRIP_MS,
};
/** @brief The rows: the two set points' cards, then the list's. */
#define ROW_COUNT (2 + (int)(sizeof(k_ids) / sizeof(k_ids[0])))

/** @brief Whether the last row opened is the list's. */
static bool s_list;

/** @brief The keypad SUPPLY holds for the row opened last. */
static const ui_keypad_t *kp(void)
{
    return s_list ? supply_settings_keypad() : supply_screen_keypad();
}

/**
 * @brief A key of SUPPLY's keypad tapped, in the screen's coordinates.
 * @param k The key.
 */
static void kkey(ui_key_t k)
{
    const gfx_rect_t r = ui_keypad_key_rect(kp(), k);
    tap(r.x + r.w / 2, r.y + r.h / 2);
}

/**
 * @brief The keypad of row @p row: a tap on its card on LIVE, or on its
 *        value in the SETTINGS list.
 * @param row The row.
 */
static void open_row(int row)
{
    s_list = row >= 2;
    if (!s_list) {
        tap(CARD_X, row == 0 ? CARD_V_Y : CARD_I_Y);
        return;
    }
    if (!supply_screen_settings_shown()) {
        open_settings();
    }
    tap_value(k_ids[row - 2]);
}

/** @brief SUPPLY's keypad as keypad_rows_rig.h reaches it. */
static const kpr_screen_t k_supply = { kp, kkey, open_row };

/*
 * The set points take the caps narrowed by the operator's, the caps the
 * supply's own, the start values the set points' range, the trips and
 * the trip time the schema's.  The list's rows carry the setting's own
 * title, as SETUP's.
 */
TEST_CASE(every_supply_keypad_takes_ok_and_refuses_and_cancels)
{
    kpr_row_t rows[ROW_COUNT] = {
        { "set V", TR(SUP_VOLTAGE), "V", 4.0f, 20.0f, 2, 12.34f },
        { "set I", TR(SUP_CURRENT_LIMIT), "A", 1.0f, 4.5f, 2, 2.35f },
    };
    for (int r = 2; r < ROW_COUNT; ++r) {
        rows[r] = kpr_setting(k_ids[r - 2], NULL);
    }
    /* The caps and the start values: the supply's caps, as the set points. */
    static const float k_ok[4] = { 15.0f, 3.0f, 9.0f, 1.5f };
    for (int r = 2; r < 6; ++r) {
        const bool volts = (r % 2) == 0;
        rows[r].min = volts ? k_caps.v_min : k_caps.i_min;
        rows[r].max = volts ? k_caps.v_max : k_caps.i_max;
        rows[r].ok  = k_ok[r - 2];
    }
    for (int r = 0; r < ROW_COUNT; ++r) {
        fresh();
        supply_screen_set_caps(&k_caps);
        kpr_row(&k_supply, r, &rows[r]);
    }
    /* What OK set reached the set point and the settings. */
    fresh();
    supply_screen_set_caps(&k_caps);
    open_row(0);
    kpr_type(&k_supply, "12.34");
    kkey(UI_KEY_OK);
    CHECK_NEAR(supply_screen_set_v(), 12.34f, 1e-4f);
    open_row(8);
    kpr_type(&k_supply, "250");
    kkey(UI_KEY_OK);
    CHECK_EQ(settings_get_int(SET_SUPPLY_TRIP_MS), 250);
}

/*
 * Every number of the schema's supply category has a row above: the
 * seven rows of the SETTINGS list, each once.
 */
TEST_CASE(every_supply_number_has_a_keypad_row)
{
    setting_id_t cat[64];
    const int n = settings_in_category(SET_CAT_SUPPLY, cat, 64);
    int numbers = 0;
    for (int i = 0; i < n; ++i) {
        const setting_type_t t = settings_def(cat[i])->type;
        if (t != SET_TYPE_INT && t != SET_TYPE_FLOAT) {
            continue;
        }
        ++numbers;
        int seen = 0;
        for (size_t k = 0; k < sizeof(k_ids) / sizeof(k_ids[0]); ++k) {
            seen += (k_ids[k] == cat[i]) ? 1 : 0;
        }
        CHECK_EQ(seen, 1);
    }
    CHECK_EQ(numbers, 7);
    /* Each opens the keypad with its unit, from its row. */
    for (int r = 2; r < ROW_COUNT; ++r) {
        fresh();
        open_row(r);
        CHECK(kp()->open);
        CHECK_STR_EQ(kp()->unit, settings_def(k_ids[r - 2])->unit);
        kkey(UI_KEY_CANCEL);
    }
}

/*
 * A cap typed under the start value takes the start value down with it,
 * and both wait for the list's SAVE: no save is asked, and DISCARD puts
 * back the 21.00V cap and the 6.00V start value.
 */
TEST_CASE(a_cap_under_the_start_value_waits_for_save)
{
    fresh();
    supply_screen_set_caps(&k_caps);
    CHECK_NEAR(settings_get(SET_SUPPLY_V_START), 6.0f, 1e-4f);
    open_row(2);
    kpr_type(&k_supply, "5");
    kkey(UI_KEY_OK);
    CHECK_NEAR(settings_get(SET_SUPPLY_V_MAX), 5.0f, 1e-4f);
    CHECK_NEAR(settings_get(SET_SUPPLY_V_START), 5.0f, 1e-4f);
    CHECK(settings_dirty());
    CHECK(!settings_save_asked());
    CHECK_EQ(settings_discard(), 0);
    CHECK_NEAR(settings_get(SET_SUPPLY_V_MAX), 21.0f, 1e-4f);
    CHECK_NEAR(settings_get(SET_SUPPLY_V_START), 6.0f, 1e-4f);
    CHECK(!settings_save_asked());
}

/*
 * A cap rounds down and a trip above 0 stays a trip (row_typed()): what
 * was stored differs from what was typed, and the alert band's notice
 * names it.  A value on a step shows none.
 */
TEST_CASE(a_rounded_cap_or_trip_names_what_was_stored)
{
    fresh();
    supply_screen_set_caps(&k_caps);
    open_row(3);                               /* Current max */
    kpr_type(&k_supply, "1.03");
    kkey(UI_KEY_OK);
    CHECK_NEAR(settings_get(SET_SUPPLY_I_MAX), 1.0f, 1e-4f);
    CHECK(ui_router_alert_is_notice());
    CHECK_STR_EQ(ui_router_alert(),
                 "Current max: 1.00A stored, steps of 0.05A.");
    ui_router_set_alert(NULL);

    open_row(6);                               /* Current trip */
    kpr_type(&k_supply, "0.02");
    kkey(UI_KEY_OK);
    CHECK_NEAR(settings_get(SET_SUPPLY_TRIP_I), 0.05f, 1e-4f);
    CHECK_STR_EQ(ui_router_alert(),
                 "Current trip: 0.05A stored, steps of 0.05A.");
    ui_router_set_alert(NULL);

    open_row(3);
    kpr_type(&k_supply, "1.5");
    kkey(UI_KEY_OK);
    CHECK_NEAR(settings_get(SET_SUPPLY_I_MAX), 1.5f, 1e-4f);
    CHECK(ui_router_alert() == NULL || ui_router_alert()[0] == '\0');
}

int main(void)
{
    RUN(every_supply_keypad_takes_ok_and_refuses_and_cancels);
    RUN(every_supply_number_has_a_keypad_row);
    RUN(a_cap_under_the_start_value_waits_for_save);
    RUN(a_rounded_cap_or_trip_names_what_was_stored);
    return test_summary("keypad_rows_supply");
}
