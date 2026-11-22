/*
 * The remote servo node, board 3 (shared/outputs/out_board_rsn.c,
 * rsn_pins.h, board_fix.c): its entry, its catalogue and its roles.
 *
 * Under test: the board's entry and identity; the 27 catalogue entries
 * against the role table and the pin map, with each pad on its header
 * (P1.n = n, P2.n = 16 + n, P3.n = 32 + n, P4.n = 48 + n) and each pad 63
 * or less; every role pin free, every other listed pin reserved and named,
 * the reserved mask the IOmcu builds handing out the role pins only; each
 * output on a PWM slice of its own and every PIO pin in GPIO 16 to 39;
 * a driven slot taken on each servo output and refused on the pins either
 * side; SUPPLY's pins with the pins either side refused, and its set point
 * at 8400mV taken and at 8410mV refused; SENSE's pins, its INA228 taken and
 * its encoder refused; TONE refused whole; each programming port's PIN
 * taken on each servo output and on no pin, refused on each receiver input
 * and the other pins; FUNC NONE, SERVO PWM and the DShot rates taken; the
 * fixed SENSE_MAP judged whole on its 3 parts, 9 watches and 9 shunts of
 * 20.0mΩ; a kept record put on the node's pins; each channel's critical
 * limit at 4000mA, lowered by a lower SENSE_LIMIT and not raised by a
 * higher one, and none on the other boards; the supply's ceiling of
 * 8400mV on the SUPPLY page and on a programming port's ON, 8410mV
 * refused on both.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "board_fix.h"
#include "link_pages.h"
#include "out_bind.h"
#include "out_pwm_map.h"
#include "out_store_rec.h"
#include "outputs.h"
#include "prog_port.h"
#include "rsn_pins.h"
#include "supply_page.h"

#define RSN ((uint16_t)OUTBIND_BOARD_RSN)
#define BIT(pin) ((uint64_t)1u << (pin))

static const outbind_roles_t *roles(void) { return outbind_roles(RSN); }

/* One register written: whether the node takes @p v. */
static bool one(uint8_t page, unsigned reg, uint16_t v)
{
    return board_fix_write_ok(roles(), page, (uint8_t)reg, 1u, &v);
}

/* ------------------------------------------------------------ the board */

TEST_CASE(board_3_is_the_node_with_its_role_pins)
{
    const outbind_board_t *bd = outbind_board(RSN);
    const outbind_roles_t *r = roles();
    if (bd == NULL || r == NULL) { T_FAIL("no board 3"); return; }
    CHECK_EQ(OUTBIND_BOARD_RSN, 3);
    CHECK(bd->fixed);
    CHECK(bd->roles == r);
    CHECK_EQ(bd->count, 27u);
    CHECK(bd->shape == NULL);           /* pads on 4 edges: no two rows */
    CHECK(bd->pads == NULL);
    CHECK_EQ(outbind_out_count(r), 9u);
    for (unsigned k = 0; k < RSN_PORTS; ++k) {
        CHECK_EQ(r->out[k], RSN_PIN_OUT(k));
        CHECK_EQ(board_fix_out_index(r, RSN_PIN_OUT(k)), (int)k);
    }
    CHECK_EQ(r->out[0], 16u);
    CHECK_EQ(r->out[8], 32u);
    CHECK_EQ(r->sda, 4u);
    CHECK_EQ(r->scl, 5u);
    CHECK_EQ(r->pd_tx, 34u);
    CHECK_EQ(r->pd_rx, 35u);
    CHECK_EQ(r->heartbeat, 3u);
    CHECK_EQ(r->tap, OUTBIND_NO_GPIO);
    CHECK_EQ(r->prog_5v_en, OUTBIND_NO_GPIO);
    CHECK(r->prog_on_outs);
    CHECK(r->prog_dshot);
    CHECK(r->no_encoder);
    CHECK_EQ(r->supply_max_mv, 8400u);
    /* The connectors: J1 to J9 one output each, in order. */
    CHECK_EQ(r->conn_count, 9u);
    for (unsigned k = 0; k < r->conn_count; ++k) {
        CHECK_EQ(r->conns[k].count, 1u);
        CHECK_EQ(r->conns[k].gpio[0], RSN_PIN_OUT(k));
    }
    /* The rbmod carrier keeps its 4. */
    CHECK_EQ(outbind_out_count(outbind_roles(OUTBIND_BOARD_RBMOD)), 4u);
    CHECK_EQ(outbind_out_count(NULL), 0u);
}

/* The pad of GPIO @p g on the Core2350B's headers (schematic). */
static unsigned pad_of(unsigned g)
{
    static const struct { uint8_t gpio, pad; } k[] = {
        { 0, 2 }, { 1, 4 }, { 2, 3 }, { 3, 6 }, { 4, 5 }, { 5, 8 },
        { 16, 32 + 4 }, { 17, 32 + 3 }, { 18, 32 + 6 }, { 19, 32 + 5 },
        { 20, 32 + 8 }, { 21, 32 + 7 }, { 22, 32 + 10 }, { 23, 32 + 9 },
        { 24, 32 + 12 }, { 25, 32 + 11 }, { 26, 32 + 14 }, { 27, 32 + 13 },
        { 28, 32 + 16 }, { 29, 32 + 15 }, { 30, 48 + 2 }, { 31, 48 + 4 },
        { 32, 48 + 3 }, { 33, 48 + 6 }, { 34, 48 + 5 }, { 35, 48 + 8 },
        { 39, 48 + 12 },
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        if (k[i].gpio == g) { return k[i].pad; }
    }
    return 0u;
}

/* Bit n: GPIO n is a pin a page role can name. */
static uint64_t role_pins(const outbind_roles_t *r)
{
    uint64_t m = BIT(r->sda) | BIT(r->scl) | BIT(r->pd_tx) | BIT(r->pd_rx);
    for (unsigned i = 0; i < outbind_out_count(r); ++i) { m |= BIT(r->out[i]); }
    return m;
}

TEST_CASE(the_catalogue_is_the_role_pins_on_their_pads)
{
    const outbind_board_t *bd = outbind_board(RSN);
    const outbind_roles_t *r = roles();
    if (bd == NULL || r == NULL) { T_FAIL("no board 3"); return; }
    const uint64_t roles_m = role_pins(r);
    /* 9 outputs, 2 bus, 2 PD mini: 13 pins, none twice. */
    CHECK_EQ(__builtin_popcountll(roles_m), 13);
    uint16_t regs[LINK_CAT_COUNT];
    outbind_board_to_regs(bd, regs);
    for (uint8_t i = 0; i < bd->count; ++i) {
        const outbind_pin_t *p = &bd->pins[i];
        if (p->pad != pad_of(p->gpio) || p->pad == 0u || p->pad > 63u) {
            T_FAIL("GP%u on pad %u", p->gpio, p->pad);
        }
        const bool role = (roles_m & BIT(p->gpio)) != 0u;
        if (role == p->reserved) {
            T_FAIL("GP%u: role %d, reserved %d", p->gpio, role, p->reserved);
        }
        CHECK_EQ(LINK_CAT_GPIO(regs[i]), p->gpio);
        CHECK_EQ(LINK_CAT_PAD(regs[i]), p->pad);
    }
    /* Past the 27: no pin. */
    CHECK_EQ(LINK_CAT_PAD(regs[27]), 0u);
    /* The receiver inputs are reserved and named. */
    for (unsigned k = 0; k < RSN_PORTS; ++k) {
        const uint8_t i = outbind_index_of(RSN, (uint8_t)RSN_PIN_IN(k));
        if (i >= bd->count) { T_FAIL("IN%u not listed", k + 1u); continue; }
        CHECK(bd->pins[i].reserved);
        CHECK(bd->pins[i].held_by != NULL
              && strncmp(bd->pins[i].held_by, "RX IN", 5) == 0);
    }
    /* The heartbeat's hold reaches the wire as the heartbeat's. */
    CHECK_EQ(LINK_CAT_HOLD(regs[outbind_index_of(RSN, 3u)]),
             (unsigned)LINK_PIN_HEARTBEAT);
    /* The mask hands out the role pins and nothing else: the port enables,
     * the strap, CRIT_N and the spare pins are left out of the catalogue
     * and reserved with every listed reserved pin. */
    const uint64_t mask = outbind_reserved_mask(RSN);
    CHECK_EQ(~mask, roles_m);
    for (unsigned k = 0; k < RSN_PORTS; ++k) {
        CHECK((mask & BIT(RSN_PIN_PORT_EN(k))) != 0u);
    }
    CHECK((mask & BIT(RSN_PIN_SW_FITTED)) != 0u);
    CHECK((mask & BIT(RSN_PIN_CRIT_N)) != 0u);
    CHECK((mask & BIT(RSN_PIN_RAIL_EN)) != 0u);
    CHECK((mask & BIT(RSN_PIN_PSRAM_CS)) != 0u);
    /* Only the servo outputs are a cell of the grid. */
    CHECK_EQ(outbind_cell_count(RSN), 9u);
    for (uint8_t c = 0; c < 9u; ++c) {
        CHECK_EQ(outbind_cell_index(RSN, c),
                 outbind_index_of(RSN, (uint8_t)RSN_PIN_OUT(c)));
    }
    CHECK_EQ(outbind_cell_index(RSN, 9u), bd->count);
}

TEST_CASE(each_output_has_its_own_slice_and_every_pio_pin_one_window)
{
    uint16_t slices = 0u;
    for (unsigned k = 0; k < RSN_PORTS; ++k) {
        const uint8_t s = out_pwm_slice_of((uint8_t)RSN_PIN_OUT(k));
        CHECK_EQ(s, (uint8_t)k);
        CHECK_EQ(out_pwm_channel_of((uint8_t)RSN_PIN_OUT(k)), 0u);  /* A */
        slices |= (uint16_t)(1u << s);
        /* Input k one GPIO above output k: one mirror instruction. */
        CHECK_EQ(RSN_PIN_IN(k), RSN_PIN_OUT(k) + 1u);
    }
    CHECK_EQ(slices, 0x01FFu);
    /* Every pin a PIO program touches lies in GPIO 16 to 39. */
    CHECK(RSN_PIN_OUT(0) >= 16u && RSN_PIN_IN(8) <= 39u);
    CHECK(RSN_PIN_PD_TX >= 16u && RSN_PIN_PD_RX <= 39u);
    /* No outside signal on an ADC pin (GPIO 40 to 47). */
    CHECK(RSN_PIN_LED < 40u && RSN_PIN_CRIT_N < 40u);
}

/* ------------------------------------------------------------ the pages */

static void slot(uint16_t *slots, unsigned s, uint16_t driver, uint16_t pin)
{
    uint16_t *x = &slots[s * LINK_OS_STRIDE];
    x[LINK_OS_DRIVER]  = driver;
    x[LINK_OS_PIN]     = pin;
    x[LINK_OS_RANGE]   = LINK_OS_RANGE_OF(s, 1u);
    x[LINK_OS_RATE_HZ] = 50u;
}

TEST_CASE(a_driven_slot_sits_on_one_of_the_nine_outputs)
{
    const outbind_roles_t *r = roles();
    uint16_t slots[LINK_OS_ALL];
    memset(slots, 0, sizeof(slots));
    for (unsigned k = 0; k < RSN_PORTS; ++k) {
        slot(slots, k, LINK_DRIVER_PWM, (uint16_t)RSN_PIN_OUT(k));
    }
    CHECK(board_fix_slots_ok(r, slots));
    /* The pins either side of each output: an input or the strap. */
    for (unsigned k = 0; k < RSN_PORTS; ++k) {
        const uint16_t either[2] = { (uint16_t)(RSN_PIN_OUT(k) - 1u),
                                     (uint16_t)(RSN_PIN_OUT(k) + 1u) };
        for (unsigned e = 0; e < 2u; ++e) {
            slot(slots, 12u, LINK_DRIVER_DSHOT, either[e]);
            if (board_fix_slots_ok(r, slots)) {
                T_FAIL("a slot on GP%u is taken", either[e]);
            }
        }
    }
    slot(slots, 12u, LINK_DRIVER_DSHOT_BIDIR, 32u);   /* S9, slot 13 */
    CHECK(board_fix_slots_ok(r, slots));
}

TEST_CASE(each_interface_pin_is_held_and_the_set_point_capped)
{
    static const struct { uint8_t page; unsigned reg; uint16_t pin; } k[] = {
        { LINK_PAGE_SUPPLY, LINK_SP_TX_PIN,  34u },
        { LINK_PAGE_SUPPLY, LINK_SP_RX_PIN,  35u },
        { LINK_PAGE_SENSE,  LINK_SN_SDA_PIN, 4u },
        { LINK_PAGE_SENSE,  LINK_SN_SCL_PIN, 5u },
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        CHECK(one(k[i].page, k[i].reg, k[i].pin));
        CHECK(!one(k[i].page, k[i].reg, (uint16_t)(k[i].pin - 1u)));
        CHECK(!one(k[i].page, k[i].reg, (uint16_t)(k[i].pin + 1u)));
    }
    CHECK(one(LINK_PAGE_SUPPLY, LINK_SP_SET_MV, 8400u));
    CHECK(!one(LINK_PAGE_SUPPLY, LINK_SP_SET_MV, 8410u));
    CHECK(!one(LINK_PAGE_SUPPLY, LINK_SP_SET_MV, 8401u));
    CHECK(one(LINK_PAGE_SUPPLY, LINK_SP_SET_MV, 5000u));
    /* A frame of OUTPUT, SET_MV and SET_MA: judged on the set point. */
    uint16_t on[3] = { 1u, 8410u, 2000u };
    CHECK(!board_fix_write_ok(roles(), LINK_PAGE_SUPPLY, LINK_SP_OUTPUT, 3u,
                              on));
    on[1] = 8400u;
    CHECK(board_fix_write_ok(roles(), LINK_PAGE_SUPPLY, LINK_SP_OUTPUT, 3u,
                             on));
    /* The rbmod carrier's set points are the PD mini's. */
    const uint16_t hi = 8410u;
    CHECK(board_fix_write_ok(outbind_roles(OUTBIND_BOARD_RBMOD),
                             LINK_PAGE_SUPPLY, LINK_SP_SET_MV, 1u, &hi));
    /* The INA228 on J26 and the INA3221s; no encoder. */
    CHECK(one(LINK_PAGE_SENSE, LINK_SN_ENABLE,
              LINK_SN_EN_I228 | LINK_SN_EN_I3221));
    CHECK(!one(LINK_PAGE_SENSE, LINK_SN_ENABLE, LINK_SN_EN_AS5600));
    CHECK(!one(LINK_PAGE_SENSE, LINK_SN_ENABLE, LINK_SN_EN_ALL));
    /* No phase tap: TONE takes no write at all. */
    CHECK(!one(LINK_PAGE_TONE, LINK_TN_ENABLE, 0u));
    CHECK(!one(LINK_PAGE_TONE, LINK_TN_F_MIN_HZ, 1u));
}

TEST_CASE(a_programming_port_takes_any_output_and_no_input)
{
    for (unsigned port = 0; port < LINK_PG_PORTS_MAX; ++port) {
        const unsigned pin = LINK_PG_REG(port, LINK_PG_PIN);
        for (unsigned k = 0; k < RSN_PORTS; ++k) {
            if (!one(LINK_PAGE_PROG, pin, (uint16_t)RSN_PIN_OUT(k))) {
                T_FAIL("port %u refused S%u", port, k + 1u);
            }
            if (one(LINK_PAGE_PROG, pin, (uint16_t)RSN_PIN_IN(k))) {
                T_FAIL("port %u took IN%u", port, k + 1u);
            }
        }
        CHECK(one(LINK_PAGE_PROG, pin, LINK_PG_PIN_NONE));
        CHECK(!one(LINK_PAGE_PROG, pin, 15u));
        CHECK(!one(LINK_PAGE_PROG, pin, 34u));
        CHECK(!one(LINK_PAGE_PROG, pin, 0u));
        CHECK(!one(LINK_PAGE_PROG, pin, 0x00FFu));
        /* Registers of the port that are no pin are the page's. */
        CHECK(one(LINK_PAGE_PROG, LINK_PG_REG(port, LINK_PG_PIN) + 1u, 7u));
        /* FUNC: NONE, SERVO PWM and every DShot rate. */
        const unsigned fn = LINK_PG_FUNC(port);
        CHECK(one(LINK_PAGE_PROG, fn, LINK_PG_FN_NONE));
        CHECK(one(LINK_PAGE_PROG, fn, LINK_PG_FN_SERVO_PWM));
        CHECK(one(LINK_PAGE_PROG, fn, LINK_PG_FN_DSHOT150));
        CHECK(one(LINK_PAGE_PROG, fn, LINK_PG_FN_DSHOT600));
        CHECK(!one(LINK_PAGE_PROG, fn, LINK_PG_FN_COUNT));
    }
    /* The rbmod carrier still takes no DShot on a port. */
    const uint16_t d = LINK_PG_FN_DSHOT300;
    CHECK(!board_fix_write_ok(outbind_roles(OUTBIND_BOARD_RBMOD),
                              LINK_PAGE_PROG, LINK_PG_FUNC(0u), 1u, &d));
}

/* --------------------------------------------------------- the monitors */

/* The map as the node solders it. */
static void node_map(uint16_t *m)
{
    memset(m, 0, sizeof(uint16_t) * LINK_SM_KEPT);
    m[LINK_SM_ENABLE] = 0x0007u;
    m[LINK_SM_FAST]   = LINK_SM_FAST_NONE;
    for (unsigned c = 0; c < 9u; ++c) {
        m[LINK_SM_WATCH(c)] = (uint16_t)LINK_SM_WATCH_OUT(c);
        m[LINK_SM_SHUNT(c)] = 200u;
    }
}

TEST_CASE(the_map_is_fixed_on_three_parts_nine_watches_and_shunts)
{
    const outbind_roles_t *r = roles();
    CHECK(r->ina.on);
    CHECK_EQ(r->ina.part, 0u);
    CHECK_EQ(r->ina.parts, 3u);
    CHECK_EQ(r->ina.shunt_dmohm, 200u);
    CHECK_EQ(r->ina.crit_ma, 4000u);
    /* S9 is output 9: WATCH 0x40 (protocol 4.41). */
    CHECK_EQ(r->ina.watch[8], 0x40u);
    CHECK_EQ(r->ina.watch[0], 0x01u);
    CHECK_EQ(r->ina.feeds[8], BIT(32));
    uint16_t m[LINK_SM_KEPT];
    node_map(m);
    CHECK(board_fix_map_ok(r, m));
    /* A fourth part and a FAST channel are the page's. */
    m[LINK_SM_ENABLE] = 0x000Fu;
    m[LINK_SM_FAST] = 4u;
    CHECK(board_fix_map_ok(r, m));
    /* Each part missing. */
    for (unsigned a = 0; a < 3u; ++a) {
        node_map(m);
        m[LINK_SM_ENABLE] = (uint16_t)(0x0007u & ~(1u << a));
        CHECK(!board_fix_map_ok(r, m));
    }
    /* Each watch and each shunt changed. */
    for (unsigned c = 0; c < 9u; ++c) {
        node_map(m);
        m[LINK_SM_WATCH(c)] = LINK_SM_WATCH_NONE;
        CHECK(!board_fix_map_ok(r, m));
        node_map(m);
        m[LINK_SM_SHUNT(c)] = 199u;
        CHECK(!board_fix_map_ok(r, m));
        m[LINK_SM_SHUNT(c)] = 201u;
        CHECK(!board_fix_map_ok(r, m));
    }
}

TEST_CASE(a_kept_record_starts_on_the_nodes_pins)
{
    const outbind_roles_t *r = roles();
    out_store_t cfg;
    out_store_cfg_defaults(&cfg);
    slot(cfg.slots, 0u, LINK_DRIVER_PWM, 16u);       /* S1: stays */
    slot(cfg.slots, 1u, LINK_DRIVER_PWM, 17u);       /* IN1: goes */
    slot(cfg.slots_hi, 0u, LINK_DRIVER_DSHOT, 32u);  /* S9: stays */
    slot(cfg.slots_hi, 1u, LINK_DRIVER_DSHOT, 2u);   /* RAIL_EN: goes */
    cfg.supply[LINK_SP_TX_PIN] = 7u;
    cfg.sense[LINK_SN_ENABLE]  = LINK_SN_EN_ALL;
    cfg.tone[LINK_TN_ENABLE]   = 1u;
    cfg.prog[0] = 13u;
    cfg.prog_func = 0u;
    board_fix_store(r, &cfg);
    CHECK_EQ(cfg.slots[LINK_OS_PIN], 16u);
    CHECK_EQ(cfg.slots[LINK_OS_STRIDE + LINK_OS_DRIVER], (uint16_t)LINK_DRIVER_NONE);
    CHECK_EQ(cfg.slots_hi[LINK_OS_PIN], 32u);
    CHECK_EQ(cfg.slots_hi[LINK_OS_STRIDE + LINK_OS_DRIVER],
             (uint16_t)LINK_DRIVER_NONE);
    CHECK_EQ(cfg.supply[LINK_SP_TX_PIN], 34u);
    CHECK_EQ(cfg.supply[LINK_SP_RX_PIN], 35u);
    CHECK_EQ(cfg.sense[LINK_SN_SDA_PIN], 4u);
    CHECK_EQ(cfg.sense[LINK_SN_SCL_PIN], 5u);
    CHECK_EQ(cfg.sense[LINK_SN_ENABLE] & LINK_SN_EN_AS5600, 0u);
    CHECK_EQ(cfg.sense[LINK_SN_ENABLE] & LINK_SN_EN_I228, LINK_SN_EN_I228);
    CHECK_EQ(cfg.tone[LINK_TN_ENABLE], 0u);
    for (unsigned port = 0; port < LINK_PG_PORTS_MAX; ++port) {
        CHECK_EQ(cfg.prog[(size_t)port * PROG_PORT_KEPT],
                 (uint16_t)LINK_PG_PIN_NONE);
    }
    CHECK_EQ(cfg.prog_func, BOARD_FIX_PROG_FUNC_NONE);
    CHECK_EQ(cfg.sense_map_set, 1u);
    CHECK(board_fix_map_ok(r, cfg.sense_map));
    for (unsigned c = 0; c < 9u; ++c) {
        CHECK_EQ(cfg.sense_map[LINK_SM_SHUNT(c)], 200u);
        /* No limit kept: each channel starts at 3000mA. */
        CHECK_EQ(cfg.sense_limit[c], 3000u);
    }
    CHECK_EQ(cfg.sense_limit[9], 0u);
    /* A record with a limit on one channel keeps every limit as it is. */
    out_store_cfg_defaults(&cfg);
    cfg.sense_limit[4] = 2500u;
    board_fix_store(r, &cfg);
    CHECK_EQ(cfg.sense_limit[4], 2500u);
    CHECK_EQ(cfg.sense_limit[0], 0u);
}

TEST_CASE(each_channels_critical_limit_is_4000ma_or_a_lower_limit)
{
    const outbind_roles_t *r = roles();
    uint16_t lim[LINK_SM_CHANNELS] = { 0 };
    uint16_t crit[LINK_SM_CHANNELS];
    board_fix_crit(r, lim, crit);
    for (unsigned c = 0; c < LINK_SM_CHANNELS; ++c) {
        CHECK_EQ(crit[c], (c < 9u) ? 4000u : 0u);
    }
    lim[0] = 3000u;          /* SENSE_LIMIT's default on board 3: lower */
    lim[1] = 3999u;
    lim[2] = 4000u;
    lim[3] = 4001u;          /* higher: the critical limit stays */
    lim[9] = 1000u;          /* a channel no part of the board holds */
    board_fix_crit(r, lim, crit);
    CHECK_EQ(crit[0], 3000u);
    CHECK_EQ(crit[1], 3999u);
    CHECK_EQ(crit[2], 4000u);
    CHECK_EQ(crit[3], 4000u);
    CHECK_EQ(crit[9], 0u);
    board_fix_crit(r, NULL, crit);
    CHECK_EQ(crit[0], 4000u);
    /* None on the other boards. */
    board_fix_crit(outbind_roles(OUTBIND_BOARD_RBMOD), lim, crit);
    CHECK_EQ(crit[0], 0u);
    board_fix_crit(NULL, lim, crit);
    CHECK_EQ(crit[0], 0u);
    board_fix_crit(r, lim, NULL);            /* nothing for NULL */
}

/* Board 3's supply ceiling on every way to the PD mini: the SUPPLY page
 * and a programming port's ON (ESC_WIRE, KST, Hitec), at 8400mV and
 * 8410mV; supply_page_init() keeps it. */
TEST_CASE(the_supply_ceiling_holds_for_the_page_and_the_ports)
{
    const outbind_roles_t *r = outbind_roles((uint16_t)OUTBIND_BOARD_RSN);
    if (r == NULL) { T_FAIL("no board 3"); return; }
    CHECK_EQ(r->supply_max_mv, (uint16_t)RSN_SUPPLY_MAX_MV);
    supply_page_t sp;
    memset(&sp, 0, sizeof(sp));
    supply_page_ceiling(&sp, r->supply_max_mv);
    supply_page_init(&sp);
    sp.regs[LINK_SP_ENABLE] = 1u;
    CHECK(!supply_page_port_set(&sp, true, 8410u, 1000u, true));
    CHECK_EQ(sp.regs[LINK_SP_OUTPUT], 0u);
    CHECK(supply_page_port_set(&sp, true, 8400u, 1000u, true));
    CHECK_EQ(sp.regs[LINK_SP_SET_MV], 8400u);
    supply_port_view_t v;
    supply_page_port_view(&sp, &v);
    CHECK_EQ(v.max_mv, 8400u);
    CHECK(supply_page_port_set(&sp, false, 0u, 0u, true));
    /* The page itself, without the board's write check. */
    outputs_t o;
    outputs_init(&o, 0u);
    const uint16_t hi[3] = { 1u, 8410u, 1000u };
    CHECK_EQ(supply_page_write(&sp, LINK_SP_OUTPUT, 3u, hi, &o, true),
             (uint8_t)LINK_NACK_BAD_VALUE);
    CHECK_EQ(sp.regs[LINK_SP_SET_MV], 8400u);
    /* Another board: the module's own range. */
    supply_page_ceiling(&sp, 0u);
    CHECK(supply_page_port_set(&sp, true, 12000u, 1000u, true));
    supply_page_port_view(&sp, &v);
    CHECK_EQ(v.max_mv, (uint16_t)PDMINI_V_MAX_MV);
}

int main(void)
{
    RUN(board_3_is_the_node_with_its_role_pins);
    RUN(the_catalogue_is_the_role_pins_on_their_pads);
    RUN(each_output_has_its_own_slice_and_every_pio_pin_one_window);
    RUN(a_driven_slot_sits_on_one_of_the_nine_outputs);
    RUN(each_interface_pin_is_held_and_the_set_point_capped);
    RUN(a_programming_port_takes_any_output_and_no_input);
    RUN(the_map_is_fixed_on_three_parts_nine_watches_and_shunts);
    RUN(a_kept_record_starts_on_the_nodes_pins);
    RUN(each_channels_critical_limit_is_4000ma_or_a_lower_limit);
    RUN(the_supply_ceiling_holds_for_the_page_and_the_ports);
    return test_summary("board_fix_rsn");
}
