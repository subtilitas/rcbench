/*
 * The age of an ESC's last reading, as the IOmcu's output layer
 * (firmware/iomcu/src/outputs_hw.c) reports it, across the wrap of the
 * 32-bit millisecond clock at 2^32 ms (49.7 days).
 *
 * outputs_hw_erpm() and outputs_hw_edt() give the age as now - stamp in
 * uint32_t.  The bench's stale rules (bench_state_from_telem(): 200 ms for
 * speed, 2,000 ms for the extended kinds) read that age, so an age that
 * jumps by 2^32 ms at the wrap would blank a live reading, and one that
 * stays small would keep a dead one.  Each case runs at tick 0 and with the
 * reading taken 50 ms before the wrap (tick_wrap.h).
 *
 * The pin drivers are fakes: a bind succeeds, a frame goes nowhere, and a
 * poll returns the one reply the case queued.  The clock is
 * pico_stub/pico/stdlib.h's.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "greatest.h"
#include "tick_wrap.h"

#include "dshot.h"
#include "out_dshot.h"
#include "out_ppm.h"
#include "out_pwm.h"
#include "outputs.h"
#include "outputs_hw.h"
#include "pico/stdlib.h"

uint64_t pico_stub_us;

/* The reply the next poll returns, once. */
static bool          g_reply_queued;
static dshot_telem_t g_reply;

bool out_dshot_bind(uint8_t pin, uint16_t rate_kbit, bool bidirectional)
{
    (void)pin;
    (void)rate_kbit;
    (void)bidirectional;
    return true;
}

void out_dshot_release(uint8_t pin) { (void)pin; }

void out_dshot_send(uint8_t pin, uint16_t value, bool telemetry)
{
    (void)pin;
    (void)value;
    (void)telemetry;
}

void out_dshot_stop(uint8_t pin) { (void)pin; }

bool out_dshot_poll(uint8_t pin, bool edt, dshot_telem_t *out)
{
    (void)pin;
    (void)edt;
    if (!g_reply_queued || out == NULL) {
        return false;
    }
    *out = g_reply;
    g_reply_queued = false;
    return true;
}

bool out_ppm_bind(uint8_t pin, uint8_t channels, uint16_t rate_hz)
{
    (void)pin;
    (void)channels;
    (void)rate_hz;
    return true;
}

void out_ppm_release(uint8_t pin) { (void)pin; }

bool out_ppm_write(uint8_t pin, const uint16_t *channel_us, uint8_t channels)
{
    (void)pin;
    (void)channel_us;
    (void)channels;
    return true;
}

void out_ppm_stop(uint8_t pin) { (void)pin; }

bool out_pwm_bind(uint8_t pin, uint16_t rate_hz)
{
    (void)pin;
    (void)rate_hz;
    return true;
}

void out_pwm_release(uint8_t pin) { (void)pin; }

void out_pwm_write(uint8_t pin, uint16_t pulse_us)
{
    (void)pin;
    (void)pulse_us;
}

bool out_pwm_write_stamped(uint8_t pin, uint16_t pulse_us, uint64_t *frame_us)
{
    (void)pin;
    (void)pulse_us;
    if (frame_us != NULL) {
        *frame_us = pico_stub_us;
    }
    return true;
}

/* The millisecond clock at @p ms, which may be past the wrap. */
static void at_ms(uint32_t ms)
{
    pico_stub_us = (uint64_t)ms * 1000u;
}

/* A bank with one bidirectional DShot slot on pin 2 at 600 kbit/s, bound by
 * the output layer.  Disarmed: the layer still reads each reply. */
static outputs_t g_bank;

static void bind_one_bidir_slot(uint32_t now_ms)
{
    at_ms(now_ms);
    g_reply_queued = false;
    outputs_init(&g_bank, now_ms);
    const out_slot_t slot = { OUT_DRIVER_DSHOT_BIDIR, 0u, 1u, 2u, 600u };
    CHECK(outputs_configure(&g_bank, 0u, &slot));
    uint16_t rate[OUT_MAX_SLOTS];
    memset(rate, 0, sizeof(rate));
    rate[0] = 600u;
    outputs_hw_init();
    outputs_hw_apply(&g_bank, rate);
    CHECK(outputs_hw_bound(0u));
}

/* One reply read by one pass of the layer. */
static void hear(const dshot_telem_t *t)
{
    g_reply        = *t;
    g_reply_queued = true;
    outputs_hw_service(&g_bank);
    CHECK(!g_reply_queued);
}

/* A speed reading taken at t0 is 0 ms old at t0, 49 ms old 1 ms before the
 * next tick 0, 50 ms at it and 250 ms later on: the age grows by the time
 * passed and by nothing else. */
static void a_speed_reading_ages_by_the_time_passed(uint32_t t0)
{
    bind_one_bidir_slot(t0);
    dshot_telem_t t;
    memset(&t, 0, sizeof(t));
    t.kind = DSHOT_TELEM_ERPM;
    t.erpm = 12345u;
    hear(&t);

    uint32_t erpm = 0u;
    uint32_t age  = 0xFFFFFFFFu;
    CHECK(outputs_hw_erpm(&erpm, &age));
    CHECK_EQ(erpm, 12345u);
    CHECK_EQ(age, 0u);

    at_ms(t0 + 49u);
    CHECK(outputs_hw_erpm(&erpm, &age));
    CHECK_EQ(age, 49u);
    at_ms(t0 + 50u);
    CHECK(outputs_hw_erpm(&erpm, &age));
    CHECK_EQ(age, 50u);
    at_ms(t0 + 300u);
    CHECK(outputs_hw_erpm(&erpm, &age));
    CHECK_EQ(age, 300u);
}

TEST_CASE(a_speed_readings_age_holds_across_the_millisecond_wrap)
{
    at_tick_0_and_before_the_wrap(a_speed_reading_ages_by_the_time_passed,
                                  50u);
}

/* The same for an extended kind, which keeps its own stamp: a temperature
 * heard at t0 is 2,000 ms old 2,000 ms later, the edge of its stale rule,
 * whether or not the clock wrapped in between. */
static void a_temperature_reading_ages_by_the_time_passed(uint32_t t0)
{
    bind_one_bidir_slot(t0);
    dshot_telem_t t;
    memset(&t, 0, sizeof(t));
    t.kind  = DSHOT_TELEM_TEMPERATURE;
    t.value = 41u;
    hear(&t);

    uint16_t value = 0u;
    uint32_t age   = 0xFFFFFFFFu;
    CHECK(outputs_hw_edt(DSHOT_TELEM_TEMPERATURE, &value, &age));
    CHECK_EQ(value, 41u);
    CHECK_EQ(age, 0u);

    at_ms(t0 + 1999u);
    CHECK(outputs_hw_edt(DSHOT_TELEM_TEMPERATURE, &value, &age));
    CHECK_EQ(age, 1999u);
    at_ms(t0 + 2000u);
    CHECK(outputs_hw_edt(DSHOT_TELEM_TEMPERATURE, &value, &age));
    CHECK_EQ(age, 2000u);
    /* No speed was heard, so none is reported, wrap or not. */
    uint32_t erpm = 0u;
    CHECK(!outputs_hw_erpm(&erpm, &age));
}

TEST_CASE(a_temperature_readings_age_holds_across_the_millisecond_wrap)
{
    at_tick_0_and_before_the_wrap(
        a_temperature_reading_ages_by_the_time_passed, 50u);
}

/* Two kinds heard on either side of the wrap keep their own ages: a speed
 * 10 ms before it and a voltage 10 ms after, asked 20 ms after that. */
TEST_CASE(readings_on_either_side_of_the_wrap_keep_their_own_ages)
{
    const uint32_t t0 = tick_before_wrap(10u);
    bind_one_bidir_slot(t0);
    dshot_telem_t t;
    memset(&t, 0, sizeof(t));
    t.kind = DSHOT_TELEM_ERPM;
    t.erpm = 900u;
    hear(&t);
    at_ms(t0 + 20u);                       /* tick 10 */
    memset(&t, 0, sizeof(t));
    t.kind  = DSHOT_TELEM_VOLTAGE;
    t.value = 60u;
    hear(&t);

    at_ms(t0 + 40u);                       /* tick 30 */
    uint32_t erpm = 0u;
    uint32_t age  = 0u;
    CHECK(outputs_hw_erpm(&erpm, &age));
    CHECK_EQ(age, 40u);
    uint16_t value = 0u;
    CHECK(outputs_hw_edt(DSHOT_TELEM_VOLTAGE, &value, &age));
    CHECK_EQ(value, 60u);
    CHECK_EQ(age, 20u);
}

int main(void)
{
    RUN(a_speed_readings_age_holds_across_the_millisecond_wrap);
    RUN(a_temperature_readings_age_holds_across_the_millisecond_wrap);
    RUN(readings_on_either_side_of_the_wrap_keep_their_own_ages);
    return test_summary("outputs_hw");
}
