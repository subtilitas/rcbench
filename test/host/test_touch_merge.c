/*
 * The glass's contacts and a touch injector's, merged (shared/touch,
 * touch_merge.h), and what the merge must never do: hide or delay a STOP
 * pressed on the glass, or keep touch healthy while the injector is silent.
 *
 * The panel's path is the same as here: the merged frame goes through the
 * tracker, and the tracker's events to ui_router_event().  Touch health is
 * firmware/panel/main/main.c's rule: touch_age_ms() under 200 ms counts as
 * touch seen, and arming.h takes it from there.
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include "greatest.h"

#include "arming.h"
#include "heartbeat.h"
#include "splash_screen.h"
#include "touch_map.h"
#include "touch_merge.h"
#include "ui_band.h"
#include "ui_screen.h"
#include "ui_theme.h"

/* main.c: touch younger than this is touch seen. */
#define PANEL_TOUCH_FRESH_MS 200u

static touch_point_t pt(uint8_t id, int x, int y)
{
    return (touch_point_t){ .id = id, .x = (int16_t)x, .y = (int16_t)y,
                            .strength = 24 };
}

/* --------------------------------------------------------- the merge */

TEST_CASE(glass_first_and_injected_ids_marked)
{
    touch_merge_t m;
    touch_merge_reset(&m);
    const touch_point_t g[2] = { pt(0, 10, 10), pt(1, 20, 20) };
    const touch_point_t i[1] = { pt(0, 300, 300) };
    touch_merge_glass(&m, g, 2);
    CHECK(touch_merge_inject(&m, i, 1, true, 0));

    touch_point_t out[TOUCH_MAX_POINTS];
    CHECK_EQ(touch_merge_frame(&m, out, TOUCH_MAX_POINTS), 3);
    CHECK_EQ(out[0].id, 0);
    CHECK_EQ(out[0].x, 10);
    CHECK_EQ(out[1].id, 1);
    CHECK_EQ(out[2].id, 0x80);
    CHECK_EQ(out[2].x, 300);
    CHECK_EQ(out[2].strength, 24);
}

/* Five points fill a frame: the injector's are the ones left out. */
TEST_CASE(a_full_frame_drops_injected_points_never_glass)
{
    touch_merge_t m;
    touch_merge_reset(&m);
    touch_point_t g[TOUCH_MAX_POINTS];
    touch_point_t inj[TOUCH_MAX_POINTS];
    for (int k = 0; k < TOUCH_MAX_POINTS; ++k) {
        g[k] = pt((uint8_t)k, 10 * k, 10);
        inj[k] = pt((uint8_t)k, 10 * k, 300);
    }
    touch_point_t out[TOUCH_MAX_POINTS];

    touch_merge_glass(&m, g, TOUCH_MAX_POINTS);
    (void)touch_merge_inject(&m, inj, TOUCH_MAX_POINTS, true, 0);
    CHECK_EQ(touch_merge_frame(&m, out, TOUCH_MAX_POINTS), 5);
    for (int k = 0; k < TOUCH_MAX_POINTS; ++k) {
        CHECK_EQ(out[k].y, 10);
        CHECK(out[k].id < 0x80);
    }

    touch_merge_glass(&m, g, 1);
    CHECK_EQ(touch_merge_frame(&m, out, TOUCH_MAX_POINTS), 5);
    CHECK_EQ(out[0].y, 10);
    for (int k = 1; k < TOUCH_MAX_POINTS; ++k) {
        CHECK_EQ(out[k].y, 300);
    }

    /* A small buffer, a count past the limit, and nothing at all. */
    CHECK_EQ(touch_merge_frame(&m, out, 2), 2);
    CHECK_EQ(touch_merge_frame(&m, out, 0), 0);
    CHECK_EQ(touch_merge_frame(&m, NULL, 5), 0);
    CHECK_EQ(touch_merge_frame(NULL, out, 5), 0);
    touch_merge_glass(&m, g, 99);
    CHECK_EQ(m.glass_n, TOUCH_MAX_POINTS);
    touch_merge_glass(&m, g, -3);
    CHECK_EQ(m.glass_n, 0);
    touch_merge_glass(&m, NULL, 2);
    CHECK_EQ(m.glass_n, 0);
}

TEST_CASE(a_source_with_no_report_keeps_its_set)
{
    touch_merge_t m;
    touch_merge_reset(&m);
    const touch_point_t i[1] = { pt(0, 50, 60) };
    CHECK(touch_merge_inject(&m, i, 1, true, 0));
    /* Answered, nothing new: unchanged. */
    CHECK(!touch_merge_inject(&m, NULL, 0, false, 10));
    CHECK_EQ(m.inject_n, 1);
    /* The same point again: no change either. */
    CHECK(!touch_merge_inject(&m, i, 1, true, 20));
    /* Moved, or lifted: a change. */
    const touch_point_t j[1] = { pt(0, 51, 60) };
    CHECK(touch_merge_inject(&m, j, 1, true, 30));
    CHECK(touch_merge_inject(&m, NULL, 0, true, 40));
    CHECK_EQ(m.inject_n, 0);
}

TEST_CASE(three_failed_reads_drop_the_injected_contacts)
{
    touch_merge_t m;
    touch_merge_reset(&m);
    const touch_point_t i[1] = { pt(0, 50, 60) };
    (void)touch_merge_inject(&m, i, 1, true, 0);

    CHECK(touch_merge_inject_due(&m, 10));
    CHECK(!touch_merge_inject_failed(&m, 10));
    CHECK(touch_merge_inject_due(&m, 20));
    CHECK(!touch_merge_inject_failed(&m, 20));
    CHECK(touch_merge_inject_due(&m, 20));   /* two failures: still every poll */
    CHECK_EQ(m.inject_n, 1);
    CHECK(touch_merge_inject_failed(&m, 30));   /* the third */
    CHECK_EQ(m.inject_n, 0);
    CHECK(!touch_merge_inject_failed(&m, 30));   /* nothing left to drop */

    /* Asked again only every 100 ms from the last try. */
    CHECK(!touch_merge_inject_due(&m, 31));
    CHECK(!touch_merge_inject_due(&m, 129));
    CHECK(touch_merge_inject_due(&m, 130));
    (void)touch_merge_inject_failed(&m, 130);
    CHECK(!touch_merge_inject_due(&m, 229));
    CHECK(touch_merge_inject_due(&m, 230));

    /* An answer puts it back on every poll. */
    CHECK(touch_merge_inject(&m, i, 1, true, 230));
    CHECK(touch_merge_inject_due(&m, 231));
    CHECK_EQ(m.fails, 0);
}

TEST_CASE(the_retry_timer_survives_the_clock_wrap)
{
    touch_merge_t m;
    touch_merge_reset(&m);
    const uint32_t t = UINT32_MAX - 40u;
    for (int k = 0; k < 3; ++k) {
        (void)touch_merge_inject_failed(&m, t);
    }
    CHECK(!touch_merge_inject_due(&m, t + 50u));
    CHECK(touch_merge_inject_due(&m, t + 100u));
}

TEST_CASE(touch_is_as_old_as_the_older_source)
{
    CHECK_EQ(touch_merge_age(0, 0), 0);
    CHECK_EQ(touch_merge_age(5, 600), 600);
    CHECK_EQ(touch_merge_age(700, 3), 700);
    CHECK_EQ(touch_merge_age(UINT32_MAX, 0), UINT32_MAX);
}

/* A frame shorter than the glass's set holds the first glass points and
 * writes nothing past its end. */
TEST_CASE(a_short_frame_writes_nothing_past_its_end)
{
    touch_merge_t m;
    touch_merge_reset(&m);
    const touch_point_t g[2] = { pt(0, 10, 10), pt(1, 20, 20) };
    touch_merge_glass(&m, g, 2);
    touch_point_t f[3];
    f[1] = pt(9, 999, 999);
    CHECK_EQ(touch_merge_frame(&m, f, 1), 1);
    CHECK_EQ(f[0].x, 10);
    CHECK_EQ(f[1].x, 999);
    CHECK_EQ(touch_merge_frame(&m, f, 0), 0);
    CHECK_EQ(touch_merge_frame(&m, f, -1), 0);
}

/* A count past TOUCH_MAX_POINTS keeps the first five; a negative one is
 * none. */
TEST_CASE(counts_are_clamped_to_the_frame)
{
    touch_merge_t m;
    touch_merge_reset(&m);
    touch_point_t six[TOUCH_MAX_POINTS + 1];
    for (int k = 0; k <= TOUCH_MAX_POINTS; ++k) {
        six[k] = pt((uint8_t)k, 10 * k, 10);
    }
    touch_merge_glass(&m, six, TOUCH_MAX_POINTS + 1);
    CHECK_EQ(m.glass_n, TOUCH_MAX_POINTS);
    (void)touch_merge_inject(&m, six, TOUCH_MAX_POINTS + 1, true, 0);
    CHECK_EQ(m.inject_n, TOUCH_MAX_POINTS);
    touch_merge_glass(&m, six, TOUCH_MAX_POINTS);
    CHECK_EQ(m.glass_n, TOUCH_MAX_POINTS);
    touch_merge_glass(&m, six, -1);
    CHECK_EQ(m.glass_n, 0);
    CHECK(touch_merge_inject(&m, six, -1, true, 10));
    CHECK_EQ(m.inject_n, 0);
}

TEST_CASE(null_merges_are_harmless)
{
    touch_merge_reset(NULL);
    touch_merge_glass(NULL, NULL, 1);
    CHECK(!touch_merge_inject_due(NULL, 0));
    CHECK(!touch_merge_inject(NULL, NULL, 0, true, 0));
    CHECK(!touch_merge_inject_failed(NULL, 0));
}

/* ------------------------------------------- STOP, through the router */

static touch_merge_t   mg;
static touch_tracker_t trk;

static void panel_fresh(void)
{
    ui_theme_set(UI_THEME_DARK);
    ui_router_init();
    for (int i = 0; i < SPLASH_STEP_COUNT; ++i) {
        splash_screen_set((splash_step_t)i, SPLASH_OK, "");
    }
    ui_router_tick(2.0f);
    ui_router_goto(SCREEN_OVERVIEW);
    touch_merge_reset(&mg);
    touch_tracker_reset(&trk);
    (void)ui_router_take_stop();
}

/* The merged frame, through the tracker, to the router: touch.c's path. */
static void publish(void)
{
    touch_point_t f[TOUCH_MAX_POINTS];
    const int n = touch_merge_frame(&mg, f, TOUCH_MAX_POINTS);
    touch_event_t ev[TOUCH_MAX_POINTS * 2];
    const int k = touch_tracker_update(&trk, f, n, ev,
                                       (int)(sizeof(ev) / sizeof(ev[0])));
    for (int i = 0; i < k; ++i) {
        ui_router_event(&ev[i]);
    }
}

static touch_point_t stop_point(void)
{
    const gfx_rect_t r = ui_band_stop_rect();
    return pt(0, r.x + r.w / 2, r.y + r.h / 2);
}

/* The router latches STOP on the release of a press that came down on it
 * and is still on it (ui_router.c): each case lifts the finger. */
static void glass_lift(void)
{
    touch_merge_glass(&mg, NULL, 0);
    publish();
}

/* An injected hold is under way on a screen; the operator presses STOP on
 * the glass.  The router latches it. */
TEST_CASE(stop_on_the_glass_wins_over_an_injected_hold)
{
    panel_fresh();
    const touch_point_t hold[1] = { pt(0, 400, 300) };
    CHECK(touch_merge_inject(&mg, hold, 1, true, 0));
    publish();
    CHECK(!ui_router_take_stop());

    const touch_point_t stop[1] = { stop_point() };
    touch_merge_glass(&mg, stop, 1);
    publish();
    glass_lift();
    CHECK(ui_router_take_stop());
}

/* Five injected contacts, then STOP on the glass: the glass's point is in
 * the frame and an injected one makes room. */
TEST_CASE(stop_on_the_glass_wins_over_five_injected_contacts)
{
    panel_fresh();
    touch_point_t inj[TOUCH_MAX_POINTS];
    for (int k = 0; k < TOUCH_MAX_POINTS; ++k) {
        inj[k] = pt((uint8_t)k, 100 + 60 * k, 300);
    }
    (void)touch_merge_inject(&mg, inj, TOUCH_MAX_POINTS, true, 0);
    publish();
    const touch_point_t stop[1] = { stop_point() };
    touch_merge_glass(&mg, stop, 1);
    publish();
    glass_lift();
    CHECK(ui_router_take_stop());
}

/* The injector fails while STOP is pressed: the drop of its contacts does
 * not take the glass's with it. */
TEST_CASE(stop_survives_an_injector_that_fails)
{
    panel_fresh();
    const touch_point_t hold[1] = { pt(0, 400, 300) };
    (void)touch_merge_inject(&mg, hold, 1, true, 0);
    publish();
    const touch_point_t stop[1] = { stop_point() };
    touch_merge_glass(&mg, stop, 1);
    for (uint32_t t = 10; t <= 30; t += 10) {
        if (touch_merge_inject_failed(&mg, t)) {
            publish();
        }
    }
    CHECK_EQ(mg.inject_n, 0);
    publish();
    touch_point_t f[TOUCH_MAX_POINTS];
    CHECK_EQ(touch_merge_frame(&mg, f, TOUCH_MAX_POINTS), 1);
    CHECK_EQ(f[0].x, stop[0].x);
    glass_lift();
    CHECK(ui_router_take_stop());
}

/* The injector at STOP: a STOP pressed by a recipe is a STOP too. */
TEST_CASE(an_injected_stop_stops)
{
    panel_fresh();
    const touch_point_t stop[1] = { stop_point() };
    (void)touch_merge_inject(&mg, stop, 1, true, 0);
    publish();
    CHECK(!ui_router_take_stop());
    (void)touch_merge_inject(&mg, NULL, 0, true, 10);
    publish();
    CHECK(ui_router_take_stop());
}

/* ---------------------------------------- injector silence is SAFE */

/* What the panel passes: four good intervals, plus one (test_arming.c). */
#define SETTLE_MS (HEARTBEAT_GOOD_RUN * HEARTBEAT_PERIOD_MS + HEARTBEAT_PERIOD_MS)

/*
 * The glass answers every 10 ms throughout; the injector stops at 1000 ms.
 * Touch reads as seen until its age reaches 200 ms, and ARMING_TOUCH_DEAD_MS
 * (500 ms) after the last time it was seen the bench disarms: at most
 * 700 ms after the injector's last answer, the bound the glass has.
 */
TEST_CASE(a_silent_injector_takes_the_bench_to_safe)
{
    arming_t a;
    arming_init(&a, 0, SETTLE_MS);
    arming_touch_seen(&a, 0);
    arming_request_arm(&a, 0);
    uint32_t disarmed_at = 0;
    const uint32_t inj_stops = 1000;
    uint32_t inj_ok = 0;
    for (uint32_t t = 10; t <= 3000; t += 10) {
        if (t < inj_stops) {
            inj_ok = t;
        }
        const uint32_t age = touch_merge_age(0, t - inj_ok);
        if (age < PANEL_TOUCH_FRESH_MS) {
            arming_touch_seen(&a, t);
        }
        arming_touch_poll(&a, t);
        const arming_action_t act = arming_step(&a, t);
        if (t < inj_stops && t > SETTLE_MS + 20u) {
            CHECK(a.armed);
        }
        if (act == ARMING_ACT_DISARM && disarmed_at == 0u) {
            disarmed_at = t;
        }
    }
    const uint32_t last_ok = inj_stops - 10u;
    CHECK(disarmed_at > last_ok + ARMING_TOUCH_DEAD_MS);
    CHECK(disarmed_at <= last_ok + PANEL_TOUCH_FRESH_MS + ARMING_TOUCH_DEAD_MS);
    CHECK(!a.armed);
    CHECK(!arming_heartbeat(&a, 3000));
}

/* With no injector attached its age is 0: the glass alone decides. */
TEST_CASE(no_injector_leaves_the_glass_alone)
{
    CHECK_EQ(touch_merge_age(150, 0), 150);
    CHECK(touch_merge_age(0, 0) < PANEL_TOUCH_FRESH_MS);
}

int main(void)
{
    RUN(glass_first_and_injected_ids_marked);
    RUN(a_full_frame_drops_injected_points_never_glass);
    RUN(a_source_with_no_report_keeps_its_set);
    RUN(three_failed_reads_drop_the_injected_contacts);
    RUN(the_retry_timer_survives_the_clock_wrap);
    RUN(touch_is_as_old_as_the_older_source);
    RUN(a_short_frame_writes_nothing_past_its_end);
    RUN(counts_are_clamped_to_the_frame);
    RUN(null_merges_are_harmless);
    RUN(stop_on_the_glass_wins_over_an_injected_hold);
    RUN(stop_on_the_glass_wins_over_five_injected_contacts);
    RUN(stop_survives_an_injector_that_fails);
    RUN(an_injected_stop_stops);
    RUN(a_silent_injector_takes_the_bench_to_safe);
    RUN(no_injector_leaves_the_glass_alone);
    return test_summary("touch_merge");
}
