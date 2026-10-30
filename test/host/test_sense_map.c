/*
 * The SENSE_MAP, SENSE_WIN_A and SENSE_WIN_B pages (sense_map.h, protocol
 * 4.25) beside the SENSE page they share their state with.
 *
 * Under test: the layout and the minor; the map derived from SENSE until it
 * is written, and every register range of a write; each value at its
 * limits, refused and taken; the INA228's address against a fitted part
 * from either page; a first write putting the map in force and SENSE's
 * INA3221 bit and frame mirroring it from then on, an older panel's SENSE
 * write that would change them refused; no change while the bank drives,
 * the derived map written as it reads taken all the same; the pins checked
 * when the map opens the bus; a set-up generation for ENABLE, FAST and the
 * shunts and none for WATCH alone; core 1's order from the map; the
 * snapshot into the map's read-only registers and both window pages, and
 * a set-up taken clearing them; a capture armed only with a FAST channel;
 * what is kept and its restore at boot; a carrier's first boot with its
 * INA3221 in force.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "board_fix.h"
#include "link_msg.h"
#include "link_pages.h"
#include "out_bind.h"
#include "out_store_rec.h"
#include "outputs.h"
#include "sense_map.h"
#include "sense_page.h"

static outputs_t    o;
static sense_page_t pg;

#define BIT(pin) ((uint64_t)1u << (pin))

/* A bank with a surface on channel 0, slot 0 bound; the page fresh. */
static void fresh(void)
{
    outputs_init(&o, 0u);
    outputs_reserve_pins(&o, BIT(3));
    const out_slot_t pwm = { .driver = OUT_DRIVER_PWM, .first_channel = 0,
                             .channels = 1, .pin = 4, .rate_hz = 50 };
    CHECK(outputs_configure(&o, 0, &pwm));
    CHECK(outputs_set_role(&o, 0, OUT_ROLE_SURFACE));
    sense_page_init(&pg);
    sense_page_bound(&pg, 0x01u);
}

static uint16_t mreg(unsigned r)
{
    uint16_t v = 0xFFFFu;
    sense_map_read(&pg, (uint8_t)r, 1u, &v);
    return v;
}

static uint16_t sreg(unsigned r)
{
    uint16_t v = 0xFFFFu;
    sense_page_read(&pg, (uint8_t)r, 1u, &v);
    return v;
}

static uint8_t mwrite(unsigned r, uint16_t v)
{
    return sense_map_write(&pg, (uint8_t)r, 1u, &v, &o, 0u);
}

/* The whole kept map: @p fit, FAST @p fast, every channel of a fitted
 * part on 1000 (0.1Ohm), nothing watched. */
static uint8_t map_all(uint16_t fit, uint16_t fast)
{
    uint16_t m[LINK_SM_KEPT];
    memset(m, 0, sizeof(m));
    m[LINK_SM_ENABLE] = fit;
    m[LINK_SM_FAST] = fast;
    for (unsigned c = 0; c < LINK_SM_CHANNELS; ++c) {
        if ((fit & (1u << (c / 3u))) != 0u) {
            m[LINK_SM_SHUNT(c)] = 1000u;
        }
    }
    return sense_map_write(&pg, 0u, (uint8_t)LINK_SM_KEPT, m, &o, 0u);
}

TEST_CASE(the_pages_are_where_protocol_4_25_puts_them)
{
    CHECK_EQ(LINK_MINOR_SENSE_MAP, 25u);
    CHECK(LINK_PROTOCOL_MINOR >= LINK_MINOR_SENSE_MAP);
    CHECK_EQ(LINK_PAGE_SENSE_MAP, 0x35);
    CHECK_EQ(LINK_PAGE_SENSE_WIN_A, 0x37);
    CHECK_EQ(LINK_PAGE_SENSE_WIN_B, 0x38);
    CHECK_EQ(LINK_SM_SHUNT(0u), 2u);
    CHECK_EQ(LINK_SM_SHUNT(11u), 13u);
    CHECK_EQ(LINK_SM_WATCH(0u), 14u);
    CHECK_EQ(LINK_SM_WATCH(11u), 25u);
    CHECK_EQ(LINK_SM_KEPT, 26);
    CHECK_EQ(LINK_SM_ID(3u), 29u);
    CHECK_EQ(LINK_SM_FLAGS, 30);
    CHECK_EQ(LINK_SM_COUNT, LINK_MAX_REGS);
    CHECK_EQ(LINK_SX_COUNT, LINK_MAX_REGS);
    CHECK_EQ(LINK_SX_E(5u, LINK_SX_E_FLAGS), 31u);
    CHECK_EQ(LINK_SM_WATCH_PORT(3u), 0x13u);
}

TEST_CASE(until_written_the_map_is_senses_one_part)
{
    fresh();
    /* SENSE as it starts: the INA3221 off at 0x40, CH1 on 1000. */
    CHECK_EQ(mreg(LINK_SM_ENABLE), 0u);
    CHECK_EQ(mreg(LINK_SM_FAST), LINK_SM_FAST_NONE);
    CHECK_EQ(mreg(LINK_SM_SHUNT(0u)), 1000u);
    CHECK_EQ(mreg(LINK_SM_SHUNT(1u)), 0u);
    CHECK_EQ(mreg(LINK_SM_FLAGS), LINK_SM_DERIVED);
    /* The part at 0x42 with CH1 and CH3, enabled. */
    const uint16_t f[4] = { 0x42u, 500u, 0x05u, 0u };
    CHECK_EQ(sense_page_write(&pg, LINK_SN_I3221_ADDR, 4u, f, &o, 0u), 0u);
    const uint16_t en = LINK_SN_EN_I3221;
    CHECK_EQ(sense_page_write(&pg, LINK_SN_ENABLE, 1u, &en, &o, 0u), 0u);
    CHECK_EQ(mreg(LINK_SM_ENABLE), 0x04u);
    CHECK_EQ(mreg(LINK_SM_FAST), 6u);
    for (unsigned c = 0; c < LINK_SM_CHANNELS; ++c) {
        CHECK_EQ(mreg(LINK_SM_SHUNT(c)), (c == 6u || c == 8u) ? 500u : 0u);
        CHECK_EQ(mreg(LINK_SM_WATCH(c)), 0u);
    }
    /* Core 1 is ordered SENSE's one part. */
    sense_cmd_t c;
    sense_page_cmd(&pg, &c);
    CHECK_EQ(c.parts.i3221.fitted, 0x04u);
    CHECK_EQ(c.parts.i3221.fast, 6u);
    CHECK_EQ(c.parts.i3221.shunt_uohm[8], 50000u);
    /* What is kept: the derived registers, not in force. */
    uint16_t kept[LINK_SM_KEPT];
    uint16_t set = 9u;
    sense_map_kept(&pg, kept, &set);
    CHECK_EQ(set, 0u);
    CHECK_EQ(kept[LINK_SM_ENABLE], 0x04u);
    CHECK(!pg.map_set);
}

TEST_CASE(a_write_lies_within_the_kept_registers)
{
    fresh();
    uint16_t z[LINK_SM_COUNT];
    memset(z, 0, sizeof(z));
    CHECK_EQ(sense_map_write(&pg, 31u, 2u, z, &o, 0u), LINK_NACK_BAD_RANGE);
    CHECK_EQ(sense_map_write(&pg, 0u, 33u, z, &o, 0u), LINK_NACK_BAD_RANGE);
    CHECK_EQ(sense_map_write(&pg, 20u, 7u, z, &o, 0u), LINK_NACK_READ_ONLY);
    CHECK_EQ(sense_map_write(&pg, LINK_SM_FLAGS, 1u, z, &o, 0u),
             LINK_NACK_READ_ONLY);
    CHECK_EQ(sense_map_write(&pg, 0u, 0u, z, &o, 0u), 0u);
    CHECK(!pg.map_set);
    CHECK_EQ(sense_map_write(NULL, 0u, 1u, z, &o, 0u), LINK_NACK_BAD_RANGE);
    CHECK_EQ(sense_map_write(&pg, 0u, 1u, NULL, &o, 0u), LINK_NACK_BAD_RANGE);
    /* The whole kept map in one frame, and a read across both kinds. */
    CHECK_EQ(map_all(0x03u, 0u), 0u);
    uint16_t r[LINK_SM_COUNT];
    sense_map_read(&pg, 0u, (uint8_t)LINK_SM_COUNT, r);
    CHECK_EQ(r[LINK_SM_ENABLE], 0x03u);
    CHECK_EQ(r[LINK_SM_SHUNT(5u)], 1000u);
    CHECK_EQ(r[LINK_SM_SHUNT(6u)], 0u);
    CHECK_EQ(r[LINK_SM_FLAGS], LINK_SM_IN_FORCE);
    CHECK_EQ(r[LINK_SM_RESERVED_31], 0u);
}

TEST_CASE(each_value_is_taken_at_its_limits_and_refused_past_them)
{
    fresh();
    CHECK_EQ(map_all(0x0Fu, 0u), 0u);
    CHECK_EQ(mwrite(LINK_SM_ENABLE, 16u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(mwrite(LINK_SM_FAST, 12u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(mwrite(LINK_SM_FAST, 11u), 0u);
    CHECK_EQ(mwrite(LINK_SM_FAST, LINK_SM_FAST_NONE), 0u);
    CHECK_EQ(mwrite(LINK_SM_FAST, 0x00FEu), LINK_NACK_BAD_VALUE);
    /* Shunts: 0, 50 and 10000 taken; 49 and 10001 refused. */
    CHECK_EQ(mwrite(LINK_SM_SHUNT(4u), 49u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(mwrite(LINK_SM_SHUNT(4u), 10001u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(mwrite(LINK_SM_SHUNT(4u), 50u), 0u);
    CHECK_EQ(mwrite(LINK_SM_SHUNT(4u), 10000u), 0u);
    CHECK_EQ(mwrite(LINK_SM_SHUNT(4u), 0u), 0u);
    /* WATCH: the list and nothing else. */
    static const uint16_t ok[] = { 0x00u, 0x01u, 0x08u, 0x10u, 0x13u, 0x20u,
                                   0x30u, 0x31u, 0x32u };
    static const uint16_t bad[] = { 0x09u, 0x0Fu, 0x14u, 0x1Fu, 0x21u,
                                    0x2Fu, 0x33u, 0xFFFFu };
    for (unsigned k = 0; k < sizeof(ok) / sizeof(ok[0]); ++k) {
        CHECK_EQ(mwrite(LINK_SM_WATCH(11u), ok[k]), 0u);
    }
    for (unsigned k = 0; k < sizeof(bad) / sizeof(bad[0]); ++k) {
        CHECK_EQ(mwrite(LINK_SM_WATCH(11u), bad[k]), LINK_NACK_BAD_VALUE);
    }
    /* FAST on a channel off, and on a part not fitted. */
    CHECK_EQ(mwrite(LINK_SM_SHUNT(7u), 0u), 0u);
    CHECK_EQ(mwrite(LINK_SM_FAST, 7u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(mwrite(LINK_SM_FAST, 9u), 0u);
    CHECK_EQ(mwrite(LINK_SM_ENABLE, 0x07u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(mwrite(LINK_SM_FAST, 0u), 0u);
    CHECK_EQ(mwrite(LINK_SM_ENABLE, 0x07u), 0u);
    CHECK_EQ(mwrite(LINK_SM_FAST, 9u), LINK_NACK_BAD_VALUE);
    /* A part not fitted keeps values judged by range alone. */
    CHECK_EQ(mwrite(LINK_SM_SHUNT(10u), 2000u), 0u);
    CHECK_EQ(mwrite(LINK_SM_WATCH(10u), LINK_SM_WATCH_PORT(2u)), 0u);
    /* A fitted part with no channel. */
    const uint16_t z[3] = { 0u, 0u, 0u };
    CHECK_EQ(sense_map_write(&pg, (uint8_t)LINK_SM_SHUNT(3u), 3u, z, &o, 0u),
             LINK_NACK_BAD_VALUE);
    CHECK_EQ(mreg(LINK_SM_SHUNT(3u)), 1000u);    /* nothing of it taken */
    /* A refused write changes nothing. */
    CHECK_EQ(mreg(LINK_SM_ENABLE), 0x07u);
    CHECK_EQ(mreg(LINK_SM_FAST), 0u);
}

TEST_CASE(the_ina228_and_a_fitted_part_never_share_an_address)
{
    fresh();
    /* The INA228 enabled at 0x41. */
    const uint16_t a228[4] = { 0x41u, 200u, 2048u, 0u };
    CHECK_EQ(sense_page_write(&pg, LINK_SN_I228_ADDR, 4u, a228, &o, 0u), 0u);
    const uint16_t en = LINK_SN_EN_I228;
    CHECK_EQ(sense_page_write(&pg, LINK_SN_ENABLE, 1u, &en, &o, 0u), 0u);
    CHECK_EQ(map_all(0x02u, 3u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(map_all(0x0Du, 0u), 0u);
    /* And from SENSE: the INA228 moved onto 0x43, which the map fits. */
    const uint16_t to43 = 0x43u;
    CHECK_EQ(sense_page_write(&pg, LINK_SN_I228_ADDR, 1u, &to43, &o, 0u),
             LINK_NACK_BAD_VALUE);
    const uint16_t to42 = 0x42u;                 /* fitted too */
    CHECK_EQ(sense_page_write(&pg, LINK_SN_I228_ADDR, 1u, &to42, &o, 0u),
             LINK_NACK_BAD_VALUE);
    const uint16_t to45 = 0x45u;
    CHECK_EQ(sense_page_write(&pg, LINK_SN_I228_ADDR, 1u, &to45, &o, 0u), 0u);
}

TEST_CASE(a_map_in_force_owns_senses_ina3221_bit_and_frame)
{
    fresh();
    CHECK_EQ(map_all(0x0Au, LINK_SM_FAST_NONE), 0u);   /* 0x41 and 0x43 */
    CHECK(pg.map_set);
    /* The mirror: the lowest fitted part while FAST is none. */
    CHECK((sreg(LINK_SN_ENABLE) & LINK_SN_EN_I3221) != 0u);
    CHECK_EQ(sreg(LINK_SN_I3221_ADDR), 0x41u);
    CHECK_EQ(sreg(LINK_SN_I3221_SHUNT_DMOHM), 1000u);
    CHECK_EQ(sreg(LINK_SN_I3221_CHANNELS), 0x07u);
    /* FAST's part, with its CH1 off: the first shunt that is not 0. */
    const uint16_t m[3] = { 0u, 1500u, 2500u };
    CHECK_EQ(sense_map_write(&pg, (uint8_t)LINK_SM_SHUNT(9u), 3u, m, &o, 0u),
             0u);
    CHECK_EQ(mwrite(LINK_SM_FAST, 11u), 0u);
    CHECK_EQ(sreg(LINK_SN_I3221_ADDR), 0x43u);
    CHECK_EQ(sreg(LINK_SN_I3221_SHUNT_DMOHM), 1500u);
    CHECK_EQ(sreg(LINK_SN_I3221_CHANNELS), 0x06u);
    /* An older panel's SENSE frame that differs is refused; one that
     * repeats the mirror, or leaves it alone, is taken. */
    const uint16_t f[4] = { 0x40u, 1000u, 0x01u, 0u };
    CHECK_EQ(sense_page_write(&pg, LINK_SN_I3221_ADDR, 4u, f, &o, 0u),
             LINK_NACK_BAD_VALUE);
    const uint16_t same[4] = { 0x43u, 1500u, 0x06u, 0u };
    CHECK_EQ(sense_page_write(&pg, LINK_SN_I3221_ADDR, 4u, same, &o, 0u), 0u);
    uint16_t en = sreg(LINK_SN_ENABLE) & (uint16_t)~LINK_SN_EN_I3221;
    CHECK_EQ(sense_page_write(&pg, LINK_SN_ENABLE, 1u, &en, &o, 0u),
             LINK_NACK_BAD_VALUE);
    en = (uint16_t)(sreg(LINK_SN_ENABLE) | LINK_SN_EN_AS5600);
    CHECK_EQ(sense_page_write(&pg, LINK_SN_ENABLE, 1u, &en, &o, 0u), 0u);
    /* Nothing fitted: the bit clears, the frame stays as it was. */
    const uint16_t none[2] = { 0u, LINK_SM_FAST_NONE };
    CHECK_EQ(sense_map_write(&pg, 0u, 2u, none, &o, 0u), 0u);
    CHECK_EQ(sreg(LINK_SN_ENABLE) & LINK_SN_EN_I3221, 0u);
    CHECK_EQ(sreg(LINK_SN_I3221_ADDR), 0x43u);
    CHECK_EQ(mreg(LINK_SM_FLAGS), LINK_SM_IN_FORCE);   /* for good */
    CHECK_EQ(sense_page_caps(&pg) & LINK_CAP_SERVO_SENSE, 0u);
}

TEST_CASE(servo_sense_is_a_channel_watching_an_output_a_port_or_a_rail)
{
    fresh();
    /* Derived: SENSE's INA3221 bit, as before the map. */
    CHECK_EQ(sense_page_caps(&pg) & LINK_CAP_SERVO_SENSE, 0u);
    const uint16_t en = LINK_SN_EN_I3221;
    CHECK_EQ(sense_page_write(&pg, LINK_SN_ENABLE, 1u, &en, &o, 0u), 0u);
    CHECK((sense_page_caps(&pg) & LINK_CAP_SERVO_SENSE) != 0u);
    /* In force with nothing watched: no servo current. */
    CHECK_EQ(map_all(0x01u, 0u), 0u);
    CHECK_EQ(sense_page_caps(&pg) & LINK_CAP_SERVO_SENSE, 0u);
    static const struct { uint16_t w; bool cap; } k[] = {
        { LINK_SM_WATCH_OUT(0u), true },  { LINK_SM_WATCH_OUT(7u), true },
        { LINK_SM_WATCH_PORT(0u), true }, { LINK_SM_WATCH_PORT(3u), true },
        { LINK_SM_WATCH_SUPPLY, false },  { LINK_SM_WATCH_NONE, false },
        { LINK_SM_WATCH_RAIL_A, true },   { LINK_SM_WATCH_RAIL_B, true },
        { LINK_SM_WATCH_PROG_5V, true },
    };
    for (unsigned i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        CHECK_EQ(sense_map_write(&pg, (uint8_t)LINK_SM_WATCH(2u), 1u,
                                 &k[i].w, &o, 0u), 0u);
        CHECK_EQ((sense_page_caps(&pg) & LINK_CAP_SERVO_SENSE) != 0u,
                 k[i].cap);
    }
    /* A watch on a channel that is off, or of a part not fitted, counts
     * for nothing. */
    const uint16_t out1 = LINK_SM_WATCH_OUT(0u);
    const uint16_t off = 0u;
    CHECK_EQ(sense_map_write(&pg, (uint8_t)LINK_SM_SHUNT(2u), 1u, &off,
                             &o, 0u), 0u);
    CHECK_EQ(sense_map_write(&pg, (uint8_t)LINK_SM_WATCH(2u), 1u, &out1,
                             &o, 0u), 0u);
    CHECK_EQ(sense_page_caps(&pg) & LINK_CAP_SERVO_SENSE, 0u);
    CHECK_EQ(sense_map_write(&pg, (uint8_t)LINK_SM_WATCH(5u), 1u, &out1,
                             &o, 0u), 0u);
    CHECK_EQ(sense_page_caps(&pg) & LINK_CAP_SERVO_SENSE, 0u);
    CHECK_EQ(map_all(0x03u, 0u), 0u);
    CHECK_EQ(sense_map_write(&pg, (uint8_t)LINK_SM_WATCH(5u), 1u, &out1,
                             &o, 0u), 0u);
    CHECK((sense_page_caps(&pg) & LINK_CAP_SERVO_SENSE) != 0u);
}

TEST_CASE(nothing_changes_while_the_bank_drives)
{
    fresh();
    outputs_arm(&o, true, 0u);
    CHECK(outputs_driving(&o));
    const uint16_t gen = pg.cfg_gen;
    /* The derived map written as it reads: in force, the bus as it is. */
    uint16_t d[LINK_SM_KEPT];
    sense_map_read(&pg, 0u, (uint8_t)LINK_SM_KEPT, d);
    CHECK_EQ(sense_map_write(&pg, 0u, (uint8_t)LINK_SM_KEPT, d, &o, 0u), 0u);
    CHECK(pg.map_set);
    CHECK_EQ(pg.cfg_gen, gen);
    /* Any change is refused, a WATCH as well; the same map again is not. */
    CHECK_EQ(mwrite(LINK_SM_WATCH(0u), 1u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(map_all(0x01u, 0u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(sense_map_write(&pg, 0u, (uint8_t)LINK_SM_KEPT, d, &o, 0u), 0u);
    outputs_arm(&o, false, 0u);
    CHECK_EQ(map_all(0x01u, 0u), 0u);
}

TEST_CASE(the_pins_are_judged_when_the_map_opens_the_bus)
{
    fresh();
    /* No bus enabled and its pins not set, as a panel writes them. */
    const uint16_t r[4] = { 0u, 0u, 0u, LINK_SN_KHZ_BUS };
    CHECK_EQ(sense_page_write(&pg, LINK_SN_ENABLE, 4u, r, &o, 0u), 0u);
    CHECK_EQ(map_all(0x01u, 0u), LINK_NACK_BAD_VALUE);
    CHECK(!pg.map_set);
    /* GP16 and GP17, but SDA taken by another page. */
    const uint16_t p2[2] = { 16u, 17u };
    CHECK_EQ(sense_page_write(&pg, LINK_SN_SDA_PIN, 2u, p2, &o, 0u), 0u);
    uint16_t m[LINK_SM_KEPT];
    memset(m, 0, sizeof(m));
    m[LINK_SM_ENABLE] = 1u;
    m[LINK_SM_FAST] = 0u;
    m[LINK_SM_SHUNT(0u)] = 1000u;
    CHECK_EQ(sense_map_write(&pg, 0u, (uint8_t)LINK_SM_KEPT, m, &o, BIT(16)),
             LINK_NACK_BAD_VALUE);
    CHECK_EQ(sense_map_write(&pg, 0u, (uint8_t)LINK_SM_KEPT, m, &o, 0u), 0u);
    CHECK_EQ(sense_page_pins(&pg), BIT(16) | BIT(17));
}

TEST_CASE(a_set_up_moves_for_the_bus_and_not_for_a_watch)
{
    fresh();
    CHECK_EQ(map_all(0x01u, 0u), 0u);
    uint16_t gen = pg.cfg_gen;
    CHECK_EQ(mwrite(LINK_SM_WATCH(0u), LINK_SM_WATCH_OUT(0u)), 0u);
    CHECK_EQ(pg.cfg_gen, gen);
    CHECK_EQ(mreg(LINK_SM_WATCH(0u)), 1u);
    CHECK_EQ(mwrite(LINK_SM_SHUNT(1u), 500u), 0u);
    CHECK_EQ(pg.cfg_gen, (uint16_t)(gen + 1u));
    gen = pg.cfg_gen;
    CHECK_EQ(mwrite(LINK_SM_FAST, 1u), 0u);
    CHECK_EQ(pg.cfg_gen, (uint16_t)(gen + 1u));
    gen = pg.cfg_gen;
    CHECK_EQ(mwrite(LINK_SM_FAST, 1u), 0u);         /* as it is */
    CHECK_EQ(pg.cfg_gen, gen);
    /* Core 1's order is the map's. */
    sense_cmd_t c;
    sense_page_cmd(&pg, &c);
    CHECK_EQ(c.cfg_gen, pg.cfg_gen);
    CHECK_EQ(c.parts.i3221.fitted, 0x01u);
    CHECK_EQ(c.parts.i3221.fast, 1u);
    CHECK_EQ(c.parts.i3221.shunt_uohm[0], 100000u);
    CHECK_EQ(c.parts.i3221.shunt_uohm[1], 50000u);
    CHECK_EQ(c.parts.i3221.shunt_uohm[3], 0u);
    CHECK_EQ(mwrite(LINK_SM_FAST, LINK_SM_FAST_NONE), 0u);
    sense_page_cmd(&pg, &c);
    CHECK_EQ(c.parts.i3221.fast, SENSE_FAST_NONE);
}

/* A snapshot of @p fitted parts online, each channel c's window at
 * (c + 1) x 10mA and 5000 + c mV. */
static sense_snap_t snapshot(uint8_t online)
{
    sense_snap_t s;
    memset(&s, 0, sizeof(s));
    s.cfg_gen = pg.cfg_gen;
    s.open = true;
    s.have_win = true;
    for (unsigned a = 0; a < SENSE_I3221_PARTS; ++a) {
        if ((online & (1u << a)) != 0u) {
            s.i3221[a] = SENSE_PART_ONLINE;
            s.i3221_maker[a] = 0x5449u;
            s.i3221_die[a] = 0x3220u;
        }
    }
    for (unsigned c = 0; c < SENSE_SRC_COUNT; ++c) {
        sense_window_t *w = &s.win[c];
        w->number = 300u;
        if (c < SENSE_CHANNELS && (online & (1u << (c / 3u))) != 0u) {
            w->n_i = 5u;
            w->n_v = 2u;
            w->i_mean_ua = (int32_t)((c + 1u) * 10000u);
            w->i_max_ua = (int32_t)((c + 1u) * 20000u);
            w->v_mean_uv = (int32_t)((5000u + c) * 1000u);
            w->v_min_uv = (int32_t)((4900u + c) * 1000u);
        }
    }
    return s;
}

TEST_CASE(every_channels_window_is_published_on_two_pages)
{
    fresh();
    CHECK_EQ(map_all(0x0Fu, 0u), 0u);
    sense_snap_t s = snapshot(0x0Bu);             /* 0x42 not answering */
    s.i3221[2] = SENSE_PART_WRONG_ID;
    s.i3221_die[2] = 0x0000u;
    s.i3221_maker[2] = 0x1408u;
    s.win[1].clip_hi = true;
    s.win[9].clip_lo = true;
    s.win[11].i_mean_ua = -2000;
    /* 0x42 gave samples early in the window and went offline before it
     * closed: its channels read 0 all the same. */
    s.win[7].n_i = 3u;
    s.win[7].i_mean_ua = 70000;
    s.win[7].i_max_ua = 90000;
    s.win[7].clip_hi = true;
    sense_page_publish(&pg, &s, 0u);
    CHECK_EQ(mreg(LINK_SM_FLAGS),
             (uint16_t)(LINK_SM_ONLINE(0u) | LINK_SM_ONLINE(1u)
                        | LINK_SM_ONLINE(3u) | LINK_SM_ID_WRONG(2u)
                        | LINK_SM_IN_FORCE));
    CHECK_EQ(mreg(LINK_SM_ID(0u)), 0x3220u);
    CHECK_EQ(mreg(LINK_SM_ID(2u)), 0u);
    uint16_t a[LINK_SX_COUNT];
    uint16_t b[LINK_SX_COUNT];
    sense_xwin_read(&pg, 0u, 0u, (uint8_t)LINK_SX_COUNT, a);
    sense_xwin_read(&pg, 1u, 0u, (uint8_t)LINK_SX_COUNT, b);
    CHECK_EQ(a[LINK_SX_WINDOW], 300u);
    CHECK_EQ(b[LINK_SX_WINDOW], 300u);
    CHECK_EQ(a[LINK_SX_FLAGS], (uint16_t)(0x0Bu | LINK_SX_HAVE));
    CHECK_EQ(b[LINK_SX_FLAGS], (uint16_t)(0x0Bu | LINK_SX_HAVE));
    CHECK_EQ(a[LINK_SX_E(0u, LINK_SX_E_MEAN_MA)], 10u);
    CHECK_EQ(a[LINK_SX_E(0u, LINK_SX_E_MAX_MA)], 20u);
    CHECK_EQ(a[LINK_SX_E(0u, LINK_SX_E_MEAN_MV)], 5000u);
    CHECK_EQ(a[LINK_SX_E(0u, LINK_SX_E_MIN_MV)], 4900u);
    CHECK_EQ(a[LINK_SX_E(0u, LINK_SX_E_FLAGS)],
             (uint16_t)(LINK_SX_E_CURRENT | LINK_SX_E_VOLTAGE));
    CHECK_EQ(a[LINK_SX_E(1u, LINK_SX_E_FLAGS)],
             (uint16_t)(LINK_SX_E_CURRENT | LINK_SX_E_VOLTAGE
                        | LINK_SX_E_CLIP_HI));
    CHECK_EQ(a[LINK_SX_E(5u, LINK_SX_E_MEAN_MA)], 60u);
    /* Channels 6 to 8, 0x42's: not online, 0 throughout. */
    for (unsigned r = 0; r < LINK_SX_E_STRIDE; ++r) {
        CHECK_EQ(b[LINK_SX_E(0u, r)], 0u);
        CHECK_EQ(b[LINK_SX_E(1u, r)], 0u);
        CHECK_EQ(b[LINK_SX_E(2u, r)], 0u);
    }
    CHECK_EQ(b[LINK_SX_E(3u, LINK_SX_E_FLAGS)],
             (uint16_t)(LINK_SX_E_CURRENT | LINK_SX_E_VOLTAGE
                        | LINK_SX_E_CLIP_LO));
    CHECK_EQ((int16_t)b[LINK_SX_E(5u, LINK_SX_E_MEAN_MA)], -2);
    CHECK_EQ(b[LINK_SX_E(5u, LINK_SX_E_MAX_MA)], 240u);
    /* A part range read past the page is not served. */
    uint16_t x = 0xABCDu;
    sense_xwin_read(&pg, 2u, 0u, 1u, &x);
    sense_xwin_read(&pg, 0u, 31u, 2u, &x);
    CHECK_EQ(x, 0xABCDu);
    /* A snapshot of an earlier set-up publishes nothing; a new set-up
     * clears both pages and the map's read-only registers. */
    sense_snap_t old = s;
    old.win[0].i_mean_ua = 999000;
    old.cfg_gen = (uint16_t)(pg.cfg_gen - 1u);
    sense_page_publish(&pg, &old, 0u);
    sense_xwin_read(&pg, 0u, 0u, (uint8_t)LINK_SX_COUNT, a);
    CHECK_EQ(a[LINK_SX_E(0u, LINK_SX_E_MEAN_MA)], 10u);
    CHECK_EQ(mwrite(LINK_SM_SHUNT(0u), 500u), 0u);
    sense_xwin_read(&pg, 0u, 0u, (uint8_t)LINK_SX_COUNT, a);
    for (unsigned r = 0; r < LINK_SX_COUNT; ++r) {
        CHECK_EQ(a[r], 0u);
    }
    CHECK_EQ(mreg(LINK_SM_FLAGS), LINK_SM_IN_FORCE);
    CHECK_EQ(mreg(LINK_SM_ID(0u)), 0u);
    /* No window yet: the online bits without HAVE. */
    s = snapshot(0x01u);
    s.have_win = false;
    sense_page_publish(&pg, &s, 0u);
    sense_xwin_read(&pg, 1u, 0u, 2u, b);
    CHECK_EQ(b[LINK_SX_WINDOW], 0u);
    CHECK_EQ(b[LINK_SX_FLAGS], 0x01u);
}

TEST_CASE(a_capture_with_the_map_in_force_needs_a_fast_channel)
{
    fresh();
    CHECK_EQ(map_all(0x01u, LINK_SM_FAST_NONE), 0u);
    outputs_arm(&o, true, 0u);
    const uint16_t f[4] = { LINK_SS_ARM_OF(1u, 0u), 900u, 100u, 50u };
    CHECK_EQ(sense_servo_write(&pg, LINK_SS_CAP_ARM, 4u, f, &o),
             LINK_NACK_BAD_VALUE);
    outputs_arm(&o, false, 0u);
    /* FAST on CH2 with CH1 off: SENSE's CH1 bit is of no account. */
    CHECK_EQ(mwrite(LINK_SM_FAST, 1u), 0u);
    CHECK_EQ(mwrite(LINK_SM_SHUNT(0u), 0u), 0u);
    CHECK_EQ(mwrite(LINK_SM_SHUNT(2u), 0u), 0u);
    CHECK_EQ(sreg(LINK_SN_I3221_CHANNELS), 0x02u);
    outputs_arm(&o, true, 0u);
    CHECK_EQ(sense_servo_write(&pg, LINK_SS_CAP_ARM, 4u, f, &o), 0u);
}

TEST_CASE(a_kept_map_comes_back_in_force_or_not_at_all)
{
    fresh();
    CHECK_EQ(map_all(0x05u, 6u), 0u);
    CHECK_EQ(mwrite(LINK_SM_WATCH(6u), LINK_SM_WATCH_PORT(0u)), 0u);
    uint16_t kept[LINK_SM_KEPT];
    uint16_t set = 0u;
    sense_map_kept(&pg, kept, &set);
    CHECK_EQ(set, 1u);
    uint16_t sense[LINK_SN_CONFIG_COUNT];
    memcpy(sense, pg.sense, sizeof(sense));
    /* A boot: SENSE's frame restored, then the map. */
    fresh();
    CHECK_EQ(sense_page_write(&pg, LINK_SN_ENABLE,
                              (uint8_t)LINK_SN_CONFIG_COUNT, sense, &o, 0u),
             0u);
    CHECK_EQ(sense_map_restore(&pg, kept, set, &o, 0u), 0u);
    CHECK(pg.map_set);
    CHECK_EQ(mreg(LINK_SM_ENABLE), 0x05u);
    CHECK_EQ(mreg(LINK_SM_WATCH(6u)), 0x10u);
    CHECK_EQ(sreg(LINK_SN_I3221_ADDR), 0x42u);
    /* Not in force: nothing restored, the map derived. */
    fresh();
    CHECK_EQ(sense_map_restore(&pg, kept, 0u, &o, 0u), 0u);
    CHECK(!pg.map_set);
    /* A kept map the checks refuse stays derived. */
    kept[LINK_SM_FAST] = 3u;                      /* 0x41 is not fitted */
    CHECK_EQ(sense_map_restore(&pg, kept, 1u, &o, 0u), LINK_NACK_BAD_VALUE);
    CHECK(!pg.map_set);
    CHECK_EQ(mreg(LINK_SM_FLAGS), LINK_SM_DERIVED);
    sense_map_kept(NULL, kept, &set);             /* nothing to keep */
    sense_map_read(&pg, 30u, 3u, kept);           /* off the page */
}

TEST_CASE(a_carriers_boot_puts_its_ina3221_in_force)
{
    /* Nothing kept on a fresh carrier: the record the IOmcu starts from
     * (main.c), its pins and its INA3221 put on by board_fix_store(). */
    out_store_t cfg;
    out_store_cfg_defaults(&cfg);
    board_fix_store(outbind_roles(OUTBIND_BOARD_RBMOD), &cfg);
    fresh();
    CHECK_EQ(sense_page_write(&pg, LINK_SN_ENABLE,
                              (uint8_t)LINK_SN_CONFIG_COUNT, cfg.sense, &o,
                              0u), 0u);
    CHECK_EQ(sense_map_restore(&pg, cfg.sense_map, cfg.sense_map_set, &o, 0u),
             0u);
    CHECK(pg.map_set);
    CHECK_EQ(mreg(LINK_SM_ENABLE) & 0x01u, 0x01u);
    CHECK_EQ(mreg(LINK_SM_WATCH(0u)), LINK_SM_WATCH_RAIL_B);
    CHECK_EQ(mreg(LINK_SM_WATCH(1u)), LINK_SM_WATCH_RAIL_A);
    CHECK_EQ(mreg(LINK_SM_WATCH(2u)), LINK_SM_WATCH_PROG_5V);
    /* SENSE mirrors it: the INA3221 on, at 0x40, on the carrier's bus. */
    CHECK((sreg(LINK_SN_ENABLE) & LINK_SN_EN_I3221) != 0u);
    CHECK_EQ(sreg(LINK_SN_I3221_ADDR), 0x40u);
    CHECK_EQ(sreg(LINK_SN_SDA_PIN), 16u);
    CHECK((sense_page_caps(&pg) & LINK_CAP_SERVO_SENSE) != 0u);
}

int main(void)
{
    RUN(the_pages_are_where_protocol_4_25_puts_them);
    RUN(until_written_the_map_is_senses_one_part);
    RUN(a_write_lies_within_the_kept_registers);
    RUN(each_value_is_taken_at_its_limits_and_refused_past_them);
    RUN(the_ina228_and_a_fitted_part_never_share_an_address);
    RUN(a_map_in_force_owns_senses_ina3221_bit_and_frame);
    RUN(servo_sense_is_a_channel_watching_an_output_a_port_or_a_rail);
    RUN(nothing_changes_while_the_bank_drives);
    RUN(the_pins_are_judged_when_the_map_opens_the_bus);
    RUN(a_set_up_moves_for_the_bus_and_not_for_a_watch);
    RUN(every_channels_window_is_published_on_two_pages);
    RUN(a_capture_with_the_map_in_force_needs_a_fast_channel);
    RUN(a_kept_map_comes_back_in_force_or_not_at_all);
    RUN(a_carriers_boot_puts_its_ina3221_in_force);
    return test_summary("sense_map");
}
