/*
 * The bench the HITEC page suites run on: a model of the wire driver with a
 * servo on it, the port's hardware with a simulated PD mini, the page's
 * registers, the staged block and its command, and the clock.  The page is
 * shared/outputs/hitec_port.c.
 *
 * For test_hitec_port.c and test_hitec_port_stop.c.
 *
 * The clock: a pass is 1 ms on the port's clock and 1000 us on the
 * driver's.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "hitec_port.h"
#include "hitec_wire.h"
#include "pdmini.h"
#include "tick_wrap.h"

#define CH    0u     /* the port under test */
#define OTHER 1u     /* the build's second port */
#define PIN   13u
#define KEY   LINK_HITEC_WRITE_KEY
#define MV    5000u
#define MA    1000u
#define LAG_MS 20u   /* the simulated PD mini reads back this late */

typedef enum {
    SUPPLY_NONE,     /* no callbacks: a build without a PD mini */
    SUPPLY_OFFLINE,  /* callbacks, and no PD mini answers */
    SUPPLY_ON,       /* asked on at MV and read on */
} supply_kind_t;

static hitec_port_t g_port;
static uint32_t g_ms;

/* The driver and the servo on its line. */
static struct {
    uint32_t us;
    bool     open;
    uint8_t  family, drive;
    bool     busy, aborted, hang;
    bool     start_ok;
    hitec_xfer_t x;
    uint32_t t_start;
    uint32_t t_rx;        /* the receiver's start */
    uint32_t t_end;
    hitec_rx_byte_t reply[HITEC_RX_MAX];
    uint8_t  n_reply;
    unsigned starts, aborts;
    /* The servo. */
    uint8_t  eeprom[256];
    uint16_t dreg[256];
    bool     boot_pending;  /* switched on: the boot byte comes */
    uint8_t  boot_byte;
    uint32_t boot_ns;       /* from the ON to the boot byte */
    uint8_t  listen_n;      /* bytes a listen hears without a power-up */
} g_drv;

static struct {
    uint8_t  pin[LINK_PG_PORTS_MAX];
    bool     take_ok;
    unsigned takes, gives;
    uint8_t  take_port, take_pin, take_family, take_drive, take_rx;
    supply_port_view_t v;
    bool     set_ok;
    bool     follow;
    bool     moving;
    uint32_t asked_ms;
    unsigned sets, ons, offs;
    uint32_t on_ms, off_read_ms;
    uint32_t listen_at_on_us;   /* the driver's clock at the ON */
    bool     has_supply;
} g_hw;

/* ------------------------------------------------------------- the driver */

static inline uint32_t bits_us(uint8_t family, uint32_t bits)
{
    return hitec_bits_ns(family, bits) / 1000u;
}

static inline bool servo_powered(void)
{
    return !g_hw.has_supply || g_hw.v.on;
}

static inline void reply_bytes(const uint8_t *b, unsigned n,
                               uint32_t first_ns)
{
    const uint32_t step = hitec_bits_ns(g_drv.family, 10u);

    g_drv.n_reply = (uint8_t)n;
    for (unsigned i = 0; i < n; ++i) {
        g_drv.reply[i].value = b[i];
        g_drv.reply[i].framing = 0;
        g_drv.reply[i].at_ns = first_ns + i * step;
    }
}

/* What the servo says to @p x. */
static inline void servo_answers(const hitec_xfer_t *x)
{
    g_drv.n_reply = 0;
    if (x->kind == HITEC_XFER_LISTEN) {
        if (g_drv.boot_pending) {
            reply_bytes(&g_drv.boot_byte, 1u, g_drv.boot_ns);
            g_drv.boot_pending = false;
        } else if (g_drv.listen_n > 0u) {
            uint8_t b[HITEC_RX_MAX];

            for (unsigned i = 0; i < g_drv.listen_n; ++i) {
                b[i] = (uint8_t)(0xA0u + i);
            }
            reply_bytes(b, g_drv.listen_n, 1000000u);
        }
        return;
    }
    if (x->kind != HITEC_XFER_SEND || !servo_powered()) {
        return;
    }
    if (x->family == HITEC_CLASSIC && x->n_tx == 4u
        && hitec_c_frame_ok(x->tx)) {
        uint8_t r[2];

        if (x->tx[0] == HITEC_C_READ_EEPROM) {
            r[0] = g_drv.eeprom[x->tx[1]];
            r[1] = HITEC_C_ACK;
        } else if (x->tx[0] == HITEC_C_WRITE_EEPROM) {
            g_drv.eeprom[x->tx[1]] = x->tx[2];
            r[0] = HITEC_C_ACK;
            r[1] = HITEC_C_ACK;
        } else {
            return;
        }
        /* 520 us from the start of the master's last byte. */
        reply_bytes(r, 2u, 520000u - hitec_bits_ns(HITEC_CLASSIC, 9u));
    } else if (x->family == HITEC_D && x->n_tx == HITEC_D_READ_LEN
               && x->tx[0] == HITEC_D_HEADER_REQUEST) {
        const uint16_t v = g_drv.dreg[x->tx[2]];
        uint8_t a[HITEC_D_ANSWER_LEN + 1u] = {
            HITEC_D_HEADER_ANSWER, x->tx[1], x->tx[2], HITEC_D_DATA_LEN,
            (uint8_t)(v & 0xFFu), (uint8_t)(v >> 8), 0u, 0u };

        a[6] = hitec_d_sum(&a[1], 5u);
        reply_bytes(a, 8u, 6000000u);
    }
}

static inline bool drv_start(void *ctx, const hitec_xfer_t *x)
{
    uint32_t tx_us = 0;

    (void)ctx;
    if (!g_drv.open || g_drv.busy || !g_drv.start_ok || !hitec_xfer_valid(x)
        || x->family != g_drv.family
        || (x->kind == HITEC_XFER_LEVEL && x->level != 0u
            && g_drv.drive == HITEC_DRIVE_OPEN_DRAIN)) {
        return false;
    }
    g_drv.starts++;
    g_drv.x = *x;
    g_drv.busy = true;
    g_drv.aborted = false;
    g_drv.t_start = g_drv.us;
    if (x->kind == HITEC_XFER_SEND) {
        tx_us = x->lead_us
                + bits_us(x->family, (uint32_t)x->n_tx * (10u + x->gap_bits));
    } else if (x->kind == HITEC_XFER_LEVEL) {
        tx_us = x->level_us;
    }
    g_drv.t_rx = g_drv.us + tx_us;
    servo_answers(x);
    /* What comes after the window is not heard. */
    while (x->window_us != 0u && g_drv.n_reply > 0u
           && g_drv.reply[g_drv.n_reply - 1u].at_ns / 1000u > x->window_us) {
        g_drv.n_reply--;
    }
    if (g_drv.n_reply > x->max_rx) {
        g_drv.n_reply = x->max_rx;
    }
    if (x->max_rx > 0u && g_drv.n_reply == x->max_rx) {
        g_drv.t_end = g_drv.t_rx
                      + g_drv.reply[g_drv.n_reply - 1u].at_ns / 1000u
                      + bits_us(x->family, 10u);
    } else {
        g_drv.t_end = g_drv.t_rx + x->window_us;
    }
    return true;
}

static inline bool drv_poll(void *ctx, hitec_capture_t *out)
{
    const uint32_t now = g_drv.us;
    bool ended;

    (void)ctx;
    if (!g_drv.busy) {
        return false;
    }
    ended = g_drv.aborted || (!g_drv.hang && (int32_t)(now - g_drv.t_end) >= 0);
    if (!ended) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->family = g_drv.family;
    out->window_ns = g_drv.x.window_us * 1000u;
    for (unsigned i = 0; i < g_drv.n_reply; ++i) {
        /* Cut: only the bytes that had come. */
        if (g_drv.aborted && (int32_t)(now - g_drv.t_rx) < 0) {
            break;
        }
        if (g_drv.aborted
            && g_drv.reply[i].at_ns / 1000u > now - g_drv.t_rx) {
            break;
        }
        out->byte[out->n++] = g_drv.reply[i];
    }
    out->full = (uint8_t)(g_drv.x.max_rx > 0u && out->n == g_drv.x.max_rx);
    out->answered = (uint8_t)(g_drv.x.answer != 0u && out->n > 0u
                              && !g_drv.aborted);
    out->aborted = (uint8_t)g_drv.aborted;
    g_drv.busy = false;
    g_drv.aborted = false;
    return true;
}

static inline uint32_t drv_now(void *ctx)
{
    (void)ctx;
    return g_drv.us;
}

/* ----------------------------------------------------------- the hardware */

static inline uint8_t hw_pin(void *ctx, uint8_t port)
{
    (void)ctx;
    return g_hw.pin[port];
}

static inline bool hw_take(void *ctx, uint8_t port, uint8_t pin,
                           uint8_t family, uint8_t drive, uint8_t rx)
{
    (void)ctx;
    g_hw.takes++;
    g_hw.take_rx = rx;
    g_hw.take_port = port;
    g_hw.take_pin = pin;
    g_hw.take_family = family;
    g_hw.take_drive = drive;
    if (!g_hw.take_ok) {
        return false;
    }
    g_drv.open = true;
    g_drv.family = family;
    g_drv.drive = drive;
    return true;
}

static inline void hw_give(void *ctx)
{
    (void)ctx;
    g_hw.gives++;
    g_drv.open = false;
    g_drv.busy = false;
}

static inline void hw_abort(void *ctx)
{
    (void)ctx;
    g_drv.aborts++;
    if (g_drv.busy) {
        g_drv.aborted = true;
    }
}

static inline void hw_run(void *ctx, hitec_port_run_t *out)
{
    (void)ctx;
    out->lead_ns = g_drv.x.lead_us * 1000u;
    out->span_us = 1234u;
    out->bit_ns = hitec_bits_ns(g_drv.family, 1u);
}

static inline void hw_supply(void *ctx, supply_port_view_t *out)
{
    (void)ctx;
    *out = g_hw.v;
}

static inline bool hw_supply_set(void *ctx, bool on, uint16_t mv,
                                 uint16_t ma)
{
    (void)ctx;
    g_hw.sets++;
    if (!on) {
        g_hw.offs++;
        g_hw.v.asked_on = false;
        g_hw.v.on = false;
        g_hw.v.off = false;
        g_hw.moving = true;
        g_hw.asked_ms = g_ms;
        return true;
    }
    if (!g_hw.set_ok || !g_hw.v.online) {
        return false;
    }
    g_hw.ons++;
    g_hw.on_ms = g_ms;
    g_hw.listen_at_on_us = g_drv.us;
    g_hw.v.asked_on = true;
    g_hw.v.set_mv = mv;
    g_hw.v.set_ma = ma;
    /* The read lags: off stays as last read until the PD mini reports. */
    g_hw.v.on = false;
    g_hw.moving = true;
    g_hw.asked_ms = g_ms;
    g_drv.boot_pending = true;
    return true;
}

static inline void fresh_at(uint32_t t0, supply_kind_t supply)
{
    hitec_port_hw_t hw;

    memset(&g_drv, 0, sizeof(g_drv));
    memset(&g_hw, 0, sizeof(g_hw));
    g_drv.start_ok = true;
    g_drv.boot_byte = 0x5Au;
    g_drv.boot_ns = 30000000u;
    for (unsigned i = 0; i < 256u; ++i) {
        g_drv.eeprom[i] = (uint8_t)(i ^ 0x3Cu);
        g_drv.dreg[i] = (uint16_t)(0x1200u + i);
    }
    g_drv.us = 1000u * t0;
    g_hw.pin[CH] = PIN;
    g_hw.pin[OTHER] = 0xFFu;
    g_hw.pin[2] = 0xFFu;
    g_hw.pin[3] = 0xFFu;
    g_hw.take_ok = true;
    g_hw.set_ok = true;
    g_hw.follow = true;
    g_hw.has_supply = supply != SUPPLY_NONE;
    g_hw.v.online = supply == SUPPLY_ON;
    g_hw.v.asked_on = supply == SUPPLY_ON;
    g_hw.v.on = supply == SUPPLY_ON;
    g_hw.v.set_mv = MV;
    g_hw.v.set_ma = MA;
    g_hw.v.vin_mv = 20000u;

    memset(&hw, 0, sizeof(hw));
    hw.ports = 2u;
    hw.pin = hw_pin;
    hw.take = hw_take;
    hw.give = hw_give;
    hw.abort = hw_abort;
    hw.run = hw_run;
    if (supply != SUPPLY_NONE) {
        hw.supply = hw_supply;
        hw.supply_set = hw_supply_set;
    }
    hw.driver.start = drv_start;
    hw.driver.poll = drv_poll;
    hw.driver.now_us = drv_now;
    CHECK(hitec_port_init(&g_port, &hw));
    g_ms = t0;
    hitec_port_step(&g_port, true, false, t0);
}

static inline void fresh(void)
{
    fresh_at(0u, SUPPLY_ON);
}

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

static inline void tick_as(bool safe, bool stop)
{
    if (g_hw.follow && g_hw.moving
        && (uint32_t)(g_ms - g_hw.asked_ms) >= LAG_MS) {
        g_hw.v.on = g_hw.v.asked_on;
        g_hw.v.off = !g_hw.v.asked_on;
        g_hw.moving = false;
        if (g_hw.v.off) {
            g_hw.off_read_ms = g_ms;
        }
    }
    hitec_port_step(&g_port, safe, stop, g_ms);
    g_ms++;
    g_drv.us += 1000u;
}

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

