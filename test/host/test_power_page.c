/*
 * The POWER page of the remote servo node's IOmcu
 * (shared/outputs/power_page.c, protocol 4.41).
 *
 * Under test: the page at the start with and without the port switches;
 * RAIL 1 refused without a live heartbeat, on a chip before A4 and before
 * the critical limits are held, and taken with all three; a new PORT_WANT
 * bit refused without a heartbeat and a throttle port's bit refused; every
 * OFF taken without a heartbeat; values past the page's range, the read
 * only registers and a range off the page refused; FITTED 0 with PORT_WANT
 * kept and PORT_ON following RAIL in every bit, FITTED 0x1FF with PORT_ON
 * as asked; CRIT_N low, a new CF flag and a new SENSE_LIMIT trip each an
 * error within the pass that reads it, an old count not one; every error
 * and power_off() clearing RAIL, PORT_ON and PORT_WANT, TRIPPED kept until
 * the next RAIL 1; a port under half the rail on 2 windows in a row an
 * error, on 1 not, a window from before the port came on not counted, a
 * window without a reading neither counting nor breaking the run, the
 * rail not known leaving the check out, the master switch's shutdown found
 * on every port without the switches, and the window number's wrap at
 * 65536; the BEC check at 490mV and 500mV, only with the rail and every
 * port off and only on windows wholly after; the fault bits; the throttle
 * ports read off the bank.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "ina3221.h"
#include "link_msg.h"
#include "link_pages.h"
#include "out_bind.h"
#include "outputs.h"
#include "power_page.h"

static power_page_t pg;
static power_ctx_t  ctx;

static uint16_t reg(unsigned i)
{
    uint16_t v = 0xFFFFu;
    power_read(&pg, (uint8_t)i, 1u, &v);
    return v;
}

static uint8_t wr(unsigned r, uint16_t v)
{
    return power_write(&pg, (uint8_t)r, 1u, &v, &ctx);
}

/* A page with everything a RAIL 1 wants. */
static void start(bool switches)
{
    power_init(&pg, switches, true);
    ctx.beat_alive  = true;
    ctx.limits_held = true;
    ctx.throttle    = 0u;
}

static const power_in_t k_quiet = { 0 };

/* A window with every port at @p mv. */
static uint16_t win(uint16_t number, int32_t mv, uint16_t rail_mv)
{
    int32_t v[LINK_PW_PORTS];
    for (unsigned k = 0; k < LINK_PW_PORTS; ++k) { v[k] = mv; }
    return power_window(&pg, 0u, number, v, LINK_PW_PORT_ALL, rail_mv);
}

/* ------------------------------------------------------------ the page */

TEST_CASE(the_page_starts_off_with_the_switches_as_strapped)
{
    start(false);
    CHECK_EQ(reg(LINK_PW_RAIL), 0u);
    CHECK_EQ(reg(LINK_PW_FITTED), 0u);
    CHECK_EQ(reg(LINK_PW_PORT_ON), 0u);
    CHECK_EQ(reg(LINK_PW_PORT_WANT), 0u);
    CHECK_EQ(reg(LINK_PW_TRIPPED), 0u);
    CHECK_EQ(reg(LINK_PW_BEC), 0u);
    CHECK_EQ(reg(LINK_PW_COUNT), 0u);           /* past the page reads 0 */
    start(true);
    CHECK_EQ(reg(LINK_PW_FITTED), 0x01FFu);
    CHECK(!power_rail(&pg));
    CHECK(!power_rail(NULL));
    CHECK_EQ(power_port_on(NULL), 0u);
    CHECK_EQ(LINK_PAGE_POWER, 0x43);
    CHECK_EQ(LINK_PW_COUNT, 6);
}

TEST_CASE(rail_on_wants_the_heartbeat_the_chip_and_the_limits)
{
    start(true);
    ctx.beat_alive = false;
    CHECK_EQ(wr(LINK_PW_RAIL, 1u), (uint8_t)LINK_NACK_NOT_ARMED);
    CHECK_EQ(reg(LINK_PW_RAIL), 0u);
    ctx.beat_alive  = true;
    ctx.limits_held = false;
    CHECK_EQ(wr(LINK_PW_RAIL, 1u), (uint8_t)LINK_NACK_BUSY);
    CHECK_EQ(reg(LINK_PW_RAIL), 0u);
    ctx.limits_held = true;
    CHECK_EQ(wr(LINK_PW_RAIL, 1u), 0u);
    CHECK_EQ(reg(LINK_PW_RAIL), 1u);
    CHECK(power_rail(&pg));
    /* A chip before A4: refused with every other condition met. */
    power_init(&pg, true, false);
    CHECK_EQ(wr(LINK_PW_RAIL, 1u), (uint8_t)LINK_NACK_NOT_ARMED);
    CHECK_EQ(wr(LINK_PW_PORT_WANT, 0x0001u), (uint8_t)LINK_NACK_NOT_ARMED);
    CHECK_EQ(reg(LINK_PW_RAIL), 0u);
    CHECK_EQ(wr(LINK_PW_RAIL, 0u), 0u);          /* an OFF is taken */
}

TEST_CASE(a_new_port_bit_wants_the_heartbeat_and_no_throttle)
{
    start(true);
    CHECK_EQ(wr(LINK_PW_RAIL, 1u), 0u);
    ctx.beat_alive = false;
    CHECK_EQ(wr(LINK_PW_PORT_WANT, 0x0004u), (uint8_t)LINK_NACK_NOT_ARMED);
    CHECK_EQ(reg(LINK_PW_PORT_WANT), 0u);
    ctx.beat_alive = true;
    CHECK_EQ(wr(LINK_PW_PORT_WANT, 0x0004u), 0u);
    CHECK_EQ(reg(LINK_PW_PORT_ON), 0x0004u);
    /* A bit already set needs no heartbeat to stay set. */
    ctx.beat_alive = false;
    CHECK_EQ(wr(LINK_PW_PORT_WANT, 0x0004u), 0u);
    CHECK_EQ(wr(LINK_PW_PORT_WANT, 0x0006u), (uint8_t)LINK_NACK_NOT_ARMED);
    ctx.beat_alive = true;
    /* Port 9 bound as a throttle: its bit is refused, a heartbeat or not. */
    ctx.throttle = 0x0100u;
    CHECK_EQ(wr(LINK_PW_PORT_WANT, 0x0104u), (uint8_t)LINK_NACK_BAD_VALUE);
    CHECK_EQ(wr(LINK_PW_PORT_WANT, 0x00FFu), 0u);
    CHECK_EQ(reg(LINK_PW_PORT_ON), 0x00FFu);
    /* Past port 9. */
    CHECK_EQ(wr(LINK_PW_PORT_WANT, 0x0200u), (uint8_t)LINK_NACK_BAD_VALUE);
    CHECK_EQ(reg(LINK_PW_PORT_WANT), 0x00FFu);
}

TEST_CASE(an_off_is_always_taken)
{
    start(true);
    CHECK_EQ(wr(LINK_PW_RAIL, 1u), 0u);
    CHECK_EQ(wr(LINK_PW_PORT_WANT, 0x01FFu), 0u);
    ctx.beat_alive  = false;
    ctx.limits_held = false;
    ctx.throttle    = 0x01FFu & ~0x0003u;   /* bits kept are not throttles */
    CHECK_EQ(wr(LINK_PW_PORT_WANT, 0x0003u), 0u);  /* bits cleared */
    CHECK_EQ(reg(LINK_PW_PORT_ON), 0x0003u);
    ctx.throttle = 0u;
    CHECK_EQ(wr(LINK_PW_RAIL, 0u), 0u);
    CHECK_EQ(reg(LINK_PW_RAIL), 0u);
    CHECK_EQ(reg(LINK_PW_PORT_ON), 0u);
    /* RAIL 0 keeps PORT_WANT. */
    CHECK_EQ(reg(LINK_PW_PORT_WANT), 0x0003u);
    CHECK_EQ(wr(LINK_PW_PORT_WANT, 0u), 0u);
    CHECK_EQ(reg(LINK_PW_PORT_WANT), 0u);
}

TEST_CASE(the_write_is_judged_at_the_pages_limits)
{
    start(true);
    CHECK_EQ(wr(LINK_PW_RAIL, 2u), (uint8_t)LINK_NACK_BAD_VALUE);
    CHECK_EQ(wr(LINK_PW_FITTED, 0u), (uint8_t)LINK_NACK_READ_ONLY);
    CHECK_EQ(wr(LINK_PW_PORT_ON, 0u), (uint8_t)LINK_NACK_READ_ONLY);
    CHECK_EQ(wr(LINK_PW_TRIPPED, 0u), (uint8_t)LINK_NACK_READ_ONLY);
    CHECK_EQ(wr(LINK_PW_BEC, 0u), (uint8_t)LINK_NACK_READ_ONLY);
    CHECK_EQ(wr(LINK_PW_COUNT, 0u), (uint8_t)LINK_NACK_BAD_RANGE);
    const uint16_t four[4] = { 1u, 0u, 0u, 1u };
    CHECK_EQ(power_write(&pg, 0u, 4u, four, &ctx), (uint8_t)LINK_NACK_READ_ONLY);
    CHECK_EQ(power_write(&pg, 0u, 0u, four, &ctx), (uint8_t)LINK_NACK_BAD_RANGE);
    CHECK_EQ(power_write(&pg, 0u, 1u, NULL, &ctx), (uint8_t)LINK_NACK_BAD_RANGE);
    CHECK_EQ(power_write(&pg, 0u, 1u, four, NULL), (uint8_t)LINK_NACK_BAD_RANGE);
    CHECK_EQ(power_write(NULL, 0u, 1u, four, &ctx), (uint8_t)LINK_NACK_BAD_RANGE);
    /* Nothing of a refused write is kept. */
    CHECK_EQ(reg(LINK_PW_RAIL), 0u);
}

TEST_CASE(without_the_switches_every_port_follows_the_rail)
{
    start(false);
    CHECK_EQ(wr(LINK_PW_PORT_WANT, 0x0010u), 0u);
    CHECK_EQ(reg(LINK_PW_PORT_WANT), 0x0010u);   /* kept and reported */
    CHECK_EQ(reg(LINK_PW_PORT_ON), 0u);
    CHECK_EQ(wr(LINK_PW_RAIL, 1u), 0u);
    CHECK_EQ(reg(LINK_PW_PORT_ON), 0x01FFu);
    CHECK_EQ(wr(LINK_PW_RAIL, 0u), 0u);
    CHECK_EQ(reg(LINK_PW_PORT_ON), 0u);
    /* With them PORT_ON is PORT_WANT while the rail is on. */
    start(true);
    CHECK_EQ(wr(LINK_PW_PORT_WANT, 0x0010u), 0u);
    CHECK_EQ(reg(LINK_PW_PORT_ON), 0u);
    CHECK_EQ(wr(LINK_PW_RAIL, 1u), 0u);
    CHECK_EQ(reg(LINK_PW_PORT_ON), 0x0010u);
}

/* ------------------------------------------------------------ the trips */

/* Rail and every port on, nothing tripped. */
static void all_on(bool switches)
{
    start(switches);
    CHECK_EQ(wr(LINK_PW_PORT_WANT, 0x01FFu), 0u);
    CHECK_EQ(wr(LINK_PW_RAIL, 1u), 0u);
    CHECK_EQ(reg(LINK_PW_PORT_ON), 0x01FFu);
}

static bool all_off(void)
{
    return reg(LINK_PW_RAIL) == 0u && reg(LINK_PW_PORT_ON) == 0u
           && reg(LINK_PW_PORT_WANT) == 0u;
}

TEST_CASE(crit_n_low_is_an_error_within_the_pass)
{
    all_on(true);
    CHECK_EQ(power_step(&pg, &k_quiet), 0u);
    CHECK_EQ(reg(LINK_PW_RAIL), 1u);
    power_in_t in = k_quiet;
    in.crit_n_low = true;
    CHECK_EQ(power_step(&pg, &in), (uint16_t)LINK_PW_TRIP_CRIT_N);
    CHECK(all_off());
    CHECK_EQ(reg(LINK_PW_TRIPPED), (uint16_t)LINK_PW_TRIP_CRIT_N);
    CHECK_EQ(power_faults(&pg), (uint16_t)LINK_FAULT_OVERCURRENT);
    /* Kept until the next RAIL 1. */
    CHECK_EQ(power_step(&pg, &k_quiet), 0u);
    CHECK_EQ(reg(LINK_PW_TRIPPED), (uint16_t)LINK_PW_TRIP_CRIT_N);
    CHECK_EQ(wr(LINK_PW_RAIL, 1u), 0u);
    CHECK_EQ(reg(LINK_PW_TRIPPED), 0u);
    CHECK_EQ(power_faults(&pg), 0u);
    CHECK_EQ(power_step(NULL, &in), 0u);
    CHECK_EQ(power_step(&pg, NULL), 0u);
}

TEST_CASE(a_new_cf_flag_is_an_error_naming_its_channel)
{
    all_on(true);
    power_in_t in = k_quiet;
    in.me_ok = 0x07u;
    in.me[1] = INA3221_ME_CEN | INA3221_ME_CF2;   /* part 1 CH2: S5 */
    in.me_n[1] = 7u;
    /* The first count seen is taken as it stands. */
    CHECK_EQ(power_step(&pg, &in), 0u);
    CHECK_EQ(reg(LINK_PW_RAIL), 1u);
    /* The same read again is no new read. */
    CHECK_EQ(power_step(&pg, &in), 0u);
    /* A new read with the flag: an error, within that pass. */
    in.me_n[1] = 8u;
    CHECK_EQ(power_step(&pg, &in), LINK_PW_TRIP_CF(4u));
    CHECK(all_off());
    CHECK_EQ(reg(LINK_PW_TRIPPED), LINK_PW_TRIP_CF(4u));
    /* Each channel's flag: part 2 CH1 and CH3 are S7 and S9. */
    all_on(true);
    in.me[1] = INA3221_ME_CEN;
    in.me_n[1] = 9u;
    CHECK_EQ(power_step(&pg, &in), 0u);
    in.me[2] = INA3221_ME_CF1 | INA3221_ME_CF3;
    in.me_n[2] = 1u;
    in.me_n[1] = 10u;
    CHECK_EQ(power_step(&pg, &in),
             (uint16_t)(LINK_PW_TRIP_CF(6u) | LINK_PW_TRIP_CF(8u)));
    /* A part not read yet is not judged. */
    all_on(true);
    power_in_t none = k_quiet;
    none.me[0] = INA3221_ME_CF1;
    none.me_n[0] = 3u;
    CHECK_EQ(power_step(&pg, &none), 0u);
    none.me_n[0] = 4u;
    CHECK_EQ(power_step(&pg, &none), 0u);
    CHECK_EQ(reg(LINK_PW_RAIL), 1u);
}

TEST_CASE(a_new_sense_limit_trip_is_an_error)
{
    all_on(true);
    power_in_t in = k_quiet;
    in.over_n[3] = 12u;
    CHECK_EQ(power_step(&pg, &in), 0u);          /* the count as it stands */
    CHECK_EQ(power_step(&pg, &in), 0u);
    in.over_n[3] = 13u;
    CHECK_EQ(power_step(&pg, &in), (uint16_t)LINK_PW_TRIP_LIMIT);
    CHECK(all_off());
    CHECK_EQ(power_faults(&pg), (uint16_t)LINK_FAULT_OVERCURRENT);
    /* Its wrap at 65536 is a change as well. */
    all_on(true);
    in.over_n[8] = 0xFFFFu;
    CHECK_EQ(power_step(&pg, &in), 0u);
    in.over_n[8] = 0u;
    CHECK_EQ(power_step(&pg, &in), (uint16_t)LINK_PW_TRIP_LIMIT);
}

TEST_CASE(every_error_and_every_off_clears_rail_ports_and_wants)
{
    all_on(true);
    power_off(&pg);
    CHECK(all_off());
    CHECK_EQ(reg(LINK_PW_TRIPPED), 0u);
    all_on(true);
    power_trip(&pg, (uint16_t)LINK_PW_TRIP_THERMAL);
    CHECK(all_off());
    CHECK_EQ(reg(LINK_PW_TRIPPED), (uint16_t)LINK_PW_TRIP_THERMAL);
    CHECK_EQ(power_faults(&pg), (uint16_t)LINK_FAULT_OVERTEMP);
    power_trip(&pg, (uint16_t)LINK_PW_TRIP_LIMIT);
    CHECK_EQ(power_faults(&pg), (uint16_t)(LINK_FAULT_OVERTEMP
                                           | LINK_FAULT_OVERCURRENT));
    /* Without the switches the same. */
    all_on(false);
    power_trip(&pg, (uint16_t)LINK_PW_TRIP_CRIT_N);
    CHECK(all_off());
    power_off(NULL);
    power_trip(NULL, 1u);
    CHECK_EQ(power_faults(NULL), 0u);
}

/* ----------------------------------------------------- thermal shutdown */

TEST_CASE(a_port_under_half_the_rail_on_two_windows_is_an_error)
{
    start(true);
    CHECK_EQ(win(100u, 0, 6000u), 0u);          /* the newest window: 100 */
    CHECK_EQ(wr(LINK_PW_PORT_WANT, 0x0001u), 0u);
    CHECK_EQ(wr(LINK_PW_RAIL, 1u), 0u);
    int32_t v[LINK_PW_PORTS] = { 0 };
    /* Window 101 was filling as the port came on: not counted. */
    CHECK_EQ(power_window(&pg, 0u, 101u, v, 0x0001u, 6000u), 0u);
    /* 102 is the first wholly after; under half (2999mV of 6000mV). */
    v[0] = 2999;
    CHECK_EQ(power_window(&pg, 0u, 102u, v, 0x0001u, 6000u), 0u);
    CHECK_EQ(reg(LINK_PW_RAIL), 1u);
    /* The same number again is no new window. */
    CHECK_EQ(power_window(&pg, 0u, 102u, v, 0x0001u, 6000u), 0u);
    CHECK_EQ(reg(LINK_PW_RAIL), 1u);
    /* The second in a row trips. */
    CHECK_EQ(power_window(&pg, 0u, 103u, v, 0x0001u, 6000u),
             (uint16_t)LINK_PW_TRIP_THERMAL);
    CHECK(all_off());
    CHECK_EQ(reg(LINK_PW_TRIPPED), (uint16_t)LINK_PW_TRIP_THERMAL);
}

TEST_CASE(half_the_rail_itself_and_a_broken_run_do_not_trip)
{
    start(true);
    CHECK_EQ(win(10u, 0, 6000u), 0u);
    CHECK_EQ(wr(LINK_PW_PORT_WANT, 0x0002u), 0u);
    CHECK_EQ(wr(LINK_PW_RAIL, 1u), 0u);
    int32_t v[LINK_PW_PORTS] = { 0 };
    v[1] = 3000;                                 /* exactly half: not under */
    CHECK_EQ(power_window(&pg, 0u, 12u, v, 0x0002u, 6000u), 0u);
    CHECK_EQ(power_window(&pg, 0u, 13u, v, 0x0002u, 6000u), 0u);
    v[1] = 100;
    CHECK_EQ(power_window(&pg, 0u, 14u, v, 0x0002u, 6000u), 0u);
    /* A window without a reading neither counts nor breaks the run. */
    CHECK_EQ(power_window(&pg, 0u, 15u, v, 0x0000u, 6000u), 0u);
    v[1] = 5900;                                 /* back up: the run ends */
    CHECK_EQ(power_window(&pg, 0u, 16u, v, 0x0002u, 6000u), 0u);
    v[1] = 100;
    CHECK_EQ(power_window(&pg, 0u, 17u, v, 0x0002u, 6000u), 0u);
    CHECK_EQ(reg(LINK_PW_RAIL), 1u);
    /* A port that is off is not judged. */
    v[0] = 0;
    CHECK_EQ(power_window(&pg, 0u, 18u, v, 0x0001u, 6000u), 0u);
    CHECK_EQ(power_window(&pg, 0u, 19u, v, 0x0001u, 6000u), 0u);
    /* The rail's voltage not known: the check is left out. */
    CHECK_EQ(power_window(&pg, 0u, 20u, v, 0x0002u, 0u), 0u);
    CHECK_EQ(power_window(&pg, 0u, 21u, v, 0x0002u, 0u), 0u);
    CHECK_EQ(reg(LINK_PW_RAIL), 1u);
    /* Then known again, and 2 in a row. */
    CHECK_EQ(power_window(&pg, 0u, 22u, v, 0x0002u, 6000u),
             (uint16_t)LINK_PW_TRIP_THERMAL);
    CHECK_EQ(power_window(NULL, 0u, 1u, v, 1u, 1u), 0u);
    CHECK_EQ(power_window(&pg, 0u, 1u, NULL, 1u, 1u), 0u);
}

TEST_CASE(without_the_switches_every_port_finds_the_master_switch)
{
    start(false);
    CHECK_EQ(win(65534u, 0, 5000u), 0u);
    CHECK_EQ(wr(LINK_PW_RAIL, 1u), 0u);
    /* The window number wraps at 65536: 0 is the second after 65534. */
    CHECK_EQ(win(65535u, 0, 5000u), 0u);
    CHECK_EQ(win(0u, 2000, 5000u), 0u);
    CHECK_EQ(win(1u, 2000, 5000u), (uint16_t)LINK_PW_TRIP_THERMAL);
    CHECK(all_off());
}

/* --------------------------------------------------------- the BEC check */

TEST_CASE(the_bec_check_sets_a_port_at_500mv_and_not_at_490mv)
{
    start(true);
    int32_t v[LINK_PW_PORTS] = { 0 };
    v[2] = 490;
    v[3] = 500;
    /* From the start every window counts: nothing was ever on. */
    CHECK_EQ(power_window(&pg, 0u, 5u, v, LINK_PW_PORT_ALL, 0u), 0u);
    CHECK_EQ(reg(LINK_PW_BEC), 0x0008u);
    /* A port without a reading keeps its bit. */
    v[3] = 0;
    CHECK_EQ(power_window(&pg, 0u, 6u, v, (uint16_t)(LINK_PW_PORT_ALL & ~0x0008u),
                          0u), 0u);
    CHECK_EQ(reg(LINK_PW_BEC), 0x0008u);
    /* Read under, the bit goes. */
    CHECK_EQ(power_window(&pg, 0u, 7u, v, LINK_PW_PORT_ALL, 0u), 0u);
    CHECK_EQ(reg(LINK_PW_BEC), 0u);
}

TEST_CASE(the_bec_check_runs_only_with_rail_and_ports_off)
{
    start(true);
    int32_t v[LINK_PW_PORTS] = { 0 };
    CHECK_EQ(power_window(&pg, 0u, 40u, v, LINK_PW_PORT_ALL, 0u), 0u);
    CHECK_EQ(wr(LINK_PW_RAIL, 1u), 0u);
    /* With the rail on the rail is read: no check, the register stands. */
    for (unsigned k = 0; k < LINK_PW_PORTS; ++k) { v[k] = 6000; }
    CHECK_EQ(power_window(&pg, 0u, 41u, v, LINK_PW_PORT_ALL, 6000u), 0u);
    CHECK_EQ(reg(LINK_PW_BEC), 0u);
    /* Off again: the window filling at the change and the one before
     * still hold the rail; the next one is the first checked. */
    CHECK_EQ(wr(LINK_PW_RAIL, 0u), 0u);
    CHECK_EQ(power_window(&pg, 0u, 42u, v, LINK_PW_PORT_ALL, 0u), 0u);
    CHECK_EQ(reg(LINK_PW_BEC), 0u);
    for (unsigned k = 0; k < LINK_PW_PORTS; ++k) { v[k] = (k == 5u) ? 5000 : 0; }
    CHECK_EQ(power_window(&pg, 0u, 43u, v, LINK_PW_PORT_ALL, 0u), 0u);
    CHECK_EQ(reg(LINK_PW_BEC), 0x0020u);         /* S6: a BEC on its lead */
    /* A port on stops the check: its bit stands as last checked. */
    CHECK_EQ(wr(LINK_PW_PORT_WANT, 0x0001u), 0u);
    CHECK_EQ(reg(LINK_PW_PORT_ON), 0u);          /* rail off: still quiet */
    CHECK_EQ(wr(LINK_PW_RAIL, 1u), 0u);
    for (unsigned k = 0; k < LINK_PW_PORTS; ++k) { v[k] = 0; }
    CHECK_EQ(power_window(&pg, 0u, 44u, v, LINK_PW_PORT_ALL, 0u), 0u);
    CHECK_EQ(reg(LINK_PW_BEC), 0x0020u);
}

/* ------------------------------------------------------ throttle ports */

TEST_CASE(a_throttle_port_is_read_off_the_bank)
{
    const outbind_roles_t *r = outbind_roles((uint16_t)OUTBIND_BOARD_RSN);
    if (r == NULL) { T_FAIL("no board 3"); return; }
    outputs_t o;
    outputs_init(&o, 0u);
    CHECK_EQ(power_throttle(&o, r), 0u);
    /* A DShot slot on S3 (GPIO 20) rendering channel 4, a throttle. */
    const out_slot_t dshot = { .driver = OUT_DRIVER_DSHOT, .first_channel = 4u,
                               .channels = 1u, .pin = 20u, .rate_hz = 600u };
    CHECK(outputs_configure(&o, 0u, &dshot));
    CHECK(outputs_set_role(&o, 4u, OUT_ROLE_THROTTLE));
    /* A servo slot on S1 rendering channel 0, a surface. */
    const out_slot_t pwm = { .driver = OUT_DRIVER_PWM, .first_channel = 0u,
                             .channels = 1u, .pin = 16u, .rate_hz = 50u };
    CHECK(outputs_configure(&o, 1u, &pwm));
    CHECK_EQ(power_throttle(&o, r), 0x0004u);
    /* MOTOR PWM: a PWM slot whose channel is a throttle. */
    CHECK(outputs_set_role(&o, 0u, OUT_ROLE_THROTTLE));
    CHECK_EQ(power_throttle(&o, r), 0x0005u);
    CHECK_EQ(power_throttle(NULL, r), 0u);
    CHECK_EQ(power_throttle(&o, NULL), 0u);
}

/* ------------------------------------------------ review round 1 cases */

/* Port 1 on from window 10, then windows numbered 10 + @p from onward,
 * every one at full rail, then two under half: tripped. */
static bool trips_after(uint32_t from)
{
    start(true);
    int32_t v[LINK_PW_PORTS] = { 0 };
    CHECK_EQ(power_window(&pg, 0u, 10u, v, 0u, 0u), 0u);
    CHECK_EQ(wr(LINK_PW_PORT_WANT, 0x0001u), 0u);
    CHECK_EQ(wr(LINK_PW_RAIL, 1u), 0u);
    v[0] = 6000;
    uint32_t w = 11u;
    for (; w < 10u + from; ++w) {
        (void)power_window(&pg, 0u, (uint16_t)w, v, 0x0001u, 6000u);
    }
    v[0] = 100;
    (void)power_window(&pg, 0u, (uint16_t)w, v, 0x0001u, 6000u);
    return power_window(&pg, 0u, (uint16_t)(w + 1u), v, 0x0001u, 6000u)
           == (uint16_t)LINK_PW_TRIP_THERMAL;
}

TEST_CASE(the_thermal_check_stays_on_however_long_the_port_is_on)
{
    /* 32,767 and 32,768 windows: half the 16-bit window number; 65,535
     * and 65,536: its wrap; 131,072: the wrap twice. */
    static const uint32_t k_n[] = { 32767u, 32768u, 32769u, 65535u, 65536u,
                                    65537u, 131072u };
    for (size_t i = 0; i < sizeof(k_n) / sizeof(k_n[0]); ++i) {
        if (!trips_after(k_n[i])) {
            T_FAIL("no trip after %u windows", (unsigned)k_n[i]);
        }
    }
}

TEST_CASE(the_bec_check_stays_on_however_long_everything_is_off)
{
    start(true);
    int32_t v[LINK_PW_PORTS] = { 0 };
    CHECK_EQ(power_window(&pg, 0u, 5u, v, LINK_PW_PORT_ALL, 0u), 0u);
    CHECK_EQ(wr(LINK_PW_RAIL, 1u), 0u);
    CHECK_EQ(wr(LINK_PW_RAIL, 0u), 0u);
    for (uint32_t w = 6u; w < 6u + 40000u; ++w) {
        (void)power_window(&pg, 0u, (uint16_t)w, v, LINK_PW_PORT_ALL, 0u);
    }
    v[2] = 600;
    (void)power_window(&pg, 0u, (uint16_t)(6u + 40000u), v,
                       LINK_PW_PORT_ALL, 0u);
    CHECK_EQ(reg(LINK_PW_BEC), 0x0004u);
}

TEST_CASE(a_restarted_window_series_keeps_both_checks)
{
    /* The port came on at window 1000; the sensor schedule starts again
     * (a changed critical limit) and numbers its windows from 0. */
    start(true);
    int32_t v[LINK_PW_PORTS] = { 0 };
    CHECK_EQ(power_window(&pg, 1u, 1000u, v, 0u, 6000u), 0u);
    CHECK_EQ(wr(LINK_PW_PORT_WANT, 0x0001u), 0u);
    CHECK_EQ(wr(LINK_PW_RAIL, 1u), 0u);
    v[0] = 100;
    CHECK_EQ(power_window(&pg, 2u, 0u, v, 0x0001u, 6000u), 0u);
    CHECK_EQ(power_window(&pg, 2u, 1u, v, 0x0001u, 6000u),
             (uint16_t)LINK_PW_TRIP_THERMAL);
    /* The BEC check the same: off at window 500 of one series, read in
     * the next one's first windows. */
    start(true);
    CHECK_EQ(power_window(&pg, 1u, 500u, v, 0u, 0u), 0u);
    CHECK_EQ(wr(LINK_PW_RAIL, 1u), 0u);
    CHECK_EQ(wr(LINK_PW_RAIL, 0u), 0u);
    for (unsigned k = 0; k < LINK_PW_PORTS; ++k) { v[k] = 0; }
    v[7] = 700;
    CHECK_EQ(power_window(&pg, 2u, 0u, v, LINK_PW_PORT_ALL, 0u), 0u);
    CHECK_EQ(reg(LINK_PW_BEC), 0x0080u);
    /* Within one series, the same number twice is one window. */
    CHECK_EQ(power_window(&pg, 2u, 0u, v, LINK_PW_PORT_ALL, 0u), 0u);
}

TEST_CASE(rail_off_is_taken_with_a_kept_port_now_a_throttle)
{
    start(true);
    CHECK_EQ(wr(LINK_PW_PORT_WANT, 0x0003u), 0u);
    CHECK_EQ(wr(LINK_PW_RAIL, 1u), 0u);
    /* Port 2's output is bound as a throttle afterwards. */
    ctx.throttle = 0x0002u;
    ctx.beat_alive = false;
    CHECK_EQ(wr(LINK_PW_RAIL, 0u), 0u);
    CHECK_EQ(reg(LINK_PW_RAIL), 0u);
    CHECK_EQ(reg(LINK_PW_PORT_ON), 0u);
    const uint16_t both[4] = { 0u, 0u, 0u, 0x0003u };
    CHECK_EQ(power_write(&pg, 0u, 1u, both, &ctx), 0u);
    /* Clearing port 1 keeps port 2's bit: taken, nothing switched on. */
    CHECK_EQ(wr(LINK_PW_PORT_WANT, 0x0002u), 0u);
    /* Switching on with that bit kept powers a throttle: refused. */
    ctx.beat_alive = true;
    CHECK_EQ(wr(LINK_PW_RAIL, 1u), (uint8_t)LINK_NACK_BAD_VALUE);
    CHECK_EQ(reg(LINK_PW_RAIL), 0u);
    CHECK_EQ(wr(LINK_PW_PORT_WANT, 0u), 0u);
    CHECK_EQ(wr(LINK_PW_RAIL, 1u), 0u);
}

int main(void)
{
    RUN(the_page_starts_off_with_the_switches_as_strapped);
    RUN(rail_on_wants_the_heartbeat_the_chip_and_the_limits);
    RUN(a_new_port_bit_wants_the_heartbeat_and_no_throttle);
    RUN(an_off_is_always_taken);
    RUN(the_write_is_judged_at_the_pages_limits);
    RUN(without_the_switches_every_port_follows_the_rail);
    RUN(crit_n_low_is_an_error_within_the_pass);
    RUN(a_new_cf_flag_is_an_error_naming_its_channel);
    RUN(a_new_sense_limit_trip_is_an_error);
    RUN(every_error_and_every_off_clears_rail_ports_and_wants);
    RUN(a_port_under_half_the_rail_on_two_windows_is_an_error);
    RUN(half_the_rail_itself_and_a_broken_run_do_not_trip);
    RUN(without_the_switches_every_port_finds_the_master_switch);
    RUN(the_bec_check_sets_a_port_at_500mv_and_not_at_490mv);
    RUN(the_bec_check_runs_only_with_rail_and_ports_off);
    RUN(a_throttle_port_is_read_off_the_bank);
    RUN(the_thermal_check_stays_on_however_long_the_port_is_on);
    RUN(the_bec_check_stays_on_however_long_everything_is_off);
    RUN(a_restarted_window_series_keeps_both_checks);
    RUN(rail_off_is_taken_with_a_kept_port_now_a_throttle);
    return test_summary("power_page");
}
