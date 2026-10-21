/*
 * Device settings: a typed schema, a value per entry, and a persistence seam.
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

typedef enum {
    SET_CAT_APP = 0,     /**< theme, brightness, language, units, plot  */
    SET_CAT_IFACE,       /**< the sensors that plug into the bench    */
    SET_CAT_CARD,        /**< the SD card: which logs are written     */
    SET_CAT_SENSORS,     /**< the INA3221 parts: fitted, shunts, what
                              each channel watches (settings_sensors.h) */
    SET_CAT_ESC,         /**< pack, motor, telemetry, throttle output */
    SET_CAT_SUPPLY,      /**< the SUPPLY screen's limits and start    */
    SET_CAT_SERVO,       /**< the SERVO screen's test and its limits  */
    SET_CAT_STICK,       /**< stick programming: supply and timing    */
    SET_CAT_UPDATE,      /**< the release reminder and the releases a
                              check offers, on SETUP's UPDATES          */
    SET_CAT_COUNT
} setting_cat_t;

/**
 * The categories SETUP opens from its tiles, which come first.  SET_CAT_ESC
 * is edited on MOTOR & ESC > SETTINGS, SET_CAT_SUPPLY, SET_CAT_SERVO and
 * SET_CAT_STICK on their own screens, beside what they set, and
 * SET_CAT_UPDATE on the UPDATES screen a SETUP tile opens.
 */
#define SET_CAT_SETUP_COUNT SET_CAT_ESC

typedef enum {
    SET_TYPE_INT = 0,
    SET_TYPE_FLOAT,
    SET_TYPE_BOOL,
    SET_TYPE_ENUM,
} setting_type_t;

/* The INA3221 parts SETUP > SENSORS sets up: four, at 0x40 to 0x43, three
 * channels each, and the rows a channel has (settings_sensors.h). */
#define SET_INA_PARTS    4
#define SET_INA_CHANNELS 12
#define SET_INA_CH_ROWS  2

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
    SET_LANGUAGE,
    SET_UNITS,
    SET_DIM_AFTER,
    SET_PLOT_PAN,
    SET_KNOB_EN,
    SET_KNOB_SCALE,
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
    SET_ENC_CENTRE,
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
    /* Part a's FITTED, then channel by channel (part a's CHn) WATCHES and
     * SHUNT, SET_INA_W_<a><n> for part a's CHn: settings_sensors.h names
     * them by channel number. */
    SET_INA_FIT_0, SET_INA_FIT_1, SET_INA_FIT_2, SET_INA_FIT_3,
    SET_INA_W_01, SET_INA_SH_01,
    SET_INA_W_02, SET_INA_SH_02,
    SET_INA_W_03, SET_INA_SH_03,
    SET_INA_W_11, SET_INA_SH_11,
    SET_INA_W_12, SET_INA_SH_12,
    SET_INA_W_13, SET_INA_SH_13,
    SET_INA_W_21, SET_INA_SH_21,
    SET_INA_W_22, SET_INA_SH_22,
    SET_INA_W_23, SET_INA_SH_23,
    SET_INA_W_31, SET_INA_SH_31,
    SET_INA_W_32, SET_INA_SH_32,
    SET_INA_W_33, SET_INA_SH_33,
    SET_INA_FIT_LAST = SET_INA_FIT_3,
    SET_INA_CH_0     = SET_INA_W_01,       /**< channel 0's first row     */
    SET_INA_CH_LAST  = SET_INA_SH_33,
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
    setting_cat_t      cat;
    setting_type_t     type;
    float              min;
    float              max;
    float              step;
    float              def;
    const char *const *options;  /**< SET_TYPE_ENUM only */
    uint8_t            option_count;
    /** A number whose values between 0 and this are not taken: 0 is
     *  "off" and the next value up is this one.  0 for none. */
    float              off_below;
} setting_def_t;

/** Called after any value actually changes, so the app can act on it. */
typedef void (*settings_observer_fn)(setting_id_t id);

/*
 * A few short strings beside the numbers, kept by the same store and saved
 * by the same request.  A string holds up to SETTINGS_TEXT_MAX - 1
 * characters.
 */
typedef enum {
    SET_TEXT_DUT_NAME = 0,   /**< the device under test, for the report */
    SET_TEXT_LOG_AUTO_DIR,   /**< AUTO LOG FOLDER: the automatic logs'  */
    SET_TEXT_LOG_USER_DIR,   /**< USER LOG FOLDER: the tests' files     */
    SETTING_TEXT_COUNT
} setting_text_id_t;

#define SETTINGS_TEXT_MAX 24

/** Called after a string actually changes, as settings_observer_fn is
 *  for a value: an edit, a discard, a reset and the load at start-up. */
typedef void (*settings_text_observer_fn)(setting_text_id_t id);

typedef struct {
    bool (*load)(float *values, int count);
    /*
     * True only if every value reached the medium.  A store that cannot
     * say so cannot be believed: the model keeps the edit and the screen
     * goes on offering the write, rather than reporting a save that did
     * not happen and refusing the retry that would notice.
     */
    bool (*save)(const float *values, int count);
    /* The strings, under the same rules; NULL in a store that keeps none,
     * which then keeps them for the session only. */
    bool (*load_text)(char (*texts)[SETTINGS_TEXT_MAX], int count);
    bool (*save_text)(const char (*texts)[SETTINGS_TEXT_MAX], int count);
} settings_store_t;

/** Reset to defaults, then load from the store if one is set. */
void settings_init(void);

void settings_set_store(const settings_store_t *store);
void settings_set_observer(settings_observer_fn fn);
void settings_set_text_observer(settings_text_observer_fn fn);

/** Whether a setting is held: something in use depends on the value as it
 *  is. */
typedef bool (*settings_hold_fn)(setting_id_t id);
/** Who says which settings are held, or NULL for none.  Asked at every
 *  change, so a hold begins and ends with what it names. */
void settings_set_hold(settings_hold_fn fn);
/** Whether @p id is held now: settings_set(), settings_adjust(),
 *  settings_reset() and settings_reset_all() leave it, and SETUP draws its
 *  row without keys.  The servo tester's target and the port's pulse while
 *  the servo programmer holds the port (kst_panel.h). */
bool settings_held(setting_id_t id);

const setting_def_t *settings_def(setting_id_t id);

/** Whether @p id follows another setting: settings_set() and
 *  settings_adjust() leave it, and SETUP draws its row without keys.
 *  Sensor SCL, which is the GPIO after Sensor SDA. */
bool settings_derived(setting_id_t id);
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

float settings_get(setting_id_t id);
int   settings_get_int(setting_id_t id);
bool  settings_get_bool(setting_id_t id);

/** Clamped to the schema; enums wrap. Fires the observer when it changes.
 *  A derived setting and a held one are left as they are. */
void settings_set(setting_id_t id, float value);
/** Step by @p steps increments.  A number stops at its range's ends.
 *  Booleans and enums cycle (settings_cycles()), except the INA228 and
 *  INA3221 addresses, which stop at the first and the last of their list.
 *  Sensor SDA steps from one value settings_sense_sda_valid() takes to the
 *  next and stops at the first and the last.  PD mini TX and PD mini RX
 *  step over the GPIO the other holds, so a step never sets them to one
 *  pin; with that GPIO at the end of the range the step stays where it
 *  is.  -1, not wired, is a step for both. */
void settings_adjust(setting_id_t id, int steps);

/** Whether settings_adjust() wraps @p id round from one end to the other:
 *  a switch or a choice, drawn with < and > keys.  False for a number and
 *  for the monitors' addresses, which stop at their ends and are drawn
 *  with - and + keys; false for an id out of range. */
bool settings_cycles(setting_id_t id);

/**
 * Whether Sensor SDA takes @p gpio: -1, the bus's pins not set, or an
 * IOmcu GPIO the sensor bus can have its SDA on with SCL on the GPIO
 * after it.  That is one I2C block's pair, link_sn_pins_pair() -- the rule
 * the IOmcu's SENSE page judges a write by -- on two pins the
 * RP2350-CAN brings out and does not reserve, outbind_reserved_mask(): the
 * mask the IOmcu reserves at boot.  0, 4, 6, 14, 16, 18, 20 and 26.
 * A pin an output, the supply or the phase tap holds is not known here;
 * the page refuses it and SETUP marks the row.
 */
bool settings_sense_sda_valid(int gpio);

/**
 * Whether PD mini TX and PD mini RX hold one GPIO, 0 or above.  The steps
 * never set that; a stored set-up can hold it.  The panel writes such a
 * wiring to the SUPPLY page disabled (supply_link.c), so the PD mini
 * stays off, and SETUP marks both rows.
 */
bool settings_pdmini_same_pin(void);

/** Restore one category, or all of them, to the schema defaults: its
 *  values and its strings (settings_text_category()). */
void settings_reset(setting_cat_t cat);
void settings_reset_all(void);

/** Ids in a category, in schema order.  Returns how many there are. */
int settings_in_category(setting_cat_t cat, setting_id_t *out, int max);

/** Render the value as it should appear on screen, unit excluded. */
const char *settings_value_text(setting_id_t id, char *buf, size_t n);
/** Render @p value as settings_value_text() renders the value of @p id. */
const char *settings_value_text_of(setting_id_t id, float value, char *buf,
                                   size_t n);

/** The category a string is set in on SETUP, or SET_CAT_COUNT for one
 *  set elsewhere: the DUT name, on SERVO > SETTINGS. */
setting_cat_t settings_text_category(setting_text_id_t id);
/** The strings of @p cat, in id order.  Returns how many there are. */
int settings_texts_in_category(setting_cat_t cat, setting_text_id_t *out,
                               int max);

/** A string setting.  Never NULL. */
const char *settings_text(setting_text_id_t id);
/** Its NVS key, 15 characters or fewer.  NULL for an id out of range. */
const char *settings_text_key(setting_text_id_t id);
/** Set it, cut to SETTINGS_TEXT_MAX - 1 characters; an edit like any other. */
void settings_set_text(setting_text_id_t id, const char *text);

#ifdef ESP_PLATFORM
/** NVS-backed store; pass to settings_set_store() before settings_init(). */
const settings_store_t *settings_nvs_store(void);
#endif

/**
 * Whether the values in force differ from the stored copy, or the copy is
 * not known to be the medium: true after a refused save until a save
 * succeeds.  An edit back to the stored value leaves it false.
 */
bool settings_dirty(void);

/* ------------------------------------------------------ the stored copy */

/*
 * The values and strings the medium holds, as far as the panel knows: the
 * ones settings_init() loaded (the defaults where the store had none), or
 * the ones the last successful settings_save() wrote.  A refused save leaves
 * the copy as it was.
 */

/** The stored value of @p id; 0 for an id out of range. */
float settings_stored(setting_id_t id);
/** The stored string of @p id; "" for an id out of range.  Never NULL. */
const char *settings_stored_text(setting_text_id_t id);
/** Whether the value of @p id in force differs from its stored value. */
bool settings_differs(setting_id_t id);
/** Whether the string of @p id differs from its stored string. */
bool settings_text_differs(setting_text_id_t id);
/** How many values and strings differ from their stored ones. */
int settings_changes(void);

/**
 * Put every changed value and string back to its stored one, by
 * settings_set() and settings_set_text(): the observer hears each value
 * that moves as it hears an edit by hand, in schema order.  A held value is
 * left, as settings_set() leaves it; a derived one follows its source.
 * After a refused save, when the medium can hold part of that write, it
 * also asks for a save (settings_request_save()), so a restart loads the
 * values put back.  Returns how many values and strings still differ: the
 * held ones.
 */
int settings_discard(void);

/** Persist through the store and clear the dirty flag. */
/*
 * Write the values through the store.  True if the store took them.  False
 * if there is no store, or the store refused: the values stay dirty, the
 * request is answered so the button offers the write again, and
 * settings_save_failed() stands until the next successful save or the next
 * edit.
 */
bool settings_save(void);

/** Whether the last attempt failed and nothing has been written since. */
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

/** Whether a save has been asked for and not yet taken. */
bool settings_save_asked(void);

/**
 * Take the save if one was asked for and @p safe says now will do.
 *
 * Returns true when it wrote. Call it wherever the application knows the
 * bench is idle -- an armed bench is not the moment to stop both cores.
 */
bool settings_save_tick(bool safe);

/** Forget a request that has not been taken. */
void settings_cancel_save(void);

#ifdef __cplusplus
}
#endif
