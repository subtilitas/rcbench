/*
 * The age of an ESC's last reading, as the IOmcu's output layer
 * (firmware/iomcu/src/outputs_hw.c) reports it, across the wrap of the
 * 32-bit millisecond clock at 2^32ms (49.7 days).
 *
 * outputs_hw_erpm() and outputs_hw_edt() give the age as now - stamp in
 * uint32_t.  The bench's stale rules (bench_state_from_telem(): 200ms for
 * speed, 2,000ms for the extended kinds) read that age, so an age that
 * jumps by 2^32ms at the wrap would blank a live reading, and one that
 * stays small would keep a dead one.  Each case runs at tick 0 and with the
 * reading taken 50ms before the wrap (tick_wrap.h).
 *
 * And the edge into driving on a bidirectional slot: the pin is woken from
 * its low rest one frame period before the first frame, also across the
 * wrap of the 32-bit microsecond clock; a disarm stops it in the same pass;
 * a slot the ESC_WIRE page holds is neither stopped nor woken.
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
#include "port_park.h"

uint64_t pico_stub_us;

/* The reply the next poll returns, once. */
static bool          g_reply_queued;
static dshot_telem_t g_reply;

/* What the ESC_WIRE takeover case counts. */
static int      g_binds, g_releases;
static uint8_t  g_bind_pin, g_release_pin;
static uint16_t g_bind_kbit;
static bool     g_bind_bidir;
static bool     g_bind_refused;

/*
 * The pin's rest, as out_dshot.c keeps it: a bidirectional pin is low from
 * its bind and from a stop, and a wake releases it once; a released pin is
 * the driver's no more.  The suites' bidirectional slot is on pin 2.  The
 * pins woken, stopped and sent to (bit n for pin n), pin 2's frames with the
 * clock of the first, and any frame sent from the low rest.
 */
static bool     g_low;
static uint32_t g_wake_pins, g_stop_pins, g_send_pins;
static int      g_wakes, g_sends, g_from_low;
static uint64_t g_wake_us, g_first_us;
static uint16_t g_send_value;
static bool     g_send_telemetry;

bool out_dshot_bind(uint8_t pin, uint16_t rate_kbit, bool bidirectional)
{
    g_binds++;
    g_bind_pin = pin;
    g_bind_kbit = rate_kbit;
    g_bind_bidir = bidirectional;
    if (pin == 2u) {
        g_low = bidirectional && !g_bind_refused;
    }
    return !g_bind_refused;
}

void out_dshot_release(uint8_t pin)
{
    g_releases++;
    g_release_pin = pin;
    if (pin == 2u) {
        g_low = false;
    }
}

bool out_dshot_wake(uint8_t pin)
{
    g_wake_pins |= 1u << pin;
    if (pin != 2u || !g_low) {
        return false;
    }
    g_low = false;
    g_wakes++;
    g_wake_us = pico_stub_us;
    return true;
}

void out_dshot_send(uint8_t pin, uint16_t value, bool telemetry)
{
    g_send_pins |= 1u << pin;
    if (pin != 2u) {
        return;
    }
    if (g_low) {
        g_from_low++;
    }
    if (g_sends == 0) {
        g_first_us = pico_stub_us;
    }
    g_sends++;
    g_send_value = value;
    g_send_telemetry = telemetry;
}

void out_dshot_stop(uint8_t pin)
{
    g_stop_pins |= 1u << pin;
    if (pin == 2u) {
        g_low = true;
    }
}

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

/* No port is parked here: test_outputs_hw_ports.c holds them. */
bool port_park(uint8_t pin)
{
    (void)pin;
    return true;
}

void port_unpark(uint8_t pin) { (void)pin; }

/* The millisecond clock at @p ms, which may be past the wrap. */
static void at_ms(uint32_t ms)
{
    pico_stub_us = (uint64_t)ms * 1000u;
}

/* The suite's records of the DShot fake back to zero. */
static void dshot_counts_zero(void)
{
    g_wake_pins = 0u;
    g_stop_pins = 0u;
    g_send_pins = 0u;
    g_wakes = 0;
    g_sends = 0;
    g_from_low = 0;
}

/* The microsecond clock at @p us, which may be past the 32-bit wrap. */
static void at_us(uint64_t us)
{
    pico_stub_us = us;
}

/* A bank with one bidirectional DShot slot on pin 2 at 600kbit/s, bound by
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

/* A speed reading taken at t0 is 0ms old at t0, 49ms old 1ms before the
 * next tick 0, 50ms at it and 250ms later on: the age grows by the time
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
 * heard at t0 is 2,000ms old 2,000ms later, the edge of its stale rule,
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
 * 10ms before it and a voltage 10ms after, asked 20ms after that. */
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

/*
 * A slot the ESC_WIRE page holds: left out of the mask it is let go and no other
 * slot moves; put back it binds with what it had.  Armed in between, nothing is
 * rendered on it.
 */
TEST_CASE(a_held_slot_is_let_go_alone_and_comes_back_the_same)
{
    uint16_t rate[OUT_MAX_SLOTS];
    const out_slot_t a = { OUT_DRIVER_DSHOT_BIDIR, 0u, 1u, 2u, 600u };
    const out_slot_t b = { OUT_DRIVER_DSHOT, 1u, 1u, 3u, 300u };

    at_ms(1000u);
    outputs_init(&g_bank, 1000u);
    CHECK(outputs_configure(&g_bank, 0u, &a));
    CHECK(outputs_configure(&g_bank, 1u, &b));
    memset(rate, 0, sizeof(rate));
    rate[0] = 600u;
    rate[1] = 300u;
    outputs_hw_init();
    outputs_hw_apply(&g_bank, rate);
    CHECK(outputs_hw_bound(0u));
    CHECK(outputs_hw_bound(1u));
    g_binds = 0;
    g_releases = 0;
    /* Slot 0 out. */
    outputs_hw_apply_only(&g_bank, rate, 0xFEu);
    CHECK(!outputs_hw_bound(0u));
    CHECK(outputs_hw_bound(1u));
    CHECK_EQ(g_releases, 1);
    CHECK_EQ(g_release_pin, 2u);
    CHECK_EQ(g_binds, 0);
    /* Held, the pin is the programmer's: no pass stops it to the low rest,
     * wakes it or sends on it, SAFE or ARMED; slot 1 goes on as before. */
    dshot_counts_zero();
    outputs_hw_service(&g_bank);
    outputs_arm(&g_bank, true, 1000u);
    outputs_hw_service(&g_bank);
    at_ms(1001u);
    outputs_hw_service(&g_bank);
    outputs_arm(&g_bank, false, 1001u);
    outputs_hw_service(&g_bank);
    CHECK_EQ(g_stop_pins & (1u << 2), 0u);
    CHECK_EQ(g_wake_pins & (1u << 2), 0u);
    CHECK_EQ(g_send_pins & (1u << 2), 0u);
    CHECK((g_stop_pins & (1u << 3)) != 0u);
    CHECK((g_send_pins & (1u << 3)) != 0u);
    /* Again, as any apply in a session asks: nothing moves. */
    g_releases = 0;
    outputs_hw_apply_only(&g_bank, rate, 0xFEu);
    CHECK_EQ(g_releases + g_binds, 0);
    /* Back: its pin, its rate, its kind; slot 1 stays. */
    CHECK(outputs_hw_return_slot(&g_bank, rate, 0u));
    CHECK(outputs_hw_bound(0u));
    CHECK_EQ(g_binds, 1);
    CHECK_EQ(g_releases, 0);
    CHECK_EQ(g_bind_pin, 2u);
    CHECK_EQ(g_bind_kbit, 600u);
    CHECK(g_bind_bidir);
    /* Bound already: nothing to do.  A bad argument, a bind that fails. */
    g_binds = 0;
    CHECK(outputs_hw_return_slot(&g_bank, rate, 0u));
    CHECK_EQ(g_binds, 0);
    CHECK(!outputs_hw_return_slot(NULL, rate, 0u));
    CHECK(!outputs_hw_return_slot(&g_bank, NULL, 0u));
    CHECK(!outputs_hw_return_slot(&g_bank, rate, OUT_MAX_SLOTS));
    outputs_hw_apply_only(&g_bank, rate, 0xFEu);
    g_bind_refused = true;
    CHECK(!outputs_hw_return_slot(&g_bank, rate, 0u));
    g_bind_refused = false;
    CHECK(!outputs_hw_bound(0u));
    /* An empty slot stays unbound. */
    CHECK(!outputs_hw_return_slot(&g_bank, rate, 5u));
}

/*
 * The edge into driving on a bidirectional slot, at microsecond @p t0: the
 * first pass wakes the pin and sends nothing, the first frame goes out
 * DSHOT_PERIOD_US (1000us) later and not at 999us, and the ask that leads
 * the run keeps its DSHOT_CMD_REPEATS (10) frames before the throttle's stop
 * frame.  A disarm stops the pin every pass; the next arm wakes it again
 * before any frame.  No frame goes out from the low rest.
 */
static void a_run_starts_one_frame_period_after_the_wake(uint64_t t0)
{
    bind_one_bidir_slot((uint32_t)(t0 / 1000u));
    CHECK(outputs_set_role(&g_bank, 0u, OUT_ROLE_THROTTLE));
    at_us(t0);
    outputs_hw_service(&g_bank);              /* SAFE: at rest */
    dshot_counts_zero();

    outputs_arm(&g_bank, true, (uint32_t)(t0 / 1000u));
    outputs_hw_service(&g_bank);
    CHECK_EQ(g_wakes, 1);
    CHECK_EQ(g_sends, 0);
    at_us(t0 + 999u);
    outputs_hw_service(&g_bank);
    CHECK_EQ(g_sends, 0);
    at_us(t0 + 1000u);
    outputs_hw_service(&g_bank);
    CHECK_EQ(g_sends, 1);
    CHECK_EQ((long)(g_first_us - g_wake_us), 1000);
    CHECK_EQ(g_send_value, (uint16_t)DSHOT_CMD_EDT_ENABLE);
    CHECK(g_send_telemetry);
    for (unsigned i = 2u; i <= DSHOT_CMD_REPEATS; ++i) {
        at_us(t0 + 1000u * i);
        outputs_hw_service(&g_bank);
        CHECK_EQ(g_send_value, (uint16_t)DSHOT_CMD_EDT_ENABLE);
    }
    CHECK_EQ(g_sends, (int)DSHOT_CMD_REPEATS);
    at_us(t0 + 1000u * (DSHOT_CMD_REPEATS + 1u));
    outputs_hw_service(&g_bank);
    CHECK_EQ(g_send_value, (uint16_t)DSHOT_CMD_MOTOR_STOP);
    CHECK(!g_send_telemetry);
    CHECK_EQ(g_wakes, 1);

    /* SAFE: the pin stops, and stays stopped, every pass. */
    const uint64_t t1 = t0 + 1000u * (DSHOT_CMD_REPEATS + 2u);
    outputs_arm(&g_bank, false, (uint32_t)(t1 / 1000u));
    g_stop_pins = 0u;
    const int sent = g_sends;
    at_us(t1);
    outputs_hw_service(&g_bank);
    CHECK_EQ(g_stop_pins, 1u << 2);
    at_us(t1 + 5000u);
    outputs_hw_service(&g_bank);
    CHECK_EQ(g_sends, sent);
    /* ARM again: a wake first, the frame a period later. */
    outputs_arm(&g_bank, true, (uint32_t)(t1 / 1000u) + 5u);
    outputs_hw_service(&g_bank);
    CHECK_EQ(g_wakes, 2);
    CHECK_EQ(g_sends, sent);
    at_us(t1 + 6000u);
    outputs_hw_service(&g_bank);
    CHECK_EQ(g_sends, sent + 1);
    CHECK_EQ(g_from_low, 0);
}

TEST_CASE(a_bidirectional_run_starts_one_frame_period_after_the_wake)
{
    a_run_starts_one_frame_period_after_the_wake(5000000u);
    /* Across the wrap of the 32-bit microsecond clock, 2^32us (71.6min),
     * between the wake and the first frame. */
    a_run_starts_one_frame_period_after_the_wake(0x100000000ull - 500u);
}

/*
 * An error, a STOP and a lost link all disarm the bank through
 * outputs_off(), which services the pins at once: the bidirectional slot is
 * stopped in that pass, so its pin goes to the low rest with no frame after.
 */
TEST_CASE(a_disarm_stops_the_bidirectional_pin_in_the_same_pass)
{
    bind_one_bidir_slot(1000u);
    at_us(1000000u);
    outputs_arm(&g_bank, true, 1000u);
    outputs_hw_service(&g_bank);
    at_us(1001000u);
    outputs_hw_service(&g_bank);
    dshot_counts_zero();
    outputs_arm(&g_bank, false, 1001u);
    outputs_hw_service(&g_bank);
    CHECK_EQ(g_stop_pins, 1u << 2);
    CHECK_EQ(g_send_pins, 0u);
    CHECK_EQ(g_wake_pins, 0u);
}

int main(void)
{
    RUN(a_bidirectional_run_starts_one_frame_period_after_the_wake);
    RUN(a_disarm_stops_the_bidirectional_pin_in_the_same_pass);
    RUN(a_held_slot_is_let_go_alone_and_comes_back_the_same);
    RUN(a_speed_readings_age_holds_across_the_millisecond_wrap);
    RUN(a_temperature_readings_age_holds_across_the_millisecond_wrap);
    RUN(readings_on_either_side_of_the_wrap_keep_their_own_ages);
    return test_summary("outputs_hw");
}
