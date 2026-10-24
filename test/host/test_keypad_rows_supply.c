/**
 * @file test_keypad_rows_supply.c
 * @brief SUPPLY's keypads held to keypad_rows_rig.h's four rules: the two
 *        set points on LIVE and every number row of SETTINGS.  A tap opens
 *        the keypad with the row's title, unit, range and digits; a value
 *        past the range is refused and the row keeps its value; CANCEL
 *        keeps it; OK sets the value typed.
 *
 * The supply reports caps of 4.00 to 20.00V in 0.02V steps and 1.00 to
 * 4.50A in 0.05A steps, inside the schema's 3.3 to 21.0V and 0.5 to 5.0A,
 * so a range that is the caps' tells itself from one that is the
 * schema's.
 *
 * SPDX-License-Identifier: MIT
 */
#include "supply_screen_rig.h"

#include "keypad_rows_rig.h"
#include "ui_text.h"

/** @brief The caps the supply reports. */
static const supply_caps_t k_caps = { 4.0f, 20.0f, 0.02f, 1.0f, 4.5f, 0.05f };

/** @brief A row: where a tap opens its keypad, and on which task. */
typedef struct {
    bool settings;  /**< on SETTINGS, else on LIVE */
    int  x, y;      /**< the tap, in the screen's coordinates */
} where_t;

/** @brief The rows, in the order of k_rows below. */
static const where_t k_where[] = {
    { false, CARD_X,   CARD_V_Y },              /* VOLTAGE's card     */
    { false, CARD_X,   CARD_I_Y },              /* CURRENT's card     */
    { true,  ROW_L_X,  VMAX_Y },                /* VOLTAGE MAX        */
    { true,  ROW_L_X,  VMAX_Y + SU_ROW_PITCH }, /* CURRENT MAX        */
    { true,  ROW_L_X,  SU_VSTART_Y },           /* START VOLTAGE      */
    { true,  ROW_L_X,  ISTART_Y },              /* START CURRENT      */
    { true,  ROW_R_X,  TRIPI_Y },               /* CURRENT TRIP       */
    { true,  ROW_R_X,  TRIPI_Y + SU_ROW_PITCH },     /* VOLTAGE TRIP  */
    { true,  ROW_R_X,  TRIPI_Y + 2 * SU_ROW_PITCH }, /* TRIP TIME     */
};
#define ROW_COUNT ((int)(sizeof(k_where) / sizeof(k_where[0])))

/** @brief The keypad SUPPLY holds. */
static const ui_keypad_t *kp(void) { return supply_screen_keypad(); }

/**
 * @brief A key of SUPPLY's keypad tapped, in the screen's coordinates.
 * @param k The key.
 */
static void kkey(ui_key_t k)
{
    const gfx_rect_t r = ui_keypad_key_rect(supply_screen_keypad(), k);
    tap(r.x + r.w / 2, r.y + r.h / 2);
}

/**
 * @brief The keypad of row @p row, by a tap on its card or its row.
 * @param row The row, an index of k_where.
 */
static void open_row(int row)
{
    tap(k_where[row].settings ? SET_TAB_X : LIVE_TAB_X, STRIP_Y);
    tap(k_where[row].x, k_where[row].y);
}

/** @brief SUPPLY's keypad as keypad_rows_rig.h reaches it. */
static const kpr_screen_t k_supply = { kp, kkey, open_row };

/*
 * The set points take the caps narrowed by the operator's, the caps the
 * supply's own, the start values the set points' range, the trips and
 * the trip time the schema's.
 */
TEST_CASE(every_supply_keypad_takes_ok_and_refuses_and_cancels)
{
    const kpr_row_t rows[ROW_COUNT] = {
        { "set V", TR(SUP_VOLTAGE), "V", 4.0f, 20.0f, 2, 12.34f },
        { "set I", TR(SUP_CURRENT_LIMIT), "A", 1.0f, 4.5f, 2, 2.35f },
        { "V max", TR(SUP_ROW_V_MAX), "V", 4.0f, 20.0f, 2, 15.0f },
        { "I max", TR(SUP_ROW_I_MAX), "A", 1.0f, 4.5f, 2, 3.0f },
        { "V start", TR(SUP_ROW_V_START), "V", 4.0f, 20.0f, 2, 9.0f },
        { "I start", TR(SUP_ROW_I_START), "A", 1.0f, 4.5f, 2, 1.5f },
        { "trip I", TR(SUP_ROW_TRIP_I), "A", 0.0f, 5.0f, 2, 2.5f },
        { "trip V", TR(SUP_ROW_TRIP_V), "V", 0.0f, 21.0f, 2, 18.0f },
        { "trip ms", TR(SUP_ROW_TRIP_MS), "ms", 0.0f, 5000.0f, 0, 250.0f },
    };
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
 * seven SETTINGS rows, each once.
 */
TEST_CASE(every_supply_number_has_a_keypad_row)
{
    static const setting_id_t k_ids[] = {
        SET_SUPPLY_V_MAX, SET_SUPPLY_I_MAX, SET_SUPPLY_V_START,
        SET_SUPPLY_I_START, SET_SUPPLY_TRIP_I, SET_SUPPLY_TRIP_V,
        SET_SUPPLY_TRIP_MS,
    };
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

int main(void)
{
    RUN(every_supply_keypad_takes_ok_and_refuses_and_cancels);
    RUN(every_supply_number_has_a_keypad_row);
    return test_summary("keypad_rows_supply");
}
