/**
 * @file test_servo_curve.c
 * @brief What the servo screen draws from a sweep's acknowledgements: the origin of
 * a resume, a restart, a changed curve, hold and the far end's slew.
 *
 * Cases:
 * - leaving_disarms_and_lets_go_of_the_output
 * - trim_shifts_the_pulse_and_not_the_angle
 * - feedback_is_shown_rather_than_travelled_to
 * - a_refused_sweep_leaves_nothing_driven
 * - an_early_resume_keeps_the_reading_at_the_hold
 * - a_released_pause_says_no_drawn_angle
 * - leaving_forgets_the_sweep_model
 * - a_start_acknowledgement_applies_to_its_own_command
 * - a_later_repeat_acknowledged_ends_the_wait
 * - a_restart_drains_the_old_curve_in_its_old_mapping
 * - two_sweep_taps_in_one_pass_send_nothing
 * - speed_100_is_immediate_in_the_drawing_and_the_replay
 * - a_resume_is_drawn_from_its_acknowledgement
 * - a_curve_changed_while_paused_starts_over
 * - a_pause_ends_where_a_hold_ended
 * - hold_without_feedback_matches_the_far_ends_slew
 * - the_drawn_output_starts_where_the_far_ends_output_starts
 * - the_horn_follows_the_curve_from_where_the_far_end_started_it
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_screen_rig.h"

/* A refused sweep drives nothing: a change of profile after it says no
 * position. */
TEST_CASE(a_refused_sweep_leaves_nothing_driven)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    tap(SWEEP_X, BTN_Y);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_SWEEP);
    servo_screen_sweep_refused();
    open_settings();
    tap(TRIM_UP_X, ROW_Y(3));
    CHECK(last_cmd().kind != SERVO_CMD_POSITION);
    close_settings();
}

/*
 * With feedback, a HOLD acknowledged after an early PAUSED keeps the
 * reading for the resume: feedback gone before the resume's
 * acknowledgement, the resume goes on from 1700 us, not the tap's 1600.
 */
TEST_CASE(an_early_resume_keeps_the_reading_at_the_hold)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    servo_screen_feedback(1500u, 0.2f, true);
    sweep_go();
    frames(0.2f);
    servo_screen_feedback(1600u, 0.2f, true);
    frames(0.05f);
    const uint32_t tap_phase = servo_screen_curve_ms();
    const servo_cmd_t hold = pause_go();
    tap(SWEEP_X, BTN_Y);                       /* PAUSED, early */
    CHECK(last_cmd().resume);
    servo_screen_feedback(1700u, 0.2f, true);
    frames(0.05f);
    servo_screen_sweep_held(hold.pause_seq, tap_phase + 100u);
    servo_screen_feedback(0u, 0.0f, false);    /* gone */
    ack_resume(tap_phase + 100u, 0u);
    CHECK(abs((int)servo_screen_drawn() - 1700) <= 2);
}

/*
 * A HOLD the panel had to let go of -- answered only at a retry, or after
 * longer than a hold lives -- while the curve was drawn on: the pause ends,
 * and a profile change after it says no position from the drawing, only
 * the rest the surfaces were released to.
 */
TEST_CASE(a_released_pause_says_no_drawn_angle)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    tap(ARM_X + 1, SPEED_Y);                   /* SPEED's slowest */
    sweep_go();
    frames(0.3f);
    (void)pause_go();
    frames(0.2f);                              /* drawn on, not acked */
    servo_screen_released(g_hold_seq);
    CHECK(!servo_screen_paused());
    open_settings();
    tap(TRIM_UP_X, ROW_Y(3));
    CHECK_EQ(last_cmd().kind, SERVO_CMD_RELEASE);
    close_settings();
    frames(0.6f);                              /* no drain left */
    open_settings();
    tap(TRIM_DN_X, ROW_Y(3));
    CHECK_EQ(last_cmd().kind, SERVO_CMD_RELEASE);
}

/*
 * Leaving while a PAUSE is drawn on, and opening SERVO again: no curve,
 * drain or acknowledgement of the old sweep is drawn, and a new sweep
 * starts from its own beginning.
 */
TEST_CASE(leaving_forgets_the_sweep_model)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    sweep_go();
    const uint16_t old_seq = g_sweep_seq;
    frames(0.2f);
    const servo_cmd_t hold = pause_go();       /* drawn on, not acked */
    scr->leave();
    CHECK_EQ(last_cmd().kind, SERVO_CMD_DISARM);
    servo_invalidate();                        /* opened again */
    const uint16_t at = servo_screen_drawn();
    frames(0.3f);
    CHECK_EQ(servo_screen_drawn(), at);        /* nothing stale drawn */
    CHECK(!servo_screen_sweeping());
    CHECK(!servo_screen_paused());
    ack_hold(&hold);                           /* late: changes nothing */
    servo_screen_sweep_started(old_seq, 0u, SERVO_SWEEP_FROM_REST, 0u);
    CHECK_EQ(servo_screen_drawn(), at);

    servo_screen_set_armed(false);             /* the disarm, answered */
    servo_screen_set_armed(true);
    sweep_go();
    frames(0.25f);
    CHECK(servo_screen_commanded() > 1760u && servo_screen_commanded() < 1800u);
}

/*
 * While a start waits for its acknowledgement, a SPEED change and a curve
 * change replace what the screen would draw, not what was sent.  A SPEED
 * change is a repeat of the same curve: the start's own acknowledgement,
 * 307 ms late, is worked through at the start's SPEED, 12 %, not the new
 * 100 %.  A curve change is a new start: the first start's acknowledgement
 * does not end the wait, the new one's does, with its own curve.
 */
TEST_CASE(a_start_acknowledgement_applies_to_its_own_command)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    tap(ARM_X + 1, SPEED_Y);                   /* SPEED's slowest */
    tap(SWEEP_X, BTN_Y);
    const servo_cmd_t start = last_cmd();
    CHECK_EQ(start.kind, SERVO_CMD_SWEEP);
    tap(ARM_X + ARM_W - 1, SPEED_Y);           /* SPEED 100 % */
    const servo_cmd_t repeat = last_cmd();
    CHECK_EQ(repeat.kind, SERVO_CMD_SWEEP);
    CHECK(repeat.start_seq != start.start_seq);
    frames(0.3f);
    CHECK_EQ(servo_screen_drawn(), 1500u);     /* not before it is known */
    servo_screen_sweep_started(start.start_seq, 307u, SERVO_SWEEP_FROM_REST,
                               0u);
    const uint16_t at = servo_screen_drawn();
    CHECK(at > 1500u && at <= 1575u);          /* 307 ms at 240 a second */

    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    tap(SWEEP_X, BTN_Y);
    const servo_cmd_t first = last_cmd();
    settings_set(SET_SERVO_TEST_HZ, 1.0f);
    frames(0.025f);                            /* tick() restarts it */
    const servo_cmd_t second = last_cmd();
    CHECK_EQ(second.kind, SERVO_CMD_SWEEP);
    CHECK_EQ(second.sweep_mhz, 1000u);
    servo_screen_sweep_started(first.start_seq, 0u, SERVO_SWEEP_FROM_REST, 0u);
    frames(0.2f);
    CHECK_EQ(servo_screen_commanded(), 1500u); /* still waiting */
    servo_screen_sweep_started(second.start_seq, 0u, SERVO_SWEEP_FROM_REST,
                               0u);
    frames(0.125f);                            /* 1 Hz, 128 ms in */
    CHECK(servo_screen_commanded() > 1760u);
}

/*
 * The start's write failed and a repeat -- SPEED moved -- started the sweep
 * at the far end instead: only the repeat is acknowledged.  The wait ends
 * with it, at the repeat's SPEED, 100 %: 307 ms into the 0.5 Hz sine,
 * near 1829 us.  The same for a resume whose RESUME failed: the repeat
 * that followed it started the curve from rest.  An acknowledgement of a
 * command older than the start, or newer than any posted, is not taken.
 */
TEST_CASE(a_later_repeat_acknowledged_ends_the_wait)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    tap(ARM_X + 1, SPEED_Y);                   /* SPEED's slowest */
    tap(SWEEP_X, BTN_Y);
    const servo_cmd_t start = last_cmd();
    tap(ARM_X + ARM_W - 1, SPEED_Y);           /* SPEED 100 % */
    const servo_cmd_t repeat = last_cmd();
    CHECK_EQ(repeat.kind, SERVO_CMD_SWEEP);
    frames(0.3f);
    servo_screen_sweep_started((uint16_t)(start.start_seq - 1u), 0u,
                               SERVO_SWEEP_FROM_REST, 0u);
    servo_screen_sweep_started((uint16_t)(repeat.start_seq + 1u), 0u,
                               SERVO_SWEEP_FROM_REST, 0u);
    CHECK_EQ(servo_screen_drawn(), 1500u);     /* neither is this wait's */
    servo_screen_sweep_started(repeat.start_seq, 307u, SERVO_SWEEP_FROM_REST,
                               0u);
    CHECK(servo_screen_drawn() > 1800u && servo_screen_drawn() < 1860u);

    /* A resume, SPEED moved after it was taken, and only the repeat
     * acknowledged: a start from rest, out of the wait. */
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    sweep_go();
    frames(0.25f);
    const servo_cmd_t hold = pause_go();
    ack_hold(&hold);
    tap(SWEEP_X, BTN_Y);                       /* PAUSED */
    const servo_cmd_t resume = last_cmd();
    CHECK(resume.resume);
    tap(ARM_X + 1, SPEED_Y);                   /* SPEED moved */
    const servo_cmd_t again = last_cmd();
    CHECK_EQ(again.kind, SERVO_CMD_SWEEP);
    CHECK(!again.resume);
    const uint16_t held = servo_screen_drawn();
    frames(0.2f);
    CHECK_EQ(servo_screen_drawn(), held);
    servo_screen_sweep_started(again.start_seq, 0u, SERVO_SWEEP_FROM_REST,
                               0u);
    frames(0.25f);
    CHECK(servo_screen_commanded() > 1760u && servo_screen_commanded() < 1800u);
}

/*
 * TRAVEL 90 to 45 deg while a sweep runs: the far end runs the old curve
 * under the old mapping until the restart is acknowledged, so the drawing
 * goes on to the old curve's peak, 1900 us, not clamped at 45 deg (1750 us).
 * After the acknowledgement the new curve keeps within 45 deg.
 */
TEST_CASE(a_restart_drains_the_old_curve_in_its_old_mapping)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    sweep_go();
    frames(0.4f);
    open_settings();
    tap(ROW_L_X, ROW_Y(4));                    /* TRAVEL */
    keys("45");
    const servo_cmd_t c = last_cmd();
    CHECK_EQ(c.kind, SERVO_CMD_SWEEP);
    CHECK_EQ(c.sweep_span, 200u);
    frames(0.15f);                             /* the dwell at the peak */
    CHECK(servo_screen_drawn() > 1880u);
    acked(0u, SERVO_SWEEP_FROM_HERE, 0u);
    frames(0.6f);
    CHECK(servo_screen_drawn() <= 1750u);
    CHECK(servo_screen_commanded() <= 1750u);
}

/*
 * PAUSE and PAUSED drained in one pass, after a late frame: the HOLD never
 * left, so neither does a RESUME.  Nothing is sent, the sweep runs on, and
 * the curve's clock is untouched.  SWEEP and PAUSE in one pass likewise
 * leave nothing sent and no sweep, and a command queued before them is
 * still queued.
 */
TEST_CASE(two_sweep_taps_in_one_pass_send_nothing)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    sweep_go();
    frames(0.3f);
    const uint32_t phase = servo_screen_curve_ms();
    tap(SWEEP_X, BTN_Y);                       /* PAUSE */
    tap(SWEEP_X, BTN_Y);                       /* PAUSED, same pass */
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
    CHECK(servo_screen_sweeping());
    CHECK(!servo_screen_paused());
    CHECK_EQ(servo_screen_curve_ms(), phase);
    frames(0.2f);                              /* on, as at the far end */
    CHECK(servo_screen_commanded() > 1880u);
    /* And the pair after it is a pair again. */
    (void)pause_go();

    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    int x, y;
    dial_at(30.0f, ARC_R - 20, &x, &y);
    tap(x, y);                                 /* a position, queued */
    const uint16_t pos = servo_screen_commanded();
    tap(SWEEP_X, BTN_Y);                       /* SWEEP */
    tap(SWEEP_X, BTN_Y);                       /* PAUSE, same pass */
    CHECK(!servo_screen_sweeping());
    CHECK(!servo_screen_paused());
    const servo_cmd_t c = last_cmd();
    CHECK_EQ(c.kind, SERVO_CMD_POSITION);
    CHECK_EQ(c.value_us, pos);
}

/*
 * At SPEED 100 % the far end takes a command at once, and so does the
 * drawing: a square's edge inside the HOLD's acknowledgement interval puts
 * the paused output at the far end, 1100 us, not part way down a ramp, and
 * a trim changed while paused says that.  A drag at 100 % is drawn at the
 * command on the next frame.
 */
TEST_CASE(speed_100_is_immediate_in_the_drawing_and_the_replay)
{
    fresh();
    settings_set(SET_SERVO_CURVE, 0.0f);       /* SQUARE */
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    sweep_go();
    frames(1.1f);                              /* +A until 1200 ms */
    CHECK_EQ(servo_screen_drawn(), 1900u);
    const servo_cmd_t hold = pause_go();
    frames(0.2f);                              /* the edge passes */
    ack_hold(&hold);
    CHECK_EQ(servo_screen_drawn(), 1100u);
    CHECK_EQ(servo_screen_commanded(), 1100u);
    open_settings();
    tap(TRIM_UP_X, ROW_Y(3));
    const servo_cmd_t c = last_cmd();
    CHECK_EQ(c.kind, SERVO_CMD_POSITION);
    CHECK_EQ(c.value_us, 1105u);
    close_settings();

    fresh();
    int x, y;
    dial_at(60.0f, ARC_R - 20, &x, &y);
    tap(x, y);
    scr->tick(1.0f / 39.0f);
    CHECK_EQ(servo_screen_drawn(), servo_screen_commanded());
}

/*
 * PAUSED does not move the horn: the far end stays held until the RESUME
 * reaches it.  Acknowledged 300 ms after it did, the drawn output is worked
 * on from the held point for those 300 ms at SPEED, as the far end's
 * output slews -- the bank the IOmcu runs, fed the same curve.
 */
TEST_CASE(a_resume_is_drawn_from_its_acknowledgement)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    tap(ARM_X + 1, SPEED_Y);                   /* SPEED's slowest */
    sweep_go();
    frames(0.3f);
    const servo_cmd_t hold = pause_go();
    const uint32_t kept = servo_screen_curve_ms();
    ack_hold(&hold);
    const uint16_t held = servo_screen_drawn();
    frames(1.0f);
    tap(SWEEP_X, BTN_Y);                       /* PAUSED */
    const servo_cmd_t c = last_cmd();
    CHECK(c.resume);
    frames(0.3f);
    CHECK_EQ(servo_screen_drawn(), held);      /* not before it is known */
    ack_resume(kept, 307u);

    outputs_t far;
    outputs_init(&far, 0u);
    CHECK(outputs_set_role(&far, 0, OUT_ROLE_SURFACE));
    CHECK(outputs_set_endpoints(&far, 0, c.min_us, c.max_us));
    outputs_arm(&far, true, 0u);
    CHECK(outputs_set(&far, 0, (uint16_t)(held - c.min_us), 0u));
    outputs_step(&far, 0u);
    CHECK(outputs_set_slew(&far, 0, c.slew_per_s));
    sweep_t w;
    const sweep_cfg_t cfg = { (sweep_kind_t)c.sweep_kind, c.sweep_mhz,
                              c.sweep_span, c.sweep_dwell_ms, 0u };
    CHECK(sweep_start(&w, &cfg, 0u));
    for (uint32_t t = 1u; t <= 307u; ++t) {
        uint16_t cmd = 0;
        (void)sweep_step(&w, kept + t, &cmd);
        (void)outputs_set(&far, 0, cmd, t);
        outputs_step(&far, t);
    }
    const int far_us = (int)outputs_pulse_us(&far, 0);
    CHECK(far_us > (int)held + 60);
    CHECK(abs((int)servo_screen_drawn() - far_us) <= 3);
}

/* A curve changed on the TEST page while paused is a new sweep, from its
 * beginning, not a resume. */
TEST_CASE(a_curve_changed_while_paused_starts_over)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    tap(SWEEP_X, BTN_Y);
    frames(0.25f);
    tap(SWEEP_X, BTN_Y);                       /* PAUSE */
    settings_set(SET_SERVO_TEST_HZ, 1.0f);
    tap(SWEEP_X, BTN_Y);                       /* PAUSED */
    const servo_cmd_t c = last_cmd();
    CHECK_EQ(c.kind, SERVO_CMD_SWEEP);
    CHECK(!c.resume);
    CHECK_EQ(c.sweep_mhz, 1000u);
}

/* Pause, then each thing that ends a pause, and the command it sends. */
static void paused_sweep(void)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    sweep_go();
    frames(0.2f);
    (void)pause_go();
    CHECK(servo_screen_paused());
}

/*
 * A pause ends as a hold did: CENTRE, RELEASE, a finger on the dial, a
 * disarm, STOP, leaving the screen, an IOmcu that stops sweeping, a
 * hold the panel had to let go of, and a changed profile, which says the
 * position again.  The button reads SWEEP after each.
 */
TEST_CASE(a_pause_ends_where_a_hold_ended)
{
    paused_sweep();
    tap(ARM_X + 40, BTN_Y);                    /* CENTRE */
    CHECK(!servo_screen_paused());
    CHECK_EQ(last_cmd().kind, SERVO_CMD_CENTRE);

    paused_sweep();
    tap(ARM_X + ARM_W - 40, BTN_Y);            /* RELEASE */
    CHECK(!servo_screen_paused());
    CHECK_EQ(last_cmd().kind, SERVO_CMD_RELEASE);

    paused_sweep();
    int x, y;
    dial_at(30.0f, ARC_R - 20, &x, &y);
    tap(x, y);
    CHECK(!servo_screen_paused());
    CHECK_EQ(last_cmd().kind, SERVO_CMD_POSITION);

    paused_sweep();
    servo_screen_set_armed(false);
    CHECK(!servo_screen_paused());

    paused_sweep();
    servo_screen_cancel_arm();                 /* STOP */
    CHECK(!servo_screen_paused());

    paused_sweep();
    scr->leave();
    CHECK(!servo_screen_paused());
    CHECK_EQ(last_cmd().kind, SERVO_CMD_DISARM);

    paused_sweep();
    servo_screen_set_sweep(false);
    CHECK(!servo_screen_paused());
    CHECK_EQ(last_cmd().kind, SERVO_CMD_RELEASE);

    paused_sweep();
    servo_screen_released(g_hold_seq);                   /* unrepeated for 500 ms */
    CHECK(!servo_screen_paused());

    paused_sweep();
    choose_type(1);
    CHECK(!servo_screen_paused());
    CHECK_EQ(last_cmd().kind, SERVO_CMD_POSITION);

    /* The button has left the paused fill. */
    scr->render(&cv, 0);
    CHECK_EQ(sweep_btn_pixels(ui_theme_color(UI_C_WARN)), 0);
}

/*
 * Without feedback the horn is drawn where the far end's output is, slewing
 * as the IOmcu slews -- in its command units -- so the horn PAUSE
 * leaves drawn is where the IOmcu holds the servo, with CENTRE off
 * the middle as well.  The far end
 * here is the bank the IOmcu runs, fed the same sweep.
 */
TEST_CASE(hold_without_feedback_matches_the_far_ends_slew)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    open_settings();
    tap(ROW_R_X, ROW_Y(2));                    /* PULSE CENTRE */
    keys("1520");
    tap(ROW_L_X, ROW_Y(2));                    /* PULSE MIN */
    keys("900");
    close_settings();
    tap(ARM_X + 1, SPEED_Y);                   /* SPEED 10 % */
    (void)last_cmd();
    tap(SWEEP_X, BTN_Y);
    servo_cmd_t c = last_cmd();
    CHECK_EQ(c.kind, SERVO_CMD_SWEEP);
    acked(0u, SERVO_SWEEP_FROM_REST, 0u);

    outputs_t far;
    outputs_init(&far, 0u);
    CHECK(outputs_set_role(&far, 0, OUT_ROLE_SURFACE));
    CHECK(outputs_set_endpoints(&far, 0, c.min_us, c.max_us));
    CHECK(outputs_set_slew(&far, 0, c.slew_per_s));
    outputs_arm(&far, true, 0u);
    sweep_t w;
    const sweep_cfg_t cfg = { (sweep_kind_t)c.sweep_kind, c.sweep_mhz,
                              c.sweep_span, c.sweep_dwell_ms, 0u };
    CHECK(sweep_start(&w, &cfg, 0u));
    for (int i = 1; i <= 20; ++i) {            /* half a second */
        scr->tick(1.0f / 39.0f);
        const uint32_t t = (uint32_t)((float)i * 1000.0f / 39.0f);
        uint16_t cmd = 0;
        (void)sweep_step(&w, t, &cmd);
        (void)outputs_set(&far, 0, cmd, t);
        outputs_step(&far, t);
    }
    const servo_cmd_t hold = pause_go();
    ack_hold(&hold);                           /* where the far end is */
    const int far_us = (int)outputs_pulse_us(&far, 0);
    CHECK(far_us > 1560);                      /* well on its way */
    CHECK(abs((int)servo_screen_commanded() - far_us) <= 6);
}

/*
 * Without feedback the horn follows only what the far end is known to do.
 * A sweep asked for is not drawn until its start is acknowledged; then the
 * output is worked on from where it was drawn when the far end began, at
 * SPEED: acknowledged 300 ms after it began, it is where one acknowledged
 * at once is 300 ms in.  A sweep that froze unrepeated starts from where it
 * froze.
 */
TEST_CASE(the_drawn_output_starts_where_the_far_ends_output_starts)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    tap(ARM_X + 1, SPEED_Y);                   /* SPEED 12 % */
    tap(SWEEP_X, BTN_Y);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_SWEEP);
    frames(0.3f);                              /* not acknowledged */
    CHECK_EQ(servo_screen_drawn(), 1500u);
    CHECK_EQ(servo_screen_commanded(), 1500u);
    acked(307u, SERVO_SWEEP_FROM_REST, 0u);
    const uint16_t late = servo_screen_drawn();

    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    tap(ARM_X + 1, SPEED_Y);
    sweep_go();
    frames(0.3f);
    const uint16_t at_once = servo_screen_drawn();
    CHECK(at_once > 1520u);
    CHECK(abs((int)late - (int)at_once) <= 3);

    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    tap(ARM_X + 1, SPEED_Y);
    sweep_go();
    frames(0.2f);
    const uint16_t at_02 = servo_screen_drawn();
    frames(0.3f);
    acked(0u, SERVO_SWEEP_FROM_FROZEN, 307u);
    CHECK(at_02 > 1520u);
    CHECK(abs((int)servo_screen_drawn() - (int)at_02) <= 3);
}

/* The horn is drawn along the far end's curve from when it started there. */
TEST_CASE(the_horn_follows_the_curve_from_where_the_far_end_started_it)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    sweep_go();
    frames(0.5f);                              /* the peak, as drawn */
    CHECK(servo_screen_commanded() > 1880u);
    acked(0u, SERVO_SWEEP_FROM_HERE, 0u);  /* just now */
    scr->tick(0.001f);
    const uint16_t at = servo_screen_commanded();
    CHECK(at > 1490u && at < 1520u);
}

/*
 * Leaving disarms, the same rule as the motor bench's: a screen that does
 * not show the horn must not be holding it somewhere, and it must not leave
 * the bench armed behind itself either.  The disarm lets go of the pin on
 * its way, so one command covers both.
 */
TEST_CASE(leaving_disarms_and_lets_go_of_the_output)
{
    fresh();
    scr->leave();
    CHECK_EQ(last_cmd().kind, SERVO_CMD_DISARM);
}

/*
 * The trim moves the pulse the same angle maps to; the angle itself does not
 * change, because the horn has not moved, the linkage under it has.
 */
TEST_CASE(trim_shifts_the_pulse_and_not_the_angle)
{
    fresh();
    int x, y;
    dial_at(0.0f, ARC_R - 30, &x, &y);
    ev(x, y, TOUCH_EVENT_DOWN, 1);
    ev(x, y, TOUCH_EVENT_UP, 1);
    CHECK_EQ(servo_screen_commanded(), 1500);

    open_settings();
    tap(TRIM_UP_X, ROW_Y(3));           /* trim up, five microseconds */
    CHECK_EQ(servo_screen_commanded(), 1505);
    tap(TRIM_DN_X, ROW_Y(3));
    CHECK_EQ(servo_screen_commanded(), 1500);
}

/* Feedback places the arm rather than animating towards it: a servo already
 * at 40° was never at zero, and the screen reports what the hardware did. */
TEST_CASE(feedback_is_shown_rather_than_travelled_to)
{
    fresh();
    servo_screen_feedback(1750, 0.8f, true);
    scr->render(&cv, 0);

    /* Rendering twice with no change must paint nothing, so the second pass
     * proves the first one settled rather than still easing somewhere. */
    gfx_color_t *first = malloc((size_t)W * H * sizeof(gfx_color_t));
    REQUIRE_ALLOC(first);
    memcpy(first, fb, (size_t)W * H * sizeof(gfx_color_t));
    scr->tick(0.05f);
    scr->render(&cv, 0);
    CHECK_EQ(memcmp(first, fb, (size_t)W * H * sizeof(gfx_color_t)), 0);
    free(first);
}

int main(void)
{
    RUN(leaving_disarms_and_lets_go_of_the_output);
    RUN(trim_shifts_the_pulse_and_not_the_angle);
    RUN(feedback_is_shown_rather_than_travelled_to);
    RUN(a_refused_sweep_leaves_nothing_driven);
    RUN(an_early_resume_keeps_the_reading_at_the_hold);
    RUN(a_released_pause_says_no_drawn_angle);
    RUN(leaving_forgets_the_sweep_model);
    RUN(a_start_acknowledgement_applies_to_its_own_command);
    RUN(a_later_repeat_acknowledged_ends_the_wait);
    RUN(a_restart_drains_the_old_curve_in_its_old_mapping);
    RUN(two_sweep_taps_in_one_pass_send_nothing);
    RUN(speed_100_is_immediate_in_the_drawing_and_the_replay);
    RUN(a_resume_is_drawn_from_its_acknowledgement);
    RUN(a_curve_changed_while_paused_starts_over);
    RUN(a_pause_ends_where_a_hold_ended);
    RUN(hold_without_feedback_matches_the_far_ends_slew);
    RUN(the_drawn_output_starts_where_the_far_ends_output_starts);
    RUN(the_horn_follows_the_curve_from_where_the_far_end_started_it);
    return test_summary("servo_curve");
}
