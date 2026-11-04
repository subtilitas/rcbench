/**
 * @file
 * @brief RUDDER's side view of the control surface: its geometry and its
 *        drawing (servo_rudder_flap.h).
 *
 * Every shape is gfx_subpixel.h's anti-aliased capsule or disc, placed in
 * fractions of a pixel, so a flap turning by a tenth of a degree moves its
 * trailing edge by about a tenth of a pixel rather than a whole one.  The
 * tapers are runs of short capsules whose widths step down along the run.
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_rudder_flap.h"

#include <math.h>
#include <stddef.h>

#include "gfx_subpixel.h"
#include "ui_theme.h"

/** @brief Degrees to radians. */
#define DEG2RAD (3.14159265358979f / 180.0f)
/** @brief A dash and the gap after it along a dashed line, px. */
#define DASH_ON  5.0f
#define DASH_OFF 4.0f /**< the gap, px */
/** @brief A dashed line's and a tick's width, px. */
#define LINE_W 1.5f
/** @brief The capsules a taper is drawn in. */
#define TAPER_STEPS 8

rudder_flap_t rudder_flap_layout(gfx_rect_t area)
{
    rudder_flap_t f;
    const float hw = (float)area.w / 2.0f;
    f.hinge_x = (float)area.x + hw;
    f.hinge_y = (float)area.y + (float)area.h / 2.0f;
    float len = (float)area.h / 2.0f - RUD_FLAP_LEN_INSET;
    const float left =
        (hw - RUD_FLAP_PAD - RUD_FLAP_LE_R) / RUD_FLAP_FIXED_SHARE;
    const float right = hw - RUD_FLAP_PAD - RUD_FLAP_TICK_OUT;
    len = fminf(len, fminf(left, right));
    f.flap_len  = (len > 0.0f) ? floorf(len) : 0.0f;
    f.fixed_len = f.flap_len * RUD_FLAP_FIXED_SHARE;
    return f;
}

float rudder_flap_clamp(float deg)
{
    if (isnan(deg)) {
        return 0.0f;
    }
    if (deg > RUD_FLAP_MAX_DEG) {
        return RUD_FLAP_MAX_DEG;
    }
    if (deg < -RUD_FLAP_MAX_DEG) {
        return -RUD_FLAP_MAX_DEG;
    }
    return deg;
}

void rudder_flap_at(const rudder_flap_t *f, float deg, float r, float *x,
                    float *y)
{
    const float a = rudder_flap_clamp(deg) * DEG2RAD;
    *x = f->hinge_x + r * cosf(a);
    *y = f->hinge_y - r * sinf(a);
}

gfx_rect_t rudder_flap_bounds(const rudder_flap_t *f, float deg)
{
    float tx = 0.0f, ty = 0.0f;
    rudder_flap_at(f, deg, f->flap_len, &tx, &ty);
    /* Half the root's width, the capsule's half pixel of blend and one
     * more for the rounding. */
    const float m = RUD_FLAP_ROOT_W / 2.0f + 2.0f;
    const int x0 = (int)floorf(fminf(f->hinge_x, tx) - m);
    const int y0 = (int)floorf(fminf(f->hinge_y, ty) - m);
    const int x1 = (int)ceilf(fmaxf(f->hinge_x, tx) + m);
    const int y1 = (int)ceilf(fmaxf(f->hinge_y, ty) + m);
    return gfx_rect_make(x0, y0, x1 - x0, y1 - y0);
}

/**
 * @brief A wedge from (@p ax, @p ay), @p w0 wide, to (@p bx, @p by),
 *        @p w1 wide, in TAPER_STEPS capsules.
 * @param c the canvas
 * @param ax the wide end's column, px
 * @param ay its row, px
 * @param bx the narrow end's column, px
 * @param by its row, px
 * @param w0 the width at the wide end, px
 * @param w1 the width at the narrow end, px
 * @param col the colour
 */
static void taper(gfx_canvas_t *c, float ax, float ay, float bx, float by,
                  float w0, float w1, gfx_color_t col)
{
    for (int i = 0; i < TAPER_STEPS; ++i) {
        const float t0 = (float)i / (float)TAPER_STEPS;
        const float t1 = (float)(i + 1) / (float)TAPER_STEPS;
        const float w = w0 + (w1 - w0) * (t0 + t1) * 0.5f;
        gfx_capsule_aaf(c, ax + (bx - ax) * t0, ay + (by - ay) * t0,
                        ax + (bx - ax) * t1, ay + (by - ay) * t1, w, col);
    }
}

/**
 * @brief A dashed line along @p deg from the hinge dot's edge out to the
 *        flap's length.
 * @param c the canvas
 * @param f the places
 * @param deg the angle, degrees
 * @param col the colour
 */
static void dashed(gfx_canvas_t *c, const rudder_flap_t *f, float deg,
                   gfx_color_t col)
{
    for (float r = RUD_FLAP_HINGE_R + 3.0f; r < f->flap_len;
         r += DASH_ON + DASH_OFF) {
        const float r1 = fminf(r + DASH_ON, f->flap_len);
        float x0, y0, x1, y1;
        rudder_flap_at(f, deg, r, &x0, &y0);
        rudder_flap_at(f, deg, r1, &x1, &y1);
        gfx_capsule_aaf(c, x0, y0, x1, y1, LINE_W, col);
    }
}

/**
 * @brief The scale's ticks at 0, ±30, ±60 and ±90°, just past the flap's
 *        reach.
 * @param c the canvas
 * @param f the places
 */
static void ticks(gfx_canvas_t *c, const rudder_flap_t *f)
{
    const gfx_color_t col = ui_theme_color(UI_C_TEXT_FAINT);
    for (int d = -90; d <= 90; d += 30) {
        float x0, y0, x1, y1;
        rudder_flap_at(f, (float)d, f->flap_len + RUD_FLAP_TICK_IN, &x0, &y0);
        rudder_flap_at(f, (float)d, f->flap_len + RUD_FLAP_TICK_OUT, &x1, &y1);
        gfx_capsule_aaf(c, x0, y0, x1, y1, LINE_W, col);
    }
}

void rudder_flap_draw(gfx_canvas_t *c, gfx_rect_t area,
                      const rudder_flap_view_t *v)
{
    if (c == NULL || area.w <= 0 || area.h <= 0) {
        return;
    }
    const gfx_rect_t old = c->clip;
    if (!gfx_clip_intersect(c, area)) {
        c->clip = old;
        return;
    }
    gfx_fill_rect(c, area.x, area.y, area.w, area.h,
                  ui_theme_color(UI_C_PANEL));
    if (v != NULL) {
        const rudder_flap_t f = rudder_flap_layout(area);
        const float hx = f.hinge_x, hy = f.hinge_y;
        gfx_hline(c, (int)hx, (int)hy, (int)f.flap_len,
                  ui_theme_color(UI_C_GRID));
        ticks(c, &f);
        for (int k = 0; k < 2; ++k) {
            if (v->have_max[k]) {
                const float m = fabsf(v->max_deg[k]);
                dashed(c, &f, (k == 0) ? -m : m,
                       ui_theme_color(UI_C_GRID_STRONG));
            }
        }
        if (v->targets) {
            for (int k = 0; k < 2; ++k) {
                dashed(c, &f, v->target_deg[k], ui_theme_color(UI_C_WARN));
            }
        }
        /* The fixed surface: round at the leading edge, tapering to the
         * hinge. */
        taper(c, hx - f.fixed_len, hy, hx, hy, 2.0f * RUD_FLAP_LE_R,
              RUD_FLAP_FIXED_END_W, ui_theme_color(UI_C_TEXT_FAINT));
        /* The flap: live in the accent colour, else dim on the neutral
         * line, so no angle that is not a reading is drawn. */
        const float deg = v->live ? v->defl_deg : 0.0f;
        float tx, ty;
        rudder_flap_at(&f, deg, f.flap_len, &tx, &ty);
        taper(c, hx, hy, tx, ty, RUD_FLAP_ROOT_W, RUD_FLAP_TIP_W,
              v->live ? ui_theme_color(UI_C_ACCENT)
                      : ui_theme_color(UI_C_GRID_STRONG));
        gfx_fill_circle_aaf(c, hx, hy, RUD_FLAP_HINGE_R,
                            ui_theme_color(UI_C_TEXT));
    }
    c->clip = old;
}
