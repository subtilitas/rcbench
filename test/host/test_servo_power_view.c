/**
 * @file test_servo_power_view.c
 * @brief The plot of what feeds the servo (svs_power_view()), the same on
 *        every SERVO function screen: LIVE, SWEEP and RUDDER on the SET
 *        row, KST and HITEC under their READY rows, TEST under the caps
 *        button, LIMIT RUN under its state lines, RUDDER AUTO right of
 *        ZERO and RESET MAX.  Its legend reads volts, amps and watts in
 *        their traces' colours; a new reading repaints the plot and the
 *        rail line alone; on the rbmod carrier's programming port it and
 *        the rail line read the PROG 5V, `NO READING` and `--V  --A`
 *        without a window, never the PD mini's values.
 *
 * Cases:
 * - every_function_screen_shows_the_plot_in_its_place
 * - the_legend_reads_volts_amps_and_watts_in_their_colours
 * - a_new_reading_repaints_only_the_plot_and_the_line
 * - on_the_carrier_port_no_window_reads_no_reading_not_the_pd_mini
 *
 * SPDX-License-Identifier: MIT
 */
#include "servo_screen_rig.h"

#include "out_bind.h"
#include "prog5v_box.h"
#include "servo_ready_int.h"
#include "servo_rudder_auto_int.h"
#include "ui_board.h"
#include "ui_footer.h"

/** @brief SERVO fresh, the PD mini answering at 5.0V and 1.0A, its output
 *         on, and 20 samples of the rail plotted. */
static void fresh_fed(void)
{
    prog5v_box_reset();
    ui_text_set_language(UI_LANG_EN);
    fresh();
    ui_router_set_alert(NULL);
    supply_screen_put(5.0f, 1.0f);
    supply_screen_set_output(true);
    go(SERVO_FN_LIVE);
    for (int i = 0; i < 20; ++i) {
        rail_supply(5.0f, 0.2f + 0.01f * (float)i);
        scr->tick(0.05f);
    }
    supply_cmd_t c;
    while (supply_cmd(&c)) { }
}

/** @brief The pixels of colour @p col in @p r, buffer 0. */
static int count_color(gfx_rect_t r, ui_color_id_t col)
{
    const gfx_color_t want = ui_theme_color(col);
    int n = 0;
    for (int y = r.y; y < r.y + r.h; ++y) {
        for (int x = r.x; x < r.x + r.w; ++x) {
            n += fb[(size_t)y * W + x] == want;
        }
    }
    return n;
}

/** @brief The function screens and where each shows the plot. */
static const servo_fn_t k_fns[] = {
    SERVO_FN_LIVE, SERVO_FN_SWEEP, SERVO_FN_RUDDER, SERVO_FN_TEST,
    SERVO_FN_LIMRUN, SERVO_FN_RAUTO, SERVO_FN_KST, SERVO_FN_HITEC,
};
#define N_FNS ((int)(sizeof(k_fns) / sizeof(k_fns[0])))

/* Each function screen places the plot: the drive card's SET row, KST's
 * and HITEC's READY card between the last row and the rail line, TEST's,
 * LIMIT RUN's and RUDDER AUTO's left card.  The hub shows none. */
TEST_CASE(every_function_screen_shows_the_plot_in_its_place)
{
    fresh_fed();
    for (int f = 0; f < N_FNS; ++f) {
        go(k_fns[f]);
        scr->tick(0.025f);
        const gfx_rect_t r = svs_power_rect();
        CHECK(r.w >= 180);
        CHECK(r.h >= UI_TAP_MIN);
        switch (k_fns[f]) {
        case SERVO_FN_LIVE:
        case SERVO_FN_SWEEP:
        case SERVO_FN_RUDDER:
        {
            const gfx_rect_t d = drive_plot_rect();
            CHECK(r.x == d.x && r.y == d.y && r.w == d.w && r.h == d.h);
            break;
        }
        case SERVO_FN_KST:
        case SERVO_FN_HITEC: {
            const gfx_rect_t last = ready_row_rect(ready_count() - 1);
            CHECK(r.y >= last.y + last.h);
            CHECK(r.y + r.h <= ready_rail_rect().y);
            CHECK_EQ(r.x, SV_RC_X);
            CHECK_EQ(r.w, SV_RC_W);
            break;
        }
        case SERVO_FN_LIMRUN:
            /* Under eight rows of 24px from y 178: y 374, 600 x 58px. */
            CHECK_EQ(r.y, 374);
            CHECK_EQ(r.w, 600);
            CHECK_EQ(r.h, 58);
            CHECK(r.y + r.h <= UI_FOOTER_Y);
            break;
        case SERVO_FN_RAUTO:
            CHECK(r.x >= UI_MARGIN + 10 + 2 * 160 + 2 * 10);
            CHECK_EQ(r.w, 260);
            CHECK(r.y + r.h <= rauto_lines_rect().y);
            break;
        default:                /* TEST: under the caps button */
            CHECK_EQ(r.w, 600);
            break;
        }
        scr->render(&cv, 0);
        CHECK(count_color(r, UI_C_VOLT) > 0);
        CHECK(count_color(r, UI_C_CURR) > 0);
        CHECK(count_color(r, UI_C_POWER) > 0);
    }
    go(SERVO_FN_HUB);
    CHECK_EQ(svs_power_rect().w, 0);
}

/* The legend over the plot: `5.00V  0.390A  1.95W`, each in its trace's
 * colour, the meter's name dim before them where the width takes both
 * (TEST, LIMIT RUN, KST), the values alone on the 180px of LIVE. */
TEST_CASE(the_legend_reads_volts_amps_and_watts_in_their_colours)
{
    fresh_fed();
    go(SERVO_FN_TEST);
    scr->tick(0.025f);
    scr->render(&cv, 0);
    gfx_rect_t r = svs_power_rect();
    const gfx_rect_t band = { r.x, r.y, r.w, 20 };
    want_fresh();
    gfx_fill_rect(&want_cv, band.x, band.y, band.w, band.h,
                  ui_theme_color(UI_C_PANEL));
    /* Right-aligned values, 2 cells apart; the name at the left edge. */
    static const char *const k_v[3] = { "5.00V", "0.390A", "1.95W" };
    static const ui_color_id_t k_c[3] = { UI_C_VOLT, UI_C_CURR, UI_C_POWER };
    int w = 0;
    for (int k = 0; k < 3; ++k) {
        w += gfx_text_width(&gfx_font_10x20, k_v[k], 1) + (k > 0 ? 20 : 0);
    }
    gfx_text(&want_cv, band.x, band.y, "PD MINI", &gfx_font_10x20,
             ui_theme_color(UI_C_TEXT_DIM), 1);
    int x = band.x + band.w - w;
    for (int k = 0; k < 3; ++k) {
        x += gfx_text(&want_cv, x, band.y, k_v[k], &gfx_font_10x20,
                      ui_theme_color(k_c[k]), 1) + 20;
    }
    CHECK(same_in(fb, want_fb, band.x, band.y, band.w, band.h));
    /* LIVE's 180 x 48px: the 8 x 16 face, values from the left edge, no
     * name (the rail line under the card names the meter). */
    go(SERVO_FN_LIVE);
    scr->tick(0.025f);
    scr->render(&cv, 0);
    r = svs_power_rect();
    const gfx_rect_t small = { r.x, r.y, r.w, 16 };
    want_fresh();
    gfx_fill_rect(&want_cv, small.x, small.y, small.w, small.h,
                  ui_theme_color(UI_C_PANEL));
    x = small.x;
    for (int k = 0; k < 3; ++k) {
        x += gfx_text(&want_cv, x, small.y, k_v[k], &gfx_font_8x16,
                      ui_theme_color(k_c[k]), 1) + 16;
    }
    CHECK(same_in(fb, want_fb, small.x, small.y, small.w, small.h));
    CHECK_EQ(count_color(small, UI_C_TEXT_DIM), 0);
}

/* A sample of the rail repaints the plot and the rail line, each clipped,
 * and nothing else: on TEST, LIMIT RUN, RUDDER AUTO, KST and HITEC as on
 * LIVE (test_servo_drive_supply). */
TEST_CASE(a_new_reading_repaints_only_the_plot_and_the_line)
{
    fresh_fed();
    gfx_color_t *was = malloc((size_t)W * H * sizeof(gfx_color_t));
    REQUIRE_ALLOC(was);
    static const servo_fn_t k_some[] = { SERVO_FN_TEST, SERVO_FN_LIMRUN,
                                         SERVO_FN_RAUTO, SERVO_FN_KST,
                                         SERVO_FN_HITEC };
    for (size_t f = 0; f < sizeof(k_some) / sizeof(k_some[0]); ++f) {
        go(k_some[f]);
        rail_supply(5.0f, 0.3f);
        scr->tick(0.05f);
        scr->render(&cv, 0);
        scr->render(&cv, 0);
        memcpy(was, fb, (size_t)W * H * sizeof(gfx_color_t));
        rail_supply(5.0f, 0.9f + 0.1f * (float)f);
        scr->render(&cv, 0);
        const gfx_rect_t p = svs_power_rect();
        const gfx_rect_t l = ready_rail_rect();
        int in_plot = 0, in_line = 0, outside = 0;
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                if (fb[(size_t)y * W + x] == was[(size_t)y * W + x]) {
                    continue;
                }
                if (gfx_rect_contains(p, x, y)) {
                    ++in_plot;
                } else if (gfx_rect_contains(l, x, y)) {
                    ++in_line;
                } else {
                    ++outside;
                }
            }
        }
        CHECK(in_plot > 0);
        CHECK(in_line > 0);
        CHECK_EQ(outside, 0);
    }
    free(was);
}

/* On the rbmod carrier's programming port the PROG 5V feeds the servo:
 * without a window of its channel the rail line reads `PROG 5V NO
 * READING` and `--V  --A`, the legend `--V --A --W`, whatever the PD mini
 * reads; a window brings its values to both. */
TEST_CASE(on_the_carrier_port_no_window_reads_no_reading_not_the_pd_mini)
{
    fresh_fed();
    prog5v_box_publish(3u, 21u, (uint8_t)PROG5V_HELD_NONE);    /* ON */
    ui_board_set(OUTBIND_BOARD_RBMOD);
    servo_screen_set_drive(1u, 13u, false);
    rail_supply(7.4f, 0.5f);
    static const servo_fn_t k_some[] = { SERVO_FN_LIVE, SERVO_FN_LIMRUN,
                                         SERVO_FN_RAUTO, SERVO_FN_KST };
    for (size_t f = 0; f < sizeof(k_some) / sizeof(k_some[0]); ++f) {
        go(k_some[f]);
        scr->tick(0.025f);
        CHECK(rail_line_reads("PROG 5V", "NO READING", "--V", "--A"));
        scr->render(&cv, 0);
        const gfx_rect_t r = svs_power_rect();
        CHECK(count_color(r, UI_C_POWER) > 0);              /* --W */
    }
    ui_text_set_language(UI_LANG_DE);
    servo_invalidate();
    CHECK(rail_line_reads("PROG 5V", "KEIN MESSWERT", "--V", "--A"));
    ui_text_set_language(UI_LANG_EN);
    servo_invalidate();
    prog5v_box_reading(true, 5020u, 120, 1u);
    scr->tick(0.05f);
    CHECK(rail_line_reads("PROG 5V", NULL, "5.02V", "0.120A"));
    go(SERVO_FN_LIVE);
    scr->tick(0.05f);
    CHECK(rail_line_reads("PROG 5V", NULL, "5.02V", "0.120A"));
    ui_board_set(OUTBIND_BOARD_UNKNOWN);
    prog5v_box_reset();
}

int main(void)
{
    RUN(every_function_screen_shows_the_plot_in_its_place);
    RUN(the_legend_reads_volts_amps_and_watts_in_their_colours);
    RUN(a_new_reading_repaints_only_the_plot_and_the_line);
    RUN(on_the_carrier_port_no_window_reads_no_reading_not_the_pd_mini);
    return test_summary("servo_power_view");
}
