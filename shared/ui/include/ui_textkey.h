/**
 * @file
 * @brief An on-screen keyboard for a short name: capitals, digits, space and the
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
 * Opened for a file name (ui_textkey_open_file()) it is the Wi-Fi keyboard
 * with its three layers, for a name of up to FM_NAME_MAX (59) bytes: OK
 * takes the name only when fm_name_check() (fm_name.h) does, as typed.  A
 * refused OK keeps the keyboard open, and its line says why: an empty
 * name, over 59 bytes, one of  " * / : < > ? \ |  , a space or a full
 * stop at an end, a device name.  DEL takes the last character, also one
 * of two bytes.
 *
 * Opened for the text editor (ui_textkey_open_editor()) it types into a
 * text the caller holds, one key per event: it keeps no text, draws no
 * title and no field, and its UI_TEXTKEY_EDITOR_ROWS (4) rows of keys fill
 * its area.  It has no digit row: the first row is the letters Q to P.
 * Four layers share the 3 character rows: lower case, capitals, and two
 * pages of marks.  The key left of Z switches lower case and capitals
 * ("ABC", "abc"); the key left of DEL switches to the first page of marks
 * and back ("123", "abc").  The first page holds the digits 0 to 9 on its
 * top row and 17 marks; there the key left of Z switches to the second
 * page ("#+=") and back ("123"), which holds the other 15 marks.  DEL is
 * a backspace.  The bottom row is ENTER (2 columns), SPACE (4), "<-" and
 * "->" (1 each) and DONE (2); CLR is not there, since it would clear the
 * file.
 *
 * The editor's keyboard can let the text behind it show through
 * (ui_textkey_set_see()): 0, 25 or 50% of each pixel of its keys and
 * their gaps is the text's, the labels stay opaque.  Other keyboards draw
 * opaque: they carry their own field, and nothing behind them is typed
 * into.
 * Each key's release answers what it was (ui_textkey_result_t):
 * UI_TEXTKEY_TYPED with the character in @c typed, UI_TEXTKEY_BACKSPACE,
 * UI_TEXTKEY_ENTER, UI_TEXTKEY_LEFT, UI_TEXTKEY_RIGHT, and UI_TEXTKEY_DONE,
 * which closes the keyboard.  A layer key answers UI_TEXTKEY_NONE.
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

/** @brief The longest name or search it takes, in characters. */
#define UI_TEXTKEY_MAX 23
/** @brief The longest Wi-Fi password, in characters. */
#define UI_TEXTKEY_PASS_MAX 63
/** @brief The shortest Wi-Fi password, in characters. */
#define UI_TEXTKEY_PASS_MIN 8
/** @brief The longest Wi-Fi network name, in bytes. */
#define UI_TEXTKEY_SSID_MAX 32

/** @brief The keys: forty characters in four rows of ten, then SPACE, CLR,
 *         CANCEL and OK along the bottom. */
#define UI_TEXTKEY_CHARS 40
/** @brief The bottom row's keys, numbered after the characters. */
enum {
    UI_TK_SPACE = UI_TEXTKEY_CHARS,  /**< SPACE                          */
    UI_TK_CLR,                       /**< CLR: the whole text            */
    UI_TK_CANCEL,                    /**< CANCEL: closes, nothing taken  */
    UI_TK_OK,                        /**< OK: takes the text             */
    UI_TK_KEYS                       /**< the count of the keys above    */
};
/** @brief SHOW, the password keyboard's sixth bottom key; zero size
 *         otherwise. */
#define UI_TK_SHOW UI_TK_KEYS
/** @brief The editor's bottom row after SHOW: ENTER, "<-", "->" and DONE;
 *         zero size outside the editor, where CLR, CANCEL, OK and SHOW
 *         are zero size in turn.  SPACE is UI_TK_SPACE in both. */
enum {
    UI_TK_ENTER = UI_TK_SHOW + 1,    /**< ENTER: a line end              */
    UI_TK_LEFT,                      /**< "<-": the cursor one left      */
    UI_TK_RIGHT,                     /**< "->": the cursor one right     */
    UI_TK_DONE,                      /**< DONE: closes the keyboard      */
    UI_TK_ALL                        /**< the count of every key         */
};
/** @brief The Wi-Fi keyboard's capitals key, left of Z. */
#define UI_TK_SHIFT 30
/** @brief The Wi-Fi keyboard's marks key, left of DEL. */
#define UI_TK_LAYER 38
/** @brief The DEL key, last of the character rows. */
#define UI_TK_DEL (UI_TEXTKEY_CHARS - 1)
/** @brief The key that types "_", or "*" in a search. */
#define UI_TK_MARK (UI_TEXTKEY_CHARS - 3)
/** @brief The key that types ".", or "/" for a folder. */
#define UI_TK_DOT (UI_TEXTKEY_CHARS - 2)

/** @brief What an event on the keyboard came to. */
typedef enum {
    UI_TEXTKEY_NONE = 0,   /**< still open, or the event was not its   */
    UI_TEXTKEY_OK,         /**< closed with a name                     */
    UI_TEXTKEY_CANCELLED,  /**< closed, the name as it was             */
    UI_TEXTKEY_TYPED,      /**< editor: a character, in @c typed       */
    UI_TEXTKEY_BACKSPACE,  /**< editor: DEL                            */
    UI_TEXTKEY_ENTER,      /**< editor: ENTER                          */
    UI_TEXTKEY_LEFT,       /**< editor: "<-"                           */
    UI_TEXTKEY_RIGHT,      /**< editor: "->"                           */
    UI_TEXTKEY_DONE,       /**< editor: DONE, the keyboard closed      */
} ui_textkey_result_t;

/** @brief The Wi-Fi keyboard's layers, and the editor's. */
typedef enum {
    UI_TK_LOWER = 0,       /**< lower case, and the digits but in the
                                editor                                 */
    UI_TK_UPPER,           /**< capitals, and the digits but in the
                                editor                                 */
    UI_TK_MARKS,           /**< the 32 marks; in the editor the digits
                                and 17 marks                           */
    UI_TK_MORE,            /**< the editor's other 15 marks            */
    UI_TK_LAYERS           /**< the count of the layers                */
} ui_textkey_layer_t;

/** @brief The editor's rows of keys: three of characters, no digit row,
 *         and the bottom row. */
#define UI_TEXTKEY_EDITOR_ROWS 4
/** @brief The editor's see-through steps: 0, 25 and 50% of the text
 *         behind shows through its keys. */
#define UI_TEXTKEY_SEE_STEPS 3
/** @brief The percent between two see-through steps. */
#define UI_TEXTKEY_SEE_STEP 25
/** @brief The most see-through, percent: the last step. */
#define UI_TEXTKEY_SEE_MAX ((UI_TEXTKEY_SEE_STEPS - 1) * UI_TEXTKEY_SEE_STEP)

/** @brief A keyboard: what it was opened for, what is typed, and the
 *         press it holds. */
typedef struct {
    bool       open;        /**< drawn and taking events              */
    gfx_rect_t area;        /**< where it draws, screen coordinates   */
    char       title[48];   /**< what is typed, top left              */
    char       text[UI_TEXTKEY_PASS_MAX + 1]; /**< the text, '\0'-ended */
    int        len;         /**< bytes in text                        */
    int        max_len;     /**< the most it takes                    */
    bool       refused;     /**< OK on an empty name                  */
    bool       search;      /**< a search pattern: see the top        */
    bool       folder;      /**< a folder of the card: see the top    */
    bool       file;        /**< a file name: see the top             */
    const char *ok_label;   /**< what OK reads; NULL is "OK"           */
    uint8_t    why;         /**< a folder's refusal, card_folder_t; a
                                 file name's, fm_name_t              */
    bool       wifi;        /**< a Wi-Fi name or password             */
    bool       secret;      /**< a password: masked, 8 at least       */
    bool       show;        /**< the password shown                   */
    ui_textkey_layer_t layer; /**< the Wi-Fi layer shown              */
    bool       editor;      /**< the text editor's keys: see the top  */
    uint8_t    see;         /**< editor: percent of the text behind
                                 that shows through, 0 to
                                 UI_TEXTKEY_SEE_MAX                   */
    char       typed;       /**< editor: the character of the last
                                 UI_TEXTKEY_TYPED                     */
    int        pressed;     /**< the key under the press, or -1       */
    uint8_t    press_id;    /**< the touch id of that press           */
    uint32_t   revision;    /**< bumped on every change a render shows */
} ui_textkey_t;

/**
 * @brief Open over @p area on @p text.
 * @param k       The keyboard; NULL does nothing.
 * @param area    Where it draws and takes presses.
 * @param title   What is typed, top left.
 * @param text    The text it starts with, cut to @p max_len.
 * @param max_len The most characters it takes; 0, less, or more than
 *                UI_TEXTKEY_MAX is UI_TEXTKEY_MAX.
 */
void ui_textkey_open(ui_textkey_t *k, gfx_rect_t area, const char *title,
                     const char *text, int max_len);

/**
 * @brief Open for a search pattern; see the top of this file.
 * @param k       The keyboard; NULL does nothing.
 * @param area    Where it draws.
 * @param title   What is searched for.
 * @param text    The pattern it starts with.
 * @param max_len The most characters, as ui_textkey_open() takes it.
 */
void ui_textkey_open_search(ui_textkey_t *k, gfx_rect_t area,
                            const char *title, const char *text,
                            int max_len);

/**
 * @brief Open for a folder of the card; see the top of this file.  Up to
 *        UI_TEXTKEY_MAX characters.
 * @param k     The keyboard; NULL does nothing.
 * @param area  Where it draws.
 * @param title The setting's name.
 * @param text  The folder in force.
 */
void ui_textkey_open_folder(ui_textkey_t *k, gfx_rect_t area,
                            const char *title, const char *text);

/**
 * @brief Open for a Wi-Fi password or network name; see the top of this
 *        file.  Lower case first.
 * @param k      The keyboard; NULL does nothing.
 * @param area   Where it draws.
 * @param title  What is typed.
 * @param text   The text it starts with.
 * @param secret A password: masked, 8 to 63 characters; else a name of 1
 *               to 32 bytes.
 */
void ui_textkey_open_wifi(ui_textkey_t *k, gfx_rect_t area,
                          const char *title, const char *text, bool secret);

/**
 * @brief Open for a file name; see the top of this file.  Lower case
 *        first.
 * @param k     The keyboard; NULL does nothing.
 * @param area  Where it draws.
 * @param title What is typed.
 * @param text  The name it starts with, cut to FM_NAME_MAX bytes between
 *              two characters.
 */
void ui_textkey_open_file(ui_textkey_t *k, gfx_rect_t area,
                          const char *title, const char *text);

/**
 * @brief Open for the text editor; see the top of this file.  Lower case
 *        first.
 * @param k     The keyboard; NULL does nothing.
 * @param area  Where it draws: its keys fill it, 8px in, and end at
 *              UI_KEYS_FOOT where it reaches further.
 */
void ui_textkey_open_editor(ui_textkey_t *k, gfx_rect_t area);

/**
 * @brief The percent of the text behind that shows through the editor's
 *        keyboard at see-through step @p step (SETUP > PANEL's Keyboard
 *        see-through): 0 (OFF), 25 or 50.
 * @param step The step, 0 to UI_TEXTKEY_SEE_STEPS - 1; one below is 0, one
 *             above the last step.
 * @return The percent, 0 to UI_TEXTKEY_SEE_MAX.
 */
int ui_textkey_see_pct(int step);

/**
 * @brief How much of the text behind the editor's keyboard shows through
 *        its keys and gaps.  A keyboard opened for anything else stays
 *        opaque.
 * @param k   The keyboard; NULL does nothing.
 * @param pct The percent; below 0 is 0, above UI_TEXTKEY_SEE_MAX is
 *            UI_TEXTKEY_SEE_MAX.
 */
void ui_textkey_set_see(ui_textkey_t *k, int pct);

/**
 * @brief The editor's key that types @p ch, and the layer it is on.
 * @param ch    A printable ASCII character but the space, 0x21 to 0x7E.
 * @param layer Takes the layer; NULL is allowed.
 * @return The key, 10 to UI_TK_DEL - 1; -1 for any other character.
 */
int ui_textkey_editor_key(char ch, ui_textkey_layer_t *layer);

/**
 * @brief The editor's key that leads from layer @p from towards layer
 *        @p to: UI_TK_SHIFT within the letters and within the marks,
 *        UI_TK_LAYER between the two.
 * @param from The layer shown.
 * @param to   The layer wanted.
 * @param next Takes the layer the key shows; NULL is allowed.
 * @return The key; -1 when @p from is @p to or either is out of range.
 */
int ui_textkey_editor_step(ui_textkey_layer_t from, ui_textkey_layer_t to,
                           ui_textkey_layer_t *next);

/**
 * @brief Close, dropping a press under way.
 * @param k The keyboard; NULL does nothing.
 */
void ui_textkey_close(ui_textkey_t *k);

/**
 * @brief A press under way is dropped: the touch stream lost an event.
 *        What was typed stays typed.
 * @param k The keyboard; NULL does nothing.
 */
void ui_textkey_cancel_press(ui_textkey_t *k);

/**
 * @brief One touch event.
 *
 * A key acts on the release of the contact that pressed it, over the same
 * key; a second contact is ignored while one is down.
 * @param k   The keyboard.
 * @param evt The event, in the keyboard's coordinates.
 * @param out Where OK writes the name, ends trimmed; a search's text as
 *            typed; a folder in upper case, as card_folder_check() writes
 *            it.
 * @param n   The bytes at @p out: CARD_DIR_MAX or more for a folder.
 * @return UI_TEXTKEY_OK or UI_TEXTKEY_CANCELLED when it closed,
 *         UI_TEXTKEY_NONE otherwise and for a NULL or closed keyboard; in
 *         the editor, the key's answer (see the top of this file).
 */
ui_textkey_result_t ui_textkey_event(ui_textkey_t *k, const touch_event_t *evt,
                                     char *out, size_t n);

/**
 * @brief Where a key is drawn and hit: the grid ends 8px inside the
 *        area, or at UI_KEYS_FOOT (4px above the alert band, body
 *        coordinates) where the area reaches further.
 * @param k   The keyboard.
 * @param key The key: 0 to UI_TK_OK, UI_TK_SHOW on a password, the
 *            characters from 10, UI_TK_SPACE and UI_TK_ENTER to UI_TK_DONE
 *            in the editor.
 * @return Its rectangle; zero size for one out of range or not shown.
 */
gfx_rect_t ui_textkey_key_rect(const ui_textkey_t *k, int key);

/**
 * @brief A key's label as a name keyboard shows it.
 * @param key The key.
 * @return The label, or "" for one out of range.
 */
const char *ui_textkey_label(int key);

/**
 * @brief A key's label as @p k shows it.
 * @param k   The keyboard.
 * @param key The key.
 * @return "*" for UI_TK_MARK in a search, "/" for UI_TK_DOT for a folder,
 *         the layer's character on a Wi-Fi keyboard and in the editor, ""
 *         for a blank key and the editor's digit row, the editor's ENTER,
 *         "<-", "->" and DONE, else ui_textkey_label().
 */
const char *ui_textkey_key_label(const ui_textkey_t *k, int key);

/**
 * @brief The face of the character keys: the largest of 20 x 36, 16 x 28,
 *        12 x 24 and 10 x 20 whose cell fits a character key with 4px on
 *        each side, else 8 x 16.  Over the WI-FI screen's 1008 x 484px a
 *        key is 93 x 71px and takes 20 x 36.
 * @param k The keyboard, opened.
 * @return The face; 8 x 16 for NULL.
 */
const gfx_font_t *ui_textkey_char_font(const ui_textkey_t *k);

/**
 * @brief The face of the word keys (the bottom row, DEL, and the Wi-Fi
 *        layer keys) in the language shown: the largest of 20 x 36,
 *        16 x 28, 12 x 24 and 10 x 20, never larger than
 *        ui_textkey_char_font(), in which every one of them fits its key
 *        with 4px each side, in every state it can show (SHOW and HIDE,
 *        ABC, abc, 123 and #+=), so the words of one keyboard share one face
 *        and a press does not change it; else 8 x 16.
 * @param k The keyboard, opened.
 * @return The face; 8 x 16 for NULL.
 */
const gfx_font_t *ui_textkey_word_font(const ui_textkey_t *k);

/**
 * @brief The whole keyboard over its area: the title and the count in
 *        10 x 20 (8 x 16 where they do not fit side by side), the text in
 *        20 x 36 (16 x 28 in a field narrower than UI_TEXTKEY_MAX
 *        characters and the caret), the character keys in
 *        ui_textkey_char_font() and the word keys in
 *        ui_textkey_word_font().  The editor's keyboard with a see-through
 *        above 0 blends its fills over what the canvas holds, the labels
 *        and the outline opaque.  Nothing while it is closed.
 * @param k The keyboard.
 * @param c Canvas.
 */
void ui_textkey_render(const ui_textkey_t *k, gfx_canvas_t *c);

#ifdef __cplusplus
}
#endif
