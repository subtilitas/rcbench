/*
 * The servo rail's meter from the part to the panel, for a host suite: the
 * modelled INA3221 of fake_ina.h, read by the coprocessor's 1 ms schedule
 * (sense_svc), published into its pages (sense_page) every millisecond, and
 * read by the panel's sense_link over a link this header models.
 *
 * One clock runs both ends.  The coprocessor steps once per millisecond.
 * The panel's tick is the same clock plus an offset, so a suite puts the
 * 2^32 ms wrap wherever it wants it.  An exchange takes ch.exch_ms, during
 * which the coprocessor runs on: a window closes between two exchanges of
 * one poll when the grid falls that way.
 *
 * CH1's current is a function of the window it is read in, 100 mA plus
 * 2 mA for each number modulo 500, so a window handed over under another
 * window's number shows in its mean (chain_mean_ma()).  2 mA is 5 steps of
 * the part's 40 uV across 0.1 Ohm: every mean is a whole number of mA.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "fake_ina.h"
#include "link_msg.h"
#include "link_pages.h"
#include "outputs.h"
#include "sense_link.h"
#include "sense_page.h"
#include "sense_svc.h"

#define CHAIN_I3221_ADDR 0x40u

typedef struct {
    fake_bus_t   fb;
    fake_part_t *i3221;
    sense_svc_t  svc;
    sense_snap_t snap;
    outputs_t    o;
    sense_page_t pg;         /* the coprocessor's pages                 */
    sense_link_t sl;         /* the panel's end                         */
    uint64_t us;             /* the coprocessor's clock                 */
    uint32_t tick0;          /* the panel's tick at us == 0             */
    uint16_t minor;          /* the coprocessor's protocol minor        */
    unsigned exch_ms;        /* one exchange                            */
    bool     idle;           /* the bank, for the set-up's writes       */
    bool     no_win_page;    /* SERVO_WIN refused whatever the minor    */
    unsigned lose_win;       /* the next n SERVO_WIN replies are lost   */
    unsigned lose_status;    /* the next n SENSE status replies, too    */
    /* What the link carried. */
    unsigned to_win;         /* requests to SERVO_WIN                   */
    unsigned win_head, win_all;   /* of them, by their length           */
    unsigned status_n;       /* the registers of the last status read   */
    unsigned writes;
    unsigned poll_frames;    /* CAN frames of the last poll             */
    bool     flat;           /* CH1's current is left as the suite set it */
} chain_t;

static chain_t ch;

static uint32_t chain_now(void)
{
    return (uint32_t)(ch.tick0 + (uint32_t)(ch.us / 1000u));
}

static uint64_t chain_clock(void *ctx)
{
    (void)ctx;
    return ch.us;
}

static void chain_recover(void *ctx)
{
    (void)ctx;
}

static bool chain_open(void *ctx, uint8_t sda, uint8_t scl)
{
    (void)ctx;
    (void)sda;
    (void)scl;
    return true;
}

static void chain_close(void *ctx)
{
    (void)ctx;
}

static sense_err_t chain_ask(void *ctx, uint8_t addr)
{
    return (fake_find((fake_bus_t *)ctx, addr) != NULL) ? SENSE_OK
                                                         : SENSE_NACK;
}

/* The mean CH1 reads over window @p number, mA. */
static int chain_mean_ma(uint16_t number)
{
    return 100 + 2 * (int)(number % 500u);
}

/* The coprocessor, @p ms milliseconds: the order from the page, one tick
 * of core 1, and what it read into the pages. */
static void chain_far(unsigned ms)
{
    for (unsigned i = 0u; i < ms; ++i) {
        sense_cmd_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        sense_page_cmd(&ch.pg, &cmd);
        if (!ch.flat) {
            /* The window this tick's sample falls in: number 0 in the
             * tick that takes a set-up and starts the schedule afresh. */
            const sense_sched_t *sc = &ch.svc.sched;
            const uint64_t now_ms = ch.us / 1000u;
            const bool afresh = !ch.svc.started
                                || cmd.cfg_gen != ch.svc.cfg_gen
                                || !sc->started;
            const uint64_t win = afresh
                                     ? 0u
                                     : (now_ms - sc->t0_ms) / SENSE_WINDOW_MS;
            ch.i3221->amps[0] = (double)chain_mean_ma((uint16_t)win) / 1000.0;
        }
        sense_svc_step(&ch.svc, &cmd, &ch.snap);
        sense_page_publish(&ch.pg, &ch.snap, 0u);
        ch.us += 1000u;
    }
}

/* The coprocessor's tick held for @p ms: its clock runs and nothing is
 * read. */
static void chain_far_late(unsigned ms)
{
    ch.us += (uint64_t)ms * 1000u;
}

/* The coprocessor as it starts: no window, the page's set-up kept. */
static void chain_far_boot(void)
{
    const sense_svc_io_t io = {
        .sched = { { fake_read, fake_write, &ch.fb }, chain_clock,
                   chain_recover, &ch.fb },
        .open = chain_open, .close = chain_close, .ask = chain_ask,
    };
    uint16_t cfg[LINK_SN_CONFIG_COUNT];
    sense_page_read(&ch.pg, 0u, (uint8_t)LINK_SN_CONFIG_COUNT, cfg);
    sense_svc_init(&ch.svc, &io);
    memset(&ch.snap, 0, sizeof(ch.snap));
    sense_page_init(&ch.pg);
    outputs_arm(&ch.o, false, 0u);
    for (uint8_t at = 4u; at <= 8u; at = (uint8_t)(at + 4u)) {
        (void)sense_page_write(&ch.pg, at, 4u, &cfg[at], &ch.o, 0u);
    }
    (void)sense_page_write(&ch.pg, 0u, 4u, &cfg[0], &ch.o, 0u);
}

/* The set-up SETUP names in every case here: the INA3221 at 0x40 on
 * GP16 and GP17 with 0.1 Ohm, channels @p channels. */
static sense_setup_t chain_setup(uint8_t channels)
{
    const sense_setup_t w = {
        .i228 = false, .i3221 = true, .sda = 16, .scl = 17,
        .i228_addr = 0x45u, .i228_uohm = 200u, .i228_max_da = 2048u,
        .i3221_addr = CHAIN_I3221_ADDR, .i3221_dmohm = 1000u,
        .i3221_ch = channels,
    };
    return w;
}

/*
 * A bench with the INA3221 on the bus, a coprocessor of protocol minor
 * @p minor that holds the set-up already, and a panel that has not linked.
 * The panel's tick reads @p tick0 now.
 */
static void chain_start(uint16_t minor, uint32_t tick0, uint8_t channels)
{
    memset(&ch, 0, sizeof(ch));
    sense_bus_t scratch;
    fake_bus_init(&ch.fb, &scratch);
    ch.i3221 = fake_add(&ch.fb, FAKE_INA3221, CHAIN_I3221_ADDR, 0.1);
    for (unsigned c = 0u; c < 3u; ++c) {
        ch.i3221->volts[c] = 6.0;
        ch.i3221->amps[c]  = 0.1 * (double)(c + 1u);
    }
    ch.us      = 5000000u;
    ch.tick0   = (uint32_t)(tick0 - (uint32_t)(ch.us / 1000u));
    ch.minor   = minor;
    ch.exch_ms = 1u;
    ch.idle    = true;
    outputs_init(&ch.o, 0u);
    outputs_reserve_pins(&ch.o, (uint64_t)1u << 3);
    sense_page_init(&ch.pg);
    const uint16_t bus[4]  = { LINK_SN_EN_I3221, 16u, 17u, LINK_SN_KHZ_BUS };
    const uint16_t part[4] = { CHAIN_I3221_ADDR, 1000u, channels, 0u };
    (void)sense_page_write(&ch.pg, (uint8_t)LINK_SN_I3221_ADDR, 4u, part,
                           &ch.o, 0u);
    (void)sense_page_write(&ch.pg, 0u, 4u, bus, &ch.o, 0u);
    chain_far_boot();
    sense_link_init(&ch.sl);
    const sense_setup_t w = chain_setup(channels);
    sense_link_want(&ch.sl, &w, chain_now());
}

/* The link comes up: the panel has read the coprocessor's minor. */
static void chain_link_up(void)
{
    sense_link_came_up(&ch.sl, ch.minor, chain_now());
}

/* One exchange as the coprocessor answers it, after the time it takes. */
static int chain_exchange(const sense_link_op_t *op, uint16_t *regs)
{
    chain_far(ch.exch_ms);
    ch.poll_frames += 1u + (op->write ? 0u : ((unsigned)op->n + 3u) / 4u);
    memset(regs, 0, LINK_MAX_REGS * sizeof(uint16_t));
    if (op->page == LINK_PAGE_SERVO_WIN) {
        ++ch.to_win;
        if (op->n == LINK_SW_COUNT) {
            ++ch.win_all;
        } else {
            ++ch.win_head;
        }
        if (ch.minor < LINK_MINOR_SERVO_WIN || ch.no_win_page) {
            return LINK_NACK_BAD_PAGE;
        }
        if (ch.lose_win > 0u) {
            --ch.lose_win;
            return SENSE_LINK_NO_ANSWER;
        }
        sense_win_read(&ch.pg, op->off, op->n, regs);
        return SENSE_LINK_ACK;
    }
    if (op->page == LINK_PAGE_IDENTITY) {
        regs[LINK_ID_PROTOCOL_MAJOR] = LINK_PROTOCOL_MAJOR;
        regs[LINK_ID_PROTOCOL_MINOR] = ch.minor;
        regs[LINK_ID_CAPABILITIES]   = sense_page_caps(&ch.pg);
        return SENSE_LINK_ACK;
    }
    if (ch.minor < SENSE_LINK_MINOR) {
        return LINK_NACK_BAD_PAGE;
    }
    if (op->page == LINK_PAGE_SENSE) {
        if (op->write) {
            ++ch.writes;
            const uint8_t nack = sense_page_write(&ch.pg, op->off, op->n,
                                                  op->regs, &ch.o, 0u);
            return (nack == 0u) ? SENSE_LINK_ACK : (int)nack;
        }
        if (op->kind == SENSE_LINK_OP_STATUS) {
            ch.status_n = op->n;
            if (ch.lose_status > 0u) {
                --ch.lose_status;
                return SENSE_LINK_NO_ANSWER;
            }
        }
        sense_page_read(&ch.pg, op->off, op->n, regs);
        return SENSE_LINK_ACK;
    }
    sense_servo_read(&ch.pg, op->off, op->n, regs);
    return SENSE_LINK_ACK;
}

/* One poll of the control task: every exchange owed, as
 * sense_link_service() makes them.  Returns the milliseconds it took. */
static unsigned chain_poll(void)
{
    const uint64_t began = ch.us;
    ch.poll_frames = 0u;
    for (int k = 0; k < 6; ++k) {
        sense_link_op_t op;
        if (!sense_link_next(&ch.sl, chain_now(), ch.idle, &op)) {
            break;
        }
        uint16_t regs[LINK_MAX_REGS];
        const int result = chain_exchange(&op, regs);
        sense_link_done(&ch.sl, result, op.write ? NULL : regs, chain_now());
        if (result == SENSE_LINK_NO_ANSWER) {
            break;
        }
    }
    return (unsigned)((ch.us - began) / 1000u);
}

/* A poll, and the rest of @p period_ms until the next one starts. */
static void chain_cycle(unsigned period_ms)
{
    const unsigned spent = chain_poll();
    if (spent < period_ms) {
        chain_far(period_ms - spent);
    }
}
