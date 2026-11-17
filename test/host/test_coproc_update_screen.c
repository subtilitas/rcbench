/*
 * The IOmcu update's screen and its driver: the progress, which takes no
 * touch and carries no band, inside its canvas at every step and count;
 * the version comparison behind the word "older"; the START UPDATES' hold
 * hands over and the way back to UPDATES; and the offer as the driver
 * serves it: screens the panel forces over UPDATES and STOP decline
 * nothing, the next link-up offers again, a decline counts for the offer
 * UPDATES showed only, and the band line names what the opening waits for
 * and stays silent while an update run waits to open UPDATES itself.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>
#include <string.h>

#include "greatest.h"
#include "guard_canvas.h"

#include "coproc_update_screen.h"
#include "ui_band.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "upd_screen.h"
#include "upd_single.h"

/* UPDATES' SKIP THIS STEP, in the body's coordinates. */
#include "upd_spots.h"

static const ui_screen_t *scr(void) { return coproc_update_screen(); }

static const cu_version_t k_old = { 0, 17, 0 };
static const cu_version_t k_new = { 0, 18, 0 };

TEST_CASE(older_compares_major_then_minor_then_patch)
{
    CHECK(coproc_update_older((cu_version_t){ 0, 18, 0 },
                              (cu_version_t){ 0, 17, 9 }));
    CHECK(coproc_update_older((cu_version_t){ 1, 0, 0 },
                              (cu_version_t){ 0, 99, 99 }));
    CHECK(coproc_update_older((cu_version_t){ 0, 18, 2 },
                              (cu_version_t){ 0, 18, 1 }));
    CHECK(!coproc_update_older((cu_version_t){ 0, 18, 0 },
                               (cu_version_t){ 0, 18, 0 }));
    CHECK(!coproc_update_older((cu_version_t){ 0, 17, 9 },
                               (cu_version_t){ 0, 18, 0 }));
    CHECK(!coproc_update_older((cu_version_t){ 0, 99, 99 },
                               (cu_version_t){ 1, 0, 0 }));
}

/* The progress takes no touch, has no home tag and no band: the IOmcu
 * refuses ARM for the whole session, so STOP has nothing to stop. */
TEST_CASE(the_progress_takes_no_touch_and_carries_no_band)
{
    CHECK(scr()->event == NULL);
    CHECK(scr()->back == NULL);
    CHECK(scr()->hold == NULL);
    ui_router_init();
    ui_router_goto(SCREEN_COPROC_UPDATE);
    CHECK(!ui_router_stop_live());
    ui_router_goto(SCREEN_OVERVIEW);
    CHECK(ui_router_stop_live());
}

/* Every step, with the count at 0, part way, at the end and past it, for
 * every pair of versions, in both languages, inside the canvas. */
TEST_CASE(every_view_renders_inside_its_canvas)
{
    static gfx_color_t px[GUARD_CANVAS_PIXELS(UI_PANEL_W, UI_PANEL_H)];
    gfx_canvas_t c;
    guard_canvas_t g;
    static const cu_step_t steps[] = { CU_STEP_PREPARE, CU_STEP_SEND,
                                       CU_STEP_VERIFY, CU_STEP_RESTART,
                                       CU_STEP_BUY };
    static const uint16_t counts[][2] = { { 0, 0 }, { 0, 2994 },
                                          { 1497, 2994 }, { 2994, 2994 },
                                          { 3000, 2994 } };
    static const cu_version_t pairs[][2] = {
        { { 0, 17, 0 }, { 0, 18, 0 } },
        { { 0, 18, 0 }, { 0, 17, 0 } },
        { { 65535, 65535, 65535 }, { 65535, 65535, 65535 } },
    };
    ui_theme_set(UI_THEME_DARK);
    for (int lang = 0; lang < 2; ++lang) {
        ui_text_set_language(lang == 0 ? UI_LANG_EN : UI_LANG_DE);
        for (size_t p = 0; p < sizeof(pairs) / sizeof(pairs[0]); ++p) {
            scr()->reset();
            coproc_update_screen_versions(pairs[p][0], pairs[p][1]);
            for (size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); ++i) {
                for (size_t k = 0; k < sizeof(counts) / sizeof(counts[0]);
                     ++k) {
                    coproc_update_screen_progress(steps[i], counts[k][0],
                                                  counts[k][1]);
                    guard_canvas_init(&g, &c, px, UI_PANEL_W, UI_PANEL_H);
                    coproc_update_screen_invalidate();
                    scr()->render(&c, 1);
                    CHECK_EQ(guard_canvas_touched(&g), 0u);
                    CHECK_EQ(guard_canvas_unpainted(&g), 0u);
                }
            }
        }
    }
    ui_text_set_language(UI_LANG_EN);
}

/* ------------------------------------------------------------ the driver */

static cu_driver_t g_drv;
static cu_feed_t   g_feed;

/* The menu, nothing offered, the driver fresh, an IOmcu on another build
 * answering. */
static void drive_fresh(void)
{
    ui_theme_set(UI_THEME_DARK);
    ui_router_init();
    ui_router_goto(SCREEN_OVERVIEW);
    ui_router_set_alert(NULL);
    memset(&g_drv, 0, sizeof(g_drv));
    memset(&g_feed, 0, sizeof(g_feed));
    g_feed.running = k_old;
    g_feed.carried = k_new;
    g_feed.have_image = true;
    g_feed.reachable = true;
    g_feed.check = CU_CHECK_DIFFERENT;
}

static cu_hand_t frame(void)
{
    cu_hand_t h;
    coproc_update_drive(&g_drv, &g_feed, false, &h);
    return h;
}

/* An offer, and the frame that opens UPDATES for it. */
static void offer(void)
{
    g_feed.offered = true;
    ++g_feed.offer_gen;
    (void)frame();
}

/* The other link-up: the check withdraws the offer, then makes it anew. */
static cu_hand_t link_up_again(void)
{
    g_feed.offered = false;
    (void)frame();
    g_feed.offered = true;
    ++g_feed.offer_gen;
    return frame();
}

static void router_tap(int x, int y)
{
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = (int16_t)x,
                                   .y = (int16_t)y, .strength = 40 } };
    ui_router_event(&e);
    e.type = TOUCH_EVENT_UP;
    ui_router_event(&e);
}

/* SKIP THIS STEP on UPDATES' IOmcu step. */
static void skip_tap(void)
{
    upd_spot_tap(upd_screen(), UP_ACT_X, UP_SKIP_Y);
}

static bool band_says(ui_text_id_t id)
{
    const char *a = ui_router_alert();
    return a != NULL && strcmp(a, ui_tr(id)) == 0;
}

TEST_CASE(the_driver_takes_no_null)
{
    cu_hand_t h = { 5u, true };
    coproc_update_drive(NULL, &g_feed, false, &h);
    CHECK_EQ(h.starts, 0u);
    CHECK(!h.decline);
    coproc_update_drive(&g_drv, NULL, false, &h);
    coproc_update_drive(&g_drv, &g_feed, false, NULL);
}

/* UPDATES' completed hold: the START once, the progress, an alert's jump
 * during the update undone, and UPDATES again at the end. */
TEST_CASE(a_start_from_updates_runs_the_progress_and_returns_to_updates)
{
    drive_fresh();
    offer();
    CHECK_EQ(ui_router_current(), SCREEN_UPDATES);
    upd_single_fire_coproc();
    cu_hand_t h = frame();
    CHECK_EQ(h.starts, 1u);
    CHECK(!h.decline);
    CHECK_EQ(ui_router_current(), SCREEN_COPROC_UPDATE);

    /* Taken: the offer goes, the update runs. */
    g_feed.offered = false;
    g_feed.active = true;
    g_feed.starts_taken = 1u;
    g_feed.step = CU_STEP_SEND;
    g_feed.blocks = 2994u;
    h = frame();
    CHECK_EQ(h.starts, 0u);
    CHECK_EQ(ui_router_current(), SCREEN_COPROC_UPDATE);
    ui_router_goto(SCREEN_LOGS);
    h = frame();
    CHECK(!h.decline);
    CHECK_EQ(ui_router_current(), SCREEN_COPROC_UPDATE);

    g_feed.active = false;
    h = frame();
    CHECK(!h.decline);
    CHECK_EQ(h.starts, 0u);
    CHECK_EQ(ui_router_current(), SCREEN_UPDATES);

    /* A hold completed off UPDATES starts nothing. */
    ui_router_goto(SCREEN_OVERVIEW);
    g_feed.offered = true;
    g_feed.offer_gen = 7u;
    g_drv.shown_gen = 7u;
    upd_single_fire_coproc();
    h = frame();
    CHECK_EQ(h.starts, 0u);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
}

/*
 * Every screen the panel can force over UPDATES, and the rest of the
 * router's jumps: nobody declined, and the same offer does not open
 * UPDATES again.  The two the panel forces are SCREEN_BUSFAULT
 * (panel_frame.c, a link gone for 4000ms) and SCREEN_UPDATE
 * (panel_update.c, a download whose hold completed on the reminder).
 */
TEST_CASE(a_screen_forced_over_updates_declines_nothing)
{
    static const ui_screen_id_t k_to[] = {
        SCREEN_BUSFAULT, SCREEN_UPDATE, SCREEN_OVERVIEW, SCREEN_MOTOR,
        SCREEN_LOGS, SCREEN_SETUP, SCREEN_OUTPUTS, SCREEN_SUPPLY,
    };
    for (size_t i = 0; i < sizeof(k_to) / sizeof(k_to[0]); ++i) {
        drive_fresh();
        offer();
        CHECK_EQ(ui_router_current(), SCREEN_UPDATES);
        ui_router_goto(k_to[i]);
        for (int n = 0; n < 3; ++n) {
            const cu_hand_t h = frame();
            CHECK(!h.decline);
            CHECK_EQ(ui_router_current(), k_to[i]);
        }
    }
}

/* STOP leaves UPDATES up and declines nothing; the fault screen after it
 * declines nothing either. */
TEST_CASE(stop_then_the_fault_screen_declines_nothing)
{
    drive_fresh();
    offer();
    const gfx_rect_t stop = ui_band_stop_rect();
    router_tap(stop.x + stop.w / 2, stop.y + stop.h / 2);
    CHECK(ui_router_take_stop());
    cu_hand_t h = frame();
    CHECK(!h.decline);
    CHECK_EQ(ui_router_current(), SCREEN_UPDATES);
    ui_router_goto(SCREEN_BUSFAULT);
    h = frame();
    CHECK(!h.decline);
}

/* What the station saw: the link gone, the fault screen.  Still gone at
 * the acknowledgement: the menu, and UPDATES does not open over a dead
 * link.  The next link-up's check: UPDATES again, and a SKIP there
 * declines that offer. */
TEST_CASE(the_next_link_up_offers_again_after_the_fault_screen)
{
    drive_fresh();
    offer();
    ui_router_goto(SCREEN_BUSFAULT);
    cu_hand_t h = frame();
    CHECK(!h.decline);
    ui_router_goto(SCREEN_OVERVIEW);    /* acknowledged */
    for (int i = 0; i < 3; ++i) {
        h = frame();
        CHECK(!h.decline);
        CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    }
    h = link_up_again();
    CHECK(!h.decline);
    CHECK_EQ(ui_router_current(), SCREEN_UPDATES);
    CHECK_EQ(g_drv.shown_gen, 2u);
    skip_tap();
    h = frame();
    CHECK(h.decline);
}

/* The link back under the fault screen: the screen stays until its
 * acknowledgement, and UPDATES opens on the menu after it. */
TEST_CASE(a_link_up_under_the_fault_screen_waits_for_the_acknowledgement)
{
    drive_fresh();
    offer();
    ui_router_goto(SCREEN_BUSFAULT);
    cu_hand_t h = frame();
    CHECK(!h.decline);
    h = link_up_again();
    CHECK(!h.decline);
    CHECK_EQ(ui_router_current(), SCREEN_BUSFAULT);
    ui_router_goto(SCREEN_OVERVIEW);
    h = frame();
    CHECK(!h.decline);
    CHECK_EQ(ui_router_current(), SCREEN_UPDATES);
}

/* The new offer after the fault screen opens over nothing the first one
 * would not have: each condition holds it back. */
TEST_CASE(the_offer_after_the_fault_screen_keeps_the_conditions)
{
    drive_fresh();
    offer();
    ui_router_goto(SCREEN_BUSFAULT);
    (void)frame();
    ui_router_goto(SCREEN_OVERVIEW);
    g_feed.offered = false;
    (void)frame();
    g_feed.offered = true;
    ++g_feed.offer_gen;

    cu_hand_t h;
    coproc_update_drive(&g_drv, &g_feed, true, &h);     /* ARMED */
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    g_feed.radio_up = true;
    (void)frame();
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    g_feed.radio_up = false;
    g_feed.installing = true;
    (void)frame();
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    g_feed.installing = false;
    ui_router_goto(SCREEN_MOTOR);
    (void)frame();
    CHECK_EQ(ui_router_current(), SCREEN_MOTOR);
    ui_router_goto(SCREEN_OVERVIEW);
    h = frame();
    CHECK(!h.decline);
    CHECK_EQ(ui_router_current(), SCREEN_UPDATES);
}

/* An operator's SKIP just before the link goes is still his answer, the
 * fault screen on top of it or not. */
TEST_CASE(a_skip_just_before_the_fault_screen_still_declines)
{
    drive_fresh();
    offer();
    skip_tap();
    ui_router_goto(SCREEN_BUSFAULT);
    cu_hand_t h = frame();
    CHECK(h.decline);
    CHECK_EQ(ui_router_current(), SCREEN_BUSFAULT);   /* the fault stays */
    h = frame();
    CHECK(!h.decline);
}

/* The fault screen and a new offer between two frames: nothing declined,
 * and the new offer opens UPDATES after the acknowledgement. */
TEST_CASE(a_forced_screen_as_the_offer_changes_declines_nothing)
{
    drive_fresh();
    offer();
    ui_router_goto(SCREEN_BUSFAULT);
    ++g_feed.offer_gen;
    cu_hand_t h = frame();
    CHECK(!h.decline);
    CHECK_EQ(ui_router_current(), SCREEN_BUSFAULT);
    ui_router_goto(SCREEN_OVERVIEW);
    h = frame();
    CHECK(!h.decline);
    CHECK_EQ(ui_router_current(), SCREEN_UPDATES);
    CHECK_EQ(g_drv.shown_gen, g_feed.offer_gen);
}

/* An offer that goes declines nothing, and UPDATES stays where it is. */
TEST_CASE(an_offer_that_goes_declines_nothing)
{
    drive_fresh();
    offer();
    g_feed.offered = false;
    cu_hand_t h = frame();
    CHECK(!h.decline);
    CHECK_EQ(ui_router_current(), SCREEN_UPDATES);
    skip_tap();
    h = frame();
    CHECK(!h.decline);
}

/*
 * The band line of an offer names what the opening of UPDATES waits for:
 * SAFE and the supply off, the panel's Wi-Fi off, or the panel's install
 * to end, in that order.  Said once per offer on the first frame with a
 * band, again when it waits for something else, plain when nothing holds
 * it back.  While an update run waits to open UPDATES the line is the
 * run's: the offer says nothing and opens nothing, and its line counts as
 * said.
 */
TEST_CASE(the_offer_line_names_what_updates_waits_for)
{
    CHECK_EQ(coproc_update_offer_text(CU_WAIT_NONE), TX_ALERT_CU_OFFER);
    CHECK_EQ(coproc_update_offer_text(CU_WAIT_UNSAFE), TX_ALERT_CU_OFFER_SAFE);
    CHECK_EQ(coproc_update_offer_text(CU_WAIT_RADIO), TX_ALERT_CU_OFFER_WIFI);
    CHECK_EQ(coproc_update_offer_text(CU_WAIT_INSTALL),
             TX_ALERT_CU_OFFER_INSTALL);
    CHECK(strstr(TR(ALERT_CU_OFFER_SAFE), "UPDATES") != NULL);
    CHECK(strstr(TR(ALERT_CU_OFFER_WIFI), "UPDATES") != NULL);
    CHECK(strstr(TR(ALERT_CU_OFFER_INSTALL), "UPDATES") != NULL);

    drive_fresh();
    g_feed.radio_up = true;
    g_feed.installing = true;
    CHECK_EQ(coproc_update_wait(&g_feed, true), CU_WAIT_UNSAFE);
    CHECK_EQ(coproc_update_wait(&g_feed, false), CU_WAIT_RADIO);
    g_feed.radio_up = false;
    CHECK_EQ(coproc_update_wait(&g_feed, false), CU_WAIT_INSTALL);
    g_feed.installing = false;
    CHECK_EQ(coproc_update_wait(&g_feed, false), CU_WAIT_NONE);

    /* Under the splash nothing is said; the menu says the Wi-Fi. */
    drive_fresh();
    ui_router_goto(SCREEN_SPLASH);
    g_feed.offered = true;
    g_feed.offer_gen = 1u;
    g_feed.radio_up = true;
    (void)frame();
    CHECK(ui_router_alert() == NULL);
    ui_router_goto(SCREEN_OVERVIEW);
    (void)frame();
    CHECK(band_says(TX_ALERT_CU_OFFER_WIFI));
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);

    /* Said once: cleared, the same wait says nothing again. */
    ui_router_set_alert(NULL);
    (void)frame();
    CHECK(ui_router_alert() == NULL);

    /* Another wait is said; its end opens UPDATES, not a line. */
    g_feed.radio_up = false;
    g_feed.installing = true;
    (void)frame();
    CHECK(band_says(TX_ALERT_CU_OFFER_INSTALL));
    ui_router_set_alert(NULL);
    g_feed.installing = false;
    (void)frame();
    CHECK(ui_router_alert() == NULL);
    CHECK_EQ(ui_router_current(), SCREEN_UPDATES);

    /* ARMED: SAFE and the supply off. */
    drive_fresh();
    g_feed.offered = true;
    g_feed.offer_gen = 1u;
    cu_hand_t h;
    coproc_update_drive(&g_drv, &g_feed, true, &h);
    CHECK(band_says(TX_ALERT_CU_OFFER_SAFE));

    /* An update run waits to open UPDATES: the run's line stands, in every
     * wait, and the offer opens nothing. */
    drive_fresh();
    ui_router_set_alert("run");
    g_feed.offered = true;
    g_feed.offer_gen = 1u;
    g_feed.run_waits = true;
    coproc_update_drive(&g_drv, &g_feed, true, &h);
    g_feed.radio_up = true;
    (void)frame();
    g_feed.radio_up = false;
    (void)frame();
    CHECK_STR_EQ(ui_router_alert(), "run");
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    CHECK_EQ(g_drv.said_gen, 1u);
    /* The run opened UPDATES: the offer is shown there, no line follows. */
    g_feed.run_waits = false;
    ui_router_goto(SCREEN_UPDATES);
    (void)frame();
    CHECK_EQ(g_drv.shown_gen, 1u);
    CHECK_STR_EQ(ui_router_alert(), "run");
}

int main(void)
{
    RUN(older_compares_major_then_minor_then_patch);
    RUN(the_progress_takes_no_touch_and_carries_no_band);
    RUN(every_view_renders_inside_its_canvas);
    RUN(the_driver_takes_no_null);
    RUN(a_start_from_updates_runs_the_progress_and_returns_to_updates);
    RUN(a_screen_forced_over_updates_declines_nothing);
    RUN(stop_then_the_fault_screen_declines_nothing);
    RUN(the_next_link_up_offers_again_after_the_fault_screen);
    RUN(a_link_up_under_the_fault_screen_waits_for_the_acknowledgement);
    RUN(the_offer_after_the_fault_screen_keeps_the_conditions);
    RUN(a_skip_just_before_the_fault_screen_still_declines);
    RUN(a_forced_screen_as_the_offer_changes_declines_nothing);
    RUN(an_offer_that_goes_declines_nothing);
    RUN(the_offer_line_names_what_updates_waits_for);
    return test_summary("coproc_update_screen");
}
