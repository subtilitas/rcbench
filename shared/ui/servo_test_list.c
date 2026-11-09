/**
 * @file
 * @brief TEST's SETTINGS list (ui_setlist.h): the automatic test's 21
 *        rows under six headings, opened by the footer's SETTINGS.
 *
 *   - VOLTAGE STEPS, its line the steps a run takes or why none: STEP
 *     4.8V, STEP 6.0V, HV SERVO, STEP 7.4V and STEP 8.4V.  7.4V and 8.4V
 *     take a press with HV SERVO on only;
 *   - BROWN-OUT, its line the walk a run takes on this supply: BROWN-OUT,
 *     its lowest voltage and its step;
 *   - PASS/FAIL LIMITS, its line a STALL AT no reading can pass: STALL AT,
 *     the idle and holding current, the travel time;
 *   - LENGTH AND TIMING: LENGTH BY, TEST TIME (by time only), MOVEMENTS
 *     (by moves only), DWELL TIME and SETTLING TIME;
 *   - RANGE, its line the run's ends in us, in the warning colour while
 *     they are not either side of PULSE CENTER: RANGE, the share of the
 *     travel a run covers, and OUTPUT, the profile in force, whose tap
 *     opens the profile (svs_profile_open()) on PULSE MIN;
 *   - DUT: the device under test's NAME and REPORT.
 *
 * RANGE and DWELL TIME are SWEEP's too, NAME and REPORT the hub's: one
 * setting each, whichever list sets it.  The list writes nothing to the
 * card on a change: SAVE writes, and the leave warning's DISCARD puts the
 * stored values back through settings_apply_all().  HV SERVO is the
 * profile's session value, as on the hub's list, and RESET TEST puts it
 * off.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>

#include "settings.h"
#include "supply_screen.h"
#include "ui_setlist.h"
#include "ui_text.h"

/* Last: the layout macros of the headers it includes have short names
 * (W, H, PAD), which no header included after it may meet. */
#include "servo_screen_core_int.h"

/* --------------------------------------------------------- the headings */

/**
 * @brief VOLTAGE STEPS' line: the steps a run takes, `4.8V, 6.0V,
 *        BROWN-OUT`, or why none.
 * @param out takes it
 * @param n its size
 */
static void steps_line(char *out, size_t n)
{
    float v[SERVO_TEST_STEPS_MAX];
    const uint8_t k = test_steps(v);
    const bool bo = settings_get_bool(SET_SERVO_BROWNOUT);
    if (k == 0u && !bo) {
        snprintf(out, n, "%s", ui_servo_str(SERVO_STR_START_NO_STEPS));
        return;
    }
    servo_screen_plan_text(out, n, v, k, bo);
}

/** @brief Whether the steps refuse a run: none chosen, or one outside
 *         SUPPLY's caps.  @return non-NULL then, for the warning colour */
static const char *steps_warn(void)
{
    float v[SERVO_TEST_STEPS_MAX];
    const uint8_t k = test_steps(v);
    if (k == 0u && !settings_get_bool(SET_SERVO_BROWNOUT)) {
        return "";
    }
    const supply_caps_t caps = supply_screen_caps();
    for (uint8_t i = 0; i < k; ++i) {
        if (v[i] > caps.v_max + 0.001f || v[i] < caps.v_min - 0.001f) {
            return "";
        }
    }
    return NULL;
}

/**
 * @brief BROWN-OUT's line: the walk a run started now takes with every
 *        voltage moving, its start, step, floor and count on this supply.
 * @param out takes it
 * @param n its size
 */
static void walk_line(char *out, size_t n)
{
    const supply_caps_t caps = supply_screen_caps();
    float start = 0.0f, floor_v = 0.0f, step = 0.0f;
    servo_test_brownout_walk(settings_get(SET_SERVO_BO_FLOOR),
                             settings_get(SET_SERVO_BO_STEP), caps.v_min,
                             caps.v_max, &start, &floor_v, &step);
    snprintf(out, n, TR(SV_BO_NOTE_WALK), (double)start, (double)step,
             (double)floor_v, servo_test_brownout_count(start, floor_v, step));
}

/**
 * @brief PASS/FAIL LIMITS' line: a STALL AT the run's readings cannot
 *        pass, or nothing.
 * @param out takes it
 * @param n its size
 */
static void stall_line(char *out, size_t n)
{
    servo_test_cfg_t tc;
    test_cfg(&tc);
    if (!servo_test_stall_note(&tc, out, n)) {
        out[0] = '\0';
    }
}

/** @brief Whether the stall line warns.  @return non-NULL then */
static const char *stall_warn(void)
{
    char note[96];
    stall_line(note, sizeof(note));
    return (note[0] != '\0') ? "" : NULL;
}

/** @brief VOLTAGE STEPS' heading line. */
static const ui_setlist_custom_t k_steps_line = {
    .label = TX_TST_H_STEPS, .help = TX_COUNT, .value_text = steps_line,
    .locked = steps_warn,
};
/** @brief BROWN-OUT's heading line. */
static const ui_setlist_custom_t k_walk_line = {
    .label = TX_SV_ROW_BROWNOUT, .help = TX_COUNT, .value_text = walk_line,
};
/** @brief PASS/FAIL LIMITS' heading line. */
static const ui_setlist_custom_t k_stall_line = {
    .label = TX_TST_H_LIMITS, .help = TX_COUNT, .value_text = stall_line,
    .locked = stall_warn,
};

/* ------------------------------------------------------------- HV SERVO */

/** @brief HV SERVO's value: the profile's, ON or OFF. */
static void hv_value(char *out, size_t n) { svs_profile_value(R_HV, out, n); }

/** @brief HV SERVO stepped: the other state, for a run started after it. */
static void hv_step(int dir) { svs_profile_step(R_HV, dir); }

/** @brief The HV SERVO switch, as on the hub's list. */
static const ui_setlist_custom_t k_hv = {
    .label = TX_SV_ROW_HV, .help = TX_SVL_HV_HELP, .cycles = true,
    .value_text = hv_value, .step = hv_step,
};

/* ------------------------------------------------------- RANGE, OUTPUT */

/**
 * @brief RANGE's line: the ends a run started now takes, `1100-1900us`.
 * @param out takes it
 * @param n its size
 */
static void ends_line(char *out, size_t n)
{
    uint16_t lo = 0u, hi = 0u;
    test_ends(&lo, &hi);
    snprintf(out, n, "%u-%uus", (unsigned)lo, (unsigned)hi);
}

/** @brief Whether the ends refuse a run: not either side of PULSE CENTER.
 *  @return non-NULL then, for the warning colour */
static const char *ends_warn(void)
{
    uint16_t lo = 0u, hi = 0u;
    test_ends(&lo, &hi);
    return (lo < s.center_us && s.center_us < hi) ? NULL : "";
}

/** @brief RANGE's heading line. */
static const ui_setlist_custom_t k_ends_line = {
    .label = TX_RDY_RANGE, .help = TX_COUNT, .value_text = ends_line,
    .locked = ends_warn,
};

/** @brief A tap on OUTPUT: the profile, on PULSE MIN. */
static void output_tap(void) { svs_profile_open(SVS_PROF_MIN); }

/** @brief The OUTPUT row: the profile in force, as the type pill reads
 *         it; a tap opens the profile. */
static const ui_setlist_custom_t k_output = {
    .label = TX_SV_PG_OUTPUT, .help = TX_SVL_OUTPUT_HELP,
    .value_text = svs_tag_text, .tap = output_tap,
};

/* ------------------------------------------------------------- the rows */

const ui_setlist_row_t svs_test_rows[SVS_TEST_ROWS] = {
    UI_SL_ROW_HEADING(TX_TST_H_STEPS, &k_steps_line),
    UI_SL_ROW_SETTING(SET_SERVO_STEP_48),
    UI_SL_ROW_SETTING(SET_SERVO_STEP_60),
    UI_SL_ROW_CUSTOM(&k_hv),
    UI_SL_ROW_SETTING(SET_SERVO_STEP_74),
    UI_SL_ROW_SETTING(SET_SERVO_STEP_84),
    UI_SL_ROW_HEADING(TX_SV_ROW_BROWNOUT, &k_walk_line),
    UI_SL_ROW_SETTING(SET_SERVO_BROWNOUT),
    UI_SL_ROW_SETTING(SET_SERVO_BO_FLOOR),
    UI_SL_ROW_SETTING(SET_SERVO_BO_STEP),
    UI_SL_ROW_HEADING(TX_TST_H_LIMITS, &k_stall_line),
    UI_SL_ROW_SETTING(SET_SERVO_STALL_A),
    UI_SL_ROW_SETTING(SET_SERVO_IDLE_MAX),
    UI_SL_ROW_SETTING(SET_SERVO_HOLD_MAX),
    UI_SL_ROW_SETTING(SET_SERVO_TRAVEL_MAX_MS),
    UI_SL_ROW_HEADING(TX_TST_H_LENGTH, NULL),
    UI_SL_ROW_SETTING(SET_SERVO_LEN_BY),
    UI_SL_ROW_SETTING(SET_SERVO_LEN_S),
    UI_SL_ROW_SETTING(SET_SERVO_LEN_MOVES),
    UI_SL_ROW_SETTING(SET_SERVO_DWELL_MS),
    UI_SL_ROW_SETTING(SET_SERVO_SETTLE_MS),
    UI_SL_ROW_HEADING(TX_RDY_RANGE, &k_ends_line),
    UI_SL_ROW_SETTING(SET_SERVO_TEST_RANGE),
    UI_SL_ROW_CUSTOM(&k_output),
    UI_SL_ROW_HEADING(TX_SV_PG_DUT, NULL),
    { (uint8_t)UI_SL_TEXT, (int8_t)SET_TEXT_DUT_NAME, (setting_id_t)0,
      TX_COUNT, NULL },
    UI_SL_ROW_SETTING(SET_SERVO_REPORT),
};

const char *svs_test_locked(setting_id_t id)
{
    const int by = settings_get_int(SET_SERVO_LEN_BY);
    if ((id == SET_SERVO_STEP_74 || id == SET_SERVO_STEP_84) && !s.test_hv) {
        return TR(SVL_HV_ONLY);
    }
    if (id == SET_SERVO_LEN_S && by != 0) {
        return TR(SVL_BY_TIME);
    }
    if (id == SET_SERVO_LEN_MOVES && by == 0) {
        return TR(SVL_BY_MOVES);
    }
    return NULL;
}
