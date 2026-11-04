/**
 * @file test_servo_rudder_flap.c
 * @brief RUDDER's side view of the surface (servo_rudder_flap.h): the
 *        places in RUDDER's area, the trailing edge at 0, ±45 and ±90°,
 *        an angle past ±90° held there, the whole flap inside the area at
 *        every degree from -90 to +90, and the drawing's colours and
 *        bounds.
 *
 * Cases:
 * - the_places_follow_the_area
 * - the_trailing_edge_sits_at_0_45_and_90_degrees_either_side
 * - an_angle_past_90_degrees_is_drawn_at_90
 * - the_flap_stays_inside_the_area_at_every_degree
 * - a_small_area_gives_no_negative_length
 * - the_live_flap_is_cyan_above_the_neutral_line_at_a_positive_angle
 * - without_a_reading_the_flap_lies_dim_on_the_neutral_line
 * - the_max_and_the_targets_are_dashed_in_their_colours
 * - the_drawing_paints_nothing_outside_its_area
 * - a_new_angle_changes_pixels_only_inside_the_two_bounds
 * - the_ticks_mark_0_30_60_and_90_degrees_either_side
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <string.h>

#include "greatest.h"

#include "gfx.h"
#include "servo_rudder_flap.h"
#include "ui_theme.h"

/** @brief RUDDER's area for the drawing: x 428 to 618, y 340 to 492. */
static const gfx_rect_t k_area = { 428, 340, 190, 152 };

#define W 640 /**< The test canvas's width, px. */
#define H 520 /**< Its height, px. */

static gfx_color_t s_px[W * H];
static gfx_canvas_t s_c;

/** @brief The canvas cleared to a colour no drawing uses. */
static void fresh(void)
{
    for (size_t i = 0; i < (size_t)W * H; ++i) {
        s_px[i] = 0x1234u;
    }
    gfx_canvas_init(&s_c, s_px, W, H, 0);
}

/**
 * @brief The pixel at (@p x, @p y).
 * @return its colour
 */
static gfx_color_t px_at(int x, int y)
{
    return s_px[(size_t)y * W + (size_t)x];
}

/* The hinge at the area's centre, (523, 416); the flap half of 152px less
 * 14px, 62px; the fixed surface 1.15 x that, 71.3px. */
TEST_CASE(the_places_follow_the_area)
{
    const rudder_flap_t f = rudder_flap_layout(k_area);
    CHECK_NEAR(f.hinge_x, 523.0f, 1e-4f);
    CHECK_NEAR(f.hinge_y, 416.0f, 1e-4f);
    CHECK_NEAR(f.flap_len, 62.0f, 1e-4f);
    CHECK_NEAR(f.fixed_len, 62.0f * 1.15f, 1e-3f);
    /* A wide, low area: the height sets the length, 50 - 14 = 36px. */
    const rudder_flap_t g = rudder_flap_layout((gfx_rect_t){ 0, 0, 400, 100 });
    CHECK_NEAR(g.flap_len, 36.0f, 1e-4f);
    /* A narrow, tall one: the fixed surface and its leading edge hold the
     * half width less the margin, (60 - 6 - 7) / 1.15 = 40.9, so 40px. */
    const rudder_flap_t n = rudder_flap_layout((gfx_rect_t){ 0, 0, 120, 400 });
    CHECK_NEAR(n.flap_len, 40.0f, 1e-4f);
    CHECK(n.hinge_x - n.fixed_len - RUD_FLAP_LE_R >= RUD_FLAP_PAD);
}

/* Positive is the trailing edge up, the screen's rows counting down. */
TEST_CASE(the_trailing_edge_sits_at_0_45_and_90_degrees_either_side)
{
    const rudder_flap_t f = rudder_flap_layout(k_area);
    const float hx = f.hinge_x, hy = f.hinge_y, l = f.flap_len;
    const float d = l * 0.70710678f;
    float x = 0.0f, y = 0.0f;
    rudder_flap_at(&f, 0.0f, l, &x, &y);
    CHECK_NEAR(x, hx + l, 1e-3f);
    CHECK_NEAR(y, hy, 1e-3f);
    rudder_flap_at(&f, 45.0f, l, &x, &y);
    CHECK_NEAR(x, hx + d, 1e-3f);
    CHECK_NEAR(y, hy - d, 1e-3f);
    rudder_flap_at(&f, -45.0f, l, &x, &y);
    CHECK_NEAR(x, hx + d, 1e-3f);
    CHECK_NEAR(y, hy + d, 1e-3f);
    rudder_flap_at(&f, 90.0f, l, &x, &y);
    CHECK_NEAR(x, hx, 1e-3f);
    CHECK_NEAR(y, hy - l, 1e-3f);
    rudder_flap_at(&f, -90.0f, l, &x, &y);
    CHECK_NEAR(x, hx, 1e-3f);
    CHECK_NEAR(y, hy + l, 1e-3f);
    /* Half way along, half the distance. */
    rudder_flap_at(&f, 90.0f, l / 2.0f, &x, &y);
    CHECK_NEAR(y, hy - l / 2.0f, 1e-3f);
}

/* 90.1°, 135° and 180° are drawn at 90°, their negatives at -90°; a value
 * that is no number lies on the neutral line. */
TEST_CASE(an_angle_past_90_degrees_is_drawn_at_90)
{
    CHECK_EQ(rudder_flap_clamp(90.0f), 90.0f);
    CHECK_EQ(rudder_flap_clamp(90.1f), 90.0f);
    CHECK_EQ(rudder_flap_clamp(135.0f), 90.0f);
    CHECK_EQ(rudder_flap_clamp(180.0f), 90.0f);
    CHECK_EQ(rudder_flap_clamp(-90.0f), -90.0f);
    CHECK_EQ(rudder_flap_clamp(-90.1f), -90.0f);
    CHECK_EQ(rudder_flap_clamp(-179.9f), -90.0f);
    CHECK_EQ(rudder_flap_clamp(89.9f), 89.9f);
    CHECK_EQ(rudder_flap_clamp(-0.1f), -0.1f);
    CHECK_EQ(rudder_flap_clamp(NAN), 0.0f);
    CHECK_EQ(rudder_flap_clamp(INFINITY), 90.0f);
    CHECK_EQ(rudder_flap_clamp(-INFINITY), -90.0f);

    const rudder_flap_t f = rudder_flap_layout(k_area);
    float x0, y0, x1, y1;
    rudder_flap_at(&f, 90.0f, f.flap_len, &x0, &y0);
    rudder_flap_at(&f, 170.0f, f.flap_len, &x1, &y1);
    CHECK_NEAR(x1, x0, 1e-4f);
    CHECK_NEAR(y1, y0, 1e-4f);
    rudder_flap_at(&f, -90.0f, f.flap_len, &x0, &y0);
    rudder_flap_at(&f, -120.0f, f.flap_len, &x1, &y1);
    CHECK_NEAR(x1, x0, 1e-4f);
    CHECK_NEAR(y1, y0, 1e-4f);
}

/* The trailing edge with half its thickness and half a pixel of blend
 * round it, and the outer end of the ticks, inside the area at every whole
 * degree from -90 to +90, and past them; the flap's root and the fixed
 * surface's leading edge too. */
TEST_CASE(the_flap_stays_inside_the_area_at_every_degree)
{
    const rudder_flap_t f = rudder_flap_layout(k_area);
    const float x0 = (float)k_area.x, y0 = (float)k_area.y;
    const float x1 = x0 + (float)k_area.w, y1 = y0 + (float)k_area.h;
    const float e = RUD_FLAP_TIP_W / 2.0f + 0.5f;
    const float t = 1.0f;   /* a tick's half width and its blend */
    for (int d = -100; d <= 100; ++d) {
        float x = 0.0f, y = 0.0f;
        rudder_flap_at(&f, (float)d, f.flap_len, &x, &y);
        if (x - e < x0 || x + e > x1 || y - e < y0 || y + e > y1) {
            T_FAIL("the trailing edge at %d° lies at (%.1f, %.1f)", d,
                   (double)x, (double)y);
        }
        rudder_flap_at(&f, (float)d, f.flap_len + RUD_FLAP_TICK_OUT, &x, &y);
        if (x - t < x0 || x + t > x1 || y - t < y0 || y + t > y1) {
            T_FAIL("a tick at %d° reaches (%.1f, %.1f)", d, (double)x,
                   (double)y);
        }
    }
    const float r = RUD_FLAP_ROOT_W / 2.0f + 0.5f;
    CHECK(f.hinge_y - r >= y0 && f.hinge_y + r <= y1);
    CHECK(f.hinge_x - f.fixed_len - RUD_FLAP_LE_R - 0.5f >= x0);
    CHECK(f.hinge_y - RUD_FLAP_LE_R - 0.5f >= y0);
    CHECK(f.hinge_y + RUD_FLAP_LE_R + 0.5f <= y1);
}

/* An area too small for the margins and the leading edge: no length below
 * 0px, and nothing drawn outside it. */
TEST_CASE(a_small_area_gives_no_negative_length)
{
    const rudder_flap_t f = rudder_flap_layout((gfx_rect_t){ 10, 10, 8, 4 });
    CHECK(f.flap_len >= 0.0f);
    CHECK(f.fixed_len >= 0.0f);
    const rudder_flap_t z = rudder_flap_layout((gfx_rect_t){ 0, 0, 0, 0 });
    CHECK_EQ(z.flap_len, 0.0f);
    CHECK_EQ(z.fixed_len, 0.0f);
    fresh();
    rudder_flap_draw(&s_c, (gfx_rect_t){ 10, 10, 0, 0 }, NULL);
    CHECK_EQ(px_at(10, 10), 0x1234u);
    rudder_flap_draw(NULL, k_area, NULL);
}

/* At +45° the flap's middle is cyan above and right of the hinge, and the
 * same point mirrored below the neutral line is the card's colour. */
TEST_CASE(the_live_flap_is_cyan_above_the_neutral_line_at_a_positive_angle)
{
    fresh();
    const rudder_flap_view_t v = { .live = true, .defl_deg = 45.0f };
    rudder_flap_draw(&s_c, k_area, &v);
    const rudder_flap_t f = rudder_flap_layout(k_area);
    float x = 0.0f, y = 0.0f;
    rudder_flap_at(&f, 45.0f, f.flap_len * 0.5f, &x, &y);
    CHECK_EQ(px_at((int)lroundf(x), (int)lroundf(y)),
             ui_theme_color(UI_C_ACCENT));
    const int my = (int)lroundf(2.0f * f.hinge_y - y);
    CHECK_EQ(px_at((int)lroundf(x), my), ui_theme_color(UI_C_PANEL));
    /* The hinge dot over the root, the fixed surface left of it. */
    CHECK_EQ(px_at((int)f.hinge_x, (int)f.hinge_y),
             ui_theme_color(UI_C_TEXT));
    CHECK_EQ(px_at((int)(f.hinge_x - f.fixed_len), (int)f.hinge_y),
             ui_theme_color(UI_C_TEXT_FAINT));

    /* At -170° the flap points straight down, as at -90°. */
    fresh();
    const rudder_flap_view_t dn = { .live = true, .defl_deg = -170.0f };
    rudder_flap_draw(&s_c, k_area, &dn);
    CHECK_EQ(px_at((int)f.hinge_x, (int)(f.hinge_y + f.flap_len * 0.5f)),
             ui_theme_color(UI_C_ACCENT));
}

/* No reading: the flap on the neutral line in the dim grid colour, not
 * cyan, whatever angle the view still holds. */
TEST_CASE(without_a_reading_the_flap_lies_dim_on_the_neutral_line)
{
    fresh();
    const rudder_flap_view_t v = { .live = false, .defl_deg = 45.0f };
    rudder_flap_draw(&s_c, k_area, &v);
    const rudder_flap_t f = rudder_flap_layout(k_area);
    const int mx = (int)lroundf(f.hinge_x + f.flap_len * 0.5f);
    CHECK_EQ(px_at(mx, (int)f.hinge_y), ui_theme_color(UI_C_GRID_STRONG));
    float x = 0.0f, y = 0.0f;
    rudder_flap_at(&f, 45.0f, f.flap_len * 0.5f, &x, &y);
    CHECK_EQ(px_at((int)lroundf(x), (int)lroundf(y)),
             ui_theme_color(UI_C_PANEL));
    for (int yy = k_area.y; yy < k_area.y + k_area.h; ++yy) {
        for (int xx = k_area.x; xx < k_area.x + k_area.w; ++xx) {
            if (px_at(xx, yy) == ui_theme_color(UI_C_ACCENT)) {
                T_FAIL("cyan at (%d, %d) without a reading", xx, yy);
                return;
            }
        }
    }
}

/**
 * @brief How many pixels of colour @p col lie on the ray at @p deg from
 *        the hinge, from the hinge dot out to the flap's length.
 * @return the count
 */
static int on_ray(const rudder_flap_t *f, float deg, gfx_color_t col)
{
    int n = 0;
    for (float r = RUD_FLAP_HINGE_R + 2.0f; r <= f->flap_len; r += 1.0f) {
        float x = 0.0f, y = 0.0f;
        rudder_flap_at(f, deg, r, &x, &y);
        n += px_at((int)lroundf(x), (int)lroundf(y)) == col;
    }
    return n;
}

/* MAX -30° and +60° dashed in the strong grid colour, AUTO's targets
 * -20° and +40° in amber: lit along part of each ray, not all of it. */
TEST_CASE(the_max_and_the_targets_are_dashed_in_their_colours)
{
    fresh();
    const rudder_flap_view_t v = {
        .live = true, .defl_deg = 0.0f,
        .have_max = { true, true }, .max_deg = { 30.0f, 60.0f },
        .targets = true, .target_deg = { -20.0f, 40.0f },
    };
    rudder_flap_draw(&s_c, k_area, &v);
    const rudder_flap_t f = rudder_flap_layout(k_area);
    const int span = (int)(f.flap_len - RUD_FLAP_HINGE_R - 2.0f);
    const gfx_color_t dim = ui_theme_color(UI_C_GRID_STRONG);
    const gfx_color_t amber = ui_theme_color(UI_C_WARN);
    const int m0 = on_ray(&f, -30.0f, dim), m1 = on_ray(&f, 60.0f, dim);
    const int t0 = on_ray(&f, -20.0f, amber), t1 = on_ray(&f, 40.0f, amber);
    CHECK(m0 > span / 4 && m0 < span);
    CHECK(m1 > span / 4 && m1 < span);
    CHECK(t0 > span / 4 && t0 < span);
    CHECK(t1 > span / 4 && t1 < span);
    /* No MAX, no targets: nothing on those rays. */
    fresh();
    const rudder_flap_view_t bare = { .live = true, .defl_deg = 0.0f };
    rudder_flap_draw(&s_c, k_area, &bare);
    CHECK_EQ(on_ray(&f, 60.0f, dim), 0);
    CHECK_EQ(on_ray(&f, 40.0f, amber), 0);
}

/* At every tenth degree from -90 to +90, with both MAX and both targets
 * at ±90°, no pixel outside the area changes. */
TEST_CASE(the_drawing_paints_nothing_outside_its_area)
{
    for (int d = -900; d <= 900; d += 75) {
        fresh();
        const rudder_flap_view_t v = {
            .live = true, .defl_deg = (float)d / 10.0f,
            .have_max = { true, true }, .max_deg = { 120.0f, 90.0f },
            .targets = true, .target_deg = { -90.0f, 95.0f },
        };
        rudder_flap_draw(&s_c, k_area, &v);
        int outside = 0;
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                const bool in = x >= k_area.x && x < k_area.x + k_area.w
                                && y >= k_area.y && y < k_area.y + k_area.h;
                outside += !in && px_at(x, y) != 0x1234u;
            }
        }
        if (outside != 0) {
            T_FAIL("%d pixels outside the area at %.1f°", outside,
                   (double)d / 10.0);
        }
    }
}

/** @brief A second canvas, the drawing at the angle before. */
static gfx_color_t s_old[W * H];

/* From each angle to one 0.3° and one 20° further: every pixel that
 * differs lies in the bounds of the flap before or after, so a repaint
 * clipped to the two draws what a whole one would; past ±90° nothing
 * differs. */
TEST_CASE(a_new_angle_changes_pixels_only_inside_the_two_bounds)
{
    const rudder_flap_t f = rudder_flap_layout(k_area);
    static const float k_step[2] = { 0.3f, 20.0f };
    for (int d = -95; d <= 95; d += 5) {
        for (int k = 0; k < 2; ++k) {
            const float a = (float)d, b = (float)d + k_step[k];
            rudder_flap_view_t v = {
                .live = true, .defl_deg = a,
                .have_max = { true, true }, .max_deg = { 40.0f, 70.0f },
                .targets = true, .target_deg = { -20.0f, 50.0f },
            };
            fresh();
            rudder_flap_draw(&s_c, k_area, &v);
            memcpy(s_old, s_px, sizeof(s_old));
            v.defl_deg = b;
            rudder_flap_draw(&s_c, k_area, &v);
            const gfx_rect_t ra = rudder_flap_bounds(&f, a);
            const gfx_rect_t rb = rudder_flap_bounds(&f, b);
            int stray = 0, moved = 0;
            for (int y = 0; y < H; ++y) {
                for (int x = 0; x < W; ++x) {
                    if (s_px[(size_t)y * W + x] == s_old[(size_t)y * W + x]) {
                        continue;
                    }
                    ++moved;
                    stray += !gfx_rect_contains(ra, x, y)
                             && !gfx_rect_contains(rb, x, y);
                }
            }
            /* Two angles held to the same ±90° draw the same flap. */
            const bool same = rudder_flap_clamp(a) == rudder_flap_clamp(b);
            if (stray != 0 || (moved == 0) != same) {
                T_FAIL("%.1f° to %.1f°: %d of %d changed pixels outside",
                       (double)a, (double)b, stray, moved);
            }
        }
    }
    /* The bounds hold the hinge dot and the trailing edge. */
    const gfx_rect_t r = rudder_flap_bounds(&f, 90.0f);
    CHECK(gfx_rect_contains(r, (int)f.hinge_x, (int)(f.hinge_y - f.flap_len)));
    CHECK(gfx_rect_contains(r, (int)(f.hinge_x + RUD_FLAP_HINGE_R),
                            (int)f.hinge_y));
}

/* A tick at every 30° from -90 to +90, past the flap's reach, and none
 * at the 15° between them. */
TEST_CASE(the_ticks_mark_0_30_60_and_90_degrees_either_side)
{
    fresh();
    const rudder_flap_view_t v = { .live = true, .defl_deg = 0.0f };
    rudder_flap_draw(&s_c, k_area, &v);
    const rudder_flap_t f = rudder_flap_layout(k_area);
    const float r = f.flap_len + (RUD_FLAP_TICK_IN + RUD_FLAP_TICK_OUT) / 2.0f;
    for (int d = -90; d <= 90; d += 15) {
        float x = 0.0f, y = 0.0f;
        rudder_flap_at(&f, (float)d, r, &x, &y);
        const bool tick = (d % 30) == 0;
        const gfx_color_t px = px_at((int)lroundf(x), (int)lroundf(y));
        /* A 1.5px tick on a slant blends its centre pixel. */
        if ((px != ui_theme_color(UI_C_PANEL)) != tick) {
            T_FAIL("at %d° the pixel is %04x", d, (unsigned)px);
        }
    }
}

int main(void)
{
    RUN(the_places_follow_the_area);
    RUN(the_trailing_edge_sits_at_0_45_and_90_degrees_either_side);
    RUN(an_angle_past_90_degrees_is_drawn_at_90);
    RUN(the_flap_stays_inside_the_area_at_every_degree);
    RUN(a_small_area_gives_no_negative_length);
    RUN(the_live_flap_is_cyan_above_the_neutral_line_at_a_positive_angle);
    RUN(without_a_reading_the_flap_lies_dim_on_the_neutral_line);
    RUN(the_max_and_the_targets_are_dashed_in_their_colours);
    RUN(the_drawing_paints_nothing_outside_its_area);
    RUN(a_new_angle_changes_pixels_only_inside_the_two_bounds);
    RUN(the_ticks_mark_0_30_60_and_90_degrees_either_side);
    return test_summary("servo_rudder_flap");
}
