/*
 * What the SERVO screen's SETTINGS overlay decides: the profiles
 * and the warning that applies a heli type or a rate above 60Hz,
 * the note on the rate in force and the target, and the supply's
 * PD MINI row on LIVE: its set point redrawn, its ON past 6.0V only
 * through the HV warning.
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
    sl_tap(HL_TYPE);                    /* TYPE */
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
    sl_tap(HL_MIN);                    /* PULSE MIN */
    keys("350");
    key(UI_KEY_CANCEL);
    sl_tap(HL_MAX);                    /* PULSE MAX */
    keys("2600");
    key(UI_KEY_CANCEL);
    /* A centre further from one end than the ceiling allows the other:
     * 1800us with MIN at 1000 would carry 1000..2600. */
    sl_tap(HL_CENTRE);                    /* PULSE CENTRE */
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
    sl_tap(HL_CENTRE);                    /* PULSE CENTRE */
    keys("1060");                              /* would carry 410..1710 */
    key(UI_KEY_CANCEL);
    close_settings();
    servo_screen_set_commanded(90.0f);
    servo_cmd_t c = last_cmd();
    CHECK_EQ(c.max_us, 1110);                  /* refused: still 760 */
    open_settings();
    sl_tap(HL_CENTRE);
    keys("840");                               /* 410..1270 fits */
    close_settings();
    servo_screen_set_commanded(90.0f);
    c = last_cmd();
    CHECK_EQ(c.min_us, 410);
    CHECK_EQ(c.max_us, 1270);
    CHECK_EQ(c.frame_hz, 560);

    fresh_armed();
    open_settings();
    sl_tap(HL_CENTRE);                    /* PULSE CENTRE */
    keys("1700");                              /* 1000..2400 */
    sl_tap(HL_RATE);                    /* FRAME RATE */
    tap(CH_X(6), CH_Y(6));                     /* CUSTOM, after 250Hz */
    keys("333");                               /* past 294Hz: refused */
    key(UI_KEY_CANCEL);
    CHECK_EQ(servo_screen_frame_hz(), 50);
    sl_tap(HL_RATE);
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
    sl_tap(HL_CENTRE);                    /* PULSE CENTRE */
    keys("1520");
    sl_tap(HL_MIN);                    /* PULSE MIN */
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
    sl_tap(HL_TYPE);
    tap(CH_X(4), CH_Y(4));                     /* HELI TAIL 760 */
    tap(WARN_CANCEL_X, WARN_Y);
    for (int i = 0; i < 120; ++i) {
        scr->tick(1.0f / 39.0f);
    }
    CHECK_STR_EQ(servo_screen_type_name(), "STANDARD PWM");

    /* A hold the touch stream lost does not complete on its own. */
    sl_tap(HL_TYPE);
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
    sl_tap(HL_RATE);                    /* FRAME RATE */
    tap(CH_X(1), CH_Y(1));                     /* 60Hz */
    CHECK_EQ(servo_screen_frame_hz(), 60);
    sl_tap(HL_RATE);
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
    sl_tap(HL_RATE);
    tap(CH_X(7), CH_Y(7));                     /* 333Hz, the last */
    hold_apply(2.3f);
    CHECK_EQ(servo_screen_frame_hz(), 333);
    sl_tap(HL_MAX);                    /* PULSE MAX */
    keys("2100");                              /* refused: no pause left */
    key(UI_KEY_CANCEL);
    sl_tap(HL_MAX);
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
    sl_tap(HL_RATE);
    tap(CH_X(7), CH_Y(7));                     /* CUSTOM: 7 rates fit 312Hz */
    keys("320");
    key(UI_KEY_CANCEL);
    CHECK_EQ(servo_screen_frame_hz(), 50);
    sl_tap(HL_RATE);
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
    sl_step(HL_REVERSE, +1);                    /* REVERSE */
    CHECK(last_cmd().value_us < 1500);         /* said again, mirrored */

    fresh_armed();
    open_settings();
    sl_tap(HL_CENTRE);                    /* PULSE CENTRE */
    keys("1520");
    sl_tap(HL_MIN);                    /* PULSE MIN */
    keys("900");
    close_settings();
    dial_at(0.0f, ARC_R - 20, &x, &y);
    tap(x, y);
    const servo_cmd_t c = last_cmd();
    CHECK_EQ(c.value_us, 1520);
    CHECK_EQ(c.min_us, 900);
}

/* SWEEP's rows and TEST's rows are kept and saved by the list's SAVE;
 * SUPPLY's caps are not edited here (their home is SUPPLY > SETTINGS),
 * and TEST's list closes to TEST with the home tag.  The hub's list holds
 * the device under test's NAME and REPORT. */
TEST_CASE(the_sweep_and_run_settings_are_kept)
{
    fresh();
    go(SERVO_FN_SWEEP);
    tap_settings();                            /* SWEEP's list */
    CHECK(servo_screen_list()->open);
    const int curve = settings_get_int(SET_SERVO_CURVE);
    sl_step(0, +1);                            /* CURVE: the next */
    CHECK(settings_get_int(SET_SERVO_CURVE) != curve);
    sl_part(1, UI_SL_PART_VALUE);              /* SPEED: the keypad */
    sl_keys("1.25");
    CHECK_NEAR(settings_get(SET_SERVO_TEST_HZ), 1.25f, 1e-4f);
    CHECK(settings_dirty());                   /* until the list's SAVE */

    open_run_settings(0);                      /* TEST's list */
    CHECK(servo_screen_list()->open);
    sl_step(TL_STEP_48, +1);                   /* STEP 4.8V: a switch */
    CHECK(!settings_get_bool(SET_SERVO_STEP_48));
    sl_part(TL_IDLE, UI_SL_PART_VALUE);        /* IDLE CURRENT */
    sl_keys("0.3");
    CHECK_NEAR(settings_get(SET_SERVO_IDLE_MAX), 0.3f, 1e-4f);
    sl_part(TL_STALL, UI_SL_PART_VALUE);       /* STALL AT */
    sl_keys("1.5");
    CHECK_NEAR(settings_get(SET_SERVO_STALL_A), 1.5f, 1e-4f);
    CHECK_NEAR(settings_get(SET_SUPPLY_V_MAX), 21.0f, 1e-4f);
    CHECK(settings_dirty());                   /* until the list's SAVE */
    tap_settings();                            /* SAVE */
    CHECK(home_tag());
    CHECK(!servo_screen_list()->open);
    CHECK_EQ(servo_screen_task(), (int)UI_WB_TEST);
    CHECK(!home_tag());                        /* TEST climbs no further */

    open_settings();
    sl_tap(HL_NAME);                           /* NAME: the keyboard */
    const int seq[] = { UI_TK_CLR, 31 /* X */, 15 /* Y */, 0 /* 1 */,
                        UI_TK_OK };
    for (size_t i = 0; i < sizeof(seq) / sizeof(seq[0]); ++i) {
        sl_text_key(seq[i]);
    }
    CHECK_STR_EQ(settings_text(SET_TEXT_DUT_NAME), "XY1");
    sl_step(HL_REPORT, +1);                    /* REPORT */
    CHECK(!settings_get_bool(SET_SERVO_REPORT));
}

TEST_CASE(the_overlay_leaves_the_right_card_working)
{
    /* ARM, CENTRE and RELEASE work beside a panel over the left column,
     * PORT's list; the dial under it does not.  CENTRE is a position,
     * taken on an armed bench. */
    fresh();
    tap(SV_PORT_X, SV_PORT_Y);
    CHECK(scr->covered());
    arm_press();
    hold_for(2.2f);
    arm_release();
    CHECK_EQ(last_cmd().kind, SERVO_CMD_ARM);
    servo_screen_set_armed(true);
    tap(SV_CENTRE_X, SV_BTN_Y);                /* CENTRE */
    CHECK_EQ(last_cmd().kind, SERVO_CMD_CENTRE);
    int x, y;
    dial_at(45.0f, ARC_R - 20, &x, &y);
    tap(x, y);                                 /* on the keypad, not the dial */
    CHECK(servo_screen_commanded() == 1500);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
    tap(CH_CANCEL_X, CH_CANCEL_Y);
    CHECK(!scr->covered());
}

TEST_CASE(the_tag_is_red_while_a_dangerous_profile_is_in_force)
{
    fresh();
    scr->render(&cv, 0);
    /* The tag, inside its pill and left of its text. */
    const gfx_color_t calm = fb[(SV_TAG_Y + 20) * W + 660];
    CHECK(calm != ui_theme_color(UI_C_DANGER));
    choose_type(3);
    scr->render(&cv, 0);
    CHECK_EQ(fb[(SV_TAG_Y + 20) * W + 660], ui_theme_color(UI_C_DANGER));
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
 * in the digits shown repaints the rail line under LIVE's READY rows, and
 * LIVE's CURRENT reading at the foot of the left card, and nothing else. */
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
            const bool in = rc && y >= 400 && y < 428;
            const bool row = x >= 416 && x < 610 && y >= 384 && y < 428;
            if (in) { ++inside; } else if (row) { ++in_row; } else { ++outside; }
        }
    }
    free(was);
    CHECK(inside > 0);
    CHECK(in_row > 0);
    CHECK_EQ(outside, 0);
    servo_screen_supply(NULL);
}


/*
 * Each face of the function screens -- TEST's and LIMIT RUN's cards and
 * each run's settings, the hub's list, a choice, the keypad, the keyboard
 * and the warning
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

    (void)sl_row(HL_NAME);
    CHECK(both_whole());
    sl_tap(HL_NAME);                           /* DUT NAME: the keyboard */
    CHECK(both_whole());
    sl_text_key(UI_TK_CANCEL);
    CHECK_STR_EQ(settings_text(SET_TEXT_DUT_NAME), "SERVO");
    CHECK(both_whole());

    (void)sl_row(HL_TRAVEL);
    CHECK(both_whole());
    memcpy(page, fb, FB_BYTES);
    sl_tap(HL_TRAVEL);                         /* TRAVEL: the keypad */
    CHECK(both_whole());
    key(UI_KEY_CANCEL);
    CHECK(both_whole());
    CHECK_EQ(memcmp(fb, page, FB_BYTES), 0);

    (void)sl_row(HL_TYPE);
    CHECK(both_whole());
    memcpy(page, fb, FB_BYTES);
    sl_tap(HL_TYPE);                           /* TYPE: the list */
    CHECK(both_whole());
    tap(CH_CANCEL_X, CH_CANCEL_Y);
    CHECK_STR_EQ(servo_screen_type_name(), "STANDARD PWM");
    CHECK(both_whole());
    CHECK_EQ(memcmp(fb, page, FB_BYTES), 0);

    sl_tap(HL_TYPE);
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

/** @brief Pixels in the warning colour on the hub list's OUTPUT heading,
 *         its first row.  @return the count */
static int heading_warn(void)
{
    const gfx_rect_t r = ui_setlist_row_rect(servo_screen_list(), 0);
    int n = 0;
    for (int y = r.y; y < r.y + r.h; ++y) {
        for (int x = r.x; x < r.x + r.w; ++x) {
            n += fb[y * W + x] == ui_theme_color(UI_C_WARN);
        }
    }
    return n;
}

/*
 * The hub list's OUTPUT heading says whether the frame rate reached the
 * pins, and in the warning colour when it did not: refused by the binding,
 * or an IOmcu that takes none.  News about another rate is not news about
 * this one.
 */
TEST_CASE(the_output_heading_says_whether_the_rate_reached_the_pins)
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
        CHECK(heading_warn() > 0);
    }
    free(unsent);
}

/*
 * What an acknowledged rate reaches depends on the IOmcu.  From 4.19
 * the TARGET alone runs at it; a 4.1 to 4.18 IOmcu runs every PWM
 * surface at it, whatever the target, and the note says that instead in
 * its first line.  Both are news, not a warning.
 */
/* Whether the screen's rate note is @p l1 and @p l2, in the warning colour
 * with @p warn. */
static bool note_matches(const char *l1, const char *l2, bool warn)
{
    char a[96], b[96];
    const bool w = servo_screen_rate_note(a, b, sizeof(a));
    return w == warn && strcmp(a, l1) == 0 && strcmp(b, l2) == 0;
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
    CHECK_EQ(heading_warn(), 0);
    CHECK(note_matches("Active: all PWM control surfaces run at 50Hz.",
                       ui_tr(TX_SV_IN_FORCE_2), false));

    /* The rate is in the "every PWM surface" line: 50Hz and 60Hz read
     * differently there, and the second line stays. */
    memcpy(target, fb, FB_BYTES);
    sl_tap(HL_RATE);                    /* FRAME RATE */
    tap(CH_X(1), CH_Y(1));                     /* 60Hz */
    CHECK_EQ(servo_screen_frame_hz(), 60);
    servo_screen_rate(servo_rate_in_force(18u), 60u);
    CHECK(both_whole());
    CHECK(memcmp(fb, target, FB_BYTES) != 0);
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
#define NOT_SET  "The PORT chosen is not active."
static const note_state_t k_note[] = {
    { 0, 0u, SERVO_SCREEN_NO_PIN, false, "Active: OUTPUTS run at 50Hz.",
      PPM_LINE, false },
    { 1, 1u, 13u, true, "Active: PROG PORT 1 GPIO 13 runs at 50Hz.", PPM_LINE,
      false },
    /* PROG PORT set and the port unbound: the outputs stay the target. */
    { 1, 0u, SERVO_SCREEN_NO_PIN, false, "Active: OUTPUTS run at 50Hz.",
      NOT_SET, true },
    /* The port held while OUTPUTS is set: the change waits for rest. */
    { 0, 1u, 13u, true, "Active: PROG PORT 1 GPIO 13 runs at 50Hz.", NOT_SET,
      true },
    { 1, 1u, SERVO_SCREEN_NO_PIN, true,
      "Active: 50Hz on PROG PORT 1. No pin is assigned to it.", PPM_LINE, true },
    { 1, 1u, 63u, false, "Active: PROG PORT 1 GPIO 63 runs at 50Hz.",
      "PORT PWM is OFF. The port sends no pulses.", true },
    /* The port with no pin and its pulse off: the first line still says
     * there is no pin, the second that no pulse goes out. */
    { 1, 1u, SERVO_SCREEN_NO_PIN, false,
      "Active: 50Hz on PROG PORT 1. No pin is assigned to it.",
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
    CHECK(note_matches("Active: PROG PORT 1 GPIO 13 runs at 50Hz.", NOT_SET,
                       true));
    servo_screen_set_drive(1u, 13u, false);
    CHECK(both_whole());
    CHECK(note_matches("Active: PROG PORT 1 GPIO 13 runs at 50Hz.", NOT_SET,
                       true));
    servo_screen_set_drive(0u, SERVO_SCREEN_NO_PIN, false);
    CHECK(both_whole());
    CHECK(note_matches("Active: OUTPUTS run at 50Hz.", PPM_LINE, false));

    /* German, the same states: protocol and setting names as the UI writes
     * them. */
    static const char *const de[NOTE_N][2] = {
        { "Aktiv: Ausgänge laufen mit 50Hz.",
          "PPM-Ausgänge behalten ihre eigene Pulsrate." },
        { "Aktiv: PROG-PORT 1 GPIO 13 läuft mit 50Hz.",
          "PPM-Ausgänge behalten ihre eigene Pulsrate." },
        { "Aktiv: Ausgänge laufen mit 50Hz.",
          "Der gewählte PORT ist nicht aktiv." },
        { "Aktiv: PROG-PORT 1 GPIO 13 läuft mit 50Hz.",
          "Der gewählte PORT ist nicht aktiv." },
        { "Aktiv: 50Hz am PROG-PORT 1. Ihm ist kein Pin zugeordnet.",
          "PPM-Ausgänge behalten ihre eigene Pulsrate." },
        { "Aktiv: PROG-PORT 1 GPIO 63 läuft mit 50Hz.",
          "PORT-PWM ist AUS. Der Port gibt keine Pulse aus." },
        { "Aktiv: 50Hz am PROG-PORT 1. Ihm ist kein Pin zugeordnet.",
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
    CHECK_STR_EQ(a, "Active: PROG PORT 1 GPIO 13 runs at 560Hz.");
    CHECK(!servo_in_force_note(a, b, sizeof(a), 560u, 0u, 13u, true, 0));
    CHECK_STR_EQ(a, "Active: OUTPUTS run at 560Hz.");
    /* The port's number is the drive's target, 1 to 4, in either
     * language, with a pin and without one. */
    CHECK(!servo_in_force_note(a, b, sizeof(a), 333u, 3u, 26u, true, 3));
    CHECK_STR_EQ(a, "Active: PROG PORT 3 GPIO 26 runs at 333Hz.");
    CHECK(servo_in_force_note(a, b, sizeof(a), 50u, 4u, SERVO_SCREEN_NO_PIN,
                              true, 4));
    CHECK_STR_EQ(a, "Active: 50Hz on PROG PORT 4. No pin is assigned to it.");
    ui_text_set_language(UI_LANG_DE);
    CHECK(!servo_in_force_note(a, b, sizeof(a), 333u, 2u, 28u, true, 2));
    CHECK_STR_EQ(a, "Aktiv: PROG-PORT 2 GPIO 28 läuft mit 333Hz.");
    CHECK(servo_in_force_note(a, b, sizeof(a), 50u, 4u, SERVO_SCREEN_NO_PIN,
                              true, 4));
    CHECK_STR_EQ(a, "Aktiv: 50Hz am PROG-PORT 4. Ihm ist kein Pin zugeordnet.");
    ui_text_set_language(UI_LANG_EN);
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
    CHECK(note_matches("Active: PROG PORT 1 GPIO 13 runs at 560Hz.", PPM_LINE,
                       false));

    char a[96], b[96];
    CHECK(servo_in_force_note(a, b, sizeof(a), 560u,
                              SERVO_SCREEN_TARGET_UNKNOWN, 13u, true, 1));
    CHECK_STR_EQ(a, "Active rate: 560Hz. Target unknown.");
    CHECK(servo_in_force_note(a, b, sizeof(a), 560u, 1u, SERVO_SCREEN_NO_PIN,
                              true, 1));
    CHECK_STR_EQ(a, "Active: 560Hz on PROG PORT 1. No pin is assigned to it.");

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
            CHECK(note_matches("Active: PROG PORT 1 GPIO 13 runs at 50Hz.",
                               PPM_LINE, false));
        } else {
            CHECK(note_matches("Active: 50Hz on PROG PORT 1. No pin is "
                               "assigned to it.", PPM_LINE, true));
        }
    }
}

/*
 * SAVE follows the store and not the list: a save refused after
 * the change repaints that button alone, NOT SAVED in red.
 */
TEST_CASE(a_refused_save_repaints_only_the_save_button)
{
    fresh();
    two_buffers();
    open_settings();
    sl_step(HL_REPORT, +1);                    /* REPORT: not saved */
    CHECK(settings_dirty());
    CHECK(both_whole());
    gfx_color_t *was = malloc(FB_BYTES);
    REQUIRE_ALLOC(was);
    memcpy(was, fb, FB_BYTES);
    CHECK(!settings_save());                   /* no store: refused */
    scr->tick(1.0f / 39.0f);
    scr->render(&cv, 0);
    scr->render(&cv1, 1);
    int outside = 0, warn = 0;
    const gfx_rect_t save = ui_setlist_strip_rect(2);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            /* The list's SAVE in the footer: NOT SAVED in red. */
            const bool in = gfx_rect_contains(save, x, y);
            if (in && fb[y * W + x] == ui_theme_color(UI_C_DANGER)) {
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
    tap(COL_X + 1, SPEED_Y);                   /* the slowest sweep, 10% */
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

/* SERVO sets no set point and has no OUTPUT ON: SET V, SET A and OUTPUT
 * ON / OFF are SUPPLY's (test_supply_*).  LIVE, SWEEP and RUDDER show the
 * supply as a READY row whose ARM switches it on (test_servo_drive_ready),
 * KST and HITEC as a row whose tap switches it off (test_kst_panel_frame).
 * These cases take LIVE's PD MINI row. */

/** @brief LIVE, a PD mini answering at @p v and 1.0A, its output
 *         @p on. */
static void pd_on_live(float v, bool on)
{
    fresh();
    settings_set(SET_SUPPLY_V_MAX, 8.4f);
    supply_screen_limits_changed();
    supply_screen_put(v, 1.0f);
    supply_screen_set_output(on);
    supply_state_t st;
    memset(&st, 0, sizeof(st));
    st.online = true;
    st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
    st.output = on;
    st.set_v = v;
    st.set_i = 1.0f;
    servo_screen_supply(&st);
    scr->tick(0.025f);
    supply_cmd_t c;
    while (supply_cmd(&c)) { }
}

/* A set point changed on SUPPLY, or a cap that moved it, is drawn on
 * LIVE's PD MINI row on the next frame, without a full repaint. */
TEST_CASE(a_set_point_changed_on_supply_is_redrawn_here)
{
    pd_on_live(5.0f, true);
    scr->render(&cv, 0);
    gfx_color_t *before = malloc((size_t)W * H * sizeof(gfx_color_t));
    REQUIRE_ALLOC(before);
    memcpy(before, fb, (size_t)W * H * sizeof(gfx_color_t));
    supply_screen_put(5.5f, 1.0f);
    scr->tick(0.025f);
    scr->render(&cv, 0);
    int changed = 0;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            if (fb[(size_t)y * W + x] != before[(size_t)y * W + x]) {
                CHECK(y >= SV_DRV_ROW_Y(1) - 15 && y < SV_DRV_ROW_Y(1) + 15);
                ++changed;
            }
        }
    }
    CHECK(changed > 0);
    free(before);
}

/* A set point past 6.0V already in force -- made on SUPPLY, or before
 * this screen was opened -- goes on only through the HV warning: ARM
 * switches nothing on, a tap on the red PD MINI row opens the warning, a
 * tap on APPLY does nothing, STOP ends the hold, CANCEL switches nothing
 * on, and only the two-second hold on APPLY sends the ON. */
TEST_CASE(a_pd_mini_past_6_v_goes_on_only_through_the_hv_hold)
{
    pd_on_live(7.4f, false);
    ev(SV_ARM_X, SV_FOOT_Y, TOUCH_EVENT_DOWN, 1);
    for (int i = 0; i < 100; ++i) {
        scr->tick(1.0f / 40.0f);
    }
    ev(SV_ARM_X, SV_FOOT_Y, TOUCH_EVENT_UP, 1);
    CHECK(!supply_cmd(NULL));                   /* ARM: refused */

    tap(SV_RDY_X, SV_DRV_ROW_Y(1));             /* the PD MINI row */
    CHECK(servo_screen_output_held());          /* the warning */
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

    /* CANCEL switches nothing on. */
    pd_on_live(8.4f, false);
    tap(SV_RDY_X, SV_DRV_ROW_Y(1));
    CHECK(servo_screen_output_held());
    tap(WARN_CANCEL_X, WARN_Y);
    for (int i = 0; i < 100; ++i) {
        scr->tick(1.0f / 40.0f);
    }
    CHECK(!supply_cmd(NULL));
    CHECK(!servo_screen_output_held());
}

/* STOP ends the profile warning's hold as it ends ARM's: the rest of the
 * two seconds applies no profile. */
TEST_CASE(stop_ends_the_warning_holds)
{
    fresh();
    open_settings();
    sl_tap(HL_TYPE);
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
    RUN(a_refused_save_repaints_only_the_save_button);
    RUN(the_output_heading_says_whether_the_rate_reached_the_pins);
    RUN(the_in_force_note_names_what_the_coprocessor_runs_at_the_rate);
    RUN(the_in_force_note_names_the_target_the_coprocessor_holds);
    RUN(the_in_force_note_carries_the_rate_and_names_none_without_a_link);
    RUN(the_in_force_note_follows_the_prog_page_not_the_setting);
    RUN(the_horn_travels_and_breathes_as_a_full_redraw_would);
    RUN(drags_and_samples_leave_both_buffers_as_a_full_redraw_would);
    RUN(a_set_point_changed_on_supply_is_redrawn_here);
    RUN(a_pd_mini_past_6_v_goes_on_only_through_the_hv_hold);
    RUN(stop_ends_the_warning_holds);
    return test_summary("servo_settings");
}
