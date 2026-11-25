/*
 * The bench the YGE_PROG suites run on: the page of shared/outputs/yge_port.c
 * on the simulated ESC of oy_sim.h, with the pin's, the line's and the
 * supply's hooks faked and a clock that steps 1ms a pass.
 *
 * The ESC has power while the PD mini reads on (SWITCHED) or while a test
 * says so (BY HAND, g.battery), and hears and answers only then and only
 * while the line listens.  Power on starts it as from cold: auto frames
 * 300ms later, every 50ms, until a request takes it over.
 *
 * For test_yge_port.c, test_yge_port_stop.c, test_yge_port_gate.c and
 * test_yge_console.c.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <string.h>

#include "greatest.h"
#include "link_crc.h"
#include "oy_sim.h"
#include "prog_supply.h"
#include "safety_gate.h"
#include "yge_port.h"

static yge_port_t g_p;

/* A helper not every suite of the rig calls. */
#define RIG_MAY_GO_UNUSED __attribute__((unused))

/* The hooks' side. */
static struct {
    uint8_t  pin[4];           /* each port's pin, YGE_PORT_NO_PIN none */
    bool     take_ok;
    int      takes, gives;
    uint8_t  last_port, last_pin;
    bool     line_open;
    bool     listen;           /* the line's pull-up and receiver */
    int      parks;            /* line(false) calls */
    bool     give_refused;
    supply_port_view_t v;
    bool     refuse_on;
    bool     stuck_on;         /* never reads off */
    bool     never_on;
    bool     dropped;          /* read off after it read on */
    uint32_t on_lag_ms;        /* ON reads on this long after the ask */
    int      ons, offs;
    uint16_t on_mv, on_ma;
    uint32_t on_at_us;
    bool     listen_at_on;     /* the line listened when ON was asked */
    bool     battery;          /* BY HAND: the operator's battery is on */
    bool     powered;          /* the ESC had power at the last look */
    int      others_off;
    uint16_t control[LINK_CT_COUNT]; /* as the give clears it */
    prog_supply_t psup;              /* as others_off switches it */
} g;

/* The ESC's power: the PD mini read on, or the battery. */
static bool esc_powered(void)
{
    return g.battery || (g.v.asked_on && g.v.on);
}

/* An ESC that just got power starts from cold, its table kept. */
static void esc_follow_power(void)
{
    const bool now = esc_powered();

    if (now && !g.powered) {
        sim.t_on = sim.us;
        sim.taken = false;
        sim.auto_next = sim.auto_first_us;
        sim.cyc = 0u;
        sim.nq = 0u;
        sim.n_out = 0u;
    }
    g.powered = now;
}

static bool rig_tx(void *ctx, const uint8_t *f, size_t n)
{
    esc_follow_power();
    if (!g.line_open || !g.listen) {
        return false;
    }
    if (!g.powered) {
        /* Recorded, never answered. */
        const bool silent = sim.silent;

        sim.silent = true;
        (void)sim_tx(ctx, f, n);
        sim.silent = silent;
        return true;
    }
    return sim_tx(ctx, f, n);
}

static size_t rig_rx(void *ctx, uint8_t *buf, size_t cap)
{
    esc_follow_power();
    if (!g.line_open || !g.listen || !g.powered) {
        return 0u;
    }
    return sim_rx(ctx, buf, cap);
}

static uint8_t hw_pin(void *ctx, uint8_t port)
{
    (void)ctx;
    return port < 4u ? g.pin[port] : YGE_PORT_NO_PIN;
}

static bool hw_take(void *ctx, uint8_t port, uint8_t pin)
{
    (void)ctx;
    g.takes++;
    g.last_port = port;
    g.last_pin = pin;
    g.line_open = g.take_ok;
    g.listen = false;
    return g.take_ok;
}

static bool hw_give(void *ctx)
{
    (void)ctx;
    g.gives++;
    g.line_open = false;
    g.listen = false;
    safety_gate_hold_ended(g.control);
    return !g.give_refused;
}

static void hw_line(void *ctx, bool listen)
{
    (void)ctx;
    g.listen = listen;
    g.parks += listen ? 0 : 1;
}

static void hw_supply(void *ctx, supply_port_view_t *out)
{
    (void)ctx;
    if (g.v.asked_on && !g.never_on && !g.dropped
        && sim.us - g.on_at_us >= g.on_lag_ms * 1000u) {
        g.v.off = false;
        g.v.on = true;
    }
    *out = g.v;
}

static bool hw_supply_set(void *ctx, bool on, uint16_t mv, uint16_t ma)
{
    (void)ctx;
    if (on) {
        if (g.refuse_on) {
            return false;
        }
        g.ons++;
        g.on_mv = mv;
        g.on_ma = ma;
        g.on_at_us = sim.us;
        g.listen_at_on = g.listen;
        g.v.asked_on = true;
    } else {
        g.offs++;
        g.v.asked_on = false;
        g.v.on = false;
        g.v.off = !g.stuck_on;
    }
    esc_follow_power();
    return true;
}

static void hw_others_off(void *ctx)
{
    (void)ctx;
    g.others_off++;
    (void)prog_supply_port_set(&g.psup, false, true);
}

/* ------------------------------------------------------------ the bench */

static void rig_up(void)
{
    memset(&g, 0, sizeof(g));
    memset(g.pin, YGE_PORT_NO_PIN, sizeof(g.pin));
    sim_reset(1000u * MS);
    g.pin[0] = 13u;     /* port 0 on GPIO 13 */
    g.pin[2] = 15u;     /* port 2 on GPIO 15 */
    g.take_ok = true;
    g.v.online = true;
    g.v.off = true;
    const yge_port_hw_t hw = {
        .ctx = NULL, .ports = 4u, .pin = hw_pin, .take = hw_take,
        .give = hw_give, .line = hw_line, .supply = hw_supply,
        .supply_set = hw_supply_set, .others_off = hw_others_off,
        .driver = { NULL, rig_tx, rig_rx, sim_now },
    };

    (void)yge_port_init(&g_p, &hw);
}

static uint32_t now_ms(void)
{
    return sim.us / 1000u;
}

/* One pass, 1ms on. */
static void pass_as(bool safe, bool stop)
{
    sim.us += MS;
    yge_port_step(&g_p, safe, stop, now_ms());
}

static void pass(void)
{
    pass_as(true, false);
}

static RIG_MAY_GO_UNUSED void run_ms(uint32_t ms)
{
    for (uint32_t i = 0u; i < ms; ++i) {
        pass();
    }
}

/* Passes until the state is left or @p ms have gone. */
static void run_while(link_yge_state_t st, uint32_t ms)
{
    for (uint32_t i = 0u; i < ms && yge_port_state(&g_p) == st; ++i) {
        pass();
    }
}

static uint16_t reg(unsigned i)
{
    uint16_t v = 0xDEADu;

    yge_port_read(&g_p, (uint8_t)i, 1u, &v);
    return v;
}

static RIG_MAY_GO_UNUSED unsigned result(void)
{
    return reg(LINK_YP_RESULT) & 0xFFu;
}

static unsigned refusal(void)
{
    return reg(LINK_YP_RESULT) >> 8;
}

static bool flag(uint16_t f)
{
    return (reg(LINK_YP_FLAGS) & f) != 0u;
}

static RIG_MAY_GO_UNUSED unsigned line_word(void)
{
    return reg(LINK_YP_LINE) & 0xFFu;
}

/* The staged registers, with defaults a test changes before stage(). */
static uint16_t g_st[LINK_YP_W_STAGED];

#define ST_REG(r) g_st[(r) - LINK_YP_W_PORT]

static void stage_defaults(uint16_t port)
{
    memset(g_st, 0, sizeof(g_st));
    ST_REG(LINK_YP_W_PORT) = port;
    ST_REG(LINK_YP_W_POWER) = LINK_YGE_SWITCHED;
    ST_REG(LINK_YP_W_MV) = 7400u;
    ST_REG(LINK_YP_W_MA) = 1000u;
    ST_REG(LINK_YP_W_BOOT_MS) = (uint16_t)OY_BOOT_MS_MIN;
}

static uint8_t stage(void)
{
    return yge_port_write(&g_p, LINK_YP_W_PORT, LINK_YP_W_STAGED, g_st,
                          now_ms());
}

/* A command with the next SEQ; the CRC of g_st unless @p bad_crc. */
static uint8_t cmd_full(link_yge_op_t op, uint16_t key, bool bad_crc)
{
    uint16_t f[LINK_YP_W_CMD_FRAME];

    f[LINK_YP_W_CMD] = (uint16_t)op;
    f[LINK_YP_W_SEQ] = (uint16_t)(reg(LINK_YP_SEQ) + 1u);
    f[LINK_YP_W_KEY] = key;
    f[LINK_YP_W_CRC] = (uint16_t)(link_kst_crc(g_st, LINK_YP_W_STAGED)
                                  ^ (bad_crc ? 1u : 0u));
    return yge_port_write(&g_p, 0u, LINK_YP_W_CMD_FRAME, f, now_ms());
}

/* A command; ENTER and WRITE carry the key, as a panel sends them after its
 * 2s hold. */
static uint8_t cmd(link_yge_op_t op)
{
    const bool keyed = op == LINK_YGE_OP_ENTER || op == LINK_YGE_OP_WRITE;

    return cmd_full(op, keyed ? LINK_YGE_KEY : 0u, false);
}

/* OPEN of @p port, through STOPPING, to the pin held. */
static void open_port(uint16_t port)
{
    stage_defaults(port);
    (void)stage();
    pass();
    (void)cmd(LINK_YGE_OP_OPEN);
    run_while(LINK_YGE_STOPPING, 200u);
}

/* ENTER as staged, to READY or the end of it. */
static void enter_now(void)
{
    (void)cmd(LINK_YGE_OP_ENTER);
    run_while(LINK_YGE_POWER_OFF, 5000u);
    run_while(LINK_YGE_POWER_ON, 5000u);
    run_while(LINK_YGE_RUN, 30000u);
}

/* Port 0 held and the ESC taken over. */
static RIG_MAY_GO_UNUSED void ready_on(uint16_t port)
{
    open_port(port);
    enter_now();
}

/* A read, to its end. */
static RIG_MAY_GO_UNUSED void read_now(void)
{
    (void)cmd(LINK_YGE_OP_READ);
    run_while(LINK_YGE_RUN, 30000u);
}

/* Put @p t, LINK_YP_ENTRIES values, in the draft, in blocks. */
static void draft_put(const uint16_t *t)
{
    uint16_t all[OPENYGE_MAX_PARAMS];

    memset(all, 0, sizeof(all));
    memcpy(all, t, (size_t)reg(LINK_YP_ENTRIES) * sizeof(all[0]));
    for (unsigned k = 0u; k < 2u; ++k) {
        const uint16_t which = (uint16_t)(LINK_YD_DRAFT + k);

        (void)yge_port_write(&g_p, LINK_YP_W_BLOCK, 1u, &which, now_ms());
        (void)yge_port_data_write(&g_p, 0u, LINK_YD_COUNT,
                                  &all[k * LINK_YD_COUNT]);
    }
}

/* The draft @p draft of the read copy staged and WRITE sent. */
static RIG_MAY_GO_UNUSED uint8_t write_table(const uint16_t *draft)
{
    draft_put(draft);
    ST_REG(LINK_YP_W_READS) = reg(LINK_YP_READS);
    ST_REG(LINK_YP_W_READ_CRC) = reg(LINK_YP_READ_CRC);
    ST_REG(LINK_YP_W_DRAFT_CRC) = oy_guard_table_crc(draft,
                                                     reg(LINK_YP_ENTRIES));
    (void)stage();
    return cmd(LINK_YGE_OP_WRITE);
}

/* The read copy with index @p i set to @p v. */
static uint16_t g_draft[OPENYGE_MAX_PARAMS];

static RIG_MAY_GO_UNUSED void draft_of(unsigned i, uint16_t v)
{
    memcpy(g_draft, g_p.ses.orig, sizeof(g_draft));
    g_draft[i] = v;
}

/* What an error or a stop left: the line parked, the supply off. */
#define CHECK_ALL_OFF()                                                  \
    do {                                                                 \
        CHECK(!g.listen);                                                \
        CHECK_EQ(line_word(), LINK_YGE_LINE_PARK);                       \
        CHECK(!g.v.asked_on);                                            \
        CHECK(!flag(LINK_YP_F_SUPPLY));                                  \
        CHECK(!flag(LINK_YP_F_TAKEN));                                   \
        CHECK(!yge_port_writing(&g_p));                                  \
        CHECK_EQ(yge_port_state(&g_p), LINK_YGE_OPEN);                   \
    } while (0)
