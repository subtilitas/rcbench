/**
 * @file test_hitec_panel_gate.c
 * @brief The Hitec overlay's write gate around WRITE n VALUES' hold: the
 *        gate opened for one write and shut when the write is refused,
 *        the hold that a finger, the home tag or the session ends, the
 *        presses a moving session keeps, and the taps that look at the
 *        card slot.
 *
 * Cases:
 * - a_card_request_between_the_hold_and_its_send_shuts_the_gate
 * - the_session_dropping_the_edits_at_the_send_sends_nothing
 * - a_finger_that_leaves_write_ends_its_hold
 * - a_finger_that_leaves_write_after_it_fired_keeps_the_write
 * - restore_looks_at_the_card_slot
 * - the_home_tag_under_the_hold_ends_it_with_the_leave_page
 * - edits_dropped_under_the_hold_end_it
 * - the_hold_rect_is_writes_while_held_and_while_it_flashes
 * - a_session_that_moves_keeps_the_press_on_a_tab
 * - connect_is_drawn_pressed_under_its_hold
 * - the_page_labels_are_null_with_no_page
 * - the_taps_that_look_at_the_card_slot
 *
 * SPDX-License-Identifier: MIT
 */

#include "hitec_panel_rig.h"

/** @brief SOFT START of a D485HW to 60% by its list. */
static void soft_start_60(void)
{
    tap(hitec_panel_tab_rect(HITEC_PANEL_MOTION));
    for (int row = 0; row < 6; ++row) {
        const char *l = hitec_panel_label(HITEC_PANEL_MOTION, row, 0);

        if (l != NULL && strcmp(l, TR(HP_SOFTSTART)) == 0) {
            tap(val(HITEC_PANEL_MOTION, row, 0));
            break;
        }
    }
    CHECK_EQ(hitec_panel_editor(), 2);
    for (int i = 0; hitec_panel_choice_label(i) != NULL; ++i) {
        if (strcmp(hitec_panel_choice_label(i), "60%") == 0) {
            tap(hitec_panel_choice_rect(i));
            break;
        }
    }
    CHECK_EQ(hitec_panel_editor(), 0);
    CHECK_EQ(hitec_prog_changes(model()), 1u);
}

/** @brief A D485HW read, held, with SOFT START edited to 60%. */
static void edited(void)
{
    open_en();
    servo_d485();
    tap_connect();
    soft_start_60();
}

/** @brief WRITE n VALUES' button on the SERVO tab. */
static gfx_rect_t write_btn(void)
{
    tap(hitec_panel_tab_rect(HITEC_PANEL_SERVO));
    return val(HITEC_PANEL_SERVO, 3, 1);
}

/** @brief One tick of the overlay and 20ms of the bench. */
static void step(void)
{
    hitec_panel_tick(0.02f);
    prun(20u);
}

/**
 * @brief Presses WRITE n VALUES and ticks until its hold has just
 *        completed: 1.98s, then a tick of 50ms that passes 2s.  The send
 *        is due on the next tick.
 * @param r the button
 */
static void hold_to_due(gfx_rect_t r)
{
    ev(cx(r), cy(r), TOUCH_EVENT_DOWN);
    for (int i = 0; i < 99; ++i) {
        step();
    }
    CHECK(hitec_panel_pressed());
    hitec_panel_tick(0.05f);
}

/**
 * @brief The colour drawn at the middle of @p r's left edge, 3px in.
 * @param r a button
 * @return the pixel
 */
static gfx_color_t edge_px(gfx_rect_t r)
{
    return fb[(size_t)(r.y + r.h / 2) * UI_PANEL_W + (size_t)(r.x + 3)];
}

/* A card request taken between the hold's end and its send: the gate
 * opens, the write is refused while the card works, and the gate shuts
 * again with nothing sent. */
TEST_CASE(a_card_request_between_the_hold_and_its_send_shuts_the_gate)
{
    edited();
    g_card = true;
    prun(20u);
    hold_to_due(write_btn());
    g_files_slow = true;
    CHECK_EQ(hitec_prog_files(model(), 0u), HITEC_PROG_MSG_NONE);
    CHECK(hitec_prog_card_busy(model()));
    step();
    ev(0, 0, TOUCH_EVENT_UP);
    CHECK_EQ(msg(), (unsigned)HITEC_PROG_MSG_BUSY);
    CHECK(!hitec_prog_write_on(model()));
    files_answer();
    prun(500u);
    CHECK_EQ(g_drv.d_writes, 0u);
    CHECK_EQ(hitec_prog_changes(model()), 1u);
}

/* The edits dropped between the hold's end and its send: WRITE takes no
 * press any more, and the send does not go. */
TEST_CASE(the_session_dropping_the_edits_at_the_send_sends_nothing)
{
    edited();
    hold_to_due(write_btn());
    hitec_prog_discard(model());
    step();
    ev(0, 0, TOUCH_EVENT_UP);
    prun(500u);
    CHECK_EQ(g_drv.d_writes, 0u);
    CHECK(!hitec_prog_write_on(model()));
}

/* A finger that moves on the button keeps its hold; one that leaves it
 * ends the hold, and its release writes nothing. */
TEST_CASE(a_finger_that_leaves_write_ends_its_hold)
{
    edited();
    const gfx_rect_t r = write_btn();

    ev(cx(r), cy(r), TOUCH_EVENT_DOWN);
    for (int i = 0; i < 25; ++i) {
        step();
    }
    ev(cx(r) + 4, cy(r), TOUCH_EVENT_MOVE);
    CHECK(hitec_panel_pressed());
    ev(r.x - 20, cy(r), TOUCH_EVENT_MOVE);
    CHECK(!hitec_panel_pressed());
    for (int i = 0; i < 100; ++i) {
        step();
    }
    ev(r.x - 20, cy(r), TOUCH_EVENT_UP);
    prun(500u);
    CHECK_EQ(g_drv.d_writes, 0u);
    CHECK_EQ(hitec_prog_changes(model()), 1u);
}

/* A finger that leaves the button after the hold fired: the write goes
 * all the same. */
TEST_CASE(a_finger_that_leaves_write_after_it_fired_keeps_the_write)
{
    edited();
    const gfx_rect_t r = write_btn();

    ev(cx(r), cy(r), TOUCH_EVENT_DOWN);
    for (int i = 0; i < 105; ++i) {
        step();
    }
    ev(r.x - 20, cy(r), TOUCH_EVENT_MOVE);
    ev(r.x - 20, cy(r), TOUCH_EVENT_UP);
    until(HITEC_PROG_HELD);
    CHECK_EQ(g_drv.dreg[0x60], 6u);
    CHECK_EQ(hitec_prog_changes(model()), 0u);
}

/* RESTORE asks the screen to look at the card slot as well. */
TEST_CASE(restore_looks_at_the_card_slot)
{
    edited();
    const gfx_rect_t w = write_btn();

    ev(cx(w), cy(w), TOUCH_EVENT_DOWN);
    for (int i = 0; i < 105; ++i) {
        step();
    }
    ev(cx(w), cy(w), TOUCH_EVENT_UP);
    until(HITEC_PROG_HELD);
    CHECK(hitec_panel_row_live(HITEC_PANEL_SERVO, 3, 0));
    (void)hitec_panel_card_look();
    tap(val(HITEC_PANEL_SERVO, 3, 0));
    CHECK(hitec_panel_card_look());
}

/* The home tag under the hold opens LEAVE PROGRAMMING: the hold ends and
 * the edit is not written. */
TEST_CASE(the_home_tag_under_the_hold_ends_it_with_the_leave_page)
{
    edited();
    const gfx_rect_t r = write_btn();

    ev(cx(r), cy(r), TOUCH_EVENT_DOWN);
    for (int i = 0; i < 50; ++i) {
        step();
    }
    hitec_panel_back();
    CHECK_EQ(hitec_panel_editor(), 3);
    for (int i = 0; i < 75; ++i) {
        step();
    }
    CHECK(!hitec_panel_pressed());
    ev(cx(r), cy(r), TOUCH_EVENT_UP);
    prun(500u);
    CHECK_EQ(g_drv.d_writes, 0u);
    CHECK_EQ(hitec_prog_changes(model()), 1u);
}

/* The edit dropped under the hold (DISCARD EDITS by the model): WRITE
 * takes no press any more and the hold ends unsent. */
TEST_CASE(edits_dropped_under_the_hold_end_it)
{
    edited();
    const gfx_rect_t r = write_btn();

    ev(cx(r), cy(r), TOUCH_EVENT_DOWN);
    for (int i = 0; i < 50; ++i) {
        step();
    }
    hitec_prog_discard(model());
    step();
    CHECK(!hitec_panel_pressed());
    for (int i = 0; i < 75; ++i) {
        step();
    }
    ev(cx(r), cy(r), TOUCH_EVENT_UP);
    prun(500u);
    CHECK_EQ(g_drv.d_writes, 0u);
}

/* The hold's rectangle names WRITE while a finger holds it and while it
 * flashes after the release, and not after the flash. */
TEST_CASE(the_hold_rect_is_writes_while_held_and_while_it_flashes)
{
    edited();
    const gfx_rect_t r = write_btn();
    gfx_rect_t h;

    ev(cx(r), cy(r), TOUCH_EVENT_DOWN);
    step();
    h = hitec_panel_pages.hold_rect();
    CHECK_EQ(h.x, r.x);
    CHECK_EQ(h.y, r.y);
    for (int i = 0; i < 101; ++i) {
        step();
    }
    ev(cx(r), cy(r), TOUCH_EVENT_UP);
    h = hitec_panel_pages.hold_rect();
    CHECK_EQ(h.x, r.x);
    CHECK_EQ(h.y, r.y);
    for (int i = 0; i < 50; ++i) {
        step();
    }
    h = hitec_panel_pages.hold_rect();
    CHECK(h.x != r.x || h.y != r.y);
    until(HITEC_PROG_HELD);
}

/* A read the model starts while a finger is on a tab, the state out of
 * HELD: the press is kept, and its release selects the tab. */
TEST_CASE(a_session_that_moves_keeps_the_press_on_a_tab)
{
    open_en();
    servo_d485();
    tap_connect();
    tap(hitec_panel_tab_rect(HITEC_PANEL_SERVO));
    const gfx_rect_t t = hitec_panel_tab_rect(HITEC_PANEL_MOTION);

    ev(cx(t), cy(t), TOUCH_EVENT_DOWN);
    CHECK_EQ(hitec_prog_read(model()), HITEC_PROG_MSG_NONE);
    prun(20u);
    CHECK_EQ(pstate(), HITEC_PROG_READING);
    CHECK(hitec_panel_pressed());
    ev(cx(t), cy(t), TOUCH_EVENT_UP);
    CHECK_EQ(hitec_panel_page(), HITEC_PANEL_MOTION);
    until(HITEC_PROG_HELD);
}

/* CONNECT's button is drawn pressed while its hold runs, and plain once
 * the finger has gone. */
TEST_CASE(connect_is_drawn_pressed_under_its_hold)
{
    open_en();
    servo_d485();
    const gfx_rect_t c = val(HITEC_PANEL_SERVO, 0, 0);
    gfx_color_t plain;

    show();
    plain = edge_px(c);
    ev(cx(c), cy(c), TOUCH_EVENT_DOWN);
    show();
    CHECK(edge_px(c) != plain);
    ev(cx(c), cy(c), TOUCH_EVENT_UP);
    show();
    CHECK(edge_px(c) == plain);
}

/* With no page open the page's labels are NULL; on LEAVE's page they
 * name its buttons. */
TEST_CASE(the_page_labels_are_null_with_no_page)
{
    open_en();
    servo_d485();
    tap_connect();
    CHECK(hitec_panel_apply_label() == NULL);
    CHECK(hitec_panel_cancel_label() == NULL);
    press_port();
    CHECK_EQ(hitec_panel_editor(), 3);
    CHECK_STR_EQ(hitec_panel_apply_label(), TR(KP_HOLD_LEAVE));
    CHECK_STR_EQ(hitec_panel_cancel_label(), TR(CANCEL));
}

/* FILES and MORE ask the screen to look at the card slot, as WRITE's
 * send does; READ ALL does not. */
TEST_CASE(the_taps_that_look_at_the_card_slot)
{
    static const struct {
        int row, col;
        bool looks;
    } k_rows[] = {
        { 1, 1, false },    /* READ ALL */
        { 1, 0, true },     /* FILES */
    };

    open_en();
    servo_d485();
    /* MORE in PWM, before CONNECT. */
    tap(hitec_panel_tab_rect(HITEC_PANEL_SERVO));
    (void)hitec_panel_card_look();
    tap(val(HITEC_PANEL_SERVO, 2, 0));
    CHECK(hitec_panel_card_look());
    CHECK_EQ(hitec_panel_editor(), 2);
    hitec_panel_back();
    tap_connect();
    tap(hitec_panel_tab_rect(HITEC_PANEL_SERVO));
    for (size_t i = 0; i < sizeof(k_rows) / sizeof(k_rows[0]); ++i) {
        CHECK(hitec_panel_row_live(HITEC_PANEL_SERVO, k_rows[i].row,
                                   k_rows[i].col));
        (void)hitec_panel_card_look();
        tap(val(HITEC_PANEL_SERVO, k_rows[i].row, k_rows[i].col));
        CHECK_EQ(hitec_panel_card_look(), k_rows[i].looks);
        if (hitec_panel_editor() != 0) {
            hitec_panel_back();
        }
        prun(500u);         /* the card's answer to FILES' list */
        until(HITEC_PROG_HELD);
        show();
    }
}

int main(void)
{
    RUN(a_card_request_between_the_hold_and_its_send_shuts_the_gate);
    RUN(the_session_dropping_the_edits_at_the_send_sends_nothing);
    RUN(a_finger_that_leaves_write_ends_its_hold);
    RUN(a_finger_that_leaves_write_after_it_fired_keeps_the_write);
    RUN(restore_looks_at_the_card_slot);
    RUN(the_home_tag_under_the_hold_ends_it_with_the_leave_page);
    RUN(edits_dropped_under_the_hold_end_it);
    RUN(the_hold_rect_is_writes_while_held_and_while_it_flashes);
    RUN(a_session_that_moves_keeps_the_press_on_a_tab);
    RUN(connect_is_drawn_pressed_under_its_hold);
    RUN(the_page_labels_are_null_with_no_page);
    RUN(the_taps_that_look_at_the_card_slot);
    return test_summary("hitec_panel_gate");
}
