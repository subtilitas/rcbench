/*
 * Several injected contacts beside the glass's (touch_merge.h): their track
 * ids, the five GT911 slots the two sources share, a finger and an injected
 * contact on the same pixel, the order they lift in, the injected contact
 * cut for a finger on the glass, and the band's 60px rows.
 *
 * The path is touch.c's: each source mapped with its own map, merged,
 * cropped to the UI's 1024 x 600, through the tracker, the cut UPs marked
 * (touch_merge_mark_cut()), and the events to ui_router_event().
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include "greatest.h"

#include "splash_screen.h"
#include "touch_map.h"
#include "touch_merge.h"
#include "ui_band.h"
#include "ui_layout.h"
#include "ui_screen.h"
#include "ui_theme.h"

/** @brief A contact as a controller reports it. */
static touch_point_t pt(uint8_t id, int x, int y)
{
    return (touch_point_t){ .id = id, .x = (int16_t)x, .y = (int16_t)y,
                            .strength = 24 };
}

static touch_merge_t   mg;
static touch_tracker_t trk;
static touch_event_t   ev[TOUCH_MAX_POINTS * 2];
static int             nev;
static bool            to_router;

/** @brief The injector's map: panel pixels of the UI, unmirrored. */
static const touch_map_t k_inject_map = {
    .src_w = 1024, .src_h = 600, .mirror_x = false, .mirror_y = false,
    .rotation = TOUCH_ROTATION_0,
};

/** @brief A merge and a tracker with nothing down; no router. */
static void fresh(void)
{
    touch_merge_attach(&mg, 0);
    touch_tracker_reset(&trk);
    nev = 0;
    to_router = false;
}

/** @brief The merged frame through the crop, the tracker and the marks. */
static void publish(void)
{
    touch_point_t f[TOUCH_MAX_POINTS];
    int n = touch_merge_frame(&mg, f, TOUCH_MAX_POINTS);
    n = touch_map_crop(f, n, 1024, 600);
    nev = touch_tracker_update(&trk, f, n, ev, (int)(sizeof(ev) / sizeof(ev[0])));
    (void)touch_merge_mark_cut(&mg, ev, nev);
    for (int i = 0; to_router && i < nev; ++i) {
        ui_router_event(&ev[i]);
    }
}

/** @brief The injector reports @p n points, mapped as touch_inject.c maps. */
static void inject(const touch_point_t *pts, int n)
{
    touch_point_t p[TOUCH_MAX_POINTS];
    if (n > 0) {
        memcpy(p, pts, sizeof(p[0]) * (size_t)n);
    }
    touch_map_points(&k_inject_map, p, n);
    (void)touch_merge_inject(&mg, p, n, true, 0);
    publish();
}

/** @brief The glass reports @p n points, already in panel pixels. */
static void glass(const touch_point_t *pts, int n)
{
    touch_merge_glass(&mg, pts, n);
    publish();
}

/** @brief The event for track id @p id, or NULL. */
static const touch_event_t *event_of(uint8_t id)
{
    for (int i = 0; i < nev; ++i) {
        if (ev[i].point.id == id) {
            return &ev[i];
        }
    }
    return NULL;
}

/* ----------------------------------------------------------- the ids */

/* A drag on injected contact 0, a tap on injected contact 1: two track
 * ids, the tap's UP a tap, the drag's moves untouched by it. */
TEST_CASE(two_injected_contacts_are_two_tracks)
{
    fresh();
    touch_point_t p[2] = { pt(0, 100, 300) };
    inject(p, 1);
    CHECK_EQ(nev, 1);
    CHECK_EQ(ev[0].type, TOUCH_EVENT_DOWN);
    CHECK_EQ(ev[0].point.id, 0x80);

    p[0] = pt(0, 140, 300);
    p[1] = pt(1, 900, 500);
    inject(p, 2);
    CHECK_EQ(nev, 2);
    CHECK_EQ(event_of(0x80)->type, TOUCH_EVENT_MOVE);
    CHECK_EQ(event_of(0x81)->type, TOUCH_EVENT_DOWN);
    CHECK_EQ(event_of(0x81)->point.x, 900);

    p[0] = pt(0, 180, 300);
    inject(p, 1);
    CHECK_EQ(nev, 2);
    CHECK_EQ(event_of(0x81)->type, TOUCH_EVENT_UP);
    CHECK(touch_event_is_tap_up(event_of(0x81)));
    CHECK_EQ(event_of(0x80)->type, TOUCH_EVENT_MOVE);
    CHECK_EQ(event_of(0x80)->point.x, 180);
}

/* A finger and an injected contact on one pixel: two contacts, two ids. */
TEST_CASE(a_finger_and_an_injected_contact_on_one_pixel_are_two)
{
    fresh();
    const touch_point_t g[1] = { pt(0, 500, 300) };
    const touch_point_t i[1] = { pt(0, 500, 300) };
    touch_merge_glass(&mg, g, 1);
    inject(i, 1);
    CHECK_EQ(nev, 2);
    CHECK_EQ(event_of(0x00)->type, TOUCH_EVENT_DOWN);
    CHECK_EQ(event_of(0x80)->type, TOUCH_EVENT_DOWN);
}

/* Either source lifts first; each UP is its own, and a tap. */
TEST_CASE(the_two_sources_lift_in_either_order)
{
    for (int order = 0; order < 2; ++order) {
        fresh();
        const touch_point_t g[1] = { pt(3, 200, 200) };
        const touch_point_t i[1] = { pt(3, 800, 400) };
        glass(g, 1);
        inject(i, 1);
        if (order == 0) {
            glass(NULL, 0);
            CHECK_EQ(nev, 1);
            CHECK_EQ(ev[0].point.id, 3);
            inject(NULL, 0);
            CHECK_EQ(nev, 1);
            CHECK_EQ(ev[0].point.id, 0x83);
        } else {
            inject(NULL, 0);
            CHECK_EQ(nev, 1);
            CHECK_EQ(ev[0].point.id, 0x83);
            glass(NULL, 0);
            CHECK_EQ(nev, 1);
            CHECK_EQ(ev[0].point.id, 3);
        }
        CHECK(touch_event_is_tap_up(&ev[0]));
    }
}

/* Five contacts that all jump past TOUCH_JUMP_PX in one report make ten
 * events, and the touch task's buffer of TOUCH_MAX_POINTS * 2 holds all. */
TEST_CASE(five_injected_jumps_fit_the_event_buffer)
{
    fresh();
    touch_point_t p[TOUCH_MAX_POINTS];
    for (int k = 0; k < TOUCH_MAX_POINTS; ++k) {
        p[k] = pt((uint8_t)k, 100 + 150 * k, 100);
    }
    inject(p, TOUCH_MAX_POINTS);
    CHECK_EQ(nev, 5);
    for (int k = 0; k < TOUCH_MAX_POINTS; ++k) {
        p[k].y = 500;
    }
    inject(p, TOUCH_MAX_POINTS);
    CHECK_EQ(nev, 10);
    int ups = 0, downs = 0;
    for (int k = 0; k < nev; ++k) {
        ups += ev[k].type == TOUCH_EVENT_UP;
        downs += ev[k].type == TOUCH_EVENT_DOWN;
    }
    CHECK_EQ(ups, 5);
    CHECK_EQ(downs, 5);
}

/* ------------------------------------------------- the five slots */

/* Five injected contacts, then a finger: the last injected one is cut, its
 * UP is no tap, and it does not come back when the finger lifts. */
TEST_CASE(a_finger_on_a_full_frame_cuts_an_injected_contact)
{
    fresh();
    touch_point_t p[TOUCH_MAX_POINTS];
    for (int k = 0; k < TOUCH_MAX_POINTS; ++k) {
        p[k] = pt((uint8_t)k, 100 + 150 * k, 300);
    }
    inject(p, TOUCH_MAX_POINTS);
    const touch_point_t g[1] = { pt(0, 512, 100) };
    glass(g, 1);
    CHECK_EQ(nev, 2);
    CHECK_EQ(event_of(0x00)->type, TOUCH_EVENT_DOWN);
    const touch_event_t *cut = event_of(0x84);
    CHECK(cut != NULL);
    CHECK_EQ(cut->type, TOUCH_EVENT_UP);
    CHECK(!touch_event_is_tap_up(cut));

    /* The finger lifts: room again, and still no press from the cut one. */
    glass(NULL, 0);
    CHECK_EQ(nev, 1);
    CHECK_EQ(ev[0].point.id, 0x00);
    inject(p, TOUCH_MAX_POINTS);       /* the injector reports it again */
    CHECK_EQ(nev, 0);

    /* The injector lifts it, then puts it down again: a new press. */
    inject(p, TOUCH_MAX_POINTS - 1);
    CHECK_EQ(nev, 0);
    inject(p, TOUCH_MAX_POINTS);
    CHECK_EQ(nev, 1);
    CHECK_EQ(ev[0].type, TOUCH_EVENT_DOWN);
    CHECK_EQ(ev[0].point.id, 0x84);
}

/* A full frame and a new injected contact: the newcomer is the one cut,
 * never a contact already down, and it reaches no screen at all. */
TEST_CASE(a_new_injected_contact_on_a_full_frame_never_shows)
{
    fresh();
    const touch_point_t g[2] = { pt(0, 10, 100), pt(1, 30, 100) };
    glass(g, 2);
    touch_point_t p[4] = { pt(1, 300, 300), pt(2, 400, 300), pt(3, 500, 300) };
    inject(p, 3);
    CHECK_EQ(nev, 3);
    p[3] = p[2];
    p[2] = p[1];
    p[1] = p[0];
    p[0] = pt(0, 200, 300);            /* the newcomer, first in the report */
    inject(p, 4);
    CHECK_EQ(nev, 0);
    touch_point_t f[TOUCH_MAX_POINTS];
    CHECK_EQ(touch_merge_frame(&mg, f, TOUCH_MAX_POINTS), 5);
    for (int k = 0; k < 5; ++k) {
        CHECK(f[k].id != 0x80);
    }
    glass(g, 1);
    CHECK_EQ(nev, 1);
    CHECK_EQ(ev[0].point.id, 0x01);
}

/* Only cut contacts' UPs are marked: a lift on either source stays a tap. */
TEST_CASE(marks_touch_only_the_cut)
{
    fresh();
    touch_event_t e[4] = {
        { .type = TOUCH_EVENT_UP, .point = pt(0x01, 1, 1) },
        { .type = TOUCH_EVENT_UP, .point = pt(0x82, 1, 1) },
        { .type = TOUCH_EVENT_DOWN, .point = pt(0x82, 1, 1) },
        { .type = TOUCH_EVENT_MOVE, .point = pt(0x83, 1, 1) },
    };
    CHECK_EQ(touch_merge_mark_cut(&mg, e, 4), 0);
    for (int k = 0; k < 4; ++k) {
        CHECK_EQ(e[k].flags, 0);
    }
    CHECK_EQ(touch_merge_mark_cut(NULL, e, 4), 0);
    CHECK_EQ(touch_merge_mark_cut(&mg, NULL, 4), 0);
    CHECK_EQ(touch_merge_mark_cut(&mg, e, 0), 0);
}

/* A detach forgets the cuts with the contacts: back, the injector's first
 * report is its whole set. */
TEST_CASE(a_detach_forgets_the_cuts)
{
    fresh();
    touch_point_t g[TOUCH_MAX_POINTS];
    for (int k = 0; k < TOUCH_MAX_POINTS; ++k) {
        g[k] = pt((uint8_t)k, 10 + 20 * k, 100);
    }
    glass(g, TOUCH_MAX_POINTS);
    const touch_point_t p[1] = { pt(0, 500, 300) };
    inject(p, 1);
    CHECK_EQ(nev, 0);
    CHECK(touch_merge_expire(&mg, TOUCH_MERGE_DETACH_MS));
    CHECK(touch_merge_probe(&mg, true, 2u * TOUCH_MERGE_DETACH_MS));
    glass(NULL, 0);
    inject(p, 1);
    CHECK_EQ(nev, 1);
    CHECK_EQ(ev[0].type, TOUCH_EVENT_DOWN);
}

/* ------------------------------------------------------ the band */

/* An injected point on the band's rows, y 0 to UI_BAND_H - 1, lands on the
 * pixel it names, as every other: the band is the router's, not the map's. */
TEST_CASE(injected_points_on_the_band_stay_panel_pixels)
{
    static const int k_rows[] = { 0, 1, 30, UI_BAND_H - 1, UI_BAND_H };
    for (size_t r = 0; r < sizeof(k_rows) / sizeof(k_rows[0]); ++r) {
        fresh();
        const touch_point_t p[2] = { pt(0, 0, k_rows[r]),
                                     pt(1, 1023, k_rows[r]) };
        inject(p, 2);
        CHECK_EQ(nev, 2);
        CHECK_EQ(event_of(0x80)->point.x, 0);
        CHECK_EQ(event_of(0x80)->point.y, k_rows[r]);
        CHECK_EQ(event_of(0x81)->point.x, 1023);
        CHECK_EQ(event_of(0x81)->point.y, k_rows[r]);
    }
    CHECK_EQ(UI_BAND_H, 60);
}

/* ---------------------------------------------- through the router */

/** @brief The overview on a dark theme, nothing down. */
static void panel_fresh(void)
{
    ui_theme_set(UI_THEME_DARK);
    ui_router_init();
    for (int i = 0; i < SPLASH_STEP_COUNT; ++i) {
        splash_screen_set((splash_step_t)i, SPLASH_OK, "");
    }
    ui_router_tick(2.0f);
    ui_router_goto(SCREEN_OVERVIEW);
    fresh();
    to_router = true;
    (void)ui_router_take_stop();
}

/* The station's STOP check: an injected contact held on the body while a
 * second injected contact taps STOP.  The router latches the stop on the
 * second's press, with the first still down, and not again at its lift. */
TEST_CASE(an_injected_stop_mid_hold_stops)
{
    panel_fresh();
    const gfx_rect_t r = ui_band_stop_rect();
    touch_point_t p[2] = { pt(0, 512, 400) };
    inject(p, 1);
    p[0] = pt(0, 516, 402);
    p[1] = pt(1, r.x + r.w / 2, r.y + r.h / 2);
    inject(p, 2);
    CHECK(ui_router_take_stop());
    inject(p, 1);
    CHECK(!ui_router_take_stop());
    inject(NULL, 0);
    CHECK(!ui_router_take_stop());
}

int main(void)
{
    RUN(two_injected_contacts_are_two_tracks);
    RUN(a_finger_and_an_injected_contact_on_one_pixel_are_two);
    RUN(the_two_sources_lift_in_either_order);
    RUN(five_injected_jumps_fit_the_event_buffer);
    RUN(a_finger_on_a_full_frame_cuts_an_injected_contact);
    RUN(a_new_injected_contact_on_a_full_frame_never_shows);
    RUN(marks_touch_only_the_cut);
    RUN(a_detach_forgets_the_cuts);
    RUN(injected_points_on_the_band_stay_panel_pixels);
    RUN(an_injected_stop_mid_hold_stops);
    return test_summary("touch_merge_multi");
}
