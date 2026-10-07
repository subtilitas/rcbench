/*
 * The text keyboard.  See ui_textkey.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "ui_textkey.h"

#include <stdio.h>
#include <string.h>

#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

/*
 * A title row, the text, then five rows of keys.  Over the 488 x 418 px the
 * SERVO screen gives it, a character key is 43 x 59 px.
 */
#define TK_PAD     8
#define TK_HDR     28
#define TK_ENTRY_H 44
#define TK_GAP     4
#define TK_COLS    10
#define TK_ROWS    5

/* The character rows, in reading order; the last is DEL. */
static const char k_chars[UI_TEXTKEY_CHARS + 1] =
    "1234567890" "QWERTYUIOP" "ASDFGHJKL-" "ZXCVBNM_.";

/* SPACE and CANCEL are translated; CLR and OK are legends. */
static const ui_text_id_t k_bottom_tx[] = { TX_SPACE, TX_COUNT, TX_CANCEL,
                                            TX_COUNT };
static const char *const k_bottom[] = { NULL, "CLR", NULL, "OK" };
/* How many columns each bottom key spans, for a name and for a search. */
static const int k_bottom_span[] = { 4, 2, 2, 2 };
static const int k_search_span[] = { 3, 2, 3, 2 };

void ui_textkey_open(ui_textkey_t *k, gfx_rect_t area, const char *title,
                     const char *text, int max_len)
{
    if (k == NULL) {
        return;
    }
    const uint32_t rev = k->revision;
    memset(k, 0, sizeof(*k));
    k->open     = true;
    k->area     = area;
    k->pressed  = -1;
    k->max_len  = (max_len > 0 && max_len < UI_TEXTKEY_MAX) ? max_len
                                                            : UI_TEXTKEY_MAX;
    k->revision = rev + 1u;
    snprintf(k->title, sizeof(k->title), "%s", (title != NULL) ? title : "");
    snprintf(k->text, (size_t)k->max_len + 1u, "%s",
             (text != NULL) ? text : "");
    k->len = (int)strlen(k->text);
}

void ui_textkey_open_search(ui_textkey_t *k, gfx_rect_t area,
                            const char *title, const char *text,
                            int max_len)
{
    ui_textkey_open(k, area, title, text, max_len);
    if (k != NULL) {
        k->search = true;
    }
}

void ui_textkey_close(ui_textkey_t *k)
{
    if (k == NULL) {
        return;
    }
    k->open    = false;
    k->pressed = -1;
    ++k->revision;
}

void ui_textkey_cancel_press(ui_textkey_t *k)
{
    if (k != NULL && k->pressed >= 0) {
        k->pressed = -1;
        ++k->revision;
    }
}

const char *ui_textkey_label(int key)
{
    if (key < 0 || key >= UI_TK_KEYS) {
        return "";
    }
    if (key == UI_TK_DEL) {
        return "DEL";
    }
    if (key < UI_TEXTKEY_CHARS) {
        static char one[2];
        one[0] = k_chars[key];
        one[1] = '\0';
        return one;
    }
    const int b = key - UI_TEXTKEY_CHARS;
    return (k_bottom[b] != NULL) ? k_bottom[b] : ui_tr(k_bottom_tx[b]);
}

const char *ui_textkey_key_label(const ui_textkey_t *k, int key)
{
    if (k != NULL && k->search && key == UI_TK_MARK) {
        return "*";
    }
    return ui_textkey_label(key);
}

gfx_rect_t ui_textkey_key_rect(const ui_textkey_t *k, int key)
{
    if (k == NULL || key < 0 || key >= UI_TK_KEYS) {
        return (gfx_rect_t){ 0, 0, 0, 0 };
    }
    const int left   = k->area.x + TK_PAD;
    const int right  = k->area.x + k->area.w - TK_PAD;
    const int top    = k->area.y + TK_HDR + TK_ENTRY_H + 2 * TK_GAP;
    const int bottom = k->area.y + k->area.h - TK_PAD;
    const int kw = (right - left - (TK_COLS - 1) * TK_GAP) / TK_COLS;
    const int kh = (bottom - top - (TK_ROWS - 1) * TK_GAP) / TK_ROWS;
    int row, col, span;
    if (key < UI_TEXTKEY_CHARS) {
        row  = key / TK_COLS;
        col  = key % TK_COLS;
        span = 1;
    } else {
        const int *spans = k->search ? k_search_span : k_bottom_span;
        row  = TK_ROWS - 1;
        col  = 0;
        for (int i = 0; i < key - UI_TEXTKEY_CHARS; ++i) {
            col += spans[i];
        }
        span = spans[key - UI_TEXTKEY_CHARS];
    }
    return (gfx_rect_t){ (int16_t)(left + col * (kw + TK_GAP)),
                         (int16_t)(top + row * (kh + TK_GAP)),
                         (int16_t)(span * kw + (span - 1) * TK_GAP),
                         (int16_t)kh };
}

static void add(ui_textkey_t *k, char ch)
{
    if (k->len < k->max_len) {
        k->text[k->len++] = ch;
        k->text[k->len] = '\0';
    }
}

static ui_textkey_result_t press(ui_textkey_t *k, int key, char *out,
                                 size_t n)
{
    k->refused = false;
    ++k->revision;
    if (key == UI_TK_DEL) {
        if (k->len > 0) {
            k->text[--k->len] = '\0';
        }
        return UI_TEXTKEY_NONE;
    }
    if (key < UI_TEXTKEY_CHARS) {
        add(k, (k->search && key == UI_TK_MARK) ? '*' : k_chars[key]);
        return UI_TEXTKEY_NONE;
    }
    switch (key) {
    case UI_TK_SPACE:
        add(k, ' ');
        return UI_TEXTKEY_NONE;
    case UI_TK_CLR:
        k->len = 0;
        k->text[0] = '\0';
        return UI_TEXTKEY_NONE;
    case UI_TK_CANCEL:
        ui_textkey_close(k);
        return UI_TEXTKEY_CANCELLED;
    default: {                       /* OK */
        if (k->search) {             /* as typed: what the list shows */
            if (out != NULL && n > 0u) {
                snprintf(out, n, "%s", k->text);
            }
            ui_textkey_close(k);
            return UI_TEXTKEY_OK;
        }
        const char *a = k->text;
        while (*a == ' ') {
            ++a;
        }
        size_t len = strlen(a);
        while (len > 0u && a[len - 1u] == ' ') {
            --len;
        }
        if (len == 0u) {
            k->refused = true;       /* a name is what a report is filed under */
            return UI_TEXTKEY_NONE;
        }
        if (out != NULL && n > 0u) {
            const size_t m = (len < n - 1u) ? len : n - 1u;
            memcpy(out, a, m);
            out[m] = '\0';
        }
        ui_textkey_close(k);
        return UI_TEXTKEY_OK;
    }
    }
}

ui_textkey_result_t ui_textkey_event(ui_textkey_t *k, const touch_event_t *evt,
                                     char *out, size_t n)
{
    if (k == NULL || evt == NULL || !k->open) {
        return UI_TEXTKEY_NONE;
    }
    const int x = evt->point.x;
    const int y = evt->point.y;
    if (evt->type == TOUCH_EVENT_DOWN) {
        if (k->pressed >= 0 && evt->point.id != k->press_id) {
            return UI_TEXTKEY_NONE;     /* one press at a time */
        }
        k->pressed = -1;
        for (int key = 0; key < UI_TK_KEYS; ++key) {
            if (gfx_rect_contains(ui_textkey_key_rect(k, key), x, y)) {
                k->pressed  = key;
                k->press_id = evt->point.id;
                break;
            }
        }
        ++k->revision;
        return UI_TEXTKEY_NONE;
    }
    if (k->pressed < 0 || evt->point.id != k->press_id
        || evt->type != TOUCH_EVENT_UP) {
        return UI_TEXTKEY_NONE;
    }
    const int key = k->pressed;
    k->pressed = -1;
    ++k->revision;
    /* A press that slid off its key is not a press on it. */
    if (!gfx_rect_contains(ui_textkey_key_rect(k, key), x, y)) {
        return UI_TEXTKEY_NONE;
    }
    return press(k, key, out, n);
}

void ui_textkey_render(const ui_textkey_t *k, gfx_canvas_t *c)
{
    if (k == NULL || c == NULL || !k->open) {
        return;
    }
    const gfx_rect_t a = k->area;
    gfx_fill_rect(c, a.x, a.y, a.w, a.h, ui_theme_color(UI_C_PANEL));
    gfx_draw_rect(c, a.x, a.y, a.w, a.h, ui_theme_color(UI_C_ACCENT));

    const gfx_rect_t hdr = { (int16_t)(a.x + TK_PAD), (int16_t)(a.y + 6),
                             (int16_t)(a.w - 2 * TK_PAD), 18 };
    gfx_text_in(c, hdr, k->title, &gfx_font_8x16, ui_theme_color(UI_C_TEXT),
                1, GFX_ALIGN_LEFT);
    char room[40];
    if (k->refused) {
        snprintf(room, sizeof(room), "%s", TR(TEXTKEY_NEEDED));
    } else {
        snprintf(room, sizeof(room), TR(TEXTKEY_COUNT), k->len, k->max_len);
    }
    gfx_text_in(c, hdr, room, &gfx_font_8x16,
                k->refused ? ui_theme_color(UI_C_WARN)
                           : ui_theme_color(UI_C_TEXT_DIM),
                1, GFX_ALIGN_RIGHT);

    const gfx_rect_t box = { (int16_t)(a.x + TK_PAD),
                             (int16_t)(a.y + TK_HDR + TK_GAP),
                             (int16_t)(a.w - 2 * TK_PAD), TK_ENTRY_H };
    gfx_fill_rect(c, box.x, box.y, box.w, box.h,
                  ui_theme_color(UI_C_PANEL_SUNK));
    char shown[UI_TEXTKEY_MAX + 2];
    snprintf(shown, sizeof(shown), "%s_", k->text);   /* the caret */
    gfx_text_in(c, (gfx_rect_t){ (int16_t)(box.x + 10), box.y,
                                 (int16_t)(box.w - 20), box.h },
                shown, &gfx_font_8x16, ui_theme_color(UI_C_TEXT), 2,
                GFX_ALIGN_LEFT);

    for (int key = 0; key < UI_TK_KEYS; ++key) {
        const gfx_color_t fill = (key == UI_TK_OK)
                                     ? ui_theme_color(UI_C_ACCENT)
                                     : ui_theme_color(UI_C_PANEL_SUNK);
        ui_button(c, ui_textkey_key_rect(k, key),
                  ui_textkey_key_label(k, key), fill, k->pressed == key,
                  true);
    }
}
