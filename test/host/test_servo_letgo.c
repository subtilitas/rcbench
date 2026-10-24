/**
 * @file test_servo_letgo.c
 * @brief What the servo screen does with the answers of a sweep that was let go
 * and with a sweep it refuses: stale pauses, resumes said twice, undo.
 *
 * Cases:
 * - sweep_needs_a_surface_bound
 * - a_sweep_does_not_come_back_with_the_link
 * - sweep_needs_the_link
 * - a_resume_of_a_let_go_pause_is_dropped
 * - a_resume_origin_is_used_once
 * - commands_from_a_let_go_pause_are_stale
 * - a_resume_said_again_keeps_its_pause
 * - a_drag_after_pause_outlives_the_let_go
 * - a_resume_and_its_repeat_go_with_the_let_go
 * - a_later_pause_goes_with_the_let_go
 * - a_late_chained_hold_ends_its_root
 * - a_changed_curve_from_paused_is_a_new_start
 * - no_undo_across_a_let_go
 * - an_undone_pair_keeps_the_resume_waiting
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_screen_rig.h"

/*
 * No surface bound: SWEEP is greyed and sends nothing, since a sweep of
 * nothing never starts.  A sweep command the panel finds nothing to send to
 * ends the wait for its start.
 */
TEST_CASE(sweep_needs_a_surface_bound)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    servo_screen_set_surfaces(false);
    scr->render(&cv, 0);
    CHECK_EQ(sweep_btn_pixels(ui_theme_color(UI_C_ACCENT)), 0);
    tap(SWEEP_X, BTN_Y);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
    CHECK(!servo_screen_sweeping());

    servo_screen_set_surfaces(true);
    tap(SWEEP_X, BTN_Y);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_SWEEP);
    CHECK(servo_screen_sweeping());
    servo_screen_sweep_refused();
    CHECK(!servo_screen_sweeping());
    CHECK(!servo_screen_paused());
}

/*
 * A sweep, the link down, the link back: nothing is sent until a new tap,
 * the sweep's own command not taken before the link went included, and
 * the control task drops a kept sweep or hold at the same edge
 * (servo_cmd_survives_link_loss()).
 */
TEST_CASE(a_sweep_does_not_come_back_with_the_link)
{
    fresh();
    servo_screen_set_link(true);
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    sweep_go();
    frames(0.3f);
    servo_screen_set_link(false);
    servo_screen_set_link(true);
    frames(1.0f);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
    CHECK(!servo_screen_sweeping());

    tap(SWEEP_X, BTN_Y);                       /* a new tap ... */
    servo_screen_set_link(false);              /* ... the link goes first */
    servo_screen_set_link(true);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
    tap(SWEEP_X, BTN_Y);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_SWEEP);

    const servo_cmd_t kinds[] = {
        { .kind = SERVO_CMD_SWEEP }, { .kind = SERVO_CMD_HOLD },
        { .kind = SERVO_CMD_POSITION }, { .kind = SERVO_CMD_CENTRE },
        { .kind = SERVO_CMD_RELEASE },
    };
    CHECK(!servo_cmd_survives_link_loss(&kinds[0]));
    CHECK(!servo_cmd_survives_link_loss(&kinds[1]));
    CHECK(servo_cmd_survives_link_loss(&kinds[2]));
    CHECK(servo_cmd_survives_link_loss(&kinds[3]));
    CHECK(servo_cmd_survives_link_loss(&kinds[4]));
    CHECK(!servo_cmd_survives_link_loss(NULL));
}

/* With the link down SWEEP is greyed and starts nothing: the panel would
 * drop the command, and the screen would wait for a start that never
 * comes.  Back up, it works. */
TEST_CASE(sweep_needs_the_link)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    servo_screen_set_link(false);
    scr->render(&cv, 0);
    CHECK_EQ(sweep_btn_pixels(ui_theme_color(UI_C_ACCENT)), 0);
    tap(SWEEP_X, BTN_Y);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
    CHECK(!servo_screen_sweeping());
    servo_screen_set_link(true);
    tap(SWEEP_X, BTN_Y);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_SWEEP);
    CHECK(servo_screen_sweeping());
}

/*
 * PAUSED tapped while the HOLD is still on its way, and that HOLD then let
 * go of (acknowledged late): the resume carries the pause it resumes, which
 * is how the panel drops it as stale, and the screen ends the sweep it had
 * asked to resume, so nothing waits for a start that will not come.
 */
TEST_CASE(a_resume_of_a_let_go_pause_is_dropped)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    sweep_go();
    frames(0.2f);
    const servo_cmd_t hold = pause_go();
    tap(SWEEP_X, BTN_Y);                       /* PAUSED, HOLD in flight */
    const servo_cmd_t resume = last_cmd();
    CHECK(resume.resume);
    CHECK_EQ(resume.from_pause, hold.pause_seq);
    servo_pause_end_t ended = { true, hold.pause_seq };
    CHECK(servo_cmd_stale(&ended, &resume));
    CHECK(servo_screen_sweeping());            /* waiting for the resume */
    servo_screen_released(g_hold_seq);                   /* the late HOLD let go */
    CHECK(!servo_screen_sweeping());
    CHECK(!servo_screen_paused());
    frames(0.5f);
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
    tap(SWEEP_X, BTN_Y);                       /* a new tap starts afresh */
    const servo_cmd_t again = last_cmd();
    CHECK_EQ(again.kind, SERVO_CMD_SWEEP);
    CHECK(!again.resume);
}

/*
 * A profile changed while paused says the held angle as a POSITION asked
 * during that pause; once the panel has let the pause go, that POSITION is
 * stale and dropped, so the surfaces stay at the rest the screen shows.
 * The first command from after the pause retires the record: a pause
 * number reused later is not mistaken for it.  A disarm is never stale.
 */
TEST_CASE(commands_from_a_let_go_pause_are_stale)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    sweep_go();
    frames(0.2f);
    const servo_cmd_t hold = pause_go();
    CHECK(hold.pause_seq != 0u);
    CHECK_EQ(hold.from_pause, 0u);
    ack_hold(&hold);
    open_settings();
    tap(TRIM_UP_X, ROW_Y(3));                  /* while paused */
    const servo_cmd_t pos = last_cmd();
    CHECK_EQ(pos.kind, SERVO_CMD_POSITION);
    CHECK_EQ(pos.from_pause, hold.pause_seq);
    close_settings();

    servo_pause_end_t ended = { true, hold.pause_seq };
    CHECK(servo_cmd_stale(&ended, &pos));      /* queued behind the HOLD */
    CHECK(ended.on);
    const servo_cmd_t disarm = { .kind = SERVO_CMD_DISARM,
                                 .from_pause = hold.pause_seq };
    CHECK(!servo_cmd_stale(&ended, &disarm));
    CHECK(ended.on);                           /* not a drive command */
    servo_screen_released(g_hold_seq);
    tap(ARM_X + 40, BTN_Y);                    /* CENTRE, after the pause */
    const servo_cmd_t centre = last_cmd();
    CHECK_EQ(centre.kind, SERVO_CMD_CENTRE);
    CHECK_EQ(centre.from_pause, 0u);
    CHECK(!servo_cmd_stale(&ended, &centre));
    CHECK(!ended.on);                          /* retired */
    CHECK(!servo_cmd_stale(&ended, &pos));     /* a reused number later */
    CHECK(!servo_cmd_stale(NULL, &pos));
    CHECK(!servo_cmd_stale(&ended, NULL));
}

/*
 * The let-go of pause N, with the queue behind its late HOLD in each order
 * the screen can leave: the panel drops exactly the commands that still
 * derive from N, and the screen takes the let-go only while it is still on
 * N -- so once the queue drains, the surfaces do what the screen shows.
 */
static void paused_on_n(servo_cmd_t *hold)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    sweep_go();
    frames(0.3f);
    *hold = pause_go();                        /* its ack is late */
}

/* 1: PAUSED and then SPEED in one pass: the resume, said again at the new
 * SPEED, still derives from N and is dropped with it; the screen stops. */
TEST_CASE(a_resume_said_again_keeps_its_pause)
{
    servo_cmd_t hold;
    paused_on_n(&hold);
    tap(SWEEP_X, BTN_Y);                       /* PAUSED ... */
    tap(ARM_X + 1, SPEED_Y);                   /* ... SPEED, same pass */
    const servo_cmd_t c = last_cmd();
    CHECK_EQ(c.kind, SERVO_CMD_SWEEP);
    CHECK(c.resume);
    CHECK_EQ(c.from_pause, hold.pause_seq);
    servo_pause_end_t ended = { true, hold.pause_seq };
    CHECK(servo_cmd_stale(&ended, &c));
    servo_screen_released(hold.pause_seq);
    CHECK(!servo_screen_sweeping());
    CHECK(!servo_screen_paused());
}

/* 2a: a drag after PAUSE leaves N: its POSITION is sent and holds the
 * servo there, and the screen keeps showing it driven there. */
TEST_CASE(a_drag_after_pause_outlives_the_let_go)
{
    servo_cmd_t hold;
    paused_on_n(&hold);
    int x, y;
    dial_at(30.0f, ARC_R - 20, &x, &y);
    tap(x, y);
    const servo_cmd_t pos = last_cmd();
    CHECK_EQ(pos.kind, SERVO_CMD_POSITION);
    CHECK_EQ(pos.from_pause, 0u);
    servo_pause_end_t ended = { true, hold.pause_seq };
    CHECK(!servo_cmd_stale(&ended, &pos));     /* sent, and retires it */
    const uint16_t at = servo_screen_commanded();
    servo_screen_released(hold.pause_seq);
    CHECK_EQ(servo_screen_commanded(), at);    /* still driven there */
    open_settings();
    tap(TRIM_UP_X, ROW_Y(3));
    const servo_cmd_t again = last_cmd();
    CHECK_EQ(again.kind, SERVO_CMD_POSITION);  /* the drag, said again */
    CHECK_EQ(again.value_us, at + 5u);
    close_settings();

    /* CENTRE the same way. */
    paused_on_n(&hold);
    tap(ARM_X + 40, BTN_Y);
    const servo_cmd_t centre = last_cmd();
    CHECK_EQ(centre.kind, SERVO_CMD_CENTRE);
    CHECK_EQ(centre.from_pause, 0u);
    servo_screen_released(hold.pause_seq);
    CHECK_EQ(servo_screen_commanded(), 1500u);
}

/* 2b: SPEED moves while a taken resume waits: both the resume and the
 * SWEEP said again derive from N and are dropped; the screen stops. */
TEST_CASE(a_resume_and_its_repeat_go_with_the_let_go)
{
    servo_cmd_t hold;
    paused_on_n(&hold);
    tap(SWEEP_X, BTN_Y);                       /* PAUSED */
    const servo_cmd_t resume = last_cmd();
    tap(ARM_X + 1, SPEED_Y);                   /* SPEED, after it was taken */
    const servo_cmd_t repeat = last_cmd();
    CHECK_EQ(repeat.kind, SERVO_CMD_SWEEP);
    CHECK_EQ(resume.from_pause, hold.pause_seq);
    CHECK_EQ(repeat.from_pause, hold.pause_seq);
    servo_pause_end_t ended = { true, hold.pause_seq };
    CHECK(servo_cmd_stale(&ended, &resume));
    CHECK(servo_cmd_stale(&ended, &repeat));
    servo_screen_released(hold.pause_seq);
    CHECK(!servo_screen_sweeping());
}

/*
 * 2c, and a PAUSE after an unacknowledged resume: PAUSED, PAUSE, PAUSED
 * while HOLD N is out.  The second HOLD holds what pause N left, so it and
 * its resume derive from N and go with it; the screen, still on N, takes
 * the let-go and stops, and nothing is left held or sweeping.
 */
TEST_CASE(a_later_pause_goes_with_the_let_go)
{
    servo_cmd_t hold;
    paused_on_n(&hold);
    tap(SWEEP_X, BTN_Y);                       /* PAUSED */
    const servo_cmd_t r1 = last_cmd();
    const servo_cmd_t h2 = pause_go();         /* PAUSE: N+1 */
    CHECK_EQ(h2.from_pause, hold.pause_seq);
    servo_pause_end_t ended = { true, hold.pause_seq };
    CHECK(servo_cmd_stale(&ended, &r1));
    CHECK(servo_cmd_stale(&ended, &h2));       /* dropped: no hold of rest */
    servo_screen_released(hold.pause_seq);
    CHECK(!servo_screen_paused());
    CHECK(!servo_screen_sweeping());
    CHECK_EQ(servo_screen_commanded(), 1500u);

    /* And with a second PAUSED after it, before the let-go arrives. */
    paused_on_n(&hold);
    tap(SWEEP_X, BTN_Y);
    const servo_cmd_t a = last_cmd();
    const servo_cmd_t b = pause_go();
    tap(SWEEP_X, BTN_Y);
    const servo_cmd_t c = last_cmd();
    servo_pause_end_t e2 = { true, hold.pause_seq };
    CHECK(servo_cmd_stale(&e2, &a));
    CHECK(servo_cmd_stale(&e2, &b));
    CHECK(servo_cmd_stale(&e2, &c));
    servo_screen_released(hold.pause_seq);
    CHECK(!servo_screen_sweeping());
    CHECK(!servo_screen_paused());
}

/*
 * A chained HOLD -- PAUSE before the resume of pause N is acknowledged --
 * answered late: the panel lets go of the chain's root, N, so it drops the
 * commands tagged N and the screen, still on N, takes the let-go.  A HOLD
 * of a sweep running on its own ends its own pause.
 */
TEST_CASE(a_late_chained_hold_ends_its_root)
{
    servo_cmd_t hold;
    paused_on_n(&hold);
    CHECK_EQ(servo_cmd_pause_root(&hold), hold.pause_seq);
    tap(SWEEP_X, BTN_Y);                       /* PAUSED */
    const servo_cmd_t r1 = last_cmd();
    const servo_cmd_t h2 = pause_go();         /* PAUSE, chained */
    CHECK(h2.pause_seq != hold.pause_seq);
    const uint16_t root = servo_cmd_pause_root(&h2);
    CHECK_EQ(root, hold.pause_seq);
    servo_pause_end_t ended = { true, root };
    CHECK(servo_cmd_stale(&ended, &r1));
    open_settings();
    tap(TRIM_UP_X, ROW_Y(3));                  /* a position from the chain */
    const servo_cmd_t pos = last_cmd();
    close_settings();
    CHECK(servo_cmd_stale(&ended, &pos));
    servo_screen_released(root);
    CHECK(!servo_screen_paused());
    CHECK(!servo_screen_sweeping());
    CHECK_EQ(servo_cmd_pause_root(NULL), 0u);
}

/*
 * A PAUSED that starts a changed curve -- the TEST page changed while
 * paused -- is a new origin, not a resume: it carries no pause, survives a
 * let-go of the old one, and the screen keeps waiting for its start.
 */
TEST_CASE(a_changed_curve_from_paused_is_a_new_start)
{
    servo_cmd_t hold;
    paused_on_n(&hold);
    settings_set(SET_SERVO_TEST_HZ, 1.0f);
    tap(SWEEP_X, BTN_Y);                       /* PAUSED: a new curve */
    const servo_cmd_t c = last_cmd();
    CHECK_EQ(c.kind, SERVO_CMD_SWEEP);
    CHECK(!c.resume);
    CHECK_EQ(c.from_pause, 0u);
    servo_pause_end_t ended = { true, hold.pause_seq };
    CHECK(!servo_cmd_stale(&ended, &c));
    servo_screen_released(hold.pause_seq);
    CHECK(servo_screen_sweeping());            /* still waiting for it */
    acked(0u, SERVO_SWEEP_FROM_REST, 0u);
    frames(0.125f);
    CHECK(servo_screen_commanded() > 1760u);
}

/*
 * A sweep-button tap whose command has not left, then the let-go of the
 * pause, then a second tap in the next frame's first drain: the second tap
 * is a tap of its own, not an undo across the notification.  Variant A:
 * PAUSED pending, the screen stops, and the next tap starts a sweep from
 * no pause.  Variant A2: a taken resume, PAUSE pending, the screen stops,
 * and the next tap does not restore a wait for the dropped resume.
 */
TEST_CASE(no_undo_across_a_let_go)
{
    servo_cmd_t hold;
    paused_on_n(&hold);
    tap(SWEEP_X, BTN_Y);                       /* PAUSED, pending */
    servo_screen_released(hold.pause_seq);
    CHECK(!servo_screen_sweeping());
    tap(SWEEP_X, BTN_Y);                       /* a new SWEEP */
    servo_cmd_t c = last_cmd();
    CHECK_EQ(c.kind, SERVO_CMD_SWEEP);
    CHECK(!c.resume);
    CHECK_EQ(c.from_pause, 0u);
    CHECK(servo_screen_sweeping());
    acked(0u, SERVO_SWEEP_FROM_REST, 0u);
    frames(0.25f);
    CHECK(servo_screen_commanded() > 1760u);

    paused_on_n(&hold);
    tap(SWEEP_X, BTN_Y);                       /* PAUSED: taken, dropped */
    (void)last_cmd();
    tap(SWEEP_X, BTN_Y);                       /* PAUSE, pending */
    servo_screen_released(hold.pause_seq);
    CHECK(!servo_screen_sweeping());
    CHECK(!servo_screen_paused());
    tap(SWEEP_X, BTN_Y);                       /* not an undo */
    c = last_cmd();
    CHECK_EQ(c.kind, SERVO_CMD_SWEEP);
    CHECK(!c.resume);
    CHECK_EQ(c.from_pause, 0u);
}

/*
 * A resume waiting for its acknowledgement, a PAUSE and PAUSED drained in
 * one pass on top of it: the pair undoes itself and leaves the resume as it
 * was, its pause and origin included, so the late HOLD acknowledgement of
 * the first pause still moves the horn to the output held.
 */
TEST_CASE(an_undone_pair_keeps_the_resume_waiting)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    sweep_go();
    frames(0.1f);
    const uint32_t tap_phase = servo_screen_curve_ms();
    const servo_cmd_t hold = pause_go();
    frames(0.1f);
    tap(SWEEP_X, BTN_Y);                       /* PAUSED, before the ack */
    CHECK(last_cmd().resume);
    tap(SWEEP_X, BTN_Y);                       /* PAUSE ... */
    tap(SWEEP_X, BTN_Y);                       /* ... PAUSED, same pass */
    CHECK_EQ(last_cmd().kind, SERVO_CMD_NONE);
    frames(0.2f);
    servo_screen_sweep_held(hold.pause_seq, tap_phase + 307u);
    const uint16_t held = servo_screen_drawn();
    CHECK(held > 1870u && held < 1900u);
}

/*
 * A resume acknowledged while feedback is valid uses its origin up: the
 * repeat of the acknowledgement after feedback has gone works on from the
 * reading, 1700us, not from the 1600 saved at the tap.
 */
TEST_CASE(a_resume_origin_is_used_once)
{
    fresh();
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    servo_screen_feedback(1500u, 0.2f, true);
    sweep_go();
    frames(0.2f);
    servo_screen_feedback(1600u, 0.2f, true);
    frames(0.05f);
    const servo_cmd_t hold = pause_go();
    const uint32_t kept = servo_screen_curve_ms();
    ack_hold(&hold);
    tap(SWEEP_X, BTN_Y);                       /* PAUSED: origin 1600 */
    CHECK(last_cmd().resume);
    servo_screen_feedback(1700u, 0.2f, true);
    frames(0.05f);
    ack_resume(kept, 0u);                      /* with feedback */
    frames(0.1f);
    servo_screen_feedback(0u, 0.0f, false);    /* gone */
    ack_resume(kept, 0u);                      /* said again */
    CHECK(abs((int)servo_screen_drawn() - 1700) <= 2);
}

int main(void)
{
    RUN(sweep_needs_a_surface_bound);
    RUN(a_sweep_does_not_come_back_with_the_link);
    RUN(sweep_needs_the_link);
    RUN(a_resume_of_a_let_go_pause_is_dropped);
    RUN(a_resume_origin_is_used_once);
    RUN(commands_from_a_let_go_pause_are_stale);
    RUN(a_resume_said_again_keeps_its_pause);
    RUN(a_drag_after_pause_outlives_the_let_go);
    RUN(a_resume_and_its_repeat_go_with_the_let_go);
    RUN(a_later_pause_goes_with_the_let_go);
    RUN(a_late_chained_hold_ends_its_root);
    RUN(a_changed_curve_from_paused_is_a_new_start);
    RUN(no_undo_across_a_let_go);
    RUN(an_undone_pair_keeps_the_resume_waiting);
    return test_summary("servo_letgo");
}
