/**
 * @file
 * @brief The numeric keypad.  See ui_keypad.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "ui_keypad.h"

#include <stdio.h>
#include <string.h>

#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

/*
 * A title row, the entry, then four rows of keys, 8 px apart.  Over an
 * area of 620 x 476 px, a workbench's left card, a key is 145 x 84 px
 * (21.8 x 12.6 mm at 0.150 mm a pixel).
 */
/** @brief The room around the title, the entry and the keys: 8 px. */
#define KP_PAD     8
/** @brief The title row: the 10 x 20 face and 6 px above it. */
#define KP_HDR     32
/** @brief The entry: the 32 x 44 numerals and 8 px above and below. */
#define KP_ENTRY_H 60
/** @brief The gap between the entry and the keys, and between keys. */
#define KP_GAP     8
/** @brief The key grid: 4 columns ... */
#define KP_COLS    4
/** @brief ... and 4 rows. */
#define KP_ROWS    4
/** @brief The room a key keeps left and right of its label: 6 px. */
#define KP_KEY_PAD 6

/* DEL, CLR and +/- are the keys' legends in every language (the faces
 * hold no plus-minus glyph); CANCEL is translated, so it is looked up where
 * it is drawn. */
static const char *const k_labels[UI_KEY_COUNT] = {
    "7", "8", "9", "DEL",
    "4", "5", "6", "CLR",
    "1", "2", "3", NULL,
    "0", ".", "OK",
    "+/-",
};

void ui_keypad_open(ui_keypad_t *k, gfx_rect_t area, const char *title,
                    const char *unit, float value, float min, float max,
                    int decimals)
{
    if (k == NULL) {
        return;
    }
    const uint32_t rev = k->revision;
    memset(k, 0, sizeof(*k));
    k->open     = true;
    k->area     = area;
    k->value    = value;
    k->min      = min;
    k->max      = max;
    k->decimals = (decimals < 0) ? 0 : decimals;
    k->pressed  = -1;
    k->revision = rev + 1u;
    snprintf(k->title, sizeof(k->title), "%s", (title != NULL) ? title : "");
    snprintf(k->unit, sizeof(k->unit), "%s", (unit != NULL) ? unit : "");
}

void ui_keypad_open_signed(ui_keypad_t *k, gfx_rect_t area, const char *title,
                           const char *unit, float value, float min,
                           float max, int decimals)
{
    ui_keypad_open(k, area, title, unit, value, min, max, decimals);
    if (k != NULL) {
        k->sign = true;
    }
}

void ui_keypad_close(ui_keypad_t *k)
{
    if (k == NULL) {
        return;
    }
    k->open    = false;
    k->pressed = -1;
    ++k->revision;
}

void ui_keypad_refuse(ui_keypad_t *k)
{
    if (k == NULL) {
        return;
    }
    k->open    = true;
    k->refused = true;
    k->pressed = -1;
    k->reason[0] = '\0';
    ++k->revision;
}

void ui_keypad_refuse_why(ui_keypad_t *k, const char *reason)
{
    ui_keypad_refuse(k);
    if (k == NULL || reason == NULL) {
        return;
    }
    size_t len = strlen(reason);
    if (len >= sizeof(k->reason)) {
        /* Cut at the start of a UTF-8 character, never inside one: back
         * over continuation bytes (10xxxxxx) to the byte that starts the
         * character the cut would split. */
        len = sizeof(k->reason) - 1u;
        while (len > 0u && ((unsigned char)reason[len] & 0xC0u) == 0x80u) {
            --len;
        }
    }
    memcpy(k->reason, reason, len);
    k->reason[len] = '\0';
}

void ui_keypad_cancel_press(ui_keypad_t *k)
{
    if (k != NULL && k->pressed >= 0) {
        k->pressed = -1;
        ++k->revision;
    }
}

gfx_rect_t ui_keypad_key_rect(const ui_keypad_t *k, ui_key_t key)
{
    if (k == NULL || key < 0 || key >= UI_KEY_COUNT) {
        return (gfx_rect_t){ 0, 0, 0, 0 };
    }
    const int left   = k->area.x + KP_PAD;
    const int right  = k->area.x + k->area.w - KP_PAD;
    const int top    = k->area.y + KP_HDR + KP_ENTRY_H + 2 * KP_GAP;
    const int bottom = k->area.y + k->area.h - KP_PAD;
    const int kw = (right - left - (KP_COLS - 1) * KP_GAP) / KP_COLS;
    const int kh = (bottom - top - (KP_ROWS - 1) * KP_GAP) / KP_ROWS;
    int row  = (int)key / KP_COLS;
    int col  = (int)key % KP_COLS;
    int span = (key == UI_KEY_OK) ? 2 : 1;
    if (key == UI_KEY_SIGN) {
        if (!k->sign) {
            return (gfx_rect_t){ 0, 0, 0, 0 };
        }
        row = KP_ROWS - 1;          /* beside the point, OK's first column */
        col = 2;
    } else if (key == UI_KEY_OK && k->sign) {
        col  = 3;
        span = 1;
    }
    return (gfx_rect_t){ (int16_t)(left + col * (kw + KP_GAP)),
                         (int16_t)(top + row * (kh + KP_GAP)),
                         (int16_t)(span * kw + (span - 1) * KP_GAP),
                         (int16_t)kh };
}

/**
 * @brief What has been typed, as a number.  The entry holds an optional
 *        leading minus, digits and at most one point, so nothing here can
 *        fail.
 * @param s The entry.
 * @return Its value; "-0" and "-0.0" are 0, never a negative zero.
 */
static float parse(const char *s)
{
    float whole = 0.0f;
    float frac  = 0.0f;
    float place = 1.0f;
    bool  after = false;
    const bool minus = (*s == '-');
    for (const char *p = minus ? s + 1 : s; *p != '\0'; ++p) {
        if (*p == '.') {
            after = true;
        } else if (!after) {
            whole = whole * 10.0f + (float)(*p - '0');
        } else {
            place *= 0.1f;
            frac  += (float)(*p - '0') * place;
        }
    }
    const float v = whole + frac;
    return (minus && v != 0.0f) ? -v : v;
}

/**
 * @brief Whether the entry holds a digit: "" and "-" are nothing typed.
 * @param k The keypad.
 * @return True when a digit has been typed.
 */
static bool typed(const ui_keypad_t *k)
{
    return k->len > ((k->entry[0] == '-') ? 1 : 0);
}

/**
 * @brief The +/- key: a minus sign put in front of the entry, or taken
 *        away.  An entry already UI_KEYPAD_ENTRY_MAX long takes none.
 * @param k The keypad.
 */
static void toggle_sign(ui_keypad_t *k)
{
    if (k->entry[0] == '-') {
        memmove(k->entry, k->entry + 1, (size_t)k->len);   /* with the NUL */
        --k->len;
    } else if (k->len < UI_KEYPAD_ENTRY_MAX) {
        memmove(k->entry + 1, k->entry, (size_t)k->len + 1u);
        k->entry[0] = '-';
        ++k->len;
    }
}

static int digits_after_point(const ui_keypad_t *k)
{
    const char *dot = strchr(k->entry, '.');
    return (dot != NULL) ? (int)strlen(dot + 1) : -1;
}

/**
 * @brief A digit or the point added to the entry, if the entry has room
 *        and the value keeps it: one point, none on a whole number, no
 *        digit past the decimals.  A point on nothing typed, or after a
 *        lone minus sign, puts a 0 before it.
 * @param k The keypad.
 * @param ch '0' to '9' or '.'.
 */
static void append(ui_keypad_t *k, char ch)
{
    if (k->len >= UI_KEYPAD_ENTRY_MAX) {
        return;
    }
    const int after = digits_after_point(k);
    if (ch == '.') {
        if (after >= 0 || k->decimals == 0) {
            return;                 /* one point, and only where it means one */
        }
        if (!typed(k)) {
            k->entry[k->len++] = '0';
        }
    } else if (after >= k->decimals) {
        return;                     /* finer than the value is kept */
    }
    k->entry[k->len++] = ch;
    k->entry[k->len] = '\0';
}

/**
 * @brief Key @p key pressed and released.
 * @param k The keypad.
 * @param key The key.
 * @param out Where OK writes a value inside the range.
 * @return UI_KEYPAD_OK with a value, UI_KEYPAD_CANCEL on CANCEL and on OK
 *         with nothing typed, UI_KEYPAD_NONE otherwise, a refusal included.
 */
static ui_keypad_result_t press(ui_keypad_t *k, ui_key_t key, float *out)
{
    static const char k_digit[UI_KEY_COUNT] = {
        '7', '8', '9', 0, '4', '5', '6', 0, '1', '2', '3', 0, '0', '.', 0,
    };
    k->refused = false;
    k->reason[0] = '\0';           /* the range again */
    ++k->revision;
    switch (key) {
    case UI_KEY_DEL:
        if (k->len > 0) {
            k->entry[--k->len] = '\0';
        }
        return UI_KEYPAD_NONE;
    case UI_KEY_CLR:
        k->len = 0;
        k->entry[0] = '\0';
        return UI_KEYPAD_NONE;
    case UI_KEY_CANCEL:
        ui_keypad_close(k);
        return UI_KEYPAD_CANCEL;
    case UI_KEY_SIGN:
        toggle_sign(k);
        return UI_KEYPAD_NONE;
    case UI_KEY_OK: {
        if (!typed(k)) {
            /* Nothing typed: the value stays as it was. */
            ui_keypad_close(k);
            return UI_KEYPAD_CANCEL;
        }
        const float v = parse(k->entry);
        /* Half a unit of the last place either side, so a range end that is
         * not exact in binary can still be typed. */
        float slack = 0.5f;
        for (int i = 0; i < k->decimals; ++i) {
            slack *= 0.1f;
        }
        if (v < k->min - slack || v > k->max + slack) {
            k->refused = true;
            k->reason[0] = '\0';
            return UI_KEYPAD_NONE;
        }
        if (out != NULL) {
            *out = (v < k->min) ? k->min : (v > k->max) ? k->max : v;
        }
        ui_keypad_close(k);
        return UI_KEYPAD_OK;
    }
    default:
        append(k, k_digit[key]);
        return UI_KEYPAD_NONE;
    }
}

ui_keypad_result_t ui_keypad_event(ui_keypad_t *k, const touch_event_t *evt,
                                   float *out)
{
    if (k == NULL || evt == NULL || !k->open) {
        return UI_KEYPAD_NONE;
    }
    const int x = evt->point.x;
    const int y = evt->point.y;
    if (evt->type == TOUCH_EVENT_DOWN) {
        if (k->pressed >= 0 && evt->point.id != k->press_id) {
            return UI_KEYPAD_NONE;   /* one press at a time */
        }
        k->pressed = -1;
        for (int key = 0; key < UI_KEY_COUNT; ++key) {
            if (gfx_rect_contains(ui_keypad_key_rect(k, (ui_key_t)key), x, y)) {
                k->pressed  = key;
                k->press_id = evt->point.id;
                break;
            }
        }
        ++k->revision;
        return UI_KEYPAD_NONE;
    }
    if (k->pressed < 0 || evt->point.id != k->press_id
        || evt->type != TOUCH_EVENT_UP) {
        return UI_KEYPAD_NONE;
    }
    const ui_key_t key = (ui_key_t)k->pressed;
    k->pressed = -1;
    ++k->revision;
    /* A press that slid off its key is not a press on it, and neither is
     * a release the finger did not make. */
    if (!touch_event_is_tap_up(evt)
        || !gfx_rect_contains(ui_keypad_key_rect(k, key), x, y)) {
        return UI_KEYPAD_NONE;
    }
    return press(k, key, out);
}

/**
 * @brief The face the title and the range share: 10 x 20 when both fit the
 *        header side by side with 16 px between them, else 8 x 16.
 * @param hdr   The header row.
 * @param title The title.
 * @param range The range, or the caller's reason.
 * @return The face.
 */
static const gfx_font_t *header_face(gfx_rect_t hdr, const char *title,
                                     const char *range)
{
    const int need = gfx_text_width(&gfx_font_10x20, title, 1) + 16
                     + gfx_text_width(&gfx_font_10x20, range, 1);
    return (need <= hdr.w) ? &gfx_font_10x20 : &gfx_font_8x16;
}

const gfx_font_t *ui_keypad_word_font(const ui_keypad_t *k)
{
    if (k == NULL) {
        return &gfx_font_8x16;
    }
    static const ui_key_t words[] = { UI_KEY_DEL, UI_KEY_CLR, UI_KEY_CANCEL,
                                      UI_KEY_OK, UI_KEY_SIGN };
    static const gfx_font_t *const faces[] = { &gfx_font_12x24,
                                               &gfx_font_10x20 };
    for (size_t f = 0; f < sizeof(faces) / sizeof(faces[0]); ++f) {
        bool fit = true;
        for (size_t i = 0; i < sizeof(words) / sizeof(words[0]) && fit; ++i) {
            const gfx_rect_t r = ui_keypad_key_rect(k, words[i]);
            const char *label = (words[i] == UI_KEY_CANCEL)
                                    ? TR(CANCEL) : k_labels[words[i]];
            fit = r.w == 0 || ui_text_fits(faces[f], label, r.w, r.h,
                                           KP_KEY_PAD);
        }
        if (fit) {
            return faces[f];
        }
    }
    return &gfx_font_8x16;
}

/**
 * @brief The entry: @p number in the numeral face and @p unit beside it,
 *        right-aligned 10 px inside @p box.  32 x 44 numerals with a
 *        12 x 24 unit where they fit, else 24 x 30 with 10 x 20.
 * @param c      Canvas.
 * @param box    The entry's box.
 * @param number The number: digits, a minus sign and a point.
 * @param unit   The unit, or "".
 * @param ink    The colour of both.
 */
static void entry_text(gfx_canvas_t *c, gfx_rect_t box, const char *number,
                       const char *unit, gfx_color_t ink)
{
    const gfx_font_t *num  = &gfx_font_num_32x44;
    const gfx_font_t *unf  = &gfx_font_12x24;
    const int gap = (unit[0] != '\0') ? 10 : 0;
    if (gfx_text_width(num, number, 1) + gap + gfx_text_width(unf, unit, 1)
            + 20 > box.w
        || gfx_text_height(num, 1) > box.h) {
        num = &gfx_font_num_24x30;
        unf = &gfx_font_10x20;
    }
    const int uw = gfx_text_width(unf, unit, 1);
    const int nw = gfx_text_width(num, number, 1);
    const int nh = gfx_text_height(num, 1);
    const int x  = box.x + box.w - 10 - uw - gap - nw;
    const int y  = box.y + (box.h - nh) / 2;
    gfx_text_in(c, (gfx_rect_t){ (int16_t)x, (int16_t)y, (int16_t)nw,
                                 (int16_t)nh },
                number, num, ink, 1, GFX_ALIGN_LEFT);
    if (uw > 0) {
        /* The unit's baseline on the numerals': both faces end their
         * digits and capitals 2 px above the cell's foot or less. */
        const int uh = gfx_text_height(unf, 1);
        gfx_text_in(c, (gfx_rect_t){ (int16_t)(x + nw + gap),
                                     (int16_t)(y + nh - uh), (int16_t)uw,
                                     (int16_t)uh },
                    unit, unf, ink, 1, GFX_ALIGN_LEFT);
    }
}

void ui_keypad_render(const ui_keypad_t *k, gfx_canvas_t *c)
{
    if (k == NULL || c == NULL || !k->open) {
        return;
    }
    const gfx_rect_t a = k->area;
    gfx_fill_rect(c, a.x, a.y, a.w, a.h, ui_theme_color(UI_C_PANEL));
    gfx_draw_rect(c, a.x, a.y, a.w, a.h, ui_theme_color(UI_C_ACCENT));

    /* The title, and the range a value has to be in: in the warning colour
     * once a value outside it has been refused.  A value the caller refused
     * by its own rule shows the caller's reason there instead. */
    const gfx_rect_t hdr = { (int16_t)(a.x + KP_PAD), (int16_t)(a.y + 6),
                             (int16_t)(a.w - 2 * KP_PAD), 22 };
    char range[64];
    if (k->refused && k->reason[0] != '\0') {
        snprintf(range, sizeof(range), "%s", k->reason);   /* why, instead */
    } else {
        snprintf(range, sizeof(range), TR(KEYPAD_RANGE), k->decimals,
                 (double)k->min, k->decimals, (double)k->max, k->unit);
    }
    const gfx_font_t *hf = header_face(hdr, k->title, range);
    gfx_text_in(c, hdr, k->title, hf, ui_theme_color(UI_C_TEXT), 1,
                GFX_ALIGN_LEFT);
    gfx_text_in(c, hdr, range, hf,
                k->refused ? ui_theme_color(UI_C_WARN)
                           : ui_theme_color(UI_C_TEXT_DIM),
                1, GFX_ALIGN_RIGHT);

    /* The entry: what has been typed, or the value as it is, faint. */
    const gfx_rect_t box = { (int16_t)(a.x + KP_PAD),
                             (int16_t)(a.y + KP_HDR + KP_GAP),
                             (int16_t)(a.w - 2 * KP_PAD), KP_ENTRY_H };
    gfx_fill_rect(c, box.x, box.y, box.w, box.h,
                  ui_theme_color(UI_C_PANEL_SUNK));
    char number[32];
    if (k->len > 0) {
        snprintf(number, sizeof(number), "%s", k->entry);
    } else {
        snprintf(number, sizeof(number), "%.*f", k->decimals,
                 (double)k->value);
    }
    entry_text(c, box, number, k->unit,
               (k->len > 0) ? ui_theme_color(UI_C_TEXT)
                            : ui_theme_color(UI_C_TEXT_FAINT));

    /* Digits and the point in 12 x 24; the words in the face they all
     * fit. */
    const gfx_font_t *wf = ui_keypad_word_font(k);
    for (int key = 0; key < UI_KEY_COUNT; ++key) {
        if (key == UI_KEY_SIGN && !k->sign) {
            continue;                   /* an unsigned keypad has none */
        }
        const bool word = key == UI_KEY_DEL || key == UI_KEY_CLR
                          || key == UI_KEY_CANCEL || key == UI_KEY_OK
                          || key == UI_KEY_SIGN;
        const gfx_color_t fill = (key == UI_KEY_OK)
                                     ? ui_theme_color(UI_C_ACCENT)
                                     : ui_theme_color(UI_C_PANEL_SUNK);
        ui_button_font(c, ui_keypad_key_rect(k, (ui_key_t)key),
                       (key == UI_KEY_CANCEL) ? TR(CANCEL) : k_labels[key],
                       fill, k->pressed == key, true,
                       word ? wf : &gfx_font_12x24);
    }
}
