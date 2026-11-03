/**
 * @file test_motor_draw.c
 * @brief The bench screen's panes and redraws: both framebuffers, the plot of a run,
 * and the gestures that are cancelled.
 *
 * Cases:
 * - the_tabs_switch_panes_and_both_render
 * - each_framebuffer_is_updated_independently
 * - a_redraw_leaves_no_stale_pixels
 * - the_release_after_a_hold_does_not_disarm
 * - the_arm_button_fades_across_the_hold
 * - an_unanswered_bench_does_not_show_numbers
 * - the_plot_records_one_run
 * - leaving_the_screen_does_not_erase_the_run
 * - a_cancelled_gesture_does_not_arm
 * - a_cancelled_tab_press_does_not_switch_the_pane
 * - a_cancelled_disarm_still_disarms
 * - a_cancelled_arm_asks_for_nothing
 * - a_cancel_drops_an_arm_the_hold_already_posted
 * - a_cancel_keeps_a_disarm_already_posted
 *
 * SPDX-License-Identifier: MIT
 */

#include "esc_wire_panel.h"
#include "motor_rig.h"

TEST_CASE(the_tabs_switch_panes_and_both_render)
{
    fresh();
    bench_state_t b;
    telemetry_sim_t sim;
    memset(&b, 0, sizeof(b));
    telemetry_sim_init(&sim, NULL);
    for (int i = 0; i < 200; ++i) {
        telemetry_sim_step(&sim, 60.0f, 0.05f, &b);
        motor_screen_push(&b);
    }

    /*
     * Compared pixel for pixel, not by counting non-zero pixels: gfx_clear
     * fills with the background colour rather than with zero, so a non-zero
     * count is the whole canvas (384,000) for any pane.
     */
    scr->render(&cv, 0);
    gfx_color_t *plot = malloc((size_t)W * H * sizeof(gfx_color_t));
    REQUIRE_ALLOC(plot);
    memcpy(plot, fb, (size_t)W * H * sizeof(gfx_color_t));

    tap(MO_TABLE_TAB_X, MO_PANE_Y);         /* the TABLE tab */
    scr->render(&cv, 0);

    int differ = 0;
    for (int i = 0; i < W * H; ++i) {
        if (fb[i] != plot[i]) { ++differ; }
    }
    /* Both panes sit in the same card on the same sunken ground, so the
     * difference is their content and not the background; 6000 pixels is
     * sized to that. */
    if (differ < 6000) {
        T_FAIL("the two panes differ by only %d pixels", differ);
    }

    /* And back again lands on what it started as. */
    tap(MO_PLOT_TAB_X, MO_PANE_Y);
    scr->render(&cv, 0);
    CHECK_EQ(memcmp(plot, fb, (size_t)W * H * sizeof(gfx_color_t)), 0);
    free(plot);
}

/*
 * The screen skips painting what has not changed, which keeps it off the
 * PSRAM (pseudo-static random-access memory) bus the LCD (liquid-crystal
 * display) is scanning out of.  The failure mode that buys is a stale
 * framebuffer: the panel alternates between two, so a buffer whose last paint
 * was a sample ago still needs one even when the other is current.
 *
 * Both counters are checked, because they are bumped by different things
 * (new numbers and a touch), and either one forgetting which buffer it drew
 * into leaves half the frames showing the previous value.
 */
TEST_CASE(each_framebuffer_is_updated_independently)
{
    fresh();
    bench_state_t b;
    telemetry_sim_t sim;
    memset(&b, 0, sizeof(b));
    telemetry_sim_init(&sim, NULL);

    gfx_color_t *other = malloc((size_t)W * H * sizeof(gfx_color_t));
    REQUIRE_ALLOC(other);
    gfx_canvas_t cv1;
    gfx_canvas_init(&cv1, other, W, H, W);

    /* Both buffers start current and identical. */
    for (int i = 0; i < 200; ++i) {
        telemetry_sim_step(&sim, 20.0f, 0.05f, &b);
        motor_screen_push(&b);
    }
    scr->render(&cv, 0);
    scr->render(&cv1, 1);
    CHECK_EQ(memcmp(fb, other, (size_t)W * H * sizeof(gfx_color_t)), 0);

    /* New numbers, painted into buffer 1 only.  Buffer 0 is then a sample
     * behind, and the next render into it has to notice. */
    for (int i = 0; i < 200; ++i) {
        telemetry_sim_step(&sim, 95.0f, 0.05f, &b);
        motor_screen_push(&b);
    }
    scr->render(&cv1, 1);
    scr->render(&cv, 0);
    CHECK_EQ(memcmp(fb, other, (size_t)W * H * sizeof(gfx_color_t)), 0);

    /* Same again for a control: the ARM button changes with no new sample.
     * The arm flashes for three drawn frames, so it is run out first: a
     * buffer caught mid-flash differs from one that is not. */
    motor_screen_set_armed(true);
    for (int i = 0; i < 4; ++i) {
        scr->render(&cv1, 1);
        scr->render(&cv, 0);
    }
    scr->render(&cv1, 1);
    scr->render(&cv, 0);
    CHECK_EQ(memcmp(fb, other, (size_t)W * H * sizeof(gfx_color_t)), 0);

    /* And a frame with nothing new must leave a correct buffer alone: checked
     * by rendering twice and requiring the second to be a no-op on
     * already-correct pixels. */
    scr->render(&cv, 0);
    CHECK_EQ(memcmp(fb, other, (size_t)W * H * sizeof(gfx_color_t)), 0);
    free(other);
}

/*
 * Redrawing state B on top of state A must give the same pixels as drawing B
 * onto a buffer that never saw A.  Anything drawn outside the region it
 * clears violates that, and with alternating framebuffers the leftovers show
 * up as flicker rather than as an obviously stale pixel.
 */
TEST_CASE(a_redraw_leaves_no_stale_pixels)
{
    fresh();
    bench_state_t b;
    telemetry_sim_t sim;
    memset(&b, 0, sizeof(b));
    telemetry_sim_init(&sim, NULL);

    /* State A: a loaded bench, armed, throttle high. */
    for (int i = 0; i < 300; ++i) {
        telemetry_sim_step(&sim, 95.0f, 0.05f, &b);
        motor_screen_push(&b);
    }
    motor_screen_set_armed(true);
    motor_screen_set_throttle(95.0f);
    scr->render(&cv, 0);

    /* Then state B on top of it. */
    for (int i = 0; i < 60; ++i) {
        telemetry_sim_step(&sim, 5.0f, 0.05f, &b);
        motor_screen_push(&b);
    }
    motor_screen_set_armed(false);
    motor_screen_set_throttle(5.0f);
    /* Arming flashes for a few drawn frames; run it out so the comparison is
     * against a settled button.  Generously, so the count can change. */
    for (int i = 0; i < 10; ++i) {
        scr->render(&cv, 0);
    }
    gfx_color_t *over = malloc((size_t)W * H * sizeof(gfx_color_t));
    REQUIRE_ALLOC(over);
    memcpy(over, fb, (size_t)W * H * sizeof(gfx_color_t));

    /* The same state B, onto a buffer that never saw A. */
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    motor_invalidate();
    scr->render(&cv, 0);

    CHECK_EQ(memcmp(over, fb, (size_t)W * H * sizeof(gfx_color_t)), 0);
    free(over);
}

/*
 * The sequence the bench actually runs: the hold arms, the application is
 * told the bench is armed while the finger is still down, and then the
 * finger lifts. That release must not disarm what the same press just armed.
 */
TEST_CASE(the_release_after_a_hold_does_not_disarm)
{
    fresh();
    motor_screen_set_armed(false);

    ev(ARM_X, ARM_Y, TOUCH_EVENT_DOWN, 1);
    tick_for(HOLD_TICKS);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_ARM);

    /* What app_main does once the control task reports the bench armed. */
    motor_screen_set_armed(true);

    ev(ARM_X, ARM_Y, TOUCH_EVENT_UP, 1);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_NONE);

    /* And a fresh press still disarms: armed until touched again. */
    tap(ARM_X, ARM_Y);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_DISARM);
}

/*
 * ARM fades from the OK green to the danger red across the whole hold, so the
 * colour says how much of the two seconds is left. Disarming is a press and
 * does not fade: DISARM is not about to arm.
 */
TEST_CASE(the_arm_button_fades_across_the_hold)
{
    fresh();
    scr->render(&cv, 0);
    const gfx_color_t idle = arm_px();
    CHECK(green_of(idle) > red_of(idle));   /* the OK green */

    ev(ARM_X, ARM_Y, TOUCH_EVENT_DOWN, 1);
    scr->render(&cv, 0);
    const gfx_color_t held0 = arm_px();

    /* Half way: on the way to red, not there yet. */
    tick_for(HOLD_TICKS / 2);
    scr->render(&cv, 0);
    const gfx_color_t half = arm_px();
    CHECK(red_of(half) > red_of(held0));

    /* The rest of the hold takes it to red, and arms. */
    tick_for(HOLD_TICKS);
    scr->render(&cv, 0);
    const gfx_color_t full = arm_px();
    if (!(red_of(full) > red_of(half) && red_of(full) > green_of(full))) {
        T_FAIL("the hold went %04x -> %04x -> %04x, which is not a fade to "
               "red", held0, half, full);
    }
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_ARM);
    ev(ARM_X, ARM_Y, TOUCH_EVENT_UP, 1);

    /* Armed, and past the flash: holding DISARM must not fade. */
    motor_screen_set_armed(true);
    for (int i = 0; i < 20; ++i) {           /* 150ms in phase-sized steps */
        scr->render(&cv, 0);
        scr->tick(0.02f);
    }
    scr->render(&cv, 0);
    const gfx_color_t settled = arm_px();
    ev(ARM_X, ARM_Y, TOUCH_EVENT_DOWN, 1);
    tick_for(HOLD_TICKS);
    scr->render(&cv, 0);
    const gfx_color_t held_armed = arm_px();
    ev(ARM_X, ARM_Y, TOUCH_EVENT_UP, 1);
    /* ui_button lightens a pressed fill, so compare the hue rather than the
     * value: red must not have gained on green the way the hold does. */
    if (red_of(held_armed) - green_of(held_armed)
        > red_of(settled) - green_of(settled) + 24) {
        T_FAIL("holding DISARM went %04x -> %04x, which is a fade it should "
               "not run", settled, held_armed);
    }
}

/*
 * Before a single poll has answered there is nothing to show, and 0.00V is
 * a wrong reading; the heroes print a placeholder, so the screen differs
 * visibly from one holding real numbers.
 */
TEST_CASE(an_unanswered_bench_does_not_show_numbers)
{
    fresh();
    scr->render(&cv, 0);
    gfx_color_t *blank = malloc((size_t)W * H * sizeof(gfx_color_t));
    REQUIRE_ALLOC(blank);
    memcpy(blank, fb, (size_t)W * H * sizeof(gfx_color_t));

    bench_state_t b;
    telemetry_sim_t sim;
    memset(&b, 0, sizeof(b));
    telemetry_sim_init(&sim, NULL);
    telemetry_sim_step(&sim, 60.0f, 0.05f, &b);
    motor_screen_push(&b);
    scr->render(&cv, 0);

    int differ = 0;
    for (int i = 0; i < W * H; ++i) {
        if (fb[i] != blank[i]) { ++differ; }
    }
    CHECK(differ > 1000);
    free(blank);
}

/*
 * The run boundary: what the plot does across an arm and a disarm.
 *
 * The trace is the record of a run, so it advances only while the bench is
 * armed.  Arming clears it -- a run's trace is that run's -- and disarming
 * holds it, which is what stops a finished run scrolling off the left edge
 * 26.7s later with no way to keep it.
 */
TEST_CASE(the_plot_records_one_run)
{
    fresh();
    bench_state_t b;
    memset(&b, 0, sizeof(b));

    /* Before the first arm there is no run to show. */
    CHECK_EQ((int)motor_screen_plot_state(), (int)MOTOR_PLOT_EMPTY);
    CHECK_EQ(motor_screen_plot_samples(), 0);

    /* A disarmed bench goes on delivering samples and the plot takes none. */
    for (int i = 0; i < 10; ++i) {
        motor_screen_push(&b);
    }
    CHECK_EQ((int)motor_screen_plot_state(), (int)MOTOR_PLOT_EMPTY);
    CHECK_EQ(motor_screen_plot_samples(), 0);

    motor_screen_set_armed(true);
    CHECK_EQ((int)motor_screen_plot_state(), (int)MOTOR_PLOT_RECORDING);
    for (int i = 0; i < 25; ++i) {
        b.rpm = (float)(100 * i);
        motor_screen_push(&b);
    }
    CHECK_EQ(motor_screen_plot_samples(), 25);

    /* The run ends.  The trace stays exactly as long as it was. */
    motor_screen_set_armed(false);
    CHECK_EQ((int)motor_screen_plot_state(), (int)MOTOR_PLOT_HELD);
    CHECK_EQ(motor_screen_plot_samples(), 25);
    for (int i = 0; i < 40; ++i) {
        motor_screen_push(&b);
    }
    CHECK_EQ(motor_screen_plot_samples(), 25);
    CHECK_EQ((int)motor_screen_plot_state(), (int)MOTOR_PLOT_HELD);

    /* And the next run is its own, not this one continued. */
    motor_screen_set_armed(true);
    CHECK_EQ((int)motor_screen_plot_state(), (int)MOTOR_PLOT_RECORDING);
    CHECK_EQ(motor_screen_plot_samples(), 0);
    motor_screen_push(&b);
    CHECK_EQ(motor_screen_plot_samples(), 1);
}

/*
 * Leaving the screen asks the bench to disarm; it does not end the run by
 * itself.
 *
 * leave() posts DISARM and writes the screen's own armed flag false, while
 * the application goes on reporting the bench as armed until its state
 * catches up.  Clearing on that flag would erase the run at the moment of
 * asking to end it -- and on a link that has stopped answering, that is the
 * run worth keeping.
 */
TEST_CASE(leaving_the_screen_does_not_erase_the_run)
{
    fresh();
    bench_state_t b;
    memset(&b, 0, sizeof(b));
    motor_screen_set_armed(true);
    for (int i = 0; i < 12; ++i) {
        motor_screen_push(&b);
    }

    scr->leave();
    CHECK_EQ((int)motor_screen_plot_state(), (int)MOTOR_PLOT_RECORDING);
    CHECK_EQ(motor_screen_plot_samples(), 12);

    /* The bench answers the disarm a poll later, and that is what holds it. */
    motor_screen_set_armed(false);
    CHECK_EQ((int)motor_screen_plot_state(), (int)MOTOR_PLOT_HELD);
    CHECK_EQ(motor_screen_plot_samples(), 12);
}

/*
 * The panel could not hand over every touch event, so this screen's record
 * of what is on the glass is stale.  A hold that completes on the frame
 * timer would otherwise arm the bench on a finger that has already gone --
 * the release it is waiting for is one of the events that went missing.
 *
 * Cancelling asks for nothing, which is what letting go early already does,
 * and it must not leave the button stuck: a fresh press arms as usual.
 */
TEST_CASE(a_cancelled_gesture_does_not_arm)
{
    fresh();
    ev(ARM_X, ARM_Y, TOUCH_EVENT_DOWN, 1);
    tick_for(HOLD_TICKS / 4);

    scr->cancel();
    tick_for(HOLD_TICKS * 2);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_NONE);

    /* The contact that was down is gone as far as this screen is concerned,
     * so its release commands nothing either. */
    ev(ARM_X, ARM_Y, TOUCH_EVENT_UP, 1);
    tick_for(HOLD_TICKS);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_NONE);

    /* And nothing is stuck: a fresh press still arms. */
    hold_arm();
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_ARM);
}

/*
 * The tab row holds a press between DOWN and UP, and the release is what
 * switches the pane.  A cancel abandons that press: the track id it owned
 * is one the controller reuses, so a later contact that began elsewhere and
 * lifts over the tab must not be taken for the missing release.
 */
TEST_CASE(a_cancelled_tab_press_does_not_switch_the_pane)
{
    fresh();
    scr->render(&cv, 0);
    gfx_color_t *plot = malloc((size_t)W * H * sizeof(gfx_color_t));
    REQUIRE_ALLOC(plot);
    memcpy(plot, fb, (size_t)W * H * sizeof(gfx_color_t));

    ev(MO_TABLE_TAB_X, MO_PANE_Y, TOUCH_EVENT_DOWN, 1); /* TABLE */
    scr->cancel();
    ev(MO_TABLE_TAB_X, MO_PANE_Y, TOUCH_EVENT_UP, 1); /* a recycled id */
    scr->render(&cv, 0);
    CHECK_EQ(memcmp(plot, fb, (size_t)W * H * sizeof(gfx_color_t)), 0);

    /* And the row is not stuck: a fresh tap switches as usual. */
    tap(MO_TABLE_TAB_X, MO_PANE_Y);
    scr->render(&cv, 0);
    CHECK(memcmp(plot, fb, (size_t)W * H * sizeof(gfx_color_t)) != 0);
    free(plot);
}

/*
 * Abandoning a gesture asks for nothing, and on an armed bench that is the
 * wrong direction for one of them.  Disarming is a press, so its release is
 * the whole command: a release lost to a full touch queue is a DISARM the
 * operator made and the bench never saw, and dropping it silently leaves the
 * outputs driving.  Arming has already sent its command by the time the
 * finger lifts, so an arm cancelled part way asks for nothing.
 */
TEST_CASE(a_cancelled_disarm_still_disarms)
{
    fresh();
    hold_arm();
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_ARM);
    motor_screen_set_armed(true);

    /* The press that disarms, and then the events stop arriving. */
    ev(ARM_X, ARM_Y, TOUCH_EVENT_DOWN, 1);
    scr->cancel();
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_DISARM);
}

/* And the other direction stays as it was: an arm abandoned part way asks
 * for nothing, because the hold is the gesture and it never completed. */
TEST_CASE(a_cancelled_arm_asks_for_nothing)
{
    fresh();
    motor_screen_set_armed(false);
    ev(ARM_X, ARM_Y, TOUCH_EVENT_DOWN, 1);
    tick_for(HOLD_TICKS / 4);
    scr->cancel();
    tick_for(HOLD_TICKS * 2);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_NONE);
}

/*
 * A hold that completes on the same frame a touch event is lost has already
 * posted its arm by the time the next frame observes the loss.  A command is
 * forwarded on the frame after the one that posted it, and the frame that
 * observes a loss cancels before that forwarding, so the arm is dropped here
 * rather than reaching the bench.  A disarm is kept: it is the direction that
 * fails safe.
 */
TEST_CASE(a_cancel_drops_an_arm_the_hold_already_posted)
{
    fresh();
    ev(ARM_X, ARM_Y, TOUCH_EVENT_DOWN, 1);
    tick_for(HOLD_TICKS);

    /* The hold fired and the arm is waiting to be collected. */
    scr->cancel();
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_NONE);
}

/* And the other way: a disarm waiting to be collected survives a cancel,
 * because losing it leaves an armed bench driving. */
/* An ARM posted as a programmer took a pin is refused: no answer comes, and the
 * ask does not stand after the hold.  The TARGET row says it: locked while one
 * is asked. */
TEST_CASE(a_hold_drops_an_arm_already_posted)
{
    static esc_prog_feed_t f;
    static esc_prog_gate_t gate;

    fresh();
    esc_prog_gate_init(&gate);
    esc_wire_panel_gate(&gate);
    ev(ARM_X, ARM_Y, TOUCH_EVENT_DOWN, 1);
    tick_for(HOLD_TICKS);
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_ARM);
    ev(ARM_X, ARM_Y, TOUCH_EVENT_UP, 1);
    tick_for(2);
    CHECK(motor_screen_target_locked());
    memset(&f, 0, sizeof(f));
    f.rd.known = true;
    f.rd.held = true;
    ui_router_set_alert(NULL);
    esc_wire_panel_feed(&f);
    tick_for(2);
    /* The drop says why, and names the holder. */
    CHECK_STR_EQ(ui_router_alert(), TR(ALERT_ARM_ESC));
    f.rd.held = false;
    esc_wire_panel_feed(&f);
    tick_for(2);
    CHECK(!motor_screen_target_locked());
    /* Nothing is posted by it. */
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_NONE);
    /* A hold that comes under a press of ARM ends it, and says why. */
    ui_router_set_alert(NULL);
    ev(ARM_X, ARM_Y, TOUCH_EVENT_DOWN, 1);
    tick_for(3);
    f.rd.held = true;
    esc_wire_panel_feed(&f);
    tick_for(HOLD_TICKS);
    CHECK_STR_EQ(ui_router_alert(), TR(ALERT_ARM_ESC));
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_NONE);
    ev(ARM_X, ARM_Y, TOUCH_EVENT_UP, 1);
    f.rd.held = false;
    esc_wire_panel_feed(&f);
    tick_for(2);
}

TEST_CASE(a_cancel_keeps_a_disarm_already_posted)
{
    fresh();
    motor_screen_set_armed(true);
    ev(ARM_X, ARM_Y, TOUCH_EVENT_DOWN, 1);
    ev(ARM_X, ARM_Y, TOUCH_EVENT_UP, 1);

    scr->cancel();
    CHECK_EQ(last_cmd().kind, MOTOR_CMD_DISARM);
}

int main(void)
{
    RUN(the_tabs_switch_panes_and_both_render);
    RUN(each_framebuffer_is_updated_independently);
    RUN(a_redraw_leaves_no_stale_pixels);
    RUN(the_release_after_a_hold_does_not_disarm);
    RUN(the_arm_button_fades_across_the_hold);
    RUN(an_unanswered_bench_does_not_show_numbers);
    RUN(the_plot_records_one_run);
    RUN(leaving_the_screen_does_not_erase_the_run);
    RUN(a_cancelled_gesture_does_not_arm);
    RUN(a_cancelled_tab_press_does_not_switch_the_pane);
    RUN(a_cancelled_disarm_still_disarms);
    RUN(a_cancelled_arm_asks_for_nothing);
    RUN(a_cancel_drops_an_arm_the_hold_already_posted);
    RUN(a_hold_drops_an_arm_already_posted);
    RUN(a_cancel_keeps_a_disarm_already_posted);
    return test_summary("motor_draw");
}
