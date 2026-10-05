/*
 * An on-screen keyboard for a short name: capitals, digits, space and the
 * marks a name or a file needs, "-", "_" and ".".
 *
 * Unlike the numeric keypad it edits the text it was opened with: DEL takes
 * the last character, CLR the lot.  OK on an empty name is refused, because
 * a name is what a report is filed under.  Spaces at either end are dropped
 * on OK.
 *
 * The screen routes events to it while it is open and draws it last, as it
 * does the keypad.
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

/** The longest text it takes, in characters. */
#define UI_TEXTKEY_MAX 23

/** The keys: forty characters in four rows of ten, then SPACE, CLR, CANCEL
 *  and OK along the bottom. */
#define UI_TEXTKEY_CHARS 40
enum {
    UI_TK_SPACE = UI_TEXTKEY_CHARS,
    UI_TK_CLR,
    UI_TK_CANCEL,
    UI_TK_OK,
    UI_TK_KEYS
};
/** The DEL key, last of the character rows. */
#define UI_TK_DEL (UI_TEXTKEY_CHARS - 1)

typedef enum {
    UI_TEXTKEY_NONE = 0,   /**< still open, or the event was not its   */
    UI_TEXTKEY_OK,         /**< closed with a name                     */
    UI_TEXTKEY_CANCELLED,  /**< closed, the name as it was             */
} ui_textkey_result_t;

typedef struct {
    bool       open;
    gfx_rect_t area;
    char       title[24];
    char       text[UI_TEXTKEY_MAX + 1];
    int        len;
    int        max_len;
    bool       refused;     /**< OK on an empty name                  */
    int        pressed;     /**< the key under the press, or -1       */
    uint8_t    press_id;
    uint32_t   revision;    /**< bumped on every change a render shows */
} ui_textkey_t;

/** Open over @p area on @p text, taking up to @p max_len characters (at
 *  most UI_TEXTKEY_MAX). */
void ui_textkey_open(ui_textkey_t *k, gfx_rect_t area, const char *title,
                     const char *text, int max_len);

void ui_textkey_close(ui_textkey_t *k);

/** A press under way is dropped: the touch stream lost an event.  What was
 *  typed stays typed. */
void ui_textkey_cancel_press(ui_textkey_t *k);

/**
 * One touch event.  A key acts on the release of the contact that pressed
 * it, over the same key; a second contact is ignored while one is down.
 * UI_TEXTKEY_OK writes the name, ends trimmed, into @p out.
 */
ui_textkey_result_t ui_textkey_event(ui_textkey_t *k, const touch_event_t *evt,
                                     char *out, size_t n);

/** Where a key is drawn and hit; zero size for one out of range. */
gfx_rect_t ui_textkey_key_rect(const ui_textkey_t *k, int key);

/** A key's label, or "" for one out of range. */
const char *ui_textkey_label(int key);

/** The whole keyboard over its area.  Nothing while it is closed. */
void ui_textkey_render(const ui_textkey_t *k, gfx_canvas_t *c);

#ifdef __cplusplus
}
#endif
