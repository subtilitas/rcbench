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
 * One silence timeout for every channel: a surface stops driving after
 * silence the same as a throttle does.
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

int main(void)
{
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
    return test_summary("outputs");
}
