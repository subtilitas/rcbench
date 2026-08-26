/*
 * Host unit tests for touch coordinate mapping and event tracking.
 *
 * SPDX-License-Identifier: MIT
 */

#include "greatest.h"

#include "touch_map.h"

static touch_map_t map_for(touch_rotation_t rot, bool mx, bool my)
{
    touch_map_t m = {
        .src_w = 800,
        .src_h = 480,
        .mirror_x = mx,
        .mirror_y = my,
        .rotation = rot,
    };
    return m;
}

static void expect(const touch_map_t *m, int in_x, int in_y, int want_x, int want_y)
{
    int16_t ox = 0, oy = 0;
    touch_map_apply(m, (int16_t)in_x, (int16_t)in_y, &ox, &oy);
    if (ox != want_x || oy != want_y) {
        T_FAIL("(%d,%d) -> (%d,%d), want (%d,%d)", in_x, in_y, ox, oy, want_x, want_y);
    }
}

TEST_CASE(identity_mapping)
{
    touch_map_t m = map_for(TOUCH_ROTATION_0, false, false);
    expect(&m, 0, 0, 0, 0);
    expect(&m, 799, 479, 799, 479);
    expect(&m, 400, 240, 400, 240);

    int16_t w = 0, h = 0;
    touch_map_size(&m, &w, &h);
    CHECK_EQ(w, 800);
    CHECK_EQ(h, 480);
}

TEST_CASE(rotation_90_swaps_axes)
{
    touch_map_t m = map_for(TOUCH_ROTATION_90, false, false);
    /* Native top-left ends up at the top-right of the rotated space. */
    expect(&m, 0, 0, 479, 0);
    expect(&m, 799, 0, 479, 799);
    expect(&m, 0, 479, 0, 0);
    expect(&m, 799, 479, 0, 799);

    int16_t w = 0, h = 0;
    touch_map_size(&m, &w, &h);
    CHECK_EQ(w, 480);
    CHECK_EQ(h, 800);
}

TEST_CASE(rotation_180_flips_both)
{
    touch_map_t m = map_for(TOUCH_ROTATION_180, false, false);
    expect(&m, 0, 0, 799, 479);
    expect(&m, 799, 479, 0, 0);

    int16_t w = 0, h = 0;
    touch_map_size(&m, &w, &h);
    CHECK_EQ(w, 800);
    CHECK_EQ(h, 480);
}

TEST_CASE(rotation_270_swaps_the_other_way)
{
    touch_map_t m = map_for(TOUCH_ROTATION_270, false, false);
    expect(&m, 0, 0, 0, 799);
    expect(&m, 799, 0, 0, 0);
    expect(&m, 0, 479, 479, 799);

    int16_t w = 0, h = 0;
    touch_map_size(&m, &w, &h);
    CHECK_EQ(w, 480);
    CHECK_EQ(h, 800);
}

TEST_CASE(mirrors_apply_before_rotation)
{
    touch_map_t mx = map_for(TOUCH_ROTATION_0, true, false);
    expect(&mx, 0, 100, 799, 100);
    expect(&mx, 799, 100, 0, 100);

    touch_map_t my = map_for(TOUCH_ROTATION_0, false, true);
    expect(&my, 100, 0, 100, 479);

    /* Both mirrors together are the same as a 180 rotation. */
    touch_map_t both = map_for(TOUCH_ROTATION_0, true, true);
    touch_map_t rot = map_for(TOUCH_ROTATION_180, false, false);
    for (int i = 0; i < 800; i += 137) {
        for (int j = 0; j < 480; j += 91) {
            int16_t ax, ay, bx, by;
            touch_map_apply(&both, (int16_t)i, (int16_t)j, &ax, &ay);
            touch_map_apply(&rot, (int16_t)i, (int16_t)j, &bx, &by);
            CHECK_EQ(ax, bx);
            CHECK_EQ(ay, by);
        }
    }
}

TEST_CASE(out_of_range_input_is_clamped)
{
    touch_map_t m = map_for(TOUCH_ROTATION_0, false, false);
    expect(&m, -50, -50, 0, 0);
    expect(&m, 5000, 5000, 799, 479);

    /* A degenerate source size must not divide by zero or go negative. */
    touch_map_t bad = { .src_w = 0, .src_h = 0, .rotation = TOUCH_ROTATION_0 };
    expect(&bad, 10, 10, 0, 0);
}

TEST_CASE(map_point_updates_in_place)
{
    touch_map_t m = map_for(TOUCH_ROTATION_180, false, false);
    touch_point_t p = { .id = 3, .x = 0, .y = 0, .strength = 42 };
    touch_map_point(&m, &p);
    CHECK_EQ(p.x, 799);
    CHECK_EQ(p.y, 479);
    CHECK_EQ(p.id, 3);
    CHECK_EQ(p.strength, 42);

    /* NULL arguments are no-ops, not crashes. */
    touch_map_point(NULL, &p);
    touch_map_point(&m, NULL);
    touch_map_apply(NULL, 0, 0, NULL, NULL);
    touch_map_size(NULL, NULL, NULL);
    touch_map_apply(&m, 0, 0, NULL, NULL);
}

/* --------------------------------------------------------------- tracker */

TEST_CASE(tracker_reports_down_move_up)
{
    touch_tracker_t t;
    touch_tracker_reset(&t);
    touch_event_t ev[8];

    touch_point_t a = { .id = 1, .x = 10, .y = 20 };
    CHECK_EQ(touch_tracker_update(&t, &a, 1, ev, 8), 1);
    CHECK_EQ(ev[0].type, TOUCH_EVENT_DOWN);
    CHECK_EQ(ev[0].point.x, 10);

    /* Same position: no event. */
    CHECK_EQ(touch_tracker_update(&t, &a, 1, ev, 8), 0);

    a.x = 11;
    CHECK_EQ(touch_tracker_update(&t, &a, 1, ev, 8), 1);
    CHECK_EQ(ev[0].type, TOUCH_EVENT_MOVE);
    CHECK_EQ(ev[0].point.x, 11);

    CHECK_EQ(touch_tracker_update(&t, NULL, 0, ev, 8), 1);
    CHECK_EQ(ev[0].type, TOUCH_EVENT_UP);
    CHECK_EQ(ev[0].point.x, 11);

    /* Nothing left to release. */
    CHECK_EQ(touch_tracker_update(&t, NULL, 0, ev, 8), 0);
}

TEST_CASE(tracker_handles_multiple_contacts)
{
    touch_tracker_t t;
    touch_tracker_reset(&t);
    touch_event_t ev[8];

    touch_point_t two[2] = {
        { .id = 1, .x = 1, .y = 1 },
        { .id = 2, .x = 2, .y = 2 },
    };
    CHECK_EQ(touch_tracker_update(&t, two, 2, ev, 8), 2);
    CHECK_EQ(ev[0].type, TOUCH_EVENT_DOWN);
    CHECK_EQ(ev[1].type, TOUCH_EVENT_DOWN);

    /* Lift id 1, move id 2: the release is reported first. */
    touch_point_t one[1] = { { .id = 2, .x = 5, .y = 5 } };
    CHECK_EQ(touch_tracker_update(&t, one, 1, ev, 8), 2);
    CHECK_EQ(ev[0].type, TOUCH_EVENT_UP);
    CHECK_EQ(ev[0].point.id, 1);
    CHECK_EQ(ev[1].type, TOUCH_EVENT_MOVE);
    CHECK_EQ(ev[1].point.id, 2);

    /* A brand new id while another is held is a DOWN, not a MOVE. */
    touch_point_t mixed[2] = {
        { .id = 2, .x = 5, .y = 5 },
        { .id = 7, .x = 9, .y = 9 },
    };
    CHECK_EQ(touch_tracker_update(&t, mixed, 2, ev, 8), 1);
    CHECK_EQ(ev[0].type, TOUCH_EVENT_DOWN);
    CHECK_EQ(ev[0].point.id, 7);
}

TEST_CASE(tracker_clamps_and_survives_small_buffers)
{
    touch_tracker_t t;
    touch_tracker_reset(&t);
    touch_event_t ev[1];

    touch_point_t pts[TOUCH_MAX_POINTS + 3];
    for (int i = 0; i < TOUCH_MAX_POINTS + 3; ++i) {
        pts[i].id = (uint8_t)i;
        pts[i].x = (int16_t)i;
        pts[i].y = (int16_t)i;
        pts[i].strength = 0;
    }

    /* More contacts than the controller can produce are truncated, and only
     * as many events as fit are written -- but state still advances. */
    CHECK_EQ(touch_tracker_update(&t, pts, TOUCH_MAX_POINTS + 3, ev, 1), 1);
    CHECK_EQ(t.prev_count, TOUCH_MAX_POINTS);

    /* Releasing everything: 5 UPs happened, 1 fits. */
    CHECK_EQ(touch_tracker_update(&t, NULL, 0, ev, 1), 1);
    CHECK_EQ(t.prev_count, 0);

    /* Negative counts and NULL output are tolerated. */
    CHECK_EQ(touch_tracker_update(&t, pts, -4, ev, 1), 0);
    CHECK_EQ(touch_tracker_update(&t, pts, 2, NULL, 8), 0);
    CHECK_EQ(t.prev_count, 2);
    CHECK_EQ(touch_tracker_update(NULL, pts, 2, ev, 1), 0);

    touch_tracker_reset(NULL);
}

/*
 * The GT911 reuses track ids, and the tracker matched on id alone.  A missed
 * release frame therefore looked like the same finger crossing the panel in
 * one poll: the consumer got a MOVE where it was waiting for an UP, and on the
 * bench that left the throttle drag latched to whatever moved next.
 */
TEST_CASE(a_contact_that_teleports_is_a_release_and_a_new_press)
{
    touch_tracker_t t;
    memset(&t, 0, sizeof(t));
    touch_event_t ev[8];

    touch_point_t a = { .id = 0, .x = 120, .y = 338 };
    CHECK_EQ(touch_tracker_update(&t, &a, 1, ev, 8), 1);
    CHECK_EQ(ev[0].type, TOUCH_EVENT_DOWN);

    /* A real drag is still a drag. */
    touch_point_t near = { .id = 0, .x = 150, .y = 350 };
    CHECK_EQ(touch_tracker_update(&t, &near, 1, ev, 8), 1);
    CHECK_EQ(ev[0].type, TOUCH_EVENT_MOVE);

    /* Further than a finger can move in one poll: UP then DOWN, in that
     * order, so a consumer that latches on DOWN is released first. */
    touch_point_t far_away = { .id = 0, .x = 700, .y = 60 };
    CHECK_EQ(touch_tracker_update(&t, &far_away, 1, ev, 8), 2);
    CHECK_EQ(ev[0].type, TOUCH_EVENT_UP);
    CHECK_EQ(ev[0].point.x, 150);          /* released where it last was */
    CHECK_EQ(ev[1].type, TOUCH_EVENT_DOWN);
    CHECK_EQ(ev[1].point.x, 700);

    /* And the new position is now the tracked one. */
    touch_point_t after = { .id = 0, .x = 705, .y = 62 };
    CHECK_EQ(touch_tracker_update(&t, &after, 1, ev, 8), 1);
    CHECK_EQ(ev[0].type, TOUCH_EVENT_MOVE);
}

/* Two contacts with the same track id in one frame is the controller
 * misbehaving, and it produced two DOWNs now and two UPs later for one
 * finger. */
TEST_CASE(a_duplicate_track_id_in_one_frame_is_counted_once)
{
    touch_tracker_t t;
    memset(&t, 0, sizeof(t));
    touch_event_t ev[8];

    touch_point_t dup[2] = { { .id = 3, .x = 10, .y = 10 },
                             { .id = 3, .x = 400, .y = 300 } };
    CHECK_EQ(touch_tracker_update(&t, dup, 2, ev, 8), 1);
    CHECK_EQ(ev[0].type, TOUCH_EVENT_DOWN);
    CHECK_EQ(ev[0].point.x, 10);

    /* Lifting produces exactly one UP, not two. */
    CHECK_EQ(touch_tracker_update(&t, NULL, 0, ev, 8), 1);
    CHECK_EQ(ev[0].type, TOUCH_EVENT_UP);
}

int main(void)
{
    RUN(identity_mapping);
    RUN(rotation_90_swaps_axes);
    RUN(rotation_180_flips_both);
    RUN(rotation_270_swaps_the_other_way);
    RUN(mirrors_apply_before_rotation);
    RUN(out_of_range_input_is_clamped);
    RUN(map_point_updates_in_place);
    RUN(tracker_reports_down_move_up);
    RUN(tracker_handles_multiple_contacts);
    RUN(tracker_clamps_and_survives_small_buffers);
    RUN(a_contact_that_teleports_is_a_release_and_a_new_press);
    RUN(a_duplicate_track_id_in_one_frame_is_counted_once);
    return test_summary("touch_map");
}
