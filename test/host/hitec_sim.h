/*
 * The servo model the HITEC page suites and the panel's link half run on: a
 * model of the wire driver with a servo on it, the port's hardware with a
 * simulated PD mini, the page itself (shared/outputs/hitec_port.c) and the
 * clock.
 *
 * The servo: a classic EEPROM of 256 cells answering 0x61 and 0x62, and,
 * with c_entry, the classic entry (the `+` after the power-up, the next
 * `+` HITEC_SIM_NEXT_US after it, and the version to 0x67); a D series
 * register file of 256 registers answering reads and taking writes, the
 * save and the reboot counted.  A LISTEN hears the boot
 * byte after an ON, the next `+` of a classic entry, or listen_n bytes.
 *
 * For hitec_port_rig.h (the page suites) and test_hitec_link*.c.
 *
 * The clock: a pass is 1ms on the port's clock and 1000us on the
 * driver's.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "hitec_port.h"
#include "hitec_reg.h"
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
/* A classic servo's next `+` after its first one: measured 250 to 267ms. */
#define HITEC_SIM_NEXT_US 250000u

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
    uint32_t listen_gap_ns; /* idle between two of them */
    /* The classic entry: a boot byte answered, the servo's next `+` comes
     * HITEC_SIM_NEXT_US after it; 0x67 gets the version and `+`. */
    bool     c_entry;
    bool     next_pending;
    uint32_t next_at_us;    /* the driver's clock of the next `+` */
    uint8_t  version;
    /* A D series register whose answer carries a wrong sum, plus 1; 0
     * none. */
    uint16_t d_bad_sum_plus1;
    /* D series writes taken, saves and reboots; with d_drop the servo
     * takes no write. */
    unsigned d_writes, d_saves, d_reboots;
    bool     d_drop;
    /* Classic writes: with c_mute the servo neither takes nor answers
     * them; c_writes counts the ones taken. */
    unsigned c_writes;
    bool     c_mute;
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
            if (g_drv.c_entry && x->answer != 0u
                && (uint64_t)x->window_us * 1000u >= g_drv.boot_ns) {
                g_drv.next_pending = true;
                g_drv.next_at_us =
                    g_drv.us + g_drv.boot_ns / 1000u + HITEC_SIM_NEXT_US;
            }
        } else if (g_drv.next_pending) {
            const uint8_t plus = HITEC_C_ACK;
            const int32_t left = (int32_t)(g_drv.next_at_us - g_drv.us);

            /* A `+` sent before the receiver starts is not heard. */
            g_drv.next_pending = false;
            if (left > 0) {
                reply_bytes(&plus, 1u, (uint32_t)left * 1000u);
            }
        } else if (g_drv.listen_n > 0u) {
            uint8_t b[HITEC_RX_MAX];

            for (unsigned i = 0; i < g_drv.listen_n; ++i) {
                b[i] = (uint8_t)(0xA0u + i);
            }
            reply_bytes(b, g_drv.listen_n, 1000000u);
            for (unsigned i = 1; i < g_drv.listen_n; ++i) {
                g_drv.reply[i].at_ns += i * g_drv.listen_gap_ns;
            }
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
        } else if (x->tx[0] == HITEC_C_FIRST && g_drv.c_entry) {
            r[0] = g_drv.version;
            r[1] = HITEC_C_ACK;
        } else if (x->tx[0] == HITEC_C_WRITE_EEPROM) {
            if (g_drv.c_mute) {
                return;
            }
            g_drv.eeprom[x->tx[1]] = x->tx[2];
            g_drv.c_writes++;
            r[0] = HITEC_C_ACK;
            r[1] = HITEC_C_ACK;
        } else {
            return;
        }
        /* 520us from the start of the master's last byte. */
        reply_bytes(r, 2u, 520000u - hitec_bits_ns(HITEC_CLASSIC, 9u));
    } else if (x->family == HITEC_D && x->n_tx == HITEC_D_READ_LEN
               && x->tx[0] == HITEC_D_HEADER_REQUEST) {
        const uint16_t v = g_drv.dreg[x->tx[2]];
        uint8_t a[HITEC_D_ANSWER_LEN + 1u] = {
            HITEC_D_HEADER_ANSWER, x->tx[1], x->tx[2], HITEC_D_DATA_LEN,
            (uint8_t)(v & 0xFFu), (uint8_t)(v >> 8), 0u, 0u };

        a[6] = hitec_d_sum(&a[1], 5u);
        if (g_drv.d_bad_sum_plus1 == (uint16_t)(x->tx[2] + 1u)) {
            a[6] ^= 0xFFu;
        }
        reply_bytes(a, 8u, 6000000u);
    } else if (x->family == HITEC_D && x->n_tx == HITEC_D_WRITE_LEN
               && x->tx[0] == HITEC_D_HEADER_REQUEST
               && x->tx[3] == HITEC_D_DATA_LEN
               && hitec_d_sum(&x->tx[1], 5u) == x->tx[6]) {
        /* A write has no answer. */
        if (x->tx[2] == HITEC_D_REG_SAVE) {
            g_drv.d_saves++;
        } else if (x->tx[2] == HITEC_D_REG_REBOOT) {
            g_drv.d_reboots++;
        } else if (!g_drv.d_drop) {
            g_drv.dreg[x->tx[2]] = (uint16_t)(x->tx[4] | (x->tx[5] << 8));
            g_drv.d_writes++;
        }
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
    /* The window counts the waiting for a start bit only, none inside a
     * byte or the answer to the first: a byte whose start comes after that
     * much waiting is not heard, nor any after it. */
    if (x->window_us != 0u) {
        const uint32_t frame_ns = hitec_bits_ns(g_drv.family, 10u);
        uint64_t wait_ns = 0;
        uint8_t heard = 0;

        for (unsigned i = 0; i < g_drv.n_reply; ++i) {
            const uint32_t from_ns =
                i == 0u ? 0u
                        : g_drv.reply[i - 1u].at_ns + frame_ns
                              + (i == 1u && x->answer != 0u ? frame_ns : 0u);

            if (g_drv.reply[i].at_ns > from_ns) {
                wait_ns += g_drv.reply[i].at_ns - from_ns;
            }
            if (wait_ns > (uint64_t)x->window_us * 1000u) {
                break;
            }
            heard++;
        }
        g_drv.n_reply = heard;
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

/* The ports the build has: 2 unless a case asks for the 4 of protocol
 * 4.26 (rig_ports()). */
static unsigned g_rig_ports = 2u;

static inline void fresh_at(uint32_t t0, supply_kind_t supply)
{
    hitec_port_hw_t hw;

    memset(&g_drv, 0, sizeof(g_drv));
    memset(&g_hw, 0, sizeof(g_hw));
    g_drv.start_ok = true;
    g_drv.boot_byte = 0x5Au;
    g_drv.boot_ns = 30000000u;
    g_drv.version = 0x16u;  /* an HS-5645MG */
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
    hw.ports = (uint8_t)g_rig_ports;
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


/* ---------------------------------------------------------------- the clock */

/* One pass of the IOmcu: the PD mini reads back what it was asked
 * LAG_MS late, the page steps, both clocks move 1ms. */
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
