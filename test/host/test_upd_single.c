/*
 * SETUP > UPDATES > SINGLE UPDATES (upd_single.c): OPTIONS' row Single
 * updates opens the page, BACK and the home tag close it, an unsaved
 * option asks first; the home tag closes the devices' page, then the page,
 * then leaves UPDATES; leaving UPDATES and a lost touch end every hold
 * unfired; no check and no download starts from the page, and a job that
 * starts elsewhere closes nothing; UPDATES itself takes no press where the
 * IOmcu hold, the C6 RADIO row and the devices' entry stand on the page;
 * the IOmcu hold's rectangle at its edges and the other rows' rectangles;
 * one press at a time across the IOmcu hold, the C6 RADIO row, the
 * devices' entry and BACK; the page and the devices' page inside their
 * canvas in both languages.
 *
 * SPDX-License-Identifier: MIT
 */

#include "upd_rig.h"

#include "ui_footer.h"
#include "upd_dev.h"

/** @brief The devices' entry's middle, body coordinates. @{ */
#define ENTRY_X (UPD_DEV_X + UPD_DEV_W / 2)
#define ENTRY_Y (UPD_DEV_BTN_Y + UPD_DEV_BTN_H / 2)
/** @} */

/** @brief An IOmcu on 0.17.0, this panel carrying 0.18.0: READY. */
static const upd_cu_view_t k_ready = {
    .sec = CU_SEC_READY, .known = true, .have_image = true,
    .running = { 0, 17, 0 }, .carried = { 0, 18, 0 },
};

/* UPDATES on the router, nothing open, every update READY: the IOmcu,
 * the C6 and each device. */
static void on_updates(void)
{
    fresh();
    ui_router_goto(SCREEN_UPDATES);
    ui_router_set_alert(NULL);
    upd_screen_set_coproc(&k_ready);
    const upd_c6_view_t c6 = { .row = FW_C6_ROW_READY, .have_image = true };
    upd_c6_set(&c6);
    for (int i = 0; i < UPD_DEV_COUNT; ++i) {
        upd_dev_view_t v;
        memset(&v, 0, sizeof(v));
        v.row = DEVUPD_ROW_READY;
        v.known = true;
        v.have_image = true;
        v.carried = (fw_version_t){ 0u, 23u, 12u };
        upd_dev_set((upd_dev_id_t)i, &v);
    }
}

/* on_updates() and SINGLE UPDATES opened from OPTIONS. */
static void on_page(void)
{
    on_updates();
    upd_open_single(upd());
}

/* Every latch drained: whether any of them had fired. */
static bool any_fired(void)
{
    bool fired = upd_screen_take_coproc();
    fired = upd_c6_take() || fired;
    for (int i = 0; i < UPD_DEV_COUNT; ++i) {
        fired = upd_dev_take((upd_dev_id_t)i) || fired;
    }
    fired = upd_screen_take() != 0u || fired;
    return fired;
}

/* A press of finger @p id at (@p x, @p y) held for @p s seconds, not let
 * go, in steps of UI_HOLD_MAX_CREDIT_S and less. */
static void hold_for(uint8_t id, int x, int y, float s)
{
    ev(upd(), TOUCH_EVENT_DOWN, id, x, y, 0);
    while (s > 0.0f) {
        const float dt = s > UI_HOLD_MAX_CREDIT_S ? UI_HOLD_MAX_CREDIT_S : s;
        upd()->tick(dt);
        s -= dt;
    }
}

TEST_CASE(single_updates_opens_from_options_and_back_closes_it)
{
    on_updates();
    CHECK(!upd()->covered());
    tap(upd(), UP_SET_X, UP_SET_Y);
    const ui_setlist_t *sl = upd_screen_settings_list();
    CHECK(sl->open);
    CHECK_EQ(ui_setlist_count(sl), 3);
    const gfx_rect_t r = ui_setlist_row_rect(sl, UP_SINGLE_ROW);
    tap(upd(), r.x + r.w / 2, r.y + r.h / 2);
    CHECK(!sl->open);                 /* the list gave way to the page */
    CHECK(upd()->covered());
    CHECK(!upd_dev_shown());
    CHECK_EQ(upd_screen_take(), 0u);  /* opening asks for nothing */
    /* BACK slid off closes nothing. */
    ev(upd(), TOUCH_EVENT_DOWN, 1, UP_BACK_X, UP_BACK_Y, 0);
    ev(upd(), TOUCH_EVENT_MOVE, 1, UP_BACK_X, UP_BACK_Y - 60, 0);
    ev(upd(), TOUCH_EVENT_UP, 1, UP_BACK_X, UP_BACK_Y - 60, 0);
    CHECK(upd()->covered());
    /* BACK at its corners closes it; a tap 1px outside does not. */
    const gfx_rect_t b = ui_footer_rect(UI_FOOTER_SETTINGS);
    tap(upd(), b.x - 1, b.y + b.h / 2);
    CHECK(upd()->covered());
    tap(upd(), b.x + b.w - 1, b.y + b.h - 1);
    CHECK(!upd()->covered());
    /* UPDATES again: the same spot is OPTIONS, which opens the list. */
    tap(upd(), UP_SET_X, UP_SET_Y);
    CHECK(sl->open);
    CHECK(any_fired() == false);
}

/* An unsaved change on OPTIONS: the row raises the leave warning first;
 * DISCARD puts the value back and opens the page.  STAY keeps the list;
 * the home tag's warning after it leads back to UPDATES, not to the page. */
TEST_CASE(an_unsaved_option_asks_before_the_page_opens)
{
    on_updates();
    list_step(0, false);                       /* Reminder at start off */
    CHECK(!settings_get_bool(SET_UPD_REMIND));
    const ui_setlist_t *sl = upd_screen_settings_list();
    const gfx_rect_t r = ui_setlist_row_rect(sl, UP_SINGLE_ROW);
    tap(upd(), r.x + r.w / 2, r.y + r.h / 2);
    CHECK(sl->open);
    CHECK(sl->warn);
    const gfx_rect_t ds = ui_setlist_strip_rect(1);
    tap(upd(), ds.x + ds.w / 2, ds.y + ds.h / 2);   /* DISCARD */
    CHECK(settings_get_bool(SET_UPD_REMIND));
    CHECK(!sl->open);
    CHECK(upd()->covered());                   /* SINGLE UPDATES */
    CHECK(!settings_save_asked());
    CHECK(upd()->back());
    CHECK(!upd()->covered());

    list_step(0, false);
    tap(upd(), r.x + r.w / 2, r.y + r.h / 2);
    CHECK(sl->warn);
    const gfx_rect_t st = ui_setlist_strip_rect(0);
    tap(upd(), st.x + st.w / 2, st.y + st.h / 2);   /* STAY */
    CHECK(sl->open);
    CHECK(!sl->warn);
    CHECK(upd()->back());                      /* the home tag: the warning */
    CHECK(sl->warn);
    tap(upd(), ds.x + ds.w / 2, ds.y + ds.h / 2);   /* DISCARD */
    CHECK(!sl->open);
    CHECK(!upd()->covered());                  /* UPDATES, no page */
    CHECK(settings_get_bool(SET_UPD_REMIND));
}

TEST_CASE(the_home_tag_closes_the_page_before_it_leaves_updates)
{
    on_page();
    tap(upd(), ENTRY_X, ENTRY_Y);
    CHECK(upd_dev_shown());
    CHECK(upd()->back());              /* the devices' page */
    CHECK(!upd_dev_shown());
    CHECK(upd()->covered());
    CHECK(upd()->back());              /* SINGLE UPDATES */
    CHECK(!upd()->covered());
    CHECK(!upd()->back());             /* UPDATES: the tag leaves it */
    /* A hold under way on the page ends unfired with the home tag. */
    upd_open_single(upd());
    hold_for(1, UP_BTN_X, UP_CU_Y, 1.5f);
    CHECK(upd()->back());
    upd()->tick(1.0f);
    upd()->tick(1.0f);
    ev(upd(), TOUCH_EVENT_UP, 1, UP_BTN_X, UP_CU_Y, 0);
    CHECK(!any_fired());
}

TEST_CASE(leaving_updates_closes_the_page_and_drops_every_hold)
{
    static const struct {
        int  x, y;
        bool devices;   /* on the devices' page */
    } k_holds[] = {
        { UP_BTN_X, UP_CU_Y, false },     /* the IOmcu */
        { UP_C6_X, UP_C6_Y, false },      /* the C6 RADIO row */
        { 0, 0, true },                   /* the Dial's row */
    };
    for (size_t i = 0; i < sizeof(k_holds) / sizeof(k_holds[0]); ++i) {
        on_page();
        int x = k_holds[i].x, y = k_holds[i].y;
        if (k_holds[i].devices) {
            tap(upd(), ENTRY_X, ENTRY_Y);
            const gfx_rect_t h = upd_dev_rect(UPD_DEV_DIAL);
            x = h.x + h.w / 2;
            y = h.y + h.h / 2;
        }
        hold_for(1, x, y, 1.75f);
        ui_router_goto(SCREEN_SETUP);
        CHECK(!upd()->covered());
        CHECK(!upd_dev_shown());
        for (int k = 0; k < 8; ++k) {
            upd()->tick(UI_HOLD_MAX_CREDIT_S);
        }
        ev(upd(), TOUCH_EVENT_UP, 1, x, y, 0);
        CHECK(!any_fired());
        /* Back on UPDATES the page is closed. */
        ui_router_goto(SCREEN_UPDATES);
        CHECK(!upd()->covered());
    }
    /* A lost touch drops the hold and keeps the page. */
    on_page();
    hold_for(1, UP_BTN_X, UP_CU_Y, 1.0f);
    upd()->cancel();
    for (int k = 0; k < 8; ++k) {
        upd()->tick(UI_HOLD_MAX_CREDIT_S);
    }
    CHECK(!any_fired());
    CHECK(upd()->covered());
}

/* CHECK's place and the download's place are nothing on the page: no
 * check and no download starts from it, by a tap or a 2.5s hold. */
TEST_CASE(a_check_cannot_start_while_the_page_is_open)
{
    on_page();
    tap(upd(), UP_BTN_X, UP_CHECK_Y);
    CHECK_EQ(upd_screen_take(), 0u);
    set_newer();
    push();
    hold_for(1, UP_DL_X, UP_DL_Y, 2.5f);
    ev(upd(), TOUCH_EVENT_UP, 1, UP_DL_X, UP_DL_Y, 0);
    CHECK_EQ(upd_screen_take(), 0u);
    CHECK(upd()->covered());
    /* Closed, both act again. */
    CHECK(upd()->back());
    hold_for(1, UP_DL_X, UP_DL_Y, 2.5f);
    ev(upd(), TOUCH_EVENT_UP, 1, UP_DL_X, UP_DL_Y, 0);
    CHECK_EQ(upd_screen_take(), (unsigned)UPD_REQ_DOWNLOAD);
    base_view();
    push();
    tap(upd(), UP_BTN_X, UP_CHECK_Y);
    CHECK_EQ(upd_screen_take(), (unsigned)UPD_REQ_CHECK);
}

/* A check the start-up driver starts, and a download a browser upload
 * starts, close nothing: the page stays and its holds stay live; OPTIONS'
 * list in the same place closes. */
TEST_CASE(a_job_started_elsewhere_closes_nothing)
{
    on_page();
    g_v.job = UPD_JOB_CHECK;
    g_v.phase = UPD_PH_ASKING;
    g_v.block = UPD_WHY_BUSY;
    push();
    ticks(upd(), 3);
    CHECK(upd()->covered());
    g_v.job = UPD_JOB_DOWNLOAD;
    g_v.phase = UPD_PH_IMAGE;
    push();
    ticks(upd(), 3);
    CHECK(upd()->covered());
    hold_for(1, UP_BTN_X, UP_CU_Y, 2.0f);
    CHECK(upd_screen_take_coproc());
    ev(upd(), TOUCH_EVENT_UP, 1, UP_BTN_X, UP_CU_Y, 0);
    CHECK(upd()->back());
    CHECK(!upd()->covered());
    /* The list over a job that starts closes; the page did not. */
    base_view();
    push();
    tap(upd(), UP_SET_X, UP_SET_Y);
    CHECK(upd()->covered());
    g_v.job = UPD_JOB_CHECK;
    g_v.phase = UPD_PH_ASKING;
    push();
    ticks(upd(), 1);
    CHECK(!upd()->covered());
}

/* UPDATES draws no IOmcu section, no C6 RADIO row and no devices' entry:
 * a 2.5s hold or a tap where they stand on the page asks for nothing and
 * opens nothing. */
TEST_CASE(updates_takes_no_press_where_the_page_has_its_rows)
{
    on_updates();
    static const int k_spots[][2] = {
        { UP_BTN_X, UP_CU_Y },
        { UP_C6_X, UP_C6_Y },
        { ENTRY_X, ENTRY_Y },
    };
    for (size_t i = 0; i < sizeof(k_spots) / sizeof(k_spots[0]); ++i) {
        hold_for(1, k_spots[i][0], k_spots[i][1], 2.5f);
        ev(upd(), TOUCH_EVENT_UP, 1, k_spots[i][0], k_spots[i][1], 0);
        tap(upd(), k_spots[i][0], k_spots[i][1]);
        CHECK(!any_fired());
        CHECK(!upd()->covered());
        CHECK(!upd_dev_shown());
        CHECK(!upd_c6_pressed());
        CHECK(!upd_dev_pressed());
        CHECK(ui_router_alert() == NULL);
    }
}

/* The rows keep the rectangles they had on UPDATES: the IOmcu hold
 * 460 x 56px from x 16 at y 380, acting at its corners and not 1px
 * outside; the C6 RADIO hold 532 x 56px from x 484 at y 380; the devices'
 * entry 460 x 56px from x 16 at y 316; BACK the footer's right slot. */
TEST_CASE(the_three_rows_keep_their_rectangles)
{
    const gfx_rect_t c6 = upd_c6_rect();
    CHECK_EQ(c6.x, 484);
    CHECK_EQ(c6.y, 380);
    CHECK_EQ(c6.w, 532);
    CHECK_EQ(c6.h, 56);
    const gfx_rect_t en = upd_dev_entry_rect();
    CHECK_EQ(en.x, 16);
    CHECK_EQ(en.y, 316);
    CHECK_EQ(en.w, 460);
    CHECK_EQ(en.h, 56);
    CHECK(en.y + en.h + 8 <= UP_CU_T);
    CHECK(UP_CU_T + UP_BTN_H <= UI_FOOTER_FLOOR);
    CHECK(UP_CARD_B + 12 <= UPD_C6_LINE_Y);
    static const struct {
        int  x, y;
        bool in;
    } k_edges[] = {
        { 16, 380, true },  { 475, 380, true }, { 16, 435, true },
        { 475, 435, true }, { 15, 400, false }, { 476, 400, false },
        { 200, 379, false }, { 200, 436, false },
    };
    for (size_t i = 0; i < sizeof(k_edges) / sizeof(k_edges[0]); ++i) {
        on_page();
        hold_for(1, k_edges[i].x, k_edges[i].y, 2.25f);
        ev(upd(), TOUCH_EVENT_UP, 1, k_edges[i].x, k_edges[i].y, 0);
        CHECK_EQ(upd_screen_take_coproc(), k_edges[i].in);
    }
    on_page();
    const gfx_rect_t b = ui_footer_rect(UI_FOOTER_SETTINGS);
    CHECK(gfx_rect_contains(b, UP_BACK_X, UP_BACK_Y));
    tap(upd(), b.x, b.y);
    CHECK(!upd()->covered());
}

/* One finger at a time across the IOmcu hold, the C6 RADIO row, the
 * devices' entry and BACK: a second finger on any of them starts
 * nothing, and the first finger's hold still completes. */
TEST_CASE(one_press_at_a_time_across_iomcu_c6_and_devices)
{
    static const int k_spots[][2] = {
        { UP_BTN_X, UP_CU_Y },
        { UP_C6_X, UP_C6_Y },
        { ENTRY_X, ENTRY_Y },
        { UP_BACK_X, UP_BACK_Y },
    };
    const int n = (int)(sizeof(k_spots) / sizeof(k_spots[0]));
    for (int first = 0; first < n; ++first) {
        for (int second = 0; second < n; ++second) {
            if (second == first) {
                continue;
            }
            on_page();
            ev(upd(), TOUCH_EVENT_DOWN, 1, k_spots[first][0],
               k_spots[first][1], 0);
            hold_for(2, k_spots[second][0], k_spots[second][1], 2.5f);
            ev(upd(), TOUCH_EVENT_UP, 2, k_spots[second][0],
               k_spots[second][1], 0);
            /* Only the first finger's hold fired: the IOmcu's or the
             * C6's; the entry and BACK are taps, not yet released. */
            CHECK_EQ(upd_screen_take_coproc(), first == 0);
            CHECK_EQ(upd_c6_take(), first == 1);
            CHECK(!upd_dev_shown());
            CHECK(upd()->covered());
            ev(upd(), TOUCH_EVENT_UP, 1, k_spots[first][0],
               k_spots[first][1], 0);
            CHECK_EQ(upd_dev_shown(), first == 2);
            CHECK_EQ(upd()->covered(), first != 3);
            CHECK(!any_fired());
        }
    }
    /* On the devices' page the IOmcu hold's spot is nobody's. */
    on_page();
    tap(upd(), ENTRY_X, ENTRY_Y);
    hold_for(1, UP_BTN_X, UP_CU_Y, 2.5f);
    ev(upd(), TOUCH_EVENT_UP, 1, UP_BTN_X, UP_CU_Y, 0);
    CHECK(!any_fired());
    CHECK(upd_dev_shown());
}

/* SINGLE UPDATES and the devices' page over it inside the body canvas,
 * every pixel painted, in both languages, with every IOmcu state. */
TEST_CASE(the_page_renders_inside_its_canvas_in_both_languages)
{
    static gfx_color_t px[GUARD_CANVAS_PIXELS(UI_PANEL_W, UI_BODY_H)];
    gfx_canvas_t c;
    guard_canvas_t g;
    for (int lang = 0; lang < 2; ++lang) {
        for (int sec = CU_SEC_NO_IMAGE; sec <= CU_SEC_READY; ++sec) {
            on_page();
            ui_text_set_language(lang == 0 ? UI_LANG_EN : UI_LANG_DE);
            upd_cu_view_t v = k_ready;
            v.sec = (cu_sec_t)sec;
            upd_screen_set_coproc(&v);
            for (int page = 0; page < 2; ++page) {
                if (page == 1) {
                    tap(upd(), ENTRY_X, ENTRY_Y);
                    CHECK(upd_dev_shown());
                }
                guard_canvas_init(&g, &c, px, UI_PANEL_W, UI_BODY_H);
                upd_screen_invalidate();
                upd()->render(&c, 0);
                CHECK_EQ(guard_canvas_touched(&g), 0u);
                CHECK_EQ(guard_canvas_unpainted(&g), 0u);
            }
        }
    }
    ui_text_set_language(UI_LANG_EN);
}

int main(void)
{
    RUN(single_updates_opens_from_options_and_back_closes_it);
    RUN(an_unsaved_option_asks_before_the_page_opens);
    RUN(the_home_tag_closes_the_page_before_it_leaves_updates);
    RUN(leaving_updates_closes_the_page_and_drops_every_hold);
    RUN(a_check_cannot_start_while_the_page_is_open);
    RUN(a_job_started_elsewhere_closes_nothing);
    RUN(updates_takes_no_press_where_the_page_has_its_rows);
    RUN(the_three_rows_keep_their_rectangles);
    RUN(one_press_at_a_time_across_iomcu_c6_and_devices);
    RUN(the_page_renders_inside_its_canvas_in_both_languages);
    return test_summary("upd_single");
}
