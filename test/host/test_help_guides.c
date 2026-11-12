/**
 * @file test_help_guides.c
 * @brief SERVO's and SUPPLY's screen guide pages: what each state offers,
 * that every page fits its dock and marks controls in the live half, in
 * both languages and themes, SAFE and ARMED, and that no offered page's
 * dock covers a control that stops an output.
 *
 * Cases:
 * - every_servo_page_fits_and_hides_no_stop
 * - every_supply_page_fits_and_hides_no_stop
 * - a_live_supply_output_takes_the_pages_that_cover_output_off
 * - prog_5v_on_takes_the_pages_that_cover_prog_5v_off
 * - a_run_takes_the_output_cards_pages_and_armed_leaves_none
 * - a_panel_or_a_question_takes_every_page
 * - the_labels_follow_what_the_controls_read
 * - a_session_names_its_controls_as_they_read
 * - a_tab_of_values_offers_one_page_of_its_values
 * - a_badge_at_the_right_sits_in_the_top_right_corner
 * - servo_repaints_nothing_under_a_dock
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_bench_rig.h"

#include "help_check.h"
#include "hitec_panel.h"
#include "kst_panel.h"
#include "prog5v_box.h"
#include "supply_spots.h"
#include "ui_help.h"
#include "ui_layout.h"

/* A step the rest of a case cannot do without: names it and ends the case. */
#define REQUIRE(cond)                                                         \
    do {                                                                      \
        if (!(cond)) {                                                        \
            T_FAIL("expected %s", #cond);                                     \
            return;                                                           \
        }                                                                     \
    } while (0)

/** @brief PROG 5V's published states, as prog5v_box.h reads them. */
enum { P5_OFF = 2, P5_ON = 3 };

/* ------------------------------------------------------------- helpers */

/**
 * @brief The band's state as the router hands it to the guide: SAFE, or
 *        ARMED for SERVO.
 * @param armed The bench is ARMED.
 */
static void band_armed(bool armed)
{
    ui_bench_status_t st = *ui_router_status();
    st.armed = armed;
    st.armed_roles = armed ? UI_ARMED_SERVO : 0u;
    ui_router_set_status(&st);
}

/**
 * @brief check_pages() on @p sc as the bench is now, SAFE or @p armed.
 * @param sc    The screen.
 * @param armed The bench is ARMED.
 * @param what  The state, for a failure.
 * @return The pages offered.
 */
static int pages_checked(const ui_screen_t *sc, bool armed, const char *what)
{
    band_armed(armed);
    check_pages(sc, what);
    const int n = ui_help_count(sc, armed);
    band_armed(false);
    return n;
}

/**
 * @brief The title of page @p n that @p sc offers SAFE.
 * @param sc The screen.
 * @param n  The page.
 * @return Its title, or -1 past the last.
 */
static int title_of(const ui_screen_t *sc, int n)
{
    ui_help_page_t p;
    return ui_help_page(sc, false, n, &p) ? (int)p.title : -1;
}

/**
 * @brief Check the titles and item counts of the pages @p sc offers SAFE.
 * @param sc     The screen.
 * @param n      The pages expected.
 * @param titles Each page's title.
 * @param items  Each page's item count.
 * @param what   The state, for failure messages.
 */
static void check_marks(const ui_screen_t *sc, int n, const int *titles,
                        const int *items, const char *what)
{
    for (int i = 0; i < n; ++i) {
        ui_help_page_t p;
        if (!ui_help_page(sc, false, i, &p)) {
            T_FAIL("%s: page %d missing", what, i + 1);
            continue;
        }
        if ((int)p.title != titles[i] || p.count != items[i]) {
            T_FAIL("%s: page %d has title %d and %d items, not %d and %d",
                   what, i + 1, (int)p.title, p.count, titles[i], items[i]);
        }
    }
}

/** @brief SERVO on a state of the table below, from a fresh screen. */
typedef struct {
    const char *what;   /**< the state's name                            */
    servo_fn_t fn;      /**< the screen: the hub or a function's         */
    bool list;          /**< its SETTINGS list open                      */
    int safe;           /**< the pages offered SAFE                      */
    int armed;          /**< the pages offered ARMED                     */
    int titles[4];      /**< each SAFE page's title                      */
    int items[4];       /**< each SAFE page's item count                 */
} servo_state_t;

/**
 * @brief SERVO, fresh, put in @p st.
 * @param st The state.
 */
static void servo_to(const servo_state_t *st)
{
    fresh();
    servo_screen_set_armed(false);
    go(st->fn);
    if (st->list) {
        tap_settings();
    }
}

/** @brief SERVO's states with pages: the hub, its list, and each function's
 *         screen. */
static const servo_state_t k_servo[] = {
    { "HUB", SERVO_FN_HUB, false, 2, 2,
      { TX_HG_SVH_TITLE_TILES, TX_HG_SVH_TITLE_FOOTER },
      { 5, 2 } },
    { "LIVE", SERVO_FN_LIVE, false, 3, 0,
      { TX_HG_SV_TITLE_LIVE, TX_HG_SV_TITLE_DRV_READY, TX_HG_SV_TITLE_CARD },
      { 3, 5, 5 } },
    { "SWEEP", SERVO_FN_SWEEP, false, 3, 0,
      { TX_HG_SV_TITLE_SWEEP, TX_HG_SV_TITLE_DRV_READY, TX_HG_SV_TITLE_CARD },
      { 3, 5, 5 } },
    { "TEST", SERVO_FN_TEST, false, 2, 0,
      { TX_HG_SV_TITLE_TEST, TX_HG_SV_TITLE_READY },
      { 3, 5 } },
    { "LIMIT RUN", SERVO_FN_LIMRUN, false, 2, 0,
      { TX_HG_SV_TITLE_LIMRUN, TX_HG_SV_TITLE_READY },
      { 2, 5 } },
    { "RUDDER", SERVO_FN_RUDDER, false, 4, 0,
      { TX_HG_SV_TITLE_RUDDER, TX_HG_SV_TITLE_RUD_READ, TX_HG_SV_TITLE_DRV_READY, TX_HG_SV_TITLE_CARD },
      { 4, 3, 5, 4 } },
    { "RUDDER AUTO", SERVO_FN_RAUTO, false, 3, 0,
      { TX_HG_SV_TITLE_AUTO, TX_HG_SV_TITLE_RUD_READ, TX_HG_SV_TITLE_READY },
      { 3, 2, 5 } },
    { "KST", SERVO_FN_KST, false, 3, 0,
      { TX_HG_KST_TITLE_CONNECT, TX_HG_KST_TITLE_WRITE, TX_HG_SV_TITLE_PRG },
      { 5, 5, 5 } },
    { "HITEC", SERVO_FN_HITEC, false, 3, 0,
      { TX_HG_HT_TITLE_CONNECT, TX_HG_HT_TITLE_WRITE, TX_HG_SV_TITLE_PRG },
      { 5, 5, 5 } },
};

/** @brief The state pose_servo() puts SERVO in. */
static const servo_state_t *s_servo = &k_servo[0];

/** @brief SERVO, fresh, in s_servo's state. */
static void pose_servo(void)
{
    servo_to(s_servo);
}

/** @brief What pose_supply() opens: 0 LIVE, 1 the SETTINGS list, 2 the list
 *         at the PD mini's heading. */
static int s_supply_tabs;

/** @brief A tap of contact 1 at (@p x, @p y) of SUPPLY's body.
 *  @param x The body's x. @param y The body's y. */
static void supply_tap(int x, int y)
{
    const touch_event_t d = { .type = TOUCH_EVENT_DOWN,
                              .point = { .id = 1, .x = (int16_t)x,
                                         .y = (int16_t)y, .strength = 40 } };
    touch_event_t u = d;
    u.type = TOUCH_EVENT_UP;
    supply_screen()->event(&d);
    supply_screen()->event(&u);
}

/** @brief SUPPLY, fresh, showing what s_supply_tabs names. */
static void pose_supply(void)
{
    fresh();
    supply_screen()->enter();
    if (s_supply_tabs == 1) {
        supply_tap(SU_SETTINGS_X, SU_SETTINGS_Y);
    }
    if (s_supply_tabs == 2) {
        supply_screen_open_pdmini();
    }
}

/* ----------------------------------------------------------------- cases */

/* Each SERVO state in both languages and themes, SAFE and ARMED, the link
 * up and down: its own pages, then its READY card's.  ARMED a
 * function screen offers none: either dock covers DISARM in the footer.
 * The hub drives nothing and keeps its two. */
TEST_CASE(every_servo_page_fits_and_hides_no_stop)
{
    for (size_t k = 0; k < sizeof(k_servo) / sizeof(k_servo[0]); ++k) {
        const servo_state_t *st = &k_servo[k];
        s_servo = st;
        char name[32];
        snprintf(name, sizeof(name), "SERVO %s", st->what);
        CHECK_EQ(check_every_state(servo_fn_screen(st->fn), pose_servo,
                                   name),
                 st->safe);
        for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
            for (int th = 0; th < (int)UI_THEME_COUNT; ++th) {
                servo_to(st);
                band_armed(false);
                ui_text_set_language((ui_lang_t)lang);
                ui_theme_set((ui_theme_id_t)th);
                char what[64];
                snprintf(what, sizeof(what), "%s lang %d theme %d", name,
                         lang, th);
                check_marks(scr, st->safe, st->titles, st->items, what);
                servo_screen_set_armed(true);
                CHECK_EQ(pages_checked(scr, true, what), st->armed);
            }
        }
    }
    ui_text_set_language(UI_LANG_EN);
    ui_theme_set(UI_THEME_DARK);
}

/* SUPPLY's LIVE, and its SETTINGS list at the top and at the PD mini, with
 * the output off, both languages and themes, SAFE and ARMED, the link up and
 * down: LIVE's page, then the rail's two; over the list the list's two. */
TEST_CASE(every_supply_page_fits_and_hides_no_stop)
{
    const ui_screen_t *sup = supply_screen();
    static const struct {
        const char *what;
        int tabs;           /* 0 LIVE, 1 the list, 2 the list at PD MINI */
        int safe, armed;
        int titles[4], items[4];
    } k_states[] = {
        { "LIVE", 0, 3, 2,
          { TX_HG_SU_TITLE_LIVE, TX_HG_SU_TITLE_READ, TX_HG_SU_TITLE_CONTROL },
          { 3, 5, 4 } },
        { "SETTINGS", 1, 2, 2,
          { TX_HG_SL_TITLE_ROWS, TX_HG_SL_TITLE_VALUES }, { 3, 4 } },
        { "PD MINI", 2, 2, 2,
          { TX_HG_SL_TITLE_ROWS, TX_HG_SL_TITLE_VALUES }, { 3, 4 } },
    };
    for (size_t k = 0; k < sizeof(k_states) / sizeof(k_states[0]); ++k) {
        s_supply_tabs = k_states[k].tabs;
        char name[32];
        snprintf(name, sizeof(name), "SUPPLY %s", k_states[k].what);
        CHECK_EQ(check_every_state(sup, pose_supply, name), k_states[k].safe);
        for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
            for (int th = 0; th < (int)UI_THEME_COUNT; ++th) {
                pose_supply();
                band_armed(false);
                ui_text_set_language((ui_lang_t)lang);
                ui_theme_set((ui_theme_id_t)th);
                char what[64];
                snprintf(what, sizeof(what), "%s lang %d theme %d", name,
                         lang, th);
                check_marks(sup, k_states[k].safe, k_states[k].titles,
                            k_states[k].items, what);
                CHECK_EQ(pages_checked(sup, true, what), k_states[k].armed);
            }
        }
    }
    ui_text_set_language(UI_LANG_EN);
    ui_theme_set(UI_THEME_DARK);
}

/* OUTPUT OFF shows in SUPPLY's footer while the output is on or an ON is
 * on its way: every SUPPLY page, either dock over the footer's middle
 * slot, is not offered then, and comes back once it is off.  SERVO shows
 * no OUTPUT OFF: KST's READY page docks on the left, off its READY card and
 * the supply row that switches the output off, and stays; the session's
 * two pages dock on the right, over that row, and go.  LIVE's READY rows
 * drive nothing: their pages stay. */
TEST_CASE(a_live_supply_output_takes_the_pages_that_cover_output_off)
{
    fresh();
    go(SERVO_FN_KST);
    CHECK_EQ(scr->help_count(), 3);
    supply_screen_set_output(true);
    supply_state_t st = *supply_screen_reading();
    st.online = true;
    st.output = true;
    supply_screen_push(&st);
    REQUIRE(supply_screen_output_on());
    CHECK_EQ(pages_checked(scr, false, "SERVO KST, output on"), 1);
    CHECK_EQ(title_of(scr, 0), TX_HG_SV_TITLE_PRG);
    go(SERVO_FN_LIVE);
    CHECK_EQ(pages_checked(scr, false, "SERVO LIVE, output on"), 3);
    CHECK_EQ(title_of(scr, 0), TX_HG_SV_TITLE_LIVE);
    go(SERVO_FN_KST);
    const ui_screen_t *sup = supply_screen();
    CHECK_EQ(sup->help_count(), 0);
    st.output = false;
    supply_screen_set_output(false);
    supply_screen_push(&st);
    CHECK_EQ(scr->help_count(), 3);
    CHECK_EQ(sup->help_count(), 3);
}

/**
 * @brief How many points of @p sc's body, 8px apart, its safe_at() names.
 * @param sc The screen.
 * @return The count.
 */
static int safe_points(const ui_screen_t *sc)
{
    int n = 0;
    for (int y = 0; y < UI_BODY_H; y += 8) {
        for (int x = 0; x < UI_PANEL_W; x += 8) {
            n += sc->safe_at(x, y) ? 1 : 0;
        }
    }
    return n;
}

/* PROG 5V OFF stops the ports' own supply, while SAFE as well and with the
 * output off: while the supply reads ON it is a safe control on SUPPLY,
 * and the pages with the dock on the right over it are not offered.  At
 * OFF they come back and PROG 5V ON is no safe control.  On SERVO, LIVE
 * shows PROG 5V as a READY row, and KST keeps its READY page, docked on
 * the left, off its READY card; the session's pages, docked on the right
 * over the card, go while PROG 5V is on. */
TEST_CASE(prog_5v_on_takes_the_pages_that_cover_prog_5v_off)
{
    prog5v_box_reset();
    prog5v_box_publish(P5_ON, 21u, (uint8_t)PROG5V_HELD_NONE);
    fresh();
    REQUIRE(!supply_screen_output_live());
    /* LIVE shows PROG 5V as a READY row, no PROG 5V OFF. */
    CHECK_EQ(safe_points(scr), 0);
    CHECK_EQ(pages_checked(scr, false, "SERVO LIVE, PROG 5V on"), 3);
    go(SERVO_FN_KST);
    CHECK_EQ(pages_checked(scr, false, "SERVO KST, PROG 5V on"), 1);
    CHECK_EQ(title_of(scr, 0), TX_HG_SV_TITLE_PRG);
    const ui_screen_t *sup = supply_screen();
    sup->enter();
    CHECK(safe_points(sup) > 0);
    CHECK_EQ(pages_checked(sup, false, "SUPPLY LIVE, PROG 5V on"), 2);
    CHECK_EQ(title_of(sup, 0), TX_HG_SU_TITLE_READ);
    prog5v_box_publish(P5_OFF, 21u, (uint8_t)PROG5V_HELD_NONE);
    fresh();
    CHECK_EQ(safe_points(scr), 0);
    CHECK_EQ(scr->help_count(), 3);
    sup->enter();
    CHECK_EQ(safe_points(sup), 0);
    CHECK_EQ(sup->help_count(), 3);
    prog5v_box_reset();
    fresh();
}

/* A run's STOP TEST is on the left card, under the output card's dock: the
 * run offers no such page, and as it runs ARMED with the supply on, no
 * page at all, so the band shows no "?".  ARMED a function screen offers
 * none in any case: either dock covers DISARM in the footer.  After the
 * run, the SAFE pages come back. */
TEST_CASE(a_run_takes_the_output_cards_pages_and_armed_leaves_none)
{
    bench_fresh();
    short_runs();
    CHECK_EQ(ui_help_count(scr, true), 0);
    hold_start(2.3f);
    REQUIRE(servo_screen_testing());
    CHECK_EQ(ui_help_count(scr, true), 0);
    /* Nor a page for SAFE: the supply's output is on for the run, so no
     * task's page either. */
    CHECK_EQ(scr->help_count(), 0);
    servo_screen_set_armed(false);
    bench_frames(400u);
    REQUIRE(!servo_screen_testing());
    CHECK(ui_help_count(scr, false) >= 2);
}

/* A keypad and the supply's question are levels the guide does not
 * explain: no page, so no "?".  A list's pages are the list's. */
TEST_CASE(a_panel_or_a_question_takes_every_page)
{
    fresh();
    go(SERVO_FN_RUDDER);                       /* LENGTH's keypad */
    tap(SV_RUD_LEN_X, SV_RUD_LEN_Y);
    REQUIRE(servo_screen_keypad() != NULL && servo_screen_keypad()->open);
    CHECK_EQ(scr->help_count(), 0);
    ui_help_page_t p;
    CHECK(!scr->help_page(0, &p));
    /* TEST's and LIMIT RUN's SETTINGS are lists: their pages are the
     * list's. */
    for (int k = 0; k < 2; ++k) {
        fresh();
        open_run_settings(k);
        REQUIRE(servo_screen_list()->open);
        CHECK_EQ(scr->help_count(),
                 ui_setlist_help_count(servo_screen_list()));
    }
    CHECK(!scr->help_page(-1, &p));
    CHECK(!scr->help_page(0, NULL));

    const ui_screen_t *sup = supply_screen();
    fresh();
    sup->enter();
    CHECK_EQ(sup->help_count(), 3);
    supply_tap(SU_VAL_X, SU_V_ROW_Y);
    REQUIRE(supply_screen_keypad() != NULL && supply_screen_keypad()->open);
    CHECK_EQ(sup->help_count(), 0);
    CHECK(!sup->help_page(0, &p));
    CHECK(!sup->help_page(0, NULL));
}

/* LIVE names ARM on its READY page, KST the session's WRITE in the
 * footer's middle slot on its READY page, the last; SUPPLY names OUTPUT ON
 * and SETTINGS in its footer. */
TEST_CASE(the_labels_follow_what_the_controls_read)
{
    fresh();
    ui_help_page_t p;
    REQUIRE(ui_help_page(scr, false, 1, &p));
    CHECK_EQ(p.title, TX_HG_SV_TITLE_DRV_READY);
    CHECK_STR_EQ(p.item[3].label, "ARM");
    CHECK_EQ(p.item[3].text, TX_HG_SV_DRV_ARM);
    go(SERVO_FN_KST);
    REQUIRE(ui_help_page(scr, false, 2, &p));
    CHECK_EQ(p.title, TX_HG_SV_TITLE_PRG);
    CHECK_STR_EQ(p.item[3].label, TR(HG_SV_PRG_WRITE_NAME));
    CHECK_EQ(p.item[3].text, TX_HG_SV_PRG_WRITE);
    CHECK(!ui_help_page(scr, false, 3, &p));
    fresh();
    const ui_screen_t *sup = supply_screen();
    sup->enter();
    REQUIRE(ui_help_page(sup, false, 2, &p));
    CHECK_STR_EQ(p.item[p.count - 2].label, TR(SUP_OUTPUT_ON));
    CHECK_STR_EQ(p.item[p.count - 1].label, TR(FOOT_SETTINGS));
    /* The PD mini as the supply: the list's pages, its name SUPPLY. */
    supply_screen_set_model(false);
    supply_tap(SU_SETTINGS_X, SU_SETTINGS_Y);
    REQUIRE(ui_help_page(sup, false, 0, &p));
    CHECK_EQ(p.title, TX_HG_SL_TITLE_ROWS);
    CHECK_EQ(p.count, 3);
    CHECK_STR_EQ(p.item[1].label, "SUPPLY");
    pages_checked(sup, false, "SUPPLY SETTINGS, PD mini");
}

/* UI_HELP_AT_RIGHT: 4px inside the top-right corner at any size, and the
 * marks' key follows it; in the band the badge stays under it. */
TEST_CASE(a_badge_at_the_right_sits_in_the_top_right_corner)
{
    ui_help_page_t p;
    memset(&p, 0, sizeof(p));
    p.side = UI_HELP_DOCK_RIGHT;
    p.count = 1;
    p.item[0] = (ui_help_item_t){ "X", TX_HG_SV_LR_ENDS,
                                  gfx_rect_make(100, 100, 200, 40),
                                  UI_HELP_AT_LEFT };
    const uint32_t left = ui_help_marks_key(&p);
    /* 40px tall: left of it, centered. */
    gfx_rect_t b = ui_help_badge_rect(&p, 0);
    CHECK_EQ(b.x, 100 - 4 - 28);
    p.item[0].at = UI_HELP_AT_RIGHT;
    b = ui_help_badge_rect(&p, 0);
    CHECK_EQ(b.x, 100 + 200 - 4 - 28);
    CHECK_EQ(b.y, UI_BAND_H + 100 + 4);
    CHECK_EQ(b.w, 28);
    CHECK(ui_help_marks_key(&p) != left);
    p.item[0].rect = gfx_rect_make(300, 6 - UI_BAND_H, 100, 48);
    b = ui_help_badge_rect(&p, 0);
    CHECK_EQ(b.x, 300);
    CHECK_EQ(b.y, UI_BAND_H + 4);
}

/* The router narrows SERVO's clip to the live half: the parts SERVO
 * repaints on their own, the READY rows and the rail line here, draw
 * inside that clip, not over the dock beside it, frame after frame. */
TEST_CASE(servo_repaints_nothing_under_a_dock)
{
    bench_fresh();
    const gfx_color_t mark = 0x1234u;
    scr->render(&cv, 0);
    scr->render(&cv, 1);
    const gfx_rect_t live = ui_help_live_rect(UI_HELP_DOCK_RIGHT);
    for (int y = 0; y < UI_BODY_H; ++y) {
        for (int x = live.x + live.w; x < W; ++x) {
            fb[(size_t)y * W + x] = mark;
        }
    }
    const gfx_rect_t keep = cv.clip;
    REQUIRE(gfx_clip_intersect(&cv, (gfx_rect_t){ live.x, 0, live.w,
                                                  UI_BODY_H }));
    for (int f = 0; f < 6; ++f) {
        supply_state_t st;
        memset(&st, 0, sizeof(st));
        st.online = true;
        st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
        st.v = 5.0f + 0.1f * (float)f;
        st.i = 0.2f * (float)f;
        st.set_v = 6.0f + 0.1f * (float)f;
        st.set_i = 2.0f;
        st.samples = ++bench.samples;
        st.taken_ms = bench.now;
        st.stamped = true;
        servo_screen_supply(&st);
        bench_frames(100u);
        scr->render(&cv, f & 1);
    }
    cv.clip = keep;
    int drawn = 0;
    for (int y = 0; y < UI_BODY_H; ++y) {
        for (int x = live.x + live.w; x < W; ++x) {
            drawn += fb[(size_t)y * W + x] != mark;
        }
    }
    CHECK_EQ(drawn, 0);
}

/**
 * @brief The middle of @p r, tapped on SERVO's body.
 * @param r The rect, in the body.
 */
static void tap_rect(gfx_rect_t r)
{
    tap(r.x + r.w / 2, r.y + r.h / 2);
}

/**
 * @brief The item of @p p with the text @p text.
 * @param p    The page.
 * @param text The text.
 * @return Its index, -1 for none.
 */
static int item_of(const ui_help_page_t *p, ui_text_id_t text)
{
    for (int i = 0; i < p->count; ++i) {
        if (p->item[i].text == text) {
            return i;
        }
    }
    return -1;
}

/* The session's pages name each control as it reads now: KST's CONNECT,
 * READ ALL and TYPE, then FILES, MORE, the write switch, RESTORE and WRITE
 * n VALUES; HITEC's SERIES before CONNECT, MORE and AFTER STOP.  Each mark
 * is the control's rect, cut at the live half's edge. */
TEST_CASE(a_session_names_its_controls_as_they_read)
{
    fresh();
    go(SERVO_FN_KST);
    ui_help_page_t p;
    REQUIRE(ui_help_page(scr, false, 0, &p));
    CHECK_EQ(p.side, UI_HELP_DOCK_RIGHT);
    CHECK_EQ(p.item[0].text, TX_HG_PRG_TABS);
    CHECK_STR_EQ(p.item[1].label, TR(KP_CONNECT));
    CHECK_EQ(p.item[1].text, TX_HG_PRG_CONNECT);
    const gfx_rect_t conn = kst_panel_value_rect(0, 0, 0);
    const gfx_rect_t live = ui_help_live_rect(UI_HELP_DOCK_RIGHT);
    CHECK_EQ(p.item[1].rect.x, conn.x);
    CHECK_EQ(p.item[1].rect.x + p.item[1].rect.w, live.x + live.w);
    CHECK_STR_EQ(p.item[2].label, TR(KP_READ_ALL));
    CHECK_STR_EQ(p.item[3].label, TR(KP_TYPE));
    CHECK_EQ(p.item[3].text, TX_HG_KST_TYPE);
    CHECK_EQ(p.item[4].text, TX_HG_PRG_NOTES);
    REQUIRE(ui_help_page(scr, false, 1, &p));
    CHECK_STR_EQ(p.item[0].label, TR(KP_FILES));
    CHECK_STR_EQ(p.item[1].label, TR(KP_MORE));
    CHECK_STR_EQ(p.item[2].label, TR(KP_ENABLE));
    CHECK_STR_EQ(p.item[3].label, TR(KP_RESTORE));
    CHECK_STR_EQ(p.item[4].label, kst_panel_button(0, 4, 1));
    CHECK_EQ(p.item[4].text, TX_HG_PRG_WRITE);

    go(SERVO_FN_HITEC);
    REQUIRE(ui_help_page(scr, false, 0, &p));
    CHECK_EQ(p.title, TX_HG_HT_TITLE_CONNECT);
    CHECK_STR_EQ(p.item[1].label, hitec_panel_value(0, 0, 0));
    CHECK_STR_EQ(p.item[2].label, TR(HP_SERIES));
    CHECK_EQ(p.item[2].text, TX_HG_HT_SERIES);
    CHECK_EQ(p.item[3].text, TX_HG_PRG_READ);
    REQUIRE(ui_help_page(scr, false, 1, &p));
    CHECK_STR_EQ(p.item[0].label, TR(KP_MORE));
    CHECK_EQ(p.item[0].text, TX_HG_HT_MORE);
    CHECK_STR_EQ(p.item[1].label, TR(HP_STOP_ROW));
    CHECK_EQ(p.item[1].text, TX_HG_HT_CLEAR);
    CHECK_EQ(p.item[3].text, TX_HG_HT_RESTORE);
}

/* Another tab than SERVO: one page of its values, the dock on the right,
 * then the READY card's.  KST's TUNE marks its first value shown and, on a
 * stepped row, its keys; HITEC's TRAVEL marks MOVE SERVO apart from its
 * values. */
TEST_CASE(a_tab_of_values_offers_one_page_of_its_values)
{
    fresh();
    go(SERVO_FN_KST);
    tap_rect(kst_panel_tab_rect(1));
    REQUIRE(kst_panel_page() == 1);
    CHECK_EQ(pages_checked(scr, false, "SERVO KST TUNE"), 2);
    CHECK_EQ(title_of(scr, 0), TX_HG_KST_TITLE_VALUES);
    CHECK_EQ(title_of(scr, 1), TX_HG_SV_TITLE_PRG);
    ui_help_page_t p;
    REQUIRE(ui_help_page(scr, false, 0, &p));
    /* The first row without keys gives the value's mark; a stepped row,
     * its value right of its minus key, gives the keys'. */
    const char *first = NULL;
    bool stepped = false;
    for (int k = 0; k < 10; ++k) {
        const gfx_rect_t r = kst_panel_value_rect(1, k / 2, k % 2);
        if (r.w <= 0) {
            continue;
        }
        if (k % 2 == 0 && kst_panel_step_rect(1, k / 2, false).x < r.x) {
            stepped = true;
        } else if (first == NULL) {
            first = kst_panel_label(1, k / 2, k % 2);
        }
    }
    const int v = item_of(&p, TX_HG_PRG_VALUE);
    REQUIRE(v > 0 && first != NULL);
    CHECK_STR_EQ(p.item[v].label, first);
    CHECK_EQ(item_of(&p, TX_HG_PRG_STEP) >= 0, stepped);
    /* KST EXPERT MODE ON shows DEAD BAND, a stepped row, on TUNE. */
    settings_set(SET_KST_EXPERT, 1.0f);
    go(SERVO_FN_KST);
    tap_rect(kst_panel_tab_rect(1));
    CHECK_EQ(pages_checked(scr, false, "SERVO KST TUNE, expert"), 2);
    REQUIRE(ui_help_page(scr, false, 0, &p));
    const int step = item_of(&p, TX_HG_PRG_STEP);
    REQUIRE(step > 0);
    CHECK(gfx_rect_contains(p.item[step].rect,
                            kst_panel_step_rect(1, 0, false).x + 4,
                            kst_panel_step_rect(1, 0, false).y + 4));
    settings_set(SET_KST_EXPERT, 0.0f);

    go(SERVO_FN_HITEC);
    tap_rect(hitec_panel_tab_rect(HITEC_PANEL_TRAVEL));
    REQUIRE(hitec_panel_page() == HITEC_PANEL_TRAVEL);
    CHECK_EQ(pages_checked(scr, false, "SERVO HITEC TRAVEL"), 2);
    REQUIRE(ui_help_page(scr, false, 0, &p));
    CHECK_EQ(p.title, TX_HG_HT_TITLE_VALUES);
    const int mv = item_of(&p, TX_HG_HT_MOVE);
    CHECK_EQ(mv >= 0,
             hitec_panel_value_rect(HITEC_PANEL_TRAVEL, 4, 0).w > 0);
    if (mv >= 0) {
        CHECK_STR_EQ(p.item[mv].label,
                     hitec_panel_label(HITEC_PANEL_TRAVEL, 4, 0));
    }
    CHECK_STR_EQ(p.item[item_of(&p, TX_HG_PRG_VALUE)].label,
                 hitec_panel_label(HITEC_PANEL_TRAVEL, 0, 0));
}

int main(void)
{
    RUN(every_servo_page_fits_and_hides_no_stop);
    RUN(every_supply_page_fits_and_hides_no_stop);
    RUN(a_live_supply_output_takes_the_pages_that_cover_output_off);
    RUN(prog_5v_on_takes_the_pages_that_cover_prog_5v_off);
    RUN(a_run_takes_the_output_cards_pages_and_armed_leaves_none);
    RUN(a_panel_or_a_question_takes_every_page);
    RUN(the_labels_follow_what_the_controls_read);
    RUN(a_session_names_its_controls_as_they_read);
    RUN(a_tab_of_values_offers_one_page_of_its_values);
    RUN(a_badge_at_the_right_sits_in_the_top_right_corner);
    RUN(servo_repaints_nothing_under_a_dock);
    return test_summary("help_guides");
}
