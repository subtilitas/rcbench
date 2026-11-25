/*
 * The panel's half of the YGE_PROG pages (yge_link.c) against the IOmcu's
 * (yge_port.c) on the simulated ESC of oy_sim.h, the exchanges made
 * directly.  Every command frame is recorded with its key.
 *
 * For test_yge_link.c, test_yge_link_fetch.c and test_yge_prog.c.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <string.h>

#include "yge_link.h"
#include "yge_port_rig.h"

static yge_link_t g_l;
static int g_drop;          /* exchanges not answered from now on */
static int g_drop_status;   /* reads of YGE_PROG not answered from now on */
static int g_nack_cmd;      /* command frames refused with LINK_NACK_BUSY */
static int g_exchanges;
static int g_commands;      /* command frames the page was handed */
static int g_data_reads;    /* reads of YGE_DATA */
static uint8_t g_corrupt;   /* read-copy blocks that come back with a wrong
                               value; 0xFF all of them */
static uint16_t g_cmd_op[64];  /* the commands, in order */
static uint16_t g_cmd_key[64]; /* their keys */

static RIG_MAY_GO_UNUSED int link_xchg(void *ctx, const yge_link_xfer_t *x,
                                       uint16_t *regs)
{
    (void)ctx;
    ++g_exchanges;
    if (g_drop > 0) {
        --g_drop;
        return YGE_LINK_NO_ANSWER;
    }
    if (x->write) {
        if (x->page != LINK_PAGE_YGE_PROG) {
            return yge_port_data_write(&g_p, x->off, x->n, x->regs);
        }
        if (x->off == 0u && g_commands < 64) {
            g_cmd_op[g_commands] = x->regs[LINK_YP_W_CMD];
            g_cmd_key[g_commands] = x->regs[LINK_YP_W_KEY];
        }
        g_commands += x->off == 0u ? 1 : 0;
        if (x->off == 0u && g_nack_cmd > 0) {
            --g_nack_cmd;
            return LINK_NACK_BUSY;
        }
        return yge_port_write(&g_p, x->off, x->n, x->regs, now_ms());
    }
    if (x->page == LINK_PAGE_YGE_PROG) {
        if (g_drop_status > 0) {
            --g_drop_status;
            return YGE_LINK_NO_ANSWER;
        }
        yge_port_read(&g_p, x->off, x->n, regs);
        return YGE_LINK_ACK;
    }
    ++g_data_reads;
    yge_port_data_read(&g_p, x->off, x->n, regs);
    if (g_corrupt != 0u && reg(LINK_YP_BLOCK) < LINK_YD_DRAFT) {
        regs[1] ^= 0x0001u;
        g_corrupt = g_corrupt == 0xFFu ? g_corrupt : (uint8_t)(g_corrupt - 1u);
    }
    return YGE_LINK_ACK;
}

/* @p ms of the bench, the panel's task every 5ms. */
static RIG_MAY_GO_UNUSED void link_run(uint32_t ms)
{
    for (uint32_t i = 0u; i < ms; i += 5u) {
        run_ms(5u);
        (void)yge_link_service(&g_l, now_ms(), link_xchg, NULL);
    }
}

/* Until an outcome comes, or @p ms have gone. */
static RIG_MAY_GO_UNUSED bool link_wait(yge_link_outcome_t *o, uint32_t ms)
{
    for (uint32_t i = 0u; i < ms; i += 5u) {
        link_run(5u);
        if (yge_link_outcome(&g_l, o)) {
            return true;
        }
    }
    return false;
}

/* The counters of the exchanges back to 0. */
static RIG_MAY_GO_UNUSED void link_counts_zero(void)
{
    g_drop = 0;
    g_drop_status = 0;
    g_nack_cmd = 0;
    g_exchanges = 0;
    g_commands = 0;
    g_data_reads = 0;
    g_corrupt = 0u;
}

/* The panel started; the page as it is. */
static RIG_MAY_GO_UNUSED void link_up(void)
{
    yge_link_init(&g_l);
    yge_link_came_up(&g_l, YGE_LINK_MINOR);
    link_counts_zero();
    link_run(10u);
}

/* The power an entry on the rig takes. */
static const yge_link_power_t k_pw = { LINK_YGE_SWITCHED, 7400u, 1000u,
                                       (uint16_t)OY_BOOT_MS_MIN };

/* OPEN and ENTER through the link, to an ESC that answered. */
static RIG_MAY_GO_UNUSED void link_ready(uint8_t port)
{
    yge_link_outcome_t o;

    CHECK_EQ(yge_link_open(&g_l, port, 1u), YGE_LINK_START_OK);
    CHECK(link_wait(&o, 500u));
    CHECK_EQ(o.kind, YGE_LINK_OUT_DONE);
    CHECK_EQ(o.state, LINK_YGE_OPEN);
    CHECK_EQ(yge_link_enter(&g_l, &k_pw, 2u), YGE_LINK_START_OK);
    CHECK(link_wait(&o, 30000u));
    CHECK_EQ(o.kind, YGE_LINK_OUT_DONE);
    CHECK_EQ(o.state, LINK_YGE_READY);
}

/* A READ through the link, to its end and the copy fetched. */
static RIG_MAY_GO_UNUSED void link_read(uint32_t id)
{
    yge_link_outcome_t o;

    CHECK_EQ(yge_link_read(&g_l, id), YGE_LINK_START_OK);
    CHECK(link_wait(&o, 30000u));
    CHECK_EQ(o.kind, YGE_LINK_OUT_DONE);
    CHECK_EQ(o.result, LINK_YGE_RESULT_DONE);
    link_run(200u);
}

/* How many of the recorded commands are @p op. */
static RIG_MAY_GO_UNUSED int commands_of(link_yge_op_t op)
{
    int n = 0;

    for (int i = 0; i < g_commands && i < 64; ++i) {
        n += g_cmd_op[i] == (uint16_t)op ? 1 : 0;
    }
    return n;
}
