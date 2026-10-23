/*
 * The SERVO screen's moving horn as the frames draw it.
 *
 * A horn that moves repaints the boxes around it, as each buffer last drew
 * it and as it is now, and the numerals that follow it, not the two cards.
 * Held against the same frames drawn whole, both buffers come out pixel
 * for pixel the same.  The horn is placed in fractions of a pixel: its tip
 * moves by what its angle moves it, not by whole pixels.
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_screen_rig.h"

/** @brief The horn's length, shaft to tip, px, mirrored from
 *         servo_screen_core_int.h. */
#define HORN_L 98

/** @brief How a run of frames is drawn. */
typedef enum {
    RUN_SWEEP,      /**< a sweep, SPEED 100 */
    RUN_DRAG,       /**< a drag round the dial, SPEED 100 */
    RUN_DRAG_SLOW,  /**< a drag at SPEED 50: the commanded horn behind */
} run_t;

#define FRAMES 160

/** @brief A frame's picture, as a 64-bit FNV-1a hash of its buffer. */
static uint64_t picture_hash(const gfx_color_t *px)
{
    uint64_t h = 1469598103934665603ull;
    const uint8_t *b = (const uint8_t *)px;
    for (size_t i = 0; i < FB_BYTES; ++i) {
        h = (h ^ b[i]) * 1099511628211ull;
    }
    return h;
}

/** @brief The finger of a drag at frame @p i: round the dial and back. */
static void drag_feed(int i)
{
    const float deg = -70.0f + 140.0f * fabsf(sinf((float)i * 0.035f));
    int x, y;
    dial_at(deg, 110, &x, &y);
    ev(x, y, (i == 0) ? TOUCH_EVENT_DOWN : TOUCH_EVENT_MOVE, 1);
}

/**
 * @brief @p run's frames at @p hz, each drawn into buffer i & 1 the way
 *        the screen repaints it, or whole when @p whole.
 * @param run what moves the horn
 * @param hz the frame rate
 * @param whole every frame drawn whole
 * @param hash each frame's picture
 * @param moved whether the horn's pulse changed in that frame
 */
static void draw_run(run_t run, float hz, bool whole, uint64_t *hash,
                     bool *moved)
{
    fresh_armed();
    if (run == RUN_DRAG_SLOW) {
        tap(SPEED_X_HALF, SPEED_Y);
    }
    if (run == RUN_SWEEP) {
        servo_screen_set_sweep(true);
    }
    two_buffers();
    if (run == RUN_SWEEP) {
        sweep_go();
    }
    uint16_t was = servo_screen_drawn();
    for (int i = 0; i < FRAMES; ++i) {
        if (run != RUN_SWEEP) {
            drag_feed(i);
        }
        scr->tick(1.0f / hz);
        if (whole) {
            servo_invalidate();
        }
        scr->render((i & 1) ? &cv1 : &cv, i & 1);
        hash[i] = picture_hash((i & 1) ? fb1 : fb);
        moved[i] = servo_screen_drawn() != was;
        was = servo_screen_drawn();
        servo_cmd_t junk;
        while (servo_screen_take(&junk)) { }
    }
}

/**
 * @brief Whether @p run drawn the screen's way is the run drawn whole: at
 *        every frame when @p every, else at the frames the horn moved in.
 *
 * The grip's breathing repaints at eighths of a radian of its phase; at
 * 25Hz each frame takes it over the next eighth, so every frame of the
 * screen's way holds; at 60Hz a frame the horn stands still in may show
 * the breath an eighth behind.
 */
static bool same_as_whole(run_t run, float hz, bool every)
{
    static uint64_t part[FRAMES], full[FRAMES];
    static bool moved[FRAMES], moved_full[FRAMES];
    draw_run(run, hz, false, part, moved);
    draw_run(run, hz, true, full, moved_full);
    int checked = 0;
    for (int i = 0; i < FRAMES; ++i) {
        if (moved[i] != moved_full[i]) {
            return false;
        }
        if (!every && !moved[i]) {
            continue;
        }
        ++checked;
        if (part[i] != full[i]) {
            printf("    run %d at %.0f Hz differs at frame %d\n", (int)run,
                   (double)hz, i);
            return false;
        }
    }
    return checked > FRAMES / 2;
}

/* ------------------------------------------------------- the dirty boxes */

/* A sweep, a drag, and a drag slow enough for the commanded horn to show
 * behind the drawn one: both buffers hold what a full repaint draws. */
TEST_CASE(the_horn_repainted_in_its_boxes_is_the_whole_frame)
{
    CHECK(same_as_whole(RUN_SWEEP, 25.0f, true));
    CHECK(same_as_whole(RUN_DRAG, 25.0f, true));
    CHECK(same_as_whole(RUN_DRAG_SLOW, 25.0f, true));
    CHECK(same_as_whole(RUN_SWEEP, 60.4f, false));
    CHECK(same_as_whole(RUN_DRAG, 60.4f, false));
    CHECK(same_as_whole(RUN_DRAG_SLOW, 60.4f, false));
}

/* A drag repaints neither card whole: a pixel marked in the right card
 * and one on the left card's case survive half a second of frames, in
 * both buffers, while the horn moves. */
TEST_CASE(a_moving_horn_leaves_the_cards_alone)
{
    fresh_armed();
    two_buffers();
    drag_feed(0);
    for (int i = 0; i < 4; ++i) {
        scr->tick(1.0f / 60.4f);
        scr->render((i & 1) ? &cv1 : &cv, i & 1);
    }
    static const int k_mark[2][2] = { { SV_RC_X + 60, 250 }, { 30, 250 } };
    for (int m = 0; m < 2; ++m) {
        fb[(size_t)k_mark[m][1] * W + k_mark[m][0]]  = GFX_MAGENTA;
        fb1[(size_t)k_mark[m][1] * W + k_mark[m][0]] = GFX_MAGENTA;
    }
    const uint16_t from = servo_screen_drawn();
    for (int i = 1; i < 31; ++i) {
        drag_feed(i);
        scr->tick(1.0f / 60.4f);
        scr->render((i & 1) ? &cv1 : &cv, i & 1);
    }
    CHECK(servo_screen_drawn() != from);
    for (int m = 0; m < 2; ++m) {
        CHECK_EQ(fb[(size_t)k_mark[m][1] * W + k_mark[m][0]], GFX_MAGENTA);
        CHECK_EQ(fb1[(size_t)k_mark[m][1] * W + k_mark[m][0]], GFX_MAGENTA);
    }
}

/* ------------------------------------------------------- the sub-pixel arm */

/** @brief The perceived luminance of @p c from its RGB565 fields. */
static int lum(gfx_color_t c)
{
    return (int)((c >> 11) & 31u) * 2 + (int)((c >> 5) & 63u)
           + (int)(c & 31u) * 2;
}

/**
 * @brief The centre of the tip's hole in @p px, from the pixels within
 *        6px of (@p x, @p y): the hole is the card's colour in the arm's,
 *        and the next hole is 12px further in.
 */
static void hole_at(const gfx_color_t *px, double x, double y, double *hx,
                    double *hy)
{
    const int la = lum(ui_theme_color(UI_C_ACCENT));
    const int lp = lum(ui_theme_color(UI_C_PANEL));
    double sw = 0.0, sx = 0.0, sy = 0.0;
    for (int py = (int)y - 7; py <= (int)y + 7; ++py) {
        for (int qx = (int)x - 7; qx <= (int)x + 7; ++qx) {
            if ((qx - x) * (qx - x) + (py - y) * (py - y) > 36.0) {
                continue;
            }
            double w = (double)(la - lum(px[(size_t)py * W + qx]))
                       / (double)(la - lp);
            w = (w < 0.0) ? 0.0 : (w > 1.0) ? 1.0 : w;
            sw += w;
            sx += w * qx;
            sy += w * py;
        }
    }
    *hx = sx / sw;
    *hy = sy / sw;
}

/*
 * The horn turned in steps of 0.37deg, 0.63px at its tip: the hole
 * at the tip is drawn within 0.1px of where the angle puts it, and each
 * step moves it by the step's length to 0.05px rms.  Rounded to whole
 * pixels the hole was up to 1.3px off and its steps 0.39px rms.
 */
TEST_CASE(the_horn_moves_by_fractions_of_a_pixel)
{
    fresh_armed();
    const double k = 3.14159265358979 / 180.0;
    double worst = 0.0, sum2 = 0.0;
    int n = 0;
    double px = 0.0, py = 0.0, ix = 0.0, iy = 0.0;
    for (int i = 0; i <= 216; ++i) {
        const float deg = -40.0f + 0.37f * (float)i;
        servo_screen_set_commanded(deg);
        scr->tick(1.0f / 60.0f);
        servo_invalidate();
        scr->render(&cv, 0);
        const double x = SHAFT_X + HORN_L * cos(deg * k);
        const double y = SHAFT_Y - HORN_L * sin(deg * k);
        double hx, hy;
        hole_at(fb, x, y, &hx, &hy);
        const double off = hypot(hx - x, hy - y);
        worst = (off > worst) ? off : worst;
        if (i > 0) {
            const double dev = hypot(hx - px, hy - py) - hypot(x - ix, y - iy);
            sum2 += dev * dev;
            ++n;
        }
        px = hx; py = hy; ix = x; iy = y;
    }
    const double rms = sqrt(sum2 / n);
    if (worst > 0.1 || rms > 0.05) {
        T_FAIL("hole up to %.3f px off, steps %.3f px rms", worst, rms);
    }
}

/* A reading past three digits, +1000.0deg and over from a pulse far off
 * the profile, draws wider numerals than -888.8: the repaint of the angle
 * covers what each buffer drew last and what it draws now, so the wide
 * numerals go when the reading comes back. */
TEST_CASE(a_wide_angle_is_cleared_when_it_narrows)
{
    fresh_armed();
    two_buffers();
    servo_screen_feedback(1500u, 0.2f, true);
    CHECK(both_whole());
    servo_screen_feedback(9000u, 0.2f, true);
    CHECK(both_whole());
    servo_screen_feedback(1500u, 0.2f, true);
    CHECK(both_whole());
    servo_screen_feedback(60000u, 0.2f, true);
    CHECK(both_whole());
    servo_screen_feedback(1000u, 0.2f, true);
    CHECK(both_whole());
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    RUN(the_horn_repainted_in_its_boxes_is_the_whole_frame);
    RUN(a_moving_horn_leaves_the_cards_alone);
    RUN(the_horn_moves_by_fractions_of_a_pixel);
    RUN(a_wide_angle_is_cleared_when_it_narrows);
    return test_summary("servo_repaint");
}
