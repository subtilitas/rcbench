/*
 * The bench the ESC_WIRE suites run on: the page of shared/outputs/esc_port.c on
 * the simulated ESC of ew_sim.h, with the pin's and the supply's hooks faked and
 * a clock that steps 500us a pass.
 *
 * For test_esc_port.c, test_esc_port_data.c and test_esc_port_stop.c.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <string.h>

#include "esc_port.h"
#include "ew_sim.h"
#include "greatest.h"
#include "link_crc.h"
#include "prog_supply.h"
#include "safety_gate.h"

static sim_t esc;
static esc_port_t g_p;

/* The hooks' side. */
static struct {
    uint8_t  pin[2][8];        /* [is_port][index] */
    bool     take_ok;
    int      takes, gives;
    uint8_t  last_pin;
    supply_port_view_t v;
    bool     refuse_on;
    bool     stuck_on;         /* never reads off */
    int      ons, offs;
    uint16_t on_mv, on_ma;
    uint8_t  line_at_on;
    uint32_t on_at, off_at;
    uint8_t  line_at_give;
    bool     line_open;
    int      takes_with_line;
    int      others_off;
    bool     give_refused;
    uint32_t on_lag_ms;        /* ON reads on this long after the ask */
    bool     never_on;
    uint16_t control[LINK_CT_COUNT]; /* as esc_hw_give() clears it */
    prog_supply_t psup;              /* as esc_hw_others_off() switches it */
} g;

static uint8_t hw_pin(void *ctx, bool is_port, uint8_t index)
{
    (void)ctx;
    return g.pin[is_port ? 1 : 0][index];
}

static bool hw_take(void *ctx, uint8_t pin)
{
    (void)ctx;
    g.takes++;
    g.last_pin = pin;
    g.line_open = g.take_ok;
    return g.take_ok;
}

static bool hw_give(void *ctx)
{
    (void)ctx;
    g.gives++;
    g.line_at_give = esc.line;
    g.line_open = false;
    safety_gate_hold_ended(g.control);
    return !g.give_refused;
}

static void hw_supply(void *ctx, supply_port_view_t *out)
{
    (void)ctx;
    if (g.v.asked_on && !g.never_on && esc.now - g.on_at >= g.on_lag_ms * 1000u) {
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
        g.line_at_on = esc.line;
        g.on_at = esc.now;
        g.v.asked_on = true;
    } else {
        g.offs++;
        g.off_at = esc.now;
        /* The ESC this switched on loses its power. */
        if (g.v.asked_on) {
            sim_power_cycle(&esc);
        }
        g.v.asked_on = false;
        g.v.on = false;
        g.v.off = !g.stuck_on;
    }
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
    memset(g.pin, ESC_PORT_NO_PIN, sizeof(g.pin));
    g.pin[0][2] = 5u;   /* slot 2: a throttle on GPIO 5 */
    g.pin[1][0] = 13u;  /* port 0: a DShot port on GPIO 13 */
    g.take_ok = true;
    g.v.online = true;
    g.v.off = true;
    const esc_port_hw_t hw = {
        .ctx = NULL, .ports = 4u, .pin = hw_pin, .take = hw_take, .give = hw_give,
        .supply = hw_supply, .supply_set = hw_supply_set,
        .others_off = hw_others_off,
        .driver = sim_driver(&esc),
    };

    (void)esc_port_init(&g_p, &hw);
}

static void up_am_rig(void)
{
    sim_init_am(&esc, 4u, 2u);
    rig_up();
}

static void up_s_rig(void)
{
    sim_init_efm8(&esc, 0xB2u, false);
    rig_up();
}

/* One pass. */
static void pass_as(bool safe, bool stop)
{
    esc.now += 500u;
    esc_port_step(&g_p, safe, stop, esc.now / 1000u);
}

static void pass(void)
{
    pass_as(true, false);
}

static void run_ms(uint32_t ms)
{
    for (uint32_t i = 0u; i < ms * 2u; ++i) {
        pass();
    }
}

/* Passes until the state is left or @p ms have gone. */
static void run_while(link_esc_state_t st, uint32_t ms)
{
    for (uint32_t i = 0u; i < ms * 2u && esc_port_state(&g_p) == st; ++i) {
        pass();
    }
}

static uint16_t reg(unsigned i)
{
    uint16_t v = 0xDEADu;

    esc_port_read(&g_p, (uint8_t)i, 1u, &v);
    return v;
}

static unsigned result(void)
{
    return reg(LINK_EW_RESULT) & 0xFFu;
}

static unsigned refusal(void)
{
    return reg(LINK_EW_RESULT) >> 8;
}

static bool flag(uint16_t f)
{
    return (reg(LINK_EW_FLAGS) & f) != 0u;
}

static unsigned line_word(void)
{
    return reg(LINK_EW_LINE) & 0xFFu;
}

/* The staged registers, with defaults a test changes before stage(). */
static uint16_t g_st[LINK_EW_W_STAGED];

#define ST_REG(r) g_st[(r) - LINK_EW_W_TARGET]

static void stage_defaults(uint16_t target)
{
    memset(g_st, 0, sizeof(g_st));
    ST_REG(LINK_EW_W_TARGET) = target;
    ST_REG(LINK_EW_W_POWER) = LINK_ESC_SWITCHED;
    ST_REG(LINK_EW_W_MV) = 7400u;
    ST_REG(LINK_EW_W_MA) = 1000u;
    ST_REG(LINK_EW_W_HOLD_LO) = 3000u;
    ST_REG(LINK_EW_W_OFF_MS) = 1000u;
}

static uint8_t stage(void)
{
    return esc_port_write(&g_p, LINK_EW_W_TARGET, LINK_EW_W_STAGED, g_st,
                          esc.now / 1000u);
}

/* A command with the next SEQ; the CRC of g_st unless @p bad_crc. */
static uint8_t cmd_full(link_esc_op_t op, uint16_t key, bool bad_crc)
{
    uint16_t f[LINK_EW_W_CMD_FRAME];

    f[LINK_EW_W_CMD] = (uint16_t)op;
    f[LINK_EW_W_SEQ] = (uint16_t)(reg(LINK_EW_SEQ) + 1u);
    f[LINK_EW_W_KEY] = key;
    f[LINK_EW_W_CRC] = (uint16_t)(link_kst_crc(g_st, LINK_EW_W_STAGED)
                                  ^ (bad_crc ? 1u : 0u));
    return esc_port_write(&g_p, 0u, LINK_EW_W_CMD_FRAME, f, esc.now / 1000u);
}

static uint8_t cmd(link_esc_op_t op)
{
    return cmd_full(op, 0u, false);
}

/* OPEN of @p target, through STOPPING, to the pin held. */
static void open_target(uint16_t target)
{
    stage_defaults(target);
    (void)stage();
    pass();
    (void)cmd(LINK_ESC_OP_OPEN);
    run_while(LINK_ESC_STOPPING, 200u);
}

/* ENTER as staged, to READY or the end of it. */
static void enter_now(void)
{
    (void)cmd(LINK_ESC_OP_ENTER);
    run_while(LINK_ESC_POWER_OFF, 15000u);
    run_while(LINK_ESC_LEAD, 200u);
    run_while(LINK_ESC_POWER_ON, 5000u);
    run_while(LINK_ESC_RUN, 20000u);
}

/* Slot 2 held and the ESC answering. */
static void ready_on(uint16_t target)
{
    open_target(target);
    enter_now();
}

/* A read, to its end. */
static void read_now(void)
{
    (void)cmd(LINK_ESC_OP_READ);
    run_while(LINK_ESC_RUN, 20000u);
}

/* Put @p page in the draft, in blocks. */
static void draft_put(const uint8_t *page, size_t n)
{
    uint16_t b[LINK_ED_COUNT];
    uint8_t all[EW_PAGE_MAX];

    memset(all, 0, sizeof(all));
    memcpy(all, page, n);
    for (unsigned k = 0u; k < 4u; ++k) {
        const uint16_t which = (uint16_t)(LINK_ED_DRAFT + k);

        (void)esc_port_write(&g_p, LINK_EW_W_BLOCK, 1u, &which, esc.now / 1000u);
        for (unsigned i = 0u; i < LINK_ED_COUNT; ++i) {
            const size_t at = (size_t)k * LINK_ED_BLOCK_BYTES + i * 2u;

            b[i] = (uint16_t)(all[at] | ((uint16_t)all[at + 1u] << 8));
        }
        (void)esc_port_data_write(&g_p, 0u, LINK_ED_COUNT, b);
    }
}

/* Read the block's registers into bytes. */
static void block_get(unsigned block, uint8_t *out)
{
    uint16_t b[LINK_ED_COUNT];
    const uint16_t which = (uint16_t)block;

    (void)esc_port_write(&g_p, LINK_EW_W_BLOCK, 1u, &which, esc.now / 1000u);
    esc_port_data_read(&g_p, 0u, LINK_ED_COUNT, b);
    for (unsigned i = 0u; i < LINK_ED_COUNT; ++i) {
        out[i * 2u] = (uint8_t)(b[i] & 0xFFu);
        out[i * 2u + 1u] = (uint8_t)(b[i] >> 8);
    }
}

/* The read copy, the draft @p draft of it staged for a WRITE. */
static uint8_t write_page(const uint8_t *draft)
{
    const uint16_t len = reg(LINK_EW_LEN);

    draft_put(draft, len);
    ST_REG(LINK_EW_W_READS) = reg(LINK_EW_READS);
    ST_REG(LINK_EW_W_READ_CRC) = reg(LINK_EW_READ_CRC);
    ST_REG(LINK_EW_W_DRAFT_CRC) = link_crc(LINK_CRC_INIT, draft, len);
    (void)stage();
    return cmd_full(LINK_ESC_OP_WRITE, LINK_ESC_WRITE_KEY, false);
}

/* What the fault left: the line an input with its pull-down, the supply off. */
#define CHECK_ALL_OFF()                                                  \
    do {                                                                 \
        CHECK_EQ(esc.line, EW_LINE_PARK);                                \
        CHECK_EQ(line_word(), LINK_ESC_LINE_PARK);                       \
        CHECK(!g.v.asked_on);                                            \
        CHECK(!flag(LINK_EW_F_SUPPLY));                                  \
        CHECK(!flag(LINK_EW_F_IN_MODE));                                 \
        CHECK_EQ(esc_port_state(&g_p), LINK_ESC_OPEN);                   \
    } while (0)
