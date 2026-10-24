/**
 * @file ui_setting_keypad.c
 * @brief The numeric keypad for a number of the settings schema.  See
 *        ui_setting_keypad.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "ui_setting_keypad.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "link_pages.h"
#include "outputs.h"
#include "ui_text.h"

int ui_setting_keypad_decimals(setting_id_t id)
{
    return settings_decimals(id);   /* the digits the row shows */
}

bool ui_setting_keypad_offered(setting_id_t id)
{
    const setting_def_t *d = settings_def(id);
    if (d == NULL) {
        return false;
    }
    return (d->type == SET_TYPE_INT || d->type == SET_TYPE_FLOAT)
           && !settings_derived(id) && !settings_held(id);
}

bool ui_setting_keypad_signed(setting_id_t id)
{
    const setting_def_t *d = settings_def(id);
    return d != NULL && d->min < 0.0f
           && (d->unit == NULL || strcmp(d->unit, "GPIO") != 0);
}

void ui_setting_keypad_open(ui_keypad_t *k, gfx_rect_t area, setting_id_t id)
{
    if (k == NULL || !ui_setting_keypad_offered(id)) {
        return;
    }
    const setting_def_t *d = settings_def(id);
    ui_keypad_open(k, area, ui_setting_label(id),
                   (d->unit != NULL) ? d->unit : "", settings_get(id),
                   d->min, d->max, ui_setting_keypad_decimals(id));
    k->sign = ui_setting_keypad_signed(id);     /* the +/- key */
}

/** @brief Why ui_setting_keypad_takes() refuses @p v for @p id. */
typedef enum {
    WHY_NONE = 0,   /**< taken */
    WHY_OFF_GAP,    /**< between 0 and off_below */
    WHY_SDA,        /**< a Sensor SDA pin the bus cannot have */
    WHY_TAP,        /**< a Tap pin the module does not bring out */
    WHY_IDLE_GAP,   /**< an Idle pulse too near Full pulse */
    WHY_FULL_GAP,   /**< a Full pulse too near Idle pulse */
    WHY_PDMINI,     /**< the other PD mini pin's GPIO */
    WHY_NO_ROW,     /**< no such setting */
} why_t;

/**
 * @brief Sort @p v for @p id into taken or the rule that refuses it.
 * @param id The setting.
 * @param v The value typed.
 * @return WHY_NONE for a value settings_set() takes as typed; the rule
 *         that refuses it otherwise.
 */
static why_t why_refused(setting_id_t id, float v)
{
    const setting_def_t *d = settings_def(id);
    if (d == NULL) {
        return WHY_NO_ROW;
    }
    /* Off is 0 and the next value up is off_below: nothing between. */
    if (d->off_below > 0.0f && v > 0.0f && v < d->off_below) {
        return WHY_OFF_GAP;
    }
    const int pin = (int)lrintf(v);
    if (id == SET_SENSE_SDA) {
        return settings_sense_sda_valid(pin) ? WHY_NONE : WHY_SDA;
    }
    if (id == SET_TONE_PIN) {
        return settings_tap_pin_valid(pin) ? WHY_NONE : WHY_TAP;
    }
    /* The throttle's pair keeps LINK_CC_THROTTLE_GAP_US between its ends:
     * the same gap the payment to the IOmcu holds it to. */
    if (id == SET_OUT_MIN_US) {
        return (pin + (int)LINK_CC_THROTTLE_GAP_US
                <= settings_get_int(SET_OUT_MAX_US)) ? WHY_NONE : WHY_IDLE_GAP;
    }
    if (id == SET_OUT_MAX_US) {
        return (pin >= settings_get_int(SET_OUT_MIN_US)
                           + (int)LINK_CC_THROTTLE_GAP_US)
                   ? WHY_NONE : WHY_FULL_GAP;
    }
    if (id == SET_PDMINI_TX || id == SET_PDMINI_RX) {
        const setting_id_t other =
            (id == SET_PDMINI_TX) ? SET_PDMINI_RX : SET_PDMINI_TX;
        return (pin < 0 || pin != settings_get_int(other)) ? WHY_NONE
                                                           : WHY_PDMINI;
    }
    return WHY_NONE;
}

bool ui_setting_keypad_takes(setting_id_t id, float v)
{
    return why_refused(id, v) == WHY_NONE;
}

/**
 * @brief The GPIOs @p valid takes, in rising order with one space between,
 *        cut at the last pin that fits whole.
 * @param valid The rule: true for a pin the row takes.
 * @param buf   Where the list goes.
 * @param n     The size of @p buf in bytes.
 * @return @p buf; "" for a NULL or empty @p buf.
 */
static const char *pins_taken(bool (*valid)(int), char *buf, size_t n)
{
    if (buf == NULL || n == 0u) {
        return "";
    }
    buf[0] = '\0';
    size_t at = 0u;
    /* -1 (no bus) is not a pin; the list is the GPIOs the row can have. */
    for (int gpio = 0; gpio < (int)OUT_MAX_PIN; gpio++) {
        if (!valid(gpio)) {
            continue;
        }
        const int w = snprintf(buf + at, n - at, (at == 0u) ? "%d" : " %d",
                               gpio);
        if (w < 0 || (size_t)w >= n - at) {
            break;              /* cut at the last pin that fits whole */
        }
        at += (size_t)w;
    }
    buf[at] = '\0';
    return buf;
}

const char *ui_setting_keypad_sda_pins(char *buf, size_t n)
{
    return pins_taken(settings_sense_sda_valid, buf, n);
}

const char *ui_setting_keypad_tap_pins(char *buf, size_t n)
{
    return pins_taken(settings_tap_pin_valid, buf, n);
}

const char *ui_setting_keypad_why(setting_id_t id, float v, char *buf,
                                  size_t n)
{
    char pins[64];
    switch (why_refused(id, v)) {
    case WHY_OFF_GAP: {
        if (buf == NULL || n == 0u) {
            return "";
        }
        /* The row's own figures: a shunt's 0.1mOhm, a limit's 1mA. */
        const setting_def_t *d = settings_def(id);
        const int dec = ui_setting_keypad_decimals(id);
        const char *unit = (d->unit != NULL) ? d->unit : "";
        snprintf(buf, n, TR(KEYPAD_WHY_OFF), dec, (double)d->off_below, dec,
                 (double)d->max, ui_unit_gap(unit), unit);
        return buf;
    }
    case WHY_SDA: {
        if (buf == NULL || n == 0u) {
            return "";
        }
        /* The text without the list, measured; the list is built into
         * the room left after it, so only whole pins reach @p buf. */
        const int fixed = snprintf(buf, n, TR(KEYPAD_WHY_SDA), "");
        size_t room = 1u;
        if (fixed >= 0 && (size_t)fixed < n) {
            room = n - (size_t)fixed;
        }
        if (room > sizeof(pins)) {
            room = sizeof(pins);
        }
        snprintf(buf, n, TR(KEYPAD_WHY_SDA),
                 ui_setting_keypad_sda_pins(pins, room));
        return buf;
    }
    case WHY_TAP: {
        if (buf == NULL || n == 0u) {
            return "";
        }
        const int fixed = snprintf(buf, n, TR(KEYPAD_WHY_TAP), "");
        size_t room = 1u;
        if (fixed >= 0 && (size_t)fixed < n) {
            room = n - (size_t)fixed;
        }
        if (room > sizeof(pins)) {
            room = sizeof(pins);
        }
        snprintf(buf, n, TR(KEYPAD_WHY_TAP),
                 ui_setting_keypad_tap_pins(pins, room));
        return buf;
    }
    case WHY_IDLE_GAP:
    case WHY_FULL_GAP: {
        if (buf == NULL || n == 0u) {
            return "";
        }
        /* The bound the other end sets, in the row's own unit. */
        const bool idle = why_refused(id, v) == WHY_IDLE_GAP;
        const int bound = idle ? settings_get_int(SET_OUT_MAX_US)
                                     - (int)LINK_CC_THROTTLE_GAP_US
                               : settings_get_int(SET_OUT_MIN_US)
                                     + (int)LINK_CC_THROTTLE_GAP_US;
        snprintf(buf, n, ui_tr(idle ? TX_KEYPAD_WHY_IDLE_GAP
                                    : TX_KEYPAD_WHY_FULL_GAP),
                 (unsigned)bound, (unsigned)LINK_CC_THROTTLE_GAP_US);
        return buf;
    }
    case WHY_PDMINI:
        return TR(KEYPAD_WHY_PDMINI);
    case WHY_NO_ROW:
    case WHY_NONE:
    default:
        return NULL;
    }
}

ui_setkey_result_t ui_setting_keypad_ok(ui_keypad_t *k, setting_id_t id,
                                        float v)
{
    if (!ui_setting_keypad_offered(id)) {
        return UI_SETKEY_LOCKED;
    }
    if (!ui_setting_keypad_takes(id, v)) {
        char why[sizeof(k->reason)];
        const char *text = ui_setting_keypad_why(id, v, why, sizeof(why));
        ui_keypad_refuse_why(k, (text != NULL) ? text : "");
        return UI_SETKEY_REFUSED;
    }
    settings_set(id, v);
    return UI_SETKEY_SET;
}
