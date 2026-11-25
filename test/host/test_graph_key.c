/**
 * @file test_graph_key.c
 * @brief GRAPH on the result cards (graph_key.h): the key on LIMIT RUN,
 *        RUDDER AUTO, TEST, MOTOR & ESC and SUPPLY in each state -- no run,
 *        the run's file open, ARMED, written, USER LOGS off, the card
 *        swapped -- the tap that opens LOGS on the file in the graph, the
 *        home tag back to the card with its result still shown, two
 *        fingers, and a release the router flags TOUCH_FLAG_NO_TAP.
 *
 * The failures under test: a key that opens a file still being written,
 * opens one while ARMED, opens the same name on another card, opens with
 * USER LOGS off or before any run; a refusal nobody sees; a home tag that
 * climbs to LOGS' list instead of the card; a card that loses its result
 * on the way back; a second finger's release that taps the key; a release
 * slid onto the band that taps it.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "card_tree.h"
#include "graph_key.h"
#include "log_csv.h"
#include "log_name.h"
#include "log_select.h"
#include "log_viewer_screen.h"
#include "motor_screen.h"
#include "settings.h"
#include "splash_screen.h"
#include "supply_screen.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_theme.h"

/* Last: its layout macros have short names (W, H, PAD). */
#include "servo_screen_core_int.h"

/** @brief ui_router_int.h's: whether @p opener opens @p id, whose home
 *  tag then returns to it.  @param opener the card  @param id the screen
 *  opened  @return true for a pair of the router's openers */
bool ui_router_opens(ui_screen_id_t opener, ui_screen_id_t id);

/* ------------------------------------------------------- the fake card --- */

/** @brief One file of the fake card. */
typedef struct {
    const char *dir;   /**< its folder */
    const char *name;  /**< its name   */
    const char *text;  /**< its text   */
} gk_file_t;

/** @brief LIMIT RUN's CSV (P0's columns). */
static const char k_limit[] =
    "row;search;side;pulse (us);current (A);voltage (V);angle (deg);state;"
    "free (A);threshold (A);end point (us);info\n"
    "1;1;LOW;1490;0.050;5.00;-0.9;BASE;0.050;0.150;;\n"
    "2;1;LOW;1480;0.052;5.00;-1.8;FREE;0.050;0.150;;\n"
    "3;1;LOW;1470;0.210;4.98;-2.6;BOUND;0.050;0.150;1495;\n";
/** @brief RUDDER AUTO's CSV. */
static const char k_ruddr[] =
    "step;side (us);write;value;measured (deg);goal (deg);measured (mm);"
    "goal (mm);within;kind;pulse (us);current (A);info\n"
    "1;1000;1;150;-19.0;-20.0;-38.0;-40.0;0;STEP;1000;0.10;\n"
    "2;1000;2;160;-20.0;-20.0;-40.0;-40.0;1;STEP;1000;0.11;\n"
    "3;2000;1;150;19.5;20.0;39.0;40.0;1;STEP;2000;0.10;\n";
/** @brief An AUTOMATIC TEST's CSV. */
static const char k_atest[] =
    "time (s);test;step;phase;command (us);position (us);set (V);"
    "voltage (V);limit (A);current (A);power (W);mode;travel (ms);"
    "angle (deg);travel angle (ms)\n"
    "0.0;1;1;MOVE;1000;1500;6.0;5.9;2.0;0.10;0.6;CV;0;0;0\n"
    "0.1;1;1;MOVE;1000;1200;6.0;5.9;2.0;0.40;2.4;CV;0;0;0\n"
    "0.2;1;1;HOLD;1000;1000;6.0;5.9;2.0;0.20;1.2;CV;180;0;0\n";
/** @brief A MOTOR run's CSV. */
static const char k_motor[] =
    "time (s);voltage (V);current (A);power (W);rpm (rpm);throttle (%);"
    "esc (C);motor (C);charge (mAh);energy (Wh);ina voltage (V);"
    "ina current (A);esc current (A);window;source\n"
    "0.0;12.0;1.0;12.0;1000;10;30;30;0;0;0;0;0;0;measured\n"
    "0.1;11.9;2.0;23.8;2000;20;30;30;0;0;0;0;0;0;measured\n"
    "0.2;11.8;3.0;35.4;3000;30;30;30;0;0;0;0;0;0;measured\n";
/** @brief A SUPPLY run's CSV. */
static const char k_suppl[] =
    "time (s);set (V);voltage (V);limit (A);current (A);power (W);mode;"
    "charge (mAh);energy (Wh);source\n"
    "0.0;6.0;6.0;2.0;0.10;0.6;CV;0;0;measured\n"
    "0.1;6.0;5.9;2.0;0.50;3.0;CV;0;0;measured\n"
    "0.2;6.0;5.9;2.0;0.20;1.2;CV;0;0;measured\n";

/** @brief The card in the slot. */
static const gk_file_t k_card[] = {
    { CARD_USER_LOGS, "LIMIT013.CSV", k_limit },
    { CARD_USER_LOGS, "LIMIT013.TXT", "LIMIT RUN\n" },
    { CARD_USER_LOGS, "RUDDR004.CSV", k_ruddr },
    { CARD_USER_LOGS, "ATEST012.CSV", k_atest },
    { CARD_AUTO_LOGS, "MOTOR042.CSV", k_motor },
    { CARD_AUTO_LOGS, "SUPPL007.CSV", k_suppl },
};

static char g_dir[CARD_DIR_MAX];  /**< the folder the screen points at */
static unsigned g_mounts;         /**< the card's mount number          */
static log_mem_ctx_t g_mem;       /**< the open file's read position    */

/** @brief List hook: the files of the folder pointed at, as one page.
 *  @param out takes the entries  @param max_entries room  @param page the
 *  page asked for  @param ctx unused  @return the entries on the card */
static int fake_list(log_viewer_file_t *out, int max_entries,
                     log_viewer_page_t *page, void *ctx)
{
    (void)ctx;
    log_select_page_t sel;
    log_select_page_begin(&sel, out, max_entries, page);
    int n = 0;
    for (size_t i = 0; i < sizeof(k_card) / sizeof(k_card[0]); ++i) {
        if (strcmp(k_card[i].dir, g_dir) != 0) {
            continue;
        }
        log_viewer_file_t f;
        memset(&f, 0, sizeof(f));
        snprintf(f.name, sizeof(f.name), "%s", k_card[i].name);
        f.size = strlen(k_card[i].text);
        log_select_page_offer(&sel, &f);
        ++n;
    }
    (void)log_select_page_end(&sel, page);
    return n;
}

/** @brief Open hook: a file of the folder pointed at, from memory.
 *  @param name the file  @param src takes the source  @param ctx unused
 *  @return false for a name the folder does not hold */
static bool fake_open(const char *name, log_source_t *src, void *ctx)
{
    (void)ctx;
    for (size_t i = 0; i < sizeof(k_card) / sizeof(k_card[0]); ++i) {
        if (strcmp(k_card[i].dir, g_dir) == 0
            && strcmp(k_card[i].name, name) == 0) {
            log_source_memory(src, &g_mem, k_card[i].text,
                              strlen(k_card[i].text));
            return true;
        }
    }
    return false;
}

/** @brief Folder hook.  @param dir the folder  @param ctx unused */
static void fake_folder(const char *dir, void *ctx)
{
    (void)ctx;
    snprintf(g_dir, sizeof(g_dir), "%s", dir);
}

/** @brief Volume hook.  @param ctx unused  @return the card's label */
static const char *fake_volume(void *ctx)
{
    (void)ctx;
    return "BENCH01";
}

/** @brief Mount hook.  @param ctx unused  @return the mount number */
static unsigned fake_mounts(void *ctx)
{
    (void)ctx;
    return g_mounts;
}

/** @brief The card's hooks. */
static const log_viewer_io_t k_io = {
    .list = fake_list,
    .open = fake_open,
    .volume = fake_volume,
    .folder = fake_folder,
    .mounts = fake_mounts,
};

/* ------------------------------------------------------------ the rig --- */

static gfx_color_t *g_fb;   /**< the panel's frame */
static gfx_canvas_t g_cv;   /**< a canvas on it    */

/** @brief The bench's ARMED bit, as the band and SERVO read it.
 *  @param armed ARMED */
static void set_armed(bool armed)
{
    ui_bench_status_t st = *ui_router_status();
    st.armed = armed;
    ui_router_set_status(&st);
    servo_screen_set_armed(armed);
}

/** @brief The menu after a start: nothing run since power-on, SAFE, the
 *  card in the slot. */
static void start(void)
{
    if (g_fb == NULL) {
        g_fb = calloc((size_t)UI_PANEL_W * UI_PANEL_H, sizeof(gfx_color_t));
        REQUIRE_ALLOC(g_fb);
    }
    gfx_canvas_init(&g_cv, g_fb, UI_PANEL_W, UI_PANEL_H, UI_PANEL_W);
    settings_set_store(NULL);
    settings_init();
    ui_theme_set(UI_THEME_DARK);
    ui_text_set_language(UI_LANG_EN);
    ui_router_init();
    graph_key_reset();
    g_mounts = 1u;
    g_dir[0] = '\0';
    log_viewer_set_io(&k_io);
    for (int i = 0; i < SPLASH_STEP_COUNT; ++i) {
        splash_screen_set((splash_step_t)i, SPLASH_OK, "");
    }
    ui_router_tick(2.0f);
    ui_router_goto(SCREEN_OVERVIEW);
    set_armed(false);
}

/** @brief One touch event of contact @p id at body point (@p x, @p y).
 *  @param x x  @param y y in the body  @param type the event  @param id the
 *  contact */
static void touch(int x, int y, touch_event_type_t type, uint8_t id)
{
    touch_event_t e = { .type = type,
                        .point = { .id = id, .x = (int16_t)x,
                                   .y = (int16_t)(y + UI_BAND_H),
                                   .strength = 40 } };
    ui_router_event(&e);
}

/** @brief A frame: the tick and the drawing. */
static void frame(void)
{
    ui_router_tick(0.02f);
    ui_router_render(&g_cv, 0);
}

/** @brief A tap of contact 1 in the middle of @p r.  @param r the body
 *  rectangle */
static void tap_in(gfx_rect_t r)
{
    touch(r.x + r.w / 2, r.y + r.h / 2, TOUCH_EVENT_DOWN, 1);
    touch(r.x + r.w / 2, r.y + r.h / 2, TOUCH_EVENT_UP, 1);
}

/** @brief The home tag, tapped. */
static void tap_home(void)
{
    const gfx_rect_t home =
        ui_home_tag_rect(ui_router_title(ui_router_current()));
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1,
                                   .x = (int16_t)(home.x + home.w / 2),
                                   .y = (int16_t)(home.y + home.h / 2),
                                   .strength = 40 } };
    ui_router_event(&e);
    e.type = TOUCH_EVENT_UP;
    ui_router_event(&e);
}

/** @brief Whether a DISARM went out since the last look; takes them all.
 *  @return true for one */
static bool disarm_sent(void)
{
    bool any = false;
    servo_cmd_t c;
    for (int i = 0; i < 64; ++i) {
        c.kind = SERVO_CMD_NONE;
        servo_screen_take(&c);
        if (c.kind == SERVO_CMD_NONE) {
            break;
        }
        any = any || c.kind == SERVO_CMD_DISARM;
    }
    return any;
}

/** @brief Whether LOGS shows @p name open in the graph.  @param name the
 *  file  @return true then */
static bool logs_graph_of(const char *name)
{
    return ui_router_current() == SCREEN_LOGS
           && log_viewer_view() == LOG_VIEW_PLOT
           && strcmp(log_viewer_open_name(), name) == 0;
}

/* ----------------------------------------------------- the servo cards --- */

/** @brief The logger answers the last OPEN of outbox @p from with number
 *  @p n on the card mounted now.  @param from PEEK_*  @param n the number */
static void answer(uint8_t from, int n)
{
    s.files_from = from;
    servo_screen_test_files(n, true);
}

/** @brief LIMIT RUN shown, its last run ended PASS with a CSV, the logger
 *  answered @p n (0: not yet).  @param n the number */
static void limit_done(int n)
{
    ui_router_goto(SCREEN_SERVO_LIMRUN);
    s.lim.state = SERVO_LIMRUN_DONE;
    s.lim.out_done = true;              /* its lines and END handed over */
    s.lim.verdict = SERVO_LIMRUN_PASS;
    s.lim.rows_n = 3u;
    s.lim_off = false;
    s.lim_file = 0;
    answer(PEEK_LIM, n);
    s.logger_busy = false;
    frame();
}

/** @brief Whether LIMIT RUN's first state line reads @p t.  @param t the
 *  text  @return true then */
static bool lim_first_line(const char *t)
{
    char b[96];
    return strcmp(servo_screen_limit_line(0, b, sizeof(b)), t) == 0;
}

/** @brief Whether one of LIMIT RUN's state lines reads @p t.  @param t the
 *  text  @return true then */
static bool card_reads(const char *t)
{
    for (int i = 0; i < 8; ++i) {
        char b[96];
        if (strcmp(servo_screen_limit_line(i, b, sizeof(b)), t) == 0) {
            return true;
        }
    }
    return false;
}

/* The written run: GRAPH beside the report's line opens LOGS on
 * LIMIT013.CSV in the graph, the leave switches the bench to SAFE, and the
 * home tag on the graph returns to LIMIT RUN with the result still shown. */
TEST_CASE(limit_run_graph_opens_the_csv_and_the_tag_returns_to_the_card)
{
    start();
    limit_done(13);
    const gfx_rect_t key = servo_screen_graph_rect();
    CHECK(key.w == GRAPH_KEY_W && key.h == GRAPH_KEY_H);
    CHECK_EQ(graph_key_state(GRAPH_KEY_LIMIT), GRAPH_KEY_LIVE);
    /* Beside the report's line, flush right in the card. */
    int file = -1;
    for (int i = 0; i < 8; ++i) {
        char b[96];
        if (strcmp(servo_screen_limit_line(i, b, sizeof(b)), "LIMIT013.TXT")
            == 0) {
            file = i;
        }
    }
    CHECK(file >= 0);
    const int y = servo_screen_limit_line_y(file);
    CHECK(y >= key.y && y + 20 <= key.y + key.h);
    CHECK_EQ(key.x + key.w, UI_MARGIN + 10 + 600);
    (void)disarm_sent();
    tap_in(key);
    CHECK(logs_graph_of("LIMIT013.CSV"));
    CHECK(disarm_sent());
    CHECK(log_viewer_tab() == LOG_TAB_USER);
    frame();
    tap_home();
    CHECK_EQ(ui_router_current(), SCREEN_SERVO_LIMRUN);
    CHECK_EQ(s.lim.state, SERVO_LIMRUN_DONE);
    char want[64];
    snprintf(want, sizeof(want), TR(SV_LIM_VERDICT),
             ui_servo_str(servo_limrun_verdict_str(SERVO_LIMRUN_PASS)));
    CHECK(card_reads(want));
    CHECK(card_reads("LIMIT013.TXT"));
    /* LOGS entered from its tile later is no card's: its list, and its
     * home tag to the menu. */
    ui_router_goto(SCREEN_OVERVIEW);
    ui_router_goto(SCREEN_LOGS);
    CHECK(log_viewer_view() == LOG_VIEW_BROWSE);
    tap_home();
    CHECK(ui_router_current() != SCREEN_SERVO_LIMRUN);
}

/* No run, USER LOGS off, a run whose CSV is open or not yet numbered,
 * ARMED, and a run that judged no position: no key or a grey one whose tap
 * says why in the first line and stays. */
TEST_CASE(limit_run_graph_in_each_state)
{
    start();
    ui_router_goto(SCREEN_SERVO_LIMRUN);
    frame();
    CHECK_EQ(servo_screen_graph_rect().w, 0);           /* no run */
    CHECK_EQ(graph_key_state(GRAPH_KEY_LIMIT), GRAPH_KEY_HIDDEN);

    limit_done(0);                                      /* WRITING */
    gfx_rect_t key = servo_screen_graph_rect();
    CHECK(key.w > 0);
    CHECK_EQ(graph_key_state(GRAPH_KEY_LIMIT), GRAPH_KEY_WRITING);
    tap_in(key);
    CHECK_EQ(ui_router_current(), SCREEN_SERVO_LIMRUN);
    frame();
    CHECK(lim_first_line(TR(GK_WRITING)));
    /* Numbered, the logger still holding its END: still open.  The note
     * was the unnumbered run's, and goes with it. */
    answer(PEEK_LIM, 13);
    s.logger_busy = true;
    frame();
    CHECK_EQ(graph_key_state(GRAPH_KEY_LIMIT), GRAPH_KEY_WRITING);
    CHECK(!lim_first_line(TR(GK_WRITING)));
    tap_in(key);
    frame();
    CHECK(lim_first_line(TR(GK_WRITING)));
    /* The run's END still in its outbox: open. */
    s.logger_busy = false;
    s.lim.out_done = false;
    frame();
    CHECK_EQ(graph_key_state(GRAPH_KEY_LIMIT), GRAPH_KEY_WRITING);
    CHECK(lim_first_line(TR(GK_WRITING)));
    /* Closed: the note goes with its reason. */
    s.lim.out_done = true;
    frame();
    CHECK(!lim_first_line(TR(GK_WRITING)));
    CHECK_EQ(graph_key_state(GRAPH_KEY_LIMIT), GRAPH_KEY_LIVE);

    set_armed(true);                                    /* ARMED */
    frame();
    key = servo_screen_graph_rect();
    CHECK_EQ(graph_key_state(GRAPH_KEY_LIMIT), GRAPH_KEY_ARMED);
    tap_in(key);
    frame();
    CHECK_EQ(ui_router_current(), SCREEN_SERVO_LIMRUN);
    CHECK(lim_first_line(TR(GK_ARMED)));
    set_armed(false);
    frame();
    CHECK(!lim_first_line(TR(GK_ARMED)));

    s.lim_off = true;                                   /* USER LOGS OFF */
    frame();
    CHECK_EQ(servo_screen_graph_rect().w, 0);
    tap_in(key);
    CHECK_EQ(ui_router_current(), SCREEN_SERVO_LIMRUN);
    s.lim_off = false;
    s.lim.rows_n = 0u;                                  /* report alone */
    frame();
    CHECK_EQ(servo_screen_graph_rect().w, 0);
    s.lim.rows_n = 3u;
    answer(PEEK_LIM, -1);                               /* no file */
    frame();
    CHECK_EQ(servo_screen_graph_rect().w, 0);
}

/* The card swapped since the run: the same name on another card is not
 * opened, and the first line says the file is not on the card. */
TEST_CASE(limit_run_graph_refuses_another_card)
{
    start();
    limit_done(13);
    ++g_mounts;
    tap_in(servo_screen_graph_rect());
    CHECK_EQ(ui_router_current(), SCREEN_SERVO_LIMRUN);
    frame();
    char want[64];
    snprintf(want, sizeof(want), TR(MO_LOG_MISSING), "LIMIT013.CSV");
    CHECK(lim_first_line(want));
    /* A run numbered on this card opens again. */
    answer(PEEK_LIM, 13);
    s.lim_file = 0;
    answer(PEEK_LIM, 13);
    frame();
    tap_in(servo_screen_graph_rect());
    CHECK(logs_graph_of("LIMIT013.CSV"));
}

/* Two fingers: one on GRAPH, one elsewhere on the card.  The other
 * finger's release taps nothing; GRAPH acts on its own release.  A second
 * finger on the key itself takes no press of its own. */
TEST_CASE(limit_run_graph_acts_on_its_own_release_only)
{
    start();
    limit_done(13);
    const gfx_rect_t key = servo_screen_graph_rect();
    const int kx = key.x + key.w / 2, ky = key.y + key.h / 2;
    touch(kx, ky, TOUCH_EVENT_DOWN, 1);
    touch(100, 120, TOUCH_EVENT_DOWN, 2);               /* LOW END */
    touch(100, 120, TOUCH_EVENT_UP, 2);
    CHECK_EQ(ui_router_current(), SCREEN_SERVO_LIMRUN);
    touch(kx + 4, ky, TOUCH_EVENT_DOWN, 3);             /* on the key */
    touch(kx + 4, ky, TOUCH_EVENT_UP, 3);
    CHECK_EQ(ui_router_current(), SCREEN_SERVO_LIMRUN);
    touch(kx, ky, TOUCH_EVENT_UP, 1);
    CHECK(logs_graph_of("LIMIT013.CSV"));
}

/* A finger that slides from GRAPH onto the band: the router hands the card
 * a release flagged TOUCH_FLAG_NO_TAP, which opens nothing; nor does the
 * contact's real release on the band. */
TEST_CASE(limit_run_graph_ignores_a_no_tap_release)
{
    start();
    limit_done(13);
    const gfx_rect_t key = servo_screen_graph_rect();
    const int kx = key.x + key.w / 2, ky = key.y + key.h / 2;
    touch(kx, ky, TOUCH_EVENT_DOWN, 1);
    touch(kx, -20, TOUCH_EVENT_MOVE, 1);
    touch(kx, -20, TOUCH_EVENT_UP, 1);
    CHECK_EQ(ui_router_current(), SCREEN_SERVO_LIMRUN);
    /* The key still takes the next tap. */
    tap_in(key);
    CHECK(logs_graph_of("LIMIT013.CSV"));
}

/** @brief RUDDER AUTO shown, its last run ended PASS, the logger answered
 *  @p n.  @param n the number */
static void rauto_done(int n)
{
    ui_router_goto(SCREEN_SERVO_RAUTO);
    s.ra.run.state = SERVO_RAUTO_DONE;
    s.ra.run.out_done = true;
    s.ra.run.verdict = SERVO_RAUTO_PASS;
    s.ra.off = false;
    s.ra.file = 0;
    answer(PEEK_RAUTO, n);
    s.logger_busy = false;
    frame();
}

/** @brief Whether RUDDER AUTO's first line reads @p t.  @param t the text
 *  @return true then */
static bool ra_first_line(const char *t)
{
    char b[96];
    return strcmp(rauto_line(0, b, sizeof(b)), t) == 0;
}

/* RUDDER AUTO: GRAPH right of TOLERANCE on its row; each state; the tap to
 * RUDDR004.CSV and the home tag back with the result; two fingers and a
 * slid-off release. */
TEST_CASE(rudder_auto_graph_in_each_state)
{
    start();
    ui_router_goto(SCREEN_SERVO_RAUTO);
    frame();
    CHECK_EQ(servo_screen_graph_rect().w, 0);           /* no run */
    rauto_done(0);
    const gfx_rect_t key = servo_screen_graph_rect();
    const gfx_rect_t tol = rauto_rect(RA_C_TOL);
    CHECK(key.y == tol.y && key.h == tol.h && key.x == tol.x + tol.w + 10);
    CHECK_EQ(key.x + key.w, UI_MARGIN + LCARD_W - 10);
    tap_in(key);                                        /* WRITING */
    frame();
    CHECK_EQ(ui_router_current(), SCREEN_SERVO_RAUTO);
    CHECK(ra_first_line(TR(GK_WRITING)));
    answer(PEEK_RAUTO, 4);
    set_armed(true);                                    /* ARMED */
    frame();
    tap_in(key);
    frame();
    CHECK(ra_first_line(TR(GK_ARMED)));
    set_armed(false);
    s.ra.off = true;                                    /* USER LOGS OFF */
    frame();
    CHECK_EQ(servo_screen_graph_rect().w, 0);
    s.ra.off = false;
    frame();
    /* Two fingers: MARGIN's press waits behind GRAPH's. */
    const gfx_rect_t mar = rauto_rect(RA_C_MARGIN);
    touch(key.x + 10, key.y + 10, TOUCH_EVENT_DOWN, 1);
    touch(mar.x + 10, mar.y + 10, TOUCH_EVENT_DOWN, 2);
    touch(mar.x + 10, mar.y + 10, TOUCH_EVENT_UP, 2);
    CHECK_EQ(ui_router_current(), SCREEN_SERVO_RAUTO);
    CHECK(!s.kp.open);
    touch(key.x + 10, -20, TOUCH_EVENT_MOVE, 1);        /* slid off */
    touch(key.x + 10, -20, TOUCH_EVENT_UP, 1);
    CHECK_EQ(ui_router_current(), SCREEN_SERVO_RAUTO);
    tap_in(key);
    CHECK(logs_graph_of("RUDDR004.CSV"));
    frame();
    tap_home();
    CHECK_EQ(ui_router_current(), SCREEN_SERVO_RAUTO);
    CHECK_EQ(s.ra.run.state, SERVO_RAUTO_DONE);
    CHECK(rauto_shown());
}

/** @brief TEST shown with its run's box, the run ended, the logger
 *  answered @p n.  @param n the number */
static void test_done(int n)
{
    ui_router_goto(SCREEN_SERVO_TEST);
    s.test.state = SERVO_TEST_DONE;
    s.test.ended = true;                /* its END handed over */
    s.test.box_n = 0u;
    s.test_off = false;
    s.test_box = true;
    s.test_file = 0;
    answer(PEEK_TEST, n);
    s.logger_busy = false;
    frame();
}

/* TEST: GRAPH at the file's line in the run's box; each state; the tap to
 * ATEST012.CSV and the home tag back to TEST with the box still up. */
TEST_CASE(automatic_test_graph_in_each_state)
{
    start();
    ui_router_goto(SCREEN_SERVO_TEST);
    frame();
    CHECK_EQ(servo_screen_graph_rect().w, 0);           /* no run */
    test_done(0);
    const gfx_rect_t key = servo_screen_graph_rect();
    CHECK(key.w == GRAPH_KEY_W);
    tap_in(key);                                        /* WRITING */
    CHECK_EQ(ui_router_current(), SCREEN_SERVO_TEST);
    answer(PEEK_TEST, 12);
    set_armed(true);
    frame();
    CHECK_EQ(graph_key_state(GRAPH_KEY_ATEST), GRAPH_KEY_ARMED);
    tap_in(key);
    frame();
    CHECK_EQ(ui_router_current(), SCREEN_SERVO_TEST);
    CHECK_STR_EQ(graph_key_note(GRAPH_KEY_ATEST), TR(GK_ARMED));
    set_armed(false);
    s.test_off = true;                                  /* USER LOGS OFF */
    frame();
    CHECK_EQ(servo_screen_graph_rect().w, 0);
    s.test_off = false;
    s.test_box = false;                                 /* CLOSE */
    ++s.ctrl_rev;
    frame();
    CHECK_EQ(servo_screen_graph_rect().w, 0);
    s.test_box = true;                                  /* a run's end */
    ++s.ctrl_rev;
    frame();
    tap_in(key);
    CHECK(logs_graph_of("ATEST012.CSV"));
    frame();
    tap_home();
    CHECK_EQ(ui_router_current(), SCREEN_SERVO_TEST);
    CHECK_EQ(s.test.state, SERVO_TEST_DONE);
    CHECK(s.test_box);
}

/* ------------------------------------------------ MOTOR & ESC, SUPPLY --- */

/** @brief Card @p k's last log, @p name in @p dir on the mount now.
 *  @param k the card  @param name the file, NULL for no run  @param dir its
 *  folder  @param open a log is open */
static void last_log(graph_key_card_t k, const char *name, const char *dir,
                     bool open)
{
    const graph_key_file_t f = { .run = name != NULL, .open = open,
                                 .name = name, .dir = dir,
                                 .mount = g_mounts };
    graph_key_set(k, &f);
}

/* MOTOR & ESC: GRAPH left of RENAME LOG once a MOTOR log closed; grey with
 * a log open or ARMED, the reason in the alert band; the tap to
 * MOTOR042.CSV on the BENCH tab and the home tag back. */
TEST_CASE(motor_graph_in_each_state)
{
    start();
    ui_router_goto(SCREEN_MOTOR);
    frame();
    CHECK_EQ(motor_screen_graph_rect().w, 0);           /* no run */
    last_log(GRAPH_KEY_MOTOR, "MOTOR042.CSV", CARD_AUTO_LOGS, true);
    frame();
    const gfx_rect_t key = motor_screen_graph_rect();
    CHECK(key.w > 0);
    tap_in(key);                                        /* a log open */
    CHECK_EQ(ui_router_current(), SCREEN_MOTOR);
    CHECK_STR_EQ(ui_router_alert(), TR(GK_WRITING));
    last_log(GRAPH_KEY_MOTOR, "MOTOR042.CSV", CARD_AUTO_LOGS, false);
    set_armed(true);
    frame();
    tap_in(key);                                        /* ARMED */
    CHECK_EQ(ui_router_current(), SCREEN_MOTOR);
    CHECK_STR_EQ(ui_router_alert(), TR(GK_ARMED));
    set_armed(false);
    frame();
    /* Two fingers: RENAME LOG's press waits behind GRAPH's; a slid-off
     * release taps nothing. */
    touch(key.x + 10, key.y + 10, TOUCH_EVENT_DOWN, 1);
    const int rename_x = key.x + key.w + 8 + 20;    /* RENAME LOG */
    touch(rename_x, key.y + 10, TOUCH_EVENT_DOWN, 2);
    touch(rename_x, key.y + 10, TOUCH_EVENT_UP, 2);
    CHECK(!motor_screen_logname_shown());
    touch(key.x + 10, -20, TOUCH_EVENT_MOVE, 1);
    touch(key.x + 10, -20, TOUCH_EVENT_UP, 1);
    CHECK_EQ(ui_router_current(), SCREEN_MOTOR);
    tap_in(key);
    CHECK(logs_graph_of("MOTOR042.CSV"));
    CHECK(log_viewer_tab() == LOG_TAB_BENCH);
    frame();
    tap_home();
    CHECK_EQ(ui_router_current(), SCREEN_MOTOR);
    /* The card swapped: refused, in the band. */
    ++g_mounts;
    frame();
    tap_in(key);
    CHECK_EQ(ui_router_current(), SCREEN_MOTOR);
    char want[64];
    snprintf(want, sizeof(want), TR(MO_LOG_MISSING), "MOTOR042.CSV");
    CHECK_STR_EQ(ui_router_alert(), want);
}

/* SUPPLY: GRAPH right of RESET PEAKS once a SUPPL log closed; grey while
 * the output's log is open, the reason in the band; the tap to
 * SUPPL007.CSV and the home tag back; a slid-off release. */
TEST_CASE(supply_graph_in_each_state)
{
    start();
    ui_router_goto(SCREEN_SUPPLY);
    supply_screen_settings_loaded();
    frame();
    CHECK_EQ(supply_screen_graph_rect().w, 0);          /* no run */
    last_log(GRAPH_KEY_SUPPLY, "SUPPL007.CSV", CARD_AUTO_LOGS, true);
    frame();
    const gfx_rect_t key = supply_screen_graph_rect();
    CHECK(key.w > 0);
    tap_in(key);
    CHECK_EQ(ui_router_current(), SCREEN_SUPPLY);
    CHECK_STR_EQ(ui_router_alert(), TR(GK_WRITING));
    last_log(GRAPH_KEY_SUPPLY, "SUPPL007.CSV", CARD_AUTO_LOGS, false);
    frame();
    touch(key.x + 10, key.y + 10, TOUCH_EVENT_DOWN, 1);
    touch(key.x + 10, -20, TOUCH_EVENT_MOVE, 1);
    touch(key.x + 10, -20, TOUCH_EVENT_UP, 1);
    CHECK_EQ(ui_router_current(), SCREEN_SUPPLY);
    tap_in(key);
    CHECK(logs_graph_of("SUPPL007.CSV"));
    frame();
    tap_home();
    CHECK_EQ(ui_router_current(), SCREEN_SUPPLY);
}

/* ------------------------------------------------------- the shared key --- */

/* graph_key.h alone: each state, the note's life, a card outside the
 * enum, and the drawing of a hidden and a live key. */
TEST_CASE(the_key_follows_its_run_and_forgets_a_stale_note)
{
    start();
    for (int k = 0; k < GRAPH_KEY_COUNT; ++k) {
        const graph_key_card_t c = (graph_key_card_t)k;
        CHECK_EQ(graph_key_state(c), GRAPH_KEY_HIDDEN);
        CHECK(!graph_key_tap(c));
        CHECK(graph_key_note(c) == NULL);
        last_log(c, "", CARD_USER_LOGS, false);         /* no number yet */
        CHECK_EQ(graph_key_state(c), GRAPH_KEY_WRITING);
        last_log(c, "X001.CSV", CARD_USER_LOGS, true);
        CHECK_EQ(graph_key_state(c), GRAPH_KEY_WRITING);
        const uint32_t sig = graph_key_sig(c);
        CHECK(!graph_key_tap(c));
        CHECK_STR_EQ(graph_key_note(c), TR(GK_WRITING));
        CHECK(graph_key_sig(c) != sig);
        last_log(c, "X001.CSV", CARD_USER_LOGS, false);
        CHECK(graph_key_note(c) == NULL);               /* reason over */
        set_armed(true);
        CHECK_EQ(graph_key_state(c), GRAPH_KEY_ARMED);
        set_armed(false);
        CHECK_EQ(graph_key_state(c), GRAPH_KEY_LIVE);
        graph_key_set(c, NULL);
        CHECK_EQ(graph_key_state(c), GRAPH_KEY_HIDDEN);
    }
    const graph_key_card_t bad = GRAPH_KEY_COUNT;
    graph_key_set(bad, NULL);
    CHECK_EQ(graph_key_state(bad), GRAPH_KEY_HIDDEN);
    CHECK(!graph_key_tap(bad));
    CHECK(graph_key_note(bad) == NULL);
    /* A folder no tab lists: refused, named. */
    last_log(GRAPH_KEY_MOTOR, "MOTOR042.CSV", "ELSEWHERE", false);
    CHECK(!graph_key_tap(GRAPH_KEY_MOTOR));
    CHECK(graph_key_note(GRAPH_KEY_MOTOR) != NULL);
    /* Another file: the note goes. */
    last_log(GRAPH_KEY_MOTOR, "MOTOR043.CSV", "ELSEWHERE", false);
    CHECK(graph_key_note(GRAPH_KEY_MOTOR) == NULL);
    /* Hidden draws nothing; live draws GRAPH. */
    const gfx_rect_t r = { 10, 10, GRAPH_KEY_W, GRAPH_KEY_H };
    gfx_fill_rect(&g_cv, 0, 0, 120, 60, 0u);
    graph_key_draw(&g_cv, r, GRAPH_KEY_SUPPLY, false, &gfx_font_10x20);
    bool any = false;
    for (int y = 0; y < 60; ++y) {
        for (int x = 0; x < 120; ++x) {
            any = any || g_fb[(size_t)y * UI_PANEL_W + x] != 0u;
        }
    }
    CHECK(!any);
    graph_key_draw(&g_cv, r, GRAPH_KEY_MOTOR, false, &gfx_font_10x20);
    graph_key_draw(NULL, r, GRAPH_KEY_MOTOR, false, &gfx_font_10x20);
    for (int y = 0; y < 60; ++y) {
        for (int x = 0; x < 120; ++x) {
            any = any || g_fb[(size_t)y * UI_PANEL_W + x] != 0u;
        }
    }
    CHECK(any);
}

/* The router's openers: each card opens LOGS, whose home tag returns to
 * it. */
TEST_CASE(each_card_is_an_opener_of_logs)
{
    static const ui_screen_id_t k_cards[] = {
        SCREEN_SERVO_LIMRUN, SCREEN_SERVO_RAUTO, SCREEN_SERVO_TEST,
        SCREEN_MOTOR, SCREEN_SUPPLY,
    };
    for (size_t i = 0; i < sizeof(k_cards) / sizeof(k_cards[0]); ++i) {
        CHECK(ui_router_opens(k_cards[i], SCREEN_LOGS));
    }
    CHECK(!ui_router_opens(SCREEN_SERVO_LIVE, SCREEN_LOGS));
}

int main(void)
{
    RUN(limit_run_graph_opens_the_csv_and_the_tag_returns_to_the_card);
    RUN(limit_run_graph_in_each_state);
    RUN(limit_run_graph_refuses_another_card);
    RUN(limit_run_graph_acts_on_its_own_release_only);
    RUN(limit_run_graph_ignores_a_no_tap_release);
    RUN(rudder_auto_graph_in_each_state);
    RUN(automatic_test_graph_in_each_state);
    RUN(motor_graph_in_each_state);
    RUN(supply_graph_in_each_state);
    RUN(the_key_follows_its_run_and_forgets_a_stale_note);
    RUN(each_card_is_an_opener_of_logs);
    free(g_fb);
    return test_summary("graph_key");
}
