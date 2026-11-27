/**
 * @file node_setup.c
 * @brief The display node's boot splash and its own SETUP without
 *        hardware (node_setup.h): the screens' boxes, the taps, the
 *        timers, the edits and the texts.
 *
 * SPDX-License-Identifier: MIT
 */
#include "node_setup.h"

#include <stdio.h>
#include <string.h>

/** @brief The boxes, px, on the 320 x 240 screen. @{ */
#define SCR_W    320   /**< the screen's width */
#define SETUP_X   60   /**< the splash's SETUP button */
#define SETUP_Y  164   /**< its top */
#define SETUP_W  200   /**< its width */
#define SETUP_H   52   /**< its height */
#define ROW_Y     34   /**< SETUP's first row's top */
#define ROW_P     38   /**< the rows' pitch */
#define ROW_H     34   /**< a row's height */
#define BTN_Y    196   /**< the bottom buttons */
#define BTN_H     40   /**< their height */
#define BTN_W    148   /**< their width */
#define BTN_LX     8   /**< BACK's left edge */
#define BTN_RX   164   /**< SAVE's left edge */
#define KB_Y      40   /**< the keyboard's first key row */
#define KB_P      36   /**< its rows' pitch */
#define KB_W      32   /**< a key's width */
#define KB_H      34   /**< a key's height */
#define KB_BOT_Y 186   /**< the keyboard's bottom row */
#define KB_BOT_H  50   /**< its height */
#define KP_Y      44   /**< the keypad's first row */
#define KP_P      48   /**< its rows' pitch */
#define KP_H      44   /**< a key's height */
#define KP_CP     80   /**< its columns' pitch */
#define KP_W      76   /**< a key's width */
#define CHIP_W    50   /**< an address chip */
#define CHIP_P    52   /**< the chips' column pitch */
#define SCAN_Y    36   /**< the scan's first chip row */
#define SCAN_RP   30   /**< its rows' pitch */
#define SCAN_H    26   /**< a chip's height */
#define AUTO_Y    36   /**< the address choice's AUTO */
#define AUTO_H    34   /**< its height */
#define ADDR_Y    76   /**< its first chip row */
#define ADDR_RP   36   /**< its rows' pitch */
#define ADDR_H    32   /**< a chip's height */
#define COLS       6   /**< chips a row */
/** @} */

/** @brief The keyboard's 40 characters, row by row; the backspace character is DEL. */
static const char k_kb[NS_KB_CHARS + 1] =
    "1234567890QWERTYUIOPASDFGHJKL-ZXCVBNM_.\b";

/** @brief The keypad's digit at each of its grid's 12 places, row by row;
 *  -1 marks DEL, -2 CLR. */
static const int8_t k_kp_grid[12] = { 1, 2, 3, -1, 4, 5, 6, -2, 7, 8, 9, 0 };

/** @brief The texts, English then German, in ns_text_t order. */
static const char *const k_text[NS_TX_COUNT][2] = {
    { "POWER NODE",         "LEISTUNGSKNOTEN" },
    { "FIRMWARE %u.%u.%u",  "FIRMWARE %u.%u.%u" },
    { "JuWi made",          "JuWi made" },
    { "SETUP",              "SETUP" },
    { "NODE SETUP",         "KNOTEN-SETUP" },
    { "NAME",               "NAME" },
    { "I2C SCAN",           "I2C-SCAN" },
    { "INA228 ADDRESS",     "INA228-ADRESSE" },
    { "SHUNT",              "SHUNT" },
    { "BACK",               "ZURÜCK" },
    { "SAVE",               "SPEICHERN" },
    { "AGAIN",              "ERNEUT" },
    { "NONE",               "KEINER" },
    { "TAP TO SCAN",        "ZUM SCANNEN TIPPEN" },
    { "%u FOUND",           "%u GEFUNDEN" },
    { "AUTO",               "AUTO" },
    { "NO ADDRESS ANSWERS", "KEINE ADRESSE ANTWORTET" },
    { "NO INA228 FOUND",    "KEIN INA228 GEFUNDEN" },
    { "INA228",             "INA228" },
    { "50-10000uΩ",         "50-10000uΩ" },
    { "+%u",                "+%u" },
};

const char *ns_text(uint8_t lang, int t)
{
    if (t < 0 || t >= NS_TX_COUNT) {
        return "";
    }
    return k_text[t][lang == RCNODE_LANG_DE ? 1 : 0];
}

void ns_init(ns_t *s, const nst_vals_t *vals, uint8_t lang,
             const uint8_t fw[3], uint32_t now_ms)
{
    memset(s, 0, sizeof *s);
    s->mode = NS_SPLASH;
    s->lang = lang == RCNODE_LANG_DE ? RCNODE_LANG_DE : RCNODE_LANG_EN;
    if (fw != NULL) {
        memcpy(s->fw, fw, sizeof s->fw);
    }
    s->t_start = now_ms;
    s->t_touch = now_ms;
    if (vals != NULL) {
        s->edit = *vals;
    } else {
        s->edit.shunt = RCNODE_SHUNT_DEF_UOHM;
    }
}

bool ns_in_setup(const ns_t *s)
{
    return s != NULL && s->mode != NS_SPLASH && s->mode != NS_METER;
}

/**
 * @brief Show another screen.
 * @param s The state.  @param mode ns_mode_t.
 */
static void go(ns_t *s, int mode)
{
    s->mode = (uint8_t)mode;
    ++s->rev;
}

ns_act_t ns_tick(ns_t *s, uint32_t now_ms, bool linked)
{
    if (s == NULL) {
        return NS_ACT_NONE;
    }
    if (s->mode == NS_SPLASH) {
        if (linked || now_ms - s->t_start >= NS_SPLASH_MS) {
            go(s, NS_METER);
        }
        return NS_ACT_NONE;
    }
    if (ns_in_setup(s) && (linked || now_ms - s->t_touch >= NS_IDLE_MS)) {
        go(s, NS_METER);
        return NS_ACT_LEAVE;
    }
    return NS_ACT_NONE;
}

uint32_t ns_splash_left(const ns_t *s, uint32_t now_ms)
{
    if (s == NULL || s->mode != NS_SPLASH) {
        return 0u;
    }
    const uint32_t gone = now_ms - s->t_start;
    return gone >= NS_SPLASH_MS ? 0u : NS_SPLASH_MS - gone;
}

/**
 * @brief The chips of the address choice: the scan's INA228s, then the
 *        fixed address in force when the scan did not find it.
 * @param s The state.  @param i The chip.
 * @return The address; 0 past the last.
 */
static uint8_t addr_chip(const ns_t *s, int i)
{
    const unsigned n = s->scan.n_ina228;
    if (i < 0) {
        return 0u;
    }
    if ((unsigned)i < n) {
        return nscan_at(&s->scan, (unsigned)i, true);
    }
    const uint8_t f = s->edit.i228;
    if ((unsigned)i == n && f != RCNODE_I228_AUTO
        && !nscan_is_ina228(&s->scan, f)) {
        return f;
    }
    return 0u;
}

int ns_chips(const ns_t *s)
{
    if (s == NULL) {
        return 0;
    }
    if (s->mode == NS_SCAN) {
        return s->scan.n > NS_SCAN_CHIPS ? NS_SCAN_CHIPS - 1 : (int)s->scan.n;
    }
    if (s->mode == NS_ADDR) {
        int n = 0;
        while (n < NS_ADDR_CHIPS && addr_chip(s, n) != 0u) {
            ++n;
        }
        return n;
    }
    return 0;
}

uint8_t ns_chip_addr(const ns_t *s, int i)
{
    if (s == NULL || i < 0 || i >= ns_chips(s)) {
        return 0u;
    }
    return s->mode == NS_SCAN ? nscan_at(&s->scan, (unsigned)i, false)
                              : addr_chip(s, i);
}

/**
 * @brief A box.
 * @param out Receives it.
 * @param x Its left edge.
 * @param y Its top.
 * @param w Its width.
 * @param h Its height.
 * @return true.
 */
static bool box(gfx_rect_t *out, int x, int y, int w, int h)
{
    *out = gfx_rect_make(x, y, w, h);
    return true;
}

/**
 * @brief A keyboard key's box.
 * @param k The key.  @param out Receives it.
 * @return false out of range.
 */
static bool kb_box(int k, gfx_rect_t *out)
{
    if (k >= 0 && k < NS_KB_CHARS) {
        return box(out, (k % 10) * KB_W, KB_Y + (k / 10) * KB_P, KB_W, KB_H);
    }
    switch (k) {
    case NS_KB_SPACE:  return box(out, 0, KB_BOT_Y, 80, KB_BOT_H);
    case NS_KB_CLR:    return box(out, 80, KB_BOT_Y, 64, KB_BOT_H);
    case NS_KB_CANCEL: return box(out, 144, KB_BOT_Y, 88, KB_BOT_H);
    case NS_KB_OK:     return box(out, 232, KB_BOT_Y, 88, KB_BOT_H);
    default:           return false;
    }
}

/**
 * @brief A keypad key's box.
 * @param k The key: a digit, NS_KP_DEL, NS_KP_CLR, NS_KP_CANCEL, NS_KP_OK.
 * @param out Receives it.
 * @return false out of range.
 */
static bool kp_box(int k, gfx_rect_t *out)
{
    if (k == NS_KP_CANCEL) {
        return box(out, 2, KP_Y + 3 * KP_P, KP_CP + KP_W, KP_H);
    }
    if (k == NS_KP_OK) {
        return box(out, 2 + 2 * KP_CP, KP_Y + 3 * KP_P, KP_CP + KP_W, KP_H);
    }
    for (int g = 0; g < 12; ++g) {
        const int v = k_kp_grid[g];
        if ((k >= 0 && k <= 9 && v == k) || (k == NS_KP_DEL && v == -1)
            || (k == NS_KP_CLR && v == -2)) {
            return box(out, 2 + (g % 4) * KP_CP, KP_Y + (g / 4) * KP_P, KP_W,
                       KP_H);
        }
    }
    return false;
}

bool ns_box(const ns_t *s, int t, gfx_rect_t *out)
{
    if (s == NULL || out == NULL) {
        return false;
    }
    const int m = s->mode;
    if (t == NS_T_SETUP) {
        return m == NS_SPLASH && box(out, SETUP_X, SETUP_Y, SETUP_W, SETUP_H);
    }
    if (t >= NS_T_ROW0 && t <= NS_T_ROW3) {
        return m == NS_LIST
               && box(out, 0, ROW_Y + (t - NS_T_ROW0) * ROW_P, SCR_W, ROW_H);
    }
    if (t == NS_T_LEFT || t == NS_T_RIGHT) {
        return (m == NS_LIST || m == NS_SCAN || m == NS_ADDR)
               && box(out, t == NS_T_LEFT ? BTN_LX : BTN_RX, BTN_Y, BTN_W, BTN_H);
    }
    if (t >= NS_T_KEY0 && t <= NS_T_KEY_LAST) {
        if (m == NS_NAME) {
            return kb_box(t - NS_T_KEY0, out);
        }
        return m == NS_SHUNT && t - NS_T_KEY0 < NS_KP_KEYS
               && kp_box(t - NS_T_KEY0, out);
    }
    if (t == NS_T_AUTO) {
        return m == NS_ADDR && box(out, 4, AUTO_Y, SCR_W - 8, AUTO_H);
    }
    if (t >= NS_T_CHIP0 && t <= NS_T_CHIP_LAST) {
        const int i = t - NS_T_CHIP0;
        if (m == NS_SCAN && i < NS_SCAN_CHIPS) {
            return box(out, 4 + (i % COLS) * CHIP_P, SCAN_Y + (i / COLS) * SCAN_RP,
                       CHIP_W, SCAN_H);
        }
        if (m == NS_ADDR && i < NS_ADDR_CHIPS) {
            return box(out, 4 + (i % COLS) * CHIP_P, ADDR_Y + (i / COLS) * ADDR_RP,
                       CHIP_W, ADDR_H);
        }
    }
    return false;
}

int ns_hit(const ns_t *s, int x, int y)
{
    if (s == NULL) {
        return NS_T_NONE;
    }
    for (int t = NS_T_SETUP; t <= NS_T_CHIP_LAST; ++t) {
        gfx_rect_t r;
        if (t >= NS_T_CHIP0 && t - NS_T_CHIP0 >= ns_chips(s)) {
            break;
        }
        if (ns_box(s, t, &r) && x >= r.x && x < r.x + r.w && y >= r.y
            && y < r.y + r.h) {
            return t;
        }
    }
    return NS_T_NONE;
}

const char *ns_kb_label(const ns_t *s, int key)
{
    static char ch[2];
    const uint8_t lang = s != NULL ? s->lang : RCNODE_LANG_EN;
    const bool de = lang == RCNODE_LANG_DE;
    if (key == NS_KB_DEL) {
        return "DEL";
    }
    if (key >= 0 && key < NS_KB_CHARS) {
        ch[0] = k_kb[key];
        ch[1] = '\0';
        return ch;
    }
    switch (key) {
    case NS_KB_SPACE:  return de ? "LEER" : "SPACE";
    case NS_KB_CLR:    return "CLR";
    case NS_KB_CANCEL: return de ? "ABBRUCH" : "CANCEL";
    case NS_KB_OK:     return "OK";
    default:           return "";
    }
}

const char *ns_kp_label(const ns_t *s, int key)
{
    static const char *const k_digit[10] = { "0", "1", "2", "3", "4",
                                             "5", "6", "7", "8", "9" };
    const bool de = s != NULL && s->lang == RCNODE_LANG_DE;
    if (key >= 0 && key <= 9) {
        return k_digit[key];
    }
    switch (key) {
    case NS_KP_DEL:    return "DEL";
    case NS_KP_CLR:    return "CLR";
    case NS_KP_CANCEL: return de ? "ABBRUCH" : "CANCEL";
    case NS_KP_OK:     return "OK";
    default:           return "";
    }
}

void ns_row_value(const ns_t *s, int row, char *buf, size_t n)
{
    if (buf == NULL || n == 0u) {
        return;
    }
    buf[0] = '\0';
    if (s == NULL) {
        return;
    }
    switch (row) {
    case 0:
        (void)snprintf(buf, n, "%s", s->edit.name[0] != '\0'
                       ? s->edit.name : ns_text(s->lang, NS_TX_NONE));
        break;
    case 1:
        if (s->scan.done) {
            (void)snprintf(buf, n, ns_text(s->lang, NS_TX_FOUND),
                           (unsigned)s->scan.n);
        } else {
            (void)snprintf(buf, n, "%s", ns_text(s->lang, NS_TX_TAP_SCAN));
        }
        break;
    case 2:
        if (s->edit.i228 == RCNODE_I228_AUTO) {
            (void)snprintf(buf, n, "%s", ns_text(s->lang, NS_TX_AUTO));
        } else {
            (void)snprintf(buf, n, "0x%02X", (unsigned)s->edit.i228);
        }
        break;
    case 3:
        (void)snprintf(buf, n, "%uuΩ", (unsigned)s->edit.shunt);
        break;
    default:
        break;
    }
}

/**
 * @brief NAME's keyboard: a key typed.
 * @param s The state.  @param k The key.
 */
static void kb_key(ns_t *s, int k)
{
    const size_t len = strlen(s->text);
    if (k == NS_KB_DEL) {
        if (len > 0u) {
            s->text[len - 1u] = '\0';
        }
    } else if (k == NS_KB_CLR) {
        s->text[0] = '\0';
    } else if (k == NS_KB_CANCEL) {
        go(s, NS_LIST);
        return;
    } else if (k == NS_KB_OK) {
        /* The spaces at either end are dropped; the rest is the name. */
        const char *a = s->text;
        while (*a == ' ') {
            ++a;
        }
        size_t m = strlen(a);
        while (m > 0u && a[m - 1u] == ' ') {
            --m;
        }
        char name[RCNODE_UNAME_MAX + 1u];
        memcpy(name, a, m);
        name[m] = '\0';
        if (rcnode_uname_ok(name)) {
            memcpy(s->edit.name, name, sizeof s->edit.name);
        }
        go(s, NS_LIST);
        return;
    } else if (len < RCNODE_UNAME_MAX) {
        s->text[len] = k == NS_KB_SPACE ? ' ' : k_kb[k];
        s->text[len + 1u] = '\0';
    }
    ++s->rev;
}

/**
 * @brief SHUNT's keypad: a key typed.
 * @param s The state.  @param k The key.
 */
static void kp_key(ns_t *s, int k)
{
    const size_t len = strlen(s->num);
    s->refused = false;
    if (k >= 0 && k <= 9) {
        if (len == 1u && s->num[0] == '0') {
            s->num[0] = (char)('0' + k);       /* no leading zero */
        } else if (len < sizeof s->num - 1u) {
            s->num[len] = (char)('0' + k);
            s->num[len + 1u] = '\0';
        }
    } else if (k == NS_KP_DEL) {
        if (len > 0u) {
            s->num[len - 1u] = '\0';
        }
    } else if (k == NS_KP_CLR) {
        s->num[0] = '\0';
    } else if (k == NS_KP_CANCEL) {
        go(s, NS_LIST);
        return;
    } else if (k == NS_KP_OK) {
        unsigned v = 0u;
        for (size_t i = 0u; s->num[i] != '\0'; ++i) {
            v = v * 10u + (unsigned)(s->num[i] - '0');
        }
        if (len > 0u && v >= RCNODE_SHUNT_MIN_UOHM && v <= RCNODE_SHUNT_MAX_UOHM) {
            s->edit.shunt = (uint16_t)v;
            go(s, NS_LIST);
            return;
        }
        s->refused = true;
    }
    ++s->rev;
}

/**
 * @brief SETUP's list: a row or a button tapped.
 * @param s The state.  @param t The target.
 * @return What the caller does.
 */
static ns_act_t list_tap(ns_t *s, int t)
{
    switch (t) {
    case NS_T_ROW0:
        memcpy(s->text, s->edit.name, sizeof s->text);
        go(s, NS_NAME);
        return NS_ACT_NONE;
    case NS_T_ROW0 + 1:
        go(s, NS_SCAN);
        return NS_ACT_SCAN;
    case NS_T_ROW0 + 2:
        go(s, NS_ADDR);
        return s->scan.done ? NS_ACT_NONE : NS_ACT_SCAN;
    case NS_T_ROW3:
        (void)snprintf(s->num, sizeof s->num, "%u", (unsigned)s->edit.shunt);
        s->refused = false;
        go(s, NS_SHUNT);
        return NS_ACT_NONE;
    case NS_T_LEFT:
        go(s, NS_METER);
        return NS_ACT_LEAVE;
    case NS_T_RIGHT:
        go(s, NS_METER);
        return NS_ACT_SAVE;
    default:
        return NS_ACT_NONE;
    }
}

ns_act_t ns_tap(ns_t *s, int x, int y, uint32_t now_ms, bool linked)
{
    if (s == NULL) {
        return NS_ACT_NONE;
    }
    const int t = ns_hit(s, x, y);
    if (s->mode == NS_SPLASH) {
        if (t == NS_T_SETUP && !linked) {
            s->t_touch = now_ms;
            go(s, NS_LIST);
        }
        return NS_ACT_NONE;
    }
    if (!ns_in_setup(s)) {
        return NS_ACT_NONE;
    }
    s->t_touch = now_ms;
    switch (s->mode) {
    case NS_LIST:
        return list_tap(s, t);
    case NS_NAME:
        if (t >= NS_T_KEY0 && t < NS_T_KEY0 + NS_KB_KEYS) {
            kb_key(s, t - NS_T_KEY0);
        }
        return NS_ACT_NONE;
    case NS_SHUNT:
        if (t >= NS_T_KEY0 && t < NS_T_KEY0 + NS_KP_KEYS) {
            kp_key(s, t - NS_T_KEY0);
        }
        return NS_ACT_NONE;
    case NS_SCAN:
        if (t == NS_T_LEFT) {
            return NS_ACT_SCAN;
        }
        if (t == NS_T_RIGHT) {
            go(s, NS_LIST);
        }
        return NS_ACT_NONE;
    default:    /* NS_ADDR */
        if (t == NS_T_AUTO) {
            s->edit.i228 = RCNODE_I228_AUTO;
            go(s, NS_LIST);
        } else if (t >= NS_T_CHIP0 && t <= NS_T_CHIP_LAST) {
            s->edit.i228 = ns_chip_addr(s, t - NS_T_CHIP0);
            go(s, NS_LIST);
        } else if (t == NS_T_LEFT) {
            return NS_ACT_SCAN;
        } else if (t == NS_T_RIGHT) {
            go(s, NS_LIST);
        }
        return NS_ACT_NONE;
    }
}

void ns_scan_done(ns_t *s, const nscan_t *r)
{
    if (s == NULL || r == NULL) {
        return;
    }
    s->scan = *r;
    ++s->rev;
}
