/**
 * @file ui_setting_keypad.h
 * @brief The numeric keypad (ui_keypad.h) for a number of the settings
 *        schema: opened with the setting's label, unit, range and the
 *        decimals of its step, and its OK set through settings_set(), the
 *        call the - and + keys end in.
 *
 * settings_set() puts a value onto the setting's step inside its range,
 * so a typed value between two steps is set to the nearer one, as the
 * MOTOR & ESC and SUPPLY keypads set it.  A value the - and + keys never
 * reach is refused the way a value outside the range is: the keypad stays
 * open with the range in the warning colour.  That is a value between 0
 * and off_below (a SENSORS shunt of 0.1 to 4.9 mOhm), a Sensor SDA pin
 * settings_sense_sda_valid() refuses, and the GPIO the other PD mini pin
 * holds.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>

#include "gfx.h"
#include "settings.h"
#include "ui_keypad.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The digits the keypad takes after the point for @p id.
 * @param id The setting.
 * @return As many as its step has, 0 to 3: 0 for a step of 1 or more, 1
 *         for 0.1, 2 for 0.05, 3 for 0.001; 0 for an id out of range.
 */
int ui_setting_keypad_decimals(setting_id_t id);

/**
 * @brief Whether a tap on the value of @p id opens the keypad.
 * @param id The setting.
 * @return True for a number (SET_TYPE_INT or SET_TYPE_FLOAT) that is not
 *         derived (settings_derived()) and not held (settings_held()):
 *         the rows that draw - and + keys.  False for a switch, a choice
 *         and an id out of range.
 */
bool ui_setting_keypad_offered(setting_id_t id);

/**
 * @brief Open @p k over @p area for setting @p id.
 * @param k The keypad.
 * @param area Where it is drawn and hit, in the screen's coordinates.
 * @param id The setting: its label in the language showing as the title,
 *        its unit, its value faint behind the entry, its range and
 *        ui_setting_keypad_decimals().  Nothing opens for an id
 *        ui_setting_keypad_offered() refuses.
 */
void ui_setting_keypad_open(ui_keypad_t *k, gfx_rect_t area, setting_id_t id);

/**
 * @brief Whether settings_set() takes @p v for @p id as a value the - and
 *        + keys can reach.
 * @param id The setting.
 * @param v The value typed, inside the range the keypad holds it to.
 * @return False for a value between 0 and the setting's off_below, a Sensor
 *         SDA pin the bus cannot have, and a PD mini pin at or above 0 that
 *         the other PD mini pin holds; true otherwise.
 */
bool ui_setting_keypad_takes(setting_id_t id, float v);

/** @brief What an OK on a setting's keypad came to. */
typedef enum {
    UI_SETKEY_SET = 0,    /**< set through settings_set(); keypad closed  */
    UI_SETKEY_REFUSED,    /**< refused (ui_keypad_refuse()); keypad open  */
    UI_SETKEY_LOCKED,     /**< the row is held or derived now; nothing set,
                               keypad closed                              */
} ui_setkey_result_t;

/**
 * @brief Take the value an OK on @p k handed back for @p id.
 * @param k The keypad that returned UI_KEYPAD_OK.
 * @param id The setting it was opened for.
 * @param v The value it handed back.
 * @return UI_SETKEY_SET after settings_set(@p id, @p v);
 *         UI_SETKEY_REFUSED for a value ui_setting_keypad_takes() refuses,
 *         with @p k open again and marked; UI_SETKEY_LOCKED for a row no
 *         longer offered (ui_setting_keypad_offered()).
 */
ui_setkey_result_t ui_setting_keypad_ok(ui_keypad_t *k, setting_id_t id,
                                        float v);

#ifdef __cplusplus
}
#endif
