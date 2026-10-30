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
                                                       80) };
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
        /* CLOSE: 140 x 48px, centred at the dock's bottom, above the
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
    CHECK_EQ(ui_help_count(overview_screen(), true), 3);
    /* A screen without a guide. */
    CHECK_EQ(ui_help_count(servo_screen(), false), 0);
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

/* Whether @p a and @p b come closer than @p gap px. */
static bool near(gfx_rect_t a, gfx_rect_t b, int gap)
{
    return a.x < b.x + b.w + gap && b.x < a.x + a.w + gap
           && a.y < b.y + b.h + gap && b.y < a.y + a.h + gap;
}

/* Whether @p in lies inside @p out. */
static bool inside(gfx_rect_t in, gfx_rect_t out)
{
    return in.x >= out.x && in.y >= out.y && in.x + in.w <= out.x + out.w
           && in.y + in.h <= out.y + out.h;
}

/**
 * @brief Whether @p a and @p b are the same rect.
 * @param a A rect.
 * @param b Another.
 * @return True when x, y, w and h match.
 */
static bool same(gfx_rect_t a, gfx_rect_t b)
{
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

/** @brief Every page of @p scr, on top, as the bench is now. */
static void check_pages(const ui_screen_t *scr, const char *what)
{
    /* PREV and NEXT where page 1 has them, on every page. */
    const bool armed = ui_router_status()->armed;
    ui_help_page_t p0;
    if (!ui_help_page(scr, armed, 0, &p0)) {
        T_FAIL("%s: no page 1", what);
        return;
    }
    const gfx_rect_t prev0 = ui_help_button_rect(p0.side, UI_HELP_HIT_PREV);
    const gfx_rect_t next0 = ui_help_button_rect(p0.side, UI_HELP_HIT_NEXT);
    /* And clear of every chip the band shows now, "?" and STOP. */
    for (int chip = UI_BAND_CHIP_NONE + 1; chip < UI_BAND_CHIP_COUNT;
         ++chip) {
        gfx_rect_t r;
        if (ui_band_chip_rect(ui_router_status(), (ui_band_chip_t)chip, &r)
            && (near(r, prev0, 2) || near(r, next0, 2))) {
            T_FAIL("%s: chip %d at x %d is under PREV or NEXT", what, chip,
                   r.x);
        }
    }
    if (near(ui_band_guide_rect(), next0, 2)
        || near(ui_band_stop_rect(), next0, 2)) {
        T_FAIL("%s: NEXT covers \"?\" or STOP", what);
    }
    const int count = ui_help_count(scr, armed);
    if (count != scr->help_count() && !armed) {
        T_FAIL("%s: %d pages offered, %d given", what, count,
               scr->help_count());
    }
    const bool note = ui_help_armed_note(scr, armed);
    const gfx_rect_t band = gfx_rect_make(0, 0, W, UI_BAND_H);
    for (int n = 0; n < count; ++n) {
        ui_help_page_t p;
        if (!ui_help_page(scr, armed, n, &p)) {
            T_FAIL("%s: page %d missing", what, n + 1);
            continue;
        }
        const gfx_rect_t dock = ui_help_dock_rect(p.side);
        const gfx_rect_t live = ui_help_live_rect(p.side);
        const gfx_rect_t prev = ui_help_button_rect(p.side,
                                                    UI_HELP_HIT_PREV);
        const gfx_rect_t next = ui_help_button_rect(p.side,
                                                    UI_HELP_HIT_NEXT);
        if (!same(next, next0) || !same(prev, prev0)) {
            T_FAIL("%s: NEXT at x %d y %d on page %d, x %d y %d on page 1",
                   what, next.x, next.y, n + 1, next0.x, next0.y);
        }
        if (p.count < 1 || p.count > UI_HELP_ITEMS) {
            T_FAIL("%s: page %d has %d items", what, n + 1, p.count);
        }
        if (ui_tr(p.title)[0] == '\0') {
            T_FAIL("%s: page %d has no title", what, n + 1);
        }
        const int bottom = ui_help_text_bottom(&p, note && n == 0);
        if (bottom > ui_help_text_limit()) {
            T_FAIL("%s: page %d's text ends at y %d, past %d", what, n + 1,
                   bottom, ui_help_text_limit());
        }
        for (int i = 0; i < p.count; ++i) {
            const ui_help_item_t *it = &p.item[i];
            gfx_rect_t r = it->rect;
            r.y = (int16_t)(r.y + UI_BAND_H);
            if (it->label == NULL || it->label[0] == '\0'
                || ui_tr(it->text)[0] == '\0') {
                T_FAIL("%s: page %d item %d has no label or text", what,
                       n + 1, i + 1);
            }
            const int rows = ui_help_item_rows(&p, i);
            if (rows < 1 || rows > UI_HELP_ROWS) {
                T_FAIL("%s: page %d item %d (%s) takes %d rows", what, n + 1,
                       i + 1, it->label, rows);
            }
            if (!inside(r, live) && !inside(r, band)) {
                T_FAIL("%s: page %d item %d (%s) at x %d y %d w %d h %d is "
                       "not in the live half or the band", what, n + 1,
                       i + 1, it->label, r.x, r.y, r.w, r.h);
            }
            if (near(r, dock, 0)) {
                T_FAIL("%s: page %d item %d under the dock", what, n + 1,
                       i + 1);
            }
            if (near(r, next, 0) || near(r, prev, 0)) {
                T_FAIL("%s: page %d item %d (%s) under PREV or NEXT", what,
                       n + 1, i + 1, it->label);
            }
            const gfx_rect_t b = ui_help_badge_rect(&p, i);
            if (!inside(b, live)) {
                T_FAIL("%s: page %d badge %d not in the live half", what,
                       n + 1, i + 1);
            }
            for (int k = 0; k < i; ++k) {
                if (near(b, ui_help_badge_rect(&p, k), 2)) {
                    T_FAIL("%s: page %d badges %d and %d closer than 2px",
                           what, n + 1, k + 1, i + 1);
                }
            }
        }
        if (!p.armed_ok) {
            continue;
        }
        /* On a page for ARMED, the dock covers no control that stops an
         * output and no rect the screen must keep in view. */
        if (p.must_show.w > 0) {
            gfx_rect_t m = p.must_show;
            m.y = (int16_t)(m.y + UI_BAND_H);
            if (near(m, dock, 0)) {
                T_FAIL("%s: page %d's dock covers its must_show", what,
                       n + 1);
            }
        }
        for (int y = dock.y; y < dock.y + dock.h && scr->safe_at; y += 8) {
            for (int x = dock.x; x < dock.x + dock.w; x += 8) {
                if (scr->safe_at(x, y - UI_BAND_H)) {
                    T_FAIL("%s: page %d's dock covers a safe control at "
                           "x %d y %d", what, n + 1, x, y);
                }
            }
        }
    }
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
    /* The menu's pages: four tiles, four tiles, the band and the mark. */
    fresh();
    to_overview();
    ui_help_page_t p;
    CHECK(ui_help_page(overview_screen(), false, 0, &p) && p.count == 4);
    CHECK(ui_help_page(overview_screen(), false, 1, &p) && p.count == 4);
    CHECK(ui_help_page(overview_screen(), false, 2, &p) && p.count == 4);
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
        ui_help_render_dock(&c2, &p, page, 3, false, UI_HELP_HIT_NONE);
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

int main(void)
{
    RUN(the_dock_and_the_live_half_split_the_body);
    RUN(the_hit_test_finds_buttons_dock_and_badges);
    RUN(armed_offers_only_armed_pages_and_says_so);
    RUN(the_page_set_key_follows_the_titles);
    RUN(every_page_of_every_screen_fits);
    RUN(the_screen_draws_nothing_under_the_dock);
    return test_summary("help_pages");
}
