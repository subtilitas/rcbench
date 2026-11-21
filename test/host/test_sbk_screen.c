/*
 * The settings backup through the glass and the driver (sbk_ctl.h,
 * sbk_screen.h): SETUP > SD CARD's SAVE SETTINGS and LOAD SETTINGS, the
 * automatic copy at each save to the store, the alert of a copy that does
 * not reach the card, the restore offer and its conditions, the 2s hold at
 * 1999 and 2000ms, and the lines a load refuses.  The card is a folder in
 * memory (sbk_card_rig.h); the panel's card task is sb_run() on it, called
 * between frames.
 *
 * SPDX-License-Identifier: MIT
 */
#include "settings_rig.h"

#include "rcbench_version.h"
#include "sbk_card_rig.h"
#include "sbk_ctl.h"
#include "sbk_screen.h"
#include "settings_backup.h"
#include "settings_file.h"
#include "splash_screen.h"

/** @brief What the panel feeds the driver. */
static sbk_feed_t g_feed;
/** @brief Requests carried out. */
static int g_requests;

/** @brief An answer the "card task" has and the next frame takes. */
static sb_done_t g_done;
/** @brief Whether g_done waits for that frame. */
static bool g_answered;

/**
 * @brief One frame in panel_sbk_frame()'s order: the answer the card task
 *        finished since the last frame handed in first, with this frame's
 *        feed, then the tick and its request, which the card task carries
 *        out before the next frame; then the router's tick.
 */
static void frame(float dt_s)
{
    if (g_answered) {
        g_answered = false;
        sbk_done(&g_done, g_feed.armed);
    }
    sb_req_t q;
    if (sbk_tick(&g_feed, &q)) {
        sb_run(&k_io, &q, sbk_buffer(), sbk_buffer_size(), &g_done);
        g_answered = true;
        ++g_requests;
    }
    g_feed.committed = false;
    ui_router_tick(dt_s);
}

/** @brief Frames until nothing is under way, at most 20. */
static void settle(void)
{
    for (int i = 0; i < 20; ++i) {
        frame(0.016f);
    }
}

/** @brief A settings file of brightness 40 and the Dial "DIAL-0001" as
 *         @p name on the card, written by another panel. */
static void put_backup(const char *name, const char *written)
{
    static char text[SF_FILE_MAX + 1u];
    s_has_saved = false;
    s_has_text = false;
    settings_set_store(&s_text_store);
    settings_init();
    settings_set(SET_BRIGHTNESS, 40.0f);
    settings_set_text(SET_TEXT_DIAL_NAME, "DIAL-0001");
    CHECK(settings_save());
    const size_t n = sf_format(text, sizeof(text), "0.23.15", written);
    CHECK(n > 0u);
    CHECK_EQ(f_write(NULL, name, text, n), SB_IO_OK);
}

/** @brief A file of the defaults as @p name on the card. */
static void put_defaults(const char *name)
{
    static char text[SF_FILE_MAX + 1u];
    s_has_saved = false;
    s_has_text = false;
    settings_init();
    const size_t n = sf_format(text, sizeof(text), "0.23.15", "");
    CHECK_EQ(f_write(NULL, name, text, n), SB_IO_OK);
}

/** @brief The panel started: its store empty (@p defaults) or holding
 *         brightness 60, the router on OVERVIEW, a card in or not. */
static void start(bool defaults, bool card)
{
    if (!s_fb) {
        s_fb = calloc((size_t)W * H, sizeof(gfx_color_t));
        REQUIRE_ALLOC(s_fb);
    }
    gfx_canvas_init(&s_c, s_fb, W, H, W);
    s_has_saved = false;
    s_has_text = false;
    settings_set_observer(NULL);
    settings_set_text_observer(NULL);
    settings_set_store(&s_text_store);
    settings_init();
    if (!defaults) {
        settings_set(SET_BRIGHTNESS, 60.0f);
        CHECK(settings_save());
        settings_init();
    }
    sbk_init();
    memset(&g_feed, 0, sizeof(g_feed));
    g_feed.mounted = card;
    g_feed.wall_s = 1795269933;   /* 2026-11-21 14:05:33 UTC */
    g_requests = 0;
    g_answered = false;
    ui_text_set_language(UI_LANG_EN);
    ui_router_init();
    for (int i = 0; i < SPLASH_STEP_COUNT; ++i) {
        splash_screen_set((splash_step_t)i, SPLASH_OK, "");
    }
    ui_router_tick(2.0f);
    ui_router_goto(SCREEN_OVERVIEW);
    ui_router_set_alert(NULL);
}

/** @brief A body rectangle's middle, in panel coordinates, as a tap. */
static void tap_body(gfx_rect_t r)
{
    tap(r.x + r.w / 2, UI_BAND_H + r.y + r.h / 2);
}

/** @brief A press on a body rectangle's middle. */
static void press_body(gfx_rect_t r, touch_event_type_t type)
{
    touch_event_t e = { .type = type,
                        .point = { .id = 1, .x = (int16_t)(r.x + r.w / 2),
                                   .y = (int16_t)(UI_BAND_H + r.y
                                                  + r.h / 2) } };
    ui_router_event(&e);
}

/** @brief SD CARD's list, scrolled to its end. */
static void open_card_list(void)
{
    ui_router_goto(SCREEN_SETUP);
    open_category(SET_CAT_CARD);
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = 300, .y = 400 } };
    ui_router_event(&e);
    for (int y = 390; y >= 250; y -= 10) {
        e.type = TOUCH_EVENT_MOVE;
        e.point.y = (int16_t)y;
        ui_router_event(&e);
    }
    e.type = TOUCH_EVENT_UP;
    ui_router_event(&e);
}

/** @brief SD CARD's row @p i of the backup's two, panel coordinates. */
static void tap_card_row(int i)
{
    const int rows = settings_screen_rows(SET_CAT_CARD);
    const gfx_rect_t r = settings_screen_row_rect(rows - SBK_SETUP_ROWS + i);
    tap(r.x + r.w / 3, UI_BAND_H + r.y + r.h / 2);
}

/** @brief A custom row's value text. */
static const char *row_value(int i)
{
    static char buf[64];
    sbk_setup_row(i)->value_text(buf, sizeof(buf));
    return buf;
}

/** @brief The band's alert, "" with none. */
static const char *band(void)
{
    const char *a = ui_router_alert();
    return (a != NULL) ? a : "";
}

/* ------------------------------------------------------------ SAVE SETTINGS */

TEST_CASE(save_settings_writes_the_stored_settings_and_says_so)
{
    fake_fresh();
    start(false, true);
    settle();
    open_card_list();
    CHECK_EQ(settings_screen_rows(SET_CAT_CARD), 7);
    CHECK_STR_EQ(row_value(0), "");
    tap_card_row(0);
    CHECK_STR_EQ(row_value(0), "WRITING");
    settle();
    CHECK_STR_EQ(row_value(0), "WRITTEN");
    const fake_file_t *f = find(SB_SAVED);
    REQUIRE_ALLOC(f);
    static sf_load_t l;
    CHECK_EQ(sf_parse(f->data, f->len, &l, NULL), SF_OK);
    CHECK_FEQ(l.values[SET_BRIGHTNESS], 60.0f);
    CHECK_STR_EQ(l.head.written, "2026-11-21 14:05:33 UTC");
    CHECK_STR_EQ(l.head.version, RCBENCH_VERSION_STRING);
    /* An edit not saved: the row takes no tap and says why. */
    settings_set(SET_BRIGHTNESS, 80.0f);
    CHECK(sbk_setup_row(0)->locked() != NULL);
    CHECK_STR_EQ(sbk_setup_row(0)->locked(), ui_tr(TX_SBK_LOCK_UNSAVED));
    const unsigned writes = g_fake.writes;
    tap_card_row(0);
    settle();
    CHECK_EQ(g_fake.writes, writes);
    /* No card: locked, and nothing asked. */
    (void)settings_discard();
    g_feed.mounted = false;
    settle();
    CHECK_STR_EQ(sbk_setup_row(0)->locked(), ui_tr(TX_SBK_LOCK_NO_CARD));
    /* A full card. */
    g_feed.mounted = true;
    settle();
    g_fake.room = used_bytes();
    tap_card_row(0);
    settle();
    CHECK_EQ(sbk_save_state(), SBK_SAVE_FAILED);
    CHECK_STR_EQ(row_value(0), "CARD FULL");
    g_fake.room = 100000u;
    g_fake.deny = true;
    tap_card_row(0);
    settle();
    CHECK_STR_EQ(row_value(0), "FAILED, CODE 3");
    /* A failed SAVE SETTINGS is the row's: the band stays clear. */
    CHECK_STR_EQ(band(), "");
}

/* --------------------------------------------------------- the automatic copy */

TEST_CASE(each_save_to_the_store_with_a_card_writes_a_copy_and_rotates)
{
    fake_fresh();
    start(false, true);
    settle();
    for (int i = 1; i <= 4; ++i) {
        settings_set(SET_BRIGHTNESS, 20.0f + 10.0f * (float)i);
        CHECK(settings_save());
        g_feed.committed = true;
        settle();
        static sf_load_t l;
        const fake_file_t *f = find("AUTO1.TXT");
        REQUIRE_ALLOC(f);
        CHECK_EQ(sf_parse(f->data, f->len, &l, NULL), SF_OK);
        CHECK_FEQ(l.values[SET_BRIGHTNESS], 20.0f + 10.0f * (float)i);
    }
    CHECK(find("AUTO3.TXT") != NULL);
    CHECK(find("AUTO4.TXT") == NULL);
    static sf_load_t l3;
    const fake_file_t *f3 = find("AUTO3.TXT");
    CHECK_EQ(sf_parse(f3->data, f3->len, &l3, NULL), SF_OK);
    CHECK_FEQ(l3.values[SET_BRIGHTNESS], 40.0f);
    /* No card at the save: no copy, and none owed for the next card. */
    const unsigned writes = g_fake.writes;
    g_feed.mounted = false;
    g_feed.committed = true;
    settle();
    g_feed.mounted = true;
    settle();
    CHECK_EQ(g_fake.writes, writes);
    /* A frame without a save writes nothing. */
    settle();
    CHECK_EQ(g_fake.writes, writes);
}

TEST_CASE(a_copy_that_does_not_reach_the_card_is_said_once)
{
    fake_fresh();
    start(false, true);
    settle();
    g_fake.room = used_bytes();
    g_feed.committed = true;
    settle();
    CHECK_STR_EQ(band(), ui_tr(TX_ALERT_SB_FULL));
    /* The second failure: not said again. */
    ui_router_set_alert(NULL);
    g_feed.committed = true;
    settle();
    CHECK_STR_EQ(band(), "");
    /* A copy that works, then a card that takes no file: said, with its
     * code. */
    g_fake.room = 100000u;
    g_feed.committed = true;
    settle();
    CHECK(find("AUTO1.TXT") != NULL);
    g_fake.deny = true;
    g_feed.committed = true;
    settle();
    char want[96];
    snprintf(want, sizeof(want), ui_tr(TX_ALERT_SB_FAILED), 3u);
    CHECK_STR_EQ(band(), want);
    /* It is no error of the bench: the screen and the router go on, the
     * copies there are stay. */
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    CHECK(find("AUTO1.TXT") != NULL);
    ui_text_set_language(UI_LANG_DE);
    CHECK_STR_EQ(ui_tr(TX_ALERT_SB_FULL),
                 "Einstellungskopie nicht geschrieben: Die Karte ist voll.");
    ui_text_set_language(UI_LANG_EN);
}

/* ---------------------------------------------------------- the restore offer */

TEST_CASE(the_offer_comes_for_a_panel_on_defaults_and_a_card_with_other_settings)
{
    fake_fresh();
    put_backup("AUTO1.TXT", "2026-11-20 10:00:00 UTC");
    start(true, true);
    CHECK_EQ(sbk_offer_state(), SBK_OFFER_WAIT);
    settle();
    CHECK_EQ(ui_router_current(), SCREEN_SET_LOAD);
    CHECK(sbk_screen_offer());
    CHECK_EQ(sbk_offer_state(), SBK_OFFER_SHOWN);
    CHECK_EQ(sbk_screen_chosen(), 0);
    CHECK(sbk_screen_hold_ready());
    ui_router_render(&s_c, 0);
}

TEST_CASE(no_offer_when_the_panel_holds_settings_or_the_card_none_other)
{
    /* The panel holds its own settings. */
    fake_fresh();
    put_backup("AUTO1.TXT", "x");
    start(false, true);
    settle();
    CHECK_EQ(sbk_offer_state(), SBK_OFFER_DONE);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    /* The card's file holds the defaults too. */
    fake_fresh();
    put_defaults("AUTO1.TXT");
    start(true, true);
    settle();
    CHECK_EQ(sbk_offer_state(), SBK_OFFER_DONE);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    /* No file. */
    fake_fresh();
    start(true, true);
    settle();
    CHECK_EQ(sbk_offer_state(), SBK_OFFER_DONE);
    /* A file that is no settings file. */
    fake_fresh();
    CHECK_EQ(f_write(NULL, "AUTO1.TXT", "hello\n", 6u), SB_IO_OK);
    start(true, true);
    settle();
    CHECK_EQ(sbk_offer_state(), SBK_OFFER_DONE);
}

TEST_CASE(the_offer_waits_for_a_card_overview_and_a_safe_bench)
{
    fake_fresh();
    put_backup("AUTO2.TXT", "x");
    CHECK_EQ(f_write(NULL, "AUTO1.TXT", "cut", 3u), SB_IO_OK);
    start(true, false);
    settle();
    CHECK_EQ(sbk_offer_state(), SBK_OFFER_WAIT);
    CHECK_EQ(g_requests, 0);
    /* ARMED: due, not shown. */
    g_feed.armed = true;
    g_feed.mounted = true;
    settle();
    CHECK_EQ(sbk_offer_state(), SBK_OFFER_DUE);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    /* SAFE, on another screen: still waits. */
    g_feed.armed = false;
    ui_router_goto(SCREEN_MOTOR);
    settle();
    CHECK_EQ(sbk_offer_state(), SBK_OFFER_DUE);
    /* Back on OVERVIEW: shown, the readable copy chosen. */
    ui_router_goto(SCREEN_OVERVIEW);
    settle();
    CHECK_EQ(ui_router_current(), SCREEN_SET_LOAD);
    CHECK_EQ(sbk_screen_chosen(), sbk_offer_index());
    CHECK_EQ(sbk_offer_index(), 1);
    /* A save of other settings meanwhile ends an offer not yet shown. */
    fake_fresh();
    put_backup("AUTO1.TXT", "x");
    start(true, true);
    g_feed.armed = true;
    settle();
    CHECK_EQ(sbk_offer_state(), SBK_OFFER_DUE);
    settings_set(SET_CONTRAST, 80.0f);
    CHECK(settings_save());
    settle();
    CHECK_EQ(sbk_offer_state(), SBK_OFFER_DONE);
}

TEST_CASE(no_copy_is_written_until_the_offer_is_answered)
{
    fake_fresh();
    put_backup("AUTO1.TXT", "x");
    start(true, true);
    g_feed.armed = true;
    settle();
    CHECK_EQ(sbk_offer_state(), SBK_OFFER_DUE);
    CHECK(!sbk_auto_allowed());
    const unsigned writes = g_fake.writes;
    g_feed.committed = true;
    settle();
    CHECK_EQ(g_fake.writes, writes);
    /* SKIP answers it: the copy owed goes out, AUTO1 moved on. */
    g_feed.armed = false;
    settle();
    CHECK_EQ(ui_router_current(), SCREEN_SET_LOAD);
    tap_body(sbk_screen_part(SBK_PART_BACK));
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    CHECK_EQ(sbk_offer_state(), SBK_OFFER_DONE);
    settle();
    CHECK(g_fake.writes > writes);
    static sf_load_t l;
    const fake_file_t *f2 = find("AUTO2.TXT");
    REQUIRE_ALLOC(f2);
    CHECK_EQ(sf_parse(f2->data, f2->len, &l, NULL), SF_OK);
    CHECK_FEQ(l.values[SET_BRIGHTNESS], 40.0f);
}

/* -------------------------------------------------------- LOAD SETTINGS */

TEST_CASE(the_hold_loads_at_2000ms_and_not_at_1999ms)
{
    fake_fresh();
    put_backup("AUTO1.TXT", "2026-11-20 10:00:00 UTC");
    start(false, true);
    settle();
    open_card_list();
    tap_card_row(1);
    CHECK_EQ(ui_router_current(), SCREEN_SET_LOAD);
    CHECK(!sbk_screen_offer());
    settle();
    CHECK_EQ(sbk_list_state(), SBK_LIST_READY);
    /* No file chosen: the hold takes no press. */
    CHECK(!sbk_screen_hold_ready());
    tap_body(sbk_screen_row(0));
    CHECK_EQ(sbk_screen_chosen(), 0);
    CHECK(sbk_screen_hold_ready());
    /* Let go at 1999ms: the hint, no load. */
    const gfx_rect_t hold = sbk_screen_part(SBK_PART_HOLD);
    press_body(hold, TOUCH_EVENT_DOWN);
    for (int i = 0; i < 7; ++i) {
        frame(0.25f);
    }
    frame(0.249f);
    CHECK_EQ(sbk_load_state(), SBK_LOAD_IDLE);
    press_body(hold, TOUCH_EVENT_UP);
    CHECK_STR_EQ(band(), ui_tr(TX_HOLD_HINT_SBK_LOAD));
    CHECK_FEQ(settings_get(SET_BRIGHTNESS), 60.0f);
    /* Held to 2000ms: loaded. */
    ui_router_set_alert(NULL);
    press_body(hold, TOUCH_EVENT_DOWN);
    for (int i = 0; i < 7; ++i) {
        frame(0.25f);
    }
    frame(0.249f);
    CHECK_EQ(sbk_load_state(), SBK_LOAD_IDLE);
    frame(0.001f);
    CHECK(sbk_load_state() != SBK_LOAD_IDLE);
    press_body(hold, TOUCH_EVENT_UP);
    settle();
    CHECK_EQ(sbk_load_state(), SBK_LOAD_DONE);
    CHECK_FEQ(settings_get(SET_BRIGHTNESS), 40.0f);
    CHECK_STR_EQ(settings_text(SET_TEXT_DIAL_NAME), "DIAL-0001");
    CHECK_STR_EQ(sbk_load_name(), "AUTO1.TXT");
    int changed = 0;
    CHECK_EQ(sbk_load_result(&changed)->refused_n, 0);
    CHECK_EQ(changed, 2);
    /* The save to the store is asked for. */
    CHECK(settings_save_asked());
    ui_router_render(&s_c, 0);
    /* BACK returns to SD CARD's list. */
    tap_body(sbk_screen_part(SBK_PART_BACK));
    CHECK_EQ(ui_router_current(), SCREEN_SETUP);
    CHECK_EQ(settings_screen_category(), SET_CAT_CARD);
}

TEST_CASE(a_load_lists_each_refused_line_and_a_cut_file_loads_nothing)
{
    fake_fresh();
    static const char text[] =
        "rcbench settings, format 1\nPanel: 0.23.15\nWritten: not known\n"
        "brightness = 80\ntheme = PURPLE\nno_key = 1\nbrightness = 90\n"
        "end\n";
    CHECK_EQ(f_write(NULL, SB_SAVED, text, sizeof(text) - 1u), SB_IO_OK);
    CHECK_EQ(f_write(NULL, "AUTO1.TXT", text, sizeof(text) - 6u), SB_IO_OK);
    start(false, true);
    settle();
    sbk_screen_open(false);
    settle();
    uint8_t n = 0u;
    const sb_entry_t *files = sbk_files(&n);
    CHECK_EQ(n, 2);
    CHECK_STR_EQ(files[1].name, SB_SAVED);
    sbk_ask_load(1);
    settle();
    CHECK_EQ(sbk_load_state(), SBK_LOAD_DONE);
    const sf_load_t *l = sbk_load_result(NULL);
    CHECK_EQ(l->refused_n, 3);
    CHECK_EQ(l->refused[0].line, 5);
    CHECK_EQ(l->refused[0].code, SF_LINE_VALUE);
    CHECK_EQ(l->refused[1].line, 6);
    CHECK_EQ(l->refused[1].code, SF_LINE_KEY);
    CHECK_EQ(l->refused[2].line, 7);
    CHECK_EQ(l->refused[2].code, SF_LINE_TWICE);
    CHECK_FEQ(settings_get(SET_BRIGHTNESS), 80.0f);
    ui_router_render(&s_c, 0);
    /* The cut copy: refused whole, the settings as they were. */
    sbk_load_clear();
    sbk_ask_load(0);
    settle();
    CHECK_EQ(sbk_load_state(), SBK_LOAD_FAILED);
    uint8_t code = 0u;
    unsigned line = 0u;
    CHECK_EQ(sbk_load_why(&code, &line), SBK_WHY_FILE);
    CHECK_EQ(code, SF_ERR_TRUNCATED);
    CHECK_EQ(line, 7u);
    CHECK_FEQ(settings_get(SET_BRIGHTNESS), 80.0f);
    ui_router_render(&s_c, 0);
}

TEST_CASE(armed_or_no_card_loads_nothing)
{
    fake_fresh();
    put_backup("AUTO1.TXT", "x");
    start(false, true);
    settle();
    sbk_screen_open(false);
    settle();
    tap_body(sbk_screen_row(0));
    CHECK(sbk_screen_hold_ready());
    /* ARMED: no hold, and the row of SETUP takes no tap. */
    g_feed.armed = true;
    settle();
    CHECK(!sbk_screen_hold_ready());
    CHECK_STR_EQ(sbk_setup_row(1)->locked(), ui_tr(TX_SBK_LOCK_ARMED));
    /* A file that arrives while ARMED is not put in force. */
    g_feed.armed = false;
    settle();
    sbk_ask_load(0);
    g_feed.armed = true;
    settle();
    CHECK_EQ(sbk_load_state(), SBK_LOAD_FAILED);
    CHECK_EQ(sbk_load_why(NULL, NULL), SBK_WHY_ARMED);
    CHECK_FEQ(settings_get(SET_BRIGHTNESS), 60.0f);
    /* No card: the list says so. */
    g_feed.armed = false;
    g_feed.mounted = false;
    sbk_screen_open(false);
    ui_router_goto(SCREEN_OVERVIEW);
    sbk_screen_open(false);
    settle();
    CHECK_EQ(sbk_list_state(), SBK_LIST_READY);
    CHECK_EQ(sbk_list_code(), SB_NO_CARD);
    CHECK(!sbk_screen_hold_ready());
    ui_router_render(&s_c, 0);
    /* No file to name: nothing asked. */
    sbk_ask_load(0);
    CHECK_EQ(sbk_load_state(), SBK_LOAD_IDLE);
}

TEST_CASE(a_file_read_in_the_frame_that_arms_is_not_put_in_force)
{
    fake_fresh();
    put_backup("AUTO1.TXT", "x");
    start(false, true);
    settle();
    sbk_screen_open(false);
    settle();
    /* SAFE when the read is handed out; the card task reads the file. */
    sbk_ask_load(0);
    frame(0.016f);
    CHECK(g_answered);
    /* The frame that hands the answer in is the first to see ARMED. */
    g_feed.armed = true;
    frame(0.016f);
    CHECK_EQ(sbk_load_state(), SBK_LOAD_FAILED);
    CHECK_EQ(sbk_load_why(NULL, NULL), SBK_WHY_ARMED);
    CHECK_FEQ(settings_get(SET_BRIGHTNESS), 60.0f);
    CHECK(!settings_dirty());
}

int main(void)
{
    RUN(save_settings_writes_the_stored_settings_and_says_so);
    RUN(each_save_to_the_store_with_a_card_writes_a_copy_and_rotates);
    RUN(a_copy_that_does_not_reach_the_card_is_said_once);
    RUN(the_offer_comes_for_a_panel_on_defaults_and_a_card_with_other_settings);
    RUN(no_offer_when_the_panel_holds_settings_or_the_card_none_other);
    RUN(the_offer_waits_for_a_card_overview_and_a_safe_bench);
    RUN(no_copy_is_written_until_the_offer_is_answered);
    RUN(the_hold_loads_at_2000ms_and_not_at_1999ms);
    RUN(a_load_lists_each_refused_line_and_a_cut_file_loads_nothing);
    RUN(armed_or_no_card_loads_nothing);
    RUN(a_file_read_in_the_frame_that_arms_is_not_put_in_force);
    return test_summary("sbk_screen");
}
