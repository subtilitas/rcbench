/**
 * @file test_rudder_flap_view.c
 * @brief RUDDER's side view of the surface on the SERVO screen: the flap
 *        at the deflection DEFLECTION shows, after ZERO and REVERSE; no
 *        live flap without a reading; an angle past ±90° drawn at ±90°;
 *        the MAX of each side; AUTO's targets in AUTO's view alone; the
 *        light theme's colours; and the view's place beside the state
 *        lines.
 *
 * The AS5600 is on with its centre at count 2048; 256 counts are 22.5°.
 *
 * Cases:
 * - the_side_view_lies_beside_the_state_lines_under_the_zero_row
 * - the_flap_follows_the_deflection_shown_and_reverse
 * - without_a_reading_the_flap_is_never_drawn_live
 * - an_angle_past_90_degrees_draws_the_flap_straight_down
 * - the_max_of_each_side_is_dashed_from_the_hinge
 * - auto_s_view_adds_the_targets_in_amber
 * - the_light_theme_draws_in_its_own_colours
 * - a_new_angle_repaints_the_flap_s_boxes_alone_as_a_whole_frame_would
 * - auto_s_three_lines_are_drawn_beside_the_view
 *
 * SPDX-License-Identifier: MIT
 */
#include "servo_screen_rig.h"

#include "servo_rudder_auto_int.h"
#include "servo_rudder_flap.h"
#include "servo_rudder_int.h"

/** @brief The AS5600's centre count in these cases. */
#define CENTRE_COUNT 2048u

/** @brief The AS5600 on, its centre at CENTRE_COUNT. */
static void encoder_on(void)
{
    settings_set(SET_ENC_EN, 1.0f);
    settings_set(SET_ENC_CENTRE, (float)CENTRE_COUNT);
}

/**
 * @brief A reading at @p counts from the centre, as the panel hands it.
 * @param counts the counts, signed
 */
static void reading(int counts)
{
    static uint32_t at_ms = 1000u;
    at_ms += 40u;
    const servo_test_enc_t e = {
        .valid = true,
        .raw = (uint16_t)(((int)CENTRE_COUNT + counts) & 4095),
        .taken_ms = at_ms };
    servo_screen_encoder(&e);
}

/** @brief RUDDER with a reading at the centre taken as the zero. */
static void zeroed(void)
{
    encoder_on();
    tap(SV_TAB_RUDDER_X, SV_TAB_Y);
    reading(0);
    const gfx_rect_t z = rudder_rect(RUD_C_ZERO);
    tap(z.x + z.w / 2, z.y + z.h / 2);
}

/**
 * @brief The pixel @p r px out along a flap at @p deg, as drawn now.
 * @param deg the angle, degrees
 * @param r the distance from the hinge, px
 * @return its colour
 */
static gfx_color_t at(float deg, float r)
{
    const rudder_flap_t f = rudder_flap_layout(rudder_flap_rect());
    float x = 0.0f, y = 0.0f;
    rudder_flap_at(&f, deg, r, &x, &y);
    return fb[(size_t)lroundf(y) * W + (size_t)lroundf(x)];
}

/**
 * @brief Pixels of colour @p col in the side view's area.
 * @return the count
 */
static int in_view(gfx_color_t col)
{
    const gfx_rect_t a = rudder_flap_rect();
    int n = 0;
    for (int y = a.y; y < a.y + a.h; ++y) {
        for (int x = a.x; x < a.x + a.w; ++x) {
            n += fb[(size_t)y * W + x] == col;
        }
    }
    return n;
}

/** @brief The flap's length in the view, px. */
static float flap_len(void)
{
    return rudder_flap_layout(rudder_flap_rect()).flap_len;
}

/** @brief Whether two rectangles share a pixel. */
static bool overlap(gfx_rect_t a, gfx_rect_t b)
{
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h
           && b.y < a.y + a.h;
}

/* x 428 to 618, y 340 to 492: under the ZERO row, right of the state
 * lines, inside the card and above the alert band, on no control. */
TEST_CASE(the_side_view_lies_beside_the_state_lines_under_the_zero_row)
{
    fresh();
    tap(SV_TAB_RUDDER_X, SV_TAB_Y);
    const gfx_rect_t a = rudder_flap_rect();
    CHECK_EQ(a.x, 428);
    CHECK_EQ(a.y, 340);
    CHECK_EQ(a.x + a.w, 618);
    CHECK_EQ(a.y + a.h, UI_ALERT_Y - UI_BAND_H - 4);
    CHECK(!overlap(a, rudder_state_rect()));
    CHECK(!overlap(a, rudder_readout_rect()));
    CHECK(rudder_state_rect().x + rudder_state_rect().w + 10 == a.x);
    for (int k = RUD_C_LENGTH; k < RUD_C_COUNT; ++k) {
        CHECK(!overlap(a, rudder_rect((rudder_ctl_t)k)));
    }
    /* No touch on it: a tap on the hinge changes nothing. */
    const rudder_flap_t f = rudder_flap_layout(a);
    tap((int)f.hinge_x, (int)f.hinge_y);
    CHECK_EQ(servo_screen_task(), (int)UI_WB_RUDDER);
    CHECK(!rauto_shown());
}

/* +22.5° from the zero: the flap's middle is cyan up the right; with
 * REVERSE the deflection reads -22.5° and the flap points down. */
TEST_CASE(the_flap_follows_the_deflection_shown_and_reverse)
{
    fresh();
    zeroed();
    reading(256);
    scr->render(&cv, 0);
    const float l = flap_len();
    const gfx_color_t cyan = ui_theme_color(UI_C_ACCENT);
    CHECK_EQ(at(22.5f, l * 0.5f), cyan);
    CHECK_EQ(at(-22.5f, l * 0.5f), ui_theme_color(UI_C_PANEL));
    CHECK_EQ(at(22.5f, l - 2.0f), cyan);
    open_settings();
    tap(TAB_X(PAGE_OUTPUT), TAB_Y);
    tap(ROW_R_X, ROW_Y(4));                     /* REVERSE */
    tap(SV_TAB_RUDDER_X, SV_TAB_Y);
    float d = 0.0f;
    CHECK(rudder_defl(&d));
    CHECK_NEAR(d, -22.5f, 1e-3f);
    scr->render(&cv, 0);
    CHECK_EQ(at(-22.5f, l * 0.5f), cyan);
    CHECK_EQ(at(22.5f, l * 0.5f), ui_theme_color(UI_C_PANEL));
}

/* A reading on show, then the link lost and the source switched off: the
 * flap lies dim on the neutral line, no cyan in the view, as DEFLECTION
 * reads ---. */
TEST_CASE(without_a_reading_the_flap_is_never_drawn_live)
{
    fresh();
    zeroed();
    reading(256);
    scr->render(&cv, 0);
    CHECK(in_view(ui_theme_color(UI_C_ACCENT)) > 0);
    servo_screen_set_link(false);
    CHECK(!rudder_defl(NULL));
    scr->render(&cv, 0);
    CHECK_EQ(in_view(ui_theme_color(UI_C_ACCENT)), 0);
    CHECK_EQ(at(0.0f, flap_len() * 0.5f), ui_theme_color(UI_C_GRID_STRONG));
    servo_screen_set_link(true);
    settings_set(SET_ENC_EN, 0.0f);
    servo_invalidate();
    scr->render(&cv, 0);
    CHECK_EQ(rudder_source(), RUD_SRC_OFF);
    CHECK_EQ(in_view(ui_theme_color(UI_C_ACCENT)), 0);
    /* Never a reading at all. */
    fresh();
    tap(SV_TAB_RUDDER_X, SV_TAB_Y);
    scr->render(&cv, 0);
    CHECK_EQ(in_view(ui_theme_color(UI_C_ACCENT)), 0);
    CHECK_EQ(at(0.0f, flap_len() * 0.5f), ui_theme_color(UI_C_GRID_STRONG));
}

/* -1400 counts, -123.0° on DEFLECTION: the flap points straight down from
 * the hinge, its trailing edge inside the view. */
TEST_CASE(an_angle_past_90_degrees_draws_the_flap_straight_down)
{
    fresh();
    zeroed();
    reading(-1400);
    float d = 0.0f;
    CHECK(rudder_defl(&d));
    CHECK(d < -120.0f);
    scr->render(&cv, 0);
    const float l = flap_len();
    const gfx_color_t cyan = ui_theme_color(UI_C_ACCENT);
    CHECK_EQ(at(-90.0f, l * 0.5f), cyan);
    CHECK_EQ(at(-90.0f, l - 2.0f), cyan);
    CHECK_EQ(at(0.0f, l * 0.5f), ui_theme_color(UI_C_GRID));
}

/**
 * @brief How many pixels of colour @p col lie on the ray at @p deg from
 *        the hinge dot's edge to the flap's length.
 * @return the count
 */
static int on_ray(float deg, gfx_color_t col)
{
    int n = 0;
    for (float r = RUD_FLAP_HINGE_R + 8.0f; r <= flap_len(); r += 1.0f) {
        n += at(deg, r) == col;
    }
    return n;
}

/* MAX -22.5° and +45.0° after two readings: dashed in the strong grid
 * colour along both rays while the flap rests at 0°. */
TEST_CASE(the_max_of_each_side_is_dashed_from_the_hinge)
{
    fresh();
    zeroed();
    reading(-256);
    reading(512);
    reading(0);
    scr->render(&cv, 0);
    const gfx_color_t dim = ui_theme_color(UI_C_GRID_STRONG);
    CHECK(on_ray(-22.5f, dim) > 10);
    CHECK(on_ray(45.0f, dim) > 10);
    CHECK_EQ(on_ray(-45.0f, dim), 0);
    /* RESET MAX takes them off. */
    const gfx_rect_t r = rudder_rect(RUD_C_RESET);
    tap(r.x + r.w / 2, r.y + r.h / 2);
    scr->render(&cv, 0);
    CHECK_EQ(on_ray(45.0f, dim), 0);
}

/* AUTO's view: TARGET 1000us 15.0° and TARGET 2000us 30.0° dashed in
 * amber at -15° and +30°; MANUAL's view has no amber in the side view. */
TEST_CASE(auto_s_view_adds_the_targets_in_amber)
{
    fresh();
    zeroed();
    settings_set(SET_RUDDER_DEFL_LO, 15.0f);
    settings_set(SET_RUDDER_DEFL_HI, 30.0f);
    reading(0);
    scr->render(&cv, 0);
    const gfx_color_t amber = ui_theme_color(UI_C_WARN);
    CHECK_EQ(in_view(amber), 0);
    tap(SV_RUD_AUTO_X, SV_RUD_ZERO_Y);
    CHECK(rauto_shown());
    scr->render(&cv, 0);
    CHECK(on_ray(-15.0f, amber) > 10);
    CHECK(on_ray(30.0f, amber) > 10);
    CHECK_EQ(on_ray(-30.0f, amber), 0);
    tap(SV_RUD_AUTO_X, SV_RUD_ZERO_Y);
    scr->render(&cv, 0);
    CHECK_EQ(in_view(amber), 0);
}

/* The light theme: the flap in its accent, the fixed surface in its faint
 * text colour, the area in its card colour. */
TEST_CASE(the_light_theme_draws_in_its_own_colours)
{
    fresh();
    ui_theme_set(UI_THEME_LIGHT);
    zeroed();
    reading(256);
    servo_invalidate();
    scr->render(&cv, 0);
    const rudder_flap_t f = rudder_flap_layout(rudder_flap_rect());
    CHECK_EQ(at(22.5f, f.flap_len * 0.5f), ui_theme_color(UI_C_ACCENT));
    CHECK_EQ(fb[(size_t)f.hinge_y * W + (size_t)(f.hinge_x - f.fixed_len)],
             ui_theme_color(UI_C_TEXT_FAINT));
    CHECK_EQ(at(-22.5f, f.flap_len * 0.5f), ui_theme_color(UI_C_PANEL));
    ui_theme_set(UI_THEME_DARK);
}

/* Inside both MAX, a new angle repaints the boxes of the flap as drawn
 * and as now, not the view: a pixel of the view's corner keeps what it
 * holds.  Angles there and back, in both buffers in turn, end as frames
 * drawn whole; a new MAX repaints the view. */
TEST_CASE(a_new_angle_repaints_the_flap_s_boxes_alone_as_a_whole_frame_would)
{
    fresh();
    zeroed();
    reading(-600);
    reading(600);
    reading(0);
    two_buffers();
    CHECK(both_whole());
    const gfx_rect_t a = rudder_flap_rect();
    const size_t corner = (size_t)(a.y + 1) * W + (size_t)(a.x + 1);
    fb[corner] = 0x1234u;
    reading(30);
    scr->render(&cv, 0);
    CHECK_EQ(fb[corner], 0x1234u);
    static const int k_go[] = { 256, 128, -300, 40, -40, 500 };
    for (size_t i = 0; i < sizeof(k_go) / sizeof(k_go[0]); ++i) {
        reading(k_go[i]);
        scr->render(&cv, 0);
        scr->render(&cv1, 1);
    }
    fb[corner] = ui_theme_color(UI_C_PANEL);
    CHECK(both_whole());
    fb[corner] = 0x1234u;
    reading(700);                               /* a new MAX */
    scr->render(&cv, 0);
    CHECK_EQ(fb[corner], ui_theme_color(UI_C_PANEL));
}

/**
 * @brief Pixels in a row band of AUTO's lines, x 18 to 418, that are not
 *        the card's colour.
 * @param y0 the band's top, body px
 * @param h its height, px
 * @return the count
 */
static int ink_in(int y0, int h)
{
    int n = 0;
    for (int y = y0; y < y0 + h; ++y) {
        for (int x = 18; x < 18 + RUD_SIDE_W; ++x) {
            n += fb[(size_t)y * W + x] != ui_theme_color(UI_C_PANEL);
        }
    }
    return n;
}

/* SAFE in AUTO's box: ARM FIRST on the first line's row, nothing on the
 * second line, SERVO KST/CHASERVO on the row after; nothing past the
 * floor. */
TEST_CASE(auto_s_three_lines_are_drawn_beside_the_view)
{
    fresh();
    tap(SV_TAB_RUDDER_X, SV_TAB_Y);
    tap(SV_RUD_AUTO_X, SV_RUD_ZERO_Y);
    scr->render(&cv, 0);
    char b[96];
    CHECK_STR_EQ(rauto_line(1, b, sizeof(b)), "");
    const gfx_rect_t l = rauto_lines_rect();
    const int y0 = l.y + 2;
    CHECK(ink_in(y0, 20) > 0);                  /* ARM FIRST */
    CHECK(ink_in(y0 + 24, 20) > 0);             /* SERVO KST/CHASERVO */
    CHECK_EQ(ink_in(y0 + 48, l.y + l.h - (y0 + 48)), 0);
}

int main(void)
{
    RUN(the_side_view_lies_beside_the_state_lines_under_the_zero_row);
    RUN(the_flap_follows_the_deflection_shown_and_reverse);
    RUN(without_a_reading_the_flap_is_never_drawn_live);
    RUN(an_angle_past_90_degrees_draws_the_flap_straight_down);
    RUN(the_max_of_each_side_is_dashed_from_the_hinge);
    RUN(auto_s_view_adds_the_targets_in_amber);
    RUN(the_light_theme_draws_in_its_own_colours);
    RUN(a_new_angle_repaints_the_flap_s_boxes_alone_as_a_whole_frame_would);
    RUN(auto_s_three_lines_are_drawn_beside_the_view);
    return test_summary("rudder_flap_view");
}
