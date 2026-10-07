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

/** Characters an entry holds: "21.00" with room for a longer integer part. */
#define UI_KEYPAD_ENTRY_MAX 7

/** The keys, in reading order; OK spans the last two columns. */
typedef enum {
    UI_KEY_7 = 0, UI_KEY_8, UI_KEY_9, UI_KEY_DEL,
    UI_KEY_4, UI_KEY_5, UI_KEY_6, UI_KEY_CLR,
    UI_KEY_1, UI_KEY_2, UI_KEY_3, UI_KEY_CANCEL,
    UI_KEY_0, UI_KEY_DOT, UI_KEY_OK,
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
    int        pressed;    /**< the key under the press, or -1         */
    uint8_t    press_id;
    /** Bumped on every change a render would show. */
    uint32_t   revision;
} ui_keypad_t;

/** Open over @p area for a value of @p min .. @p max, showing @p value. */
void ui_keypad_open(ui_keypad_t *k, gfx_rect_t area, const char *title,
                    const char *unit, float value, float min, float max,
                    int decimals);

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

/** Where a key is drawn and hit. */
gfx_rect_t ui_keypad_key_rect(const ui_keypad_t *k, ui_key_t key);

/** The whole keypad over its area.  Nothing while it is closed. */
void ui_keypad_render(const ui_keypad_t *k, gfx_canvas_t *c);

#ifdef __cplusplus
}
#endif
