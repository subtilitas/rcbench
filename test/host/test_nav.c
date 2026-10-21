/*
 * The shell: the band, the router, and the two safety properties they carry.
 *
 * STOP has to work from every screen that can have something armed behind it,
 * and no screen may draw over it.  The second is enforced by handing screens a
 * sub-canvas, and this file checks the enforcement.
 *
 * This suite holds the router, the splash, the band and STOP.  The tiles, the
 * alerts, the simulation mark, the owner of a contact and the bubbles are in
 * test_nav_*.c.
 *
 * SPDX-License-Identifier: MIT
 */

#include "nav_rig.h"

/* The count moves on every switch to another screen, so away and back to the
 * same screen reads as a change although the screen is the same. */
TEST_CASE(the_navigation_count_sees_away_and_back)
{
    fresh();
    const uint32_t n0 = ui_router_navigations();
    ui_router_goto(SCREEN_SPLASH);              /* already there */
    CHECK_EQ(ui_router_navigations(), n0);
    to_overview();
    CHECK(ui_router_navigations() != n0);

    const uint32_t n1 = ui_router_navigations();
    const ui_screen_id_t here = ui_router_current();
    ui_router_goto(SCREEN_MOTOR);
    ui_router_goto(here);
    CHECK_EQ(ui_router_current(), here);
    CHECK_EQ(ui_router_navigations(), n1 + 2u);
}

TEST_CASE(the_router_starts_on_the_splash)
{
    fresh();
    CHECK_EQ(ui_router_current(), SCREEN_SPLASH);
}

/* The splash has nothing to stop and nowhere to go back to, so it owns the
 * whole panel.  Everything else carries the band. */
TEST_CASE(the_splash_has_no_band_and_everything_else_does)
{
    fresh();
    ui_router_render(&cv, 0);
    /* Nothing in the band's rows should look like a STOP button on the
     * splash: the screen painted the whole canvas itself. */
    const gfx_rect_t stop = ui_band_stop_rect();
    const gfx_color_t danger = ui_theme_color(UI_C_DANGER);
    bool found = false;
    for (int y = stop.y; y < stop.y + stop.h && !found; ++y) {
        for (int x = stop.x; x < stop.x + stop.w; ++x) {
            if (fb[(size_t)y * W + x] == danger) { found = true; break; }
        }
    }
    CHECK(!found);

    to_overview();
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    ui_router_render(&cv, 0);
    found = false;
    for (int y = stop.y; y < stop.y + stop.h && !found; ++y) {
        for (int x = stop.x; x < stop.x + stop.w; ++x) {
            if (fb[(size_t)y * W + x] == danger) { found = true; break; }
        }
    }
    CHECK(found);
}

/*
 * The property the sub-canvas exists for: after a full render, the band is
 * still there.  Every screen begins by clearing its canvas, so a screen handed
 * the panel instead of a window into it erases STOP.
 *
 * The check looks for the band's own pixels.  Comparing two renders of the
 * band region is not sufficient: without the sub-canvas both are wiped
 * identically.
 */
TEST_CASE(no_screen_can_draw_over_the_band)
{
    fresh();
    to_overview();

    const gfx_rect_t stop = ui_band_stop_rect();
    const gfx_color_t danger = ui_theme_color(UI_C_DANGER);

    for (int id = SCREEN_OVERVIEW; id < SCREEN_COUNT; ++id) {
        ui_router_goto((ui_screen_id_t)id);
        if (id == SCREEN_UPDATE) {
            /* The firmware write draws the whole panel and offers no STOP:
             * asserted, not skipped. */
            CHECK(!ui_router_stop_live());
            continue;
        }
        memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
        ui_router_render(&cv, 0);

        int lit = 0;
        for (int y = stop.y; y < stop.y + stop.h; ++y) {
            for (int x = stop.x; x < stop.x + stop.w; ++x) {
                if (fb[(size_t)y * W + x] == danger) {
                    ++lit;
                }
            }
        }
        if (lit < 200) {
            T_FAIL("screen %d left only %d STOP pixels standing", id, lit);
        }
    }
}

/* STOP is latched rather than dispatched, so the loop that owns the heartbeat
 * drains it: a stop stops the line as well as sending the command. */
TEST_CASE(stop_latches_and_clears_when_read)
{
    fresh();
    to_overview();
    CHECK(!ui_router_take_stop());

    const gfx_rect_t r = ui_band_stop_rect();
    tap(r.x + r.w / 2, r.y + r.h / 2);
    CHECK(ui_router_take_stop());
    CHECK(!ui_router_take_stop());   /* read once, gone */
}

/* Every screen that can have something armed behind it. */
TEST_CASE(stop_works_from_every_screen_that_has_a_band)
{
    fresh();
    to_overview();
    const gfx_rect_t r = ui_band_stop_rect();

    for (int id = SCREEN_OVERVIEW; id < SCREEN_COUNT; ++id) {
        /*
         * The bus-fault screen is the one exception, and it is asserted
         * rather than skipped: it has no band because nothing can be armed
         * behind it -- the panel is on a bus that does not carry frames --
         * and a STOP there would offer to stop something that is not
         * running.
         */
        if (id == SCREEN_BUSFAULT || id == SCREEN_UPDATE) {
            /* The firmware write likewise: the bench is SAFE and the
             * IOmcu in failsafe while the panel writes its flash. */
            ui_router_goto((ui_screen_id_t)id);
            CHECK(!ui_router_stop_live());
            (void)ui_router_take_stop();
            tap(r.x + r.w / 2, r.y + r.h / 2);
            if (ui_router_take_stop()) {
                T_FAIL("screen %d latched a stop it does not draw", id);
                return;
            }
            continue;
        }
        ui_router_goto((ui_screen_id_t)id);
        (void)ui_router_take_stop();
        tap(r.x + r.w / 2, r.y + r.h / 2);
        if (!ui_router_take_stop()) {
            T_FAIL("STOP did nothing on screen %d", id);
            return;
        }
    }
}

/* A press that begins on STOP and slides off is not a stop, and neither is it
 * a tap on whatever it slid onto. */
TEST_CASE(a_press_that_slides_off_stop_does_nothing)
{
    fresh();
    to_overview();
    ui_router_goto(SCREEN_MOTOR);
    const gfx_rect_t r = ui_band_stop_rect();

    touch(r.x + r.w / 2, r.y + r.h / 2, TOUCH_EVENT_DOWN, 1);
    touch(20, 300, TOUCH_EVENT_MOVE, 1);
    touch(20, 300, TOUCH_EVENT_UP, 1);
    CHECK(!ui_router_take_stop());
    CHECK_EQ(ui_router_current(), SCREEN_MOTOR);
}

TEST_CASE(the_home_tag_returns_to_the_overview)
{
    fresh();
    to_overview();
    ui_router_goto(SCREEN_ANALYSER);
    CHECK_EQ(ui_router_current(), SCREEN_ANALYSER);
    tap(UI_TAG_X + 20, UI_TAG_Y + UI_TAG_H / 2);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
}

/* The overview is where the home tag would take you, so it does not draw one:
 * an identity mark rather than a control. */
TEST_CASE(the_overview_has_no_home_tag)
{
    fresh();
    to_overview();
    tap(UI_TAG_X + 20, UI_TAG_Y + UI_TAG_H / 2);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
}

/* A second finger, or a palm, must not steal the release the first is waiting
 * for; otherwise the throttle drag latches to whatever moves next. */
TEST_CASE(a_second_contact_cannot_steal_the_release)
{
    fresh();
    to_overview();
    const gfx_rect_t r = ui_band_stop_rect();

    touch(r.x + r.w / 2, r.y + r.h / 2, TOUCH_EVENT_DOWN, 1);
    touch(r.x + r.w / 2, r.y + r.h / 2, TOUCH_EVENT_UP, 2);   /* not ours */
    CHECK(!ui_router_take_stop());
    touch(r.x + r.w / 2, r.y + r.h / 2, TOUCH_EVENT_UP, 1);
    CHECK(ui_router_take_stop());
}

/* The splash holds until every step has answered, then hands over.  A tap
 * skips the hold, but only once there is something to have read. */
TEST_CASE(the_splash_holds_then_hands_over)
{
    fresh();
    CHECK(!splash_screen_done());

    ui_router_tick(5.0f);
    CHECK(!splash_screen_done());        /* nothing has answered yet */

    tap(400, 300);
    CHECK(!splash_screen_done());        /* and a tap does not skip that */

    for (int i = 0; i < SPLASH_STEP_COUNT; ++i) {
        splash_screen_set((splash_step_t)i, SPLASH_OK, "ok");
    }
    ui_router_render(&cv, 0);
    CHECK(!splash_screen_done());        /* answered, but not yet held */

    ui_router_tick(2.0f);
    CHECK(splash_screen_done());
}

TEST_CASE(a_tap_skips_the_splash_hold)
{
    fresh();
    for (int i = 0; i < SPLASH_STEP_COUNT; ++i) {
        splash_screen_set((splash_step_t)i, SPLASH_WARN, "");
    }
    CHECK(!splash_screen_done());
    tap(400, 300);
    CHECK(splash_screen_done());
}

/* A failure is still reported: it renders, and it renders in its own colour,
 * so it can be read on the way past rather than stranding anyone. */
TEST_CASE(a_failed_step_is_drawn_in_its_own_colour)
{
    fresh();
    splash_screen_set(SPLASH_STEP_LINK, SPLASH_FAIL, "no answer");
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    ui_router_render(&cv, 0);

    int danger = 0;
    for (int i = 0; i < W * H; ++i) {
        if (fb[i] == ui_theme_color(UI_C_DANGER)) { ++danger; }
    }
    CHECK(danger > 0);
}

/* The alert is drawn across the bottom, over controls that are not working
 * anyway rather than over the numbers. */
TEST_CASE(the_alert_band_renders_at_the_bottom)
{
    fresh();
    to_overview();
    ui_router_set_alert("touch controller stopped answering");
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    ui_router_render(&cv, 0);

    int danger_bottom = 0;
    for (int y = H - 34; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            if (fb[(size_t)y * W + x] == ui_theme_color(UI_C_DANGER)) {
                ++danger_bottom;
            }
        }
    }
    CHECK(danger_bottom > 5000);
    ui_router_set_alert(NULL);
}

/* An id that names no screen draws the overview and can not be navigated to. */
TEST_CASE(an_unknown_screen_id_falls_back_to_the_overview)
{
    fresh();
    to_overview();
    CHECK_STR_EQ(ui_router_title(SCREEN_COUNT), ui_router_title(SCREEN_OVERVIEW));
    CHECK_STR_EQ(ui_router_title((ui_screen_id_t)-1),
                 ui_router_title(SCREEN_OVERVIEW));
    CHECK(ui_router_title(SCREEN_COUNT)[0] != '\0');

    ui_router_goto(SCREEN_COUNT);
    ui_router_goto((ui_screen_id_t)-1);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
}

int main(void)
{
    RUN(the_navigation_count_sees_away_and_back);
    RUN(the_router_starts_on_the_splash);
    RUN(the_splash_has_no_band_and_everything_else_does);
    RUN(no_screen_can_draw_over_the_band);
    RUN(stop_latches_and_clears_when_read);
    RUN(stop_works_from_every_screen_that_has_a_band);
    RUN(a_press_that_slides_off_stop_does_nothing);
    RUN(the_home_tag_returns_to_the_overview);
    RUN(the_overview_has_no_home_tag);
    RUN(a_second_contact_cannot_steal_the_release);
    RUN(an_unknown_screen_id_falls_back_to_the_overview);
    RUN(the_splash_holds_then_hands_over);
    RUN(a_tap_skips_the_splash_hold);
    RUN(a_failed_step_is_drawn_in_its_own_colour);
    RUN(the_alert_band_renders_at_the_bottom);
    return test_summary("nav");
}
