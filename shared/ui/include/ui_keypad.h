/**
 * @file
 * @brief A numeric keypad over part of a screen, for a value a slider is too coarse
 * to place.
 *
 * Digits, a decimal point, DEL and CLR, CANCEL and OK.  The value is typed
 * fresh: the entry starts empty with the current value shown faint behind
 * it, and OK on an empty entry leaves the value as it was.  OK on a value
 * outside the range keeps the keypad open and names the range in the warning
 * colour, so the caller is only ever handed a value inside it.
 *
 * A keypad opened signed (ui_keypad_open_signed()) has a +/- key between
 * the point and OK, which takes one column there: it puts a minus sign in
 * front of the entry or takes it away.  An unsigned keypad has no such key
 * and its keys are where they always were.
 *
 * Like the other widgets it owns nothing of the screen: the screen routes
 * events to it while it is open and draws it last.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx.h"
#include "touch_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Characters an entry holds, a minus sign included: "21.00" with
 *         room for a longer integer part, "-200". */
#define UI_KEYPAD_ENTRY_MAX 7

/** @brief The keys, in reading order; OK spans the last two columns, or
 *         the last one on a signed keypad, where UI_KEY_SIGN takes the
 *         third.  An unsigned keypad has no UI_KEY_SIGN: its rectangle is
 *         empty. */
typedef enum {
    UI_KEY_7 = 0, UI_KEY_8, UI_KEY_9, UI_KEY_DEL,
    UI_KEY_4, UI_KEY_5, UI_KEY_6, UI_KEY_CLR,
    UI_KEY_1, UI_KEY_2, UI_KEY_3, UI_KEY_CANCEL,
    UI_KEY_0, UI_KEY_DOT, UI_KEY_OK,
    UI_KEY_SIGN,
    UI_KEY_COUNT
} ui_key_t;

/** @brief What an event on the keypad came to. */
typedef enum {
    UI_KEYPAD_NONE = 0,   /**< still open, or the event was not its    */
    UI_KEYPAD_OK,         /**< closed with a value inside the range    */
    UI_KEYPAD_CANCEL,     /**< closed, the value as it was             */
} ui_keypad_result_t;

/** @brief A numeric keypad: what it was opened with, what is typed, and
 *         the press it holds. */
typedef struct {
    bool       open;       /**< drawn and taking events                */
    gfx_rect_t area;       /**< where it draws, screen coordinates     */
    char       title[48];  /**< the setting's name, top left           */
    char       unit[6];    /**< beside the entry and the range         */
    float      value;      /**< shown faint while nothing is typed     */
    float      min,        /**< the lowest value OK hands back         */
               max;        /**< the highest value OK hands back        */
    int        decimals;   /**< digits it takes after the point        */
    /** @brief What has been typed, '\0'-ended. */
    char       entry[UI_KEYPAD_ENTRY_MAX + 1];
    int        len;        /**< bytes in entry                         */
    bool       refused;    /**< the last OK was refused: outside the range, or by the caller */
    /** @brief Why the caller refused the last OK, drawn in place of the
     *         range while refused; "" for a value outside the range. */
    char       reason[64];
    bool       sign;       /**< opened signed: the +/- key is there    */
    int        pressed;    /**< the key under the press, or -1         */
    uint8_t    press_id;   /**< the touch id of that press             */
    /** @brief Bumped on every change a render would show. */
    uint32_t   revision;
} ui_keypad_t;

/** Open over @p area for a value of @p min .. @p max, showing @p value. */
void ui_keypad_open(ui_keypad_t *k, gfx_rect_t area, const char *title,
                    const char *unit, float value, float min, float max,
                    int decimals);

/**
 * @brief Open @p k as ui_keypad_open() does, with the +/- key: for a value
 *        whose range reaches below 0 by more than a "not set" -1.
 * @param k The keypad.
 * @param area Where it is drawn and hit.
 * @param title Its title.
 * @param unit The value's unit.
 * @param value The value as it is, shown faint.
 * @param min The range's lower end, below 0.
 * @param max Its upper end.
 * @param decimals The digits it takes after the point.
 */
void ui_keypad_open_signed(ui_keypad_t *k, gfx_rect_t area, const char *title,
                           const char *unit, float value, float min,
                           float max, int decimals);

void ui_keypad_close(ui_keypad_t *k);

/** A press under way is dropped: the touch stream lost an event.  The
 *  keypad stays open and what was typed stays typed. */
void ui_keypad_cancel_press(ui_keypad_t *k);

/**
 * One touch event.  A key acts on the release of the contact that pressed
 * it, over the same key; a second contact is ignored while one is down.
 * UI_KEYPAD_OK writes the value to @p out.
 */
ui_keypad_result_t ui_keypad_event(ui_keypad_t *k, const touch_event_t *evt,
                                   float *out);

/**
 * @brief Refuse the value the last OK handed back, after the caller has
 *        judged it by a rule of its own: the keypad opens again with the
 *        entry as typed and the range in the warning colour, as OK on a
 *        value outside the range leaves it.
 * @param k The keypad that returned UI_KEYPAD_OK.
 */
void ui_keypad_refuse(ui_keypad_t *k);

/**
 * @brief Refuse as ui_keypad_refuse() does, with the reason drawn in place
 *        of the range, in the warning colour, until the next key.
 * @param k The keypad that returned UI_KEYPAD_OK.
 * @param reason Why, one line; NULL or "" draws the range.  A reason of
 *        64 bytes or more is cut to at most 63 at the start of a UTF-8
 *        character, so no character is drawn half.
 */
void ui_keypad_refuse_why(ui_keypad_t *k, const char *reason);

/**
 * @brief Where a key is drawn and hit: the grid ends 8px inside the
 *        area, or at UI_KEYS_FOOT (4px above the alert band, body
 *        coordinates) where the area reaches further.
 * @param k The keypad.
 * @param key The key.
 * @return Its rectangle; an empty one for UI_KEY_SIGN on an unsigned keypad
 *         and for a key out of range.
 */
gfx_rect_t ui_keypad_key_rect(const ui_keypad_t *k, ui_key_t key);

/**
 * @brief The face of the digits and the point: the largest of 20 x 36,
 *        16 x 28, 12 x 24 and 10 x 20 whose cell fits a digit key with 6px
 *        on each side, else 8 x 16.  Over SUPPLY's 620 x 476px a key is
 *        145 x 76px and takes 20 x 36.
 * @param k The keypad, opened.
 * @return The face; 8 x 16 for NULL.
 */
const gfx_font_t *ui_keypad_digit_font(const ui_keypad_t *k);

/**
 * @brief The face of the word keys (DEL, CLR, CANCEL, OK, +/-) in the
 *        language shown: the largest of 20 x 36, 16 x 28, 12 x 24 and
 *        10 x 20, never larger than ui_keypad_digit_font(), in which every
 *        one of them fits its key with 6px each side, so the words of one
 *        keypad share one face; else 8 x 16.
 * @param k The keypad, opened.
 * @return The face; 8 x 16 for NULL.
 */
const gfx_font_t *ui_keypad_word_font(const ui_keypad_t *k);

/**
 * @brief The range line of the keypad's header: "3.30 to 21.00V", the
 *        unit joined to a symbol and after a space to a word
 *        (ui_unit_gap()), with the keypad's decimals.  A pin word
 *        (ui_unit_is_prefix()) leads: "GPIO 0 to 54".
 * @param k The keypad.
 * @param buf Takes the line.
 * @param n Bytes at @p buf.
 * @return @p buf; "" for a NULL @p k, @p buf or a zero @p n.
 */
const char *ui_keypad_range(const ui_keypad_t *k, char *buf, size_t n);

/**
 * @brief The whole keypad over its area: the title and the range in
 *        10 x 20 (8 x 16 where they do not fit side by side), the entry in
 *        the 32 x 44 numerals with its unit in 12 x 24 (24 x 30 and 10 x 20
 *        where those do not fit), the digits in ui_keypad_digit_font()
 *        and the words in ui_keypad_word_font(); the +/- key only on a signed keypad.
 *        Nothing while it is closed.
 * @param k The keypad.
 * @param c The canvas.
 */
void ui_keypad_render(const ui_keypad_t *k, gfx_canvas_t *c);

#ifdef __cplusplus
}
#endif
