/*
 * The programming ports and the PROG link page (shared/outputs/prog_port.c).
 *
 * Under test: a page that starts with no pin on any port; the build's port
 * count held to the model's four; the kept registers of a board that has
 * stored none; the kept set restored through the page's checks, with a pin
 * that is no generator pin dropped, endpoints outside 400 to 2500 us
 * replaced, and a pin something else holds flagged refused; a PIN write
 * refused on a reserved pin, a taken pin, a slot's pin, the slice of a PWM
 * slot, a pin without a PWM slice and the pin or compare register of
 * another port; a PIN write refused while the bank is armed, the wire
 * driver holds the port, the port's pulse is on or the tester points at
 * it; the pulse and the tester only on a port the generator has; a refused
 * write storing nothing; the command clamped to the span; the endpoints at
 * both limits; the slew refused above 3000 and a kept one above it started
 * at 3000; the read-only registers and the page end; a port the build
 * does not have; a slot refused on a port's pin and beside it; a write
 * taken back; no endpoint longer than the frame on a port whose pulse is
 * on.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "link_msg.h"
#include "link_pages.h"
#include "outputs.h"
#include "prog_port.h"

static outputs_t   o;
static prog_port_t pg;

#define BIT(pin) ((uint64_t)1u << (pin))
#define PIN0 LINK_PG_REG(0u, LINK_PG_PIN)
#define PIN1 LINK_PG_REG(1u, LINK_PG_PIN)

/* A bank with GP3 and GP12 reserved and a PWM output on GP4, and @p count
 * ports without a pin. */
static void fresh(uint8_t count)
{
    outputs_init(&o, 0u);
    outputs_reserve_pins(&o, BIT(3) | BIT(12));
    const out_slot_t pwm = { .driver = OUT_DRIVER_PWM, .first_channel = 0,
                             .channels = 1, .pin = 4, .rate_hz = 50 };
    CHECK(outputs_configure(&o, 0, &pwm));
    prog_port_init(&pg, count);
}

static uint16_t reg(unsigned i)
{
    uint16_t v = 0xA5A5u;
    prog_port_read(&pg, (uint8_t)i, 1u, &v);
    return v;
}

static uint8_t put(unsigned off, uint16_t v)
{
    return prog_port_write(&pg, (uint8_t)off, 1u, &v, &o, 0u, false, 0u);
}

/* Port @p port on @p pin and bound, as the firmware does after a write. */
static void bind(unsigned port, uint16_t pin)
{
    CHECK_EQ(put(LINK_PG_REG(port, LINK_PG_PIN), pin), 0u);
    prog_port_bound(&pg, (uint8_t)(pg.bound | (1u << port)));
}

TEST_CASE(a_page_starts_with_no_pin_on_any_port)
{
    fresh(1u);
    CHECK_EQ(reg(LINK_PG_COUNT), 1u);
    CHECK_EQ(reg(LINK_PG_FLAGS), 0u);
    CHECK_EQ(reg(LINK_PG_PWM), 0u);
    CHECK_EQ(reg(LINK_PG_TARGET), 0u);
    for (unsigned port = 0; port < LINK_PG_PORTS_MAX; ++port) {
        CHECK_EQ(reg(LINK_PG_REG(port, LINK_PG_PIN)), LINK_PG_PIN_NONE);
        CHECK_EQ(reg(LINK_PG_REG(port, LINK_PG_CMD)), OUT_SPAN / 2u);
        CHECK_EQ(reg(LINK_PG_REG(port, LINK_PG_SLEW)), 0u);
        CHECK_EQ(reg(LINK_PG_REG(port, LINK_PG_MIN_US)), 1000u);
        CHECK_EQ(reg(LINK_PG_REG(port, LINK_PG_MAX_US)), 2000u);
        CHECK_EQ(prog_port_pin(&pg, (uint8_t)port), PROG_PORT_NO_PIN);
    }
    CHECK_EQ(prog_port_pins(&pg), 0u);
    CHECK_EQ(prog_port_pwm(&pg), 0u);
    CHECK_EQ(prog_port_target(&pg), 0u);
    /* A port's channel is above the CHANNELS page and the bench throttle. */
    CHECK_EQ(prog_port_channel(0u), 9u);
    CHECK_EQ(prog_port_channel(3u), 12u);
    CHECK(prog_port_channel(LINK_PG_PORTS_MAX - 1u) < OUT_MAX_CHANNELS);
}

TEST_CASE(the_port_count_is_held_to_the_models_four)
{
    for (unsigned count = 0; count <= 6u; ++count) {
        fresh((uint8_t)count);
        CHECK_EQ(prog_port_count(&pg), count > 4u ? 4u : count);
        CHECK_EQ(reg(LINK_PG_COUNT), count > 4u ? 4u : count);
    }
    CHECK_EQ(prog_port_count(NULL), 0u);
    /* All four of the model can hold a pin at once. */
    fresh(4u);
    bind(0u, 13u);
    bind(1u, 14u);
    bind(2u, 16u);
    bind(3u, 18u);
    CHECK_EQ(prog_port_pins(&pg), BIT(13) | BIT(14) | BIT(16) | BIT(18));
    CHECK_EQ(reg(LINK_PG_FLAGS), 0x000Fu);
    /* A build without a port takes no register. */
    fresh(0u);
    CHECK_EQ(put(LINK_PG_PWM, 0u), 0u);
    CHECK_EQ(put(PIN0, 13u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(reg(PIN0), LINK_PG_PIN_NONE);
}

TEST_CASE(a_board_that_has_stored_none_has_port_0_on_the_default_pin)
{
    uint16_t kept[PROG_PORT_KEPT_COUNT];
    memset(kept, 0xA5, sizeof(kept));
    prog_port_defaults(kept, PROG_PORT_DEFAULT_PIN);
    CHECK_EQ(PROG_PORT_DEFAULT_PIN, 13u);
    for (unsigned port = 0; port < LINK_PG_PORTS_MAX; ++port) {
        const uint16_t *k = &kept[port * PROG_PORT_KEPT];
        CHECK_EQ(k[0], port == 0u ? 13u : LINK_PG_PIN_NONE);
        CHECK_EQ(k[1], 0u);
        CHECK_EQ(k[2], 1000u);
        CHECK_EQ(k[3], 2000u);
    }
    prog_port_defaults(kept, LINK_PG_PIN_NONE);
    CHECK_EQ(kept[0], LINK_PG_PIN_NONE);
    prog_port_defaults(NULL, 13u);
    /* The default pin is one the page takes on the bank of this file. */
    fresh(1u);
    prog_port_defaults(kept, PROG_PORT_DEFAULT_PIN);
    prog_port_restore(&pg, kept, &o, 0u);
    CHECK_EQ(prog_port_pin(&pg, 0u), 13u);
    CHECK_EQ(prog_port_pins(&pg), BIT(13));
    /* Not bound until the caller says so: refused until then. */
    CHECK_EQ(reg(LINK_PG_FLAGS), LINK_PG_F_REFUSED(0u));
    prog_port_bound(&pg, 0x01u);
    CHECK_EQ(reg(LINK_PG_FLAGS), LINK_PG_F_BOUND(0u));
}

TEST_CASE(the_kept_set_is_restored_through_the_pages_checks)
{
    uint16_t kept[PROG_PORT_KEPT_COUNT];
    uint16_t back[PROG_PORT_KEPT_COUNT];

    fresh(2u);
    prog_port_defaults(kept, 13u);
    kept[1] = 300u;
    kept[2] = 400u;
    kept[3] = 2500u;
    kept[PROG_PORT_KEPT + 0u] = 14u;
    kept[PROG_PORT_KEPT + 1u] = 0u;
    kept[PROG_PORT_KEPT + 2u] = 399u;       /* one under the floor */
    kept[PROG_PORT_KEPT + 3u] = 2000u;
    kept[2u * PROG_PORT_KEPT] = 16u;        /* a port the build has not */
    prog_port_restore(&pg, kept, &o, 0u);
    CHECK_EQ(reg(PIN0), 13u);
    CHECK_EQ(reg(LINK_PG_REG(0u, LINK_PG_SLEW)), 300u);
    CHECK_EQ(reg(LINK_PG_REG(0u, LINK_PG_MIN_US)), 400u);
    CHECK_EQ(reg(LINK_PG_REG(0u, LINK_PG_MAX_US)), 2500u);
    CHECK_EQ(o.channel[9].min_us, 400u);
    CHECK_EQ(o.channel[9].max_us, 2500u);
    CHECK_EQ(o.channel[9].slew_per_s, 300u);
    CHECK_EQ(o.channel[9].role, OUT_ROLE_SURFACE);
    CHECK_EQ(reg(PIN1), 14u);
    CHECK_EQ(reg(LINK_PG_REG(1u, LINK_PG_MIN_US)), 1000u);
    CHECK_EQ(reg(LINK_PG_REG(1u, LINK_PG_MAX_US)), 2000u);
    CHECK_EQ(reg(LINK_PG_REG(2u, LINK_PG_PIN)), LINK_PG_PIN_NONE);
    CHECK_EQ(prog_port_pins(&pg), BIT(13) | BIT(14));
    /* What is kept again is what the page holds. */
    prog_port_kept(&pg, back);
    CHECK_EQ(back[0], 13u);
    CHECK_EQ(back[1], 300u);
    CHECK_EQ(back[2], 400u);
    CHECK_EQ(back[3], 2500u);
    CHECK_EQ(back[PROG_PORT_KEPT + 2u], 1000u);
    CHECK_EQ(back[2u * PROG_PORT_KEPT], LINK_PG_PIN_NONE);
    /* One over the ceiling, and each end alone. */
    kept[2] = 1000u;
    kept[3] = 2501u;
    prog_port_restore(&pg, kept, &o, 0u);
    CHECK_EQ(reg(LINK_PG_REG(0u, LINK_PG_MAX_US)), 2000u);
    /* A restore ends every pulse and the tester's hold on a port. */
    prog_port_bound(&pg, 0x03u);
    CHECK_EQ(put(LINK_PG_PWM, 0x03u), 0u);
    CHECK_EQ(put(LINK_PG_TARGET, 2u), 0u);
    prog_port_restore(&pg, kept, &o, 0u);
    CHECK_EQ(reg(LINK_PG_PWM), 0u);
    CHECK_EQ(reg(LINK_PG_TARGET), 0u);
    CHECK_EQ(reg(LINK_PG_FLAGS),
             LINK_PG_F_REFUSED(0u) | LINK_PG_F_REFUSED(1u));
    /* Nothing without all three. */
    const prog_port_t was = pg;
    prog_port_restore(NULL, kept, &o, 0u);
    prog_port_restore(&pg, NULL, &o, 0u);
    prog_port_restore(&pg, kept, NULL, 0u);
    prog_port_kept(NULL, back);
    prog_port_kept(&pg, NULL);
    CHECK_EQ(memcmp(&was, &pg, sizeof(pg)), 0);
}

/* A kept pin that something else holds at start stays on the page, flagged
 * refused, and the port holds no pin until the pin is written again. */
TEST_CASE(a_kept_pin_something_else_holds_is_flagged_and_not_held)
{
    static const struct {
        uint16_t pin;
        uint64_t taken;
    } held[] = {
        {  3u, 0u },          /* reserved                     */
        {  4u, 0u },          /* a slot's pin                 */
        {  5u, 0u },          /* the slice of a PWM slot      */
        { 21u, 0u },          /* the same slice, folded       */
        { 13u, BIT(13) },     /* SENSE's, SUPPLY's or TONE's  */
    };
    uint16_t kept[PROG_PORT_KEPT_COUNT];

    for (unsigned i = 0; i < sizeof(held) / sizeof(held[0]); ++i) {
        fresh(1u);
        prog_port_defaults(kept, held[i].pin);
        prog_port_restore(&pg, kept, &o, held[i].taken);
        CHECK_EQ(reg(PIN0), held[i].pin);
        CHECK_EQ(prog_port_pin(&pg, 0u), PROG_PORT_NO_PIN);
        CHECK_EQ(prog_port_pins(&pg), 0u);
        prog_port_bound(&pg, 0x01u);
        CHECK_EQ(reg(LINK_PG_FLAGS), LINK_PG_F_REFUSED(0u));
        /* The pulse and the tester are refused on it. */
        CHECK_EQ(put(LINK_PG_PWM, 1u), LINK_NACK_BAD_VALUE);
        CHECK_EQ(put(LINK_PG_TARGET, 1u), LINK_NACK_BAD_VALUE);
        /* Written again while it is still held: refused again. */
        CHECK_EQ(prog_port_write(&pg, PIN0, 1u, &held[i].pin, &o,
                                 held[i].taken, false, 0u),
                 LINK_NACK_BAD_VALUE);
        CHECK_EQ(prog_port_pin(&pg, 0u), PROG_PORT_NO_PIN);
    }
    /* Free again: the same pin written again is taken. */
    fresh(1u);
    prog_port_defaults(kept, 13u);
    prog_port_restore(&pg, kept, &o, BIT(13));
    CHECK_EQ(put(PIN0, 13u), 0u);
    CHECK_EQ(prog_port_pin(&pg, 0u), 13u);
    /* A pin that is no generator pin is not kept at all. */
    fresh(1u);
    prog_port_defaults(kept, 48u);
    prog_port_restore(&pg, kept, &o, 0u);
    CHECK_EQ(reg(PIN0), LINK_PG_PIN_NONE);
    prog_port_defaults(kept, 64u);
    prog_port_restore(&pg, kept, &o, 0u);
    CHECK_EQ(reg(PIN0), LINK_PG_PIN_NONE);
    CHECK_EQ(reg(LINK_PG_FLAGS), 0u);
    /* Two kept ports on one pin, or on one compare register: the first
     * has it. */
    fresh(2u);
    prog_port_defaults(kept, 13u);
    kept[PROG_PORT_KEPT] = 29u;
    prog_port_restore(&pg, kept, &o, 0u);
    CHECK_EQ(prog_port_pin(&pg, 0u), 13u);
    CHECK_EQ(prog_port_pin(&pg, 1u), PROG_PORT_NO_PIN);
    prog_port_bound(&pg, 0x03u);
    CHECK_EQ(reg(LINK_PG_FLAGS), LINK_PG_F_BOUND(0u) | LINK_PG_F_REFUSED(1u));
}

TEST_CASE(a_pin_is_refused_where_something_else_holds_it)
{
    fresh(2u);
    CHECK_EQ(put(PIN0, 3u), LINK_NACK_BAD_VALUE);    /* reserved         */
    CHECK_EQ(put(PIN0, 12u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(put(PIN0, 4u), LINK_NACK_BAD_VALUE);    /* a slot's         */
    CHECK_EQ(put(PIN0, 5u), LINK_NACK_BAD_VALUE);    /* a PWM slot's slice */
    CHECK_EQ(put(PIN0, 20u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(put(PIN0, 48u), LINK_NACK_BAD_VALUE);   /* no PWM slice     */
    CHECK_EQ(put(PIN0, 63u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(put(PIN0, 64u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(put(PIN0, 0xFFFEu), LINK_NACK_BAD_VALUE);
    const uint16_t pin = 13u;
    CHECK_EQ(prog_port_write(&pg, PIN0, 1u, &pin, &o, BIT(13), false, 0u),
             LINK_NACK_BAD_VALUE);
    CHECK_EQ(reg(PIN0), LINK_PG_PIN_NONE);
    /* The last pin of the generator, and the slice of a slot that is not
     * PWM. */
    CHECK_EQ(put(PIN0, 47u), 0u);
    CHECK_EQ(put(PIN0, LINK_PG_PIN_NONE), 0u);
    const out_slot_t dshot = { .driver = OUT_DRIVER_DSHOT, .first_channel = 1,
                               .channels = 1, .pin = 6, .rate_hz = 300 };
    CHECK(outputs_configure(&o, 1, &dshot));
    CHECK_EQ(put(PIN0, 6u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(put(PIN0, 7u), 0u);
    /* Another port's pin and its compare register; its slice partner is
     * free, both run at the one port rate. */
    CHECK_EQ(put(PIN0, 13u), 0u);
    CHECK_EQ(put(PIN1, 13u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(put(PIN1, 29u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(put(PIN1, 28u), 0u);
    CHECK_EQ(prog_port_pins(&pg), BIT(13) | BIT(28));
    /* A pin the ports hold is theirs although the caller has reserved it
     * in the bank for them. */
    outputs_reserve_pins(&o, BIT(3) | BIT(12) | BIT(13) | BIT(28));
    CHECK_EQ(put(PIN1, LINK_PG_PIN_NONE), 0u);
    CHECK_EQ(put(PIN1, 13u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(put(PIN0, LINK_PG_PIN_NONE), 0u);
    /* Two ports swapped in one frame, and two put on one pin in one. */
    outputs_reserve_pins(&o, BIT(3) | BIT(12));
    CHECK_EQ(put(PIN0, 13u), 0u);
    CHECK_EQ(put(PIN1, 14u), 0u);
    uint16_t both[LINK_PG_STRIDE + 1u] = { 14u, 500u, 0u, 1000u, 2000u, 13u };
    CHECK_EQ(prog_port_write(&pg, PIN0, LINK_PG_STRIDE + 1u, both, &o, 0u,
                             false, 0u), 0u);
    CHECK_EQ(prog_port_pin(&pg, 0u), 14u);
    CHECK_EQ(prog_port_pin(&pg, 1u), 13u);
    both[0] = 13u;
    CHECK_EQ(prog_port_write(&pg, PIN0, LINK_PG_STRIDE + 1u, both, &o, 0u,
                             false, 0u), LINK_NACK_BAD_VALUE);
    both[0] = 16u;
    both[LINK_PG_STRIDE] = 16u;
    CHECK_EQ(prog_port_write(&pg, PIN0, LINK_PG_STRIDE + 1u, both, &o, 0u,
                             false, 0u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(prog_port_pin(&pg, 0u), 14u);
    CHECK_EQ(prog_port_pin(&pg, 1u), 13u);
}

TEST_CASE(a_pin_does_not_move_under_anything_that_drives_it)
{
    const uint16_t pin = 14u;

    /* The bank armed. */
    fresh(1u);
    bind(0u, 13u);
    outputs_arm(&o, true, 0u);
    CHECK_EQ(put(PIN0, 14u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(put(PIN0, LINK_PG_PIN_NONE), LINK_NACK_BAD_VALUE);
    /* The pin it has, written again, moves nothing. */
    CHECK_EQ(put(PIN0, 13u), 0u);
    CHECK_EQ(reg(LINK_PG_FLAGS), LINK_PG_F_BOUND(0u));
    outputs_arm(&o, false, 0u);
    /* The wire driver. */
    CHECK_EQ(prog_port_write(&pg, PIN0, 1u, &pin, &o, 0u, true, 0u),
             LINK_NACK_BAD_VALUE);
    /* The port's own pulse, on before or switched on by the same frame. */
    CHECK_EQ(put(LINK_PG_PWM, 1u), 0u);
    CHECK_EQ(put(PIN0, 14u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(put(LINK_PG_PWM, 0u), 0u);
    const uint16_t frame[3] = { 1u, 0u, 14u };
    CHECK_EQ(prog_port_write(&pg, LINK_PG_PWM, 3u, frame, &o, 0u, false, 0u),
             LINK_NACK_BAD_VALUE);
    /* The tester's sweep. */
    CHECK_EQ(put(LINK_PG_TARGET, 1u), 0u);
    CHECK_EQ(put(PIN0, 14u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(put(LINK_PG_TARGET, 0u), 0u);
    const uint16_t aim[3] = { 0u, 1u, 14u };
    CHECK_EQ(prog_port_write(&pg, LINK_PG_PWM, 3u, aim, &o, 0u, false, 0u),
             LINK_NACK_BAD_VALUE);
    CHECK_EQ(reg(PIN0), 13u);
    CHECK_EQ(reg(LINK_PG_TARGET), 0u);
    /* Nothing drives it: it moves, and is not bound until the caller has
     * bound it. */
    CHECK_EQ(put(PIN0, 14u), 0u);
    CHECK_EQ(reg(LINK_PG_FLAGS), LINK_PG_F_REFUSED(0u));
    CHECK_EQ(put(LINK_PG_PWM, 1u), LINK_NACK_BAD_VALUE);
    prog_port_bound(&pg, 0x01u);
    CHECK_EQ(put(LINK_PG_PWM, 1u), 0u);
}

TEST_CASE(the_pulse_and_the_tester_only_on_a_port_the_generator_has)
{
    fresh(2u);
    CHECK_EQ(put(LINK_PG_PWM, 1u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(put(LINK_PG_TARGET, 1u), LINK_NACK_BAD_VALUE);
    bind(0u, 13u);
    CHECK_EQ(put(LINK_PG_PWM, 1u), 0u);
    CHECK_EQ(put(LINK_PG_PWM, 2u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(put(LINK_PG_PWM, 3u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(put(LINK_PG_PWM, 0x0100u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(put(LINK_PG_TARGET, 1u), 0u);
    CHECK_EQ(put(LINK_PG_TARGET, 2u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(put(LINK_PG_TARGET, 5u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(put(LINK_PG_TARGET, 0xFFFFu), LINK_NACK_BAD_VALUE);
    CHECK_EQ(prog_port_pwm(&pg), 1u);
    CHECK_EQ(prog_port_target(&pg), 1u);
    CHECK_EQ(prog_port_pwm(NULL), 0u);
    CHECK_EQ(prog_port_target(NULL), 0u);
    bind(1u, 14u);
    CHECK_EQ(put(LINK_PG_PWM, 3u), 0u);
    CHECK_EQ(put(LINK_PG_TARGET, 2u), 0u);
    /* A pin the generator gives up takes its pulse and the tester with
     * it. */
    prog_port_bound(&pg, 0x01u);
    CHECK_EQ(reg(LINK_PG_PWM), 1u);
    CHECK_EQ(reg(LINK_PG_TARGET), 0u);
    CHECK_EQ(reg(LINK_PG_FLAGS), LINK_PG_F_BOUND(0u) | LINK_PG_F_REFUSED(1u));
    /* A port without a pin is never bound. */
    fresh(2u);
    prog_port_bound(&pg, 0xFFu);
    CHECK_EQ(reg(LINK_PG_FLAGS), 0u);
    prog_port_bound(NULL, 0xFFu);
}

TEST_CASE(a_command_is_clamped_and_reaches_the_ports_channel)
{
    fresh(1u);
    bind(0u, 13u);
    const uint16_t cmd = 750u;
    CHECK_EQ(prog_port_write(&pg, LINK_PG_REG(0u, LINK_PG_CMD), 1u, &cmd, &o,
                             0u, false, 1234u), 0u);
    CHECK_EQ(o.channel[9].command, 750u);
    CHECK_EQ(o.channel[9].last_command_ms, 1234u);
    CHECK_EQ(put(LINK_PG_REG(0u, LINK_PG_CMD), 1000u), 0u);
    CHECK_EQ(reg(LINK_PG_REG(0u, LINK_PG_CMD)), 1000u);
    CHECK_EQ(put(LINK_PG_REG(0u, LINK_PG_CMD), 1001u), 0u);
    CHECK_EQ(reg(LINK_PG_REG(0u, LINK_PG_CMD)), 1000u);
    CHECK_EQ(put(LINK_PG_REG(0u, LINK_PG_CMD), 0xFFFFu), 0u);
    CHECK_EQ(reg(LINK_PG_REG(0u, LINK_PG_CMD)), 1000u);
    CHECK_EQ(o.channel[9].command, 1000u);
    CHECK_EQ(put(LINK_PG_REG(0u, LINK_PG_CMD), 0u), 0u);
    CHECK_EQ(o.channel[9].command, 0u);
    /* No other channel hears it. */
    CHECK_EQ(o.channel[0].last_command_ms, 0u);
    CHECK_EQ(o.channel[8].last_command_ms, 0u);
    CHECK_EQ(o.channel[10].last_command_ms, 0u);
    /* Armed too: a command is what an armed bank is for. */
    outputs_arm(&o, true, 0u);
    CHECK_EQ(put(LINK_PG_REG(0u, LINK_PG_CMD), 400u), 0u);
    CHECK_EQ(o.channel[9].command, 400u);
}

TEST_CASE(the_endpoints_and_the_slew_are_held_to_the_channels_range)
{
    fresh(1u);
    CHECK_EQ(put(LINK_PG_REG(0u, LINK_PG_MIN_US), 399u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(put(LINK_PG_REG(0u, LINK_PG_MIN_US), 2501u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(put(LINK_PG_REG(0u, LINK_PG_MAX_US), 399u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(put(LINK_PG_REG(0u, LINK_PG_MAX_US), 2501u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(reg(LINK_PG_REG(0u, LINK_PG_MIN_US)), 1000u);
    CHECK_EQ(reg(LINK_PG_REG(0u, LINK_PG_MAX_US)), 2000u);
    CHECK_EQ(put(LINK_PG_REG(0u, LINK_PG_MIN_US), 400u), 0u);
    CHECK_EQ(put(LINK_PG_REG(0u, LINK_PG_MAX_US), 2500u), 0u);
    CHECK_EQ(o.channel[9].min_us, 400u);
    CHECK_EQ(o.channel[9].max_us, 2500u);
    CHECK_EQ(put(LINK_PG_REG(0u, LINK_PG_SLEW), 0xFFFFu), LINK_NACK_BAD_VALUE);
    CHECK_EQ(put(LINK_PG_REG(0u, LINK_PG_SLEW), LINK_CC_SLEW_MAX), 0u);
    CHECK_EQ(o.channel[9].slew_per_s, LINK_CC_SLEW_MAX);
    CHECK_EQ(o.channel[9].role, OUT_ROLE_SURFACE);
    /* A reversed pair is a reversed servo to the page; the channel renders
     * between the two as the CHANNELS page's does. */
    CHECK_EQ(put(LINK_PG_REG(0u, LINK_PG_MIN_US), 2500u), 0u);
    CHECK_EQ(put(LINK_PG_REG(0u, LINK_PG_MAX_US), 400u), 0u);
    CHECK_EQ(reg(LINK_PG_REG(0u, LINK_PG_MIN_US)), 2500u);
    CHECK_EQ(o.channel[9].min_us, 400u);
    CHECK_EQ(o.channel[9].max_us, 2500u);
    /* Armed too. */
    outputs_arm(&o, true, 0u);
    CHECK_EQ(put(LINK_PG_REG(0u, LINK_PG_MIN_US), 900u), 0u);
}

/* The slew is held to CHAN_CFG's: 0 to LINK_CC_SLEW_MAX, and a refused
 * slew stores nothing on the page or in the bank. */
TEST_CASE(a_slew_above_the_maximum_is_refused_and_stores_nothing)
{
    const unsigned slew1 = LINK_PG_REG(1u, LINK_PG_SLEW);

    fresh(2u);
    CHECK_EQ(put(LINK_PG_REG(0u, LINK_PG_SLEW), 0u), 0u);
    CHECK_EQ(put(LINK_PG_REG(0u, LINK_PG_SLEW), LINK_CC_SLEW_MAX), 0u);
    CHECK_EQ(reg(LINK_PG_REG(0u, LINK_PG_SLEW)), LINK_CC_SLEW_MAX);
    CHECK_EQ(o.channel[9].slew_per_s, LINK_CC_SLEW_MAX);
    const prog_port_t was = pg;
    const outputs_t bank = o;
    const uint16_t bad[] = { LINK_CC_SLEW_MAX + 1u, 10000u, 0xFFFFu };
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        CHECK_EQ(put(LINK_PG_REG(0u, LINK_PG_SLEW), bad[i]),
                 LINK_NACK_BAD_VALUE);
        CHECK_EQ(put(slew1, bad[i]), LINK_NACK_BAD_VALUE);
    }
    /* In a frame with good registers before it, nothing of the frame. */
    const uint16_t frame[3] = { 500u, LINK_CC_SLEW_MAX + 1u, 900u };
    CHECK_EQ(prog_port_write(&pg, LINK_PG_REG(0u, LINK_PG_CMD), 3u, frame, &o,
                             0u, false, 0u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(memcmp(&was, &pg, sizeof(pg)), 0);
    CHECK_EQ(memcmp(&bank, &o, sizeof(o)), 0);
    /* The last port's slew is judged as well. */
    CHECK_EQ(put(slew1, LINK_CC_SLEW_MAX), 0u);
    CHECK_EQ(o.channel[10].slew_per_s, LINK_CC_SLEW_MAX);
}

/* A slew kept above the maximum by a build before the limit starts as the
 * maximum, on the page, in the bank and in what is kept again. */
TEST_CASE(a_kept_slew_above_the_maximum_starts_as_the_maximum)
{
    uint16_t kept[PROG_PORT_KEPT_COUNT];
    uint16_t back[PROG_PORT_KEPT_COUNT];
    const uint16_t old[] = { LINK_CC_SLEW_MAX + 1u, 0xFFFFu };

    for (unsigned i = 0; i < sizeof(old) / sizeof(old[0]); ++i) {
        fresh(2u);
        prog_port_defaults(kept, 13u);
        kept[1] = old[i];
        kept[PROG_PORT_KEPT + 1u] = LINK_CC_SLEW_MAX;
        prog_port_restore(&pg, kept, &o, 0u);
        CHECK_EQ(reg(LINK_PG_REG(0u, LINK_PG_SLEW)), LINK_CC_SLEW_MAX);
        CHECK_EQ(o.channel[9].slew_per_s, LINK_CC_SLEW_MAX);
        CHECK_EQ(reg(LINK_PG_REG(1u, LINK_PG_SLEW)), LINK_CC_SLEW_MAX);
        /* The rest of the port as kept. */
        CHECK_EQ(reg(PIN0), 13u);
        CHECK_EQ(reg(LINK_PG_REG(0u, LINK_PG_MIN_US)), LINK_CC_DEFAULT_MIN);
        CHECK_EQ(reg(LINK_PG_REG(0u, LINK_PG_MAX_US)), LINK_CC_DEFAULT_MAX);
        prog_port_kept(&pg, back);
        CHECK_EQ(back[1], LINK_CC_SLEW_MAX);
    }
}

TEST_CASE(a_refused_write_stores_none_of_its_registers)
{
    fresh(1u);
    bind(0u, 13u);
    const prog_port_t was = pg;
    const outputs_t bank = o;
    /* A good pulse, a good command, a bad endpoint. */
    const uint16_t frame[7] = { 1u, 1u, 13u, 900u, 77u, 399u, 2000u };
    CHECK_EQ(prog_port_write(&pg, LINK_PG_PWM, 7u, frame, &o, 0u, false, 50u),
             LINK_NACK_BAD_VALUE);
    CHECK_EQ(memcmp(&was, &pg, sizeof(pg)), 0);
    CHECK_EQ(memcmp(&bank, &o, sizeof(o)), 0);
    /* The same with the endpoint in range is taken whole. */
    const uint16_t good[7] = { 1u, 1u, 13u, 900u, 77u, 800u, 2200u };
    CHECK_EQ(prog_port_write(&pg, LINK_PG_PWM, 7u, good, &o, 0u, false, 50u),
             0u);
    CHECK_EQ(reg(LINK_PG_PWM), 1u);
    CHECK_EQ(reg(LINK_PG_TARGET), 1u);
    CHECK_EQ(o.channel[9].command, 900u);
    CHECK_EQ(o.channel[9].slew_per_s, 77u);
    CHECK_EQ(o.channel[9].min_us, 800u);
    CHECK_EQ(o.channel[9].max_us, 2200u);
    CHECK_EQ(reg(LINK_PG_FLAGS), LINK_PG_F_BOUND(0u));
}

TEST_CASE(read_only_registers_the_page_end_and_absent_ports_are_refused)
{
    uint16_t in[LINK_PG_REGS + 1u];
    memset(in, 0, sizeof(in));

    fresh(1u);
    CHECK_EQ(put(LINK_PG_COUNT, 2u), LINK_NACK_READ_ONLY);
    CHECK_EQ(put(LINK_PG_FLAGS, 1u), LINK_NACK_READ_ONLY);
    CHECK_EQ(prog_port_write(&pg, LINK_PG_FLAGS, 2u, in, &o, 0u, false, 0u),
             LINK_NACK_READ_ONLY);
    /* The page end, at it and one past it. */
    CHECK_EQ(prog_port_write(&pg, LINK_PG_REGS, 0u, in, &o, 0u, false, 0u),
             0u);
    CHECK_EQ(prog_port_write(&pg, LINK_PG_REGS, 1u, in, &o, 0u, false, 0u),
             LINK_NACK_BAD_RANGE);
    CHECK_EQ(prog_port_write(&pg, LINK_PG_PWM, LINK_PG_REGS - 1u, in, &o, 0u,
                             false, 0u), LINK_NACK_BAD_RANGE);
    CHECK_EQ(prog_port_write(&pg, 255u, 255u, in, &o, 0u, false, 0u),
             LINK_NACK_BAD_RANGE);
    /* An empty write anywhere on the page changes nothing. */
    CHECK_EQ(prog_port_write(&pg, LINK_PG_COUNT, 0u, in, &o, 0u, false, 0u),
             0u);
    /* The last register of the port the build has, and the first of one
     * it has not. */
    CHECK_EQ(put(LINK_PG_REG(0u, LINK_PG_MAX_US), 2000u), 0u);
    CHECK_EQ(put(PIN1, LINK_PG_PIN_NONE), LINK_NACK_BAD_VALUE);
    CHECK_EQ(put(LINK_PG_REG(3u, LINK_PG_MAX_US), 2000u), LINK_NACK_BAD_VALUE);
    /* Nothing without all three. */
    CHECK_EQ(prog_port_write(NULL, LINK_PG_PWM, 1u, in, &o, 0u, false, 0u),
             LINK_NACK_BAD_RANGE);
    CHECK_EQ(prog_port_write(&pg, LINK_PG_PWM, 1u, NULL, &o, 0u, false, 0u),
             LINK_NACK_BAD_RANGE);
    CHECK_EQ(prog_port_write(&pg, LINK_PG_PWM, 1u, in, NULL, 0u, false, 0u),
             LINK_NACK_BAD_RANGE);
    /* A read past the end, or into nothing, writes nothing. */
    uint16_t out[LINK_PG_REGS + 1u];
    memset(out, 0xA5, sizeof(out));
    prog_port_read(&pg, 0u, LINK_PG_REGS + 1u, out);
    prog_port_read(&pg, LINK_PG_REGS, 1u, out);
    prog_port_read(NULL, 0u, 1u, out);
    prog_port_read(&pg, 0u, 1u, NULL);
    CHECK_EQ(out[0], 0xA5A5u);
    prog_port_read(&pg, 0u, LINK_PG_REGS, out);
    CHECK_EQ(out[LINK_PG_COUNT], 1u);
    CHECK_EQ(out[LINK_PG_REGS - 1u], 2000u);
    CHECK_EQ(out[LINK_PG_REGS], 0xA5A5u);
}

/* The other direction of the pin rule: the OUTPUTS page asks before a slot
 * takes a pin. */
TEST_CASE(a_slot_is_refused_on_a_ports_pin_and_beside_it)
{
    uint16_t slots[LINK_OS_COUNT];
    memset(slots, 0, sizeof(slots));
    uint16_t *last = &slots[(LINK_OUT_SLOTS - 1u) * LINK_OS_STRIDE];

    fresh(2u);
    bind(0u, 13u);
    CHECK_EQ(prog_port_slots_check(&pg, slots), 0u);
    /* The pin itself, whatever the driver. */
    last[LINK_OS_DRIVER] = (uint16_t)OUT_DRIVER_DSHOT;
    last[LINK_OS_PIN] = 13u;
    CHECK_EQ(prog_port_slots_check(&pg, slots), LINK_NACK_BAD_VALUE);
    last[LINK_OS_DRIVER] = (uint16_t)OUT_DRIVER_PWM;
    CHECK_EQ(prog_port_slots_check(&pg, slots), LINK_NACK_BAD_VALUE);
    /* An unused slot names no pin. */
    last[LINK_OS_DRIVER] = (uint16_t)OUT_DRIVER_NONE;
    CHECK_EQ(prog_port_slots_check(&pg, slots), 0u);
    /* The slice: a PWM slot would set the port's frame rate, another
     * driver does not use the slice. */
    last[LINK_OS_DRIVER] = (uint16_t)OUT_DRIVER_PWM;
    last[LINK_OS_PIN] = 12u;
    CHECK_EQ(prog_port_slots_check(&pg, slots), LINK_NACK_BAD_VALUE);
    last[LINK_OS_PIN] = 28u;
    CHECK_EQ(prog_port_slots_check(&pg, slots), LINK_NACK_BAD_VALUE);
    last[LINK_OS_PIN] = 29u;
    CHECK_EQ(prog_port_slots_check(&pg, slots), LINK_NACK_BAD_VALUE);
    last[LINK_OS_DRIVER] = (uint16_t)OUT_DRIVER_DSHOT;
    CHECK_EQ(prog_port_slots_check(&pg, slots), 0u);
    last[LINK_OS_DRIVER] = (uint16_t)OUT_DRIVER_PWM;
    last[LINK_OS_PIN] = 14u;
    CHECK_EQ(prog_port_slots_check(&pg, slots), 0u);
    /* A pin past the bank is the OUTPUTS page's to refuse. */
    last[LINK_OS_PIN] = 64u;
    CHECK_EQ(prog_port_slots_check(&pg, slots), 0u);
    /* A port that holds no pin, or has given it up, refuses nothing. */
    last[LINK_OS_PIN] = 13u;
    CHECK_EQ(put(PIN0, LINK_PG_PIN_NONE), 0u);
    CHECK_EQ(prog_port_slots_check(&pg, slots), 0u);
    /* A kept pin the port does not hold refuses nothing either. */
    uint16_t kept[PROG_PORT_KEPT_COUNT];
    fresh(1u);
    prog_port_defaults(kept, 13u);
    prog_port_restore(&pg, kept, &o, BIT(13));
    CHECK_EQ(prog_port_slots_check(&pg, slots), 0u);
    CHECK_EQ(prog_port_slots_check(NULL, slots), 0u);
}

/* A slot's channels stop below the ports' bank channels, at both ends. */
TEST_CASE(a_slot_is_refused_on_a_ports_bank_channel)
{
    uint16_t slots[LINK_OS_COUNT];
    memset(slots, 0, sizeof(slots));
    uint16_t *last = &slots[(LINK_OUT_SLOTS - 1u) * LINK_OS_STRIDE];
    const uint8_t lo = (uint8_t)PROG_PORT_CHANNEL0;
    const uint8_t hi = (uint8_t)(PROG_PORT_CHANNEL0 + LINK_PG_PORTS_MAX);

    /* No port holds a pin: the channels are the ports' all the same. */
    fresh(1u);
    last[LINK_OS_DRIVER] = (uint16_t)OUT_DRIVER_DSHOT;
    last[LINK_OS_PIN] = 2u;
    /* Up to the channel under the first port's. */
    last[LINK_OS_RANGE] = LINK_OS_RANGE_OF(0u, lo);
    CHECK_EQ(prog_port_slots_check(&pg, slots), 0u);
    CHECK_EQ(prog_port_slots_free(slots), 0u);
    last[LINK_OS_RANGE] = LINK_OS_RANGE_OF(lo - 1u, 1u);
    CHECK_EQ(prog_port_slots_check(&pg, slots), 0u);
    /* One further reaches it. */
    last[LINK_OS_RANGE] = LINK_OS_RANGE_OF(0u, lo + 1u);
    CHECK_EQ(prog_port_slots_check(&pg, slots), LINK_NACK_BAD_VALUE);
    last[LINK_OS_RANGE] = LINK_OS_RANGE_OF(lo - 1u, 2u);
    CHECK_EQ(prog_port_slots_check(&pg, slots), LINK_NACK_BAD_VALUE);
    /* Each port's channel alone, of a port the build has or not. */
    for (uint8_t c = lo; c < hi; ++c) {
        last[LINK_OS_RANGE] = LINK_OS_RANGE_OF(c, 1u);
        CHECK_EQ(prog_port_slots_check(&pg, slots), LINK_NACK_BAD_VALUE);
    }
    /* A range that spans them all. */
    last[LINK_OS_RANGE] = LINK_OS_RANGE_OF(0u, OUT_MAX_CHANNELS);
    CHECK_EQ(prog_port_slots_check(&pg, slots), LINK_NACK_BAD_VALUE);
    /* The first channel past the last port's is free, and so is a range of
     * no channel on a port's. */
    last[LINK_OS_RANGE] = LINK_OS_RANGE_OF(hi, 1u);
    CHECK_EQ(prog_port_slots_check(&pg, slots), 0u);
    last[LINK_OS_RANGE] = LINK_OS_RANGE_OF(lo, 0u);
    CHECK_EQ(prog_port_slots_check(&pg, slots), 0u);
    /* An unused slot names no channel. */
    last[LINK_OS_RANGE] = LINK_OS_RANGE_OF(lo, 1u);
    last[LINK_OS_DRIVER] = (uint16_t)OUT_DRIVER_NONE;
    CHECK_EQ(prog_port_slots_check(&pg, slots), 0u);
    CHECK_EQ(prog_port_slots_free(slots), 0u);

    /* A stored page: the slots on a port's channel start unbound, the
     * others stay. */
    slots[LINK_OS_DRIVER] = (uint16_t)OUT_DRIVER_PWM;
    slots[LINK_OS_PIN] = 3u;
    slots[LINK_OS_RANGE] = LINK_OS_RANGE_OF(0u, 4u);
    last[LINK_OS_DRIVER] = (uint16_t)OUT_DRIVER_DSHOT;
    last[LINK_OS_RANGE] = LINK_OS_RANGE_OF(hi - 1u, 1u);
    CHECK_EQ(prog_port_slots_check(&pg, slots), LINK_NACK_BAD_VALUE);
    CHECK_EQ(prog_port_slots_free(slots), 1u);
    CHECK_EQ(last[LINK_OS_DRIVER], (uint16_t)OUT_DRIVER_NONE);
    CHECK_EQ(last[LINK_OS_PIN], 2u);
    CHECK_EQ(slots[LINK_OS_DRIVER], (uint16_t)OUT_DRIVER_PWM);
    CHECK_EQ(slots[LINK_OS_RANGE], LINK_OS_RANGE_OF(0u, 4u));
    CHECK_EQ(prog_port_slots_check(&pg, slots), 0u);
    CHECK_EQ(prog_port_slots_free(NULL), 0u);
    CHECK_EQ(prog_port_slots_check(&pg, NULL), LINK_NACK_BAD_VALUE);
}

/* The caller could not bind what the page accepted: the page and the
 * channel go back to what they were. */
TEST_CASE(a_write_is_taken_back_whole)
{
    fresh(1u);
    bind(0u, 13u);
    CHECK_EQ(put(LINK_PG_REG(0u, LINK_PG_SLEW), 200u), 0u);
    const prog_port_t was = pg;
    const uint16_t frame[LINK_PG_STRIDE] = { 14u, 500u, 900u, 600u, 2400u };
    CHECK_EQ(prog_port_write(&pg, PIN0, LINK_PG_STRIDE, frame, &o, 0u, false,
                             0u), 0u);
    CHECK_EQ(prog_port_pin(&pg, 0u), 14u);
    CHECK_EQ(o.channel[9].min_us, 600u);
    prog_port_revert(&pg, &was, &o);
    CHECK_EQ(memcmp(&was, &pg, sizeof(pg)), 0);
    CHECK_EQ(prog_port_pin(&pg, 0u), 13u);
    CHECK_EQ(reg(LINK_PG_FLAGS), LINK_PG_F_BOUND(0u));
    CHECK_EQ(o.channel[9].slew_per_s, 200u);
    CHECK_EQ(o.channel[9].min_us, 1000u);
    CHECK_EQ(o.channel[9].max_us, 2000u);
    prog_port_revert(NULL, &was, &o);
    prog_port_revert(&pg, NULL, &o);
    prog_port_revert(&pg, &was, NULL);
    CHECK_EQ(memcmp(&was, &pg, sizeof(pg)), 0);
    CHECK_EQ(prog_port_pin(NULL, 0u), PROG_PORT_NO_PIN);
    CHECK_EQ(prog_port_pin(&pg, 1u), PROG_PORT_NO_PIN);
    CHECK_EQ(prog_port_pin(&pg, 200u), PROG_PORT_NO_PIN);
    CHECK_EQ(prog_port_pins(NULL), 0u);
    prog_port_init(NULL, 1u);
}

/*
 * Protocol 4.16: no port whose pulse is on carries an endpoint longer than
 * its frame -- one microsecond under the period of the rate the ports run
 * at.  At 560 Hz (period 1785 us) 1784 us is taken and 1785 and 1786 us are
 * refused; at 500 Hz (2000 us) 1999 is taken and 2000 refused; at 50 Hz the
 * 2500 us ceiling governs.  A pulse switched on under a range that does
 * not fit is refused as well, and a port whose pulse is off is not judged.
 */
TEST_CASE(a_pulsing_port_takes_no_endpoint_longer_than_its_frame)
{
    static const struct {
        uint16_t hz;
        uint16_t period;
    } k[] = { { 560u, 1785u }, { 500u, 2000u } };
    for (unsigned i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        fresh(1u);
        CHECK_EQ(pg.hz, PROG_PORT_RATE_HZ);
        bind(0u, 13u);
        prog_port_rate(&pg, k[i].hz);
        /* 1000 to 2000 us does not fit: the pulse stays off. */
        if (k[i].period <= 2000u) {
            CHECK_EQ(put(LINK_PG_PWM, 1u), LINK_NACK_BAD_VALUE);
            CHECK_EQ(reg(LINK_PG_PWM), 0u);
        }
        /* With the pulse off any range is taken. */
        CHECK_EQ(put(LINK_PG_REG(0u, LINK_PG_MAX_US), 2500u), 0u);
        CHECK_EQ(put(LINK_PG_REG(0u, LINK_PG_MAX_US),
                     (uint16_t)(k[i].period - 1u)), 0u);
        CHECK_EQ(put(LINK_PG_PWM, 1u), 0u);
        CHECK_EQ(prog_port_pwm_channels(&pg), (uint16_t)(1u << 9));
        /* Pulsing: the period and one more are refused, on either end. */
        CHECK_EQ(put(LINK_PG_REG(0u, LINK_PG_MAX_US), k[i].period),
                 LINK_NACK_BAD_VALUE);
        CHECK_EQ(put(LINK_PG_REG(0u, LINK_PG_MAX_US),
                     (uint16_t)(k[i].period + 1u)), LINK_NACK_BAD_VALUE);
        CHECK_EQ(put(LINK_PG_REG(0u, LINK_PG_MIN_US), k[i].period),
                 LINK_NACK_BAD_VALUE);
        CHECK_EQ(reg(LINK_PG_REG(0u, LINK_PG_MAX_US)), k[i].period - 1u);
        CHECK_EQ(o.channel[9].max_us, k[i].period - 1u);
        /* The pulse and its range in one frame are judged together. */
        const uint16_t frame[2] = { 1u, 0u };
        CHECK_EQ(prog_port_write(&pg, LINK_PG_PWM, 1u, frame, &o, 0u, false,
                                 0u), 0u);
        const uint16_t off_range[3] = { 0u, 1000u, k[i].period };
        CHECK_EQ(prog_port_write(&pg, LINK_PG_REG(0u, LINK_PG_SLEW), 3u,
                                 off_range, &o, 0u, false, 0u),
                 LINK_NACK_BAD_VALUE);
        /* Off, the range is the port's again. */
        CHECK_EQ(put(LINK_PG_PWM, 0u), 0u);
        CHECK_EQ(prog_port_pwm_channels(&pg), 0u);
        CHECK_EQ(prog_port_write(&pg, LINK_PG_REG(0u, LINK_PG_SLEW), 3u,
                                 off_range, &o, 0u, false, 0u), 0u);
        CHECK_EQ(put(LINK_PG_PWM, 1u), LINK_NACK_BAD_VALUE);
    }
    /* At 50 Hz the frame holds the ceiling. */
    fresh(2u);
    bind(0u, 13u);
    bind(1u, 14u);
    prog_port_rate(&pg, 50u);
    CHECK_EQ(put(LINK_PG_REG(0u, LINK_PG_MAX_US), 2500u), 0u);
    CHECK_EQ(put(LINK_PG_PWM, 3u), 0u);
    CHECK_EQ(prog_port_pwm_channels(&pg), (uint16_t)((1u << 9) | (1u << 10)));
    /* The rate the page judges by is the one last named, and every port
     * that pulses is judged: port 0 at 2500 us refuses a write to port 1
     * at 400 Hz until it is shortened. */
    prog_port_rate(&pg, 400u);
    CHECK_EQ(put(LINK_PG_REG(1u, LINK_PG_MAX_US), 2400u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(put(LINK_PG_REG(0u, LINK_PG_MAX_US), 2499u), 0u);
    CHECK_EQ(put(LINK_PG_REG(1u, LINK_PG_MAX_US), 2499u), 0u);
    CHECK_EQ(put(LINK_PG_REG(1u, LINK_PG_MAX_US), 2500u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(prog_port_pwm_channels(NULL), 0u);
    prog_port_rate(NULL, 50u);
}

int main(void)
{
    RUN(a_page_starts_with_no_pin_on_any_port);
    RUN(the_port_count_is_held_to_the_models_four);
    RUN(a_board_that_has_stored_none_has_port_0_on_the_default_pin);
    RUN(the_kept_set_is_restored_through_the_pages_checks);
    RUN(a_kept_pin_something_else_holds_is_flagged_and_not_held);
    RUN(a_pin_is_refused_where_something_else_holds_it);
    RUN(a_pin_does_not_move_under_anything_that_drives_it);
    RUN(the_pulse_and_the_tester_only_on_a_port_the_generator_has);
    RUN(a_command_is_clamped_and_reaches_the_ports_channel);
    RUN(the_endpoints_and_the_slew_are_held_to_the_channels_range);
    RUN(a_slew_above_the_maximum_is_refused_and_stores_nothing);
    RUN(a_kept_slew_above_the_maximum_starts_as_the_maximum);
    RUN(a_refused_write_stores_none_of_its_registers);
    RUN(read_only_registers_the_page_end_and_absent_ports_are_refused);
    RUN(a_slot_is_refused_on_a_ports_pin_and_beside_it);
    RUN(a_slot_is_refused_on_a_ports_bank_channel);
    RUN(a_write_is_taken_back_whole);
    RUN(a_pulsing_port_takes_no_endpoint_longer_than_its_frame);
    return test_summary("prog_port");
}
