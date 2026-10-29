/*
 * A board with soldered pins: the rbmod carrier (shared/outputs/
 * out_board_rbmod.c, board_fix.c) and the IOmcu image a panel offers it
 * (shared/fwupdate fw_coproc_image()).
 *
 * Under test: the carrier's catalogue against its role table (every role
 * pin free, every other pin reserved and named, PROGP03 on GP15 and GP26
 * unused); the reserved mask the IOmcu builds from it; the OUTPUTS page
 * judged whole (a driven slot on each servo output taken, on every other
 * pin refused, an undriven slot not judged); each pin register of SUPPLY,
 * SENSE, TONE and PROG held to its role's pin with the pins either side
 * refused, PROG's FUNC held to NONE; registers that are no pin left to
 * their page; a kept record of the module put on the carrier's pins with
 * its enables and ranges kept; the programming ports restored on the
 * carrier with a servo output beside PROGP04 in the silicon; a board whose
 * pins are chosen untouched; the image chosen for each board, none for an
 * unknown one or one whose image is not carried.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "board_fix.h"
#include "fw_install.h"
#include "link_pages.h"
#include "out_bind.h"
#include "out_store_rec.h"
#include "outputs.h"
#include "prog_port.h"

#define RBMOD ((uint16_t)OUTBIND_BOARD_RBMOD)
#define BIT(pin) ((uint64_t)1u << (pin))

static const outbind_roles_t *roles(void) { return outbind_roles(RBMOD); }

/* ------------------------------------------------------------ the board */

TEST_CASE(the_carrier_is_a_board_with_its_role_pins)
{
    const outbind_board_t *bd = outbind_board(RBMOD);
    const outbind_roles_t *r = roles();
    if (bd == NULL || r == NULL) { T_FAIL("no rbmod board"); return; }
    CHECK(bd->fixed);
    CHECK(bd->roles == r);
    /* The module wired by hand has no roles. */
    CHECK(outbind_roles(OUTBIND_BOARD_PICO_HEADER) == NULL);
    CHECK(outbind_roles(OUTBIND_BOARD_UNKNOWN) == NULL);
    CHECK(outbind_roles(3u) == NULL);

    /* The owner's map, PROGP03 on GP15 on every carrier. */
    static const uint8_t k_out[4]  = { 2u, 4u, 5u, 1u };
    static const uint8_t k_prog[4] = { 13u, 14u, 15u, 0u };
    CHECK_EQ(memcmp(r->out, k_out, sizeof(k_out)), 0);
    CHECK_EQ(memcmp(r->prog, k_prog, sizeof(k_prog)), 0);
    CHECK_EQ(r->sda, 16u);
    CHECK_EQ(r->scl, 17u);
    CHECK_EQ(r->sda2, 18u);
    CHECK_EQ(r->scl2, 19u);
    CHECK_EQ(r->pd_tx, 7u);
    CHECK_EQ(r->pd_rx, 6u);
    CHECK_EQ(r->tap, 22u);
    CHECK_EQ(r->prog_5v_en, 21u);
    CHECK_EQ(r->heartbeat, 3u);
}

/* Bit n set: GPIO n is a pin the carrier gives to an output or an interface
 * a page can name. */
static uint64_t role_pins(const outbind_roles_t *r)
{
    uint64_t m = BIT(r->sda) | BIT(r->scl) | BIT(r->pd_tx) | BIT(r->pd_rx)
                 | BIT(r->tap);
    for (unsigned i = 0; i < OUTBIND_FIXED_OUTS; ++i) { m |= BIT(r->out[i]); }
    for (unsigned i = 0; i < OUTBIND_FIXED_PROGS; ++i) { m |= BIT(r->prog[i]); }
    return m;
}

TEST_CASE(every_role_pin_is_free_and_every_other_pin_reserved)
{
    const outbind_board_t *bd = outbind_board(RBMOD);
    const outbind_roles_t *r = roles();
    if (bd == NULL || r == NULL) { T_FAIL("no rbmod board"); return; }
    const uint64_t roles_m = role_pins(r);
    /* 4 outputs, 4 ports, 2 bus, 2 PD mini, 1 tap: 13 pins, none twice. */
    CHECK_EQ(__builtin_popcountll(roles_m), 13);
    for (uint8_t i = 0; i < bd->count; ++i) {
        const outbind_pin_t *p = &bd->pins[i];
        const bool role = (roles_m & BIT(p->gpio)) != 0u;
        if (role == p->reserved) {
            T_FAIL("GP%u: role %d, reserved %d", p->gpio, role, p->reserved);
        }
    }
    /* The pins the mask hands out are the role pins, no more and no fewer:
     * the heartbeat, CAN, bus 2, the 5V enable, GP20 and GP26 to GP28 are
     * reserved, and so is every pin the module does not bring out. */
    const uint64_t mask = outbind_reserved_mask(RBMOD);
    CHECK_EQ(~mask, roles_m);
    CHECK((mask & BIT(r->prog_5v_en)) != 0u);
    CHECK((mask & BIT(26)) != 0u);
    CHECK((mask & BIT(15)) == 0u);
    CHECK((mask & (BIT(18) | BIT(19) | BIT(20))) == (BIT(18) | BIT(19) | BIT(20)));
    /* No pin is chosen on it: a protocol is given to a servo output, and
     * every other pin takes none. */
    for (uint8_t i = 0; i < bd->count; ++i) {
        const bool out = board_fix_out_index(r, bd->pins[i].gpio) >= 0;
        if (outbind_pin_selectable(bd, i) != out) {
            T_FAIL("GP%u selectable %d", bd->pins[i].gpio, !out);
        }
    }
}

TEST_CASE(an_output_index_is_the_carriers_numbering)
{
    const outbind_roles_t *r = roles();
    CHECK_EQ(board_fix_out_index(r, 2u), 0);   /* PWM01 */
    CHECK_EQ(board_fix_out_index(r, 4u), 1);
    CHECK_EQ(board_fix_out_index(r, 5u), 2);
    CHECK_EQ(board_fix_out_index(r, 1u), 3);   /* PWM04 */
    CHECK_EQ(board_fix_out_index(r, 0u), -1);  /* PROGP04 */
    CHECK_EQ(board_fix_out_index(r, 3u), -1);
    CHECK_EQ(board_fix_out_index(r, 6u), -1);
    CHECK_EQ(board_fix_out_index(NULL, 2u), -1);
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

TEST_CASE(a_driven_slot_sits_on_a_servo_output)
{
    const outbind_roles_t *r = roles();
    uint16_t slots[LINK_OS_COUNT];
    memset(slots, 0, sizeof(slots));
    /* Nothing driven: taken, whatever the pins say. */
    slot(slots, 5u, LINK_DRIVER_NONE, 13u);
    CHECK(board_fix_slots_ok(r, slots));
    /* Each servo output, under each driver. */
    slot(slots, 0u, LINK_DRIVER_PWM, 2u);
    slot(slots, 1u, LINK_DRIVER_DSHOT, 4u);
    slot(slots, 2u, LINK_DRIVER_DSHOT_BIDIR, 5u);
    slot(slots, 3u, LINK_DRIVER_PPM, 1u);
    CHECK(board_fix_slots_ok(r, slots));
    /* And refused on the pins either side of one, and on the programming
     * ports' and the interfaces' pins. */
    static const uint16_t k_off[] = { 0u, 3u, 6u, 7u, 13u, 15u, 16u, 21u, 22u };
    for (size_t i = 0; i < sizeof(k_off) / sizeof(k_off[0]); ++i) {
        slot(slots, 7u, LINK_DRIVER_PWM, k_off[i]);
        if (board_fix_slots_ok(r, slots)) {
            T_FAIL("a PWM slot on GP%u is taken", k_off[i]);
        }
    }
    slot(slots, 7u, LINK_DRIVER_NONE, 0u);
    CHECK(board_fix_slots_ok(r, slots));
    /* No roles: every page is the page's own business. */
    slot(slots, 7u, LINK_DRIVER_PWM, 27u);
    CHECK(board_fix_slots_ok(NULL, slots));
    CHECK(!board_fix_slots_ok(r, NULL));
    CHECK(board_fix_slots_ok(NULL, NULL));
}

/* One register written to @p page: whether the carrier takes @p v. */
static bool one(uint8_t page, unsigned reg, uint16_t v)
{
    return board_fix_write_ok(roles(), page, (uint8_t)reg, 1u, &v);
}

TEST_CASE(each_interface_pin_is_held_to_its_role)
{
    /* The role's pin taken, the pins either side refused. */
    static const struct {
        uint8_t  page;
        unsigned reg;
        uint16_t pin;
    } k[] = {
        { LINK_PAGE_SUPPLY, LINK_SP_TX_PIN,  7u },
        { LINK_PAGE_SUPPLY, LINK_SP_RX_PIN,  6u },
        { LINK_PAGE_SENSE,  LINK_SN_SDA_PIN, 16u },
        { LINK_PAGE_SENSE,  LINK_SN_SCL_PIN, 17u },
        { LINK_PAGE_TONE,   LINK_TN_PIN,     22u },
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        if (!one(k[i].page, k[i].reg, k[i].pin)) {
            T_FAIL("page 0x%02X reg %u: GP%u refused", k[i].page, k[i].reg,
                   k[i].pin);
        }
        if (one(k[i].page, k[i].reg, (uint16_t)(k[i].pin - 1u))
            || one(k[i].page, k[i].reg, (uint16_t)(k[i].pin + 1u))
            || one(k[i].page, k[i].reg, 0xFFFFu)) {
            T_FAIL("page 0x%02X reg %u: a pin beside GP%u taken",
                   k[i].page, k[i].reg, k[i].pin);
        }
    }
    /* The second bus's pins are no sensor bus. */
    CHECK(!one(LINK_PAGE_SENSE, LINK_SN_SDA_PIN, 18u));
    /* A frame with the enable and both pins: judged on the pins. */
    uint16_t sp[4] = { 1u, 7u, 6u, 1u };
    CHECK(board_fix_write_ok(roles(), LINK_PAGE_SUPPLY, LINK_SP_ENABLE, 4u,
                             sp));
    sp[1] = 6u; sp[2] = 7u;    /* swapped */
    CHECK(!board_fix_write_ok(roles(), LINK_PAGE_SUPPLY, LINK_SP_ENABLE, 4u,
                              sp));
    /* A register that is no pin is the page's own. */
    CHECK(one(LINK_PAGE_SUPPLY, LINK_SP_BAUD, 99u));
    CHECK(one(LINK_PAGE_SENSE, LINK_SN_I228_ADDR, 0u));
    CHECK(one(LINK_PAGE_TONE, LINK_TN_F_MIN_HZ, 1u));
    CHECK(one(LINK_PAGE_OUTPUTS, LINK_OS_PIN, 40u));
    CHECK(one(LINK_PAGE_CONTROL, 0u, 1u));
}

TEST_CASE(each_programming_port_keeps_its_pin_and_no_function)
{
    const outbind_roles_t *r = roles();
    for (unsigned port = 0; port < LINK_PG_PORTS_MAX; ++port) {
        const unsigned pin = LINK_PG_REG(port, LINK_PG_PIN);
        const uint16_t own = r->prog[port];
        if (!one(LINK_PAGE_PROG, pin, own)) {
            T_FAIL("port %u refused its own GP%u", port, own);
        }
        /* No pin is no choice either, and another port's pin is refused. */
        if (one(LINK_PAGE_PROG, pin, LINK_PG_PIN_NONE)
            || one(LINK_PAGE_PROG, pin, r->prog[(port + 1u) % 4u])
            || one(LINK_PAGE_PROG, pin, (uint16_t)(own + 1u))) {
            T_FAIL("port %u took a pin not its own", port);
        }
        /* FUNC: NONE only; the signal protocols are the servo outputs'. */
        CHECK(one(LINK_PAGE_PROG, LINK_PG_FUNC(port), LINK_PG_FN_NONE));
        CHECK(!one(LINK_PAGE_PROG, LINK_PG_FUNC(port), LINK_PG_FN_SERVO_PWM));
        CHECK(!one(LINK_PAGE_PROG, LINK_PG_FUNC(port), LINK_PG_FN_DSHOT150));
        CHECK(!one(LINK_PAGE_PROG, LINK_PG_FUNC(port), LINK_PG_FN_DSHOT600));
        /* The other registers of a port are the page's. */
        CHECK(one(LINK_PAGE_PROG, LINK_PG_REG(port, LINK_PG_CMD), 999u));
        CHECK(one(LINK_PAGE_PROG, LINK_PG_REG(port, LINK_PG_SLEW), 7u));
        CHECK(one(LINK_PAGE_PROG, LINK_PG_REG(port, LINK_PG_MAX_US), 2100u));
    }
    CHECK(one(LINK_PAGE_PROG, LINK_PG_PWM, 0xFu));
    CHECK(one(LINK_PAGE_PROG, LINK_PG_TARGET, 1u));
    /* A frame from port 0's PIN over all four ports and their FUNCs. */
    uint16_t all[LINK_PG_REGS - LINK_PG_PORT0];
    memset(all, 0, sizeof(all));
    for (unsigned port = 0; port < LINK_PG_PORTS_MAX; ++port) {
        all[LINK_PG_REG(port, LINK_PG_PIN) - LINK_PG_PORT0] = r->prog[port];
        all[LINK_PG_FUNC(port) - LINK_PG_PORT0] = LINK_PG_FN_NONE;
    }
    CHECK(board_fix_write_ok(r, LINK_PAGE_PROG, LINK_PG_PORT0,
                             (uint8_t)(LINK_PG_REGS - LINK_PG_PORT0), all));
    /* The last FUNC, port 3's, is judged like the first. */
    all[LINK_PG_FUNC(3u) - LINK_PG_PORT0] = LINK_PG_FN_SERVO_PWM;
    CHECK(!board_fix_write_ok(r, LINK_PAGE_PROG, LINK_PG_PORT0,
                              (uint8_t)(LINK_PG_REGS - LINK_PG_PORT0), all));
}

TEST_CASE(a_board_whose_pins_are_chosen_is_not_judged)
{
    const uint16_t v = 40u;
    CHECK(board_fix_write_ok(NULL, LINK_PAGE_TONE, LINK_TN_PIN, 1u, &v));
    CHECK(board_fix_write_ok(NULL, LINK_PAGE_PROG, LINK_PG_FUNC(0u), 1u, &v));
    CHECK(board_fix_write_ok(roles(), LINK_PAGE_TONE, LINK_TN_PIN, 0u, NULL));
    CHECK(!board_fix_write_ok(roles(), LINK_PAGE_TONE, LINK_TN_PIN, 1u, NULL));
}

/* ------------------------------------------------------- the kept record */

TEST_CASE(a_record_of_the_module_starts_on_the_carriers_pins)
{
    const outbind_roles_t *r = roles();
    out_store_t cfg;
    out_store_cfg_defaults(&cfg);
    slot(cfg.slots, 0u, LINK_DRIVER_PWM, 2u);      /* PWM01: stays */
    slot(cfg.slots, 1u, LINK_DRIVER_PWM, 13u);     /* PROGP01: goes */
    slot(cfg.slots, 2u, LINK_DRIVER_DSHOT, 26u);   /* unused: goes */
    slot(cfg.slots, 3u, LINK_DRIVER_DSHOT, 1u);    /* PWM04: stays */
    cfg.supply[LINK_SP_ENABLE] = 1u;
    cfg.supply[LINK_SP_TX_PIN] = 0u;
    cfg.supply[LINK_SP_RX_PIN] = 1u;
    cfg.supply[LINK_SP_BAUD]   = 4u;
    cfg.sense[LINK_SN_ENABLE]  = LINK_SN_EN_I3221;
    cfg.sense[LINK_SN_SDA_PIN] = 4u;
    cfg.sense[LINK_SN_SCL_PIN] = 5u;
    cfg.tone[LINK_TN_ENABLE]   = LINK_TN_EN_TAP;
    cfg.tone[LINK_TN_PIN]      = 13u;
    cfg.prog[1u * PROG_PORT_KEPT + 1u] = 9u;       /* port 1's slew */
    cfg.prog_func = 0x2131u;                       /* port 1 DShot... */

    board_fix_store(r, &cfg);

    CHECK_EQ(cfg.slots[0u * LINK_OS_STRIDE + LINK_OS_PIN], 2u);
    CHECK_EQ(cfg.slots[0u * LINK_OS_STRIDE + LINK_OS_DRIVER], LINK_DRIVER_PWM);
    for (unsigned k = 0; k < LINK_OS_STRIDE; ++k) {
        CHECK_EQ(cfg.slots[1u * LINK_OS_STRIDE + k], 0u);
        CHECK_EQ(cfg.slots[2u * LINK_OS_STRIDE + k], 0u);
    }
    CHECK_EQ(cfg.slots[3u * LINK_OS_STRIDE + LINK_OS_PIN], 1u);
    CHECK_EQ(cfg.slots[3u * LINK_OS_STRIDE + LINK_OS_DRIVER],
             LINK_DRIVER_DSHOT);
    CHECK(board_fix_slots_ok(r, cfg.slots));
    /* Pins moved, enables and settings kept. */
    CHECK_EQ(cfg.supply[LINK_SP_ENABLE], 1u);
    CHECK_EQ(cfg.supply[LINK_SP_TX_PIN], 7u);
    CHECK_EQ(cfg.supply[LINK_SP_RX_PIN], 6u);
    CHECK_EQ(cfg.supply[LINK_SP_BAUD], 4u);
    CHECK_EQ(cfg.sense[LINK_SN_ENABLE], LINK_SN_EN_I3221);
    CHECK_EQ(cfg.sense[LINK_SN_SDA_PIN], 16u);
    CHECK_EQ(cfg.sense[LINK_SN_SCL_PIN], 17u);
    CHECK_EQ(cfg.tone[LINK_TN_ENABLE], LINK_TN_EN_TAP);
    CHECK_EQ(cfg.tone[LINK_TN_PIN], 22u);
    CHECK_EQ(cfg.tone[LINK_TN_F_MIN_HZ], LINK_TN_DEFAULT_F_MIN);
    for (unsigned port = 0; port < 4u; ++port) {
        CHECK_EQ(cfg.prog[port * PROG_PORT_KEPT], r->prog[port]);
    }
    CHECK_EQ(cfg.prog[1u * PROG_PORT_KEPT + 1u], 9u);
    CHECK_EQ(cfg.prog_func, BOARD_FIX_PROG_FUNC_NONE);
    CHECK_EQ(cfg.prog_func, 0x2222u);
    /* Every register a write could carry now passes the gate. */
    CHECK(board_fix_write_ok(r, LINK_PAGE_SUPPLY, LINK_SP_ENABLE,
                             (uint8_t)LINK_SP_OUTPUT, cfg.supply));
    CHECK(board_fix_write_ok(r, LINK_PAGE_SENSE, LINK_SN_ENABLE,
                             (uint8_t)LINK_SN_CONFIG_COUNT, cfg.sense));
    CHECK(board_fix_write_ok(r, LINK_PAGE_TONE, LINK_TN_ENABLE,
                             (uint8_t)LINK_TN_CONFIG_COUNT, cfg.tone));

    /* A board whose pins are chosen keeps its record as it is. */
    out_store_t mod, before;
    out_store_cfg_defaults(&mod);
    slot(mod.slots, 0u, LINK_DRIVER_PWM, 26u);
    memcpy(&before, &mod, sizeof(mod));
    board_fix_store(NULL, &mod);
    CHECK_EQ(memcmp(&mod, &before, sizeof(mod)), 0);
    board_fix_store(r, NULL);
}

TEST_CASE(the_defaults_are_what_an_old_record_starts_from)
{
    /* out_store_cfg_defaults() is the fill out_store_cfg_read() lays an old
     * record over: a version 3 record (slots and chan_cfg only) reads back
     * as those defaults past its end. */
    out_store_t d, rd;
    out_store_cfg_defaults(&d);
    uint8_t v3[sizeof(out_store_t)];
    memset(v3, 0, sizeof(v3));
    CHECK(out_store_cfg_read(OUT_STORE_VERSION_V3, v3, &rd));
    CHECK_EQ(memcmp(&d, &rd, sizeof(d)), 0);
    CHECK_EQ(d.prog[0], PROG_PORT_DEFAULT_PIN);
    CHECK_EQ(d.prog[PROG_PORT_KEPT], LINK_PG_PIN_NONE);
    CHECK_EQ(d.tone[LINK_TN_PIN], LINK_TN_DEFAULT_PIN);
    out_store_cfg_defaults(NULL);
}

TEST_CASE(the_ports_hold_their_pins_with_a_servo_output_beside_progp04)
{
    /*
     * PWM04 (GP1) and PROGP04 (GP0) are one PWM slice.  On the carrier the
     * ports are NONE, under no PWM rule, so a restore with PWM04 driven
     * gives every port its pin.
     */
    const outbind_roles_t *r = roles();
    outputs_t o;
    prog_port_t pg;
    outputs_init(&o, 0u);
    outputs_reserve_pins(&o, outbind_reserved_mask(RBMOD));
    for (unsigned i = 0; i < OUTBIND_FIXED_OUTS; ++i) {
        const out_slot_t pwm = { .driver = OUT_DRIVER_PWM,
                                 .first_channel = (uint8_t)i, .channels = 1,
                                 .pin = r->out[i], .rate_hz = 50 };
        CHECK(outputs_configure(&o, (uint8_t)i, &pwm));
    }
    out_store_t cfg;
    out_store_cfg_defaults(&cfg);
    board_fix_store(r, &cfg);
    prog_port_init(&pg, LINK_PG_PORTS_MAX);
    prog_port_restore_fn(&pg, cfg.prog, cfg.prog_func, &o,
                         BIT(r->pd_tx) | BIT(r->pd_rx) | BIT(r->sda)
                             | BIT(r->scl) | BIT(r->tap));
    for (unsigned port = 0; port < 4u; ++port) {
        CHECK_EQ(prog_port_pin(&pg, (uint8_t)port), r->prog[port]);
        CHECK_EQ(prog_port_func(&pg, (uint8_t)port), LINK_PG_FN_NONE);
    }
}

/* ------------------------------------------------------------- the images */

TEST_CASE(each_board_is_offered_its_own_image)
{
    static const uint8_t a[4] = { 1, 2, 3, 4 };
    static const uint8_t b[2] = { 5, 6 };
    fw_coproc_image_t tab[2] = {
        { OUTBIND_BOARD_PICO_HEADER, a, sizeof(a) },
        { OUTBIND_BOARD_RBMOD,       b, sizeof(b) },
    };
    CHECK(fw_coproc_image(tab, 2u, OUTBIND_BOARD_PICO_HEADER) == &tab[0]);
    CHECK(fw_coproc_image(tab, 2u, OUTBIND_BOARD_RBMOD) == &tab[1]);
    /* No image for an unknown board, a board past the table, or a board
     * whose image this build does not carry. */
    CHECK(fw_coproc_image(tab, 2u, OUTBIND_BOARD_UNKNOWN) == NULL);
    CHECK(fw_coproc_image(tab, 2u, 3u) == NULL);
    CHECK(fw_coproc_image(tab, 1u, OUTBIND_BOARD_RBMOD) == NULL);
    tab[1].len = 0u;
    CHECK(fw_coproc_image(tab, 2u, OUTBIND_BOARD_RBMOD) == NULL);
    tab[1].len = sizeof(b);
    tab[1].image = NULL;
    CHECK(fw_coproc_image(tab, 2u, OUTBIND_BOARD_RBMOD) == NULL);
    CHECK(fw_coproc_image(NULL, 2u, OUTBIND_BOARD_PICO_HEADER) == NULL);
    /* The module's board is never handed the carrier's image. */
    tab[0].image = NULL;
    tab[1].image = b;
    CHECK(fw_coproc_image(tab, 2u, OUTBIND_BOARD_PICO_HEADER) == NULL);
}

/* ---------------------------------------------------------- the binding */

/* The protocol named @p name in the list. */
static uint8_t proto_named(const char *name)
{
    const outbind_proto_t *p = outbind_protos();
    for (uint8_t i = 0; i < OUTBIND_PROTOS; ++i) {
        if (strcmp(p[i].name, name) == 0) {
            return i;
        }
    }
    T_FAIL("no protocol %s", name);
    return 0u;
}

static uint8_t at(uint8_t gpio) { return outbind_index_of(RBMOD, gpio); }

TEST_CASE(the_grid_is_the_four_servo_outputs_in_their_order)
{
    CHECK_EQ(outbind_cell_count(RBMOD), 4u);
    CHECK_EQ(outbind_cell_index(RBMOD, 0u), at(2u));
    CHECK_EQ(outbind_cell_index(RBMOD, 1u), at(4u));
    CHECK_EQ(outbind_cell_index(RBMOD, 2u), at(5u));
    CHECK_EQ(outbind_cell_index(RBMOD, 3u), at(1u));
    CHECK_EQ(outbind_cell_index(RBMOD, 4u), outbind_pin_count(RBMOD));
    /* The module's grid is its catalogue. */
    const uint16_t mod = OUTBIND_BOARD_PICO_HEADER;
    CHECK_EQ(outbind_cell_count(mod), outbind_pin_count(mod));
    CHECK_EQ(outbind_cell_index(mod, 7u), 7u);
    CHECK_EQ(outbind_cell_index(mod, outbind_pin_count(mod)),
             outbind_pin_count(mod));
    CHECK_EQ(outbind_cell_count(OUTBIND_BOARD_UNKNOWN), 0u);
    CHECK_EQ(outbind_cell_index(OUTBIND_BOARD_UNKNOWN, 0u), 0u);
}

TEST_CASE(a_protocol_goes_to_a_servo_output_and_nowhere_else)
{
    outbind_t b;
    outbind_init(&b);
    outbind_set_board(&b, RBMOD);
    outbind_set_proto(&b, proto_named("SERVO PWM"));
    /* Every servo output takes it, in either direction. */
    CHECK(outbind_toggle(&b, at(2u)));
    CHECK(outbind_toggle(&b, at(1u)));
    CHECK(outbind_toggle(&b, at(1u)));
    CHECK(outbind_toggle(&b, at(1u)));
    /* A programming port's pin, an interface's, the heartbeat: none. */
    static const uint8_t k_no[] = { 0u, 3u, 6u, 13u, 15u, 16u, 22u, 26u };
    for (size_t i = 0; i < sizeof(k_no) / sizeof(k_no[0]); ++i) {
        if (outbind_can_add(&b, at(k_no[i])) || outbind_toggle(&b, at(k_no[i]))) {
            T_FAIL("GP%u took a protocol", k_no[i]);
        }
    }
    /* PWM04 (GP1) shares slice 0 with PROGP04 (GP0): no programming port
     * carries PWM on the carrier, so the port's pin clashes with nothing. */
    const uint8_t ports[4] = { 13u, 14u, 15u, 0u };
    CHECK_EQ(outbind_pwm_clash_ports(&b, at(4u), ports, 4u, NULL),
             OUTBIND_CLASH_NONE);

    /* The page: PWM01 first, then PWM04, whatever their GPIOs. */
    uint16_t regs[LINK_OS_COUNT];
    CHECK_EQ(outbind_to_slots(&b, regs), 2u);
    CHECK_EQ(regs[0u * LINK_OS_STRIDE + LINK_OS_PIN], 2u);
    CHECK_EQ(regs[1u * LINK_OS_STRIDE + LINK_OS_PIN], 1u);
    CHECK(board_fix_slots_ok(roles(), regs));
    /* And it reads back as the binding it came from. */
    outbind_t back;
    CHECK(outbind_from_slots(&back, RBMOD, regs, NULL));
    CHECK_EQ(back.pins[proto_named("SERVO PWM")], b.pins[proto_named("SERVO PWM")]);
    /* A page with a slot off the outputs is none this screen shows. */
    regs[1u * LINK_OS_STRIDE + LINK_OS_PIN] = 13u;
    CHECK(!outbind_from_slots(&back, RBMOD, regs, NULL));
    /* A bit on a pin that is no output does not survive a trim. */
    b.pins[proto_named("SERVO PWM")] |= (uint32_t)1u << at(13u);
    outbind_trim(&b);
    CHECK_EQ(b.pins[proto_named("SERVO PWM")] & ((uint32_t)1u << at(13u)), 0u);
}

TEST_CASE(a_surface_and_a_throttle_do_not_share_slice_2)
{
    outbind_t b;
    outbind_init(&b);
    outbind_set_board(&b, RBMOD);
    outbind_set_proto(&b, proto_named("SERVO PWM"));
    CHECK(outbind_toggle(&b, at(4u)));          /* PWM02 a surface */
    /* PWM03 (GP5) as a throttle: one slice, maybe two rates. */
    outbind_set_proto(&b, proto_named("MOTOR PWM"));
    uint8_t with = 0u;
    CHECK_EQ(outbind_pwm_clash_ports(&b, at(5u), NULL, 0u, &with),
             OUTBIND_CLASH_OUT_SLICE);
    CHECK_EQ(with, 4u);
    CHECK(!outbind_can_add(&b, at(5u)));
    CHECK(!outbind_toggle(&b, at(5u)));
    /* PWM01 and PWM04 are other slices; DShot is no PWM. */
    CHECK(outbind_toggle(&b, at(2u)));
    outbind_set_proto(&b, proto_named("DSHOT300"));
    CHECK_EQ(outbind_pwm_clash_ports(&b, at(5u), NULL, 0u, NULL),
             OUTBIND_CLASH_NONE);
    /* The same role on both is one rate: taken. */
    outbind_set_proto(&b, proto_named("SERVO PWM"));
    CHECK_EQ(outbind_pwm_clash_ports(&b, at(5u), NULL, 0u, NULL),
             OUTBIND_CLASH_NONE);
    CHECK(outbind_toggle(&b, at(5u)));
    /* On the module the rule is not the screen's (the IOmcu's rate check). */
    outbind_t m;
    outbind_init(&m);
    outbind_set_board(&m, OUTBIND_BOARD_PICO_HEADER);
    outbind_set_proto(&m, proto_named("SERVO PWM"));
    const uint16_t mod = OUTBIND_BOARD_PICO_HEADER;
    CHECK(outbind_toggle(&m, outbind_index_of(mod, 4u)));
    outbind_set_proto(&m, proto_named("MOTOR PWM"));
    CHECK_EQ(outbind_pwm_clash_ports(&m, outbind_index_of(mod, 5u), NULL, 0u,
                                     NULL), OUTBIND_CLASH_NONE);
}

int main(void)
{
    RUN(the_carrier_is_a_board_with_its_role_pins);
    RUN(every_role_pin_is_free_and_every_other_pin_reserved);
    RUN(an_output_index_is_the_carriers_numbering);
    RUN(a_driven_slot_sits_on_a_servo_output);
    RUN(each_interface_pin_is_held_to_its_role);
    RUN(each_programming_port_keeps_its_pin_and_no_function);
    RUN(a_board_whose_pins_are_chosen_is_not_judged);
    RUN(a_record_of_the_module_starts_on_the_carriers_pins);
    RUN(the_defaults_are_what_an_old_record_starts_from);
    RUN(the_ports_hold_their_pins_with_a_servo_output_beside_progp04);
    RUN(each_board_is_offered_its_own_image);
    RUN(the_grid_is_the_four_servo_outputs_in_their_order);
    RUN(a_protocol_goes_to_a_servo_output_and_nowhere_else);
    RUN(a_surface_and_a_throttle_do_not_share_slice_2);
    return test_summary("board_fix");
}
