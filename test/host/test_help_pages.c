/**
 * @file test_help_pages.c
 * @brief The screen guide's pages and dock: every page of every screen
 * fits its dock and marks controls the dock leaves in view, in both
 * languages and themes, SAFE and ARMED; ARMED offers only the pages marked
 * for it; the screen draws nothing under the dock.
 *
 * Cases:
 * - the_dock_and_the_live_half_split_the_body
 * - the_hit_test_finds_buttons_dock_and_badges
 * - armed_offers_only_armed_pages_and_says_so
 * - the_page_set_key_follows_the_titles
 * - every_page_of_every_screen_fits: also PREV and NEXT in one place on
 *   every page, over no marked control, chip, "?" or STOP
 * - the_screen_draws_nothing_under_the_dock
 * - an_umlaut_label_moves_the_text_by_its_cells: the text after a German
 *   label with an umlaut starts where it starts after an ASCII label of
 *   the same cells
 * - a_badge_moves_off_a_string_the_screen_drew: a string under a badge's
 *   first place moves it to the next free one, read through a sub-canvas
 *   as the screen draws; another page starts afresh
 * - a_badge_shows_on_a_fill_of_its_own_colour: a dark ring parts the
 *   badge from a lit accent button
 * - a_guide_opened_again_notes_the_strings_afresh
 * - a_badge_with_no_place_left_stays_in_the_live_half
 * - a_badge_on_its_controls_edge_covers_no_neighbour
 *
 * SPDX-License-Identifier: MIT
 */

#include "nav_rig.h"

#include "battery_screen.h"
#include "ui_help.h"

#include "help_check.h"

/* A step the rest of a case cannot do without: names it and ends the case. */
#define REQUIRE(cond)                                                         \
    do {                                                                      \
        if (!(cond)) {                                                        \
            T_FAIL("expected %s", #cond);                                     \
            return;                                                           \
        }                                                                     \
    } while (0)

/* ------------------------------------------------- a screen of the test */

/* Three pages: the first and the last for ARMED as well, the middle one
 * SAFE only; titles the test can change. */
static ui_text_id_t s_fake_title[3] = { TX_HG_OV_TITLE_LEFT,
                                        TX_HG_OV_TITLE_RIGHT,
                                        TX_HG_OV_TITLE_BAND };

static int fake_count(void) { return 3; }

static bool fake_page(int i, ui_help_page_t *out)
{
    if (i < 0 || i >= 3) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->title = s_fake_title[i];
    out->side = UI_HELP_DOCK_RIGHT;
    out->armed_ok = i != 1;
    out->count = (uint8_t)(i + 1);
    for (int k = 0; k < out->count; ++k) {
        out->item[k] = (ui_help_item_t){ "SERVO", TX_HG_OV_SERVO,
                                         gfx_rect_make(20, 20 + 100 * k, 200,
                                                       80),
                                         UI_HELP_AT_LEFT };
    }
    return true;
}

static const ui_screen_t k_fake = {
    .title = "FAKE", .help_count = fake_count, .help_page = fake_page,
};

/* ----------------------------------------------------------------- cases */

TEST_CASE(the_dock_and_the_live_half_split_the_body)
{
    for (int side = 0; side < 2; ++side) {
        const gfx_rect_t d = ui_help_dock_rect((ui_help_side_t)side);
        const gfx_rect_t l = ui_help_live_rect((ui_help_side_t)side);
        CHECK_EQ(d.w, 480);
        CHECK_EQ(d.h, 488);
        CHECK_EQ(d.y, 64);
        CHECK_EQ(d.y + d.h, 552);
        CHECK(d.y + d.h <= UI_ALERT_Y - 4);
        CHECK_EQ(l.y, UI_BAND_H);
        CHECK_EQ(l.h, UI_BODY_H);
        CHECK_EQ(l.w, 528);
        /* 8px from the panel's edge and from the live half. */
        if (side == UI_HELP_DOCK_LEFT) {
            CHECK_EQ(d.x, 8);
            CHECK_EQ(l.x, d.x + d.w + 8);
            CHECK_EQ(l.x + l.w, W);
        } else {
            CHECK_EQ(d.x + d.w, W - 8);
            CHECK_EQ(l.x, 0);
            CHECK_EQ(l.x + l.w + 8, d.x);
        }
        /* CLOSE: 140 x 48px, centered at the dock's bottom, above the
         * alert band, a row and more under the dock's text. */
        const gfx_rect_t b = ui_help_button_rect((ui_help_side_t)side,
                                                 UI_HELP_HIT_CLOSE);
        CHECK_EQ(b.w, 140);
        CHECK(b.h >= UI_TAP_MIN);
        CHECK(b.y + b.h < UI_ALERT_Y);
        CHECK(b.x >= d.x && b.x + b.w <= d.x + d.w);
        CHECK(b.y >= d.y && b.y + b.h <= d.y + d.h);
        CHECK_EQ(b.x - d.x, d.x + d.w - (b.x + b.w));
        CHECK(ui_help_text_limit() <= b.y - 21);
        /* PREV and NEXT: 124 x 48px in the band, 8px apart, from the home
         * tag's x 8 to x 264, the same for either side. */
        const gfx_rect_t pv = ui_help_button_rect((ui_help_side_t)side,
                                                  UI_HELP_HIT_PREV);
        const gfx_rect_t nx = ui_help_button_rect((ui_help_side_t)side,
                                                  UI_HELP_HIT_NEXT);
        CHECK_EQ(pv.x, 8);
        CHECK_EQ(pv.y, 6);
        CHECK_EQ(pv.w, 124);
        CHECK(pv.h >= UI_TAP_MIN);
        CHECK_EQ(pv.y + pv.h, UI_BAND_H - 6);
        CHECK_EQ(nx.x - (pv.x + pv.w), 8);
        CHECK_EQ(nx.y, pv.y);
        CHECK_EQ(nx.w, pv.w);
        CHECK_EQ(nx.h, pv.h);
        CHECK_EQ(nx.x + nx.w, 264);
    }
    CHECK_EQ(ui_help_button_rect(UI_HELP_DOCK_LEFT, UI_HELP_HIT_DOCK).w, 0);
}

TEST_CASE(the_hit_test_finds_buttons_dock_and_badges)
{
    ui_help_page_t p;
    REQUIRE(fake_page(2, &p));
    const gfx_rect_t d = ui_help_dock_rect(p.side);
    for (int h = UI_HELP_HIT_PREV; h <= UI_HELP_HIT_CLOSE; ++h) {
        const gfx_rect_t b = ui_help_button_rect(p.side, (ui_help_hit_t)h);
        CHECK_EQ(ui_help_hit(&p, b.x, b.y), (ui_help_hit_t)h);
        CHECK_EQ(ui_help_hit(&p, b.x + b.w - 1, b.y + b.h - 1),
                 (ui_help_hit_t)h);
    }
    CHECK_EQ(ui_help_hit(&p, d.x, d.y), UI_HELP_HIT_DOCK);
    CHECK_EQ(ui_help_hit(&p, d.x - 1, d.y), UI_HELP_HIT_NONE);
    const gfx_rect_t b = ui_help_badge_rect(&p, 1);
    CHECK_EQ(ui_help_hit(&p, b.x + 2, b.y + 2), UI_HELP_HIT_DOCK);
    /* The badge sits 4px inside its control's corner. */
    CHECK_EQ(b.x, p.item[1].rect.x + 4);
    CHECK_EQ(b.y, UI_BAND_H + p.item[1].rect.y + 4);
    CHECK_EQ(ui_help_hit(&p, 300, 300), UI_HELP_HIT_NONE);
    CHECK_EQ(ui_help_hit(NULL, d.x, d.y), UI_HELP_HIT_NONE);
    /* A control in the band: its badge under the band, at its left end. */
    p.item[0].rect = gfx_rect_make(850, 6 - UI_BAND_H, 168, 48);
    const gfx_rect_t s = ui_help_badge_rect(&p, 0);
    CHECK_EQ(s.x, 850);
    CHECK_EQ(s.y, UI_BAND_H + 4);
}

TEST_CASE(armed_offers_only_armed_pages_and_says_so)
{
    CHECK_EQ(ui_help_count(&k_fake, false), 3);
    CHECK_EQ(ui_help_count(&k_fake, true), 2);
    ui_help_page_t p;
    REQUIRE(ui_help_page(&k_fake, true, 1, &p));
    CHECK_EQ(p.title, TX_HG_OV_TITLE_BAND);
    CHECK(!ui_help_page(&k_fake, true, 2, &p));
    CHECK(!ui_help_page(&k_fake, false, 3, &p));
    CHECK(!ui_help_page(&k_fake, false, -1, &p));
    CHECK(ui_help_armed_note(&k_fake, true));
    CHECK(!ui_help_armed_note(&k_fake, false));
    /* The menu's pages all show while ARMED: no note. */
    CHECK(!ui_help_armed_note(overview_screen(), true));
    CHECK_EQ(ui_help_count(overview_screen(), true), 4);
    /* A screen without a guide. */
    CHECK_EQ(ui_help_count(battery_screen(), false), 0);
    CHECK_EQ(ui_help_count(NULL, false), 0);

    /* The note takes its rows above the items and is drawn in the warning
     * colour. */
    REQUIRE(ui_help_page(&k_fake, true, 0, &p));
    CHECK(ui_help_text_bottom(&p, true) > ui_help_text_bottom(&p, false));
    fresh();
    ui_help_render_dock(&cv, &p, 0, 2, true, UI_HELP_HIT_NONE);
    const gfx_rect_t d = ui_help_dock_rect(p.side);
    int warn = 0;
    for (int y = d.y + 36; y < d.y + 72; ++y) {
        for (int x = d.x; x < d.x + d.w; ++x) {
            warn += fb[(size_t)y * W + x] == ui_theme_color(UI_C_WARN);
        }
    }
    CHECK(warn > 0);
}

TEST_CASE(the_page_set_key_follows_the_titles)
{
    const uint32_t a = ui_help_set_key(&k_fake, false);
    CHECK_EQ(ui_help_set_key(&k_fake, false), a);
    CHECK(ui_help_set_key(&k_fake, true) != a);
    s_fake_title[2] = TX_HG_OV_TITLE_LEFT;
    CHECK(ui_help_set_key(&k_fake, false) != a);
    s_fake_title[2] = TX_HG_OV_TITLE_BAND;
    CHECK_EQ(ui_help_set_key(&k_fake, false), a);
}

/* Each screen with pages, in each language and theme, SAFE and ARMED, with
 * the link up and every capability, and with the link down. */
TEST_CASE(every_page_of_every_screen_fits)
{
    static const struct {
        ui_screen_id_t id;
        const ui_screen_t *(*screen)(void);
    } k_screens[] = { { SCREEN_OVERVIEW, overview_screen } };
    for (size_t s_ = 0; s_ < sizeof(k_screens) / sizeof(k_screens[0]); ++s_) {
        for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
            for (int th = 0; th < (int)UI_THEME_COUNT; ++th) {
                for (int armed = 0; armed < 2; ++armed) {
                    for (int up = 0; up < 2; ++up) {
                        fresh();
                        ui_theme_set((ui_theme_id_t)th);
                        ui_text_set_language((ui_lang_t)lang);
                        to_overview();
                        ui_bench_status_t st;
                        memset(&st, 0, sizeof(st));
                        st.link_up = up != 0;
                        st.mode = ui_band_mode(up != 0);
                        st.armed = armed != 0;
                        st.armed_roles = armed ? UI_ARMED_MOTOR : 0u;
                        st.faults = up ? 0x7Fu : 0u;
                        st.run_seconds = 10u * 3600u;
                        st.capabilities = up ? 0xFFFFu : 0u;
                        ui_router_set_status(&st);
                        ui_router_goto(k_screens[s_].id);
                        char what[64];
                        snprintf(what, sizeof(what),
                                 "screen %d lang %d theme %d armed %d up %d",
                                 (int)k_screens[s_].id, lang, th, armed,
                                 up);
                        check_pages(k_screens[s_].screen(), what);
                    }
                }
            }
        }
    }
    ui_text_set_language(UI_LANG_EN);
    ui_theme_set(UI_THEME_DARK);
    /* The menu's pages: four tiles, four tiles, the band and the mark,
     * the screenshot's hold. */
    fresh();
    to_overview();
    ui_help_page_t p;
    CHECK(ui_help_page(overview_screen(), false, 0, &p) && p.count == 4);
    CHECK(ui_help_page(overview_screen(), false, 1, &p) && p.count == 4);
    CHECK(ui_help_page(overview_screen(), false, 2, &p) && p.count == 4);
    CHECK(ui_help_page(overview_screen(), false, 3, &p) && p.count == 1);
}

/* The dock's half of the panel holds the dock and nothing of the screen:
 * the same pixels as the dock drawn alone, frame after frame, also when
 * the screen repaints its chrome and the dock is not drawn again. */
TEST_CASE(the_screen_draws_nothing_under_the_dock)
{
    static gfx_color_t alone[(size_t)W * H];
    for (int page = 0; page < 3; ++page) {
        fresh();
        to_overview();
        int x = ui_band_guide_rect().x + 20;
        tap(x, 30);
        REQUIRE(ui_router_guide_open());
        ui_help_page_t p;
        for (int k = 0; k < page; ++k) {
            REQUIRE(ui_help_page(overview_screen(), false, k, &p));
            const gfx_rect_t b = ui_help_button_rect(p.side, UI_HELP_HIT_NEXT);
            tap(b.x + 10, b.y + 10);
        }
        REQUIRE(ui_help_page(overview_screen(), false, page, &p));
        for (int f = 0; f < 4; ++f) {
            /* The menu repaints its tiles on its own from frame 2 on, as
             * a change of the PD mini setting makes it. */
            if (f >= 2) {
                overview_invalidate();
            }
            ui_router_tick(0.05f);
            ui_router_render(&cv, f & 1);
        }
        gfx_canvas_t c2;
        gfx_canvas_init(&c2, alone, W, H, W);
        gfx_clear(&c2, 0);
        ui_help_render_dock(&c2, &p, page, 4, false, UI_HELP_HIT_NONE);
        const gfx_rect_t d = ui_help_dock_rect(p.side);
        int diff = 0;
        for (int y = UI_BAND_H; y < UI_ALERT_Y; ++y) {
            for (int xx = d.x - 8; xx < d.x + d.w + 8; ++xx) {
                diff += fb[(size_t)y * W + xx] != alone[(size_t)y * W + xx];
            }
        }
        if (diff != 0) {
            T_FAIL("page %d: %d pixels of the dock's half differ", page + 1,
                   diff);
        }
    }
}

/* A label's text starts two cells after the label's last character: a
 * German label with an umlaut (2 UTF-8 bytes, 1 cell) puts it where an
 * ASCII label of the same 4 cells does, not 1 cell later. */
TEST_CASE(an_umlaut_label_moves_the_text_by_its_cells)
{
    static gfx_color_t plain[(size_t)W * H];
    ui_text_set_language(UI_LANG_DE);
    ui_help_page_t p;
    memset(&p, 0, sizeof(p));
    p.title = TX_HG_OV_TITLE_LEFT;
    p.side = UI_HELP_DOCK_RIGHT;
    p.count = 1;
    p.item[0] = (ui_help_item_t){ "MENU", TX_HG_OV_SERVO,
                                  gfx_rect_make(20, 20, 200, 80),
                                  UI_HELP_AT_LEFT };
    gfx_canvas_t c2;
    gfx_canvas_init(&c2, plain, W, H, W);
    gfx_clear(&c2, 0);
    ui_help_render_dock(&c2, &p, 0, 1, false, UI_HELP_HIT_NONE);
    p.item[0].label = "MEN\xc3\x9c";
    fresh();
    ui_help_render_dock(&cv, &p, 0, 1, false, UI_HELP_HIT_NONE);
    ui_text_set_language(UI_LANG_EN);

    /* The first row of the item, from the text's first cell (badge 28px,
     * 8px gap, 4 label cells and 2 spaces of 10px) to the dock's pad. */
    const gfx_rect_t d = ui_help_dock_rect(p.side);
    const int x0 = d.x + 16 + 36 + 6 * 10;
    const int y0 = d.y + 36 + 8;
    int diff = 0;
    int ink = 0;
    for (int y = y0; y < y0 + 20; ++y) {
        for (int x = x0; x < d.x + d.w - 16; ++x) {
            diff += fb[(size_t)y * W + x] != plain[(size_t)y * W + x];
            ink += plain[(size_t)y * W + x] == ui_theme_color(UI_C_TEXT);
        }
    }
    CHECK(ink > 0);
    if (diff != 0) {
        T_FAIL("%d pixels of the text after MEN\xc3\x9c differ from after MENU",
               diff);
    }
}

TEST_CASE(a_badge_moves_off_a_string_the_screen_drew)
{
    fresh();
    ui_help_page_t p;
    memset(&p, 0, sizeof(p));
    p.side = UI_HELP_DOCK_RIGHT;
    p.count = 2;
    p.item[0] = (ui_help_item_t){ "A", TX_HG_OV_SERVO,
                                  gfx_rect_make(100, 100, 200, 100),
                                  UI_HELP_AT_LEFT };
    p.item[1] = (ui_help_item_t){ "B", TX_HG_OV_SERVO,
                                  gfx_rect_make(100, 300, 200, 100),
                                  UI_HELP_AT_LEFT };
    const gfx_rect_t first = ui_help_badge_rect(&p, 0);
    CHECK_EQ(first.x, 104);
    CHECK_EQ(first.y, UI_BAND_H + 104);
    const uint32_t key = ui_help_marks_key(&p);

    /* The screen draws on the body's sub-canvas, as the router hands it:
     * a label at the control's top-left, under the first place. */
    CHECK(ui_help_watch_begin(&cv, &p));
    gfx_canvas_t body;
    REQUIRE(gfx_canvas_sub(&cv, gfx_rect_make(0, UI_BAND_H, W, UI_BODY_H),
                           &body));
    gfx_text(&body, 112, 110, "LABEL", &gfx_font_10x20, GFX_WHITE, 1);
    /* Item B's label sits right of its first place: it stays. */
    gfx_text(&body, 140, 310, "OTHER", &gfx_font_10x20, GFX_WHITE, 1);
    ui_help_watch_end();
    /* Drawn with no watch: nothing is noted. */
    gfx_text(&body, 104, 304, "LATE", &gfx_font_10x20, GFX_WHITE, 1);

    const gfx_rect_t label = gfx_rect_make(112, UI_BAND_H + 110, 50, 20);
    const gfx_rect_t moved = ui_help_badge_rect(&p, 0);
    CHECK(!near(moved, label, 0));
    /* The next place: inside the top-right corner. */
    CHECK_EQ(moved.x, 100 + 200 - 4 - 28);
    CHECK_EQ(moved.y, UI_BAND_H + 104);
    const gfx_rect_t b = ui_help_badge_rect(&p, 1);
    CHECK_EQ(b.x, 104);
    CHECK_EQ(b.y, UI_BAND_H + 304);
    CHECK(ui_help_marks_key(&p) != key);
    CHECK_EQ(ui_help_hit(&p, moved.x + 2, moved.y + 2), UI_HELP_HIT_DOCK);

    /* The same page again keeps what was noted; leading and trailing
     * spaces cover nothing. */
    CHECK(!ui_help_watch_begin(&cv, &p));
    gfx_text(&body, 100 + 200 - 4 - 28 - 60, 104, "      ",
             &gfx_font_10x20, GFX_WHITE, 1);
    ui_help_watch_end();
    CHECK_EQ(ui_help_badge_rect(&p, 0).x, moved.x);

    /* A reset forgets it, as the guide's opening, closing and a change of
     * screen do: the same page starts afresh at its first place. */
    ui_help_watch_reset();
    CHECK_EQ(ui_help_badge_rect(&p, 0).x, 104);
    CHECK(ui_help_watch_begin(&cv, &p));
    ui_help_watch_end();
    CHECK_EQ(ui_help_badge_rect(&p, 0).x, 104);

    /* Another page starts afresh: its first place again. */
    p.item[0].rect.w = 210;
    CHECK(ui_help_watch_begin(&cv, &p));
    ui_help_watch_end();
    CHECK_EQ(ui_help_badge_rect(&p, 0).x, 104);
}

/* Closing the guide and opening it again forgets what the screen drew:
 * the router's first frame after the opening starts the page afresh. */
TEST_CASE(a_guide_opened_again_notes_the_strings_afresh)
{
    fresh();
    ui_router_goto(SCREEN_SETUP);
    ui_router_render(&cv, 0);
    gfx_rect_t g = ui_band_guide_rect();
    tap(g.x + g.w / 2, g.y + g.h / 2);
    ui_help_page_t p;
    REQUIRE(ui_router_guide_now(&p));
    ui_router_render(&cv, 0);
    /* Open: the page is noted, a second start keeps it. */
    CHECK(!ui_help_watch_begin(&cv, &p));
    ui_help_watch_end();
    g = ui_help_button_rect(p.side, UI_HELP_HIT_CLOSE);
    tap(g.x + g.w / 2, g.y + g.h / 2);
    REQUIRE(!ui_router_guide_open());
    /* Closed: forgotten. */
    CHECK(ui_help_watch_begin(&cv, &p));
    ui_help_watch_end();
    ui_help_watch_reset();
}

/* A small control at the live half's left edge, inside a marked panel whose
 * badge takes its corner: every place is out of the live half, near that
 * badge or over the panel from outside.  The badge falls back to the first
 * place in the live half, never the one left of it under the dock. */
TEST_CASE(a_badge_with_no_place_left_stays_in_the_live_half)
{
    fresh();
    ui_help_watch_reset();
    ui_help_page_t p;
    memset(&p, 0, sizeof(p));
    p.side = UI_HELP_DOCK_LEFT;           /* live half from x 496 */
    p.count = 2;
    p.item[0] = (ui_help_item_t){ "PANEL", TX_HG_OV_SERVO,
                                  gfx_rect_make(496, 0, 528, 400),
                                  UI_HELP_AT_LEFT };
    p.item[1] = (ui_help_item_t){ "A", TX_HG_OV_SERVO,
                                  gfx_rect_make(496, 0, 40, 40),
                                  UI_HELP_AT_LEFT };
    const gfx_rect_t b = ui_help_badge_rect(&p, 1);
    CHECK(inside(b, ui_help_live_rect(p.side)));
}

/* A place on its control's left edge or top-left corner counts as outside
 * it: it covers no neighbouring marked control. */
TEST_CASE(a_badge_on_its_controls_edge_covers_no_neighbour)
{
    fresh();
    ui_help_watch_reset();
    ui_help_page_t p;
    memset(&p, 0, sizeof(p));
    p.side = UI_HELP_DOCK_RIGHT;
    p.count = 2;
    /* B left of A, 4px apart: A's left-edge place reaches into B. */
    p.item[0] = (ui_help_item_t){ "B", TX_HG_OV_SERVO,
                                  gfx_rect_make(20, 100, 100, 40),
                                  UI_HELP_AT_LEFT };
    p.item[1] = (ui_help_item_t){ "A", TX_HG_OV_SERVO,
                                  gfx_rect_make(124, 100, 100, 40),
                                  UI_HELP_AT_LEFT };
    /* Strings from x 139 over A and above and under it: every place but
     * the ones on its left edge and top-left corner is covered. */
    REQUIRE(ui_help_watch_begin(&cv, &p));
    for (int y = 60; y <= 180; y += 20) {
        gfx_text(&cv, 139, UI_BAND_H + y, "AAAAAAAAAAAAAAAAAAAA",
                 &gfx_font_10x20, GFX_WHITE, 1);
    }
    ui_help_watch_end();
    const gfx_rect_t b = ui_help_badge_rect(&p, 1);
    gfx_rect_t other = p.item[0].rect;
    other.y = (int16_t)(other.y + UI_BAND_H);
    CHECK(!near(b, other, 0));
    ui_help_watch_reset();
}

TEST_CASE(a_badge_shows_on_a_fill_of_its_own_colour)
{
    fresh();
    ui_help_page_t p;
    memset(&p, 0, sizeof(p));
    p.side = UI_HELP_DOCK_RIGHT;
    p.count = 1;
    p.item[0] = (ui_help_item_t){ "A", TX_HG_OV_SERVO,
                                  gfx_rect_make(100, 100, 200, 100),
                                  UI_HELP_AT_LEFT };
    for (int th = 0; th < (int)UI_THEME_COUNT; ++th) {
        ui_theme_set((ui_theme_id_t)th);
        /* A lit button: the accent fill under the whole control. */
        gfx_fill_rect(&cv, 100, UI_BAND_H + 100, 200, 100, UI_ACCENT);
        ui_help_render_marks(&cv, &p);
        const gfx_rect_t b = ui_help_badge_rect(&p, 0);
        const int cy = b.y + 14;
        /* The ring's pixels at the disc's left edge are not the fill's. */
        CHECK(fb[(size_t)cy * W + (size_t)(b.x + 1)] != UI_ACCENT);
        CHECK_EQ(fb[(size_t)cy * W + (size_t)(b.x + 1)], UI_TEXT_ON_LIGHT);
        /* The disc inside it is the accent. */
        CHECK_EQ(fb[(size_t)cy * W + (size_t)(b.x + 4)], UI_ACCENT);
    }
    ui_theme_set(UI_THEME_DARK);
}

int main(void)
{
    RUN(the_dock_and_the_live_half_split_the_body);
    RUN(the_hit_test_finds_buttons_dock_and_badges);
    RUN(armed_offers_only_armed_pages_and_says_so);
    RUN(the_page_set_key_follows_the_titles);
    RUN(every_page_of_every_screen_fits);
    RUN(the_screen_draws_nothing_under_the_dock);
    RUN(an_umlaut_label_moves_the_text_by_its_cells);
    RUN(a_badge_moves_off_a_string_the_screen_drew);
    RUN(a_badge_shows_on_a_fill_of_its_own_colour);
    RUN(a_guide_opened_again_notes_the_strings_afresh);
    RUN(a_badge_with_no_place_left_stays_in_the_live_half);
    RUN(a_badge_on_its_controls_edge_covers_no_neighbour);
    return test_summary("help_pages");
}
