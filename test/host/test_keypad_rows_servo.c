/**
 * @file test_keypad_rows_servo.c
 * @brief SERVO's keypads held to keypad_rows_rig.h's four rules: the
 *        hub list's pulse widths, TRIM, TRAVEL and a CUSTOM frame rate,
 *        RUDDER's LENGTH and RUDDER AUTO's box.  The supply's set points
 *        are SUPPLY's keypads (test_keypad_rows_supply).  SWEEP's and
 *        TEST's numbers are rows of their lists, whose keypad is
 *        ui_setlist.h's (test_servo_test_list).  A tap opens the keypad
 *        with the row's title, unit, range and digits; a value past the
 *        range is refused and the row keeps its value; CANCEL keeps it; OK
 *        sets the value typed.
 *
 * STANDARD PWM at its defaults: 1000, 1500 and 2000us at 50Hz.  The
 * pulse rows' ranges follow from those (servo_settings_panel.c): an end
 * at least 50us from the center and the command's range inside 400 to
 * 2500us; the CUSTOM rate up to 1,000,000 / (2000 + 1000)us = 333Hz.
 *
 * SPDX-License-Identifier: MIT
 */
#include "servo_rig.h"

#include "keypad_rows_rig.h"
#include "servo_rudder_auto_int.h"
#include "ui_text.h"

/** @brief Where a row's keypad is opened from. */
typedef enum {
    AT_OUTPUT = 0,  /**< the hub's list; y is the row */
    AT_RATE,        /**< its FRAME RATE > CUSTOM    */
    AT_RUDDER,      /**< RUDDER's LENGTH row        */
    AT_RAUTO,       /**< RUDDER AUTO's box          */
} at_t;

/** @brief A row: where, and the tap on its value. */
typedef struct {
    at_t at;
    int  x, y;
} where_t;

/** @brief CUSTOM, the list's entry after STANDARD PWM's eight rates. */
#define CUSTOM 8

/** @brief The rows, in the order of the table in the case. */
static const where_t k_where[] = {
    { AT_OUTPUT, 0, HL_MIN },              /* PULSE MIN     */
    { AT_OUTPUT, 0, HL_CENTER },           /* PULSE CENTER  */
    { AT_OUTPUT, 0, HL_MAX },              /* PULSE MAX     */
    { AT_OUTPUT, 0, HL_TRIM },             /* TRIM          */
    { AT_OUTPUT, 0, HL_TRAVEL },           /* TRAVEL        */
    { AT_RATE,   CH_X(CUSTOM), CH_Y(CUSTOM) },  /* FRAME RATE */
    { AT_RUDDER, SV_RUD_LEN_X, SV_RUD_LEN_Y }, /* LENGTH     */
    { AT_RAUTO,  SV_RA_L_X, SV_RA_ROW_Y(0) },  /* TARGET 1000us */
    { AT_RAUTO,  SV_RA_R_X, SV_RA_ROW_Y(0) },  /* TARGET 2000us */
    { AT_RAUTO,  SV_RA_L_X, SV_RA_ROW_Y(1) },  /* MARGIN     */
    { AT_RAUTO,  SV_RA_R_X, SV_RA_ROW_Y(1) },  /* TOLERANCE  */
};
#define ROW_COUNT ((int)(sizeof(k_where) / sizeof(k_where[0])))

/** @brief The settings the rows of RUDDER and RUDDER AUTO set, in the
 *         order of k_where from LENGTH on. */
static const setting_id_t k_ids[] = {
    SET_RUDDER_LEN_MM, SET_RUDDER_DEFL_LO, SET_RUDDER_DEFL_HI,
    SET_RUDDER_MARGIN, SET_RUDDER_TOL,
};
#define FIRST_SETTING 6
_Static_assert(FIRST_SETTING + (int)(sizeof(k_ids) / sizeof(k_ids[0]))
                   == (int)(sizeof(k_where) / sizeof(k_where[0])),
               "the rows from LENGTH on are k_ids's settings");

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
        sl_tap(w->y);
        return;
    case AT_RATE:
        open_settings();
        sl_tap(HL_RATE);                   /* FRAME RATE: the list */
        break;
    case AT_RUDDER:
        go(SERVO_FN_RUDDER);
        break;
    case AT_RAUTO:
        go(SERVO_FN_RAUTO);
        break;
    }
    tap(w->x, w->y);
}

/** @brief SERVO's keypad as keypad_rows_rig.h reaches it. */
static const kpr_screen_t k_servo = { kp, kkey, open_row };

/*
 * Every keypad on SERVO opens with its title, unit, range and digits,
 * refuses a value past the range, keeps the row on CANCEL and sets the
 * value OK hands it.
 */
TEST_CASE(every_servo_keypad_takes_ok_and_refuses_and_cancels)
{
    kpr_row_t rows[ROW_COUNT] = {
        { "min", TR(SV_ROW_MIN), "us", 500.0f, 1450.0f, 0, 1200.0f },
        { "center", TR(SV_ROW_CENTER), "us", 1200.0f, 1750.0f, 0, 1550.0f },
        { "max", TR(SV_ROW_MAX), "us", 1550.0f, 2500.0f, 0, 2200.0f },
        { "trim", TR(SV_ROW_TRIM), "us", -200.0f, 200.0f, 0, 35.0f },
        { "travel", TR(SV_ROW_TRAVEL), "deg", 10.0f, 90.0f, 0, 60.0f },
        { "rate", TR(SV_ROW_RATE), "Hz", 50.0f, 333.0f, 0, 60.0f },
        { 0 }, { 0 }, { 0 }, { 0 }, { 0 },
    };
    static const ui_text_id_t k_labels[] = { TX_RUD_LENGTH };
    const int n_lbl = (int)(sizeof(k_labels) / sizeof(k_labels[0]));
    for (int r = 0; r < ROW_COUNT; ++r) {
        fresh();
        /* From LENGTH on, the last rows of k_where, each a setting of
         * k_ids; RUDDER AUTO's rows are titled with the setting's own
         * label. */
        const int i = r - FIRST_SETTING;
        if (i >= 0) {
            rows[r] = kpr_setting(k_ids[i], i < n_lbl
                                  ? ui_tr(k_labels[i])
                                  : ui_setting_label(k_ids[i]));
        }
        kpr_row(&k_servo, r, &rows[r]);
    }
    /* What OK set reached the settings. */
    fresh();
    open_row(FIRST_SETTING);
    kpr_type(&k_servo, "125");
    kkey(UI_KEY_OK);
    CHECK_NEAR(settings_get(SET_RUDDER_LEN_MM), 125.0f, 1e-4f);
    open_row(5);
    kpr_type(&k_servo, "60");
    kkey(UI_KEY_OK);
    CHECK_EQ(servo_screen_frame_hz(), 60);
}

/*
 * Every number of the schema's servo category has a keypad: on SERVO's
 * pages above, on TEST's list (ui_setlist.h's keypad), or among the
 * programmer's options (voltage, current, PWM wait: test_keypad_rows_kst,
 * and LIMIT RUN's page).
 */
TEST_CASE(every_servo_number_has_a_keypad_row)
{
    static const setting_id_t k_prog[] = {
        SET_SERVO_PROG_V, SET_SERVO_PROG_I, SET_SERVO_PROG_WAIT,
        SET_SERVO_TEST_HZ, SET_SERVO_TEST_RANGE,
        /* TEST's list. */
        SET_SERVO_LEN_S, SET_SERVO_LEN_MOVES, SET_SERVO_DWELL_MS,
        SET_SERVO_SETTLE_MS, SET_SERVO_STALL_A, SET_SERVO_IDLE_MAX,
        SET_SERVO_HOLD_MAX, SET_SERVO_TRAVEL_MAX_MS, SET_SERVO_BO_FLOOR,
        SET_SERVO_BO_STEP,
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
    CHECK_EQ(numbers, 20);
}

int main(void)
{
    RUN(every_servo_keypad_takes_ok_and_refuses_and_cancels);
    RUN(every_servo_number_has_a_keypad_row);
    return test_summary("keypad_rows_servo");
}
