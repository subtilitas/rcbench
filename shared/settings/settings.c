/*
 * The settings, as a schema the screen draws rather than a screen per setting.
 *
 * Every setting is one row in a table -- its type, its range, its step, its
 * default -- and the settings screen renders whatever the table says, so
 * adding a setting is a row and never new UI code.  The values live behind an
 * injectable store (NVS, non-volatile storage, on the panel; memory in a
 * test) and every change goes through one observer, which is the single
 * place a changed value reaches the rest of the bench.
 *
 * A stored value is never trusted to match the schema.  A word read back from
 * flash can be off the declared grid or outside the range (written by another
 * build, stale, or corrupt), so a value is coerced onto the step and clamped
 * into range on the way out, rather than cycling round or being handed to a
 * screen that assumes it is valid.
 *
 * SPDX-License-Identifier: MIT
 */

#include "settings.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static const char *const k_telem_src[] = {
    "SIMULATED", "BLHELI SERIAL", "DSHOT TELEM", "VESC", "SENSORS",
};
static const char *const k_out_proto[] = { "SERVO PWM", "DSHOT300", "DSHOT600" };
static const char *const k_theme[]     = { "DARK", "LIGHT" };
static const char *const k_language[]  = { "ENGLISH", "DEUTSCH" };
static const char *const k_units[]     = { "METRIC", "IMPERIAL" };
static const char *const k_ina_addr[]  = { "0x40", "0x41", "0x44", "0x45" };

#define ENUM_OPTS(a) .options = (a), .option_count = (uint8_t)(sizeof(a) / sizeof((a)[0]))

/*
 * One row per setting.  Ranges are the physical ones: a pack cannot have zero
 * cells, a servo pulse below 800 us is not a pulse, and an ESC (electronic
 * speed controller) that ramps faster than 300 %/s is not ramping.
 */
static const setting_def_t k_defs[SETTING_COUNT] = {
    [SET_PACK_CELLS] = {
        "pack_cells", "Cells", "Series count in the pack", "S",
        SET_CAT_ESC, SET_TYPE_INT, 1, 14, 1, 6, NULL, 0 },
    [SET_PACK_MAH] = {
        "pack_mah", "Capacity", "Used for the remaining-charge estimate", "mAh",
        SET_CAT_ESC, SET_TYPE_INT, 0, 30000, 100, 5000, NULL, 0 },
    [SET_PACK_MOHM] = {
        "pack_mohm", "Internal R", "Whole pack, for the sag model", "mOhm",
        SET_CAT_ESC, SET_TYPE_FLOAT, 0, 200, 1, 18, NULL, 0 },
    [SET_MOTOR_POLES] = {
        "motor_poles", "Motor poles", "eRPM divided by poles/2 gives RPM", "",
        SET_CAT_ESC, SET_TYPE_INT, 2, 42, 2, 14, NULL, 0 },
    [SET_MOTOR_KV] = {
        "motor_kv", "Rated kV",
        "Used when the ESC reports none; 0 shows the field empty", "rpm/V",
        SET_CAT_ESC, SET_TYPE_INT, 0, 10000, 10, 0, NULL, 0 },
    [SET_TELEM_SRC] = {
        "telem_src", "Telemetry", "Where V, A and RPM come from", "",
        SET_CAT_ESC, SET_TYPE_ENUM, 0, 0, 1, 0, ENUM_OPTS(k_telem_src) },
    [SET_TELEM_HZ] = {
        "telem_hz", "Sample rate", "Trace is 762 wide; 20 Hz is ~38 s", "Hz",
        SET_CAT_ESC, SET_TYPE_INT, 5, 100, 5, 20, NULL, 0 },
    [SET_OUT_PROTO] = {
        "out_proto", "Output", "Throttle signal to the ESC", "",
        SET_CAT_ESC, SET_TYPE_ENUM, 0, 0, 1, 0, ENUM_OPTS(k_out_proto) },
    [SET_OUT_PIN] = {
        "out_pin", "Output pin", "-1 disables the throttle output", "GPIO",
        SET_CAT_ESC, SET_TYPE_INT, -1, 48, 1, 6, NULL, 0 },
    [SET_OUT_MIN_US] = {
        "out_min_us", "Idle pulse", "Width at 0 % throttle", "us",
        SET_CAT_ESC, SET_TYPE_INT, 800, 1600, 10, 1000, NULL, 0 },
    [SET_OUT_MAX_US] = {
        "out_max_us", "Full pulse", "Width at 100 % throttle", "us",
        SET_CAT_ESC, SET_TYPE_INT, 1400, 2400, 10, 2000, NULL, 0 },
    [SET_OUT_RAMP] = {
        "out_ramp", "Ramp limit", "How fast the output chases the command", "%/s",
        SET_CAT_ESC, SET_TYPE_INT, 5, 300, 5, 55, NULL, 0 },

    [SET_THEME] = {
        "theme", "Theme", "", "",
        SET_CAT_APP, SET_TYPE_ENUM, 0, 0, 1, 0, ENUM_OPTS(k_theme) },
    [SET_BRIGHTNESS] = {
        "brightness", "Brightness", "Scales the palette; see help on backlight", "%",
        SET_CAT_APP, SET_TYPE_INT, 20, 100, 5, 100, NULL, 0 },
    [SET_CONTRAST] = {
        "contrast", "Contrast", "Pushes colours away from mid grey", "%",
        SET_CAT_APP, SET_TYPE_INT, 60, 140, 5, 100, NULL, 0 },
    [SET_BACKLIGHT] = {
        "backlight", "Backlight", "This board wires it as on/off only", "",
        SET_CAT_APP, SET_TYPE_BOOL, 0, 1, 1, 1, NULL, 0 },
    [SET_LANGUAGE] = {
        "language", "Language", "", "",
        SET_CAT_APP, SET_TYPE_ENUM, 0, 0, 1, 0, ENUM_OPTS(k_language) },
    [SET_UNITS] = {
        "units", "Units", "", "",
        SET_CAT_APP, SET_TYPE_ENUM, 0, 0, 1, 0, ENUM_OPTS(k_units) },
    [SET_DIM_AFTER] = {
        "dim_after", "Dim after", "0 keeps the screen at full brightness", "min",
        SET_CAT_APP, SET_TYPE_INT, 0, 60, 5, 0, NULL, 0 },

    [SET_INA228_EN] = {
        "ina228_en", "INA228", "I2C current and voltage monitor", "",
        SET_CAT_IFACE, SET_TYPE_BOOL, 0, 1, 1, 0, NULL, 0 },
    [SET_INA228_ADDR] = {
        "ina228_addr", "INA228 address", "Set by its A0 and A1 pins", "",
        SET_CAT_IFACE, SET_TYPE_ENUM, 0, 0, 1, 0, ENUM_OPTS(k_ina_addr) },
    [SET_INA228_SHUNT] = {
        "ina228_shunt", "Shunt", "Resistance the INA228 measures across", "mOhm",
        SET_CAT_IFACE, SET_TYPE_FLOAT, 0.1f, 20, 0.1f, 0.5f, NULL, 0 },
    [SET_VIBE_EN] = {
        "vibe_en", "Vibration", "Accelerometer for the balancing mode", "",
        SET_CAT_IFACE, SET_TYPE_BOOL, 0, 1, 1, 0, NULL, 0 },
    [SET_OPTICAL_EN] = {
        "optical_en", "Optical tacho", "Once per revolution, gives the phase", "",
        SET_CAT_IFACE, SET_TYPE_BOOL, 0, 1, 1, 0, NULL, 0 },
    [SET_OPTICAL_PIN] = {
        "optical_pin", "Tacho pin", "-1 until one is wired", "GPIO",
        SET_CAT_IFACE, SET_TYPE_INT, -1, 48, 1, -1, NULL, 0 },
    [SET_I2C_KHZ] = {
        "i2c_khz", "I2C speed", "Shared with the touch controller", "kHz",
        SET_CAT_IFACE, SET_TYPE_INT, 100, 400, 100, 400, NULL, 0 },
};

static const char *const k_cat_names[SET_CAT_COUNT] = {
    "ESC / BENCH", "APPLICATION", "INTERFACES",
};

static struct {
    float values[SETTING_COUNT];
    bool  dirty;
    const settings_store_t *store;
    settings_observer_fn observer;
} s;

static float clampf(float v, float lo, float hi)
{
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

const setting_def_t *settings_def(setting_id_t id)
{
    if (id < 0 || id >= SETTING_COUNT) {
        return NULL;
    }
    return &k_defs[id];
}

const char *settings_category_name(setting_cat_t cat)
{
    return (cat >= 0 && cat < SET_CAT_COUNT) ? k_cat_names[cat] : "";
}

/* A value that arrives off the declared grid (a stale NVS word, a schema
 * whose step differs) otherwise stays off it: settings_adjust() adds step to
 * whatever is already there. */
static float snap_to_step(const setting_def_t *d, float v)
{
    if (d->step <= 0.0f) {
        return v;
    }
    return d->min + roundf((v - d->min) / d->step) * d->step;
}

static float coerce(const setting_def_t *d, float v)
{
    switch (d->type) {
    case SET_TYPE_BOOL:
        return (v != 0.0f) ? 1.0f : 0.0f;
    case SET_TYPE_ENUM: {
        int n = d->option_count ? d->option_count : 1;
        int i = (int)lrintf(v);
        /* Out of range means a corrupt or stale stored word, not "cycle
         * round": wrapping turns it into a different option that looks
         * deliberate.  settings_adjust() does its own modulo, so cycling
         * works there. */
        return (i >= 0 && i < n) ? (float)i : d->def;
    }
    case SET_TYPE_INT:
        return clampf(snap_to_step(d, (float)lrintf(v)), d->min, d->max);
    case SET_TYPE_FLOAT:
    default:
        return clampf(snap_to_step(d, v), d->min, d->max);
    }
}

/*
 * Resetting changes values, and the observer is the only path a changed value
 * has to the hardware.  Without the notification RESET CATEGORY restores the
 * pulse range and ramp limit on screen while the ESC keeps receiving the old
 * ones: the screen and the output disagree with nothing to say so.
 */
static void notify_changed(int id, float before)
{
    if (s.observer && s.values[id] != before) {
        s.observer((setting_id_t)id);
    }
}

void settings_reset(setting_cat_t cat)
{
    for (int i = 0; i < SETTING_COUNT; ++i) {
        if (k_defs[i].cat == cat) {
            float before = s.values[i];
            s.values[i] = k_defs[i].def;
            notify_changed(i, before);
        }
    }
    s.dirty = true;
}

void settings_reset_all(void)
{
    for (int i = 0; i < SETTING_COUNT; ++i) {
        float before = s.values[i];
        s.values[i] = k_defs[i].def;
        notify_changed(i, before);
    }
    s.dirty = true;
}

void settings_set_store(const settings_store_t *store)
{
    s.store = store;
}

void settings_set_observer(settings_observer_fn fn)
{
    s.observer = fn;
}

void settings_init(void)
{
    settings_reset_all();
    if (s.store && s.store->load) {
        if (s.store->load(s.values, SETTING_COUNT)) {
            /* Whatever came back has to satisfy the schema: a stored value
             * from an older build may be out of today's range. */
            for (int i = 0; i < SETTING_COUNT; ++i) {
                s.values[i] = coerce(&k_defs[i], s.values[i]);
            }
        }
    }
    s.dirty = false;

    if (s.observer) {
        for (int i = 0; i < SETTING_COUNT; ++i) {
            s.observer((setting_id_t)i);
        }
    }
}

float settings_get(setting_id_t id)
{
    return (id >= 0 && id < SETTING_COUNT) ? s.values[id] : 0.0f;
}

int settings_get_int(setting_id_t id)
{
    return (int)lrintf(settings_get(id));
}

bool settings_get_bool(setting_id_t id)
{
    return settings_get(id) != 0.0f;
}

void settings_set(setting_id_t id, float value)
{
    const setting_def_t *d = settings_def(id);
    if (!d) {
        return;
    }
    float v = coerce(d, value);
    if (v == s.values[id]) {
        return;
    }
    s.values[id] = v;
    s.dirty = true;
    if (s.observer) {
        s.observer(id);
    }
}

void settings_adjust(setting_id_t id, int steps)
{
    const setting_def_t *d = settings_def(id);
    if (!d || steps == 0) {
        return;
    }
    if (d->type == SET_TYPE_BOOL || d->type == SET_TYPE_ENUM) {
        /* Cycling is the only sensible interpretation of "+1" on a list. */
        int n = (d->type == SET_TYPE_BOOL) ? 2
                                           : (d->option_count ? d->option_count : 1);
        int cur = (int)lrintf(s.values[id]);
        int next = ((cur + steps) % n + n) % n;
        settings_set(id, (float)next);
        return;
    }
    float step = (d->step > 0.0f) ? d->step : 1.0f;
    settings_set(id, s.values[id] + step * (float)steps);
}

int settings_in_category(setting_cat_t cat, setting_id_t *out, int max)
{
    int n = 0;
    for (int i = 0; i < SETTING_COUNT; ++i) {
        if (k_defs[i].cat != cat) {
            continue;
        }
        if (out && n < max) {
            out[n] = (setting_id_t)i;
        }
        ++n;
    }
    return n;
}

const char *settings_value_text(setting_id_t id, char *buf, size_t n)
{
    const setting_def_t *d = settings_def(id);
    if (!buf || n == 0) {
        return "";
    }
    if (!d) {
        snprintf(buf, n, "--");
        return buf;
    }
    float v = s.values[id];
    switch (d->type) {
    case SET_TYPE_BOOL:
        snprintf(buf, n, "%s", (v != 0.0f) ? "ON" : "OFF");
        break;
    case SET_TYPE_ENUM: {
        int i = (int)lrintf(v);
        if (d->options && i >= 0 && i < d->option_count) {
            snprintf(buf, n, "%s", d->options[i]);
        } else {
            snprintf(buf, n, "%d", i);
        }
        break;
    }
    case SET_TYPE_FLOAT:
        snprintf(buf, n, "%.1f", (double)v);
        break;
    case SET_TYPE_INT:
    default:
        snprintf(buf, n, "%d", (int)lrintf(v));
        break;
    }
    return buf;
}

bool settings_dirty(void)
{
    return s.dirty;
}

void settings_save(void)
{
    if (s.store && s.store->save) {
        s.store->save(s.values, SETTING_COUNT);
    }
    s.dirty = false;
}
