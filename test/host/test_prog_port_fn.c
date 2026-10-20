/*
 * The programming ports' functions (shared/outputs/prog_port.c, protocol
 * 4.26): FUNC per port, SERVO PWM or NONE.
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

TEST_CASE(a_func_write_takes_the_two_functions_and_nothing_else)
{
    fresh();
    CHECK_EQ(put(FN(1u), PWM_FN), 0u);
    CHECK_EQ(reg(FN(1u)), PWM_FN);
    CHECK_EQ(put(FN(1u), NONE_FN), 0u);
    CHECK_EQ(reg(FN(1u)), NONE_FN);
    /* DSHOT150, 300 and 600 are not this minor's, nor anything past. */
    static const uint16_t bad[] = { 2u, 3u, 4u, 15u, 0x8000u, 0xFFFFu };
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
 * A board taken to an image older than 4.26 and back: the older image
 * reads the record's word as spare and writes its records with 0 there.
 * The ports keep their pins and come back at their default functions.
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

    /* A version 6 record has the word 0 as well. */
    CHECK(out_store_cfg_read(6u, &rec, &back));
    CHECK_EQ(back.prog_func, 0u);
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

int main(void)
{
    RUN(a_fresh_page_has_port_0_servo_pwm_and_the_others_none);
    RUN(a_func_write_takes_the_two_functions_and_nothing_else);
    RUN(a_none_port_is_under_the_pin_rules_and_no_pwm_rule);
    RUN(a_func_change_waits_for_a_bench_at_rest);
    RUN(servo_pwm_is_refused_on_a_pin_the_generator_cannot_take);
    RUN(the_pulse_and_the_target_name_servo_pwm_ports_only);
    RUN(the_kept_word_restores_each_ports_function);
    RUN(a_record_an_older_image_wrote_back_restores_the_defaults);
    RUN(a_revert_takes_the_function_back);
    return test_summary("prog_port_fn");
}
