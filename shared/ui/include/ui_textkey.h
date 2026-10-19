/*
 * An on-screen keyboard for a short name: capitals, digits, space and the
 * marks a name or a file needs, "-", "_" and ".".
 *
 * Unlike the numeric keypad it edits the text it was opened with: DEL takes
 * the last character, CLR the lot.  OK on an empty name is refused, because
 * a name is what a report is filed under.  Spaces at either end are dropped
 * on OK.
 *
 * Opened for a search (ui_textkey_open_search()) it types a search pattern
 * instead: the "_" key types "*", the wildcard; OK takes the text as typed,
 * empty included, since an empty search is no search; and the bottom row
 * gives SPACE and CANCEL three columns each, CLR and OK two, so CANCEL's
 * longer translations fit an area narrower than the SERVO screen's.  The
 * caller reads the text as it is typed from @c text.
 *
 * Opened for a folder of the card (ui_textkey_open_folder()) it types a
 * path such as BENCH/LOGS: the "." key shows and types "/", and OK takes
 * the path only when card_folder_check() (card_tree.h) does, in upper
 * case.  A refused OK keeps the keyboard open, and its line says why:
 * an empty folder, over 8 characters, over 2 folders deep, a character
 * outside A-Z, 0-9, "_" and "-", a reserved name, a folder of the bench's
 * own files.  The keys and the rest of the layout are the name's.
 *
 * Opened for Wi-Fi (ui_textkey_open_wifi()) it types a network name of up
 * to 32 bytes or a password of 8 to 63 characters: any printable ASCII
 * (American Standard Code for Information Interchange) character, lower
 * case included.  Three layers share the grid: lower case, capitals, and
 * the 32 marks.  The key left of Z switches lower case and capitals ("ABC",
 * "abc"); the key left of DEL switches to the marks and back ("#+=",
 * "abc").  The digits stay on the top row of both letter layers.  A
 * password shows as "*" until SHOW, which a sixth bottom key carries
 * (UI_TK_SHOW); SHOW again hides it.  OK takes the text as typed, spaces
 * included, and refuses a password under 8 characters or an empty name.
 * A text longer than the field shows its end.
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

/** The longest name or search it takes, in characters. */
#define UI_TEXTKEY_MAX 23
/** The longest Wi-Fi password, and the shortest. */
#define UI_TEXTKEY_PASS_MAX 63
#define UI_TEXTKEY_PASS_MIN 8
/** The longest Wi-Fi network name, in bytes. */
#define UI_TEXTKEY_SSID_MAX 32

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
/** SHOW, the password keyboard's sixth bottom key; zero size otherwise. */
#define UI_TK_SHOW UI_TK_KEYS
/** The Wi-Fi keyboard's layer keys: capitals, and marks. */
#define UI_TK_SHIFT 30
#define UI_TK_LAYER 38
/** The DEL key, last of the character rows. */
#define UI_TK_DEL (UI_TEXTKEY_CHARS - 1)
/** The key that types "_", or "*" in a search. */
#define UI_TK_MARK (UI_TEXTKEY_CHARS - 3)
/** The key that types ".", or "/" for a folder. */
#define UI_TK_DOT (UI_TEXTKEY_CHARS - 2)

typedef enum {
    UI_TEXTKEY_NONE = 0,   /**< still open, or the event was not its   */
    UI_TEXTKEY_OK,         /**< closed with a name                     */
    UI_TEXTKEY_CANCELLED,  /**< closed, the name as it was             */
} ui_textkey_result_t;

/** The Wi-Fi keyboard's layers. */
typedef enum {
    UI_TK_LOWER = 0,
    UI_TK_UPPER,
    UI_TK_MARKS,
} ui_textkey_layer_t;

typedef struct {
    bool       open;
    gfx_rect_t area;
    char       title[48];
    char       text[UI_TEXTKEY_PASS_MAX + 1];
    int        len;
    int        max_len;
    bool       refused;     /**< OK on an empty name                  */
    bool       search;      /**< a search pattern: see the top        */
    bool       folder;      /**< a folder of the card: see the top    */
    uint8_t    why;         /**< a folder's refusal, card_folder_t    */
    bool       wifi;        /**< a Wi-Fi name or password             */
    bool       secret;      /**< a password: masked, 8 at least       */
    bool       show;        /**< the password shown                   */
    ui_textkey_layer_t layer;
    int        pressed;     /**< the key under the press, or -1       */
    uint8_t    press_id;
    uint32_t   revision;    /**< bumped on every change a render shows */
} ui_textkey_t;

/** Open over @p area on @p text, taking up to @p max_len characters (at
 *  most UI_TEXTKEY_MAX). */
void ui_textkey_open(ui_textkey_t *k, gfx_rect_t area, const char *title,
                     const char *text, int max_len);

/** Open for a search pattern; see the top of this file. */
void ui_textkey_open_search(ui_textkey_t *k, gfx_rect_t area,
                            const char *title, const char *text,
                            int max_len);

/** Open for a folder of the card; see the top of this file.  Up to
 *  UI_TEXTKEY_MAX characters. */
void ui_textkey_open_folder(ui_textkey_t *k, gfx_rect_t area,
                            const char *title, const char *text);

/** Open for a Wi-Fi password (@p secret) or network name; see the top of
 *  this file.  Lower case first. */
void ui_textkey_open_wifi(ui_textkey_t *k, gfx_rect_t area,
                          const char *title, const char *text, bool secret);

void ui_textkey_close(ui_textkey_t *k);

/** A press under way is dropped: the touch stream lost an event.  What was
 *  typed stays typed. */
void ui_textkey_cancel_press(ui_textkey_t *k);

/**
 * One touch event.  A key acts on the release of the contact that pressed
 * it, over the same key; a second contact is ignored while one is down.
 * UI_TEXTKEY_OK writes the name, ends trimmed, into @p out; a search's
 * text as typed; a folder in upper case, as card_folder_check() writes
 * it (@p n of CARD_DIR_MAX or more).
 */
ui_textkey_result_t ui_textkey_event(ui_textkey_t *k, const touch_event_t *evt,
                                     char *out, size_t n);

/** Where a key is drawn and hit; zero size for one out of range. */
gfx_rect_t ui_textkey_key_rect(const ui_textkey_t *k, int key);

/** A key's label, or "" for one out of range; as a name keyboard shows
 *  it. */
const char *ui_textkey_label(int key);

/** A key's label as @p k shows it: "*" for UI_TK_MARK in a search, "/"
 *  for UI_TK_DOT for a folder, the
 *  layer's character on a Wi-Fi keyboard, "" for a blank key. */
const char *ui_textkey_key_label(const ui_textkey_t *k, int key);

/** The whole keyboard over its area.  Nothing while it is closed. */
void ui_textkey_render(const ui_textkey_t *k, gfx_canvas_t *c);

#ifdef __cplusplus
}
#endif
