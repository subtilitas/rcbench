/*
 * The servo rail's meter from the part to the panel, for a host suite: the
 * modelled INA3221 of fake_ina.h, read by the IOmcu's 1ms schedule
 * (sense_svc), published into its pages (sense_page) every millisecond, and
 * read by the panel's sense_link over a link this header models.
 *
 * One clock runs both ends.  The IOmcu steps once per millisecond.
 * The panel's tick is the same clock plus an offset, so a suite puts the
 * 2^32ms wrap wherever it wants it.  An exchange takes ch.exch_ms, during
 * which the IOmcu runs on: a window closes between two exchanges of
 * one poll when the grid falls that way.
 *
 * CH1's current is a function of the window it is read in, 100mA plus
 * 2mA for each number modulo 500, so a window handed over under another
 * window's number shows in its mean (chain_mean_ma()).  2mA is 5 steps of
 * the part's 40 uV across 0.1Ohm: every mean is a whole number of mA.
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
    sense_page_t pg;         /* the IOmcu's pages                 */
    sense_link_t sl;         /* the panel's end                         */
    uint64_t us;             /* the IOmcu's clock                 */
    uint32_t tick0;          /* the panel's tick at us == 0             */
    uint16_t minor;          /* the IOmcu's protocol minor        */
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

static chain_t chain;

static inline uint32_t chain_now(void)
{
    return (uint32_t)(chain.tick0 + (uint32_t)(chain.us / 1000u));
}

static inline uint64_t chain_clock(void *ctx)
{
    (void)ctx;
    return chain.us;
}

static inline void chain_recover(void *ctx)
{
    (void)ctx;
}

static inline bool chain_open(void *ctx, uint8_t sda, uint8_t scl)
{
    (void)ctx;
    (void)sda;
    (void)scl;
    return true;
}

static inline void chain_close(void *ctx)
{
    (void)ctx;
}

static inline sense_err_t chain_ask(void *ctx, uint8_t addr)
{
    return (fake_find((fake_bus_t *)ctx, addr) != NULL) ? SENSE_OK
                                                         : SENSE_NACK;
}

/* The mean CH1 reads over window @p number, mA. */
static inline int chain_mean_ma(uint16_t number)
{
    return 100 + 2 * (int)(number % 500u);
}

/* The IOmcu, @p ms milliseconds: the order from the page, one tick
 * of core 1, and what it read into the pages. */
static inline void chain_far(unsigned ms)
{
    for (unsigned i = 0u; i < ms; ++i) {
        sense_cmd_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        sense_page_cmd(&chain.pg, &cmd);
        if (!chain.flat) {
            /* The window this tick's sample falls in: number 0 in the
             * tick that takes a set-up and starts the schedule afresh. */
            const sense_sched_t *sc = &chain.svc.sched;
            const uint64_t now_ms = chain.us / 1000u;
            const bool afresh = !chain.svc.started
                                || cmd.cfg_gen != chain.svc.cfg_gen
                                || !sc->started;
            const uint64_t win = afresh
                                     ? 0u
                                     : (now_ms - sc->t0_ms) / SENSE_WINDOW_MS;
            chain.i3221->amps[0] = (double)chain_mean_ma((uint16_t)win) / 1000.0;
        }
        sense_svc_step(&chain.svc, &cmd, &chain.snap);
        sense_page_publish(&chain.pg, &chain.snap, 0u);
        chain.us += 1000u;
    }
}

/* The IOmcu's tick held for @p ms: its clock runs and nothing is
 * read. */
static inline void chain_far_late(unsigned ms)
{
    chain.us += (uint64_t)ms * 1000u;
}

/* The IOmcu as it starts: no window, the page's set-up kept. */
static inline void chain_far_boot(void)
{
    const sense_svc_io_t io = {
        .sched = { { fake_read, fake_write, &chain.fb }, chain_clock,
                   chain_recover, &chain.fb },
        .open = chain_open, .close = chain_close, .ask = chain_ask,
    };
    uint16_t cfg[LINK_SN_CONFIG_COUNT];
    sense_page_read(&chain.pg, 0u, (uint8_t)LINK_SN_CONFIG_COUNT, cfg);
    sense_svc_init(&chain.svc, &io);
    memset(&chain.snap, 0, sizeof(chain.snap));
    sense_page_init(&chain.pg);
    outputs_arm(&chain.o, false, 0u);
    for (uint8_t at = 4u; at <= 8u; at = (uint8_t)(at + 4u)) {
        (void)sense_page_write(&chain.pg, at, 4u, &cfg[at], &chain.o, 0u);
    }
    (void)sense_page_write(&chain.pg, 0u, 4u, &cfg[0], &chain.o, 0u);
}

/* The set-up SETUP names in every case here: the INA3221 at 0x40 on
 * GP16 and GP17 with 0.1Ohm, channels @p channels. */
static inline sense_setup_t chain_setup(uint8_t channels)
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
 * A bench with the INA3221 on the bus, an IOmcu of protocol minor
 * @p minor that holds the set-up already, and a panel that has not linked.
 * The panel's tick reads @p tick0 now.
 */
static inline void chain_start(uint16_t minor, uint32_t tick0,
                               uint8_t channels)
{
    memset(&chain, 0, sizeof(chain));
    sense_bus_t scratch;
    fake_bus_init(&chain.fb, &scratch);
    chain.i3221 = fake_add(&chain.fb, FAKE_INA3221, CHAIN_I3221_ADDR, 0.1);
    for (unsigned c = 0u; c < 3u; ++c) {
        chain.i3221->volts[c] = 6.0;
        chain.i3221->amps[c]  = 0.1 * (double)(c + 1u);
    }
    chain.us      = 5000000u;
    chain.tick0   = (uint32_t)(tick0 - (uint32_t)(chain.us / 1000u));
    chain.minor   = minor;
    chain.exch_ms = 1u;
    chain.idle    = true;
    outputs_init(&chain.o, 0u);
    outputs_reserve_pins(&chain.o, (uint64_t)1u << 3);
    sense_page_init(&chain.pg);
    const uint16_t bus[4]  = { LINK_SN_EN_I3221, 16u, 17u, LINK_SN_KHZ_BUS };
    const uint16_t part[4] = { CHAIN_I3221_ADDR, 1000u, channels, 0u };
    (void)sense_page_write(&chain.pg, (uint8_t)LINK_SN_I3221_ADDR, 4u, part,
                           &chain.o, 0u);
    (void)sense_page_write(&chain.pg, 0u, 4u, bus, &chain.o, 0u);
    chain_far_boot();
    sense_link_init(&chain.sl);
    const sense_setup_t w = chain_setup(channels);
    sense_link_want(&chain.sl, &w, chain_now());
}

/* The link comes up: the panel has read the IOmcu's minor. */
static inline void chain_link_up(void)
{
    sense_link_came_up(&chain.sl, chain.minor, chain_now());
}

/* One exchange as the IOmcu answers it, after the time it takes. */
static inline int chain_exchange(const sense_link_op_t *op, uint16_t *regs)
{
    chain_far(chain.exch_ms);
    chain.poll_frames += 1u + (op->write ? 0u : ((unsigned)op->n + 3u) / 4u);
    memset(regs, 0, LINK_MAX_REGS * sizeof(uint16_t));
    if (op->page == LINK_PAGE_SERVO_WIN) {
        ++chain.to_win;
        if (op->n == LINK_SW_COUNT) {
            ++chain.win_all;
        } else {
            ++chain.win_head;
        }
        if (chain.minor < LINK_MINOR_SERVO_WIN || chain.no_win_page) {
            return LINK_NACK_BAD_PAGE;
        }
        if (chain.lose_win > 0u) {
            --chain.lose_win;
            return SENSE_LINK_NO_ANSWER;
        }
        sense_win_read(&chain.pg, op->off, op->n, regs);
        return SENSE_LINK_ACK;
    }
    if (op->page == LINK_PAGE_IDENTITY) {
        regs[LINK_ID_PROTOCOL_MAJOR] = LINK_PROTOCOL_MAJOR;
        regs[LINK_ID_PROTOCOL_MINOR] = chain.minor;
        regs[LINK_ID_CAPABILITIES]   = sense_page_caps(&chain.pg);
        return SENSE_LINK_ACK;
    }
    if (chain.minor < SENSE_LINK_MINOR) {
        return LINK_NACK_BAD_PAGE;
    }
    if (op->page == LINK_PAGE_SENSE) {
        if (op->write) {
            ++chain.writes;
            const uint8_t nack = sense_page_write(&chain.pg, op->off, op->n,
                                                  op->regs, &chain.o, 0u);
            return (nack == 0u) ? SENSE_LINK_ACK : (int)nack;
        }
        if (op->kind == SENSE_LINK_OP_STATUS) {
            chain.status_n = op->n;
            if (chain.lose_status > 0u) {
                --chain.lose_status;
                return SENSE_LINK_NO_ANSWER;
            }
        }
        sense_page_read(&chain.pg, op->off, op->n, regs);
        return SENSE_LINK_ACK;
    }
    sense_servo_read(&chain.pg, op->off, op->n, regs);
    return SENSE_LINK_ACK;
}

/* One poll of the control task: every exchange owed, as
 * sense_link_service() makes them.  Returns the milliseconds it took. */
static inline unsigned chain_poll(void)
{
    const uint64_t began = chain.us;
    chain.poll_frames = 0u;
    for (int k = 0; k < 6; ++k) {
        sense_link_op_t op;
        if (!sense_link_next(&chain.sl, chain_now(), chain.idle, &op)) {
            break;
        }
        uint16_t regs[LINK_MAX_REGS];
        const int result = chain_exchange(&op, regs);
        sense_link_done(&chain.sl, result, op.write ? NULL : regs, chain_now());
        if (result == SENSE_LINK_NO_ANSWER) {
            break;
        }
    }
    return (unsigned)((chain.us - began) / 1000u);
}

/* A poll, and the rest of @p period_ms until the next one starts. */
static inline void chain_cycle(unsigned period_ms)
{
    const unsigned spent = chain_poll();
    if (spent < period_ms) {
        chain_far(period_ms - spent);
    }
}
