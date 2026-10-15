/*
 * The Feetech bus session (protocols/feetech/feetech_bus.c) against
 * simulated servos (feetech_sim.h).
 *
 * Under test: the scan at the ends of the ID range (0, 253) and the range
 * refused (254, a start above the end), two servos on one ID; READ and
 * WRITE with every reply fault the decoder knows, a driver that does not
 * start and one that does not end; the EEPROM write in both byte orders:
 * unlock, write, lock, read back, the ID and the rate it moves followed,
 * the lock flag alone, a servo that keeps nothing, a failure at each step;
 * STOP during a scan, during an EEPROM write before and while the write is
 * on the wire, and with the rate moving; an ID or rate write that never
 * arrived, locked at the old ID and rate; a stop whose frames the driver
 * refuses, retried and then reported; the watch, its counters and its
 * block emptied by a failed read; the clock wrapping.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "feetech_bus.h"
#include "feetech_sim.h"

static ftsim_bus_t sim;
static feetech_bus_t bus;
static uint32_t now;

static void setup(uint32_t start_us)
{
    feetech_driver_t d;

    ftsim_init(&sim, 1000000u);
    d = ftsim_driver(&sim);
    now = start_us;
    CHECK(feetech_bus_init(&bus, &d, 1000000u));
}

/* Step every 100 us until the session is idle, at most @p max_steps. */
static feetech_res_t run(unsigned max_steps)
{
    feetech_res_t r = FEETECH_RES_BUSY;

    for (unsigned i = 0; i < max_steps; ++i) {
        r = feetech_bus_step(&bus, now);
        now += 100u;
        if (r != FEETECH_RES_BUSY && !feetech_bus_on_wire(&bus)) {
            return r;
        }
    }
    return r;
}

/* ------------------------------------------------------------- the scan */

TEST_CASE(a_scan_finds_the_servos_at_both_ends_of_the_id_range)
{
    setup(0);
    ftsim_add(&sim, 0, FEETECH_STS);
    ftsim_add(&sim, 1, FEETECH_SCS);
    ftsim_add(&sim, 253, FEETECH_STS);
    CHECK_EQ(feetech_bus_scan(&bus, 0, 253), FEETECH_RES_OK);
    CHECK(feetech_bus_busy(&bus));
    CHECK_EQ(feetech_bus_scan(&bus, 0, 1), FEETECH_RES_BUSY);
    CHECK_EQ(run(2000), FEETECH_RES_OK);
    CHECK_EQ(bus.found_n, 3);
    CHECK(feetech_bus_found(&bus, 0));
    CHECK(feetech_bus_found(&bus, 1));
    CHECK(!feetech_bus_found(&bus, 2));
    CHECK(feetech_bus_found(&bus, 253));
    CHECK(!feetech_bus_found(&bus, 254));
    CHECK_EQ(bus.bad_n, 0);
    /* One PING an ID, 254 of them, the last to 253. */
    CHECK_EQ(sim.n_log, FTSIM_LOG);
    CHECK_EQ(sim.n_start, 254u);
}

TEST_CASE(a_scan_range_beyond_253_or_backwards_is_refused)
{
    setup(0);
    CHECK_EQ(feetech_bus_scan(&bus, 0, 254), FEETECH_RES_ARG);
    CHECK_EQ(feetech_bus_scan(&bus, 254, 254), FEETECH_RES_ARG);
    CHECK_EQ(feetech_bus_scan(&bus, 10, 9), FEETECH_RES_ARG);
    CHECK(!feetech_bus_busy(&bus));
    /* A range of one ID at either end. */
    CHECK_EQ(feetech_bus_scan(&bus, 253, 253), FEETECH_RES_OK);
    CHECK_EQ(run(100), FEETECH_RES_OK);
    CHECK_EQ(sim.n_start, 1u);
    CHECK_EQ(sim.log[0].bytes[2], 253);
    CHECK_EQ(feetech_bus_scan(&bus, 0, 0), FEETECH_RES_OK);
    CHECK_EQ(run(100), FEETECH_RES_OK);
    CHECK_EQ(sim.log[1].bytes[2], 0);
}

TEST_CASE(two_servos_on_one_id_are_counted_not_found)
{
    setup(0);
    ftsim_add(&sim, 5, FEETECH_STS)->regs[FEETECH_REG_STATUS] = 0x20;
    ftsim_add(&sim, 5, FEETECH_STS);
    ftsim_add(&sim, 6, FEETECH_STS);
    CHECK_EQ(feetech_bus_scan(&bus, 1, 10), FEETECH_RES_OK);
    CHECK_EQ(run(500), FEETECH_RES_OK);
    CHECK(!feetech_bus_found(&bus, 5));
    CHECK(feetech_bus_found(&bus, 6));
    CHECK_EQ(bus.found_n, 1);
    CHECK_EQ(bus.bad_n, 1);
}

TEST_CASE(a_servo_at_another_rate_is_not_found_until_the_line_moves)
{
    ftsim_servo_t *v;

    setup(0);
    v = ftsim_add(&sim, 1, FEETECH_STS);
    v->regs[FEETECH_REG_BAUD] = 4;   /* 115200 */
    CHECK_EQ(feetech_bus_scan(&bus, 1, 1), FEETECH_RES_OK);
    CHECK_EQ(run(100), FEETECH_RES_OK);
    CHECK_EQ(bus.found_n, 0);
    CHECK_EQ(feetech_bus_set_baud(&bus, 115200u), FEETECH_RES_OK);
    CHECK_EQ(bus.baud, 115200u);
    CHECK_EQ(feetech_bus_scan(&bus, 1, 1), FEETECH_RES_OK);
    CHECK_EQ(run(100), FEETECH_RES_OK);
    CHECK_EQ(bus.found_n, 1);
    /* A rate the driver refuses leaves the rate in force. */
    sim.refuse_baud = true;
    CHECK_EQ(feetech_bus_set_baud(&bus, 57600u), FEETECH_RES_DRIVER);
    CHECK_EQ(bus.baud, 115200u);
    CHECK_EQ(feetech_bus_set_baud(&bus, 0), FEETECH_RES_ARG);
    CHECK_EQ(feetech_bus_scan(&bus, 1, 2), FEETECH_RES_OK);
    CHECK_EQ(feetech_bus_set_baud(&bus, 57600u), FEETECH_RES_BUSY);
}

/* ------------------------------------------------------ READ and WRITE */

TEST_CASE(a_read_gives_the_bytes_and_the_err_byte)
{
    ftsim_servo_t *v;

    setup(0);
    v = ftsim_add(&sim, 1, FEETECH_STS);
    v->regs[56] = 0x18;
    v->regs[57] = 0x05;
    v->regs[FEETECH_REG_STATUS] = FEETECH_ERR_TEMPERATURE;
    CHECK_EQ(feetech_bus_read(&bus, 1, 56, 2), FEETECH_RES_OK);
    CHECK_EQ(run(100), FEETECH_RES_OK);
    CHECK_EQ(bus.read_n, 2);
    CHECK_EQ(feetech_get16(FEETECH_STS, bus.read), 1304);
    CHECK_EQ(feetech_get16(FEETECH_SCS, bus.read), 6149);
    CHECK_EQ(bus.rx_id, 1);
    CHECK_EQ(bus.rx_error, FEETECH_ERR_TEMPERATURE);
    /* The frame on the wire is the manual's. */
    CHECK_EQ(sim.log[0].bytes[7], 0xBE);
}

TEST_CASE(a_read_or_write_outside_the_limits_is_refused)
{
    const uint8_t d[FEETECH_DATA_MAX + 1u] = { 0 };

    setup(0);
    CHECK_EQ(feetech_bus_read(&bus, 254, 56, 2), FEETECH_RES_ARG);
    CHECK_EQ(feetech_bus_read(&bus, 1, 56, 0), FEETECH_RES_ARG);
    CHECK_EQ(feetech_bus_read(&bus, 1, 56, FEETECH_DATA_MAX + 1u),
             FEETECH_RES_ARG);
    CHECK_EQ(feetech_bus_read(&bus, 1, 250, 7), FEETECH_RES_ARG);
    CHECK_EQ(feetech_bus_read(&bus, 1, 240, FEETECH_DATA_MAX),
             FEETECH_RES_OK);
    CHECK_EQ(run(100), FEETECH_RES_NO_REPLY);
    CHECK_EQ(feetech_bus_write(&bus, 1, 42, NULL, 2), FEETECH_RES_ARG);
    CHECK_EQ(feetech_bus_write(&bus, 1, 42, d, 0), FEETECH_RES_ARG);
    CHECK_EQ(feetech_bus_write(&bus, 1, 42, d, FEETECH_DATA_MAX + 1u),
             FEETECH_RES_ARG);
    CHECK_EQ(feetech_bus_write(&bus, FEETECH_ID_BROADCAST, 42, d, 2),
             FEETECH_RES_ARG);
    CHECK(!feetech_bus_busy(&bus));
}

TEST_CASE(every_reply_fault_comes_back_as_its_result)
{
    const uint8_t goal[2] = { 0x00, 0x08 };

    setup(0);
    ftsim_add(&sim, 1, FEETECH_STS);
    CHECK_EQ(feetech_bus_write(&bus, 1, 42, goal, 2), FEETECH_RES_OK);
    CHECK_EQ(run(100), FEETECH_RES_OK);
    CHECK_EQ(ftsim_find(&sim, 1)->regs[42], 0x00);
    CHECK_EQ(ftsim_find(&sim, 1)->regs[43], 0x08);

    sim.drop = true;
    CHECK_EQ(feetech_bus_read(&bus, 1, 56, 2), FEETECH_RES_OK);
    CHECK_EQ(run(100), FEETECH_RES_NO_REPLY);
    sim.bad_checksum = true;
    CHECK_EQ(feetech_bus_read(&bus, 1, 56, 2), FEETECH_RES_OK);
    CHECK_EQ(run(100), FEETECH_RES_CHECKSUM);
    sim.truncate = 1;
    CHECK_EQ(feetech_bus_read(&bus, 1, 56, 2), FEETECH_RES_OK);
    CHECK_EQ(run(100), FEETECH_RES_TRUNCATED);
    sim.wrong_id = true;
    CHECK_EQ(feetech_bus_read(&bus, 1, 56, 2), FEETECH_RES_OK);
    CHECK_EQ(run(100), FEETECH_RES_WRONG_ID);
    CHECK_EQ(bus.rx_id, 2);
    CHECK_EQ(bus.read_n, 0);
    /* The echo of the request is counted off before the reply. */
    sim.echo = true;
    CHECK_EQ(feetech_bus_read(&bus, 1, 56, 2), FEETECH_RES_OK);
    CHECK_EQ(run(100), FEETECH_RES_OK);
    CHECK_EQ(bus.read_n, 2);
    /* An echo and no reply is no reply. */
    sim.drop = true;
    CHECK_EQ(feetech_bus_read(&bus, 1, 56, 2), FEETECH_RES_OK);
    CHECK_EQ(run(100), FEETECH_RES_NO_REPLY);
}

TEST_CASE(a_driver_that_hears_itself_keeps_the_echo_and_the_whole_reply)
{
    setup(0);
    ftsim_add(&sim, 1, FEETECH_STS);
    sim.echo = true;
    /* A PING's echo is 6 bytes, as long as its reply: n_rx is the reply's
     * count alone, so the driver keeps both. */
    CHECK_EQ(feetech_bus_scan(&bus, 1, 1), FEETECH_RES_OK);
    CHECK_EQ(run(100), FEETECH_RES_OK);
    CHECK_EQ(sim.n_rx, 6u);
    CHECK(feetech_bus_found(&bus, 1));
    /* The longest read: 15 bytes of reply past an 8-byte echo. */
    CHECK_EQ(feetech_bus_read(&bus, 1, FEETECH_REG_PRESENT, FEETECH_LIVE_STS),
             FEETECH_RES_OK);
    CHECK_EQ(run(100), FEETECH_RES_OK);
    CHECK_EQ(sim.n_rx, FEETECH_LIVE_STS + FEETECH_FRAME_OVERHEAD);
    CHECK_EQ(bus.read_n, FEETECH_LIVE_STS);
}

TEST_CASE(a_driver_that_does_not_start_or_does_not_end_is_driver)
{
    uint32_t t0;

    setup(0);
    ftsim_add(&sim, 1, FEETECH_STS);
    sim.refuse_start = true;
    CHECK_EQ(feetech_bus_read(&bus, 1, 56, 2), FEETECH_RES_OK);
    CHECK_EQ(run(10), FEETECH_RES_DRIVER);
    sim.refuse_start = false;
    sim.hang = true;
    CHECK_EQ(feetech_bus_read(&bus, 1, 56, 2), FEETECH_RES_OK);
    t0 = now;
    CHECK_EQ(feetech_bus_step(&bus, now), FEETECH_RES_BUSY);
    CHECK(feetech_bus_on_wire(&bus));
    /* The frame, the reply window and the grace: still waiting just
     * before, ended just after. */
    CHECK_EQ(feetech_bus_step(&bus, bus.limit_us), FEETECH_RES_BUSY);
    CHECK_EQ(feetech_bus_step(&bus, bus.limit_us + 1u), FEETECH_RES_DRIVER);
    CHECK(bus.limit_us - t0 >= FEETECH_BUS_GRACE_US + FEETECH_BUS_REPLY_US);
    CHECK(!feetech_bus_on_wire(&bus));
}

TEST_CASE(the_reply_wait_is_the_window_and_twice_the_reply)
{
    setup(0);
    ftsim_add(&sim, 1, FEETECH_STS);
    CHECK_EQ(feetech_bus_read(&bus, 1, 56, 15), FEETECH_RES_OK);
    CHECK_EQ(feetech_bus_step(&bus, now), FEETECH_RES_BUSY);
    /* 21 bytes at 1 Mbaud are 210 us. */
    CHECK_EQ(sim.n_rx, 21u);
    CHECK_EQ(sim.wait_us, FEETECH_BUS_REPLY_US + 420u);
    CHECK_EQ(run(100), FEETECH_RES_OK);
    /* A write that is not answered waits for nothing. */
    CHECK_EQ(feetech_bus_torque_off(&bus), FEETECH_RES_OK);
    CHECK_EQ(feetech_bus_step(&bus, now + 1000u), FEETECH_RES_BUSY);
    CHECK_EQ(sim.n_rx, 0u);
    CHECK_EQ(sim.wait_us, 0u);
}

TEST_CASE(the_gap_between_transactions_holds_across_a_clock_wrap)
{
    setup(0xFFFFFF00u);
    ftsim_add(&sim, 1, FEETECH_STS);
    ftsim_add(&sim, 2, FEETECH_STS);
    CHECK_EQ(feetech_bus_scan(&bus, 1, 2), FEETECH_RES_OK);
    CHECK_EQ(feetech_bus_step(&bus, now), FEETECH_RES_BUSY);
    CHECK_EQ(sim.n_start, 1u);
    /* The reply is in: the next PING waits FEETECH_BUS_GAP_US. */
    CHECK_EQ(feetech_bus_step(&bus, now + 10u), FEETECH_RES_BUSY);
    CHECK_EQ(sim.n_start, 1u);
    CHECK_EQ(feetech_bus_step(&bus, now + 10u + FEETECH_BUS_GAP_US - 1u),
             FEETECH_RES_BUSY);
    CHECK_EQ(sim.n_start, 1u);
    CHECK_EQ(feetech_bus_step(&bus, now + 10u + FEETECH_BUS_GAP_US),
             FEETECH_RES_BUSY);
    CHECK_EQ(sim.n_start, 2u);
    now += 400u;
    CHECK_EQ(run(10), FEETECH_RES_OK);
    CHECK_EQ(bus.found_n, 2);
    CHECK(now < 0x1000u);
}

/* ----------------------------------------------------- the EEPROM write */

TEST_CASE(an_eeprom_write_unlocks_writes_locks_and_reads_back)
{
    uint8_t lim[2];
    ftsim_servo_t *v;

    setup(0);
    v = ftsim_add(&sim, 1, FEETECH_STS);
    feetech_put16(FEETECH_STS, lim, 300);
    CHECK_EQ(feetech_bus_write_ee(&bus, FEETECH_STS, 1, FEETECH_REG_MIN_ANGLE,
                                  lim, 2), FEETECH_RES_OK);
    CHECK_EQ(run(100), FEETECH_RES_OK);
    CHECK_EQ(sim.n_log, 4u);
    CHECK(ftsim_is_write1(&sim.log[0], 1, FEETECH_REG_LOCK_STS, 0));
    CHECK_EQ(sim.log[1].bytes[5], FEETECH_REG_MIN_ANGLE);
    CHECK_EQ(sim.log[1].bytes[6], 0x2C);   /* 300 low byte first */
    CHECK_EQ(sim.log[1].bytes[7], 0x01);
    CHECK(ftsim_is_write1(&sim.log[2], 1, FEETECH_REG_LOCK_STS, 1));
    CHECK_EQ(sim.log[3].bytes[4], FEETECH_READ);
    CHECK_EQ(feetech_get16(FEETECH_STS, &v->regs[9]), 300);
    CHECK_EQ(v->regs[FEETECH_REG_LOCK_STS], 1);
    CHECK_EQ(bus.unlocked, 0);
}

TEST_CASE(an_scs_eeprom_write_puts_the_high_byte_first_and_uses_lock_48)
{
    uint8_t lim[2];
    ftsim_servo_t *v;

    setup(0);
    v = ftsim_add(&sim, 3, FEETECH_SCS);
    feetech_put16(FEETECH_SCS, lim, 1000);
    CHECK_EQ(feetech_bus_write_ee(&bus, FEETECH_SCS, 3, FEETECH_REG_MAX_ANGLE,
                                  lim, 2), FEETECH_RES_OK);
    CHECK_EQ(run(100), FEETECH_RES_OK);
    CHECK(ftsim_is_write1(&sim.log[0], 3, FEETECH_REG_LOCK_SCS, 0));
    CHECK_EQ(sim.log[1].bytes[6], 0x03);
    CHECK_EQ(sim.log[1].bytes[7], 0xE8);
    CHECK(ftsim_is_write1(&sim.log[2], 3, FEETECH_REG_LOCK_SCS, 1));
    CHECK_EQ(feetech_get16(FEETECH_SCS, &v->regs[11]), 1000);
    CHECK_EQ(v->regs[11], 0x03);
}

TEST_CASE(a_new_id_and_a_new_rate_are_followed)
{
    const uint8_t id9 = 9;
    const uint8_t baud4 = 4;
    ftsim_servo_t *v;

    setup(0);
    v = ftsim_add(&sim, 1, FEETECH_STS);
    CHECK_EQ(feetech_bus_write_ee(&bus, FEETECH_STS, 1, FEETECH_REG_ID, &id9,
                                  1), FEETECH_RES_OK);
    CHECK_EQ(run(100), FEETECH_RES_OK);
    CHECK_EQ(v->regs[FEETECH_REG_ID], 9);
    CHECK(ftsim_is_write1(&sim.log[2], 9, FEETECH_REG_LOCK_STS, 1));
    CHECK_EQ(sim.log[3].bytes[2], 9);
    CHECK_EQ(v->regs[FEETECH_REG_LOCK_STS], 1);

    CHECK_EQ(feetech_bus_write_ee(&bus, FEETECH_STS, 9, FEETECH_REG_BAUD,
                                  &baud4, 1), FEETECH_RES_OK);
    CHECK_EQ(run(100), FEETECH_RES_OK);
    CHECK_EQ(bus.baud, 115200u);
    CHECK_EQ(sim.baud, 115200u);
    CHECK_EQ(sim.log_baud[5], 1000000u);   /* the write */
    CHECK_EQ(sim.log_baud[6], 115200u);    /* the lock */
    CHECK_EQ(v->regs[FEETECH_REG_LOCK_STS], 1);
    /* An ID or a rate a servo does not take is refused. */
    {
        const uint8_t id254 = 254;
        const uint8_t baud8 = 8;

        CHECK_EQ(feetech_bus_write_ee(&bus, FEETECH_STS, 9, FEETECH_REG_ID,
                                      &id254, 1), FEETECH_RES_ARG);
        CHECK_EQ(feetech_bus_write_ee(&bus, FEETECH_STS, 9, FEETECH_REG_BAUD,
                                      &baud8, 1), FEETECH_RES_ARG);
    }
}

/* Step until the unlock's reply is in and the write is next. */
static void past_the_unlock(void)
{
    (void)feetech_bus_step(&bus, now);     /* the unlock out */
    now += 200u;
    (void)feetech_bus_step(&bus, now);     /* its reply in */
    now += 200u;
}

TEST_CASE(an_id_write_that_never_arrived_is_locked_at_the_old_id)
{
    const uint8_t id9 = 9;
    ftsim_servo_t *v;

    setup(0);
    v = ftsim_add(&sim, 1, FEETECH_STS);
    CHECK_EQ(feetech_bus_write_ee(&bus, FEETECH_STS, 1, FEETECH_REG_ID, &id9,
                                  1), FEETECH_RES_OK);
    past_the_unlock();
    CHECK_EQ(v->regs[FEETECH_REG_LOCK_STS], 0);
    sim.deaf = true;                       /* the write reaches nobody */
    CHECK_EQ(run(100), FEETECH_RES_NO_REPLY);
    /* Unlock at 1, the write, the lock at 9 unanswered, the lock at 1. */
    CHECK_EQ(sim.n_log, 4u);
    CHECK(ftsim_is_write1(&sim.log[2], 9, FEETECH_REG_LOCK_STS, 1));
    CHECK(ftsim_is_write1(&sim.log[3], 1, FEETECH_REG_LOCK_STS, 1));
    CHECK_EQ(v->regs[FEETECH_REG_ID], 1);
    CHECK_EQ(v->regs[FEETECH_REG_LOCK_STS], 1);
    CHECK_EQ(bus.unlocked, 0);
    CHECK_EQ(bus.id, 1);
    CHECK_EQ(bus.old_id, 0xFF);
}

TEST_CASE(a_rate_write_that_never_arrived_puts_the_line_back)
{
    const uint8_t baud4 = 4;
    ftsim_servo_t *v;

    setup(0);
    v = ftsim_add(&sim, 1, FEETECH_STS);
    CHECK_EQ(feetech_bus_write_ee(&bus, FEETECH_STS, 1, FEETECH_REG_BAUD,
                                  &baud4, 1), FEETECH_RES_OK);
    past_the_unlock();
    sim.deaf = true;
    CHECK_EQ(run(100), FEETECH_RES_NO_REPLY);
    CHECK_EQ(sim.n_log, 4u);
    CHECK_EQ(sim.log_baud[2], 115200u);   /* the lock where it would be */
    CHECK_EQ(sim.log_baud[3], 1000000u);  /* and where it is */
    CHECK_EQ(bus.baud, 1000000u);
    CHECK_EQ(sim.baud, 1000000u);
    CHECK_EQ(v->regs[FEETECH_REG_BAUD], FEETECH_BAUD_DEFAULT);
    CHECK_EQ(v->regs[FEETECH_REG_LOCK_STS], 1);
    CHECK_EQ(bus.unlocked, 0);
    /* A line that cannot go back says DRIVER. */
    CHECK_EQ(feetech_bus_write_ee(&bus, FEETECH_STS, 1, FEETECH_REG_BAUD,
                                  &baud4, 1), FEETECH_RES_OK);
    now += 200u;
    past_the_unlock();
    sim.deaf = true;
    for (unsigned i = 0; i < 20u && bus.baud != 115200u; ++i) {
        (void)feetech_bus_step(&bus, now);
        now += 200u;
    }
    CHECK_EQ(bus.baud, 115200u);
    sim.refuse_baud = true;
    CHECK_EQ(run(100), FEETECH_RES_DRIVER);
    CHECK_EQ(bus.baud, 115200u);
}

TEST_CASE(an_id_write_answered_only_by_its_lock_moved)
{
    const uint8_t id9 = 9;
    ftsim_servo_t *v;

    setup(0);
    v = ftsim_add(&sim, 1, FEETECH_STS);
    CHECK_EQ(feetech_bus_write_ee(&bus, FEETECH_STS, 1, FEETECH_REG_ID, &id9,
                                  1), FEETECH_RES_OK);
    past_the_unlock();
    sim.drop = true;                       /* the write's reply only */
    CHECK_EQ(run(100), FEETECH_RES_OK);
    CHECK_EQ(v->regs[FEETECH_REG_ID], 9);
    CHECK_EQ(sim.n_log, 4u);               /* unlock, write, lock, read */
    CHECK_EQ(bus.id, 9);
}

TEST_CASE(the_lock_flag_alone_is_one_write_and_its_read)
{
    const uint8_t zero = 0;
    const uint8_t two = 2;
    ftsim_servo_t *v;

    setup(0);
    v = ftsim_add(&sim, 1, FEETECH_STS);
    CHECK_EQ(feetech_bus_write_ee(&bus, FEETECH_STS, 1, FEETECH_REG_LOCK_STS,
                                  &zero, 1), FEETECH_RES_OK);
    CHECK_EQ(run(100), FEETECH_RES_OK);
    CHECK_EQ(sim.n_log, 2u);
    CHECK_EQ(v->regs[FEETECH_REG_LOCK_STS], 0);
    CHECK_EQ(feetech_bus_write_ee(&bus, FEETECH_STS, 1, FEETECH_REG_LOCK_STS,
                                  &two, 1), FEETECH_RES_ARG);
    /* RAM and a span across the EEPROM end are no EEPROM write. */
    CHECK_EQ(feetech_bus_write_ee(&bus, FEETECH_STS, 1, 42, &zero, 1),
             FEETECH_RES_ARG);
    {
        const uint8_t two_bytes[2] = { 0, 0 };

        CHECK_EQ(feetech_bus_write_ee(&bus, FEETECH_STS, 1, 39, two_bytes, 2),
                 FEETECH_RES_ARG);
        CHECK_EQ(feetech_bus_write_ee(&bus, FEETECH_STS, 1, 38, two_bytes, 2),
                 FEETECH_RES_OK);
        CHECK_EQ(run(100), FEETECH_RES_OK);
        CHECK_EQ(feetech_bus_write_ee(&bus, FEETECH_SERIES_COUNT, 1, 38,
                                      two_bytes, 2), FEETECH_RES_ARG);
    }
}

TEST_CASE(the_lock_flag_written_on_purpose_settles_what_is_owed)
{
    const uint8_t zero = 0;
    const uint8_t one = 1;
    const uint8_t mode1 = 1;
    ftsim_servo_t *v;

    setup(0);
    v = ftsim_add(&sim, 1, FEETECH_STS);
    /* LOCK 0 on purpose: the session owes no lock, STOP sends none. */
    CHECK_EQ(feetech_bus_write_ee(&bus, FEETECH_STS, 1, FEETECH_REG_LOCK_STS,
                                  &zero, 1), FEETECH_RES_OK);
    CHECK_EQ(run(100), FEETECH_RES_OK);
    CHECK_EQ(bus.unlocked, 0);
    feetech_bus_stop(&bus);
    (void)run(100);
    CHECK_EQ(sim.n_log, 3u);               /* write, read, torque off */
    CHECK_EQ(v->regs[FEETECH_REG_LOCK_STS], 0);
    /* A write whose lock went unanswered leaves it owed; LOCK 1 on purpose
     * settles it. */
    v->regs[FEETECH_REG_LOCK_STS] = 1;
    CHECK_EQ(feetech_bus_write_ee(&bus, FEETECH_STS, 1, FEETECH_REG_MODE,
                                  &mode1, 1), FEETECH_RES_OK);
    past_the_unlock();
    (void)feetech_bus_step(&bus, now);     /* the write out */
    now += 200u;
    (void)feetech_bus_step(&bus, now);     /* its reply in */
    now += 200u;
    sim.drop = true;                       /* the lock's reply */
    CHECK_EQ(run(100), FEETECH_RES_NO_REPLY);
    CHECK_EQ(bus.unlocked, 1);
    CHECK_EQ(feetech_bus_write_ee(&bus, FEETECH_STS, 1, FEETECH_REG_LOCK_STS,
                                  &one, 1), FEETECH_RES_OK);
    CHECK_EQ(run(100), FEETECH_RES_OK);
    CHECK_EQ(bus.unlocked, 0);
    CHECK_EQ(v->regs[FEETECH_REG_LOCK_STS], 1);
}

TEST_CASE(a_lock_owed_to_one_servo_goes_before_a_write_to_another)
{
    const uint8_t mode1 = 1;
    const uint8_t mode2 = 2;
    ftsim_servo_t *v1;
    ftsim_servo_t *v2;
    unsigned at;

    setup(0);
    v1 = ftsim_add(&sim, 1, FEETECH_STS);
    v2 = ftsim_add(&sim, 2, FEETECH_SCS);
    /* Servo 1's lock goes unanswered and stays owed. */
    CHECK_EQ(feetech_bus_write_ee(&bus, FEETECH_STS, 1, FEETECH_REG_MODE,
                                  &mode1, 1), FEETECH_RES_OK);
    past_the_unlock();
    (void)feetech_bus_step(&bus, now);
    now += 200u;
    (void)feetech_bus_step(&bus, now);
    now += 200u;
    sim.deaf = true;                       /* the lock reaches nobody */
    CHECK_EQ(run(100), FEETECH_RES_NO_REPLY);
    CHECK_EQ(bus.unlocked, 1);
    CHECK_EQ(bus.unlocked_id, 1);
    CHECK_EQ(v1->regs[FEETECH_REG_LOCK_STS], 0);
    /* A write to servo 2 locks servo 1 first, then runs its own four. */
    at = sim.n_log;
    CHECK_EQ(feetech_bus_write_ee(&bus, FEETECH_SCS, 2, FEETECH_REG_MODE,
                                  &mode2, 1), FEETECH_RES_OK);
    CHECK_EQ(run(100), FEETECH_RES_OK);
    CHECK_EQ(sim.n_log, at + 5u);
    CHECK(ftsim_is_write1(&sim.log[at], 1, FEETECH_REG_LOCK_STS, 1));
    CHECK(ftsim_is_write1(&sim.log[at + 1u], 2, FEETECH_REG_LOCK_SCS, 0));
    CHECK_EQ(v1->regs[FEETECH_REG_LOCK_STS], 1);
    CHECK_EQ(v2->regs[FEETECH_REG_MODE], 2);
    CHECK_EQ(bus.unlocked, 0);
    /* Owed again, and the lock first is unanswered: the write does not
     * start, the debt stays with servo 1, STOP sends it. */
    CHECK_EQ(feetech_bus_write_ee(&bus, FEETECH_STS, 1, FEETECH_REG_MODE,
                                  &mode2, 1), FEETECH_RES_OK);
    past_the_unlock();
    (void)feetech_bus_step(&bus, now);
    now += 200u;
    (void)feetech_bus_step(&bus, now);
    now += 200u;
    sim.deaf = true;
    CHECK_EQ(run(100), FEETECH_RES_NO_REPLY);
    CHECK_EQ(bus.unlocked_id, 1);
    at = sim.n_log;
    CHECK_EQ(feetech_bus_write_ee(&bus, FEETECH_SCS, 2, FEETECH_REG_MODE,
                                  &mode1, 1), FEETECH_RES_OK);
    sim.deaf = true;
    CHECK_EQ(run(100), FEETECH_RES_NO_REPLY);
    CHECK_EQ(sim.n_log, at + 1u);
    CHECK_EQ(v2->regs[FEETECH_REG_MODE], 2);
    CHECK_EQ(bus.unlocked, 1);
    CHECK_EQ(bus.unlocked_id, 1);
    CHECK_EQ(bus.unlocked_series, FEETECH_STS);
    feetech_bus_stop(&bus);
    (void)run(100);
    CHECK_EQ(bus.unlocked, 0);
    CHECK_EQ(v1->regs[FEETECH_REG_LOCK_STS], 1);
    /* A write to the servo owed the lock needs no lock first. */
    CHECK_EQ(feetech_bus_write_ee(&bus, FEETECH_STS, 1, FEETECH_REG_MODE,
                                  &mode1, 1), FEETECH_RES_OK);
    bus.unlocked = 1;
    bus.unlocked_id = 1;
    bus.unlocked_series = FEETECH_STS;
    bus.op = FEETECH_OP_NONE;
    CHECK_EQ(feetech_bus_write_ee(&bus, FEETECH_STS, 1, FEETECH_REG_MODE,
                                  &mode1, 1), FEETECH_RES_OK);
    at = sim.n_log;
    CHECK_EQ(run(100), FEETECH_RES_OK);
    CHECK_EQ(sim.n_log, at + 4u);
    CHECK(ftsim_is_write1(&sim.log[at], 1, FEETECH_REG_LOCK_STS, 0));
}

TEST_CASE(a_servo_that_keeps_nothing_fails_the_read_back)
{
    const uint8_t mode1 = 1;
    ftsim_servo_t *v;

    setup(0);
    v = ftsim_add(&sim, 1, FEETECH_STS);
    /* The unlock is answered and not kept: the write is not kept either. */
    sim.stuck_lock = true;
    CHECK_EQ(feetech_bus_write_ee(&bus, FEETECH_STS, 1, FEETECH_REG_MODE,
                                  &mode1, 1), FEETECH_RES_OK);
    CHECK_EQ(run(100), FEETECH_RES_VERIFY);
    CHECK_EQ(sim.n_log, 4u);
    CHECK_EQ(v->regs[FEETECH_REG_MODE], 0);
    CHECK_EQ(bus.read_n, 1);
    CHECK_EQ(bus.read[0], 0);
}

TEST_CASE(a_failed_unlock_or_write_still_sends_the_lock)
{
    const uint8_t mode1 = 1;
    ftsim_servo_t *v;

    setup(0);
    v = ftsim_add(&sim, 1, FEETECH_STS);
    sim.drop = true;   /* the unlock's reply */
    CHECK_EQ(feetech_bus_write_ee(&bus, FEETECH_STS, 1, FEETECH_REG_MODE,
                                  &mode1, 1), FEETECH_RES_OK);
    CHECK_EQ(run(100), FEETECH_RES_NO_REPLY);
    /* Unlock, lock; no write. */
    CHECK_EQ(sim.n_log, 2u);
    CHECK(ftsim_is_write1(&sim.log[1], 1, FEETECH_REG_LOCK_STS, 1));
    CHECK_EQ(v->regs[FEETECH_REG_MODE], 0);
    CHECK_EQ(v->regs[FEETECH_REG_LOCK_STS], 1);
    CHECK_EQ(bus.unlocked, 0);

    sim.n_log = 0;
    CHECK_EQ(feetech_bus_write_ee(&bus, FEETECH_STS, 1, FEETECH_REG_MODE,
                                  &mode1, 1), FEETECH_RES_OK);
    CHECK_EQ(feetech_bus_step(&bus, now), FEETECH_RES_BUSY);
    now += 200u;
    sim.bad_checksum = false;
    (void)feetech_bus_step(&bus, now);     /* the unlock's reply */
    sim.bad_checksum = true;               /* the write's */
    now += 200u;
    CHECK_EQ(run(100), FEETECH_RES_CHECKSUM);
    CHECK_EQ(sim.n_log, 3u);
    CHECK(ftsim_is_write1(&sim.log[2], 1, FEETECH_REG_LOCK_STS, 1));
    CHECK_EQ(v->regs[FEETECH_REG_LOCK_STS], 1);

    /* A lock that is not answered: the servo may be unlocked. */
    sim.n_log = 0;
    CHECK_EQ(feetech_bus_write_ee(&bus, FEETECH_STS, 1, FEETECH_REG_MODE,
                                  &mode1, 1), FEETECH_RES_OK);
    for (unsigned i = 0; i < 10u && sim.n_log < 2u; ++i) {
        (void)feetech_bus_step(&bus, now);
        now += 200u;
    }
    CHECK_EQ(sim.n_log, 2u);
    sim.drop = true;   /* the lock's reply: the write's is computed */
    CHECK_EQ(run(100), FEETECH_RES_NO_REPLY);
    CHECK_EQ(bus.unlocked, 1);
    CHECK_EQ(bus.unlocked_id, 1);
}

TEST_CASE(a_step_the_driver_refuses_after_the_unlock_still_locks)
{
    const uint8_t mode1 = 1;
    ftsim_servo_t *v;

    setup(0);
    v = ftsim_add(&sim, 1, FEETECH_STS);
    CHECK_EQ(feetech_bus_write_ee(&bus, FEETECH_STS, 1, FEETECH_REG_MODE,
                                  &mode1, 1), FEETECH_RES_OK);
    (void)feetech_bus_step(&bus, now);     /* the unlock out */
    now += 200u;
    (void)feetech_bus_step(&bus, now);     /* its reply in */
    CHECK_EQ(v->regs[FEETECH_REG_LOCK_STS], 0);
    sim.refuse_n = 1;                      /* the write's start */
    CHECK_EQ(run(100), FEETECH_RES_DRIVER);
    CHECK_EQ(sim.n_log, 2u);
    CHECK(ftsim_is_write1(&sim.log[1], 1, FEETECH_REG_LOCK_STS, 1));
    CHECK_EQ(v->regs[FEETECH_REG_LOCK_STS], 1);
    CHECK_EQ(v->regs[FEETECH_REG_MODE], 0);
    CHECK_EQ(bus.unlocked, 0);
    /* The lock refused as well: it stays owed, the rate stays where the
     * servo answers, and STOP sends it. */
    sim.n_log = 0;
    CHECK_EQ(feetech_bus_write_ee(&bus, FEETECH_STS, 1, FEETECH_REG_MODE,
                                  &mode1, 1), FEETECH_RES_OK);
    (void)feetech_bus_step(&bus, now);
    now += 200u;
    (void)feetech_bus_step(&bus, now);
    sim.refuse_n = 2;                      /* the write's and the lock's */
    CHECK_EQ(run(100), FEETECH_RES_DRIVER);
    CHECK_EQ(sim.n_log, 1u);
    CHECK_EQ(bus.unlocked, 1);
    CHECK_EQ(v->regs[FEETECH_REG_LOCK_STS], 0);
    CHECK_EQ(feetech_bus_set_baud(&bus, 115200u), FEETECH_RES_BUSY);
    CHECK_EQ(sim.baud, 1000000u);
    feetech_bus_stop(&bus);
    (void)run(100);
    CHECK_EQ(bus.unlocked, 0);
    CHECK_EQ(v->regs[FEETECH_REG_LOCK_STS], 1);
    CHECK_EQ(feetech_bus_set_baud(&bus, 115200u), FEETECH_RES_OK);
    CHECK_EQ(sim.baud, 115200u);
}

/* ----------------------------------------------------------------- STOP */

TEST_CASE(stop_during_a_scan_ends_it_and_sends_torque_off)
{
    setup(0);
    ftsim_add(&sim, 1, FEETECH_STS);
    CHECK_EQ(feetech_bus_scan(&bus, 0, 253), FEETECH_RES_OK);
    for (unsigned i = 0; i < 10u; ++i) {
        (void)feetech_bus_step(&bus, now);
        now += 50u;
    }
    CHECK(feetech_bus_on_wire(&bus));
    {
        const unsigned before = sim.n_log;

        feetech_bus_stop(&bus);
        CHECK(feetech_bus_busy(&bus));
        CHECK_EQ(feetech_bus_scan(&bus, 0, 1), FEETECH_RES_BUSY);
        CHECK_EQ(run(100), FEETECH_RES_ABORTED);
        /* The PING on the wire finished; then one broadcast, no more. */
        CHECK_EQ(sim.n_log, before + 1u);
        CHECK(ftsim_is_write1(&sim.log[before], FEETECH_ID_BROADCAST,
                            FEETECH_REG_TORQUE, 0));
    }
    CHECK(bus.scan_at < 20u);
    /* The session takes the next operation. */
    CHECK_EQ(feetech_bus_read(&bus, 1, 56, 2), FEETECH_RES_OK);
    CHECK_EQ(run(100), FEETECH_RES_OK);
}

TEST_CASE(stop_after_the_unlock_relocks_the_servo)
{
    const uint8_t mode1 = 1;
    ftsim_servo_t *v;

    setup(0);
    v = ftsim_add(&sim, 1, FEETECH_STS);
    v->regs[FEETECH_REG_TORQUE] = 1;
    CHECK_EQ(feetech_bus_write_ee(&bus, FEETECH_STS, 1, FEETECH_REG_MODE,
                                  &mode1, 1), FEETECH_RES_OK);
    (void)feetech_bus_step(&bus, now);     /* the unlock out */
    now += 200u;
    (void)feetech_bus_step(&bus, now);     /* its reply in */
    CHECK_EQ(v->regs[FEETECH_REG_LOCK_STS], 0);
    CHECK(!feetech_bus_on_wire(&bus));
    feetech_bus_stop(&bus);
    CHECK_EQ(run(100), FEETECH_RES_ABORTED);
    /* Unlock, torque off, lock: the write never went. */
    CHECK_EQ(sim.n_log, 3u);
    CHECK(ftsim_is_write1(&sim.log[1], FEETECH_ID_BROADCAST,
                        FEETECH_REG_TORQUE, 0));
    CHECK(ftsim_is_write1(&sim.log[2], 1, FEETECH_REG_LOCK_STS, 1));
    CHECK_EQ(v->regs[FEETECH_REG_MODE], 0);
    CHECK_EQ(v->regs[FEETECH_REG_TORQUE], 0);
    CHECK_EQ(v->regs[FEETECH_REG_LOCK_STS], 1);
    CHECK_EQ(bus.unlocked, 0);
}

TEST_CASE(stop_while_an_id_write_is_on_the_wire_locks_the_new_id)
{
    const uint8_t id7 = 7;
    ftsim_servo_t *v;

    setup(0);
    v = ftsim_add(&sim, 1, FEETECH_STS);
    CHECK_EQ(feetech_bus_write_ee(&bus, FEETECH_STS, 1, FEETECH_REG_ID, &id7,
                                  1), FEETECH_RES_OK);
    (void)feetech_bus_step(&bus, now);     /* the unlock out */
    now += 200u;
    (void)feetech_bus_step(&bus, now);     /* its reply in */
    now += 200u;
    (void)feetech_bus_step(&bus, now);     /* the write out */
    CHECK(feetech_bus_on_wire(&bus));
    CHECK_EQ(sim.n_log, 2u);
    feetech_bus_stop(&bus);
    CHECK_EQ(run(100), FEETECH_RES_ABORTED);
    CHECK_EQ(v->regs[FEETECH_REG_ID], 7);
    CHECK_EQ(sim.n_log, 4u);
    CHECK(ftsim_is_write1(&sim.log[3], 7, FEETECH_REG_LOCK_STS, 1));
    CHECK_EQ(v->regs[FEETECH_REG_LOCK_STS], 1);
}

TEST_CASE(stop_while_a_rate_write_is_on_the_wire_follows_the_rate)
{
    const uint8_t baud7 = 7;
    ftsim_servo_t *v;

    setup(0);
    v = ftsim_add(&sim, 1, FEETECH_STS);
    CHECK_EQ(feetech_bus_write_ee(&bus, FEETECH_STS, 1, FEETECH_REG_BAUD,
                                  &baud7, 1), FEETECH_RES_OK);
    (void)feetech_bus_step(&bus, now);
    now += 200u;
    (void)feetech_bus_step(&bus, now);
    now += 200u;
    (void)feetech_bus_step(&bus, now);
    CHECK(feetech_bus_on_wire(&bus));
    feetech_bus_stop(&bus);
    CHECK_EQ(run(100), FEETECH_RES_ABORTED);
    CHECK_EQ(bus.baud, 38400u);
    /* Torque off at the old rate and the new one, then the lock. */
    CHECK_EQ(sim.n_log, 5u);
    CHECK(ftsim_is_write1(&sim.log[2], FEETECH_ID_BROADCAST,
                        FEETECH_REG_TORQUE, 0));
    CHECK_EQ(sim.log_baud[2], 1000000u);
    CHECK(ftsim_is_write1(&sim.log[3], FEETECH_ID_BROADCAST,
                        FEETECH_REG_TORQUE, 0));
    CHECK_EQ(sim.log_baud[3], 38400u);
    CHECK(ftsim_is_write1(&sim.log[4], 1, FEETECH_REG_LOCK_STS, 1));
    CHECK_EQ(v->regs[FEETECH_REG_LOCK_STS], 1);
}

TEST_CASE(stop_with_nothing_running_sends_only_torque_off)
{
    setup(0);
    ftsim_add(&sim, 1, FEETECH_STS)->regs[FEETECH_REG_TORQUE] = 1;
    feetech_bus_stop(&bus);
    CHECK_EQ(run(100), FEETECH_RES_OK);
    CHECK_EQ(sim.n_log, 1u);
    CHECK_EQ(ftsim_find(&sim, 1)->regs[FEETECH_REG_TORQUE], 0);
    CHECK_EQ(bus.stop_res, FEETECH_RES_OK);
    /* A driver that refuses twice: the third start sends torque off. */
    ftsim_find(&sim, 1)->regs[FEETECH_REG_TORQUE] = 1;
    sim.refuse_n = FEETECH_BUS_STOP_TRIES - 1u;
    feetech_bus_stop(&bus);
    CHECK_EQ(run(100), FEETECH_RES_OK);
    CHECK_EQ(sim.n_log, 2u);
    CHECK_EQ(ftsim_find(&sim, 1)->regs[FEETECH_REG_TORQUE], 0);
    CHECK_EQ(bus.stop_res, FEETECH_RES_OK);
    /* A driver that cannot start: FEETECH_BUS_STOP_TRIES starts, then the
     * stop ends and says so. */
    sim.refuse_start = true;
    {
        const unsigned starts = sim.n_start;

        feetech_bus_stop(&bus);
        CHECK_EQ(run(100), FEETECH_RES_OK);
        CHECK(!feetech_bus_busy(&bus));
        CHECK_EQ(sim.n_start - starts, FEETECH_BUS_STOP_TRIES);
        CHECK_EQ(bus.stop_res, FEETECH_RES_DRIVER);
    }
    /* The next stop starts clean. */
    sim.refuse_start = false;
    feetech_bus_stop(&bus);
    CHECK_EQ(bus.stop_res, FEETECH_RES_OK);
    CHECK_EQ(run(100), FEETECH_RES_OK);
    CHECK_EQ(bus.stop_res, FEETECH_RES_OK);
}

TEST_CASE(a_lock_the_stop_cannot_start_stays_owed)
{
    const uint8_t mode1 = 1;
    ftsim_servo_t *v;

    setup(0);
    v = ftsim_add(&sim, 1, FEETECH_STS);
    CHECK_EQ(feetech_bus_write_ee(&bus, FEETECH_STS, 1, FEETECH_REG_MODE,
                                  &mode1, 1), FEETECH_RES_OK);
    (void)feetech_bus_step(&bus, now);     /* the unlock out */
    now += 200u;
    (void)feetech_bus_step(&bus, now);     /* its reply in */
    CHECK_EQ(v->regs[FEETECH_REG_LOCK_STS], 0);
    feetech_bus_stop(&bus);
    /* Torque off goes; the lock's starts are all refused. */
    now += 200u;
    (void)feetech_bus_step(&bus, now);
    CHECK_EQ(sim.n_log, 2u);
    now += 200u;
    (void)feetech_bus_step(&bus, now);
    sim.refuse_start = true;
    CHECK_EQ(run(100), FEETECH_RES_ABORTED);
    CHECK_EQ(bus.stop_res, FEETECH_RES_DRIVER);
    CHECK_EQ(bus.unlocked, 1);
    CHECK_EQ(v->regs[FEETECH_REG_LOCK_STS], 0);
    /* The next stop sends it. */
    sim.refuse_start = false;
    feetech_bus_stop(&bus);
    (void)run(100);
    CHECK_EQ(bus.stop_res, FEETECH_RES_OK);
    CHECK_EQ(bus.unlocked, 0);
    CHECK_EQ(v->regs[FEETECH_REG_LOCK_STS], 1);
}

TEST_CASE(a_rate_the_stop_cannot_follow_says_driver)
{
    const uint8_t baud7 = 7;

    setup(0);
    ftsim_add(&sim, 1, FEETECH_STS);
    CHECK_EQ(feetech_bus_write_ee(&bus, FEETECH_STS, 1, FEETECH_REG_BAUD,
                                  &baud7, 1), FEETECH_RES_OK);
    (void)feetech_bus_step(&bus, now);
    now += 200u;
    (void)feetech_bus_step(&bus, now);
    now += 200u;
    (void)feetech_bus_step(&bus, now);
    CHECK(feetech_bus_on_wire(&bus));
    feetech_bus_stop(&bus);
    sim.refuse_baud = true;
    CHECK_EQ(run(100), FEETECH_RES_ABORTED);
    CHECK_EQ(bus.baud, 1000000u);
    CHECK_EQ(bus.stop_res, FEETECH_RES_DRIVER);
}

/* ---------------------------------------------------------------- watch */

TEST_CASE(the_watch_reads_the_live_block_every_50_ms_between_operations)
{
    ftsim_servo_t *v;
    feetech_live_t live;

    setup(0);
    v = ftsim_add(&sim, 1, FEETECH_STS);
    feetech_put16(FEETECH_STS, &v->regs[56], 2048);
    v->regs[69] = 100;
    v->regs[FEETECH_REG_STATUS] = 0x04;
    feetech_bus_watch(&bus, 1, FEETECH_STS);
    (void)run(5);
    CHECK_EQ(bus.watch_ok, 1);
    CHECK_EQ(bus.live_n, FEETECH_LIVE_STS);
    /* The last good reply is the watch's. */
    CHECK_EQ(bus.rx_id, 1);
    CHECK_EQ(bus.rx_error, 0x04);
    v->regs[FEETECH_REG_STATUS] = 0;
    CHECK(feetech_live(FEETECH_STS, bus.live, bus.live_n, &live));
    CHECK_EQ(live.position, 2048);
    CHECK_EQ(live.voltage_dv, 74);
    CHECK_EQ(feetech_current_ma(live.current_raw), 650u);
    /* Nothing until 50 ms after the read. */
    for (unsigned i = 0; i < 480u; ++i) {
        (void)feetech_bus_step(&bus, now);
        now += 100u;
    }
    CHECK_EQ(bus.watch_ok, 1);
    for (unsigned i = 0; i < 30u; ++i) {
        (void)feetech_bus_step(&bus, now);
        now += 100u;
    }
    CHECK_EQ(bus.watch_ok, 2);
    /* An operation goes before the next read. */
    CHECK_EQ(feetech_bus_read(&bus, 1, 5, 1), FEETECH_RES_OK);
    CHECK_EQ(run(10), FEETECH_RES_OK);
    CHECK_EQ(bus.read[0], 1);
    /* A servo that stops answering counts bad reads and empties the block:
     * the last good one is not shown as live. */
    v->present = false;
    bus.watch_now = 1;
    (void)run(20);
    CHECK_EQ(bus.watch_bad, 1);
    CHECK_EQ(bus.watch_res, FEETECH_RES_NO_REPLY);
    CHECK_EQ(bus.live_n, 0);
    /* It answers again: the next read fills it. */
    v->present = true;
    bus.watch_now = 1;
    (void)run(20);
    CHECK_EQ(bus.watch_ok, 3);
    CHECK_EQ(bus.watch_res, FEETECH_RES_OK);
    CHECK_EQ(bus.live_n, FEETECH_LIVE_STS);
    /* SCS reads 11 bytes; an ID above 253 ends the watch. */
    feetech_bus_watch(&bus, 2, FEETECH_SCS);
    CHECK_EQ(bus.live_n, 0);
    (void)feetech_bus_step(&bus, now);
    CHECK_EQ(sim.log[sim.n_log - 1u].bytes[6], FEETECH_LIVE_SCS);
    now += 2000u;
    (void)run(20);
    feetech_bus_watch(&bus, 254, FEETECH_SCS);
    CHECK_EQ(bus.watch_id, 0xFF);
}

/* @p n passes, 100 us apart. */
static void steps(unsigned n)
{
    for (unsigned i = 0; i < n; ++i) {
        (void)feetech_bus_step(&bus, now);
        now += 100u;
    }
}

TEST_CASE(a_read_on_the_wire_for_a_watch_since_changed_is_dropped)
{
    ftsim_servo_t *v1;
    ftsim_servo_t *v2;

    setup(0);
    v1 = ftsim_add(&sim, 1, FEETECH_STS);
    v2 = ftsim_add(&sim, 2, FEETECH_STS);
    feetech_put16(FEETECH_STS, &v1->regs[56], 1000);
    feetech_put16(FEETECH_STS, &v2->regs[56], 3000);
    feetech_bus_watch(&bus, 1, FEETECH_STS);
    (void)feetech_bus_step(&bus, now);     /* servo 1's read out */
    CHECK(feetech_bus_on_wire(&bus));
    feetech_bus_watch(&bus, 2, FEETECH_STS);
    now += 300u;
    (void)feetech_bus_step(&bus, now);     /* servo 1's reply in */
    CHECK_EQ(bus.live_n, 0);
    CHECK_EQ(bus.watch_ok, 0);
    CHECK_EQ(bus.watch_bad, 0);
    /* The next pass reads servo 2 at once. */
    steps(20);
    CHECK_EQ(sim.log[sim.n_log - 1u].bytes[2], 2);
    CHECK_EQ(bus.watch_ok, 1);
    CHECK_EQ(feetech_get16(FEETECH_STS, &bus.live[0]), 3000);
    /* The series changed mid-read: dropped as well. */
    bus.watch_now = 1;
    (void)feetech_bus_step(&bus, now);
    CHECK(feetech_bus_on_wire(&bus));
    feetech_bus_watch(&bus, 2, FEETECH_SCS);
    now += 300u;
    (void)feetech_bus_step(&bus, now);
    CHECK_EQ(bus.live_n, 0);
    CHECK_EQ(bus.watch_ok, 1);
    /* The watch ended mid-read: nothing kept. */
    steps(20);
    bus.watch_now = 1;
    (void)feetech_bus_step(&bus, now);
    CHECK(feetech_bus_on_wire(&bus));
    feetech_bus_watch(&bus, 254, FEETECH_SCS);
    now += 300u;
    (void)feetech_bus_step(&bus, now);
    CHECK_EQ(bus.watch_drop, 0);
    CHECK_EQ(bus.watch_id, 0xFF);
}

TEST_CASE(init_refuses_a_driver_without_a_function)
{
    feetech_driver_t d;
    feetech_bus_t b;

    ftsim_init(&sim, 1000000u);
    d = ftsim_driver(&sim);
    d.poll = NULL;
    CHECK(!feetech_bus_init(&b, &d, 1000000u));
    d = ftsim_driver(&sim);
    CHECK(!feetech_bus_init(&b, &d, 0));
    CHECK(!feetech_bus_init(&b, NULL, 1000000u));
    CHECK(feetech_bus_init(&b, &d, 1000000u));
    CHECK_EQ(b.watch_id, 0xFF);
}

int main(void)
{
    RUN(a_scan_finds_the_servos_at_both_ends_of_the_id_range);
    RUN(a_scan_range_beyond_253_or_backwards_is_refused);
    RUN(two_servos_on_one_id_are_counted_not_found);
    RUN(a_servo_at_another_rate_is_not_found_until_the_line_moves);
    RUN(a_read_gives_the_bytes_and_the_err_byte);
    RUN(a_read_or_write_outside_the_limits_is_refused);
    RUN(every_reply_fault_comes_back_as_its_result);
    RUN(a_driver_that_hears_itself_keeps_the_echo_and_the_whole_reply);
    RUN(a_driver_that_does_not_start_or_does_not_end_is_driver);
    RUN(the_reply_wait_is_the_window_and_twice_the_reply);
    RUN(the_gap_between_transactions_holds_across_a_clock_wrap);
    RUN(an_eeprom_write_unlocks_writes_locks_and_reads_back);
    RUN(an_scs_eeprom_write_puts_the_high_byte_first_and_uses_lock_48);
    RUN(a_new_id_and_a_new_rate_are_followed);
    RUN(an_id_write_that_never_arrived_is_locked_at_the_old_id);
    RUN(a_rate_write_that_never_arrived_puts_the_line_back);
    RUN(an_id_write_answered_only_by_its_lock_moved);
    RUN(the_lock_flag_alone_is_one_write_and_its_read);
    RUN(the_lock_flag_written_on_purpose_settles_what_is_owed);
    RUN(a_lock_owed_to_one_servo_goes_before_a_write_to_another);
    RUN(a_servo_that_keeps_nothing_fails_the_read_back);
    RUN(a_failed_unlock_or_write_still_sends_the_lock);
    RUN(a_step_the_driver_refuses_after_the_unlock_still_locks);
    RUN(stop_during_a_scan_ends_it_and_sends_torque_off);
    RUN(stop_after_the_unlock_relocks_the_servo);
    RUN(stop_while_an_id_write_is_on_the_wire_locks_the_new_id);
    RUN(stop_while_a_rate_write_is_on_the_wire_follows_the_rate);
    RUN(stop_with_nothing_running_sends_only_torque_off);
    RUN(a_lock_the_stop_cannot_start_stays_owed);
    RUN(a_rate_the_stop_cannot_follow_says_driver);
    RUN(the_watch_reads_the_live_block_every_50_ms_between_operations);
    RUN(a_read_on_the_wire_for_a_watch_since_changed_is_dropped);
    RUN(init_refuses_a_driver_without_a_function);
    return test_summary("feetech_bus");
}
