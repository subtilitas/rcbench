/**
 * @file test_help_router.c
 * @brief The screen guide in the router: the band's "?" opens it, its
 * buttons turn the pages, every other press closes it, and STOP, the chips,
 * the knob and a lost touch stream act through it as they act through a
 * chip's bubble.  On the menu, the one screen with guide pages.
 *
 * Cases:
 * - the_chip_shows_where_the_screen_has_pages
 * - the_chips_and_the_guide_chip_never_share_a_point
 * - a_tap_on_the_chip_opens_the_first_page
 * - a_slide_off_the_chip_opens_nothing
 * - next_and_prev_turn_the_pages_and_stop_at_the_ends
 * - a_button_acts_on_a_release_on_itself_only
 * - next_at_one_spot_turns_every_page
 * - the_band_keeps_prev_and_next_while_the_guide_is_open
 * - close_and_the_chip_close_it
 * - a_tap_elsewhere_closes_it_and_reaches_no_screen
 * - a_press_on_the_dock_or_a_badge_does_nothing
 * - stop_works_through_the_guide
 * - a_chip_closes_the_guide_and_the_guide_a_bubble
 * - opening_releases_the_screens_contacts
 * - leaving_the_screen_closes_it
 * - the_bench_state_keeps_it_open_and_its_labels_follow
 * - lost_touch_events_keep_it_open_and_drop_a_held_button
 * - a_knob_turn_moves_nothing_and_keeps_it_open
 * - five_contacts_on_the_dock_reach_no_screen
 * - the_dock_is_drawn_once_a_buffer_and_the_marks_every_frame
 * - a_closed_guide_leaves_no_pixels_behind
 * - marks_that_move_leave_nothing_behind
 *
 * SPDX-License-Identifier: MIT
 */

#include "nav_rig.h"

#include "ui_help.h"

/* A step the rest of a case cannot do without: names it and ends the case. */
#define REQUIRE(cond)                                                         \
    do {                                                                      \
        if (!(cond)) {                                                        \
            T_FAIL("expected %s", #cond);                                     \
            return;                                                           \
        }                                                                     \
    } while (0)

/** @brief The middle of @p r. */
static void mid(gfx_rect_t r, int *x, int *y)
{
    *x = r.x + r.w / 2;
    *y = r.y + r.h / 2;
}

/** @brief A tap on the band's "?". */
static void tap_guide(void)
{
    int x, y;
    mid(ui_band_guide_rect(), &x, &y);
    tap(x, y);
}

/** @brief The page the guide shows now. */
static bool page_now(ui_help_page_t *p)
{
    return ui_help_page(overview_screen(), ui_router_status()->armed,
                        ui_router_guide_page(), p);
}

/** @brief A tap on @p hit of the dock showing now. */
static void tap_button(ui_help_hit_t hit)
{
    ui_help_page_t p;
    REQUIRE(page_now(&p));
    int x, y;
    mid(ui_help_button_rect(p.side, hit), &x, &y);
    tap(x, y);
}

/** @brief The menu, SAFE with the link up, the guide open on page 1. */
static void open_on_menu(void)
{
    fresh();
    to_overview();
    const ui_bench_status_t st = { .link_up = true, .mode = "BENCH" };
    ui_router_set_status(&st);
    tap_guide();
}

/** @brief Whether the pixel at (@p x, @p y) of the framebuffer is @p c. */
static bool px_is(int x, int y, gfx_color_t c)
{
    return fb[(size_t)y * W + x] == c;
}

TEST_CASE(the_chip_shows_where_the_screen_has_pages)
{
    fresh();
    to_overview();
    CHECK(ui_router_guide_shown());
    ui_router_render(&cv, 0);
    const gfx_rect_t g = ui_band_guide_rect();
    /* The chip's sunk fill at its left end, inside the round. */
    CHECK(px_is(g.x + g.w / 2, g.y + 2, ui_theme_color(UI_C_PANEL_SUNK)));

    /* SERVO has no pages yet: no chip, the band's fill in its place. */
    ui_router_goto(SCREEN_SERVO);
    CHECK(!ui_router_guide_shown());
    ui_router_render(&cv, 0);
    CHECK(px_is(g.x + g.w / 2, g.y + 2, ui_theme_color(UI_C_PANEL)));
    /* And a tap there opens nothing. */
    tap_guide();
    CHECK(!ui_router_guide_open());

    /* The splash carries no band. */
    fresh();
    CHECK(!ui_router_guide_shown());
}

/* The chips' hit strips, the "?"'s and STOP's never share a point, with
 * every chip on the band and with the widest row. */
TEST_CASE(the_chips_and_the_guide_chip_never_share_a_point)
{
    const ui_bench_status_t rows[2] = {
        { .link_up = true, .faults = 0x10, .run_seconds = 5,
          .mode = "BENCH" },
        { .link_up = true, .armed = true, .armed_roles = UI_ARMED_MOTOR,
          .faults = 0x7F, .run_seconds = 10u * 3600u, .mode = "BENCH" },
    };
    const gfx_rect_t stop = ui_band_stop_rect();
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        for (int k = 0; k < 2; ++k) {
            for (int x = 0; x < W; ++x) {
                const bool chip =
                    ui_band_chip_at(&rows[k], x, 30) != UI_BAND_CHIP_NONE;
                const bool guide = ui_band_guide_at(x, 30);
                const bool on_stop = gfx_rect_contains(stop, x, 30);
                if ((int)chip + (int)guide + (int)on_stop > 1) {
                    T_FAIL("language %d, row %d: x %d is hit twice", lang, k,
                           x);
                }
            }
        }
    }
    ui_text_set_language(UI_LANG_EN);
    /* "?" is 40 x 44px, 8px left of STOP. */
    const gfx_rect_t g = ui_band_guide_rect();
    CHECK_EQ(g.x, 802);
    CHECK_EQ(g.w, 40);
    CHECK_EQ(g.x + g.w + 8, stop.x);
}

TEST_CASE(a_tap_on_the_chip_opens_the_first_page)
{
    fresh();
    to_overview();
    const uint32_t handed = ui_router_dispatched();
    CHECK(!ui_router_guide_open());
    CHECK_EQ(ui_router_guide_page(), -1);
    tap_guide();
    CHECK(ui_router_guide_open());
    CHECK_EQ(ui_router_guide_page(), 0);
    CHECK_EQ(ui_router_dispatched(), handed);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    /* Drawn open: "?" on the accent. */
    ui_router_render(&cv, 0);
    const gfx_rect_t g = ui_band_guide_rect();
    CHECK(px_is(g.x + g.w / 2, g.y + 2, ui_theme_color(UI_C_ACCENT)));
}

TEST_CASE(a_slide_off_the_chip_opens_nothing)
{
    fresh();
    to_overview();
    int x, y;
    mid(ui_band_guide_rect(), &x, &y);
    touch(x, y, TOUCH_EVENT_DOWN, 1);
    touch(x - 200, y, TOUCH_EVENT_MOVE, 1);
    touch(x - 200, y, TOUCH_EVENT_UP, 1);
    CHECK(!ui_router_guide_open());
    /* A release the tracker made is no tap either. */
    touch(x, y, TOUCH_EVENT_DOWN, 1);
    touch_event_t up = { .type = TOUCH_EVENT_UP, .flags = TOUCH_FLAG_NO_TAP,
                         .point = { .id = 1, .x = (int16_t)x,
                                    .y = (int16_t)y } };
    ui_router_event(&up);
    CHECK(!ui_router_guide_open());
}

TEST_CASE(next_and_prev_turn_the_pages_and_stop_at_the_ends)
{
    open_on_menu();
    const int count = ui_help_count(overview_screen(), false);
    CHECK_EQ(count, 3);
    for (int i = 1; i < count; ++i) {
        tap_button(UI_HELP_HIT_NEXT);
        CHECK_EQ(ui_router_guide_page(), i);
    }
    tap_button(UI_HELP_HIT_NEXT);        /* the last page: dimmed, nothing */
    CHECK_EQ(ui_router_guide_page(), count - 1);
    CHECK(ui_router_guide_open());
    for (int i = count - 2; i >= 0; --i) {
        tap_button(UI_HELP_HIT_PREV);
        CHECK_EQ(ui_router_guide_page(), i);
    }
    tap_button(UI_HELP_HIT_PREV);        /* the first page: nothing */
    CHECK_EQ(ui_router_guide_page(), 0);
    CHECK(ui_router_guide_open());
}

TEST_CASE(a_button_acts_on_a_release_on_itself_only)
{
    open_on_menu();
    ui_help_page_t p;
    REQUIRE(page_now(&p));
    int nx, ny, cx, cy;
    mid(ui_help_button_rect(p.side, UI_HELP_HIT_NEXT), &nx, &ny);
    mid(ui_help_button_rect(p.side, UI_HELP_HIT_CLOSE), &cx, &cy);
    /* Down on NEXT, up on CLOSE: neither. */
    touch(nx, ny, TOUCH_EVENT_DOWN, 1);
    touch(cx, cy, TOUCH_EVENT_MOVE, 1);
    touch(cx, cy, TOUCH_EVENT_UP, 1);
    CHECK(ui_router_guide_open());
    CHECK_EQ(ui_router_guide_page(), 0);
    /* Down on NEXT, a wander, back and up on NEXT: one page. */
    touch(nx, ny, TOUCH_EVENT_DOWN, 1);
    touch(nx + 30, ny, TOUCH_EVENT_MOVE, 1);
    touch(nx, ny, TOUCH_EVENT_MOVE, 1);
    touch(nx, ny, TOUCH_EVENT_UP, 1);
    CHECK_EQ(ui_router_guide_page(), 1);
    /* Page 2 has its dock on the other side. */
    REQUIRE(page_now(&p));
    mid(ui_help_button_rect(p.side, UI_HELP_HIT_NEXT), &nx, &ny);
    /* A release the tracker made acts on nothing. */
    touch(nx, ny, TOUCH_EVENT_DOWN, 1);
    touch_event_t up = { .type = TOUCH_EVENT_UP, .flags = TOUCH_FLAG_NO_TAP,
                         .point = { .id = 1, .x = (int16_t)nx,
                                    .y = (int16_t)ny } };
    ui_router_event(&up);
    CHECK_EQ(ui_router_guide_page(), 1);
    /* A second finger's release is not the first one's. */
    touch(nx, ny, TOUCH_EVENT_DOWN, 1);
    touch(nx, ny, TOUCH_EVENT_UP, 2);
    CHECK_EQ(ui_router_guide_page(), 1);
    touch(nx, ny, TOUCH_EVENT_UP, 1);
    CHECK_EQ(ui_router_guide_page(), 2);
}

/*
 * Two taps at the spot of page 1's NEXT turn two pages: NEXT does not move
 * when the dock changes sides, and no tile lies under it to close the
 * guide.  Back the same way with PREV.
 */
TEST_CASE(next_at_one_spot_turns_every_page)
{
    open_on_menu();
    ui_help_page_t p;
    REQUIRE(page_now(&p));
    int nx, ny, px, py;
    mid(ui_help_button_rect(p.side, UI_HELP_HIT_NEXT), &nx, &ny);
    mid(ui_help_button_rect(p.side, UI_HELP_HIT_PREV), &px, &py);
    const int count = ui_help_count(overview_screen(), false);
    REQUIRE(count == 3);
    for (int n = 1; n < count; ++n) {
        tap(nx, ny);
        CHECK(ui_router_guide_open());
        CHECK_EQ(ui_router_guide_page(), n);
    }
    tap(nx, ny);                         /* the last page: nothing */
    CHECK(ui_router_guide_open());
    CHECK_EQ(ui_router_guide_page(), count - 1);
    for (int n = count - 2; n >= 0; --n) {
        tap(px, py);
        CHECK(ui_router_guide_open());
        CHECK_EQ(ui_router_guide_page(), n);
    }
    /* No screen saw a tap: the menu is still on top. */
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
}

/*
 * PREV and NEXT stay drawn in the band when the band paints again without
 * the dock -- the run clock moving, a fault coming up -- and the tag comes
 * back when the guide closes.
 */
TEST_CASE(the_band_keeps_prev_and_next_while_the_guide_is_open)
{
    static gfx_color_t alone[(size_t)W * H];
    open_on_menu();
    for (int f = 0; f < 2; ++f) {
        ui_router_tick(0.05f);
        ui_router_render(&cv, f & 1);
    }
    /* A band change alone: the dock stays as it was drawn. */
    ui_bench_status_t st = { .link_up = true, .mode = "BENCH",
                             .run_seconds = 61u, .faults = 0x10u };
    ui_router_set_status(&st);
    REQUIRE(ui_router_guide_open());
    for (int f = 0; f < 2; ++f) {
        ui_router_tick(0.05f);
        ui_router_render(&cv, f & 1);
    }
    gfx_canvas_t c2;
    gfx_canvas_init(&c2, alone, W, H, W);
    /* On the band's fill, which the buttons' round corners show. */
    gfx_clear(&c2, ui_theme_color(UI_C_PANEL));
    ui_help_render_nav(&c2, 0, 3, UI_HELP_HIT_NONE);
    const gfx_rect_t pv = ui_help_button_rect(UI_HELP_DOCK_LEFT,
                                              UI_HELP_HIT_PREV);
    const gfx_rect_t nx = ui_help_button_rect(UI_HELP_DOCK_LEFT,
                                              UI_HELP_HIT_NEXT);
    int diff = 0;
    for (int y = pv.y; y < pv.y + pv.h; ++y) {
        for (int x = pv.x; x < nx.x + nx.w; ++x) {
            if (x >= pv.x + pv.w && x < nx.x) {
                continue;
            }
            diff += fb[(size_t)y * W + x] != alone[(size_t)y * W + x];
        }
    }
    CHECK_EQ(diff, 0);
    /* Closed: the band draws its tag where the buttons were. */
    tap_button(UI_HELP_HIT_CLOSE);
    REQUIRE(!ui_router_guide_open());
    ui_router_tick(0.05f);
    ui_router_render(&cv, 0);
    diff = 0;
    for (int y = pv.y; y < pv.y + pv.h; ++y) {
        for (int x = pv.x; x < nx.x + nx.w; ++x) {
            diff += fb[(size_t)y * W + x] != alone[(size_t)y * W + x];
        }
    }
    CHECK(diff > 0);
}

TEST_CASE(close_and_the_chip_close_it)
{
    open_on_menu();
    tap_button(UI_HELP_HIT_CLOSE);
    CHECK(!ui_router_guide_open());
    tap_guide();
    CHECK(ui_router_guide_open());
    tap_button(UI_HELP_HIT_NEXT);
    /* "?" closes at the press, and its release opens nothing. */
    int x, y;
    mid(ui_band_guide_rect(), &x, &y);
    touch(x, y, TOUCH_EVENT_DOWN, 1);
    CHECK(!ui_router_guide_open());
    touch(x, y, TOUCH_EVENT_UP, 1);
    CHECK(!ui_router_guide_open());
    /* Opened again, it starts at page 1. */
    tap_guide();
    CHECK_EQ(ui_router_guide_page(), 0);
}

TEST_CASE(a_tap_elsewhere_closes_it_and_reaches_no_screen)
{
    open_on_menu();
    ui_help_page_t p;
    REQUIRE(page_now(&p));
    const uint32_t handed = ui_router_dispatched();
    /* MOTOR & ESC's tile, in the live half with the dock on the right. */
    const gfx_rect_t tile = overview_screen_tile_rect(0);
    const int x = tile.x + tile.w / 2;
    const int y = UI_BAND_H + tile.y + tile.h / 2;
    REQUIRE(gfx_rect_contains(ui_help_live_rect(p.side), x, y));
    touch(x, y, TOUCH_EVENT_DOWN, 1);
    CHECK(!ui_router_guide_open());
    touch(x, y, TOUCH_EVENT_UP, 1);
    CHECK_EQ(ui_router_dispatched(), handed);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    /* The next tap is the tile's. */
    tap(x, y);
    CHECK_EQ(ui_router_current(), SCREEN_MOTOR);

    /* An alert's band: the press closes the guide and leaves the alert. */
    open_on_menu();
    ui_router_set_alert("TEST ALERT");
    tap(W / 2, UI_ALERT_Y + UI_ALERT_H / 2);
    CHECK(!ui_router_guide_open());
    CHECK(ui_router_alert() != NULL);
    /* A plain part of the band. */
    open_on_menu();
    tap(400, 2);
    CHECK(!ui_router_guide_open());
}

TEST_CASE(a_press_on_the_dock_or_a_badge_does_nothing)
{
    open_on_menu();
    ui_help_page_t p;
    REQUIRE(page_now(&p));
    const uint32_t handed = ui_router_dispatched();
    const gfx_rect_t d = ui_help_dock_rect(p.side);
    tap(d.x + d.w / 2, d.y + 100);
    CHECK(ui_router_guide_open());
    CHECK_EQ(ui_router_guide_page(), 0);
    int x, y;
    mid(ui_help_badge_rect(&p, 0), &x, &y);
    tap(x, y);
    CHECK(ui_router_guide_open());
    CHECK_EQ(ui_router_dispatched(), handed);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
}

TEST_CASE(stop_works_through_the_guide)
{
    for (int armed = 0; armed < 2; ++armed) {
        for (int last = 0; last < 2; ++last) {
            for (int alert = 0; alert < 2; ++alert) {
                open_on_menu();
                const ui_bench_status_t st = {
                    .link_up = true, .mode = "BENCH", .armed = armed != 0,
                    .armed_roles = armed ? UI_ARMED_MOTOR : 0u };
                ui_router_set_status(&st);
                REQUIRE(ui_router_guide_open());
                if (last) {
                    tap_button(UI_HELP_HIT_NEXT);
                    tap_button(UI_HELP_HIT_NEXT);
                    REQUIRE(ui_router_guide_page() == 2);
                }
                if (alert) {
                    ui_router_set_alert("TEST ALERT");
                }
                (void)ui_router_take_stop();
                int x, y;
                mid(ui_band_stop_rect(), &x, &y);
                touch(x, y, TOUCH_EVENT_DOWN, 3);
                CHECK(ui_router_take_stop());
                CHECK(!ui_router_take_stop());
                CHECK(!ui_router_guide_open());
                touch(x, y, TOUCH_EVENT_UP, 3);
                CHECK(!ui_router_take_stop());
            }
        }
    }
}

TEST_CASE(a_chip_closes_the_guide_and_the_guide_a_bubble)
{
    open_on_menu();
    gfx_rect_t r;
    REQUIRE(ui_band_chip_rect(ui_router_status(), UI_BAND_CHIP_ARMED, &r));
    tap(r.x + r.w / 2, r.y + r.h / 2);
    CHECK(!ui_router_guide_open());
    CHECK_EQ(ui_router_help(), UI_BAND_CHIP_ARMED);
    tap_guide();
    CHECK_EQ(ui_router_help(), UI_BAND_CHIP_NONE);
    CHECK(ui_router_guide_open());
}

TEST_CASE(opening_releases_the_screens_contacts)
{
    fresh();
    to_overview();
    const gfx_rect_t tile = overview_screen_tile_rect(1);
    const int x = tile.x + tile.w / 2;
    const int y = UI_BAND_H + tile.y + tile.h / 2;
    touch(x, y, TOUCH_EVENT_DOWN, 2);      /* SERVO's tile held */
    tap_guide();
    REQUIRE(ui_router_guide_open());
    touch(x, y, TOUCH_EVENT_UP, 2);        /* its release opens nothing */
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    CHECK(ui_router_guide_open());
}

TEST_CASE(leaving_the_screen_closes_it)
{
    open_on_menu();
    ui_router_goto(SCREEN_SERVO);
    CHECK(!ui_router_guide_open());
    ui_router_goto(SCREEN_OVERVIEW);
    CHECK(!ui_router_guide_open());
    CHECK(ui_router_guide_shown());
}

/** @brief A hash of the dock's half of the framebuffer. */
static uint32_t dock_hash(ui_help_side_t side)
{
    const gfx_rect_t d = ui_help_dock_rect(side);
    uint32_t h = 2166136261u;
    for (int y = d.y; y < d.y + d.h; ++y) {
        for (int x = d.x; x < d.x + d.w; ++x) {
            h = (h ^ (uint32_t)fb[(size_t)y * W + x]) * 16777619u;
        }
    }
    return h;
}

/* ARMED, a link lost and back: the menu's pages all show while ARMED, so
 * the guide stays open, and the arm chip's label on page 3 follows. */
TEST_CASE(the_bench_state_keeps_it_open_and_its_labels_follow)
{
    open_on_menu();
    tap_button(UI_HELP_HIT_NEXT);
    tap_button(UI_HELP_HIT_NEXT);
    REQUIRE(ui_router_guide_page() == 2);
    ui_router_render(&cv, 0);
    const uint32_t safe = dock_hash(UI_HELP_DOCK_LEFT);
    ui_help_page_t p;
    REQUIRE(page_now(&p));
    CHECK_STR_EQ(p.item[1].label, "SAFE");

    const ui_bench_status_t armed = { .link_up = true, .mode = "BENCH",
                                      .armed = true,
                                      .armed_roles = UI_ARMED_MOTOR };
    ui_router_set_status(&armed);
    CHECK(ui_router_guide_open());
    CHECK_EQ(ui_router_guide_page(), 2);
    REQUIRE(page_now(&p));
    CHECK_STR_EQ(p.item[1].label, "ARMED MOTOR");
    ui_router_render(&cv, 0);
    CHECK(dock_hash(UI_HELP_DOCK_LEFT) != safe);

    const ui_bench_status_t lost = { .link_up = false, .mode = "SIM" };
    ui_router_set_status(&lost);
    CHECK(ui_router_guide_open());
}

TEST_CASE(lost_touch_events_keep_it_open_and_drop_a_held_button)
{
    open_on_menu();
    ui_help_page_t p;
    REQUIRE(page_now(&p));
    int x, y;
    mid(ui_help_button_rect(p.side, UI_HELP_HIT_NEXT), &x, &y);
    touch(x, y, TOUCH_EVENT_DOWN, 1);
    ui_router_cancel_gestures();
    CHECK(ui_router_guide_open());
    touch(x, y, TOUCH_EVENT_UP, 1);        /* the press is gone */
    CHECK_EQ(ui_router_guide_page(), 0);
    tap(x, y);
    CHECK_EQ(ui_router_guide_page(), 1);
}

TEST_CASE(a_knob_turn_moves_nothing_and_keeps_it_open)
{
    open_on_menu();
    CHECK(!ui_router_take_knob());
    CHECK(!ui_router_take_knob());
    CHECK(ui_router_guide_open());
    tap_button(UI_HELP_HIT_CLOSE);
    CHECK(ui_router_take_knob());
}

TEST_CASE(five_contacts_on_the_dock_reach_no_screen)
{
    open_on_menu();
    ui_help_page_t p;
    REQUIRE(page_now(&p));
    const gfx_rect_t d = ui_help_dock_rect(p.side);
    const uint32_t handed = ui_router_dispatched();
    for (uint8_t id = 0; id < TOUCH_MAX_POINTS; ++id) {
        touch(d.x + 40 + 60 * id, d.y + 120, TOUCH_EVENT_DOWN, id);
    }
    for (uint8_t id = 0; id < TOUCH_MAX_POINTS; ++id) {
        touch(d.x + 40 + 60 * id, d.y + 140, TOUCH_EVENT_MOVE, id);
        touch(d.x + 40 + 60 * id, d.y + 140, TOUCH_EVENT_UP, id);
    }
    CHECK(ui_router_guide_open());
    CHECK_EQ(ui_router_dispatched(), handed);
}

TEST_CASE(the_dock_is_drawn_once_a_buffer_and_the_marks_every_frame)
{
    open_on_menu();
    ui_help_page_t p;
    REQUIRE(page_now(&p));
    ui_router_render(&cv, 0);
    const gfx_rect_t d = ui_help_dock_rect(p.side);
    const gfx_rect_t b = ui_help_badge_rect(&p, 0);
    const size_t in_dock = (size_t)(d.y + 200) * W + d.x + 200;
    const size_t in_badge = (size_t)(b.y + b.h / 2) * W + b.x + 3;
    const gfx_color_t badge = fb[in_badge];
    CHECK_EQ(badge, ui_theme_color(UI_C_ACCENT));
    fb[in_dock] = 0x1234u;
    fb[in_badge] = 0x1234u;
    ui_router_render(&cv, 0);
    CHECK_EQ(fb[in_dock], 0x1234u);       /* the dock is not drawn again */
    CHECK_EQ(fb[in_badge], badge);        /* the badge is */
    /* A page change draws the dock again. */
    tap_button(UI_HELP_HIT_NEXT);
    ui_router_render(&cv, 0);
    CHECK(fb[in_dock] != 0x1234u);
}

TEST_CASE(a_closed_guide_leaves_no_pixels_behind)
{
    fresh();
    to_overview();
    const ui_bench_status_t st = { .link_up = true, .mode = "BENCH" };
    ui_router_set_status(&st);
    ui_router_render(&cv, 0);
    uint32_t want = 2166136261u;
    for (size_t i = 0; i < (size_t)W * H; ++i) {
        want = (want ^ (uint32_t)fb[i]) * 16777619u;
    }
    for (int page = 0; page < 3; ++page) {
        tap_guide();
        for (int k = 0; k < page; ++k) {
            tap_button(UI_HELP_HIT_NEXT);
        }
        ui_router_render(&cv, 0);
        tap_button(UI_HELP_HIT_CLOSE);
        ui_router_render(&cv, 0);
        uint32_t got = 2166136261u;
        for (size_t i = 0; i < (size_t)W * H; ++i) {
            got = (got ^ (uint32_t)fb[i]) * 16777619u;
        }
        if (got != want) {
            T_FAIL("page %d left pixels behind", page + 1);
        }
    }
}

/** @brief A hash of the whole framebuffer. */
static uint32_t frame_hash(void)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < (size_t)W * H; ++i) {
        h = (h ^ (uint32_t)fb[i]) * 16777619u;
    }
    return h;
}

/* Page 3 marks the arm chip, which moves when the bench is ARMED: the
 * frame after the change equals one drawn from scratch, so the badge's old
 * place is gone. */
TEST_CASE(marks_that_move_leave_nothing_behind)
{
    open_on_menu();
    tap_button(UI_HELP_HIT_NEXT);
    tap_button(UI_HELP_HIT_NEXT);
    REQUIRE(ui_router_guide_page() == 2);
    ui_help_page_t safe;
    REQUIRE(page_now(&safe));
    ui_router_render(&cv, 0);
    ui_router_render(&cv, 0);
    const ui_bench_status_t armed = { .link_up = true, .mode = "BENCH",
                                      .armed = true, .run_seconds = 5,
                                      .armed_roles = UI_ARMED_MOTOR };
    ui_router_set_status(&armed);
    ui_help_page_t now;
    REQUIRE(page_now(&now));
    CHECK(ui_help_marks_key(&now) != ui_help_marks_key(&safe));
    ui_router_render(&cv, 0);
    const uint32_t got = frame_hash();
    ui_router_invalidate();
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    ui_router_render(&cv, 0);
    CHECK_EQ(got, frame_hash());
}

int main(void)
{
    RUN(the_chip_shows_where_the_screen_has_pages);
    RUN(the_chips_and_the_guide_chip_never_share_a_point);
    RUN(a_tap_on_the_chip_opens_the_first_page);
    RUN(a_slide_off_the_chip_opens_nothing);
    RUN(next_and_prev_turn_the_pages_and_stop_at_the_ends);
    RUN(a_button_acts_on_a_release_on_itself_only);
    RUN(next_at_one_spot_turns_every_page);
    RUN(the_band_keeps_prev_and_next_while_the_guide_is_open);
    RUN(close_and_the_chip_close_it);
    RUN(a_tap_elsewhere_closes_it_and_reaches_no_screen);
    RUN(a_press_on_the_dock_or_a_badge_does_nothing);
    RUN(stop_works_through_the_guide);
    RUN(a_chip_closes_the_guide_and_the_guide_a_bubble);
    RUN(opening_releases_the_screens_contacts);
    RUN(leaving_the_screen_closes_it);
    RUN(the_bench_state_keeps_it_open_and_its_labels_follow);
    RUN(lost_touch_events_keep_it_open_and_drop_a_held_button);
    RUN(a_knob_turn_moves_nothing_and_keeps_it_open);
    RUN(five_contacts_on_the_dock_reach_no_screen);
    RUN(the_dock_is_drawn_once_a_buffer_and_the_marks_every_frame);
    RUN(a_closed_guide_leaves_no_pixels_behind);
    RUN(marks_that_move_leave_nothing_behind);
    return test_summary("help_router");
}
