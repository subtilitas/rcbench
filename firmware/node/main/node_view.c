/**
 * @file node_view.c
 * @brief The display node's screen without hardware (node_view.h).
 *
 * SPDX-License-Identifier: MIT
 */
#include "node_view.h"

#include <stdio.h>
#include <string.h>

/** @brief The colours. @{ */
#define C_BG     GFX_BLACK               /**< the rows */
#define C_BAND   GFX_GREY(28)            /**< the band */
#define C_LINE   GFX_GREY(70)            /**< the separators */
#define C_TEXT   GFX_WHITE               /**< the band's text */
#define C_DIM    GFX_GREY(120)           /**< "--", the units */
#define C_WARN   GFX_RGB(255, 170, 0)    /**< NO SENSOR */
#define C_BAR    GFX_RGB(255, 170, 0)    /**< the hold's bar */
#define C_HI     GFX_RGB(255, 110, 90)   /**< the highest's triangle */
#define C_LO     GFX_RGB(90, 160, 255)   /**< the lowest's triangle */
/** @} */

/** @brief Each row's value colour, NV_Q_* order. */
static const gfx_color_t k_q_color[NV_Q_N] = {
    GFX_RGB(120, 200, 255),   /* volts */
    GFX_RGB(255, 210, 90),    /* amperes */
    GFX_RGB(140, 235, 140),   /* watts */
};

/** @brief Each row's unit, NV_Q_* order. */
static const char *const k_unit[NV_Q_N] = { "V", "A", "W" };

/** @brief The geometry of a row, from its top, px. @{ */
#define DIGITS_DY    13  /**< the digits' top */
#define UNIT_X      198  /**< the unit's left edge */
#define UNIT_DY      21  /**< the unit's top: its cell's foot on the digits' */
#define EXT_TRI_X   228  /**< the triangles' left edge */
#define EXT_TRI_W    12  /**< a triangle's width */
#define HI_DY        10  /**< the highest's top */
#define LO_DY        40  /**< the lowest's top */
#define BAR_H         4  /**< the hold bar's height, at the band's foot */
/** @} */

void nv_stats_init(nv_stats_t *s)
{
    if (s != NULL) {
        memset(s, 0, sizeof *s);
    }
}

/**
 * @brief @p num / @p den rounded half away from zero.
 * @param num The numerator.
 * @param den The denominator, above 0.
 * @return The quotient.
 */
static int64_t div_round(int64_t num, int64_t den)
{
    return num >= 0 ? (num + den / 2) / den : -((-num + den / 2) / den);
}

/**
 * @brief A 64-bit value clamped to 32 bits.
 * @param v The value.
 * @return It, clamped.
 */
static int32_t clamp32(int64_t v)
{
    if (v > INT32_MAX) {
        return INT32_MAX;
    }
    return v < INT32_MIN ? INT32_MIN : (int32_t)v;
}

void nv_stats_add(nv_stats_t *s, int32_t v_mv, int32_t i_ua)
{
    if (s == NULL) {
        return;
    }
    const int32_t q[NV_Q_N] = {
        v_mv,
        clamp32(div_round(i_ua, 1000)),
        /* mV × µA is nW. */
        clamp32(div_round((int64_t)v_mv * i_ua, 1000000)),
    };
    for (int k = 0; k < NV_Q_N; ++k) {
        nv_ext_t *e = &s->ext[k];
        if (!e->have) {
            e->have = true;
            e->lo = q[k];
            e->hi = q[k];
        } else if (q[k] < e->lo) {
            e->lo = q[k];
        } else if (q[k] > e->hi) {
            e->hi = q[k];
        }
        s->sum[k] += q[k];
    }
    ++s->n;
}

void nv_stats_window(nv_stats_t *s, const rcnode_pwr_win_t *w)
{
    if (s == NULL || w == NULL || w->n == 0u || w->i_max == RCNODE_I_NONE) {
        return;
    }
    const int32_t v_mv = (int32_t)((uint32_t)w->v_mean * (RCNODE_V_STEP_UV / 1000u));
    nv_stats_add(s, v_mv, w->i_mean);
}

void nv_stats_reset(nv_stats_t *s, int q)
{
    if (s == NULL) {
        return;
    }
    for (int k = 0; k < NV_Q_N; ++k) {
        if (q == NV_Q_N || q == k) {
            s->ext[k].have = false;
            s->ext[k].lo = 0;
            s->ext[k].hi = 0;
        }
    }
}

void nv_stats_frame(nv_stats_t *s, nv_frame_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof *out);
    if (s == NULL) {
        return;
    }
    out->ok = s->n > 0u;
    for (int k = 0; k < NV_Q_N; ++k) {
        if (out->ok) {
            out->val[k] = clamp32(div_round(s->sum[k], (int64_t)s->n));
        }
        out->ext[k] = s->ext[k];
        s->sum[k] = 0;
    }
    s->n = 0u;
}

size_t nv_fmt(int q, int32_t v, char *buf, size_t n)
{
    if (buf == NULL || n < 8u) {
        return 0u;
    }
    const int64_t a = v < 0 ? -(int64_t)v : (int64_t)v;
    const char *sign = v < 0 ? "-" : "";
    int len;
    if (q == NV_Q_V) {
        int64_t cv = v < 0 ? 0 : (a + 5) / 10;        /* 10mV steps */
        if (cv > 9999) {
            cv = 9999;
        }
        len = snprintf(buf, n, "%d.%02d", (int)(cv / 100), (int)(cv % 100));
    } else if (q == NV_Q_A) {
        int64_t da = (a + 50) / 100;                  /* 100mA steps */
        if (da > 9999) {
            da = 9999;
        }
        len = snprintf(buf, n, "%s%d.%d", da == 0 ? "" : sign,
                       (int)(da / 10), (int)(da % 10));
    } else if (q == NV_Q_W) {
        const int64_t dw = (a + 50) / 100;            /* 100mW steps */
        if (dw < 1000) {
            len = snprintf(buf, n, "%s%d.%d", dw == 0 ? "" : sign,
                           (int)(dw / 10), (int)(dw % 10));
        } else {
            int64_t w = (a + 500) / 1000;
            if (w > 99999) {
                w = 99999;
            }
            len = snprintf(buf, n, "%s%d", sign, (int)w);
        }
    } else {
        return 0u;
    }
    return len > 0 && (size_t)len < n ? (size_t)len : 0u;
}

size_t nv_fmt_unit(int q, int32_t v, char *buf, size_t n)
{
    const size_t len = nv_fmt(q, v, buf, n);
    if (len == 0u || len + 2u > n) {
        return 0u;
    }
    buf[len] = k_unit[q][0];
    buf[len + 1u] = '\0';
    return len + 1u;
}

nv_touch_ev_t nv_touch_step(nv_touch_t *t, bool down, int x, int y,
                            uint32_t now_ms)
{
    if (t == NULL) {
        return NV_TOUCH_NONE;
    }
    if (down) {
        t->t_seen = now_ms;
        if (!t->down) {
            t->down = true;
            t->fired = false;
            t->t_down = now_ms;
            t->x = (int16_t)x;
            t->y = (int16_t)y;
            return NV_TOUCH_NONE;
        }
        if (!t->fired && (uint32_t)(now_ms - t->t_down) >= NV_HOLD_MS) {
            t->fired = true;
            return NV_TOUCH_HOLD;
        }
        return NV_TOUCH_NONE;
    }
    if (!t->down || (uint32_t)(now_ms - t->t_seen) < NV_UP_MS) {
        /* A gap of a read or two inside a press is still the press. The
         * hold fires only on a read that sees the finger, so a release
         * shortly before NV_HOLD_MS stays a tap. */
        return NV_TOUCH_NONE;
    }
    t->down = false;
    return t->fired ? NV_TOUCH_NONE : NV_TOUCH_TAP;
}

uint16_t nv_touch_progress(const nv_touch_t *t, uint32_t now_ms)
{
    if (t == NULL || !t->down || t->fired) {
        return 0u;
    }
    const uint32_t held = now_ms - t->t_down;
    if (held < NV_HINT_MS) {
        return 0u;
    }
    return held >= NV_HOLD_MS ? 1000u : (uint16_t)(held * 1000u / NV_HOLD_MS);
}

int nv_hit(int x, int y)
{
    if (x < NV_SPLIT_X || x >= NV_W || y < NV_BAND_H || y >= NV_H) {
        return -1;
    }
    return (y - NV_BAND_H) / NV_ROW_H;
}

void nv_touch_map(int tx, int ty, int *sx, int *sy)
{
    if (sx == NULL || sy == NULL) {
        return;
    }
    /* Portrait 240 x 320 turned to landscape 320 x 240: the long axis is
     * the screen's x, the short one its y, mirrored. */
    int x = ty;
    int y = (NV_H - 1) - tx;
    *sx = x < 0 ? 0 : (x >= NV_W ? NV_W - 1 : x);
    *sy = y < 0 ? 0 : (y >= NV_H ? NV_H - 1 : y);
}

const char *nv_text_link(uint8_t lang, bool linked)
{
    if (!linked) {
        return "STANDALONE";
    }
    return lang == RCNODE_LANG_DE ? "VERBUNDEN" : "CONNECTED";
}

const char *nv_text_right(const nv_model_t *m)
{
    if (m == NULL) {
        return "";
    }
    const bool de = m->linked && m->lang == RCNODE_LANG_DE;
    if (m->hold_pm > 0u) {
        return de ? "ALLE ZURÜCKSETZEN" : "RESET ALL";
    }
    if (!m->f.ok) {
        return de ? "KEIN SENSOR" : "NO SENSOR";
    }
    return "";
}

void nv_view_init(nv_view_t *v)
{
    if (v != NULL) {
        memset(v, 0, sizeof *v);
    }
}

/**
 * @brief Copy @p s into @p dst when it differs.
 * @param dst  The kept text.
 * @param n    Its size.
 * @param s    The new text.
 * @param all  Count it changed regardless.
 * @return true when it changed, or @p all.
 */
static bool take(char *dst, size_t n, const char *s, bool all)
{
    if (!all && strncmp(dst, s, n) == 0) {
        return false;
    }
    (void)snprintf(dst, n, "%s", s);
    return true;
}

/**
 * @brief A highest or lowest's text: the value and its unit, or "--".
 * @param q   NV_Q_*.
 * @param e   The extremes.
 * @param hi  The highest, else the lowest.
 * @param buf Receives the text.
 * @param n   Its size.
 */
static void ext_text(int q, const nv_ext_t *e, bool hi, char *buf, size_t n)
{
    if (!e->have || nv_fmt_unit(q, hi ? e->hi : e->lo, buf, n) == 0u) {
        (void)snprintf(buf, n, "--");
    }
}

/**
 * @brief The band's left or right text.
 * @param c     The canvas.
 * @param right The right one, else the left.
 * @param s     The text.
 * @param color Its colour.
 */
static void draw_band_text(gfx_canvas_t *c, bool right, const char *s,
                           gfx_color_t color)
{
    const gfx_rect_t box = right
        ? gfx_rect_make(NV_BAND_SPLIT, 0, NV_W - NV_BAND_SPLIT - NV_BAND_PAD,
                        NV_BAND_H - BAR_H)
        : gfx_rect_make(NV_BAND_PAD, 0, NV_BAND_SPLIT - NV_BAND_PAD, NV_BAND_H - BAR_H);
    gfx_fill_rect(c, right ? NV_BAND_SPLIT : 0, 0,
                  right ? NV_W - NV_BAND_SPLIT : NV_BAND_SPLIT, NV_BAND_H - BAR_H,
                  C_BAND);
    gfx_text_in(c, box, s, &gfx_font_10x20, color, 1,
                right ? GFX_ALIGN_RIGHT : GFX_ALIGN_LEFT);
}

/**
 * @brief The hold's bar at the band's foot.
 * @param c The canvas.
 * @param w Its width, px.
 */
static void draw_bar(gfx_canvas_t *c, int w)
{
    gfx_fill_rect(c, 0, NV_BAND_H - BAR_H, NV_W, BAR_H, C_BAND);
    if (w > 0) {
        gfx_fill_rect(c, 0, NV_BAND_H - BAR_H, w, BAR_H, C_BAR);
    }
}

/**
 * @brief A row's live value and unit.
 * @param c The canvas.
 * @param q NV_Q_*.
 * @param s The digits, or "--".
 * @param ok A value, else "--" in grey.
 */
static void draw_value(gfx_canvas_t *c, int q, const char *s, bool ok)
{
    const int y0 = NV_BAND_H + q * NV_ROW_H;
    gfx_fill_rect(c, 0, y0 + 1, NV_SPLIT_X, NV_ROW_H - 1, C_BG);
    const int w = gfx_text_width(&gfx_font_num_32x44, s, 1);
    (void)gfx_text(c, NV_VAL_RIGHT - w, y0 + DIGITS_DY, s, &gfx_font_num_32x44,
                   ok ? k_q_color[q] : C_DIM, 1);
    (void)gfx_text(c, UNIT_X, y0 + UNIT_DY, k_unit[q], &gfx_font_20x36,
                   ok ? k_q_color[q] : C_DIM, 1);
}

/**
 * @brief A row's highest or lowest: its triangle and its text.
 * @param c  The canvas.
 * @param q  NV_Q_*.
 * @param hi The highest, else the lowest.
 * @param s  The text.
 */
static void draw_ext(gfx_canvas_t *c, int q, bool hi, const char *s)
{
    const int y0 = NV_BAND_H + q * NV_ROW_H;
    const int top = y0 + (hi ? 1 : NV_ROW_H / 2);
    gfx_fill_rect(c, NV_SPLIT_X + 1, top, NV_W - NV_SPLIT_X - 1,
                  NV_ROW_H / 2 - (hi ? 1 : 0), C_BG);
    const int ty = y0 + (hi ? HI_DY : LO_DY) + 4;
    const int mid = EXT_TRI_X + EXT_TRI_W / 2;
    if (hi) {
        gfx_fill_trapezoid(c, ty, 10, mid, mid + 1, EXT_TRI_X,
                           EXT_TRI_X + EXT_TRI_W, C_HI);
    } else {
        gfx_fill_trapezoid(c, ty, 10, EXT_TRI_X, EXT_TRI_X + EXT_TRI_W, mid,
                           mid + 1, C_LO);
    }
    const bool none = strcmp(s, "--") == 0;
    (void)gfx_text(c, NV_EXT_TEXT_X, y0 + (hi ? HI_DY : LO_DY), s,
                   &gfx_font_10x20, none ? C_DIM : C_TEXT, 1);
}

/**
 * @brief The lines between the parts.
 * @param c The canvas.
 */
static void draw_lines(gfx_canvas_t *c)
{
    for (int q = 0; q < NV_Q_N; ++q) {
        const int y0 = NV_BAND_H + q * NV_ROW_H;
        gfx_hline(c, 0, y0, NV_W, C_LINE);
        gfx_vline(c, NV_SPLIT_X, y0 + 8, NV_ROW_H - 16, C_LINE);
    }
}

int nv_render(nv_view_t *v, gfx_canvas_t *c, const nv_model_t *m)
{
    if (v == NULL || c == NULL || m == NULL) {
        return 0;
    }
    const bool all = !v->drawn;
    int parts = 0;
    if (all) {
        gfx_fill_rect(c, 0, 0, NV_W, NV_BAND_H, C_BAND);
        gfx_fill_rect(c, 0, NV_BAND_H, NV_W, NV_H - NV_BAND_H, C_BG);
        draw_lines(c);
        v->drawn = true;
        ++parts;
    }
    if (take(v->left, sizeof v->left, nv_text_link(m->lang, m->linked), all)) {
        draw_band_text(c, false, v->left, C_TEXT);
        ++parts;
    }
    if (take(v->right, sizeof v->right, nv_text_right(m), all)) {
        draw_band_text(c, true, v->right, m->hold_pm > 0u ? C_TEXT : C_WARN);
        ++parts;
    }
    const uint16_t bar = (uint16_t)((uint32_t)m->hold_pm * NV_W / 1000u);
    if (all || bar != v->bar) {
        v->bar = bar;
        draw_bar(c, bar);
        ++parts;
    }
    for (int q = 0; q < NV_Q_N; ++q) {
        char s[12];
        if (!m->f.ok || nv_fmt(q, m->f.val[q], s, sizeof s) == 0u) {
            (void)snprintf(s, sizeof s, "--");
        }
        if (take(v->val[q], sizeof v->val[q], s, all)) {
            draw_value(c, q, v->val[q], m->f.ok);
            ++parts;
        }
        ext_text(q, &m->f.ext[q], true, s, sizeof s);
        if (take(v->hi[q], sizeof v->hi[q], s, all)) {
            draw_ext(c, q, true, v->hi[q]);
            ++parts;
        }
        ext_text(q, &m->f.ext[q], false, s, sizeof s);
        if (take(v->lo[q], sizeof v->lo[q], s, all)) {
            draw_ext(c, q, false, v->lo[q]);
            ++parts;
        }
    }
    return parts;
}
