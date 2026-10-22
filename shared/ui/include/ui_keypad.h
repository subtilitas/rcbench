/*
 * A numeric keypad over part of a screen, for a value a slider is too coarse
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
#include <stdint.h>

#include "gfx.h"
#include "touch_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Characters an entry holds, a minus sign included: "21.00" with room for
 *  a longer integer part, "-200". */
#define UI_KEYPAD_ENTRY_MAX 7

/** The keys, in reading order; OK spans the last two columns, or the last
 *  one on a signed keypad, where UI_KEY_SIGN takes the third.  An unsigned
 *  keypad has no UI_KEY_SIGN: its rectangle is empty. */
typedef enum {
    UI_KEY_7 = 0, UI_KEY_8, UI_KEY_9, UI_KEY_DEL,
    UI_KEY_4, UI_KEY_5, UI_KEY_6, UI_KEY_CLR,
    UI_KEY_1, UI_KEY_2, UI_KEY_3, UI_KEY_CANCEL,
    UI_KEY_0, UI_KEY_DOT, UI_KEY_OK,
    UI_KEY_SIGN,
    UI_KEY_COUNT
} ui_key_t;

typedef enum {
    UI_KEYPAD_NONE = 0,   /**< still open, or the event was not its    */
    UI_KEYPAD_OK,         /**< closed with a value inside the range    */
    UI_KEYPAD_CANCEL,     /**< closed, the value as it was             */
} ui_keypad_result_t;

typedef struct {
    bool       open;
    gfx_rect_t area;
    char       title[48];
    char       unit[6];
    float      value;      /**< shown faint while nothing is typed     */
    float      min, max;
    int        decimals;   /**< digits it takes after the point        */
    char       entry[UI_KEYPAD_ENTRY_MAX + 1];
    int        len;
    bool       refused;    /**< the last OK was outside the range      */
    bool       sign;       /**< opened signed: the +/- key is there    */
    int        pressed;    /**< the key under the press, or -1         */
    uint8_t    press_id;
    /** Bumped on every change a render would show. */
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
 * @brief Where a key is drawn and hit.
 * @param k The keypad.
 * @param key The key.
 * @return Its rectangle; an empty one for UI_KEY_SIGN on an unsigned keypad
 *         and for a key out of range.
 */
gfx_rect_t ui_keypad_key_rect(const ui_keypad_t *k, ui_key_t key);

/**
 * @brief The whole keypad over its area: the title, the range, the entry
 *        and the keys, the +/- key only on a signed keypad.  Nothing while
 *        it is closed.
 * @param k The keypad.
 * @param c The canvas.
 */
void ui_keypad_render(const ui_keypad_t *k, gfx_canvas_t *c);

#ifdef __cplusplus
}
#endif
