/**
 * @file test_logs_strip.c
 * @brief The LOGS tile's strip: one tile, two screens.  Tabs change screen,
 *        the home tag of each goes to the menu, a level under the first takes
 *        no tab.  LOGS' WI-FI button: where it sits, what takes no press on
 *        it, and the way back from WI-FI.
 *
 * Coverage: logs_strip.c, the strip's calls in log_viewer_events.c and
 * files_events.c, the footer's WI-FI button, FILES' way back to LOGS from a
 * report.
 *
 * SPDX-License-Identifier: MIT
 */

#include "ui_band.h"

#include "files_edit_rig.h"

#include "fm_rules.h"
#include "log_name.h"
#include "log_select.h"
#include "log_take.h"
#include "log_viewer_screen.h"
#include "logs_strip.h"
#include "logview_spots.h"
#include "settings_screen.h"
#include "ui_tabs.h"
#include "ui_widgets.h"
#include "wifi_screen.h"
#include "wifi_spots.h"

#include "files_int.h"

/** @brief The tabs' middles: 499px wide, 4px apart. */
#define TAB_CX(i) (LOGS_STRIP_X + (i) * 503 + 249)
#define TAB_LOGS  0
#define TAB_FILES 1
/** @brief Their middle row, in the screen's coordinates. */
#define TAB_CY    (LOGS_STRIP_Y + LOGS_STRIP_H / 2)

static char s_lv_dir[CARD_DIR_MAX];
static log_mem_ctx_t s_lv_mem;
static const char k_csv[] = "time,voltage\n0.0,22.3\n0.1,22.2\n0.2,22.1\n";
static const char k_report[] = "RCBENCH SERVO TEST REPORT\nResult: PASS\n";

/** @brief LOGS' folder(): the folder the next call reads. */
static void lv_folder(const char *dir, void *ctx)
{
    (void)ctx;
    snprintf(s_lv_dir, sizeof(s_lv_dir), "%s", (dir != NULL) ? dir : "");
}

/**
 * @brief The name of card entry @p i when it is a file of LOGS' folder.
 * @param i the entry
 * @return the name, or NULL
 */
static const char *name_in(int i)
{
    const fk_node_t *e = &g_h.card.n[i];
    const char *slash = strrchr(e->path, '/');
    const size_t len = (slash != NULL) ? (size_t)(slash - e->path) : 0u;
    if (e->dir || strlen(s_lv_dir) != len
        || strncmp(e->path, s_lv_dir, len) != 0) {
        return NULL;
    }
    return (slash != NULL) ? slash + 1 : e->path;
}

/** @brief LOGS' list(): the folder's files by the panel's rule. */
static int lv_list(log_viewer_file_t *out, int max_entries,
                   log_viewer_page_t *page, void *ctx)
{
    (void)ctx;
    log_select_page_t sel;
    log_select_page_begin(&sel, out, max_entries, page);
    int n = 0;
    for (int i = 0; i < g_h.card.count; ++i) {
        const char *name = name_in(i);
        if (name == NULL || !log_select_lists(name)) {
            continue;
        }
        log_viewer_file_t f;
        memset(&f, 0, sizeof(f));
        snprintf(f.name, sizeof(f.name), "%s", name);
        f.size = g_h.card.n[i].size;
        log_select_page_offer(&sel, &f);
        ++n;
    }
    (void)log_select_page_end(&sel, page);
    return n;
}

/** @brief LOGS' open(): the file's bytes from the card. */
static bool lv_open(const char *name, log_source_t *src, void *ctx)
{
    (void)ctx;
    for (int i = 0; i < g_h.card.count; ++i) {
        const char *here = name_in(i);
        if (here != NULL && strcmp(here, name) == 0) {
            log_source_memory(src, &s_lv_mem,
                              (const char *)g_h.card.n[i].data,
                              g_h.card.n[i].size);
            return true;
        }
    }
    return false;
}

/** @brief LOGS' remove(): every file may go. */
static bool lv_remove(const char *name, void *ctx)
{
    (void)name;
    (void)ctx;
    return true;
}

/** @brief LOGS' mounts(): the card's mount number. */
static unsigned lv_mounts(void *ctx)
{
    (void)ctx;
    return g_h.card.mount;
}

/**
 * @brief The default card with a CSV and a report in BENCH/LOGS, LOGS' I/O on
 *        it, the bench SAFE, LOGS on top from the menu.
 */
static void fresh_strip(void)
{
    fresh_bare();
    card_default();
    fk_file(&g_h.card, "BENCH/LOGS/ATEST011.CSV", (const uint8_t *)k_csv,
            strlen(k_csv));
    fk_file(&g_h.card, "BENCH/LOGS/ATEST012.TXT", (const uint8_t *)k_report,
            strlen(k_report));
    const log_viewer_io_t lv = { .list = lv_list, .open = lv_open,
                                 .folder = lv_folder, .mounts = lv_mounts,
                                 .remove = lv_remove };
    log_viewer_set_io(&lv);
    (void)wifi_screen_take();
    ui_router_goto(SCREEN_OVERVIEW);
    ui_router_goto(SCREEN_LOGS);
    logs_strip_cancel();
}

/** @brief One event of contact @p id, at (@p x, @p y) of the body. */
static void body(touch_event_type_t type, int id, int x, int y)
{
    touch_event_t e = { .type = type,
                        .point = { .id = (uint8_t)id, .x = (int16_t)x,
                                   .y = (int16_t)(UI_BAND_H + y),
                                   .strength = 40 } };
    ui_router_event(&e);
}

/** @brief A tap through the router, then the card task's answers. */
static void btap(int x, int y)
{
    body(TOUCH_EVENT_DOWN, 1, x, y);
    body(TOUCH_EVENT_UP, 1, x, y);
    settle();
}

/** @brief A tap on tab @p i. */
static void tab(int i)
{
    btap(TAB_CX(i), TAB_CY);
}

/** @brief A tap on the home tag of the screen on top. */
static void tag(void)
{
    const gfx_rect_t r =
        ui_home_tag_rect(ui_router_title(ui_router_current()));
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = (int16_t)(r.x + r.w / 2),
                                   .y = (int16_t)(r.y + r.h / 2),
                                   .strength = 40 } };
    ui_router_event(&e);
    e.type = TOUCH_EVENT_UP;
    ui_router_event(&e);
    settle();
}

/** @brief The canvas of the whole panel's body, drawn by the router. */
static void paint(void)
{
    gfx_canvas_t all;
    gfx_canvas_init(&all, s_fb, W, H, W);
    ui_router_render(&all, 0);
}

TEST_CASE(each_tab_opens_its_screen_and_the_selected_tab_does_nothing)
{
    fresh_strip();
    const ui_screen_id_t order[] = { SCREEN_FILES, SCREEN_LOGS, SCREEN_FILES,
                                     SCREEN_LOGS };
    const int tabs[] = { TAB_FILES, TAB_LOGS, TAB_FILES, TAB_LOGS };
    for (size_t i = 0; i < sizeof(tabs) / sizeof(tabs[0]); ++i) {
        tab(tabs[i]);
        paint();
        CHECK_EQ(ui_router_current(), order[i]);
    }
    /* LOGS' tab on LOGS: no navigation, no list read over it. */
    const uint32_t nav = ui_router_navigations();
    tab(TAB_LOGS);
    CHECK_EQ(ui_router_current(), SCREEN_LOGS);
    CHECK_EQ(ui_router_navigations(), nav);
    /* The gap between two tabs is no tab. */
    btap(LOGS_STRIP_X + 500, TAB_CY);
    CHECK_EQ(ui_router_navigations(), nav);
}

TEST_CASE(the_home_tag_of_each_tab_goes_to_the_menu_whichever_opened_it)
{
    const ui_screen_id_t screens[] = { SCREEN_LOGS, SCREEN_FILES };
    for (int from = 0; from < 2; ++from) {
        for (int to = 0; to < 2; ++to) {
            fresh_strip();
            tab(from);
            tab(to);
            CHECK_EQ(ui_router_current(), screens[to]);
            CHECK_EQ(ui_router_parent(screens[to]), SCREEN_OVERVIEW);
            CHECK(!settings_screen_opens(screens[to]));
            tag();
            CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
        }
    }
}

TEST_CASE(a_press_that_slid_off_lifted_for_the_finger_or_cancelled_is_no_tab)
{
    fresh_strip();
    const uint32_t nav = ui_router_navigations();
    /* Slid off its tab: released over the next one, and under the strip. */
    body(TOUCH_EVENT_DOWN, 1, TAB_CX(TAB_FILES), TAB_CY);
    body(TOUCH_EVENT_MOVE, 1, TAB_CX(TAB_LOGS), TAB_CY);
    body(TOUCH_EVENT_UP, 1, TAB_CX(TAB_LOGS), TAB_CY);
    body(TOUCH_EVENT_DOWN, 1, TAB_CX(TAB_FILES), TAB_CY);
    body(TOUCH_EVENT_UP, 1, TAB_CX(TAB_FILES), TAB_CY + 100);
    CHECK_EQ(ui_router_navigations(), nav);
    /* A release the router made up: the contact went into the band. */
    body(TOUCH_EVENT_DOWN, 1, TAB_CX(TAB_FILES), TAB_CY);
    touch_event_t e = { .type = TOUCH_EVENT_MOVE,
                        .point = { .id = 1, .x = (int16_t)TAB_CX(TAB_FILES),
                                   .y = 10, .strength = 40 } };
    ui_router_event(&e);
    e.type = TOUCH_EVENT_UP;
    ui_router_event(&e);
    CHECK_EQ(ui_router_current(), SCREEN_LOGS);
    CHECK_EQ(ui_router_navigations(), nav);
    /* The stream broke: another contact's release finds no press. */
    body(TOUCH_EVENT_DOWN, 1, TAB_CX(TAB_FILES), TAB_CY);
    ui_router_cancel_gestures();
    body(TOUCH_EVENT_DOWN, 2, TAB_CX(TAB_FILES), 300);
    body(TOUCH_EVENT_UP, 1, TAB_CX(TAB_FILES), TAB_CY);
    body(TOUCH_EVENT_UP, 2, TAB_CX(TAB_FILES), 300);
    CHECK_EQ(ui_router_navigations(), nav);
}

/*
 * A lost event ends the press on each screen: the track id is reused, so a
 * release handed to the screen later is no tap on the tab.
 */
TEST_CASE(each_screen_drops_a_press_on_the_strip_when_events_are_lost)
{
    const int tabs[] = { TAB_LOGS, TAB_FILES };
    const ui_screen_t *const screens[] = { log_viewer_screen(), files_screen() };
    for (int k = 0; k < 2; ++k) {
        fresh_strip();
        tab(tabs[k]);
        const int other = tabs[(k + 1) % 2];
        const uint32_t nav = ui_router_navigations();
        touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                            .point = { .id = 1, .x = (int16_t)TAB_CX(other),
                                       .y = (int16_t)TAB_CY, .strength = 40 } };
        screens[k]->event(&e);
        screens[k]->cancel();
        e.type = TOUCH_EVENT_UP;
        screens[k]->event(&e);
        CHECK_EQ(ui_router_navigations(), nav);
    }
}

TEST_CASE(a_second_finger_on_the_strip_ends_no_press_of_the_first)
{
    fresh_strip();
    const uint32_t nav = ui_router_navigations();
    /* A tab held: another contact's tab and row press nothing, and the first
     * contact's lift is the tap. */
    body(TOUCH_EVENT_DOWN, 1, TAB_CX(TAB_FILES), TAB_CY);
    body(TOUCH_EVENT_DOWN, 2, LV_WIFI_CX, LV_FOOT_CY);
    body(TOUCH_EVENT_UP, 2, LV_WIFI_CX, LV_FOOT_CY);
    body(TOUCH_EVENT_DOWN, 2, LV_ROW_CX, LV_ROW_CY(1));
    body(TOUCH_EVENT_UP, 2, LV_ROW_CX, LV_ROW_CY(1));
    CHECK_EQ(ui_router_navigations(), nav);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);
    body(TOUCH_EVENT_UP, 1, TAB_CX(TAB_FILES), TAB_CY);
    CHECK_EQ(ui_router_current(), SCREEN_FILES);
    fresh_strip();
    /* LOGS: a folder tab held. */
    const uint32_t nav0 = ui_router_navigations();
    body(TOUCH_EVENT_DOWN, 1, LV_TAB_CX(0), LV_HEAD_CY);
    body(TOUCH_EVENT_DOWN, 2, TAB_CX(TAB_FILES), TAB_CY);
    body(TOUCH_EVENT_UP, 2, TAB_CX(TAB_FILES), TAB_CY);
    CHECK_EQ(ui_router_navigations(), nav0);
    body(TOUCH_EVENT_UP, 1, LV_TAB_CX(0), LV_HEAD_CY);
    CHECK_EQ(log_viewer_tab(), LOG_TAB_ROOT);
    /* LOGS: a row held. */
    const uint32_t nav1 = ui_router_navigations();
    body(TOUCH_EVENT_DOWN, 1, LV_ROW_CX, LV_ROW_CY(1));
    body(TOUCH_EVENT_DOWN, 2, TAB_CX(TAB_FILES), TAB_CY);
    body(TOUCH_EVENT_UP, 2, TAB_CX(TAB_FILES), TAB_CY);
    CHECK_EQ(ui_router_navigations(), nav1);
    body(TOUCH_EVENT_UP, 1, LV_ROW_CX, LV_ROW_CY(1));
    /* FILES: a footer button held. */
    tab(TAB_FILES);
    const uint32_t nav2 = ui_router_navigations();
    body(TOUCH_EVENT_DOWN, 1, FS_SPOT_ROW_CX(0), FS_SPOT_ROW_CY(1));
    body(TOUCH_EVENT_DOWN, 2, TAB_CX(TAB_LOGS), TAB_CY);
    body(TOUCH_EVENT_UP, 2, TAB_CX(TAB_LOGS), TAB_CY);
    CHECK_EQ(ui_router_navigations(), nav2);
    body(TOUCH_EVENT_UP, 1, FS_SPOT_ROW_CX(0), FS_SPOT_ROW_CY(1));
}

/* ------------------------------------------------------------ the bench */

TEST_CASE(armed_the_strip_works_and_each_screen_keeps_its_own_refusal)
{
    fresh_strip();
    const ui_bench_status_t armed = { .link_up = true, .armed = true };
    ui_router_set_status(&armed);
    tab(TAB_FILES);
    CHECK_EQ(ui_router_current(), SCREEN_FILES);
    settle();
    CHECK(pick(0, "NOTES.TXT"));
    fm_err_t why = FM_OK;
    CHECK(!fs_foot_live(FS_COPY, &why));
    CHECK_EQ(why, FM_ERR_ARMED);
    tab(TAB_LOGS);
    CHECK_EQ(ui_router_current(), SCREEN_LOGS);
}

/* -------------------------------------------------- what the tabs carry */

TEST_CASE(logs_takes_no_tab_over_a_file_or_the_delete_question)
{
    fresh_strip();
    const uint32_t nav = ui_router_navigations();
    /* BENCH's rows: the report, chosen as the newest, then the CSV. */
    btap(LV_ROW_CX, LV_ROW_CY(1));
    btap(LV_OPEN_CX, LV_FOOT_CY);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_IMPORT);
    tab(TAB_FILES);
    CHECK_EQ(ui_router_navigations(), nav);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_IMPORT);
    tag();
    CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);
    CHECK_EQ(ui_router_current(), SCREEN_LOGS);
    btap(LV_DELETE_CX, LV_FOOT_CY);
    tab(TAB_FILES);
    CHECK_EQ(ui_router_current(), SCREEN_LOGS);
    tag();
    CHECK_EQ(ui_router_current(), SCREEN_LOGS);
    tag();
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
}

TEST_CASE(files_opens_a_csv_in_logs_and_the_strip_goes_back)
{
    fresh_strip();
    tab(TAB_FILES);
    CHECK(open_dir(0, "BENCH"));
    CHECK(open_dir(0, "LOGS"));
    CHECK(pick(0, "ATEST011.CSV"));
    tap_foot(FS_SPOT_OPEN);
    CHECK_EQ(ui_router_current(), SCREEN_LOGS);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_IMPORT);
    CHECK_STR_EQ(log_viewer_open_name(), "ATEST011.CSV");
    /* One tag to the list, the FILES tab to where it was. */
    tag();
    CHECK_EQ(ui_router_current(), SCREEN_LOGS);
    tab(TAB_FILES);
    CHECK_EQ(ui_router_current(), SCREEN_FILES);
    CHECK_STR_EQ(files_screen_pane(0)->dir, "BENCH/LOGS");
}

TEST_CASE(logs_opens_a_report_in_the_viewer_and_the_tag_returns_to_logs)
{
    fresh_strip();
    const fm_err_t err = files_screen_open_text(
        "BENCH/LOGS", "ATEST012.TXT", g_h.card.mount, strlen(k_report), true);
    CHECK_EQ(err, FM_OK);
    ui_router_goto(SCREEN_FILES);
    settle();
    CHECK_EQ(files_screen_view(), FILES_VIEW_TEXT);
    /* The viewer takes no tab. */
    const uint32_t nav = ui_router_navigations();
    tab(TAB_LOGS);
    CHECK_EQ(ui_router_navigations(), nav);
    CHECK_EQ(files_screen_view(), FILES_VIEW_TEXT);
    /* One tag back to LOGS, one more to the menu. */
    tag();
    CHECK_EQ(ui_router_current(), SCREEN_LOGS);
    CHECK_EQ(files_screen_view(), FILES_VIEW_PANES);
    CHECK_EQ(text_view_state(), TEXT_VIEW_CLOSED);
    tag();
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    /* A file FILES opened itself closes onto the panes, and FILES stays. */
    fresh_strip();
    tab(TAB_FILES);
    CHECK_EQ(files_screen_open_text("", "NOTES.TXT", g_h.card.mount, 512u,
                                    false), FM_OK);
    settle();
    tag();
    CHECK_EQ(ui_router_current(), SCREEN_FILES);
    CHECK_EQ(files_screen_view(), FILES_VIEW_PANES);
}

TEST_CASE(the_editor_asks_before_any_tab_and_keeps_its_text_over_a_tab)
{
    open_text("NOTE.TXT", "hello\n");
    const log_viewer_io_t lv = { .list = lv_list, .folder = lv_folder };
    log_viewer_set_io(&lv);
    edit();
    ed_type("q");
    tag();                      /* the keyboard closes */
    CHECK(text_view_dirty());
    /* The editor takes no tab: the text stays, no question, no screen. */
    const uint32_t nav = ui_router_navigations();
    tab(TAB_LOGS);
    CHECK_EQ(ui_router_navigations(), nav);
    CHECK(text_view_dirty());
    /* The home tag asks. */
    tag();
    CHECK_EQ(text_view_ask(), TEXT_VIEW_ASK_BACK);
    CHECK_EQ(ui_router_current(), SCREEN_FILES);
    tag();                      /* STAY */
    CHECK_EQ(text_view_ask(), TEXT_VIEW_ASK_NONE);
    CHECK(text_view_dirty());
    /* A route the application takes keeps the editor; FILES' tab returns to
     * it, and the question is still the way out. */
    ui_router_goto(SCREEN_LOGS);
    tab(TAB_FILES);
    CHECK_EQ(ui_router_current(), SCREEN_FILES);
    CHECK_EQ(files_screen_view(), FILES_VIEW_TEXT);
    CHECK(text_view_dirty());
}

TEST_CASE(a_running_job_keeps_its_box_and_takes_no_tab)
{
    fresh_strip();
    tab(TAB_FILES);
    fk_file(&g_h.card, "BIG.BIN", NULL, 5u * FM_CHUNK);
    tap_foot(FS_SPOT_RESCAN);
    CHECK(pick(0, "BIG.BIN"));
    g_h.defer = true;
    tap_foot(FS_SPOT_COPY);
    CHECK_EQ(files_screen_view(), FILES_VIEW_JOB);
    const uint32_t nav = ui_router_navigations();
    tab(TAB_LOGS);
    CHECK_EQ(ui_router_navigations(), nav);
    CHECK_EQ(files_screen_view(), FILES_VIEW_JOB);
    CHECK(files_screen_busy());
    /* A route the application takes leaves the job running; the tab shows
     * its box again. */
    ui_router_goto(SCREEN_LOGS);
    CHECK(files_screen_busy());
    tab(TAB_FILES);
    CHECK_EQ(ui_router_current(), SCREEN_FILES);
    CHECK_EQ(files_screen_view(), FILES_VIEW_JOB);
    while (fh_run(&g_h)) {
        settle();
    }
    g_h.defer = false;
    settle();
    CHECK(!files_screen_busy());
}

TEST_CASE(a_box_takes_no_tab)
{
    fresh_strip();
    tab(TAB_FILES);
    CHECK(pick(0, "NOTES.TXT"));
    const uint32_t nav = ui_router_navigations();
    const int boxes[] = { FS_SPOT_INFO, FS_SPOT_DELETE, FS_SPOT_RENAME,
                          FS_SPOT_NEW };
    for (size_t k = 0; k < sizeof(boxes) / sizeof(boxes[0]); ++k) {
        tap_foot(boxes[k]);
        CHECK(files_screen_view() != FILES_VIEW_PANES);
        tab(TAB_LOGS);
        CHECK_EQ(ui_router_navigations(), nav);
        /* NEW's choice closed on that press; the rest close by the tag. */
        if (files_screen_view() != FILES_VIEW_PANES) {
            tag();
        }
        CHECK_EQ(files_screen_view(), FILES_VIEW_PANES);
    }
}

TEST_CASE(the_strip_fits_in_both_languages_and_stands_above_each_first_row)
{
    fresh_strip();
    for (int l = 0; l < 2; ++l) {
        ui_text_set_language(l == 0 ? UI_LANG_EN : UI_LANG_DE);
        ui_tabs_t t;
        const char *labels[] = { ui_router_title(SCREEN_LOGS),
                                 ui_router_title(SCREEN_FILES) };
        ui_tabs_init(&t, labels, 2,
                     gfx_rect_make(LOGS_STRIP_X, LOGS_STRIP_Y, LOGS_STRIP_W,
                                   LOGS_STRIP_H));
        CHECK(ui_tabs_font(&t) == &gfx_font_10x20);
        CHECK_EQ(t.rect[0].w, 499);
        CHECK_EQ(t.rect[1].x, LOGS_STRIP_X + 503);
        CHECK(t.rect[1].x + t.rect[1].w + 3 <= UI_PANEL_W - 8);
    }
    ui_text_set_language(UI_LANG_EN);
    CHECK(LOGS_STRIP_FOOT < LV_HEAD_TOP);
    CHECK(LOGS_STRIP_FOOT < FS_SPOT_HEAD_CY - 24);
}

/* ------------------------------------------------- LOGS' WI-FI button */

/* The button opens WI-FI; the home tag comes back, the radio off on the way. */
TEST_CASE(the_wifi_button_opens_wifi_and_the_tag_returns_to_logs)
{
    fresh_strip();
    btap(LV_WIFI_CX, LV_FOOT_CY);
    CHECK_EQ(ui_router_current(), SCREEN_WIFI);
    CHECK_EQ(ui_router_parent(SCREEN_WIFI), SCREEN_LOGS);
    CHECK_EQ(wifi_screen_take(), 0u);       /* opening starts no radio */
    wifi_view_t v;
    memset(&v, 0, sizeof(v));
    v.code_on = true;
    v.radio = WIFI_RADIO_AP;
    wifi_screen_set(&v);
    tag();
    CHECK_EQ(ui_router_current(), SCREEN_LOGS);
    const unsigned r = wifi_screen_take();
    CHECK((r & WIFI_REQ_OFF) != 0u);
    CHECK((r & WIFI_REQ_DISCARD) != 0u);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);
    /* LOGS is the menu's again from here. */
    CHECK_EQ(ui_router_parent(SCREEN_LOGS), SCREEN_OVERVIEW);
    tag();
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
}

/* The network list climbs to the buttons, a staged image's first tag answers
 * CANCEL, and only then the tag leaves for LOGS. */
TEST_CASE(wifi_levels_from_logs_climb_before_the_tag_returns_to_logs)
{
    fresh_strip();
    btap(LV_WIFI_CX, LV_FOOT_CY);
    btap(WF_BTN_CX(2), WF_BTN_CY(2));
    const uint32_t nav = ui_router_navigations();
    tag();
    CHECK_EQ(ui_router_current(), SCREEN_WIFI);
    CHECK_EQ(ui_router_navigations(), nav);
    wifi_view_t v;
    memset(&v, 0, sizeof(v));
    v.code_on = true;
    v.up = WIFI_UP_STAGED;
    wifi_screen_set(&v);
    tag();
    CHECK_EQ(ui_router_current(), SCREEN_WIFI);
    CHECK((wifi_screen_take() & WIFI_REQ_DISCARD) != 0u);
    tag();
    CHECK_EQ(ui_router_current(), SCREEN_LOGS);
}

/* ARMED the button opens WI-FI as its tile does, and WI-FI's own buttons
 * refuse: the application blocks them. */
TEST_CASE(armed_the_wifi_button_opens_wifi_and_its_buttons_refuse)
{
    fresh_strip();
    const ui_bench_status_t armed = { .link_up = true, .armed = true };
    ui_router_set_status(&armed);
    btap(LV_WIFI_CX, LV_FOOT_CY);
    CHECK_EQ(ui_router_current(), SCREEN_WIFI);
    wifi_view_t v;
    memset(&v, 0, sizeof(v));
    v.code_on = true;
    v.block = WIFI_WHY_ARMED;
    wifi_screen_set(&v);
    (void)wifi_screen_take();
    btap(WF_BTN_CX(0), WF_BTN_CY(0));
    btap(WF_BTN_CX(2), WF_BTN_CY(2));
    CHECK_EQ(wifi_screen_take(), 0u);
    tag();
    CHECK_EQ(ui_router_current(), SCREEN_LOGS);
    const ui_bench_status_t safe = { .link_up = true };
    ui_router_set_status(&safe);
}

/* Slid off, a second contact, or a broken touch stream: no way to WI-FI. */
TEST_CASE(a_press_that_slid_off_the_wifi_button_or_was_lost_opens_nothing)
{
    fresh_strip();
    const uint32_t nav = ui_router_navigations();
    /* Slid onto RESCAN. */
    body(TOUCH_EVENT_DOWN, 1, LV_WIFI_CX, LV_FOOT_CY);
    body(TOUCH_EVENT_MOVE, 1, LV_RESCAN_CX, LV_FOOT_CY);
    body(TOUCH_EVENT_UP, 1, LV_RESCAN_CX, LV_FOOT_CY);
    CHECK_EQ(ui_router_navigations(), nav);
    /* A row held: another contact on the button presses nothing. */
    body(TOUCH_EVENT_DOWN, 1, LV_ROW_CX, LV_ROW_CY(1));
    body(TOUCH_EVENT_DOWN, 2, LV_WIFI_CX, LV_FOOT_CY);
    body(TOUCH_EVENT_UP, 2, LV_WIFI_CX, LV_FOOT_CY);
    CHECK_EQ(ui_router_navigations(), nav);
    body(TOUCH_EVENT_UP, 1, LV_ROW_CX, LV_ROW_CY(1));
    /* The stream broke: the release of a reused track id is no tap. */
    body(TOUCH_EVENT_DOWN, 1, LV_WIFI_CX, LV_FOOT_CY);
    log_viewer_screen()->cancel();
    touch_event_t e = { .type = TOUCH_EVENT_UP,
                        .point = { .id = 1, .x = LV_WIFI_CX, .y = LV_FOOT_CY,
                                   .strength = 40 } };
    log_viewer_screen()->event(&e);
    CHECK_EQ(ui_router_navigations(), nav);
    CHECK_EQ(ui_router_current(), SCREEN_LOGS);
}

/* The DELETE question replaces the footer; a file's views have their own. */
TEST_CASE(the_wifi_button_takes_no_press_under_a_question_or_a_file)
{
    fresh_strip();
    const uint32_t nav = ui_router_navigations();
    btap(LV_DELETE_CX, LV_FOOT_CY);
    btap(LV_WIFI_CX, LV_FOOT_CY);
    CHECK_EQ(ui_router_navigations(), nav);
    tag();                              /* CANCEL */
    /* The CSV's import view: the spot is its first footer button. */
    btap(LV_ROW_CX, LV_ROW_CY(1));
    btap(LV_OPEN_CX, LV_FOOT_CY);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_IMPORT);
    btap(LV_WIFI_CX, LV_FOOT_CY);
    CHECK_EQ(ui_router_navigations(), nav);
    CHECK_EQ(log_viewer_view(), LOG_VIEW_IMPORT);
    tag();
    CHECK_EQ(log_viewer_view(), LOG_VIEW_BROWSE);
    /* And back on the list it works. */
    btap(LV_WIFI_CX, LV_FOOT_CY);
    CHECK_EQ(ui_router_current(), SCREEN_WIFI);
}

/* A card with no file to list keeps the button: WI-FI also takes an upload. */
TEST_CASE(the_wifi_button_shows_with_nothing_listed_and_files_has_none)
{
    fresh_strip();
    const log_viewer_io_t none = { .folder = lv_folder, .mounts = lv_mounts };
    log_viewer_set_io(&none);
    log_viewer_refresh();
    settle();
    btap(LV_WIFI_CX, LV_FOOT_CY);
    CHECK_EQ(ui_router_current(), SCREEN_WIFI);
    /* FILES has no such button: the spot is RESCAN's or nothing there. */
    fresh_strip();
    tab(TAB_FILES);
    const uint32_t nav = ui_router_navigations();
    btap(LV_WIFI_CX, LV_FOOT_CY);
    CHECK_EQ(ui_router_navigations(), nav);
    CHECK_EQ(ui_router_current(), SCREEN_FILES);
}

/* The button's word is the screen's title, in both languages, in its 100px. */
TEST_CASE(the_wifi_button_label_fits_in_both_languages)
{
    fresh_strip();
    for (int l = 0; l < 2; ++l) {
        ui_text_set_language(l == 0 ? UI_LANG_EN : UI_LANG_DE);
        CHECK(gfx_text_width(&gfx_font_10x20, TR(LOG_WIFI), 1) + 2 * 8 <= 100);
        /* The word of the screen it opens, as a tile's. */
        CHECK_STR_EQ(TR(LOG_WIFI), ui_router_title(SCREEN_WIFI));
    }
    ui_text_set_language(UI_LANG_EN);
}

int main(void)
{
    RUN(each_tab_opens_its_screen_and_the_selected_tab_does_nothing);
    RUN(the_home_tag_of_each_tab_goes_to_the_menu_whichever_opened_it);
    RUN(a_press_that_slid_off_lifted_for_the_finger_or_cancelled_is_no_tab);
    RUN(each_screen_drops_a_press_on_the_strip_when_events_are_lost);
    RUN(a_second_finger_on_the_strip_ends_no_press_of_the_first);
    RUN(armed_the_strip_works_and_each_screen_keeps_its_own_refusal);
    RUN(logs_takes_no_tab_over_a_file_or_the_delete_question);
    RUN(files_opens_a_csv_in_logs_and_the_strip_goes_back);
    RUN(logs_opens_a_report_in_the_viewer_and_the_tag_returns_to_logs);
    RUN(the_editor_asks_before_any_tab_and_keeps_its_text_over_a_tab);
    RUN(a_running_job_keeps_its_box_and_takes_no_tab);
    RUN(a_box_takes_no_tab);
    RUN(the_strip_fits_in_both_languages_and_stands_above_each_first_row);
    RUN(the_wifi_button_opens_wifi_and_the_tag_returns_to_logs);
    RUN(wifi_levels_from_logs_climb_before_the_tag_returns_to_logs);
    RUN(armed_the_wifi_button_opens_wifi_and_its_buttons_refuse);
    RUN(a_press_that_slid_off_the_wifi_button_or_was_lost_opens_nothing);
    RUN(the_wifi_button_takes_no_press_under_a_question_or_a_file);
    RUN(the_wifi_button_shows_with_nothing_listed_and_files_has_none);
    RUN(the_wifi_button_label_fits_in_both_languages);
    fh_free(&g_h);
    free(s_fb);
    return test_summary("logs_strip");
}
