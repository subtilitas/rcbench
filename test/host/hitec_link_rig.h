/*
 * The loopback the panel's link half runs on (shared/bench/hitec_link.c):
 * the module, the IOmcu's link dispatch (shared/link/link_dev.c), the HITEC
 * page (shared/outputs/hitec_port.c) and the servo model of hitec_sim.h.
 * Every exchange goes through xchg(), which can lose a request or its
 * answer, refuse a frame, strip the key, slip in another writer's frame or
 * make the page read BUSY.
 *
 * The clock: 1ms is one pass of the IOmcu (hitec_sim.h); every
 * HITEC_LINK_POLL_MS (5ms) the panel makes one pass, hitec_link_service()
 * with at most HITEC_LINK_PASS_MAX exchanges.
 *
 * For test_hitec_link.c, test_hitec_link_loss.c and test_hitec_link_run.c.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "hitec_sim.h"

#include "hitec_link.h"
#include "hitec_session.h"
#include "link_dev.h"
#include "link_msg.h"

static link_dev_t g_dev;
static hitec_link_t g_l;
static bool g_safe;
static uint32_t g_id;

/* What went over the link. */
static unsigned g_sent;            /* exchanges */
static unsigned g_cmds;            /* command frames */
static unsigned g_op_cmds[16];     /* command frames by their operation */
static uint16_t g_last_cmd[4];
static unsigned g_max_pass;        /* most exchanges one pass made */
static unsigned g_passes;          /* passes made */

/* What goes wrong on the way. */
static unsigned g_lose_cmd;        /* command frames lost before the page */
static unsigned g_lose_cmd_reply;  /* answers to command frames lost */
static unsigned g_lose_any;        /* exchanges lost, whatever they are */
static unsigned g_lose_status;     /* reads of registers 0 to 15 lost */
static unsigned g_nack_status;     /* reads of 0 to 15 refused BAD_RANGE */
static uint8_t  g_nack_stage;      /* the answer to a staged window, or 0 */
static uint8_t  g_nack_run;        /* the answer to a read of 0 to 31 */
static bool     g_strip_key;       /* the key taken out of a command */
static bool     g_fake_busy;       /* reads of STATE say BUSY */
static bool     g_fake_cap;        /* reads of CAP say 17 bytes */
static unsigned g_nack_cmd_n;      /* command frames refused with */
static uint8_t  g_nack_cmd;        /* this NACK before the page */
static unsigned g_poke_staged;     /* another writer changes register 5
                                      before the next command */
static unsigned g_other_cmd;       /* another writer sends g_other_op
                                      before the next command */
static link_hitec_op_t g_other_op = LINK_HITEC_OP_CLEAR;
static bool     g_abort_in_flight; /* hitec_link_abort() while the next
                                      command frame is on its way */

/* ------------------------------------------------------------ the IOmcu */

static inline void page_read(void *ctx, uint8_t off, uint8_t n,
                             uint16_t *out)
{
    (void)ctx;
    hitec_port_read(&g_port, off, n, out);
}

static inline uint8_t page_write(void *ctx, uint8_t off, uint8_t n,
                                 const uint16_t *in)
{
    (void)ctx;
    return hitec_port_write(&g_port, off, n, in, g_ms);
}

static const link_page_t k_pages[] = {
    { LINK_PAGE_HITEC, LINK_HT_COUNT, page_read, page_write },
};

/* A frame of another writer, the bench console: @p op on @p port with
 * the page's SEQ plus 1 and the CRC of the staged registers it holds. */
static inline uint8_t other_cmd(link_hitec_op_t op, unsigned port,
                                uint16_t key)
{
    uint16_t in[4];

    in[0] = LINK_HT_W_CMD_OF(op, port);
    in[1] = (uint16_t)(g_port.seq + 1u);
    in[2] = key;
    in[3] = link_kst_crc(g_port.staged, LINK_HT_W_STAGED);
    return hitec_port_write(&g_port, 0u, 4u, in, g_ms);
}

/* ------------------------------------------------------------- the link */

static inline int xchg(void *ctx, const hitec_link_xfer_t *x,
                       uint16_t *regs)
{
    link_msg_t req, rep;
    const bool cmd = x->write && x->off == LINK_HT_W_CMD;

    (void)ctx;
    g_sent++;
    CHECK_EQ(x->page, LINK_PAGE_HITEC);
    CHECK(x->n >= 1u && x->n <= LINK_HT_COUNT);
    CHECK(!x->write || x->n <= 4u);
    CHECK(x->write == (regs == NULL));
    if (x->n == 0u || x->n > LINK_HT_COUNT || (!x->write && regs == NULL)) {
        return HITEC_LINK_NO_ANSWER;
    }
    if (cmd) {
        g_cmds++;
        g_op_cmds[x->regs[LINK_HT_W_CMD] & 0xFu]++;
        memcpy(g_last_cmd, x->regs, sizeof(g_last_cmd));
        if (g_abort_in_flight) {
            g_abort_in_flight = false;
            hitec_link_abort(&g_l);
        }
        if (g_poke_staged > 0u) {
            const uint16_t v = 0x0BADu;

            g_poke_staged--;
            CHECK_EQ(hitec_port_write(&g_port, LINK_HT_W_NTX, 1u, &v, g_ms),
                     0u);
        }
        if (g_other_cmd > 0u) {
            g_other_cmd--;
            CHECK_EQ(other_cmd(g_other_op, CH, 0u), 0u);
        }
        if (g_lose_cmd > 0u) {
            g_lose_cmd--;
            return HITEC_LINK_NO_ANSWER;
        }
        if (g_nack_cmd_n > 0u) {
            g_nack_cmd_n--;
            return g_nack_cmd;
        }
    }
    if (g_lose_any > 0u) {
        g_lose_any--;
        return HITEC_LINK_NO_ANSWER;
    }
    if (!x->write && x->off == LINK_HT_STATE && x->n == LINK_HT_DATA) {
        if (g_lose_status > 0u) {
            g_lose_status--;
            return HITEC_LINK_NO_ANSWER;
        }
        if (g_nack_status > 0u) {
            g_nack_status--;
            return (int)LINK_NACK_BAD_RANGE;
        }
    }
    if (x->write && !cmd && g_nack_stage != 0u) {
        return g_nack_stage;
    }
    if (!x->write && x->n == LINK_HT_COUNT && g_nack_run != 0u) {
        return g_nack_run;
    }
    memset(&req, 0, sizeof(req));
    memset(&rep, 0, sizeof(rep));
    req.op = x->write ? LINK_OP_WRITE : LINK_OP_READ;
    req.page = x->page;
    req.offset = x->off;
    req.count = x->n;
    if (x->write) {
        memcpy(req.regs, x->regs, x->n * sizeof(x->regs[0]));
        if (cmd && g_strip_key) {
            req.regs[LINK_HT_W_KEY] = 0u;
        }
    }
    CHECK(link_dev_dispatch(&g_dev, &req, &rep, g_ms));
    if (cmd && g_lose_cmd_reply > 0u) {
        g_lose_cmd_reply--;
        return HITEC_LINK_NO_ANSWER;
    }
    if (rep.op == LINK_OP_DATA) {
        memcpy(regs, rep.regs, x->n * sizeof(regs[0]));
        if (g_fake_busy && x->off == LINK_HT_STATE) {
            regs[0] = (uint16_t)((regs[0] & ~0x7u) | LINK_HITEC_BUSY);
        }
        if (g_fake_cap && x->off == LINK_HT_STATE) {
            regs[LINK_HT_CAP] =
                (uint16_t)((regs[LINK_HT_CAP] & ~0x1Fu) | (HITEC_RX_MAX + 1u));
        }
        return HITEC_LINK_ACK;
    }
    if (rep.op == LINK_OP_ACK) {
        return HITEC_LINK_ACK;
    }
    return rep.regs[0];
}

/* The panel's pass. */
static inline void pass(void)
{
    const unsigned n = hitec_link_service(&g_l, g_ms, xchg, NULL);

    g_passes++;
    if (n > g_max_pass) {
        g_max_pass = n;
    }
}

/* 1ms: a pass of the IOmcu, and every 5ms one of the panel. */
static inline void ms(void)
{
    tick_as(g_safe, false);
    if (g_ms % HITEC_LINK_POLL_MS == 0u) {
        pass();
    }
}

static inline void run_for(unsigned n)
{
    for (unsigned i = 0; i < n; ++i) {
        ms();
    }
}

/* A switch to SAFE at the IOmcu: one pass with the stop. */
static inline void stop_pulse(void)
{
    tick_as(g_safe, true);
}

/* Run until the module hands an outcome out; 40s is the bound. */
static inline hitec_link_outcome_t outcome(void)
{
    hitec_link_outcome_t out;

    memset(&out, 0, sizeof(out));
    for (unsigned i = 0; i < 40000u; ++i) {
        if (hitec_link_outcome(&g_l, &out)) {
            return out;
        }
        ms();
    }
    CHECK(hitec_link_outcome(&g_l, &out));
    return out;
}

static inline void until_phase(hitec_link_phase_t p)
{
    for (unsigned i = 0; i < 40000u && hitec_link_phase(&g_l) != p; ++i) {
        ms();
    }
    CHECK_EQ(hitec_link_phase(&g_l), p);
}

static inline hitec_link_readout_t readout(void)
{
    hitec_link_readout_t r;

    hitec_link_readout(&g_l, &r);
    return r;
}

/* A fresh bench at @p t0 with @p pages pages on the link; nothing up. */
static inline void bench_at(uint32_t t0, supply_kind_t supply,
                            uint8_t pages)
{
    fresh_at(t0, supply);
    link_dev_init(&g_dev, k_pages, pages, NULL, g_ms);
    hitec_link_init(&g_l);
    g_safe = true;
    g_id = 0;
    g_sent = 0;
    g_cmds = 0;
    memset(g_op_cmds, 0, sizeof(g_op_cmds));
    memset(g_last_cmd, 0, sizeof(g_last_cmd));
    g_max_pass = 0;
    g_passes = 0;
    g_lose_cmd = 0;
    g_lose_cmd_reply = 0;
    g_lose_any = 0;
    g_lose_status = 0;
    g_nack_status = 0;
    g_nack_stage = 0;
    g_nack_run = 0;
    g_strip_key = false;
    g_fake_busy = false;
    g_fake_cap = false;
    g_nack_cmd_n = 0;
    g_nack_cmd = 0;
    g_poke_staged = 0;
    g_other_cmd = 0;
    g_other_op = LINK_HITEC_OP_CLEAR;
    g_abort_in_flight = false;
}

/* The link up at the minor this build speaks, and the page read. */
static inline void link_up(void)
{
    hitec_link_came_up(&g_l, LINK_PROTOCOL_MINOR);
    until_phase(HITEC_LINK_READY);
}

static inline void up_at(uint32_t t0, supply_kind_t supply)
{
    bench_at(t0, supply, 1u);
    link_up();
}

static inline void up(void)
{
    up_at(0u, SUPPLY_ON);
}

/* ------------------------------------------------------------ requests */

static inline hitec_link_outcome_t open_as(uint8_t port, uint8_t family,
                                           uint8_t drive)
{
    CHECK_EQ(hitec_link_open(&g_l, port, family, drive, ++g_id),
             HITEC_LINK_START_OK);
    return outcome();
}

/* Up, and port CH opened for @p family. */
static inline void opened(uint8_t family)
{
    hitec_link_outcome_t out;

    up();
    out = open_as(CH, family, HITEC_DRIVE_OPEN_DRAIN);
    CHECK_EQ(out.kind, HITEC_LINK_OUT_DONE);
    CHECK_EQ(out.result, LINK_HITEC_RESULT_DONE);
    CHECK_EQ(hitec_port_state(&g_port), LINK_HITEC_OPEN);
}

static inline hitec_link_outcome_t run_step(const hitec_ses_step_t *st)
{
    CHECK_EQ(hitec_link_step(&g_l, CH, st, MV, MA, ++g_id),
             HITEC_LINK_START_OK);
    return outcome();
}

static inline hitec_link_outcome_t run_bare(link_hitec_op_t op,
                                            uint8_t port)
{
    const hitec_link_start_t s = op == LINK_HITEC_OP_CLOSE
                                     ? hitec_link_close(&g_l, port, ++g_id)
                                     : hitec_link_clear(&g_l, port, ++g_id);

    CHECK_EQ(s, HITEC_LINK_START_OK);
    return outcome();
}

/* ------------------------------------------------------------- the steps */

static inline hitec_ses_step_t c_read_step(uint8_t addr)
{
    hitec_ses_step_t st;

    memset(&st, 0, sizeof(st));
    st.kind = HITEC_SES_STEP_XFER;
    hitec_c_xfer(HITEC_C_READ_EEPROM, addr, 0u, &st.xfer);
    st.decoder = HITEC_DEC_C;
    st.dec_arg = HITEC_C_READ_EEPROM;
    return st;
}

static inline hitec_ses_step_t d_read_step(uint8_t addr)
{
    hitec_ses_step_t st;

    memset(&st, 0, sizeof(st));
    st.kind = HITEC_SES_STEP_XFER;
    hitec_d_read_xfer(0u, addr, &st.xfer);
    st.decoder = HITEC_DEC_D;
    st.dec_arg = addr;
    return st;
}

static inline hitec_ses_step_t listen_step(uint8_t family,
                                           uint32_t window_us,
                                           uint8_t max_rx, bool answer)
{
    hitec_ses_step_t st;

    memset(&st, 0, sizeof(st));
    st.kind = HITEC_SES_STEP_XFER;
    CHECK(hitec_listen_xfer(family, window_us, max_rx, answer, HITEC_C_ACK,
                            &st.xfer));
    return st;
}

static inline hitec_ses_step_t power_step(uint8_t family, uint16_t off_ms,
                                          uint32_t window_us)
{
    hitec_ses_step_t st = listen_step(family, window_us, 1u, false);

    st.kind = HITEC_SES_STEP_POWER;
    st.off_ms = off_ms;
    return st;
}

/* A LISTEN with the decoder that wants `+`. */
static inline hitec_ses_step_t plus_listen(uint32_t window_us)
{
    hitec_ses_step_t st = listen_step(HITEC_CLASSIC, window_us, 1u, false);

    st.decoder = HITEC_DEC_LISTEN;
    st.dec_arg = HITEC_C_ACK;
    return st;
}

/* A request started, run until the module's phase is @p p. */
static inline void start_until(const hitec_ses_step_t *st,
                               hitec_link_phase_t p)
{
    CHECK_EQ(hitec_link_step(&g_l, CH, st, MV, MA, ++g_id),
             HITEC_LINK_START_OK);
    until_phase(p);
}

/* One exchange by hand, outside a pass. */
static inline void one_exchange(void)
{
    hitec_link_xfer_t x;
    uint16_t regs[LINK_HT_COUNT] = { 0 };
    int r;

    if (!hitec_link_next(&g_l, g_ms, &x)) {
        return;
    }
    r = xchg(NULL, &x, x.write ? NULL : regs);
    hitec_link_done(&g_l, r, !x.write && r == HITEC_LINK_ACK ? regs : NULL);
}
