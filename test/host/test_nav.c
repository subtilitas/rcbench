/*
 * The shell: the band, the router, and the two things about them that are
 * safety properties rather than conveniences.
 *
 * STOP has to work from every screen that can have something armed behind it,
 * and no screen may draw over it.  The second is enforced by handing screens a
 * sub-canvas rather than by asking them nicely, and this file checks the
 * enforcement rather than the manners.
 */
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "gfx.h"
#include "overview_screen.h"
#include "splash_screen.h"
#include "stub_screen.h"
#include "ui_band.h"
#include "ui_screen.h"
#include "ui_theme.h"
#include "ui_widgets.h"

#define W 800
#define H 480

static gfx_color_t *fb;
static gfx_canvas_t cv;

static void fresh(void)
{
    if (fb == NULL) {
        fb = calloc((size_t)W * H, sizeof(gfx_color_t));
    }
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    gfx_canvas_init(&cv, fb, W, H, W);
    ui_theme_set(UI_THEME_DARK);
    ui_router_init();
}

static void touch(int x, int y, touch_event_type_t type, uint8_t id)
{
    touch_event_t e = { .type = type,
                        .point = { .id = id, .x = (int16_t)x,
                                   .y = (int16_t)y, .strength = 40 } };
    ui_router_event(&e);
}

static void tap(int x, int y)
{
    touch(x, y, TOUCH_EVENT_DOWN, 1);
    touch(x, y, TOUCH_EVENT_UP, 1);
}

/** Leave the splash the way the application does. */
static void to_overview(void)
{
    for (int i = 0; i < SPLASH_STEP_COUNT; ++i) {
        splash_screen_set((splash_step_t)i, SPLASH_OK, "");
    }
    ui_router_tick(2.0f);
    ui_router_goto(SCREEN_OVERVIEW);
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
 * still there.  Every screen begins by clearing its canvas, so if it were
 * handed the panel instead of a window into it, STOP would be erased on the
 * way past -- silently, and looking like a cosmetic glitch rather than the
 * safety problem it is.
 *
 * An earlier version of this case rendered twice and compared the two band
 * regions.  Both were wiped identically when the sub-canvas was removed, so it
 * passed against exactly the bug it was written for.  It now looks for the
 * band's own pixels rather than for agreement between two renders.
 */
TEST_CASE(no_screen_can_draw_over_the_band)
{
    fresh();
    to_overview();

    const gfx_rect_t stop = ui_band_stop_rect();
    const gfx_color_t danger = ui_theme_color(UI_C_DANGER);

    for (int id = SCREEN_OVERVIEW; id < SCREEN_COUNT; ++id) {
        ui_router_goto((ui_screen_id_t)id);
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
 * drains it -- a stop stops the line as well as sending the command. */
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
        ui_router_goto((ui_screen_id_t)id);
        (void)ui_router_take_stop();
        tap(r.x + r.w / 2, r.y + r.h / 2);
        if (!ui_router_take_stop()) {
            T_FAIL("STOP did nothing on screen %d", id);
        }
    }
}

/* A press that begins on STOP and slides off is not a stop -- but neither is
 * it a tap on whatever it slid onto. */
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
 * for.  On the bench screen that used to leave the throttle drag latched to
 * whatever moved next. */
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

/* Tiles navigate, and a press that slid off its tile is not a tap on it. */
TEST_CASE(a_tile_navigates_and_a_slip_does_not)
{
    fresh();
    to_overview();

    /* First tile: top-left of the body, which is UI_BAND_H down the panel. */
    tap(110, UI_BAND_H + 90);
    CHECK_EQ(ui_router_current(), SCREEN_MOTOR);

    ui_router_goto(SCREEN_OVERVIEW);
    touch(110, UI_BAND_H + 90, TOUCH_EVENT_DOWN, 1);
    touch(310, UI_BAND_H + 90, TOUCH_EVENT_MOVE, 1);
    touch(310, UI_BAND_H + 90, TOUCH_EVENT_UP, 1);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
}

/* Every stub says what it will do, and either names a blocker or says plainly
 * that nothing is blocking it.  A screen that renders nothing is a screen
 * nobody notices is empty. */
TEST_CASE(every_stub_renders_something_and_says_something)
{
    fresh();
    to_overview();
    for (int id = SCREEN_MOTOR; id < SCREEN_COUNT; ++id) {
        ui_router_goto((ui_screen_id_t)id);
        memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
        ui_router_render(&cv, 0);

        int lit = 0;
        for (int y = UI_BAND_H; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                if (fb[(size_t)y * W + x] != ui_theme_color(UI_C_BG)) {
                    ++lit;
                }
            }
        }
        if (lit < 2000) {
            T_FAIL("screen %d drew only %d pixels", id, lit);
        }
        CHECK(ui_router_title((ui_screen_id_t)id)[0] != '\0');
    }
}

/* The alert survives a change of screen, because it describes the bench and
 * not the screen. */
TEST_CASE(an_alert_survives_navigation)
{
    fresh();
    to_overview();
    ui_router_set_alert("touch controller stopped answering");
    ui_router_goto(SCREEN_LOGS);
    CHECK(ui_router_alert() != NULL);
    ui_router_set_alert(NULL);
    CHECK(ui_router_alert() == NULL);
}

/* The band's other states.  A fault badge that has never been rendered is a
 * fault badge nobody has seen, and this is the one place it can be looked at
 * without breaking something on a bench first. */
TEST_CASE(the_band_shows_what_is_wrong)
{
    fresh();
    to_overview();

    const ui_bench_status_t bad = {
        .link_up = false, .armed = true, .faults = 0x11,
        .run_seconds = 0, .mode = NULL,
    };
    ui_router_set_status(&bad);
    ui_router_invalidate();
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    ui_router_render(&cv, 0);

    /* Armed and faulted both paint in the danger and warning colours, and
     * neither is the background. */
    int danger = 0, warn = 0;
    for (int y = 0; y < UI_BAND_H; ++y) {
        for (int x = 0; x < W; ++x) {
            const gfx_color_t p = fb[(size_t)y * W + x];
            if (p == ui_theme_color(UI_C_DANGER)) { ++danger; }
            if (p == ui_theme_color(UI_C_WARN))   { ++warn; }
        }
    }
    CHECK(danger > 400);   /* STOP, and the ARMED badge */
    CHECK(warn > 100);     /* the fault chip */

    CHECK_EQ(ui_router_status()->faults, 0x11);
}

/* The splash holds until every step has answered, then hands over.  A tap
 * skips the hold -- but only once there is something to have read. */
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

int main(void)
{
    RUN(the_router_starts_on_the_splash);
    RUN(the_splash_has_no_band_and_everything_else_does);
    RUN(no_screen_can_draw_over_the_band);
    RUN(stop_latches_and_clears_when_read);
    RUN(stop_works_from_every_screen_that_has_a_band);
    RUN(a_press_that_slides_off_stop_does_nothing);
    RUN(the_home_tag_returns_to_the_overview);
    RUN(the_overview_has_no_home_tag);
    RUN(a_second_contact_cannot_steal_the_release);
    RUN(a_tile_navigates_and_a_slip_does_not);
    RUN(every_stub_renders_something_and_says_something);
    RUN(an_alert_survives_navigation);
    RUN(the_band_shows_what_is_wrong);
    RUN(the_splash_holds_then_hands_over);
    RUN(a_tap_skips_the_splash_hold);
    RUN(a_failed_step_is_drawn_in_its_own_colour);
    RUN(the_alert_band_renders_at_the_bottom);
    return test_summary("nav");
}
