/*
 * The bench the suites of programming for YGE ESCs' screens run on: the
 * session's page set (yge_panel*.c), its model over the panel's half of the
 * pages and the IOmcu's on the simulated ESC (yge_link_rig.h), and a card in
 * memory behind yge_card_run().
 *
 * For test_yge_panel.c, test_yge_panel_write.c and test_yge_panel_stages.c.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdio.h>

#include "port_view.h"
#include "settings.h"
#include "supply_screen.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "yge_card.h"
#include "yge_panel.h"
#include "yge_panel_int.h"

#include "yge_link_rig.h"

/* The session's area on MOTOR & ESC. */
static const gfx_rect_t k_yg_area = { 0, PROG_FULL_Y, UI_PANEL_W,
                                      (int16_t)(UI_BODY_H - PROG_FULL_Y) };

static yge_prog_gate_t g_gate;
static yge_prog_feed_t g_f;
static uint32_t g_answer_id;
static uint8_t g_answer;
static uint32_t g_outcomes;
static yge_link_outcome_t g_out;
static bool g_stopped;
static bool g_armed;
static bool g_link_down;   /* the feed says no IOmcu answers */

/* The card: names and texts in memory. */
#define CARD_FILES 16
static char g_names[CARD_FILES][SERVO_STORE_NAME_MAX];
static char g_texts[CARD_FILES][OY_FILE_MAX + 1u];
static int g_card_files;
static bool g_card_in;
static bool g_card_wait;    /* the card's task is slow: a request stays */
static int g_card_requests;
static yge_card_req_t g_req;
static yge_card_done_t g_done;

static bool io_mounted(void *ctx)
{
    (void)ctx;
    return g_card_in;
}

static bool io_walk(void *ctx, servo_store_visit_fn visit, void *vctx)
{
    (void)ctx;
    for (int i = 0; i < g_card_files; ++i) {
        visit(g_names[i], false, vctx);
    }
    return true;
}

static int find_file(const char *name)
{
    for (int i = 0; i < g_card_files; ++i) {
        if (strcmp(g_names[i], name) == 0) {
            return i;
        }
    }
    return -1;
}

static int32_t io_size(void *ctx, const char *name)
{
    const int i = find_file(name);

    (void)ctx;
    return i < 0 ? -1 : (int32_t)strlen(g_texts[i]);
}

static int32_t io_read(void *ctx, const char *name, char *buf, size_t cap)
{
    const int i = find_file(name);

    (void)ctx;
    if (i < 0) {
        return -1;
    }
    size_t n = strlen(g_texts[i]);

    n = n < cap ? n : cap;
    memcpy(buf, g_texts[i], n);
    return (int32_t)n;
}

static servo_store_io_result_t io_create(void *ctx, const char *name,
                                         const char *data, size_t len)
{
    (void)ctx;
    if (find_file(name) >= 0) {
        return SERVO_STORE_IO_EXISTS;
    }
    if (g_card_files >= CARD_FILES || len > OY_FILE_MAX) {
        return SERVO_STORE_IO_FULL;
    }
    snprintf(g_names[g_card_files], sizeof(g_names[0]), "%s", name);
    memcpy(g_texts[g_card_files], data, len);
    g_texts[g_card_files][len] = '\0';
    ++g_card_files;
    return SERVO_STORE_IO_OK;
}

static servo_store_io_result_t io_remove(void *ctx, const char *name)
{
    (void)ctx;
    (void)name;
    return SERVO_STORE_IO_ERROR;
}

static const servo_store_io_t k_yg_io = {
    .ctx = NULL, .stem = OY_FILE_STEM, .mounted = io_mounted,
    .walk = io_walk, .size = io_size, .read = io_read, .create = io_create,
    .tagged = true, .remove = io_remove,
};

/* The log task's side, at once. */
static void card_serve(void)
{
    (void)yge_panel_card_look();
    if (g_card_wait || !yge_panel_card_take(&g_req)) {
        return;
    }
    ++g_card_requests;
    yge_card_run(&k_yg_io, &g_req, &g_done);
    yge_panel_card_done(&g_done);
}

/* What the control task does with a command (firmware/panel/main/panel_yge.c). */
static void carry_out(const yge_prog_cmd_t *c)
{
    yge_link_start_t a = YGE_LINK_START_ARG;

    if (c->kind != YGE_PROG_CMD_ABORT && !yge_prog_gate_claim(&g_gate, c->id)) {
        return;
    }
    if (!yge_prog_cmd_allowed(c->kind, g_stopped)) {
        g_answer = (uint8_t)YGE_LINK_START_BUSY;
        g_answer_id = c->id;
        return;
    }
    switch (c->kind) {
    case YGE_PROG_CMD_OPEN:
        a = yge_link_open(&g_l, c->port, c->id);
        break;
    case YGE_PROG_CMD_ENTER:
        a = yge_link_enter(&g_l, &c->power, c->id);
        break;
    case YGE_PROG_CMD_HAND_DONE:
        a = yge_link_hand_done(&g_l, c->id);
        break;
    case YGE_PROG_CMD_READ:
        a = yge_link_read(&g_l, c->id);
        break;
    case YGE_PROG_CMD_WRITE:
        a = yge_link_write(&g_l, c->draft, c->n, c->reads, c->read_crc, c->id);
        break;
    case YGE_PROG_CMD_LEAVE:
        a = yge_link_leave(&g_l, c->id);
        break;
    case YGE_PROG_CMD_CLEAR:
        a = yge_link_clear(&g_l, c->id);
        break;
    case YGE_PROG_CMD_CLOSE:
        a = yge_link_close(&g_l, c->id);
        break;
    default:
        yge_link_abort(&g_l);
        return;
    }
    g_answer = (uint8_t)a;
    g_answer_id = c->id;
}

/* One frame's feed, as the control task publishes it. */
static void feed_now(void)
{
    yge_link_outcome_t o;

    if (yge_link_outcome(&g_l, &o)) {
        g_out = o;
        ++g_outcomes;
    }
    g_f.now_ms = now_ms();
    yge_link_readout(&g_l, &g_f.rd);
    if (g_link_down) {
        g_f.rd.phase = YGE_LINK_DOWN;
    }
    g_f.image = *yge_link_image(&g_l);
    g_f.answer_id = g_answer_id;
    g_f.answer = g_answer;
    g_f.outcomes = g_outcomes;
    g_f.outcome = g_out;
    g_f.stopped = g_stopped;
    g_f.armed = g_armed;
    yge_panel_feed(&g_f);
}

/* @p ms of the bench: the link's task, the frame and the card every 5ms. */
static RIG_MAY_GO_UNUSED void bench_run(uint32_t ms)
{
    static yge_prog_cmd_t c;

    for (uint32_t i = 0u; i < ms; i += 5u) {
        if (yge_panel_next_cmd(&c)) {
            carry_out(&c);
        }
        link_run(5u);
        feed_now();
        card_serve();
        yge_pages.tick(0.005f);
    }
}

/* A press and a release at the middle of @p r. */
static RIG_MAY_GO_UNUSED void yg_tap(gfx_rect_t r)
{
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = (int16_t)(r.x + r.w / 2),
                                   .y = (int16_t)(r.y + r.h / 2),
                                   .strength = 40 } };

    CHECK(r.w > 0);
    yge_pages.event(&e);
    e.type = TOUCH_EVENT_UP;
    yge_pages.event(&e);
    bench_run(5u);
}

/* A finger on the 2s control, @p ms of the bench, and off. */
static RIG_MAY_GO_UNUSED void yg_hold_ms(uint32_t ms)
{
    const gfx_rect_t r = yge_panel_hold_rect();
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = (int16_t)(r.x + r.w / 2),
                                   .y = (int16_t)(r.y + r.h / 2),
                                   .strength = 40 } };

    CHECK(r.w > 0);
    yge_pages.event(&e);
    bench_run(ms);
    e.type = TOUCH_EVENT_UP;
    yge_pages.event(&e);
    bench_run(5u);
}

/* Types @p text on the keypad, then OK. */
static RIG_MAY_GO_UNUSED void kp_type(const char *text)
{
    const ui_keypad_t *k = yge_panel_keypad();
    static const ui_key_t k_digit[10] = { UI_KEY_0, UI_KEY_1, UI_KEY_2, UI_KEY_3,
                                          UI_KEY_4, UI_KEY_5, UI_KEY_6, UI_KEY_7,
                                          UI_KEY_8, UI_KEY_9 };

    for (const char *p = text; *p != '\0'; ++p) {
        yg_tap(ui_keypad_key_rect(k, *p == '.' ? UI_KEY_DOT : k_digit[*p - '0']));
    }
    yg_tap(ui_keypad_key_rect(k, UI_KEY_OK));
}

/* The session started over, SUPPLY at 7.40V 1.00A, port 1 on GPIO 13, the
 * boot wait at its lowest so a connect is quick. */
static RIG_MAY_GO_UNUSED void session_up(void)
{
    port_view_t v;

    rig_up();
    link_up();
    settings_init();
    ui_router_init();
    yge_prog_gate_init(&g_gate);
    yge_panel_gate(&g_gate);
    yge_pages.init(k_yg_area);
    memset(&g_f, 0, sizeof(g_f));
    g_answer_id = 0u;
    g_answer = 0u;
    g_outcomes = 0u;
    g_stopped = false;
    g_armed = false;
    g_link_down = false;
    g_card_files = 0;
    g_card_in = true;
    g_card_wait = false;
    g_card_requests = 0;
    ui_text_set_language(UI_LANG_EN);
    const supply_caps_t caps = SUPPLY_CAPS_PPS_DEFAULT;

    supply_screen_set_caps(&caps);
    supply_screen_settings_loaded();
    supply_screen_put(7.4f, 1.0f);
    port_view_unknown(&v);
    v.count = 4u;
    v.pin[0] = 13u;
    v.pin[1] = 14u;
    v.pin[2] = 15u;
    v.pin[3] = 16u;
    port_view_set(&v);
    yge_pages.open();
    ygp_s.boot_ms = (uint16_t)OY_BOOT_MS_MIN;
    bench_run(20u);
}

/* CONNECT held, and the bench run until the ESC was read. */
static RIG_MAY_GO_UNUSED void connected(void)
{
    yg_hold_ms(2100u);
    for (int i = 0; i < 6000 && yge_panel_stage() != YGE_PANEL_SESSION; ++i) {
        bench_run(5u);
    }
    CHECK_EQ(yge_panel_stage(), YGE_PANEL_SESSION);
}

/* The tab @p t shown. */
static RIG_MAY_GO_UNUSED void tab(int t)
{
    yg_tap(yge_panel_tab_rect(t));
    CHECK_EQ(ygp_s.tabs.selected, t);
}

/* The row of tab @p t named @p name: its place. */
static RIG_MAY_GO_UNUSED int row_named(int t, const char *name)
{
    tab(t);
    for (int i = 0; yge_panel_row_name(i) != NULL; ++i) {
        if (strcmp(yge_panel_row_name(i), name) == 0) {
            return i;
        }
    }
    return -1;
}

/* The choice labelled @p label in the list open: tapped. */
static RIG_MAY_GO_UNUSED void choose(const char *label)
{
    for (int i = 0; yge_panel_choice_label(i) != NULL; ++i) {
        if (strcmp(yge_panel_choice_label(i), label) == 0) {
            yg_tap(yge_panel_choice_rect(i));
            return;
        }
    }
    CHECK(false);
}
