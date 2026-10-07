/*
 * The numeric keypad.  See ui_keypad.h.
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
 * A title row, the entry, then four rows of keys.  Over the 534 x 402 px the
 * SUPPLY screen gives it, a key is 126 x 70 px, larger than a fingertip on
 * every side.
 */
#define KP_PAD     8
#define KP_HDR     30
#define KP_ENTRY_H 52
#define KP_GAP     6
#define KP_COLS    4
#define KP_ROWS    4

/* DEL and CLR are the keys' legends in every language; CANCEL is
 * translated, so it is looked up where it is drawn. */
static const char *const k_labels[UI_KEY_COUNT] = {
    "7", "8", "9", "DEL",
    "4", "5", "6", "CLR",
    "1", "2", "3", NULL,
    "0", ".", "OK",
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

void ui_keypad_close(ui_keypad_t *k)
{
    if (k == NULL) {
        return;
    }
    k->open    = false;
    k->pressed = -1;
    ++k->revision;
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
    const int row  = (int)key / KP_COLS;
    const int col  = (int)key % KP_COLS;
    const int span = (key == UI_KEY_OK) ? 2 : 1;
    return (gfx_rect_t){ (int16_t)(left + col * (kw + KP_GAP)),
                         (int16_t)(top + row * (kh + KP_GAP)),
                         (int16_t)(span * kw + (span - 1) * KP_GAP),
                         (int16_t)kh };
}

/* What has been typed, as a number.  The entry holds digits and at most one
 * point, so nothing here can fail. */
static float parse(const char *s)
{
    float whole = 0.0f;
    float frac  = 0.0f;
    float place = 1.0f;
    bool  after = false;
    for (const char *p = s; *p != '\0'; ++p) {
        if (*p == '.') {
            after = true;
        } else if (!after) {
            whole = whole * 10.0f + (float)(*p - '0');
        } else {
            place *= 0.1f;
            frac  += (float)(*p - '0') * place;
        }
    }
    return whole + frac;
}

static int digits_after_point(const ui_keypad_t *k)
{
    const char *dot = strchr(k->entry, '.');
    return (dot != NULL) ? (int)strlen(dot + 1) : -1;
}

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
        if (k->len == 0) {
            k->entry[k->len++] = '0';
        }
    } else if (after >= k->decimals) {
        return;                     /* finer than the value is kept */
    }
    k->entry[k->len++] = ch;
    k->entry[k->len] = '\0';
}

static ui_keypad_result_t press(ui_keypad_t *k, ui_key_t key, float *out)
{
    static const char k_digit[UI_KEY_COUNT] = {
        '7', '8', '9', 0, '4', '5', '6', 0, '1', '2', '3', 0, '0', '.', 0,
    };
    k->refused = false;
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
    case UI_KEY_OK: {
        if (k->len == 0) {
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
    /* A press that slid off its key is not a press on it. */
    if (!gfx_rect_contains(ui_keypad_key_rect(k, key), x, y)) {
        return UI_KEYPAD_NONE;
    }
    return press(k, key, out);
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
     * once a value outside it has been refused. */
    const gfx_rect_t hdr = { (int16_t)(a.x + KP_PAD), (int16_t)(a.y + 6),
                             (int16_t)(a.w - 2 * KP_PAD), 18 };
    gfx_text_in(c, hdr, k->title, &gfx_font_8x16, ui_theme_color(UI_C_TEXT),
                1, GFX_ALIGN_LEFT);
    char range[48];
    snprintf(range, sizeof(range), TR(KEYPAD_RANGE), k->decimals,
             (double)k->min, k->decimals, (double)k->max, k->unit);
    gfx_text_in(c, hdr, range, &gfx_font_8x16,
                k->refused ? ui_theme_color(UI_C_WARN)
                           : ui_theme_color(UI_C_TEXT_DIM),
                1, GFX_ALIGN_RIGHT);

    /* The entry: what has been typed, or the value as it is, faint. */
    const gfx_rect_t box = { (int16_t)(a.x + KP_PAD),
                             (int16_t)(a.y + KP_HDR + KP_GAP),
                             (int16_t)(a.w - 2 * KP_PAD), KP_ENTRY_H };
    gfx_fill_rect(c, box.x, box.y, box.w, box.h,
                  ui_theme_color(UI_C_PANEL_SUNK));
    char text[32];
    if (k->len > 0) {
        snprintf(text, sizeof(text), "%s %s", k->entry, k->unit);
    } else {
        snprintf(text, sizeof(text), "%.*f %s", k->decimals,
                 (double)k->value, k->unit);
    }
    gfx_text_in(c, (gfx_rect_t){ (int16_t)(box.x + 10), box.y,
                                 (int16_t)(box.w - 20), box.h },
                text, &gfx_font_8x16,
                (k->len > 0) ? ui_theme_color(UI_C_TEXT)
                             : ui_theme_color(UI_C_TEXT_FAINT),
                2, GFX_ALIGN_RIGHT);

    for (int key = 0; key < UI_KEY_COUNT; ++key) {
        const gfx_color_t fill = (key == UI_KEY_OK)
                                     ? ui_theme_color(UI_C_ACCENT)
                                     : ui_theme_color(UI_C_PANEL_SUNK);
        ui_button(c, ui_keypad_key_rect(k, (ui_key_t)key),
                  (key == UI_KEY_CANCEL) ? TR(CANCEL) : k_labels[key],
                  fill, k->pressed == key, true);
    }
}
