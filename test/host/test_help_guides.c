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
 * - a_badge_at_the_right_sits_in_the_top_right_corner
 * - servo_repaints_nothing_under_a_dock
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_bench_rig.h"

#include "help_check.h"
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
    int tab_x;          /**< the strip's tab, 0 for LIVE                 */
    int page_x;         /**< SETTINGS' page tab, 0 for none              */
    bool autobox;       /**< RUDDER with AUTO's box                      */
    int safe;           /**< the pages offered SAFE                      */
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
    if (st->tab_x > 0) {
        tap(st->tab_x, SV_TAB_Y);
    }
    if (st->page_x > 0) {
        tap(st->page_x, SV_PAGE_TAB_Y);
    }
    if (st->autobox) {
        tap(SV_RUD_AUTO_X, SV_RUD_ZERO_Y);
    }
}

/** @brief SERVO's states with pages: each task and SETTINGS' pages. */
static const servo_state_t k_servo[] = {
    { "LIVE", 0, 0, false, 3,
      { TX_HG_SV_TITLE_LIVE, TX_HG_SV_TITLE_CARD, TX_HG_SV_TITLE_DRIVE },
      { 4, 4, 3 } },
    { "TEST", SV_TAB_TEST_X, 0, false, 3,
      { TX_HG_SV_TITLE_TEST, TX_HG_SV_TITLE_CARD, TX_HG_SV_TITLE_DRIVE },
      { 4, 4, 3 } },
    { "PROGRAM", SV_TAB_PROGRAM_X, 0, false, 3,
      { TX_HG_SV_TITLE_PROG, TX_HG_SV_TITLE_CARD, TX_HG_SV_TITLE_DRIVE },
      { 1, 4, 3 } },
    { "RUDDER", SV_TAB_RUDDER_X, 0, false, 4,
      { TX_HG_SV_TITLE_RUDDER, TX_HG_SV_TITLE_RUD_READ, TX_HG_SV_TITLE_CARD, TX_HG_SV_TITLE_DRIVE },
      { 5, 3, 4, 4 } },
    { "AUTO", SV_TAB_RUDDER_X, 0, true, 4,
      { TX_HG_SV_TITLE_AUTO, TX_HG_SV_TITLE_RUD_READ, TX_HG_SV_TITLE_CARD, TX_HG_SV_TITLE_DRIVE },
      { 4, 2, 4, 4 } },
    { "OUTPUT", SV_TAB_SETTINGS_X, 0, false, 4,
      { TX_HG_SV_TITLE_OUTPUT, TX_HG_SV_TITLE_OUTPUT_2, TX_HG_SV_TITLE_CARD, TX_HG_SV_TITLE_DRIVE },
      { 5, 4, 4, 3 } },
    { "SWEEP", SV_TAB_SETTINGS_X, SV_PAGE_X(1), false, 3,
      { TX_HG_SV_TITLE_SWEEP, TX_HG_SV_TITLE_CARD, TX_HG_SV_TITLE_DRIVE },
      { 3, 4, 3 } },
    { "DUT", SV_TAB_SETTINGS_X, SV_PAGE_X(2), false, 3,
      { TX_HG_SV_TITLE_DUT, TX_HG_SV_TITLE_CARD, TX_HG_SV_TITLE_DRIVE },
      { 5, 4, 3 } },
};

/** @brief The state pose_servo() puts SERVO in. */
static const servo_state_t *s_servo = &k_servo[0];

/** @brief SERVO, fresh, in s_servo's state. */
static void pose_servo(void)
{
    servo_to(s_servo);
}

/** @brief The tabs pose_supply() taps: 0 LIVE, 1 SETTINGS, 2 PD MINI. */
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

/** @brief SUPPLY, fresh, on the task s_supply_tabs names. */
static void pose_supply(void)
{
    fresh();
    supply_screen()->enter();
    if (s_supply_tabs > 0) {
        supply_tap(SU_TAB_X(1), SU_TAB_Y);
    }
    if (s_supply_tabs > 1) {
        supply_tap(SU_OV_PDMINI_X, SU_OV_TAB_Y);
    }
}

/* ----------------------------------------------------------------- cases */

/* Each SERVO state in both languages and themes, SAFE and ARMED, the link
 * up and down: its own pages, then the output card's two; ARMED the output
 * card's two alone, with the note, DISARM named. */
TEST_CASE(every_servo_page_fits_and_hides_no_stop)
{
    for (size_t k = 0; k < sizeof(k_servo) / sizeof(k_servo[0]); ++k) {
        const servo_state_t *st = &k_servo[k];
        s_servo = st;
        char name[32];
        snprintf(name, sizeof(name), "SERVO %s", st->what);
        CHECK_EQ(check_every_state(servo_screen(), pose_servo, name),
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
                CHECK_EQ(pages_checked(scr, true, what), 2);
                CHECK(ui_help_armed_note(scr, true));
                ui_help_page_t p;
                CHECK(ui_help_page(scr, true, 1, &p));
                CHECK_STR_EQ(p.item[2].label, "DISARM");
            }
        }
    }
    ui_text_set_language(UI_LANG_EN);
    ui_theme_set(UI_THEME_DARK);
}

/* SUPPLY's LIVE, LIMITS and PD MINI with the output off, both languages and
 * themes, SAFE and ARMED, the link up and down: the task's page or pages,
 * then the rail's two. */
TEST_CASE(every_supply_page_fits_and_hides_no_stop)
{
    const ui_screen_t *sup = supply_screen();
    static const struct {
        const char *what;
        int tabs;           /* taps: 0 LIVE, 1 SETTINGS, 2 PD MINI */
        int safe, armed;
        int titles[4], items[4];
    } k_states[] = {
        { "LIVE", 0, 3, 2,
          { TX_HG_SU_TITLE_LIVE, TX_HG_SU_TITLE_READ, TX_HG_SU_TITLE_CONTROL },
          { 4, 5, 3 } },
        { "LIMITS", 1, 4, 3,
          { TX_HG_SU_TITLE_LIMITS, TX_HG_SU_TITLE_TRIPS, TX_HG_SU_TITLE_READ,
            TX_HG_SU_TITLE_CONTROL },
          { 4, 5, 5, 2 } },
        { "PD MINI", 2, 3, 2,
          { TX_HG_SU_TITLE_PDMINI, TX_HG_SU_TITLE_READ,
            TX_HG_SU_TITLE_CONTROL },
          { 4, 5, 2 } },
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

/* OUTPUT OFF shows on SERVO's SET row and SUPPLY's rail while the output
 * is on or an ON is on its way: the pages with the dock on the right are
 * not offered then, and come back once it is off. */
TEST_CASE(a_live_supply_output_takes_the_pages_that_cover_output_off)
{
    fresh();
    CHECK_EQ(scr->help_count(), 3);
    supply_screen_set_output(true);
    supply_state_t st = *supply_screen_reading();
    st.online = true;
    st.output = true;
    supply_screen_push(&st);
    REQUIRE(supply_screen_output_on());
    CHECK_EQ(pages_checked(scr, false, "SERVO LIVE, output on"), 2);
    CHECK_EQ(title_of(scr, 0), TX_HG_SV_TITLE_CARD);
    const ui_screen_t *sup = supply_screen();
    CHECK_EQ(pages_checked(sup, false, "SUPPLY LIVE, output on"), 2);
    CHECK_EQ(title_of(sup, 0), TX_HG_SU_TITLE_READ);
    ui_help_page_t p;
    CHECK(ui_help_page(sup, false, 1, &p));
    CHECK_STR_EQ(p.item[p.count - 1].label, TR(SUP_OUTPUT_OFF));
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
 * output off: while the supply reads ON it is a safe control on SERVO and
 * SUPPLY, and the pages with the dock on the right over it are not
 * offered.  At OFF they come back and PROG 5V ON is no safe control. */
TEST_CASE(prog_5v_on_takes_the_pages_that_cover_prog_5v_off)
{
    prog5v_box_reset();
    prog5v_box_publish(P5_ON, 21u, (uint8_t)PROG5V_HELD_NONE);
    fresh();
    REQUIRE(!supply_screen_output_live());
    CHECK(safe_points(scr) > 0);
    CHECK_EQ(pages_checked(scr, false, "SERVO LIVE, PROG 5V on"), 2);
    CHECK_EQ(title_of(scr, 0), TX_HG_SV_TITLE_CARD);
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
 * page at all, so the band shows no "?".  After it, they come back. */
TEST_CASE(a_run_takes_the_output_cards_pages_and_armed_leaves_none)
{
    bench_fresh();
    short_runs();
    CHECK_EQ(ui_help_count(scr, true), 2);
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

/* A keypad, a run's settings and the supply's question are levels the guide
 * does not explain: no page, so no "?". */
TEST_CASE(a_panel_or_a_question_takes_every_page)
{
    fresh();
    tap(SV_SUP_V_X, SV_SUP_Y);
    REQUIRE(servo_screen_keypad() != NULL && servo_screen_keypad()->open);
    CHECK_EQ(scr->help_count(), 0);
    ui_help_page_t p;
    CHECK(!scr->help_page(0, &p));
    fresh();
    tap(SV_TAB_TEST_X, SV_TAB_Y);
    tap(SV_RUN_SET_X, SV_RUN_SET_Y(0));
    CHECK_EQ(scr->help_count(), 0);
    tap(SV_RUN_CLOSE_X, SV_RUN_CLOSE_Y);
    CHECK_EQ(scr->help_count(), 3);
    CHECK(!scr->help_page(3, &p));
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

/* ARM reads DISARM while ARMED, OUTPUT ON OUTPUT OFF while on, and on a
 * board that switches the ports' 5V PROG 5V stands where the plot is. */
TEST_CASE(the_labels_follow_what_the_controls_read)
{
    fresh();
    ui_help_page_t p;
    REQUIRE(ui_help_page(scr, false, 2, &p));
    CHECK_STR_EQ(p.item[2].label, "ARM");
    REQUIRE(ui_help_page(scr, false, 1, &p));
    CHECK_EQ(p.item[1].text, TX_HG_SV_PLOT);
    prog5v_box_reset();
    prog5v_box_publish(P5_OFF, 21u, (uint8_t)PROG5V_HELD_NONE);
    fresh();
    REQUIRE(ui_help_page(scr, false, 1, &p));
    CHECK_STR_EQ(p.item[1].label, "PROG 5V");
    CHECK_EQ(p.item[1].text, TX_HG_SV_P5);
    pages_checked(scr, false, "SERVO LIVE, PROG 5V");
    prog5v_box_reset();
    fresh();
    const ui_screen_t *sup = supply_screen();
    sup->enter();
    REQUIRE(ui_help_page(sup, false, 2, &p));
    CHECK_STR_EQ(p.item[p.count - 1].label, TR(SUP_OUTPUT_ON));
    /* The PD mini as the supply: RESET PD MINI on LIMITS' page. */
    supply_screen_set_model(false);
    supply_tap(SU_TAB_X(1), SU_TAB_Y);
    REQUIRE(ui_help_page(sup, false, 0, &p));
    CHECK_EQ(p.title, TX_HG_SU_TITLE_LIMITS);
    CHECK_EQ(p.count, 4);
    CHECK_EQ(p.item[3].text, TX_HG_SU_RESET_PD);
    pages_checked(sup, false, "SUPPLY LIMITS, PD mini");
}

/* UI_HELP_AT_RIGHT: 4px inside the top-right corner at any size, and the
 * marks' key follows it; in the band the badge stays under it. */
TEST_CASE(a_badge_at_the_right_sits_in_the_top_right_corner)
{
    ui_help_page_t p;
    memset(&p, 0, sizeof(p));
    p.side = UI_HELP_DOCK_RIGHT;
    p.count = 1;
    p.item[0] = (ui_help_item_t){ "X", TX_HG_SV_TASKS,
                                  gfx_rect_make(100, 100, 200, 40),
                                  UI_HELP_AT_LEFT };
    const uint32_t left = ui_help_marks_key(&p);
    /* 40px tall: left of it, centred. */
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
 * repaints on their own, the SET row and the rail's plot here, draw
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

int main(void)
{
    RUN(every_servo_page_fits_and_hides_no_stop);
    RUN(every_supply_page_fits_and_hides_no_stop);
    RUN(a_live_supply_output_takes_the_pages_that_cover_output_off);
    RUN(prog_5v_on_takes_the_pages_that_cover_prog_5v_off);
    RUN(a_run_takes_the_output_cards_pages_and_armed_leaves_none);
    RUN(a_panel_or_a_question_takes_every_page);
    RUN(the_labels_follow_what_the_controls_read);
    RUN(a_badge_at_the_right_sits_in_the_top_right_corner);
    RUN(servo_repaints_nothing_under_a_dock);
    return test_summary("help_guides");
}
