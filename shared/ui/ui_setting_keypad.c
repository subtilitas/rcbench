/**
 * @file ui_setting_keypad.c
 * @brief The numeric keypad for a number of the settings schema.  See
 *        ui_setting_keypad.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "ui_setting_keypad.h"

#include <math.h>

#include "ui_text.h"

/** The most digits after the point a step carries: 0.001 A, the servo
 *  programmer's current. */
#define DECIMALS_MAX 3

int ui_setting_keypad_decimals(setting_id_t id)
{
    const setting_def_t *d = settings_def(id);
    if (d == NULL || !(d->step > 0.0f)) {
        return 0;
    }
    float step = d->step;
    int decimals = 0;
    /* A step of 0.1 is 1.0000000149 once multiplied: whole within 0.1 %. */
    while (decimals < DECIMALS_MAX
           && fabsf(step - roundf(step)) > 1e-3f * step) {
        step *= 10.0f;
        ++decimals;
    }
    return decimals;
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

void ui_setting_keypad_open(ui_keypad_t *k, gfx_rect_t area, setting_id_t id)
{
    if (k == NULL || !ui_setting_keypad_offered(id)) {
        return;
    }
    const setting_def_t *d = settings_def(id);
    ui_keypad_open(k, area, ui_setting_label(id),
                   (d->unit != NULL) ? d->unit : "", settings_get(id),
                   d->min, d->max, ui_setting_keypad_decimals(id));
}

bool ui_setting_keypad_takes(setting_id_t id, float v)
{
    const setting_def_t *d = settings_def(id);
    if (d == NULL) {
        return false;
    }
    /* Off is 0 and the next value up is off_below: nothing between. */
    if (d->off_below > 0.0f && v > 0.0f && v < d->off_below) {
        return false;
    }
    const int pin = (int)lrintf(v);
    if (id == SET_SENSE_SDA) {
        return settings_sense_sda_valid(pin);
    }
    if (id == SET_PDMINI_TX || id == SET_PDMINI_RX) {
        const setting_id_t other =
            (id == SET_PDMINI_TX) ? SET_PDMINI_RX : SET_PDMINI_TX;
        return pin < 0 || pin != settings_get_int(other);
    }
    return true;
}

ui_setkey_result_t ui_setting_keypad_ok(ui_keypad_t *k, setting_id_t id,
                                        float v)
{
    if (!ui_setting_keypad_offered(id)) {
        return UI_SETKEY_LOCKED;
    }
    if (!ui_setting_keypad_takes(id, v)) {
        ui_keypad_refuse(k);
        return UI_SETKEY_REFUSED;
    }
    settings_set(id, v);
    return UI_SETKEY_SET;
}
