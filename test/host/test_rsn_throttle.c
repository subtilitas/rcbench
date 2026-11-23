/*
 * Throttle outputs on the remote servo node's pins (board 3, PLAN 2.8):
 * the IOmcu's output layer (firmware/iomcu/src/outputs_hw.c) over fake pin
 * drivers that keep every pin's state, run by a model of the main loop's
 * safety steps (safety_gate_step(), outputs_off(), outputs_arm_roles(),
 * outputs_step(), outputs_hw_service()) on a 1ms clock; the SENSE page
 * with board 3's fixed map; the sensor schedule with board 3's parts.
 *
 * Under test: plain DShot on S1 (GPIO 16), MOTOR PWM on S3 (GPIO 20), PPM
 * on S5 (GPIO 24) and bidirectional DShot on S9 (GPIO 32, slot 9, on
 * OUTPUTS_HI) driven while ARMED and at rest after a DISARM (SAFE), the
 * node's STOP (ARM 0 and its heartbeat edges stopped), an error (the arm
 * latched and outputs_off()), the link failsafe at 200ms of silence and
 * the heartbeat lost at 150ms: in the pass of the event, or not before
 * the timeout and in the pass that reaches it, every DShot pin stopped
 * low, the PPM frame stopped, the PWM pulse 0, no frame after it; BENCH
 * filled from S9's bidirectional telemetry, rpm with the poles, voltage,
 * current and temperature, through bench_state_from_telem() as the IOmcu's
 * 50Hz sample does, and emptied at the end of the run; the INA228 at
 * 0x40, 0x41 and 0x42 refused with the node's map in force, 0x43 to 0x45
 * taken; the schedule with the 3 INA3221 (9 channels on 20.0mΩ, no FAST
 * channel) and the INA228 at 0x45: each channel's current 111 times a
 * second, the INA228's current and voltage 500 times, each tick inside
 * 1ms of bus time at 400kHz.
 *
 * The pin drivers are fakes: a bind succeeds, a frame goes nowhere, a poll
 * returns the reply the case queued.  The clock is pico_stub/pico/
 * stdlib.h's.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "greatest.h"

#include "bench_state.h"
#include "board_fix.h"
#include "dshot.h"
#include "fake_ina.h"
#include "heartbeat.h"
#include "link_dev.h"
#include "link_pages.h"
#include "out_bind.h"
#include "out_dshot.h"
#include "out_ppm.h"
#include "out_pwm.h"
#include "outputs.h"
#include "outputs_hw.h"
#include "outputs_pages.h"
#include "pico/stdlib.h"
#include "port_park.h"
#include "rsn_pins.h"
#include "safety_gate.h"
#include "sense_map.h"
#include "sense_page.h"
#include "sense_sched.h"
#include "sense_svc.h"

uint64_t pico_stub_us;

#define S1 ((uint8_t)RSN_PIN_OUT(0))      /* GPIO 16: plain DShot   */
#define S3 ((uint8_t)RSN_PIN_OUT(2))      /* GPIO 20: MOTOR PWM     */
#define S5 ((uint8_t)RSN_PIN_OUT(4))      /* GPIO 24: PPM           */
#define S9 ((uint8_t)RSN_PIN_OUT(8))      /* GPIO 32: bidirectional */
#define BIT(pin) ((uint64_t)1u << (pin))

/* ------------------------------------------------------ the pin drivers */

/* Each pin: bound, resting (DShot low, PPM stopped, PWM at 0), frames
 * sent since the case's mark, the last PWM pulse. */
static struct {
    uint64_t bound;
    uint64_t rest;
    unsigned frames[64];
    uint16_t pulse[64];
    bool     reply_queued;
    dshot_telem_t reply;
} f;

bool out_dshot_bind(uint8_t pin, uint16_t rate_kbit, bool bidirectional)
{
    (void)rate_kbit;
    (void)bidirectional;
    f.bound |= BIT(pin);
    f.rest  |= BIT(pin);
    return true;
}
void out_dshot_release(uint8_t pin)
{
    f.bound &= ~BIT(pin);
}
bool out_dshot_wake(uint8_t pin)
{
    /* S9 is bidirectional: released once from its rest before a frame. */
    if (pin != S9 || (f.rest & BIT(pin)) == 0u) {
        return false;
    }
    f.rest &= ~BIT(pin);
    return true;
}
void out_dshot_send(uint8_t pin, uint16_t value, bool telemetry)
{
    (void)value;
    (void)telemetry;
    f.rest &= ~BIT(pin);
    f.frames[pin]++;
}
void out_dshot_stop(uint8_t pin)
{
    f.rest |= BIT(pin);
}
bool out_dshot_poll(uint8_t pin, bool edt, dshot_telem_t *out)
{
    (void)edt;
    if (pin != S9 || !f.reply_queued || out == NULL) {
        return false;
    }
    *out = f.reply;
    f.reply_queued = false;
    return true;
}
bool out_ppm_bind(uint8_t pin, uint8_t channels, uint16_t rate_hz)
{
    (void)channels;
    (void)rate_hz;
    f.bound |= BIT(pin);
    f.rest  |= BIT(pin);
    return true;
}
void out_ppm_release(uint8_t pin)
{
    f.bound &= ~BIT(pin);
}
bool out_ppm_write(uint8_t pin, const uint16_t *channel_us, uint8_t channels)
{
    (void)channel_us;
    (void)channels;
    f.rest &= ~BIT(pin);
    f.frames[pin]++;
    return true;
}
void out_ppm_stop(uint8_t pin)
{
    f.rest |= BIT(pin);
}
bool out_pwm_bind(uint8_t pin, uint16_t rate_hz)
{
    (void)rate_hz;
    f.bound |= BIT(pin);
    f.rest  |= BIT(pin);
    return true;
}
void out_pwm_release(uint8_t pin)
{
    f.bound &= ~BIT(pin);
}
void out_pwm_write(uint8_t pin, uint16_t pulse_us)
{
    f.pulse[pin] = pulse_us;
    if (pulse_us == 0u) {
        f.rest |= BIT(pin);
    } else {
        f.rest &= ~BIT(pin);
        f.frames[pin]++;
    }
}
bool out_pwm_write_stamped(uint8_t pin, uint16_t pulse_us, uint64_t *frame_us)
{
    out_pwm_write(pin, pulse_us);
    if (frame_us != NULL) {
        *frame_us = pico_stub_us;
    }
    return true;
}
bool port_park(uint8_t pin)
{
    (void)pin;
    return true;
}
void port_unpark(uint8_t pin) { (void)pin; }

/* --------------------------------------------------- the main loop model */

static outputs_t       g_bank;
static uint16_t        g_control[LINK_CT_COUNT];
static link_dev_t      g_dev;
static heartbeat_mon_t g_beat;
static uint32_t        g_now;
static bool            g_link_up;     /* the panel's requests arrive */
static bool            g_beat_up;     /* the heartbeat line edges    */

/* The four throttle and surface outputs on their pins. */
static const uint8_t k_pins[] = { S1, S3, S5, S9 };
#define THR_S1  0u                     /* channels */
#define THR_S3  1u
#define PPM_CH0 2u                     /* 2 to 5, surfaces */
#define THR_S9  8u

static void at_ms(uint32_t ms)
{
    g_now = ms;
    pico_stub_us = (uint64_t)ms * 1000u;
}

static void mark(void)
{
    memset(f.frames, 0, sizeof(f.frames));
}

static bool all_at_rest(void)
{
    for (size_t i = 0; i < sizeof(k_pins); ++i) {
        if ((f.rest & BIT(k_pins[i])) == 0u) {
            return false;
        }
    }
    return f.pulse[S3] == 0u;
}

static unsigned frames_sent(void)
{
    unsigned n = 0u;
    for (size_t i = 0; i < sizeof(k_pins); ++i) {
        n += f.frames[k_pins[i]];
    }
    return n;
}

/* outputs_off() as main.c's: disarmed, ARM and THROTTLE 0, the pins now. */
static void outputs_off_model(void)
{
    outputs_arm(&g_bank, false, g_now);
    g_control[LINK_CT_ARM] = 0u;
    g_control[LINK_CT_THROTTLE] = 0u;
    outputs_hw_service(&g_bank);
}

/* One pass of the IOmcu's main loop, at g_now: the request and the edge
 * the case lets through, the gate, the arm, the commands' keep-alive, the
 * step and the pins. */
static void pass(void)
{
    if (g_link_up) {
        g_dev.last_request_ms = g_now;
        g_dev.heard = true;
    }
    const bool edge = g_beat_up && (g_now % HEARTBEAT_PERIOD_MS) == 0u;
    const safety_gate_t gate =
        safety_gate_step(&g_beat, edge, &g_dev, g_control, g_now);
    if (gate.off) {
        outputs_off_model();
    }
    outputs_arm_roles(&g_bank, gate.arm ? outputs_arm_roles_of(gate.roles)
                                        : 0u, g_now);
    if (g_link_up) {
        /* The panel's commands, every pass while the link is up. */
        (void)outputs_set(&g_bank, THR_S1, OUT_SPAN / 4u, g_now);
        (void)outputs_set(&g_bank, THR_S3, OUT_SPAN / 4u, g_now);
        (void)outputs_set(&g_bank, THR_S9, OUT_SPAN / 4u, g_now);
        for (uint8_t c = 0; c < 4u; ++c) {
            (void)outputs_set(&g_bank, (uint8_t)(PPM_CH0 + c), OUT_SPAN / 2u,
                              g_now);
        }
    }
    outputs_step(&g_bank, g_now);
    outputs_hw_service(&g_bank);
}

static void passes_to(uint32_t until)
{
    while (g_now != until) {
        at_ms(g_now + 1u);
        pass();
    }
}

/* The node's outputs bound, the line trusted, the latch cleared, ARMED for
 * every role, and 100ms of frames on every pin. */
static void armed_run(void)
{
    memset(&f, 0, sizeof(f));
    at_ms(1000u);
    outputs_init(&g_bank, g_now);
    const out_slot_t s1 = { OUT_DRIVER_DSHOT, THR_S1, 1u, S1, 600u };
    const out_slot_t s3 = { OUT_DRIVER_PWM, THR_S3, 1u, S3, 490u };
    const out_slot_t s5 = { OUT_DRIVER_PPM, PPM_CH0, 4u, S5, 50u };
    const out_slot_t s9 = { OUT_DRIVER_DSHOT_BIDIR, THR_S9, 1u, S9, 600u };
    CHECK(outputs_configure(&g_bank, 0u, &s1));
    CHECK(outputs_configure(&g_bank, 2u, &s3));
    CHECK(outputs_configure(&g_bank, 4u, &s5));
    CHECK(outputs_configure(&g_bank, 8u, &s9));   /* slot 9, OUTPUTS_HI */
    CHECK(outputs_set_role(&g_bank, THR_S1, OUT_ROLE_THROTTLE));
    CHECK(outputs_set_role(&g_bank, THR_S3, OUT_ROLE_THROTTLE));
    CHECK(outputs_set_role(&g_bank, THR_S9, OUT_ROLE_THROTTLE));
    uint16_t rate[OUT_MAX_SLOTS];
    memset(rate, 0, sizeof(rate));
    rate[0] = 600u;
    rate[2] = 490u;
    rate[4] = 50u;
    rate[8] = 600u;
    outputs_hw_init();
    outputs_hw_apply(&g_bank, rate);
    for (uint8_t s = 0; s < OUT_MAX_SLOTS; ++s) {
        CHECK_EQ(outputs_hw_bound(s), s == 0u || s == 2u || s == 4u || s == 8u);
    }
    CHECK_EQ(f.bound, BIT(S1) | BIT(S3) | BIT(S5) | BIT(S9));
    CHECK(all_at_rest());

    memset(g_control, 0, sizeof(g_control));
    link_dev_init(&g_dev, NULL, 0, NULL, g_now);
    heartbeat_mon_init(&g_beat);
    g_link_up = true;
    g_beat_up = true;
    passes_to(1200u);                    /* the line trusted */
    CHECK(g_beat.alive);
    CHECK_EQ(safety_gate_control_write(g_control, LINK_CT_CLEAR, 1u,
                                       &(uint16_t){ LINK_CLEAR_MAGIC },
                                       &g_beat, &g_dev), 0u);
    CHECK_EQ(safety_gate_control_write(g_control, LINK_CT_ARM, 1u,
                                       &(uint16_t){ LINK_ARM_ALL },
                                       &g_beat, &g_dev), 0u);
    mark();
    passes_to(1300u);
    CHECK(outputs_driving(&g_bank));
    for (size_t i = 0; i < sizeof(k_pins); ++i) {
        CHECK(f.frames[k_pins[i]] > 0u);
        CHECK((f.rest & BIT(k_pins[i])) == 0u);
    }
    CHECK(f.pulse[S3] > 0u);
}

/* After the event: every pin at rest and 100 passes without a frame. */
static void quiet_after(void)
{
    CHECK(all_at_rest());
    CHECK(!outputs_driving(&g_bank));
    mark();
    passes_to(g_now + 100u);
    CHECK_EQ(frames_sent(), 0u);
    CHECK(all_at_rest());
}

/* ------------------------------------------------------------ the cases */

/* SAFE: a DISARM written; the pass after it has every pin at rest. */
TEST_CASE(dshot_pins_rest_low_on_safe)
{
    armed_run();
    CHECK_EQ(safety_gate_control_write(g_control, LINK_CT_ARM, 1u,
                                       &(uint16_t){ 0u }, &g_beat, &g_dev),
             0u);
    at_ms(g_now + 1u);
    pass();
    quiet_after();
}

/* The node's STOP: ARM 0 on the UART and its heartbeat edges stopped.
 * The ARM 0 brings the pins to rest at once; the heartbeat's 150ms then
 * latches the arm, so a new ARM is refused until a CLEAR. */
TEST_CASE(dshot_pins_rest_low_on_the_nodes_stop)
{
    armed_run();
    g_beat_up = false;
    CHECK_EQ(safety_gate_control_write(g_control, LINK_CT_ARM, 1u,
                                       &(uint16_t){ 0u }, &g_beat, &g_dev),
             0u);
    at_ms(g_now + 1u);
    pass();
    quiet_after();
    passes_to(g_now + 100u);
    CHECK(!g_beat.alive);
    CHECK(link_dev_arm_latched(&g_dev));
    CHECK_EQ(safety_gate_control_write(g_control, LINK_CT_ARM, 1u,
                                       &(uint16_t){ LINK_ARM_ALL },
                                       &g_beat, &g_dev),
             LINK_NACK_NOT_ARMED);
}

/* An error (a POWER trip, rsn_pass()): the arm latched and outputs_off()
 * in that pass, before the gate; the pins at rest within the pass. */
TEST_CASE(dshot_pins_rest_low_on_an_error)
{
    armed_run();
    at_ms(g_now + 1u);
    link_dev_latch_arm(&g_dev);
    outputs_off_model();
    CHECK(all_at_rest());
    pass();
    quiet_after();
    CHECK(link_dev_arm_latched(&g_dev));
}

/* The link failsafe: the last request at t0; at t0 + 199ms the pins still
 * run, at t0 + 200ms they rest. */
TEST_CASE(dshot_pins_rest_low_on_the_link_failsafe)
{
    armed_run();
    const uint32_t t0 = g_now;
    g_link_up = false;
    passes_to(t0 + LINK_DEV_SILENCE_MS - 1u);
    CHECK(outputs_driving(&g_bank));
    CHECK(!g_dev.failsafe);
    at_ms(t0 + LINK_DEV_SILENCE_MS);
    pass();
    CHECK(g_dev.failsafe);
    quiet_after();
}

/* The heartbeat lost: the last edge at t0; the pins run at t0 + 149ms and
 * rest at t0 + 150ms. */
TEST_CASE(dshot_pins_rest_low_when_the_heartbeat_stops)
{
    armed_run();
    /* The next edge is due at a multiple of 20ms: the last one was at
     * 1300ms, the end of the run. */
    const uint32_t t0 = g_now;
    CHECK_EQ(t0 % HEARTBEAT_PERIOD_MS, 0u);
    g_beat_up = false;
    passes_to(t0 + HEARTBEAT_MAX_GAP_MS - 1u);
    CHECK(outputs_driving(&g_bank));
    at_ms(t0 + HEARTBEAT_MAX_GAP_MS);
    pass();
    CHECK(!g_beat.alive);
    quiet_after();
}

/* BENCH from S9's replies: speed, voltage, current and temperature into
 * the live readings with their bits, as the IOmcu's 50Hz sample takes
 * them; the slot on OUTPUTS_HI reports, and the readings go at the end of
 * the run. */
TEST_CASE(bench_is_filled_from_s9s_bidirectional_telemetry)
{
    armed_run();
    const struct {
        dshot_telem_kind_t kind;
        uint32_t erpm;
        uint8_t  value;
    } replies[] = {
        { DSHOT_TELEM_ERPM, 70000u, 0u },
        { DSHOT_TELEM_VOLTAGE, 0u, 100u },        /* 25.00V at 0.25V a step */
        { DSHOT_TELEM_CURRENT, 0u, 30u },
        { DSHOT_TELEM_TEMPERATURE, 0u, 45u },
    };
    for (size_t i = 0; i < sizeof(replies) / sizeof(replies[0]); ++i) {
        memset(&f.reply, 0, sizeof(f.reply));
        f.reply.kind  = replies[i].kind;
        f.reply.erpm  = replies[i].erpm;
        f.reply.value = replies[i].value;
        f.reply_queued = true;
        at_ms(g_now + 1u);
        pass();
        CHECK(!f.reply_queued);
    }
    /* main.c's sample(): poles 14, so 7 pole pairs. */
    bench_telem_t t;
    memset(&t, 0, sizeof(t));
    t.poles = 14u;
    t.erpm_ok = outputs_hw_erpm(&t.erpm, &t.erpm_age_ms);
    t.voltage.ok = outputs_hw_edt(DSHOT_TELEM_VOLTAGE, &t.voltage.count,
                                  &t.voltage.age_ms);
    t.current.ok = outputs_hw_edt(DSHOT_TELEM_CURRENT, &t.current.count,
                                  &t.current.age_ms);
    t.temperature.ok = outputs_hw_edt(DSHOT_TELEM_TEMPERATURE,
                                      &t.temperature.count,
                                      &t.temperature.age_ms);
    CHECK(t.erpm_ok && t.voltage.ok && t.current.ok && t.temperature.ok);
    CHECK_EQ(t.erpm_age_ms, 3u);
    bench_state_t b;
    memset(&b, 0, sizeof(b));
    bench_state_from_telem(&b, &t);
    CHECK_EQ(b.flags, LINK_BN_RPM_OK | LINK_BN_VOLTAGE_OK
                      | LINK_BN_CURRENT_OK | LINK_BN_TEMP_OK);
    CHECK_NEAR(b.rpm, 10000.0f, 0.0f);
    CHECK_NEAR(b.voltage, 25.0f, 0.0f);
    CHECK_NEAR(b.current, 30.0f, 0.0f);
    CHECK_NEAR(b.temp_esc, 45.0f, 0.0f);

    /* 201ms on with no reply: the speed is stale, the rest is not. */
    at_ms(g_now + BENCH_RPM_STALE_MS + 1u - t.erpm_age_ms);
    CHECK(outputs_hw_erpm(&t.erpm, &t.erpm_age_ms));
    CHECK_EQ(t.erpm_age_ms, BENCH_RPM_STALE_MS + 1u);
    bench_state_from_telem(&b, &t);
    CHECK_EQ(b.flags & LINK_BN_RPM_OK, 0u);
    CHECK((b.flags & LINK_BN_VOLTAGE_OK) != 0u);

    /* The run ends: the readings are forgotten. */
    CHECK_EQ(safety_gate_control_write(g_control, LINK_CT_ARM, 1u,
                                       &(uint16_t){ 0u }, &g_beat, &g_dev),
             0u);
    pass();
    CHECK(!outputs_hw_erpm(&t.erpm, &t.erpm_age_ms));
    CHECK(!outputs_hw_edt(DSHOT_TELEM_VOLTAGE, &t.voltage.count,
                          &t.voltage.age_ms));
}

/* ------------------------------------------------------------- SENSE */

/* The SENSE page with board 3's map in force, as board_fix_store() keeps
 * it at boot, FAST set to @p fast; then a SENSE write of the page's own
 * set-up with the INA228 enabled at @p addr.
 *
 * @return The write's answer. */
static uint8_t node_sense(sense_page_t *pg, const outputs_t *o,
                          uint16_t fast, uint16_t addr)
{
    const outbind_roles_t *r = outbind_roles((uint16_t)OUTBIND_BOARD_RSN);
    out_store_t cfg;
    out_store_cfg_defaults(&cfg);
    board_fix_store(r, &cfg);
    CHECK_EQ(cfg.sense_map_set, 1u);
    cfg.sense_map[LINK_SM_FAST] = fast;
    sense_page_init(pg);
    CHECK_EQ(sense_map_write(pg, 0u, (uint8_t)LINK_SM_KEPT, cfg.sense_map,
                             o, 0u), 0u);
    CHECK(pg->map_set);
    /* The kept set-up on the node's pins, with what the map mirrors into
     * it: ENABLE's INA3221 bit and the frame of registers 8 to 10. */
    uint16_t m[LINK_SN_CONFIG_COUNT];
    sense_page_read(pg, 0u, (uint8_t)LINK_SN_CONFIG_COUNT, m);
    uint16_t c[LINK_SN_CONFIG_COUNT];
    memcpy(c, cfg.sense, sizeof(c));
    memcpy(&c[LINK_SN_I3221_ADDR], &m[LINK_SN_I3221_ADDR],
           3u * sizeof(uint16_t));
    c[LINK_SN_ENABLE] = (uint16_t)((m[LINK_SN_ENABLE] & LINK_SN_EN_I3221)
                                   | LINK_SN_EN_I228);
    c[LINK_SN_I228_ADDR] = addr;
    CHECK(board_fix_write_ok(r, LINK_PAGE_SENSE, 0u,
                             (uint8_t)LINK_SN_CONFIG_COUNT, c));
    return sense_page_write(pg, 0u, (uint8_t)LINK_SN_CONFIG_COUNT, c, o, 0u);
}

/* An INA228 on an INA3221's address, 0x40 to 0x42, is refused; 0x43 to
 * 0x45, the MATEK board's 0x44 and 0x45 among them, are taken. */
TEST_CASE(the_ina228_is_refused_on_an_ina3221s_address)
{
    outputs_t o;
    outputs_init(&o, 0u);
    for (uint16_t addr = 0x40u; addr <= 0x45u; ++addr) {
        sense_page_t pg;
        const uint8_t want = (addr <= 0x42u) ? (uint8_t)LINK_NACK_BAD_VALUE
                                             : 0u;
        CHECK_EQ(node_sense(&pg, &o, LINK_SM_FAST_NONE, addr), want);
        uint16_t got = 0u;
        sense_page_read(&pg, (uint8_t)LINK_SN_I228_ADDR, 1u, &got);
        CHECK_EQ(got, (want == 0u) ? addr : (uint16_t)SENSE_DEFAULT_I228_ADDR);
    }
}

/* The schedule's bus, its clock and board 3's parts. */
static fake_bus_t    fb;
static sense_sched_t sch;
static uint64_t      g_sense_us;

static uint64_t sense_now(void *ctx)
{
    (void)ctx;
    return g_sense_us;
}

static uint64_t bus_clocks(void)
{
    uint64_t n = 0u;
    for (size_t i = 0; i < fb.n; ++i) {
        n += fb.part[i].clocks;
    }
    return n;
}

/* The schedule as core 1 runs it from the page's order (sense_page_cmd()),
 * with FAST at @p fast, against the node's 3 INA3221 and the INA228 at
 * 0x45 on J26, for 1000 ticks of 1ms.  Each part's reads are counted from
 * after the probes; @p worst is the most bus clocks one tick took. */
static void run_schedule(uint16_t fast, fake_part_t **part, fake_part_t **i228,
                         unsigned *worst)
{
    outputs_t o;
    outputs_init(&o, 0u);
    sense_page_t pg;
    CHECK_EQ(node_sense(&pg, &o, fast, 0x45u), 0u);
    sense_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    sense_page_cmd(&pg, &cmd);
    const sense_sched_cfg_t sc = cmd.parts;
    CHECK_EQ(sc.i3221.fitted, 0x07u);
    for (unsigned ch = 0; ch < SENSE_CHANNELS; ++ch) {
        CHECK_EQ(sc.i3221.shunt_uohm[ch], (ch < 9u) ? 20000u : 0u);
    }
    CHECK(sc.ina228_en);
    CHECK_EQ(sc.ina228_addr, 0x45u);

    sense_bus_t scratch;
    fake_bus_init(&fb, &scratch);
    for (unsigned a = 0; a < 3u; ++a) {
        part[a] = fake_add(&fb, FAKE_INA3221, (uint8_t)(0x40u + a), 0.020);
        for (unsigned ch = 0; ch < 3u; ++ch) {
            part[a]->amps[ch] = 0.1 * (double)(3u * a + ch + 1u);
            part[a]->volts[ch] = 6.0;
        }
    }
    *i228 = fake_add(&fb, FAKE_INA228, 0x45u, 200e-6);
    (*i228)->volts[0] = 16.8;
    const sense_sched_io_t io = { { fake_read, fake_write, &fb }, sense_now,
                                  NULL, NULL, NULL };
    g_sense_us = 10000000u;
    sense_sched_init(&sch, &io, &sc);
    for (unsigned k = 0; k < 10u; ++k) {          /* the probes */
        const uint64_t at = g_sense_us;
        sense_sched_tick(&sch);
        g_sense_us = at + 1000u;
    }
    for (unsigned a = 0; a < 3u; ++a) {
        CHECK_EQ(ina3221_state(&sch.i3221[a]), SENSE_PART_ONLINE);
    }
    CHECK_EQ(ina228_state(&sch.i228), SENSE_PART_ONLINE);
    for (size_t i = 0; i < fb.n; ++i) {
        memset(fb.part[i].reads, 0, sizeof(fb.part[i].reads));
    }
    *worst = 0u;
    for (unsigned k = 0; k < 1000u; ++k) {
        const uint64_t at = g_sense_us;
        const uint64_t c0 = bus_clocks();
        sense_sched_tick(&sch);
        const unsigned used = (unsigned)(bus_clocks() - c0);
        *worst = (used > *worst) ? used : *worst;
        g_sense_us = at + 1000u;
    }
    CHECK_EQ((*i228)->reads[INA228_CURRENT], 500u);
    CHECK_EQ((*i228)->reads[INA228_VBUS], 500u);
    CHECK_EQ((*i228)->reads[INA228_ENERGY], 50u);
    CHECK_EQ((*i228)->reads[INA228_CHARGE], 50u);
    for (unsigned a = 0; a < 3u; ++a) {
        for (unsigned ch = 0; ch < 3u; ++ch) {
            const unsigned n_v = part[a]->reads[INA3221_BUS1 + 2u * ch];
            CHECK(n_v == 41u || n_v == 42u);
        }
        CHECK_EQ(part[a]->reads[INA3221_MASK_ENABLE], 25u);
    }
    CHECK_EQ(fb.bad_width, 0u);
}

/* The 3 INA3221 at 0x40 to 0x42, every channel on 20.0mΩ, and the INA228
 * at 0x45 on J26: without a FAST channel each channel's current is read
 * 111 or 112 times a second (1000 / 9); with S1 as FAST, S1 1000 times and
 * each other 125 times (1000 / 8).  Either way the INA228's current and
 * voltage 500 times each, ENERGY and CHARGE 50 times, each bus voltage 41
 * or 42 times, and no tick over 1ms of bus time at 2.5µs a clock: 570µs
 * (228 clocks) at most without FAST, 690µs (276) with it, as with one
 * part. */
TEST_CASE(the_schedule_reads_nine_channels_and_the_ina228)
{
    fake_part_t *part[3];
    fake_part_t *i228 = NULL;
    unsigned worst = 0u;
    run_schedule(LINK_SM_FAST_NONE, part, &i228, &worst);
    for (unsigned a = 0; a < 3u; ++a) {
        for (unsigned ch = 0; ch < 3u; ++ch) {
            const unsigned n_i = part[a]->reads[INA3221_SHUNT1 + 2u * ch];
            CHECK(n_i == 111u || n_i == 112u);
        }
    }
    CHECK(worst * 25u < 10000u);
    CHECK_EQ(worst, 228u);

    run_schedule(0u, part, &i228, &worst);
    for (unsigned a = 0; a < 3u; ++a) {
        for (unsigned ch = 0; ch < 3u; ++ch) {
            const unsigned n_i = part[a]->reads[INA3221_SHUNT1 + 2u * ch];
            CHECK_EQ(n_i, (a == 0u && ch == 0u) ? 1000u : 125u);
        }
    }
    CHECK(worst * 25u < 10000u);
    CHECK_EQ(worst, 276u);
}

int main(void)
{
    RUN(dshot_pins_rest_low_on_safe);
    RUN(dshot_pins_rest_low_on_the_nodes_stop);
    RUN(dshot_pins_rest_low_on_an_error);
    RUN(dshot_pins_rest_low_on_the_link_failsafe);
    RUN(dshot_pins_rest_low_when_the_heartbeat_stops);
    RUN(bench_is_filled_from_s9s_bidirectional_telemetry);
    RUN(the_ina228_is_refused_on_an_ina3221s_address);
    RUN(the_schedule_reads_nine_channels_and_the_ina228);
    return test_summary("rsn_throttle");
}
