/*
 * The bench the HITEC page suites run on: the servo model, the port's
 * hardware and the clock of hitec_sim.h, the page's registers, the staged
 * block and its command.  The page is shared/outputs/hitec_port.c.
 *
 * For test_hitec_port.c, test_hitec_port_stop.c, test_hitec_port_rx.c and
 * test_hitec_port_hold.c.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "hitec_sim.h"

/* --------------------------------------------------------------- the page */

static inline uint16_t reg(unsigned i)
{
    uint16_t v = 0xDEADu;

    hitec_port_read(&g_port, (uint8_t)i, 1, &v);
    return v;
}

static inline link_hitec_state_t state(void)
{
    return (link_hitec_state_t)LINK_HT_STATE_OF(reg(LINK_HT_STATE));
}

static inline bool flag(uint16_t f)
{
    return (reg(LINK_HT_STATE) & f) != 0u;
}

static inline unsigned result(void)
{
    return reg(LINK_HT_RESULT) & 0xFFu;
}

static inline unsigned refusal(void)
{
    return reg(LINK_HT_RESULT) >> 8;
}

static inline unsigned count(void)
{
    return reg(LINK_HT_OP) & 0x0FFFu;
}

static inline unsigned cap_n(void)
{
    return reg(LINK_HT_CAP) & 0x1Fu;
}

static inline unsigned rx_code(void)
{
    return reg(LINK_HT_DEC) & 0xFFu;
}

static inline uint8_t cap_byte(unsigned k)
{
    return (uint8_t)(reg(LINK_HT_BYTES + k / 2u) >> (8u * (k & 1u)));
}

/* The staged registers, as the writer keeps them. */
static uint16_t g_st[LINK_HT_W_STAGED];

#define SST(r) g_st[(r) - LINK_HT_W_LINE]

static inline void st32(unsigned lo, uint32_t v)
{
    SST(lo) = (uint16_t)(v & 0xFFFFu);
    SST(lo + 1u) = (uint16_t)(v >> 16);
}

/* Write the staged block; the CRC of it. */
static inline uint16_t stage(void)
{
    CHECK_EQ(hitec_port_write(&g_port, LINK_HT_W_LINE, LINK_HT_W_STAGED,
                              g_st, g_ms), 0);
    return link_kst_crc(g_st, LINK_HT_W_STAGED);
}

static inline uint8_t cmd_raw(uint16_t word, uint16_t key, uint16_t crc)
{
    const uint16_t in[4] = { word, (uint16_t)(reg(LINK_HT_SEQ) + 1u), key,
                             crc };

    return hitec_port_write(&g_port, 0, 4, in, g_ms);
}

/* A command over the staged block as it is: stage, then the frame. */
static inline uint8_t cmd(link_hitec_op_t o, unsigned port, uint16_t key)
{
    const uint16_t crc = stage();

    return cmd_raw(LINK_HT_W_CMD_OF(o, port), key, crc);
}

/* ---------------------------------------------------------------- the clock */

static inline void tick(void)
{
    tick_as(true, false);
}

static inline void run_ms(unsigned ms)
{
    for (unsigned i = 0; i < ms; ++i) {
        tick();
    }
}

static inline void until_state(link_hitec_state_t s)
{
    for (unsigned i = 0; i < 20000u && state() != s; ++i) {
        tick();
    }
    CHECK_EQ(state(), s);
}

/* -------------------------------------------------------------- the stages */

static inline void line(uint8_t family, uint8_t drive)
{
    memset(g_st, 0, sizeof(g_st));
    SST(LINK_HT_W_LINE) = LINK_HT_LINE_OF(family, drive);
}

static inline void open_line(uint8_t family, uint8_t drive)
{
    line(family, drive);
    CHECK_EQ(cmd(LINK_HITEC_OP_OPEN, CH, 0), 0);
    CHECK_EQ(refusal(), LINK_HITEC_REF_NONE);
    until_state(LINK_HITEC_OPEN);
    CHECK_EQ(result(), LINK_HITEC_RESULT_DONE);
}

static inline void open_classic(void)
{
    fresh();
    open_line(HITEC_CLASSIC, HITEC_DRIVE_OPEN_DRAIN);
}

/* Stage a SEND of @p x. */
static inline void stage_send(const hitec_xfer_t *x, uint8_t dec,
                              uint8_t arg)
{
    SST(LINK_HT_W_NTX) = (uint16_t)(x->n_tx | (x->gap_bits << 8));
    for (unsigned k = 0; k < 4u; ++k) {
        SST(LINK_HT_W_TX + k) =
            (uint16_t)(x->tx[2u * k] | (x->tx[2u * k + 1u] << 8));
    }
    st32(LINK_HT_W_LEAD_LO, x->lead_us);
    st32(LINK_HT_W_WINDOW_LO, x->window_us);
    SST(LINK_HT_W_RX) = (uint16_t)(x->max_rx | (x->answer << 8));
    SST(LINK_HT_W_DECODE) = (uint16_t)(dec | (arg << 8));
}

static inline void stage_listen(uint32_t window_us, uint8_t max_rx,
                                bool answer, uint8_t answer_byte,
                                uint8_t dec, uint8_t arg)
{
    st32(LINK_HT_W_WINDOW_LO, window_us);
    SST(LINK_HT_W_RX) = (uint16_t)(max_rx | (answer ? 0x100u : 0u));
    SST(LINK_HT_W_ANSWER) = answer_byte;
    SST(LINK_HT_W_DECODE) = (uint16_t)(dec | (arg << 8));
}

static inline void stage_classic_read(uint8_t addr)
{
    hitec_xfer_t x;

    hitec_c_xfer(HITEC_C_READ_EEPROM, addr, 0u, &x);
    stage_send(&x, LINK_HITEC_DEC_C, HITEC_C_READ_EEPROM);
}

static inline void settle(void)
{
    for (unsigned i = 0; i < 20000u && state() == LINK_HITEC_BUSY; ++i) {
        tick();
    }
    CHECK(state() != LINK_HITEC_BUSY);
}

