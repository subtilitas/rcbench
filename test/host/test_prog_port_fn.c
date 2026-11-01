/*
 * The programming ports' functions (shared/outputs/prog_port.c, protocol
 * 4.26 and 4.28): FUNC per port, SERVO PWM, NONE, DSHOT150, DSHOT300 or
 * DSHOT600.
 *
 * Under test: a fresh page with port 0 SERVO PWM and the others NONE; FUNC
 * writes of the two functions taken and every other value refused, a FUNC
 * of a port the build does not have refused; a NONE port's pin under no
 * PWM rule (no slice, the slice of a PWM slot, the compare register of a
 * SERVO PWM port) but under the pin rules (a slot's pin, a reserved pin, a
 * taken pin, another port's pin); a FUNC change refused while the bank is
 * armed, a wire page holds a port, the port's pulse is on or it is the
 * TARGET; a change to SERVO PWM refused on a pin the generator cannot
 * take; the pulse and the TARGET refused on a NONE port; FLAGS bound,
 * parked and refused; the kept word, its restore, a nibble no write would
 * take, a pin judged under the function it is restored with; a record an
 * older image wrote back (the word 0) restoring the defaults; the masks
 * the IOmcu binds and parks from; a revert taking the function back.
 * DShot: the three values taken, the port's channel a throttle at its rest
 * with no slew and back to a surface, a CMD write refused on it, the pulse
 * and the TARGET refused on it, its pin under the pin rules and no PWM
 * rule, FLAGS bit 12 + n, the masks and bit rates the IOmcu binds from,
 * nibbles 3 to 5 restored, and a 4.26 image reading such a nibble as its
 * default.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "link_msg.h"
#include "link_pages.h"
#include "out_store_rec.h"
#include "outputs.h"
#include "prog_port.h"

static outputs_t   o;
static prog_port_t pg;

#define BIT(pin) ((uint64_t)1u << (pin))
#define PIN(port) LINK_PG_REG(port, LINK_PG_PIN)
#define FN(port)  LINK_PG_FUNC(port)
#define PWM_FN    ((uint16_t)LINK_PG_FN_SERVO_PWM)
#define NONE_FN   ((uint16_t)LINK_PG_FN_NONE)
#define D150_FN   ((uint16_t)LINK_PG_FN_DSHOT150)
#define D300_FN   ((uint16_t)LINK_PG_FN_DSHOT300)
#define D600_FN   ((uint16_t)LINK_PG_FN_DSHOT600)

/* A bank with GP3 and GP12 reserved and a PWM output on GP4, and 4 ports
 * without a pin at their default functions. */
static void fresh(void)
{
    outputs_init(&o, 0u);
    outputs_reserve_pins(&o, BIT(3) | BIT(12));
    const out_slot_t pwm = { .driver = OUT_DRIVER_PWM, .first_channel = 0,
                             .channels = 1, .pin = 4, .rate_hz = 50 };
    CHECK(outputs_configure(&o, 0, &pwm));
    prog_port_init(&pg, LINK_PG_PORTS_MAX);
}

static uint16_t reg(unsigned i)
{
    uint16_t v = 0xA5A5u;
    prog_port_read(&pg, (uint8_t)i, 1u, &v);
    return v;
}

static uint8_t put_ex(unsigned off, uint16_t v, uint64_t taken, bool locked)
{
    return prog_port_write(&pg, (uint8_t)off, 1u, &v, &o, taken, locked,
                           0u);
}

static uint8_t put(unsigned off, uint16_t v)
{
    return put_ex(off, v, 0u, false);
}

/* What the IOmcu does after a write: every port that holds a pin is held
 * by its function. */
static void hold_all(void)
{
    uint8_t m = 0u;
    for (uint8_t port = 0; port < LINK_PG_PORTS_MAX; ++port) {
        if (prog_port_pin(&pg, port) != PROG_PORT_NO_PIN) {
            m |= (uint8_t)(1u << port);
        }
    }
    prog_port_bound(&pg, m);
}

TEST_CASE(a_fresh_page_has_port_0_servo_pwm_and_the_others_none)
{
    fresh();
    CHECK_EQ(reg(FN(0u)), PWM_FN);
    for (unsigned port = 1; port < LINK_PG_PORTS_MAX; ++port) {
        CHECK_EQ(reg(FN(port)), NONE_FN);
        CHECK_EQ(prog_port_func(&pg, (uint8_t)port), NONE_FN);
    }
    CHECK_EQ(prog_port_func(&pg, 0u), PWM_FN);
    CHECK_EQ(prog_port_func(&pg, LINK_PG_PORTS_MAX), NONE_FN);
    CHECK_EQ(prog_port_func(NULL, 0u), NONE_FN);
    /* As the kept word: 1 + FUNC in each nibble. */
    CHECK_EQ(prog_port_kept_func(&pg), 0x2221u);
    CHECK_EQ(prog_port_kept_func(NULL), PROG_PORT_FUNC_UNSTORED);
    CHECK_EQ(reg(LINK_PG_REGS - 1u), NONE_FN);
}

TEST_CASE(a_func_write_takes_the_five_functions_and_nothing_else)
{
    fresh();
    CHECK_EQ(put(FN(1u), PWM_FN), 0u);
    CHECK_EQ(reg(FN(1u)), PWM_FN);
    CHECK_EQ(put(FN(1u), D150_FN), 0u);
    CHECK_EQ(reg(FN(1u)), D150_FN);
    CHECK_EQ(put(FN(1u), D300_FN), 0u);
    CHECK_EQ(put(FN(1u), D600_FN), 0u);
    CHECK_EQ(reg(FN(1u)), D600_FN);
    CHECK_EQ(put(FN(1u), NONE_FN), 0u);
    CHECK_EQ(reg(FN(1u)), NONE_FN);
    /* Nothing past DSHOT600. */
    static const uint16_t bad[] = { 5u, 15u, 0x8000u, 0xFFFFu };
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        CHECK_EQ(put(FN(1u), bad[i]), LINK_NACK_BAD_VALUE);
        CHECK_EQ(reg(FN(1u)), NONE_FN);
    }
    /* The whole frame at once, and one past the end. */
    const uint16_t four[LINK_PG_PORTS_MAX] = { NONE_FN, PWM_FN, PWM_FN,
                                               NONE_FN };
    CHECK_EQ(prog_port_write(&pg, LINK_PG_FUNC0, LINK_PG_PORTS_MAX, four, &o,
                             0u, false, 0u), 0u);
    CHECK_EQ(prog_port_kept_func(&pg), 0x2112u);
    CHECK_EQ(prog_port_write(&pg, LINK_PG_FUNC0, LINK_PG_PORTS_MAX + 1u, four,
                             &o, 0u, false, 0u), LINK_NACK_BAD_RANGE);
    /* A build with 2 ports: the FUNC of port 2 is no register it has. */
    prog_port_init(&pg, 2u);
    CHECK_EQ(put(FN(1u), PWM_FN), 0u);
    CHECK_EQ(put(FN(2u), NONE_FN), LINK_NACK_BAD_VALUE);
    CHECK_EQ(put(FN(3u), NONE_FN), LINK_NACK_BAD_VALUE);
    CHECK_EQ(put(LINK_PG_REG(2u, LINK_PG_CMD), 500u), LINK_NACK_BAD_VALUE);
}

TEST_CASE(a_none_port_is_under_the_pin_rules_and_no_pwm_rule)
{
    fresh();
    /* GP48 has no PWM slice: a NONE port takes it, a SERVO PWM one not. */
    CHECK_EQ(put(PIN(1u), 48u), 0u);
    CHECK_EQ(reg(PIN(1u)), 48u);
    CHECK_EQ(put(PIN(0u), 48u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(put(PIN(2u), 49u), 0u);
    /* GP5 shares GP4's slice, a PWM slot's: NONE yes, SERVO PWM no. */
    CHECK_EQ(put(PIN(3u), 5u), 0u);
    CHECK_EQ(put(PIN(0u), 5u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(put(PIN(3u), LINK_PG_PIN_NONE), 0u);
    CHECK_EQ(put(PIN(0u), 5u), LINK_NACK_BAD_VALUE);
    /* The compare register of a SERVO PWM port: GP13 and GP29 share one.
     * A NONE port beside it is no clash; a second SERVO PWM port is. */
    CHECK_EQ(put(PIN(0u), 13u), 0u);
    CHECK_EQ(put(PIN(3u), 29u), 0u);
    CHECK_EQ(put(PIN(3u), LINK_PG_PIN_NONE), 0u);
    CHECK_EQ(put(FN(3u), PWM_FN), 0u);
    CHECK_EQ(put(PIN(3u), 29u), LINK_NACK_BAD_VALUE);
    /* The pin rules every port is under: one pin a port, no slot's pin, no
     * reserved pin, no taken pin, no pin past the header. */
    CHECK_EQ(put(PIN(1u), 13u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(put(PIN(1u), 4u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(put(PIN(1u), 3u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(put_ex(PIN(1u), 20u, BIT(20), false), LINK_NACK_BAD_VALUE);
    CHECK_EQ(put(PIN(1u), 64u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(put(PIN(1u), 0xFFFEu), LINK_NACK_BAD_VALUE);
    CHECK_EQ(reg(PIN(1u)), 48u);
    /* A PWM slot is refused on a NONE port's pin, never beside it. */
    uint16_t slots[LINK_OS_COUNT];
    memset(slots, 0, sizeof(slots));
    hold_all();
    CHECK_EQ(put(PIN(2u), 7u), 0u);   /* GP6's slice partner */
    hold_all();
    slots[LINK_OS_DRIVER] = (uint16_t)OUT_DRIVER_PWM;
    slots[LINK_OS_PIN]    = 6u;
    slots[LINK_OS_RANGE]  = 0u;
    CHECK_EQ(prog_port_slots_check(&pg, slots), 0u);
    slots[LINK_OS_PIN] = 7u;
    CHECK_EQ(prog_port_slots_check(&pg, slots), LINK_NACK_BAD_VALUE);
    /* The same slot beside a SERVO PWM port's pin is refused. */
    slots[LINK_OS_PIN] = 12u;     /* GP13's slice partner */
    CHECK_EQ(prog_port_slots_check(&pg, slots), LINK_NACK_BAD_VALUE);
}

TEST_CASE(a_func_change_waits_for_a_bench_at_rest)
{
    fresh();
    CHECK_EQ(put(PIN(0u), 13u), 0u);
    hold_all();
    /* Locked: the bank armed or asked to arm, or a wire page on a port. */
    CHECK_EQ(put_ex(FN(1u), PWM_FN, 0u, true), LINK_NACK_BAD_VALUE);
    CHECK_EQ(put_ex(FN(0u), NONE_FN, 0u, true), LINK_NACK_BAD_VALUE);
    /* The same value written again is no change, locked or not. */
    CHECK_EQ(put_ex(FN(0u), PWM_FN, 0u, true), 0u);
    /* Not under its own pulse, nor as the TARGET. */
    CHECK_EQ(put(LINK_PG_PWM, 1u), 0u);
    CHECK_EQ(put(FN(0u), NONE_FN), LINK_NACK_BAD_VALUE);
    CHECK_EQ(put(LINK_PG_PWM, 0u), 0u);
    CHECK_EQ(put(LINK_PG_TARGET, 1u), 0u);
    CHECK_EQ(put(FN(0u), NONE_FN), LINK_NACK_BAD_VALUE);
    /* Nor in a frame that names it the TARGET or switches its pulse on. */
    CHECK_EQ(put(LINK_PG_TARGET, 0u), 0u);
    const uint16_t frame[2] = { 1u, 1u };   /* PWM, TARGET */
    CHECK_EQ(prog_port_write(&pg, LINK_PG_PWM, 2u, frame, &o, 0u, false, 0u),
             0u);
    CHECK_EQ(put(LINK_PG_TARGET, 0u), 0u);
    CHECK_EQ(put(LINK_PG_PWM, 0u), 0u);
    uint16_t page[LINK_PG_REGS];
    prog_port_read(&pg, 0u, LINK_PG_REGS, page);
    page[LINK_PG_TARGET] = 1u;
    page[FN(0u)] = NONE_FN;
    CHECK_EQ(prog_port_write(&pg, LINK_PG_TARGET, LINK_PG_REGS - 3u,
                             &page[LINK_PG_TARGET], &o, 0u, false, 0u),
             LINK_NACK_BAD_VALUE);
    CHECK_EQ(reg(FN(0u)), PWM_FN);
    /* At rest: taken, and the port is held anew. */
    CHECK_EQ(put(FN(0u), NONE_FN), 0u);
    CHECK_EQ(reg(LINK_PG_FLAGS), LINK_PG_F_REFUSED(0u));
    hold_all();
    CHECK_EQ(reg(LINK_PG_FLAGS), LINK_PG_F_PARKED(0u));
    /* An armed bank refuses it too. */
    outputs_arm(&o, true, 0u);
    CHECK_EQ(put(FN(0u), PWM_FN), LINK_NACK_BAD_VALUE);
}

TEST_CASE(servo_pwm_is_refused_on_a_pin_the_generator_cannot_take)
{
    fresh();
    CHECK_EQ(put(PIN(1u), 48u), 0u);
    CHECK_EQ(put(FN(1u), PWM_FN), LINK_NACK_BAD_VALUE);
    CHECK_EQ(put(PIN(1u), 5u), 0u);       /* GP4's slice: a PWM slot's */
    CHECK_EQ(put(FN(1u), PWM_FN), LINK_NACK_BAD_VALUE);
    CHECK_EQ(reg(FN(1u)), NONE_FN);
    /* Pin and function in one frame are judged together. */
    const uint16_t pin_fn[1] = { 14u };
    CHECK_EQ(prog_port_write(&pg, PIN(1u), 1u, pin_fn, &o, 0u, false, 0u),
             0u);
    CHECK_EQ(put(FN(1u), PWM_FN), 0u);
    CHECK_EQ(reg(PIN(1u)), 14u);
}

TEST_CASE(the_pulse_and_the_target_name_servo_pwm_ports_only)
{
    fresh();
    CHECK_EQ(put(PIN(0u), 13u), 0u);
    CHECK_EQ(put(PIN(1u), 14u), 0u);
    hold_all();
    CHECK_EQ(reg(LINK_PG_FLAGS),
             LINK_PG_F_BOUND(0u) | LINK_PG_F_PARKED(1u));
    CHECK_EQ(prog_port_pwm_ports(&pg), 1u);
    CHECK_EQ(prog_port_parked(&pg), 2u);
    CHECK_EQ(put(LINK_PG_PWM, 2u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(put(LINK_PG_TARGET, 2u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(put(LINK_PG_PWM, 1u), 0u);
    CHECK_EQ(put(LINK_PG_TARGET, 1u), 0u);
    /* Two ports on one slice: only SERVO PWM ports count as sliced. */
    CHECK_EQ(put(LINK_PG_TARGET, 0u), 0u);
    CHECK_EQ(put(LINK_PG_PWM, 0u), 0u);
    CHECK_EQ(put(PIN(1u), 12u), LINK_NACK_BAD_VALUE);   /* reserved */
    CHECK_EQ(put(PIN(2u), 15u), 0u);   /* GP14's slice, port 1 NONE */
    hold_all();
    CHECK_EQ(prog_port_sliced_channels(&pg), 0u);
    CHECK_EQ(put(FN(1u), PWM_FN), 0u);
    CHECK_EQ(put(FN(2u), PWM_FN), 0u);
    hold_all();
    CHECK_EQ(prog_port_sliced_channels(&pg),
             (uint16_t)((1u << prog_port_channel(1u))
                        | (1u << prog_port_channel(2u))));
    /* A port the IOmcu did not hold is refused, whatever its function. */
    prog_port_bound(&pg, 1u);
    CHECK_EQ(reg(LINK_PG_FLAGS),
             LINK_PG_F_BOUND(0u) | LINK_PG_F_REFUSED(1u)
                 | LINK_PG_F_REFUSED(2u));
    CHECK_EQ(prog_port_pwm_ports(NULL), 0u);
    CHECK_EQ(prog_port_parked(NULL), 0u);
}

TEST_CASE(the_kept_word_restores_each_ports_function)
{
    uint16_t kept[PROG_PORT_KEPT_COUNT];

    fresh();
    prog_port_defaults(kept, 13u);
    kept[1u * PROG_PORT_KEPT] = 48u;   /* port 1: no PWM slice */
    kept[2u * PROG_PORT_KEPT] = 14u;
    /* Port 1 NONE, port 2 SERVO PWM, port 3 a nibble no write takes. */
    prog_port_restore_fn(&pg, kept, 0xF121u, &o, 0u);
    CHECK_EQ(prog_port_func(&pg, 0u), PWM_FN);
    CHECK_EQ(prog_port_func(&pg, 1u), NONE_FN);
    CHECK_EQ(prog_port_func(&pg, 2u), PWM_FN);
    CHECK_EQ(prog_port_func(&pg, 3u), NONE_FN);
    CHECK_EQ(prog_port_pin(&pg, 1u), 48u);
    CHECK_EQ(prog_port_pin(&pg, 2u), 14u);
    CHECK_EQ(prog_port_kept_func(&pg), 0x2121u);
    /* The same pin under SERVO PWM is no pin of the generator: dropped. */
    prog_port_restore_fn(&pg, kept, 0x2211u, &o, 0u);
    CHECK_EQ(prog_port_pin(&pg, 1u), PROG_PORT_NO_PIN);
    CHECK_EQ(reg(PIN(1u)), LINK_PG_PIN_NONE);
    /* Nothing stored: the defaults. */
    prog_port_restore_fn(&pg, kept, PROG_PORT_FUNC_UNSTORED, &o, 0u);
    CHECK_EQ(prog_port_kept_func(&pg), 0x2221u);
    CHECK_EQ(prog_port_pin(&pg, 1u), 48u);
    /* A taken pin is refused under either function. */
    prog_port_restore_fn(&pg, kept, 0x2221u, &o, BIT(48));
    CHECK_EQ(prog_port_pin(&pg, 1u), PROG_PORT_NO_PIN);
    CHECK_EQ(reg(PIN(1u)), 48u);
    CHECK_EQ(reg(LINK_PG_FLAGS) & LINK_PG_F_REFUSED(1u),
             LINK_PG_F_REFUSED(1u));
    prog_port_restore_fn(NULL, kept, 0u, &o, 0u);
    prog_port_restore_fn(&pg, NULL, 0u, &o, 0u);
    prog_port_restore_fn(&pg, kept, 0u, NULL, 0u);
}

/*
 * A board taken to an image older than 4.26 and back.  An image of 4.25
 * reads the version 8 record's word as spare and writes its records with
 * 0 there: the ports keep their pins and come back at their default
 * functions.  An image older than 4.25 reads no version 8 record and
 * starts empty, the same as a restore with nothing saved.  A version 7
 * record carries the word at the same place.
 */
TEST_CASE(a_record_an_older_image_wrote_back_restores_the_defaults)
{
    fresh();
    CHECK_EQ(put(FN(0u), NONE_FN), 0u);
    CHECK_EQ(put(PIN(0u), 48u), 0u);
    CHECK_EQ(put(FN(1u), PWM_FN), 0u);
    CHECK_EQ(put(PIN(1u), 14u), 0u);
    out_store_t rec;
    memset(&rec, 0, sizeof(rec));
    prog_port_kept(&pg, rec.prog);
    rec.prog_func = prog_port_kept_func(&pg);
    CHECK_EQ(rec.prog_func, 0x2212u);

    /* Up: the new image reads its own record back whole. */
    out_store_t back;
    CHECK(out_store_cfg_read(OUT_STORE_VERSION, &rec, &back));
    prog_port_restore_fn(&pg, back.prog, back.prog_func, &o, 0u);
    CHECK_EQ(prog_port_func(&pg, 0u), NONE_FN);
    CHECK_EQ(prog_port_pin(&pg, 0u), 48u);
    CHECK_EQ(prog_port_func(&pg, 1u), PWM_FN);

    /* Down: the older image saves what it read, its spare word 0. */
    rec.prog_func = 0u;
    /* Up again: defaults.  Port 0's GP48 is no generator pin, so port 0
     * comes back without one; port 1 keeps GP14 as NONE. */
    CHECK(out_store_cfg_read(OUT_STORE_VERSION, &rec, &back));
    CHECK_EQ(back.prog_func, 0u);
    prog_port_restore_fn(&pg, back.prog, back.prog_func, &o, 0u);
    CHECK_EQ(prog_port_func(&pg, 0u), PWM_FN);
    CHECK_EQ(prog_port_pin(&pg, 0u), PROG_PORT_NO_PIN);
    CHECK_EQ(prog_port_func(&pg, 1u), NONE_FN);
    CHECK_EQ(prog_port_pin(&pg, 1u), 14u);

    /* A version 7 record carries the word where version 8 does. */
    rec.prog_func = 0x2212u;
    CHECK(out_store_cfg_read(OUT_STORE_VERSION_V7, &rec, &back));
    CHECK_EQ(back.prog_func, 0x2212u);
    CHECK_EQ(memcmp(back.prog, rec.prog, sizeof(rec.prog)), 0);
    /* A version 6 image wrote the word as its spare, 0. */
    rec.prog_func = 0u;
    CHECK(out_store_cfg_read(6u, &rec, &back));
    CHECK_EQ(back.prog_func, 0u);
    /* Nothing saved, as after an image older than 4.25: the start-up's
     * defaults, port 0 SERVO PWM on GP13 and the others NONE without a
     * pin. */
    uint16_t none[PROG_PORT_KEPT_COUNT];
    prog_port_defaults(none, 13u);
    prog_port_restore_fn(&pg, none, 0u, &o, 0u);
    CHECK_EQ(prog_port_func(&pg, 0u), PWM_FN);
    CHECK_EQ(prog_port_pin(&pg, 0u), 13u);
    CHECK_EQ(prog_port_func(&pg, 1u), NONE_FN);
    CHECK_EQ(prog_port_pin(&pg, 1u), PROG_PORT_NO_PIN);
}

TEST_CASE(a_revert_takes_the_function_back)
{
    fresh();
    CHECK_EQ(put(PIN(1u), 14u), 0u);
    hold_all();
    const prog_port_t was = pg;
    CHECK_EQ(put(FN(1u), PWM_FN), 0u);
    CHECK_EQ(prog_port_func(&pg, 1u), PWM_FN);
    prog_port_revert(&pg, &was, &o);
    CHECK_EQ(prog_port_func(&pg, 1u), NONE_FN);
    CHECK_EQ(reg(LINK_PG_FLAGS), LINK_PG_F_PARKED(1u));
}

TEST_CASE(a_dshot_port_is_a_throttle_and_takes_no_command_of_its_own)
{
    fresh();
    CHECK_EQ(put(PIN(1u), 14u), 0u);
    CHECK_EQ(put(LINK_PG_REG(1u, LINK_PG_SLEW), 200u), 0u);
    const uint8_t ch = prog_port_channel(1u);
    CHECK_EQ(o.channel[ch].role, OUT_ROLE_SURFACE);
    CHECK_EQ(put(FN(1u), D300_FN), 0u);
    /* A throttle at its rest, the port's slew not applied, the slew kept
     * on the page for SERVO PWM. */
    CHECK_EQ(o.channel[ch].role, OUT_ROLE_THROTTLE);
    CHECK_EQ(o.channel[ch].rest, outputs_role_rest(OUT_ROLE_THROTTLE));
    CHECK_EQ(o.channel[ch].actual, o.channel[ch].rest);
    CHECK_EQ(o.channel[ch].command, o.channel[ch].rest);
    CHECK_EQ(o.channel[ch].slew_per_s, 0u);
    CHECK_EQ(reg(LINK_PG_REG(1u, LINK_PG_SLEW)), 200u);
    /* The throttle is the CONTROL page's: a CMD on this page is refused,
     * alone or in a frame with the FUNC that makes the port DShot. */
    CHECK_EQ(put(LINK_PG_REG(1u, LINK_PG_CMD), 500u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(o.channel[ch].command, o.channel[ch].rest);
    CHECK_EQ(put(FN(2u), NONE_FN), 0u);
    uint16_t page[LINK_PG_REGS];
    prog_port_read(&pg, 0u, LINK_PG_REGS, page);
    page[FN(2u)] = D150_FN;
    CHECK_EQ(prog_port_write(&pg, LINK_PG_REG(2u, LINK_PG_CMD),
                             (uint8_t)(LINK_PG_REGS
                                       - LINK_PG_REG(2u, LINK_PG_CMD)),
                             &page[LINK_PG_REG(2u, LINK_PG_CMD)], &o, 0u,
                             false, 0u),
             LINK_NACK_BAD_VALUE);
    CHECK_EQ(reg(FN(2u)), NONE_FN);
    /* Its endpoints and slew still take a write; they are the page's. */
    CHECK_EQ(put(LINK_PG_REG(1u, LINK_PG_SLEW), 300u), 0u);
    CHECK_EQ(o.channel[ch].slew_per_s, 0u);
    /* Back to SERVO PWM: a surface at its rest, the slew applied again. */
    CHECK_EQ(put(FN(1u), PWM_FN), 0u);
    CHECK_EQ(o.channel[ch].role, OUT_ROLE_SURFACE);
    CHECK_EQ(o.channel[ch].actual, outputs_role_rest(OUT_ROLE_SURFACE));
    CHECK_EQ(o.channel[ch].slew_per_s, 300u);
    CHECK_EQ(put(LINK_PG_REG(1u, LINK_PG_CMD), 500u), 0u);
}

TEST_CASE(a_dshot_port_is_under_the_pin_rules_and_no_pwm_rule)
{
    fresh();
    /* GP48 has no PWM slice and GP5 is the slice of a PWM slot: DShot
     * takes both. */
    CHECK_EQ(put(FN(1u), D600_FN), 0u);
    CHECK_EQ(put(PIN(1u), 48u), 0u);
    CHECK_EQ(put(PIN(1u), 5u), 0u);
    /* Beside a SERVO PWM port on its compare register: no clash. */
    CHECK_EQ(put(PIN(0u), 13u), 0u);
    CHECK_EQ(put(PIN(1u), 29u), 0u);
    /* The pin rules: no other port's pin, no slot's, no reserved, no
     * taken, no pin past the header. */
    CHECK_EQ(put(PIN(1u), 13u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(put(PIN(1u), 4u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(put(PIN(1u), 3u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(put_ex(PIN(1u), 20u, BIT(20), false), LINK_NACK_BAD_VALUE);
    CHECK_EQ(put(PIN(1u), 64u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(reg(PIN(1u)), 29u);
    /* A DShot pin is no PWM rule for a slot either. */
    hold_all();
    CHECK_EQ(put(PIN(1u), 7u), 0u);   /* GP6's slice partner */
    hold_all();
    uint16_t slots[LINK_OS_COUNT];
    memset(slots, 0, sizeof(slots));
    slots[LINK_OS_DRIVER] = (uint16_t)OUT_DRIVER_PWM;
    slots[LINK_OS_PIN]    = 6u;
    CHECK_EQ(prog_port_slots_check(&pg, slots), 0u);
    slots[LINK_OS_PIN] = 7u;
    CHECK_EQ(prog_port_slots_check(&pg, slots), LINK_NACK_BAD_VALUE);
}

TEST_CASE(a_dshot_port_is_no_pulse_no_target_and_waits_for_rest)
{
    fresh();
    CHECK_EQ(put(PIN(0u), 13u), 0u);
    CHECK_EQ(put(PIN(1u), 14u), 0u);
    CHECK_EQ(put(FN(1u), D300_FN), 0u);
    CHECK_EQ(put(PIN(2u), 15u), 0u);
    CHECK_EQ(put(FN(2u), D150_FN), 0u);
    hold_all();
    /* FLAGS: port 0 bound, ports 1 and 2 DShot. */
    CHECK_EQ(reg(LINK_PG_FLAGS), LINK_PG_F_BOUND(0u) | LINK_PG_F_DSHOT(1u)
                                     | LINK_PG_F_DSHOT(2u));
    CHECK_EQ(prog_port_pwm_ports(&pg), 1u);
    CHECK_EQ(prog_port_parked(&pg), 0u);
    CHECK_EQ(prog_port_dshot(&pg), 6u);
    CHECK_EQ(prog_port_kbit(&pg, 1u), 300u);
    CHECK_EQ(prog_port_kbit(&pg, 2u), 150u);
    CHECK_EQ(prog_port_kbit(&pg, 0u), 0u);
    CHECK_EQ(prog_port_kbit(&pg, LINK_PG_PORTS_MAX), 0u);
    CHECK_EQ(prog_port_kbit(NULL, 1u), 0u);
    CHECK_EQ(prog_port_dshot(NULL), 0u);
    /* No pulse and no TARGET on a DShot port. */
    CHECK_EQ(put(LINK_PG_PWM, 2u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(put(LINK_PG_TARGET, 2u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(put(LINK_PG_TARGET, 3u), LINK_NACK_BAD_VALUE);
    /* A FUNC change waits for a bench at rest, to DShot or from it. */
    CHECK_EQ(put_ex(FN(1u), D600_FN, 0u, true), LINK_NACK_BAD_VALUE);
    CHECK_EQ(put_ex(FN(0u), D300_FN, 0u, true), LINK_NACK_BAD_VALUE);
    outputs_arm_roles(&o, OUT_ROLE_BIT(OUT_ROLE_THROTTLE), 0u);
    CHECK_EQ(put(FN(1u), NONE_FN), LINK_NACK_BAD_VALUE);
    CHECK_EQ(put(FN(1u), D600_FN), LINK_NACK_BAD_VALUE);
    CHECK_EQ(reg(FN(1u)), D300_FN);
    outputs_arm(&o, false, 0u);
    /* Not while port 0 is the TARGET, to make it DShot. */
    CHECK_EQ(put(LINK_PG_TARGET, 1u), 0u);
    CHECK_EQ(put(FN(0u), D300_FN), LINK_NACK_BAD_VALUE);
    CHECK_EQ(put(LINK_PG_TARGET, 0u), 0u);
    /* DSHOT300 to DSHOT600 is a change: held anew at the new rate. */
    CHECK_EQ(put(FN(1u), D600_FN), 0u);
    CHECK_EQ(reg(LINK_PG_FLAGS) & LINK_PG_F_REFUSED(1u),
             LINK_PG_F_REFUSED(1u));
    hold_all();
    CHECK_EQ(prog_port_kbit(&pg, 1u), 600u);
    /* A DShot port the IOmcu did not hold (no state machine left) reads
     * refused. */
    prog_port_bound(&pg, 1u);
    CHECK_EQ(reg(LINK_PG_FLAGS), LINK_PG_F_BOUND(0u) | LINK_PG_F_REFUSED(1u)
                                     | LINK_PG_F_REFUSED(2u));
    CHECK_EQ(prog_port_dshot(&pg), 6u);   /* still asked for */
}

/* A session's wire page (KST, HITEC) gets no pin on a DShot port: an ESC
 * is on that lead.  SERVO PWM and NONE ports hand theirs over while the
 * IOmcu holds them. */
TEST_CASE(a_wire_session_gets_no_pin_on_a_dshot_port)
{
    fresh();
    CHECK_EQ(put(PIN(0u), 13u), 0u);
    CHECK_EQ(put(PIN(1u), 14u), 0u);
    CHECK_EQ(put(PIN(2u), 15u), 0u);
    CHECK_EQ(put(FN(2u), D300_FN), 0u);
    hold_all();
    CHECK_EQ(prog_port_wire_pin(&pg, 0u, 0x7u), 13u);   /* SERVO PWM */
    CHECK_EQ(prog_port_wire_pin(&pg, 1u, 0x7u), 14u);   /* NONE */
    CHECK_EQ(prog_port_wire_pin(&pg, 2u, 0x7u), PROG_PORT_NO_PIN);
    /* The ESC's page has the opposite rule. */
    CHECK_EQ(prog_port_esc_pin(&pg, 2u, 0x7u), 15u);
    CHECK_EQ(prog_port_esc_pin(&pg, 0u, 0x7u), PROG_PORT_NO_PIN);
    CHECK_EQ(prog_port_esc_pin(&pg, 1u, 0x7u), PROG_PORT_NO_PIN);
    CHECK_EQ(prog_port_esc_pin(&pg, 2u, 0x3u), PROG_PORT_NO_PIN);
    CHECK_EQ(prog_port_esc_pin(&pg, LINK_PG_PORTS_MAX, 0xFFu), PROG_PORT_NO_PIN);
    CHECK_EQ(prog_port_esc_pin(NULL, 2u, 0x7u), PROG_PORT_NO_PIN);
    /* Not held, past the ports, no page: none. */
    CHECK_EQ(prog_port_wire_pin(&pg, 0u, 0x6u), PROG_PORT_NO_PIN);
    CHECK_EQ(prog_port_wire_pin(&pg, 3u, 0xFu), PROG_PORT_NO_PIN);
    CHECK_EQ(prog_port_wire_pin(&pg, LINK_PG_PORTS_MAX, 0xFFu),
             PROG_PORT_NO_PIN);
    CHECK_EQ(prog_port_wire_pin(NULL, 0u, 0x7u), PROG_PORT_NO_PIN);
    /* Back to NONE: handed over again. */
    CHECK_EQ(put(FN(2u), NONE_FN), 0u);
    hold_all();
    CHECK_EQ(prog_port_wire_pin(&pg, 2u, 0x7u), 15u);
}

/*
 * The kept word holds 1 + FUNC: DShot is 3 to 5.  A 4.26 or 4.27 image
 * takes 1 and 2 only and reads 3 to 5 as the port's default, as it reads
 * any nibble no write of its own would take.
 */
TEST_CASE(the_kept_word_restores_dshot_and_an_older_image_its_default)
{
    uint16_t kept[PROG_PORT_KEPT_COUNT];

    fresh();
    prog_port_defaults(kept, 13u);
    kept[1u * PROG_PORT_KEPT] = 48u;   /* no PWM slice: fine for DShot */
    kept[2u * PROG_PORT_KEPT] = 14u;
    kept[3u * PROG_PORT_KEPT] = 15u;
    prog_port_restore_fn(&pg, kept, 0x5432u, &o, 0u);
    CHECK_EQ(prog_port_func(&pg, 0u), NONE_FN);
    CHECK_EQ(prog_port_func(&pg, 1u), D150_FN);
    CHECK_EQ(prog_port_func(&pg, 2u), D300_FN);
    CHECK_EQ(prog_port_func(&pg, 3u), D600_FN);
    CHECK_EQ(prog_port_pin(&pg, 1u), 48u);
    CHECK_EQ(prog_port_kept_func(&pg), 0x5432u);
    CHECK_EQ(o.channel[prog_port_channel(3u)].role, OUT_ROLE_THROTTLE);
    CHECK_EQ(o.channel[prog_port_channel(0u)].role, OUT_ROLE_SURFACE);
    hold_all();
    CHECK_EQ(prog_port_dshot(&pg), 0xEu);
    /* 6 and up: the default. */
    prog_port_restore_fn(&pg, kept, 0x6621u, &o, 0u);
    CHECK_EQ(prog_port_func(&pg, 2u), NONE_FN);
    CHECK_EQ(prog_port_func(&pg, 3u), NONE_FN);
    CHECK_EQ(o.channel[prog_port_channel(3u)].role, OUT_ROLE_SURFACE);
    /* What a 4.26 image makes of the word 0x5432: the rule it applies,
     * a nibble less 1 at or past its 2 functions is the default. */
    for (unsigned port = 0; port < LINK_PG_PORTS_MAX; ++port) {
        const unsigned v = (0x5432u >> (4u * port)) & 0xFu;
        const bool old_takes = v != 0u && v - 1u < LINK_PG_FN_COUNT_V426;
        CHECK_EQ(old_takes, port == 0u);
    }
}

int main(void)
{
    RUN(a_fresh_page_has_port_0_servo_pwm_and_the_others_none);
    RUN(a_func_write_takes_the_five_functions_and_nothing_else);
    RUN(a_none_port_is_under_the_pin_rules_and_no_pwm_rule);
    RUN(a_func_change_waits_for_a_bench_at_rest);
    RUN(servo_pwm_is_refused_on_a_pin_the_generator_cannot_take);
    RUN(the_pulse_and_the_target_name_servo_pwm_ports_only);
    RUN(the_kept_word_restores_each_ports_function);
    RUN(a_record_an_older_image_wrote_back_restores_the_defaults);
    RUN(a_revert_takes_the_function_back);
    RUN(a_dshot_port_is_a_throttle_and_takes_no_command_of_its_own);
    RUN(a_dshot_port_is_under_the_pin_rules_and_no_pwm_rule);
    RUN(a_dshot_port_is_no_pulse_no_target_and_waits_for_rest);
    RUN(the_kept_word_restores_dshot_and_an_older_image_its_default);
    RUN(a_wire_session_gets_no_pin_on_a_dshot_port);
    return test_summary("prog_port_fn");
}
