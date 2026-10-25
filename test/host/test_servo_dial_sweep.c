/**
 * @file test_servo_dial_sweep.c
 * @brief What the servo screen shows and sends while the coprocessor sweeps: the
 * horn, the knob, a changed setting, SPEED's limit, and what it draws and
 * replays across a pause: the acknowledged hold, feedback, the early
 * resume and the link going.
 *
 * Cases:
 * - a_sweep_needs_an_armed_bench_and_a_coprocessor_that_sweeps
 * - the_horn_follows_the_sweep_and_hold_keeps_it_where_it_is
 * - a_sweep_ends_on_the_dial_centre_release_disarm_and_leave
 * - a_changed_setting_starts_the_sweep_over
 * - a_profile_changed_while_it_sweeps_goes_with_the_sweep
 * - hold_keeps_the_output_where_it_has_got_to
 * - speed_says_when_it_limits_the_sweep
 * - pause_holds_the_sweep_and_a_second_tap_resumes_it
 * - paused_carries_the_sweep_on_from_its_phase
 * - the_pause_is_drawn_from_when_the_hold_was_acknowledged
 * - a_pause_with_feedback_holds_the_angle_the_servo_reports
 * - a_pause_replays_from_the_tap_when_feedback_goes
 * - the_acknowledged_pause_moves_the_output_at_speed
 * - an_early_resume_is_rebased_on_the_phase_the_far_end_kept
 * - a_changed_curve_is_drawn_from_its_acknowledged_start
 * - a_change_during_a_pause_drain_holds_the_drawn_angle
 * - an_early_resume_keeps_the_output_the_far_end_held
 * - the_link_going_ends_a_pause_at_rest
 * - an_unacknowledged_pause_is_drawn_on_for_500_ms_at_most
 * - an_acknowledgement_after_the_pause_ended_changes_nothing
 * - a_range_changed_while_it_sweeps_goes_with_its_own_amplitude
 * - a_turn_in_a_frame_where_a_sweep_ran_is_dropped
 * - the_knob_does_not_take_the_horn_from_a_sweep
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_screen_rig.h"

/* SWEEP asks the IOmcu for the TEST page's curve, and only on an armed
 * bench and an IOmcu that sweeps: anything else would refuse it. */
TEST_CASE(a_sweep_needs_an_armed_bench_and_a_coprocessor_that_sweeps)
{
    fresh();
    tap(SWEEP_X, BTN_Y);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
    servo_screen_set_armed(true);
    tap(SWEEP_X, BTN_Y);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
    CHECK(!servo_screen_sweeping());

    servo_screen_set_sweep(true);
    tap(SWEEP_X, BTN_Y);
    const servo_cmd_t c = last_cmd();
    CHECK_EQ(c.kind, SERVO_CMD_SWEEP);
    CHECK_EQ(c.sweep_kind, SWEEP_SINE);         /* CURVE: SINE */
    CHECK_EQ(c.sweep_mhz, 500u);                /* SPEED: 0.5Hz */
    CHECK_EQ(c.sweep_span, 400u);               /* RANGE: 80% of +/-500 */
    CHECK_EQ(c.sweep_dwell_ms, 200u);
    CHECK_EQ(c.min_us, 1000u);
    CHECK_EQ(c.max_us, 2000u);
    CHECK(servo_screen_sweeping());
}

/* The horn follows the curve the IOmcu runs; PAUSE asks the
 * IOmcu to hold the output where it is, and the drawing stops there. */
TEST_CASE(the_horn_follows_the_sweep_and_hold_keeps_it_where_it_is)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    two_buffers();
    sweep_go();
    for (int i = 0; i < 20; ++i) {             /* half a second, 0.5Hz sine */
        scr->tick(1.0f / 39.0f);
        scr->render((i % 2) ? &cv1 : &cv, i % 2);
    }
    CHECK(both_whole());
    const uint16_t at = servo_screen_commanded();
    CHECK(at > 1880u && at <= 1900u);          /* 1500 + 400 at the peak */
    tap(SWEEP_X, BTN_Y);                       /* PAUSE */
    CHECK(!servo_screen_sweeping());
    CHECK_EQ(last_cmd().kind, SERVO_CMD_HOLD);
    const uint16_t held_at = servo_screen_commanded();
    CHECK(held_at > 1880u && held_at <= 1900u);
}

/* A sweep, running or paused, owns the horn: the knob does not take it. */
TEST_CASE(the_knob_does_not_take_the_horn_from_a_sweep)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    sweep_go();
    servo_screen_knob(0.3f);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
    CHECK(servo_screen_sweeping());

    (void)pause_go();
    CHECK(servo_screen_paused());
    servo_screen_knob(0.3f);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
}

/* A sweep that ended before the turn is applied still owned the frame. */
TEST_CASE(a_turn_in_a_frame_where_a_sweep_ran_is_dropped)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    sweep_go();
    servo_screen_knob_frame();          /* the frame starts mid-sweep */
    servo_screen_set_sweep(false);      /* ends it, with a RELEASE */
    CHECK_EQ(last_cmd().kind, SERVO_CMD_RELEASE);
    servo_screen_knob(0.3f);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
    servo_screen_knob_frame();
    servo_screen_knob(0.3f);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_POSITION);
}

/* A finger on the dial, CENTRE, RELEASE, a disarm and leaving each end the
 * sweep, with the command that ends it. */
TEST_CASE(a_sweep_ends_on_the_dial_centre_release_disarm_and_leave)
{
    int x, y;
    dial_at(30.0f, ARC_R - 20, &x, &y);
    const struct { int px, py; servo_cmd_kind_t kind; } by[] = {
        { 0, 0, SERVO_CMD_POSITION },          /* the dial, below */
        { ARM_X + 40, BTN_Y, SERVO_CMD_CENTRE },
        { ARM_X + ARM_W - 40, BTN_Y, SERVO_CMD_RELEASE },
    };
    for (int k = 0; k < 3; ++k) {
        fresh();
        servo_screen_set_armed(true);
        servo_screen_set_sweep(true);
        tap(SWEEP_X, BTN_Y);
        frames(0.2f);
        (void)last_cmd();
        tap(k == 0 ? x : by[k].px, k == 0 ? y : by[k].py);
        CHECK(!servo_screen_sweeping());
        const servo_cmd_t c = last_cmd();
        CHECK_EQ(c.kind, by[k].kind);
        CHECK(c.ends_sweep);                   /* it gives way at once */
        tap(k == 0 ? x : by[k].px, k == 0 ? y : by[k].py);
        CHECK(!last_cmd().ends_sweep);         /* only the one that ended it */
    }

    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    tap(SWEEP_X, BTN_Y);
    servo_screen_set_armed(false);
    CHECK(!servo_screen_sweeping());

    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    tap(SWEEP_X, BTN_Y);
    scr->leave();
    CHECK(!servo_screen_sweeping());
    CHECK_EQ(last_cmd().kind, SERVO_CMD_DISARM);

    /* A touch stream that lost events, the PAUSE perhaps among them: the
     * sweep stops and the output is held where it is. */
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    tap(SWEEP_X, BTN_Y);
    frames(0.2f);
    (void)last_cmd();
    scr->cancel();
    CHECK(!servo_screen_sweeping());
    CHECK_EQ(last_cmd().kind, SERVO_CMD_HOLD);

    /* A hold the panel had to let go of: the horn goes to the centre the
     * surfaces were released to, and nothing is held. */
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    sweep_go();
    frames(0.5f);
    (void)pause_go();
    CHECK(servo_screen_commanded() > 1800u);
    servo_screen_released(g_hold_seq);
    CHECK_EQ(servo_screen_commanded(), 1500u);
    (void)last_cmd();
    choose_type(1);                            /* not driving: a rest */
    CHECK_EQ(last_cmd().kind, SERVO_CMD_RELEASE);

    /* And an IOmcu that cannot sweep any more: what the panel holds is
     * ended with a release. */
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    tap(SWEEP_X, BTN_Y);
    (void)last_cmd();
    servo_screen_set_sweep(false);
    CHECK(!servo_screen_sweeping());
    CHECK_EQ(last_cmd().kind, SERVO_CMD_RELEASE);
}

/* A setting changed while it runs starts the sweep over with it, as the
 * IOmcu's does when it is written. */
TEST_CASE(a_changed_setting_starts_the_sweep_over)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    tap(SWEEP_X, BTN_Y);
    frames(0.3f);
    (void)last_cmd();
    settings_set(SET_SERVO_TEST_RANGE, 50.0f);
    frames(0.05f);
    servo_cmd_t c = last_cmd();
    CHECK_EQ(c.kind, SERVO_CMD_SWEEP);
    CHECK_EQ(c.sweep_span, 250u);
    /* TRAVEL limits it too: half the travel is half the sweep. */
    open_settings();
    tap(ROW_L_X, ROW_Y(4));                    /* TRAVEL */
    keys("45");
    close_settings();
    frames(0.05f);
    c = last_cmd();
    CHECK_EQ(c.kind, SERVO_CMD_SWEEP);
    CHECK_EQ(c.sweep_span, 125u);
}

/* A profile changed while it sweeps is said again with the sweep: NARROW
 * 760's range and rate, though its curve in command units is the same. */
TEST_CASE(a_profile_changed_while_it_sweeps_goes_with_the_sweep)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    tap(SWEEP_X, BTN_Y);
    frames(0.2f);
    (void)last_cmd();
    choose_type(1);                            /* NARROW 760 */
    const servo_cmd_t c = last_cmd();
    CHECK_EQ(c.kind, SERVO_CMD_SWEEP);
    CHECK_EQ(c.min_us, 660u);
    CHECK_EQ(c.max_us, 860u);
    CHECK_EQ(c.sweep_span, 400u);
    CHECK(servo_screen_sweeping());
}

/* A range changed while it sweeps goes out with the amplitude it gives,
 * not the one before it: half the travel is half the sweep at once. */
TEST_CASE(a_range_changed_while_it_sweeps_goes_with_its_own_amplitude)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    tap(SWEEP_X, BTN_Y);
    frames(0.2f);
    (void)last_cmd();
    open_settings();
    tap(ROW_L_X, ROW_Y(4));                    /* TRAVEL */
    keys("45");
    const servo_cmd_t c = last_cmd();          /* before any tick */
    CHECK_EQ(c.kind, SERVO_CMD_SWEEP);
    CHECK_EQ(c.sweep_span, 200u);
    CHECK(servo_screen_sweeping());
}

/* PAUSE keeps the horn where the output has got to, which a slow SPEED
 * leaves well behind the curve. */
TEST_CASE(hold_keeps_the_output_where_it_has_got_to)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    tap(ARM_X + 1, SPEED_Y);                   /* SPEED 12%: 43deg/s */
    sweep_go();
    frames(0.3f);                              /* the curve is near 58deg */
    const servo_cmd_t hold = pause_go();
    ack_hold(&hold);                           /* acknowledged at once */
    const uint16_t at = servo_screen_commanded();   /* drawn where it got to */
    CHECK(at > 1500u && at < 1600u);           /* about 13deg */
}

/* Pixels of @p col on SPEED's row of the output card. */
static int speed_row_pixels(gfx_color_t col)
{
    int n = 0;
    for (int y = SV_SPEED_ROW_Y; y < SV_SPEED_ROW_Y + 20; ++y) {
        for (int x = ARM_X; x < ARM_X + ARM_W; ++x) {
            n += (fb[(size_t)y * W + x] == col) ? 1 : 0;
        }
    }
    return n;
}

/*
 * SPEED's row says when SPEED rather than the curve shapes the sweep, in the
 * warning colour, from the settings: before SWEEP is pressed and on a bench
 * that is not armed.  The TEST page's sine, 0.5Hz over 400 units, asks for
 * 1257 units a second; SPEED at the left end of its track allows at most
 * 240.  The rate typed on the TEST page clears it while the page is open:
 * 0.05Hz asks for 126.
 */
TEST_CASE(speed_says_when_it_limits_the_sweep)
{
    fresh();
    scr->render(&cv, 0);
    CHECK_EQ(speed_row_pixels(ui_theme_color(UI_C_WARN)), 0);   /* 100% */

    tap(ARM_X + 1, SPEED_Y);                   /* SPEED's slowest */
    scr->render(&cv, 0);
    CHECK(speed_row_pixels(ui_theme_color(UI_C_WARN)) > 100);
    CHECK_EQ(speed_row_pixels(ui_theme_color(UI_C_DANGER)), 0);

    open_settings();
    tap(TAB_X(PAGE_SWEEP), TAB_Y);             /* SWEEP */
    tap(ROW_L_X, ROW_Y(1));                    /* SPEED, the rate */
    keys("0.05");
    scr->render(&cv, 0);
    CHECK_EQ(speed_row_pixels(ui_theme_color(UI_C_WARN)), 0);

    tap(ROW_L_X, ROW_Y(1));
    keys("0.5");
    scr->render(&cv, 0);
    CHECK(speed_row_pixels(ui_theme_color(UI_C_WARN)) > 100);
    close_settings();

    tap(ARM_X + ARM_W - 1, SPEED_Y);           /* SPEED 100%: no slew */
    scr->render(&cv, 0);
    CHECK_EQ(speed_row_pixels(ui_theme_color(UI_C_WARN)), 0);
}

/*
 * PAUSE pauses a running sweep with the link's HOLD and reads PAUSED, filled
 * in the warning colour rather than the accent; a second tap starts the
 * curve again, from its beginning, as the IOmcu does.  In both themes.
 */
TEST_CASE(pause_holds_the_sweep_and_a_second_tap_resumes_it)
{
    const ui_theme_id_t themes[] = { UI_THEME_DARK, UI_THEME_LIGHT };
    for (unsigned k = 0; k < 2u; ++k) {
        fresh();
        ui_theme_set(themes[k]);
        servo_screen_set_armed(true);
        servo_screen_set_sweep(true);
        scr->render(&cv, 0);
        CHECK_EQ(sweep_btn_pixels(ui_theme_color(UI_C_ACCENT)), 0);
        CHECK_EQ(sweep_btn_pixels(ui_theme_color(UI_C_WARN)), 0);

        tap(SWEEP_X, BTN_Y);                   /* SWEEP */
        CHECK_EQ(last_cmd().kind, SERVO_CMD_SWEEP);
        frames(0.3f);
        scr->render(&cv, 0);
        CHECK(sweep_btn_pixels(ui_theme_color(UI_C_ACCENT)) > 1000);
        CHECK_EQ(sweep_btn_pixels(ui_theme_color(UI_C_WARN)), 0);

        tap(SWEEP_X, BTN_Y);                   /* PAUSE */
        CHECK_EQ(last_cmd().kind, SERVO_CMD_HOLD);
        CHECK(servo_screen_paused());
        CHECK(!servo_screen_sweeping());
        const uint16_t at = servo_screen_commanded();
        frames(1.0f);                          /* the drawing stays there */
        CHECK_EQ(servo_screen_commanded(), at);
        scr->render(&cv, 0);
        CHECK(sweep_btn_pixels(ui_theme_color(UI_C_WARN)) > 1000);
        CHECK_EQ(sweep_btn_pixels(ui_theme_color(UI_C_ACCENT)), 0);

        /* SPEED changed while paused keeps the pause, and the resume
         * carries the new rate. */
        tap(SPEED_X_HALF, SPEED_Y);
        CHECK(servo_screen_paused());
        CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);

        tap(SWEEP_X, BTN_Y);                   /* PAUSE again: on */
        const servo_cmd_t c = last_cmd();
        CHECK_EQ(c.kind, SERVO_CMD_SWEEP);
        CHECK(c.slew_per_s > 0);
        CHECK_EQ(c.sweep_kind, SWEEP_SINE);
        CHECK(c.resume);
        CHECK(!c.ends_sweep);
        CHECK(servo_screen_sweeping());
        CHECK(!servo_screen_paused());
        scr->render(&cv, 0);
        CHECK(sweep_btn_pixels(ui_theme_color(UI_C_ACCENT)) > 1000);
        CHECK_EQ(sweep_btn_pixels(ui_theme_color(UI_C_WARN)), 0);
    }
    ui_theme_set(UI_THEME_DARK);
}

/*
 * The horn goes on from the phase it was paused at, however long the pause:
 * the 0.5Hz sine paused a quarter of a second in is at its peak a quarter
 * of a second after PAUSED.  An IOmcu that started the curve over
 * instead (older than 4.6, or refusing) is followed: drawn from the curve's
 * beginning.  One that resumed later than the tap is followed too.
 */
TEST_CASE(paused_carries_the_sweep_on_from_its_phase)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    tap(SWEEP_X, BTN_Y);                       /* SWEEP */
    CHECK(!last_cmd().resume);
    acked(0u, SERVO_SWEEP_FROM_REST, 0u);
    frames(0.25f);
    const servo_cmd_t hold = pause_go();
    const uint32_t kept = servo_screen_curve_ms();
    ack_hold(&hold);
    frames(2.0f);
    tap(SWEEP_X, BTN_Y);                       /* PAUSED: on */
    CHECK(last_cmd().resume);
    const uint16_t held = servo_screen_commanded();
    frames(0.1f);                              /* not acknowledged yet */
    CHECK_EQ(servo_screen_commanded(), held);
    ack_resume(kept, 0u);
    frames(0.25f);
    CHECK(servo_screen_commanded() > 1880u);   /* the peak, 0.5s in */

    /* Started over at the far end: drawn from the beginning. */
    acked(0u, SERVO_SWEEP_FROM_REST, 0u);
    frames(0.25f);
    CHECK(servo_screen_commanded() > 1760u && servo_screen_commanded() < 1800u);
    (void)pause_go();

    /* Resumed 1450ms before the panel heard: about 1960ms into the
     * cycle, in the 200ms dwell at the low end, 1100us. */
    tap(SWEEP_X, BTN_Y);
    (void)last_cmd();
    ack_resume(servo_screen_curve_ms(), 1450u);
    frames(0.25f);
    CHECK(servo_screen_commanded() >= 1100u && servo_screen_commanded() < 1120u);

    /* A repeat before the panel took it is still a resume. */
    tap(SWEEP_X, BTN_Y);                       /* PAUSE */
    (void)last_cmd();
    tap(SWEEP_X, BTN_Y);                       /* PAUSED */
    choose_type(0);                            /* says the sweep again */
    CHECK(last_cmd().resume);
}

/*
 * The far end runs its curve on until the HOLD reaches it.  With 300ms
 * between the tap and the acknowledgement, the phase it keeps is 400ms
 * in, not the tap's 100ms, and the resumed horn is drawn from there: 50ms
 * later 450ms into the 0.5Hz sine, near its peak, where the tap's phase
 * would draw it 150ms in, below 1700us.
 *
 * Without feedback the drawn output goes on along the curve while the HOLD
 * is on its way, as the far end does, and the acknowledgement puts it where
 * the far end held it: from about 1620us at the tap to the curve's
 * 1880us, which SPEED 100% follows at once, in both buffers.  The resume
 * goes on from there, not from the tap's angle.
 */
TEST_CASE(the_pause_is_drawn_from_when_the_hold_was_acknowledged)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    two_buffers();
    for (int i = 0; i < 40; ++i) {             /* ARM's flash runs out */
        scr->tick(1.0f / 39.0f);
        scr->render((i % 2) ? &cv1 : &cv, i % 2);
    }
    sweep_go();
    frames(0.1f);
    const servo_cmd_t hold = pause_go();
    const uint16_t at_tap = servo_screen_drawn();
    CHECK(at_tap > 1590u && at_tap < 1650u);
    frames(0.3f);                              /* the HOLD on its way */
    CHECK(servo_screen_drawn() > at_tap + 200u);   /* drawn on, as it runs */
    scr->render(&cv, 0);
    scr->render(&cv1, 1);
    const uint32_t kept = servo_screen_curve_ms();
    ack_hold(&hold);                           /* acknowledged now */
    CHECK(both_whole());
    const uint16_t at_ack = servo_screen_drawn();
    CHECK(at_ack > 1860u && at_ack < 1900u);
    CHECK_EQ(servo_screen_commanded(), at_ack);
    frames(1.0f);
    CHECK_EQ(servo_screen_drawn(), at_ack);
    tap(SWEEP_X, BTN_Y);                       /* PAUSED: on */
    (void)last_cmd();
    ack_resume(kept, 0u);
    frames(0.05f);
    CHECK(servo_screen_commanded() > 1880u);
    CHECK(servo_screen_drawn() >= at_ack);     /* on from there */

    /* An acknowledgement with nothing paused here changes nothing, nor
     * does an earlier pause's during a later one: paused about 450ms in,
     * near the peak, it resumes there and reaches the dwell at the peak. */
    servo_screen_sweep_held(hold.pause_seq, 0u);
    CHECK(servo_screen_sweeping());
    tap(SWEEP_X, BTN_Y);                       /* PAUSE */
    const servo_cmd_t again = last_cmd();
    CHECK(again.pause_seq != hold.pause_seq);
    const uint16_t paused_at = servo_screen_drawn();
    servo_screen_sweep_held(hold.pause_seq, 100000u);
    CHECK_EQ(servo_screen_drawn(), paused_at);
    frames(0.5f);
    tap(SWEEP_X, BTN_Y);                       /* PAUSED: on */
    (void)last_cmd();
    ack_resume(servo_screen_curve_ms(), 0u);
    frames(0.05f);
    CHECK(servo_screen_commanded() > 1880u);
}

/* A sweep at SPEED's slowest, paused 300ms in and acknowledged 307ms
 * later, SPEED raised to 100% in between when @p raise; the drawn output
 * at the tap into @p at_tap, and returned as it is at the acknowledgement. */
static uint16_t slow_pause_acknowledged(bool raise, uint16_t *at_tap)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    tap(ARM_X + 1, SPEED_Y);                   /* SPEED's slowest */
    sweep_go();
    frames(0.3f);
    const servo_cmd_t hold = pause_go();
    *at_tap = servo_screen_drawn();
    frames(0.1f);
    if (raise) {
        tap(ARM_X + ARM_W - 1, SPEED_Y);       /* 100%: nothing sent */
        CHECK(servo_screen_paused());
        CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
    }
    frames(0.2f);
    ack_hold(&hold);
    return servo_screen_drawn();
}

/*
 * With feedback, the servo goes on moving after the tap -- while the HOLD
 * is on its way and while it settles -- and the paused angle follows what
 * it reports: a trim changed while paused says the angle it is at, 5us
 * up, not the one it had at the tap.
 */
TEST_CASE(a_pause_with_feedback_holds_the_angle_the_servo_reports)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    servo_screen_feedback(1500u, 0.2f, true);
    sweep_go();
    frames(0.2f);
    servo_screen_feedback(1600u, 0.2f, true);
    frames(0.05f);
    tap(SWEEP_X, BTN_Y);                       /* PAUSE */
    const servo_cmd_t hold = last_cmd();
    CHECK_EQ(hold.kind, SERVO_CMD_HOLD);
    CHECK_EQ(servo_screen_drawn(), 1600u);
    servo_screen_feedback(1700u, 0.2f, true);  /* moved on after the tap */
    frames(0.1f);
    ack_hold(&hold);
    CHECK_EQ(servo_screen_commanded(), 1700u);
    CHECK_EQ(servo_screen_drawn(), 1700u);
    servo_screen_feedback(1720u, 0.2f, true);  /* and settled */
    CHECK_EQ(servo_screen_commanded(), 1720u);
    CHECK(servo_screen_paused());

    open_settings();
    tap(TRIM_UP_X, ROW_Y(3));
    const servo_cmd_t c = last_cmd();
    CHECK_EQ(c.kind, SERVO_CMD_POSITION);
    CHECK_EQ(c.value_us, 1725u);
    CHECK(!servo_screen_paused());
}

/*
 * Feedback lost between the tap and the acknowledgement: the replay of the
 * interval starts from where the output was drawn at the tap, 1600us, and
 * moves at most SPEED's 74us, not from a reading that came after it.
 */
TEST_CASE(a_pause_replays_from_the_tap_when_feedback_goes)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    tap(ARM_X + 1, SPEED_Y);                   /* SPEED's slowest */
    servo_screen_feedback(1500u, 0.2f, true);
    sweep_go();
    frames(0.25f);
    servo_screen_feedback(1600u, 0.2f, true);
    /* tapped before a frame has passed: the reading is the start */
    tap(SWEEP_X, BTN_Y);                       /* PAUSE */
    const servo_cmd_t hold = last_cmd();
    servo_screen_feedback(1700u, 0.2f, true);
    frames(0.05f);
    servo_screen_feedback(0u, 0.0f, false);    /* gone */
    frames(0.25f);
    ack_hold(&hold);
    const uint16_t at_ack = servo_screen_drawn();
    CHECK(at_ack > 1600u && at_ack <= 1674u);
    CHECK_EQ(servo_screen_commanded(), at_ack);
    frames(0.5f);                              /* held, and drawn held */
    CHECK_EQ(servo_screen_drawn(), at_ack);
}

/*
 * At SPEED's slowest the drawn output lags the curve, and moves on by no
 * more than SPEED allows over the 307ms (12 frames) the HOLD took: at most
 * 240 units a second, 74us on a 1000 to 2000us range.
 */
TEST_CASE(the_acknowledged_pause_moves_the_output_at_speed)
{
    uint16_t at_tap = 0u;
    const uint16_t at_ack = slow_pause_acknowledged(false, &at_tap);
    CHECK(at_ack > at_tap);
    CHECK(at_ack <= at_tap + 74u);
    CHECK(at_ack < 1800u);                     /* behind the curve's 1880 */

    /*
     * SPEED raised after the tap sends nothing while paused, so the far end
     * slews at the old rate until the HOLD reaches it: the output is worked
     * on at the SPEED of the tap, to the same place.
     */
    uint16_t raised_tap = 0u;
    CHECK_EQ(slow_pause_acknowledged(true, &raised_tap), at_ack);
    CHECK_EQ(raised_tap, at_tap);
}

/*
 * PAUSED tapped before the HOLD's acknowledgement reaches the screen: the
 * screen resumes from the tap's phase, 100ms in, but the far end kept the
 * phase at its acknowledgement, 410ms in.  The acknowledgement then finds
 * no pause and changes nothing; the resume's acknowledgement, which the
 * panel times from the far end's kept phase, rebases the curve: 410ms on
 * it is 820ms in, 1872us, where a rebase on the tap's phase plus the
 * kept one would put it 922ms in, near 1806us, and one on the tap's alone
 * 512ms in, at the peak.  The same with both acknowledgements in one
 * frame, the HOLD's first.
 */
TEST_CASE(an_early_resume_is_rebased_on_the_phase_the_far_end_kept)
{
    for (int same_frame = 0; same_frame < 2; ++same_frame) {
        fresh();
        servo_screen_set_armed(true);
        servo_screen_set_sweep(true);
        sweep_go();
        frames(0.1f);
        const servo_cmd_t hold = pause_go();
        frames(0.3f);
        const uint32_t kept = servo_screen_curve_ms();  /* the far end's */
        if (!same_frame) {
            tap(SWEEP_X, BTN_Y);               /* PAUSED, before the ack */
            CHECK(last_cmd().resume);
            ack_hold(&hold);                   /* too late: no pause */
            CHECK(servo_screen_sweeping());
        } else {
            tap(SWEEP_X, BTN_Y);
            CHECK(last_cmd().resume);
            servo_screen_sweep_held(hold.pause_seq, kept);
        }
        ack_resume(kept, 0u);
        frames(0.4f);                          /* 820ms in: 1872us */
        CHECK(servo_screen_commanded() > 1850u && servo_screen_commanded() < 1890u);
    }
}

/*
 * A disarm, STOP or leaving between PAUSE and its acknowledgement ends the
 * pause; the acknowledgement that follows changes nothing.
 */
TEST_CASE(an_acknowledgement_after_the_pause_ended_changes_nothing)
{
    for (int how = 0; how < 3; ++how) {
        fresh();
        servo_screen_set_armed(true);
        servo_screen_set_sweep(true);
        sweep_go();
        frames(0.2f);
        const servo_cmd_t hold = pause_go();
        if (how == 0) {
            servo_screen_set_armed(false);
        } else if (how == 1) {
            servo_screen_cancel_arm();         /* STOP */
        } else {
            scr->leave();
        }
        CHECK(!servo_screen_paused());
        const uint16_t was = servo_screen_commanded();
        ack_hold(&hold);
        CHECK(!servo_screen_paused());
        CHECK_EQ(servo_screen_commanded(), was);
    }
}

/*
 * A HOLD never acknowledged: the curve is drawn on for OUT_DEFAULT_TIMEOUT_MS
 * after the tap, the longest the far end can run unwritten, and the horn
 * then stands where it got to.
 */
TEST_CASE(an_unacknowledged_pause_is_drawn_on_for_500_ms_at_most)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    sweep_go();
    frames(0.1f);
    (void)pause_go();
    const uint16_t at_tap = servo_screen_drawn();
    frames(0.3f);
    CHECK(servo_screen_drawn() > at_tap + 200u);   /* on, as the far end */
    frames(0.3f);
    const uint16_t stood = servo_screen_drawn();
    CHECK_EQ(servo_screen_commanded(), stood);
    frames(1.0f);
    CHECK_EQ(servo_screen_drawn(), stood);
    CHECK(servo_screen_paused());
}

/*
 * A curve changed while the sweep runs: the far end runs the old one until
 * the new start reaches it, so the old curve is drawn on until that start
 * is acknowledged -- near the old 0.5Hz sine's peak 0.45s in -- and the
 * new one from there, from its beginning.
 */
TEST_CASE(a_changed_curve_is_drawn_from_its_acknowledged_start)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    sweep_go();
    frames(0.25f);
    settings_set(SET_SERVO_TEST_HZ, 1.0f);
    frames(0.025f);                            /* tick() sees it */
    const servo_cmd_t c = last_cmd();
    CHECK_EQ(c.kind, SERVO_CMD_SWEEP);
    CHECK_EQ(c.sweep_mhz, 1000u);
    frames(0.2f);
    CHECK(servo_screen_commanded() > 1880u);   /* still the old curve */
    acked(0u, SERVO_SWEEP_FROM_HERE, 0u);
    scr->tick(0.001f);
    CHECK(servo_screen_commanded() < 1520u);   /* the new one, at 0 */
}

/*
 * A trim changed while a PAUSE is drawn on -- its HOLD not yet acknowledged
 * -- says the angle the horn is at, plus the trim, never a point the curve
 * was still heading for, and the curve is no longer drawn on: a second
 * change says the same angle again.  Without feedback the angle is the
 * drawn estimate, with it the reading.
 */
TEST_CASE(a_change_during_a_pause_drain_holds_the_drawn_angle)
{
    for (int fb_pass = 0; fb_pass < 2; ++fb_pass) {
        fresh();
        servo_screen_set_armed(true);
        servo_screen_set_sweep(true);
        tap(ARM_X + 1, SPEED_Y);               /* SPEED's slowest */
        if (fb_pass) {
            servo_screen_feedback(1500u, 0.2f, true);
        }
        sweep_go();
        frames(0.3f);
        if (fb_pass) {
            servo_screen_feedback(1600u, 0.2f, true);
        }
        (void)pause_go();
        frames(0.15f);                         /* drawn on, not acked */
        if (fb_pass) {
            servo_screen_feedback(1620u, 0.2f, true);
        }
        const uint16_t at = servo_screen_drawn();
        if (fb_pass) {
            CHECK_EQ(at, 1620u);
        }
        open_settings();
        tap(TRIM_UP_X, ROW_Y(3));
        servo_cmd_t c = last_cmd();
        CHECK_EQ(c.kind, SERVO_CMD_POSITION);
        CHECK_EQ(c.value_us, at + 5u);
        frames(0.3f);
        tap(TRIM_UP_X, ROW_Y(3));
        c = last_cmd();
        CHECK_EQ(c.kind, SERVO_CMD_POSITION);
        CHECK_EQ(c.value_us, at + 10u);
        close_settings();
    }
}

/*
 * PAUSED tapped 0.1s after PAUSE, the HOLD acknowledged 0.2s after that:
 * the far end ran on to the acknowledgement, 307ms past the tap, and holds
 * there until the RESUME arrives.  The late acknowledgement moves the drawn
 * output to that held point -- near the 0.5Hz sine's 1884us, not the
 * 1740us the drawing had stopped at -- and the resume goes on from it.
 */
TEST_CASE(an_early_resume_keeps_the_output_the_far_end_held)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    sweep_go();
    frames(0.1f);
    const uint32_t tap_phase = servo_screen_curve_ms();
    const servo_cmd_t hold = pause_go();
    frames(0.1f);                              /* drawn on */
    tap(SWEEP_X, BTN_Y);                       /* PAUSED, before the ack */
    CHECK(last_cmd().resume);
    frames(0.2f);
    CHECK(servo_screen_drawn() < 1780u);       /* stopped at the tap */
    servo_screen_sweep_held(hold.pause_seq, tap_phase + 307u);
    const uint16_t held = servo_screen_drawn();
    CHECK(held > 1870u && held < 1900u);
    ack_resume(tap_phase + 307u, 0u);
    CHECK(servo_screen_drawn() >= held - 2u);
    frames(0.05f);
    CHECK(servo_screen_drawn() > 1880u);
}

/*
 * The link going ends a sweep and a pause: the far end lets them go 500ms
 * after its last word and rests the surfaces, and so does the drawing.  The
 * next tap starts a sweep; nothing is resumed.
 */
TEST_CASE(the_link_going_ends_a_pause_at_rest)
{
    fresh();
    servo_screen_set_link(true);
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    sweep_go();
    frames(0.4f);
    const servo_cmd_t hold = pause_go();
    ack_hold(&hold);
    CHECK(servo_screen_paused());
    CHECK(servo_screen_drawn() > 1800u);
    servo_screen_set_link(false);
    CHECK(!servo_screen_paused());
    CHECK(!servo_screen_sweeping());
    CHECK_EQ(servo_screen_drawn(), 1500u);
    frames(0.5f);
    CHECK_EQ(servo_screen_drawn(), 1500u);
    servo_screen_set_link(true);
    tap(SWEEP_X, BTN_Y);
    const servo_cmd_t c = last_cmd();
    CHECK_EQ(c.kind, SERVO_CMD_SWEEP);
    CHECK(!c.resume);
}

int main(void)
{
    RUN(a_sweep_needs_an_armed_bench_and_a_coprocessor_that_sweeps);
    RUN(the_horn_follows_the_sweep_and_hold_keeps_it_where_it_is);
    RUN(a_sweep_ends_on_the_dial_centre_release_disarm_and_leave);
    RUN(a_changed_setting_starts_the_sweep_over);
    RUN(a_profile_changed_while_it_sweeps_goes_with_the_sweep);
    RUN(hold_keeps_the_output_where_it_has_got_to);
    RUN(speed_says_when_it_limits_the_sweep);
    RUN(pause_holds_the_sweep_and_a_second_tap_resumes_it);
    RUN(paused_carries_the_sweep_on_from_its_phase);
    RUN(the_pause_is_drawn_from_when_the_hold_was_acknowledged);
    RUN(a_pause_with_feedback_holds_the_angle_the_servo_reports);
    RUN(a_pause_replays_from_the_tap_when_feedback_goes);
    RUN(the_acknowledged_pause_moves_the_output_at_speed);
    RUN(an_early_resume_is_rebased_on_the_phase_the_far_end_kept);
    RUN(a_changed_curve_is_drawn_from_its_acknowledged_start);
    RUN(a_change_during_a_pause_drain_holds_the_drawn_angle);
    RUN(an_early_resume_keeps_the_output_the_far_end_held);
    RUN(the_link_going_ends_a_pause_at_rest);
    RUN(an_unacknowledged_pause_is_drawn_on_for_500_ms_at_most);
    RUN(an_acknowledgement_after_the_pause_ended_changes_nothing);
    RUN(a_range_changed_while_it_sweeps_goes_with_its_own_amplitude);
    RUN(a_turn_in_a_frame_where_a_sweep_ran_is_dropped);
    RUN(the_knob_does_not_take_the_horn_from_a_sweep);
    return test_summary("servo_dial_sweep");
}
