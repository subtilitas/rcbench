/*
 * The bench the ESC signal-wire session's suites run on: the session's page set,
 * its model over the panel's and the IOmcu's half on the simulated ESC, and a card
 * in memory behind esc_card_run().
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdio.h>

#include "esc_card.h"
#include "esc_wire_panel.h"
#include "motor_screen.h"
#include "settings.h"
#include "supply_screen.h"
#include "ui_screen.h"
#include "ui_text.h"

#include "esc_link_rig.h"

/** @brief The session's area on MOTOR & ESC. */
static const gfx_rect_t k_ew_area = { 0, PROG_FULL_Y, UI_PANEL_W,
                                      (int16_t)(UI_BODY_H - PROG_FULL_Y) };

static esc_prog_gate_t g_gate;
static esc_prog_feed_t g_f;
static uint32_t g_answer_id;
static uint8_t  g_answer;
static uint32_t g_outcomes;
static esc_link_outcome_t g_out;
static bool     g_stopped;
static bool     g_armed;
static bool     g_hand;     /* g_f is the test's: no bench behind it */

/* The card: names and texts in memory; a record apart. */
#define CARD_FILES 16
static char   g_names[CARD_FILES][SERVO_STORE_NAME_MAX];
static char   g_texts[CARD_FILES][EW_FILE_MAX + 1u];
static int    g_card_files;
static bool   g_card_in;
static bool   g_card_refuses;
static char   g_record[ESC_CARD_TEXT_MAX + 1u];
static size_t g_record_len;
static int    g_card_requests;
static esc_card_req_t  g_req;
static esc_card_done_t g_done;
/** @brief The card's task is slow: a request stays where it is. */
static bool   g_card_wait;

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

static servo_store_io_result_t io_create(void *ctx, const char *name, const char *data,
                                         size_t len)
{
    (void)ctx;
    if (g_card_refuses) {
        return SERVO_STORE_IO_DENIED;
    }
    if (find_file(name) >= 0) {
        return SERVO_STORE_IO_EXISTS;
    }
    if (g_card_files >= CARD_FILES || len > EW_FILE_MAX) {
        return SERVO_STORE_IO_FULL;
    }
    snprintf(g_names[g_card_files], sizeof(g_names[0]), "%s", name);
    memcpy(g_texts[g_card_files], data, len);
    g_texts[g_card_files][len] = '\0';
    ++g_card_files;
    return SERVO_STORE_IO_OK;
}

static const servo_store_io_t k_ew_io = {
    .ctx = NULL, .stem = EW_FILE_STEM, .mounted = io_mounted, .walk = io_walk,
    .size = io_size, .read = io_read, .create = io_create,
};

/** @brief The log task's side, at once. */
static void card_serve(void)
{
    if (esc_wire_panel_card_look()) {
        /* A tap looks; the rig's card is there or not. */
    }
    if (g_card_wait) {
        return;
    }
    if (!esc_wire_panel_card_take(&g_req)) {
        return;
    }
    ++g_card_requests;
    if (g_req.op == (uint8_t)ESC_CARD_RECORD) {
        memset(&g_done, 0, sizeof(g_done));
        g_done.op = g_req.op;
        g_done.id = g_req.id;
        g_done.result = !g_card_in ? ESC_CARD_DONE_NO_CARD
                      : g_card_refuses ? ESC_CARD_DONE_DENIED : ESC_CARD_DONE_OK;
        if (g_done.result == ESC_CARD_DONE_OK) {
            memcpy(g_record, g_req.text, g_req.len);
            g_record[g_req.len] = '\0';
            g_record_len = g_req.len;
        }
    } else {
        esc_card_run(&k_ew_io, &g_req, &g_done);
    }
    esc_wire_panel_card_done(&g_done);
}

static void carry_out(const esc_prog_cmd_t *c)
{
    esc_link_start_t a = ESC_LINK_START_ARG;

    if (c->kind != (uint8_t)ESC_PROG_CMD_ABORT
        && !esc_prog_gate_claim(&g_gate, c->id)) {
        return;
    }
    switch (c->kind) {
    case ESC_PROG_CMD_OPEN:
        a = esc_link_open(&g_l, c->target, c->id);
        break;
    case ESC_PROG_CMD_ENTER:
        a = esc_link_enter(&g_l, &c->power, c->id);
        break;
    case ESC_PROG_CMD_POWERED:
        a = esc_link_powered(&g_l, c->id);
        break;
    case ESC_PROG_CMD_READ:
        a = esc_link_read(&g_l, c->id);
        break;
    case ESC_PROG_CMD_WRITE:
        a = esc_link_write(&g_l, c->draft, c->len, c->reads, c->read_crc, c->id);
        break;
    case ESC_PROG_CMD_RESTORE:
        a = esc_link_restore(&g_l, c->draft, c->len, c->identity, c->reads,
                             c->read_crc, c->id);
        break;
    case ESC_PROG_CMD_LEAVE:
        a = esc_link_leave(&g_l, c->id);
        break;
    case ESC_PROG_CMD_CLEAR:
        a = esc_link_clear(&g_l, c->id);
        break;
    case ESC_PROG_CMD_CLOSE:
        a = esc_link_close(&g_l, c->id);
        break;
    default:
        esc_link_abort(&g_l);
        return;
    }
    g_answer = (uint8_t)a;
    g_answer_id = c->id;
}

/** @brief @p ms of the bench: the link's task, the frame and the card every 5ms. */
static void bench_run(uint32_t ms)
{
    static esc_prog_cmd_t c;

    for (uint32_t i = 0u; i < ms; i += 5u) {
        if (g_hand) {
            esc_wire_panel_feed(&g_f);
            continue;
        }
        if (esc_wire_panel_next_cmd(&c)) {
            carry_out(&c);
        }
        link_run(5u);
        esc_link_outcome_t o;

        if (esc_link_outcome(&g_l, &o)) {
            g_out = o;
            ++g_outcomes;
        }
        g_f.now_ms = esc.now / 1000u;
        esc_link_readout(&g_l, &g_f.rd);
        g_f.image = *esc_link_image(&g_l);
        g_f.answer_id = g_answer_id;
        g_f.answer = g_answer;
        g_f.outcomes = g_outcomes;
        g_f.outcome = g_out;
        g_f.stopped = g_stopped;
        g_f.armed = g_armed;
        esc_wire_panel_feed(&g_f);
        card_serve();
        esc_wire_pages.tick(0.005f);
    }
}

/** @brief A press and a release at the middle of @p r. */
static void ew_tap(gfx_rect_t r)
{
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = (int16_t)(r.x + r.w / 2),
                                   .y = (int16_t)(r.y + r.h / 2), .strength = 40 } };

    CHECK(r.w > 0);
    esc_wire_pages.event(&e);
    e.type = TOUCH_EVENT_UP;
    esc_wire_pages.event(&e);
    bench_run(5u);
}

/** @brief @p s of a hold of the 2s control, the bench running. */
static void ew_hold(float s)
{
    const gfx_rect_t r = esc_wire_panel_hold_rect();
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = (int16_t)(r.x + r.w / 2),
                                   .y = (int16_t)(r.y + r.h / 2), .strength = 40 } };

    CHECK(r.w > 0);
    esc_wire_pages.event(&e);
    bench_run((uint32_t)(s * 1000.0f));
    e.type = TOUCH_EVENT_UP;
    esc_wire_pages.event(&e);
    bench_run(5u);
}

/** @brief Types @p text on the keypad, then OK. */
static void kp_type(const char *text)
{
    const ui_keypad_t *k = esc_wire_panel_keypad();

    static const ui_key_t k_digit[10] = { UI_KEY_0, UI_KEY_1, UI_KEY_2, UI_KEY_3,
                                          UI_KEY_4, UI_KEY_5, UI_KEY_6, UI_KEY_7,
                                          UI_KEY_8, UI_KEY_9 };

    for (const char *p = text; *p != '\0'; ++p) {
        ew_tap(ui_keypad_key_rect(k, *p == '.' ? UI_KEY_DOT : k_digit[*p - '0']));
    }
    ew_tap(ui_keypad_key_rect(k, UI_KEY_OK));
}

/** @brief The session started over on the rig's ESC, SUPPLY at 7.40V 1.00A. */
static void session_up(void (*esc_up)(void))
{
    static const esc_wire_target_t k_t[2] = { { 2u, 5u },
                                              { LINK_EW_TARGET_PORT, 13u } };

    esc_up();
    link_up();
    settings_init();
    ui_router_init();
    esc_prog_gate_init(&g_gate);
    esc_wire_panel_gate(&g_gate);
    esc_wire_pages.init(k_ew_area);
    esc_prog_gate_set(esc_wire_panel_model(), &g_gate);
    memset(&g_f, 0, sizeof(g_f));
    g_answer_id = 0u;
    g_answer = 0u;
    g_outcomes = 0u;
    g_stopped = false;
    g_armed = false;
    g_hand = false;
    g_card_files = 0;
    g_card_in = true;
    g_card_refuses = false;
    g_card_wait = false;
    g_record[0] = '\0';
    g_record_len = 0u;
    g_card_requests = 0;
    ui_text_set_language(UI_LANG_EN);
    const supply_caps_t caps = SUPPLY_CAPS_PPS_DEFAULT;

    supply_screen_set_caps(&caps);
    supply_screen_settings_loaded();
    supply_screen_put(7.4f, 1.0f);
    esc_wire_panel_set_targets(k_t, 2u, true);
    esc_wire_pages.open();
    bench_run(20u);
}

/** @brief CONNECT held, and the bench run until the ESC was read. */
static void connected(void)
{
    ew_hold(2.2f);
    for (int i = 0; i < 4000 && esc_wire_panel_stage() != ESC_WIRE_SESSION; ++i) {
        bench_run(5u);
    }
    CHECK_EQ(esc_wire_panel_stage(), ESC_WIRE_SESSION);
}
