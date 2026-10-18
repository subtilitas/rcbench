/*
 * SETUP > UPDATES, the start-up reminder and the download's progress: what
 * each button asks for and when it may, the hold, the reminder's answers
 * across a restart, when it opens and when it yields, the start-up check's
 * window across a millis wrap, every result's text, and every view inside
 * its canvas in both languages.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "greatest.h"
#include "guard_canvas.h"

#include "fw_release.h"
#include "settings.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "upd_screen.h"
#include "wa_fetch.h"
#include "wifi_screen.h"

/* Mirrored from upd_screen.c, in the body's coordinates. */
#define BTN_X     196
#define CHECK_Y   44
#define REMIND_Y  112
#define DL_Y      180
#define R_IGNORE_X 244
#define R_NEVER_X  556
#define R_ROW_Y    276
#define R_HOLD_X   400
#define R_HOLD_Y   352
/* PANEL UPDATE's one button, on the whole panel. */
#define U_BTN_X   144
#define U_BTN_Y   434

#define TICK_S     (1.0f / 39.0f)
#define HOLD_TICKS ((int)(UI_HOLD_S / TICK_S) + 5)

/* ------------------------------------------------- a store that survives */

static float g_kept[SETTING_COUNT];
static bool  g_have_kept;
static int   g_saves;

static bool mem_load(float *values, int count)
{
    if (!g_have_kept) {
        return false;
    }
    memcpy(values, g_kept, (size_t)count * sizeof(float));
    return true;
}

static bool mem_save(const float *values, int count)
{
    memcpy(g_kept, values, (size_t)count * sizeof(float));
    g_have_kept = true;
    ++g_saves;
    return true;
}

static const settings_store_t k_store = { mem_load, mem_save, NULL, NULL };

/* ---------------------------------------------------------------- helpers */

static const ui_screen_t *upd(void) { return upd_screen(); }
static const ui_screen_t *rem(void) { return upd_remind_screen(); }

static upd_view_t g_v;

static void base_view(void)
{
    memset(&g_v, 0, sizeof(g_v));
    g_v.have_net = true;
    snprintf(g_v.net, sizeof(g_v.net), "%s", "workshop");
    snprintf(g_v.running, sizeof(g_v.running), "%s", "0.19.0");
}

static void set_newer(void)
{
    g_v.job = UPD_JOB_CHECK;
    g_v.phase = UPD_PH_ENDED;
    g_v.got = UPD_GOT_NEWER;
    snprintf(g_v.found, sizeof(g_v.found), "%s", "0.20.0");
}

static void push(void) { upd_screen_set(&g_v); }

static void fresh(void)
{
    ui_theme_set(UI_THEME_DARK);
    ui_text_set_language(UI_LANG_EN);
    g_have_kept = false;
    g_saves = 0;
    settings_set_store(&k_store);
    settings_init();
    ui_router_init();
    upd()->reset();
    rem()->reset();
    base_view();
    push();
    (void)upd_screen_take();
}

static void ev(const ui_screen_t *scr, touch_event_type_t type, uint8_t id,
               int x, int y, uint8_t flags)
{
    touch_event_t e = { .type = type, .flags = flags,
                        .point = { .id = id, .x = (int16_t)x,
                                   .y = (int16_t)y, .strength = 40 } };
    scr->event(&e);
}

static void tap(const ui_screen_t *scr, int x, int y)
{
    ev(scr, TOUCH_EVENT_DOWN, 1, x, y, 0);
    ev(scr, TOUCH_EVENT_UP, 1, x, y, 0);
}

static void ticks(const ui_screen_t *scr, int n)
{
    for (int i = 0; i < n; ++i) {
        scr->tick(TICK_S);
    }
}

static void router_tap(int x, int y)
{
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = (int16_t)x,
                                   .y = (int16_t)y, .strength = 40 } };
    ui_router_event(&e);
    e.type = TOUCH_EVENT_UP;
    ui_router_event(&e);
}

/* ------------------------------------------------------------ the mirrors */

TEST_CASE(the_enums_mirror_the_fetch_and_the_release_parser)
{
    CHECK_EQ((int)UPD_JOB_CHECK, (int)WA_JOB_CHECK);
    CHECK_EQ((int)UPD_JOB_DOWNLOAD, (int)WA_JOB_DOWNLOAD);

    CHECK_EQ((int)UPD_PH_IDLE, (int)WA_PH_IDLE);
    CHECK_EQ((int)UPD_PH_JOINING, (int)WA_PH_JOINING);
    CHECK_EQ((int)UPD_PH_ASKING, (int)WA_PH_ASKING);
    CHECK_EQ((int)UPD_PH_SUMS, (int)WA_PH_SUMS);
    CHECK_EQ((int)UPD_PH_IMAGE, (int)WA_PH_IMAGE);
    CHECK_EQ((int)UPD_PH_CHECKING, (int)WA_PH_CHECKING);
    CHECK_EQ((int)UPD_PH_ENDED, (int)WA_PH_ENDED);

    CHECK_EQ((int)UPD_GOT_NONE, (int)WA_GOT_NONE);
    CHECK_EQ((int)UPD_GOT_NEWER, (int)WA_GOT_NEWER);
    CHECK_EQ((int)UPD_GOT_CURRENT, (int)WA_GOT_CURRENT);
    CHECK_EQ((int)UPD_GOT_STAGED, (int)WA_GOT_STAGED);
    CHECK_EQ((int)UPD_GOT_NO_NET, (int)WA_GOT_NO_NET);
    CHECK_EQ((int)UPD_GOT_BLOCKED, (int)WA_GOT_BLOCKED);
    CHECK_EQ((int)UPD_GOT_ABORTED, (int)WA_GOT_ABORTED);
    CHECK_EQ((int)UPD_GOT_JOIN, (int)WA_GOT_JOIN);
    CHECK_EQ((int)UPD_GOT_RADIO, (int)WA_GOT_RADIO);
    CHECK_EQ((int)UPD_GOT_TIMEOUT, (int)WA_GOT_TIMEOUT);
    CHECK_EQ((int)UPD_GOT_CONNECT, (int)WA_GOT_CONNECT);
    CHECK_EQ((int)UPD_GOT_REFUSED, (int)WA_GOT_REFUSED);
    CHECK_EQ((int)UPD_GOT_UNEXPECTED, (int)WA_GOT_UNEXPECTED);
    CHECK_EQ((int)UPD_GOT_SUMS, (int)WA_GOT_SUMS);
    CHECK_EQ((int)UPD_GOT_HASH, (int)WA_GOT_HASH);
    CHECK_EQ((int)UPD_GOT_IMAGE, (int)WA_GOT_IMAGE);
    CHECK_EQ((int)UPD_GOT_MEMORY, (int)WA_GOT_MEMORY);

    CHECK_EQ((int)UPD_WHY_NONE, (int)WA_WHY_NONE);
    CHECK_EQ((int)UPD_WHY_ARMED, (int)WA_WHY_ARMED);
    CHECK_EQ((int)UPD_WHY_SUPPLY, (int)WA_WHY_SUPPLY);
    CHECK_EQ((int)UPD_WHY_RUN, (int)WA_WHY_RUN);
    CHECK_EQ((int)UPD_WHY_STOP, (int)WA_WHY_STOP);
    CHECK_EQ((int)UPD_WHY_FILES, (int)WA_WHY_FILES);
    CHECK_EQ((int)UPD_WHY_COPROC, (int)WA_WHY_COPROC);
    CHECK_EQ((int)UPD_WHY_INSTALL, (int)WA_WHY_INSTALL);
    CHECK_EQ((int)UPD_WHY_CANCEL, (int)WA_WHY_CANCEL);
    CHECK_EQ((int)UPD_WHY_BUSY, (int)WA_WHY_BUSY);
    CHECK_EQ((int)UPD_WHY_DRAWING, (int)WA_WHY_DRAWING);

    CHECK_EQ((int)UPD_REL_OK, (int)FW_REL_OK);
    CHECK_EQ((int)UPD_REL_MISSING, (int)FW_REL_MISSING);
    CHECK_EQ((int)UPD_REL_LONG, (int)FW_REL_LONG);
    CHECK_EQ((int)UPD_REL_CHAR, (int)FW_REL_CHAR);
    CHECK_EQ((int)UPD_REL_PLACE, (int)FW_REL_PLACE);
    CHECK_EQ((int)UPD_REL_VERSION, (int)FW_REL_VERSION);
    CHECK_EQ((int)UPD_REL_STATUS, (int)FW_REL_STATUS);
    CHECK_EQ((int)UPD_REL_HOPS, (int)FW_REL_HOPS);
    CHECK_EQ((int)UPD_REL_HOST, (int)FW_REL_HOST);
    CHECK_EQ((int)UPD_REL_NO_LENGTH, (int)FW_REL_NO_LENGTH);
    CHECK_EQ((int)UPD_REL_EMPTY, (int)FW_REL_EMPTY);
    CHECK_EQ((int)UPD_REL_TOO_BIG, (int)FW_REL_TOO_BIG);
    CHECK_EQ((int)UPD_REL_SHORT, (int)FW_REL_SHORT);
    CHECK_EQ((int)UPD_REL_OVERRUN, (int)FW_REL_OVERRUN);
    CHECK_EQ((int)UPD_REL_SUMS_BAD, (int)FW_REL_SUMS_BAD);
    CHECK_EQ((int)UPD_REL_SUMS_NONE, (int)FW_REL_SUMS_NONE);
    CHECK_EQ((int)UPD_REL_SUMS_TWICE, (int)FW_REL_SUMS_TWICE);
    CHECK_EQ((int)UPD_REL_SUMS_TWICE + 1, (int)FW_REL_COUNT);
}

/* -------------------------------------------------------------- UPDATES */

TEST_CASE(check_asks_once_with_a_home_network_and_the_gate_open)
{
    fresh();
    tap(upd(), BTN_X, CHECK_Y);
    CHECK_EQ(upd_screen_take(), UPD_REQ_CHECK);
    CHECK_EQ(upd_screen_take(), 0u);

    /* A release off the button, or a press that was no tap, asks nothing. */
    ev(upd(), TOUCH_EVENT_DOWN, 1, BTN_X, CHECK_Y, 0);
    ev(upd(), TOUCH_EVENT_UP, 1, BTN_X, CHECK_Y + 200, 0);
    ev(upd(), TOUCH_EVENT_DOWN, 1, BTN_X, CHECK_Y, 0);
    ev(upd(), TOUCH_EVENT_UP, 1, BTN_X, CHECK_Y, TOUCH_FLAG_NO_TAP);
    CHECK_EQ(upd_screen_take(), 0u);
}

TEST_CASE(check_is_grey_without_a_network_wifi_or_the_gate)
{
    static const upd_why_t k_blocks[] = {
        UPD_WHY_ARMED, UPD_WHY_SUPPLY, UPD_WHY_RUN, UPD_WHY_COPROC,
        UPD_WHY_INSTALL, UPD_WHY_BUSY,
    };
    fresh();
    g_v.have_net = false;
    push();
    tap(upd(), BTN_X, CHECK_Y);
    CHECK_EQ(upd_screen_take(), 0u);

    fresh();
    g_v.unavailable = true;
    push();
    tap(upd(), BTN_X, CHECK_Y);
    CHECK_EQ(upd_screen_take(), 0u);

    for (size_t i = 0; i < sizeof(k_blocks) / sizeof(k_blocks[0]); ++i) {
        fresh();
        g_v.block = k_blocks[i];
        push();
        tap(upd(), BTN_X, CHECK_Y);
        CHECK_EQ(upd_screen_take(), 0u);
    }
}

TEST_CASE(a_running_check_turns_the_button_into_cancel)
{
    static const upd_phase_t k_busy[] = { UPD_PH_JOINING, UPD_PH_ASKING };
    for (size_t i = 0; i < 2; ++i) {
        fresh();
        g_v.job = UPD_JOB_CHECK;
        g_v.phase = k_busy[i];
        g_v.left_s = 21u;
        /* The gate's own BUSY does not grey CANCEL. */
        g_v.block = UPD_WHY_BUSY;
        push();
        CHECK(upd_busy(&g_v));
        tap(upd(), BTN_X, CHECK_Y);
        CHECK_EQ(upd_screen_take(), UPD_REQ_CANCEL);
    }
    CHECK(!upd_busy(NULL));
}

TEST_CASE(the_reminder_switch_flips_the_setting_and_asks_for_the_save)
{
    fresh();
    CHECK(settings_get_bool(SET_UPD_REMIND));    /* on by default */
    tap(upd(), BTN_X, REMIND_Y);
    CHECK(!settings_get_bool(SET_UPD_REMIND));
    CHECK(settings_save_asked());
    CHECK(settings_save_tick(true));
    tap(upd(), BTN_X, REMIND_Y);
    CHECK(settings_get_bool(SET_UPD_REMIND));
    CHECK(settings_save_tick(true));
    /* It asks the application for nothing: the setting is the request. */
    CHECK_EQ(upd_screen_take(), 0u);
    /* And it works without a home network: turning reminders back on
     * needs none. */
    g_v.have_net = false;
    push();
    tap(upd(), BTN_X, REMIND_Y);
    CHECK(!settings_get_bool(SET_UPD_REMIND));
}

TEST_CASE(the_download_hold_is_there_only_for_a_newer_release)
{
    fresh();
    /* No result: the row is empty and holds nothing. */
    ev(upd(), TOUCH_EVENT_DOWN, 1, BTN_X, DL_Y, 0);
    ticks(upd(), HOLD_TICKS);
    ev(upd(), TOUCH_EVENT_UP, 1, BTN_X, DL_Y, 0);
    CHECK_EQ(upd_screen_take(), 0u);

    /* Up to date: none either. */
    g_v.job = UPD_JOB_CHECK;
    g_v.phase = UPD_PH_ENDED;
    g_v.got = UPD_GOT_CURRENT;
    push();
    ev(upd(), TOUCH_EVENT_DOWN, 1, BTN_X, DL_Y, 0);
    ticks(upd(), HOLD_TICKS);
    ev(upd(), TOUCH_EVENT_UP, 1, BTN_X, DL_Y, 0);
    CHECK_EQ(upd_screen_take(), 0u);

    /* Newer: the whole hold asks once; a tap or half of it nothing. */
    set_newer();
    push();
    tap(upd(), BTN_X, DL_Y);
    ticks(upd(), HOLD_TICKS);
    CHECK_EQ(upd_screen_take(), 0u);
    ev(upd(), TOUCH_EVENT_DOWN, 1, BTN_X, DL_Y, 0);
    ticks(upd(), HOLD_TICKS / 2);
    ev(upd(), TOUCH_EVENT_UP, 1, BTN_X, DL_Y, 0);
    ticks(upd(), HOLD_TICKS);
    CHECK_EQ(upd_screen_take(), 0u);
    ev(upd(), TOUCH_EVENT_DOWN, 1, BTN_X, DL_Y, 0);
    ticks(upd(), HOLD_TICKS);
    CHECK_EQ(upd_screen_take(), UPD_REQ_DOWNLOAD);
    ticks(upd(), HOLD_TICKS);
    CHECK_EQ(upd_screen_take(), 0u);
    ev(upd(), TOUCH_EVENT_UP, 1, BTN_X, DL_Y, 0);
}

TEST_CASE(the_download_hold_ends_unfired_when_the_gate_closes_under_it)
{
    static const upd_why_t k_blocks[] = {
        UPD_WHY_ARMED, UPD_WHY_SUPPLY, UPD_WHY_RUN, UPD_WHY_COPROC,
        UPD_WHY_DRAWING, UPD_WHY_INSTALL,
    };
    for (size_t i = 0; i < sizeof(k_blocks) / sizeof(k_blocks[0]); ++i) {
        fresh();
        set_newer();
        push();
        ev(upd(), TOUCH_EVENT_DOWN, 1, BTN_X, DL_Y, 0);
        ticks(upd(), HOLD_TICKS / 2);
        g_v.dl_block = k_blocks[i];
        push();
        ticks(upd(), HOLD_TICKS);
        /* Open again under the same finger: the hold does not resume. */
        g_v.dl_block = UPD_WHY_NONE;
        push();
        ticks(upd(), HOLD_TICKS);
        ev(upd(), TOUCH_EVENT_UP, 1, BTN_X, DL_Y, 0);
        CHECK_EQ(upd_screen_take(), 0u);
    }
    /* Sliding off abandons it too; and a lost gesture. */
    fresh();
    set_newer();
    push();
    ev(upd(), TOUCH_EVENT_DOWN, 1, BTN_X, DL_Y, 0);
    ticks(upd(), HOLD_TICKS / 2);
    ev(upd(), TOUCH_EVENT_MOVE, 1, BTN_X, DL_Y + 120, 0);
    ev(upd(), TOUCH_EVENT_MOVE, 1, BTN_X, DL_Y, 0);
    ticks(upd(), HOLD_TICKS);
    CHECK_EQ(upd_screen_take(), 0u);
    ev(upd(), TOUCH_EVENT_UP, 1, BTN_X, DL_Y, 0);
    ev(upd(), TOUCH_EVENT_DOWN, 1, BTN_X, DL_Y, 0);
    ticks(upd(), HOLD_TICKS / 2);
    upd()->cancel();
    ticks(upd(), HOLD_TICKS);
    CHECK_EQ(upd_screen_take(), 0u);
}

/* ---------------------------------------------------------- the texts */

TEST_CASE(every_result_has_a_text_in_both_languages)
{
    char a[96], b[96];
    for (int lang = 0; lang < 2; ++lang) {
        ui_text_set_language(lang == 0 ? UI_LANG_EN : UI_LANG_DE);
        base_view();
        g_v.phase = UPD_PH_ENDED;
        for (int got = UPD_GOT_NEWER; got <= UPD_GOT_MEMORY; ++got) {
            for (int rel = 0; rel <= UPD_REL_SUMS_TWICE; ++rel) {
                for (int why = 0; why <= UPD_WHY_DRAWING; ++why) {
                    for (int job = 0; job < 2; ++job) {
                        g_v.got = (upd_got_t)got;
                        g_v.rel = (upd_rel_t)rel;
                        g_v.why = (upd_why_t)why;
                        g_v.job = (upd_job_t)job;
                        g_v.status = 404;
                        g_v.budget_s = 30u;
                        upd_result_text(&g_v, a, sizeof(a), b, sizeof(b));
                        CHECK(a[0] != '\0');
                        CHECK(strlen(a) < sizeof(a));
                        CHECK(strlen(b) < sizeof(b));
                    }
                }
            }
        }
    }
    ui_text_set_language(UI_LANG_EN);

    /* The numbers land where the operator reads them. */
    base_view();
    g_v.phase = UPD_PH_ENDED;
    g_v.got = UPD_GOT_TIMEOUT;
    g_v.budget_s = 30u;
    upd_result_text(&g_v, a, sizeof(a), b, sizeof(b));
    CHECK_STR_EQ(a, "No response within 30 s.");
    g_v.got = UPD_GOT_REFUSED;
    g_v.status = 503;
    upd_result_text(&g_v, a, sizeof(a), b, sizeof(b));
    CHECK_STR_EQ(a, "GitHub rejected the request: HTTP 503.");
    g_v.got = UPD_GOT_SUMS;
    g_v.rel = UPD_REL_STATUS;
    g_v.status = 404;
    upd_result_text(&g_v, a, sizeof(a), b, sizeof(b));
    CHECK_STR_EQ(b, "HTTP status 404.");
    g_v.got = UPD_GOT_NEWER;
    snprintf(g_v.found, sizeof(g_v.found), "%s", "0.20.0");
    upd_result_text(&g_v, a, sizeof(a), b, sizeof(b));
    CHECK_STR_EQ(a, "Version 0.20.0 is available.");
    g_v.got = UPD_GOT_CURRENT;
    snprintf(g_v.found, sizeof(g_v.found), "%s", "0.19.0");
    upd_result_text(&g_v, a, sizeof(a), b, sizeof(b));
    CHECK_STR_EQ(a, "Up to date. Panel version: 0.19.0.");
    CHECK_STR_EQ(b, "Latest release on GitHub: 0.19.0.");
    /* A check's VERSION is the tag's text; a download's the image's. */
    g_v.got = UPD_GOT_UNEXPECTED;
    g_v.rel = UPD_REL_VERSION;
    g_v.job = UPD_JOB_CHECK;
    upd_result_text(&g_v, a, sizeof(a), b, sizeof(b));
    CHECK_STR_EQ(b, "The version text has an invalid format.");
    g_v.job = UPD_JOB_DOWNLOAD;
    upd_result_text(&g_v, a, sizeof(a), b, sizeof(b));
    CHECK_STR_EQ(b, "The firmware file specifies a different version.");
}

TEST_CASE(a_running_job_says_its_step_and_the_time_left)
{
    char a[96], b[96];
    fresh();
    g_v.job = UPD_JOB_CHECK;
    g_v.phase = UPD_PH_JOINING;
    g_v.left_s = 29u;
    upd_result_text(&g_v, a, sizeof(a), b, sizeof(b));
    CHECK_STR_EQ(a, "Connecting to workshop");
    CHECK_STR_EQ(b, "29 s left");
    g_v.phase = UPD_PH_ASKING;
    g_v.left_s = 0u;
    upd_result_text(&g_v, a, sizeof(a), b, sizeof(b));
    CHECK_STR_EQ(a, "Checking GitHub.");
    CHECK_STR_EQ(b, "0 s left");
    /* None ran: nothing to say. */
    base_view();
    upd_result_text(&g_v, a, sizeof(a), b, sizeof(b));
    CHECK_STR_EQ(a, "");
    CHECK_STR_EQ(b, "");
    /* No buffer, no view: nothing written past what there is. */
    upd_result_text(NULL, a, sizeof(a), b, sizeof(b));
    CHECK_STR_EQ(a, "");
    upd_result_text(&g_v, NULL, 0u, b, sizeof(b));
    upd_result_text(&g_v, a, sizeof(a), NULL, 0u);
}

TEST_CASE(a_result_cut_to_a_short_buffer_stays_terminated)
{
    char a[8], b[4];
    fresh();
    g_v.phase = UPD_PH_ENDED;
    g_v.got = UPD_GOT_CURRENT;
    snprintf(g_v.found, sizeof(g_v.found), "%s", "65535.65535.65535");
    upd_result_text(&g_v, a, sizeof(a), b, sizeof(b));
    CHECK_EQ(strlen(a), 7u);
    CHECK_EQ(strlen(b), 3u);
}

/* --------------------------------------------------------- the reminder */

static upd_driver_t g_drv;
static upd_feed_t   g_feed;

/* The menu, a newer release found at start, the bench SAFE. */
static void drive_fresh(void)
{
    fresh();
    set_newer();
    push();
    ui_router_goto(SCREEN_OVERVIEW);
    memset(&g_drv, 0, sizeof(g_drv));
    memset(&g_feed, 0, sizeof(g_feed));
    g_feed.remind = true;
    g_feed.available = true;
    g_feed.have_net = true;
    g_feed.gate_open = true;
    g_feed.newer = true;
}

static void frame(void)
{
    g_feed.remind = settings_get_bool(SET_UPD_REMIND);
    upd_remind_drive(&g_drv, &g_feed);
}

TEST_CASE(the_reminder_opens_on_a_safe_overview_only)
{
    drive_fresh();
    g_feed.unsafe = true;
    frame();
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);   /* ARMED: not yet */
    g_feed.unsafe = false;
    g_feed.coproc = true;
    frame();
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);   /* the question first */
    g_feed.coproc = false;
    ui_router_goto(SCREEN_MOTOR);
    frame();
    CHECK_EQ(ui_router_current(), SCREEN_MOTOR);      /* not on a screen */
    ui_router_goto(SCREEN_OVERVIEW);
    g_feed.newer = false;
    frame();
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);   /* nothing newer */
    g_feed.newer = true;
    frame();
    CHECK_EQ(ui_router_current(), SCREEN_UPD_REMIND);
    CHECK(!g_drv.dismissed);
}

TEST_CASE(the_reminder_yields_unanswered_and_comes_back)
{
    static const int k_ways = 3;
    for (int way = 0; way < k_ways; ++way) {
        drive_fresh();
        frame();
        CHECK_EQ(ui_router_current(), SCREEN_UPD_REMIND);
        if (way == 0) {
            g_feed.unsafe = true;           /* ARMED, an arm waiting */
        } else if (way == 1) {
            g_feed.coproc = true;           /* the IOmcu's offer */
        } else {
            g_feed.newer = false;           /* another check ran */
        }
        frame();
        CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
        CHECK(!g_drv.dismissed);
        frame();
        CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
        g_feed.unsafe = false;
        g_feed.coproc = false;
        g_feed.newer = true;
        frame();
        CHECK_EQ(ui_router_current(), SCREEN_UPD_REMIND);
    }
}

TEST_CASE(ignore_now_closes_it_for_this_start_only)
{
    drive_fresh();
    frame();
    tap(rem(), R_IGNORE_X, R_ROW_Y);
    frame();
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    CHECK(g_drv.dismissed);
    for (int i = 0; i < 5; ++i) {
        frame();
        CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    }
    CHECK(settings_get_bool(SET_UPD_REMIND));
    CHECK(!settings_save_asked());

    /* The next start: a fresh driver, the setting as stored. */
    CHECK(settings_save_tick(true) == false);
    settings_init();
    memset(&g_drv, 0, sizeof(g_drv));
    CHECK(settings_get_bool(SET_UPD_REMIND));
    frame();
    CHECK_EQ(ui_router_current(), SCREEN_UPD_REMIND);
}

TEST_CASE(never_remind_me_switches_it_off_across_a_restart)
{
    drive_fresh();
    frame();
    tap(rem(), R_NEVER_X, R_ROW_Y);
    CHECK(!settings_get_bool(SET_UPD_REMIND));
    CHECK(settings_save_asked());
    frame();
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    CHECK(settings_save_tick(true));
    CHECK_EQ(g_saves, 1);

    /* The next start: the setting comes back off, so neither the check
     * nor the reminder runs. */
    settings_init();
    memset(&g_drv, 0, sizeof(g_drv));
    CHECK(!settings_get_bool(SET_UPD_REMIND));
    g_feed.remind = false;
    CHECK(!upd_startup_due(&g_drv, &g_feed));
    frame();
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);

    /* SETUP > UPDATES turns it back on, and the start after that checks. */
    tap(upd(), BTN_X, REMIND_Y);
    CHECK(settings_get_bool(SET_UPD_REMIND));
    CHECK(settings_save_tick(true));
    settings_init();
    memset(&g_drv, 0, sizeof(g_drv));
    g_feed.remind = settings_get_bool(SET_UPD_REMIND);
    CHECK(upd_startup_due(&g_drv, &g_feed));
}

TEST_CASE(the_home_tag_is_ignore_now)
{
    drive_fresh();
    frame();
    CHECK_EQ(ui_router_current(), SCREEN_UPD_REMIND);
    const gfx_rect_t home =
        ui_home_tag_rect(ui_router_title(SCREEN_UPD_REMIND));
    router_tap(home.x + home.w / 2, home.y + home.h / 2);
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    frame();
    CHECK(g_drv.dismissed);
    frame();
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    CHECK(settings_get_bool(SET_UPD_REMIND));
}

TEST_CASE(the_reminders_hold_asks_for_the_download_once)
{
    drive_fresh();
    frame();
    tap(rem(), R_HOLD_X, R_HOLD_Y);
    ticks(rem(), HOLD_TICKS);
    CHECK_EQ(upd_screen_take(), 0u);
    ev(rem(), TOUCH_EVENT_DOWN, 1, R_HOLD_X, R_HOLD_Y, 0);
    ticks(rem(), HOLD_TICKS / 2);
    ev(rem(), TOUCH_EVENT_MOVE, 1, R_HOLD_X, R_HOLD_Y - 150, 0);
    ev(rem(), TOUCH_EVENT_MOVE, 1, R_HOLD_X, R_HOLD_Y, 0);
    ticks(rem(), HOLD_TICKS);
    CHECK_EQ(upd_screen_take(), 0u);
    ev(rem(), TOUCH_EVENT_UP, 1, R_HOLD_X, R_HOLD_Y, 0);

    ev(rem(), TOUCH_EVENT_DOWN, 1, R_HOLD_X, R_HOLD_Y, 0);
    ticks(rem(), HOLD_TICKS);
    CHECK_EQ(upd_screen_take(), UPD_REQ_DOWNLOAD);
    ticks(rem(), HOLD_TICKS);
    CHECK_EQ(upd_screen_take(), 0u);
    ev(rem(), TOUCH_EVENT_UP, 1, R_HOLD_X, R_HOLD_Y, 0);
    frame();
    CHECK(g_drv.dismissed);
    CHECK(settings_get_bool(SET_UPD_REMIND));
}

TEST_CASE(the_reminders_hold_is_grey_while_the_download_may_not_start)
{
    drive_fresh();
    g_v.dl_block = UPD_WHY_DRAWING;
    push();
    frame();
    CHECK_EQ(ui_router_current(), SCREEN_UPD_REMIND);
    ev(rem(), TOUCH_EVENT_DOWN, 1, R_HOLD_X, R_HOLD_Y, 0);
    ticks(rem(), HOLD_TICKS);
    ev(rem(), TOUCH_EVENT_UP, 1, R_HOLD_X, R_HOLD_Y, 0);
    CHECK_EQ(upd_screen_take(), 0u);
    /* The taps still answer. */
    tap(rem(), R_IGNORE_X, R_ROW_Y);
    frame();
    CHECK(g_drv.dismissed);
}

TEST_CASE(the_driver_takes_no_null)
{
    upd_remind_drive(NULL, &g_feed);
    upd_remind_drive(&g_drv, NULL);
    CHECK(!upd_startup_due(NULL, &g_feed));
    CHECK(!upd_startup_due(&g_drv, NULL));
}

/* ------------------------------------------------- the start-up check */

static void startup_fresh(uint32_t now)
{
    drive_fresh();
    g_feed.newer = false;
    g_feed.now_ms = now;
}

TEST_CASE(the_startup_check_starts_once_when_the_bench_allows)
{
    startup_fresh(5000u);
    ui_router_goto(SCREEN_SPLASH);
    CHECK(!upd_startup_due(&g_drv, &g_feed));     /* the splash first */
    ui_router_goto(SCREEN_OVERVIEW);
    g_feed.gate_open = false;
    CHECK(!upd_startup_due(&g_drv, &g_feed));     /* ARMED, a log run */
    g_feed.gate_open = true;
    g_feed.files_open = true;
    CHECK(!upd_startup_due(&g_drv, &g_feed));     /* FILES & UPDATE */
    g_feed.files_open = false;
    g_feed.coproc = true;
    CHECK(!upd_startup_due(&g_drv, &g_feed));     /* the IOmcu's */
    g_feed.coproc = false;
    CHECK(upd_startup_due(&g_drv, &g_feed));
    CHECK(!upd_startup_due(&g_drv, &g_feed));     /* once */
    g_feed.now_ms += 1000u;
    CHECK(!upd_startup_due(&g_drv, &g_feed));
}

TEST_CASE(no_startup_check_without_reminder_network_or_wifi)
{
    for (int k = 0; k < 3; ++k) {
        startup_fresh(0u);
        if (k == 0) {
            g_feed.remind = false;
        } else if (k == 1) {
            g_feed.have_net = false;
        } else {
            g_feed.available = false;
        }
        CHECK(!upd_startup_due(&g_drv, &g_feed));
        /* None later at this start either, when it comes right. */
        g_feed.remind = g_feed.have_net = g_feed.available = true;
        CHECK(!upd_startup_due(&g_drv, &g_feed));
    }
}

TEST_CASE(the_startup_check_gives_up_after_its_window_across_a_wrap)
{
    static const uint32_t k_starts[] = { 0u, 0xFFFFF000u,
                                         0xFFFFFFFFu - UPD_START_WAIT_MS / 2u };
    for (size_t i = 0; i < sizeof(k_starts) / sizeof(k_starts[0]); ++i) {
        /* The gate stays shut until the last ms of the window. */
        startup_fresh(k_starts[i]);
        g_feed.gate_open = false;
        CHECK(!upd_startup_due(&g_drv, &g_feed));
        g_feed.now_ms = k_starts[i] + UPD_START_WAIT_MS - 1u;
        CHECK(!upd_startup_due(&g_drv, &g_feed));
        g_feed.gate_open = true;
        CHECK(upd_startup_due(&g_drv, &g_feed));

        /* One ms later it would have been too late. */
        startup_fresh(k_starts[i]);
        g_feed.gate_open = false;
        CHECK(!upd_startup_due(&g_drv, &g_feed));
        g_feed.now_ms = k_starts[i] + UPD_START_WAIT_MS;
        g_feed.gate_open = true;
        CHECK(!upd_startup_due(&g_drv, &g_feed));
        g_feed.now_ms += 1000u;
        CHECK(!upd_startup_due(&g_drv, &g_feed));
    }
}

/* ----------------------------------------------------- PANEL UPDATE */

static void u_tap(int x, int y)
{
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = (int16_t)x,
                                   .y = (int16_t)y, .strength = 40 } };
    update_screen()->event(&e);
    e.type = TOUCH_EVENT_UP;
    update_screen()->event(&e);
}

TEST_CASE(the_download_cancels_and_its_failure_closes)
{
    update_view_t v;
    memset(&v, 0, sizeof(v));
    update_screen()->reset();
    v.state = UPDATE_FETCHING;
    update_screen_set(&v);
    u_tap(U_BTN_X, U_BTN_Y);
    CHECK(update_screen_take_cancel());
    CHECK(!update_screen_take_cancel());
    CHECK(!update_screen_take_close());

    v.state = UPDATE_FETCH_FAILED;
    update_screen_set(&v);
    u_tap(U_BTN_X, U_BTN_Y);
    CHECK(update_screen_take_close());
    CHECK(!update_screen_take_cancel());

    /* The write takes no touch: nothing stops a flash write half way. */
    v.state = UPDATE_WRITING;
    update_screen_set(&v);
    u_tap(U_BTN_X, U_BTN_Y);
    CHECK(!update_screen_take_close());
    CHECK(!update_screen_take_cancel());
    /* Nor a tap off the button. */
    v.state = UPDATE_FETCHING;
    update_screen_set(&v);
    u_tap(U_BTN_X, 200);
    CHECK(!update_screen_take_cancel());
}

/* ------------------------------------------------------------ rendering */

TEST_CASE(every_view_renders_inside_its_canvas)
{
    static gfx_color_t px[GUARD_CANVAS_PIXELS(800, 480)];
    gfx_canvas_t c;
    guard_canvas_t g;
    for (int lang = 0; lang < 2; ++lang) {
        ui_text_set_language(lang == 0 ? UI_LANG_EN : UI_LANG_DE);
        for (int got = UPD_GOT_NONE; got <= UPD_GOT_MEMORY; ++got) {
            for (int net = 0; net < 3; ++net) {
                base_view();
                g_v.have_net = (net != 0);
                g_v.unavailable = (net == 2);
                g_v.phase = (got == UPD_GOT_NONE) ? UPD_PH_IDLE
                                                  : UPD_PH_ENDED;
                g_v.got = (upd_got_t)got;
                g_v.rel = UPD_REL_SUMS_TWICE;
                g_v.why = UPD_WHY_DRAWING;
                g_v.block = (got % 2 == 0) ? UPD_WHY_ARMED : UPD_WHY_NONE;
                g_v.dl_block = UPD_WHY_COPROC;
                memset(g_v.net, 'W', WIFI_SSID_MAX);
                g_v.net[WIFI_SSID_MAX] = '\0';
                snprintf(g_v.found, sizeof(g_v.found), "%s",
                         "65535.65535.65535");
                push();
                guard_canvas_init(&g, &c, px, 800, 432);
                upd_screen_invalidate();
                upd()->render(&c, 0);
                CHECK_EQ(guard_canvas_touched(&g), 0u);
                CHECK_EQ(guard_canvas_unpainted(&g), 0u);
                guard_canvas_init(&g, &c, px, 800, 432);
                upd_screen_invalidate();
                rem()->render(&c, 1);
                CHECK_EQ(guard_canvas_touched(&g), 0u);
                CHECK_EQ(guard_canvas_unpainted(&g), 0u);
            }
        }
        for (int ph = UPD_PH_JOINING; ph <= UPD_PH_CHECKING; ++ph) {
            base_view();
            g_v.phase = (upd_phase_t)ph;
            push();
            guard_canvas_init(&g, &c, px, 800, 432);
            upd_screen_invalidate();
            upd()->render(&c, 0);
            CHECK_EQ(guard_canvas_touched(&g), 0u);
        }
        static const update_state_t k_states[] = {
            UPDATE_WRITING, UPDATE_RESTARTING, UPDATE_FAILED,
            UPDATE_FETCHING, UPDATE_FETCH_FAILED,
        };
        static const uint32_t k_bytes[][2] = { { 0, 0 }, { 0, 1932320 },
                                               { 966160, 1932320 },
                                               { 4063232, 4063232 } };
        for (size_t i = 0; i < sizeof(k_states) / sizeof(k_states[0]); ++i) {
            for (size_t k = 0; k < sizeof(k_bytes) / sizeof(k_bytes[0]);
                 ++k) {
                update_view_t v;
                memset(&v, 0, sizeof(v));
                v.state = k_states[i];
                snprintf(v.version, sizeof(v.version), "%s",
                         "65535.65535.65535");
                v.written = k_bytes[k][0];
                v.total = k_bytes[k][1];
                memset(v.what, 'x', sizeof(v.what) - 1u);
                memset(v.why, 'y', sizeof(v.why) - 1u);
                update_screen_set(&v);
                guard_canvas_init(&g, &c, px, 800, 480);
                update_screen_invalidate();
                update_screen()->render(&c, 0);
                CHECK_EQ(guard_canvas_touched(&g), 0u);
                CHECK_EQ(guard_canvas_unpainted(&g), 0u);
            }
        }
    }
    ui_text_set_language(UI_LANG_EN);
}

TEST_CASE(both_screens_carry_the_band_and_the_home_tag)
{
    fresh();
    ui_router_goto(SCREEN_UPDATES);
    CHECK(ui_router_stop_live());
    CHECK_STR_EQ(ui_router_title(SCREEN_UPDATES), "UPDATES");
    ui_router_goto(SCREEN_UPD_REMIND);
    CHECK(ui_router_stop_live());
    ui_router_goto(SCREEN_UPDATE);
    CHECK(!ui_router_stop_live());
}

/*
 * The card of UPDATES and of FILES & UPDATE breaks a text into lines of
 * its 352 px, 44 cells.  A text no view draws declares 88 cells in
 * ui_text.def, two of those lines, or fewer where another screen draws it
 * on one line; here each is held to the two lines as the card breaks it.
 */
TEST_CASE(every_card_text_takes_two_lines_at_most)
{
    static const ui_text_id_t k_card[] = {
        TX_WF_UNAVAIL_1, TX_WF_UNAVAIL_2, TX_WF_SAVE_FAILED_1,
        TX_WF_SAVE_FAILED_2, TX_WF_OPEN_1, TX_WF_OPEN_2, TX_WF_OFF_ARMED,
        TX_WF_OFF_SUPPLY, TX_WF_OFF_RUN, TX_WF_OFF_STOP, TX_WF_OFF_IDLE,
        TX_WF_OFF_GUESSES, TX_WF_OFF_FAILED, TX_WF_JOIN_PASSWORD,
        TX_WF_JOIN_NOT_FOUND, TX_WF_JOIN_NO_ADDRESS, TX_WF_JOIN_LOST,
        TX_WF_TRY_AP, TX_WF_NEEDS_1, TX_WF_NEEDS_2, TX_UPD_STAGED,
        TX_UPD_WAIT_COPROC, TX_UPD_WAIT_DRAWING, TX_UPD_WAIT_BUSY,
        TX_UPD_STOP_FILES, TX_UPD_STOP_CANCEL, TX_UPD_STOP_COPROC,
        TX_UPD_STOP_INSTALL, TX_UPD_STOP_DRAWING, TX_UPD_FAIL_CONNECT,
        TX_UPD_FAIL_REFUSED, TX_UPD_FAIL_UNEXPECTED, TX_UPD_FAIL_SUMS,
        TX_UPD_FAIL_IMAGE, TX_UPD_FAIL_MEMORY, TX_UPD_REL_MISSING,
        TX_UPD_REL_LONG, TX_UPD_REL_CHAR, TX_UPD_REL_PLACE,
        TX_UPD_REL_VERSION, TX_UPD_REL_IMG_VERSION, TX_UPD_REL_STATUS,
        TX_UPD_REL_HOPS, TX_UPD_REL_HOST, TX_UPD_REL_NO_LENGTH,
        TX_UPD_REL_EMPTY, TX_UPD_REL_TOO_BIG, TX_UPD_REL_SHORT,
        TX_UPD_REL_OVERRUN, TX_UPD_REL_SUMS_BAD, TX_UPD_REL_SUMS_NONE,
        TX_UPD_REL_SUMS_TWICE, TX_UPD_DL_SUMS, TX_UPD_DL_CHECKING,
    };
    const gfx_rect_t line = gfx_rect_make(416, 0, 352, 18);
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        for (size_t i = 0; i < sizeof(k_card) / sizeof(k_card[0]); ++i) {
            const char *t = ui_tr(k_card[i]);
            const int n = ui_text_wrap(NULL, line, 20, t, UI_FONT_LABEL,
                                       UI_TEXT);
            if (n < 1 || n > 2) {
                T_FAIL("language %d: %s takes %d lines: \"%s\"", lang,
                       ui_text_name(k_card[i]), n, t);
            }
        }
    }
    ui_text_set_language(UI_LANG_EN);
}

int main(void)
{
    RUN(the_enums_mirror_the_fetch_and_the_release_parser);
    RUN(check_asks_once_with_a_home_network_and_the_gate_open);
    RUN(check_is_grey_without_a_network_wifi_or_the_gate);
    RUN(a_running_check_turns_the_button_into_cancel);
    RUN(the_reminder_switch_flips_the_setting_and_asks_for_the_save);
    RUN(the_download_hold_is_there_only_for_a_newer_release);
    RUN(the_download_hold_ends_unfired_when_the_gate_closes_under_it);
    RUN(every_result_has_a_text_in_both_languages);
    RUN(a_running_job_says_its_step_and_the_time_left);
    RUN(a_result_cut_to_a_short_buffer_stays_terminated);
    RUN(the_reminder_opens_on_a_safe_overview_only);
    RUN(the_reminder_yields_unanswered_and_comes_back);
    RUN(ignore_now_closes_it_for_this_start_only);
    RUN(never_remind_me_switches_it_off_across_a_restart);
    RUN(the_home_tag_is_ignore_now);
    RUN(the_reminders_hold_asks_for_the_download_once);
    RUN(the_reminders_hold_is_grey_while_the_download_may_not_start);
    RUN(the_driver_takes_no_null);
    RUN(the_startup_check_starts_once_when_the_bench_allows);
    RUN(no_startup_check_without_reminder_network_or_wifi);
    RUN(the_startup_check_gives_up_after_its_window_across_a_wrap);
    RUN(the_download_cancels_and_its_failure_closes);
    RUN(every_view_renders_inside_its_canvas);
    RUN(both_screens_carry_the_band_and_the_home_tag);
    RUN(every_card_text_takes_two_lines_at_most);
    return test_summary("upd_screen");
}
