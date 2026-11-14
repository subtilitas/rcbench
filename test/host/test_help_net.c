/**
 * @file test_help_net.c
 * @brief WI-FI's and UPDATES' screen guides: the pages each state offers,
 * that every page fits its dock and marks controls in the live half in
 * both languages and themes, SAFE and ARMED, that the labels follow what
 * the controls read, and that a level the guide does not explain leaves
 * no page.
 *
 * Cases:
 * - every_wifi_page_fits
 * - the_wifi_pages_name_the_buttons_as_they_read
 * - a_list_a_keyboard_or_the_question_leaves_wifi_no_page
 * - every_updates_page_fits
 * - the_updates_pages_follow_the_check_and_the_c6
 *
 * SPDX-License-Identifier: MIT
 */

#include "upd_rig.h"

#include "help_check.h"
#include "upd_dev.h"
#include "ui_help.h"
#include "wifi_spots.h"

/* A step the rest of a case cannot do without: names it and ends the case. */
#define REQUIRE(cond)                                                         \
    do {                                                                      \
        if (!(cond)) {                                                        \
            T_FAIL("expected %s", #cond);                                     \
            return;                                                           \
        }                                                                     \
    } while (0)

/* ------------------------------------------------------------- WI-FI */

/** @brief The view WI-FI is handed. */
static wifi_view_t s_wv;

/** @brief What wf_pose() shows: 0 the radio off, 1 the access point up,
 *         2 the same with Browser writes HOLD, 3 the network joined,
 *         4 the radio starting. */
static int s_wf_state;

/** @brief WI-FI's screen. @return It. */
static const ui_screen_t *wf(void) { return wifi_screen(); }

/** @brief WI-FI, fresh, the radio off, nothing kept. */
static void wf_fresh(void)
{
    ui_theme_set(UI_THEME_DARK);
    ui_text_set_language(UI_LANG_EN);
    wf()->reset();
    wf()->enter();
    memset(&s_wv, 0, sizeof(s_wv));
    s_wv.code_on = true;
    wifi_screen_set(&s_wv);
    (void)wifi_screen_take();
}

/**
 * @brief A tap of contact 1 at (@p x, @p y) of WI-FI's body.
 * @param x The body's x.
 * @param y The body's y.
 */
static void wf_tap(int x, int y)
{
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = (int16_t)x,
                                   .y = (int16_t)y, .strength = 40 } };
    wf()->event(&e);
    e.type = TOUCH_EVENT_UP;
    wf()->event(&e);
}

/** @brief WI-FI, fresh, in s_wf_state's state. */
static void wf_pose(void)
{
    wf_fresh();
    static const wifi_radio_t k_radio[] = {
        WIFI_RADIO_OFF, WIFI_RADIO_AP, WIFI_RADIO_AP, WIFI_RADIO_JOINED,
        WIFI_RADIO_STARTING,
    };
    s_wv.radio = k_radio[s_wf_state];
    snprintf(s_wv.name, sizeof(s_wv.name), "%s", "rcbench-3F7A");
    snprintf(s_wv.net, sizeof(s_wv.net), "%s", "Workshop 2.4");
    snprintf(s_wv.code, sizeof(s_wv.code), "%s", "r4tcw9pe");
    if (s_wf_state == 2) {
        s_wv.writes = WIFI_WRITES_HOLD;
    }
    wifi_screen_set(&s_wv);
}

/* Each state in both languages and themes, SAFE and ARMED, the link up
 * and down: three pages with the radio off, one with it on, two while the
 * writes' button stands. */
TEST_CASE(every_wifi_page_fits)
{
    static const struct {
        const char *what;
        int pages;
        int titles[3];
        int items[3];
    } k_states[] = {
        { "WI-FI off", 3,
          { TX_HG_WF_TITLE_START, TX_HG_WF_TITLE_KEPT, TX_HG_WF_TITLE_STATE },
          { 5, 2, 1 } },
        { "WI-FI access point", 1, { TX_HG_WF_TITLE_ON }, { 3 } },
        { "WI-FI writes HOLD", 2,
          { TX_HG_WF_TITLE_ON, TX_HG_WF_TITLE_WRITES }, { 3, 1 } },
        { "WI-FI joined", 1, { TX_HG_WF_TITLE_ON }, { 3 } },
        { "WI-FI starting", 1, { TX_HG_WF_TITLE_ON }, { 2 } },
    };
    for (int k = 0; k < (int)(sizeof(k_states) / sizeof(k_states[0])); ++k) {
        s_wf_state = k;
        CHECK_EQ(check_every_state(wf(), wf_pose, k_states[k].what),
                 k_states[k].pages);
        wf_pose();
        for (int n = 0; n < k_states[k].pages; ++n) {
            ui_help_page_t p;
            REQUIRE(ui_help_page(wf(), true, n, &p));
            CHECK_EQ((int)p.title, k_states[k].titles[n]);
            CHECK_EQ(p.count, k_states[k].items[n]);
        }
    }
}

/* The access code's, the kept values' and the writes' buttons are named as
 * they read now. */
TEST_CASE(the_wifi_pages_name_the_buttons_as_they_read)
{
    s_wf_state = 0;
    wf_pose();
    ui_help_page_t p;
    REQUIRE(ui_help_page(wf(), false, 0, &p));
    CHECK_STR_EQ(p.item[0].label, TR(WF_AP));
    CHECK_STR_EQ(p.item[4].label, TR(WF_CODE_ON));
    CHECK(gfx_rect_contains(p.item[4].rect, WF_BTN_CX(4), WF_BTN_CY(4)));
    REQUIRE(ui_help_page(wf(), false, 1, &p));
    CHECK_STR_EQ(p.item[0].label, TR(WF_KEPT_PASS_RANDOM));
    CHECK_STR_EQ(p.item[1].label, TR(WF_KEPT_CODE_RANDOM));
    s_wv.code_on = false;
    snprintf(s_wv.kept_pass, sizeof(s_wv.kept_pass), "%s", "kept-pass");
    snprintf(s_wv.kept_code, sizeof(s_wv.kept_code), "%s", "kept-code");
    wifi_screen_set(&s_wv);
    REQUIRE(ui_help_page(wf(), false, 0, &p));
    CHECK_STR_EQ(p.item[4].label, TR(WF_CODE_OFF));
    REQUIRE(ui_help_page(wf(), false, 1, &p));
    CHECK_STR_EQ(p.item[0].label, TR(WF_KEPT_PASS_SET));
    CHECK_STR_EQ(p.item[1].label, TR(WF_KEPT_CODE_SET));

    s_wf_state = 2;
    wf_pose();
    REQUIRE(ui_help_page(wf(), false, 0, &p));
    CHECK_STR_EQ(p.item[2].label, TR(WF_OFF));
    CHECK(gfx_rect_contains(p.item[2].rect, WF_OFF_CX, WF_OFF_CY));
    REQUIRE(ui_help_page(wf(), false, 1, &p));
    CHECK_STR_EQ(p.item[0].label, TR(WF_HOLD_WRITES));
    CHECK(gfx_rect_contains(p.item[0].rect, WF_WR_CX, WF_WR_CY));
    s_wv.writes_open = true;
    wifi_screen_set(&s_wv);
    REQUIRE(ui_help_page(wf(), false, 1, &p));
    CHECK_STR_EQ(p.item[0].label, TR(WF_WRITES_STOP));
}

/* The network list, the kept value's view and the question about an
 * uploaded image are levels the guide does not explain: no page, so no
 * "?". */
TEST_CASE(a_list_a_keyboard_or_the_question_leaves_wifi_no_page)
{
    s_wf_state = 0;
    wf_pose();
    REQUIRE(wf()->help_count() == 3);
    wf_tap(WF_BTN_CX(2), WF_BTN_CY(2));               /* CHOOSE: the list */
    REQUIRE(wf()->covered());
    CHECK_EQ(wf()->help_count(), 0);
    ui_help_page_t p;
    CHECK(!wf()->help_page(0, &p));

    wf_pose();
    wf_tap(WF_BTN_CX(5), WF_BTN_CY(5));      /* HOTSPOT PASSWORD's view */
    REQUIRE(wf()->covered());
    CHECK_EQ(wf()->help_count(), 0);

    wf_pose();
    s_wv.up = WIFI_UP_STAGED;
    snprintf(s_wv.new_ver, sizeof(s_wv.new_ver), "%s", "0.20.0");
    snprintf(s_wv.run_ver, sizeof(s_wv.run_ver), "%s", "0.19.0");
    wifi_screen_set(&s_wv);
    CHECK_EQ(wf()->help_count(), 0);
    CHECK(!wf()->help_page(0, NULL));
}

/* ----------------------------------------------------------- UPDATES */

/** @brief What upd_pose() shows: 0 as fresh, 1 a newer release found,
 *         2 the OPTIONS list open, 3 the wireless devices' page open. */
static int s_upd_state;

/** @brief UPDATES, fresh, in s_upd_state's state. */
static void upd_pose(void)
{
    fresh();
    if (s_upd_state == 1) {
        set_newer();
        push();
    }
    if (s_upd_state == 2) {
        tap(upd(), UP_SET_X, UP_SET_Y);
    }
    if (s_upd_state == 3) {
        tap(upd(), UPD_DEV_X + UPD_DEV_W / 2,
            UPD_DEV_BTN_Y + UPD_DEV_BTN_H / 2);
    }
}

/* Each state in both languages and themes, SAFE and ARMED: CHECK's page
 * and the card's, the download's hold on it once a check found a newer
 * release; over the OPTIONS list the list's two; over the wireless
 * devices' page its one, with the three rows and BACK. */
TEST_CASE(every_updates_page_fits)
{
    static const struct {
        const char *what;
        int pages;
        int titles[2];
        int items[2];
    } k_states[] = {
        { "UPDATES", 2, { TX_HG_UPD_TITLE_CHECK, TX_HG_UPD_TITLE_CARD },
          { 4, 3 } },
        { "UPDATES newer", 2,
          { TX_HG_UPD_TITLE_CHECK, TX_HG_UPD_TITLE_CARD }, { 4, 4 } },
        { "UPDATES OPTIONS", 2,
          { TX_HG_SL_TITLE_ROWS, TX_HG_SL_TITLE_VALUES }, { 3, 3 } },
        { "UPDATES devices", 1, { TX_HG_UPD_TITLE_DEVS, 0 }, { 4, 0 } },
    };
    for (int k = 0; k < (int)(sizeof(k_states) / sizeof(k_states[0])); ++k) {
        s_upd_state = k;
        CHECK_EQ(check_every_state(upd(), upd_pose, k_states[k].what),
                 k_states[k].pages);
        upd_pose();
        for (int n = 0; n < k_states[k].pages; ++n) {
            ui_help_page_t p;
            REQUIRE(ui_help_page(upd(), true, n, &p));
            CHECK_EQ((int)p.title, k_states[k].titles[n]);
            CHECK_EQ(p.count, k_states[k].items[n]);
        }
    }
}

/* CHECK is named CANCEL while a check runs; the download's hold is marked
 * while it stands; the C6 radio's hold is named as it reads. */
TEST_CASE(the_updates_pages_follow_the_check_and_the_c6)
{
    s_upd_state = 0;
    upd_pose();
    ui_help_page_t p;
    REQUIRE(ui_help_page(upd(), false, 0, &p));
    CHECK_STR_EQ(p.item[0].label, TR(UPD_CHECK));
    CHECK(gfx_rect_contains(p.item[0].rect, UP_BTN_X, UP_CHECK_Y));
    /* The IOmcu section ends above the WIRELESS DEVICES entry, which is
     * an item of its own around its heading, line and button. */
    CHECK_EQ(p.item[1].text, TX_HG_UPD_CU);
    CHECK(p.item[1].rect.y + p.item[1].rect.h < UPD_DEV_HEAD_Y);
    CHECK_EQ(p.item[3].text, TX_HG_UPD_DEVS);
    CHECK(gfx_rect_contains(p.item[3].rect, UPD_DEV_X + UPD_DEV_W / 2,
                            UPD_DEV_HEAD_Y));
    CHECK(gfx_rect_contains(p.item[3].rect, UPD_DEV_X + UPD_DEV_W / 2,
                            UPD_DEV_BTN_Y + UPD_DEV_BTN_H - 1));
    REQUIRE(ui_help_page(upd(), false, 1, &p));
    char c6[32];
    upd_c6_label(upd_c6_view(), c6, sizeof(c6));
    CHECK_STR_EQ(p.item[1].label, c6);
    CHECK_EQ(p.item[1].text, TX_HG_UPD_C6);
    CHECK_EQ(p.item[2].text, TX_HG_UPD_OPTIONS);

    g_v.job = UPD_JOB_CHECK;
    g_v.phase = UPD_PH_JOINING;
    push();
    REQUIRE(ui_help_page(upd(), false, 0, &p));
    CHECK_STR_EQ(p.item[0].label, TR(CANCEL));

    base_view();
    set_newer();
    push();
    REQUIRE(ui_help_page(upd(), false, 1, &p));
    CHECK_STR_EQ(p.item[2].label, TR(UPD_HOLD_DL));
    CHECK_EQ(p.item[2].text, TX_HG_UPD_DL);
    CHECK(!ui_help_page(upd(), false, 2, &p));

    /* The devices' page names each row by its heading, around its
     * heading, line and hold, then BACK. */
    s_upd_state = 3;
    upd_pose();
    REQUIRE(upd_dev_shown());
    REQUIRE(ui_help_page(upd(), false, 0, &p));
    static const int k_text[UPD_DEV_COUNT] = {
        TX_HG_UPD_DIAL, TX_HG_UPD_PNODE, TX_HG_UPD_BAL,
    };
    static const ui_text_id_t k_head[UPD_DEV_COUNT] = {
        TX_UPD_DIAL_HEAD, TX_UPD_PNODE_HEAD, TX_UPD_BAL_HEAD,
    };
    for (int i = 0; i < UPD_DEV_COUNT; ++i) {
        CHECK_STR_EQ(p.item[i].label, ui_tr(k_head[i]));
        CHECK_EQ((int)p.item[i].text, k_text[i]);
        /* Cut to the half the dock leaves: the hold whole. */
        const gfx_rect_t h = upd_dev_rect((upd_dev_id_t)i);
        CHECK(gfx_rect_contains(p.item[i].rect, h.x, h.y));
        CHECK(gfx_rect_contains(p.item[i].rect, h.x + h.w - 1,
                                h.y + h.h - 1));
    }
    CHECK_EQ(p.item[3].text, TX_HG_UPD_DEVS_BACK);
    CHECK(!ui_help_page(upd(), false, 1, &p));
    s_upd_state = 0;
}

int main(void)
{
    RUN(every_wifi_page_fits);
    RUN(the_wifi_pages_name_the_buttons_as_they_read);
    RUN(a_list_a_keyboard_or_the_question_leaves_wifi_no_page);
    RUN(every_updates_page_fits);
    RUN(the_updates_pages_follow_the_check_and_the_c6);
    return test_summary("help_net");
}
