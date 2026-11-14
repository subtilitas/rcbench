/**
 * @file
 * @brief Device settings: a typed schema, a value per entry, and a persistence
 *        seam.
 *
 * The schema carries range, step and unit, so the settings screen is generic
 * -- it renders whatever the table says rather than hard-coding a control per
 * option.  Adding a setting is one table row, and its label and help line
 * are two entries of ui_text.def (shared/text), with every language.
 *
 * Pure C.  Persistence is a pair of function pointers, so the host tests use
 * memory and the firmware uses NVS (non-volatile storage) without the model
 * knowing the difference.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The groups SETUP lists settings in. */
typedef enum {
    SET_CAT_APP = 0,     /**< theme, brightness, rotation, language,
                              time zone, dimming, plot; units hold a
                              key and show no row                      */
    SET_CAT_CARD,        /**< the SD card: which logs are written     */
    SET_CAT_SENSORS,     /**< what the IOmcu reads: the sensor bus, the
                              INA228, the INA3221 parts (settings_sensors.h),
                              the AS5600, the phase tap; vibration and the
                              optical tacho hold keys and show no row   */
    SET_CAT_WIRELESS,    /**< the panel's Bluetooth LE devices: the IMU
                              with the angle source, and the Dial      */
    SET_CAT_ESC,         /**< pack, motor, telemetry, throttle output */
    SET_CAT_SUPPLY,      /**< the SUPPLY screen's limits and start    */
    SET_CAT_PDMINI,      /**< the PD mini's wiring, on SUPPLY's PD MINI
                              tab: apart from the limits, so a reset of
                              SET_CAT_SUPPLY leaves the wiring          */
    SET_CAT_SERVO,       /**< the SERVO screen's test and its limits  */
    SET_CAT_STICK,       /**< stick programming: supply and timing    */
    SET_CAT_UPDATE,      /**< the release reminder and the releases a
                              check offers, on SETUP's UPDATES          */
    SET_CAT_COUNT
} setting_cat_t;

/**
 * @brief The categories SETUP opens from its tiles, which come first.  SET_CAT_ESC
 * is edited on MOTOR & ESC > SETTINGS, SET_CAT_SUPPLY, SET_CAT_PDMINI,
 * SET_CAT_SERVO and SET_CAT_STICK on their own screens, beside what they
 * set, and
 * SET_CAT_UPDATE on the UPDATES screen a SETUP tile opens.
 */
#define SET_CAT_SETUP_COUNT SET_CAT_ESC

/** @brief How a value is kept, stepped and shown. */
typedef enum {
    SET_TYPE_INT = 0,
    SET_TYPE_FLOAT,
    SET_TYPE_BOOL,
    SET_TYPE_ENUM,
} setting_type_t;

/** @brief The INA3221 parts SETUP > SENSORS sets up: four, at 0x40 to
 *         0x43, three channels each, and the rows a channel has: WATCHES,
 *         SHUNT and LIMIT (settings_sensors.h). */
#define SET_INA_PARTS    4   /**< INA3221 parts, 0x40 to 0x43        */
#define SET_INA_CHANNELS 12  /**< channels, three a part              */
#define SET_INA_CH_ROWS  3   /**< a channel's rows: WATCHES, SHUNT, LIMIT */

/** @brief Every setting's id, in schema order. */
typedef enum {
    /* --- pack, motor, telemetry, output ------------------------------- */
    SET_PACK_CELLS = 0,
    SET_PACK_MAH,
    SET_PACK_MOHM,
    SET_MOTOR_POLES,
    SET_MOTOR_KV,
    SET_OUT_MIN_US,
    SET_OUT_MAX_US,
    SET_OUT_RAMP,
    /* --- application --------------------------------------------------- */
    SET_THEME,
    SET_BRIGHTNESS,
    SET_CONTRAST,
    SET_SCREEN_ROT,
    SET_LANGUAGE,
    SET_UNITS,
    SET_TIME_ZONE,
    SET_DIM_AFTER,
    SET_PLOT_PAN,
    SET_KNOB_EN,
    SET_KNOB_SCALE,
    SET_KB_SEE,
    SET_DEMO_MODE,
    SET_HW_STOP,
    /* --- interfaces ---------------------------------------------------- */
    SET_INA228_EN,
    SET_INA228_ADDR,
    SET_INA228_UOHM,
    SET_INA228_MAX_A,
    /* The one INA3221 of 0.21: in no category and on no screen, loaded
     * and saved as stored, and read once to seed SENSORS
     * (settings_sensors.h). */
    SET_INA3221_EN,
    SET_INA3221_ADDR,
    SET_INA3221_MOHM,
    SET_INA3221_CH,
    SET_SENSE_SDA,
    SET_SENSE_SCL,
    SET_ENC_EN,
    SET_ENC_CENTER,
    SET_TONE_EN,
    SET_TONE_PIN,
    SET_TONE_F_MIN,
    SET_TONE_F_MAX,
    SET_TONE_SPLIT,
    SET_TONE_GAP,
    SET_TONE_PERIODS,
    SET_VIBE_EN,
    SET_OPTICAL_EN,
    SET_OPTICAL_PIN,
    SET_PDMINI_EN,
    SET_PDMINI_TX,
    SET_PDMINI_RX,
    SET_PDMINI_BAUD,
    /* --- the programming port ------------------------------------------ */
    SET_HITEC_RX,
    /* --- the INA3221 parts, on SETUP > SENSORS (settings_sensors.h) ----- */
    /* Part a's FITTED, then channel by channel (part a's CHn) WATCHES,
     * SHUNT and LIMIT, SET_INA_W_<a><n> for part a's CHn:
     * settings_sensors.h names them by channel number. */
    SET_INA_FIT_0, SET_INA_FIT_1, SET_INA_FIT_2, SET_INA_FIT_3,
    SET_INA_W_01, SET_INA_SH_01, SET_INA_LIM_01,
    SET_INA_W_02, SET_INA_SH_02, SET_INA_LIM_02,
    SET_INA_W_03, SET_INA_SH_03, SET_INA_LIM_03,
    SET_INA_W_11, SET_INA_SH_11, SET_INA_LIM_11,
    SET_INA_W_12, SET_INA_SH_12, SET_INA_LIM_12,
    SET_INA_W_13, SET_INA_SH_13, SET_INA_LIM_13,
    SET_INA_W_21, SET_INA_SH_21, SET_INA_LIM_21,
    SET_INA_W_22, SET_INA_SH_22, SET_INA_LIM_22,
    SET_INA_W_23, SET_INA_SH_23, SET_INA_LIM_23,
    SET_INA_W_31, SET_INA_SH_31, SET_INA_LIM_31,
    SET_INA_W_32, SET_INA_SH_32, SET_INA_LIM_32,
    SET_INA_W_33, SET_INA_SH_33, SET_INA_LIM_33,
    SET_INA_FIT_LAST = SET_INA_FIT_3,
    SET_INA_CH_0     = SET_INA_W_01,       /**< channel 0's first row     */
    SET_INA_CH_LAST  = SET_INA_LIM_33,
    /* --- the programmable supply --------------------------------------- */
    SET_SUPPLY_V_MAX,
    SET_SUPPLY_I_MAX,
    SET_SUPPLY_V_START,
    SET_SUPPLY_I_START,
    SET_SUPPLY_TRIP_I,
    SET_SUPPLY_TRIP_V,
    SET_SUPPLY_TRIP_MS,
    SET_SUPPLY_CONFIRM_SLIDE,
    SET_SUPPLY_CONFIRM_KEYS,
    /* --- the servo test ------------------------------------------------- */
    SET_SERVO_CURVE,
    SET_SERVO_TEST_HZ,
    SET_SERVO_TEST_RANGE,
    SET_SERVO_LEN_BY,
    SET_SERVO_LEN_S,
    SET_SERVO_LEN_MOVES,
    SET_SERVO_DWELL_MS,
    SET_SERVO_SETTLE_MS,
    SET_SERVO_STEP_48,
    SET_SERVO_STEP_60,
    SET_SERVO_STEP_74,
    SET_SERVO_STEP_84,
    SET_SERVO_BROWNOUT,
    SET_SERVO_BO_FLOOR,
    SET_SERVO_BO_STEP,
    SET_SERVO_IDLE_MAX,
    SET_SERVO_HOLD_MAX,
    SET_SERVO_TRAVEL_MAX_MS,
    SET_SERVO_STALL_A,
    SET_SERVO_REPORT,
    SET_SERVO_PROG_V,
    SET_SERVO_PROG_I,
    SET_SERVO_PROG_WAIT,
    SET_SERVO_TARGET,
    SET_SERVO_PORT_PWM,
    SET_KST_EXPERT,
    SET_HITEC_SERIES,
    SET_LIMIT_FAMILY,
    SET_RAUTO_FAMILY,
    SET_ESCW_BY_HAND,
    /* --- stick programming ---------------------------------------------- */
    SET_STICK_V,
    SET_STICK_I,
    SET_STICK_BEEP_MIN,
    SET_STICK_GAP_MIN,
    SET_STICK_LONG,
    SET_STICK_LONG_MAX,
    SET_STICK_GROUP_GAP,
    SET_STICK_ENTRY,
    SET_STICK_STORE,
    SET_STICK_OFF,
    SET_STICK_SILENCE,
    SET_STICK_TIMEOUT,
    SET_STICK_THRESHOLD,
    SET_STICK_HYST,
    /* --- updates --------------------------------------------------------- */
    SET_UPD_REMIND,
    SET_UPD_CHANNEL,
    /* --- the SD card ----------------------------------------------------- */
    SET_LOG_AUTO,
    SET_LOG_USER,
    SET_WEB_WRITES,
    /* --- the angle source of the servo runs ---------------------------- */
    SET_ANGLE_SOURCE,
    /* --- the SERVO workbench's RUDDER task -------------------------------- */
    SET_RUDDER_LEN_MM,
    /* --- the Bluetooth IMU (SETUP > WIRELESS > IMU, imu_ctl.h) ----------- */
    SET_IMU_EN,
    SET_IMU_AXIS,
    SET_IMU_SIGN,
    SET_IMU_RATE,
    SET_IMU_ALGO,
    SET_IMU_FILTER,
    /* --- RUDDER's AUTO end points ---------------------------------------- */
    SET_RUDDER_DEFL_LO,
    SET_RUDDER_DEFL_HI,
    SET_RUDDER_MARGIN,
    SET_RUDDER_TOL,
    /* --- RUDDER's saved zero --------------------------------------------- */
    SET_RUDDER_ZERO_DEG,
    SET_RUDDER_ZERO_KEY,
    /* --- the M5Stack Dial (SETUP > WIRELESS > DIAL, dial_ctl.h) ---------- */
    SET_DIAL_EN,
    SET_DIAL_THR,
    SET_DIAL_SRV,

    SETTING_COUNT
} setting_id_t;

/**
 * @brief A setting's row of the schema: its key, unit, category, type,
 *        range, step, default and options.  Its label and help line are
 *        ui_text.def's (settings_label(), settings_help()).
 */
typedef struct {
    const char        *key;      /**< NVS key, 15 chars or fewer   */
    const char        *unit;     /**< shown beside the value; "" none */
    setting_cat_t      cat;             /**< the category the setting is listed in */
    setting_type_t     type;            /**< the type of the value */
    float              min;             /**< the lowest value */
    float              max;             /**< the highest value */
    float              step;            /**< the step of the value's keys and slider */
    float              def;             /**< the default value */
    const char *const *options;  /**< SET_TYPE_ENUM only */
    uint8_t            option_count;    /**< number of options in options */
    /** A number whose values between 0 and this are not taken: 0 is
     *  "off" and the next value up is this one.  0 for none. */
    float              off_below;
} setting_def_t;

/** Called after any value actually changes, so the app can act on it. */
typedef void (*settings_observer_fn)(setting_id_t id);

/**
 * @brief The string settings: a few short strings beside the numbers, kept by
 * the same store and saved by the same request.
 *
 * A string holds up to SETTINGS_TEXT_MAX - 1 characters.
 */
typedef enum {
    SET_TEXT_DUT_NAME = 0,   /**< the device under test, for the report */
    SET_TEXT_LOG_AUTO_DIR,   /**< AUTO LOG FOLDER: the automatic logs'  */
    SET_TEXT_LOG_USER_DIR,   /**< USER LOG FOLDER: the tests' files     */
    SET_TEXT_IMU_ADDR,       /**< the bound sensor's BLE address and its
                                  type, "00:0C:BF:08:19:29 P"; key
                                  imu_ble                                 */
    SET_TEXT_IMU_NAME,       /**< its name from the scan, 23 characters */
    SET_TEXT_DIAL_ADDR,      /**< the bound Dial's BLE address and its
                                  type; key dial_ble                      */
    SET_TEXT_DIAL_NAME,      /**< its name from the scan, "DIAL-3A10"    */
    SETTING_TEXT_COUNT
} setting_text_id_t;

/** @brief Bytes of a string setting, the terminator included. */
#define SETTINGS_TEXT_MAX 24

/** Called after a string actually changes, as settings_observer_fn is
 *  for a value: an edit, a discard, a reset and the load at start-up. */
typedef void (*settings_text_observer_fn)(setting_text_id_t id);

/** @brief The medium the settings are kept on. */
typedef struct {
    /** @brief Read the values.  @p count is SETTING_COUNT; true when read. */
    bool (*load)(float *values, int count);
    /**
     * @brief Write the values.  @p count is SETTING_COUNT.
     *
     * True only if every value reached the medium.  A store that cannot
     * say so cannot be believed: the model keeps the edit and the screen
     * goes on offering the write, rather than reporting a save that did
     * not happen and refusing the retry that would notice.
     */
    bool (*save)(const float *values, int count);
    /**
     * @brief Read the strings, under the same rules.  @p count is
     * SETTING_TEXT_COUNT.
     *
     * NULL in a store that keeps none, which then keeps them for the session
     * only.
     */
    bool (*load_text)(char (*texts)[SETTINGS_TEXT_MAX], int count);
    /** @brief Write the strings, under the same rules as save(). */
    bool (*save_text)(const char (*texts)[SETTINGS_TEXT_MAX], int count);
} settings_store_t;

/** @brief Reset to defaults, then load from the store if one is set. */
void settings_init(void);

/**
 * @brief Set the medium.
 * @param store  The store; NULL for none.
 */
void settings_set_store(const settings_store_t *store);
/**
 * @brief Set who is told of a changed value.
 * @param fn  Called after a value changes; NULL for none.
 */
void settings_set_observer(settings_observer_fn fn);
/**
 * @brief Set who is told of a changed string.
 * @param fn  Called after a string changes; NULL for none.
 */
void settings_set_text_observer(settings_text_observer_fn fn);

/** @brief Whether a setting is held: something in use depends on the value as
 *  it is. */
typedef bool (*settings_hold_fn)(setting_id_t id);
/**
 * @brief Who says which settings are held.
 *
 * Asked at every change, so a hold begins and ends with what it names.
 *
 * @param fn  The source; NULL for none.
 */
void settings_set_hold(settings_hold_fn fn);
/**
 * @brief Whether @p id is held now: settings_set(), settings_adjust(),
 *        settings_reset() and settings_reset_all() leave it, and SETUP
 *        draws its row without keys.
 *
 * The servo tester's target and the port's pulse while the servo
 * programmer holds the port (kst_panel.h); a SAFE-only setting while the
 * bench is ARMED (settings_safe_only()).
 *
 * @param id The setting.
 * @return True while held; false for an id out of range.
 */
bool settings_held(setting_id_t id);

/**
 * @brief Who says whether the bench is ARMED now.
 * @return True while ARMED.
 */
typedef bool (*settings_armed_fn)(void);

/**
 * @brief Install who says whether the bench is ARMED, or NULL for a bench
 *        that is never ARMED.  Asked at every change, as the hold is.
 * @param fn The source; the panel's reads the router's status.
 */
void settings_set_armed(settings_armed_fn fn);

/**
 * @brief Whether @p id changes on a SAFE bench only: held while the
 *        source settings_set_armed() installed says ARMED, as the throttle
 *        endpoints on MOTOR & ESC > SETTINGS are locked while ARMED.
 *        Screen rotation, which moves every control under the finger, and
 *        the PD mini's wiring, whose change switches off the supply an
 *        ARMED servo may run from.
 * @param id The setting.
 * @return True for SET_SCREEN_ROT, SET_PDMINI_EN, SET_PDMINI_TX, SET_PDMINI_RX
 *         and SET_PDMINI_BAUD.
 */
bool settings_safe_only(setting_id_t id);

/**
 * @brief A setting's schema row.
 * @param id  The setting.
 * @return The row; NULL for an id out of range.
 */
const setting_def_t *settings_def(setting_id_t id);

/**
 * @brief Whether @p id follows another setting.
 *
 * settings_set() and settings_adjust() leave it, and SETUP draws its row
 * without keys.  Sensor SCL is derived: the GPIO after Sensor SDA.
 *
 * @param id  The setting.
 * @return true for Sensor SCL.
 */
bool settings_derived(setting_id_t id);

/**
 * @brief Hold @p id at @p value: a pin a board solders, which no setting
 *        moves.
 *
 * settings_get() then answers @p value; the stored value stays as it is,
 * is saved as it is and comes back with settings_unfix().  SETUP draws the
 * row without keys (settings_fixed()).
 *
 * @param id     The setting.
 * @param value  The value it reads as.
 * @return true when the value settings_get() answers changed.
 */
bool settings_fix(setting_id_t id, int value);

/**
 * @brief Let @p id read its own value again.
 *
 * @param id  The setting.
 * @return true when the value settings_get() answers changed.
 */
bool settings_unfix(setting_id_t id);

/**
 * @brief Whether @p id is held by settings_fix().
 *
 * @param id  The setting.
 * @return true while it is.
 */
bool settings_fixed(setting_id_t id);
/**
 * @brief A category's name in English, from ui_text.def.
 * @param cat The category.
 * @return Its name; "" for one out of range.  ui_setting_category() gives
 *         it in the language showing.
 */
const char *settings_category_name(setting_cat_t cat);
/**
 * @brief A setting's label in English, from ui_text.def.
 * @param id The setting.
 * @return Its label; "" for an id out of range.  ui_setting_label() gives
 *         it in the language showing.
 */
const char *settings_label(setting_id_t id);
/**
 * @brief A setting's help line in English, from ui_text.def.
 * @param id The setting.
 * @return Its help line, "" for none and for an id out of range.
 *         ui_setting_help() gives it in the language showing.
 */
const char *settings_help(setting_id_t id);
/**
 * @brief Which option list a setting shows, so a language's options are
 *        found by it.
 * @param id The setting.
 * @return The list's place among the UI_OPTIONS() of ui_text.def, counted
 *         from 0; -1 for a setting that is no choice and for an id out of
 *         range.
 */
int settings_option_list(setting_id_t id);

/**
 * @brief A setting's value.
 * @param id  The setting.
 * @return The value, or the one settings_fix() holds it at; 0 for an id out
 *         of range.
 */
float settings_get(setting_id_t id);
/**
 * @brief A setting's value as an int, rounded.
 * @param id  The setting.
 * @return The value; 0 for an id out of range.
 */
int   settings_get_int(setting_id_t id);
/**
 * @brief A setting's value as a bool.
 * @param id  The setting.
 * @return true when the value is not 0.
 */
bool  settings_get_bool(setting_id_t id);

/**
 * @brief Set a value, fitted to the schema.
 *
 * A number is clamped to its range and snapped to its step, and an enum or a
 * pin out of its range takes the default.  Fires the observer when the value
 * changes.  A derived setting and a held one are left as they are.
 *
 * @param id     The setting.
 * @param value  The value.
 */
void settings_set(setting_id_t id, float value);
/**
 * @brief Step a value by @p steps increments.
 *
 * A number stops at its range's ends.  Booleans and enums cycle
 * (settings_cycles()), except the INA228 and INA3221 addresses, which stop at
 * the first and the last of their list.  Sensor SDA steps from one value
 * settings_sense_sda_valid() takes to the next and stops at the first and the
 * last.  PD mini TX and PD mini RX step over the GPIO the other holds, so a
 * step never sets them to one pin; with that GPIO at the end of the range the
 * step stays where it is.  -1, not wired, is a step for both.
 *
 * @param id     The setting.
 * @param steps  Increments, negative for down.
 */
void settings_adjust(setting_id_t id, int steps);

/**
 * @brief Whether settings_adjust() wraps @p id round from one end to the
 * other: a switch or a choice, drawn with < and > keys.
 *
 * False for a number and for the monitors' addresses, which stop at their ends
 * and are drawn with - and + keys.
 *
 * @param id  The setting.
 * @return true when it wraps; false for an id out of range.
 */
bool settings_cycles(setting_id_t id);

/**
 * @brief Whether Sensor SDA takes @p gpio: -1, the bus's pins not set, or an
 * IOmcu GPIO the sensor bus can have its SDA on with SCL on the GPIO
 * after it.  That is one I2C block's pair, link_sn_pins_pair() -- the rule
 * the IOmcu's SENSE page judges a write by -- on two pins the
 * RP2350-CAN brings out and does not reserve, outbind_reserved_mask(): the
 * mask the IOmcu reserves at boot.  0, 4, 6, 14, 16, 18, 20 and 26.
 * A pin an output, the supply or the phase tap holds is not known here;
 * the page refuses it and SETUP marks the row.
 *
 * @param gpio  An IOmcu GPIO number, or -1.
 * @return true when Sensor SDA takes it.
 */
bool settings_sense_sda_valid(int gpio);

/**
 * @brief Whether Tap pin takes @p gpio: an IOmcu GPIO the RP2350-CAN brings out
 * and does not reserve, outbind_reserved_mask(), that is no ADC pin -- the
 * TONE page refuses GP26 to GP29 of the RP2350A and GP40 to GP47 of the
 * RP2354B.  That leaves 0, 1, 2, 4, 5, 6, 7 and 13 to 22.  A pin an output,
 * the supply or the sensor bus holds is not known here; the page refuses it
 * and SETUP marks the row.
 *
 * @param gpio An IOmcu GPIO number.
 * @return true when Tap pin takes it.
 */
bool settings_tap_pin_valid(int gpio);

/**
 * @brief Whether PD mini TX and PD mini RX hold one GPIO, 0 or above.
 *
 * The steps never set that; a stored set-up can hold it.  The panel writes
 * such a wiring to the SUPPLY page disabled (supply_link.c), so the PD mini
 * stays off, and SETUP marks both rows.
 *
 * @return true when both hold the same GPIO.
 */
bool settings_pdmini_same_pin(void);

/**
 * @brief Restore one category to the schema defaults: its values and its
 * strings (settings_text_category()).
 *
 * @param cat  The category.
 */
void settings_reset(setting_cat_t cat);
/**
 * @brief Settings back to their defaults, as settings_reset() does for a
 *        category: a held row stays, an observer hears each change, and only
 *        an actual change is an edit.
 * @param ids The settings; NULL resets none.
 * @param n   How many.
 */
void settings_reset_rows(const setting_id_t *ids, int n);
/**
 * @brief Restore every value and string to the schema defaults; a held row
 * stays.
 */
void settings_reset_all(void);

/**
 * @brief The ids in a category, in schema order.
 *
 * @param cat  The category.
 * @param out  Receives the ids, up to @p max of them; may be NULL.
 * @param max  Capacity of @p out.
 * @return How many ids the category has, also past @p max.
 */
int settings_in_category(setting_cat_t cat, setting_id_t *out, int max);

/**
 * @brief The digits after the point a number of @p id is shown and typed
 *        with: as many as its step has.
 * @param id The setting.
 * @return 0 for a step of 1 or more, 1 for 0.1, 2 for 0.05, 0.02 and 0.01,
 *         3 for 0.001; 0 for an id out of range and a step of 0.
 */
int settings_decimals(setting_id_t id);

/**
 * @brief Render the value as it should appear on screen, unit excluded: a
 *        float with settings_decimals() digits after the point.
 * @param id The setting.
 * @param buf Where the text goes.
 * @param n The size of @p buf in bytes.
 * @return @p buf; "" for a NULL @p buf or an @p n of 0.
 */
const char *settings_value_text(setting_id_t id, char *buf, size_t n);
/**
 * @brief Render @p value as settings_value_text() renders the value of @p id.
 * @param id The setting.
 * @param value The value.
 * @param buf Where the text goes.
 * @param n Its size.
 * @return @p buf; "" for no buffer.
 */
const char *settings_value_text_of(setting_id_t id, float value, char *buf,
                                   size_t n);

/**
 * @brief The category a string is set in on SETUP.
 * @param id  The string.
 * @return The category, or SET_CAT_COUNT for one set elsewhere: the DUT name,
 *         on SERVO > SETTINGS.
 */
setting_cat_t settings_text_category(setting_text_id_t id);
/**
 * @brief The strings of @p cat, in id order.
 * @param cat  The category.
 * @param out  Receives the ids, up to @p max of them; may be NULL.
 * @param max  Capacity of @p out.
 * @return How many strings the category has.
 */
int settings_texts_in_category(setting_cat_t cat, setting_text_id_t *out,
                               int max);

/**
 * @brief A string setting.
 * @param id  The string.
 * @return The text; "" for an id out of range.  Never NULL.
 */
const char *settings_text(setting_text_id_t id);
/**
 * @brief A string's NVS key, 15 characters or fewer.
 * @param id  The string.
 * @return The key; NULL for an id out of range.
 */
const char *settings_text_key(setting_text_id_t id);
/**
 * @brief Set a string, cut to SETTINGS_TEXT_MAX - 1 characters; an edit like
 * any other.
 * @param id    The string.
 * @param text  The text.
 */
void settings_set_text(setting_text_id_t id, const char *text);

/**
 * @brief Erases one key from the medium.
 * @param key The key.
 * @return Whether the medium held it and it is gone now.
 */
typedef bool (*settings_forget_fn)(const char *key);

/**
 * @brief Hand each retired key to @p forget: keys an earlier build wrote
 *        and no build reads, such as `imu_addr`, the HC-05's address,
 *        whose binding is not carried over to `imu_ble`.
 *        The panel's NVS store does this at its first call.
 * @param forget Erases one key; NULL erases none.
 * @return The keys @p forget erased.
 */
int settings_forget_retired(settings_forget_fn forget);

/**
 * @brief A retired key by index.
 * @param i The index, from 0.
 * @return The key, NULL past the last.
 */
const char *settings_retired_key(int i);

#ifdef ESP_PLATFORM
/**
 * @brief NVS-backed store; pass to settings_set_store() before
 * settings_init().
 * @return The store.
 */
const settings_store_t *settings_nvs_store(void);
#endif

/**
 * @brief Whether the values in force differ from the stored copy, or the copy
 * is not known to be the medium.
 *
 * True after a refused save until a save succeeds.  An edit back to the
 * stored value leaves it false.
 *
 * @return true when there is something to write.
 */
bool settings_dirty(void);

/* ------------------------------------------------------ the stored copy */

/*
 * The values and strings the medium holds, as far as the panel knows: the
 * ones settings_init() loaded (the defaults where the store had none), or
 * the ones the last successful settings_save() wrote.  A refused save leaves
 * the copy as it was.
 */

/**
 * @brief The stored value of @p id.
 * @param id  The setting.
 * @return The value; 0 for an id out of range.
 */
float settings_stored(setting_id_t id);
/**
 * @brief The stored string of @p id.
 * @param id  The string.
 * @return The text; "" for an id out of range.  Never NULL.
 */
const char *settings_stored_text(setting_text_id_t id);
/**
 * @brief Whether the value of @p id in force differs from its stored value.
 * @param id  The setting.
 * @return true when it differs.
 */
bool settings_differs(setting_id_t id);
/**
 * @brief Whether the string of @p id differs from its stored string.
 * @param id  The string.
 * @return true when it differs.
 */
bool settings_text_differs(setting_text_id_t id);
/**
 * @brief How many values and strings differ from their stored ones.
 * @return The count.
 */
int settings_changes(void);

/**
 * @brief Put every changed value and string back to its stored one, by
 * settings_set() and settings_set_text(): the observer hears each value
 * that moves as it hears an edit by hand, in schema order.  A held value is
 * left, as settings_set() leaves it; a derived one follows its source.
 * After a refused save, when the medium can hold part of that write, it
 * also asks for a save (settings_request_save()), so a restart loads the
 * values put back.
 *
 * @return How many values and strings still differ: the held ones.
 */
int settings_discard(void);

/**
 * @brief Write the values through the store and clear the dirty flag.
 *
 * If there is no store, or the store refused, the values stay dirty, the
 * request is answered so the button offers the write again, and
 * settings_save_failed() stands until the next successful save or the next
 * edit.
 *
 * @return true if the store took them.
 */
bool settings_save(void);

/**
 * @brief Whether the last attempt failed and nothing has been written since.
 * @return true after a failed save, until a save succeeds or a value is
 *         edited.
 */
bool settings_save_failed(void);

/* ------------------------------------------------------- asking to save */

/**
 * Ask for the values to be written, without writing them.
 *
 * A save is a flash write, and on the panel a flash write disables the cache
 * and stalls both cores: the task that beats the safety line does not run
 * for its duration. So the operator says when they want it kept and the
 * application says when that is survivable, which is the same split the
 * IOmcu's own store already makes.
 */
void settings_request_save(void);

/**
 * @brief Whether a save has been asked for and not yet taken.
 * @return true while one stands.
 */
bool settings_save_asked(void);

/**
 * @brief Take the save if one was asked for and @p safe says now will do.
 *
 * Call it wherever the application knows the bench is idle -- an armed bench
 * is not the moment to stop both cores.
 *
 * @param safe  true when a flash write is survivable now.
 * @return true when it wrote.
 */
bool settings_save_tick(bool safe);

/** Forget a request that has not been taken. */
void settings_cancel_save(void);

#ifdef __cplusplus
}
#endif
