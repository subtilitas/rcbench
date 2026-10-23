/**
 * @file
 * @brief The text keyboard.  See ui_textkey.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "ui_textkey.h"

#include <stdio.h>
#include <string.h>

#include "card_tree.h"
#include "fm_name.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

/*
 * A title row, the text, then five rows of keys, 6 px apart.  Over the
 * 1008 x 484 px the WI-FI screen gives it, a key is 93 x 71 px
 * (14.0 x 10.7 mm at 0.150 mm a pixel).
 */
/** @brief The room around the title, the text and the keys: 8 px. */
#define TK_PAD     8
/** @brief The title row: the 10 x 20 face and 6 px above it. */
#define TK_HDR     32
/** @brief The text: the 20 x 36 face and 8 px above and below. */
#define TK_ENTRY_H 52
/** @brief The gap between the text and the keys, and between keys. */
#define TK_GAP     6
/** @brief The key grid: 10 columns ... */
#define TK_COLS    10
/** @brief ... and 5 rows, the last the bottom row of words. */
#define TK_ROWS    5
/** @brief The room a key keeps left and right of its label: 4 px. */
#define TK_KEY_PAD 4

/** @brief The character rows, in reading order; the last is DEL. */
static const char k_chars[UI_TEXTKEY_CHARS + 1] =
    "1234567890" "QWERTYUIOP" "ASDFGHJKL-" "ZXCVBNM_.";

/** @brief The Wi-Fi layers.  Key UI_TK_SHIFT and key UI_TK_LAYER switch
 *         them and type nothing; a blank ("\0") types nothing either.  The
 *         marks are the 32 printable ASCII characters that are neither a
 *         letter, a digit nor a space, in code order. */
static const char k_wifi[3][UI_TEXTKEY_CHARS + 1] = {
    "1234567890" "qwertyuiop" "asdfghjkl-" " zxcvbnm  ",
    "1234567890" "QWERTYUIOP" "ASDFGHJKL-" " ZXCVBNM  ",
    "!\"#$%&'()*" "+,-./:;<=>" "?@[\\]^_`{|" "}~        ",
};

/** @brief The bottom row's texts: SPACE and CANCEL are translated. */
static const ui_text_id_t k_bottom_tx[] = { TX_SPACE, TX_COUNT, TX_CANCEL,
                                            TX_COUNT };
/** @brief The bottom row's legends: CLR and OK, the same in every
 *         language. */
static const char *const k_bottom[] = { NULL, "CLR", NULL, "OK" };
/** @brief How many columns each bottom key spans, for a name. */
static const int k_bottom_span[] = { 4, 2, 2, 2 };
/** @brief How many columns each bottom key spans, for a search and a
 *         Wi-Fi name. */
static const int k_search_span[] = { 3, 2, 3, 2 };
/** @brief A password's bottom row, in key order SPACE, CLR, CANCEL, OK,
 *         SHOW: where each starts, two columns each, SHOW second from the
 *         left. */
static const int k_secret_col[] = { 0, 4, 6, 8, 2 };

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

void ui_textkey_open_folder(ui_textkey_t *k, gfx_rect_t area,
                            const char *title, const char *text)
{
    ui_textkey_open(k, area, title, text, UI_TEXTKEY_MAX);
    if (k != NULL) {
        k->folder = true;
    }
}

void ui_textkey_open_wifi(ui_textkey_t *k, gfx_rect_t area,
                          const char *title, const char *text, bool secret)
{
    if (k == NULL) {
        return;
    }
    ui_textkey_open(k, area, title, NULL, 0);
    k->wifi    = true;
    k->secret  = secret;
    k->max_len = secret ? UI_TEXTKEY_PASS_MAX : UI_TEXTKEY_SSID_MAX;
    snprintf(k->text, (size_t)k->max_len + 1u, "%s",
             (text != NULL) ? text : "");
    k->len = (int)strlen(k->text);
}

void ui_textkey_open_file(ui_textkey_t *k, gfx_rect_t area,
                          const char *title, const char *text)
{
    if (k == NULL) {
        return;
    }
    ui_textkey_open_wifi(k, area, title, NULL, false);
    k->file    = true;
    k->max_len = (int)FM_NAME_MAX;
    /* Whole characters only: a name a computer wrote can hold more bytes
     * than the keyboard takes, and a cut in a German letter would leave
     * half of it. */
    const char *at = (text != NULL) ? text : "";
    size_t keep = 0u;
    while (*at != '\0') {
        const char *next = at;
        (void)gfx_utf8_next(&next);
        const size_t step = (size_t)(next - at);
        if (keep + step > (size_t)k->max_len) {
            break;
        }
        keep += step;
        at = next;
    }
    memcpy(k->text, (text != NULL) ? text : "", keep);
    k->text[keep] = '\0';
    k->len = (int)keep;
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
    if (k != NULL && k->folder && key == UI_TK_DOT) {
        return "/";
    }
    if (k != NULL && k->wifi) {
        if (key == UI_TK_SHOW) {
            return k->secret ? (k->show ? TR(TK_HIDE) : TR(TK_SHOW)) : "";
        }
        if (key == UI_TK_SHIFT && k->layer != UI_TK_MARKS) {
            return (k->layer == UI_TK_UPPER) ? "abc" : "ABC";
        }
        if (key == UI_TK_LAYER) {
            return (k->layer == UI_TK_MARKS) ? "abc" : "#+=";
        }
        if (key >= 0 && key < UI_TK_DEL) {
            static char one[2];
            one[0] = k_wifi[k->layer][key];
            one[1] = '\0';
            return (one[0] == ' ') ? "" : one;
        }
    }
    return ui_textkey_label(key);
}

/**
 * @brief The character a Wi-Fi key types.
 * @param k   The keyboard.
 * @param key The key.
 * @return The character of the layer shown, or 0 for a layer key, a blank
 *         and DEL.
 */
static char wifi_char(const ui_textkey_t *k, int key)
{
    if (key < 0 || key >= UI_TK_DEL || key == UI_TK_LAYER
        || (key == UI_TK_SHIFT && k->layer != UI_TK_MARKS)) {
        return '\0';
    }
    const char ch = k_wifi[k->layer][key];
    return (ch == ' ') ? '\0' : ch;
}

gfx_rect_t ui_textkey_key_rect(const ui_textkey_t *k, int key)
{
    const bool secret = k != NULL && k->wifi && k->secret;
    if (k == NULL || key < 0 || key > (secret ? UI_TK_SHOW : UI_TK_KEYS - 1)) {
        return (gfx_rect_t){ 0, 0, 0, 0 };
    }
    const int left   = k->area.x + TK_PAD;
    const int right  = k->area.x + k->area.w - TK_PAD;
    const int top    = k->area.y + TK_HDR + TK_ENTRY_H + 2 * TK_GAP;
    /* The area's foot, or UI_KEYS_FOOT where the area reaches under the
     * alert band. */
    const int foot   = k->area.y + k->area.h - TK_PAD;
    const int bottom = (foot < UI_KEYS_FOOT) ? foot : UI_KEYS_FOOT;
    const int kw = (right - left - (TK_COLS - 1) * TK_GAP) / TK_COLS;
    const int kh = (bottom - top - (TK_ROWS - 1) * TK_GAP) / TK_ROWS;
    int row, col, span;
    if (key < UI_TEXTKEY_CHARS) {
        row  = key / TK_COLS;
        col  = key % TK_COLS;
        span = 1;
    } else if (secret) {
        row  = TK_ROWS - 1;
        col  = k_secret_col[key - UI_TEXTKEY_CHARS];
        span = 2;
    } else {
        const int *spans = (k->search || k->wifi) ? k_search_span
                                                  : k_bottom_span;
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

/**
 * @brief @p ch after the text, while it is shorter than its most.
 * @param k  The keyboard.
 * @param ch The character.
 */
static void add(ui_textkey_t *k, char ch)
{
    if (k->len < k->max_len) {
        k->text[k->len++] = ch;
        k->text[k->len] = '\0';
    }
}

/**
 * @brief Key @p key pressed and released.
 * @param k   The keyboard.
 * @param key The key.
 * @param out Where OK writes what it takes.
 * @param n   The bytes at @p out.
 * @return UI_TEXTKEY_OK or UI_TEXTKEY_CANCELLED when it closed,
 *         UI_TEXTKEY_NONE otherwise, a refusal included.
 */
static ui_textkey_result_t press(ui_textkey_t *k, int key, char *out,
                                 size_t n)
{
    k->refused = false;
    ++k->revision;
    if (key == UI_TK_DEL) {
        /* A file name's last character, also one of two bytes. */
        while (k->file && k->len > 1
               && ((unsigned char)k->text[k->len - 1] & 0xC0u) == 0x80u) {
            k->text[--k->len] = '\0';
        }
        if (k->len > 0) {
            k->text[--k->len] = '\0';
        }
        return UI_TEXTKEY_NONE;
    }
    if (k->wifi && key < UI_TEXTKEY_CHARS) {
        if (key == UI_TK_LAYER) {
            k->layer = (k->layer == UI_TK_MARKS) ? UI_TK_LOWER : UI_TK_MARKS;
        } else if (key == UI_TK_SHIFT && k->layer != UI_TK_MARKS) {
            k->layer = (k->layer == UI_TK_UPPER) ? UI_TK_LOWER : UI_TK_UPPER;
        } else {
            const char ch = wifi_char(k, key);
            if (ch != '\0') {
                add(k, ch);
            }
        }
        return UI_TEXTKEY_NONE;
    }
    if (key < UI_TEXTKEY_CHARS) {
        add(k, (k->search && key == UI_TK_MARK) ? '*'
               : (k->folder && key == UI_TK_DOT) ? '/'
                                                  : k_chars[key]);
        return UI_TEXTKEY_NONE;
    }
    if (key == UI_TK_SHOW) {
        k->show = !k->show;
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
        if (k->file) {               /* as fm_name_check() takes it */
            const fm_name_t r = fm_name_check(k->text);
            if (r != FM_NAME_OK) {
                k->refused = true;
                k->why     = (uint8_t)r;
                return UI_TEXTKEY_NONE;
            }
            if (out != NULL && n > 0u) {
                snprintf(out, n, "%s", k->text);
            }
            ui_textkey_close(k);
            return UI_TEXTKEY_OK;
        }
        if (k->wifi && k->len < (k->secret ? UI_TEXTKEY_PASS_MIN : 1)) {
            k->refused = true;       /* the radio would refuse it */
            return UI_TEXTKEY_NONE;
        }
        if (k->folder) {             /* as the card takes it, or not at all */
            char dir[CARD_DIR_MAX];
            const card_folder_t r = card_folder_check(k->text, dir,
                                                       sizeof(dir));
            if (r != CARD_FOLDER_OK) {
                k->refused = true;
                k->why     = (uint8_t)r;
                return UI_TEXTKEY_NONE;
            }
            if (out != NULL && n > 0u) {
                snprintf(out, n, "%s", dir);
            }
            ui_textkey_close(k);
            return UI_TEXTKEY_OK;
        }
        if (k->search || k->wifi) {  /* as typed: spaces count */
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
        for (int key = 0; key <= UI_TK_SHOW; ++key) {
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
    /* A press that slid off its key is not a press on it, and neither is
     * a release the finger did not make. */
    if (!touch_event_is_tap_up(evt)
        || !gfx_rect_contains(ui_textkey_key_rect(k, key), x, y)) {
        return UI_TEXTKEY_NONE;
    }
    return press(k, key, out, n);
}

/**
 * @brief Why a folder was refused, in the words of its line.
 * @param why A card_folder_t.
 * @return The line.
 */
static const char *folder_why(uint8_t why)
{
    switch ((card_folder_t)why) {
    case CARD_FOLDER_LONG:     return TR(TEXTKEY_DIR_LONG);
    case CARD_FOLDER_DEEP:     return TR(TEXTKEY_DIR_DEEP);
    case CARD_FOLDER_CHAR:     return TR(TEXTKEY_DIR_CHAR);
    case CARD_FOLDER_RESERVED: return TR(TEXTKEY_DIR_RESERVED);
    case CARD_FOLDER_BENCH:    return TR(TEXTKEY_DIR_BENCH);
    case CARD_FOLDER_EMPTY:
    default:                   return TR(TEXTKEY_DIR_EMPTY);
    }
}

/**
 * @brief Why a file name was refused, in the words of its line.
 * @param why An fm_name_t.
 * @return The line.
 */
static const char *file_why(uint8_t why)
{
    switch ((fm_name_t)why) {
    case FM_NAME_LONG:     return TR(TEXTKEY_NAME_LONG);
    case FM_NAME_CHAR:     return TR(TEXTKEY_NAME_CHAR);
    case FM_NAME_EDGE:     return TR(TEXTKEY_NAME_EDGE);
    case FM_NAME_RESERVED: return TR(TEXTKEY_DIR_RESERVED);
    case FM_NAME_EMPTY:
    case FM_NAME_OK:
    default:               return TR(TEXTKEY_NEEDED);
    }
}

/**
 * @brief The face of the text field: 20 x 36 when UI_TEXTKEY_MAX
 *        characters and the caret fit @p box with 10 px each side and the
 *        face's height fits it, else 16 x 28.  A Wi-Fi text longer than the
 *        field shows its end in either.
 * @param box The field.
 * @return The face.
 */
static const gfx_font_t *entry_face(gfx_rect_t box)
{
    const gfx_font_t *big = &gfx_font_20x36;
    return ((UI_TEXTKEY_MAX + 1) * big->width + 20 <= box.w
            && big->height <= box.h)
               ? big : &gfx_font_16x28;
}

/**
 * @brief Whether key @p key of @p k is a word rather than one character:
 *        the bottom row, DEL, and the Wi-Fi layer keys (the capitals key
 *        types "}" on the marks layer, a character there).
 * @param k   The keyboard.
 * @param key The key.
 * @return True for a word key.
 */
static bool is_word(const ui_textkey_t *k, int key)
{
    return key >= UI_TK_DEL
           || (k->wifi && (key == UI_TK_LAYER
                           || (key == UI_TK_SHIFT
                               && k->layer != UI_TK_MARKS)));
}

const gfx_font_t *ui_textkey_word_font(const ui_textkey_t *k)
{
    if (k == NULL) {
        return &gfx_font_8x16;
    }
    static const gfx_font_t *const faces[] = { &gfx_font_12x24,
                                               &gfx_font_10x20 };
    for (size_t f = 0; f < sizeof(faces) / sizeof(faces[0]); ++f) {
        bool fit = true;
        for (int key = 0; key <= UI_TK_SHOW && fit; ++key) {
            const gfx_rect_t r = ui_textkey_key_rect(k, key);
            if (r.w == 0 || !is_word(k, key)) {
                continue;
            }
            const char *label = ui_textkey_key_label(k, key);
            if (key == UI_TK_SHOW) {
                label = (gfx_text_cells(TR(TK_HIDE))
                         > gfx_text_cells(TR(TK_SHOW))) ? TR(TK_HIDE)
                                                        : TR(TK_SHOW);
            } else if (k->wifi && (key == UI_TK_SHIFT
                                   || key == UI_TK_LAYER)) {
                label = "#+=";      /* as wide as "ABC" and "abc" */
            }
            fit = ui_text_fits(faces[f], label, r.w, r.h, TK_KEY_PAD);
        }
        if (fit) {
            return faces[f];
        }
    }
    return &gfx_font_8x16;
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
                             (int16_t)(a.w - 2 * TK_PAD), 22 };
    char room[64];
    if (k->refused && k->folder) {
        snprintf(room, sizeof(room), "%s", folder_why(k->why));
    } else if (k->refused && k->file) {
        snprintf(room, sizeof(room), "%s", file_why(k->why));
    } else if (k->refused && k->wifi) {
        if (k->secret) {
            snprintf(room, sizeof(room), TR(TEXTKEY_SHORT),
                     UI_TEXTKEY_PASS_MIN);
        } else {
            snprintf(room, sizeof(room), "%s", TR(TEXTKEY_SSID_NEEDED));
        }
    } else if (k->refused) {
        snprintf(room, sizeof(room), "%s", TR(TEXTKEY_NEEDED));
    } else {
        snprintf(room, sizeof(room), TR(TEXTKEY_COUNT), k->len, k->max_len);
    }
    /* 10 x 20 when the title and its line fit side by side, 16 px apart. */
    const gfx_font_t *hf =
        (gfx_text_width(&gfx_font_10x20, k->title, 1) + 16
             + gfx_text_width(&gfx_font_10x20, room, 1) <= hdr.w)
            ? &gfx_font_10x20 : &gfx_font_8x16;
    gfx_text_in(c, hdr, k->title, hf, ui_theme_color(UI_C_TEXT), 1,
                GFX_ALIGN_LEFT);
    gfx_text_in(c, hdr, room, hf,
                k->refused ? ui_theme_color(UI_C_WARN)
                           : ui_theme_color(UI_C_TEXT_DIM),
                1, GFX_ALIGN_RIGHT);

    const gfx_rect_t box = { (int16_t)(a.x + TK_PAD),
                             (int16_t)(a.y + TK_HDR + TK_GAP),
                             (int16_t)(a.w - 2 * TK_PAD), TK_ENTRY_H };
    gfx_fill_rect(c, box.x, box.y, box.w, box.h,
                  ui_theme_color(UI_C_PANEL_SUNK));
    /* The text in 20 x 36, or in 16 x 28 where a name of UI_TEXTKEY_MAX
     * characters and the caret would not fit the field. */
    const gfx_font_t *ef = entry_face(box);
    char shown[UI_TEXTKEY_PASS_MAX + 2];
    if (k->wifi) {
        /* A password as "*" unless shown; the end of a text longer than
         * the field, a cell of the face a character. */
        int fit = (box.w - 20) / ef->width - 1;
        fit = (fit < 1) ? 1 : fit;
        const int from = (k->len > fit) ? k->len - fit : 0;
        int n = 0;
        for (int i = from; i < k->len; ++i) {
            shown[n++] = (k->secret && !k->show) ? '*' : k->text[i];
        }
        shown[n++] = '_';
        shown[n] = '\0';
    } else {
        snprintf(shown, sizeof(shown), "%s_", k->text);   /* the caret */
    }
    gfx_text_in(c, (gfx_rect_t){ (int16_t)(box.x + 10), box.y,
                                 (int16_t)(box.w - 20), box.h },
                shown, ef, ui_theme_color(UI_C_TEXT), 1, GFX_ALIGN_LEFT);

    /* Characters in 12 x 24; the words in the face they all fit. */
    const gfx_font_t *wf = ui_textkey_word_font(k);
    for (int key = 0; key <= UI_TK_SHOW; ++key) {
        const gfx_rect_t r = ui_textkey_key_rect(k, key);
        if (r.w == 0) {
            continue;
        }
        const char *label = ui_textkey_key_label(k, key);
        if (k->wifi && key < UI_TK_DEL && label[0] == '\0') {
            continue;                /* a blank of the marks layer */
        }
        const gfx_color_t fill = (key == UI_TK_OK)
                                     ? ui_theme_color(UI_C_ACCENT)
                                     : ui_theme_color(UI_C_PANEL_SUNK);
        ui_button_font(c, r, label, fill, k->pressed == key, true,
                       is_word(k, key) ? wf : &gfx_font_12x24);
    }
}
