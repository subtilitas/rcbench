/*
 * What the SERVO screen's SETTINGS overlay decides: the profiles
 * and the warning that applies a heli type or a rate above 60Hz,
 * the note on the rate in force and the target, and the supply's
 * set points and output typed and switched from SERVO.
 *
 * The screen and its fixtures are servo_rig.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "servo_screen.h"
#include "settings.h"
#include "supply_screen.h"
#include "ui_keypad.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_textkey.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "ui_workbench.h"
#include "link_pages.h"
#include "outputs.h"
#include "outputs_pages.h"
#include "prog_link.h"

#include "servo_rig.h"

/* ------------------------------------------------ profiles and the warning */

TEST_CASE(a_heli_type_waits_for_the_warning_held_two_seconds)
{
    fresh_armed();
    open_settings();
    tap(ROW_L_X, ROW_Y(0));                    /* TYPE */
    tap(CH_X(3), CH_Y(3));                     /* HELI CYCLIC */
    CHECK_STR_EQ(servo_screen_type_name(), "STANDARD PWM");
    hold_apply(0.5f);                          /* not long enough */
    CHECK_STR_EQ(servo_screen_type_name(), "STANDARD PWM");
    hold_apply(2.3f);
    CHECK_STR_EQ(servo_screen_type_name(), "HELI CYCLIC");
    CHECK_EQ(servo_screen_frame_hz(), 333);

    /* The command carries the profile: its endpoints and its rate. */
    close_settings();
    int x, y;
    dial_at(0.0f, ARC_R - 20, &x, &y);
    tap(x, y);
    const servo_cmd_t c = last_cmd();
    CHECK_EQ(c.min_us, 820);
    CHECK_EQ(c.max_us, 2220);
    CHECK_EQ(c.frame_hz, 333);
    CHECK_EQ(c.value_us, 1520);
}

/*
 * Every profile's endpoints are a range the IOmcu takes: one it
 * refused would leave the panel driving a range the profile never named.
 * HELI TAIL 760's end of travel, 410us, is the narrowest of them.
 */
TEST_CASE(every_profile_is_a_range_the_coprocessor_takes)
{
    for (int t = 0; t < 5; ++t) {
        fresh_armed();
        choose_type(t);
        servo_screen_set_commanded(-90.0f);
        const servo_cmd_t c = last_cmd();
        CHECK(c.min_us >= LINK_CC_FLOOR_US);
        CHECK(c.max_us <= LINK_CC_CEILING_US);
        CHECK_EQ(c.value_us, c.min_us);
        uint16_t regs[LINK_CC_COUNT];
        outputs_chan_cfg_defaults(regs);
        const uint16_t in[LINK_CC_STRIDE] = {
            [LINK_CC_ROLE]   = LINK_CC_ROLE_SURFACE,
            [LINK_CC_SLEW]   = 0u,
            [LINK_CC_MIN_US] = c.min_us,
            [LINK_CC_MAX_US] = c.max_us,
        };
        CHECK_EQ(outputs_chan_cfg_write(regs, 0, LINK_CC_STRIDE, in), 0u);
    }
    CHECK_STR_EQ(servo_screen_type_name(), "HELI TAIL 760");
    servo_screen_set_commanded(-90.0f);
    CHECK_EQ(last_cmd().value_us, 410);
}

/* And a pulse width typed outside that range is refused at the keypad. */
TEST_CASE(a_pulse_width_the_coprocessor_would_refuse_is_not_taken)
{
    fresh_armed();
    open_settings();
    tap(ROW_L_X, ROW_Y(2));                    /* PULSE MIN */
    keys("350");
    key(UI_KEY_CANCEL);
    tap(ROW_L_X, ROW_Y(3));                    /* PULSE MAX */
    keys("2600");
    key(UI_KEY_CANCEL);
    /* A centre further from one end than the ceiling allows the other:
     * 1800us with MIN at 1000 would carry 1000..2600. */
    tap(ROW_R_X, ROW_Y(2));                    /* PULSE CENTRE */
    keys("1800");
    key(UI_KEY_CANCEL);
    close_settings();
    servo_screen_set_commanded(0.0f);
    const servo_cmd_t c = last_cmd();
    CHECK_EQ(c.value_us, 1500);
    CHECK_EQ(c.min_us, 1000);
    CHECK_EQ(c.max_us, 2000);
}

/*
 * The range a command carries reaches past PULSE MAX when CENTRE is off the
 * middle, and the far end can render its top between two transactions: the
 * frame rate's pause is kept from that top.  At HELI TAIL 760's 560Hz,
 * 1285us is the longest pulse with 0.5ms after it, so CENTRE may not move
 * the range past it; at 50Hz a centre of 1700us carries 1000..2400us and
 * leaves 294Hz as the fastest rate.
 */
TEST_CASE(the_pause_is_kept_from_the_top_of_the_range_a_command_carries)
{
    fresh_armed();
    choose_type(4);                            /* HELI TAIL 760, 560Hz */
    open_settings();
    tap(ROW_R_X, ROW_Y(2));                    /* PULSE CENTRE */
    keys("1060");                              /* would carry 410..1710 */
    key(UI_KEY_CANCEL);
    close_settings();
    servo_screen_set_commanded(90.0f);
    servo_cmd_t c = last_cmd();
    CHECK_EQ(c.max_us, 1110);                  /* refused: still 760 */
    open_settings();
    tap(ROW_R_X, ROW_Y(2));
    keys("840");                               /* 410..1270 fits */
    close_settings();
    servo_screen_set_commanded(90.0f);
    c = last_cmd();
    CHECK_EQ(c.min_us, 410);
    CHECK_EQ(c.max_us, 1270);
    CHECK_EQ(c.frame_hz, 560);

    fresh_armed();
    open_settings();
    tap(ROW_R_X, ROW_Y(2));                    /* PULSE CENTRE */
    keys("1700");                              /* 1000..2400 */
    tap(ROW_L_X, ROW_Y(1));                    /* FRAME RATE */
    tap(CH_X(6), CH_Y(6));                     /* CUSTOM, after 250Hz */
    keys("333");                               /* past 294Hz: refused */
    key(UI_KEY_CANCEL);
    CHECK_EQ(servo_screen_frame_hz(), 50);
    tap(ROW_L_X, ROW_Y(1));
    tap(CH_X(6), CH_Y(6));
    keys("290");                               /* fits; past 60Hz: warned */
    hold_apply(2.3f);
    close_settings();
    CHECK_EQ(servo_screen_frame_hz(), 290);
    servo_screen_set_commanded(0.0f);
    c = last_cmd();
    CHECK_EQ(c.max_us, 2400);
    CHECK_EQ(c.value_us, 1700);
}

/*
 * MIN, CENTRE and MAX are set one by one, so each side of centre runs to
 * its own end: -90deg is MIN and +90deg is MAX.  The range a command
 * carries is centred on CENTRE, because the far end rests a surface at its
 * midpoint -- on RELEASE and before an arm -- and that rest is the centre.
 */
TEST_CASE(each_side_of_centre_runs_to_its_own_end)
{
    fresh_armed();
    open_settings();
    tap(ROW_R_X, ROW_Y(2));                    /* PULSE CENTRE */
    keys("1520");
    tap(ROW_L_X, ROW_Y(2));                    /* PULSE MIN */
    keys("900");
    close_settings();
    servo_screen_set_commanded(-90.0f);
    CHECK_EQ(last_cmd().value_us, 900);
    servo_screen_set_commanded(-45.0f);
    CHECK_EQ(last_cmd().value_us, 1210);
    servo_screen_set_commanded(45.0f);
    CHECK_EQ(last_cmd().value_us, 1760);
    servo_screen_set_commanded(90.0f);
    const servo_cmd_t c = last_cmd();
    CHECK_EQ(c.value_us, 2000);
    CHECK_EQ(c.min_us, 900);
    CHECK_EQ(c.max_us, 2140);                  /* 1520 +/- 620 */
    tap(SV_RELEASE_X, SV_BTN_Y);               /* RELEASE */
    const servo_cmd_t r = last_cmd();
    CHECK_EQ(r.kind, SERVO_CMD_RELEASE);
    CHECK_EQ(r.min_us, 900);
    CHECK_EQ(r.max_us, 2140);
}

TEST_CASE(a_warning_cancelled_dropped_or_left_applies_nothing)
{
    fresh();
    open_settings();
    tap(ROW_L_X, ROW_Y(0));
    tap(CH_X(4), CH_Y(4));                     /* HELI TAIL 760 */
    tap(WARN_CANCEL_X, WARN_Y);
    for (int i = 0; i < 120; ++i) {
        scr->tick(1.0f / 39.0f);
    }
    CHECK_STR_EQ(servo_screen_type_name(), "STANDARD PWM");

    /* A hold the touch stream lost does not complete on its own. */
    tap(ROW_L_X, ROW_Y(0));
    tap(CH_X(4), CH_Y(4));
    ev(WARN_APPLY_X, WARN_Y, TOUCH_EVENT_DOWN, 1);
    for (int i = 0; i < 40; ++i) {
        scr->tick(1.0f / 39.0f);
    }
    scr->cancel();
    for (int i = 0; i < 120; ++i) {
        scr->tick(1.0f / 39.0f);
    }
    CHECK_STR_EQ(servo_screen_type_name(), "STANDARD PWM");

    /* A finger that slides off APPLY abandons it. */
    ev(WARN_APPLY_X, WARN_Y, TOUCH_EVENT_DOWN, 1);
    ev(WARN_CANCEL_X, WARN_Y, TOUCH_EVENT_MOVE, 1);
    for (int i = 0; i < 120; ++i) {
        scr->tick(1.0f / 39.0f);
    }
    ev(WARN_CANCEL_X, WARN_Y, TOUCH_EVENT_UP, 1);
    CHECK_STR_EQ(servo_screen_type_name(), "STANDARD PWM");

    /* Leaving the screen with the warning open drops it. */
    scr->leave();
    for (int i = 0; i < 120; ++i) {
        scr->tick(1.0f / 39.0f);
    }
    CHECK_STR_EQ(servo_screen_type_name(), "STANDARD PWM");
}

TEST_CASE(a_rate_above_60_hz_needs_the_warning_and_60_does_not)
{
    fresh();
    open_settings();
    tap(ROW_L_X, ROW_Y(1));                    /* FRAME RATE */
    tap(CH_X(1), CH_Y(1));                     /* 60Hz */
    CHECK_EQ(servo_screen_frame_hz(), 60);
    tap(ROW_L_X, ROW_Y(1));
    tap(CH_X(2), CH_Y(2));                     /* 100Hz: asked first */
    CHECK_EQ(servo_screen_frame_hz(), 60);
    hold_apply(2.3f);
    CHECK_EQ(servo_screen_frame_hz(), 100);
}

TEST_CASE(standard_pwm_keeps_a_millisecond_between_pulses)
{
    /* 1 / (2000us + 1ms) is 333Hz: the list stops there, a custom rate
     * above it is refused, and at 333Hz the longest pulse is 2003us. */
    fresh_armed();
    open_settings();
    tap(ROW_L_X, ROW_Y(1));
    tap(CH_X(7), CH_Y(7));                     /* 333Hz, the last */
    hold_apply(2.3f);
    CHECK_EQ(servo_screen_frame_hz(), 333);
    tap(ROW_L_X, ROW_Y(3));                    /* PULSE MAX */
    keys("2100");                              /* refused: no pause left */
    key(UI_KEY_CANCEL);
    tap(ROW_L_X, ROW_Y(3));
    keys("2003");
    int x, y;
    close_settings();
    dial_at(90.0f, ARC_R - 20, &x, &y);
    tap(x, y);
    CHECK_EQ(last_cmd().max_us, 2003);

    /* WIDE's 2200us leaves 312Hz: CUSTOM refuses 320, takes 300. */
    fresh();
    choose_type(2);
    open_settings();
    tap(ROW_L_X, ROW_Y(1));
    tap(CH_X(7), CH_Y(7));                     /* CUSTOM: 7 rates fit 312Hz */
    keys("320");
    key(UI_KEY_CANCEL);
    CHECK_EQ(servo_screen_frame_hz(), 50);
    tap(ROW_L_X, ROW_Y(1));
    tap(CH_X(7), CH_Y(7));                     /* CUSTOM */
    keys("55");                                /* 60Hz or under: no warning */
    CHECK_EQ(servo_screen_frame_hz(), 55);
}

TEST_CASE(a_restart_is_standard_pwm_at_50_hz)
{
    fresh();
    choose_type(4);
    CHECK_STR_EQ(servo_screen_type_name(), "HELI TAIL 760");
    CHECK_EQ(servo_screen_frame_hz(), 560);
    scr->reset();                              /* what a restart does */
    CHECK_STR_EQ(servo_screen_type_name(), "STANDARD PWM");
    CHECK_EQ(servo_screen_frame_hz(), 50);
}

TEST_CASE(reverse_and_the_pulse_widths_reshape_the_command)
{
    fresh_armed();
    int x, y;
    dial_at(45.0f, ARC_R - 20, &x, &y);
    tap(x, y);
    const uint16_t ahead = last_cmd().value_us;
    CHECK(ahead > 1500);
    open_settings();
    tap(ROW_R_X, ROW_Y(4));                    /* REVERSE */
    CHECK(last_cmd().value_us < 1500);         /* said again, mirrored */

    fresh_armed();
    open_settings();
    tap(ROW_R_X, ROW_Y(2));                    /* PULSE CENTRE */
    keys("1520");
    tap(ROW_L_X, ROW_Y(2));                    /* PULSE MIN */
    keys("900");
    close_settings();
    dial_at(0.0f, ARC_R - 20, &x, &y);
    tap(x, y);
    const servo_cmd_t c = last_cmd();
    CHECK_EQ(c.value_us, 1520);
    CHECK_EQ(c.min_us, 900);
}

/* SWEEP's rows and a run's own rows are kept and saved; SUPPLY's caps are
 * not edited here (their home is SUPPLY > SETTINGS), and the run's
 * settings close to TEST with the home tag. */
TEST_CASE(the_sweep_and_run_settings_are_kept)
{
    fresh();
    open_settings();
    tap(TAB_X(PAGE_SWEEP), TAB_Y);             /* SWEEP */
    tap(ROW_L_X, ROW_Y(0));                    /* CURVE: a list */
    tap(CH_X(2), CH_Y(2));                     /* TRIANGLE */
    CHECK_EQ(settings_get_int(SET_SERVO_CURVE), 2);
    tap(ROW_L_X, ROW_Y(1));                    /* SPEED: the keypad */
    keys("1.25");
    CHECK_NEAR(settings_get(SET_SERVO_TEST_HZ), 1.25f, 1e-4f);
    CHECK(settings_save_asked());

    open_run_settings(0);                      /* AUTOMATIC TEST's */
    CHECK_EQ(servo_screen_run_settings(), 1);
    tap(LROW_X(0), LROW_Y(0));                 /* STEP 4.8V: a switch */
    CHECK(!settings_get_bool(SET_SERVO_STEP_48));
    tap(LROW_X(1), LROW_Y(1));                 /* IDLE CURRENT */
    keys("0.3");
    CHECK_NEAR(settings_get(SET_SERVO_IDLE_MAX), 0.3f, 1e-4f);
    tap(LROW_X(1), LROW_Y(0));                 /* STALL AT */
    keys("1.5");
    CHECK_NEAR(settings_get(SET_SERVO_STALL_A), 1.5f, 1e-4f);
    CHECK_NEAR(settings_get(SET_SUPPLY_V_MAX), 21.0f, 1e-4f);
    CHECK(home_tag());
    CHECK_EQ(servo_screen_run_settings(), 0);
    CHECK_EQ(servo_screen_task(), (int)UI_WB_TEST);
    CHECK(!home_tag());                        /* TEST climbs no further */

    open_settings();
    tap(TAB_X(PAGE_DUT), TAB_Y);               /* DUT */
    tap(ROW_L_X, ROW_Y(0));                    /* NAME: the keyboard */
    ui_textkey_t probe;
    memset(&probe, 0, sizeof(probe));
    ui_textkey_open(&probe, SV_OV_AREA, "", "", 23);
    const int seq[] = { UI_TK_CLR, 31 /* X */, 15 /* Y */, 0 /* 1 */,
                        UI_TK_OK };
    for (size_t i = 0; i < sizeof(seq) / sizeof(seq[0]); ++i) {
        const gfx_rect_t r = ui_textkey_key_rect(&probe, seq[i]);
        tap(r.x + r.w / 2, r.y + r.h / 2);
    }
    CHECK_STR_EQ(settings_text(SET_TEXT_DUT_NAME), "XY1");
    tap(ROW_L_X, ROW_Y(1));                    /* REPORT */
    CHECK(!settings_get_bool(SET_SERVO_REPORT));
}

TEST_CASE(the_overlay_leaves_the_right_card_working)
{
    /* ARM, CENTRE and RELEASE work under the overlay; the dial under it
     * does not.  CENTRE is a position, taken on an armed bench. */
    fresh();
    open_settings();
    arm_press();
    hold_for(2.2f);
    arm_release();
    CHECK_EQ(last_cmd().kind, SERVO_CMD_ARM);
    servo_screen_set_armed(true);
    tap(SV_CENTRE_X, SV_BTN_Y);                /* CENTRE */
    CHECK_EQ(last_cmd().kind, SERVO_CMD_CENTRE);
    int x, y;
    dial_at(45.0f, ARC_R - 20, &x, &y);
    tap(x, y);                                 /* on the overlay, not the dial */
    CHECK(servo_screen_commanded() == 1500);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
}

TEST_CASE(the_tag_is_red_while_a_dangerous_profile_is_in_force)
{
    fresh();
    scr->render(&cv, 0);
    /* The tag, inside its pill and left of its text. */
    const gfx_color_t calm = fb[40 * W + 660];
    CHECK(calm != ui_theme_color(UI_C_DANGER));
    choose_type(3);
    scr->render(&cv, 0);
    CHECK_EQ(fb[40 * W + 660], ui_theme_color(UI_C_DANGER));
}

TEST_CASE(drags_and_samples_leave_both_buffers_as_a_full_redraw_would)
{
    /* The horn's drag repaints the right card without its plot, and the
     * plot repaints itself on its own samples; through any mix of the two,
     * each buffer ends as one drawn whole. */
    fresh();
    gfx_color_t *fb_one = calloc((size_t)W * H, sizeof(gfx_color_t));
    REQUIRE_ALLOC(fb_one);
    gfx_color_t *ref_px = calloc((size_t)W * H, sizeof(gfx_color_t));
    gfx_canvas_t cv_one, cv_ref;
    gfx_canvas_init(&cv_one, fb_one, W, H, W);
    gfx_canvas_init(&cv_ref, ref_px, W, H, W);
    supply_state_t st;
    memset(&st, 0, sizeof(st));
    st.online = true;
    st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
    st.v = 6.0f;
    for (int step = 0; step < 24; ++step) {
        st.i = 0.1f * (float)(step % 7);
        st.p = st.v * st.i;
        if (step % 3 != 1) {
            servo_screen_supply(&st);
        }
        if (step % 2 == 0) {
            int x, y;
            dial_at((float)(step * 5 - 60), ARC_R - 20, &x, &y);
            tap(x, y);
        }
        scr->render((step % 2) ? &cv_one : &cv, step % 2);
    }
    scr->render(&cv, 0);
    scr->render(&cv_one, 1);
    CHECK_EQ(memcmp(fb, fb_one, (size_t)W * H * sizeof(gfx_color_t)), 0);
    servo_invalidate();
    scr->render(&cv_ref, 0);
    CHECK_EQ(memcmp(fb, ref_px, (size_t)W * H * sizeof(gfx_color_t)), 0);
    free(fb_one);
    free(ref_px);
}

/* The supply is the servo rail's meter here: a sample whose current differs
 * in the digits shown repaints the line and the plot, and LIVE's CURRENT
 * reading at the foot of the left card, and nothing else. */
TEST_CASE(a_supply_sample_repaints_only_the_power_plot_and_the_current_row)
{
    fresh();
    scr->render(&cv, 0);
    gfx_color_t *was = malloc((size_t)W * H * sizeof(gfx_color_t));
    REQUIRE_ALLOC(was);
    supply_state_t st;
    memset(&st, 0, sizeof(st));
    st.online = true;
    st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
    for (int i = 0; i < 30; ++i) {
        st.v = 6.0f; st.i = 0.2f + 0.02f * (float)i; st.p = st.v * st.i;
        servo_screen_supply(&st);
    }
    scr->render(&cv, 0);
    memcpy(was, fb, (size_t)W * H * sizeof(gfx_color_t));
    st.i = 1.5f; st.p = 9.0f;
    servo_screen_supply(&st);
    scr->render(&cv, 0);
    int outside = 0, inside = 0, in_row = 0;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            if (fb[y * W + x] == was[y * W + x]) {
                continue;
            }
            const bool rc = x >= SV_RC_X && x < SV_RC_X + SV_RC_W;
            const bool in = rc && y >= 70 && y < 166;
            const bool row = x >= 416 && x < 610 && y >= 476 && y < 520;
            if (in) { ++inside; } else if (row) { ++in_row; } else { ++outside; }
        }
    }
    free(was);
    CHECK(inside > 0);
    CHECK(in_row > 0);
    CHECK_EQ(outside, 0);
    servo_screen_supply(NULL);
}

/* A key of the text keyboard, where the screen opens it over the left
 * card. */
static void text_key(int k)
{
    ui_textkey_t probe;
    memset(&probe, 0, sizeof(probe));
    ui_textkey_open(&probe, SV_OV_AREA, "", "", 23);
    const gfx_rect_t r = ui_textkey_key_rect(&probe, k);
    tap(r.x + r.w / 2, r.y + r.h / 2);
}

/*
 * Each face of the workbench -- TEST's cards and each run's settings,
 * SETTINGS' three pages, a list, the keypad, the keyboard and the warning
 * with a hold let go part way -- leaves both buffers as one drawn whole,
 * and a list or a warning cancelled leaves the page as it was.  A panel
 * covers the strip's tabs with the left column, and the page and the strip
 * come back whole when it goes.
 */
TEST_CASE(each_face_of_the_overlay_draws_as_a_full_redraw_would)
{
    fresh();
    two_buffers();
    open_test();
    CHECK(both_whole());
    open_run_settings(0);
    CHECK(both_whole());
    CHECK(home_tag());
    CHECK(both_whole());
    open_run_settings(1);
    CHECK(both_whole());
    open_settings();
    CHECK(both_whole());
    gfx_color_t *page = malloc(FB_BYTES);
    REQUIRE_ALLOC(page);
    memcpy(page, fb, FB_BYTES);

    for (int tab = 1; tab < 3; ++tab) {
        tap(TAB_X(tab), TAB_Y);
        CHECK(both_whole());
        CHECK(memcmp(fb, page, FB_BYTES) != 0);
    }
    tap(ROW_L_X, ROW_Y(0));                    /* DUT NAME: the keyboard */
    CHECK(both_whole());
    text_key(UI_TK_CANCEL);
    CHECK_STR_EQ(settings_text(SET_TEXT_DUT_NAME), "SERVO");

    tap(TAB_X(0), TAB_Y);
    tap(ROW_L_X, ROW_Y(4));                    /* TRAVEL: the keypad */
    CHECK(both_whole());
    key(UI_KEY_CANCEL);
    CHECK(both_whole());
    CHECK_EQ(memcmp(fb, page, FB_BYTES), 0);

    tap(ROW_L_X, ROW_Y(0));                    /* TYPE: the list */
    CHECK(both_whole());
    tap(CH_CANCEL_X, CH_CANCEL_Y);
    CHECK_STR_EQ(servo_screen_type_name(), "STANDARD PWM");
    CHECK(both_whole());
    CHECK_EQ(memcmp(fb, page, FB_BYTES), 0);

    tap(ROW_L_X, ROW_Y(0));
    tap(CH_X(3), CH_Y(3));                     /* HELI CYCLIC: the warning */
    CHECK(both_whole());
    CHECK_EQ(fb[UI_MARGIN * W + UI_MARGIN], ui_theme_color(UI_C_DANGER));
    ev(WARN_APPLY_X, WARN_Y, TOUCH_EVENT_DOWN, 1);
    for (int i = 0; i < 39; ++i) {             /* a second of the two */
        scr->tick(1.0f / 39.0f);
        scr->render((i % 2) ? &cv1 : &cv, i % 2);
    }
    CHECK(both_whole());
    ev(WARN_APPLY_X, WARN_Y, TOUCH_EVENT_UP, 1);
    CHECK_STR_EQ(servo_screen_type_name(), "STANDARD PWM");
    CHECK(both_whole());
    tap(WARN_CANCEL_X, WARN_Y);
    CHECK(both_whole());
    CHECK_EQ(memcmp(fb, page, FB_BYTES), 0);
    close_settings();
    CHECK(both_whole());
    free(page);
}

/*
 * The OUTPUT page says whether the frame rate reached the pins, and in the
 * warning colour when it did not: refused by the binding, or an IOmcu
 * that takes none.  News about another rate is not news about this one.
 */
TEST_CASE(the_output_page_says_whether_the_rate_reached_the_pins)
{
    fresh();
    two_buffers();
    open_settings();
    CHECK(both_whole());
    gfx_color_t *unsent = malloc(FB_BYTES);
    REQUIRE_ALLOC(unsent);
    memcpy(unsent, fb, FB_BYTES);
    servo_screen_rate(SERVO_RATE_IN_FORCE, 50u);
    CHECK(both_whole());
    CHECK(memcmp(fb, unsent, FB_BYTES) != 0);
    servo_screen_rate(SERVO_RATE_IN_FORCE, 333u);   /* not the rate shown */
    CHECK(both_whole());
    CHECK_EQ(memcmp(fb, unsent, FB_BYTES), 0);

    const servo_rate_state_t bad[] = { SERVO_RATE_REFUSED,
                                       SERVO_RATE_UNSUPPORTED };
    for (int k = 0; k < 2; ++k) {
        servo_screen_rate(bad[k], 50u);
        CHECK(both_whole());
        int warn = 0;
        for (int y = 390; y < 510; ++y) {
            for (int x = 8; x < 628; ++x) {
                warn += fb[y * W + x] == ui_theme_color(UI_C_WARN);
            }
        }
        CHECK(warn > 0);
    }
    free(unsent);
}

/*
 * What an acknowledged rate reaches depends on the IOmcu.  From 4.19
 * the TARGET alone runs at it; a 4.1 to 4.18 IOmcu runs every PWM
 * surface at it, whatever the target, and the note says that instead in
 * its first line.  Both are news, not a warning.
 */
/* The rate note on the OUTPUT page, mirrored from servo_settings_draw.c:
 * its first line at OV_NOTE_Y + 2 * 21 (408), its second 21px below,
 * each 20px high, from 10px into the page to 600px further. */
#define NOTE_X    (UI_MARGIN + 10)
#define NOTE_L1_Y 429
#define NOTE_L2_Y (NOTE_L1_Y + 21)
#define NOTE_R    (UI_MARGIN + 610)

/* Whether one line's 20 rows are the same in @p was as in the screen. */
static bool note_rows_equal(const gfx_color_t *was, int y0)
{
    for (int y = y0; y < y0 + 20; ++y) {
        if (memcmp(fb + y * W, was + y * W, W * sizeof(gfx_color_t)) != 0) {
            return false;
        }
    }
    return true;
}

/* Whether the note's two lines on the screen are the pixels of @p l1 and
 * @p l2 drawn as the screen draws them: the 10x20 face, faint, or in the
 * warning colour with @p warn. */
static bool note_matches(const char *l1, const char *l2, bool warn)
{
    static gfx_color_t want[(size_t)W * H];
    gfx_canvas_t c;
    memcpy(want, fb, sizeof(want));
    gfx_canvas_init(&c, want, W, H, W);
    const gfx_color_t bg = fb[NOTE_L1_Y * W + NOTE_X - 4];
    gfx_fill_rect(&c, NOTE_X, NOTE_L1_Y, NOTE_R - NOTE_X, 42, bg);
    const gfx_color_t col = warn ? ui_theme_color(UI_C_WARN)
                                 : ui_theme_color(UI_C_TEXT_FAINT);
    gfx_text(&c, NOTE_X, NOTE_L1_Y, l1, &gfx_font_10x20, col, 1);
    gfx_text(&c, NOTE_X, NOTE_L2_Y, l2, &gfx_font_10x20, col, 1);
    return memcmp(want, fb, sizeof(want)) == 0;
}

TEST_CASE(the_in_force_note_names_what_the_coprocessor_runs_at_the_rate)
{
    CHECK_EQ(servo_rate_in_force(LINK_MINOR_SERVO_TARGET_RATE),
             SERVO_RATE_IN_FORCE);
    CHECK_EQ(servo_rate_in_force(18u), SERVO_RATE_IN_FORCE_SURFACES);
    CHECK_EQ(servo_rate_in_force(1u), SERVO_RATE_IN_FORCE_SURFACES);
    CHECK_STR_EQ(ui_tr(TX_SV_IN_FORCE_SURFACES_1),
                 "Active: all PWM control surfaces run at %uHz.");

    fresh();
    two_buffers();
    open_settings();
    servo_screen_rate(servo_rate_in_force(LINK_MINOR_SERVO_TARGET_RATE), 50u);
    CHECK(both_whole());
    gfx_color_t *target = malloc(FB_BYTES);
    REQUIRE_ALLOC(target);
    memcpy(target, fb, FB_BYTES);
    servo_screen_rate(servo_rate_in_force(18u), 50u);
    CHECK(both_whole());
    CHECK(memcmp(fb, target, FB_BYTES) != 0);
    int warn = 0;
    for (int y = 390; y < 510; ++y) {
        for (int x = 8; x < 628; ++x) {
            warn += fb[y * W + x] == ui_theme_color(UI_C_WARN);
        }
    }
    CHECK_EQ(warn, 0);
    /* Only the first line differs, inside its own rows: the second is the
     * same "in force" line. */
    int first = -1, last = -1;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            if (fb[y * W + x] != target[y * W + x]) {
                if (first < 0) {
                    first = y;
                }
                last = y;
                break;
            }
        }
    }
    CHECK(first >= NOTE_L1_Y);
    CHECK(last < NOTE_L1_Y + 20);
    CHECK(note_rows_equal(target, NOTE_L2_Y));

    /* The rate is in the "every PWM surface" line: 50Hz and 60Hz read
     * differently there, and the second line stays. */
    memcpy(target, fb, FB_BYTES);
    tap(ROW_L_X, ROW_Y(1));                    /* FRAME RATE */
    tap(CH_X(1), CH_Y(1));                     /* 60Hz */
    CHECK_EQ(servo_screen_frame_hz(), 60);
    servo_screen_rate(servo_rate_in_force(18u), 60u);
    CHECK(both_whole());
    CHECK(!note_rows_equal(target, NOTE_L1_Y));
    CHECK(note_rows_equal(target, NOTE_L2_Y));
    CHECK(note_matches("Active: all PWM control surfaces run at 60Hz.",
                       ui_tr(TX_SV_IN_FORCE_2), false));
    free(target);
}

/* The states the note is held to: the TARGET setting, the drive the
 * IOmcu holds, the English lines it must give and the warning colour. */
typedef struct {
    int         setting;    /* SET_SERVO_TARGET */
    uint8_t     target;     /* the drive: 0, 1 + n, _UNKNOWN or _NO_LINK */
    uint8_t     gpio;
    bool        pwm;
    const char *l1;
    const char *l2;
    bool        warn;
} note_state_t;

#define PPM_LINE "PPM outputs keep their own frame rate."
#define NOT_SET  "The TARGET chosen is not active."
static const note_state_t k_note[] = {
    { 0, 0u, SERVO_SCREEN_NO_PIN, false, "Active: OUTPUTS run at 50Hz.",
      PPM_LINE, false },
    { 1, 1u, 13u, true, "Active: PROG PORT GPIO 13 runs at 50Hz.", PPM_LINE,
      false },
    /* PROG PORT set and the port unbound: the outputs stay the target. */
    { 1, 0u, SERVO_SCREEN_NO_PIN, false, "Active: OUTPUTS run at 50Hz.",
      NOT_SET, true },
    /* The port held while OUTPUTS is set: the change waits for rest. */
    { 0, 1u, 13u, true, "Active: PROG PORT GPIO 13 runs at 50Hz.", NOT_SET,
      true },
    { 1, 1u, SERVO_SCREEN_NO_PIN, true,
      "Active: 50Hz on PROG PORT. No pin is assigned to it.", PPM_LINE, true },
    { 1, 1u, 63u, false, "Active: PROG PORT GPIO 63 runs at 50Hz.",
      "PORT PWM is OFF. The port sends no pulses.", true },
    /* The port with no pin and its pulse off: the first line still says
     * there is no pin, the second that no pulse goes out. */
    { 1, 1u, SERVO_SCREEN_NO_PIN, false,
      "Active: 50Hz on PROG PORT. No pin is assigned to it.",
      "PORT PWM is OFF. The port sends no pulses.", true },
    { 1, SERVO_SCREEN_TARGET_UNKNOWN, 13u, true,
      "Active rate: 50Hz. Target unknown.", PPM_LINE, true },
    /* No link: the IOmcu may have restarted, so no rate is named. */
    { 0, SERVO_SCREEN_TARGET_NO_LINK, 13u, true,
      "No link. The active frame rate is unknown.", PPM_LINE, true },
};
#define NOTE_N ((int)(sizeof(k_note) / sizeof(k_note[0])))

/* The note on the OUTPUT page follows the target the IOmcu holds, not
 * the TARGET setting: the outputs, the port with its pin, the port
 * without one, and a target not known.  The warning colour marks a setting
 * the drive does not follow, a port without a pin or with its pulse off,
 * and a target not known. */
TEST_CASE(the_in_force_note_names_the_target_the_coprocessor_holds)
{
    for (int i = 0; i < NOTE_N; ++i) {
        const note_state_t *st = &k_note[i];
        fresh();
        two_buffers();
        settings_set(SET_SERVO_TARGET, (float)st->setting);
        servo_screen_set_drive(st->target, st->gpio, st->pwm);
        open_settings();
        servo_screen_rate(SERVO_RATE_IN_FORCE, 50u);
        CHECK(both_whole());
        CHECK(note_matches(st->l1, st->l2, st->warn));

        char l1[96], l2[96];
        CHECK_EQ(servo_in_force_note(l1, l2, sizeof(l1), 50u, st->target,
                                     st->gpio, st->pwm, st->setting),
                 st->warn);
        CHECK_STR_EQ(l1, st->l1);
        CHECK_STR_EQ(l2, st->l2);
    }

    /* A drive change with the page open redraws the note, without a tap:
     * the port's pin, then the outputs once more. */
    servo_screen_set_drive(1u, 13u, true);
    CHECK(both_whole());
    CHECK(note_matches("Active: PROG PORT GPIO 13 runs at 50Hz.", NOT_SET,
                       true));
    servo_screen_set_drive(1u, 13u, false);
    CHECK(both_whole());
    CHECK(note_matches("Active: PROG PORT GPIO 13 runs at 50Hz.", NOT_SET,
                       true));
    servo_screen_set_drive(0u, SERVO_SCREEN_NO_PIN, false);
    CHECK(both_whole());
    CHECK(note_matches("Active: OUTPUTS run at 50Hz.", PPM_LINE, false));

    /* German, the same states: protocol and setting names as the UI writes
     * them. */
    static const char *const de[NOTE_N][2] = {
        { "Aktiv: AUSGÄNGE laufen mit 50Hz.",
          "PPM-Ausgänge behalten ihre eigene Pulsrate." },
        { "Aktiv: PROG-PORT GPIO 13 läuft mit 50Hz.",
          "PPM-Ausgänge behalten ihre eigene Pulsrate." },
        { "Aktiv: AUSGÄNGE laufen mit 50Hz.",
          "Das gewählte ZIEL ist nicht aktiv." },
        { "Aktiv: PROG-PORT GPIO 13 läuft mit 50Hz.",
          "Das gewählte ZIEL ist nicht aktiv." },
        { "Aktiv: 50Hz am PROG-PORT. Ihm ist kein Pin zugeordnet.",
          "PPM-Ausgänge behalten ihre eigene Pulsrate." },
        { "Aktiv: PROG-PORT GPIO 63 läuft mit 50Hz.",
          "PORT-PWM ist AUS. Der Port gibt keine Pulse aus." },
        { "Aktiv: 50Hz am PROG-PORT. Ihm ist kein Pin zugeordnet.",
          "PORT-PWM ist AUS. Der Port gibt keine Pulse aus." },
        { "Aktive Pulsrate: 50Hz. Ziel unbekannt.",
          "PPM-Ausgänge behalten ihre eigene Pulsrate." },
        { "Keine Verbindung. Die aktive Pulsrate ist unbekannt.",
          "PPM-Ausgänge behalten ihre eigene Pulsrate." },
    };
    ui_text_set_language(UI_LANG_DE);
    for (int i = 0; i < NOTE_N; ++i) {
        const note_state_t *st = &k_note[i];
        char l1[96], l2[96];
        CHECK_EQ(servo_in_force_note(l1, l2, sizeof(l1), 50u, st->target,
                                     st->gpio, st->pwm, st->setting),
                 st->warn);
        CHECK_STR_EQ(l1, de[i][0]);
        CHECK_STR_EQ(l2, de[i][1]);
    }
    ui_text_set_language(UI_LANG_EN);

    /* The rate is the one passed; a NULL buffer or no room writes nothing. */
    char a[96], b[96];
    CHECK(!servo_in_force_note(a, b, sizeof(a), 560u, 1u, 13u, true, 1));
    CHECK_STR_EQ(a, "Active: PROG PORT GPIO 13 runs at 560Hz.");
    CHECK(!servo_in_force_note(a, b, sizeof(a), 560u, 0u, 13u, true, 0));
    CHECK_STR_EQ(a, "Active: OUTPUTS run at 560Hz.");
    memset(a, 'x', sizeof(a));
    memset(b, 'x', sizeof(b));
    CHECK(!servo_in_force_note(NULL, b, sizeof(b), 50u, 0u, 0xFFu, false, 1));
    CHECK(!servo_in_force_note(a, NULL, sizeof(a), 50u, 0u, 0xFFu, false, 1));
    CHECK(!servo_in_force_note(a, b, 0u, 50u, 0u, 0xFFu, false, 1));
    CHECK_EQ(a[0], 'x');
    CHECK_EQ(b[0], 'x');
    /* A setting out of range is not the drive's target. */
    CHECK(servo_in_force_note(a, b, sizeof(a), 50u, 0u, 0xFFu, false, -1));
    CHECK_STR_EQ(b, NOT_SET);
}

/* The note carries the rate the screen runs, not 50Hz: HELI TAIL 760's
 * 560Hz on the port's pin as drawn, and in the lines for a target not
 * known and for a port with no pin.  No link names no rate at all, also
 * for an IOmcu before 4.19 whose note says "every PWM surface". */
TEST_CASE(the_in_force_note_carries_the_rate_and_names_none_without_a_link)
{
    fresh();
    two_buffers();
    choose_type(4);                            /* HELI TAIL 760, 560Hz */
    CHECK_EQ(servo_screen_frame_hz(), 560);
    settings_set(SET_SERVO_TARGET, 1.0f);
    servo_screen_set_drive(1u, 13u, true);
    open_settings();
    servo_screen_rate(SERVO_RATE_IN_FORCE, 560u);
    CHECK(both_whole());
    CHECK(note_matches("Active: PROG PORT GPIO 13 runs at 560Hz.", PPM_LINE,
                       false));

    char a[96], b[96];
    CHECK(servo_in_force_note(a, b, sizeof(a), 560u,
                              SERVO_SCREEN_TARGET_UNKNOWN, 13u, true, 1));
    CHECK_STR_EQ(a, "Active rate: 560Hz. Target unknown.");
    CHECK(servo_in_force_note(a, b, sizeof(a), 560u, 1u, SERVO_SCREEN_NO_PIN,
                              true, 1));
    CHECK_STR_EQ(a, "Active: 560Hz on PROG PORT. No pin is assigned to it.");

    /* The link goes: the drawn note names no rate, in the warning colour. */
    servo_screen_set_drive(SERVO_SCREEN_TARGET_NO_LINK, SERVO_SCREEN_NO_PIN,
                           false);
    CHECK(both_whole());
    CHECK(note_matches("No link. The active frame rate is unknown.", PPM_LINE,
                       true));
    /* A 4.1 to 4.18 IOmcu's "every PWM surface" line too. */
    servo_screen_rate(servo_rate_in_force(18u), 560u);
    CHECK(both_whole());
    CHECK(note_matches("No link. The active frame rate is unknown.", PPM_LINE,
                       true));
    /* And back with the link: the surfaces at the rate again. */
    servo_screen_set_drive(SERVO_SCREEN_TARGET_UNKNOWN, SERVO_SCREEN_NO_PIN,
                           false);
    CHECK(both_whole());
    CHECK(note_matches("Active: all PWM control surfaces run at 560Hz.",
                       PPM_LINE, false));
}

/* The note and the drive come from the same record: PROG PORT set with no
 * pin bound to port 0 leaves the IOmcu on the standard outputs, which the
 * tester then does not drive (prog_link_no_drive()): the drive names the
 * port without a pin and the note says so in the warning colour; with
 * GP13 bound the TARGET write lands and the note names the port's pin. */
TEST_CASE(the_in_force_note_follows_the_prog_page_not_the_setting)
{
    static prog_link_t p;
    uint16_t page[LINK_PG_REGS];
    for (int bound = 0; bound < 2; ++bound) {
        memset(page, 0, sizeof(page));
        page[LINK_PG_COUNT] = 1u;
        page[LINK_PG_FLAGS] = bound ? LINK_PG_F_BOUND(0) : 0u;
        page[LINK_PG_REG(0, LINK_PG_PIN)] = bound ? 13u : LINK_PG_PIN_NONE;
        page[LINK_PG_PWM] = 1u;                 /* port 0 renders its pulse */
        for (unsigned n = 1; n < LINK_PG_PORTS_MAX; ++n) {
            page[LINK_PG_REG(n, LINK_PG_PIN)] = LINK_PG_PIN_NONE;
        }
        prog_link_init(&p);
        prog_link_came_up(&p, LINK_MINOR_SERVO_TARGET_RATE);
        prog_link_want(&p, 1u, true);
        prog_link_read(&p, page, 0u);
        prog_link_write_t w;
        int writes = 0;
        while (writes < 10 && prog_link_next(&p, true, &w)) {
            page[w.off] = w.value;
            prog_link_written(&p, &w, true);
            prog_link_read(&p, page, 1u);
            ++writes;
        }
        const prog_link_drive_t d = prog_link_drive(&p);
        CHECK_EQ(d.target, 1u);
        CHECK_EQ(d.pin, bound ? 13u : PROG_LINK_NO_PIN);
        CHECK_EQ(prog_link_no_drive(&p), !bound);
        CHECK_EQ(prog_link_command(&p, false), (bool)bound);
        CHECK(prog_link_command(&p, true));

        fresh();
        two_buffers();
        settings_set(SET_SERVO_TARGET, 1.0f);
        servo_screen_set_drive(d.target, d.pin, d.pwm);
        open_settings();
        servo_screen_rate(SERVO_RATE_IN_FORCE, 50u);
        CHECK(both_whole());
        if (bound) {
            CHECK(note_matches("Active: PROG PORT GPIO 13 runs at 50Hz.",
                               PPM_LINE, false));
        } else {
            CHECK(note_matches("Active: 50Hz on PROG PORT. No pin is "
                               "assigned to it.", PPM_LINE, true));
        }
    }
}

/*
 * The save line follows the store and not the page: a save refused after
 * the change repaints that line alone, in the warning colour.
 */
TEST_CASE(a_refused_save_repaints_only_the_save_line)
{
    fresh();
    two_buffers();
    open_settings();
    tap(TAB_X(PAGE_DUT), TAB_Y);
    tap(ROW_L_X, ROW_Y(1));                    /* REPORT: asks to save */
    CHECK(settings_save_asked());
    CHECK(both_whole());
    gfx_color_t *was = malloc(FB_BYTES);
    REQUIRE_ALLOC(was);
    memcpy(was, fb, FB_BYTES);
    CHECK(!settings_save());                   /* no store: refused */
    scr->render(&cv, 0);
    scr->render(&cv1, 1);
    int outside = 0, warn = 0;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            /* The save line, 20px tall, 4px above the alert band. */
            const int sy = UI_ALERT_Y - UI_BAND_H - 24;
            const bool in = x >= UI_MARGIN + 10 && x < UI_MARGIN + 254
                            && y >= sy && y < sy + 20;
            if (in && fb[y * W + x] == ui_theme_color(UI_C_WARN)) {
                ++warn;
            }
            if (!in && fb[y * W + x] != was[y * W + x]) {
                ++outside;
            }
        }
    }
    free(was);
    CHECK(warn > 0);
    CHECK_EQ(outside, 0);
    CHECK(both_whole());
}

/*
 * With nothing reporting, the horn chases the command at the set speed
 * rather than jumping to it, and its grip breathes while it is driven:
 * every frame of either leaves both buffers as a full redraw would.
 */
TEST_CASE(the_horn_travels_and_breathes_as_a_full_redraw_would)
{
    fresh_armed();
    two_buffers();
    /* Past the arm's flash, 150ms: 15 frames of 26ms. */
    for (int i = 0; i < UI_HOLD_FLASH_MS / 10; ++i) {
        scr->render((i % 2) ? &cv1 : &cv, i % 2);
        scr->tick(1.0f / 39.0f);
    }
    tap(ARM_X + 1, SPEED_Y);                   /* the slowest sweep, 10% */
    int x, y;
    dial_at(60.0f, ARC_R - 20, &x, &y);
    tap(x, y);
    scr->tick(1.0f / 39.0f);
    CHECK(both_whole());
    gfx_color_t *early = malloc(FB_BYTES);
    REQUIRE_ALLOC(early);
    memcpy(early, fb, FB_BYTES);
    for (int i = 0; i < 4 * 39; ++i) {         /* 60deg at 36deg/s, and on */
        scr->tick(1.0f / 39.0f);
        scr->render((i % 2) ? &cv1 : &cv, i % 2);
    }
    CHECK(both_whole());
    CHECK(memcmp(fb, early, FB_BYTES) != 0);
    free(early);
}

/* ------------------------------------------------- the supply, from SERVO */

/* A set point typed on SERVO is SUPPLY's set point, and the keypad that
 * opened the overlay for it closes it again: the dial works at once. */
TEST_CASE(a_set_point_typed_on_servo_is_the_supplys)
{
    fresh_armed();
    tap(SUP_V_X, SUP_ROW_Y);
    keys("5.5");
    CHECK(fabsf(supply_screen_set_v() - 5.5f) < 1e-4f);
    tap(SUP_I_X, SUP_ROW_Y);
    keys("0.5");
    CHECK(fabsf(supply_screen_set_i() - 0.5f) < 1e-4f);
    CHECK(!supply_cmd(NULL));               /* a level, not a command */

    int x, y;
    dial_at(40.0f, ARC_R - 30, &x, &y);
    ev(x, y, TOUCH_EVENT_DOWN, 1);
    ev(x, y, TOUCH_EVENT_UP, 1);
    CHECK(servo_screen_commanded() != 1500);   /* no overlay in the way */

    /* CANCEL leaves the set point as it was. */
    tap(SUP_V_X, SUP_ROW_Y);
    key(UI_KEY_CANCEL);
    CHECK(fabsf(supply_screen_set_v() - 5.5f) < 1e-4f);
}

/* Typed outside the caps, it is snapped into them, as on SUPPLY. */
TEST_CASE(a_set_point_typed_on_servo_keeps_to_the_caps)
{
    fresh();
    settings_set(SET_SUPPLY_V_MAX, 8.4f);
    supply_screen_limits_changed();
    tap(SUP_V_X, SUP_ROW_Y);
    keys("12");                             /* past the keypad's range */
    CHECK(supply_screen_set_v() <= 8.4f + 1e-4f);
}

/* OUTPUT ON is SUPPLY's two-second hold; a short press asks for nothing. */
TEST_CASE(output_on_from_servo_is_a_two_second_hold)
{
    fresh();
    ev(SUP_OUT_X, SUP_ROW_Y, TOUCH_EVENT_DOWN, 1);
    for (int i = 0; i < 20; ++i) {
        scr->tick(1.0f / 40.0f);
    }
    ev(SUP_OUT_X, SUP_ROW_Y, TOUCH_EVENT_UP, 1);
    CHECK(!supply_cmd(NULL));

    supply_cmd_t c;
    ev(SUP_OUT_X, SUP_ROW_Y, TOUCH_EVENT_DOWN, 1);
    for (int i = 0; i < 100; ++i) {
        scr->tick(1.0f / 40.0f);
    }
    ev(SUP_OUT_X, SUP_ROW_Y, TOUCH_EVENT_UP, 1);
    CHECK(supply_cmd(&c));
    CHECK(c.on);
    CHECK(!c.off);
}

/* On, the same switch is OUTPUT OFF and a tap: and a finger that slides
 * off it before lifting asks for nothing. */
TEST_CASE(output_off_from_servo_is_a_tap)
{
    fresh();
    supply_screen_set_output(true);
    scr->tick(0.025f);
    ev(SUP_OUT_X, SUP_ROW_Y, TOUCH_EVENT_DOWN, 1);
    ev(SUP_OUT_X, 100, TOUCH_EVENT_MOVE, 1);
    ev(SUP_OUT_X, 100, TOUCH_EVENT_UP, 1);
    CHECK(!supply_cmd(NULL));

    supply_cmd_t c;
    tap(SUP_OUT_X, SUP_ROW_Y);
    CHECK(supply_cmd(&c));
    CHECK(c.off);
    CHECK(!c.on);
}

/* STOP ends OUTPUT ON's hold here too: the rest of the two seconds switches
 * nothing on. */
TEST_CASE(stop_ends_the_output_hold_on_servo)
{
    fresh();
    ev(SUP_OUT_X, SUP_ROW_Y, TOUCH_EVENT_DOWN, 1);
    for (int i = 0; i < 40; ++i) {
        scr->tick(1.0f / 40.0f);
    }
    servo_screen_cancel_arm();
    for (int i = 0; i < 80; ++i) {
        scr->tick(1.0f / 40.0f);
    }
    ev(SUP_OUT_X, SUP_ROW_Y, TOUCH_EVENT_UP, 1);
    CHECK(!supply_cmd(NULL));
}

/* With the output live, a typed set point waits for SUPPLY's question:
 * APPLY gives it to the supply, CANCEL drops it. */
TEST_CASE(a_set_point_for_a_live_output_waits_for_apply)
{
    fresh();
    supply_screen_set_output(true);
    scr->tick(0.025f);
    const float was = supply_screen_set_v();
    tap(SUP_V_X, SUP_ROW_Y);
    keys("5");
    CHECK_EQ(supply_screen_set_v(), was);
    tap(WARN_CANCEL_X, WARN_Y);
    CHECK_EQ(supply_screen_set_v(), was);

    tap(SUP_V_X, SUP_ROW_Y);
    keys("5");
    CHECK_EQ(supply_screen_set_v(), was);
    tap(WARN_APPLY_X, WARN_Y);
    CHECK(fabsf(supply_screen_set_v() - 5.0f) < 1e-4f);

    /* Asked not to ask: it goes straight through. */
    settings_set(SET_SUPPLY_CONFIRM_KEYS, 0.0f);
    tap(SUP_I_X, SUP_ROW_Y);
    keys("1");
    CHECK(fabsf(supply_screen_set_i() - 1.0f) < 1e-4f);
}

/* The question is about a live output: once the output is off it goes,
 * unanswered, and the change it held with it. */
TEST_CASE(the_question_goes_with_the_live_output)
{
    fresh();
    supply_screen_set_output(true);
    scr->tick(0.025f);
    const float was = supply_screen_set_v();
    tap(SUP_V_X, SUP_ROW_Y);
    keys("5");
    supply_screen_set_output(false);
    scr->tick(0.025f);
    tap(WARN_APPLY_X, WARN_Y);              /* nothing there to press */
    CHECK_EQ(supply_screen_set_v(), was);
}

/* A set point changed on SUPPLY, or a cap that moved it, is drawn here on
 * the next frame, without a full repaint. */
TEST_CASE(a_set_point_changed_on_supply_is_redrawn_here)
{
    fresh();
    scr->render(&cv, 0);
    gfx_color_t *before = malloc((size_t)W * H * sizeof(gfx_color_t));
    REQUIRE_ALLOC(before);
    memcpy(before, fb, (size_t)W * H * sizeof(gfx_color_t));
    supply_screen_put(9.0f, 1.0f);
    scr->tick(0.025f);
    scr->render(&cv, 0);
    int changed = 0;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            if (fb[(size_t)y * W + x] != before[(size_t)y * W + x]) {
                CHECK(y >= SUP_ROW_Y - 13 && y < SUP_ROW_Y + 13);
                ++changed;
            }
        }
    }
    CHECK(changed > 0);
    free(before);
}

/* A voltage raised past 6.0V, a standard servo's rating, waits for the HV
 * warning's two-second hold: a tap does nothing, CANCEL drops it. */
TEST_CASE(a_voltage_raised_past_6_v_takes_the_hv_hold)
{
    fresh();
    supply_screen_put(5.0f, 1.0f);
    tap(SUP_V_X, SUP_ROW_Y);
    keys("6");                              /* at the rating: no warning */
    CHECK(fabsf(supply_screen_set_v() - 6.0f) < 1e-4f);

    tap(SUP_V_X, SUP_ROW_Y);
    keys("7.4");
    CHECK(fabsf(supply_screen_set_v() - 6.0f) < 1e-4f);
    tap(WARN_APPLY_X, WARN_Y);              /* a tap is not the hold */
    CHECK(fabsf(supply_screen_set_v() - 6.0f) < 1e-4f);
    tap(WARN_CANCEL_X, WARN_Y);
    CHECK(fabsf(supply_screen_set_v() - 6.0f) < 1e-4f);

    tap(SUP_V_X, SUP_ROW_Y);
    keys("7.4");
    hold_apply(2.3f);
    CHECK(fabsf(supply_screen_set_v() - 7.4f) < 1e-4f);

    /* Already past it: the warning was given, and a further change is a
     * plain set point again. */
    tap(SUP_V_X, SUP_ROW_Y);
    keys("8");
    CHECK(fabsf(supply_screen_set_v() - 8.0f) < 1e-4f);
}

/* With the output on, the HV hold stands in for SUPPLY's question, and the
 * output going off does not drop it: it is about the servo. */
TEST_CASE(the_hv_hold_stands_for_the_live_question)
{
    fresh();
    supply_screen_put(5.0f, 1.0f);
    supply_screen_set_output(true);
    scr->tick(0.025f);
    tap(SUP_V_X, SUP_ROW_Y);
    keys("8.4");
    supply_screen_set_output(false);
    scr->tick(0.025f);
    hold_apply(2.3f);
    CHECK(fabsf(supply_screen_set_v() - 8.4f) < 1e-4f);
}

/* OUTPUT ON with a set point past 6.0V already in force -- made on SUPPLY,
 * or before this screen was opened -- goes through the HV warning: the
 * switch's own hold switches nothing on, a tap on APPLY does nothing, STOP
 * ends the hold, and only the two-second hold on APPLY sends the ON. */
TEST_CASE(output_on_past_6_v_takes_the_hv_hold)
{
    fresh();
    supply_screen_put(7.4f, 1.0f);              /* as if set on SUPPLY */
    scr->tick(0.025f);
    ev(SUP_OUT_X, SUP_ROW_Y, TOUCH_EVENT_DOWN, 1);
    for (int i = 0; i < 100; ++i) {
        scr->tick(1.0f / 40.0f);
    }
    ev(SUP_OUT_X, SUP_ROW_Y, TOUCH_EVENT_UP, 1);
    CHECK(!supply_cmd(NULL));                   /* the switch's hold: no */

    tap(WARN_APPLY_X, WARN_Y);                  /* a tap is not the hold */
    CHECK(!supply_cmd(NULL));

    ev(WARN_APPLY_X, WARN_Y, TOUCH_EVENT_DOWN, 1);
    for (int i = 0; i < 40; ++i) {
        scr->tick(1.0f / 40.0f);
    }
    servo_screen_cancel_arm();                  /* STOP mid-hold */
    for (int i = 0; i < 80; ++i) {
        scr->tick(1.0f / 40.0f);
    }
    ev(WARN_APPLY_X, WARN_Y, TOUCH_EVENT_UP, 1);
    CHECK(!supply_cmd(NULL));

    hold_apply(2.3f);
    supply_cmd_t c;
    CHECK(supply_cmd(&c));
    CHECK(c.on);
    CHECK(!c.off);
    CHECK(fabsf(supply_screen_set_v() - 7.4f) < 1e-4f);

    /* CANCEL switches nothing on; the warning drawn says what is asked. */
    fresh();
    supply_screen_put(8.4f, 1.0f);
    scr->tick(0.025f);
    tap(SUP_OUT_X, SUP_ROW_Y);
    scr->tick(0.025f);
    scr->render(&cv, 0);
    tap(WARN_CANCEL_X, WARN_Y);
    for (int i = 0; i < 100; ++i) {
        scr->tick(1.0f / 40.0f);
    }
    CHECK(!supply_cmd(NULL));

    /* At 6.0V it is the ordinary hold. */
    fresh();
    supply_screen_put(6.0f, 1.0f);
    scr->tick(0.025f);
    ev(SUP_OUT_X, SUP_ROW_Y, TOUCH_EVENT_DOWN, 1);
    for (int i = 0; i < 100; ++i) {
        scr->tick(1.0f / 40.0f);
    }
    ev(SUP_OUT_X, SUP_ROW_Y, TOUCH_EVENT_UP, 1);
    CHECK(supply_cmd(&c));
    CHECK(c.on);
}

/* A second hold on OUTPUT ON while the first ON is on its way: the output
 * reporting on during it does not make its release an OFF. */
TEST_CASE(an_on_reported_during_a_second_servo_hold_is_not_an_off)
{
    fresh();
    ev(SUP_OUT_X, SUP_ROW_Y, TOUCH_EVENT_DOWN, 1);
    for (int i = 0; i < 100; ++i) {
        scr->tick(1.0f / 40.0f);
    }
    ev(SUP_OUT_X, SUP_ROW_Y, TOUCH_EVENT_UP, 1);
    CHECK(supply_cmd(NULL));                    /* the first ON */
    ev(SUP_OUT_X, SUP_ROW_Y, TOUCH_EVENT_DOWN, 1);
    scr->tick(1.0f / 40.0f);
    supply_screen_set_output(true);
    scr->tick(1.0f / 40.0f);
    ev(SUP_OUT_X, SUP_ROW_Y, TOUCH_EVENT_UP, 1);
    supply_cmd_t c;
    CHECK(!supply_cmd(&c) || !c.off);
}

/* A question asked while an ON was on its way goes when STOP drops that
 * ON, though the output never reported on. */
TEST_CASE(a_servo_question_for_a_dropped_on_goes)
{
    fresh();
    supply_screen_put(5.0f, 1.0f);
    ev(SUP_OUT_X, SUP_ROW_Y, TOUCH_EVENT_DOWN, 1);
    for (int i = 0; i < 100; ++i) {
        scr->tick(1.0f / 40.0f);
    }
    ev(SUP_OUT_X, SUP_ROW_Y, TOUCH_EVENT_UP, 1);
    tap(SUP_V_X, SUP_ROW_Y);
    keys("5.5");
    CHECK(fabsf(supply_screen_set_v() - 5.0f) < 1e-4f);   /* the question */
    supply_screen_cancel_on();
    servo_screen_cancel_arm();
    scr->tick(1.0f / 40.0f);
    tap(WARN_APPLY_X, WARN_Y);                  /* nothing there now */
    CHECK(fabsf(supply_screen_set_v() - 5.0f) < 1e-4f);
}

/* APPLY pressed on the question, then the output goes off before the finger
 * lifts: the release applies nothing, whether the tick that drops the
 * question comes first or not. */
TEST_CASE(an_apply_held_as_the_output_goes_off_applies_nothing)
{
    for (int tick_first = 0; tick_first < 2; ++tick_first) {
        fresh();
        supply_screen_put(5.0f, 1.0f);
        supply_screen_set_output(true);
        scr->tick(0.025f);
        tap(SUP_V_X, SUP_ROW_Y);
        keys("5.5");
        ev(WARN_APPLY_X, WARN_Y, TOUCH_EVENT_DOWN, 1);
        supply_screen_set_output(false);
        if (tick_first) {
            scr->tick(0.025f);
        }
        ev(WARN_APPLY_X, WARN_Y, TOUCH_EVENT_UP, 1);
        CHECK(fabsf(supply_screen_set_v() - 5.0f) < 1e-4f);
    }
}

/* APPLY gives the supply the set point that was typed, and leaves the
 * other as it is now: a cap that moved it meanwhile is not undone. */
TEST_CASE(apply_sets_only_the_set_point_that_was_typed)
{
    fresh();
    supply_screen_put(5.0f, 1.0f);
    supply_screen_set_output(true);
    scr->tick(0.025f);
    tap(SUP_I_X, SUP_ROW_Y);
    keys("1.5");
    supply_screen_put(4.0f, 1.0f);              /* the voltage moved */
    scr->tick(0.025f);
    tap(WARN_APPLY_X, WARN_Y);
    CHECK(fabsf(supply_screen_set_i() - 1.5f) < 1e-4f);
    CHECK(fabsf(supply_screen_set_v() - 4.0f) < 1e-4f);
}

/* A press on OUTPUT OFF in the frame the output was reported on: the
 * screen's drawn state has not caught up, and a lost release still sends
 * the OFF. */
TEST_CASE(an_off_press_before_the_tick_is_still_an_off)
{
    fresh();
    supply_screen_set_output(true);             /* no tick yet */
    ev(SUP_OUT_X, SUP_ROW_Y, TOUCH_EVENT_DOWN, 1);
    scr->cancel();
    supply_cmd_t c;
    CHECK(supply_cmd(&c));
    CHECK(c.off);
}

/* The HV warning says whether the output is on, and says it again when
 * that changes. */
TEST_CASE(the_hv_warning_redraws_when_the_output_changes)
{
    fresh();
    supply_screen_put(5.0f, 1.0f);
    tap(SUP_V_X, SUP_ROW_Y);
    keys("7.4");
    scr->tick(0.025f);
    scr->render(&cv, 0);
    gfx_color_t *before = malloc((size_t)W * H * sizeof(gfx_color_t));
    REQUIRE_ALLOC(before);
    memcpy(before, fb, (size_t)W * H * sizeof(gfx_color_t));
    supply_screen_set_output(true);
    scr->tick(0.025f);
    scr->render(&cv, 0);
    int changed = 0;
    for (int y = 0; y < 430; ++y) {             /* the warning's text */
        for (int x = 0; x < 628; ++x) {
            changed += (fb[(size_t)y * W + x] != before[(size_t)y * W + x]);
        }
    }
    CHECK(changed > 0);
    free(before);
}

/* A set point's keypad shut by SETTINGS leaves nothing behind: SETTINGS
 * opened afterwards stays open when a keypad from its own rows is done. */
TEST_CASE(settings_shuts_a_set_points_keypad_cleanly)
{
    fresh();
    tap(SUP_V_X, SUP_ROW_Y);                    /* the keypad, on its own */
    open_settings();                            /* shuts it */
    open_settings();                            /* the settings */
    tap(SUP_V_X, SUP_ROW_Y);                    /* a set point over them */
    key(UI_KEY_CANCEL);                         /* back to the settings */
    int x, y;
    dial_at(40.0f, ARC_R - 30, &x, &y);
    ev(x, y, TOUCH_EVENT_DOWN, 1);
    ev(x, y, TOUCH_EVENT_UP, 1);
    CHECK_EQ(servo_screen_commanded(), 1500);   /* the overlay is over it */
}

/* A cap that comes down while the question stands takes the waiting value
 * with it; the cap recovering before APPLY does not bring it back. */
TEST_CASE(a_cap_lowered_under_the_question_lowers_its_value)
{
    fresh();
    supply_screen_put(9.0f, 1.0f);              /* past 6V already */
    supply_screen_set_output(true);
    scr->tick(0.025f);
    tap(SUP_V_X, SUP_ROW_Y);
    keys("12");
    settings_set(SET_SUPPLY_V_MAX, 10.0f);
    supply_screen_limits_changed();
    scr->tick(0.025f);
    settings_set(SET_SUPPLY_V_MAX, 21.0f);
    supply_screen_limits_changed();
    scr->tick(0.025f);
    tap(WARN_APPLY_X, WARN_Y);
    CHECK(supply_screen_set_v() <= 10.0f + 1e-4f);
    CHECK(supply_screen_set_v() > 9.0f);
}

/* The rest of the SET line's gestures, each drawn as it happens: a value
 * typed as it was, the question drawn, the HV hold slid off and drawn as it
 * fills, an ON hold under a finger as the output comes and goes, and an
 * OFF press the screen is left on. */
TEST_CASE(the_set_lines_gestures_draw_and_end_as_they_should)
{
    fresh();
    supply_screen_put(5.0f, 1.0f);
    tap(SUP_V_X, SUP_ROW_Y);
    keys("5");                                  /* the value it has */
    CHECK(fabsf(supply_screen_set_v() - 5.0f) < 1e-4f);

    supply_screen_set_output(true);
    scr->tick(0.025f);
    tap(SUP_V_X, SUP_ROW_Y);
    keys("5.5");                                /* the question */
    scr->render(&cv, 0);
    CHECK_EQ(fb[UI_MARGIN * W + UI_MARGIN], ui_theme_color(UI_C_WARN));
    tap(WARN_CANCEL_X, WARN_Y);

    tap(SUP_V_X, SUP_ROW_Y);
    supply_screen_set_output(false);
    scr->tick(0.025f);
    keys("7.4");                                /* the HV warning */
    ev(WARN_APPLY_X, WARN_Y, TOUCH_EVENT_DOWN, 1);
    for (int i = 0; i < 20; ++i) {
        scr->tick(1.0f / 40.0f);
        scr->render(&cv, 0);
    }
    ev(WARN_APPLY_X, 100, TOUCH_EVENT_MOVE, 1);  /* slid off: abandoned */
    ev(WARN_APPLY_X, 100, TOUCH_EVENT_UP, 1);
    for (int i = 0; i < 100; ++i) {
        scr->tick(1.0f / 40.0f);
    }
    CHECK(fabsf(supply_screen_set_v() - 5.0f) < 1e-4f);
    tap(WARN_CANCEL_X, WARN_Y);

    ev(SUP_OUT_X, SUP_ROW_Y, TOUCH_EVENT_DOWN, 1);
    scr->tick(1.0f / 40.0f);
    supply_screen_set_output(true);
    scr->tick(1.0f / 40.0f);
    supply_screen_set_output(false);
    scr->tick(1.0f / 40.0f);
    ev(SUP_OUT_X, SUP_ROW_Y, TOUCH_EVENT_UP, 1);
    CHECK(!supply_cmd(NULL));

    supply_screen_set_output(true);
    scr->tick(1.0f / 40.0f);
    ev(SUP_OUT_X, SUP_ROW_Y, TOUCH_EVENT_DOWN, 1);
    scr->leave();
    supply_cmd_t c;
    CHECK(supply_cmd(&c));
    CHECK(c.off);
}

/* STOP ends the warnings' holds as it ends ARM's: the rest of the two
 * seconds applies neither the voltage nor the profile. */
TEST_CASE(stop_ends_the_warning_holds)
{
    fresh();
    supply_screen_put(5.0f, 1.0f);
    tap(SUP_V_X, SUP_ROW_Y);
    keys("7.4");
    ev(WARN_APPLY_X, WARN_Y, TOUCH_EVENT_DOWN, 1);
    for (int i = 0; i < 40; ++i) {
        scr->tick(1.0f / 40.0f);
    }
    servo_screen_cancel_arm();
    for (int i = 0; i < 80; ++i) {
        scr->tick(1.0f / 40.0f);
    }
    ev(WARN_APPLY_X, WARN_Y, TOUCH_EVENT_UP, 1);
    CHECK(fabsf(supply_screen_set_v() - 5.0f) < 1e-4f);
    tap(WARN_CANCEL_X, WARN_Y);

    open_settings();
    tap(ROW_L_X, ROW_Y(0));
    tap(CH_X(3), CH_Y(3));                      /* HELI CYCLIC: the warning */
    ev(WARN_APPLY_X, WARN_Y, TOUCH_EVENT_DOWN, 1);
    for (int i = 0; i < 40; ++i) {
        scr->tick(1.0f / 40.0f);
    }
    servo_screen_cancel_arm();
    for (int i = 0; i < 80; ++i) {
        scr->tick(1.0f / 40.0f);
    }
    ev(WARN_APPLY_X, WARN_Y, TOUCH_EVENT_UP, 1);
    CHECK_STR_EQ(servo_screen_type_name(), "STANDARD PWM");
}

/* APPLY tapped on the question while the output is on, and STOP before the
 * finger lifts: the release applies nothing. */
TEST_CASE(stop_lets_go_of_an_apply_press)
{
    fresh();
    supply_screen_put(5.0f, 1.0f);
    supply_screen_set_output(true);
    scr->tick(0.025f);
    tap(SUP_V_X, SUP_ROW_Y);
    keys("5.5");
    ev(WARN_APPLY_X, WARN_Y, TOUCH_EVENT_DOWN, 1);
    servo_screen_cancel_arm();
    supply_screen_cancel_on();
    ev(WARN_APPLY_X, WARN_Y, TOUCH_EVENT_UP, 1);
    CHECK(fabsf(supply_screen_set_v() - 5.0f) < 1e-4f);
}

/* STOP and a whole APPLY tap in one frame: the stop is seen first, as the
 * render loop does, while the supply has not yet reported the output off.
 * The tap applies nothing, and the question has gone by the frame's tick. */
TEST_CASE(stop_and_an_apply_tap_in_one_frame_apply_nothing)
{
    fresh();
    supply_screen_put(5.0f, 1.0f);
    supply_screen_set_output(true);
    scr->tick(0.025f);
    tap(SUP_V_X, SUP_ROW_Y);
    keys("5.5");
    servo_screen_cancel_arm();
    supply_screen_cancel_on();
    ev(WARN_APPLY_X, WARN_Y, TOUCH_EVENT_DOWN, 1);
    ev(WARN_APPLY_X, WARN_Y, TOUCH_EVENT_UP, 1);
    CHECK(fabsf(supply_screen_set_v() - 5.0f) < 1e-4f);
    scr->tick(0.025f);
    tap(WARN_APPLY_X, WARN_Y);                  /* nothing there now */
    CHECK(fabsf(supply_screen_set_v() - 5.0f) < 1e-4f);
}

/* A question asked after a stop, on an output still on, is not the one the
 * stop ended: APPLY applies it. */
TEST_CASE(a_question_asked_after_a_stop_stands)
{
    fresh();
    supply_screen_put(5.0f, 1.0f);
    supply_screen_set_output(true);
    scr->tick(0.025f);
    servo_screen_cancel_arm();
    scr->tick(0.025f);
    tap(SUP_V_X, SUP_ROW_Y);
    keys("5.5");
    tap(WARN_APPLY_X, WARN_Y);
    CHECK(fabsf(supply_screen_set_v() - 5.5f) < 1e-4f);
}

/* The output reported off and on again between two frames: the question
 * was about the run that ended, and APPLY gives the new run nothing. */
TEST_CASE(a_question_does_not_outlive_its_run)
{
    fresh();
    supply_screen_put(5.0f, 1.0f);
    supply_screen_set_output(true);
    scr->tick(0.025f);
    tap(SUP_V_X, SUP_ROW_Y);
    keys("5.5");
    supply_screen_set_output(false);           /* no tick between */
    supply_screen_set_output(true);
    tap(WARN_APPLY_X, WARN_Y);
    CHECK(fabsf(supply_screen_set_v() - 5.0f) < 1e-4f);
    scr->tick(0.025f);                          /* and the question is gone */
    tap(WARN_APPLY_X, WARN_Y);
    CHECK(fabsf(supply_screen_set_v() - 5.0f) < 1e-4f);
}

int main(void)
{
    RUN(a_heli_type_waits_for_the_warning_held_two_seconds);
    RUN(every_profile_is_a_range_the_coprocessor_takes);
    RUN(a_pulse_width_the_coprocessor_would_refuse_is_not_taken);
    RUN(each_side_of_centre_runs_to_its_own_end);
    RUN(the_pause_is_kept_from_the_top_of_the_range_a_command_carries);
    RUN(a_warning_cancelled_dropped_or_left_applies_nothing);
    RUN(a_rate_above_60_hz_needs_the_warning_and_60_does_not);
    RUN(standard_pwm_keeps_a_millisecond_between_pulses);
    RUN(a_restart_is_standard_pwm_at_50_hz);
    RUN(reverse_and_the_pulse_widths_reshape_the_command);
    RUN(the_sweep_and_run_settings_are_kept);
    RUN(the_overlay_leaves_the_right_card_working);
    RUN(the_tag_is_red_while_a_dangerous_profile_is_in_force);
    RUN(a_supply_sample_repaints_only_the_power_plot_and_the_current_row);
    RUN(each_face_of_the_overlay_draws_as_a_full_redraw_would);
    RUN(a_refused_save_repaints_only_the_save_line);
    RUN(the_output_page_says_whether_the_rate_reached_the_pins);
    RUN(the_in_force_note_names_what_the_coprocessor_runs_at_the_rate);
    RUN(the_in_force_note_names_the_target_the_coprocessor_holds);
    RUN(the_in_force_note_carries_the_rate_and_names_none_without_a_link);
    RUN(the_in_force_note_follows_the_prog_page_not_the_setting);
    RUN(the_horn_travels_and_breathes_as_a_full_redraw_would);
    RUN(drags_and_samples_leave_both_buffers_as_a_full_redraw_would);
    RUN(a_set_point_typed_on_servo_is_the_supplys);
    RUN(a_set_point_typed_on_servo_keeps_to_the_caps);
    RUN(output_on_from_servo_is_a_two_second_hold);
    RUN(output_off_from_servo_is_a_tap);
    RUN(stop_ends_the_output_hold_on_servo);
    RUN(a_set_point_for_a_live_output_waits_for_apply);
    RUN(the_question_goes_with_the_live_output);
    RUN(a_set_point_changed_on_supply_is_redrawn_here);
    RUN(a_voltage_raised_past_6_v_takes_the_hv_hold);
    RUN(the_hv_hold_stands_for_the_live_question);
    RUN(output_on_past_6_v_takes_the_hv_hold);
    RUN(an_on_reported_during_a_second_servo_hold_is_not_an_off);
    RUN(a_servo_question_for_a_dropped_on_goes);
    RUN(an_apply_held_as_the_output_goes_off_applies_nothing);
    RUN(apply_sets_only_the_set_point_that_was_typed);
    RUN(an_off_press_before_the_tick_is_still_an_off);
    RUN(the_hv_warning_redraws_when_the_output_changes);
    RUN(settings_shuts_a_set_points_keypad_cleanly);
    RUN(a_cap_lowered_under_the_question_lowers_its_value);
    RUN(the_set_lines_gestures_draw_and_end_as_they_should);
    RUN(stop_ends_the_warning_holds);
    RUN(stop_lets_go_of_an_apply_press);
    RUN(stop_and_an_apply_tap_in_one_frame_apply_nothing);
    RUN(a_question_asked_after_a_stop_stands);
    RUN(a_question_does_not_outlive_its_run);
    return test_summary("servo_settings");
}
