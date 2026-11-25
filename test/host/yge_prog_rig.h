/*
 * The model of programming for YGE ESCs (yge_prog.c) on the panel's half of
 * the pages (yge_link_rig.h): the control task's handling of a command, the
 * feed it publishes, and a session connected and read.
 *
 * For test_yge_prog.c and test_yge_prog_ends.c.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "yge_link_rig.h"
#include "yge_prog.h"

static yge_prog_t g_m;
static yge_prog_gate_t g_gate;
static yge_prog_feed_t g_f;
static uint32_t g_answer_id;
static uint8_t g_answer;
static uint32_t g_outcomes;
static yge_link_outcome_t g_out;
static bool g_stopped;
static bool g_armed;
static RIG_MAY_GO_UNUSED char g_text[OY_FILE_MAX];
static RIG_MAY_GO_UNUSED char g_tag[8];
static RIG_MAY_GO_UNUSED uint32_t g_card_id;

/* What the control task does with a command (firmware/panel/main/panel_yge.c). */
static RIG_MAY_GO_UNUSED void carry_out(const yge_prog_cmd_t *c)
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

/* The feed as the control task publishes it. */
static RIG_MAY_GO_UNUSED void feed_now(void)
{
    yge_link_outcome_t o;

    if (yge_link_outcome(&g_l, &o)) {
        g_out = o;
        ++g_outcomes;
    }
    g_f.now_ms = now_ms();
    yge_link_readout(&g_l, &g_f.rd);
    g_f.image = *yge_link_image(&g_l);
    g_f.answer_id = g_answer_id;
    g_f.answer = g_answer;
    g_f.outcomes = g_outcomes;
    g_f.outcome = g_out;
    g_f.stopped = g_stopped;
    g_f.armed = g_armed;
    yge_prog_feed(&g_m, &g_f);
}

/* @p ms of the bench: the link's task and the frame every 5ms. */
static RIG_MAY_GO_UNUSED void model_run(uint32_t ms)
{
    static yge_prog_cmd_t c;

    for (uint32_t i = 0u; i < ms; i += 5u) {
        if (yge_prog_next_cmd(&g_m, &c)) {
            carry_out(&c);
        }
        link_run(5u);
        feed_now();
    }
}

static RIG_MAY_GO_UNUSED void model_up(void)
{
    link_up();
    yge_prog_init(&g_m);
    yge_prog_gate_init(&g_gate);
    yge_prog_gate_set(&g_m, &g_gate);
    memset(&g_f, 0, sizeof(g_f));
    g_answer_id = 0u;
    g_answer = 0u;
    g_outcomes = 0u;
    g_stopped = false;
    g_armed = false;
    model_run(10u);
}

static const yge_prog_connect_t k_conn = {
    0u, { LINK_YGE_SWITCHED, 7400u, 1000u, (uint16_t)OY_BOOT_MS_MIN }
};

static RIG_MAY_GO_UNUSED void connected(void)
{
    CHECK_EQ(yge_prog_connect(&g_m, &k_conn), YGE_PROG_MSG_NONE);
    model_run(30000u);
    CHECK_EQ(yge_prog_message(&g_m, NULL), YGE_PROG_MSG_READ_DONE);
    CHECK_EQ(yge_port_state(&g_p), LINK_YGE_READY);
    CHECK(g_f.image.gen != 0u);
    CHECK_EQ(yge_prog_backup_where(&g_m), YGE_PROG_BACKUP_HELD);
}

/* The read copy with index @p i set to @p v. */
static RIG_MAY_GO_UNUSED const uint16_t *draft_with(unsigned i, uint16_t v)
{
    static uint16_t d[YGE_LINK_VALUES_MAX];

    memcpy(d, g_f.image.value, sizeof(d));
    d[i] = v;
    return d;
}

