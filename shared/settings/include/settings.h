/*
 * Device settings: a typed schema, a value per entry, and a persistence seam.
 *
 * The schema carries range, step, unit and help text, so the settings screen
 * is generic -- it renders whatever the table says rather than hard-coding a
 * control per option.  Adding a setting is one table row.
 *
 * Pure C.  Persistence is a pair of function pointers, so the host tests use
 * memory and the firmware uses NVS without the model knowing the difference.
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
    SET_CAT_APP,         /**< theme, brightness, language, units      */
    SET_CAT_IFACE,       /**< the sensors that plug into the bench    */
    SET_CAT_COUNT
} setting_cat_t;

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
    SET_TELEM_SRC,
    SET_TELEM_HZ,
    SET_OUT_PROTO,
    SET_OUT_PIN,
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
    /* --- interfaces ---------------------------------------------------- */
    SET_INA228_EN,
    SET_INA228_ADDR,
    SET_INA228_SHUNT,
    SET_VIBE_EN,
    SET_OPTICAL_EN,
    SET_OPTICAL_PIN,
    SET_I2C_KHZ,

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

typedef struct {
    bool (*load)(float *values, int count);
    void (*save)(const float *values, int count);
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

#ifdef ESP_PLATFORM
/** NVS-backed store; pass to settings_set_store() before settings_init(). */
const settings_store_t *settings_nvs_store(void);
#endif

bool settings_dirty(void);
/** Persist through the store and clear the dirty flag. */
void settings_save(void);

#ifdef __cplusplus
}
#endif
