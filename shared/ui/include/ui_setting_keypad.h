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
 * reach is refused: the keypad stays open and shows, in the warning
 * colour and in place of the range, why (ui_setting_keypad_why()).  That
 * is a value between 0 and off_below (a SENSORS shunt of 0.1 to 4.9mΩ:
 * "0 or 5.0 to 1000.0mΩ"; a SENSORS limit of 1 to 9mA: "0 or 10 to
 * 32767mA"), a Sensor SDA pin settings_sense_sda_valid()
 * refuses and a Tap pin settings_tap_pin_valid() refuses (the pins each
 * takes, listed), and the GPIO the other PD mini pin
 * holds.  The next key brings the range back.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

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
 * @brief Whether the keypad of @p id carries the +/- key.
 * @param id The setting.
 * @return True for a number whose range reaches below 0, except a pin
 *         (unit "GPIO"), whose one value below 0 is -1, not set, which "-"
 *         reaches.  No setting of today's schema is signed; SERVO's TRIM,
 *         -200 to +200us, is a signed row of its own screen.
 */
bool ui_setting_keypad_signed(setting_id_t id);

/**
 * @brief Open @p k over @p area for setting @p id.
 * @param k The keypad.
 * @param area Where it is drawn and hit, in the screen's coordinates.
 * @param id The setting: its label in the language showing as the title,
 *        its unit, its value faint behind the entry, its range and
 *        ui_setting_keypad_decimals(); signed by ui_setting_keypad_signed().  Nothing opens for an id
 *        ui_setting_keypad_offered() refuses.
 */
void ui_setting_keypad_open(ui_keypad_t *k, gfx_rect_t area, setting_id_t id);

/**
 * @brief Whether settings_set() takes @p v for @p id as a value the - and
 *        + keys can reach.
 * @param id The setting.
 * @param v The value typed, inside the range the keypad holds it to.
 * @return False for a value between 0 and the setting's off_below, a Sensor
 *         SDA pin the bus cannot have, a Tap pin the module does not bring out,
 *         and a PD mini pin at or above 0 that
 *         the other PD mini pin holds; true otherwise.
 */
bool ui_setting_keypad_takes(setting_id_t id, float v);

/**
 * @brief The GPIOs Sensor SDA can have, from settings_sense_sda_valid().
 * @param buf Where the list goes.
 * @param n The size of @p buf in bytes.
 * @return @p buf, holding the pins in rising order with one space between
 *         ("0 4 6 14 16 18 20 26" for the Pico header), cut at the last pin
 *         that fits whole; "" for a NULL or empty @p buf.
 */
const char *ui_setting_keypad_sda_pins(char *buf, size_t n);

/**
 * @brief The GPIOs Tap pin can have, from settings_tap_pin_valid().
 * @param buf Where the list goes.
 * @param n The size of @p buf in bytes.
 * @return @p buf, holding the pins in rising order with one space between
 *         ("0 1 2 4 5 6 7 13 14 15 16 17 18 19 20 21 22" for the Pico
 *         header), cut at the last pin that fits whole; "" for a NULL or
 *         empty @p buf.
 */
const char *ui_setting_keypad_tap_pins(char *buf, size_t n);

/**
 * @brief The line a setting's keypad shows in place of the range after
 *        refusing @p v inside it.
 * @param id The setting.
 * @param v The value typed.
 * @param buf Where a text with numbers in it goes (the off gap's figures,
 *        the Sensor SDA pins).
 * @param n The size of @p buf in bytes; 64 holds every reason.  A
 *        smaller @p buf holds the text and as many whole pins as fit.
 * @return NULL for a value ui_setting_keypad_takes() takes; otherwise
 *         @p buf holding KEYPAD_WHY_OFF with the row's off_below,
 *         maximum and unit for a value between 0 and off_below, @p buf
 *         holding KEYPAD_WHY_SDA with the pins for a Sensor SDA pin the bus
 *         cannot have, KEYPAD_WHY_TAP with the pins for a Tap pin the
 *         module does not bring out, and KEYPAD_WHY_PDMINI for the other
 *         PD mini pin's GPIO, in the panel's language.
 */
const char *ui_setting_keypad_why(setting_id_t id, float v, char *buf,
                                  size_t n);

/** @brief What an OK on a setting's keypad came to. */
typedef enum {
    UI_SETKEY_SET = 0,    /**< set through settings_set(); keypad closed  */
    UI_SETKEY_REFUSED,    /**< refused (ui_keypad_refuse_why()); open   */
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
 *         with @p k open again, marked and showing
 *         ui_setting_keypad_why() in place of the range; UI_SETKEY_LOCKED for a row no
 *         longer offered (ui_setting_keypad_offered()).
 */
ui_setkey_result_t ui_setting_keypad_ok(ui_keypad_t *k, setting_id_t id,
                                        float v);

#ifdef __cplusplus
}
#endif
