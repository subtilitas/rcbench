/*
 * The programming ports in the IOmcu's output layer
 * (firmware/iomcu/src/outputs_hw.c): a SERVO PWM port bound to the PWM
 * generator, a NONE port parked (protocol 4.26), a DSHOT port bound to a
 * DShot state machine (4.28).
 *
 * Under test: a parked port held with no generator binding and counted
 * as held; no pulse written on it, armed or not; a port moved between the
 * two functions let go the old way before it is taken the new way; a
 * port whose function and pin stay is left alone; a pin the chip does
 * not have is not held; a port without a pin let go; a session taking a
 * parked pin and the pin parked again when it comes back; a DShot port
 * bound at its kbit/s rate and moved by a rate or function change, framed
 * every 1 ms only while the bank is armed for the throttle, its frame the
 * throttle, 0 at the rest, and no frame on SAFE, on ARM SURFACE or on
 * STOP; a DShot port no state machine is left for not held.
 *
 * The pin drivers are fakes that count what they are asked.  The clock is
 * pico_stub/pico/stdlib.h's.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "greatest.h"

#include "link_pages.h"
#include "out_dshot.h"
#include "out_ppm.h"
#include "out_pwm.h"
#include "outputs.h"
#include "outputs_hw.h"
#include "pico/stdlib.h"
#include "port_park.h"

uint64_t pico_stub_us;

static int g_pwm_binds, g_pwm_releases, g_pwm_writes;
static int g_parks, g_unparks;
static uint8_t g_last_pin;
static int g_dshot_binds, g_dshot_releases, g_dshot_sends, g_dshot_stops;
static uint16_t g_dshot_kbit, g_dshot_value;
static bool g_dshot_bidir, g_dshot_full;

bool out_dshot_bind(uint8_t pin, uint16_t rate_kbit, bool bidirectional)
{
    if (g_dshot_full) {
        return false;                /* no state machine left */
    }
    ++g_dshot_binds;
    g_last_pin    = pin;
    g_dshot_kbit  = rate_kbit;
    g_dshot_bidir = bidirectional;
    return true;
}
void out_dshot_release(uint8_t pin)
{
    ++g_dshot_releases;
    g_last_pin = pin;
}
void out_dshot_send(uint8_t pin, uint16_t value, bool telemetry)
{
    (void)telemetry;
    ++g_dshot_sends;
    g_last_pin    = pin;
    g_dshot_value = value;
}
void out_dshot_stop(uint8_t pin)
{
    (void)pin;
    ++g_dshot_stops;
}
bool out_dshot_poll(uint8_t pin, bool edt, dshot_telem_t *out)
{
    (void)pin;
    (void)edt;
    (void)out;
    return false;
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
    (void)rate_hz;
    ++g_pwm_binds;
    g_last_pin = pin;
    return true;
}
void out_pwm_release(uint8_t pin)
{
    ++g_pwm_releases;
    g_last_pin = pin;
}
void out_pwm_write(uint8_t pin, uint16_t pulse_us)
{
    (void)pin;
    (void)pulse_us;
    ++g_pwm_writes;
}
bool out_pwm_write_stamped(uint8_t pin, uint16_t pulse_us, uint64_t *frame_us)
{
    (void)pin;
    (void)pulse_us;
    (void)frame_us;
    ++g_pwm_writes;
    return true;
}

/* As the chip: GP0 to GP47. */
bool port_park(uint8_t pin)
{
    if (pin >= 48u) {
        return false;
    }
    ++g_parks;
    g_last_pin = pin;
    return true;
}
void port_unpark(uint8_t pin)
{
    ++g_unparks;
    g_last_pin = pin;
}

static const uint16_t k_hz[LINK_PG_PORTS_MAX] = { 50u, 50u, 50u, 50u };

static void counts_zero(void)
{
    g_pwm_binds = g_pwm_releases = g_pwm_writes = 0;
    g_parks = g_unparks = 0;
    g_dshot_binds = g_dshot_releases = g_dshot_sends = g_dshot_stops = 0;
    g_dshot_kbit = g_dshot_value = 0u;
    g_dshot_bidir = g_dshot_full = false;
}

/* Port 0 on @p pin0 and port 1 on @p pin1, port 1 parked by @p parked. */
static void ports(uint8_t pin0, uint8_t pin1, uint8_t parked)
{
    const uint8_t pin[LINK_PG_PORTS_MAX] = { pin0, pin1, 0xFFu, 0xFFu };
    outputs_hw_ports(pin, k_hz, parked, 0u, 9u);
}

TEST_CASE(a_none_port_is_parked_and_held_without_the_generator)
{
    outputs_hw_init();
    counts_zero();
    ports(13u, 14u, 2u);
    CHECK_EQ(g_pwm_binds, 1);
    CHECK_EQ(g_parks, 1);
    CHECK_EQ(g_last_pin, 14u);
    CHECK_EQ(outputs_hw_ports_bound(), 3u);
    /* The same again moves nothing. */
    counts_zero();
    ports(13u, 14u, 2u);
    CHECK_EQ(g_pwm_binds + g_pwm_releases + g_parks + g_unparks, 0);
}

TEST_CASE(a_parked_port_renders_no_pulse_armed_or_not)
{
    outputs_t o;
    outputs_init(&o, 0u);
    outputs_hw_init();
    ports(0xFFu, 14u, 2u);
    outputs_hw_ports_drive(2u);
    counts_zero();
    outputs_arm(&o, true, 0u);
    outputs_hw_service(&o);
    CHECK_EQ(g_pwm_writes, 0);
    outputs_arm(&o, false, 0u);
    outputs_hw_service(&o);
    CHECK_EQ(g_pwm_writes, 0);
    /* A SERVO PWM port beside it is written, its pulse or 0. */
    ports(13u, 14u, 2u);
    outputs_hw_ports_drive(3u);
    counts_zero();
    outputs_hw_service(&o);
    CHECK_EQ(g_pwm_writes, 1);
}

TEST_CASE(a_function_change_lets_go_the_old_way_first)
{
    outputs_hw_init();
    ports(0xFFu, 14u, 0u);           /* SERVO PWM */
    counts_zero();
    ports(0xFFu, 14u, 2u);           /* to NONE */
    CHECK_EQ(g_pwm_releases, 1);
    CHECK_EQ(g_parks, 1);
    CHECK_EQ(g_unparks, 0);
    CHECK_EQ(outputs_hw_ports_bound(), 2u);
    counts_zero();
    ports(0xFFu, 14u, 0u);           /* back */
    CHECK_EQ(g_unparks, 1);
    CHECK_EQ(g_pwm_binds, 1);
    CHECK_EQ(g_pwm_releases, 0);
    /* A rate change moves a SERVO PWM port, not a parked one. */
    ports(0xFFu, 14u, 2u);
    counts_zero();
    const uint8_t pin[LINK_PG_PORTS_MAX] = { 0xFFu, 14u, 0xFFu, 0xFFu };
    const uint16_t hz[LINK_PG_PORTS_MAX] = { 50u, 333u, 50u, 50u };
    outputs_hw_ports(pin, hz, 2u, 0u, 9u);
    CHECK_EQ(g_pwm_binds + g_pwm_releases + g_parks + g_unparks, 0);
}

TEST_CASE(a_pin_the_chip_has_not_is_not_held_and_no_pin_lets_go)
{
    outputs_hw_init();
    counts_zero();
    ports(0xFFu, 48u, 2u);
    CHECK_EQ(outputs_hw_ports_bound(), 0u);
    ports(0xFFu, 47u, 2u);
    CHECK_EQ(outputs_hw_ports_bound(), 2u);
    counts_zero();
    ports(0xFFu, 0xFFu, 2u);         /* a wire page takes the pin */
    CHECK_EQ(g_unparks, 1);
    CHECK_EQ(g_last_pin, 47u);
    CHECK_EQ(outputs_hw_ports_bound(), 0u);
    outputs_hw_ports(NULL, k_hz, 0u, 0u, 9u);
    outputs_hw_ports(NULL, NULL, 0u, 0u, 9u);
}

/* A session on a NONE port: the wire page takes the parked pin (the port
 * handed no pin), the SERVO PWM port beside it stays bound, and the give
 * back parks the pin again. */
TEST_CASE(a_session_takes_a_parked_pin_and_parks_it_again)
{
    outputs_hw_init();
    ports(13u, 14u, 2u);
    counts_zero();
    ports(13u, 0xFFu, 2u);           /* the session takes port 1 */
    CHECK_EQ(g_unparks, 1);
    CHECK_EQ(g_last_pin, 14u);
    CHECK_EQ(g_pwm_releases + g_pwm_binds + g_parks, 0);
    CHECK_EQ(outputs_hw_ports_bound(), 1u);
    counts_zero();
    ports(13u, 14u, 2u);             /* and gives it back */
    CHECK_EQ(g_parks, 1);
    CHECK_EQ(g_last_pin, 14u);
    CHECK_EQ(g_pwm_releases + g_pwm_binds + g_unparks, 0);
    CHECK_EQ(outputs_hw_ports_bound(), 3u);
}

/* Port 1 on @p pin1 as DShot at @p kbit, port 0 SERVO PWM on GP13. */
static void dshot_port(uint8_t pin1, uint16_t kbit)
{
    const uint8_t pin[LINK_PG_PORTS_MAX] = { 13u, pin1, 0xFFu, 0xFFu };
    const uint16_t rate[LINK_PG_PORTS_MAX] = { 50u, kbit, 50u, 50u };
    outputs_hw_ports(pin, rate, 0u, 2u, 9u);
}

TEST_CASE(a_dshot_port_is_bound_at_its_bit_rate_and_moved_by_it)
{
    outputs_hw_init();
    counts_zero();
    dshot_port(14u, 300u);
    CHECK_EQ(g_dshot_binds, 1);
    CHECK_EQ(g_dshot_kbit, 300u);
    CHECK(!g_dshot_bidir);           /* plain: no telemetry wire */
    CHECK_EQ(g_last_pin, 14u);
    CHECK_EQ(g_pwm_binds, 1);        /* port 0, SERVO PWM */
    CHECK_EQ(outputs_hw_ports_bound(), 3u);
    /* The same again moves nothing. */
    counts_zero();
    dshot_port(14u, 300u);
    CHECK_EQ(g_dshot_binds + g_dshot_releases + g_pwm_binds
             + g_pwm_releases, 0);
    /* DSHOT300 to DSHOT600: let go and bound again at the new rate. */
    dshot_port(14u, 600u);
    CHECK_EQ(g_dshot_releases, 1);
    CHECK_EQ(g_dshot_binds, 1);
    CHECK_EQ(g_dshot_kbit, 600u);
    /* DSHOT150, the slowest the slots offer. */
    counts_zero();
    dshot_port(14u, 150u);
    CHECK_EQ(g_dshot_kbit, 150u);
    /* To SERVO PWM at 50 Hz: the DShot way let go, the generator taken. */
    counts_zero();
    ports(13u, 14u, 0u);
    CHECK_EQ(g_dshot_releases, 1);
    CHECK_EQ(g_pwm_releases, 0);
    CHECK_EQ(g_pwm_binds, 1);
    /* And back: the generator let go, a state machine taken. */
    counts_zero();
    dshot_port(14u, 300u);
    CHECK_EQ(g_pwm_releases, 1);
    CHECK_EQ(g_dshot_binds, 1);
    /* To NONE: parked, no state machine. */
    counts_zero();
    ports(13u, 14u, 2u);
    CHECK_EQ(g_dshot_releases, 1);
    CHECK_EQ(g_parks, 1);
    /* A session takes the pin: the DShot port lets it go. */
    dshot_port(14u, 300u);
    counts_zero();
    dshot_port(0xFFu, 300u);
    CHECK_EQ(g_dshot_releases, 1);
    CHECK_EQ(outputs_hw_ports_bound(), 1u);
}

TEST_CASE(a_dshot_port_no_state_machine_is_left_for_is_not_held)
{
    outputs_hw_init();
    counts_zero();
    g_dshot_full = true;
    dshot_port(14u, 300u);
    CHECK_EQ(outputs_hw_ports_bound(), 1u);
    /* Not held is not let go, and nothing is sent on it. */
    outputs_t o;
    outputs_init(&o, 0u);
    (void)outputs_set_role(&o, 10u, OUT_ROLE_THROTTLE);
    outputs_arm(&o, true, 0u);
    outputs_hw_service(&o);
    CHECK_EQ(g_dshot_sends + g_dshot_stops + g_dshot_releases, 0);
    /* A state machine free again: the next bind takes it. */
    g_dshot_full = false;
    dshot_port(0xFFu, 300u);
    dshot_port(14u, 300u);
    CHECK_EQ(outputs_hw_ports_bound(), 3u);
}

/* The bank as iomcu_pages.c leaves it for a DShot port 1: channel 10 a
 * throttle. */
static void throttle_bank(outputs_t *o)
{
    outputs_init(o, 0u);
    (void)outputs_set_role(o, 10u, OUT_ROLE_THROTTLE);
    (void)outputs_set_slew(o, 10u, 0u);
}

TEST_CASE(a_dshot_port_frames_every_ms_only_while_armed_for_the_throttle)
{
    outputs_t o;
    throttle_bank(&o);
    outputs_hw_init();
    pico_stub_us = 1000000u;
    dshot_port(14u, 300u);
    counts_zero();
    /* SAFE: no frame, the line held stopped. */
    outputs_hw_service(&o);
    CHECK_EQ(g_dshot_sends, 0);
    CHECK_EQ(g_dshot_stops, 1);
    /* ARM SURFACE (the SERVO screen's) is no arm for a motor. */
    outputs_arm_roles(&o, OUT_ROLE_BIT(OUT_ROLE_SURFACE), 0u);
    counts_zero();
    outputs_hw_service(&o);
    CHECK_EQ(g_dshot_sends, 0);
    CHECK_EQ(g_dshot_stops, 1);
    /* ARM THROTTLE: frame 0 at the rest, at once. */
    outputs_arm_roles(&o, OUT_ROLE_BIT(OUT_ROLE_THROTTLE), 0u);
    counts_zero();
    g_dshot_value = 0xFFFFu;
    outputs_hw_service(&o);
    CHECK_EQ(g_dshot_sends, 1);
    CHECK_EQ(g_dshot_value, 0u);
    CHECK_EQ(g_last_pin, 14u);
    /* No second frame inside the 1 ms period; the next at 1 ms. */
    pico_stub_us += 999u;
    outputs_hw_service(&o);
    CHECK_EQ(g_dshot_sends, 1);
    pico_stub_us += 1u;
    outputs_hw_service(&o);
    CHECK_EQ(g_dshot_sends, 2);
    /* The throttle: full span is DShot 2047. */
    (void)outputs_set(&o, 10u, OUT_SPAN, 0u);
    outputs_step(&o, 0u);
    pico_stub_us += 1000u;
    outputs_hw_service(&o);
    CHECK_EQ(g_dshot_value, 2047u);
    /* STOP disarms: the frames stop at once, mid-period. */
    pico_stub_us += 500u;
    outputs_arm(&o, false, 0u);
    counts_zero();
    outputs_hw_service(&o);
    CHECK_EQ(g_dshot_sends, 0);
    CHECK_EQ(g_dshot_stops, 1);
    /* And the throttle is at its rest when it is armed again. */
    outputs_arm_roles(&o, OUT_ROLE_BIT(OUT_ROLE_THROTTLE), 0u);
    counts_zero();
    g_dshot_value = 0xFFFFu;
    outputs_hw_service(&o);
    CHECK_EQ(g_dshot_sends, 1);
    CHECK_EQ(g_dshot_value, 0u);
}

/* The port's own drive bit is the tester's, for SERVO PWM: a DShot port
 * needs none and ignores one. */
TEST_CASE(a_dshot_port_ignores_the_tester_drive_bit)
{
    outputs_t o;
    throttle_bank(&o);
    outputs_hw_init();
    dshot_port(14u, 300u);
    outputs_hw_ports_drive(0u);
    outputs_arm_roles(&o, OUT_ROLE_BIT(OUT_ROLE_THROTTLE), 0u);
    counts_zero();
    pico_stub_us += 1000u;
    outputs_hw_service(&o);
    CHECK_EQ(g_dshot_sends, 1);
    outputs_hw_ports_drive(2u);
    outputs_arm(&o, false, 0u);
    counts_zero();
    outputs_hw_service(&o);
    CHECK_EQ(g_dshot_sends, 0);
}

/* The 1 ms period across the microsecond clock's wrap at 2^32 us. */
TEST_CASE(a_dshot_port_period_survives_the_clock_wrap)
{
    outputs_t o;
    throttle_bank(&o);
    outputs_hw_init();
    pico_stub_us = 0xFFFFFC00u;      /* 1,024 us before the wrap */
    dshot_port(14u, 300u);
    outputs_arm_roles(&o, OUT_ROLE_BIT(OUT_ROLE_THROTTLE), 0u);
    counts_zero();
    outputs_hw_service(&o);
    CHECK_EQ(g_dshot_sends, 1);
    pico_stub_us += 999u;            /* not due, still before the wrap */
    outputs_hw_service(&o);
    CHECK_EQ(g_dshot_sends, 1);
    pico_stub_us += 1u;              /* due, 24 us before the wrap */
    outputs_hw_service(&o);
    CHECK_EQ(g_dshot_sends, 2);
    pico_stub_us += 1000u;           /* due, past the wrap */
    outputs_hw_service(&o);
    CHECK_EQ(g_dshot_sends, 3);
}

int main(void)
{
    RUN(a_none_port_is_parked_and_held_without_the_generator);
    RUN(a_parked_port_renders_no_pulse_armed_or_not);
    RUN(a_function_change_lets_go_the_old_way_first);
    RUN(a_pin_the_chip_has_not_is_not_held_and_no_pin_lets_go);
    RUN(a_session_takes_a_parked_pin_and_parks_it_again);
    RUN(a_dshot_port_is_bound_at_its_bit_rate_and_moved_by_it);
    RUN(a_dshot_port_no_state_machine_is_left_for_is_not_held);
    RUN(a_dshot_port_frames_every_ms_only_while_armed_for_the_throttle);
    RUN(a_dshot_port_ignores_the_tester_drive_bit);
    RUN(a_dshot_port_period_survives_the_clock_wrap);
    return test_summary("outputs_hw_ports");
}
