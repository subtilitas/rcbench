/**
 * @file test_servo_restore.c
 * @brief SERVO's question after a programming port let go of a live output
 *        at its own set points: the operator's set points from before the
 *        session back onto it, YES or NO (supply_screen_restore_offer(),
 *        servo_screen_asks_restore()).
 *
 * Cases:
 * - a_leave_with_the_output_on_asks_after_kst_and_hitec
 * - yes_applies_the_operators_set_points
 * - no_keeps_the_sessions_until_the_off
 * - yes_past_6_v_takes_the_hv_warning_and_its_hold
 * - no_question_while_a_port_holds_the_supply
 * - the_question_waits_for_servos_own_panels
 * - the_question_waits_for_output_ons_hold
 * - no_question_after_an_off_an_error_or_a_stop
 * - an_off_asked_ends_the_question_before_the_output_reads_off
 * - no_question_when_nothing_differs_or_a_set_point_moved
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_screen_rig.h"

/* ---------------------------------------------------------------- helpers */

/* The port's set points: the programming voltage and current limit. */
#define PORT_V 5.0f
#define PORT_I 1.0f

/* A frame of the screen. */
static void frame_tick(void) { scr->tick(0.025f); }

/* Whether the supply's set points are @p v and @p i. */
static bool set_at(float v, float i)
{
    return fabsf(supply_screen_set_v() - v) < 1e-4f
           && fabsf(supply_screen_set_i() - i) < 1e-4f;
}

/* A fresh SERVO screen with the operator's set points @p v and @p i, the
 * caps up to 8.4V. */
static void own(float v, float i)
{
    fresh();
    settings_set(SET_SUPPLY_V_MAX, 8.4f);
    supply_screen_limits_changed();
    supply_screen_put(v, i);
    frame_tick();
}

/* A programming port's word on the supply: the KST programmer's, or the
 * Hitec programmer's. */
static void port_holds(bool kst, bool held)
{
    if (kst) {
        supply_screen_set_port_held(held);
    } else {
        supply_screen_set_hitec_held(held);
    }
}

/* A session takes the supply, sets PORT_V and PORT_I with the output on,
 * and is still held. */
static void session_on(bool kst)
{
    port_holds(kst, true);
    supply_screen_adopt(PORT_V, PORT_I);
    supply_screen_set_output(true);
    frame_tick();
}

/* As session_on(), then the LEAVE: the port lets go, the output stays on. */
static void left_on(bool kst)
{
    session_on(kst);
    port_holds(kst, false);
}

/* ------------------------------------------------------------- the cases */

/* A LEAVE that ends with the output on: the question, at the next frame,
 * after the KST programmer's and the Hitec programmer's alike.  Nothing
 * changes before the answer. */
TEST_CASE(a_leave_with_the_output_on_asks_after_kst_and_hitec)
{
    for (int kst = 0; kst < 2; ++kst) {
        own(5.5f, 1.5f);
        left_on(kst != 0);
        CHECK(!servo_screen_asks_restore());
        frame_tick();
        CHECK(servo_screen_asks_restore());
        CHECK(servo_screen_output_held());
        CHECK(set_at(PORT_V, PORT_I));
        CHECK(!supply_cmd(NULL));
        /* Once: asked, the offer is gone. */
        CHECK(!supply_screen_restore_offer(NULL, NULL));
    }
}

/* YES: the operator's set points on the live output, both of them. */
TEST_CASE(yes_applies_the_operators_set_points)
{
    own(5.5f, 1.5f);
    left_on(false);
    frame_tick();
    CHECK(servo_screen_asks_restore());
    tap(WARN_APPLY_X, WARN_Y);
    CHECK(!servo_screen_asks_restore());
    CHECK(set_at(5.5f, 1.5f));
    frame_tick();
    CHECK(!servo_screen_asks_restore());
    /* The change stands at the OFF: nothing goes back over it. */
    supply_screen_set_output(false);
    supply_screen_port_done(true);
    CHECK(set_at(5.5f, 1.5f));
}

/* NO: the session's set points stay on the live output; the operator's
 * come back at its OFF, as without the question. */
TEST_CASE(no_keeps_the_sessions_until_the_off)
{
    own(5.5f, 1.5f);
    left_on(true);
    frame_tick();
    CHECK(servo_screen_asks_restore());
    tap(WARN_CANCEL_X, WARN_Y);
    CHECK(!servo_screen_asks_restore());
    CHECK(set_at(PORT_V, PORT_I));
    for (int i = 0; i < 40; ++i) {
        frame_tick();
    }
    CHECK(!servo_screen_asks_restore());   /* asked once */
    supply_screen_port_done(true);          /* on: nothing */
    CHECK(set_at(PORT_V, PORT_I));
    supply_screen_set_output(false);
    supply_screen_port_done(true);
    CHECK(set_at(5.5f, 1.5f));
}

/* YES to 7.4V from 5.0V: the HV warning, whose tap applies nothing and
 * whose two-second hold applies both set points. */
TEST_CASE(yes_past_6_v_takes_the_hv_warning_and_its_hold)
{
    own(7.4f, 1.5f);
    left_on(false);
    frame_tick();
    CHECK(servo_screen_asks_restore());
    tap(WARN_APPLY_X, WARN_Y);
    CHECK(servo_screen_asks_restore());     /* now the HV warning */
    CHECK(set_at(PORT_V, PORT_I));
    tap(WARN_APPLY_X, WARN_Y);              /* a tap is no hold */
    CHECK(set_at(PORT_V, PORT_I));
    hold_apply(1.0f);                       /* too short */
    CHECK(set_at(PORT_V, PORT_I));
    hold_apply(2.3f);
    CHECK(!servo_screen_asks_restore());
    CHECK(set_at(7.4f, 1.5f));

    /* The output off under the HV warning: it goes, nothing applied. */
    own(7.4f, 1.5f);
    left_on(false);
    frame_tick();
    tap(WARN_APPLY_X, WARN_Y);
    CHECK(servo_screen_asks_restore());
    ev(WARN_APPLY_X, WARN_Y, TOUCH_EVENT_DOWN, 1);
    for (int i = 0; i < 40; ++i) {
        frame_tick();
    }
    supply_screen_set_output(false);
    for (int i = 0; i < 60; ++i) {
        frame_tick();
    }
    ev(WARN_APPLY_X, WARN_Y, TOUCH_EVENT_UP, 1);
    CHECK(!servo_screen_asks_restore());
    CHECK(set_at(PORT_V, PORT_I));

    /* 6.0V is a standard servo's: YES applies it at once. */
    own(6.0f, 1.5f);
    left_on(false);
    frame_tick();
    tap(WARN_APPLY_X, WARN_Y);
    CHECK(!servo_screen_asks_restore());
    CHECK(set_at(6.0f, 1.5f));
}

/* While a port holds the supply there is no question; a port that takes it
 * again before the next frame, or while the question stands, ends it. */
TEST_CASE(no_question_while_a_port_holds_the_supply)
{
    own(5.5f, 1.5f);
    session_on(false);
    for (int i = 0; i < 40; ++i) {
        frame_tick();
    }
    CHECK(!servo_screen_asks_restore());
    port_holds(false, false);
    port_holds(false, true);                /* CONNECT again */
    frame_tick();
    CHECK(!servo_screen_asks_restore());
    port_holds(false, false);               /* and its LEAVE */
    frame_tick();
    CHECK(servo_screen_asks_restore());

    own(5.5f, 1.5f);
    left_on(false);
    frame_tick();
    CHECK(servo_screen_asks_restore());
    port_holds(true, true);                 /* the KST port takes it */
    frame_tick();
    CHECK(!servo_screen_asks_restore());
    CHECK(set_at(PORT_V, PORT_I));

    /* The KST port and the Hitec port: held while either holds. */
    own(5.5f, 1.5f);
    session_on(true);
    port_holds(false, true);
    port_holds(true, false);
    frame_tick();
    CHECK(!servo_screen_asks_restore());
}

/* A keypad of a set point and SERVO's own question about a live output
 * stand first: the question waits for them and follows once they close. */
TEST_CASE(the_question_waits_for_servos_own_panels)
{
    own(5.5f, 1.5f);
    left_on(false);
    tap(SUP_I_X, SUP_ROW_Y);                /* the current limit's keypad */
    frame_tick();
    CHECK(!servo_screen_asks_restore());
    keys("0.8");                            /* asked about: output on */
    frame_tick();
    CHECK(servo_screen_output_held());
    CHECK(!servo_screen_asks_restore());
    tap(WARN_CANCEL_X, WARN_Y);             /* CANCEL: nothing changed */
    CHECK(set_at(PORT_V, PORT_I));
    frame_tick();
    CHECK(servo_screen_asks_restore());

    /* Answered APPLY, the change stands: the offer is gone. */
    own(5.5f, 1.5f);
    left_on(false);
    tap(SUP_I_X, SUP_ROW_Y);
    keys("0.8");
    tap(WARN_APPLY_X, WARN_Y);
    CHECK(set_at(PORT_V, 0.8f));
    for (int i = 0; i < 40; ++i) {
        frame_tick();
    }
    CHECK(!servo_screen_asks_restore());
}

/* A finger on OUTPUT ON whose ON landed under it: the question waits for
 * the finger to let go. */
TEST_CASE(the_question_waits_for_output_ons_hold)
{
    own(5.5f, 1.5f);
    ev(SUP_OUT_X, SUP_ROW_Y, TOUCH_EVENT_DOWN, 1);
    for (int i = 0; i < 100; ++i) {
        frame_tick();
    }
    supply_cmd_t c;
    CHECK(supply_cmd(&c) && c.on);
    supply_screen_set_output(true);
    frame_tick();
    CHECK(servo_screen_output_held());
    /* A port takes the supply and lets go of it between two frames. */
    port_holds(false, true);
    supply_screen_adopt(PORT_V, PORT_I);
    port_holds(false, false);
    for (int i = 0; i < 10; ++i) {
        frame_tick();
    }
    CHECK(!servo_screen_asks_restore());
    ev(SUP_OUT_X, SUP_ROW_Y, TOUCH_EVENT_UP, 1);
    frame_tick();
    CHECK(servo_screen_asks_restore());
}

/* An OFF, an error that switched the output off, a stop, or an output off
 * at the LEAVE: no question, now or after a new ON.  The operator's set
 * points come back at the OFF as before. */
TEST_CASE(no_question_after_an_off_an_error_or_a_stop)
{
    enum { NONE = 0, OFF_AFTER, OFF_ASKED, STOP, OFF_BEFORE, OFF_OPEN };

    for (int k = NONE; k <= OFF_OPEN; ++k) {
        own(5.5f, 1.5f);
        if (k == OFF_BEFORE) {
            /* The error's OFF read before the port let go. */
            session_on(false);
            supply_screen_set_output(false);
            port_holds(false, false);
        } else {
            left_on(false);
        }
        if (k == OFF_AFTER) {
            supply_screen_set_output(false);    /* the error's OFF */
            supply_screen_set_output(true);     /* and a new ON */
        } else if (k == OFF_ASKED) {
            supply_screen_ask_off();            /* OUTPUT OFF tapped */
            CHECK(supply_cmd(NULL));            /* and sent, not yet read */
        } else if (k == STOP) {
            supply_screen_cancel_on();
            servo_screen_cancel_arm();
        } else if (k == OFF_OPEN) {
            frame_tick();
            CHECK(servo_screen_asks_restore());
            supply_screen_set_output(false);
        }
        for (int i = 0; i < 10; ++i) {
            frame_tick();
        }
        CHECK_EQ(servo_screen_asks_restore(), k == NONE);
        if (k == OFF_BEFORE || k == OFF_OPEN) {
            supply_screen_port_done(true);
            CHECK(set_at(5.5f, 1.5f));
        }
    }
}

/* OUTPUT OFF tapped while the question or the HV warning after YES stands:
 * it goes at once, before the supply reports the output off, and nothing
 * is applied, in the same frame as the OFF or after it. */
TEST_CASE(an_off_asked_ends_the_question_before_the_output_reads_off)
{
    for (int same_frame = 0; same_frame < 2; ++same_frame) {
        own(5.5f, 1.5f);
        left_on(false);
        frame_tick();
        CHECK(servo_screen_asks_restore());
        tap(SUP_OUT_X, SUP_ROW_Y);              /* OUTPUT OFF */
        supply_cmd_t c;
        CHECK(supply_cmd(&c) && c.off);         /* sent; still reads on */
        if (!same_frame) {
            frame_tick();
            CHECK(!servo_screen_asks_restore());
        }
        tap(WARN_APPLY_X, WARN_Y);
        CHECK(!servo_screen_asks_restore());
        CHECK(set_at(PORT_V, PORT_I));
    }

    own(7.4f, 1.5f);
    left_on(false);
    frame_tick();
    tap(WARN_APPLY_X, WARN_Y);                  /* YES: the HV warning */
    CHECK(servo_screen_asks_restore());
    ev(WARN_APPLY_X, WARN_Y, TOUCH_EVENT_DOWN, 1);
    for (int i = 0; i < 40; ++i) {
        frame_tick();
    }
    supply_screen_ask_off();
    CHECK(supply_cmd(NULL));
    for (int i = 0; i < 60; ++i) {
        frame_tick();
    }
    ev(WARN_APPLY_X, WARN_Y, TOUCH_EVENT_UP, 1);
    CHECK(!servo_screen_asks_restore());
    CHECK(set_at(PORT_V, PORT_I));
}

/* The operator's set points equal to the port's, or a set point moved
 * between the LEAVE and the frame: no question. */
TEST_CASE(no_question_when_nothing_differs_or_a_set_point_moved)
{
    own(PORT_V, PORT_I);
    left_on(false);
    frame_tick();
    CHECK(!servo_screen_asks_restore());

    own(5.0f, 1.5f);                        /* the current differs */
    left_on(false);
    frame_tick();
    CHECK(servo_screen_asks_restore());

    own(5.5f, 1.5f);
    left_on(false);
    supply_screen_put(5.2f, 1.0f);          /* typed on SUPPLY */
    frame_tick();
    CHECK(!servo_screen_asks_restore());
    CHECK(set_at(5.2f, 1.0f));

    /* A cap lowered under the operator's voltage: offered as the caps
     * take it. */
    own(7.4f, 1.5f);
    left_on(false);
    settings_set(SET_SUPPLY_V_MAX, 6.0f);
    supply_screen_limits_changed();
    frame_tick();
    CHECK(servo_screen_asks_restore());
    tap(WARN_APPLY_X, WARN_Y);
    CHECK(!servo_screen_asks_restore());
    CHECK(set_at(6.0f, 1.5f));
}

int main(void)
{
    RUN(a_leave_with_the_output_on_asks_after_kst_and_hitec);
    RUN(yes_applies_the_operators_set_points);
    RUN(no_keeps_the_sessions_until_the_off);
    RUN(yes_past_6_v_takes_the_hv_warning_and_its_hold);
    RUN(no_question_while_a_port_holds_the_supply);
    RUN(the_question_waits_for_servos_own_panels);
    RUN(the_question_waits_for_output_ons_hold);
    RUN(no_question_after_an_off_an_error_or_a_stop);
    RUN(an_off_asked_ends_the_question_before_the_output_reads_off);
    RUN(no_question_when_nothing_differs_or_a_set_point_moved);
    return test_summary("servo_restore");
}
