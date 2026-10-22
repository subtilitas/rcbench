/*
 * The rotary knob: the MT6701 angle decode, the relative-motion mapping
 * across the 16383 to 0 wrap, and the rules that stop a knob that is gone
 * from moving anything.  A model of an I2C (Inter-Integrated Circuit) bus
 * with the GT911 touch controller on it holds the knob's traffic to its
 * own address.  On the 7-inch ESP32-P4 board the knob has a bus of its
 * own; a bus it shares with the GT911 is the harder case, and the one the
 * table in panel_i2c.h guards.  And one rule of the
 * knob's command on the SERVO screen, through the router, with touch fed as
 * the panel produces it (touch_feed.h).
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <string.h>

#include "greatest.h"
#include "touch_feed.h"

#include "knob.h"
#include "panel_i2c.h"
#include "servo_screen.h"
#include "servo_spots.h"
#include "settings.h"
#include "splash_screen.h"
#include "ui_screen.h"
#include "ui_theme.h"

/* ---- the bus model ---------------------------------------------------- */

/*
 * One transaction of the knob's: the register address written to @p addr,
 * a repeated START, one byte read.  The GT911 answers at 0x5D, the MT6701
 * at 0x06 when fitted, and nothing else answers.  A probe is the address
 * byte alone.
 */
typedef struct {
    bool     mt6701;         /* an MT6701 at KNOB_I2C_ADDR               */
    uint16_t angle;          /* its angle, the datasheet's 0..16383      */
    int      xfers;          /* transactions started, probes included    */
    int      probes;         /* of them address-only probes              */
    int      reads;          /* of them register reads                   */
    int      to_reserved;    /* of them at an address of the board's     */
    int      to_other;       /* at an address neither 0x06 nor reserved  */
} bus_t;

static void bus_up(bus_t *b, bool mt6701, uint16_t angle)
{
    memset(b, 0, sizeof *b);
    b->mt6701 = mt6701;
    b->angle = angle;
}

static void bus_count(bus_t *b, uint8_t addr)
{
    ++b->xfers;
    if (panel_i2c_addr_reserved(addr)) {
        ++b->to_reserved;
    } else if (addr != KNOB_I2C_ADDR) {
        ++b->to_other;
    }
}

static bool bus_probe(bus_t *b, uint8_t addr)
{
    bus_count(b, addr);
    ++b->probes;
    return addr == 0x5Du || (addr == KNOB_I2C_ADDR && b->mt6701);
}

static bool bus_read(bus_t *b, uint8_t addr, uint8_t reg, uint8_t *byte)
{
    bus_count(b, addr);
    ++b->reads;
    if (addr == 0x5Du) {
        *byte = 0;
        return true;
    }
    if (addr == KNOB_I2C_ADDR && b->mt6701) {
        if (reg == KNOB_REG_ANGLE_HI) {
            *byte = (uint8_t)(b->angle >> 6);
            return true;
        }
        if (reg == KNOB_REG_ANGLE_LO) {
            /* Bits 1 and 0 are not used; the model fills them. */
            *byte = (uint8_t)(((b->angle & 0x3Fu) << 2) | 0x03u);
            return true;
        }
        return false;
    }
    return false;
}

/* One poll as knob_task.c makes it. */
static int poll(knob_t *k, bus_t *b, bool *answered)
{
    uint8_t bytes[KNOB_POLL_READS] = { 0 };
    const int addr = knob_poll_addr(k);
    bool ok = addr >= 0;
    if (ok && knob_poll_probe_first(k)) {
        ok = bus_probe(b, (uint8_t)addr);
    }
    for (int i = 0; ok && i < KNOB_POLL_READS; ++i) {
        ok = bus_read(b, (uint8_t)addr, knob_poll_regs[i], &bytes[i]);
    }
    if (answered != NULL) {
        *answered = ok;
    }
    return knob_poll_done(k, ok, bytes);
}

/* A stable reading: 0x03, 0x04, 0x03 of one angle. */
static void bytes_of(uint16_t angle, uint8_t out[KNOB_POLL_READS])
{
    out[0] = (uint8_t)(angle >> 6);
    out[1] = (uint8_t)((angle & 0x3Fu) << 2);
    out[2] = out[0];
}

/* ---- the sensor ------------------------------------------------------- */

TEST_CASE(the_address_and_the_registers_are_the_datasheets)
{
    CHECK_EQ(KNOB_I2C_ADDR, 0x06);
    CHECK_EQ(KNOB_REG_ANGLE_HI, 0x03);
    CHECK_EQ(KNOB_REG_ANGLE_LO, 0x04);
    CHECK_EQ(KNOB_POLL_READS, 3);
    /* 0x03 first, then 0x04, then 0x03 again for the tear check. */
    CHECK_EQ(knob_poll_regs[0], KNOB_REG_ANGLE_HI);
    CHECK_EQ(knob_poll_regs[1], KNOB_REG_ANGLE_LO);
    CHECK_EQ(knob_poll_regs[2], KNOB_REG_ANGLE_HI);
    CHECK_EQ(KNOB_COUNTS, 16384);
    CHECK(!panel_i2c_addr_reserved(KNOB_I2C_ADDR));
    /* The address the datasheet's EEPROM moves it to: 0x46 in version 1.9
     * and 0x26 in version 0.1 are both free. */
    CHECK(!panel_i2c_addr_reserved(0x46));
    CHECK(!panel_i2c_addr_reserved(0x26));
}

TEST_CASE(decode_reads_fourteen_bits_over_two_registers)
{
    CHECK_EQ(knob_decode(0x00, 0x00), 0);
    CHECK_EQ(knob_decode(0x00, 0x04), 1);              /* Angle[0]  */
    CHECK_EQ(knob_decode(0x00, 0xFC), 63);             /* Angle[5:0] */
    CHECK_EQ(knob_decode(0x01, 0x00), 64);             /* Angle[6]  */
    CHECK_EQ(knob_decode(0x80, 0x00), 8192);           /* Angle[13] */
    CHECK_EQ(knob_decode(0xFF, 0xFC), 16383);          /* the largest */
    CHECK_EQ(knob_decode(0xAB, 0xCD), (0xAB << 6) | (0xCD >> 2));
    /* Bits 1 and 0 of 0x04 are not part of the angle. */
    CHECK_EQ(knob_decode(0xFF, 0xFF), 16383);
    CHECK_EQ(knob_decode(0x00, 0x03), 0);
    CHECK_EQ(knob_decode(0x12, 0x35), knob_decode(0x12, 0x34));
    /* Every angle survives the two bytes. */
    for (unsigned a = 0; a < KNOB_COUNTS; ++a) {
        uint8_t b[KNOB_POLL_READS];
        bytes_of((uint16_t)a, b);
        if (knob_decode(b[0], b[1]) != a) {
            CHECK_EQ(knob_decode(b[0], b[1]), a);
            break;
        }
    }
}

TEST_CASE(clockwise_mirrors_the_angle_and_keeps_zero)
{
    CHECK_EQ(knob_clockwise(0), 0);
    CHECK_EQ(knob_clockwise(1), 16383);
    CHECK_EQ(knob_clockwise(16383), 1);
    CHECK_EQ(knob_clockwise(8192), 8192);
    CHECK_EQ(knob_clockwise(knob_clockwise(1234)), 1234);
}

/* ---- the step logic --------------------------------------------------- */

TEST_CASE(the_first_reading_sets_a_reference_and_moves_nothing)
{
    knob_t k;
    CHECK(knob_attach(&k, KNOB_I2C_ADDR));
    CHECK_EQ(knob_feed(&k, true, 3000), 0);
    CHECK_EQ(knob_feed(&k, true, 3010), 10);
    CHECK_EQ(knob_feed(&k, true, 3005), -5);
    CHECK_EQ(knob_feed(&k, true, 3005), 0);
}

TEST_CASE(motion_is_relative_across_the_wrap)
{
    knob_t k;
    CHECK(knob_attach(&k, KNOB_I2C_ADDR));
    (void)knob_feed(&k, true, 16378);
    /* Up through 16383 to 0: +10, not -16374. */
    CHECK_EQ(knob_feed(&k, true, 4), 10);
    /* And back down. */
    CHECK_EQ(knob_feed(&k, true, 16378), -10);
    CHECK_EQ(knob_feed(&k, true, 0), 6);
    CHECK_EQ(knob_feed(&k, true, 16383), -1);
    CHECK_EQ(knob_feed(&k, true, 0), 1);

    /* The steps of one turn, taken 512 at a time, sum to one turn. */
    knob_reset(&k);
    (void)knob_feed(&k, true, 1000);
    int total = 0;
    for (int i = 1; i <= 32; ++i) {
        total += knob_feed(&k, true, (uint16_t)((1000 + 512 * i) % KNOB_COUNTS));
    }
    CHECK_EQ(total, KNOB_COUNTS);
}

TEST_CASE(a_poll_across_the_wrap_counts_clockwise_as_positive)
{
    knob_t k;
    uint8_t b[KNOB_POLL_READS];
    CHECK(knob_attach(&k, KNOB_I2C_ADDR));

    /* The datasheet's angle falls 16383 -> 0 -> ... : a clockwise turn. */
    bytes_of(2, b);
    CHECK_EQ(knob_poll_done(&k, true, b), 0);         /* the reference */
    bytes_of(0, b);
    CHECK_EQ(knob_poll_done(&k, true, b), 2);
    bytes_of(16383, b);
    CHECK_EQ(knob_poll_done(&k, true, b), 1);
    bytes_of(16380, b);
    CHECK_EQ(knob_poll_done(&k, true, b), 3);

    /* And rising 16383 -> 0 -> 5: counter-clockwise. */
    bytes_of(16383, b);
    CHECK_EQ(knob_poll_done(&k, true, b), -3);
    bytes_of(0, b);
    CHECK_EQ(knob_poll_done(&k, true, b), -1);
    bytes_of(5, b);
    CHECK_EQ(knob_poll_done(&k, true, b), -5);

    /* At 0 and at the largest angle alike. */
    knob_reset(&k);
    bytes_of(0, b);
    CHECK_EQ(knob_poll_done(&k, true, b), 0);
    bytes_of(0, b);
    CHECK_EQ(knob_poll_done(&k, true, b), 0);
    knob_reset(&k);
    bytes_of(16383, b);
    CHECK_EQ(knob_poll_done(&k, true, b), 0);
    bytes_of(16383, b);
    CHECK_EQ(knob_poll_done(&k, true, b), 0);
}

TEST_CASE(a_reading_out_of_range_is_taken_modulo_a_turn)
{
    knob_t k;
    CHECK(knob_attach(&k, KNOB_I2C_ADDR));
    (void)knob_feed(&k, true, 10);
    CHECK_EQ(knob_feed(&k, true, KNOB_COUNTS + 20), 10);
}

TEST_CASE(a_knob_that_stops_answering_holds_no_value)
{
    knob_t k;
    CHECK(knob_attach(&k, KNOB_I2C_ADDR));
    (void)knob_feed(&k, true, 500);
    CHECK_EQ(knob_feed(&k, true, 510), 10);

    /* No answer: no motion, and the reference goes. */
    CHECK_EQ(knob_feed(&k, false, 0), 0);
    CHECK_EQ(knob_feed(&k, false, 2000), 0);
    CHECK(!k.have_ref);

    /* The knob came back somewhere else: that is a new reference, not a
     * 1490-step turn. */
    CHECK_EQ(knob_feed(&k, true, 2000), 0);
    CHECK_EQ(knob_feed(&k, true, 2003), 3);
}

TEST_CASE(a_nack_or_a_timeout_leaves_the_knob_idle)
{
    knob_t k;
    uint8_t b[KNOB_POLL_READS];
    CHECK(knob_attach(&k, KNOB_I2C_ADDR));
    bytes_of(1000, b);
    (void)knob_poll_done(&k, true, b);
    bytes_of(990, b);
    CHECK_EQ(knob_poll_done(&k, true, b), 10);

    /* The bus driver's NACK and its 5 ms timeout both end as "not
     * answered": whatever the bytes hold, nothing moves. */
    bytes_of(0, b);
    for (int i = 0; i < 50; ++i) {
        CHECK_EQ(knob_poll_done(&k, false, b), 0);
    }
    CHECK(!k.have_ref);
    CHECK_EQ(knob_poll_done(&k, false, NULL), 0);
    CHECK_EQ(knob_poll_done(&k, true, NULL), 0);

    /* Back: a new reference first. */
    bytes_of(5000, b);
    CHECK_EQ(knob_poll_done(&k, true, b), 0);
    bytes_of(4999, b);
    CHECK_EQ(knob_poll_done(&k, true, b), 1);

    /* Through the bus model: no sensor fitted.  The knob has no reference,
     * so it probes; the probe is not acknowledged and no read is made. */
    bus_t bus;
    bus_up(&bus, false, 0);
    knob_reset(&k);
    bool answered = true;
    CHECK(knob_poll_probe_first(&k));
    CHECK_EQ(poll(&k, &bus, &answered), 0);
    CHECK(!answered);
    CHECK_EQ(bus.probes, 1);
    CHECK_EQ(bus.reads, 0);
    CHECK(!k.have_ref);
}

TEST_CASE(a_sensor_that_goes_costs_one_unanswered_read_then_only_probes)
{
    knob_t k;
    bus_t bus;
    bus_up(&bus, true, 5000);
    CHECK(knob_attach(&k, KNOB_I2C_ADDR));

    /* First poll: probe, then the three reads; then reads only. */
    bool answered = false;
    CHECK_EQ(poll(&k, &bus, &answered), 0);
    CHECK(answered);
    CHECK_EQ(bus.probes, 1);
    CHECK_EQ(bus.reads, KNOB_POLL_READS);
    CHECK(!knob_poll_probe_first(&k));
    for (int i = 0; i < 5; ++i) {
        (void)poll(&k, &bus, &answered);
    }
    CHECK_EQ(bus.probes, 1);
    CHECK_EQ(bus.reads, 6 * KNOB_POLL_READS);

    /* The sensor goes.  The next poll still reads and its first read is
     * not answered (the driver logs that one); every poll after it probes
     * and reads nothing. */
    bus.mt6701 = false;
    CHECK_EQ(poll(&k, &bus, &answered), 0);
    CHECK(!answered);
    CHECK_EQ(bus.reads, 6 * KNOB_POLL_READS + 1);
    for (int i = 0; i < 20; ++i) {
        CHECK_EQ(poll(&k, &bus, &answered), 0);
        CHECK(!answered);
    }
    CHECK_EQ(bus.probes, 1 + 20);
    CHECK_EQ(bus.reads, 6 * KNOB_POLL_READS + 1);

    /* Back, turned: the probe answers, the reads set a new reference. */
    bus.mt6701 = true;
    bus.angle = 9000;
    CHECK_EQ(poll(&k, &bus, &answered), 0);
    CHECK(answered);
    CHECK(k.have_ref);
    bus.angle = 8990;
    CHECK_EQ(poll(&k, &bus, &answered), 10);
    CHECK_EQ(bus.to_reserved, 0);
    CHECK_EQ(bus.to_other, 0);
}

TEST_CASE(a_high_byte_that_moved_between_the_reads_is_no_reading)
{
    knob_t k;
    uint8_t b[KNOB_POLL_READS];
    CHECK(knob_attach(&k, KNOB_I2C_ADDR));
    bytes_of(70, b);
    CHECK_EQ(knob_poll_done(&k, true, b), 0);          /* the reference */
    const uint16_t ref = k.ref;

    /* A tear across 63 -> 64: 0x03 read at 63 (0x00), 0x04 read at 64
     * (Angle[5:0] = 0), 0x03 read again at 64 (0x01).  Decoded alone the
     * first two bytes say 0, 64 counts off.  Refused, reference kept. */
    b[0] = 0x00;
    b[1] = 0x00;
    b[2] = 0x01;
    CHECK_EQ(knob_poll_done(&k, true, b), 0);
    CHECK(k.have_ref);
    CHECK_EQ(k.ref, ref);
    /* And the other way, 64 -> 63. */
    b[0] = 0x01;
    b[1] = 0xFC;
    b[2] = 0x00;
    CHECK_EQ(knob_poll_done(&k, true, b), 0);
    CHECK_EQ(k.ref, ref);

    /* The stable reading after it counts from the kept reference: 70 -> 64
     * is 6 counts clockwise (the angle falls). */
    bytes_of(64, b);
    CHECK_EQ(knob_poll_done(&k, true, b), 6);
    bytes_of(63, b);
    CHECK_EQ(knob_poll_done(&k, true, b), 1);
    /* Equal 0x03 bytes with any 0x04 are taken. */
    b[1] = 0xFF;
    CHECK_EQ(knob_poll_done(&k, true, b), 0);           /* 63 again */
}

TEST_CASE(a_step_over_a_quarter_turn_is_a_glitch_and_moves_nothing)
{
    knob_t k;
    CHECK(knob_attach(&k, KNOB_I2C_ADDR));
    (void)knob_feed(&k, true, 1000);
    /* The largest step still taken, in both directions. */
    CHECK_EQ(knob_feed(&k, true, 1000 + KNOB_MAX_STEP), KNOB_MAX_STEP);
    CHECK_EQ(knob_feed(&k, true, 1000), -KNOB_MAX_STEP);
    /* One more is not. */
    CHECK_EQ(knob_feed(&k, true, 1000 + KNOB_MAX_STEP + 1), 0);
    /* And it was taken as the new reference. */
    CHECK_EQ(knob_feed(&k, true, 1000 + KNOB_MAX_STEP + 4), 3);
    CHECK_EQ(knob_feed(&k, true, 1000 + KNOB_MAX_STEP + 4 - KNOB_MAX_STEP - 1),
             0);
    /* Half a turn is ambiguous in direction and is refused the same way. */
    knob_reset(&k);
    (void)knob_feed(&k, true, 0);
    CHECK_EQ(knob_feed(&k, true, KNOB_COUNTS / 2), 0);
}

/* ---- the bus ---------------------------------------------------------- */

TEST_CASE(an_address_the_board_answers_at_is_refused_and_never_sent)
{
    knob_t k;
    bus_t bus;
    bus_up(&bus, true, 100);

    static const unsigned refused[] = {
        0x00, 0x03, 0x14, 0x5D, 0x78, 0x7F, 0x80, 0xFF, 0x15D,
    };
    for (size_t i = 0; i < sizeof refused / sizeof refused[0]; ++i) {
        bus_up(&bus, true, 100);
        CHECK(!knob_attach(&k, refused[i]));
        CHECK_EQ(knob_poll_addr(&k), -1);
        for (int n = 0; n < 20; ++n) {
            bool answered = true;
            CHECK_EQ(poll(&k, &bus, &answered), 0);
            CHECK(!answered);
        }
        CHECK_EQ(bus.xfers, 0);
        CHECK(!k.have_ref);
        CHECK(!knob_poll_probe_first(&k));
        /* Bytes handed to a detached knob move nothing either. */
        uint8_t b[KNOB_POLL_READS];
        bytes_of(1000, b);
        CHECK_EQ(knob_poll_done(&k, true, b), 0);
        bytes_of(900, b);
        CHECK_EQ(knob_poll_done(&k, true, b), 0);
        CHECK(!k.have_ref);
    }
    /* A refused attach after a good one takes the address away. */
    CHECK(knob_attach(&k, KNOB_I2C_ADDR));
    CHECK_EQ(knob_poll_addr(&k), KNOB_I2C_ADDR);
    CHECK(!knob_attach(&k, 0x5D));
    CHECK_EQ(knob_poll_addr(&k), -1);
}

TEST_CASE(a_turning_knob_reaches_only_its_own_address)
{
    knob_t k;
    bus_t bus;
    bus_up(&bus, true, 300);
    CHECK(knob_attach(&k, KNOB_I2C_ADDR));

    /* Clockwise, 100 counts a poll, through 0 and on: 400 polls, 40000
     * counts, which is twice across the wrap. */
    int total = 0;
    bool answered = false;
    CHECK_EQ(poll(&k, &bus, &answered), 0);
    CHECK(answered);
    for (int i = 0; i < 400; ++i) {
        bus.angle = (uint16_t)((bus.angle + KNOB_COUNTS - 100) % KNOB_COUNTS);
        total += poll(&k, &bus, &answered);
        CHECK(answered);
    }
    CHECK_EQ(total, 40000);
    CHECK_EQ(bus.probes, 1);
    CHECK_EQ(bus.reads, 401 * KNOB_POLL_READS);
    CHECK_EQ(bus.to_reserved, 0);
    CHECK_EQ(bus.to_other, 0);
}

TEST_CASE(enabling_the_knob_with_no_sensor_changes_nothing_else)
{
    settings_set_store(NULL);
    settings_init();
    float before[SETTING_COUNT];
    for (int i = 0; i < SETTING_COUNT; ++i) {
        before[i] = settings_get((setting_id_t)i);
    }
    CHECK(!settings_get_bool(SET_KNOB_EN));

    knob_t k;
    bus_t bus;
    bus_up(&bus, false, 0);
    CHECK(knob_attach(&k, KNOB_I2C_ADDR));

    /* Off: the task polls nothing (knob_task.c gates on the setting). */
    int total = 0;
    for (int i = 0; i < 50; ++i) {
        if (settings_get_bool(SET_KNOB_EN)) {
            total += poll(&k, &bus, NULL);
        }
    }
    CHECK_EQ(bus.xfers, 0);

    /* On, through the key as the SETUP row sets it, for 10 s of idle
     * polls with no sensor there. */
    settings_adjust(SET_KNOB_EN, 1);
    CHECK(settings_get_bool(SET_KNOB_EN));
    for (int i = 0; i < 10000 / KNOB_POLL_IDLE_MS; ++i) {
        if (settings_get_bool(SET_KNOB_EN)) {
            total += poll(&k, &bus, NULL);
        }
    }
    CHECK_EQ(total, 0);
    CHECK(!k.have_ref);
    /* One unacknowledged probe a poll, no register read: the driver logs
     * nothing for a probe. */
    CHECK_EQ(bus.probes, 10000 / KNOB_POLL_IDLE_MS);
    CHECK_EQ(bus.reads, 0);
    CHECK_EQ(bus.to_reserved, 0);
    CHECK_EQ(bus.to_other, 0);
    for (int i = 0; i < SETTING_COUNT; ++i) {
        if (i != SET_KNOB_EN && settings_get((setting_id_t)i) != before[i]) {
            CHECK_EQ(i, -1);    /* names the setting that moved */
        }
    }
    settings_reset_all();
}

TEST_CASE(a_span_fraction_is_degrees_over_the_scale)
{
    /* A quarter turn, 90 degrees, on a 270 degree scale is a third. */
    CHECK_NEAR(knob_span_fraction(KNOB_COUNTS / 4, 270), 1.0f / 3.0f, 0.0001f);
    CHECK_NEAR(knob_span_fraction(-KNOB_COUNTS / 4, 270), -1.0f / 3.0f, 0.0001f);
    /* The scale itself is a full span. */
    CHECK_NEAR(knob_span_fraction(KNOB_COUNTS * 270 / 360, 270), 1.0f, 0.0001f);
    CHECK_NEAR(knob_span_fraction(0, 270), 0.0f, 0.0f);
    /* One step: 360/16384 degrees. */
    CHECK_NEAR(knob_span_fraction(1, 360), 1.0f / 16384.0f, 1e-8f);
}

TEST_CASE(the_scale_is_clamped_to_the_settings_range)
{
    const float lo = knob_span_fraction(100, KNOB_SCALE_DEG_MIN);
    const float hi = knob_span_fraction(100, KNOB_SCALE_DEG_MAX);
    CHECK_NEAR(knob_span_fraction(100, 1), lo, 1e-9f);
    CHECK_NEAR(knob_span_fraction(100, -50), lo, 1e-9f);
    CHECK_NEAR(knob_span_fraction(100, 100000), hi, 1e-9f);
    CHECK(lo > hi);
}

TEST_CASE(the_settings_agree_with_the_constants)
{
    settings_set_store(NULL);
    settings_init();
    const setting_def_t *en = settings_def(SET_KNOB_EN);
    const setting_def_t *sc = settings_def(SET_KNOB_SCALE);
    CHECK_EQ(en->type, SET_TYPE_BOOL);
    CHECK(!settings_get_bool(SET_KNOB_EN));            /* off by default */
    CHECK_EQ(settings_get_int(SET_KNOB_SCALE), KNOB_SCALE_DEG_DEFAULT);
    CHECK_EQ((int)sc->min, KNOB_SCALE_DEG_MIN);
    CHECK_EQ((int)sc->max, KNOB_SCALE_DEG_MAX);
    CHECK_EQ(en->cat, SET_CAT_APP);
    CHECK_EQ(sc->cat, SET_CAT_APP);

    settings_set(SET_KNOB_SCALE, 5000.0f);
    CHECK_EQ(settings_get_int(SET_KNOB_SCALE), KNOB_SCALE_DEG_MAX);
    settings_set(SET_KNOB_SCALE, 0.0f);
    CHECK_EQ(settings_get_int(SET_KNOB_SCALE), KNOB_SCALE_DEG_MIN);
    settings_adjust(SET_KNOB_EN, 1);
    CHECK(settings_get_bool(SET_KNOB_EN));
    settings_reset_all();
    CHECK(!settings_get_bool(SET_KNOB_EN));
}

static servo_cmd_t servo_took(void)
{
    servo_cmd_t c = { .kind = SERVO_CMD_NONE };
    servo_screen_take(&c);
    return c;
}

/*
 * The panel's order.  Frame N: a tap on the dial is held, and the knob's
 * turn is applied at the end of the frame.  Frame N+1: the knob's frame
 * starts, a stop ends the arm, the bench reports disarmed, and then the
 * first drain finds touch events lost: the knob's command is withdrawn and
 * every gesture is cancelled.
 *
 * The withdrawal puts back what the knob found, and the knob found a held
 * output.  The bench is disarmed by then and holds nothing, so a drag on
 * SPEED says no position: on the disarmed bench, and after the next arm.
 */
TEST_CASE(a_knob_command_withdrawn_after_a_stop_leaves_nothing_held)
{
    ui_theme_set(UI_THEME_DARK);
    settings_set_store(NULL);
    settings_init();
    ui_router_init();
    for (int i = 0; i < SPLASH_STEP_COUNT; ++i) {
        splash_screen_set((splash_step_t)i, SPLASH_OK, "");
    }
    ui_router_tick(2.0f);
    ui_router_goto(SCREEN_OVERVIEW);
    ui_router_goto(SCREEN_SERVO);
    feed_reset();
    servo_screen_set_armed(true);
    while (servo_took().kind != SERVO_CMD_NONE) { }

    /* The dial at 40 deg, 110 px from the shaft (servo_spots.h). */
    const float k = 3.14159265358979f / 180.0f;
    const int x = SV_SHAFT_X + (int)(110.0f * cosf(40.0f * k) + 0.5f);
    const int y = UI_BAND_H + SV_SHAFT_Y - (int)(110.0f * sinf(40.0f * k) + 0.5f);
    feed_tap(FEED_LONE, x, y);
    CHECK_EQ(servo_took().kind, SERVO_CMD_POSITION);
    const uint16_t held = servo_screen_commanded();

    servo_screen_knob_frame();
    servo_screen_knob(0.1f);                     /* end of frame N */
    CHECK(servo_screen_commanded() != held);

    servo_screen_knob_frame();                   /* frame N+1 */
    servo_screen_cancel_arm();
    servo_screen_set_armed(false);
    servo_screen_knob_cancel();
    ui_router_cancel_gestures();
    CHECK_EQ(servo_took().kind, SERVO_CMD_NONE);
    CHECK_EQ(servo_screen_commanded(), held);

    /* SPEED: track x 514..781, panel y 344..365. */
    finger(FEED_LONE, SV_RC_X + 44, UI_BAND_H + SV_SPEED_Y);
    glide(FEED_LONE, SV_RC_X + 104, UI_BAND_H + SV_SPEED_Y, 8);
    lift(FEED_LONE);
    const servo_cmd_t c = servo_took();
    CHECK_EQ(c.kind, SERVO_CMD_NONE);

    servo_screen_set_armed(true);                /* the next arm */
    while (servo_took().kind != SERVO_CMD_NONE) { }
    finger(FEED_LONE, SV_RC_X + 44, UI_BAND_H + SV_SPEED_Y);
    glide(FEED_LONE, 660, UI_BAND_H + 306, 8);
    lift(FEED_LONE);
    CHECK(servo_took().kind != SERVO_CMD_POSITION);
}

int main(void)
{
    RUN(the_address_and_the_registers_are_the_datasheets);
    RUN(decode_reads_fourteen_bits_over_two_registers);
    RUN(clockwise_mirrors_the_angle_and_keeps_zero);
    RUN(the_first_reading_sets_a_reference_and_moves_nothing);
    RUN(motion_is_relative_across_the_wrap);
    RUN(a_poll_across_the_wrap_counts_clockwise_as_positive);
    RUN(a_reading_out_of_range_is_taken_modulo_a_turn);
    RUN(a_knob_that_stops_answering_holds_no_value);
    RUN(a_nack_or_a_timeout_leaves_the_knob_idle);
    RUN(a_sensor_that_goes_costs_one_unanswered_read_then_only_probes);
    RUN(a_high_byte_that_moved_between_the_reads_is_no_reading);
    RUN(a_step_over_a_quarter_turn_is_a_glitch_and_moves_nothing);
    RUN(an_address_the_board_answers_at_is_refused_and_never_sent);
    RUN(a_turning_knob_reaches_only_its_own_address);
    RUN(enabling_the_knob_with_no_sensor_changes_nothing_else);
    RUN(a_span_fraction_is_degrees_over_the_scale);
    RUN(the_scale_is_clamped_to_the_settings_range);
    RUN(the_settings_agree_with_the_constants);
    RUN(a_knob_command_withdrawn_after_a_stop_leaves_nothing_held);
    return test_summary("knob");
}
