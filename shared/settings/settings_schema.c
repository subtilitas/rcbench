/**
 * @file
 * @brief The schema: one row per setting, its key, unit, category, type,
 *        range, step and default.
 *
 * settings.c holds the model that reads it; settings_sensors.c the SENSORS
 * rows that stand in for some of these (settings_def()).
 *
 * SPDX-License-Identifier: MIT
 */

#include "settings_int.h"

#include "settings_opts.h"

/** @brief An enum row's options: list @p name of ui_text.def
 *  (settings_opts.h). */
#define ENUM_OPTS(name)                                                    \
    .options = settings_opts_##name,                                       \
    .option_count = (uint8_t)settings_opts_n_##name

/*
 * One row per setting.  Ranges are the physical ones: a pack cannot have zero
 * cells, a servo pulse below 800us is not a pulse, and an ESC (electronic
 * speed controller) that ramps faster than 300%/s is not ramping.
 */
const setting_def_t settings_schema[SETTING_COUNT] = {
    [SET_PACK_CELLS] = {
        "pack_cells", "S",
        SET_CAT_ESC, SET_TYPE_INT, 1, 14, 1, 6, NULL, 0 },
    [SET_PACK_MAH] = {
        "pack_mah", "mAh",
        SET_CAT_ESC, SET_TYPE_INT, 0, 30000, 100, 5000, NULL, 0 },
    [SET_PACK_MOHM] = {
        "pack_mohm", "mΩ",
        SET_CAT_ESC, SET_TYPE_FLOAT, 0, 200, 1, 18, NULL, 0 },
    [SET_MOTOR_POLES] = {
        "motor_poles", "",
        SET_CAT_ESC, SET_TYPE_INT, 2, 42, 2, 14, NULL, 0 },
    [SET_MOTOR_KV] = {
        "motor_kv", "rpm/V",
        SET_CAT_ESC, SET_TYPE_INT, 0, 10000, 10, 0, NULL, 0 },
    [SET_OUT_MIN_US] = {
        "out_min_us", "us",
        SET_CAT_ESC, SET_TYPE_INT, 800, 1600, 10, 1000, NULL, 0 },
    [SET_OUT_MAX_US] = {
        "out_max_us", "us",
        SET_CAT_ESC, SET_TYPE_INT, 1400, 2400, 10, 2000, NULL, 0 },
    [SET_OUT_RAMP] = {
        "out_ramp", "%/s",
        SET_CAT_ESC, SET_TYPE_INT, 5, 300, 5, 55, NULL, 0 },

    [SET_THEME] = {
        "theme", "",
        SET_CAT_APP, SET_TYPE_ENUM, 0, 0, 1, 0, ENUM_OPTS(theme) },
    [SET_BRIGHTNESS] = {
        "brightness", "%",
        SET_CAT_APP, SET_TYPE_INT, 20, 100, 5, 100, NULL, 0 },
    [SET_CONTRAST] = {
        "contrast", "%",
        SET_CAT_APP, SET_TYPE_INT, 60, 140, 5, 100, NULL, 0 },
    /* The panel's scan and the glass's map turned by 180 degrees: 0
     * NORMAL, 1 turned (display.h, touch.h). */
    [SET_SCREEN_ROT] = {
        "screen_rot", "",
        SET_CAT_APP, SET_TYPE_ENUM, 0, 0, 1, 0, ENUM_OPTS(screen_rot) },
    [SET_LANGUAGE] = {
        "language", "",
        SET_CAT_APP, SET_TYPE_ENUM, 0, 0, 1, 0, ENUM_OPTS(language) },
    [SET_UNITS] = {
        "units", "",
        SET_CAT_APP, SET_TYPE_ENUM, 0, 0, 1, 0, ENUM_OPTS(units) },
    /* The zone file dates are written in: an index into wall_clock.c's
     * POSIX TZ strings, 2 Berlin (wall_zone_posix()). */
    [SET_TIME_ZONE] = {
        "time_zone", "",
        SET_CAT_APP, SET_TYPE_ENUM, 0, 0, 1, 2, ENUM_OPTS(time_zone) },
    [SET_DIM_AFTER] = {
        "dim_after", "min",
        SET_CAT_APP, SET_TYPE_INT, 0, 60, 5, 0, NULL, 0 },
    [SET_PLOT_PAN] = {
        "plot_pan1", "",
        SET_CAT_APP, SET_TYPE_BOOL, 0, 1, 1, 0, NULL, 0 },
    [SET_KNOB_EN] = {
        "knob_en", "",
        SET_CAT_APP, SET_TYPE_BOOL, 0, 1, 1, 0, NULL, 0 },
    [SET_KNOB_SCALE] = {
        "knob_scale", "deg",
        SET_CAT_APP, SET_TYPE_INT, 90, 720, 10, 270, NULL, 0 },
    /* How much of the text shows through the text editor's keyboard,
     * ui_textkey_see_pct() of the option: OFF, 25%, 50%. */
    [SET_KB_SEE] = {
        "kb_see", "",
        SET_CAT_APP, SET_TYPE_ENUM, 0, 0, 1, 0, ENUM_OPTS(kb_see) },
    /* Hides the SIMULATION watermark (ui_watermark()); the band's SIM chip
     * and every other statement that values are simulated stay. */
    [SET_DEMO_MODE] = {
        "demo_mode", "",
        SET_CAT_APP, SET_TYPE_BOOL, 0, 1, 1, 0, NULL, 0 },

    /*
     * The current monitors, on the IOmcu's I2C (Inter-Integrated
     * Circuit) bus and written to its SENSE page by the panel (protocol
     * 4.7).  The ranges are the page's: an INA228 shunt of 50 to 20000 uOhm
     * and a range of 1.0 to 300.0A, an INA3221 shunt of 5mOhm to 1Ohm.
     * The defaults are the modules the bench is built with: the MATEK
     * I2C-INA-BM's INA228 at 0x45 on 200 uOhm, ranged for 204.8A, and the
     * bus on GP16 and GP17; the INA3221 parts are in
     * settings_sensors.c.  The bus clock is not a setting: the
     * page takes 400kHz and nothing else.  The address keys are new ones:
     * the INA228's old key held an index into four addresses, which read
     * as one of sixteen would name another address.
     */
    [SET_INA228_EN] = {
        "ina228_en", "",
        SET_CAT_SENSORS, SET_TYPE_BOOL, 0, 1, 1, 0, NULL, 0 },
    [SET_INA228_ADDR] = {
        "ina228_adr", "",
        SET_CAT_SENSORS, SET_TYPE_ENUM, 0, 0, 1, 5, ENUM_OPTS(ina228_addr) },
    [SET_INA228_UOHM] = {
        "ina228_uohm", "uΩ",
        SET_CAT_SENSORS, SET_TYPE_INT, 50, 20000, 1, 200, NULL, 0 },
    [SET_INA228_MAX_A] = {
        "ina228_max_a", "A",
        SET_CAT_SENSORS, SET_TYPE_FLOAT, 1.0f, 300.0f, 0.1f, 204.8f, NULL, 0 },
    /*
     * The bus's pins: IOmcu GPIO, one I2C block's SDA and SCL.
     * Sensor SDA takes the values settings_sense_sda_valid() does and no
     * other: -1, the pins not set, and the SDA pins the IOmcu's page
     * can take.  Sensor SCL is derived: the GPIO after SDA, and -1 with
     * it.  Its key is kept and saved with the value derived, so a panel
     * build that reads both keys reads a pair.
     */
    [SET_SENSE_SDA] = {
        "sns_sda", "GPIO",
        SET_CAT_SENSORS, SET_TYPE_INT, -1, 47, 1, 16, NULL, 0 },
    [SET_SENSE_SCL] = {
        "sns_scl", "GPIO",
        SET_CAT_SENSORS, SET_TYPE_INT, -1, 47, 1, 17, NULL, 0 },
    /*
     * The output encoder (protocol 4.9): an AS5600 magnetic angle sensor on
     * the horn shaft of the servo under test, at 0x36 on the sensor bus
     * above.  AS5600 centre is the sensor's 12-bit count with the servo at
     * its neutral; angles are shown and reported from it.  The SERVO
     * screen's ENC CENTRE row sets it from the live reading.
     */
    [SET_ENC_EN] = {
        "enc_en", "",
        SET_CAT_SENSORS, SET_TYPE_BOOL, 0, 1, 1, 0, NULL, 0 },
    [SET_ENC_CENTRE] = {
        "enc_centre", "",
        SET_CAT_SENSORS, SET_TYPE_INT, 0, 4095, 1, 0, NULL, 0 },
    /*
     * The phase tap, written to the IOmcu's TONE page (protocol
     * 4.8): one GPIO that reads an ESC's beeps off one motor phase.  The
     * ranges and defaults are the page's (LINK_TN_*): the pin GP22 (pad 29),
     * tones from 400 to 6500Hz, a new beep when the pitch moves 8%, a
     * silence of 3ms ending a beep, 3 tone periods making one.  The tap is
     * off until it is enabled here and the pin is wired.
     */
    [SET_TONE_EN] = {
        "tone_en", "",
        SET_CAT_SENSORS, SET_TYPE_BOOL, 0, 1, 1, 0, NULL, 0 },
    [SET_TONE_PIN] = {
        "tone_pin", "GPIO",
        SET_CAT_SENSORS, SET_TYPE_INT, 0, 47, 1, 22, NULL, 0 },
    [SET_TONE_F_MIN] = {
        "tone_fmin", "Hz",
        SET_CAT_SENSORS, SET_TYPE_INT, 50, 2000, 10, 400, NULL, 0 },
    [SET_TONE_F_MAX] = {
        "tone_fmax", "Hz",
        SET_CAT_SENSORS, SET_TYPE_INT, 100, 6900, 50, 6500, NULL, 0 },
    [SET_TONE_SPLIT] = {
        "tone_split", "%",
        SET_CAT_SENSORS, SET_TYPE_INT, 0, 50, 1, 8, NULL, 0 },
    [SET_TONE_GAP] = {
        "tone_gap", "ms",
        SET_CAT_SENSORS, SET_TYPE_INT, 1, 100, 1, 3, NULL, 0 },
    [SET_TONE_PERIODS] = {
        "tone_periods", "",
        SET_CAT_SENSORS, SET_TYPE_INT, 1, 64, 1, 3, NULL, 0 },
    [SET_VIBE_EN] = {
        "vibe_en", "",
        SET_CAT_SENSORS, SET_TYPE_BOOL, 0, 1, 1, 0, NULL, 0 },
    [SET_OPTICAL_EN] = {
        "optical_en", "",
        SET_CAT_SENSORS, SET_TYPE_BOOL, 0, 1, 1, 0, NULL, 0 },
    [SET_OPTICAL_PIN] = {
        "optical_pin", "GPIO",
        SET_CAT_SENSORS, SET_TYPE_INT, -1, 48, 1, -1, NULL, 0 },
    /*
     * The PD mini's UART: the module's DM is its RX and DP its TX, at 3.3V.
     * IOmcu GPIO, written to its SUPPLY page by the panel; the pins
     * are -1 until the module is wired, and the page drives nothing then.
     */
    [SET_PDMINI_EN] = {
        "pdm_en", "",
        SET_CAT_PDMINI, SET_TYPE_BOOL, 0, 1, 1, 0, NULL, 0 },
    [SET_PDMINI_TX] = {
        "pdm_tx", "GPIO",
        SET_CAT_PDMINI, SET_TYPE_INT, -1, 47, 1, -1, NULL, 0 },
    [SET_PDMINI_RX] = {
        "pdm_rx", "GPIO",
        SET_CAT_PDMINI, SET_TYPE_INT, -1, 47, 1, -1, NULL, 0 },
    [SET_PDMINI_BAUD] = {
        "pdm_baud", "",
        SET_CAT_PDMINI, SET_TYPE_ENUM, 0, 0, 1, 7, ENUM_OPTS(pdmini_baud) },
    /*
     * How the IOmcu receives a Hitec D series answer on its programming
     * port (the HITEC page's register 23, link_hitec_rx_t): PULL-UP, the
     * line released with a 2.2kOhm pull-up on the signal wire, or
     * PRECHARGE, with none.  The IOmcu takes it at its next OPEN of a
     * D series line; classic servos need neither.  Set on the Hitec tab's
     * MORE list, beside the series.
     */
    [SET_HITEC_RX] = {
        "hitec_rx", "",
        SET_CAT_SERVO, SET_TYPE_ENUM, 0, 0, 1, 0, ENUM_OPTS(hitec_rx) },

    /*
     * The supply's.  The ranges are a USB-PD PPS (Programmable Power Supply)
     * source's widest profile, SUPPLY_CAPS_PPS_DEFAULT; a supply that offers
     * less narrows them again where they are used.  The caps default to the
     * whole range and the trips to off, so nothing is limited until the
     * operator limits it; the start set points are a servo rail's.
     */
    [SET_SUPPLY_V_MAX] = {
        "sup_v_max", "V",
        SET_CAT_SUPPLY, SET_TYPE_FLOAT, 3.3f, 21.0f, 0.02f, 21.0f, NULL, 0 },
    [SET_SUPPLY_I_MAX] = {
        "sup_i_max", "A",
        SET_CAT_SUPPLY, SET_TYPE_FLOAT, 0.5f, 5.0f, 0.05f, 5.0f, NULL, 0 },
    [SET_SUPPLY_V_START] = {
        "sup_v_start", "V",
        SET_CAT_SUPPLY, SET_TYPE_FLOAT, 3.3f, 21.0f, 0.02f, 6.0f, NULL, 0 },
    [SET_SUPPLY_I_START] = {
        "sup_i_start", "A",
        SET_CAT_SUPPLY, SET_TYPE_FLOAT, 0.5f, 5.0f, 0.05f, 2.0f, NULL, 0 },
    [SET_SUPPLY_TRIP_I] = {
        "sup_trip_i", "A",
        SET_CAT_SUPPLY, SET_TYPE_FLOAT, 0.0f, 5.0f, 0.05f, 0.0f, NULL, 0 },
    [SET_SUPPLY_TRIP_V] = {
        "sup_trip_v", "V",
        SET_CAT_SUPPLY, SET_TYPE_FLOAT, 0.0f, 21.0f, 0.02f, 0.0f, NULL, 0 },
    [SET_SUPPLY_TRIP_MS] = {
        "sup_trip_ms", "ms",
        SET_CAT_SUPPLY, SET_TYPE_INT, 0, 5000, 10, 100, NULL, 0 },
    /* Whether a set point changed while the output is on waits for a
     * confirmation: from the slider and its steps, and from the keypad. */
    [SET_SUPPLY_CONFIRM_SLIDE] = {
        "sup_conf_sl", "",
        SET_CAT_SUPPLY, SET_TYPE_BOOL, 0, 1, 1, 1, NULL, 0 },
    [SET_SUPPLY_CONFIRM_KEYS] = {
        "sup_conf_kp", "",
        SET_CAT_SUPPLY, SET_TYPE_BOOL, 0, 1, 1, 1, NULL, 0 },

    /*
     * The servo test's.  What it moves through -- a curve, how fast, how far
     * and for how long -- the supply steps it repeats that at, and the
     * limits it judges the servo by.  A pass/fail limit of 0 is not
     * checked.  The servo's own type, pulse widths and frame rate are not
     * here: they start at STANDARD PWM and 50Hz at every restart, so a
     * servo plugged in later never meets a rate meant for another.
     */
    [SET_SERVO_CURVE] = {
        "srv_curve", "",
        SET_CAT_SERVO, SET_TYPE_ENUM, 0, 0, 1, 1, ENUM_OPTS(servo_curve) },
    [SET_SERVO_TEST_HZ] = {
        "srv_test_hz", "Hz",
        SET_CAT_SERVO, SET_TYPE_FLOAT, 0.05f, 5.0f, 0.05f, 0.5f, NULL, 0 },
    [SET_SERVO_TEST_RANGE] = {
        "srv_range", "%",
        SET_CAT_SERVO, SET_TYPE_INT, 10, 100, 5, 80, NULL, 0 },
    [SET_SERVO_LEN_BY] = {
        "srv_len_by", "",
        SET_CAT_SERVO, SET_TYPE_ENUM, 0, 0, 1, 0, ENUM_OPTS(servo_len_by) },
    [SET_SERVO_LEN_S] = {
        "srv_len_s", "s",
        SET_CAT_SERVO, SET_TYPE_INT, 5, 3600, 5, 60, NULL, 0 },
    [SET_SERVO_LEN_MOVES] = {
        "srv_len_moves", "",
        SET_CAT_SERVO, SET_TYPE_INT, 1, 1000, 1, 20, NULL, 0 },
    [SET_SERVO_DWELL_MS] = {
        "srv_dwell_ms", "ms",
        SET_CAT_SERVO, SET_TYPE_INT, 0, 5000, 10, 200, NULL, 0 },
    [SET_SERVO_SETTLE_MS] = {
        "srv_settle_ms", "ms",
        SET_CAT_SERVO, SET_TYPE_INT, 0, 10000, 50, 500, NULL, 0 },
    [SET_SERVO_STEP_48] = {
        "srv_step48", "",
        SET_CAT_SERVO, SET_TYPE_BOOL, 0, 1, 1, 1, NULL, 0 },
    [SET_SERVO_STEP_60] = {
        "srv_step60", "",
        SET_CAT_SERVO, SET_TYPE_BOOL, 0, 1, 1, 1, NULL, 0 },
    [SET_SERVO_STEP_74] = {
        "srv_step74", "",
        SET_CAT_SERVO, SET_TYPE_BOOL, 0, 1, 1, 1, NULL, 0 },
    [SET_SERVO_STEP_84] = {
        "srv_step84", "",
        SET_CAT_SERVO, SET_TYPE_BOOL, 0, 1, 1, 1, NULL, 0 },
    [SET_SERVO_BROWNOUT] = {
        "srv_brownout", "",
        SET_CAT_SERVO, SET_TYPE_BOOL, 0, 1, 1, 1, NULL, 0 },
    /* The brown-out walk's LOWEST VOLTAGE and STEP.  2.0 to 4.8V and 0.1
     * to 1.0V hold the walk to at most 31 voltages from 5.0V
     * (SERVO_TEST_BROWNOUT_MAX). */
    [SET_SERVO_BO_FLOOR] = {
        "srv_bo_floor", "V",
        SET_CAT_SERVO, SET_TYPE_FLOAT, 2.0f, 4.8f, 0.1f, 3.0f, NULL, 0 },
    [SET_SERVO_BO_STEP] = {
        "srv_bo_step", "V",
        SET_CAT_SERVO, SET_TYPE_FLOAT, 0.1f, 1.0f, 0.1f, 0.2f, NULL, 0 },
    [SET_SERVO_IDLE_MAX] = {
        "srv_idle_max", "A",
        SET_CAT_SERVO, SET_TYPE_FLOAT, 0.0f, 5.0f, 0.01f, 0.0f, NULL, 0 },
    [SET_SERVO_HOLD_MAX] = {
        "srv_hold_max", "A",
        SET_CAT_SERVO, SET_TYPE_FLOAT, 0.0f, 5.0f, 0.01f, 0.0f, NULL, 0 },
    [SET_SERVO_TRAVEL_MAX_MS] = {
        "srv_travel_ms", "ms",
        SET_CAT_SERVO, SET_TYPE_INT, 0, 5000, 10, 0, NULL, 0 },
    [SET_SERVO_STALL_A] = {
        "srv_stall_a", "A",
        SET_CAT_SERVO, SET_TYPE_FLOAT, 0.1f, 5.0f, 0.05f, 2.0f, NULL, 0 },
    /*
     * The servo programmer's, set on its own page.  The ranges are the
     * programming port's (kst_port.h) and the model's (kst_prog.h).
     */
    [SET_SERVO_PROG_V] = {
        "srv_prog_v", "V",
        SET_CAT_SERVO, SET_TYPE_FLOAT, 4.8f, 8.4f, 0.1f, 5.0f, NULL, 0 },
    [SET_SERVO_PROG_I] = {
        "srv_prog_i", "A",
        SET_CAT_SERVO, SET_TYPE_FLOAT, 0.05f, 3.0f, 0.001f, 1.0f, NULL, 0 },
    [SET_SERVO_PROG_WAIT] = {
        "srv_prog_wait", "ms",
        SET_CAT_SERVO, SET_TYPE_INT, 0, 10000, 1, 100, NULL, 0 },
    /*
     * What the SERVO screen's tester drives: the standard servo outputs or
     * the programming port, and whether that port renders its pulse.
     */
    [SET_SERVO_TARGET] = {
        "srv_target", "",
        SET_CAT_SERVO, SET_TYPE_ENUM, 0, 0, 1, 0, ENUM_OPTS(servo_target) },
    [SET_SERVO_PORT_PWM] = {
        "srv_port_pwm", "",
        SET_CAT_SERVO, SET_TYPE_BOOL, 0, 1, 1, 1, NULL, 0 },
    /*
     * The series the programmer for servos by Hitec enters at its next
     * CONNECT, in hitec_family_t's order: classic (HS-5xxx, HS-7xxx) or D
     * series.  Set on its SERVO tab.
     */
    [SET_HITEC_SERIES] = {
        "hitec_series", "",
        SET_CAT_SERVO, SET_TYPE_ENUM, 0, 0, 1, 1, ENUM_OPTS(hitec_series) },
    /*
     * The ESC signal-wire session's STROM: SWITCHED (0) or BY HAND (1), as the
     * operator last chose it on the connect page.  Kept with the programmers'
     * choices, as the Hitec series is: SERVO's screen lists its rows by name,
     * so no screen shows it as a row and no DEFAULTS resets it.
     */
    [SET_ESCW_BY_HAND] = {
        "escw_by_hand", "",
        SET_CAT_SERVO, SET_TYPE_BOOL, 0, 1, 1, 0, NULL, 0 },
    /*
     * Stick programming's.  The supply it powers the ESC (electronic speed
     * controller) from, and how it hears the beeps.  No ESC has been
     * recorded, so every time below is a default standing in for a
     * measurement; esc_stick_timing_defaults() holds the same numbers.
     */
    [SET_STICK_V] = {
        "stk_v", "V",
        SET_CAT_STICK, SET_TYPE_FLOAT, 0.0f, 20.0f, 0.1f, 0.0f, NULL, 0 },
    [SET_STICK_I] = {
        "stk_i", "A",
        SET_CAT_STICK, SET_TYPE_FLOAT, 0.1f, 3.0f, 0.05f, 1.0f, NULL, 0 },
    [SET_STICK_BEEP_MIN] = {
        "stk_beep_min", "ms",
        SET_CAT_STICK, SET_TYPE_INT, 20, 2000, 10, 200, NULL, 0 },
    [SET_STICK_GAP_MIN] = {
        "stk_gap_min", "ms",
        SET_CAT_STICK, SET_TYPE_INT, 20, 2000, 10, 200, NULL, 0 },
    [SET_STICK_LONG] = {
        "stk_long", "ms",
        SET_CAT_STICK, SET_TYPE_INT, 50, 5000, 10, 500, NULL, 0 },
    [SET_STICK_LONG_MAX] = {
        "stk_long_max", "ms",
        SET_CAT_STICK, SET_TYPE_INT, 100, 10000, 50, 1500, NULL, 0 },
    [SET_STICK_GROUP_GAP] = {
        "stk_group_gap", "ms",
        SET_CAT_STICK, SET_TYPE_INT, 50, 10000, 50, 700, NULL, 0 },
    [SET_STICK_ENTRY] = {
        "stk_entry", "ms",
        SET_CAT_STICK, SET_TYPE_INT, 1000, 60000, 500, 5000, NULL, 0 },
    [SET_STICK_STORE] = {
        "stk_store", "ms",
        SET_CAT_STICK, SET_TYPE_INT, 0, 10000, 250, 2000, NULL, 0 },
    [SET_STICK_OFF] = {
        "stk_off", "ms",
        SET_CAT_STICK, SET_TYPE_INT, 500, 20000, 500, 3000, NULL, 0 },
    [SET_STICK_SILENCE] = {
        "stk_silence", "ms",
        SET_CAT_STICK, SET_TYPE_INT, 1000, 60000, 1000, 10000, NULL, 0 },
    [SET_STICK_TIMEOUT] = {
        "stk_timeout", "ms",
        SET_CAT_STICK, SET_TYPE_INT, 5000, 600000, 5000, 180000, NULL, 0 },
    [SET_STICK_THRESHOLD] = {
        "stk_thresh", "mA",
        SET_CAT_STICK, SET_TYPE_INT, 10, 2000, 10, 100, NULL, 0 },
    [SET_STICK_HYST] = {
        "stk_hyst", "mA",
        SET_CAT_STICK, SET_TYPE_INT, 0, 1000, 10, 40, NULL, 0 },
    [SET_SERVO_REPORT] = {
        "srv_report", "",
        SET_CAT_SERVO, SET_TYPE_BOOL, 0, 1, 1, 1, NULL, 0 },
    /*
     * Whether the panel asks GitHub for a newer release at start, on the
     * home network it keeps, and says so on OVERVIEW.  NEVER REMIND ME
     * switches it off; the UPDATES screen switches it back on.
     */
    [SET_UPD_REMIND] = {
        "upd_remind", "",
        SET_CAT_UPDATE, SET_TYPE_BOOL, 0, 1, 1, 1, NULL, 0 },
    /*
     * Which releases a check reads.  STABLE: GitHub's latest release,
     * which is never a pre-release; a testing release (odd minor number) is
     * published as one.  TESTING: the newest release, testing or stable.
     * Either way only a release of the panel's line (fw_release.h: every
     * release on the ESP32-P4 panel, 0.22.x on the ESP32-S3 panel's line)
     * newer than the running build is offered.
     */
    [SET_UPD_CHANNEL] = {
        "upd_channel", "",
        SET_CAT_UPDATE, SET_TYPE_ENUM, 0, 0, 1, 0, ENUM_OPTS(upd_channel) },
    /*
     * Which logs reach the card (card_tree.h).  AUTO LOGS: a log of each
     * ARMED run and each switch-on of the SUPPLY output, in BENCH/LOGS.
     * USER LOGS: the AUTOMATIC TEST's CSV and report and the LIMIT RUN's
     * report, in USER/LOGS.  Off, nothing is written and no number taken.
     */
    [SET_LOG_AUTO] = {
        "log_auto", "",
        SET_CAT_CARD, SET_TYPE_BOOL, 0, 1, 1, 1, NULL, 0 },
    [SET_LOG_USER] = {
        "log_user", "",
        SET_CAT_CARD, SET_TYPE_BOOL, 0, 1, 1, 1, NULL, 0 },
    /*
     * What a browser on the Wi-Fi page needs to write to the card
     * (wa_writes_t, wa_api.h): OPEN nothing beyond reaching the page, the
     * default; ACCESS CODE this session's access code, with the code
     * switch off too; HOLD a 2s hold on WI-FI once a radio session; OFF no
     * write.  The page's guards against other sites hold in every case.
     */
    [SET_WEB_WRITES] = {
        "web_writes", "",
        SET_CAT_CARD, SET_TYPE_ENUM, 0, 0, 1, 0, ENUM_OPTS(web_writes) },
    /*
     * What the servo test, the LIMIT RUN and the SERVO run log read the
     * angle from (servo_angle_src_t, servo_angle.h): the AS5600, the
     * default, or the IMU's AXIS with its SIGN, carried in AS5600 counts.
     */
    [SET_ANGLE_SOURCE] = {
        "angle_source", "",
        SET_CAT_SENSORS, SET_TYPE_ENUM, 0, 0, 1, 0, ENUM_OPTS(angle_source) },
    /*
     * RUDDER's LENGTH: the control surface from its hinge line to its
     * trailing edge, which turns a deflection into mm (servo_rudder.h).
     * 0 is not set, and the task shows degrees alone; 1 to 9mm are not
     * taken.
     */
    [SET_RUDDER_LEN_MM] = {
        "rud_len_mm", "mm",
        SET_CAT_SERVO, SET_TYPE_FLOAT, 0.0f, 500.0f, 1.0f, 0.0f, NULL, 0,
        10.0f },
};
