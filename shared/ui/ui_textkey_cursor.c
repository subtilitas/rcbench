/**
 * @file
 * @brief The cursor of the file-name keyboard: typing at it, DEL before
 *        it, "<-" and "->", a tap in the field, and the field's window.
 *        See ui_textkey.h.
 *
 * The text is at most UI_TEXTKEY_PASS_MAX (63) bytes, so every step walks
 * it from the start with gfx_utf8_next(), the walk the drawing takes: a
 * character is what gfx draws in one cell, a malformed byte included.
 *
 * SPDX-License-Identifier: MIT
 */

#include "ui_textkey.h"

#include <string.h>

/**
 * @brief The characters in the first @p bytes bytes of @p text.
 * @param text  The text, '\0'-ended.
 * @param bytes A byte offset at the start of a character.
 * @return The count.
 */
static int chars_before(const char *text, int bytes)
{
    const char *p = text;
    int n = 0;
    while (*p != '\0' && (int)(p - text) < bytes) {
        (void)gfx_utf8_next(&p);
        ++n;
    }
    return n;
}

/**
 * @brief Where character @p idx of @p text starts.
 * @param text The text, '\0'-ended.
 * @param idx  The character index; past the last, the text's end.
 * @return The byte offset.
 */
static int byte_of(const char *text, int idx)
{
    const char *p = text;
    for (int i = 0; i < idx && *p != '\0'; ++i) {
        (void)gfx_utf8_next(&p);
    }
    return (int)(p - text);
}

/**
 * @brief The start of the character of @p text that holds byte @p byte.
 * @param text The text, '\0'-ended.
 * @param len  Its bytes.
 * @param byte The byte offset.
 * @return 0 for @p byte at or below 0, @p len at or past the end.
 */
static int snap(const char *text, int len, int byte)
{
    if (byte <= 0) {
        return 0;
    }
    if (byte >= len) {
        return len;
    }
    const char *p = text;
    while (*p != '\0') {
        const char *next = p;
        (void)gfx_utf8_next(&next);
        if ((int)(next - text) > byte) {
            break;
        }
        p = next;
    }
    return (int)(p - text);
}

/**
 * @brief The cursor back on a character's start inside the text: a caller
 *        that writes @c text and @c len itself can leave it anywhere.
 * @param k The keyboard.
 */
static void clamp(ui_textkey_t *k)
{
    k->cur = snap(k->text, k->len, k->cur);
}

/**
 * @brief The window kept where it shows the cursor, and a change a render
 *        shows counted.
 * @param k The keyboard.
 */
static void follow(ui_textkey_t *k)
{
    k->view = ui_textkey_view(k);
    ++k->revision;
}

int ui_textkey_cursor_chars(const ui_textkey_t *k)
{
    if (k == NULL) {
        return 0;
    }
    return chars_before(k->text, snap(k->text, k->len, k->cur));
}

int ui_textkey_view(const ui_textkey_t *k)
{
    if (k == NULL) {
        return 0;
    }
    const int fit = ui_textkey_field_cells(k);
    const int n = gfx_text_cells(k->text);
    const int c = ui_textkey_cursor_chars(k);
    int v = (k->view < 0) ? 0 : k->view;
    if (v > c) {
        v = c;                      /* the cursor left of the window */
    }
    if (c - v > fit) {
        v = c - fit;                /* right of it: the bar at its end */
    }
    if (v > 0 && n - v < fit) {
        v = (n > fit) ? n - fit : 0;   /* room after the text */
    }
    return v;
}

void ui_textkey_set_cursor(ui_textkey_t *k, int byte)
{
    if (k == NULL) {
        return;
    }
    k->cur = snap(k->text, k->len, byte);
    follow(k);
}

int ui_textkey_stem_end(const char *name)
{
    if (name == NULL) {
        return 0;
    }
    const char *dot = strrchr(name, '.');
    return (dot == NULL || dot == name) ? (int)strlen(name)
                                        : (int)(dot - name);
}

bool ui_textkey_insert(ui_textkey_t *k, const char *s)
{
    if (k == NULL || s == NULL || s[0] == '\0') {
        return false;
    }
    clamp(k);
    const size_t add = strlen(s);
    const size_t len = (size_t)k->len;
    if (len + add > (size_t)k->max_len || len + add >= sizeof(k->text)) {
        return false;               /* refused: the text and cursor stay */
    }
    char *at = k->text + k->cur;
    memmove(at + add, at, len - (size_t)k->cur + 1u);
    memcpy(at, s, add);
    k->len += (int)add;
    k->cur += (int)add;
    follow(k);
    return true;
}

void ui_textkey_backspace(ui_textkey_t *k)
{
    if (k == NULL) {
        return;
    }
    clamp(k);
    if (k->cur == 0) {
        return;
    }
    const int from = snap(k->text, k->len, k->cur - 1);
    memmove(k->text + from, k->text + k->cur,
            (size_t)(k->len - k->cur) + 1u);
    k->len -= k->cur - from;
    k->cur = from;
    follow(k);
}

void ui_textkey_move(ui_textkey_t *k, int chars)
{
    if (k == NULL) {
        return;
    }
    const int n = gfx_text_cells(k->text);
    int c = ui_textkey_cursor_chars(k) + chars;
    c = (c < 0) ? 0 : (c > n) ? n : c;
    k->cur = byte_of(k->text, c);
    follow(k);
}

void ui_textkey_tap_text(ui_textkey_t *k, int x)
{
    if (k == NULL) {
        return;
    }
    const gfx_rect_t box = ui_textkey_field_rect(k);
    const int w = ui_textkey_field_face(k)->width;
    const int fit = ui_textkey_field_cells(k);
    /* The gap nearest the tap: half a cell either side of it. */
    const int dx = x - (box.x + UI_TEXTKEY_FIELD_IN);
    int col = (dx < 0) ? 0 : (dx + w / 2) / w;
    col = (col > fit) ? fit : col;
    const int n = gfx_text_cells(k->text);
    int c = ui_textkey_view(k) + col;
    c = (c > n) ? n : c;
    k->cur = byte_of(k->text, c);
    follow(k);
}
