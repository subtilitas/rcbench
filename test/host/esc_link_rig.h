/*
 * The panel's half of the ESC_WIRE pages (esc_link.c) against the IOmcu's
 * (esc_port.c) on the simulated ESC of ew_sim.h, the exchanges made directly.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <string.h>

#include "esc_link.h"
#include "esc_port_rig.h"

static esc_link_t g_l;
static int g_drop;          /* exchanges not answered from now on */
static int g_drop_cmd;      /* commands the page takes and does not answer */
static int g_exchanges;
static int g_commands;
static uint8_t g_corrupt;   /* read-copy blocks come back with a wrong byte */

static int link_xchg(void *ctx, const esc_link_xfer_t *x, uint16_t *regs)
{
    (void)ctx;
    ++g_exchanges;
    if (g_drop > 0) {
        --g_drop;
        return ESC_LINK_NO_ANSWER;
    }
    uint8_t nack = 0u;
    const uint32_t now = esc.now / 1000u;

    if (x->write) {
        if (x->page == LINK_PAGE_ESC_WIRE) {
            nack = esc_port_write(&g_p, x->off, x->n, x->regs, now);
            if (x->off == 0u) {
                ++g_commands;
                if (g_drop_cmd > 0) {
                    --g_drop_cmd;
                    return ESC_LINK_NO_ANSWER;
                }
            }
        } else {
            nack = esc_port_data_write(&g_p, x->off, x->n, x->regs);
        }
        return nack;
    }
    if (x->page == LINK_PAGE_ESC_WIRE) {
        esc_port_read(&g_p, x->off, x->n, regs);
    } else {
        esc_port_data_read(&g_p, x->off, x->n, regs);
        if (g_corrupt != 0u && reg(LINK_EW_BLOCK) < LINK_ED_DRAFT) {
            regs[0] ^= 0x0101u;
        }
    }
    return ESC_LINK_ACK;
}

/* @p ms of the bench, the panel's task every 5ms. */
static void link_run(uint32_t ms)
{
    for (uint32_t i = 0u; i < ms; i += 5u) {
        run_ms(5u);
        (void)esc_link_service(&g_l, esc.now / 1000u, link_xchg, NULL);
    }
}

/* Until an outcome comes, or @p ms have gone. */
static bool link_wait(esc_link_outcome_t *o, uint32_t ms)
{
    for (uint32_t i = 0u; i < ms; i += 5u) {
        link_run(5u);
        if (esc_link_outcome(&g_l, o)) {
            return true;
        }
    }
    return false;
}

/* The panel started, the page free. */
static void link_up(void)
{
    esc_link_init(&g_l);
    esc_link_came_up(&g_l, ESC_LINK_MINOR);
    g_drop = 0;
    g_drop_cmd = 0;
    g_exchanges = 0;
    g_commands = 0;
    g_corrupt = 0u;
    link_run(10u);
}

/* The power an entry on the rig takes. */
static const esc_link_power_t k_pw = { LINK_ESC_SWITCHED, 7400u, 1000u, 3000u,
                                       1000u };

/* OPEN and ENTER through the link, to an ESC that answered. */
static void link_ready(uint16_t target)
{
    esc_link_outcome_t o;

    CHECK_EQ(esc_link_open(&g_l, target, 1u), ESC_LINK_START_OK);
    CHECK(link_wait(&o, 500u));
    CHECK_EQ(o.kind, ESC_LINK_OUT_DONE);
    CHECK_EQ(esc_link_enter(&g_l, &k_pw, 2u), ESC_LINK_START_OK);
    CHECK(link_wait(&o, 30000u));
    CHECK_EQ(o.kind, ESC_LINK_OUT_DONE);
    CHECK_EQ(o.state, LINK_ESC_READY);
}
