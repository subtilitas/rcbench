/**
 * @file test_kst_panel_gate.c
 * @brief The programmer overlay's write gate around WRITE n VALUES' hold
 *        and the hold pages: what the press refuses, the gate opened for
 *        one operation and shut when it is refused or ends, the hold that
 *        a finger, the home tag or the session ends, and the presses that
 *        a session leaving its state keeps until the tick ends them.
 *
 * Cases:
 * - a_press_with_the_gate_already_open_still_takes_the_hold
 * - the_press_on_write_looks_at_the_card_slot
 * - an_arm_between_the_hold_and_its_send_shuts_the_gate_unsent
 * - a_draft_the_planner_refuses_at_the_send_shuts_the_gate
 * - the_session_dropping_the_edits_at_the_send_sends_nothing
 * - a_finger_that_leaves_write_ends_its_hold
 * - a_finger_that_leaves_write_after_it_fired_keeps_the_write
 * - the_home_tag_under_the_hold_ends_it_with_the_leave_page
 * - edits_dropped_under_the_hold_end_it
 * - the_hold_rect_is_writes_while_it_flashes
 * - a_press_on_a_tab_outlives_the_session_leaving_programming
 * - writes_hold_ends_on_the_tick_after_the_session_leaves_programming
 * - connects_hold_ends_on_the_tick_after_the_link_goes
 * - release_pairing_on_an_armed_bench_says_why_and_opens_nothing
 * - connect_is_drawn_pressed_under_its_hold
 * - an_arm_on_a_restore_page_shuts_the_gate_unsent
 * - a_release_the_servo_no_longer_needs_shuts_the_gate
 * - leave_with_edits_while_a_card_request_runs_keeps_them
 * - the_page_labels_are_null_with_no_page
 *
 * SPDX-License-Identifier: MIT
 */

#include "kst_panel_screen_rig.h"

/** @brief WRITE n VALUES' button on the SERVO tab. */
static gfx_rect_t write_btn(void)
{
    tap(kst_panel_tab_rect(PG_SERVO));
    return val(PG_SERVO, 3, 1);
}

/**
 * @brief Presses WRITE n VALUES and ticks until its hold has just
 *        completed: 1.98s, then a tick of 50ms that passes 2s.  The send
 *        is due on the next tick.
 * @param r the button
 */
static void hold_to_due(gfx_rect_t r)
{
    ev(cx(r), cy(r), TOUCH_EVENT_DOWN, 1);
    for (int i = 0; i < 99; ++i) {
        tick(0.02f);
    }
    CHECK(kst_panel_pressed());
    tick(0.05f);
}

/**
 * @brief The colour drawn at the middle of @p r's left edge, 3px in.
 * @param r a button
 * @return the pixel
 */
static gfx_color_t edge_px(gfx_rect_t r)
{
    return fb[(size_t)(r.y + r.h / 2) * W + (size_t)(r.x + 3)];
}

/* With the gate already open (as a limit run opens it), the press still
 * starts a hold and sends nothing on its own: a release before 2s writes
 * nothing. */
TEST_CASE(a_press_with_the_gate_already_open_still_takes_the_hold)
{
    fresh();
    connected();
    edits();
    CHECK_EQ(kst_prog_enable(M, true), KST_PROG_MSG_NONE);
    const gfx_rect_t r = write_btn();

    ev(cx(r), cy(r), TOUCH_EVENT_DOWN, 1);
    CHECK(kst_panel_pressed());
    ticks(1.0f);
    ev(cx(r), cy(r), TOUCH_EVENT_UP, 1);
    rig_frames(50);
    CHECK_EQ(g_rig.sim.n_writes, 0);
    CHECK_EQ(kst_prog_changes(M), 3);
    (void)kst_prog_enable(M, false);
}

/* A press on WRITE asks the screen to look at the card slot, as a tap on
 * READ ALL or FILES does: the write may save a backup file first. */
TEST_CASE(the_press_on_write_looks_at_the_card_slot)
{
    fresh();
    connected();
    edits();
    const gfx_rect_t r = write_btn();

    (void)kst_panel_card_look();
    CHECK(!kst_panel_card_look());
    ev(cx(r), cy(r), TOUCH_EVENT_DOWN, 1);
    CHECK(kst_panel_card_look());
    ev(cx(r), cy(r), TOUCH_EVENT_UP, 1);
}

/* The bench armed after the hold completed and before its send: the gate
 * the send opens is refused, nothing goes, the edits stay and the gate
 * stays shut. */
TEST_CASE(an_arm_between_the_hold_and_its_send_shuts_the_gate_unsent)
{
    fresh();
    connected();
    edits();
    hold_to_due(write_btn());
    kst_panel_bench_armed(true);
    tick(0.02f);
    ev(0, 0, TOUCH_EVENT_UP, 1);
    CHECK_EQ(msg(), KST_PROG_MSG_NEED_DISARM);
    rig_frames(50);
    CHECK_EQ(g_rig.sim.n_writes, 0);
    CHECK_EQ(kst_prog_changes(M), 3);
    CHECK(!kst_prog_write_enabled(M));
    kst_panel_bench_armed(false);
}

/* A draft that breaks a hard rule by the time the hold sends (the model
 * changed it under the hold): the planner refuses the write after the
 * gate opened, and the gate shuts again with nothing sent. */
TEST_CASE(a_draft_the_planner_refuses_at_the_send_shuts_the_gate)
{
    uint16_t lo;
    uint16_t hi;

    fresh();
    connected();
    edits();
    CHECK(kst_prog_range(M, KST_F_NEUTRAL, &lo, &hi));
    hold_to_due(write_btn());
    CHECK(kst_field_set(&M->draft, KST_F_NEUTRAL, (uint16_t)(hi + 1u)));
    tick(0.02f);
    ev(0, 0, TOUCH_EVENT_UP, 1);
    CHECK_EQ(msg(), KST_PROG_MSG_EDIT_HARD);
    rig_frames(50);
    CHECK_EQ(g_rig.sim.n_writes, 0);
    CHECK(!kst_prog_write_enabled(M));
    kst_prog_discard(M);
}

/* The edits dropped between the hold's end and its send: WRITE takes no
 * press any more, and the send does not go. */
TEST_CASE(the_session_dropping_the_edits_at_the_send_sends_nothing)
{
    fresh();
    connected();
    edits();
    hold_to_due(write_btn());
    kst_prog_discard(M);
    tick(0.02f);
    ev(0, 0, TOUCH_EVENT_UP, 1);
    rig_frames(50);
    CHECK_EQ(g_rig.sim.n_writes, 0);
    CHECK(!kst_prog_write_enabled(M));
}

/* A finger that moves on the button keeps its hold; one that leaves it
 * ends the hold, and its release writes nothing. */
TEST_CASE(a_finger_that_leaves_write_ends_its_hold)
{
    fresh();
    connected();
    edits();
    const gfx_rect_t r = write_btn();

    ev(cx(r), cy(r), TOUCH_EVENT_DOWN, 1);
    ticks(0.5f);
    ev(cx(r) + 4, cy(r), TOUCH_EVENT_MOVE, 1);
    CHECK(kst_panel_pressed());
    ticks(0.5f);
    ev(r.x - 20, cy(r), TOUCH_EVENT_MOVE, 1);
    CHECK(!kst_panel_pressed());
    ticks(2.0f);
    ev(r.x - 20, cy(r), TOUCH_EVENT_UP, 1);
    rig_frames(50);
    CHECK_EQ(g_rig.sim.n_writes, 0);
    CHECK_EQ(kst_prog_changes(M), 3);
}

/* A finger that leaves the button after the hold fired: the write goes
 * all the same. */
TEST_CASE(a_finger_that_leaves_write_after_it_fired_keeps_the_write)
{
    fresh();
    connected();
    edits();
    const gfx_rect_t r = write_btn();

    ev(cx(r), cy(r), TOUCH_EVENT_DOWN, 1);
    ticks(2.2f);
    ev(r.x - 20, cy(r), TOUCH_EVENT_MOVE, 1);
    ev(r.x - 20, cy(r), TOUCH_EVENT_UP, 1);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK_EQ(kst_prog_changes(M), 0);
}

/* The home tag under the hold opens LEAVE PROGRAMMING: the hold ends and
 * the edits are not written. */
TEST_CASE(the_home_tag_under_the_hold_ends_it_with_the_leave_page)
{
    fresh();
    connected();
    edits();
    const gfx_rect_t r = write_btn();

    ev(cx(r), cy(r), TOUCH_EVENT_DOWN, 1);
    ticks(1.0f);
    kst_panel_back();
    CHECK_EQ(kst_panel_editor(), 3);
    ticks(1.5f);
    CHECK(!kst_panel_pressed());
    ev(cx(r), cy(r), TOUCH_EVENT_UP, 1);
    rig_frames(50);
    CHECK_EQ(g_rig.sim.n_writes, 0);
    CHECK_EQ(kst_prog_changes(M), 3);
}

/* The edits dropped under the hold (DISCARD EDITS by the model): WRITE
 * takes no press any more and the hold ends unsent. */
TEST_CASE(edits_dropped_under_the_hold_end_it)
{
    fresh();
    connected();
    edits();
    const gfx_rect_t r = write_btn();

    ev(cx(r), cy(r), TOUCH_EVENT_DOWN, 1);
    ticks(1.0f);
    kst_prog_discard(M);
    ticks(0.1f);
    CHECK(!kst_panel_pressed());
    edits();
    ticks(1.5f);
    ev(cx(r), cy(r), TOUCH_EVENT_UP, 1);
    rig_frames(50);
    CHECK_EQ(g_rig.sim.n_writes, 0);
    CHECK_EQ(kst_prog_changes(M), 3);
}

/* After the release the hold still flashes on WRITE, and the hold's
 * rectangle names WRITE until the flash ends. */
TEST_CASE(the_hold_rect_is_writes_while_it_flashes)
{
    fresh();
    connected();
    edits();
    const gfx_rect_t r = write_btn();
    gfx_rect_t h;

    ev(cx(r), cy(r), TOUCH_EVENT_DOWN, 1);
    ticks(2.03f);
    ev(cx(r), cy(r), TOUCH_EVENT_UP, 1);
    h = kst_panel_pages.hold_rect();
    CHECK_EQ(h.x, r.x);
    CHECK_EQ(h.y, r.y);
    ticks(1.0f);
    h = kst_panel_pages.hold_rect();
    CHECK(h.x != r.x || h.y != r.y);
    settle();
}

/**
 * @brief Runs the bench until the session's state is no longer @p st;
 *        10,000 frames at most.
 * @param st the state to leave
 */
static void frames_past(kst_prog_state_t st)
{
    for (unsigned i = 0; i < 10000u && kst_prog_state(M) == st; ++i) {
        rig_frame();
    }
    CHECK(kst_prog_state(M) != st);
}

/* The link lost under a press on a tab, the session out of PROGRAMMING:
 * the press is kept, and its release selects the tab. */
TEST_CASE(a_press_on_a_tab_outlives_the_session_leaving_programming)
{
    fresh();
    connected();
    const gfx_rect_t t = kst_panel_tab_rect(PG_TUNE);

    ev(cx(t), cy(t), TOUCH_EVENT_DOWN, 1);
    rig_link_lost();
    frames_past(KST_PROG_PROGRAMMING);
    CHECK(kst_panel_pressed());
    ev(cx(t), cy(t), TOUCH_EVENT_UP, 1);
    CHECK_EQ(kst_panel_page(), PG_TUNE);
}

/* The link lost under WRITE's hold, the session out of PROGRAMMING: the
 * press stays the hold's until the next tick ends it, and nothing is written. */
TEST_CASE(writes_hold_ends_on_the_tick_after_the_session_leaves_programming)
{
    fresh();
    connected();
    edits();
    const gfx_rect_t r = write_btn();

    ev(cx(r), cy(r), TOUCH_EVENT_DOWN, 1);
    ticks(1.0f);
    rig_link_lost();
    frames_past(KST_PROG_PROGRAMMING);
    tick(0.02f);
    CHECK(!kst_panel_pressed());
    ev(cx(r), cy(r), TOUCH_EVENT_UP, 1);
    CHECK_EQ(g_rig.sim.n_writes, 0);
}

/* The link lost under CONNECT's hold: the press stays the hold's until
 * the next tick ends it, and nothing is connected. */
TEST_CASE(connects_hold_ends_on_the_tick_after_the_link_goes)
{
    fresh();
    g_rig.rail = true;
    link_up();
    CHECK(connect_holds());
    const gfx_rect_t c = val(PG_SERVO, 0, 0);

    ev(cx(c), cy(c), TOUCH_EVENT_DOWN, 1);
    ticks(1.0f);
    rig_link_lost();
    frames_past(KST_PROG_PWM);
    tick(0.02f);
    CHECK(!kst_panel_pressed());
    ev(cx(c), cy(c), TOUCH_EVENT_UP, 1);
    CHECK_EQ(g_rig.starts, 0u);
}

/* RELEASE PAIRING asked for on an ARMED bench: MORE says why and opens
 * no page. */
TEST_CASE(release_pairing_on_an_armed_bench_says_why_and_opens_nothing)
{
    fresh();
    connected();
    kst_panel_bench_armed(true);
    tap(val(PG_SERVO, 2, 1));
    tap(kst_panel_choice_rect(choice(TR(KP_RELEASE_TITLE))));
    CHECK_EQ(kst_panel_editor(), 0);
    CHECK_EQ(msg(), KST_PROG_MSG_NEED_DISARM);
    kst_panel_bench_armed(false);
}

/* CONNECT's button is drawn pressed while its hold runs, and plain once
 * the finger has gone. */
TEST_CASE(connect_is_drawn_pressed_under_its_hold)
{
    fresh();
    g_rig.rail = true;
    link_up();
    CHECK(connect_holds());
    const gfx_rect_t c = val(PG_SERVO, 0, 0);
    gfx_color_t up;
    gfx_color_t down;

    show();
    up = edge_px(c);
    ev(cx(c), cy(c), TOUCH_EVENT_DOWN, 1);
    show();
    down = edge_px(c);
    CHECK(down != up);
    ev(cx(c), cy(c), TOUCH_EVENT_UP, 1);
    show();
    CHECK(edge_px(c) == up);
}

/* The bench armed while RESTORE's page is open: its hold opens no gate,
 * nothing is restored, and the page's refusal is said. */
TEST_CASE(an_arm_on_a_restore_page_shuts_the_gate_unsent)
{
    fresh();
    connected();
    edits();
    hold_write();
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    const unsigned writes = g_rig.sim.n_writes;

    tap(val(PG_SERVO, 3, 0));
    tap(kst_panel_choice_rect(choice(TR(KP_FROM_SESSION))));
    CHECK_EQ(kst_panel_editor(), 3);
    /* The list's page first, then the restore's own. */
    CHECK_STR_EQ(kst_panel_apply_label(), TR(KP_HOLD_NEXT));
    hold(2.1f);
    CHECK_EQ(kst_panel_editor(), 3);
    CHECK_STR_EQ(kst_panel_apply_label(), TR(KP_HOLD_RESTORE));
    kst_panel_bench_armed(true);
    hold(2.1f);
    CHECK_EQ(msg(), KST_PROG_MSG_NEED_DISARM);
    rig_frames(50);
    CHECK_EQ(g_rig.sim.n_writes, writes);
    CHECK(!kst_prog_write_enabled(M));
    kst_panel_bench_armed(false);
}

/* RELEASE PAIRING's page held after the servo's pairing went to 00 under
 * it: the gate opens, the release is refused, and the gate shuts again
 * with nothing sent. */
TEST_CASE(a_release_the_servo_no_longer_needs_shuts_the_gate)
{
    fresh();
    connected();
    tap(val(PG_SERVO, 2, 1));
    tap(kst_panel_choice_rect(choice(TR(KP_RELEASE_TITLE))));
    CHECK_EQ(kst_panel_editor(), 3);
    M->servo.r[KST_REG_NODE_ADDR] = 0u;
    hold(2.1f);
    CHECK_EQ(msg(), KST_PROG_MSG_NO_CHANGES);
    rig_frames(50);
    CHECK_EQ(g_rig.sim.n_writes, 0);
    CHECK(!kst_prog_write_enabled(M));
}

/* LEAVE PROGRAMMING with edits held while a card request has not been
 * answered: the session does not take the LEAVE now, and the edits are
 * kept for the operator. */
TEST_CASE(leave_with_edits_while_a_card_request_runs_keeps_them)
{
    fresh();
    connected();
    edits();
    kst_panel_back();
    CHECK_EQ(kst_panel_editor(), 3);
    g_rig.fc.silent = true;
    CHECK_EQ(kst_prog_files(M, 0u), KST_PROG_MSG_NONE);
    rig_frames(5);
    CHECK(kst_prog_card_busy(M));
    hold(2.1f);
    CHECK_EQ(kst_prog_changes(M), 3);
    CHECK_EQ(kst_prog_state(M), KST_PROG_PROGRAMMING);
    g_rig.fc.silent = false;
}

/* With no page open the page's labels are NULL; on a page they name its
 * buttons. */
TEST_CASE(the_page_labels_are_null_with_no_page)
{
    fresh();
    connected();
    CHECK(kst_panel_apply_label() == NULL);
    CHECK(kst_panel_cancel_label() == NULL);
    kst_panel_back();
    CHECK_STR_EQ(kst_panel_apply_label(), TR(KP_HOLD_LEAVE));
    CHECK_STR_EQ(kst_panel_cancel_label(), TR(CANCEL));
}

int main(void)
{
    RUN(a_press_with_the_gate_already_open_still_takes_the_hold);
    RUN(the_press_on_write_looks_at_the_card_slot);
    RUN(an_arm_between_the_hold_and_its_send_shuts_the_gate_unsent);
    RUN(a_draft_the_planner_refuses_at_the_send_shuts_the_gate);
    RUN(the_session_dropping_the_edits_at_the_send_sends_nothing);
    RUN(a_finger_that_leaves_write_ends_its_hold);
    RUN(a_finger_that_leaves_write_after_it_fired_keeps_the_write);
    RUN(the_home_tag_under_the_hold_ends_it_with_the_leave_page);
    RUN(edits_dropped_under_the_hold_end_it);
    RUN(the_hold_rect_is_writes_while_it_flashes);
    RUN(a_press_on_a_tab_outlives_the_session_leaving_programming);
    RUN(writes_hold_ends_on_the_tick_after_the_session_leaves_programming);
    RUN(connects_hold_ends_on_the_tick_after_the_link_goes);
    RUN(release_pairing_on_an_armed_bench_says_why_and_opens_nothing);
    RUN(connect_is_drawn_pressed_under_its_hold);
    RUN(an_arm_on_a_restore_page_shuts_the_gate_unsent);
    RUN(a_release_the_servo_no_longer_needs_shuts_the_gate);
    RUN(leave_with_edits_while_a_card_request_runs_keeps_them);
    RUN(the_page_labels_are_null_with_no_page);
    return test_summary("kst_panel_gate");
}
