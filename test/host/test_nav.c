/*
 * The shell: the band, the router, and the two safety properties they carry.
 *
 * STOP has to work from every screen that can have something armed behind it,
 * and no screen may draw over it.  The second is enforced by handing screens a
 * sub-canvas, and this file checks the enforcement.
 *
 * And who owns a contact: one owner from its DOWN to its UP, the screen or
 * the router.  Those cases feed frames of contacts through the tracker
 * (touch_feed.h), as the panel does.
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"
#include "touch_feed.h"

#include "gfx.h"
#include "motor_screen.h"
#include "overview_screen.h"
#include "servo_screen.h"
#include "settings.h"
#include "settings_screen.h"
#include "splash_screen.h"
#include "supply_screen.h"
#include "ui_band.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_watermark.h"
#include "ui_widgets.h"

#define W 800
#define H 480

static gfx_color_t *fb;
static gfx_canvas_t cv;

static void fresh(void)
{
    if (fb == NULL) {
        fb = calloc((size_t)W * H, sizeof(gfx_color_t));
        REQUIRE_ALLOC(fb);
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
             * coprocessor in failsafe while the panel writes its flash. */
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

/* The middle of SETUP's tile @p index, in panel coordinates. */
static void hub_point(int index, int *x, int *y)
{
    gfx_rect_t r = gfx_rect_make(0, 0, 0, 0);
    CHECK(settings_hub_slot(index, &r));
    *x = r.x + r.w / 2;
    *y = UI_BAND_H + r.y + r.h / 2;
}

static void tap_tile(int index)
{
    int x = 0, y = 0;
    hub_point(index, &x, &y);
    tap(x, y);
}

static void tap_home(void)
{
    const gfx_rect_t home =
        ui_home_tag_rect(ui_router_title(ui_router_current()));
    tap(home.x + home.w / 2, home.y + home.h / 2);
}

/*
 * SETUP's tiles.  They are the only way to either outputs view and to
 * UPDATES, so a tile that stopped navigating would leave a screen nobody
 * can reach.  The table's order: the three categories, then OUTPUTS, PICK A
 * PIN and UPDATES.
 */
TEST_CASE(the_setup_tiles_open_the_outputs_the_picker_and_updates)
{
    static const ui_screen_id_t k_tools[] = {
        SCREEN_OUTPUTS, SCREEN_PICKER, SCREEN_UPDATES,
    };
    fresh();
    for (int i = 0; i < 3; ++i) {
        ui_router_goto(SCREEN_SETUP);
        CHECK_EQ(settings_screen_category(), -1);
        tap_tile(3 + i);
        CHECK_EQ(ui_router_current(), k_tools[i]);
        CHECK(settings_screen_opens(k_tools[i]));
        /* Its home tag leads back to the tiles, not to the menu. */
        tap_home();
        CHECK_EQ(ui_router_current(), SCREEN_SETUP);
        CHECK_EQ(settings_screen_category(), -1);
    }

    /* A tile's edge rows are the tile's; the margin between two is not. */
    gfx_rect_t r = gfx_rect_make(0, 0, 0, 0);
    CHECK(settings_hub_slot(3, &r));
    ui_router_goto(SCREEN_SETUP);
    tap(r.x + 1, UI_BAND_H + r.y + r.h - 1);
    CHECK_EQ(ui_router_current(), SCREEN_OUTPUTS);
    ui_router_goto(SCREEN_SETUP);
    tap(r.x + r.w + 4, UI_BAND_H + r.y + r.h / 2);
    CHECK_EQ(ui_router_current(), SCREEN_SETUP);
    /* An empty slot opens nothing. */
    int x = 0, y = 0;
    hub_point(settings_hub_tiles(), &x, &y);
    tap(x, y);
    CHECK_EQ(ui_router_current(), SCREEN_SETUP);
    CHECK_EQ(settings_screen_category(), -1);

    /* And a press that slides off a tile is not a tap on it. */
    hub_point(4, &x, &y);
    touch(x, y, TOUCH_EVENT_DOWN, 1);
    touch(x + 300, y, TOUCH_EVENT_MOVE, 1);
    touch(x + 300, y, TOUCH_EVENT_UP, 1);
    CHECK_EQ(ui_router_current(), SCREEN_SETUP);

    /* The reminder's own screen is not a tile's: its tag leaves to the
     * menu, as every screen's that no tile opens. */
    CHECK(!settings_screen_opens(SCREEN_UPD_REMIND));
    CHECK(!settings_screen_opens(SCREEN_MOTOR));
    CHECK(!settings_screen_opens(SCREEN_SETUP));
    ui_router_goto(SCREEN_WIFI);
    tap_home();
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
}

/*
 * A category's tile opens its list on the same screen, and the home tag
 * goes back up: from the list to the tiles, from the tiles to the menu.
 */
TEST_CASE(the_home_tag_climbs_from_a_setup_list_to_the_tiles_to_the_menu)
{
    fresh();
    to_overview();
    ui_router_goto(SCREEN_SETUP);
    for (int cat = 0; cat < SET_CAT_SETUP_COUNT; ++cat) {
        const uint32_t n0 = ui_router_navigations();
        tap_tile(cat);
        CHECK_EQ(ui_router_current(), SCREEN_SETUP);
        CHECK_EQ(settings_screen_category(), cat);
        CHECK_EQ(ui_router_navigations(), n0);
        tap_home();
        CHECK_EQ(ui_router_current(), SCREEN_SETUP);
        CHECK_EQ(settings_screen_category(), -1);
        CHECK_EQ(ui_router_navigations(), n0);
    }
    tap_home();
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);

    /* SETUP opens on its tiles again, whichever list was open when it
     * was left. */
    ui_router_goto(SCREEN_SETUP);
    tap_tile(SET_CAT_IFACE);
    CHECK_EQ(settings_screen_category(), SET_CAT_IFACE);
    ui_router_goto(SCREEN_MOTOR);
    ui_router_goto(SCREEN_SETUP);
    CHECK_EQ(settings_screen_category(), -1);
}

/* Tiles navigate, and a press that slid off its tile is not a tap on it. */
/*
 * A tile navigates on its release, so a press left latched after a lost
 * event is a navigation waiting for any release over that tile.  The GT911
 * reuses track ids, so that release need not belong to the same contact.
 */
TEST_CASE(a_cancelled_tile_press_navigates_nowhere)
{
    to_overview();
    ui_router_render(&cv, 0);
    gfx_color_t *idle = malloc((size_t)W * H * sizeof(gfx_color_t));
    REQUIRE_ALLOC(idle);
    memcpy(idle, fb, (size_t)W * H * sizeof(gfx_color_t));

    touch(110, UI_BAND_H + 90, TOUCH_EVENT_DOWN, 3);
    ui_router_cancel_gestures();

    /* No contact is left to release the tile, so it is not drawn pressed. */
    ui_router_render(&cv, 0);
    CHECK_EQ(memcmp(idle, fb, (size_t)W * H * sizeof(gfx_color_t)), 0);
    free(idle);

    touch(110, UI_BAND_H + 90, TOUCH_EVENT_UP, 3);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);

    /* And nothing is stuck: a fresh tap still navigates. */
    tap(110, UI_BAND_H + 90);
    CHECK_EQ(ui_router_current(), SCREEN_MOTOR);
}

/*
 * The loss is observed after the frame's events were dispatched, and one of
 * them can have navigated.  The screen that took the earlier events is then
 * off the top with its press still latched, and a cancel that reached only
 * the screen on top would leave it there for the next visit, where a
 * recycled id lifting over the tab would switch the pane.
 */
TEST_CASE(a_cancel_reaches_a_screen_left_during_the_frame)
{
    fresh();
    to_overview();
    ui_router_goto(SCREEN_ANALYSER);
    ui_router_render(&cv, 0);
    gfx_color_t *before = malloc((size_t)W * H * sizeof(gfx_color_t));
    REQUIRE_ALLOC(before);
    memcpy(before, fb, (size_t)W * H * sizeof(gfx_color_t));

    touch(200, UI_BAND_H + 20, TOUCH_EVENT_DOWN, 2);   /* the second tab */
    ui_router_goto(SCREEN_OVERVIEW);                    /* a HOME that survived */
    ui_router_cancel_gestures();

    ui_router_goto(SCREEN_ANALYSER);
    touch(200, UI_BAND_H + 20, TOUCH_EVENT_UP, 2);      /* a recycled id */
    ui_router_render(&cv, 0);
    CHECK_EQ(memcmp(before, fb, (size_t)W * H * sizeof(gfx_color_t)), 0);

    /* And the row is not stuck. */
    tap(200, UI_BAND_H + 20);
    ui_router_render(&cv, 0);
    CHECK(memcmp(before, fb, (size_t)W * H * sizeof(gfx_color_t)) != 0);
    free(before);
}

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

/* A screen that renders nothing is a screen nobody notices is empty. */
TEST_CASE(every_screen_renders_something_and_has_a_title)
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

/* A point on the alert band that, with no alert there, opens a screen from
 * the overview: the tap the band must keep from the screen beneath.  -1
 * when the overview has nothing there. */
static int alert_band_x_over_a_tile(void)
{
    for (int x = 10; x < W; x += 20) {
        to_overview();
        tap(x, H - 10);
        if (ui_router_current() != SCREEN_OVERVIEW) {
            return x;
        }
    }
    return -1;
}

/* The frame the alert arrives in, then 30 s. */
static void expire_alert(void)
{
    ui_router_tick(0.0f);
    ui_router_tick(UI_ALERT_SHOW_S);
}

TEST_CASE(an_alert_clears_after_30_s_and_a_new_one_starts_again)
{
    fresh();
    to_overview();
    ui_router_set_alert("PD mini output would not switch");
    ui_router_tick(0.0f);
    ui_router_tick(UI_ALERT_SHOW_S - 0.5f);
    CHECK(ui_router_alert() != NULL);
    ui_router_set_alert("PD mini output would not switch");   /* again */
    ui_router_tick(0.0f);
    ui_router_tick(UI_ALERT_SHOW_S - 0.5f);
    CHECK(ui_router_alert() != NULL);
    ui_router_tick(1.0f);
    CHECK(ui_router_alert() == NULL);
}

/* The frame's interval was measured before the alert was set: a stall that
 * long must not clear the alert before it is drawn. */
TEST_CASE(the_frame_an_alert_arrives_in_does_not_count)
{
    fresh();
    to_overview();
    ui_router_set_alert("the card did not keep up -- run not recorded");
    ui_router_tick(2.0f * UI_ALERT_SHOW_S);
    CHECK(ui_router_alert() != NULL);
    ui_router_tick(UI_ALERT_SHOW_S);
    CHECK(ui_router_alert() == NULL);
}

/* A second finger on the band takes nothing from the first: the first one's
 * lift still clears, and the second's events reach no screen. */
TEST_CASE(a_second_contact_cannot_take_the_alert)
{
    fresh();
    const int x = alert_band_x_over_a_tile();
    CHECK(x >= 0);
    to_overview();
    ui_router_set_alert("coprocessor refused the pulse range");
    touch(400, H - 10, TOUCH_EVENT_DOWN, 1);
    touch(x, H - 10, TOUCH_EVENT_DOWN, 2);
    touch(x, 200, TOUCH_EVENT_MOVE, 2);
    touch(x, 200, TOUCH_EVENT_UP, 2);           /* slid off: clears nothing */
    CHECK(ui_router_alert() != NULL);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    touch(400, H - 10, TOUCH_EVENT_UP, 1);
    CHECK(ui_router_alert() == NULL);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
}

TEST_CASE(a_tap_on_the_alert_clears_it_and_reaches_no_screen)
{
    fresh();
    const int x = alert_band_x_over_a_tile();
    CHECK(x >= 0);
    to_overview();
    ui_router_set_alert("supply not answering -- output off");
    tap(x, H - 10);
    CHECK(ui_router_alert() == NULL);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);

    /* Pressed on the band and lifted off it: a slip, not a tap. */
    ui_router_set_alert("supply not answering -- output off");
    touch(x, H - 10, TOUCH_EVENT_DOWN, 1);
    touch(x, 200, TOUCH_EVENT_MOVE, 1);
    touch(x, 200, TOUCH_EVENT_UP, 1);
    CHECK(ui_router_alert() != NULL);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    ui_router_set_alert(NULL);
}

/* An alert that arrives under a finger has not been read; the lift that
 * was meant for the one before does not clear it. */
TEST_CASE(a_lift_clears_only_the_alert_it_was_pressed_on)
{
    fresh();
    to_overview();
    ui_router_set_alert("coprocessor refused to arm");
    touch(400, H - 10, TOUCH_EVENT_DOWN, 1);
    ui_router_set_alert("coprocessor disarmed -- arm again");
    touch(400, H - 10, TOUCH_EVENT_UP, 1);
    CHECK_STR_EQ(ui_router_alert(), "coprocessor disarmed -- arm again");
    ui_router_set_alert(NULL);
}

/* A press on the band that the alert's expiry overtakes is still the band's:
 * the screen never saw it begin, so it does not see it end. */
TEST_CASE(a_press_on_an_alert_that_expires_stays_off_the_screen)
{
    fresh();
    const int x = alert_band_x_over_a_tile();
    CHECK(x >= 0);
    to_overview();
    ui_router_set_alert("card full or unwritable -- run not recorded");
    touch(x, H - 10, TOUCH_EVENT_DOWN, 1);
    expire_alert();
    CHECK(ui_router_alert() == NULL);
    touch(x, H - 10, TOUCH_EVENT_UP, 1);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
}

/* Cancelled, the band lets go of its press; a release that arrives anyway
 * clears nothing. */
TEST_CASE(cancelling_gestures_lets_go_of_the_alert)
{
    fresh();
    to_overview();
    ui_router_set_alert("touch lost while arming -- arm again");
    touch(400, H - 10, TOUCH_EVENT_DOWN, 1);
    ui_router_cancel_gestures();
    touch(400, H - 10, TOUCH_EVENT_UP, 1);
    CHECK(ui_router_alert() != NULL);
    ui_router_set_alert(NULL);
}

/* The start-up fault that leaves no touch: neither time nor a tap clears
 * it, and a tap goes to the screen as if the band were not there. */
TEST_CASE(a_held_alert_stays_and_lets_taps_through)
{
    fresh();
    const int x = alert_band_x_over_a_tile();
    CHECK(x >= 0);
    to_overview();
    ui_router_hold_alert("touch did not answer -- the bench will not arm");
    ui_router_tick(10.0f * UI_ALERT_SHOW_S);
    CHECK(ui_router_alert() != NULL);
    tap(x, H - 10);
    CHECK(ui_router_alert() != NULL);
    CHECK(ui_router_current() != SCREEN_OVERVIEW);
    ui_router_set_alert(NULL);                  /* not the held one */
    CHECK(ui_router_alert() != NULL);
    ui_router_hold_alert(NULL);
    CHECK(ui_router_alert() == NULL);
}

/* A passing alert shows over the held one and, cleared by time or by a tap,
 * gives way to it again. */
TEST_CASE(a_held_alert_returns_after_a_passing_one)
{
    fresh();
    to_overview();
    ui_router_hold_alert("touch did not answer -- the bench will not arm");
    ui_router_set_alert("protocol mismatch -- will not arm");
    CHECK_STR_EQ(ui_router_alert(), "protocol mismatch -- will not arm");
    expire_alert();
    CHECK_STR_EQ(ui_router_alert(),
                 "touch did not answer -- the bench will not arm");
    ui_router_set_alert("protocol mismatch -- will not arm");
    tap(400, H - 10);
    CHECK_STR_EQ(ui_router_alert(),
                 "touch did not answer -- the bench will not arm");
    ui_router_hold_alert(NULL);
}

/* The screens cache what they drew per framebuffer, and the band was drawn
 * over that; cleared, it has to be painted over, not left in red. */
TEST_CASE(a_cleared_alert_leaves_no_red_behind)
{
    fresh();
    to_overview();
    ui_router_set_alert("PD mini set points would not take");
    ui_router_render(&cv, 0);
    expire_alert();
    CHECK(ui_router_alert() == NULL);
    ui_router_render(&cv, 0);
    int danger_bottom = 0;
    for (int y = H - 34; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            if (fb[(size_t)y * W + x] == ui_theme_color(UI_C_DANGER)) {
                ++danger_bottom;
            }
        }
    }
    CHECK_EQ(danger_bottom, 0);
}

/* The band's other states: armed, and a fault badge.  Rendered here rather
 * than only on a faulted bench. */
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

/* The mode chip says what drives the outputs, and the link chip beside it
 * says whether the link is up: the mode never repeats the link's word.
 * Mode names, so the same in German. */
TEST_CASE(the_band_names_the_bench_or_the_model_not_the_link)
{
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        CHECK_STR_EQ(ui_band_mode(true), "BENCH");
        CHECK_STR_EQ(ui_band_mode(false), "SIM");
        CHECK(strstr(ui_band_mode(true), "LINK") == NULL);
        CHECK(strstr(ui_band_mode(false), "LINK") == NULL);
    }
    ui_text_set_language(UI_LANG_EN);
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

/* ------------------------------------------------------- the simulation mark */

/*
 * The bench runs without hardware, and the risk is a modelled number being
 * photographed and quoted as a measured one.  The mark is drawn over
 * everything, on every screen, and no screen can opt out.
 */
TEST_CASE(the_simulation_mark_covers_the_whole_screen)
{
    fresh();
    to_overview();

    ui_bench_status_t st = *ui_router_status();
    st.simulated = false;
    ui_router_set_status(&st);
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    ui_router_render(&cv, 0);
    gfx_color_t *plain = malloc((size_t)W * H * sizeof(gfx_color_t));
    REQUIRE_ALLOC(plain);
    memcpy(plain, fb, (size_t)W * H * sizeof(gfx_color_t));

    st.simulated = true;
    ui_router_set_status(&st);
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    ui_router_render(&cv, 0);

    /*
     * The property is anti-crop: no horizontal strip and no vertical strip
     * of the screen is free of the mark, so no crop of a photograph loses
     * it.
     *
     * Quadrant counts are not asserted: a single diagonal word leaves a
     * corner quadrant with about 51 marked pixels.
     *
     * Ink inside the status band is not asserted either.  The router draws
     * the mark last, over the whole canvas, clipped to nothing; but the top
     * 48 rows are a corner of the diagonal, and their count depends on the
     * shape of the letters rather than on the policy.
     */
    const int strips = 5;
    for (int i = 0; i < strips; ++i) {
        int rows = 0, cols = 0;
        for (int y = i * H / strips; y < (i + 1) * H / strips; ++y) {
            for (int x = 0; x < W; ++x) {
                if (fb[(size_t)y * W + x] != plain[(size_t)y * W + x]) {
                    ++rows;
                }
            }
        }
        for (int x = i * W / strips; x < (i + 1) * W / strips; ++x) {
            for (int y = 0; y < H; ++y) {
                if (fb[(size_t)y * W + x] != plain[(size_t)y * W + x]) {
                    ++cols;
                }
            }
        }
        if (rows < 100) {
            T_FAIL("horizontal strip %d has only %d marked pixels", i, rows);
        }
        if (cols < 100) {
            T_FAIL("vertical strip %d has only %d marked pixels", i, cols);
        }
    }

    free(plain);
}

/*
 * The numbers underneath stay readable.
 *
 * The mark is a stencil, not a blend.  It is laid over screens that repaint
 * only what changed, and a blend applied to pixels already carrying the mark
 * compounds: 15% over 15% is 28%, then 39%, until solid.  Readability comes
 * from sparseness, which is what this measures: a marked pixel's neighbours
 * are mostly unmarked, so the content underneath reads through the gaps.  A
 * solid fill scores close to eight.
 */
TEST_CASE(the_simulation_mark_lets_the_screen_through)
{
    fresh();
    to_overview();
    ui_bench_status_t st = *ui_router_status();

    st.simulated = false;
    ui_router_set_status(&st);
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    ui_router_render(&cv, 0);
    gfx_color_t *plain = malloc((size_t)W * H * sizeof(gfx_color_t));
    REQUIRE_ALLOC(plain);
    memcpy(plain, fb, (size_t)W * H * sizeof(gfx_color_t));

    st.simulated = true;
    ui_router_set_status(&st);
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    ui_router_render(&cv, 0);

    const gfx_color_t ink = ui_theme_color(UI_C_TEXT);
    int changed = 0, not_ink = 0, marked_neighbours = 0;
    for (int y = 1; y < H - 1; ++y) {
        for (int x = 1; x < W - 1; ++x) {
            const size_t at = (size_t)y * W + (size_t)x;
            if (fb[at] == plain[at]) {
                continue;
            }
            ++changed;
            if (fb[at] != ink) {
                ++not_ink;
            }
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) {
                        continue;
                    }
                    const size_t n = (size_t)(y + dy) * W + (size_t)(x + dx);
                    if (fb[n] != plain[n]) {
                        ++marked_neighbours;
                    }
                }
            }
        }
    }
    CHECK(changed > 2000);
    /* A stencil, so a marked pixel is the ink colour and nothing between. */
    CHECK_EQ(not_ink, 0);
    if (marked_neighbours > changed * 4) {
        T_FAIL("marked pixels average %.1f marked neighbours of 8, which is "
               "closer to a fill than to a watermark",
               (double)marked_neighbours / (double)changed);
    }
    free(plain);
}

/*
 * Every screen caches its chrome per framebuffer, so ui_router_invalidate()
 * has to reach every screen.  Five of them had no hook at all, which left a
 * theme change or a change of the SIMULATED flag showing stale chrome, and
 * cost nothing measurable, so nothing caught it.  Scribbling over a settled
 * canvas and invalidating has to restore exactly what a fresh render draws.
 */
TEST_CASE(invalidating_the_router_repaints_every_screen)
{
    const size_t bytes = (size_t)W * H * sizeof(gfx_color_t);
    gfx_color_t *settled = malloc(bytes);
    CHECK(settled != NULL);
    if (settled == NULL) {
        return;
    }

    for (int id = 0; id < SCREEN_COUNT; ++id) {
        /*
         * Both framebuffers, because the caches are per buffer: a hook that
         * cleared one bit and not the other would pass a check of buffer 0
         * and leave the panel alternating between a repainted frame and a
         * stale one, which reads as flicker.
         */
        for (int buf = 0; buf < 2; ++buf) {
            fresh();
            ui_router_goto((ui_screen_id_t)id);
            ui_router_render(&cv, 0);
            ui_router_render(&cv, 1);
            ui_router_render(&cv, buf);
            memcpy(settled, fb, bytes);

            /* Something else owned the buffer in between. */
            memset(fb, 0x5a, bytes);
            ui_router_invalidate();
            ui_router_render(&cv, buf);

            if (memcmp(settled, fb, bytes) != 0) {
                long differ = 0;
                for (long i = 0; i < (long)W * H; ++i) {
                    if (settled[i] != fb[i]) {
                        ++differ;
                    }
                }
                T_FAIL("screen %d buffer %d keeps %ld px of stale chrome "
                       "after ui_router_invalidate()", id, buf, differ);
            }
        }
    }
    free(settled);
}

/*
 * The mark's stencil is cached, so a canvas whose geometry or ink differs
 * from the cached one has to rebuild it rather than replay the wrong points,
 * and a canvas covering more points than the table holds falls back to
 * scanning.  A 1600x960 canvas covers about four times the panel's 3,439.
 */
TEST_CASE(the_simulation_mark_follows_the_canvas_it_is_given)
{
    enum { BW = 1600, BH = 960 };
    gfx_color_t *big = calloc((size_t)BW * BH, sizeof(gfx_color_t));
    CHECK(big != NULL);
    if (big == NULL) {
        return;
    }
    gfx_canvas_t bc;
    gfx_canvas_init(&bc, big, BW, BH, 0);

    ui_watermark_invalidate();
    ui_watermark(&bc);
    int wide = 0;
    for (long i = 0; i < (long)BW * BH; ++i) {
        if (big[i] != 0) {
            ++wide;
        }
    }
    CHECK(wide > 3439);

    /* Back to a panel-sized canvas: the cache rebuilds rather than replaying
     * points from the larger one, which would land outside this canvas. */
    gfx_color_t *small = calloc((size_t)W * H, sizeof(gfx_color_t));
    CHECK(small != NULL);
    if (small == NULL) {
        free(big);
        return;
    }
    gfx_canvas_t sc;
    gfx_canvas_init(&sc, small, W, H, 0);
    ui_watermark(&sc);
    int narrow = 0;
    for (long i = 0; i < (long)W * H; ++i) {
        if (small[i] != 0) {
            ++narrow;
        }
    }
    CHECK(narrow > 0);
    CHECK(narrow < wide);

    /* And drawing it again is still idempotent through the cache. */
    gfx_color_t *once = malloc((size_t)W * H * sizeof(gfx_color_t));
    if (once != NULL) {
        memcpy(once, small, (size_t)W * H * sizeof(gfx_color_t));
        ui_watermark(&sc);
        CHECK_EQ(memcmp(once, small,
                        (size_t)W * H * sizeof(gfx_color_t)), 0);
        free(once);
    }
    free(small);
    free(big);
}

/*
 * Screens cache their chrome per framebuffer.  Switching the mark changes
 * pixels they believe they have already drawn correctly, so the router has to
 * invalidate them; otherwise the mark appears on one buffer and not the
 * other, which on a panel that alternates between two is flicker.
 *
 * Both buffers are painted before the flag flips.  Flipping first draws each
 * buffer for the first time anyway, and the case then passes without the
 * invalidation.
 */
TEST_CASE(switching_the_mark_invalidates_the_cached_chrome)
{
    fresh();
    to_overview();
    ui_bench_status_t st = *ui_router_status();

    st.simulated = false;
    ui_router_set_status(&st);
    ui_router_render(&cv, 0);
    ui_router_render(&cv, 1);   /* both caches warm and unmarked */

    st.simulated = true;
    ui_router_set_status(&st);

    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    ui_router_render(&cv, 0);
    gfx_color_t *first = malloc((size_t)W * H * sizeof(gfx_color_t));
    REQUIRE_ALLOC(first);
    memcpy(first, fb, (size_t)W * H * sizeof(gfx_color_t));

    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    ui_router_render(&cv, 1);

    /* Both buffers must be the marked screen, not one of each. */
    CHECK_EQ(memcmp(first, fb, (size_t)W * H * sizeof(gfx_color_t)), 0);

    /* And they must actually be drawn, not left as the memset. */
    int lit = 0;
    for (int i = 0; i < W * H; ++i) {
        if (fb[i] != 0) { ++lit; }
    }
    CHECK(lit > 100000);
    free(first);
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


/*
 * The menu says what the bench can do, not what it has screens for.  The
 * marks derive from the capability bits the coprocessor reports, not from a
 * preference: a capability corrects itself when the part is fitted, and until
 * then the screen says its numbers are modelled.
 */
TEST_CASE(the_menu_marks_what_is_not_fitted)
{
    fresh();
    to_overview();

    ui_bench_status_t st = *ui_router_status();
    st.capabilities = 0;                 /* nothing soldered on */
    ui_router_set_status(&st);
    ui_router_render(&cv, 0);
    int bare = 0;
    for (int i = 0; i < W * H; ++i) {
        if (fb[i] == ui_theme_color(UI_C_PANEL_SUNK)) { ++bare; }
    }

    /* Everything fitted: the badges that said MODELLED go away, so there is
     * strictly less of the badge colour on the screen. */
    st.capabilities = 0xFFFFu;
    ui_router_set_status(&st);
    ui_router_render(&cv, 0);
    int full = 0;
    for (int i = 0; i < W * H; ++i) {
        if (fb[i] == ui_theme_color(UI_C_PANEL_SUNK)) { ++full; }
    }

    if (full >= bare) {
        T_FAIL("fitting the hardware did not remove any badges: %d then %d",
               bare, full);
    }
    /* But the two that have no screen still say so. */
    CHECK(full > 0);

    /* SUPPLY is marked whatever the coprocessor reports -- until SETUP
     * enables the PD mini, and again once it is disabled. */
    overview_screen_set_supply_real(true);
    ui_router_render(&cv, 0);
    int real = 0;
    for (int i = 0; i < W * H; ++i) {
        if (fb[i] == ui_theme_color(UI_C_PANEL_SUNK)) { ++real; }
    }
    if (real >= full) {
        T_FAIL("the PD mini enabled left SUPPLY marked: %d then %d",
               full, real);
    }
    overview_screen_set_supply_real(false);
    ui_router_render(&cv, 0);
    int again = 0;
    for (int i = 0; i < W * H; ++i) {
        if (fb[i] == ui_theme_color(UI_C_PANEL_SUNK)) { ++again; }
    }
    CHECK_EQ(again, full);
}

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

    /* A press on STOP, and then the events stop arriving. */
    touch(sx, sy, TOUCH_EVENT_DOWN, 7);
    ui_router_cancel_gestures();

    /* The release the router never saw arrives on a reused id, over a tile.
     * With the band press cleared it is not the band's, so it neither stops
     * the bench nor navigates from a press nobody made. */
    const ui_screen_id_t before = ui_router_current();
    touch(sx, sy, TOUCH_EVENT_UP, 7);
    CHECK_EQ(ui_router_current(), before);
    CHECK(!ui_router_take_stop());
}

/* ------------------------------------------------ one owner per contact */

/*
 * Geometry in panel coordinates, mirrored from the screens (their own
 * suites carry the screen-local values).
 * MOTOR & ESC: the throttle track x 72..485, y 384..423; + at x 492..547.
 * SERVO: the shaft; SPEED's track x 514..781, y 344..365.
 * SUPPLY: both tracks x 72..485; OUTPUT ON / OFF on the right rail.
 */
#define M_TRACK_Y  (336 + UI_BAND_H + 20)
#define M_PX_PCT   (100.0f / 413.0f)     /* throttle per px of travel */
#define SV_SHAFT_X 300
#define SV_SHAFT_Y (UI_BAND_H + (H - UI_BAND_H) / 2)
#define SV_SPEED_Y (UI_BAND_H + 306)
#define SU_TRACK_X 72
#define SU_V_Y     (335 + UI_BAND_H)
#define SU_I_Y     (397 + UI_BAND_H)
#define SU_OUT_X   676
#define SU_OUT_Y   (318 + UI_BAND_H)
/* On the band, 3 px under STOP (y 6..41) and 4 px above the body. */
#define UNDER_STOP_X 720
#define UNDER_STOP_Y 44

static bool motor_posted(motor_cmd_t *c)
{
    motor_cmd_t got = { .kind = MOTOR_CMD_NONE };
    const bool any = motor_screen_poll_cmd(&got);
    if (c != NULL) {
        *c = got;
    }
    return any;
}

static void drain_motor(void)
{
    while (motor_posted(NULL)) { }
}

static servo_cmd_t servo_took(void)
{
    servo_cmd_t c = { .kind = SERVO_CMD_NONE };
    servo_screen_take(&c);
    return c;
}

static void drain_servo(void)
{
    while (servo_took().kind != SERVO_CMD_NONE) { }
}

static void drain_supply(void)
{
    supply_cmd_t c;
    while (supply_screen_poll_cmd(&c)) { }
}

/* A bench screen on top, nothing on the glass, nothing waiting. */
static void on_screen(ui_screen_id_t id)
{
    settings_set_store(NULL);
    settings_init();
    fresh();
    to_overview();
    ui_router_goto(id);
    if (id == SCREEN_SUPPLY) {
        supply_screen_settings_loaded();
    }
    drain_motor();
    drain_servo();
    drain_supply();
    feed_reset();
}

/* Whether the knob moves the throttle: it does not while a finger owns the
 * slider.  The turn is taken back. */
static bool knob_moves_the_throttle(void)
{
    motor_screen_knob_frame();
    motor_screen_knob(0.05f);
    const bool posted = motor_posted(NULL);
    if (posted) {
        motor_screen_knob_frame();
        motor_screen_knob(-0.05f);
        (void)motor_posted(NULL);
    }
    return posted;
}

static void dial_at(float deg, int r, int *x, int *y)
{
    const float k = 3.14159265358979f / 180.0f;
    *x = SV_SHAFT_X + (int)((float)r * cosf(deg * k) + 0.5f);
    *y = SV_SHAFT_Y - (int)((float)r * sinf(deg * k) + 0.5f);
}

/* A point on the band, clear of STOP and of the home tag. */
static void check_plain_band(int x, int y)
{
    CHECK(y < UI_BAND_H);
    CHECK(!gfx_rect_contains(ui_band_stop_rect(), x, y));
    CHECK(!gfx_rect_contains(
              ui_home_tag_rect(ui_router_title(ui_router_current())), x, y));
}

/*
 * Armed.  A finger drags the throttle track 60 px, slides up the glass at
 * 8 px per report and leaves it on the band.  The next lone finger aims at
 * STOP, lands in the 6 px of band under the button and rolls 5 px down into
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

    /* One step short of the band: still the screen's, and the 4 px to the
     * right count. */
    glide(FEED_LONE, 364, UI_BAND_H, 4);
    CHECK_EQ(ui_router_dispatched(), d0 + 1u);
    const float at_edge = motor_screen_throttle();
    CHECK_NEAR(at_edge, 64.0f * M_PX_PCT, 0.01f);
    drain_motor();

    /* The first row of the band, 2 px further right: one event to the
     * screen, the release, and the 2 px do not count. */
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
        SCREEN_SETUP, SCREEN_LOGS, SCREEN_PROGRAMMER,
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

/* The splash carries no band, so its top 48 px are the screen's. */
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
 * speeds.  At 130 px per report the tracker splits each step into an UP and
 * a DOWN (TOUCH_JUMP_PX is 120), so the screen has its release from the
 * tracker; at 100 px per report it has it from the router.
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
 * STOP answers a second finger while the first drags, and the drag goes on:
 * the stop is the application's to act on, and the bench's disarm is what
 * ends the run.
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
    CHECK(!ui_router_take_stop());      /* on the release, not the press */
    lift(1);
    CHECK(ui_router_take_stop());
    CHECK(!ui_router_take_stop());

    glide(0, 380, M_TRACK_Y, 8);
    CHECK_NEAR(motor_screen_throttle(), 80.0f * M_PX_PCT, 0.01f);
    lift(0);
    CHECK_NEAR(motor_screen_throttle(), 80.0f * M_PX_PCT, 0.01f);
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
    CHECK(!ui_router_take_stop());      /* it lifted off the button */
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
    hub_point(SET_CAT_IFACE, &x, &y);
    feed_tap(FEED_LONE, x, y);
    CHECK_EQ(settings_screen_category(), SET_CAT_IFACE);

    finger(0, 300, UI_BAND_H + 300);
    glide(0, 300, UI_BAND_H + 200, 8);
    CHECK(settings_screen_scroll(NULL) > 0);
    const gfx_rect_t home = ui_home_tag_rect(ui_router_title(SCREEN_SETUP));
    feed_tap(1, home.x + home.w / 2, home.y + home.h / 2);
    CHECK_EQ(settings_screen_category(), -1);

    const uint32_t d0 = ui_router_dispatched();
    hub_point(3, &x, &y);
    glide(0, x, y, 8);                  /* onto OUTPUTS */
    lift(0);
    CHECK_EQ(ui_router_dispatched(), d0);
    CHECK_EQ(ui_router_current(), SCREEN_SETUP);

    /* The next press is an ordinary one. */
    feed_tap(0, x, y);
    CHECK_EQ(ui_router_current(), SCREEN_OUTPUTS);
}

/* The alert strip answers a second finger during a drag, and the drag goes
 * on.  A press on the strip that wanders to the band and lifts there clears
 * nothing and leaves the strip answering the next tap. */
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
    CHECK_EQ(ui_router_dispatched(), d0);
    glide(0, 380, M_TRACK_Y, 8);
    lift(0);
    CHECK_NEAR(motor_screen_throttle(), 80.0f * M_PX_PCT, 0.01f);

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

/* What ends a throttle drag while the finger stays on the track. */
typedef enum { END_STOP, END_DISARM, END_ARM, END_HOME, END_LOSS } ender_t;

/*
 * A drag on the throttle track, then one of the things that end a run or a
 * gesture, in the order the panel's frame does them.  From there the finger
 * still on the track moves nothing, and a later contact with its id moves
 * nothing on its way from the band into the body.  On a bench the ender
 * left disarmed the knob and a fresh drag move nothing either; armed, the
 * knob has the throttle again, and a fresh drag moves it by its own travel.
 * The press before an arm is on a disarmed bench, and takes no drag.
 */
static void a_throttle_drag_then(ender_t e)
{
    on_screen(SCREEN_MOTOR);
    if (e != END_ARM) {
        motor_screen_set_armed(true);
    }
    drain_motor();
    const gfx_rect_t stop = ui_band_stop_rect();
    const gfx_rect_t home = ui_home_tag_rect(ui_router_title(SCREEN_MOTOR));

    finger(0, 300, M_TRACK_Y);
    glide(0, 340, M_TRACK_Y, 8);
    const float dragged = motor_screen_throttle();
    CHECK_NEAR(dragged, (e == END_ARM) ? 0.0f : 40.0f * M_PX_PCT, 0.01f);
    float want = dragged;
    bool disarmed = false;

    switch (e) {
    case END_STOP:
        feed_tap(1, stop.x + stop.w / 2, stop.y + stop.h / 2);
        CHECK(ui_router_take_stop());
        motor_screen_cancel_arm();
        /* the bench disarms on a stop: the next case's checks apply */
        /* fall through */
    case END_DISARM:
        motor_screen_set_throttle(0.0f);
        drain_motor();
        motor_screen_set_armed(false);
        want = 0.0f;
        disarmed = true;
        break;
    case END_ARM:
        motor_screen_set_armed(true);
        want = 0.0f;
        break;
    case END_HOME:
        feed_tap(1, home.x + home.w / 2, home.y + home.h / 2);
        CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
        ui_router_goto(SCREEN_MOTOR);
        disarmed = true;        /* asked for; the bench has not answered */
        break;
    case END_LOSS:
        motor_screen_knob_cancel();
        ui_router_cancel_gestures();
        break;
    }
    drain_motor();
    CHECK_NEAR(motor_screen_throttle(), want, 0.001f);

    glide(0, 420, M_TRACK_Y, 8);
    CHECK_NEAR(motor_screen_throttle(), want, 0.001f);
    lift(0);
    CHECK_NEAR(motor_screen_throttle(), want, 0.001f);

    finger(0, 560, UNDER_STOP_Y);
    glide(0, 560, UI_BAND_H + 1, 3);
    glide(0, 640, UI_BAND_H + 1, 8);
    lift(0);
    CHECK_NEAR(motor_screen_throttle(), want, 0.001f);
    CHECK(!motor_posted(NULL));

    if (disarmed) {
        /* Not armed: no control moves the throttle.  The bench's answer to
         * a disarm asked for by leaving returns it to zero. */
        CHECK(!knob_moves_the_throttle());
        finger(0, 200, M_TRACK_Y);
        glide(0, 240, M_TRACK_Y, 8);
        lift(0);
        CHECK_NEAR(motor_screen_throttle(), want, 0.001f);
        CHECK(!motor_posted(NULL));
        motor_screen_set_armed(false);
        want = 0.0f;
        CHECK_NEAR(motor_screen_throttle(), want, 0.001f);
        motor_screen_set_armed(true);
        drain_motor();
    }
    CHECK(knob_moves_the_throttle());

    finger(0, 200, M_TRACK_Y);
    glide(0, 240, M_TRACK_Y, 8);
    lift(0);
    CHECK_NEAR(motor_screen_throttle(), want + 40.0f * M_PX_PCT, 0.01f);
}

TEST_CASE(a_stop_ends_a_throttle_drag)      { a_throttle_drag_then(END_STOP); }
TEST_CASE(a_disarm_ends_a_throttle_drag)    { a_throttle_drag_then(END_DISARM); }
TEST_CASE(an_arm_ends_a_throttle_drag)      { a_throttle_drag_then(END_ARM); }
TEST_CASE(leaving_ends_a_throttle_drag)     { a_throttle_drag_then(END_HOME); }
TEST_CASE(a_touch_loss_ends_a_throttle_drag) { a_throttle_drag_then(END_LOSS); }

/*
 * SERVO.  A dial drag slides up the glass and leaves it on the band.  The
 * knob has the horn again, and the next lone finger, which presses the body
 * beside the dial and slides onto it, commands nothing: it did not press
 * the dial.
 */
TEST_CASE(a_dial_drag_that_leaves_by_the_band_ends_at_the_edge)
{
    on_screen(SCREEN_SERVO);
    servo_screen_set_armed(true);
    drain_servo();

    int x, y;
    dial_at(80.0f, 110, &x, &y);
    finger(FEED_LONE, x, y);
    CHECK_EQ(servo_took().kind, SERVO_CMD_POSITION);
    glide(FEED_LONE, x, 30, 8);
    lift(FEED_LONE);
    const uint16_t held = servo_screen_commanded();
    drain_servo();

    servo_screen_knob_frame();
    servo_screen_knob(0.1f);
    const servo_cmd_t k = servo_took();
    CHECK_EQ(k.kind, SERVO_CMD_POSITION);
    CHECK(k.value_us != held);
    const uint16_t turned = servo_screen_commanded();

    /* 190 px right of the shaft: outside the arc, which ends at 174 px. */
    finger(FEED_LONE, SV_SHAFT_X + 190, SV_SHAFT_Y);
    const servo_cmd_t d = servo_took();
    glide(FEED_LONE, SV_SHAFT_X + 150, SV_SHAFT_Y, 8);
    const servo_cmd_t c = servo_took();
    lift(FEED_LONE);
    CHECK_EQ(d.kind, SERVO_CMD_NONE);
    CHECK_EQ(c.kind, SERVO_CMD_NONE);
    CHECK_EQ(servo_screen_commanded(), turned);
}

/* The same press and slide with no drag before it, for comparison. */
TEST_CASE(a_slide_onto_the_dial_commands_nothing)
{
    on_screen(SCREEN_SERVO);
    servo_screen_set_armed(true);
    drain_servo();
    finger(FEED_LONE, SV_SHAFT_X + 190, SV_SHAFT_Y);
    glide(FEED_LONE, SV_SHAFT_X + 150, SV_SHAFT_Y, 8);
    const servo_cmd_t c = servo_took();
    lift(FEED_LONE);
    CHECK_EQ(c.kind, SERVO_CMD_NONE);
}

/*
 * SPEED is part of the command, so on a held output a change of it says the
 * position again at the new rate.  A drag on it that leaves by the band
 * ends at the edge: a contact from the band into the body changes no rate.
 */
TEST_CASE(a_speed_drag_that_leaves_by_the_band_ends_at_the_edge)
{
    on_screen(SCREEN_SERVO);
    servo_screen_set_armed(true);
    drain_servo();
    int x, y;
    dial_at(40.0f, 110, &x, &y);
    feed_tap(FEED_LONE, x, y);
    CHECK_EQ(servo_took().kind, SERVO_CMD_POSITION);

    finger(FEED_LONE, 560, SV_SPEED_Y);
    glide(FEED_LONE, 620, SV_SPEED_Y, 8);
    const servo_cmd_t set = servo_took();
    CHECK_EQ(set.kind, SERVO_CMD_POSITION);
    glide(FEED_LONE, 620, 30, 8);
    lift(FEED_LONE);
    CHECK_EQ(servo_took().kind, SERVO_CMD_NONE);

    check_plain_band(700, UNDER_STOP_Y);
    finger(FEED_LONE, 700, UNDER_STOP_Y);
    glide(FEED_LONE, 700, UI_BAND_H + 1, 3);
    glide(FEED_LONE, 760, UI_BAND_H + 1, 8);
    lift(FEED_LONE);
    CHECK_EQ(servo_took().kind, SERVO_CMD_NONE);

    /* The rate is the one the drag set: a tap on the dial carries it. */
    feed_tap(FEED_LONE, x, y);
    const servo_cmd_t after = servo_took();
    CHECK_EQ(after.kind, SERVO_CMD_POSITION);
    CHECK_EQ(after.slew_per_s, set.slew_per_s);
}

/* ARM by touch: the button's place is found by holding each candidate. */
static bool servo_arm_by_touch(uint8_t id)
{
    for (int yy = UI_BAND_H + 392; yy < UI_BAND_H + 418; yy += 8) {
        finger(id, 650, yy);
        for (int i = 0; i < 50; ++i) {
            ui_router_tick(0.05f);
        }
        const servo_cmd_t c = servo_took();
        lift(id);
        if (c.kind == SERVO_CMD_ARM) {
            return true;
        }
    }
    return false;
}

/*
 * Finger 0 on the dial, finger 1 taps the home tag, finger 0 lifts on the
 * overview.  SERVO again, armed by a hold on ARM with a lone finger, every
 * event of which reaches the screen; then a press beside the dial slides
 * onto it.  The drag ended when the screen was left.
 */
TEST_CASE(home_under_a_dial_drag_ends_the_drag)
{
    on_screen(SCREEN_SERVO);
    servo_screen_set_armed(true);
    drain_servo();

    int x, y;
    dial_at(40.0f, 110, &x, &y);
    finger(0, x, y);
    const gfx_rect_t home = ui_home_tag_rect(ui_router_title(SCREEN_SERVO));
    feed_tap(1, home.x + 10, home.y + 10);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    lift(0);
    drain_servo();
    servo_screen_set_armed(false);

    ui_router_goto(SCREEN_SERVO);
    drain_servo();
    CHECK(servo_arm_by_touch(FEED_LONE));
    servo_screen_set_armed(true);
    drain_servo();

    /* The knob has the horn. */
    servo_screen_knob_frame();
    servo_screen_knob(0.1f);
    CHECK_EQ(servo_took().kind, SERVO_CMD_POSITION);

    finger(FEED_LONE, SV_SHAFT_X + 190, SV_SHAFT_Y);
    glide(FEED_LONE, SV_SHAFT_X + 150, SV_SHAFT_Y, 8);
    const servo_cmd_t c = servo_took();
    lift(FEED_LONE);
    CHECK_EQ(c.kind, SERVO_CMD_NONE);
}

/* A touch loss under a dial drag: the finger still on the dial commands
 * nothing more, and the knob has the horn. */
TEST_CASE(a_touch_loss_ends_a_dial_drag)
{
    on_screen(SCREEN_SERVO);
    servo_screen_set_armed(true);
    drain_servo();
    int x, y, x2, y2;
    dial_at(40.0f, 110, &x, &y);
    dial_at(70.0f, 110, &x2, &y2);
    finger(FEED_LONE, x, y);
    CHECK_EQ(servo_took().kind, SERVO_CMD_POSITION);

    servo_screen_knob_cancel();
    ui_router_cancel_gestures();
    drain_servo();
    const uint16_t held = servo_screen_commanded();
    glide(FEED_LONE, x2, y2, 8);
    lift(FEED_LONE);
    CHECK_EQ(servo_took().kind, SERVO_CMD_NONE);
    CHECK_EQ(servo_screen_commanded(), held);

    servo_screen_knob_frame();
    servo_screen_knob(0.1f);
    CHECK_EQ(servo_took().kind, SERVO_CMD_POSITION);
}

/*
 * SUPPLY, output off.  The voltage track is dragged, the finger slides up
 * and leaves on the band.  The next lone finger holds OUTPUT ON without
 * moving: the hold switches on, at the set point the drag left.
 */
TEST_CASE(a_set_point_drag_that_leaves_by_the_band_ends_at_the_edge)
{
    static const int rows[] = { SU_V_Y, SU_I_Y };
    for (int r = 0; r < 2; ++r) {
        on_screen(SCREEN_SUPPLY);
        finger(FEED_LONE, SU_TRACK_X + 60, rows[r]);
        glide(FEED_LONE, SU_TRACK_X + 80, rows[r], 8);
        const float set_v = supply_screen_set_v();
        const float set_i = supply_screen_set_i();
        glide(FEED_LONE, SU_TRACK_X + 80, 30, 8);
        lift(FEED_LONE);
        CHECK_NEAR(supply_screen_set_v(), set_v, 0.001f);
        CHECK_NEAR(supply_screen_set_i(), set_i, 0.001f);

        finger(FEED_LONE, SU_OUT_X, SU_OUT_Y);
        for (int i = 0; i < 50; ++i) {
            ui_router_tick(0.05f);
        }
        supply_cmd_t c1 = { 0 };
        const bool p1 = supply_screen_poll_cmd(&c1);
        lift(FEED_LONE);
        CHECK(p1 && c1.on);
        CHECK_NEAR(supply_screen_set_v(), set_v, 0.001f);
        CHECK_NEAR(supply_screen_set_i(), set_i, 0.001f);
    }
}

/* Output on: the same drag, and the next tap is OUTPUT OFF.  The first tap
 * switches off. */
TEST_CASE(output_off_answers_the_first_tap_after_a_drag_left_by_the_band)
{
    on_screen(SCREEN_SUPPLY);
    supply_screen_set_output(true);
    const float set = supply_screen_set_v();

    finger(FEED_LONE, SU_TRACK_X + 60, SU_V_Y);
    glide(FEED_LONE, SU_TRACK_X + 80, SU_V_Y, 8);
    glide(FEED_LONE, SU_TRACK_X + 80, 30, 8);
    lift(FEED_LONE);

    feed_tap(FEED_LONE, SU_OUT_X, SU_OUT_Y);
    supply_cmd_t c1 = { 0 };
    const bool p1 = supply_screen_poll_cmd(&c1);
    CHECK(p1 && c1.off);
    CHECK_NEAR(supply_screen_set_v(), set, 0.001f);
}

/* A press on OUTPUT OFF that slides up and leaves by the band is a press
 * that left its button: no OFF, and the next tap switches off. */
TEST_CASE(an_output_off_press_that_leaves_by_the_band_asks_for_nothing)
{
    on_screen(SCREEN_SUPPLY);
    supply_screen_set_output(true);

    finger(FEED_LONE, SU_OUT_X, SU_OUT_Y);
    glide(FEED_LONE, SU_OUT_X, 30, 8);
    lift(FEED_LONE);
    CHECK(!supply_screen_poll_cmd(NULL));

    feed_tap(FEED_LONE, SU_OUT_X, SU_OUT_Y);
    supply_cmd_t c1 = { 0 };
    const bool p1 = supply_screen_poll_cmd(&c1);
    CHECK(p1 && c1.off);
}

/* An OUTPUT ON hold that slides up to the band is abandoned where it left
 * the button, and switches nothing on, however long the finger stays. */
TEST_CASE(an_output_on_hold_that_leaves_by_the_band_switches_nothing_on)
{
    on_screen(SCREEN_SUPPLY);
    finger(FEED_LONE, SU_OUT_X, SU_OUT_Y);
    glide(FEED_LONE, SU_OUT_X, 30, 8);
    for (int i = 0; i < 80; ++i) {
        ui_router_tick(0.05f);
    }
    CHECK(!supply_screen_poll_cmd(NULL));
    lift(FEED_LONE);
    CHECK(!supply_screen_poll_cmd(NULL));
}

/* A second finger on the home tag or a touch loss under a set-point drag:
 * the finger still on the track changes nothing more. */
TEST_CASE(leaving_or_a_touch_loss_ends_a_set_point_drag)
{
    for (int how = 0; how < 2; ++how) {
        on_screen(SCREEN_SUPPLY);
        finger(0, SU_TRACK_X + 60, SU_V_Y);
        glide(0, SU_TRACK_X + 80, SU_V_Y, 8);
        if (how == 0) {
            const gfx_rect_t home =
                ui_home_tag_rect(ui_router_title(SCREEN_SUPPLY));
            feed_tap(1, home.x + 10, home.y + 10);
            CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
            ui_router_goto(SCREEN_SUPPLY);
        } else {
            ui_router_cancel_gestures();
        }
        const float set = supply_screen_set_v();
        glide(0, SU_TRACK_X + 200, SU_V_Y, 8);
        lift(0);
        CHECK_NEAR(supply_screen_set_v(), set, 0.001f);

        /* And the screen answers the next press. */
        finger(0, SU_OUT_X, SU_OUT_Y);
        for (int i = 0; i < 50; ++i) {
            ui_router_tick(0.05f);
        }
        supply_cmd_t c1 = { 0 };
        const bool p1 = supply_screen_poll_cmd(&c1);
        lift(0);
        CHECK(p1 && c1.on);
    }
}

/*
 * A release the finger did not make: the router's, for a contact the screen
 * owns that reaches the band, and the tracker's, for a contact that jumps.
 * Both are where the contact was last seen, which can be on the control it
 * pressed.  They end the press and activate nothing: not a tile, not the
 * home tag, not the alert strip, not the splash's skip.  STOP takes either.
 */
TEST_CASE(a_release_made_for_the_finger_activates_nothing_but_stop)
{
    fresh();
    to_overview();
    feed_reset();

    /* A tile, 90 px under the band, carried into the band in one report. */
    finger(FEED_LONE, 110, UI_BAND_H + 90);
    finger(FEED_LONE, 110, UI_BAND_H - 8);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    lift(FEED_LONE);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    /* The same px by px: the last point in the body is on the tile. */
    finger(FEED_LONE, 110, UI_BAND_H + 30);
    glide(FEED_LONE, 110, UI_BAND_H - 8, 4);
    lift(FEED_LONE);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);

    /* A tile, and the contact next reported off every tile. */
    finger(FEED_LONE, 110, UI_BAND_H + 90);
    finger(FEED_LONE, 110, UI_BAND_H + 90 + 121);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    lift(FEED_LONE);

    /* The tap after it is a tap. */
    feed_tap(FEED_LONE, 110, UI_BAND_H + 90);
    CHECK_EQ(ui_router_current(), SCREEN_MOTOR);

    /* The home tag, and the contact next reported in the body. */
    finger(FEED_LONE, UI_TAG_X + 20, UI_TAG_Y + UI_TAG_H / 2);
    finger(FEED_LONE, 400, 300);
    lift(FEED_LONE);
    CHECK_EQ(ui_router_current(), SCREEN_MOTOR);
    feed_tap(FEED_LONE, UI_TAG_X + 20, UI_TAG_Y + UI_TAG_H / 2);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);

    /* The alert strip. */
    ui_router_set_alert("supply not answering -- output off");
    finger(FEED_LONE, 400, H - 10);
    finger(FEED_LONE, 400, 200);
    lift(FEED_LONE);
    CHECK(ui_router_alert() != NULL);
    feed_tap(FEED_LONE, 400, H - 10);
    CHECK(ui_router_alert() == NULL);

    /* STOP: the release the tracker makes over it is a stop. */
    const gfx_rect_t stop = ui_band_stop_rect();
    CHECK(!ui_router_take_stop());
    finger(FEED_LONE, stop.x + stop.w / 2, stop.y + stop.h / 2);
    finger(FEED_LONE, 200, 300);
    CHECK(ui_router_take_stop());
    lift(FEED_LONE);

    /* The splash's skip. */
    fresh();
    for (int i = 0; i < SPLASH_STEP_COUNT; ++i) {
        splash_screen_set((splash_step_t)i, SPLASH_WARN, "");
    }
    feed_reset();
    finger(FEED_LONE, 100, 100);
    finger(FEED_LONE, 600, 400);
    CHECK(!splash_screen_done());
    lift(FEED_LONE);
    CHECK(splash_screen_done());
}

/* Everything the panel shows, as one number. */
static uint32_t panel_look(void)
{
    uint32_t h = 2166136261u;
    ui_router_invalidate();
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    ui_router_render(&cv, 0);
    for (int i = 0; i < W * H; ++i) {
        h = (h ^ (uint32_t)fb[i]) * 16777619u;
    }
    return h;
}

/* MOTOR & ESC: RESET PEAKS takes a release the finger made.  DISARM takes
 * any: disarming is not held back by how the release came about. */
TEST_CASE(reset_peaks_needs_the_fingers_release_and_disarm_does_not)
{
    /* Mirrored from motor_screen.c: the right rail's two buttons. */
    const int rail_x = 676, arm_y = UI_BAND_H + 318, reset_y = UI_BAND_H + 402;
    fresh();
    to_overview();
    ui_router_goto(SCREEN_MOTOR);
    feed_reset();
    motor_cmd_t c;
    while (motor_screen_poll_cmd(&c)) { }

    finger(FEED_LONE, rail_x, reset_y);
    finger(FEED_LONE, 300, UI_BAND_H + 120);  /* the plot */
    CHECK_EQ(feed_ups, 1);                    /* the tracker's release */
    lift(FEED_LONE);
    CHECK(!motor_screen_poll_cmd(&c));
    feed_tap(FEED_LONE, rail_x, reset_y);
    CHECK(motor_screen_poll_cmd(&c));
    CHECK_EQ(c.kind, MOTOR_CMD_RESET_PEAKS);

    motor_screen_set_armed(true);
    while (motor_screen_poll_cmd(&c)) { }
    finger(FEED_LONE, rail_x, arm_y);
    finger(FEED_LONE, 300, UI_BAND_H + 120);
    CHECK(motor_screen_poll_cmd(&c));
    CHECK_EQ(c.kind, MOTOR_CMD_DISARM);
    lift(FEED_LONE);
    motor_screen_set_armed(false);
}

/* SERVO: SETTINGS and the overlay's CLOSE lie 24 px and 27 px under the
 * band.  Carried into it, they are released for the screen where they were
 * pressed, and neither acts. */
TEST_CASE(servo_settings_and_close_carried_into_the_band_do_nothing)
{
    /* Mirrored from servo_screen.c. */
    const int set_x = 734, set_y = UI_BAND_H + 24;
    const int close_x = 439, close_y = UI_BAND_H + 27;
    fresh();
    to_overview();
    ui_router_goto(SCREEN_SERVO);
    feed_reset();
    const uint32_t shut = panel_look();

    finger(FEED_LONE, set_x, set_y);
    finger(FEED_LONE, set_x, UI_BAND_H - 8);
    lift(FEED_LONE);
    CHECK_EQ(panel_look(), shut);
    feed_tap(FEED_LONE, set_x, set_y);
    const uint32_t open = panel_look();
    CHECK(open != shut);

    finger(FEED_LONE, close_x, close_y);
    finger(FEED_LONE, close_x, UI_BAND_H - 8);
    lift(FEED_LONE);
    CHECK_EQ(panel_look(), open);
    feed_tap(FEED_LONE, close_x, close_y);
    CHECK_EQ(panel_look(), shut);
}

/* The band's chip names the role an arm covers: the motor's, the servo's,
 * or ARMED alone for both, as an arm with no link names no role. */
TEST_CASE(the_armed_chip_names_the_role)
{
    CHECK_STR_EQ(ui_band_armed_label(UI_ARMED_MOTOR), "ARMED MOTOR");
    CHECK_STR_EQ(ui_band_armed_label(UI_ARMED_SERVO), "ARMED SERVO");
    CHECK_STR_EQ(ui_band_armed_label(UI_ARMED_MOTOR | UI_ARMED_SERVO),
                 "ARMED");
    CHECK_STR_EQ(ui_band_armed_label(0u), "ARMED");
    CHECK_STR_EQ(ui_band_armed_label(0xF0u | UI_ARMED_SERVO), "ARMED SERVO");
}

/* The band draws the label, not only the label function returns it: an arm
 * for one role paints a wider chip than ARMED alone, and the two roles paint
 * different ones. */
static void band_look(uint8_t roles, int *danger, uint32_t *hash)
{
    const ui_bench_status_t st = {
        .link_up = true, .armed = true, .armed_roles = roles,
        .run_seconds = 0, .mode = NULL,
    };
    fresh();
    to_overview();
    ui_router_set_status(&st);
    ui_router_invalidate();
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    ui_router_render(&cv, 0);
    *danger = 0;
    *hash   = 2166136261u;
    for (int y = 0; y < UI_BAND_H; ++y) {
        for (int x = 0; x < W; ++x) {
            const gfx_color_t px = fb[(size_t)y * W + x];
            if (px == ui_theme_color(UI_C_DANGER)) { ++*danger; }
            *hash = (*hash ^ (uint32_t)px) * 16777619u;
        }
    }
}

TEST_CASE(the_band_renders_the_role_of_the_arm)
{
    int d_none, d_motor, d_servo, d_both;
    uint32_t h_none, h_motor, h_servo, h_both;
    band_look(0u, &d_none, &h_none);
    band_look(UI_ARMED_MOTOR, &d_motor, &h_motor);
    band_look(UI_ARMED_SERVO, &d_servo, &h_servo);
    band_look(UI_ARMED_MOTOR | UI_ARMED_SERVO, &d_both, &h_both);
    CHECK(d_motor > d_none);
    CHECK(d_servo > d_none);
    CHECK_EQ(d_both, d_none);
    CHECK_EQ(h_both, h_none);
    CHECK(h_motor != h_none);
    CHECK(h_servo != h_none);
    CHECK(h_motor != h_servo);
}


/* ------------------------------------------------------- help bubbles */

/* A step the rest of a case cannot do without: names it and ends the case. */
#define REQUIRE(cond)                                                         \
    do {                                                                      \
        if (!(cond)) {                                                        \
            T_FAIL("expected %s", #cond);                                     \
            return;                                                           \
        }                                                                     \
    } while (0)

/* Every chip on the band at once: the link up, BENCH, a fault, SAFE and
 * a run clock. */
static const ui_bench_status_t k_all_chips = {
    .link_up = true, .armed = false, .faults = 0x10, .run_seconds = 5,
    .mode = "BENCH",
};

/* A tap at the middle of @p chip as @p st lays the band out. */
static void tap_chip(const ui_bench_status_t *st, ui_band_chip_t chip)
{
    gfx_rect_t r;
    REQUIRE(ui_band_chip_rect(st, chip, &r));
    tap(r.x + r.w / 2, r.y + r.h / 2);
}

/* The menu with @p st on the band and @p chip's bubble open. */
static void open_on_overview(const ui_bench_status_t *st, ui_band_chip_t chip)
{
    fresh();
    to_overview();
    ui_router_set_status(st);
    tap_chip(st, chip);
}

TEST_CASE(a_tap_on_each_chip_opens_its_bubble)
{
    for (int chip = UI_BAND_CHIP_LINK; chip < UI_BAND_CHIP_COUNT; ++chip) {
        open_on_overview(&k_all_chips, (ui_band_chip_t)chip);
        CHECK_EQ(ui_router_help(), (ui_band_chip_t)chip);
        gfx_rect_t r;
        CHECK(ui_router_help_rect(&r));
        /* Under the band, in the panel. */
        CHECK(r.y >= UI_BAND_H);
        CHECK(r.x >= 0 && r.x + r.w <= W);
        /* Drawn: the bubble's fill where the menu has its background. */
        ui_router_render(&cv, 0);
        CHECK_EQ(fb[(size_t)(r.y + r.h - 4) * W + r.x + r.w / 2],
                 ui_theme_color(UI_C_PANEL));
    }
}

/* The DOWN that lands on a chip and the release that leaves it open
 * nothing; nor does a tap past the chips' ends. */
TEST_CASE(a_slide_off_a_chip_opens_nothing)
{
    fresh();
    to_overview();
    ui_router_set_status(&k_all_chips);
    gfx_rect_t r;
    REQUIRE(ui_band_chip_rect(&k_all_chips, UI_BAND_CHIP_LINK, &r));
    touch(r.x + 4, 20, TOUCH_EVENT_DOWN, 1);
    touch(r.x - 40, 20, TOUCH_EVENT_MOVE, 1);
    touch(r.x - 40, 20, TOUCH_EVENT_UP, 1);
    CHECK_EQ(ui_router_help(), UI_BAND_CHIP_NONE);
    /* From one chip to the next. */
    gfx_rect_t m;
    REQUIRE(ui_band_chip_rect(&k_all_chips, UI_BAND_CHIP_MODE, &m));
    touch(r.x + 4, 20, TOUCH_EVENT_DOWN, 1);
    touch(m.x + 4, 20, TOUCH_EVENT_UP, 1);
    CHECK_EQ(ui_router_help(), UI_BAND_CHIP_NONE);
    /* A gesture cancelled between the DOWN and the UP. */
    touch(r.x + 4, 20, TOUCH_EVENT_DOWN, 1);
    ui_router_cancel_gestures();
    touch(r.x + 4, 20, TOUCH_EVENT_UP, 1);
    CHECK_EQ(ui_router_help(), UI_BAND_CHIP_NONE);
    /* The band between the wordmark and the first chip. */
    tap(r.x - 20, 20);
    CHECK_EQ(ui_router_help(), UI_BAND_CHIP_NONE);
}

/* Any tap closes the bubble and does nothing else: not the tile under it,
 * not the chip it lands on, not the alert's x. */
TEST_CASE(a_tap_closes_the_bubble_and_does_nothing_else)
{
    open_on_overview(&k_all_chips, UI_BAND_CHIP_LINK);
    const uint32_t sent = ui_router_dispatched();
    tap(80, UI_BAND_H + 100);                       /* MOTOR & ESC's tile */
    CHECK_EQ(ui_router_help(), UI_BAND_CHIP_NONE);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    CHECK_EQ(ui_router_dispatched(), sent);

    /* A tap on another chip closes and opens nothing. */
    tap_chip(&k_all_chips, UI_BAND_CHIP_LINK);
    CHECK_EQ(ui_router_help(), UI_BAND_CHIP_LINK);
    tap_chip(&k_all_chips, UI_BAND_CHIP_FAULT);
    CHECK_EQ(ui_router_help(), UI_BAND_CHIP_NONE);

    /* The alert stays, and so does the home tag's screen. */
    ui_router_goto(SCREEN_ANALYSER);
    ui_router_set_alert("a passing alert");
    tap_chip(&k_all_chips, UI_BAND_CHIP_ARMED);
    CHECK_EQ(ui_router_help(), UI_BAND_CHIP_ARMED);
    tap(W / 2, H - 10);
    CHECK_EQ(ui_router_help(), UI_BAND_CHIP_NONE);
    CHECK(ui_router_alert() != NULL);
    tap_chip(&k_all_chips, UI_BAND_CHIP_ARMED);
    tap(UI_TAG_X + 20, UI_TAG_Y + UI_TAG_H / 2);
    CHECK_EQ(ui_router_help(), UI_BAND_CHIP_NONE);
    CHECK_EQ(ui_router_current(), SCREEN_ANALYSER);
    CHECK(!ui_router_take_stop());

    /* The closing contact's MOVE and UP reach no screen either. */
    tap_chip(&k_all_chips, UI_BAND_CHIP_ARMED);
    const uint32_t before = ui_router_dispatched();
    touch(300, 300, TOUCH_EVENT_DOWN, 3);
    touch(320, 310, TOUCH_EVENT_MOVE, 3);
    touch(320, 310, TOUCH_EVENT_UP, 3);
    CHECK_EQ(ui_router_dispatched(), before);
    CHECK_EQ(ui_router_help(), UI_BAND_CHIP_NONE);
}

/* STOP is not under the bubble's rule: its DOWN presses it at once, the
 * bubble closes with it, and the release latches the stop. */
TEST_CASE(stop_works_through_an_open_bubble)
{
    for (int chip = UI_BAND_CHIP_LINK; chip < UI_BAND_CHIP_COUNT; ++chip) {
        open_on_overview(&k_all_chips, (ui_band_chip_t)chip);
        const gfx_rect_t r = ui_band_stop_rect();
        touch(r.x + r.w / 2, r.y + r.h / 2, TOUCH_EVENT_DOWN, 1);
        CHECK_EQ(ui_router_help(), UI_BAND_CHIP_NONE);
        /* Pressed: drawn lighter than the button at rest. */
        ui_router_render(&cv, 0);
        CHECK(fb[(size_t)(r.y + 4) * W + r.x + 10]
              != ui_theme_color(UI_C_DANGER));
        touch(r.x + r.w / 2, r.y + r.h / 2, TOUCH_EVENT_UP, 1);
        CHECK(ui_router_take_stop());
    }
}

/* Every bubble in every state of its chip, both languages: clear of STOP,
 * inside the panel, and above the alert strip, so the tallest text fits. */
static void bubble_in_bounds(const ui_bench_status_t *st)
{
    const gfx_rect_t stop = ui_band_stop_rect();
    for (int chip = UI_BAND_CHIP_LINK; chip < UI_BAND_CHIP_COUNT; ++chip) {
        gfx_rect_t r;
        if (!ui_band_help_rect(st, (ui_band_chip_t)chip, &r)) {
            continue;
        }
        const bool apart = r.y >= stop.y + stop.h || r.x >= stop.x + stop.w
                           || r.x + r.w <= stop.x || r.y + r.h <= stop.y;
        if (!apart || r.x < 0 || r.x + r.w > W || r.y < UI_BAND_H
            || r.y + r.h > UI_ALERT_Y) {
            T_FAIL("chip %d, faults %04X, language %d: bubble %d,%d %dx%d",
                   chip, (unsigned)st->faults, (int)ui_text_language(),
                   r.x, r.y, r.w, r.h);
            return;
        }
    }
}

TEST_CASE(the_bubble_never_covers_stop_and_fits_in_both_languages)
{
    static const uint16_t faults[] = { 0x01, 0x10, 0x40, 0x51, 0x7F, 0xFF,
                                       0x80, 0xFFFF };
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        for (size_t f = 0; f < sizeof(faults) / sizeof(faults[0]); ++f) {
            for (int armed = 0; armed < 2; ++armed) {
                for (uint8_t roles = 0; roles < 4u; ++roles) {
                    for (int link = 0; link < 2; ++link) {
                        ui_bench_status_t st = k_all_chips;
                        st.faults      = faults[f];
                        st.armed       = armed != 0;
                        st.armed_roles = roles;
                        st.link_up     = link != 0;
                        st.mode        = ui_band_mode(st.link_up);
                        st.run_seconds = 3600u * 9u + 59u;
                        st.link_refused = UINT32_MAX;
                        bubble_in_bounds(&st);
                        st.mode        = ui_band_mode_inject();
                        bubble_in_bounds(&st);
                    }
                }
            }
        }
    }
    ui_text_set_language(UI_LANG_EN);
}

/* The widest row of chips under an hour, MOTOR & ESC ARMED in SIM, starts
 * right of that screen's home tag in both languages. */
TEST_CASE(the_widest_chip_row_clears_the_home_tag)
{
    ui_bench_status_t st;
    memset(&st, 0, sizeof(st));
    st.armed       = true;
    st.armed_roles = UI_ARMED_MOTOR;
    st.run_seconds = 59u * 60u + 59u;
    st.mode        = ui_band_mode(false);
    const gfx_rect_t tag = ui_home_tag_rect(ui_router_title(SCREEN_MOTOR));
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        for (int chip = UI_BAND_CHIP_NONE + 1; chip < UI_BAND_CHIP_COUNT;
             ++chip) {
            gfx_rect_t r;
            if (ui_band_chip_rect(&st, (ui_band_chip_t)chip, &r)
                && r.x < tag.x + tag.w) {
                T_FAIL("language %d, chip %d at x %d, the tag ends at %d",
                       lang, chip, r.x, tag.x + tag.w);
            }
        }
    }
    ui_text_set_language(UI_LANG_EN);
}

/*
 * The framebuffer of @p chip's bubble opened on the menu with @p first and
 * then shown with @p then, and of the bubble opened with @p then from the
 * start.  The two match when the bubble follows the state: same text, same
 * place, and nothing left of what it was.
 */
static uint32_t frame_hash(void)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < (size_t)W * H; ++i) {
        h = (h ^ (uint32_t)fb[i]) * 16777619u;
    }
    return h;
}

static void follows(const ui_bench_status_t *first,
                    const ui_bench_status_t *then, ui_band_chip_t chip)
{
    open_on_overview(then, chip);
    REQUIRE(ui_router_help() == chip);
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    ui_router_render(&cv, 0);
    const uint32_t want = frame_hash();

    open_on_overview(first, chip);
    REQUIRE(ui_router_help() == chip);
    ui_router_render(&cv, 0);
    ui_router_set_status(then);
    CHECK_EQ(ui_router_help(), chip);
    ui_router_render(&cv, 0);
    CHECK_EQ(frame_hash(), want);
}

TEST_CASE(the_fault_bubble_follows_the_bits_while_open)
{
    ui_bench_status_t a = k_all_chips, b = k_all_chips;
    a.faults = 0x10;
    b.faults = 0x11;                       /* LINK_SILENT appears */
    follows(&a, &b, UI_BAND_CHIP_FAULT);
    follows(&b, &a, UI_BAND_CHIP_FAULT);   /* and goes */
    b.faults = 0x7F;                       /* all seven at once */
    follows(&a, &b, UI_BAND_CHIP_FAULT);
    follows(&b, &a, UI_BAND_CHIP_FAULT);
    b.faults = 0x90;                       /* one this panel has no name for */
    follows(&a, &b, UI_BAND_CHIP_FAULT);
    b.faults = 0x0100;
    follows(&a, &b, UI_BAND_CHIP_FAULT);

    /* More bits, a taller bubble; an unknown bit says so with its advice. */
    gfx_rect_t r1, r2, r7, ru;
    ui_bench_status_t st = k_all_chips;
    st.faults = 0x01;
    REQUIRE(ui_band_help_rect(&st, UI_BAND_CHIP_FAULT, &r1));
    st.faults = 0x11;
    REQUIRE(ui_band_help_rect(&st, UI_BAND_CHIP_FAULT, &r2));
    st.faults = 0x7F;
    REQUIRE(ui_band_help_rect(&st, UI_BAND_CHIP_FAULT, &r7));
    st.faults = 0x80;
    REQUIRE(ui_band_help_rect(&st, UI_BAND_CHIP_FAULT, &ru));
    CHECK(r2.h > r1.h);
    CHECK(r7.h > r2.h);
    CHECK(ru.h > 30 + 10 + 18);           /* two paragraphs, not none */
}

/* The bubble closes with its chip: the faults clear, or the link goes and
 * takes the bitmap with it. */
TEST_CASE(a_bubble_whose_chip_goes_closes)
{
    open_on_overview(&k_all_chips, UI_BAND_CHIP_FAULT);
    ui_router_render(&cv, 0);
    ui_bench_status_t st = k_all_chips;
    st.faults = 0;
    ui_router_set_status(&st);
    CHECK_EQ(ui_router_help(), UI_BAND_CHIP_NONE);

    /* The clock goes when a run under a second ends. */
    st = k_all_chips;
    st.armed = true;
    st.run_seconds = 0;
    open_on_overview(&st, UI_BAND_CHIP_CLOCK);
    CHECK_EQ(ui_router_help(), UI_BAND_CHIP_CLOCK);
    st.armed = false;
    ui_router_set_status(&st);
    CHECK_EQ(ui_router_help(), UI_BAND_CHIP_NONE);
}

TEST_CASE(the_link_bubble_follows_the_link)
{
    ui_bench_status_t up = k_all_chips, down = k_all_chips;
    down.link_up = false;
    down.faults  = 0;                 /* the panel shows none without a link */
    down.mode    = ui_band_mode(false);
    up.mode      = ui_band_mode(true);
    follows(&up, &down, UI_BAND_CHIP_LINK);
    follows(&down, &up, UI_BAND_CHIP_LINK);
    follows(&up, &down, UI_BAND_CHIP_MODE);
    follows(&down, &up, UI_BAND_CHIP_MODE);
}

/*
 * INJECT, the mode a build with RCBENCH_TOUCH_INJECTOR names while a touch
 * injector is attached, has both its paragraphs under its title in both
 * languages; a mode string the band has no text for is its title alone.
 * The floor is each paragraph's cells over a line's 61, at 17 px a line
 * and 3 px between the two (ui_band.c's HELP_PITCH and HELP_PARA): a
 * bubble that drops either paragraph is shorter.
 */
TEST_CASE(the_inject_bubble_explains_the_injector)
{
    const int title_only = 30 + 10;   /* HELP_TOP + HELP_BOTTOM */
    const int cells = 61, pitch = 17, para = 3;
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        int floor_h = title_only + para;
        floor_h += pitch * ((gfx_text_cells(ui_tr(TX_BH_MODE_INJECT))
                             + cells - 1) / cells);
        floor_h += pitch * ((gfx_text_cells(ui_tr(TX_BH_MODE_INJECT_LOST))
                             + cells - 1) / cells);
        for (int link = 0; link < 2; ++link) {
            ui_bench_status_t st = k_all_chips;
            st.link_up = link != 0;
            st.mode    = ui_band_mode_inject();
            gfx_rect_t r;
            REQUIRE(ui_band_help_rect(&st, UI_BAND_CHIP_MODE, &r));
            if (r.h < floor_h) {
                T_FAIL("language %d, link %d: INJECT's bubble is %d px, "
                       "under the %d px its two paragraphs take",
                       lang, link, r.h, floor_h);
            }
            st.mode = "OTHER";
            REQUIRE(ui_band_help_rect(&st, UI_BAND_CHIP_MODE, &r));
            CHECK_EQ(r.h, title_only);
        }
        /* Each paragraph whole in the bubble's 640-byte buffer. */
        CHECK(strlen(ui_tr(TX_BH_MODE_INJECT)) < 640u);
        CHECK(strlen(ui_tr(TX_BH_MODE_INJECT_LOST)) < 640u);
    }
    ui_text_set_language(UI_LANG_EN);
    CHECK_STR_EQ(ui_band_mode_inject(), "INJECT");
}

TEST_CASE(the_armed_bubble_follows_the_roles)
{
    static const uint8_t roles[] = {
        0u, UI_ARMED_MOTOR, UI_ARMED_SERVO, UI_ARMED_MOTOR | UI_ARMED_SERVO,
    };
    ui_bench_status_t safe = k_all_chips;
    for (size_t i = 0; i < sizeof(roles) / sizeof(roles[0]); ++i) {
        ui_bench_status_t armed = k_all_chips;
        armed.armed       = true;
        armed.armed_roles = roles[i];
        follows(&safe, &armed, UI_BAND_CHIP_ARMED);
        follows(&armed, &safe, UI_BAND_CHIP_ARMED);
        /* With no link the arm runs the model, and the bubble says so. */
        ui_bench_status_t sim = armed;
        sim.link_up = false;
        sim.faults  = 0;
        sim.mode    = ui_band_mode(false);
        follows(&armed, &sim, UI_BAND_CHIP_ARMED);
        gfx_rect_t a, b;
        REQUIRE(ui_band_help_rect(&armed, UI_BAND_CHIP_ARMED, &a));
        REQUIRE(ui_band_help_rect(&sim, UI_BAND_CHIP_ARMED, &b));
        CHECK(b.h > a.h);
    }
    /* The three roles read three ways. */
    ui_bench_status_t m = k_all_chips, v = k_all_chips, all = k_all_chips;
    m.armed = v.armed = all.armed = true;
    m.armed_roles = UI_ARMED_MOTOR;
    v.armed_roles = UI_ARMED_SERVO;
    all.armed_roles = 0u;
    uint32_t hm, hv, ha;
    open_on_overview(&m, UI_BAND_CHIP_ARMED);
    ui_router_render(&cv, 0);
    hm = frame_hash();
    open_on_overview(&v, UI_BAND_CHIP_ARMED);
    ui_router_render(&cv, 0);
    hv = frame_hash();
    open_on_overview(&all, UI_BAND_CHIP_ARMED);
    ui_router_render(&cv, 0);
    ha = frame_hash();
    CHECK(hm != hv);
    CHECK(hm != ha);
    CHECK(hv != ha);
}

/*
 * The LINK bubble carries the coprocessor's refused frames as MOTOR & ESC's
 * header strip has them, ui_link_refused_label()'s line, while the link is
 * up and the count is above 0.
 */
TEST_CASE(the_link_bubble_carries_the_refused_frames)
{
    ui_bench_status_t none = k_all_chips, one = k_all_chips,
                      most = k_all_chips, down = k_all_chips;
    none.link_refused = 0u;
    one.link_refused  = 1u;
    most.link_refused = UINT32_MAX;
    down.link_up   = false;
    down.faults    = 0;
    down.mode      = ui_band_mode(false);
    ui_bench_status_t down_n = down;
    down_n.link_refused = 37u;

    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        gfx_rect_t r0, r1, rm, rd, rdn;
        REQUIRE(ui_band_help_rect(&none, UI_BAND_CHIP_LINK, &r0));
        REQUIRE(ui_band_help_rect(&one, UI_BAND_CHIP_LINK, &r1));
        REQUIRE(ui_band_help_rect(&most, UI_BAND_CHIP_LINK, &rm));
        REQUIRE(ui_band_help_rect(&down, UI_BAND_CHIP_LINK, &rd));
        REQUIRE(ui_band_help_rect(&down_n, UI_BAND_CHIP_LINK, &rdn));
        /* One line more at 1, and the widest count still one line. */
        CHECK_EQ(r1.h, r0.h + 17 + 3);
        CHECK_EQ(rm.h, r1.h);
        /* None with the link down, whatever the count. */
        CHECK_EQ(rdn.h, rd.h);

        /* The line drawn is the label, in the language showing. */
        char label[64];
        REQUIRE(ui_link_refused_label(&most, label, sizeof(label)));
        CHECK(gfx_text_cells(label)
              <= (rm.w - 32) / gfx_text_width(UI_FONT_LABEL, "M", 1));
    }
    ui_text_set_language(UI_LANG_EN);

    /* The link down draws the same bubble with a count as without. */
    open_on_overview(&down, UI_BAND_CHIP_LINK);
    ui_router_render(&cv, 0);
    const uint32_t without = frame_hash();
    open_on_overview(&down_n, UI_BAND_CHIP_LINK);
    ui_router_render(&cv, 0);
    CHECK_EQ(frame_hash(), without);

    /* Each count draws its own number. */
    open_on_overview(&one, UI_BAND_CHIP_LINK);
    ui_router_render(&cv, 0);
    const uint32_t at_one = frame_hash();
    open_on_overview(&most, UI_BAND_CHIP_LINK);
    ui_router_render(&cv, 0);
    CHECK(frame_hash() != at_one);
}

/* The count follows while the bubble is open: the line appears, its number
 * moves, it goes with the link. */
TEST_CASE(the_refused_line_follows_while_open)
{
    ui_bench_status_t none = k_all_chips, one = k_all_chips,
                      many = k_all_chips, down = k_all_chips;
    one.link_refused  = 1u;
    many.link_refused = 37u;
    down.link_up      = false;
    down.faults       = 0;
    down.mode         = ui_band_mode(false);
    down.link_refused = 37u;
    follows(&none, &one, UI_BAND_CHIP_LINK);   /* the line appears */
    follows(&one, &many, UI_BAND_CHIP_LINK);   /* the number moves */
    follows(&many, &none, UI_BAND_CHIP_LINK);  /* the count reads 0 again */
    follows(&many, &down, UI_BAND_CHIP_LINK);  /* the link goes */
    follows(&down, &many, UI_BAND_CHIP_LINK);
}

/* The clock appears while the LINK bubble is open and moves every chip
 * left; the bubble moves with its chip and leaves nothing behind. */
TEST_CASE(a_bubble_follows_its_chip_when_the_band_moves)
{
    ui_bench_status_t still = k_all_chips, run = k_all_chips;
    still.run_seconds = 0;
    run.run_seconds   = 3600u * 2u;    /* H:MM:SS, the widest clock */
    gfx_rect_t a, b;
    REQUIRE(ui_band_chip_rect(&still, UI_BAND_CHIP_MODE, &a));
    REQUIRE(ui_band_chip_rect(&run, UI_BAND_CHIP_MODE, &b));
    REQUIRE(a.x != b.x);
    follows(&still, &run, UI_BAND_CHIP_MODE);
    follows(&run, &still, UI_BAND_CHIP_MODE);
    follows(&still, &run, UI_BAND_CHIP_FAULT);
    follows(&still, &run, UI_BAND_CHIP_ARMED);
}

/* Two framebuffers, as on the panel: the menu draws its tiles once into
 * each, so pixels a bubble leaves are not painted over by themselves. */
TEST_CASE(a_closed_bubble_leaves_no_pixels_behind)
{
    gfx_color_t *fb1 = calloc((size_t)W * H, sizeof(gfx_color_t));
    REQUIRE_ALLOC(fb1);
    gfx_canvas_t cv1;
    gfx_canvas_init(&cv1, fb1, W, H, W);

    for (int chip = UI_BAND_CHIP_LINK; chip < UI_BAND_CHIP_COUNT; ++chip) {
        /* The menu never opened. */
        fresh();
        to_overview();
        ui_router_set_status(&k_all_chips);
        ui_router_render(&cv, 0);
        const uint32_t want = frame_hash();

        fresh();
        memset(fb1, 0, (size_t)W * H * sizeof(gfx_color_t));
        to_overview();
        ui_router_set_status(&k_all_chips);
        ui_router_render(&cv, 0);
        ui_router_render(&cv1, 1);
        tap_chip(&k_all_chips, (ui_band_chip_t)chip);
        ui_router_render(&cv, 0);
        ui_router_render(&cv1, 1);
        tap(W / 2, 300);                        /* close */
        ui_router_render(&cv, 0);
        CHECK_EQ(frame_hash(), want);
        ui_router_render(&cv1, 1);
        CHECK_EQ(memcmp(fb1, fb, (size_t)W * H * sizeof(gfx_color_t)), 0);
    }
    free(fb1);
}

/* Leaving the screen closes the bubble, by HOME's way or any other. */
TEST_CASE(leaving_the_screen_closes_the_bubble)
{
    fresh();
    to_overview();
    ui_router_goto(SCREEN_MOTOR);
    ui_router_set_status(&k_all_chips);
    tap_chip(&k_all_chips, UI_BAND_CHIP_FAULT);
    CHECK_EQ(ui_router_help(), UI_BAND_CHIP_FAULT);
    ui_router_goto(SCREEN_SERVO);
    CHECK_EQ(ui_router_help(), UI_BAND_CHIP_NONE);
    gfx_rect_t r;
    CHECK(!ui_router_help_rect(&r));
}

/* A drag the screen owns when a second finger opens a bubble is released
 * at the edge's way, a release that activates nothing, and the dragging
 * finger reaches the screen no more. */
TEST_CASE(opening_a_bubble_releases_the_screens_contacts)
{
    on_screen(SCREEN_MOTOR);
    ui_router_set_status(&k_all_chips);
    motor_screen_set_armed(true);
    drain_motor();
    touch(300, M_TRACK_Y, TOUCH_EVENT_DOWN, 1);
    touch(360, M_TRACK_Y, TOUCH_EVENT_MOVE, 1);
    const float dragged = motor_screen_throttle();
    CHECK(dragged > 0.0f);
    const uint32_t before = ui_router_dispatched();
    gfx_rect_t r;
    REQUIRE(ui_band_chip_rect(&k_all_chips, UI_BAND_CHIP_LINK, &r));
    touch(r.x + 10, 20, TOUCH_EVENT_DOWN, 2);
    touch(r.x + 10, 20, TOUCH_EVENT_UP, 2);
    CHECK_EQ(ui_router_help(), UI_BAND_CHIP_LINK);
    CHECK_EQ(ui_router_dispatched(), before + 1u);   /* the release */
    touch(420, M_TRACK_Y, TOUCH_EVENT_MOVE, 1);
    touch(420, M_TRACK_Y, TOUCH_EVENT_UP, 1);
    CHECK_EQ(ui_router_dispatched(), before + 1u);
    CHECK_NEAR(motor_screen_throttle(), dragged, 0.01f);
    CHECK(!ui_router_take_stop());
    drain_motor();
}

/* No bubble where there is no band. */
TEST_CASE(a_screen_without_a_band_opens_no_bubble)
{
    fresh();
    ui_router_set_status(&k_all_chips);
    tap_chip(&k_all_chips, UI_BAND_CHIP_LINK);     /* the splash */
    CHECK_EQ(ui_router_help(), UI_BAND_CHIP_NONE);
    to_overview();
    ui_router_goto(SCREEN_BUSFAULT);
    tap_chip(&k_all_chips, UI_BAND_CHIP_LINK);
    CHECK_EQ(ui_router_help(), UI_BAND_CHIP_NONE);
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
    RUN(the_setup_tiles_open_the_outputs_the_picker_and_updates);
    RUN(the_home_tag_climbs_from_a_setup_list_to_the_tiles_to_the_menu);
    RUN(home_under_a_setup_scroll_leaves_the_contact_to_no_screen);
    RUN(a_tile_navigates_and_a_slip_does_not);
    RUN(every_screen_renders_something_and_has_a_title);
    RUN(an_unknown_screen_id_falls_back_to_the_overview);
    RUN(an_alert_survives_navigation);
    RUN(an_alert_clears_after_30_s_and_a_new_one_starts_again);
    RUN(the_frame_an_alert_arrives_in_does_not_count);
    RUN(a_second_contact_cannot_take_the_alert);
    RUN(a_held_alert_returns_after_a_passing_one);
    RUN(a_tap_on_the_alert_clears_it_and_reaches_no_screen);
    RUN(a_lift_clears_only_the_alert_it_was_pressed_on);
    RUN(a_press_on_an_alert_that_expires_stays_off_the_screen);
    RUN(cancelling_gestures_lets_go_of_the_alert);
    RUN(a_held_alert_stays_and_lets_taps_through);
    RUN(a_cleared_alert_leaves_no_red_behind);
    RUN(the_band_shows_what_is_wrong);
    RUN(the_band_names_the_bench_or_the_model_not_the_link);
    RUN(the_splash_holds_then_hands_over);
    RUN(a_tap_skips_the_splash_hold);
    RUN(a_failed_step_is_drawn_in_its_own_colour);
    RUN(the_alert_band_renders_at_the_bottom);
    RUN(the_simulation_mark_covers_the_whole_screen);
    RUN(the_simulation_mark_lets_the_screen_through);
    RUN(invalidating_the_router_repaints_every_screen);
    RUN(the_simulation_mark_follows_the_canvas_it_is_given);
    RUN(switching_the_mark_invalidates_the_cached_chrome);
    RUN(the_menu_marks_what_is_not_fitted);
    RUN(cancelling_gestures_lets_go_of_the_band);
    RUN(a_cancelled_tile_press_navigates_nowhere);
    RUN(a_cancel_reaches_a_screen_left_during_the_frame);
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
    RUN(a_stop_ends_a_throttle_drag);
    RUN(a_disarm_ends_a_throttle_drag);
    RUN(an_arm_ends_a_throttle_drag);
    RUN(leaving_ends_a_throttle_drag);
    RUN(a_touch_loss_ends_a_throttle_drag);
    RUN(a_dial_drag_that_leaves_by_the_band_ends_at_the_edge);
    RUN(a_slide_onto_the_dial_commands_nothing);
    RUN(a_speed_drag_that_leaves_by_the_band_ends_at_the_edge);
    RUN(home_under_a_dial_drag_ends_the_drag);
    RUN(a_touch_loss_ends_a_dial_drag);
    RUN(a_set_point_drag_that_leaves_by_the_band_ends_at_the_edge);
    RUN(output_off_answers_the_first_tap_after_a_drag_left_by_the_band);
    RUN(an_output_off_press_that_leaves_by_the_band_asks_for_nothing);
    RUN(an_output_on_hold_that_leaves_by_the_band_switches_nothing_on);
    RUN(leaving_or_a_touch_loss_ends_a_set_point_drag);
    RUN(a_release_made_for_the_finger_activates_nothing_but_stop);
    RUN(reset_peaks_needs_the_fingers_release_and_disarm_does_not);
    RUN(servo_settings_and_close_carried_into_the_band_do_nothing);
    RUN(the_armed_chip_names_the_role);
    RUN(the_band_renders_the_role_of_the_arm);
    RUN(a_tap_on_each_chip_opens_its_bubble);
    RUN(a_slide_off_a_chip_opens_nothing);
    RUN(a_tap_closes_the_bubble_and_does_nothing_else);
    RUN(stop_works_through_an_open_bubble);
    RUN(the_bubble_never_covers_stop_and_fits_in_both_languages);
    RUN(the_widest_chip_row_clears_the_home_tag);
    RUN(the_fault_bubble_follows_the_bits_while_open);
    RUN(a_bubble_whose_chip_goes_closes);
    RUN(the_link_bubble_follows_the_link);
    RUN(the_inject_bubble_explains_the_injector);
    RUN(the_armed_bubble_follows_the_roles);
    RUN(the_link_bubble_carries_the_refused_frames);
    RUN(the_refused_line_follows_while_open);
    RUN(a_bubble_follows_its_chip_when_the_band_moves);
    RUN(a_closed_bubble_leaves_no_pixels_behind);
    RUN(leaving_the_screen_closes_the_bubble);
    RUN(opening_a_bubble_releases_the_screens_contacts);
    RUN(a_screen_without_a_band_opens_no_bubble);
    return test_summary("nav");
}
