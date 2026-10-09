/*
 * The AS5600 magnetic angle sensor (as5600.h), the output encoder: its
 * codec and driver against a modelled part (fake_ina.h), its slot in the
 * sampling schedule (sense_sched.h) and its place in the sensor service's
 * snapshot (sense_svc.h).
 *
 * Under test: the 12-bit registers and the STATUS bits; angle differences on
 * the circle across the 4095 to 0 wrap; hundredths of a degree; a probe that
 * finds the part, finds nothing, or finds a STATUS no AS5600 gives; each
 * register read in a transaction of its own (STATUS 1 byte, RAW ANGLE 2,
 * AGC 1, MAGNITUDE 2), never across the increment-suppressing registers;
 * the part offline after three failed reads and back after a probe a second
 * later; the schedule reading the angle 500 times, STATUS 480 times and
 * AGC with MAGNITUDE 20 times in 1000 ms, on the odd ticks, with the bus
 * time the header states and without moving the rate of any other read;
 * the angle sampled every 2 ms through the field slots; the still time: counting up inside the 12-count tolerance, restarting
 * at a sample outside it, across the wrap, on a ramp, cleared by an offline
 * part; the service opening the bus for the encoder alone and handing the
 * readings over.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "as5600.h"
#include "fake_ina.h"
#include "sense_sched.h"
#include "sense_svc.h"
#include "servo_test.h"

static fake_bus_t   fb;
static sense_bus_t  bus;
static as5600_t     d;
static fake_part_t *enc;

static void fresh(void)
{
    fake_bus_init(&fb, &bus);
    enc = fake_add(&fb, FAKE_AS5600, AS5600_ADDR, 0.0);
    as5600_init(&d, &bus);
}

/* -------------------------------------------------------------- codec */

TEST_CASE(a_register_pair_holds_twelve_bits)
{
    const uint8_t a[2] = { 0xF1u, 0x23u };      /* the top nibble reads 0 */
    CHECK_EQ(as5600_u12(a), 0x123u);
    const uint8_t b[2] = { 0x0Fu, 0xFFu };
    CHECK_EQ(as5600_u12(b), 4095u);
    const uint8_t z[2] = { 0x00u, 0x00u };
    CHECK_EQ(as5600_u12(z), 0u);
}

TEST_CASE(status_carries_the_magnet_bits_and_nothing_else)
{
    CHECK(as5600_status_valid(0x00u));
    CHECK(as5600_status_valid(AS5600_STATUS_MD));
    CHECK(as5600_status_valid(AS5600_STATUS_MD | AS5600_STATUS_ML
                              | AS5600_STATUS_MH));
    CHECK(!as5600_status_valid(0x01u));
    CHECK(!as5600_status_valid(0x40u));
    CHECK(!as5600_status_valid(0xFFu));
    CHECK_EQ(AS5600_STATUS_MH, 0x08u);          /* bit 3 */
    CHECK_EQ(AS5600_STATUS_ML, 0x10u);          /* bit 4 */
    CHECK_EQ(AS5600_STATUS_MD, 0x20u);          /* bit 5 */
    CHECK(as5600_md(0x20u) && !as5600_ml(0x20u) && !as5600_mh(0x20u));
    CHECK(as5600_ml(0x10u) && !as5600_md(0x10u));
    CHECK(as5600_mh(0x08u) && !as5600_md(0x08u));
}

TEST_CASE(differences_are_taken_on_the_circle)
{
    CHECK_EQ(as5600_delta(100u, 90u), 10);
    CHECK_EQ(as5600_delta(90u, 100u), -10);
    CHECK_EQ(as5600_delta(0u, 4095u), 1);          /* across the wrap */
    CHECK_EQ(as5600_delta(4095u, 0u), -1);
    CHECK_EQ(as5600_delta(5u, 4090u), 11);
    CHECK_EQ(as5600_delta(2048u, 0u), -2048);      /* half a turn: one side */
    CHECK_EQ(as5600_delta(2047u, 0u), 2047);
    CHECK_EQ(as5600_delta(0u, 2048u), -2048);
    CHECK_EQ(as5600_delta(0x1005u, 5u), 0);        /* modulo 4096 */
}

TEST_CASE(counts_are_hundredths_of_a_degree)
{
    CHECK_EQ(as5600_cdeg(0), 0);
    CHECK_EQ(as5600_cdeg(1024), 9000);
    CHECK_EQ(as5600_cdeg(-1024), -9000);
    CHECK_EQ(as5600_cdeg(2048), 18000);
    CHECK_EQ(as5600_cdeg(1), 9);                   /* 8.79 */
    CHECK_EQ(as5600_cdeg(-1), -9);
    CHECK_EQ(as5600_cdeg(12), 105);                /* the tolerance: 1.05 */
}

/* -------------------------------------------------------------- driver */

TEST_CASE(a_probe_finds_the_part_and_reads_nothing_more)
{
    fresh();
    CHECK_EQ(as5600_state(&d), SENSE_PART_UNPROBED);
    CHECK(as5600_step(&d, 0));
    CHECK_EQ(as5600_state(&d), SENSE_PART_ONLINE);
    CHECK_EQ(d.part.addr, AS5600_ADDR);
    CHECK_EQ(enc->reads[AS5600_REG_STATUS], 1u);   /* STATUS, 1 byte */
    CHECK_EQ(enc->reads[AS5600_REG_RAW_ANGLE], 0u);
    CHECK_EQ(fb.transactions, 1u);
    CHECK_EQ(fb.bad_width, 0u);
    CHECK(as5600_step(&d, 1));                     /* not probed again */
    CHECK_EQ(fb.transactions, 1u);
}

TEST_CASE(a_missing_part_is_absent_and_probed_again_a_second_later)
{
    fresh();
    enc->present = false;
    CHECK(!as5600_step(&d, 0));
    CHECK_EQ(as5600_state(&d), SENSE_PART_ABSENT);
    CHECK(!as5600_step(&d, 999));
    CHECK_EQ(fb.transactions, 1u);
    enc->present = true;
    CHECK(as5600_step(&d, 1000));
    CHECK_EQ(as5600_state(&d), SENSE_PART_ONLINE);
}

TEST_CASE(a_status_no_as5600_gives_is_not_used)
{
    fresh();
    enc->status = 0xC1u;                           /* reserved bits set */
    CHECK(!as5600_step(&d, 0));
    CHECK_EQ(as5600_state(&d), SENSE_PART_WRONG_ID);
    CHECK_EQ(d.part.id_device, 0xC1u);
    uint8_t st = 0;
    uint16_t raw = 0;
    CHECK_EQ(as5600_read_angle(&d, &st, &raw), SENSE_OFFLINE);
}

TEST_CASE(status_and_raw_angle_are_two_reads)
{
    fresh();
    enc->status = AS5600_STATUS_MD | AS5600_STATUS_MH;
    enc->raw = 0x0ABCu;
    CHECK(as5600_step(&d, 0));
    const unsigned before = fb.transactions;
    uint8_t st = 0;
    uint16_t raw = 0;
    CHECK_EQ(as5600_read_angle(&d, &st, &raw), SENSE_OK);
    CHECK_EQ(st, AS5600_STATUS_MD | AS5600_STATUS_MH);
    CHECK_EQ(raw, 0x0ABCu);
    CHECK_EQ(fb.transactions, before + 2u);
    CHECK_EQ(enc->reads[AS5600_REG_STATUS], 2u);     /* probe and this */
    CHECK_EQ(enc->reads[AS5600_REG_RAW_ANGLE], 1u);
    CHECK_EQ(fb.bad_width, 0u);
    /* The single reads. */
    enc->raw = 0x0F01u;
    CHECK_EQ(as5600_read_raw(&d, &raw), SENSE_OK);
    CHECK_EQ(raw, 0x0F01u);
    enc->status = AS5600_STATUS_ML;
    CHECK_EQ(as5600_read_status(&d, &st), SENSE_OK);
    CHECK_EQ(st, AS5600_STATUS_ML);
    CHECK_EQ(fb.bad_width, 0u);
}

TEST_CASE(agc_and_magnitude_are_two_reads)
{
    fresh();
    enc->agc = 128u;
    enc->magnitude = 0x0345u;
    CHECK(as5600_step(&d, 0));
    const unsigned before = fb.transactions;
    uint8_t agc = 0;
    uint16_t mag = 0;
    CHECK_EQ(as5600_read_magnitude(&d, &agc, &mag), SENSE_OK);
    CHECK_EQ(agc, 128u);
    CHECK_EQ(mag, 0x0345u);
    CHECK_EQ(fb.transactions, before + 2u);
    CHECK_EQ(enc->reads[AS5600_REG_AGC], 1u);
    CHECK_EQ(enc->reads[AS5600_REG_MAGNITUDE], 1u);
    CHECK_EQ(fb.bad_width, 0u);
    enc->magnitude = 0x0FFFu;
    CHECK_EQ(as5600_read_mag(&d, &mag), SENSE_OK);
    CHECK_EQ(mag, 0x0FFFu);
    enc->agc = 7u;
    CHECK_EQ(as5600_read_agc(&d, &agc), SENSE_OK);
    CHECK_EQ(agc, 7u);
    CHECK_EQ(fb.bad_width, 0u);
}

TEST_CASE(a_read_across_the_special_registers_is_a_fault_of_the_model)
{
    /* The modelled part takes one register a read: STATUS on to RAW ANGLE
     * in one read, and AGC on to MAGNITUDE, are counted as bad. */
    fresh();
    uint8_t b[3];
    CHECK_EQ(sense_bus_read(&bus, AS5600_ADDR, AS5600_REG_STATUS, b, 3u),
             SENSE_OK);
    CHECK_EQ(fb.bad_width, 1u);
    CHECK_EQ(sense_bus_read(&bus, AS5600_ADDR, AS5600_REG_AGC, b, 3u),
             SENSE_OK);
    CHECK_EQ(fb.bad_width, 2u);
    CHECK_EQ(sense_bus_read(&bus, AS5600_ADDR, AS5600_REG_RAW_ANGLE, b, 2u),
             SENSE_OK);
    CHECK_EQ(fb.bad_width, 2u);
}

TEST_CASE(a_failed_read_leaves_the_values_and_three_take_it_offline)
{
    fresh();
    enc->raw = 1234u;
    CHECK(as5600_step(&d, 0));
    uint8_t st = 0;
    uint16_t raw = 0;
    CHECK_EQ(as5600_read_angle(&d, &st, &raw), SENSE_OK);
    fb.fail_with  = SENSE_NACK;
    fb.fail_count = 3u;
    raw = 77u;
    CHECK_EQ(as5600_read_angle(&d, &st, &raw), SENSE_NACK);
    CHECK_EQ(raw, 77u);
    CHECK_EQ(as5600_state(&d), SENSE_PART_ONLINE);
    CHECK_EQ(as5600_read_angle(&d, &st, &raw), SENSE_NACK);
    CHECK_EQ(as5600_read_angle(&d, &st, &raw), SENSE_NACK);
    CHECK_EQ(as5600_state(&d), SENSE_PART_OFFLINE);
    CHECK_EQ(as5600_read_angle(&d, &st, &raw), SENSE_OFFLINE);
    CHECK(!as5600_step(&d, 500));
    CHECK(as5600_step(&d, 1000));                  /* the probe a second on */
}

/* ------------------------------------------------------------ schedule */

static sense_sched_t s;
static uint64_t      g_us;
static fake_part_t  *i3221;

static uint64_t now_us(void *ctx)
{
    (void)ctx;
    return g_us;
}

/* The encoder alone, or with the INA3221 beside it; the clock at 10 s. */
static void rig(bool with_ina)
{
    sense_bus_t scratch;
    fake_bus_init(&fb, &scratch);
    enc = fake_add(&fb, FAKE_AS5600, AS5600_ADDR, 0.0);
    i3221 = NULL;
    if (with_ina) {
        i3221 = fake_add(&fb, FAKE_INA3221, 0x40u, 0.1);
        i3221->volts[0] = 6.0;
    }
    const sense_sched_io_t io = { { fake_read, fake_write, &fb }, now_us,
                                  NULL, NULL };
    sense_sched_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.as5600_en = true;
    if (with_ina) {
        cfg.ina3221_en = true;
        cfg.ina3221_addr = 0x40u;
        cfg.ina3221_shunt_uohm = 100000u;
        cfg.ina3221_channels = 1u;
    }
    g_us = 10000000u;
    sense_sched_init(&s, &io, &cfg);
}

static void tick(void)
{
    const uint64_t at = g_us;
    sense_sched_tick(&s);
    g_us = at + 1000u;
}

static void ticks(unsigned n)
{
    for (unsigned k = 0; k < n; ++k) {
        tick();
    }
}

TEST_CASE(the_angle_is_read_500_times_and_the_field_20_in_a_second)
{
    rig(false);
    tick();                                        /* the probe */
    CHECK_EQ(as5600_state(&s.enc.dev), SENSE_PART_ONLINE);
    memset(enc->reads, 0, sizeof(enc->reads));
    enc->clocks = 0u;
    enc->raw = 2000u;
    enc->status = AS5600_STATUS_MD;
    enc->magnitude = 1500u;
    enc->agc = 100u;
    const uint16_t before = s.enc.samples;
    ticks(1000u);
    CHECK_EQ(enc->reads[AS5600_REG_RAW_ANGLE], 500u);
    CHECK_EQ((uint16_t)(s.enc.samples - before), 500u);
    CHECK_EQ(enc->reads[AS5600_REG_STATUS], 480u);
    CHECK_EQ(enc->reads[AS5600_REG_AGC], 20u);
    CHECK_EQ(enc->reads[AS5600_REG_MAGNITUDE], 20u);
    CHECK_EQ(fb.bad_width, 0u);
    CHECK(s.enc.have_angle && s.enc.have_mag);
    CHECK_EQ(s.enc.raw, 2000u);
    CHECK_EQ(s.enc.status, AS5600_STATUS_MD);
    CHECK_EQ(s.enc.magnitude, 1500u);
    CHECK_EQ(s.enc.agc, 100u);
    /* Bus time: 480 slots of STATUS (39 clocks) and RAW ANGLE (48), 20 of
     * RAW ANGLE, AGC (39) and MAGNITUDE (48): 44460 clocks of 2.5 us,
     * 111.15 ms of the second, 11.1 %. */
    CHECK_EQ(enc->clocks, 480u * (39u + 48u) + 20u * (48u + 39u + 48u));
    CHECK_EQ(enc->clocks, 44460u);
}

TEST_CASE(the_angle_is_sampled_every_2_ms_through_the_field_slots)
{
    rig(false);
    tick();
    /* 100 slots, four field slots among them: the sample count advances
     * once in every second tick, never skipping one. */
    uint16_t last = s.enc.samples;
    unsigned since = 0u;
    for (unsigned k = 0; k < 200u; ++k) {
        tick();
        ++since;
        if (s.enc.samples != last) {
            CHECK_EQ((uint16_t)(s.enc.samples - last), 1u);
            if (k > 4u) {
                CHECK_EQ(since, 2u);
            }
            since = 0u;
            last  = s.enc.samples;
        }
        CHECK(since <= 2u);
    }
    /* A field slot still stamps its sample: the still time keeps counting
     * across it. */
    enc->raw = 900u;
    ticks(400u);
    CHECK(sense_sched_enc_still_ms(&s) >= 395u);
}

TEST_CASE(the_encoders_slot_is_217_or_337_us_of_a_tick)
{
    /* CH1 alone beside the encoder: CH1 is one 2-byte read (48 clocks), so
     * a tick's reads are 48 clocks on an even tick, and on an odd tick
     * 48 plus the encoder's 87 (STATUS, RAW ANGLE) or 135 (RAW ANGLE, AGC,
     * MAGNITUDE).  The header adds the INA228's slot and the pair to that:
     * 840 us at most. */
    rig(true);
    tick();
    unsigned worst = 0u, field = 0u, plain = 0u;
    for (unsigned k = 0; k < 400u; ++k) {
        const uint64_t c0 = i3221->clocks, e0 = enc->clocks;
        tick();
        const unsigned enc_clk = (unsigned)(enc->clocks - e0);
        const unsigned all     = (unsigned)(i3221->clocks - c0) + enc_clk;
        if (all > worst) {
            worst = all;
        }
        field += (enc_clk == 135u);
        plain += (enc_clk == 87u);
        CHECK(enc_clk == 0u || enc_clk == 87u || enc_clk == 135u);
    }
    CHECK_EQ(plain + field, 200u);
    CHECK_EQ(field, 8u);
    CHECK(worst <= 48u + 135u);                    /* 457.5 us */
}

TEST_CASE(the_encoder_moves_no_other_reads_rate)
{
    rig(true);
    tick();
    CHECK_EQ(ina3221_state(&s.i3221), SENSE_PART_ONLINE);
    memset(i3221->reads, 0, sizeof(i3221->reads));
    memset(enc->reads, 0, sizeof(enc->reads));
    ticks(1000u);
    CHECK_EQ(i3221->reads[INA3221_SHUNT1], 1000u);
    CHECK_EQ(i3221->reads[INA3221_BUS1], 50u);
    CHECK_EQ(i3221->reads[INA3221_MASK_ENABLE], 50u);
    CHECK_EQ(enc->reads[AS5600_REG_RAW_ANGLE], 500u);
}

TEST_CASE(a_part_not_enabled_is_never_addressed)
{
    rig(false);
    sense_sched_t t;
    const sense_sched_io_t io = { { fake_read, fake_write, &fb }, now_us,
                                  NULL, NULL };
    const sense_sched_cfg_t off = { 0 };
    sense_sched_init(&t, &io, &off);
    for (unsigned k = 0; k < 200u; ++k) {
        sense_sched_tick(&t);
        g_us += 1000u;
    }
    CHECK_EQ(fb.transactions, 0u);
    CHECK_EQ(sense_sched_enc_still_ms(&t), 0u);
}

TEST_CASE(the_still_time_counts_up_inside_the_tolerance)
{
    rig(false);
    enc->raw = 1000u;
    ticks(2u);
    CHECK(s.enc.anchored);
    ticks(100u);
    const unsigned held = sense_sched_enc_still_ms(&s);
    CHECK(held >= 98u && held <= 101u);
    /* Moves of up to 12 counts from the anchor do not restart it. */
    enc->raw = 1012u;
    ticks(50u);
    CHECK(sense_sched_enc_still_ms(&s) >= 148u);
    enc->raw = 988u;
    ticks(50u);
    CHECK(sense_sched_enc_still_ms(&s) >= 198u);
    CHECK_EQ(SENSE_ENC_STILL_TOL, 12u);
    /* The servo test judges "settled" by the tolerance the coprocessor
     * keeps the still time to. */
    CHECK_EQ(SENSE_ENC_STILL_TOL, SERVO_TEST_ENC_TOL_COUNTS);
}

TEST_CASE(a_sample_outside_the_tolerance_restarts_it)
{
    rig(false);
    enc->raw = 1000u;
    ticks(200u);
    CHECK(sense_sched_enc_still_ms(&s) >= 190u);
    enc->raw = 1013u;                              /* 13 counts away */
    ticks(2u);
    CHECK(sense_sched_enc_still_ms(&s) <= 2u);
    ticks(20u);
    const unsigned h = sense_sched_enc_still_ms(&s);
    CHECK(h >= 18u && h <= 22u);
    /* And the new anchor is where it landed: 13 counts on from it again
     * restarts, 12 does not. */
    enc->raw = 1025u;
    ticks(10u);
    CHECK(sense_sched_enc_still_ms(&s) >= 10u);
    enc->raw = 1026u;
    ticks(2u);
    CHECK(sense_sched_enc_still_ms(&s) <= 2u);
}

TEST_CASE(the_tolerance_is_taken_across_the_wrap)
{
    rig(false);
    enc->raw = 4094u;
    ticks(100u);
    enc->raw = 4u;                                 /* 6 counts on the circle */
    ticks(100u);
    CHECK(sense_sched_enc_still_ms(&s) >= 198u);
    enc->raw = 20u;                                /* 22 from 4094: outside */
    ticks(2u);
    CHECK(sense_sched_enc_still_ms(&s) <= 2u);
}

TEST_CASE(a_ramp_keeps_restarting_it_and_a_stop_lets_it_count)
{
    rig(false);
    enc->raw = 100u;
    ticks(10u);
    /* 3 counts a 2 ms slot, 1.5 counts a ms: the anchor moves every
     * fifth slot, about 10 ms. */
    for (unsigned k = 0; k < 100u; ++k) {
        enc->raw = (uint16_t)(enc->raw + 1u + (k & 1u));
        tick();
        if (k >= 20u) {
            CHECK(sense_sched_enc_still_ms(&s) <= 16u);
        }
    }
    ticks(60u);
    CHECK(sense_sched_enc_still_ms(&s) >= 55u);
}

TEST_CASE(a_part_gone_offline_has_no_still_time_and_returns_afresh)
{
    rig(false);
    enc->raw = 700u;
    ticks(300u);
    CHECK(sense_sched_enc_still_ms(&s) >= 290u);
    enc->present = false;
    ticks(10u);
    CHECK_EQ(as5600_state(&s.enc.dev), SENSE_PART_OFFLINE);
    CHECK_EQ(sense_sched_enc_still_ms(&s), 0u);
    enc->present = true;
    enc->raw = 700u;
    ticks(1010u);
    CHECK_EQ(as5600_state(&s.enc.dev), SENSE_PART_ONLINE);
    const unsigned h = sense_sched_enc_still_ms(&s);
    CHECK(h < 20u);                                /* counted from the return */
}

TEST_CASE(the_still_time_saturates_at_65535)
{
    rig(false);
    enc->raw = 5u;
    ticks(4u);
    g_us += 70000000u;                             /* 70 s with no tick */
    CHECK_EQ(sense_sched_enc_still_ms(&s), 65535u);
}

/* ------------------------------------------------------------- service */

static sense_svc_t  v;
static sense_cmd_t  cmd;
static sense_snap_t snap;
static unsigned     g_opens, g_closes;

static bool open_bus(void *ctx, uint8_t sda, uint8_t scl)
{
    (void)ctx;
    (void)sda;
    (void)scl;
    ++g_opens;
    return true;
}

static void close_bus(void *ctx)
{
    (void)ctx;
    ++g_closes;
}

static sense_err_t ask(void *ctx, uint8_t addr)
{
    return (fake_find((fake_bus_t *)ctx, addr) != NULL) ? SENSE_OK
                                                        : SENSE_NACK;
}

static void svc_rig(void)
{
    sense_bus_t scratch;
    fake_bus_init(&fb, &scratch);
    enc = fake_add(&fb, FAKE_AS5600, AS5600_ADDR, 0.0);
    g_us = 20000000u;
    g_opens = g_closes = 0u;
    const sense_svc_io_t io = {
        .sched = { { fake_read, fake_write, &fb }, now_us, NULL, &fb },
        .open = open_bus, .close = close_bus, .ask = ask,
    };
    sense_svc_init(&v, &io);
    memset(&cmd, 0, sizeof(cmd));
    cmd.cfg_gen = 1u;
    cmd.sda = 16u;
    cmd.scl = 17u;
    cmd.parts.as5600_en = true;
    cmd.cap.rise_ua = SENSE_CAP_RISE_AUTO;
}

static void svc_steps(unsigned n)
{
    for (unsigned k = 0; k < n; ++k) {
        sense_svc_step(&v, &cmd, &snap);
        g_us += 1000u;
    }
}

TEST_CASE(the_encoder_alone_opens_the_bus_and_reaches_the_snapshot)
{
    svc_rig();
    enc->raw = 3000u;
    enc->status = AS5600_STATUS_MD | AS5600_STATUS_ML;
    enc->magnitude = 800u;
    enc->agc = 200u;
    svc_steps(60u);
    CHECK_EQ(g_opens, 1u);
    CHECK(snap.open);
    CHECK_EQ(snap.enc, SENSE_PART_ONLINE);
    CHECK(snap.enc_have_angle && snap.enc_have_mag);
    CHECK_EQ(snap.enc_raw, 3000u);
    CHECK_EQ(snap.enc_status, AS5600_STATUS_MD | AS5600_STATUS_ML);
    CHECK_EQ(snap.enc_id, AS5600_STATUS_MD | AS5600_STATUS_ML);
    CHECK_EQ(snap.enc_magnitude, 800u);
    CHECK_EQ(snap.enc_agc, 200u);
    CHECK(snap.enc_samples >= 25u);
    CHECK(snap.enc_still_ms >= 50u);
    CHECK_EQ(snap.i228, SENSE_PART_UNSET);
    CHECK_EQ(snap.i3221, SENSE_PART_UNSET);
    /* Held: the pins of the bus, as for any part. */
    CHECK_EQ(snap.held, ((uint64_t)1u << 16) | ((uint64_t)1u << 17));
}

TEST_CASE(a_set_up_without_the_encoder_leaves_its_snapshot_empty)
{
    svc_rig();
    cmd.parts.as5600_en = false;
    cmd.parts.ina3221_en = true;
    cmd.parts.ina3221_addr = 0x40u;
    cmd.parts.ina3221_shunt_uohm = 100000u;
    cmd.parts.ina3221_channels = 1u;
    (void)fake_add(&fb, FAKE_INA3221, 0x40u, 0.1);
    svc_steps(30u);
    CHECK(snap.open);
    CHECK_EQ(snap.enc, SENSE_PART_UNSET);
    CHECK(!snap.enc_have_angle);
    CHECK_EQ(snap.enc_samples, 0u);
}

TEST_CASE(a_new_set_up_forgets_the_old_readings)
{
    svc_rig();
    enc->raw = 100u;
    svc_steps(30u);
    CHECK(snap.enc_have_angle);
    cmd.cfg_gen = 2u;
    cmd.parts.as5600_en = false;
    svc_steps(1u);
    CHECK(!snap.open);
    CHECK(!snap.enc_have_angle);
    cmd.cfg_gen = 3u;
    cmd.parts.as5600_en = true;
    svc_steps(1u);
    CHECK(!snap.enc_have_angle);                   /* not read yet */
    svc_steps(10u);
    CHECK(snap.enc_have_angle);
    CHECK(snap.enc_samples <= 6u);                 /* counted afresh */
}

int main(void)
{
    RUN(a_register_pair_holds_twelve_bits);
    RUN(status_carries_the_magnet_bits_and_nothing_else);
    RUN(differences_are_taken_on_the_circle);
    RUN(counts_are_hundredths_of_a_degree);
    RUN(a_probe_finds_the_part_and_reads_nothing_more);
    RUN(a_missing_part_is_absent_and_probed_again_a_second_later);
    RUN(a_status_no_as5600_gives_is_not_used);
    RUN(status_and_raw_angle_are_two_reads);
    RUN(agc_and_magnitude_are_two_reads);
    RUN(a_read_across_the_special_registers_is_a_fault_of_the_model);
    RUN(a_failed_read_leaves_the_values_and_three_take_it_offline);
    RUN(the_angle_is_read_500_times_and_the_field_20_in_a_second);
    RUN(the_angle_is_sampled_every_2_ms_through_the_field_slots);
    RUN(the_encoders_slot_is_217_or_337_us_of_a_tick);
    RUN(the_encoder_moves_no_other_reads_rate);
    RUN(a_part_not_enabled_is_never_addressed);
    RUN(the_still_time_counts_up_inside_the_tolerance);
    RUN(a_sample_outside_the_tolerance_restarts_it);
    RUN(the_tolerance_is_taken_across_the_wrap);
    RUN(a_ramp_keeps_restarting_it_and_a_stop_lets_it_count);
    RUN(a_part_gone_offline_has_no_still_time_and_returns_afresh);
    RUN(the_still_time_saturates_at_65535);
    RUN(the_encoder_alone_opens_the_bus_and_reaches_the_snapshot);
    RUN(a_set_up_without_the_encoder_leaves_its_snapshot_empty);
    RUN(a_new_set_up_forgets_the_old_readings);
    return test_summary("as5600");
}
