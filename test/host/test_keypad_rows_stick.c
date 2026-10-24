/**
 * @file test_keypad_rows_stick.c
 * @brief Every row of ESC STICK's TIMING page held to keypad_rows_rig.h's
 *        four rules: a tap on its value opens the keypad with the row's
 *        label, unit, range and its step's digits; a value past the range
 *        is refused and the row keeps its value; CANCEL keeps it; OK sets
 *        the value typed.  The page's rows are every number of the
 *        schema's stick category.
 *
 * SPDX-License-Identifier: MIT
 */

#include "esc_stick_panel_rig.h"

#include "keypad_rows_rig.h"

/** @brief TIMING's rows a page shows. */
#define ROWS_SHOWN 6
/** @brief The rows TIMING has: the stick category's 14 numbers. */
#define ROW_COUNT 14

/** @brief The keypad ESC STICK holds. */
static const ui_keypad_t *kp(void) { return esc_stick_panel_keypad(); }

/**
 * @brief A key of TIMING's keypad tapped, in the screen's coordinates.
 * @param k The key.
 */
static void kkey(ui_key_t k)
{
    const gfx_rect_t r = ui_keypad_key_rect(esc_stick_panel_keypad(), k);
    tap(r.x + r.w / 2, r.y + r.h / 2);
}

/**
 * @brief The keypad of TIMING's row @p row: the page at its top, then
 *        scrolled down a row a tap of "v" until @p row is the last row
 *        shown, where it lies below the first page.
 * @param row The row, 0 to 13.
 */
static void open_row(int row)
{
    for (int i = 0; i < ROW_COUNT; ++i) {
        tap(PAGE_UP_X, PAGE_CY);             /* to the top */
    }
    const int down = (row >= ROWS_SHOWN) ? row - ROWS_SHOWN + 1 : 0;
    for (int i = 0; i < down; ++i) {
        tap(PAGE_DN_X, PAGE_CY);
    }
    tap(ST_VALUE_X, STEP_CY(row - down));
}

/** @brief TIMING's keypad as keypad_rows_rig.h reaches it. */
static const kpr_screen_t k_timing = { kp, kkey, open_row };

/** @brief TIMING on the HobbyWing profile, at the defaults. */
static void at_timing_page(void)
{
    fresh();
    descend_to_hobbywing();
    tap(TIMING_X, TIMING_Y);
}

/*
 * The rows are the stick category's numbers, each once; every one opens
 * its keypad with its label, unit, range and digits, refuses a value past
 * the range, keeps the row on CANCEL and sets the value OK hands it.
 */
TEST_CASE(every_timing_row_takes_ok_and_refuses_and_cancels)
{
    setting_id_t ids[ROW_COUNT];
    at_timing_page();
    for (int r = 0; r < ROW_COUNT; ++r) {
        open_row(r);
        CHECK(kp()->open);
        ids[r] = (setting_id_t)esc_stick_panel_keypad_setting();
        kkey(UI_KEY_CANCEL);
    }
    /* Every number of the category is a row, once. */
    setting_id_t cat[64];
    const int n = settings_in_category(SET_CAT_STICK, cat, 64);
    int numbers = 0;
    for (int i = 0; i < n; ++i) {
        const setting_type_t t = settings_def(cat[i])->type;
        if (t != SET_TYPE_INT && t != SET_TYPE_FLOAT) {
            continue;
        }
        ++numbers;
        int seen = 0;
        for (int r = 0; r < ROW_COUNT; ++r) {
            seen += (ids[r] == cat[i]) ? 1 : 0;
        }
        CHECK_EQ(seen, 1);
    }
    CHECK_EQ(numbers, ROW_COUNT);

    for (int r = 0; r < ROW_COUNT; ++r) {
        if ((int)ids[r] < 0) {
            T_FAIL("row %d opened no keypad", r);
            continue;
        }
        at_timing_page();
        const kpr_row_t e = kpr_setting(ids[r], NULL);
        kpr_row(&k_timing, r, &e);
        CHECK_NEAR(settings_get(ids[r]), e.ok, 1e-4f);
    }
}

int main(void)
{
    RUN(every_timing_row_takes_ok_and_refuses_and_cancels);
    return test_summary("keypad_rows_stick");
}
