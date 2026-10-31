/**
 * @file test_nav_contact.c
 * @brief Who owns a contact: one owner from its DOWN to its UP, the screen or the
 * router.  A contact that comes down on the band, leaves by it, or outlives
 * a touch loss.
 *
 * Cases:
 * - home_under_a_setup_scroll_leaves_the_contact_to_no_screen
 * - cancelling_gestures_lets_go_of_the_band
 * - a_throttle_drag_that_leaves_by_the_band_ends_at_the_edge
 * - the_release_at_the_edge_adds_no_travel_made_on_the_band
 * - a_contact_that_came_down_on_the_band_reaches_no_screen
 * - a_screen_without_a_band_owns_its_top_rows
 * - no_later_contact_continues_a_drag_that_left_by_the_band
 * - stop_answers_a_second_finger_during_a_drag
 * - sliding_onto_stop_or_home_presses_neither
 * - home_under_a_drag_leaves_the_dragging_contact_to_no_screen
 * - the_alert_strip_answers_a_second_finger_during_a_drag
 * - a_touch_loss_leaves_the_contacts_on_the_glass_to_no_screen
 * - a_down_replaces_what_its_id_owned_before
 * - a_contact_beyond_the_table_reaches_no_screen
 *
 * SPDX-License-Identifier: MIT
 */

#include "nav_rig.h"

/*
 * HOME and STOP are the router's gesture, not the screen's, so a band press
 * left latched after a lost event belongs to the router to clear.  The
 * GT911 reuses track ids: a later contact that began on the screen would
 * otherwise be taken for this one's release and act on where it lifts.
 */
TEST_CASE(cancelling_gestures_lets_go_of_the_band)
{
    to_overview();
    const gfx_rect_t stop = ui_band_stop_rect();
    const int sx = stop.x + stop.w / 2, sy = stop.y + stop.h / 2;

    /* A press on STOP, which stops at the press, and then the events stop
     * arriving. */
    touch(sx, sy, TOUCH_EVENT_DOWN, 7);
    CHECK(ui_router_take_stop());
    ui_router_cancel_gestures();

    /* The release the router never saw arrives on a reused id, over a tile.
     * With the band press cleared it is not the band's, so it neither stops
     * the bench again nor navigates from a press nobody made. */
    const ui_screen_id_t before = ui_router_current();
    touch(sx, sy, TOUCH_EVENT_UP, 7);
    CHECK_EQ(ui_router_current(), before);
    CHECK(!ui_router_take_stop());
}

/* On the band, 3px under STOP (y 6..41) and 4px above the body. */
#define UNDER_STOP_X 720

/*
 * Armed.  A finger drags the throttle track 60px, slides up the glass at
 * 8px per report and leaves it on the band.  The next lone finger aims at
 * STOP, lands in the 6px of band under the button and rolls 5px down into
 * the body.  The first drag ended at the edge of the band, so the second
 * contact has nothing to continue, and it came down on the band, so the
 * screen sees none of it.
 */
TEST_CASE(a_throttle_drag_that_leaves_by_the_band_ends_at_the_edge)
{
    on_screen(SCREEN_MOTOR);
    motor_screen_set_armed(true);
    drain_motor();

    finger(FEED_LONE, 300, M_TRACK_Y);
    glide(FEED_LONE, 360, M_TRACK_Y, 8);
    const float dragged = motor_screen_throttle();
    CHECK_NEAR(dragged, 60.0f * M_PX_PCT, 0.01f);
    glide(FEED_LONE, 360, 30, 8);
    lift(FEED_LONE);
    CHECK_EQ(feed_ups, 1);
    CHECK_NEAR(motor_screen_throttle(), dragged, 0.01f);
    drain_motor();
    /* The drag is over, so the knob has the throttle again. */
    CHECK(knob_moves_the_throttle());
    CHECK_NEAR(motor_screen_throttle(), dragged, 0.01f);

    check_plain_band(UNDER_STOP_X, UNDER_STOP_Y);
    finger(FEED_LONE, UNDER_STOP_X, UNDER_STOP_Y);
    glide(FEED_LONE, UNDER_STOP_X, UI_BAND_H + 1, 3);
    motor_cmd_t c;
    const bool posted = motor_posted(&c);
    lift(FEED_LONE);
    CHECK(!posted);
    CHECK(!motor_posted(NULL));
    CHECK_NEAR(motor_screen_throttle(), dragged, 0.01f);
    CHECK(!ui_router_take_stop());
}

/*
 * The release the screen is handed is at the last point it saw, not where
 * the finger lifts: the slider applies the horizontal distance on a
 * release, and the travel made on the band is not the screen's.  The edge
 * is y = 48: the last row of the band is 47.  A contact released there is
 * not handed back when it returns to the body.
 */
TEST_CASE(the_release_at_the_edge_adds_no_travel_made_on_the_band)
{
    on_screen(SCREEN_MOTOR);
    motor_screen_set_armed(true);
    drain_motor();

    finger(FEED_LONE, 300, M_TRACK_Y);
    glide(FEED_LONE, 360, M_TRACK_Y, 8);
    glide(FEED_LONE, 360, UI_BAND_H + 4, 8);
    const uint32_t d0 = ui_router_dispatched();

    /* One step short of the band: still the screen's, and the 4px to the
     * right count. */
    glide(FEED_LONE, 364, UI_BAND_H, 4);
    CHECK_EQ(ui_router_dispatched(), d0 + 1u);
    const float at_edge = motor_screen_throttle();
    CHECK_NEAR(at_edge, 64.0f * M_PX_PCT, 0.01f);
    drain_motor();

    /* The first row of the band, 2px further right: one event to the
     * screen, the release, and the 2px do not count. */
    glide(FEED_LONE, 366, UI_BAND_H - 1, 2);
    CHECK_EQ(ui_router_dispatched(), d0 + 2u);
    CHECK_NEAR(motor_screen_throttle(), at_edge, 0.001f);
    CHECK(knob_moves_the_throttle());

    /* Along the band, back down onto the track, along the track, and up. */
    glide(FEED_LONE, 600, 20, 8);
    glide(FEED_LONE, 600, M_TRACK_Y, 8);
    glide(FEED_LONE, 700, M_TRACK_Y, 8);
    lift(FEED_LONE);
    CHECK_EQ(ui_router_dispatched(), d0 + 2u);
    CHECK_NEAR(motor_screen_throttle(), at_edge, 0.001f);
    CHECK(!motor_posted(NULL));
}

/*
 * A contact that came down on the band is the router's until it lifts: its
 * moves into the body and its release there reach no screen.  The row
 * below the band is the screen's.
 */
TEST_CASE(a_contact_that_came_down_on_the_band_reaches_no_screen)
{
    static const ui_screen_id_t ids[] = {
        SCREEN_OVERVIEW, SCREEN_MOTOR, SCREEN_SERVO, SCREEN_SUPPLY,
        SCREEN_SETUP, SCREEN_LOGS,
    };
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i) {
        on_screen(ids[i]);
        const uint32_t d0 = ui_router_dispatched();
        const uint32_t n0 = ui_router_navigations();
        check_plain_band(560, UI_BAND_H - 1);
        finger(FEED_LONE, 560, UI_BAND_H - 1);
        glide(FEED_LONE, 560, 300, 8);
        glide(FEED_LONE, 300, M_TRACK_Y, 8);
        glide(FEED_LONE, 400, M_TRACK_Y, 8);
        lift(FEED_LONE);
        if (ui_router_dispatched() != d0) {
            T_FAIL("screen %d was handed %u event(s) of a contact from the "
                   "band", (int)ids[i],
                   (unsigned)(ui_router_dispatched() - d0));
        }
        CHECK_EQ(ui_router_navigations(), n0);
        CHECK(!ui_router_take_stop());
    }

    /* One row lower the same contact is the screen's: its DOWN, each MOVE
     * and its UP. */
    on_screen(SCREEN_MOTOR);
    const uint32_t d0 = ui_router_dispatched();
    finger(FEED_LONE, 560, UI_BAND_H);
    glide(FEED_LONE, 560, UI_BAND_H + 16, 8);
    lift(FEED_LONE);
    CHECK_EQ(ui_router_dispatched(), d0 + 4u);
}

/* The splash carries no band, so its top 48px are the screen's. */
TEST_CASE(a_screen_without_a_band_owns_its_top_rows)
{
    fresh();
    feed_reset();
    CHECK_EQ(ui_router_current(), SCREEN_SPLASH);
    const uint32_t d0 = ui_router_dispatched();
    finger(FEED_LONE, 400, 10);
    CHECK_EQ(ui_router_dispatched(), d0 + 1u);
    glide(FEED_LONE, 400, 100, 8);
    glide(FEED_LONE, 400, 4, 8);
    lift(FEED_LONE);
    CHECK_EQ(feed_downs + feed_moves + feed_ups,
             (int)(ui_router_dispatched() - d0));
}

/* A drag, a way of leaving the body, a second contact: what the throttle
 * moved by in the second. */
static float after_a_drag_then(uint8_t id2, int x0, int y0, int x1, int y1,
                               int step1)
{
    on_screen(SCREEN_MOTOR);
    motor_screen_set_armed(true);
    drain_motor();
    finger(FEED_LONE, 300, M_TRACK_Y);
    glide(FEED_LONE, 360, M_TRACK_Y, 8);
    glide(FEED_LONE, 360, 30, step1);
    lift(FEED_LONE);
    const float before = motor_screen_throttle();
    finger(id2, x0, y0);
    glide(id2, x1, y1, 8);
    lift(id2);
    return motor_screen_throttle() - before;
}

/*
 * Every way the second contact can arrive, and the first drag at three
 * speeds.  At 130px per report the tracker splits each step into an UP and
 * a DOWN (TOUCH_JUMP_PX is 120), so the screen has its release from the
 * tracker; at 100px per report it has it from the router.
 */
TEST_CASE(no_later_contact_continues_a_drag_that_left_by_the_band)
{
    CHECK_NEAR(after_a_drag_then(1, 560, 30, 560, 80, 8), 0.0f, 0.001f);
    CHECK_NEAR(after_a_drag_then(FEED_LONE, 560, 30, 560, 80, 8), 0.0f,
               0.001f);
    CHECK_NEAR(after_a_drag_then(FEED_LONE, 560, 60, 560, 110, 8), 0.0f,
               0.001f);
    CHECK_NEAR(after_a_drag_then(FEED_LONE, 560, 30, 600, 30, 8), 0.0f,
               0.001f);
    CHECK_NEAR(after_a_drag_then(FEED_LONE, 720, 24, 720, 80, 8), 0.0f,
               0.001f);
    CHECK_NEAR(after_a_drag_then(FEED_LONE, 560, 30, 560, 80, 130), 0.0f,
               0.001f);
    CHECK_NEAR(after_a_drag_then(FEED_LONE, 560, 30, 560, 80, 100), 0.0f,
               0.001f);
    CHECK_NEAR(after_a_drag_then(FEED_LONE, 560, 30, 560, 80, 120), 0.0f,
               0.001f);
    CHECK_NEAR(after_a_drag_then(FEED_LONE, 360, 30, 360, 80, 8), 0.0f,
               0.001f);
}

/*
 * STOP answers a second finger while the first drags, at the press, and
 * the press ends the drag: the first finger's travel after it moves
 * nothing.
 */
TEST_CASE(stop_answers_a_second_finger_during_a_drag)
{
    on_screen(SCREEN_MOTOR);
    motor_screen_set_armed(true);
    drain_motor();
    const gfx_rect_t stop = ui_band_stop_rect();
    const int sx = stop.x + stop.w / 2, sy = stop.y + stop.h / 2;

    finger(0, 300, M_TRACK_Y);
    glide(0, 340, M_TRACK_Y, 8);
    finger(1, sx, sy);
    CHECK(ui_router_take_stop());       /* on the press */
    lift(1);
    CHECK(!ui_router_take_stop());

    glide(0, 380, M_TRACK_Y, 8);
    CHECK_NEAR(motor_screen_throttle(), 40.0f * M_PX_PCT, 0.01f);
    lift(0);
    CHECK_NEAR(motor_screen_throttle(), 40.0f * M_PX_PCT, 0.01f);
}

/*
 * STOP and the home tag answer to a press on them.  A contact that began on
 * the body and slides onto either presses nothing, and one that began on
 * STOP and slides into the body moves nothing there.
 */
TEST_CASE(sliding_onto_stop_or_home_presses_neither)
{
    on_screen(SCREEN_MOTOR);
    motor_screen_set_armed(true);
    drain_motor();
    const gfx_rect_t stop = ui_band_stop_rect();
    const int sx = stop.x + stop.w / 2, sy = stop.y + stop.h / 2;
    const gfx_rect_t home = ui_home_tag_rect(ui_router_title(SCREEN_MOTOR));
    const int hx = home.x + home.w / 2, hy = home.y + home.h / 2;

    /* From the track up onto STOP, and lifted there. */
    finger(FEED_LONE, 300, M_TRACK_Y);
    glide(FEED_LONE, 340, M_TRACK_Y, 8);
    const float dragged = motor_screen_throttle();
    glide(FEED_LONE, sx, sy, 8);
    CHECK(gfx_rect_contains(stop, sx, sy));
    lift(FEED_LONE);
    CHECK(!ui_router_take_stop());
    drain_motor();

    /* From the plot up onto the home tag, and lifted there. */
    finger(FEED_LONE, hx, 200);
    glide(FEED_LONE, hx, hy, 8);
    lift(FEED_LONE);
    CHECK_EQ(ui_router_current(), SCREEN_MOTOR);

    /* From STOP down onto the track and along it. */
    const float before = motor_screen_throttle();
    const uint32_t d0 = ui_router_dispatched();
    finger(FEED_LONE, sx, sy);
    glide(FEED_LONE, 300, M_TRACK_Y, 8);
    glide(FEED_LONE, 400, M_TRACK_Y, 8);
    lift(FEED_LONE);
    CHECK_EQ(ui_router_dispatched(), d0);
    CHECK(ui_router_take_stop());       /* the press on STOP stopped */
    CHECK_NEAR(motor_screen_throttle(), before, 0.001f);
    CHECK(!motor_posted(NULL));
    CHECK(before > dragged - 30.0f);    /* and the first drag ended sanely */

    /* STOP itself still answers the next press. */
    feed_tap(FEED_LONE, sx, sy);
    CHECK(ui_router_take_stop());
}

/*
 * The home tag answers a second finger during a drag.  The contact that was
 * dragging belonged to the screen that was left; on the screen now on top
 * it is nobody's, so its moves and its release reach no screen.
 */
TEST_CASE(home_under_a_drag_leaves_the_dragging_contact_to_no_screen)
{
    on_screen(SCREEN_MOTOR);
    motor_screen_set_armed(true);
    drain_motor();
    const gfx_rect_t home = ui_home_tag_rect(ui_router_title(SCREEN_MOTOR));

    finger(0, 300, M_TRACK_Y);
    glide(0, 340, M_TRACK_Y, 8);
    feed_tap(1, home.x + home.w / 2, home.y + home.h / 2);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);

    const uint32_t d0 = ui_router_dispatched();
    const uint32_t n0 = ui_router_navigations();
    glide(0, 200, 200, 8);              /* across the tiles */
    lift(0);
    CHECK_EQ(ui_router_dispatched(), d0);
    CHECK_EQ(ui_router_navigations(), n0);

    /* The next press is an ordinary one. */
    finger(0, 200, 200);
    CHECK_EQ(ui_router_dispatched(), d0 + 1u);
    lift(0);
}

/*
 * The home tag under a scroll of a SETUP list: the list goes, and the
 * contact that was scrolling is nobody's on the tiles.  Its moves and its
 * lift over a tile reach no screen and open nothing.
 */
TEST_CASE(home_under_a_setup_scroll_leaves_the_contact_to_no_screen)
{
    on_screen(SCREEN_SETUP);
    feed_reset();
    int x = 0, y = 0;
    hub_point(settings_hub_cat_tile(SET_CAT_APP), &x, &y);
    feed_tap(FEED_LONE, x, y);
    CHECK_EQ(settings_screen_category(), SET_CAT_APP);

    finger(0, 300, UI_BAND_H + 300);
    glide(0, 300, UI_BAND_H + 200, 8);
    CHECK(settings_screen_scroll(NULL) > 0);
    const gfx_rect_t home = ui_home_tag_rect(ui_router_title(SCREEN_SETUP));
    feed_tap(1, home.x + home.w / 2, home.y + home.h / 2);
    CHECK_EQ(settings_screen_category(), -1);

    const uint32_t d0 = ui_router_dispatched();
    hub_point(2, &x, &y);
    glide(0, x, y, 8);                  /* onto OUTPUTS */
    lift(0);
    CHECK_EQ(ui_router_dispatched(), d0);
    CHECK_EQ(ui_router_current(), SCREEN_SETUP);

    /* The next press is an ordinary one. */
    feed_tap(0, x, y);
    CHECK_EQ(ui_router_current(), SCREEN_OUTPUTS);
}

/* The alert strip answers a second finger during a drag, and its press
 * ends the drag as a press anywhere does: the screen is handed the drag's
 * release and nothing of the strip's press.  A press on the strip that
 * wanders to the band and lifts there clears nothing and leaves the strip
 * answering the next tap. */
TEST_CASE(the_alert_strip_answers_a_second_finger_during_a_drag)
{
    on_screen(SCREEN_MOTOR);
    motor_screen_set_armed(true);
    drain_motor();
    ui_router_set_alert("touch controller stopped answering");

    finger(0, 300, M_TRACK_Y);
    glide(0, 340, M_TRACK_Y, 8);
    const uint32_t d0 = ui_router_dispatched();
    feed_tap(1, 600, H - 10);
    CHECK(ui_router_alert() == NULL);
    CHECK_EQ(ui_router_dispatched(), d0 + 1u);   /* the drag's release */
    glide(0, 380, M_TRACK_Y, 8);
    lift(0);
    CHECK_EQ(ui_router_dispatched(), d0 + 1u);
    CHECK_NEAR(motor_screen_throttle(), 40.0f * M_PX_PCT, 0.01f);

    ui_router_set_alert("again");
    const uint32_t d1 = ui_router_dispatched();
    finger(FEED_LONE, 600, H - 10);
    glide(FEED_LONE, 600, 20, 100);
    lift(FEED_LONE);
    CHECK(ui_router_alert() != NULL);
    CHECK_EQ(ui_router_dispatched(), d1);
    feed_tap(FEED_LONE, 600, H - 10);
    CHECK(ui_router_alert() == NULL);
    CHECK_EQ(ui_router_dispatched(), d1);
}

/*
 * A touch loss drops what the screen owned.  A finger still on the glass
 * is nobody's from then on: the event that went missing may have been its
 * release and a new press.
 */
TEST_CASE(a_touch_loss_leaves_the_contacts_on_the_glass_to_no_screen)
{
    on_screen(SCREEN_MOTOR);
    motor_screen_set_armed(true);
    drain_motor();
    finger(FEED_LONE, 300, M_TRACK_Y);
    glide(FEED_LONE, 340, M_TRACK_Y, 8);
    const float dragged = motor_screen_throttle();

    ui_router_cancel_gestures();
    drain_motor();
    const uint32_t d0 = ui_router_dispatched();
    glide(FEED_LONE, 420, M_TRACK_Y, 8);
    lift(FEED_LONE);
    CHECK_EQ(ui_router_dispatched(), d0);
    CHECK_NEAR(motor_screen_throttle(), dragged, 0.001f);
    CHECK(!motor_posted(NULL));

    /* Down again, it is the screen's, and moves by its own travel. */
    finger(FEED_LONE, 200, M_TRACK_Y);
    glide(FEED_LONE, 240, M_TRACK_Y, 8);
    lift(FEED_LONE);
    CHECK_NEAR(motor_screen_throttle(), dragged + 40.0f * M_PX_PCT, 0.01f);
}

/*
 * A release that reaches nobody without the loss being told -- the panel
 * tells every one, so this is the router's own guard -- does not leave the
 * screen's table holding the contact: the id's next DOWN is a new contact.
 * Six rounds, one more than the table has room for.
 */
TEST_CASE(a_down_replaces_what_its_id_owned_before)
{
    on_screen(SCREEN_MOTOR);
    for (int round = 0; round < TOUCH_MAX_POINTS + 1; ++round) {
        const uint32_t d0 = ui_router_dispatched();
        finger(FEED_LONE, 300, 200);
        CHECK_EQ(ui_router_dispatched(), d0 + 1u);
        feed_lose_next(1);
        lift(FEED_LONE);
        CHECK_EQ(ui_router_dispatched(), d0 + 1u);
    }
}

/*
 * The table has room for the five contacts the controller reports.  A
 * sixth cannot come out of the tracker, so these events are written by
 * hand: a contact the router cannot follow to its release is handed to no
 * screen, and the five it does follow are not disturbed.
 */
TEST_CASE(a_contact_beyond_the_table_reaches_no_screen)
{
    on_screen(SCREEN_MOTOR);
    const uint32_t d0 = ui_router_dispatched();
    for (int i = 0; i < TOUCH_MAX_POINTS; ++i) {
        touch(100 + 40 * i, 200, TOUCH_EVENT_DOWN, (uint8_t)(10 + i));
    }
    CHECK_EQ(ui_router_dispatched(), d0 + (uint32_t)TOUCH_MAX_POINTS);
    touch(400, 200, TOUCH_EVENT_DOWN, 99);
    touch(404, 200, TOUCH_EVENT_MOVE, 99);
    touch(404, 200, TOUCH_EVENT_UP, 99);
    CHECK_EQ(ui_router_dispatched(), d0 + (uint32_t)TOUCH_MAX_POINTS);

    touch(100, 200, TOUCH_EVENT_UP, 10);
    CHECK_EQ(ui_router_dispatched(), d0 + (uint32_t)TOUCH_MAX_POINTS + 1u);
    touch(400, 200, TOUCH_EVENT_DOWN, 99);
    touch(400, 200, TOUCH_EVENT_UP, 99);
    CHECK_EQ(ui_router_dispatched(), d0 + (uint32_t)TOUCH_MAX_POINTS + 3u);
}

int main(void)
{
    RUN(home_under_a_setup_scroll_leaves_the_contact_to_no_screen);
    RUN(cancelling_gestures_lets_go_of_the_band);
    RUN(a_throttle_drag_that_leaves_by_the_band_ends_at_the_edge);
    RUN(the_release_at_the_edge_adds_no_travel_made_on_the_band);
    RUN(a_contact_that_came_down_on_the_band_reaches_no_screen);
    RUN(a_screen_without_a_band_owns_its_top_rows);
    RUN(no_later_contact_continues_a_drag_that_left_by_the_band);
    RUN(stop_answers_a_second_finger_during_a_drag);
    RUN(sliding_onto_stop_or_home_presses_neither);
    RUN(home_under_a_drag_leaves_the_dragging_contact_to_no_screen);
    RUN(the_alert_strip_answers_a_second_finger_during_a_drag);
    RUN(a_touch_loss_leaves_the_contacts_on_the_glass_to_no_screen);
    RUN(a_down_replaces_what_its_id_owned_before);
    RUN(a_contact_beyond_the_table_reaches_no_screen);
    return test_summary("nav_contact");
}
