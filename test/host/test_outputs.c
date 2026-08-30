/*
 * The intermediary.
 *
 * These are the rules that used to be in two places and disagreed, so most of
 * what follows is a difference the old arrangement could not have expressed:
 * a role deciding what rest is and which direction is safe, and a table
 * catching two drivers reaching for one pin.
 */
#include <string.h>

#include "greatest.h"

#include "outputs.h"
#include "outputs_pages.h"
#include "link_pages.h"
#include "link_servo.h"

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
 * The bug that motivated all of this.
 *
 * The failsafe path cleared the throttle and left the servo page enabled,
 * because it was written before the servo page existed and nobody added the
 * second one to it.  With one place to switch off, there is no second one to
 * forget.
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
 * The other half of that bug: the throttle disarmed itself after silence and
 * the servo page held its last pulse forever.  One timeout now, and it
 * reaches a surface as surely as a throttle.
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
 * Arming is idempotent, and it has to be: the coprocessor recomputes whether
 * driving is allowed every pass, and a version that stamped the clock each
 * time would hold every channel alive forever and delete the timeout.
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

    /* Now down.  The throttle goes at once; the surface is ramped. */
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
 * Two drivers on one pin, and two rendering the same channel.  Neither is
 * expressible when each protocol owns its own page, because nothing in that
 * arrangement can see the other protocol's pins.
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
 * PPM is eight channels on one pin, which is the case a one-value-per-pin
 * design cannot hold at all.  The table says so, and the range it claims is
 * the range the conflict check defends.
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
    CHECK(out_driver(OUT_DRIVER_DSHOT)->reads_back);
    CHECK(!out_driver(OUT_DRIVER_PWM)->reads_back);
    /* Only pulse drivers go through the endpoints. */
    CHECK(out_driver(OUT_DRIVER_PWM)->pulsed);
    CHECK(!out_driver(OUT_DRIVER_DSHOT)->pulsed);
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
 * Endpoints are refused and commands are clamped, and keeping those apart is
 * the same distinction the servo page had.  It survives the move.
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

/* Changing a role moves where rest is, and an idle channel follows it -- a
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

static uint16_t servo_page[LINK_SV_COUNT];

static void fresh_page(void)
{
    fresh();
    link_servo_defaults(servo_page);
}

/* Enable configures a slot and clearing it takes the pin back, because "not
 * driving" has to mean there is no pin before there is a pin to stop. */
TEST_CASE(enable_is_a_slot_rather_than_an_arm)
{
    fresh_page();
    outputs_apply_servo_page(&o, servo_page, 1, 0, 9, 1000u);
    CHECK_EQ(o.slot[0].driver, OUT_DRIVER_NONE);
    CHECK(!o.armed);            /* enabling a servo did not arm anything */

    servo_page[LINK_SV_ENABLE] = 1u;
    outputs_apply_servo_page(&o, servo_page, 1, 0, 9, 1000u);
    CHECK_EQ(o.slot[0].driver, OUT_DRIVER_PWM);
    CHECK_EQ(o.slot[0].pin, 9);
    CHECK_EQ(o.slot[0].first_channel, 1);
    CHECK(!o.armed);

    servo_page[LINK_SV_ENABLE] = 0u;
    outputs_apply_servo_page(&o, servo_page, 1, 0, 9, 1000u);
    CHECK_EQ(o.slot[0].driver, OUT_DRIVER_NONE);
}

/* A pulse in microseconds becomes a proportion of travel, and comes back out
 * as the microseconds it went in as. */
TEST_CASE(a_pulse_survives_the_round_trip)
{
    fresh_page();
    servo_page[LINK_SV_ENABLE] = 1u;
    outputs_arm(&o, true, 1000u);

    const uint16_t probe[] = { 1000u, 1250u, 1500u, 1750u, 2000u };
    for (unsigned i = 0; i < sizeof(probe) / sizeof(probe[0]); ++i) {
        servo_page[LINK_SV_PULSE_US] = probe[i];
        outputs_apply_servo_page(&o, servo_page, 1, 0, 9, 1000u);
        outputs_step(&o, 1000u);
        CHECK_EQ(outputs_pulse_us(&o, 1), probe[i]);
    }
}

/* Narrow endpoints still span the whole command range, so the same command
 * means the same fraction of travel whatever the servo's range is. */
TEST_CASE(narrow_endpoints_still_span_the_command)
{
    fresh_page();
    servo_page[LINK_SV_ENABLE]  = 1u;
    servo_page[LINK_SV_MIN_US]  = 1200u;
    servo_page[LINK_SV_MAX_US]  = 1800u;
    servo_page[LINK_SV_PULSE_US]= 1800u;
    outputs_arm(&o, true, 1000u);
    outputs_apply_servo_page(&o, servo_page, 1, 0, 9, 1000u);
    outputs_step(&o, 1000u);
    CHECK_EQ(outputs_actual(&o, 1), OUT_SPAN);
    CHECK_EQ(outputs_pulse_us(&o, 1), 1800u);
}

/*
 * The slew converts too.  A page asking for its whole travel in one second
 * has to mean that on a narrow servo as much as a wide one -- a slew limit
 * that changes meaning with the endpoints is not a limit.
 */
TEST_CASE(the_slew_converts_with_the_endpoints)
{
    fresh_page();
    servo_page[LINK_SV_ENABLE] = 1u;
    servo_page[LINK_SV_MIN_US] = 1200u;
    servo_page[LINK_SV_MAX_US] = 1800u;   /* 600 us of travel */
    servo_page[LINK_SV_SLEW_US]= 600u;    /* all of it per second */
    outputs_apply_servo_page(&o, servo_page, 1, 0, 9, 1000u);
    CHECK_EQ(o.channel[1].slew_per_s, OUT_SPAN);
}

/* A degenerate range divides by zero if nothing catches it.  The page's own
 * rules straighten inverted pairs, but nothing here may lean on that. */
TEST_CASE(a_zero_width_range_is_survivable)
{
    fresh_page();
    servo_page[LINK_SV_ENABLE]   = 1u;
    servo_page[LINK_SV_MIN_US]   = 1500u;
    servo_page[LINK_SV_MAX_US]   = 1500u;
    servo_page[LINK_SV_PULSE_US] = 1500u;
    servo_page[LINK_SV_SLEW_US]  = 400u;
    outputs_apply_servo_page(&o, servo_page, 1, 0, 9, 1000u);
    CHECK_EQ(o.channel[1].slew_per_s, 0u);
    CHECK_EQ(o.channel[1].command, OUT_SPAN / 2u);
    CHECK_EQ(outputs_pulse_us(&o, 1), 1500u);
}

/* The throttle page scales from its own maximum, and arriving at rest is what
 * a zero throttle has to mean. */
TEST_CASE(the_control_page_scales_the_throttle)
{
    fresh();
    uint16_t control[LINK_CT_COUNT] = { 0 };
    outputs_arm(&o, true, 1000u);

    control[LINK_CT_THROTTLE] = LINK_THROTTLE_MAX;
    outputs_apply_control_page(&o, control, 0, 1000u);
    outputs_step(&o, 1000u);
    CHECK_EQ(outputs_actual(&o, 0), OUT_SPAN);
    CHECK_EQ(o.channel[0].role, OUT_ROLE_THROTTLE);

    control[LINK_CT_THROTTLE] = LINK_THROTTLE_MAX / 2u;
    outputs_apply_control_page(&o, control, 0, 1000u);
    outputs_step(&o, 1000u);
    CHECK_EQ(outputs_actual(&o, 0), OUT_SPAN / 2u);

    control[LINK_CT_THROTTLE] = 0u;
    outputs_apply_control_page(&o, control, 0, 1000u);
    outputs_step(&o, 1000u);
    CHECK_EQ(outputs_actual(&o, 0), 0u);
}

/* Neither adapter follows a null. */
TEST_CASE(the_adapters_refuse_nothing_at_all)
{
    fresh_page();
    outputs_apply_servo_page(NULL, servo_page, 1, 0, 9, 0);
    outputs_apply_servo_page(&o, NULL, 1, 0, 9, 0);
    outputs_apply_control_page(NULL, servo_page, 0, 0);
    outputs_apply_control_page(&o, NULL, 0, 0);
    CHECK_EQ(o.slot[0].driver, OUT_DRIVER_NONE);
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
    RUN(enable_is_a_slot_rather_than_an_arm);
    RUN(a_pulse_survives_the_round_trip);
    RUN(narrow_endpoints_still_span_the_command);
    RUN(the_slew_converts_with_the_endpoints);
    RUN(a_zero_width_range_is_survivable);
    RUN(the_control_page_scales_the_throttle);
    RUN(the_adapters_refuse_nothing_at_all);
    return test_summary("outputs");
}
