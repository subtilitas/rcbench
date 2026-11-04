/**
 * @file
 * @brief The settings, as a schema the screen draws rather than a screen per
 *        setting.
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

#include "link_pages.h"
#include "out_bind.h"
#include "settings_int.h"
#include "settings_sensors.h"

/* A row of the schema, the SENSORS rows' included (settings_sensors.c). */
#define DEF(i) (*settings_def((setting_id_t)(i)))

/** @brief The categories' names in English, from ui_text.def. */
static const char *const k_cat_names[SET_CAT_COUNT] = {
/// @cond X_MACRO
#define UI_SETTING_CATEGORY(id, en, de) [id] = (en),
#include "ui_text.def"
/// @endcond
};

/* The string settings' keys, defaults and categories; NVS keys, 15
 * characters or fewer.  The log folders' defaults are CARD_AUTO_LOGS and
 * CARD_USER_LOGS (card_tree.h), which test_settings holds them to. */
static const struct {
    const char   *key;
    const char   *def;
    setting_cat_t cat;
} k_text_defs[SETTING_TEXT_COUNT] = {
    [SET_TEXT_DUT_NAME]     = { "srv_dut", "SERVO", SET_CAT_COUNT },
    [SET_TEXT_LOG_AUTO_DIR] = { "log_auto_dir", "BENCH/LOGS", SET_CAT_CARD },
    [SET_TEXT_LOG_USER_DIR] = { "log_user_dir", "USER/LOGS", SET_CAT_CARD },
    [SET_TEXT_IMU_ADDR]     = { "imu_addr", "", SET_CAT_COUNT },
    [SET_TEXT_IMU_NAME]     = { "imu_name", "", SET_CAT_COUNT },
};

static struct {
    float values[SETTING_COUNT];
    char  texts[SETTING_TEXT_COUNT][SETTINGS_TEXT_MAX];
    /*
     * What the medium holds, as far as the panel knows: the values and
     * strings settings_init() loaded, or the last successful save wrote.
     * A changed setting is one that differs from this copy.  stored_sure
     * is false after a refused save: a refusal part way leaves the keys
     * written before it, so the medium can hold a mix and the copy is no
     * longer known to be it.
     */
    float stored[SETTING_COUNT];
    char  stored_texts[SETTING_TEXT_COUNT][SETTINGS_TEXT_MAX];
    bool  stored_sure;
    bool  dirty;
    bool     save_asked;   /* asked for, not yet taken */
    bool     save_failed;  /* the last attempt did not reach the medium */
    const settings_store_t *store;
    settings_observer_fn observer;
    settings_text_observer_fn text_observer;
    settings_hold_fn hold;
    settings_armed_fn armed;   /**< says whether the bench is ARMED */
} s;

/*
 * Whether value @p i is off its stored value by a step of its grid or more.
 * A value stepped away and back lands on the grid by arithmetic, which can
 * miss the stored word by a rounding (20.999998 for 21.0), and both read
 * the same on the screen: that is the same value.  Half a step tells them
 * apart from a real step.
 */
static bool value_differs(int i)
{
    const float step = DEF(i).step;
    if (step > 0.0f) {
        return fabsf(s.values[i] - s.stored[i]) >= 0.5f * step;
    }
    return s.values[i] != s.stored[i];
}

static bool differs_from_stored(void)
{
    for (int i = 0; i < SETTING_COUNT; ++i) {
        if (value_differs(i)) {
            return true;
        }
    }
    for (int i = 0; i < SETTING_TEXT_COUNT; ++i) {
        if (strcmp(s.texts[i], s.stored_texts[i]) != 0) {
            return true;
        }
    }
    return false;
}

/*
 * Something changed.  Dirty is whether the values in force differ from the
 * stored copy: an edit back to the stored value leaves nothing to write, and
 * a write of it would spend an erase cycle on what is there.  After a
 * refused save the copy is not known to be the medium, and dirty stands
 * until a save succeeds.  A new edit is also a fresh attempt, so it retires
 * any earlier failure: that failure described values these no longer are.
 * Nothing left to write drops a save asked for: the request was for the
 * values edited back, and the next edit is not one the operator asked to
 * keep.  Called after every value an edit moves is set, a derived one too.
 */
static void mark_dirty(void)
{
    s.dirty = !s.stored_sure || differs_from_stored();
    s.save_failed = false;
    if (!s.dirty) {
        s.save_asked = false;
    }
}

/* The values in force are what the medium holds. */
static void take_stored(void)
{
    memcpy(s.stored, s.values, sizeof(s.stored));
    memcpy(s.stored_texts, s.texts, sizeof(s.stored_texts));
    s.stored_sure = true;
}

static float clampf(float v, float lo, float hi)
{
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

const setting_def_t *settings_def(setting_id_t id)
{
    if (id < 0 || id >= SETTING_COUNT) {
        return NULL;
    }
    const setting_def_t *d = settings_sensors_def(id);
    return (d != NULL) ? d : &settings_schema[id];
}

bool settings_derived(setting_id_t id)
{
    return id == SET_SENSE_SCL;
}

void settings_set_hold(settings_hold_fn fn)
{
    s.hold = fn;
}

/** @brief Install the ARMED source (settings.h). */
void settings_set_armed(settings_armed_fn fn)
{
    s.armed = fn;
}

/** @brief Whether @p id changes on a SAFE bench only (settings.h). */
bool settings_safe_only(setting_id_t id)
{
    return id == SET_SCREEN_ROT || id == SET_PDMINI_EN || id == SET_PDMINI_TX
           || id == SET_PDMINI_RX || id == SET_PDMINI_BAUD;
}

bool settings_held(setting_id_t id)
{
    if (id < 0 || id >= SETTING_COUNT) {
        return false;
    }
    if (settings_safe_only(id) && s.armed != NULL && s.armed()) {
        return true;
    }
    return s.hold != NULL && s.hold(id);
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

bool settings_sense_sda_valid(int gpio)
{
    if (gpio == -1) {
        return true;
    }
    if (gpio < 0 || gpio >= (int)OUT_MAX_PIN) {
        return false;
    }
    const unsigned sda = (unsigned)gpio;
    const uint64_t pair = ((uint64_t)1u << sda)
                          | ((uint64_t)1u << (sda + 1u));
    return link_sn_pins_pair(sda, sda + 1u)
           && (outbind_reserved_mask(OUTBIND_BOARD_PICO_HEADER) & pair) == 0u;
}

bool settings_tap_pin_valid(int gpio)
{
    if (gpio < 0 || gpio >= (int)OUT_MAX_PIN) {
        return false;
    }
    if ((gpio >= 26 && gpio <= 29) || gpio >= 40) {
        return false;                   /* the TONE page refuses ADC pins */
    }
    return (outbind_reserved_mask(OUTBIND_BOARD_PICO_HEADER)
            & ((uint64_t)1u << (unsigned)gpio)) == 0u;
}

bool settings_pdmini_same_pin(void)
{
    const int tx = settings_get_int(SET_PDMINI_TX);
    return tx >= 0 && tx == settings_get_int(SET_PDMINI_RX);
}

/* Whether the pin row @p id takes @p pin as a step: Sensor SDA a pin the
 * bus can have, PD mini TX and RX any pin but the one the other holds. */
static bool pin_takes(setting_id_t id, int pin)
{
    if (id == SET_PDMINI_TX || id == SET_PDMINI_RX) {
        const setting_id_t other =
            (id == SET_PDMINI_TX) ? SET_PDMINI_RX : SET_PDMINI_TX;
        return pin < 0 || pin != (int)lrintf(s.values[other]);
    }
    if (id == SET_TONE_PIN) {
        return settings_tap_pin_valid(pin);
    }
    return settings_sense_sda_valid(pin);
}

static float coerce(const setting_def_t *d, float v)
{
    if (d == &settings_schema[SET_SENSE_SDA]) {
        /* A stored pin the bus cannot have is a stale word, as an enum
         * out of its range is: the default, not the nearest pin. */
        const long pin = lrintf(v);
        return (pin >= (long)d->min && pin <= (long)d->max
                && settings_sense_sda_valid((int)pin))
                   ? (float)pin : d->def;
    }
    if (d == &settings_schema[SET_TONE_PIN]) {
        /* A stored pin the module does not bring out is a stale word. */
        const long pin = lrintf(v);
        return (pin >= (long)d->min && pin <= (long)d->max
                && settings_tap_pin_valid((int)pin))
                   ? (float)pin : d->def;
    }
    switch (d->type) {
    case SET_TYPE_BOOL:
        return (v != 0.0f) ? 1.0f : 0.0f;
    case SET_TYPE_ENUM: {
        int n = settings_sensors_offered(d);
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
    default: {
        const float f = clampf(snap_to_step(d, v), d->min, d->max);
        /* Below off_below only 0 is taken: the nearer of the two. */
        if (f > 0.0f && f < d->off_below) {
            return (f < 0.5f * d->off_below) ? 0.0f : d->off_below;
        }
        return f;
    }
    }
}

/* Sensor SCL from Sensor SDA: the GPIO after it, and -1 with it.  True
 * when that moved it. */
static bool derive(void)
{
    const float sda = s.values[SET_SENSE_SDA];
    const float scl = (sda < 0.0f) ? -1.0f : sda + 1.0f;
    const bool moved = s.values[SET_SENSE_SCL] != scl;
    s.values[SET_SENSE_SCL] = scl;
    return moved;
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

/* String @p i set to @p text, cut to SETTINGS_TEXT_MAX - 1 characters; the
 * text observer hears it when it moved.  Whether it moved. */
static bool text_to(int i, const char *text)
{
    char next[SETTINGS_TEXT_MAX];
    snprintf(next, sizeof(next), "%s", text);
    if (strcmp(next, s.texts[i]) == 0) {
        return false;
    }
    memcpy(s.texts[i], next, sizeof(next));
    if (s.text_observer) {
        s.text_observer((setting_text_id_t)i);
    }
    return true;
}

/**
 * @brief Setting @p i back to its default, unless it is held; its observer
 *        hears a change.
 * @param i The setting.
 * @return Whether the value moved.
 */
static bool reset_row(int i)
{
    if (settings_held((setting_id_t)i)) {
        return false;
    }
    const float before = s.values[i];
    s.values[i] = DEF(i).def;
    const bool moved = s.values[i] != before;
    notify_changed(i, before);
    return moved;
}

void settings_reset(setting_cat_t cat)
{
    /*
     * Only an actual change is an edit.  A category already holding its
     * defaults is reset to what it has, and calling that an edit would
     * retire a standing save failure while the values whose write failed
     * are still the ones in memory.
     */
    bool moved = false;
    for (int i = 0; i < SETTING_COUNT; ++i) {
        if (DEF(i).cat == cat) {
            moved = reset_row(i) || moved;
        }
    }
    for (int i = 0; i < SETTING_TEXT_COUNT; ++i) {
        if (k_text_defs[i].cat == cat && text_to(i, k_text_defs[i].def)) {
            moved = true;
        }
    }
    if (moved) {
        mark_dirty();
    }
}

void settings_reset_rows(const setting_id_t *ids, int n)
{
    bool moved = false;
    for (int k = 0; ids != NULL && k < n; ++k) {
        if (ids[k] >= 0 && ids[k] < SETTING_COUNT) {
            moved = reset_row((int)ids[k]) || moved;
        }
    }
    if (moved) {
        mark_dirty();
    }
}

/* Every value and string to its default; a held value only at start-up,
 * @p held. */
static void defaults(bool held)
{
    bool moved = false;
    for (int i = 0; i < SETTING_COUNT; ++i) {
        if (!held && settings_held((setting_id_t)i)) {
            continue;
        }
        float before = s.values[i];
        s.values[i] = DEF(i).def;
        moved = moved || (s.values[i] != before);
        notify_changed(i, before);
    }
    for (int i = 0; i < SETTING_TEXT_COUNT; ++i) {
        if (text_to(i, k_text_defs[i].def)) {
            moved = true;
        }
    }
    if (moved) {
        mark_dirty();
    }
}

void settings_reset_all(void)
{
    defaults(false);
}

void settings_set_store(const settings_store_t *store)
{
    s.store = store;
}

void settings_set_observer(settings_observer_fn fn)
{
    s.observer = fn;
}

void settings_set_text_observer(settings_text_observer_fn fn)
{
    s.text_observer = fn;
}

void settings_init(void)
{
    defaults(true);
    /* A SENSORS key the store does not hold stays NAN through the load and
     * is seeded from the old INA3221 keys after it, as a default is, at
     * every start until a save writes it (settings_sensors.h). */
    settings_sensors_unset(s.values);
    if (s.store && s.store->load) {
        if (s.store->load(s.values, SETTING_COUNT)) {
            /* Whatever came back has to satisfy the schema: a stored value
             * from an older build may be out of today's range. */
            for (int i = 0; i < SETTING_COUNT; ++i) {
                if (!isnan(s.values[i])) {
                    s.values[i] = coerce(settings_def((setting_id_t)i),
                                         s.values[i]);
                }
            }
            /* A stored Sensor SCL is not read: whatever the medium held
             * for it, it is the pin after the SDA loaded. */
            (void)derive();
        }
    }
    (void)settings_sensors_migrate(s.values);
    if (s.store && s.store->load_text) {
        (void)s.store->load_text(s.texts, SETTING_TEXT_COUNT);
        /* Terminated whatever the medium held. */
        for (int i = 0; i < SETTING_TEXT_COUNT; ++i) {
            s.texts[i][SETTINGS_TEXT_MAX - 1] = '\0';
        }
    }
    take_stored();
    s.dirty = false;
    s.save_failed = false;
    s.save_asked = false;   /* what is loaded is what is kept */

    if (s.observer) {
        for (int i = 0; i < SETTING_COUNT; ++i) {
            s.observer((setting_id_t)i);
        }
    }
    if (s.text_observer) {
        for (int i = 0; i < SETTING_TEXT_COUNT; ++i) {
            s.text_observer((setting_text_id_t)i);
        }
    }
}

/** @brief The pins a board solders, over the settings that name them:
 *         s_fix_on[id] says whether s_fix[id] stands for the value. */
static bool  s_fix_on[SETTING_COUNT];
static float s_fix[SETTING_COUNT];  /**< The fixed values. */

bool settings_fix(setting_id_t id, int value)
{
    if (id < 0 || id >= SETTING_COUNT) {
        return false;
    }
    const float was = settings_get(id);
    s_fix_on[id] = true;
    s_fix[id] = (float)value;
    return settings_get(id) != was;
}

bool settings_unfix(setting_id_t id)
{
    if (id < 0 || id >= SETTING_COUNT || !s_fix_on[id]) {
        return false;
    }
    const float was = settings_get(id);
    s_fix_on[id] = false;
    return settings_get(id) != was;
}

bool settings_fixed(setting_id_t id)
{
    return id >= 0 && id < SETTING_COUNT && s_fix_on[id];
}

float settings_get(setting_id_t id)
{
    if (id < 0 || id >= SETTING_COUNT) {
        return 0.0f;
    }
    return s_fix_on[id] ? s_fix[id] : s.values[id];
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
    if (!d || settings_derived(id) || settings_held(id)) {
        return;
    }
    float v = coerce(d, value);
    if (v == s.values[id]) {
        return;
    }
    s.values[id] = v;
    const bool scl = (id == SET_SENSE_SDA) && derive();
    mark_dirty();
    if (s.observer) {
        s.observer(id);
        if (scl) {
            s.observer(SET_SENSE_SCL);
        }
    }
}

bool settings_cycles(setting_id_t id)
{
    const setting_def_t *d = settings_def(id);
    if (d == NULL || (d->type != SET_TYPE_BOOL && d->type != SET_TYPE_ENUM)) {
        return false;
    }
    /* The monitors' addresses are numbers in a list: they stop at its
     * ends, as a number does at its range's. */
    return id != SET_INA228_ADDR && id != SET_INA3221_ADDR;
}

void settings_adjust(setting_id_t id, int steps)
{
    const setting_def_t *d = settings_def(id);
    if (!d || steps == 0 || settings_derived(id) || settings_held(id)) {
        return;
    }
    if (d->type == SET_TYPE_ENUM && !settings_cycles(id)) {
        const int last = settings_sensors_offered(d) - 1;
        const int cur  = (int)lrintf(s.values[id]);
        /* In 64 bits: a step count near INT_MAX does not overflow. */
        int64_t next = (int64_t)cur + (int64_t)steps;
        if (next < 0) {
            next = 0;
        }
        if (next > (int64_t)last) {
            next = (int64_t)last;
        }
        settings_set(id, (float)next);
        return;
    }
    if (d->type == SET_TYPE_BOOL || d->type == SET_TYPE_ENUM) {
        /* Cycling is the only sensible interpretation of "+1" on a list. */
        int n = (d->type == SET_TYPE_BOOL) ? 2 : settings_sensors_offered(d);
        int cur = (int)lrintf(s.values[id]);
        int next = ((cur + steps) % n + n) % n;
        settings_set(id, (float)next);
        return;
    }
    if (id == SET_SENSE_SDA || id == SET_PDMINI_TX || id == SET_PDMINI_RX
        || id == SET_TONE_PIN) {
        /* From one pin the row takes to the next, and no further than the
         * first and the last. */
        int pin = (int)lrintf(s.values[id]);
        const int dir = (steps > 0) ? 1 : -1;
        for (int left = (steps > 0) ? steps : -steps; left > 0; --left) {
            int next = pin + dir;
            while (next >= (int)d->min && next <= (int)d->max
                   && !pin_takes(id, next)) {
                next += dir;
            }
            if (next < (int)d->min || next > (int)d->max) {
                break;
            }
            pin = next;
        }
        settings_set(id, (float)pin);
        return;
    }
    float step = (d->step > 0.0f) ? d->step : 1.0f;
    float next = s.values[id] + step * (float)steps;
    if (d->off_below > 0.0f && next < d->off_below) {
        /* Off is the step below off_below, and off_below the step above
         * off. */
        next = (s.values[id] > 0.0f || steps < 0)
                   ? 0.0f : d->off_below + step * (float)(steps - 1);
    }
    settings_set(id, next);
}

int settings_in_category(setting_cat_t cat, setting_id_t *out, int max)
{
    int n = 0;
    for (int i = 0; i < SETTING_COUNT; ++i) {
        if (DEF(i).cat != cat) {
            continue;
        }
        if (out && n < max) {
            out[n] = (setting_id_t)i;
        }
        ++n;
    }
    return n;
}

/** @brief The most digits after the point a step carries: 0.001A, the
 *         servo programmer's current. */
#define DECIMALS_MAX 3

int settings_decimals(setting_id_t id)
{
    const setting_def_t *d = settings_def(id);
    if (d == NULL || !(d->step > 0.0f)) {
        return 0;
    }
    float step = d->step;
    int decimals = 0;
    /* A step of 0.1 is 1.0000000149 once multiplied: whole within 0.1%. */
    while (decimals < DECIMALS_MAX
           && fabsf(step - roundf(step)) > 1e-3f * step) {
        step *= 10.0f;
        ++decimals;
    }
    return decimals;
}

const char *settings_value_text(setting_id_t id, char *buf, size_t n)
{
    return settings_value_text_of(id, settings_get(id), buf, n);
}

const char *settings_value_text_of(setting_id_t id, float v, char *buf,
                                   size_t n)
{
    const setting_def_t *d = settings_def(id);
    if (!buf || n == 0) {
        return "";
    }
    if (!d) {
        snprintf(buf, n, "--");
        return buf;
    }
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
        snprintf(buf, n, "%.*f", settings_decimals(id), (double)v);
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

bool settings_save(void)
{
    /* The strings with the numbers, so one request keeps both; a store with
     * no place for them keeps the numbers. */
    const bool wrote = s.store && s.store->save
                       && s.store->save(s.values, SETTING_COUNT)
                       && (s.store->save_text == NULL
                           || s.store->save_text(
                                  (const char (*)[SETTINGS_TEXT_MAX])s.texts,
                                  SETTING_TEXT_COUNT));
    /*
     * The request is answered either way, so a failure returns the button
     * to SAVE and the operator can try again without first nudging a value.
     * What a failure does not do is clear dirty: the medium does not hold
     * these values, and saying SAVED would be a lie the next boot exposes.
     *
     * No store at all is a failure and not a no-op.  A NULL store reaches
     * settings_save() on a panel whose NVS could not be brought up, and
     * that panel must not report a write it has nowhere to make.
     */
    s.save_asked  = false;
    s.save_failed = !wrote;
    if (wrote) {
        take_stored();
        s.dirty = false;
    } else {
        s.stored_sure = false;
    }
    return wrote;
}

const char *settings_text(setting_text_id_t id)
{
    if (id < 0 || id >= SETTING_TEXT_COUNT) {
        return "";
    }
    return s.texts[id];
}

const char *settings_text_key(setting_text_id_t id)
{
    if (id < 0 || id >= SETTING_TEXT_COUNT) {
        return NULL;
    }
    return k_text_defs[id].key;
}

void settings_set_text(setting_text_id_t id, const char *text)
{
    if (id < 0 || id >= SETTING_TEXT_COUNT || text == NULL) {
        return;
    }
    if (text_to((int)id, text)) {
        mark_dirty();
    }
}

setting_cat_t settings_text_category(setting_text_id_t id)
{
    return (id >= 0 && id < SETTING_TEXT_COUNT) ? k_text_defs[id].cat
                                                : SET_CAT_COUNT;
}

int settings_texts_in_category(setting_cat_t cat, setting_text_id_t *out,
                               int max)
{
    int n = 0;
    for (int i = 0; i < SETTING_TEXT_COUNT; ++i) {
        if (k_text_defs[i].cat != cat || cat == SET_CAT_COUNT) {
            continue;
        }
        if (out != NULL && n < max) {
            out[n] = (setting_text_id_t)i;
        }
        ++n;
    }
    return n;
}

bool settings_save_failed(void)
{
    return s.save_failed;
}

void settings_request_save(void)
{
    /* Asking for a save when there is nothing to write is not an error and
     * not a request: it would spend an erase cycle on what is already
     * there. */
    s.save_asked = s.dirty;
}

/*
 * A request only stands while there is something to write.  Values loaded
 * or reset out from under a standing request leave nothing to save, and a
 * request that outlives its reason would spend an erase cycle on what is
 * already in flash.
 */
bool settings_save_asked(void) { return s.save_asked && s.dirty; }

void settings_cancel_save(void) { s.save_asked = false; }

bool settings_save_tick(bool safe)
{
    if (!s.save_asked || !safe) {
        return false;
    }
    if (!s.dirty) {
        /* Something else wrote them in between; the request is answered. */
        s.save_asked = false;
        return false;
    }
    return settings_save();
}

/* ------------------------------------------------------ the stored copy */

float settings_stored(setting_id_t id)
{
    return (id >= 0 && id < SETTING_COUNT) ? s.stored[id] : 0.0f;
}

const char *settings_stored_text(setting_text_id_t id)
{
    if (id < 0 || id >= SETTING_TEXT_COUNT) {
        return "";
    }
    return s.stored_texts[id];
}

bool settings_differs(setting_id_t id)
{
    return id >= 0 && id < SETTING_COUNT && value_differs(id);
}

bool settings_text_differs(setting_text_id_t id)
{
    return id >= 0 && id < SETTING_TEXT_COUNT
           && strcmp(s.texts[id], s.stored_texts[id]) != 0;
}

int settings_changes(void)
{
    int n = 0;
    for (int i = 0; i < SETTING_COUNT; ++i) {
        n += settings_differs((setting_id_t)i) ? 1 : 0;
    }
    for (int i = 0; i < SETTING_TEXT_COUNT; ++i) {
        n += settings_text_differs((setting_text_id_t)i) ? 1 : 0;
    }
    return n;
}

int settings_discard(void)
{
    /*
     * Through settings_set() and settings_set_text(), one setting at a
     * time, as an edit by hand: the observer hears each value that moves,
     * in schema order, and a held value is left as settings_set() leaves
     * it.  A derived value is not set; it follows its source, which comes
     * before it in the schema.  The copy is not written, so a value the
     * copy holds is coerced already and settings_set() takes it as it is.
     */
    for (int i = 0; i < SETTING_COUNT; ++i) {
        if (settings_differs((setting_id_t)i)
            && !settings_derived((setting_id_t)i)) {
            settings_set((setting_id_t)i, s.stored[i]);
        }
    }
    /* The strings the same way: the text observer hears each one put
     * back. */
    bool texts = false;
    for (int i = 0; i < SETTING_TEXT_COUNT; ++i) {
        if (settings_text_differs((setting_text_id_t)i)) {
            char was[SETTINGS_TEXT_MAX];
            memcpy(was, s.stored_texts[i], sizeof(was));
            texts = text_to(i, was) || texts;
        }
    }
    if (texts) {
        mark_dirty();
    }
    /*
     * After a refused save the medium can hold part of the refused write,
     * and a restart would load it.  The values put back are kept only by a
     * write of them, so one is asked for.
     */
    if (!s.stored_sure) {
        settings_request_save();
    }
    return settings_changes();
}
