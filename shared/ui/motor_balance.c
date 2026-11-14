/**
 * @file motor_balance.c
 * @brief MOTOR & ESC > BALANCE.  See motor_balance.h.
 *
 * Inside the pane, top down: the live line (20px) with the trial mass at
 * its right; the TAKE keys, a finger tall, across the pane; each run's
 * state under its key (16px); then 2 RUNS, 4 RUNS and CLEAR, a finger
 * tall, with the correction and a line under it at their right.
 *
 * SPDX-License-Identifier: MIT
 */

#include "motor_balance.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "balance_screen.h"
#include "node_balance.h"
#include "settings.h"
#include "ui_layout.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

/** @brief The faces: 10 x 20 for keys and lines, 8 x 16 for the states. */
#define MB_FONT  (&gfx_font_10x20)
#define MB_FINE  UI_FONT_FINE      /**< 8 x 16 */

/** @brief The rows, from the pane's top, px. @{ */
#define MB_LIVE_Y  0                      /**< the live line */
#define MB_TAKE_Y  24                     /**< the TAKE keys */
#define MB_KEY_H   UI_TAP_MIN             /**< every key: 48px */
#define MB_CAP_Y   (MB_TAKE_Y + MB_KEY_H + 4)  /**< the runs' states: 76 */
#define MB_LOW_Y   (MB_CAP_Y + 16 + 6)    /**< the lower keys: 98 */
#define MB_PANE_H  (MB_LOW_Y + MB_KEY_H)  /**< the pane's height: 146 */
/** @} */
/** @brief The columns, px. @{ */
#define MB_GAP     8                      /**< between two TAKE keys */
#define MB_LOW_W   80                     /**< 2 RUNS, 4 RUNS, CLEAR */
#define MB_LOW_GAP 6                      /**< between two of them */
#define MB_RES_X   (3 * MB_LOW_W + 2 * MB_LOW_GAP + 14) /**< the result: 266 */
/** @} */
_Static_assert(MB_KEY_H >= UI_TAP_MIN, "every key is a finger tall");

/** @brief The pane's state: what each frame buffer drew. */
static struct {
    uint32_t drawn[2];  /**< node_balance_rev() + keys last painted */
    uint32_t keys;      /**< the keys' own revision */
    bool     drawn_any[2]; /**< drawn holds a paint */
} s_mb; /**< the pane's state */

void mbal_forget(void)
{
    s_mb.drawn_any[0] = false;
    s_mb.drawn_any[1] = false;
}

gfx_rect_t mbal_key_rect(int key, gfx_rect_t a)
{
    const nb_session_t *ses = node_balance_session();
    const int slots = (int)nb_slots(ses->method);
    gfx_rect_t r = { a.x, (int16_t)(a.y + MB_LOW_Y), MB_LOW_W, MB_KEY_H };
    if (key >= MBAL_KEY_TAKE0 && key <= MBAL_KEY_TAKE3) {
        const int i = key - MBAL_KEY_TAKE0;
        const int w = (a.w - (slots - 1) * MB_GAP) / slots;
        r.y = (int16_t)(a.y + MB_TAKE_Y);
        r.x = (int16_t)(a.x + i * (w + MB_GAP));
        r.w = (int16_t)(i < slots ? w : 0);
        return r;
    }
    switch (key) {
    case MBAL_KEY_TWO:
        break;
    case MBAL_KEY_FOUR:
        r.x = (int16_t)(a.x + MB_LOW_W + MB_LOW_GAP);
        break;
    case MBAL_KEY_CLEAR:
        r.x = (int16_t)(a.x + 2 * (MB_LOW_W + MB_LOW_GAP));
        break;
    default:
        r.w = 0;
        break;
    }
    return r;
}

/**
 * @brief A mass in g as "%u.%02u" arguments, rounded to 0.01g.
 * @param g The mass.  @param whole Receives the grams.  @param cg The
 *        hundredths.
 */
static void grams(float g, unsigned *whole, unsigned *cg)
{
    const unsigned c = g > 0.0f ? (unsigned)(g * 100.0f + 0.5f) : 0u;
    *whole = c / 100u;
    *cg = c % 100u;
}

/**
 * @brief A run's state under its key.
 * @param c The capture.  @param two Whether the method is two runs.
 * @param out The text.  @param n Its size.
 */
static void cap_text(const nb_cap_t *c, bool two, char *out, size_t n)
{
    switch (c->state) {
    case NB_CAP_TAKING:
        snprintf(out, n, TR(MB_TAKING), (unsigned)c->n,
                 (unsigned)NB_CAP_REPORTS);
        break;
    case NB_CAP_DONE: {
        const float m = two ? c->vec.mag : c->mag;
        if (c->counts) {
            snprintf(out, n, "%u", (unsigned)(m + 0.5f));
        } else {
            snprintf(out, n, "%.2fg", (double)m);
        }
        if (two) {
            const size_t l = strlen(out);
            snprintf(out + l, n - l, " %d°", (int)(c->vec.deg + 0.5f) % 360);
        }
        break;
    }
    case NB_CAP_FAILED:
        switch (c->fail) {
        case NB_FAIL_STALE:    snprintf(out, n, "%s", TR(MB_FAIL_STALE)); break;
        case NB_FAIL_NO_REV:   snprintf(out, n, "%s", TR(MB_FAIL_NO_REV)); break;
        case NB_FAIL_NO_PHASE: snprintf(out, n, "%s", TR(MB_FAIL_NO_PHASE)); break;
        default:               snprintf(out, n, "%s", TR(MB_FAIL_RPM)); break;
        }
        break;
    case NB_CAP_EMPTY:
    default:
        snprintf(out, n, "--");
        break;
    }
}

/**
 * @brief The live line: the node's reading, or why there is none, and the
 *        trial mass at the right.
 * @param c The canvas.  @param a The pane.
 */
static void draw_live(gfx_canvas_t *c, gfx_rect_t a)
{
    const nb_reading_t *r = node_balance_live();
    char line[96];
    if (r->live == NB_LIVE_SIMULATED) {
        snprintf(line, sizeof(line), "%s", TR(MB_NO_NODE));
    } else {
        char mag[24], deg[16], rpm[16];
        (void)nb_fmt_mag(r, mag, sizeof(mag));
        (void)nb_fmt_deg(r, deg, sizeof(deg));
        (void)nb_fmt_rpm(r, rpm, sizeof(rpm));
        const nb_quality_t q = (nb_quality_t)r->word;
        if (q >= NB_Q_GOOD && q <= NB_Q_POOR) {
            snprintf(line, sizeof(line), "%s  %s  %s  %s %u%%", mag, deg,
                     rpm, nb_quality_text(q), (unsigned)r->quality);
        } else {
            snprintf(line, sizeof(line), "%s  %s  %s  %s", mag, deg, rpm,
                     nb_quality_text(q));
        }
    }
    gfx_text_in(c, (gfx_rect_t){ a.x, (int16_t)(a.y + MB_LIVE_Y),
                                 (int16_t)(a.w - 130), 20 },
                line, MB_FONT,
                r->live == NB_LIVE_FRESH ? ui_theme_color(UI_C_TEXT)
                                         : ui_theme_color(UI_C_TEXT_DIM),
                1, GFX_ALIGN_LEFT);
    unsigned g, cg;
    grams(settings_get(SET_BAL_TRIAL), &g, &cg);
    snprintf(line, sizeof(line), TR(MB_TRIAL), g, cg);
    gfx_text_in(c, (gfx_rect_t){ (int16_t)(a.x + a.w - 130),
                                 (int16_t)(a.y + MB_LIVE_Y), 130, 20 },
                line, MB_FONT, ui_theme_color(UI_C_TEXT_DIM), 1,
                GFX_ALIGN_RIGHT);
}

/**
 * @brief The correction and the line under it, right of the lower keys.
 * @param c The canvas.  @param a The pane.  @param two Two runs.
 */
static void draw_result(gfx_canvas_t *c, gfx_rect_t a, bool two)
{
    const gfx_rect_t res = { (int16_t)(a.x + MB_RES_X),
                             (int16_t)(a.y + MB_LOW_Y + 2),
                             (int16_t)(a.w - MB_RES_X), 20 };
    const gfx_rect_t sub = { res.x, (int16_t)(res.y + 26), res.w, 16 };
    char line[64], hint[64];
    vibe_solution_t sol;
    snprintf(hint, sizeof(hint), "%s", two ? TR(MB_HINT_TWO)
                                           : TR(MB_HINT_FOUR));
    line[0] = '\0';
    if (node_balance_result(&sol, NULL)) {
        const bool corr = sol.status == VIBE_SOLVE_OK
                          || sol.status == VIBE_SOLVE_WEAK;
        if (corr) {
            char where[32];
            const int deg = (int)(sol.deg + 0.5f) % 360;
            if (balance_screen_rotor() == ROTOR_EDF) {
                snprintf(where, sizeof(where), TR(BAL_HUB), deg & 0x1FF);
            } else {
                int lo, hi;
                balance_nearest_blades(sol.deg, balance_screen_blades(),
                                       &lo, &hi);
                snprintf(where, sizeof(where), TR(BAL_BETWEEN), lo & 0xFF,
                         hi & 0xFF);
            }
            unsigned g, cg;
            grams(sol.mass, &g, &cg);
            snprintf(line, sizeof(line), TR(MB_RESULT), g, cg, where);
        } else {
            snprintf(line, sizeof(line), "%s", TR(MB_RESULT_NONE));
        }
        const nb_csv_t csv = node_balance_csv_state();
        switch (sol.status) {
        case VIBE_SOLVE_OK:
            snprintf(hint, sizeof(hint), "%s",
                     csv == NB_CSV_WRITTEN  ? TR(MB_SAVED)
                     : csv == NB_CSV_FAILED ? TR(MB_NOT_SAVED)
                                            : TR(MB_SAVING));
            break;
        case VIBE_SOLVE_WEAK:
            snprintf(hint, sizeof(hint), TR(MB_ST_WEAK),
                     (unsigned)(sol.effect * 100.0f + 0.5f),
                     csv == NB_CSV_WRITTEN  ? TR(MB_ROW_OK)
                     : csv == NB_CSV_FAILED ? TR(MB_ROW_FAIL)
                                            : TR(MB_ROW_WAIT));
            break;
        case VIBE_SOLVE_NO_EFFECT:
            snprintf(hint, sizeof(hint), "%s", TR(MB_ST_NO_EFFECT));
            break;
        case VIBE_SOLVE_INCONSISTENT:
            snprintf(hint, sizeof(hint), "%s", TR(MB_ST_INCONSISTENT));
            break;
        case VIBE_SOLVE_BAD_INPUT:
        default:
            snprintf(hint, sizeof(hint), "%s", TR(MB_ST_BAD));
            break;
        }
    }
    gfx_text_in(c, res, line, MB_FONT, ui_theme_color(UI_C_ACCENT), 1,
                GFX_ALIGN_LEFT);
    gfx_text_in(c, sub, hint, MB_FINE, ui_theme_color(UI_C_TEXT_DIM), 1,
                GFX_ALIGN_LEFT);
}

void mbal_render(gfx_canvas_t *c, int buf, gfx_rect_t a, gfx_color_t fill)
{
    buf &= 1;
    const uint32_t rev = node_balance_rev() + s_mb.keys;
    if (s_mb.drawn_any[buf] && s_mb.drawn[buf] == rev) {
        return;
    }
    s_mb.drawn_any[buf] = true;
    s_mb.drawn[buf] = rev;
    gfx_fill_rect(c, a.x, a.y, a.w, a.h, fill);

    const nb_session_t *ses = node_balance_session();
    const bool two = ses->method == NB_TWO_RUNS;
    const bool fresh = node_balance_live()->live == NB_LIVE_FRESH;
    draw_live(c, a);
    for (int i = 0; i < (int)nb_slots(ses->method); ++i) {
        const gfx_rect_t k = mbal_key_rect(MBAL_KEY_TAKE0 + i, a);
        char label[24];
        if (i == 0) {
            snprintf(label, sizeof(label), "%s", TR(MB_SLOT_BASE));
        } else {
            snprintf(label, sizeof(label), TR(MB_SLOT_TRIAL), (i - 1) * 120);
        }
        const nb_cap_t *cap = &ses->cap[i];
        const bool taking = cap->state == NB_CAP_TAKING;
        ui_button_font(c, k, label,
                       taking ? ui_theme_color(UI_C_ACCENT)
                              : ui_theme_color(UI_C_PANEL_HI),
                       false, fresh && (ses->taking < 0 || taking), MB_FONT);
        char st[32];
        cap_text(cap, two, st, sizeof(st));
        gfx_text_in(c, (gfx_rect_t){ k.x, (int16_t)(a.y + MB_CAP_Y), k.w,
                                     16 },
                    st, MB_FINE,
                    cap->state == NB_CAP_FAILED ? ui_theme_color(UI_C_WARN)
                                                : ui_theme_color(UI_C_TEXT),
                    1, GFX_ALIGN_CENTER);
    }
    ui_button_font(c, mbal_key_rect(MBAL_KEY_TWO, a), TR(MB_TWO),
                   two ? ui_theme_color(UI_C_ACCENT)
                       : ui_theme_color(UI_C_PANEL_HI),
                   false, ses->taking < 0, MB_FONT);
    ui_button_font(c, mbal_key_rect(MBAL_KEY_FOUR, a), TR(MB_FOUR),
                   !two ? ui_theme_color(UI_C_ACCENT)
                        : ui_theme_color(UI_C_PANEL_HI),
                   false, ses->taking < 0, MB_FONT);
    ui_button_font(c, mbal_key_rect(MBAL_KEY_CLEAR, a), TR(MB_CLEAR),
                   ui_theme_color(UI_C_PANEL_HI), false, true, MB_FONT);
    draw_result(c, a, two);
}

bool mbal_event(const touch_event_t *evt, gfx_rect_t a)
{
    if (evt == NULL || evt->type != TOUCH_EVENT_DOWN) {
        return false;
    }
    nb_session_t *ses = node_balance_session();
    int key = -1;
    for (int k = 0; k < MBAL_KEY_COUNT; ++k) {
        const gfx_rect_t r = mbal_key_rect(k, a);
        if (r.w > 0 && gfx_rect_contains(r, evt->point.x, evt->point.y)) {
            key = k;
            break;
        }
    }
    if (key < 0) {
        return false;
    }
    if (key <= MBAL_KEY_TAKE3) {
        const bool fresh = node_balance_live()->live == NB_LIVE_FRESH;
        if (nb_take(ses, (uint8_t)key, fresh) == NB_TAKE_NO_DATA) {
            ui_router_set_alert(TR(MB_NO_DATA));
        }
    } else if (key == MBAL_KEY_CLEAR) {
        nb_clear(ses);
    } else if (ses->taking < 0) {
        nb_set_method(ses, key == MBAL_KEY_TWO ? NB_TWO_RUNS : NB_FOUR_RUNS);
    }
    ++s_mb.keys;
    return true;
}
