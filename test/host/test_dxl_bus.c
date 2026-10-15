/*
 * Transactions and the ID scan on a bus of ROBOTIS Dynamixel servos
 * (protocols/dynamixel/dxl_bus.c), against the modelled bus of dxl_sim.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "dxl_bus.h"
#include "dxl_packet.h"
#include "dxl_sim.h"
#include "dxl_table.h"

static sim_t sim;
static dxl_bus_t bus;
static dxl_driver_t drv;

/* A bus at @p baud_index with one XM430-W210 at ID 1 on it. */
static sim_servo_t *setup(uint8_t baud_index)
{
    memset(&sim, 0, sizeof(sim));
    sim.now = 1000;
    drv = sim_driver(&sim);
    dxl_bus_init(&bus, &drv);
    if (dxl_bus_set_baud(&bus, dxl_baud_rate(baud_index)) != DXL_OK) {
        T_FAIL("rate index %u refused", baud_index);
    }
    return sim_add(&sim, 1, 1030, baud_index);
}

/* Step the bus, the clock moving @p tick us a step, until it ends. */
static dxl_res_t run_ticks(uint32_t tick)
{
    dxl_res_t r = DXL_BUSY;

    for (unsigned i = 0; i < 2000000u && r == DXL_BUSY; ++i) {
        r = dxl_bus_step(&bus);
        if (r == DXL_BUSY) {
            sim.now += tick;
        }
    }
    return r;
}

static dxl_res_t run(void)
{
    return run_ticks(7);
}

TEST_CASE(a_ping_returns_the_model_and_firmware_at_every_rate)
{
    for (uint8_t b = 0; b < DXL_BAUD_COUNT; ++b) {
        setup(b);
        CHECK_EQ(dxl_bus_ping(&bus, 1), DXL_BUSY);
        CHECK(dxl_bus_busy(&bus));
        CHECK_EQ(run(), DXL_OK);
        CHECK_EQ(bus.status.n_params, 3u);
        CHECK_EQ(dxl_get_u16(bus.status.params), 1030u);
        CHECK_EQ(bus.status.params[2], 45u);
        CHECK_EQ(sim.stops, 1u);
        CHECK(!dxl_bus_busy(&bus));
        /* The result holds until the next transaction. */
        CHECK_EQ(dxl_bus_step(&bus), DXL_OK);
        /* The reply came before the deadline, after the return delay. */
        CHECK(bus.t_reply_us < bus.timeout_us);
        CHECK(bus.t_reply_us >= 500u);
    }
}

TEST_CASE(a_read_returns_the_bytes_of_the_table)
{
    sim_servo_t *v = setup(1);

    v->table[132] = 0x34;
    v->table[133] = 0x12;
    CHECK_EQ(dxl_bus_read(&bus, 1, 132, 4), DXL_BUSY);
    CHECK_EQ(run(), DXL_OK);
    CHECK_EQ(bus.status.n_params, 4u);
    CHECK_EQ(dxl_get_u16(bus.status.params), 0x1234u);

    /* The longest read: 128 bytes, the first 64 the EEPROM area. */
    CHECK_EQ(dxl_bus_read(&bus, 1, 0, DXL_READ_MAX), DXL_BUSY);
    CHECK_EQ(run(), DXL_OK);
    CHECK_EQ(bus.status.n_params, DXL_READ_MAX);
    CHECK_EQ(dxl_get_u16(bus.status.params), 1030u);
    CHECK_EQ(bus.status.params[7], 1u);

    /* Lengths 0 and above DXL_READ_MAX are refused, nothing sent. */
    const unsigned starts = sim.starts;
    CHECK_EQ(dxl_bus_read(&bus, 1, 0, 0), DXL_ERR_ARG);
    CHECK_EQ(dxl_bus_read(&bus, 1, 0, DXL_READ_MAX + 1u), DXL_ERR_ARG);
    CHECK_EQ(dxl_bus_read(&bus, DXL_ID_BROADCAST, 0, 4), DXL_ERR_ARG);
    CHECK_EQ(sim.starts, starts);
}

TEST_CASE(a_write_lands_in_the_table_and_is_answered)
{
    sim_servo_t *v = setup(1);
    const uint8_t goal[4] = { 0x00, 0x02, 0x00, 0x00 };

    CHECK_EQ(dxl_bus_write(&bus, 1, 116, goal, 4), DXL_BUSY);
    CHECK_EQ(run(), DXL_OK);
    CHECK_EQ(v->table[117], 0x02u);
    CHECK_EQ(bus.status.n_params, 0u);
    /* The packet on the wire is the e-Manual's write example. */
    const uint8_t want[] = { 0xFF, 0xFF, 0xFD, 0x00, 0x01, 0x09, 0x00, 0x03,
                             0x74, 0x00, 0x00, 0x02, 0x00, 0x00, 0xCA, 0x89 };
    CHECK_EQ(sim.n_sent, sizeof(want));
    CHECK(memcmp(sim.sent, want, sizeof(want)) == 0);

    CHECK_EQ(dxl_bus_reg_write(&bus, 1, 104, goal, 4), DXL_BUSY);
    CHECK_EQ(run(), DXL_OK);
    CHECK_EQ(dxl_bus_action(&bus, 1), DXL_BUSY);
    CHECK_EQ(run(), DXL_OK);
    CHECK_EQ(dxl_bus_reboot(&bus, 1), DXL_BUSY);
    CHECK_EQ(run(), DXL_OK);
    CHECK_EQ(v->reboots, 1u);

    CHECK_EQ(dxl_bus_write(&bus, 1, 116, goal, 0), DXL_ERR_ARG);
    CHECK_EQ(dxl_bus_write(&bus, 1, 116, NULL, 4), DXL_ERR_ARG);
}

TEST_CASE(factory_reset_needs_the_confirmation_and_one_servo)
{
    const sim_servo_t *v = setup(1);

    CHECK_EQ(dxl_bus_factory_reset(&bus, 1, DXL_RESET_ALL_BUT_ID, 0),
             DXL_ERR_ARG);
    CHECK_EQ(dxl_bus_factory_reset(&bus, DXL_ID_BROADCAST,
                                   DXL_RESET_ALL_BUT_ID,
                                   DXL_FACTORY_RESET_CONFIRM),
             DXL_ERR_ARG);
    CHECK_EQ(dxl_bus_factory_reset(&bus, 1, 0x33, DXL_FACTORY_RESET_CONFIRM),
             DXL_ERR_ARG);
    CHECK_EQ(sim.starts, 0u);
    CHECK_EQ(dxl_bus_factory_reset(&bus, 1, DXL_RESET_ALL_BUT_ID,
                                   DXL_FACTORY_RESET_CONFIRM),
             DXL_BUSY);
    CHECK_EQ(run(), DXL_OK);
    CHECK_EQ(v->resets, 1u);
}

TEST_CASE(broadcast_writes_and_sync_write_take_no_reply)
{
    setup(1);
    sim_add(&sim, 2, 1060, 1);
    const uint8_t on = 1;

    CHECK_EQ(dxl_bus_write(&bus, DXL_ID_BROADCAST, 65, &on, 1), DXL_BUSY);
    const uint32_t t0 = sim.now;
    CHECK_EQ(run_ticks(1), DXL_OK);
    CHECK_EQ(sim.servo[0].table[65], 1u);
    CHECK_EQ(sim.servo[1].table[65], 1u);
    /* No reply due: the deadline is the packet's own time and the margin. */
    CHECK_EQ(sim.now - t0, dxl_deadline_us(57600, 0, sim.n_sent, 0));

    const uint8_t ids[2] = { 1, 2 };
    const uint8_t data[8] = { 0x96, 0, 0, 0, 0xAA, 0, 0, 0 };
    CHECK_EQ(dxl_bus_sync_write(&bus, 116, 4, ids, data, 2), DXL_BUSY);
    CHECK_EQ(run(), DXL_OK);
    CHECK_EQ(sim.n_sent, 24u);
    /* A servo's bytes on a no-reply transaction are counted as noise. */
    CHECK_EQ(bus.noise, 0u);
}

TEST_CASE(status_return_level_decides_which_replies_are_waited_for)
{
    sim_servo_t *v = setup(1);
    const uint8_t x = 1;

    v->srl = 1;
    dxl_bus_set_status_return(&bus, 1);
    CHECK_EQ(dxl_bus_write(&bus, 1, 65, &x, 1), DXL_BUSY);
    CHECK_EQ(run(), DXL_OK);
    CHECK_EQ(dxl_bus_read(&bus, 1, 65, 1), DXL_BUSY);
    CHECK_EQ(run(), DXL_OK);
    CHECK_EQ(bus.status.params[0], 1u);

    v->srl = 0;
    dxl_bus_set_status_return(&bus, 0);
    /* Level 0: a read is sent, no reply is waited for. */
    CHECK_EQ(dxl_bus_read(&bus, 1, 65, 1), DXL_BUSY);
    CHECK_EQ(run(), DXL_OK);
    CHECK_EQ(bus.status.n_params, 0u);
    /* PING is always answered. */
    CHECK_EQ(dxl_bus_ping(&bus, 1), DXL_BUSY);
    CHECK_EQ(run(), DXL_OK);

    /* A servo at level 2 the bus thinks is at 1: the write reply is noise. */
    v->srl = 2;
    dxl_bus_set_status_return(&bus, 1);
    CHECK_EQ(dxl_bus_write(&bus, 1, 65, &x, 1), DXL_BUSY);
    CHECK_EQ(run(), DXL_OK);
    CHECK(bus.noise > 0u);

    /* Level 3 is not a level: the last one stays. */
    dxl_bus_set_status_return(&bus, 3);
    CHECK_EQ(bus.status_return, 1u);
}

TEST_CASE(a_write_to_id_takes_the_reply_from_the_old_or_the_new_id)
{
    for (int from_new = 0; from_new < 2; ++from_new) {
        sim_servo_t *v = setup(1);
        const uint8_t id = 9;

        v->reply_new_id = (uint8_t)from_new;
        CHECK_EQ(dxl_bus_write(&bus, 1, 7, &id, 1), DXL_BUSY);
        CHECK_EQ(run(), DXL_OK);
        CHECK_EQ(v->id, 9u);
        CHECK_EQ(bus.status.id, from_new ? 9u : 1u);
        /* Any other write takes the asked ID only. */
        sim.fault = SIM_FAULT_WRONG_ID;
        sim.fault_arg = 9;
        CHECK_EQ(dxl_bus_write(&bus, 9, 65, &id, 1), DXL_BUSY);
        CHECK_EQ(run(), DXL_OK);
        sim.fault_arg = 7;
        CHECK_EQ(dxl_bus_write(&bus, 9, 65, &id, 1), DXL_BUSY);
        CHECK_EQ(run(), DXL_ERR_ID);
    }
}

TEST_CASE(a_broken_reply_has_its_own_result)
{
    static const struct {
        sim_fault_t fault;
        uint32_t arg;
        dxl_res_t want;
    } k[] = {
        { SIM_FAULT_SILENT, 0, DXL_ERR_TIMEOUT },
        { SIM_FAULT_TRUNCATE, 1, DXL_ERR_TRUNCATED },
        { SIM_FAULT_TRUNCATE, 10, DXL_ERR_TRUNCATED },
        { SIM_FAULT_CRC, 0, DXL_ERR_CRC },
        { SIM_FAULT_WRONG_ID, 2, DXL_ERR_ID },
        { SIM_FAULT_WRONG_ID, 252, DXL_ERR_ID },
        { SIM_FAULT_WRONG_ID, 0, DXL_ERR_ID },
        { SIM_FAULT_OVERSIZE, 0, DXL_ERR_LENGTH },
        { SIM_FAULT_NOT_STATUS, 0, DXL_ERR_NOT_STATUS },
        { SIM_FAULT_ERR, 0x02, DXL_ERR_STATUS },
        { SIM_FAULT_ERR, 0x87, DXL_ERR_STATUS },
        { SIM_FAULT_ERR, 0x80, DXL_OK },
        { SIM_FAULT_PARAMS, 0, DXL_ERR_PARAMS },
        { SIM_FAULT_LATE, 2000, DXL_ERR_TRUNCATED },
        { SIM_FAULT_LATE, 20000, DXL_ERR_TIMEOUT },
        { SIM_FAULT_NOISE, 5, DXL_OK },
        { SIM_FAULT_EXTRA, 3, DXL_OK },
    };

    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        setup(1);
        sim.fault = k[i].fault;
        sim.fault_arg = k[i].arg;
        CHECK_EQ(dxl_bus_ping(&bus, 1), DXL_BUSY);
        const dxl_res_t r = run();
        if (r != k[i].want) {
            T_FAIL("fault %d arg %u: result %d, want %d", (int)k[i].fault,
                   (unsigned)k[i].arg, (int)r, (int)k[i].want);
        }
        CHECK_EQ(sim.stops, 1u);
        CHECK(!dxl_bus_busy(&bus));
    }
    /* The noise in front is counted. */
    setup(1);
    sim.fault = SIM_FAULT_NOISE;
    sim.fault_arg = 5;
    CHECK_EQ(dxl_bus_ping(&bus, 1), DXL_BUSY);
    CHECK_EQ(run(), DXL_OK);
    CHECK_EQ(bus.noise, 5u);
    /* The alert bit alone passes, and is in the status. */
    setup(1);
    sim.fault = SIM_FAULT_ERR;
    sim.fault_arg = 0x80;
    CHECK_EQ(dxl_bus_ping(&bus, 1), DXL_BUSY);
    CHECK_EQ(run(), DXL_OK);
    CHECK(dxl_err_alert(bus.status.error));
}

TEST_CASE(the_reply_may_come_one_byte_a_poll)
{
    for (size_t chunk = 1; chunk <= 3u; ++chunk) {
        sim_servo_t *v = setup(3);

        /* FF FF FD in the parameters: the reply is stuffed. */
        v->table[20] = 0xFF;
        v->table[21] = 0xFF;
        v->table[22] = 0xFD;
        v->table[23] = 0x07;
        sim.chunk = chunk;
        CHECK_EQ(dxl_bus_read(&bus, 1, 20, 4), DXL_BUSY);
        CHECK_EQ(run_ticks(1), DXL_OK);
        CHECK_EQ(bus.status.n_params, 4u);
        CHECK_EQ(bus.status.params[2], 0xFDu);
        CHECK_EQ(bus.status.params[3], 0x07u);
        CHECK_EQ(bus.status.wire_len, 16u);
    }
}

/* The deadline, at every rate: BUSY 1 us before it, TIMEOUT at it. */
TEST_CASE(a_silent_servo_times_out_at_the_deadline_at_every_rate)
{
    for (uint8_t b = 0; b < DXL_BAUD_COUNT; ++b) {
        const uint32_t rate = dxl_baud_rate(b);

        setup(b);
        sim.fault = SIM_FAULT_SILENT;
        CHECK_EQ(dxl_bus_ping(&bus, 1), DXL_BUSY);
        const uint32_t t0 = sim.now;
        const uint32_t d = dxl_deadline_us(rate, DXL_RETURN_DELAY_MAX_US, 10,
                                           dxl_reply_max(3));
        CHECK_EQ(bus.timeout_us, d);
        sim.now = t0 + d - 1u;
        CHECK_EQ(dxl_bus_step(&bus), DXL_BUSY);
        sim.now = t0 + d;
        CHECK_EQ(dxl_bus_step(&bus), DXL_ERR_TIMEOUT);
    }
    /* The numbers at the ends: 9,600 and 4,500,000 bit/s.  Ping 10 bytes
     * sent, reply 15 bytes at most (14, and 1 for stuffing). */
    CHECK_EQ(dxl_reply_max(3), 15u);
    /* 20 + 110 = 130 bits = 13,542 us; 150 bits = 15,625 us. */
    CHECK_EQ(dxl_deadline_us(9600, 508, 10, 15), 13542u + 508u + 15625u
                                                     + 1000u);
    /* 130 bits = 28.9 us -> 29; 150 bits = 33.3 us -> 34. */
    CHECK_EQ(dxl_deadline_us(4500000, 0, 10, 15), 29u + 34u + 1000u);
    CHECK_EQ(dxl_deadline_us(0, 0, 10, 14), 0u);
}

TEST_CASE(the_deadline_holds_across_the_clock_s_wrap)
{
    setup(1);
    sim.now = 0xFFFFFF00u;
    sim.fault = SIM_FAULT_SILENT;
    CHECK_EQ(dxl_bus_ping(&bus, 1), DXL_BUSY);
    const uint32_t t0 = sim.now;
    sim.now = t0 + bus.timeout_us - 1u;
    CHECK(sim.now < t0);
    CHECK_EQ(dxl_bus_step(&bus), DXL_BUSY);
    sim.now++;
    CHECK_EQ(dxl_bus_step(&bus), DXL_ERR_TIMEOUT);

    /* A reply across the wrap. */
    setup(1);
    sim.now = 0xFFFFFFF0u;
    CHECK_EQ(dxl_bus_ping(&bus, 1), DXL_BUSY);
    CHECK_EQ(run(), DXL_OK);
}

TEST_CASE(a_shorter_return_delay_shortens_the_deadline)
{
    setup(1);
    const uint32_t d508 = dxl_deadline_us(57600, 508, 10, dxl_reply_max(3));

    dxl_bus_set_return_delay(&bus, 0);
    CHECK_EQ(dxl_bus_ping(&bus, 1), DXL_BUSY);
    CHECK_EQ(bus.timeout_us, d508 - 508u);
    dxl_bus_abort(&bus);
    /* The servo's own 500 us is longer than the 0 the bus assumes; the
     * margin of 1,000 us takes it. */
    sim.now += 10000u;
    CHECK_EQ(dxl_bus_ping(&bus, 1), DXL_BUSY);
    CHECK_EQ(run(), DXL_OK);
    /* A reply that starts after the deadline is no reply. */
    sim.fault = SIM_FAULT_LATE;
    sim.fault_arg = 5000u;
    CHECK_EQ(dxl_bus_ping(&bus, 1), DXL_BUSY);
    CHECK_EQ(run(), DXL_ERR_TIMEOUT);
    dxl_bus_set_return_delay(&bus, 250);
    CHECK_EQ(bus.return_delay_us, 500u);
    dxl_bus_set_return_delay(&bus, 255);
    CHECK_EQ(bus.return_delay_us, 500u);
}

TEST_CASE(one_transaction_at_a_time_and_abort_ends_it)
{
    setup(1);
    const uint8_t x = 0;

    CHECK_EQ(dxl_bus_step(&bus), DXL_ERR_STATE);
    CHECK_EQ(dxl_bus_ping(&bus, 1), DXL_BUSY);
    CHECK_EQ(dxl_bus_ping(&bus, 1), DXL_ERR_STATE);
    CHECK_EQ(dxl_bus_read(&bus, 1, 0, 2), DXL_ERR_STATE);
    CHECK_EQ(dxl_bus_write(&bus, 1, 65, &x, 1), DXL_ERR_STATE);
    CHECK_EQ(dxl_bus_set_baud(&bus, 1000000), DXL_ERR_STATE);
    CHECK_EQ(sim.starts, 1u);
    dxl_bus_abort(&bus);
    CHECK_EQ(dxl_bus_step(&bus), DXL_ERR_ABORTED);
    CHECK_EQ(sim.stops, 1u);
    dxl_bus_abort(&bus);
    CHECK_EQ(sim.stops, 1u);

    /* A driver that refuses the start. */
    sim.refuse_start = true;
    CHECK_EQ(dxl_bus_ping(&bus, 1), DXL_ERR_DRIVER);
    CHECK(!dxl_bus_busy(&bus));
    CHECK_EQ(dxl_bus_step(&bus), DXL_ERR_DRIVER);
    sim.refuse_start = false;

    /* No rate set yet. */
    dxl_bus_init(&bus, &drv);
    CHECK_EQ(dxl_bus_ping(&bus, 1), DXL_ERR_DRIVER);
    /* A rate that is not one of the 8; one the driver refuses. */
    CHECK_EQ(dxl_bus_set_baud(&bus, 19200), DXL_ERR_ARG);
    sim.refuse_mask = 0x80;
    CHECK_EQ(dxl_bus_set_baud(&bus, 4500000), DXL_ERR_DRIVER);
    CHECK_EQ(bus.baud, 0u);
    /* A driver without functions. */
    dxl_bus_init(&bus, NULL);
    CHECK_EQ(dxl_bus_set_baud(&bus, 57600), DXL_ERR_DRIVER);
    CHECK_EQ(dxl_bus_step(NULL), DXL_ERR_ARG);
    CHECK(!dxl_bus_busy(NULL));
    CHECK_EQ(dxl_bus_ping(NULL, 1), DXL_ERR_ARG);
}

TEST_CASE(ping_refuses_the_broadcast_and_non_ids)
{
    setup(1);
    CHECK_EQ(dxl_bus_ping(&bus, 253), DXL_ERR_ARG);
    CHECK_EQ(dxl_bus_ping(&bus, DXL_ID_BROADCAST), DXL_ERR_ARG);
    CHECK_EQ(dxl_bus_ping(&bus, 255), DXL_ERR_ARG);
    CHECK_EQ(sim.starts, 0u);
    CHECK_EQ(dxl_bus_ping(&bus, 252), DXL_BUSY);
    CHECK_EQ(run(), DXL_ERR_TIMEOUT);
    CHECK_EQ(dxl_bus_ping(&bus, 0), DXL_BUSY);
    CHECK_EQ(run(), DXL_ERR_TIMEOUT);
}

TEST_CASE(a_parser_refusal_ends_the_transaction_with_its_code)
{
    sim_servo_t *v = setup(1);

    v->table[20] = 0xFF;
    v->table[21] = 0xFF;
    v->table[22] = 0xFD;
    v->table[23] = 0x07;
    sim.fault = SIM_FAULT_UNSTUFFED;
    CHECK_EQ(dxl_bus_read(&bus, 1, 20, 4), DXL_BUSY);
    CHECK_EQ(run(), DXL_ERR_STUFFING);
    sim.fault = SIM_FAULT_ID253;
    CHECK_EQ(dxl_bus_read(&bus, 1, 20, 4), DXL_BUSY);
    CHECK_EQ(run(), DXL_ERR_ID);
}

TEST_CASE(bytes_behind_the_reply_in_one_poll_are_counted_as_extra)
{
    setup(1);
    sim.fault = SIM_FAULT_EXTRA;
    sim.fault_arg = 3;
    CHECK_EQ(dxl_bus_ping(&bus, 1), DXL_BUSY);
    CHECK_EQ(dxl_bus_step(&bus), DXL_BUSY);
    /* All bytes in by the next step (10 out, 500 us, 17 back: 5.2 ms),
     * still inside the deadline of 6,370 us. */
    sim.now += 6000u;
    CHECK(sim.now - bus.t_start < bus.timeout_us);
    CHECK_EQ(dxl_bus_step(&bus), DXL_OK);
    CHECK_EQ(bus.extra, 3u);
}

/* A driver that hands over more bytes than asked for. */
static size_t g_flood;

static size_t flood_poll(void *ctx, uint8_t *out, size_t cap)
{
    (void)ctx;
    if (g_flood == 0u) {
        return 0;
    }
    memset(out, 0x00, cap);
    --g_flood;
    return cap + 5u;
}

TEST_CASE(a_driver_s_count_above_the_room_is_cut_to_the_room)
{
    setup(1);
    drv.poll = flood_poll;
    dxl_bus_init(&bus, &drv);
    CHECK_EQ(dxl_bus_set_baud(&bus, 57600), DXL_OK);
    g_flood = 3;
    sim.fault = SIM_FAULT_SILENT;
    CHECK_EQ(dxl_bus_ping(&bus, 1), DXL_BUSY);
    sim.now += bus.timeout_us;
    /* Zeros hold no header: all of them are noise, and the end is a
     * broken reply, not silence. */
    CHECK_EQ(dxl_bus_step(&bus), DXL_ERR_TRUNCATED);
    CHECK_EQ(bus.noise, 3u * DXL_WIRE_MAX);
    CHECK_EQ(bus.n_rx, 0u);
}

TEST_CASE(every_transaction_refuses_a_running_one_and_a_driver_without_start)
{
    setup(1);
    const uint8_t x = 0;
    const uint8_t ids[1] = { 1 };

    dxl_bus_init(NULL, &drv);
    CHECK_EQ(dxl_bus_ping(&bus, 1), DXL_BUSY);
    CHECK_EQ(dxl_bus_reg_write(&bus, 1, 65, &x, 1), DXL_ERR_STATE);
    CHECK_EQ(dxl_bus_action(&bus, 1), DXL_ERR_STATE);
    CHECK_EQ(dxl_bus_reboot(&bus, 1), DXL_ERR_STATE);
    CHECK_EQ(dxl_bus_factory_reset(&bus, 1, DXL_RESET_ALL_BUT_ID,
                                   DXL_FACTORY_RESET_CONFIRM),
             DXL_ERR_STATE);
    CHECK_EQ(dxl_bus_sync_write(&bus, 65, 1, ids, &x, 1), DXL_ERR_STATE);
    CHECK_EQ(sim.starts, 1u);
    dxl_bus_abort(&bus);

    drv.start = NULL;
    dxl_bus_init(&bus, &drv);
    CHECK_EQ(dxl_bus_set_baud(&bus, 57600), DXL_OK);
    CHECK_EQ(dxl_bus_ping(&bus, 1), DXL_ERR_DRIVER);
    CHECK(!dxl_bus_busy(&bus));
}

/* ------------------------------------------------------------------ scan */

static dxl_scan_t scan;

static dxl_res_t scan_run(void)
{
    dxl_res_t r = DXL_BUSY;

    for (unsigned i = 0; i < 50000000u && r == DXL_BUSY; ++i) {
        r = dxl_scan_step(&scan);
        if (r == DXL_BUSY) {
            sim.now += 13;
        }
    }
    return r;
}

TEST_CASE(scan_refuses_ids_outside_0_to_252_and_an_empty_mask)
{
    setup(1);
    CHECK_EQ(dxl_scan_start(&scan, &bus, 0, 253, 0x02), DXL_ERR_ARG);
    CHECK_EQ(dxl_scan_start(&scan, &bus, 253, 253, 0x02), DXL_ERR_ARG);
    CHECK_EQ(dxl_scan_start(&scan, &bus, 254, 254, 0x02), DXL_ERR_ARG);
    CHECK_EQ(dxl_scan_start(&scan, &bus, 255, 255, 0x02), DXL_ERR_ARG);
    CHECK_EQ(dxl_scan_start(&scan, &bus, 10, 9, 0x02), DXL_ERR_ARG);
    CHECK_EQ(dxl_scan_start(&scan, &bus, 0, 10, 0x00), DXL_ERR_ARG);
    CHECK_EQ(dxl_scan_start(NULL, &bus, 0, 10, 0x02), DXL_ERR_ARG);
    CHECK_EQ(dxl_scan_start(&scan, NULL, 0, 10, 0x02), DXL_ERR_ARG);
    CHECK_EQ(sim.starts, 0u);
    CHECK_EQ(dxl_bus_ping(&bus, 1), DXL_BUSY);
    CHECK_EQ(dxl_scan_start(&scan, &bus, 0, 10, 0x02), DXL_ERR_STATE);
}

TEST_CASE(scan_pings_0_and_252_and_every_id_between)
{
    setup(1);
    sim.servo[0].id = 0;
    sim_add(&sim, 252, 1060, 1);
    sim_add(&sim, 100, 1030, 3);
    CHECK_EQ(dxl_scan_start(&scan, &bus, 0, 252, 0x02), DXL_BUSY);
    CHECK_EQ(scan_run(), DXL_OK);
    CHECK_EQ(scan.pinged, 253u);
    CHECK_EQ(scan.n_found, 2u);
    CHECK_EQ(scan.found[0].id, 0u);
    CHECK_EQ(scan.found[0].model, 1030u);
    CHECK_EQ(scan.found[0].firmware, 45u);
    CHECK_EQ(scan.found[0].baud_index, 1u);
    CHECK_EQ(scan.found[0].flags, 0u);
    CHECK_EQ(scan.found[1].id, 252u);
    CHECK_EQ(scan.found[1].model, 1060u);
    CHECK_EQ(scan.overflow, 0u);
    CHECK_EQ(scan.bus->linger, 0u);
    CHECK_EQ(dxl_scan_step(&scan), DXL_OK);

    /* One ID: first == last. */
    CHECK_EQ(dxl_scan_start(&scan, &bus, 252, 252, 0x02), DXL_BUSY);
    CHECK_EQ(scan_run(), DXL_OK);
    CHECK_EQ(scan.pinged, 1u);
    CHECK_EQ(scan.n_found, 1u);
}

TEST_CASE(scan_walks_the_rates_and_skips_the_refused_one)
{
    setup(0);
    sim_add(&sim, 5, 1060, 3);
    sim_add(&sim, 6, 1030, 7);
    sim.refuse_mask = 0x40; /* 4,000,000 bit/s */
    CHECK_EQ(dxl_scan_start(&scan, &bus, 1, 6, 0xFF), DXL_BUSY);
    CHECK_EQ(scan_run(), DXL_OK);
    CHECK_EQ(scan.refused_mask, 0x40u);
    CHECK_EQ(scan.pinged, 7u * 6u);
    CHECK_EQ(scan.n_found, 3u);
    CHECK_EQ(scan.found[0].id, 1u);
    CHECK_EQ(scan.found[0].baud_index, 0u);
    CHECK_EQ(scan.found[1].id, 5u);
    CHECK_EQ(scan.found[1].baud_index, 3u);
    CHECK_EQ(scan.found[2].id, 6u);
    CHECK_EQ(scan.found[2].baud_index, 7u);
    CHECK_EQ(bus.baud, 4500000u);

    /* The second rate of the mask refused after the first is done: the
     * scan ends there. */
    sim.refuse_mask = 0x02;
    CHECK_EQ(dxl_scan_start(&scan, &bus, 1, 2, 0x03), DXL_BUSY);
    CHECK_EQ(scan_run(), DXL_OK);
    CHECK_EQ(scan.refused_mask, 0x02u);
    CHECK_EQ(scan.pinged, 2u);

    /* Every rate of the mask refused: done, nothing sent. */
    sim.refuse_mask = 0xFF;
    const unsigned starts = sim.starts;
    CHECK_EQ(dxl_scan_start(&scan, &bus, 1, 6, 0x81), DXL_OK);
    CHECK_EQ(scan.refused_mask, 0x81u);
    CHECK_EQ(sim.starts, starts);
}

TEST_CASE(two_servos_on_one_id_show_as_garbled_or_extra)
{
    setup(3);
    /* Same ID, same delay: their bytes collide. */
    sim_servo_t *b = sim_add(&sim, 1, 1060, 3);
    CHECK_EQ(dxl_scan_start(&scan, &bus, 1, 1, 0x08), DXL_BUSY);
    CHECK_EQ(scan_run(), DXL_OK);
    CHECK_EQ(scan.n_found, 1u);
    CHECK_EQ(scan.found[0].flags, DXL_FOUND_GARBLED);
    CHECK_EQ(scan.found[0].model, 0u);
    CHECK(scan.found[0].result != DXL_OK);

    /* The second answers 508 us after the packet, when the first, at
     * 0 us, is done (14 bytes at 1,000,000 bit/s: 140 us): extra bytes. */
    b->table[9] = 254;
    sim.servo[0].table[9] = 0;
    CHECK_EQ(dxl_scan_start(&scan, &bus, 1, 1, 0x08), DXL_BUSY);
    CHECK_EQ(scan_run(), DXL_OK);
    CHECK_EQ(scan.n_found, 1u);
    CHECK_EQ(scan.found[0].result, DXL_OK);
    CHECK_EQ(scan.found[0].flags, DXL_FOUND_EXTRA);
    CHECK_EQ(scan.found[0].model, 1030u);
}

TEST_CASE(scan_marks_the_alert_and_counts_beyond_16)
{
    setup(1);
    sim.servo[0].id = 0;
    for (uint8_t id = 1; id < 20u; ++id) {
        sim_add(&sim, id, 1060, 1);
    }
    sim.fault = SIM_FAULT_ERR;
    sim.fault_arg = 0x80;
    CHECK_EQ(dxl_scan_start(&scan, &bus, 0, 30, 0x02), DXL_BUSY);
    CHECK_EQ(scan_run(), DXL_OK);
    CHECK_EQ(scan.n_found, DXL_SCAN_FOUND_MAX);
    CHECK_EQ(scan.overflow, 20u - DXL_SCAN_FOUND_MAX);
    CHECK_EQ(scan.found[15].id, 15u);
    CHECK_EQ(scan.found[3].flags, DXL_FOUND_ALERT);
}

TEST_CASE(scan_abort_ends_the_ping_and_the_scan)
{
    setup(1);
    CHECK_EQ(dxl_scan_start(&scan, &bus, 0, 252, 0x02), DXL_BUSY);
    CHECK_EQ(dxl_scan_step(&scan), DXL_BUSY);
    dxl_scan_abort(&scan);
    CHECK_EQ(dxl_scan_step(&scan), DXL_ERR_ABORTED);
    CHECK(!dxl_bus_busy(&bus));
    CHECK_EQ(bus.linger, 0u);
    dxl_scan_abort(&scan);
    dxl_scan_abort(NULL);
    CHECK_EQ(dxl_scan_step(NULL), DXL_ERR_ARG);

    /* A driver that stops taking starts ends the scan. */
    CHECK_EQ(dxl_scan_start(&scan, &bus, 0, 252, 0x02), DXL_BUSY);
    sim.refuse_start = true;
    CHECK_EQ(scan_run(), DXL_ERR_DRIVER);
    CHECK_EQ(scan.pinged, 1u);
}

int main(void)
{
    RUN(a_ping_returns_the_model_and_firmware_at_every_rate);
    RUN(a_read_returns_the_bytes_of_the_table);
    RUN(a_write_lands_in_the_table_and_is_answered);
    RUN(factory_reset_needs_the_confirmation_and_one_servo);
    RUN(broadcast_writes_and_sync_write_take_no_reply);
    RUN(status_return_level_decides_which_replies_are_waited_for);
    RUN(a_write_to_id_takes_the_reply_from_the_old_or_the_new_id);
    RUN(a_broken_reply_has_its_own_result);
    RUN(the_reply_may_come_one_byte_a_poll);
    RUN(a_silent_servo_times_out_at_the_deadline_at_every_rate);
    RUN(the_deadline_holds_across_the_clock_s_wrap);
    RUN(a_shorter_return_delay_shortens_the_deadline);
    RUN(one_transaction_at_a_time_and_abort_ends_it);
    RUN(ping_refuses_the_broadcast_and_non_ids);
    RUN(a_parser_refusal_ends_the_transaction_with_its_code);
    RUN(bytes_behind_the_reply_in_one_poll_are_counted_as_extra);
    RUN(a_driver_s_count_above_the_room_is_cut_to_the_room);
    RUN(every_transaction_refuses_a_running_one_and_a_driver_without_start);
    RUN(scan_refuses_ids_outside_0_to_252_and_an_empty_mask);
    RUN(scan_pings_0_and_252_and_every_id_between);
    RUN(scan_walks_the_rates_and_skips_the_refused_one);
    RUN(two_servos_on_one_id_show_as_garbled_or_extra);
    RUN(scan_marks_the_alert_and_counts_beyond_16);
    RUN(scan_abort_ends_the_ping_and_the_scan);
    return test_summary("dxl_bus");
}
