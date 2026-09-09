/*
 * The output intermediary: one set of rules for every output.
 *
 * A role decides where rest is and which direction is safe; a driver table
 * refuses two drivers on one pin and two slots rendering one channel; arming,
 * clamping, slew and the silence timeout are answered here for every output.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "outputs.h"
#include "outputs_pages.h"
#include "out_pwm_map.h"
#include "link_pages.h"

static outputs_t o;

static void fresh(void)
{
    outputs_init(&o, 1000u);
}

static out_slot_t pwm_slot(uint8_t first, uint8_t pin)
{
    return (out_slot_t){ .driver = OUT_DRIVER_PWM, .first_channel = first,
                         .channels = 1, .pin = pin, .rate_hz = 50 };
}

static bool put_pwm(uint8_t slot, uint8_t first, uint8_t pin)
{
    const out_slot_t s = pwm_slot(first, pin);
    return outputs_configure(&o, slot, &s);
}

/*
 * Disarming the bank switches every output off in one call, so a failsafe
 * path cannot clear one output and leave another driving.
 */
TEST_CASE(everything_goes_to_rest_together)
{
    fresh();
    outputs_set_role(&o, 0, OUT_ROLE_THROTTLE);
    outputs_set_role(&o, 1, OUT_ROLE_SURFACE);
    outputs_arm(&o, true, 1000u);
    outputs_set(&o, 0, OUT_SPAN, 1000u);
    outputs_set(&o, 1, OUT_SPAN, 1000u);
    outputs_step(&o, 1010u);
    CHECK_EQ(outputs_actual(&o, 0), OUT_SPAN);
    CHECK_EQ(outputs_actual(&o, 1), OUT_SPAN);

    outputs_all_off(&o);
    /* A throttle rests at stopped and a surface rests centred, and both
     * happened on one call. */
    CHECK_EQ(outputs_actual(&o, 0), 0u);
    CHECK_EQ(outputs_actual(&o, 1), OUT_SPAN / 2u);
}

/*
 * One silence timeout for every channel: a surface goes to its rest after
 * silence the same as a throttle does.  Both keep driving; rest is centred
 * for the surface and stopped for the throttle.
 */
TEST_CASE(silence_stops_every_role)
{
    fresh();
    outputs_set_role(&o, 0, OUT_ROLE_THROTTLE);
    outputs_arm(&o, true, 1000u);
    outputs_set(&o, 0, 800u, 1000u);
    outputs_set(&o, 1, 900u, 1000u);
    outputs_step(&o, 1100u);
    CHECK_EQ(outputs_actual(&o, 0), 800u);
    CHECK_EQ(outputs_actual(&o, 1), 900u);

    /* Inclusive, and pinned to the millisecond either side of it. */
    CHECK(!outputs_overdue(&o, 0, 1000u + OUT_DEFAULT_TIMEOUT_MS - 1u));
    CHECK(outputs_overdue(&o, 0, 1000u + OUT_DEFAULT_TIMEOUT_MS));
    outputs_step(&o, 1600u);
    CHECK_EQ(outputs_actual(&o, 0), 0u);
    CHECK_EQ(outputs_actual(&o, 1), OUT_SPAN / 2u);
}

/*
 * Staleness is a channel's own, not the bank's.  The pages that write them
 * are written independently: a servo being dragged must not keep a throttle
 * alive, and a throttle nobody has touched must not stop a servo.
 */
TEST_CASE(one_quiet_channel_does_not_stop_the_others)
{
    fresh();
    outputs_arm(&o, true, 1000u);
    outputs_set(&o, 0, 800u, 1000u);
    outputs_set(&o, 1, 900u, 1000u);

    /* Channel one keeps being commanded; channel zero goes quiet. */
    for (uint32_t t = 1100u; t <= 2000u; t += 100u) {
        outputs_set(&o, 1, 900u, t);
        outputs_step(&o, t);
    }
    CHECK_EQ(outputs_actual(&o, 0), OUT_SPAN / 2u);   /* rest */
    CHECK_EQ(outputs_actual(&o, 1), 900u);            /* still driving */
}

/*
 * Arming is idempotent: the coprocessor recomputes whether driving is allowed
 * every pass, and an arm call that stamped the clock would hold every channel
 * alive and defeat the timeout.
 */
TEST_CASE(re_arming_is_not_activity)
{
    fresh();
    outputs_arm(&o, true, 1000u);
    outputs_set(&o, 0, 800u, 1000u);
    for (uint32_t t = 1100u; t <= 2000u; t += 100u) {
        outputs_arm(&o, true, t);      /* the loop, every pass */
        outputs_step(&o, t);
    }
    CHECK(outputs_overdue(&o, 0, 2000u));
    CHECK_EQ(outputs_actual(&o, 0), OUT_SPAN / 2u);
}

/*
 * A throttle's slew is asymmetric and a surface's is not, and that is the
 * whole reason a channel carries a role.  Ramping a throttle down is a slew
 * limit on stopping; a surface has no safe direction to hurry towards.
 */
TEST_CASE(the_role_decides_which_direction_is_safe)
{
    fresh();
    outputs_set_role(&o, 0, OUT_ROLE_THROTTLE);
    outputs_set_slew(&o, 0, 500u);          /* half of span per second */
    outputs_set_role(&o, 1, OUT_ROLE_SURFACE);
    outputs_set_slew(&o, 1, 500u);
    outputs_arm(&o, true, 1000u);

    /* Commanded every hundred milliseconds for a second, the way a caller
     * does.  Stepping a whole second without commanding would be testing the
     * timeout instead, and it would pass by going to rest. */
    for (uint32_t t = 1100u; t <= 2000u; t += 100u) {
        outputs_set(&o, 0, 1000u, t);
        outputs_set(&o, 1, 1000u, t);
        outputs_step(&o, t);
    }
    CHECK_EQ(outputs_actual(&o, 0), 500u);  /* both ramp up alike */
    CHECK_EQ(outputs_actual(&o, 1), 1000u); /* from centre, so it arrived */

    /* Then down.  The throttle goes at once; the surface is ramped. */
    outputs_set(&o, 0, 0u, 2000u);
    outputs_set(&o, 1, 0u, 2000u);
    outputs_step(&o, 2100u);                /* a tenth of a second */
    CHECK_EQ(outputs_actual(&o, 0), 0u);
    CHECK_EQ(outputs_actual(&o, 1), 950u);
}

/* A slew slow enough that a step lands under one unit must still move, or the
 * output never arrives and nothing says why. */
TEST_CASE(a_slow_slew_still_moves)
{
    fresh();
    outputs_set_slew(&o, 0, 1u);            /* one unit per second */
    outputs_arm(&o, true, 1000u);
    outputs_set(&o, 0, 1000u, 1000u);
    const uint16_t was = outputs_actual(&o, 0);
    outputs_step(&o, 1001u);                /* a millisecond */
    CHECK(outputs_actual(&o, 0) > was);
}

/*
 * Two drivers on one pin, and two slots rendering the same channel: the table
 * refuses both.
 */
TEST_CASE(a_pin_belongs_to_one_driver)
{
    fresh();
    CHECK(put_pwm(0, 0, 7));
    /* Same pin, different channel. */
    CHECK(!put_pwm(1, 1, 7));
    /* Same channel, different pin. */
    CHECK(!put_pwm(1, 0, 8));
    /* Both free, so it takes. */
    CHECK(put_pwm(1, 1, 8));

    /* And a slot may always be reconfigured against itself. */
    CHECK(put_pwm(0, 0, 7));
}

/*
 * Which pins exist, and which already carry the CAN (Controller Area Network)
 * controller or the safety line, is a property of the board rather than of
 * the protocol -- and the pin arrives from the host, so it is whatever an
 * operator typed.  A driver bound to the heartbeat input is an output that
 * cannot be seen to be wrong until the interlock stops working.
 */
TEST_CASE(a_reserved_pin_is_refused)
{
    fresh();
    outputs_reserve_pins(&o, (1ull << 3) | (1ull << 8) | (1ull << 9));

    CHECK(!outputs_pin_available(&o, 3));
    CHECK(!outputs_pin_available(&o, 9));
    CHECK(outputs_pin_available(&o, 7));
    /* A pin past the width of the mask does not exist on any board here. */
    CHECK(!outputs_pin_available(&o, OUT_MAX_PIN + 1u));

    CHECK(!put_pwm(0, 0, 3));
    CHECK(!put_pwm(0, 0, 8));
    CHECK(put_pwm(0, 0, 7));

    /* An empty mask is the panel's case: it drives no pin of its own, and
     * nothing it configures may be refused for a reason it cannot know. */
    fresh();
    CHECK(put_pwm(0, 0, 3));
}

TEST_CASE(the_outputs_page_refuses_a_pin_that_does_not_fit_the_field)
{
    uint16_t regs[LINK_OS_COUNT];
    outputs_slots_defaults(regs);

    /* The register is sixteen bits and a pin is eight, so 259 would be
     * stored, truncated to 3 at apply, and drive the safety line. */
    const uint16_t pin = 259u;
    CHECK_EQ(outputs_slots_write(regs, LINK_OS_PIN, 1, &pin),
             LINK_NACK_BAD_VALUE);
    const uint16_t ok = 63u;
    CHECK_EQ(outputs_slots_write(regs, LINK_OS_PIN, 1, &ok), 0u);
}

/*
 * PPM (pulse-position modulation) is eight channels on one pin.  The table
 * says so, and the range it claims is the range the conflict check defends.
 */
TEST_CASE(ppm_claims_a_range_of_channels)
{
    fresh();
    const out_slot_t ppm = { .driver = OUT_DRIVER_PPM, .first_channel = 0,
                             .channels = 8, .pin = 5, .rate_hz = 22 };
    CHECK(outputs_configure(&o, 0, &ppm));
    /* Channel five is inside PPM's eight, on a different pin. */
    CHECK(!put_pwm(1, 5, 9));
    /* Channel eight is past them. */
    CHECK(put_pwm(1, 8, 9));

    /* More channels than PPM carries is refused rather than truncated. */
    out_slot_t too_many = ppm;
    too_many.channels = 9;
    CHECK(!outputs_configure(&o, 2, &too_many));
}

/* Bidirectional DShot listens on the pin it drives, so "how many pins" and
 * "is it only an output" are separate questions and the table answers both. */
TEST_CASE(the_table_says_what_a_driver_is_shaped_like)
{
    /* Only bidirectional DShot listens on the pin it drives. */
    CHECK(out_driver(OUT_DRIVER_DSHOT_BIDIR)->reads_back);
    CHECK(!out_driver(OUT_DRIVER_DSHOT)->reads_back);
    CHECK(!out_driver(OUT_DRIVER_PWM)->reads_back);
    /* Only pulse drivers go through the endpoints. */
    CHECK(out_driver(OUT_DRIVER_PWM)->pulsed);
    CHECK(!out_driver(OUT_DRIVER_DSHOT)->pulsed);
    CHECK(!out_driver(OUT_DRIVER_DSHOT_BIDIR)->pulsed);
    CHECK_EQ(out_driver(OUT_DRIVER_PPM)->channels_max, 8);
    CHECK(out_driver(OUT_DRIVER_COUNT) == NULL);
}

/* A rate the driver cannot produce is refused: a servo asked for 2 kHz is a
 * configuration mistake, not a fast servo. */
TEST_CASE(a_rate_outside_the_driver_is_refused)
{
    fresh();
    out_slot_t fast = pwm_slot(0, 7);
    fast.rate_hz = 2000;
    CHECK(!outputs_configure(&o, 0, &fast));
    fast.rate_hz = 10;
    CHECK(!outputs_configure(&o, 0, &fast));
}

/*
 * Endpoints are refused and commands are clamped.
 */
TEST_CASE(endpoints_are_refused_and_commands_are_clamped)
{
    fresh();
    CHECK(!outputs_set_endpoints(&o, 0, 100u, 2000u));
    CHECK(!outputs_set_endpoints(&o, 0, 1000u, 9000u));
    /* Refused, so the defaults still stand. */
    CHECK_EQ(outputs_pulse_us(&o, 0), 1500u);

    outputs_arm(&o, true, 1000u);
    CHECK(outputs_set(&o, 0, 5000u, 1000u));   /* clamped, not refused */
    outputs_step(&o, 1010u);
    CHECK_EQ(outputs_actual(&o, 0), OUT_SPAN);
    CHECK_EQ(outputs_pulse_us(&o, 0), 2000u);
}

/* An inverted pair is a mistake rather than a range, and it would make the
 * clamp unsatisfiable. */
TEST_CASE(an_inverted_range_is_straightened)
{
    fresh();
    CHECK(outputs_set_endpoints(&o, 0, 1900u, 1100u));
    CHECK_EQ(o.channel[0].min_us, 1100u);
    CHECK_EQ(o.channel[0].max_us, 1900u);
}

/* Endpoints scale the whole span, so the ends land exactly on them. */
TEST_CASE(the_pulse_spans_the_endpoints)
{
    fresh();
    CHECK(outputs_set_endpoints(&o, 0, 1100u, 1900u));
    outputs_arm(&o, true, 1000u);
    outputs_set(&o, 0, 0u, 1000u);
    outputs_step(&o, 1010u);
    CHECK_EQ(outputs_pulse_us(&o, 0), 1100u);
    outputs_set(&o, 0, OUT_SPAN, 1010u);
    outputs_step(&o, 1020u);
    CHECK_EQ(outputs_pulse_us(&o, 0), 1900u);
    outputs_set(&o, 0, OUT_SPAN / 2u, 1020u);
    outputs_step(&o, 1030u);
    CHECK_EQ(outputs_pulse_us(&o, 0), 1500u);
}

/* Commanding while disarmed is remembered and not emitted, so arming does not
 * pick up a throttle somebody set a minute ago. */
TEST_CASE(a_command_while_disarmed_is_remembered_and_not_emitted)
{
    fresh();
    outputs_set_role(&o, 0, OUT_ROLE_THROTTLE);
    outputs_set(&o, 0, 700u, 1000u);
    outputs_step(&o, 1010u);
    CHECK_EQ(outputs_actual(&o, 0), 0u);
    CHECK_EQ(o.channel[0].command, 700u);
    CHECK(!outputs_driving(&o));
}

/*
 * outputs_driving() is the bank's armed flag and asks nothing else.  It does
 * not ask whether a channel is still being commanded -- that is per channel,
 * and it decides what a channel renders rather than whether it renders -- and
 * it cannot ask about the safety line, which the end holding the wire settles
 * before it arms.  A bank armed with every channel past the timeout still
 * drives, and what reaches the pin is the channel's rest: 1500 us for a
 * surface across the default 1000 to 2000 us endpoints.
 */
TEST_CASE(driving_is_armed_and_asks_nothing_about_commands)
{
    fresh();
    CHECK(!outputs_driving(&o));
    CHECK_EQ(outputs_driving(&o), outputs_armed(&o));

    outputs_arm(&o, true, 1000u);
    outputs_set(&o, 0, 900u, 1000u);
    outputs_step(&o, 1010u);
    CHECK(outputs_driving(&o));
    CHECK_EQ(outputs_actual(&o, 0), 900u);

    /* Every channel past the timeout, and the bank drives on. */
    const uint32_t late = 1000u + OUT_DEFAULT_TIMEOUT_MS;
    outputs_step(&o, late);
    for (uint8_t ch = 0; ch < (uint8_t)OUT_MAX_CHANNELS; ++ch) {
        CHECK(outputs_overdue(&o, ch, late));
    }
    CHECK(outputs_driving(&o));
    CHECK_EQ(outputs_driving(&o), outputs_armed(&o));
    CHECK_EQ(outputs_actual(&o, 0), OUT_SPAN / 2u);
    CHECK_EQ(outputs_pulse_us(&o, 0), 1500u);

    /* A disarm is what stops it. */
    outputs_arm(&o, false, late);
    CHECK(!outputs_driving(&o));
}

/*
 * A re-arm renders the slew's answer, not the command.
 *
 * outputs_arm() refreshes the clock and nothing else; the disarm before it
 * put actual at rest.  With no slew the first step is the whole distance and
 * the pin carries the remembered command for the whole timeout.  With a slew
 * the pin carries a ramp from rest, and a rate too slow to cross the distance
 * inside timeout_ms never gets there: the timeout returns the channel to rest
 * with the command still standing.
 */
TEST_CASE(a_re_arm_ramps_from_rest_when_the_channel_is_slewed)
{
    fresh();
    /* Channel 0 unslewed, channel 1 at 200 units a second: 100 units in the
     * 500 ms timeout, against 400 units from rest to the command. */
    CHECK(outputs_set_slew(&o, 1, 200u));

    outputs_arm(&o, true, 1000u);
    outputs_set(&o, 0, 900u, 1000u);
    outputs_set(&o, 1, 900u, 1000u);
    outputs_step(&o, 1001u);
    CHECK_EQ(outputs_actual(&o, 0), 900u);
    CHECK_EQ(outputs_actual(&o, 1), OUT_SPAN / 2u + 1u);

    /* Disarmed, and still stepped: the loop calls outputs_step() every pass
     * whether the bank is armed or not, so the slew's elapsed time does not
     * count the disarmed gap in one lump. */
    outputs_arm(&o, false, 1001u);
    outputs_step(&o, 2000u);
    CHECK_EQ(outputs_actual(&o, 0), OUT_SPAN / 2u);
    CHECK_EQ(outputs_actual(&o, 1), OUT_SPAN / 2u);

    /* Re-armed with both commands still standing.  The arm refreshes the
     * clock and nothing else. */
    outputs_arm(&o, true, 2000u);
    outputs_step(&o, 2001u);
    CHECK_EQ(outputs_actual(&o, 0), 900u);   /* the whole distance at once */
    /* And one unit of it: the step is rounded up, so even a 1 ms pass moves.
     * 200 units a second is 0.2 of a unit in 1 ms. */
    CHECK_EQ(outputs_actual(&o, 1), OUT_SPAN / 2u + 1u);

    /* One millisecond before the timeout.  498 ms more at 200 units a second
     * is 100 units, so the ramp stands at 601 of the 900 commanded. */
    outputs_step(&o, 2000u + OUT_DEFAULT_TIMEOUT_MS - 1u);
    CHECK_EQ(outputs_actual(&o, 0), 900u);
    CHECK_EQ(outputs_actual(&o, 1), 601u);

    /* At the timeout both go to rest, the slewed one without ever having
     * rendered what it was commanded.  The bank is still driving. */
    outputs_step(&o, 2000u + OUT_DEFAULT_TIMEOUT_MS);
    CHECK(outputs_overdue(&o, 1, 2000u + OUT_DEFAULT_TIMEOUT_MS));
    CHECK_EQ(outputs_actual(&o, 0), OUT_SPAN / 2u);
    CHECK_EQ(outputs_actual(&o, 1), OUT_SPAN / 2u);
    CHECK(outputs_driving(&o));
}

/*
 * Rest is the midpoint of the channel's own endpoints, not 1500 us.  The
 * default 1000 to 2000 us rests at 1500 us; the narrow servo the servo screen
 * offers, 660 to 860 us, rests at 760 us.
 */
TEST_CASE(a_surface_rests_at_the_midpoint_of_its_own_endpoints)
{
    fresh();
    CHECK(outputs_set_endpoints(&o, 1, 660u, 860u));
    outputs_arm(&o, true, 1000u);
    outputs_step(&o, 1000u + OUT_DEFAULT_TIMEOUT_MS);
    CHECK_EQ(outputs_actual(&o, 0), OUT_SPAN / 2u);
    CHECK_EQ(outputs_pulse_us(&o, 0), 1500u);
    CHECK_EQ(outputs_actual(&o, 1), OUT_SPAN / 2u);
    CHECK_EQ(outputs_pulse_us(&o, 1), 760u);
}

/*
 * The timeout reaches an uncommanded channel every pass and resolves to the
 * rest it already sits at, so the bank keeps driving.  A wrong role is not
 * beyond every command either: the throttle addresses channels by role and
 * passes a surface by, and the CHANNELS page addresses them by index and
 * reaches it.
 */
TEST_CASE(the_timeout_reaches_a_channel_and_leaves_it_driving)
{
    fresh();
    outputs_arm(&o, true, 1000u);
    const uint32_t late = 1000u + OUT_DEFAULT_TIMEOUT_MS;
    outputs_step(&o, late);
    CHECK(outputs_overdue(&o, 0, late));
    CHECK(outputs_driving(&o));
    CHECK_EQ(outputs_actual(&o, 0), OUT_SPAN / 2u);

    /* By role: channel 0 is a surface, so a throttle command passes it by. */
    CHECK_EQ(outputs_set_role_channels(&o, OUT_ROLE_THROTTLE,
                                       (uint8_t)OUT_MAX_CHANNELS, OUT_SPAN,
                                       late),
             0u);
    outputs_step(&o, late + 1u);
    CHECK_EQ(outputs_actual(&o, 0), OUT_SPAN / 2u);

    /* By index: the CHANNELS page reaches it whatever its role. */
    uint16_t regs[LINK_CH_COUNT];
    outputs_channels_defaults(regs);
    regs[0] = 0u;
    outputs_channels_apply_n(&o, regs, 0u, 1u, late + 1u);
    outputs_step(&o, late + 2u);
    CHECK(!outputs_overdue(&o, 0, late + 2u));
    CHECK_EQ(outputs_actual(&o, 0), 0u);
    CHECK_EQ(outputs_pulse_us(&o, 0), 1000u);
}

/*
 * For the first OUT_DEFAULT_TIMEOUT_MS after an arm, a channel is at its last
 * command rather than at its rest: outputs_arm() stamps every channel's
 * clock, so a command given while disarmed is not overdue.
 */
TEST_CASE(an_arm_makes_a_disarmed_command_fresh_for_the_timeout)
{
    fresh();
    outputs_set(&o, 0, 900u, 1000u);
    outputs_step(&o, 1010u);
    CHECK_EQ(outputs_actual(&o, 0), OUT_SPAN / 2u);   /* not emitted */

    outputs_arm(&o, true, 2000u);
    outputs_step(&o, 2001u);
    CHECK(!outputs_overdue(&o, 0, 2001u));
    CHECK_EQ(outputs_actual(&o, 0), 900u);
    CHECK_EQ(outputs_pulse_us(&o, 0), 1900u);

    outputs_step(&o, 2000u + OUT_DEFAULT_TIMEOUT_MS - 1u);
    CHECK_EQ(outputs_actual(&o, 0), 900u);

    outputs_step(&o, 2000u + OUT_DEFAULT_TIMEOUT_MS);
    CHECK_EQ(outputs_actual(&o, 0), OUT_SPAN / 2u);
}

/* Disarming goes to rest with no ramp: the reason a stop exists is that
 * somebody wants it to have happened already. */
TEST_CASE(disarming_does_not_ramp)
{
    fresh();
    outputs_set_role(&o, 0, OUT_ROLE_THROTTLE);
    outputs_set_slew(&o, 0, 10u);           /* a hundred seconds end to end */
    outputs_arm(&o, true, 1000u);
    for (uint32_t t = 1400u; t <= 3000u; t += 400u) {
        outputs_set(&o, 0, 1000u, t);
        outputs_step(&o, t);
    }
    CHECK(outputs_actual(&o, 0) > 0u);
    CHECK(outputs_actual(&o, 0) < 1000u);   /* still on the way up */
    outputs_arm(&o, false, 3000u);
    CHECK_EQ(outputs_actual(&o, 0), 0u);
}

/* Changing a role moves where rest is, and an idle channel follows it: a
 * channel left at a throttle's zero after becoming a surface reads as a
 * surface hard over. */
TEST_CASE(a_role_change_moves_rest)
{
    fresh();
    outputs_set_role(&o, 0, OUT_ROLE_THROTTLE);
    CHECK_EQ(outputs_actual(&o, 0), 0u);
    outputs_set_role(&o, 0, OUT_ROLE_SURFACE);
    CHECK_EQ(outputs_actual(&o, 0), OUT_SPAN / 2u);
}

/* Nothing addresses a channel or slot that is not there. */
TEST_CASE(out_of_range_is_refused_everywhere)
{
    fresh();
    CHECK(!outputs_set(&o, OUT_MAX_CHANNELS, 500u, 1000u));
    CHECK(!outputs_set_role(&o, OUT_MAX_CHANNELS, OUT_ROLE_SURFACE));
    CHECK(!outputs_set_slew(&o, OUT_MAX_CHANNELS, 10u));
    CHECK(!outputs_set_endpoints(&o, OUT_MAX_CHANNELS, 1000u, 2000u));
    CHECK(!put_pwm(OUT_MAX_SLOTS, 0, 1));
    CHECK(!outputs_set_role(&o, 0, (out_role_t)7));
    CHECK_EQ(outputs_actual(&o, OUT_MAX_CHANNELS), 0u);
    CHECK_EQ(outputs_pulse_us(&o, OUT_MAX_CHANNELS), 0u);

    /* And a null bank is refused rather than followed. */
    CHECK(!outputs_set(NULL, 0, 0, 0));
    CHECK(!outputs_configure(NULL, 0, NULL));
    CHECK(outputs_overdue(NULL, 0, 0));
    CHECK(outputs_overdue(&o, OUT_MAX_CHANNELS, 0));
    CHECK(!outputs_driving(NULL));
    outputs_init(NULL, 0);
    outputs_arm(NULL, true, 0);
    outputs_step(NULL, 0);
    outputs_all_off(NULL);
}

/* A slot cleared with NONE gives its pin and channels back. */
TEST_CASE(clearing_a_slot_frees_what_it_held)
{
    fresh();
    CHECK(put_pwm(0, 0, 7));
    CHECK(!put_pwm(1, 0, 7));
    const out_slot_t none = { .driver = OUT_DRIVER_NONE };
    CHECK(outputs_configure(&o, 0, &none));
    CHECK(put_pwm(1, 0, 7));
}

/* The timeout is wrap-safe: a bench left running for forty-nine days must not
 * decide it has been silent since before the counter turned over. */
TEST_CASE(the_timeout_survives_the_wrap)
{
    const uint32_t near_wrap = (uint32_t)0u - 250u;
    outputs_init(&o, near_wrap);
    outputs_arm(&o, true, near_wrap);
    outputs_set(&o, 0, 800u, near_wrap);
    /* The deadline itself is past the turnover, which is where a naive
     * comparison fires a whole interval early. */
    CHECK(near_wrap + OUT_DEFAULT_TIMEOUT_MS < near_wrap);
    CHECK(!outputs_overdue(&o, 0, near_wrap + 125u));
    CHECK(!outputs_overdue(&o, 0, near_wrap + OUT_DEFAULT_TIMEOUT_MS - 1u));
    CHECK(outputs_overdue(&o, 0, near_wrap + OUT_DEFAULT_TIMEOUT_MS));
}



/* ------------------------------------------------- the pages as outputs */

static uint16_t chan_cfg[LINK_CC_COUNT];
static uint16_t slots[LINK_OS_COUNT];
static uint16_t chans[LINK_CH_COUNT];

static void fresh_pages(void)
{
    fresh();
    outputs_chan_cfg_defaults(chan_cfg);
    outputs_slots_defaults(slots);
    outputs_channels_defaults(chans);
}

/* A whole servo, set up the way the panel sets one up: configure the channel,
 * claim a slot, then command it, and the pulse comes back as microseconds. */
TEST_CASE(a_servo_is_configured_then_commanded)
{
    fresh_pages();
    /* channel 0: surface, no slew, 1000..2000 */
    chan_cfg[LINK_CC_ROLE]   = LINK_CC_ROLE_SURFACE;
    chan_cfg[LINK_CC_MIN_US] = 1000u;
    chan_cfg[LINK_CC_MAX_US] = 2000u;
    CHECK_EQ(outputs_chan_cfg_write(chan_cfg, 0, LINK_CC_STRIDE, chan_cfg), 0);
    outputs_chan_cfg_apply(&o, chan_cfg);

    /* slot 0: PWM (pulse-width modulation) on pin 9, channel 0 */
    slots[LINK_OS_DRIVER]  = LINK_DRIVER_PWM;
    slots[LINK_OS_PIN]     = 9u;
    slots[LINK_OS_RANGE]   = LINK_OS_RANGE_OF(0, 1);
    slots[LINK_OS_RATE_HZ] = 50u;
    CHECK_EQ(outputs_slots_write(slots, 0, LINK_OS_STRIDE, slots), 0);
    outputs_slots_apply(&o, slots);
    CHECK_EQ(o.slot[0].driver, OUT_DRIVER_PWM);
    CHECK_EQ(o.slot[0].pin, 9);

    outputs_arm(&o, true, 1000u);
    const uint16_t probe[] = { 0u, 250u, 500u, 750u, LINK_CH_SPAN };
    const uint16_t want[]  = { 1000u, 1250u, 1500u, 1750u, 2000u };
    for (unsigned i = 0; i < 5; ++i) {
        chans[0] = probe[i];
        CHECK_EQ(outputs_channels_write(chans, 0, 1, chans), 0);
        outputs_channels_apply(&o, chans, 1000u);
        outputs_step(&o, 1010u);
        CHECK_EQ(outputs_pulse_us(&o, 0), want[i]);
    }
}

/* Clearing the slot stops the output; the channel keeps its command but with
 * nothing rendering it that is inert. */
TEST_CASE(clearing_the_slot_stops_the_output)
{
    fresh_pages();
    slots[LINK_OS_DRIVER]  = LINK_DRIVER_PWM;
    slots[LINK_OS_PIN]     = 9u;
    slots[LINK_OS_RANGE]   = LINK_OS_RANGE_OF(0, 1);
    slots[LINK_OS_RATE_HZ] = 50u;
    outputs_slots_apply(&o, slots);
    CHECK_EQ(o.slot[0].driver, OUT_DRIVER_PWM);

    slots[LINK_OS_DRIVER] = LINK_DRIVER_NONE;
    outputs_slots_apply(&o, slots);
    CHECK_EQ(o.slot[0].driver, OUT_DRIVER_NONE);
}

/* A channel config with an impossible endpoint is refused, and the refusal
 * changes nothing. */
TEST_CASE(an_impossible_endpoint_is_refused_atomically)
{
    fresh_pages();
    uint16_t in[LINK_CC_STRIDE] = {
        [LINK_CC_ROLE]   = LINK_CC_ROLE_SURFACE,
        [LINK_CC_SLEW]   = 0u,
        [LINK_CC_MIN_US] = 100u,      /* below the floor */
        [LINK_CC_MAX_US] = 2000u,
    };
    CHECK_EQ(outputs_chan_cfg_write(chan_cfg, 0, LINK_CC_STRIDE, in),
             LINK_NACK_BAD_VALUE);
    /* untouched: still the default range */
    CHECK_EQ(chan_cfg[LINK_CC_MIN_US], LINK_CC_DEFAULT_MIN);

    in[LINK_CC_MIN_US] = 9000u;       /* above the ceiling */
    CHECK_EQ(outputs_chan_cfg_write(chan_cfg, 0, LINK_CC_STRIDE, in),
             LINK_NACK_BAD_VALUE);
    /* an unknown role is refused too */
    in[LINK_CC_MIN_US] = 1000u;
    in[LINK_CC_ROLE]   = 7u;
    CHECK_EQ(outputs_chan_cfg_write(chan_cfg, 0, LINK_CC_STRIDE, in),
             LINK_NACK_BAD_VALUE);
}

/* An unknown driver number is refused before it reaches the table. */
TEST_CASE(an_unknown_driver_is_refused)
{
    fresh_pages();
    uint16_t in[LINK_OS_STRIDE] = {
        [LINK_OS_DRIVER]  = 99u,
        [LINK_OS_PIN]     = 9u,
        [LINK_OS_RANGE]   = LINK_OS_RANGE_OF(0, 1),
        [LINK_OS_RATE_HZ] = 50u,
    };
    CHECK_EQ(outputs_slots_write(slots, 0, LINK_OS_STRIDE, in),
             LINK_NACK_BAD_VALUE);
    /* and both DShot drivers are known, so they store */
    in[LINK_OS_DRIVER] = LINK_DRIVER_DSHOT;
    CHECK_EQ(outputs_slots_write(slots, 0, LINK_OS_STRIDE, in), 0);
    in[LINK_OS_DRIVER] = LINK_DRIVER_DSHOT_BIDIR;
    CHECK_EQ(outputs_slots_write(slots, 0, LINK_OS_STRIDE, in), 0);
}

/* The slew on the channel-config page is in span units per second, so it
 * reaches the bank without conversion. */
TEST_CASE(the_slew_is_span_units)
{
    fresh_pages();
    chan_cfg[LINK_CC_SLEW] = 500u;    /* half a span per second */
    outputs_chan_cfg_apply(&o, chan_cfg);
    CHECK_EQ(o.channel[0].slew_per_s, 500u);
}

/* PPM claims eight channels on one pin, and the range register carries that. */
TEST_CASE(the_range_register_packs_first_and_count)
{
    fresh_pages();
    slots[LINK_OS_DRIVER]  = LINK_DRIVER_PPM;
    slots[LINK_OS_PIN]     = 5u;
    slots[LINK_OS_RANGE]   = LINK_OS_RANGE_OF(0, 8);
    slots[LINK_OS_RATE_HZ] = 22u;
    CHECK_EQ(LINK_OS_FIRST(slots[LINK_OS_RANGE]), 0);
    CHECK_EQ(LINK_OS_CHANNELS(slots[LINK_OS_RANGE]), 8);
    outputs_slots_apply(&o, slots);
    CHECK_EQ(o.slot[0].driver, OUT_DRIVER_PPM);
    CHECK_EQ(o.slot[0].channels, 8);
}

/* A command past the span is clamped in the store, so a read-back is honest. */
TEST_CASE(a_channel_command_is_clamped_in_place)
{
    fresh_pages();
    uint16_t in[1] = { 5000u };
    CHECK_EQ(outputs_channels_write(chans, 0, 1, in), 0);
    CHECK_EQ(chans[0], LINK_CH_SPAN);
}

/* Writing off the end of any page is refused. */
TEST_CASE(writing_off_a_page_end_is_refused)
{
    fresh_pages();
    uint16_t z[4] = { 0, 0, 0, 0 };
    CHECK_EQ(outputs_chan_cfg_write(chan_cfg, LINK_CC_COUNT, 1, z),
             LINK_NACK_BAD_RANGE);
    CHECK_EQ(outputs_slots_write(slots, LINK_OS_COUNT, 1, z),
             LINK_NACK_BAD_RANGE);
    CHECK_EQ(outputs_channels_write(chans, LINK_CH_COUNT, 1, z),
             LINK_NACK_BAD_RANGE);
    /* and nulls are refused rather than followed */
    CHECK_EQ(outputs_chan_cfg_write(NULL, 0, 1, z), LINK_NACK_BAD_RANGE);
    outputs_chan_cfg_apply(NULL, chan_cfg);
    outputs_slots_apply(&o, NULL);
    outputs_channels_apply(NULL, chans, 0);
}

TEST_CASE(the_throttle_reaches_every_motor_channel_and_no_other)
{
    /*
     * One slider, several pins.  A bench binds an ESC (electronic speed
     * controller) and a servo at once, and the throttle drives the motors
     * without disturbing the surface beside them.
     */
    outputs_t b;
    outputs_init(&b, 0u);
    CHECK(outputs_set_role(&b, 0, OUT_ROLE_THROTTLE));
    CHECK(outputs_set_role(&b, 1, OUT_ROLE_SURFACE));
    CHECK(outputs_set_role(&b, 2, OUT_ROLE_THROTTLE));
    const uint16_t surface_was = OUT_SPAN / 4u;
    CHECK(outputs_set(&b, 1, surface_was, 0u));

    CHECK_EQ(outputs_set_role_channels(&b, OUT_ROLE_THROTTLE,
                                       (uint8_t)LINK_OUT_CHANNELS,
                                       OUT_SPAN / 2u, 10u), 2);
    outputs_arm(&b, true, 10u);
    outputs_step(&b, 10u);
    CHECK_EQ(outputs_actual(&b, 0), OUT_SPAN / 2u);
    CHECK_EQ(outputs_actual(&b, 2), OUT_SPAN / 2u);
    CHECK_EQ(outputs_actual(&b, 1), surface_was);
}

TEST_CASE(the_throttle_stops_at_the_page_it_was_given)
{
    /* The bank is wider than the page.  A channel above the limit is not a
     * bound pin, and the fan-out must not reach it. */
    outputs_t b;
    outputs_init(&b, 0u);
    for (uint8_t ch = 0; ch < OUT_MAX_CHANNELS; ++ch) {
        CHECK(outputs_set_role(&b, ch, OUT_ROLE_THROTTLE));
    }
    CHECK_EQ(outputs_set_role_channels(&b, OUT_ROLE_THROTTLE,
                                       (uint8_t)LINK_OUT_CHANNELS,
                                       OUT_SPAN, 5u), LINK_OUT_CHANNELS);
    outputs_arm(&b, true, 5u);
    outputs_step(&b, 5u);
    CHECK_EQ(outputs_actual(&b, LINK_OUT_CHANNELS - 1u), OUT_SPAN);
    /* Never commanded, so it sits at the rest of its role. */
    CHECK_EQ(outputs_actual(&b, LINK_OUT_CHANNELS), 0u);

    /* A limit past the bank is clamped to the bank, and a null bank is not
     * followed. */
    CHECK_EQ(outputs_set_role_channels(&b, OUT_ROLE_THROTTLE, 255u,
                                       OUT_SPAN, 6u), OUT_MAX_CHANNELS);
    CHECK_EQ(outputs_set_role_channels(NULL, OUT_ROLE_THROTTLE, 8u, 0u, 6u),
             0);
}

TEST_CASE(a_channel_write_keeps_only_the_channels_it_named_alive)
{
    /*
     * The timeout is per channel because the pages that write them are
     * written independently.  A servo refreshed at 10 Hz to hold its
     * position must not stamp the clock of a surface nobody is driving, or
     * that surface never reaches its rest.
     */
    fresh_pages();
    outputs_t b;
    outputs_init(&b, 0u);
    outputs_arm(&b, true, 0u);

    uint16_t two[2] = { LINK_CH_SPAN / 4u, LINK_CH_SPAN / 2u };
    CHECK_EQ(outputs_channels_write(chans, 0, 2, two), 0);
    outputs_channels_apply_n(&b, chans, 0u, 2u, 0u);

    /* 600 ms later only channel 0 is written again, one register. */
    uint16_t one[1] = { LINK_CH_SPAN / 4u };
    CHECK_EQ(outputs_channels_write(chans, 0, 1, one), 0);
    outputs_channels_apply_n(&b, chans, 0u, 1u, 600u);

    CHECK(!outputs_overdue(&b, 0, 600u));
    CHECK(outputs_overdue(&b, 1, 600u));

    /* And the one nobody commanded goes to its rest, while the other holds. */
    outputs_step(&b, 600u);
    CHECK_EQ(outputs_actual(&b, 0), OUT_SPAN / 4u);
    CHECK_EQ(outputs_actual(&b, 1), OUT_SPAN / 2u);   /* a surface rests mid */
}

TEST_CASE(a_channel_range_outside_the_page_is_refused_rather_than_wrapped)
{
    fresh_pages();
    outputs_t b;
    outputs_init(&b, 0u);
    /* Past the end, and a count that runs past it: neither may write. */
    outputs_channels_apply_n(&b, chans, (uint8_t)LINK_CH_COUNT, 1u, 10u);
    outputs_channels_apply_n(&b, chans, (uint8_t)(LINK_CH_COUNT - 1u), 8u, 10u);
    outputs_channels_apply_n(NULL, chans, 0u, 1u, 10u);
    outputs_channels_apply_n(&b, NULL, 0u, 1u, 10u);
    /* The whole page still applies through the plain call. */
    outputs_channels_apply(&b, chans, 10u);
    CHECK(!outputs_overdue(&b, 0, 10u));
}

/* ------------------------------------------- the RP2350's PWM pin fold */

/*
 * The slice and channel each GPIO reaches, stated rather than recomputed.
 * These are the numbers the pico-sdk's PWM_GPIO_SLICE_NUM produces, and the
 * coprocessor's PWM driver decides from them which pins may be bound at once.
 */
TEST_CASE(the_pwm_fold_puts_each_pin_on_a_named_slice_and_channel)
{
    static const struct { uint8_t pin, slice, chan; } k[] = {
        {  0u,  0u, 0u }, {  1u,  0u, 1u }, {  2u,  1u, 0u }, {  3u,  1u, 1u },
        { 14u,  7u, 0u }, { 15u,  7u, 1u },
        /* The fold: GP16 lands back on slice 0 channel A, where GP0 is. */
        { 16u,  0u, 0u }, { 17u,  0u, 1u }, { 22u,  3u, 0u }, { 28u,  6u, 0u },
        { 31u,  7u, 1u },
        /* Above GP31 four slices serve sixteen pins, so it folds at eight. */
        { 32u,  8u, 0u }, { 33u,  8u, 1u }, { 39u, 11u, 1u },
        { 40u,  8u, 0u }, { 47u, 11u, 1u },
    };
    for (unsigned i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        CHECK_EQ(out_pwm_slice_of(k[i].pin), k[i].slice);
        CHECK_EQ(out_pwm_channel_of(k[i].pin), k[i].chan);
    }
    /* Twelve slices of two channels is what the arithmetic has to reach. */
    CHECK_EQ(out_pwm_slice_of(38u), OUT_PWM_SLICES - 1u);
    CHECK_EQ(out_pwm_channel_of(39u), OUT_PWM_CHANNELS - 1u);
}

/*
 * The pairs that share one compare register, which is one pulse width.
 *
 * Every pair named here is two free pins of the coprocessor's own header, so
 * an operator ticking pins on the OUTPUTS page can reach all six.
 */
TEST_CASE(two_header_pins_sixteen_apart_are_one_compare_register)
{
    static const uint8_t k_collide[][2] = {
        {  0u, 16u }, {  1u, 17u }, {  2u, 18u },
        {  4u, 20u }, {  5u, 21u }, {  6u, 22u },
    };
    for (unsigned i = 0; i < sizeof(k_collide) / sizeof(k_collide[0]); ++i) {
        CHECK(out_pwm_same_compare(k_collide[i][0], k_collide[i][1]));
        CHECK(out_pwm_same_compare(k_collide[i][1], k_collide[i][0]));
        CHECK(out_pwm_same_slice(k_collide[i][0], k_collide[i][1]));
    }

    /* The two channels of one slice: one wrap, two compare registers, so a
     * shared frame rate and two independent pulse widths. */
    static const uint8_t k_slice_only[][2] = {
        {  0u,  1u }, {  2u, 19u }, {  6u,  7u }, { 13u, 28u }, { 26u, 27u },
    };
    for (unsigned i = 0; i < sizeof(k_slice_only) / sizeof(k_slice_only[0]);
         ++i) {
        CHECK(out_pwm_same_slice(k_slice_only[i][0], k_slice_only[i][1]));
        CHECK(!out_pwm_same_compare(k_slice_only[i][0], k_slice_only[i][1]));
    }

    /* Different slices share nothing at all. */
    CHECK(!out_pwm_same_slice(0u, 2u));
    CHECK(!out_pwm_same_compare(0u, 2u));
    CHECK(!out_pwm_same_slice(31u, 32u));

    /* A pin against itself is the same register, which is what makes
     * rebinding a pin already bound a question about the pin, not the fold. */
    CHECK(out_pwm_same_compare(7u, 7u));
}

/*
 * The whole fold as one rule: below GP32 a collision is exactly sixteen pins
 * apart, at and above it exactly eight, and nothing crosses between the two
 * ranges.  Stated as the distance rather than as the slice arithmetic, so a
 * wrong divisor or a wrong mask fails here instead of agreeing with itself.
 */
TEST_CASE(the_only_pins_sharing_a_compare_register_are_the_folded_pairs)
{
    for (unsigned a = 0; a < OUT_PWM_GPIOS; ++a) {
        for (unsigned b = a + 1u; b < OUT_PWM_GPIOS; ++b) {
            const bool folded = (b < 32u) ? (b - a == 16u)
                                          : (a >= 32u && b - a == 8u);
            if (out_pwm_same_compare((uint8_t)a, (uint8_t)b) != folded) {
                T_FAIL("GP%u and GP%u: compare sharing is %d, want %d",
                       a, b, (int)out_pwm_same_compare((uint8_t)a, (uint8_t)b),
                       (int)folded);
            }
        }
    }
}

/* A pin the part does not have reaches no register, so it shares none. */
TEST_CASE(a_pin_past_the_bank_reaches_no_slice)
{
    CHECK_EQ(out_pwm_slice_of((uint8_t)OUT_PWM_GPIOS), OUT_PWM_NONE);
    CHECK_EQ(out_pwm_channel_of((uint8_t)OUT_PWM_GPIOS), OUT_PWM_NONE);
    CHECK_EQ(out_pwm_slice_of(255u), OUT_PWM_NONE);
    CHECK(!out_pwm_same_slice((uint8_t)OUT_PWM_GPIOS, 0u));
    CHECK(!out_pwm_same_slice(0u, (uint8_t)OUT_PWM_GPIOS));
    CHECK(!out_pwm_same_compare((uint8_t)OUT_PWM_GPIOS,
                                (uint8_t)OUT_PWM_GPIOS));
    CHECK(!out_pwm_same_compare(255u, 0u));
}

int main(void)
{
    RUN(a_channel_write_keeps_only_the_channels_it_named_alive);
    RUN(a_channel_range_outside_the_page_is_refused_rather_than_wrapped);
    RUN(the_throttle_reaches_every_motor_channel_and_no_other);
    RUN(the_throttle_stops_at_the_page_it_was_given);
    RUN(everything_goes_to_rest_together);
    RUN(silence_stops_every_role);
    RUN(one_quiet_channel_does_not_stop_the_others);
    RUN(re_arming_is_not_activity);
    RUN(the_role_decides_which_direction_is_safe);
    RUN(a_slow_slew_still_moves);
    RUN(a_pin_belongs_to_one_driver);
    RUN(a_reserved_pin_is_refused);
    RUN(the_outputs_page_refuses_a_pin_that_does_not_fit_the_field);
    RUN(ppm_claims_a_range_of_channels);
    RUN(the_table_says_what_a_driver_is_shaped_like);
    RUN(a_rate_outside_the_driver_is_refused);
    RUN(endpoints_are_refused_and_commands_are_clamped);
    RUN(an_inverted_range_is_straightened);
    RUN(the_pulse_spans_the_endpoints);
    RUN(a_command_while_disarmed_is_remembered_and_not_emitted);
    RUN(driving_is_armed_and_asks_nothing_about_commands);
    RUN(a_re_arm_ramps_from_rest_when_the_channel_is_slewed);
    RUN(a_surface_rests_at_the_midpoint_of_its_own_endpoints);
    RUN(the_timeout_reaches_a_channel_and_leaves_it_driving);
    RUN(an_arm_makes_a_disarmed_command_fresh_for_the_timeout);
    RUN(disarming_does_not_ramp);
    RUN(a_role_change_moves_rest);
    RUN(out_of_range_is_refused_everywhere);
    RUN(clearing_a_slot_frees_what_it_held);
    RUN(the_timeout_survives_the_wrap);
    RUN(a_servo_is_configured_then_commanded);
    RUN(clearing_the_slot_stops_the_output);
    RUN(an_impossible_endpoint_is_refused_atomically);
    RUN(an_unknown_driver_is_refused);
    RUN(the_slew_is_span_units);
    RUN(the_range_register_packs_first_and_count);
    RUN(a_channel_command_is_clamped_in_place);
    RUN(writing_off_a_page_end_is_refused);
    RUN(the_pwm_fold_puts_each_pin_on_a_named_slice_and_channel);
    RUN(two_header_pins_sixteen_apart_are_one_compare_register);
    RUN(the_only_pins_sharing_a_compare_register_are_the_folded_pairs);
    RUN(a_pin_past_the_bank_reaches_no_slice);
    return test_summary("outputs");
}
