/*
 * The settings backup through the glass and the driver (sbk_ctl.h,
 * sbk_screen.h): SETUP > SD CARD's SAVE SETTINGS and LOAD SETTINGS, the
 * automatic copy at each save to the store, the alert of a copy that does
 * not reach the card, the restore offer and its conditions, the 2s hold at
 * 1999 and 2000ms, the lines a load refuses, a load refused while an ARM
 * is queued or waits, a file whose first lines lie past the 256B the list
 * takes them from, and an edit that keeps the size and first lines.  The
 * card is a folder in memory (sbk_card_rig.h); the panel's card task is
 * sb_run() on it, called between frames.
 *
 * SPDX-License-Identifier: MIT
 */
#include "settings_rig.h"

#include "flash_gate.h"
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
    int n_changed = 0;
    CHECK_EQ(sbk_load_result(&n_changed)->refused_n, 0);
    CHECK_EQ(n_changed, 2);
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

/**
 * @brief One frame in panel_frame_loop()'s order around an ARM: the bench's
 *        ARMED taken at the frame's start, the router's tick, which queues
 *        an ARM (@p queued) the control task has not taken, or an arm that
 *        waits for the line (@p waits); then panel_sbk_frame() with ARMED
 *        as panel_frame.c passes it, arm_live_or_coming(): flash_gate_arm()
 *        of the snapshot and the ARM counts, read after the tick.
 */
static void frame_arm_under_way(bool queued, bool waits)
{
    const bool armed_now = g_feed.armed;     /* the frame's start */
    ui_router_tick(0.016f);
    const unsigned sent = queued ? 1u : 0u;  /* the ARM the tick queued */
    const bool armed = flash_gate_arm(armed_now, waits, sent, 0u);
    if (g_answered) {
        g_answered = false;
        sbk_done(&g_done, armed);
    }
    g_feed.armed = armed;
    sb_req_t q;
    if (sbk_tick(&g_feed, &q)) {
        sb_run(&k_io, &q, sbk_buffer(), sbk_buffer_size(), &g_done);
        g_answered = true;
        ++g_requests;
    }
    g_feed.armed = armed_now;
}

TEST_CASE(a_file_read_while_an_arm_is_queued_or_waits_is_not_put_in_force)
{
    /* 0: an ARM queued by the frame's own router tick; 1: an arm waiting
     * for the line; 2: neither, the limit at which the file loads. */
    for (int c = 0; c < 3; ++c) {
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
        /* SAFE at the frame's start; the answer is handed in after the
         * router's tick. */
        frame_arm_under_way(c == 0, c == 1);
        if (c < 2) {
            CHECK_EQ(sbk_load_state(), SBK_LOAD_FAILED);
            CHECK_EQ(sbk_load_why(NULL, NULL), SBK_WHY_ARMED);
            CHECK_FEQ(settings_get(SET_BRIGHTNESS), 60.0f);
            CHECK(!settings_dirty());
        } else {
            CHECK_EQ(sbk_load_state(), SBK_LOAD_DONE);
            CHECK_FEQ(settings_get(SET_BRIGHTNESS), 40.0f);
        }
    }
}

/** @brief Brightness in file @p name of the folder; -1 for none. */
static float file_brightness(const char *name)
{
    static sf_load_t l;
    const fake_file_t *f = find(name);
    if (f == NULL || sf_parse(f->data, f->len, &l, NULL) != SF_OK) {
        return -1.0f;
    }
    return l.values[SET_BRIGHTNESS];
}

TEST_CASE(a_copy_owed_while_backup_shows_waits_and_the_load_reads_the_file_shown)
{
    fake_fresh();
    put_backup("AUTO1.TXT", "2026-11-20 10:00:00 UTC");
    start(false, true);
    settle();
    sbk_screen_open(false);
    settle();
    tap_body(sbk_screen_row(0));
    CHECK_EQ(sbk_screen_chosen(), 0);
    /* A save to the store while BACKUP shows: its copy, of brightness 60,
     * waits; AUTO1.TXT is still the file of brightness 40 shown. */
    g_feed.committed = true;
    settle();
    CHECK(find("AUTO2.TXT") == NULL);
    CHECK_FEQ(file_brightness("AUTO1.TXT"), 40.0f);
    uint8_t n = 0u;
    CHECK_STR_EQ(sbk_files(&n)[0].head.written, "2026-11-20 10:00:00 UTC");
    /* The hold loads the file shown. */
    const gfx_rect_t hold = sbk_screen_part(SBK_PART_HOLD);
    press_body(hold, TOUCH_EVENT_DOWN);
    for (int i = 0; i < 9; ++i) {
        frame(0.25f);
    }
    press_body(hold, TOUCH_EVENT_UP);
    settle();
    CHECK_EQ(sbk_load_state(), SBK_LOAD_DONE);
    CHECK_STR_EQ(sbk_load_name(), "AUTO1.TXT");
    CHECK_FEQ(settings_get(SET_BRIGHTNESS), 40.0f);
    /* BACK: the copy goes, the copies move on, and the list is read
     * again with the new dates. */
    tap_body(sbk_screen_part(SBK_PART_BACK));
    CHECK(ui_router_current() != SCREEN_SET_LOAD);
    settle();
    CHECK_FEQ(file_brightness("AUTO1.TXT"), 60.0f);
    CHECK_FEQ(file_brightness("AUTO2.TXT"), 40.0f);
    const sb_entry_t *f = sbk_files(&n);
    CHECK_EQ(n, 2u);
    CHECK_STR_EQ(f[0].name, "AUTO1.TXT");
    CHECK_STR_EQ(f[0].head.written, "2026-11-21 14:05:33 UTC");
    CHECK_STR_EQ(f[1].name, "AUTO2.TXT");
    CHECK_STR_EQ(f[1].head.written, "2026-11-20 10:00:00 UTC");
}

TEST_CASE(a_list_that_changes_under_a_choice_drops_the_choice)
{
    fake_fresh();
    put_backup("AUTO1.TXT", "2026-11-20 10:00:00 UTC");
    start(false, true);
    settle();
    sbk_screen_open(false);
    settle();
    tap_body(sbk_screen_row(0));
    CHECK(sbk_screen_hold_ready());
    /* The same names and dates read again: the choice stays. */
    uint8_t n = 0u;
    static sb_entry_t before[SB_FILES];
    memcpy(before, sbk_files(&n), sizeof(before));
    sbk_screen_relisted(before, n);
    CHECK_EQ(sbk_screen_chosen(), 0);
    /* AUTO1.TXT of another date: no file chosen, no hold. */
    before[0].head.written[0] = '\0';
    sbk_screen_relisted(before, n);
    CHECK_EQ(sbk_screen_chosen(), -1);
    CHECK(!sbk_screen_hold_ready());
}

TEST_CASE(a_card_gone_before_the_offer_s_list_is_looked_at_again)
{
    fake_fresh();
    put_backup("AUTO1.TXT", "2026-11-20 10:00:00 UTC");
    start(true, false);
    settle();
    CHECK_EQ(sbk_offer_state(), SBK_OFFER_WAIT);
    /* Mounted for one frame: the list is asked for, and the card is gone
     * before the card task runs it. */
    g_feed.mounted = true;
    g_fake.mounted = false;
    frame(0.016f);
    CHECK(g_answered);
    /* The answer comes in the frame that sees no card. */
    g_feed.mounted = false;
    g_feed.committed = true;
    frame(0.016f);
    CHECK_EQ(sbk_offer_state(), SBK_OFFER_WAIT);
    settle();
    /* In again: the offer comes; no copy before it is answered. */
    g_fake.mounted = true;
    g_feed.mounted = true;
    g_feed.committed = true;
    settle();
    CHECK_EQ(ui_router_current(), SCREEN_SET_LOAD);
    CHECK(sbk_screen_offer());
    CHECK(find("AUTO2.TXT") == NULL);
    CHECK_FEQ(file_brightness("AUTO1.TXT"), 40.0f);
    /* SKIP: the owed copy goes. */
    tap_body(sbk_screen_part(SBK_PART_BACK));
    settle();
    CHECK_EQ(sbk_offer_state(), SBK_OFFER_DONE);
    CHECK_FEQ(file_brightness("AUTO2.TXT"), 40.0f);
}

TEST_CASE(a_card_gone_before_the_offer_s_read_is_looked_at_again)
{
    fake_fresh();
    put_backup("AUTO1.TXT", "2026-11-20 10:00:00 UTC");
    start(true, false);
    settle();
    /* The list works; the card is gone before the read. */
    g_feed.mounted = true;
    frame(0.016f);
    CHECK(g_answered);
    frame(0.016f);
    CHECK(g_answered);
    g_fake.mounted = false;
    sb_req_t q;
    memset(&q, 0, sizeof(q));
    q.op = SB_REQ_READ;
    (void)snprintf(q.name, sizeof(q.name), "AUTO1.TXT");
    sb_run(&k_io, &q, sbk_buffer(), sbk_buffer_size(), &g_done);
    g_feed.mounted = false;
    frame(0.016f);
    CHECK_EQ(sbk_offer_state(), SBK_OFFER_WAIT);
    g_fake.mounted = true;
    g_feed.mounted = true;
    settle();
    CHECK_EQ(ui_router_current(), SCREEN_SET_LOAD);
    CHECK(sbk_screen_offer());
}

/**
 * @brief File @p name of the folder as another panel would have written it
 *        again: brightness 80 and @p written in place of brightness 40 and
 *        its date.
 */
static void rewrite(const char *name, const char *written)
{
    fake_file_t *f = (fake_file_t *)find(name);
    REQUIRE_ALLOC(f);
    static sf_load_t l;
    if (sf_parse(f->data, f->len, &l, NULL) != SF_OK) {
        CHECK(false);
        return;
    }
    static char text[FAKE_BYTES];
    char *at = strstr(f->data, "\nbrightness = 40");
    REQUIRE_ALLOC(at);
    at[14] = '8';
    const char *old = strstr(f->data, l.head.written);
    REQUIRE_ALLOC(old);
    const size_t pre = (size_t)(old - f->data);
    (void)snprintf(text, sizeof(text), "%.*s%s%s", (int)pre, f->data,
                   written, old + strlen(l.head.written));
    f->len = strlen(text);
    memcpy(f->data, text, f->len + 1u);
}

/** @brief HOLD 2s TO LOAD held for 2250ms and let go. */
static void hold_load(void)
{
    const gfx_rect_t hold = sbk_screen_part(SBK_PART_HOLD);
    press_body(hold, TOUCH_EVENT_DOWN);
    for (int i = 0; i < 9; ++i) {
        frame(0.25f);
    }
    press_body(hold, TOUCH_EVENT_UP);
    settle();
}

/**
 * @brief A backup as put_backup() writes it, with comment lines of @p pad
 *        bytes in all between the header and `Panel:`: a file edited by
 *        hand, its first lines pushed towards or past the SB_HEAD_BYTES
 *        (256B) a list takes them from.
 */
static void put_padded(const char *name, const char *written, size_t pad)
{
    put_backup(name, written);
    fake_file_t *f = (fake_file_t *)find(name);
    REQUIRE_ALLOC(f);
    static char text[FAKE_BYTES];
    const char *nl = strchr(f->data, '\n');
    REQUIRE_ALLOC(nl);
    const size_t head = (size_t)(nl - f->data) + 1u;
    memcpy(text, f->data, head);
    size_t at = head;
    /* Lines of at most 40 bytes: '#', filler, '\n'. */
    for (size_t left = pad; left > 0u;) {
        const size_t line = (left > 40u) ? 40u : left;
        if (line == 1u) {
            text[at++] = '\n';
        } else {
            text[at++] = '#';
            memset(&text[at], 'x', line - 2u);
            at += line - 2u;
            text[at++] = '\n';
        }
        left -= line;
    }
    memcpy(&text[at], f->data + head, f->len - head + 1u);
    f->len += pad;
    memcpy(f->data, text, f->len + 1u);
}

TEST_CASE(a_file_with_its_first_lines_past_256_bytes_loads)
{
    static const char written[] = "2026-11-20 10:00:00 UTC";
    /* Where the `Written:` line lies in put_backup()'s file. */
    fake_fresh();
    put_backup("AUTO1.TXT", written);
    const fake_file_t *f = find("AUTO1.TXT");
    REQUIRE_ALLOC(f);
    const char *w = strstr(f->data, "Written:");
    REQUIRE_ALLOC(w);
    const size_t w_at = (size_t)(w - f->data);
    const size_t w_nl = (size_t)(strchr(w, '\n') - f->data);
    /* The line's end at byte 255 and at byte 256 of the file, its value cut
     * at byte 256, and the line starting at byte 256. */
    const struct {
        size_t      pad;
        const char *listed;
    } cases[] = {
        { SB_HEAD_BYTES - 1u - w_nl, written },
        { SB_HEAD_BYTES - w_nl,      written },
        { SB_HEAD_BYTES - w_at - 13u, "2026" },
        { SB_HEAD_BYTES - w_at,      "" },
    };
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        fake_fresh();
        put_padded("AUTO1.TXT", written, cases[i].pad);
        start(false, true);
        settle();
        sbk_screen_open(false);
        settle();
        uint8_t n = 0u;
        const sb_entry_t *e = sbk_files(&n);
        CHECK_EQ(n, 1u);
        CHECK_EQ(e[0].err, SF_OK);
        CHECK_STR_EQ(e[0].head.written, cases[i].listed);
        tap_body(sbk_screen_row(0));
        CHECK(sbk_screen_hold_ready());
        hold_load();
        CHECK_EQ(sbk_load_state(), SBK_LOAD_DONE);
        CHECK_FEQ(settings_get(SET_BRIGHTNESS), 40.0f);
        CHECK_STR_EQ(band(), "");
    }
}

/** @brief File @p name edited in place: brightness 40 to 80, the size and
 *         every first line kept, as an editor that leaves `Written:`. */
static void edit_in_place(const char *name)
{
    fake_file_t *f = (fake_file_t *)find(name);
    REQUIRE_ALLOC(f);
    char *at = strstr(f->data, "\nbrightness = 40");
    REQUIRE_ALLOC(at);
    at[14] = '8';
}

TEST_CASE(an_edit_that_keeps_the_size_and_first_lines_is_refused)
{
    /* 0: put_backup()'s file; 1: the same padded to SF_FILE_MAX bytes,
     * the largest file the list sums. */
    for (int c = 0; c < 2; ++c) {
        fake_fresh();
        put_backup("AUTO1.TXT", "2026-11-20 10:00:00 UTC");
        if (c == 1) {
            const size_t len = find("AUTO1.TXT")->len;
            put_padded("AUTO1.TXT", "2026-11-20 10:00:00 UTC",
                       SF_FILE_MAX - len);
            CHECK_EQ(find("AUTO1.TXT")->len, (size_t)SF_FILE_MAX);
        }
        start(false, true);
        settle();
        sbk_screen_open(false);
        settle();
        uint8_t n = 0u;
        CHECK(sbk_files(&n)[0].summed);
        tap_body(sbk_screen_row(0));
        CHECK(sbk_screen_hold_ready());
        edit_in_place("AUTO1.TXT");
        hold_load();
        CHECK(sbk_load_state() != SBK_LOAD_DONE);
        CHECK_FEQ(settings_get(SET_BRIGHTNESS), 60.0f);
        CHECK_STR_EQ(band(), ui_tr(TX_ALERT_SB_CHANGED));
        CHECK_EQ(sbk_screen_chosen(), -1);
        /* Chosen again from the new list: it loads, unchanged since. */
        ui_router_set_alert(NULL);
        tap_body(sbk_screen_row(0));
        hold_load();
        CHECK_EQ(sbk_load_state(), SBK_LOAD_DONE);
        CHECK_FEQ(settings_get(SET_BRIGHTNESS), 80.0f);
        CHECK_STR_EQ(band(), "");
    }
}

TEST_CASE(another_card_under_backup_loads_nothing_without_a_new_choice)
{
    fake_fresh();
    put_backup("AUTO1.TXT", "2026-11-20 10:00:00 UTC");
    start(false, true);
    settle();
    sbk_screen_open(false);
    settle();
    tap_body(sbk_screen_row(0));
    CHECK(sbk_screen_hold_ready());
    /* Swapped between two frames: the same name, brightness 80. */
    rewrite("AUTO1.TXT", "2026-11-21 09:00:00 UTC");
    g_feed.mount = 1u;
    frame(0.016f);
    CHECK_EQ(sbk_screen_chosen(), -1);
    CHECK(!sbk_screen_hold_ready());
    settle();
    uint8_t n = 0u;
    CHECK_EQ(sbk_list_state(), SBK_LIST_READY);
    CHECK_STR_EQ(sbk_files(&n)[0].head.written, "2026-11-21 09:00:00 UTC");
    hold_load();
    CHECK_EQ(sbk_load_state(), SBK_LOAD_IDLE);
    CHECK_FEQ(settings_get(SET_BRIGHTNESS), 60.0f);
    /* Pulled: no card shown, nothing chosen; in again: listed anew. */
    tap_body(sbk_screen_row(0));
    CHECK(sbk_screen_hold_ready());
    g_feed.mounted = false;
    frame(0.016f);
    CHECK_EQ(sbk_screen_chosen(), -1);
    CHECK_EQ(sbk_list_state(), SBK_LIST_READY);
    CHECK_EQ(sbk_list_code(), SB_NO_CARD);
    g_feed.mounted = true;
    g_feed.mount = 2u;
    settle();
    CHECK_EQ(sbk_list_state(), SBK_LIST_READY);
    CHECK_EQ(sbk_list_code(), SB_OK);
    CHECK_EQ(n, 1u);
}

TEST_CASE(a_file_written_again_after_the_choice_is_refused_and_listed_anew)
{
    fake_fresh();
    put_backup("AUTO1.TXT", "2026-11-20 10:00:00 UTC");
    start(false, true);
    settle();
    sbk_screen_open(false);
    settle();
    tap_body(sbk_screen_row(0));
    CHECK(sbk_screen_hold_ready());
    /* Written again on the same card, by a browser or another tool. */
    rewrite("AUTO1.TXT", "2026-11-21 09:00:00 UTC");
    hold_load();
    CHECK_FEQ(settings_get(SET_BRIGHTNESS), 60.0f);
    CHECK(sbk_load_state() != SBK_LOAD_DONE);
    CHECK_STR_EQ(band(), ui_tr(TX_ALERT_SB_CHANGED));
    CHECK_EQ(sbk_screen_chosen(), -1);
    uint8_t n = 0u;
    CHECK_EQ(sbk_list_state(), SBK_LIST_READY);
    CHECK_STR_EQ(sbk_files(&n)[0].head.written, "2026-11-21 09:00:00 UTC");
    /* Chosen again: it loads. */
    tap_body(sbk_screen_row(0));
    hold_load();
    CHECK_EQ(sbk_load_state(), SBK_LOAD_DONE);
    CHECK_FEQ(settings_get(SET_BRIGHTNESS), 80.0f);
}

/** @brief The name of the file BACKUP has chosen, "" for none. */
static const char *chosen_name(void)
{
    uint8_t n = 0u;
    const sb_entry_t *f = sbk_files(&n);
    const int i = sbk_screen_chosen();
    return (i >= 0 && i < (int)n) ? f[i].name : "";
}

TEST_CASE(an_offer_put_off_on_motor_comes_back_after_a_card_pull)
{
    fake_fresh();
    put_backup("AUTO1.TXT", "2026-11-20 10:00:00 UTC");
    start(true, true);
    ui_router_goto(SCREEN_MOTOR);
    settle();
    CHECK_EQ(sbk_offer_state(), SBK_OFFER_DUE);
    g_feed.mounted = false;
    settle();
    CHECK_EQ(sbk_offer_state(), SBK_OFFER_WAIT);
    g_feed.mounted = true;
    settle();
    CHECK_EQ(sbk_offer_state(), SBK_OFFER_DUE);
    ui_router_goto(SCREEN_OVERVIEW);
    settle();
    CHECK_EQ(ui_router_current(), SCREEN_SET_LOAD);
    CHECK(sbk_screen_offer());
    CHECK_STR_EQ(chosen_name(), "AUTO1.TXT");
    CHECK(sbk_screen_hold_ready());
    CHECK(find("AUTO2.TXT") == NULL);
}

TEST_CASE(a_shown_offer_says_no_card_and_comes_back_with_the_card)
{
    fake_fresh();
    put_backup("AUTO1.TXT", "2026-11-20 10:00:00 UTC");
    start(true, true);
    settle();
    CHECK_EQ(sbk_offer_state(), SBK_OFFER_SHOWN);
    g_feed.mounted = false;
    settle();
    CHECK_EQ(ui_router_current(), SCREEN_SET_LOAD);
    CHECK_EQ(sbk_offer_state(), SBK_OFFER_WAIT);
    CHECK_EQ(sbk_list_code(), SB_NO_CARD);
    CHECK(!sbk_screen_hold_ready());
    g_feed.mounted = true;
    settle();
    CHECK_EQ(sbk_offer_state(), SBK_OFFER_SHOWN);
    CHECK(sbk_screen_offer());
    CHECK_STR_EQ(chosen_name(), "AUTO1.TXT");
    CHECK(sbk_screen_hold_ready());
}

TEST_CASE(another_card_during_the_check_is_checked_itself)
{
    fake_fresh();
    put_backup("AUTO1.TXT", "2026-11-20 10:00:00 UTC");
    start(true, true);
    frame(0.016f);                  /* the list goes out */
    frame(0.016f);                  /* listed; the read of AUTO1 goes out */
    CHECK_EQ(sbk_offer_state(), SBK_OFFER_CHECKING);
    /* Another card, whose only file is SETTINGS.TXT. */
    CHECK_EQ(f_rename(NULL, "AUTO1.TXT", "SETTINGS.TXT"), SB_IO_OK);
    g_feed.mount = 1u;
    settle();
    CHECK_EQ(ui_router_current(), SCREEN_SET_LOAD);
    CHECK_STR_EQ(chosen_name(), "SETTINGS.TXT");
    CHECK(sbk_screen_hold_ready());
}

TEST_CASE(a_load_by_hand_answers_an_offer_not_yet_shown)
{
    fake_fresh();
    put_backup("AUTO1.TXT", "2026-11-20 10:00:00 UTC");
    start(true, true);
    ui_router_goto(SCREEN_SETUP);
    settle();
    CHECK_EQ(sbk_offer_state(), SBK_OFFER_DUE);
    sbk_screen_open(false);
    settle();
    tap_body(sbk_screen_row(0));
    hold_load();
    CHECK_EQ(sbk_load_state(), SBK_LOAD_DONE);
    CHECK_EQ(sbk_offer_state(), SBK_OFFER_DONE);
    tap_body(sbk_screen_part(SBK_PART_BACK));
    ui_router_goto(SCREEN_OVERVIEW);
    settle();
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
}

TEST_CASE(a_list_while_the_offer_waits_picks_its_file_again)
{
    fake_fresh();
    put_backup("AUTO1.TXT", "2026-11-19 10:00:00 UTC");
    put_backup("AUTO2.TXT", "2026-11-20 10:00:00 UTC");
    start(true, true);
    ui_router_goto(SCREEN_MOTOR);
    settle();
    CHECK_EQ(sbk_offer_state(), SBK_OFFER_DUE);
    CHECK_EQ(sbk_offer_index(), 1);
    /* AUTO1.TXT goes, through FILES or a browser; a list reads it. */
    CHECK_EQ(f_remove(NULL, "AUTO1.TXT"), SB_IO_OK);
    sbk_ask_list();
    settle();
    CHECK_EQ(sbk_offer_index(), 0);
    ui_router_goto(SCREEN_OVERVIEW);
    settle();
    CHECK_EQ(ui_router_current(), SCREEN_SET_LOAD);
    CHECK_STR_EQ(chosen_name(), "AUTO2.TXT");
    CHECK(sbk_screen_hold_ready());
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
    RUN(a_file_read_while_an_arm_is_queued_or_waits_is_not_put_in_force);
    RUN(a_file_with_its_first_lines_past_256_bytes_loads);
    RUN(an_edit_that_keeps_the_size_and_first_lines_is_refused);
    RUN(a_copy_owed_while_backup_shows_waits_and_the_load_reads_the_file_shown);
    RUN(a_list_that_changes_under_a_choice_drops_the_choice);
    RUN(a_card_gone_before_the_offer_s_list_is_looked_at_again);
    RUN(a_card_gone_before_the_offer_s_read_is_looked_at_again);
    RUN(another_card_under_backup_loads_nothing_without_a_new_choice);
    RUN(a_file_written_again_after_the_choice_is_refused_and_listed_anew);
    RUN(an_offer_put_off_on_motor_comes_back_after_a_card_pull);
    RUN(a_shown_offer_says_no_card_and_comes_back_with_the_card);
    RUN(another_card_during_the_check_is_checked_itself);
    RUN(a_load_by_hand_answers_an_offer_not_yet_shown);
    RUN(a_list_while_the_offer_waits_picks_its_file_again);
    return test_summary("sbk_screen");
}
