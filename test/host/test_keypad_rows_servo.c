/**
 * @file test_keypad_rows_servo.c
 * @brief SERVO's keypads held to keypad_rows_rig.h's four rules: the
 *        OUTPUT page's pulse widths, TRIM, TRAVEL and a CUSTOM frame rate,
 *        the SWEEP page's numbers, the AUTOMATIC TEST's numbers and the
 *        SET line's two set points.  A tap opens the keypad with the row's
 *        title, unit, range and digits; a value past the range is refused
 *        and the row keeps its value; CANCEL keeps it; OK sets the value
 *        typed.
 *
 * STANDARD PWM at its defaults: 1000, 1500 and 2000us at 50Hz.  The
 * pulse rows' ranges follow from those (servo_settings_panel.c): an end
 * at least 50us from the centre and the command's range inside 400 to
 * 2500us; the CUSTOM rate up to 1,000,000 / (2000 + 1000)us = 333Hz.
 *
 * SPDX-License-Identifier: MIT
 */
#include "servo_rig.h"

#include "keypad_rows_rig.h"
#include "ui_text.h"

/** @brief The caps the supply reports: 4.00 to 8.40V, 0.50 to 3.00A. */
static const supply_caps_t k_caps = { 4.0f, 8.4f, 0.02f, 0.5f, 3.0f, 0.05f };

/** @brief Where a row's keypad is opened from. */
typedef enum {
    AT_OUTPUT = 0,  /**< SETTINGS > OUTPUT          */
    AT_RATE,        /**< OUTPUT's FRAME RATE > CUSTOM */
    AT_SWEEP,       /**< SETTINGS > SWEEP           */
    AT_AUTO,        /**< TEST > AUTOMATIC TEST's SETTINGS */
    AT_SET,         /**< LIVE's SET line            */
} at_t;

/** @brief A row: where, and the tap on its value. */
typedef struct {
    at_t at;
    int  x, y;
} where_t;

/** @brief The middle of TRIM's value, between its two steps. */
#define TRIM_X ((TRIM_DN_X + TRIM_UP_X) / 2)
/** @brief CUSTOM, the list's entry after STANDARD PWM's eight rates. */
#define CUSTOM 8

/** @brief The rows, in the order of the table in the case. */
static const where_t k_where[] = {
    { AT_OUTPUT, ROW_L_X, ROW_Y(2) },      /* PULSE MIN     */
    { AT_OUTPUT, ROW_R_X, ROW_Y(2) },      /* PULSE CENTRE  */
    { AT_OUTPUT, ROW_L_X, ROW_Y(3) },      /* PULSE MAX     */
    { AT_OUTPUT, TRIM_X,  ROW_Y(3) },      /* TRIM          */
    { AT_OUTPUT, ROW_L_X, ROW_Y(4) },      /* TRAVEL        */
    { AT_RATE,   CH_X(CUSTOM), CH_Y(CUSTOM) },  /* FRAME RATE */
    { AT_SWEEP,  ROW_L_X, ROW_Y(1) },      /* SPEED         */
    { AT_SWEEP,  ROW_L_X, ROW_Y(2) },      /* RANGE         */
    { AT_AUTO,   LROW_X(2), LROW_Y(1) },   /* LENGTH        */
    { AT_AUTO,   LROW_X(2), LROW_Y(2) },   /* MOVES         */
    { AT_AUTO,   LROW_X(2), LROW_Y(3) },   /* DWELL         */
    { AT_AUTO,   LROW_X(2), LROW_Y(4) },   /* SETTLE        */
    { AT_AUTO,   LROW_X(1), LROW_Y(0) },   /* STALL AT      */
    { AT_AUTO,   LROW_X(1), LROW_Y(1) },   /* IDLE CURRENT  */
    { AT_AUTO,   LROW_X(1), LROW_Y(2) },   /* HOLD CURRENT  */
    { AT_AUTO,   LROW_X(1), LROW_Y(3) },   /* TRAVEL TIME   */
    { AT_SET,    SUP_V_X, SUP_ROW_Y },     /* the voltage   */
    { AT_SET,    SUP_I_X, SUP_ROW_Y },     /* the current   */
};
#define ROW_COUNT ((int)(sizeof(k_where) / sizeof(k_where[0])))

/** @brief The settings the rows of the AUTOMATIC TEST and SWEEP set, in
 *         the order of k_where from SPEED on. */
static const setting_id_t k_ids[] = {
    SET_SERVO_TEST_HZ, SET_SERVO_TEST_RANGE, SET_SERVO_LEN_S,
    SET_SERVO_LEN_MOVES, SET_SERVO_DWELL_MS, SET_SERVO_SETTLE_MS,
    SET_SERVO_STALL_A, SET_SERVO_IDLE_MAX, SET_SERVO_HOLD_MAX,
    SET_SERVO_TRAVEL_MAX_MS,
};
#define FIRST_SETTING 6

/** @brief The keypad SERVO holds. */
static const ui_keypad_t *kp(void) { return servo_screen_keypad(); }

/**
 * @brief A key of SERVO's keypad tapped.
 * @param k The key.
 */
static void kkey(ui_key_t k)
{
    const gfx_rect_t r = ui_keypad_key_rect(servo_screen_keypad(), k);
    tap(r.x + r.w / 2, r.y + r.h / 2);
}

/**
 * @brief The keypad of row @p row, from its page.
 * @param row The row, an index of k_where.
 */
static void open_row(int row)
{
    const where_t *w = &k_where[row];
    switch (w->at) {
    case AT_OUTPUT:
        open_settings();
        tap(TAB_X(PAGE_OUTPUT), TAB_Y);
        break;
    case AT_RATE:
        open_settings();
        tap(TAB_X(PAGE_OUTPUT), TAB_Y);
        tap(ROW_L_X, ROW_Y(1));            /* FRAME RATE: the list */
        break;
    case AT_SWEEP:
        open_settings();
        tap(TAB_X(PAGE_SWEEP), TAB_Y);
        break;
    case AT_AUTO:
        if (servo_screen_run_settings() != 1) {
            open_run_settings(0);
        }
        break;
    case AT_SET:
        close_settings();
        break;
    }
    tap(w->x, w->y);
}

/** @brief SERVO's keypad as keypad_rows_rig.h reaches it. */
static const kpr_screen_t k_servo = { kp, kkey, open_row };

/*
 * Every keypad on SERVO opens with its title, unit, range and digits,
 * refuses a value past the range, keeps the row on CANCEL and sets the
 * value OK hands it.  A set point stays at or under 6.00V, a standard
 * servo's rating, where the HV warning does not stand in for OK.
 */
TEST_CASE(every_servo_keypad_takes_ok_and_refuses_and_cancels)
{
    kpr_row_t rows[ROW_COUNT] = {
        { "min", TR(SV_ROW_MIN), "us", 500.0f, 1450.0f, 0, 1200.0f },
        { "centre", TR(SV_ROW_CENTRE), "us", 1200.0f, 1750.0f, 0, 1550.0f },
        { "max", TR(SV_ROW_MAX), "us", 1550.0f, 2500.0f, 0, 2200.0f },
        { "trim", TR(SV_ROW_TRIM), "us", -200.0f, 200.0f, 0, 35.0f },
        { "travel", TR(SV_ROW_TRAVEL), "deg", 10.0f, 90.0f, 0, 60.0f },
        { "rate", TR(SV_ROW_RATE), "Hz", 50.0f, 333.0f, 0, 60.0f },
        { 0 }, { 0 }, { 0 }, { 0 }, { 0 }, { 0 }, { 0 }, { 0 }, { 0 },
        { 0 },
        { "set V", TR(SUP_VOLTAGE), "V", 4.0f, 8.4f, 2, 5.5f },
        { "set I", TR(SUP_CURRENT_LIMIT), "A", 0.5f, 3.0f, 2, 1.25f },
    };
    static const ui_text_id_t k_labels[] = {
        TX_SV_ROW_SPEED, TX_SV_ROW_RANGE, TX_SV_ROW_LEN_S, TX_SV_ROW_MOVES,
        TX_SV_ROW_DWELL, TX_SV_ROW_SETTLE, TX_SV_ROW_STALL, TX_SV_ROW_IDLE,
        TX_SV_ROW_HOLD, TX_SV_ROW_TRAVEL_TIME,
    };
    for (int r = 0; r < ROW_COUNT; ++r) {
        fresh();
        supply_screen_set_caps(&k_caps);
        const int i = r - FIRST_SETTING;
        if (i >= 0 && i < (int)(sizeof(k_ids) / sizeof(k_ids[0]))) {
            rows[r] = kpr_setting(k_ids[i], ui_tr(k_labels[i]));
        }
        kpr_row(&k_servo, r, &rows[r]);
    }
    /* What OK set reached the settings and the supply. */
    fresh();
    supply_screen_set_caps(&k_caps);
    open_row(FIRST_SETTING);
    kpr_type(&k_servo, "1.25");
    kkey(UI_KEY_OK);
    CHECK_NEAR(settings_get(SET_SERVO_TEST_HZ), 1.25f, 1e-4f);
    open_row(ROW_COUNT - 2);
    kpr_type(&k_servo, "5.5");
    kkey(UI_KEY_OK);
    CHECK_NEAR(supply_screen_set_v(), 5.5f, 1e-4f);
    open_row(5);
    kpr_type(&k_servo, "60");
    kkey(UI_KEY_OK);
    CHECK_EQ(servo_screen_frame_hz(), 60);
}

/*
 * Every number of the schema's servo category has a keypad: on SERVO's
 * pages above, or among the programmer's options (voltage, current, PWM
 * wait: test_keypad_rows_kst).
 */
TEST_CASE(every_servo_number_has_a_keypad_row)
{
    static const setting_id_t k_prog[] = {
        SET_SERVO_PROG_V, SET_SERVO_PROG_I, SET_SERVO_PROG_WAIT,
    };
    setting_id_t cat[64];
    const int n = settings_in_category(SET_CAT_SERVO, cat, 64);
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
        for (size_t k = 0; k < sizeof(k_prog) / sizeof(k_prog[0]); ++k) {
            seen += (k_prog[k] == cat[i]) ? 1 : 0;
        }
        CHECK_EQ(seen, 1);
    }
    CHECK_EQ(numbers, 13);
}

int main(void)
{
    RUN(every_servo_keypad_takes_ok_and_refuses_and_cancels);
    RUN(every_servo_number_has_a_keypad_row);
    return test_summary("keypad_rows_servo");
}
