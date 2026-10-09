/*
 * Device settings: a typed schema, a value per entry, and a persistence seam.
 *
 * The schema carries range, step, unit and help text, so the settings screen
 * is generic -- it renders whatever the table says rather than hard-coding a
 * control per option.  Adding a setting is one table row.
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
    SET_CAT_ESC = 0,     /**< pack, motor, telemetry, throttle output */
    SET_CAT_APP,         /**< theme, brightness, language, units, plot  */
    SET_CAT_IFACE,       /**< the sensors that plug into the bench    */
    SET_CAT_SUPPLY,      /**< the SUPPLY screen's limits and start    */
    SET_CAT_SERVO,       /**< the SERVO screen's test and its limits  */
    SET_CAT_STICK,       /**< stick programming: supply and timing    */
    SET_CAT_COUNT
} setting_cat_t;

/**
 * The categories SETUP lists.  SET_CAT_SUPPLY, SET_CAT_SERVO and
 * SET_CAT_STICK are edited on their own screens, beside what they set, and
 * SETUP has no room for a fourth category button.
 */
#define SET_CAT_SETUP_COUNT SET_CAT_SUPPLY

typedef enum {
    SET_TYPE_INT = 0,
    SET_TYPE_FLOAT,
    SET_TYPE_BOOL,
    SET_TYPE_ENUM,
} setting_type_t;

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
    SET_BACKLIGHT,
    SET_LANGUAGE,
    SET_UNITS,
    SET_DIM_AFTER,
    SET_PLOT_PAN,
    /* --- interfaces ---------------------------------------------------- */
    SET_INA228_EN,
    SET_INA228_ADDR,
    SET_INA228_UOHM,
    SET_INA228_MAX_A,
    SET_INA3221_EN,
    SET_INA3221_ADDR,
    SET_INA3221_MOHM,
    SET_INA3221_CH,
    SET_SENSE_SDA,
    SET_SENSE_SCL,
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

    SETTING_COUNT
} setting_id_t;

typedef struct {
    const char        *key;      /**< NVS key, 15 chars or fewer   */
    const char        *label;
    const char        *help;
    const char        *unit;
    setting_cat_t      cat;
    setting_type_t     type;
    float              min;
    float              max;
    float              step;
    float              def;
    const char *const *options;  /**< SET_TYPE_ENUM only */
    uint8_t            option_count;
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
    SETTING_TEXT_COUNT
} setting_text_id_t;

#define SETTINGS_TEXT_MAX 24

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

const setting_def_t *settings_def(setting_id_t id);
const char *settings_category_name(setting_cat_t cat);

float settings_get(setting_id_t id);
int   settings_get_int(setting_id_t id);
bool  settings_get_bool(setting_id_t id);

/** Clamped to the schema; enums wrap. Fires the observer when it changes. */
void settings_set(setting_id_t id, float value);
/** Step by @p steps increments. Booleans and enums cycle. */
void settings_adjust(setting_id_t id, int steps);

/** Restore one category, or all of them, to the schema defaults. */
void settings_reset(setting_cat_t cat);
void settings_reset_all(void);

/** Ids in a category, in schema order.  Returns how many there are. */
int settings_in_category(setting_cat_t cat, setting_id_t *out, int max);

/** Render the value as it should appear on screen, unit excluded. */
const char *settings_value_text(setting_id_t id, char *buf, size_t n);

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

bool settings_dirty(void);
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
 * coprocessor's own store already makes.
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
