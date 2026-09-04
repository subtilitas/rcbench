/*
 * Binding a protocol to pins, and the OUTPUTS page that falls out of it.
 *
 * The failures under test are the ones an operator cannot see: a pin that is
 * offered and then silently refused at the far end, a protocol change that
 * leaves more pins selected than it can drive, and a page written from a new
 * selection that still carries a slot from the last one.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "out_bind.h"
#include "link_pages.h"
#include "outputs_pages.h"

static uint8_t idx(uint8_t gpio) { return outbind_index_of(gpio); }

static uint8_t proto_named(const char *name)
{
    const outbind_proto_t *p = outbind_protos();
    for (uint8_t i = 0; i < OUTBIND_PROTOS; ++i) {
        if (strcmp(p[i].name, name) == 0) {
            return i;
        }
    }
    return 0u;
}

/* ------------------------------------------------------------- catalogue */

TEST_CASE(the_catalogue_is_the_header_and_nothing_else)
{
    const outbind_pin_t *p = outbind_pins();
    /* GP23 to GP25 exist in the part and not on the pads, so they cannot be
     * wired and are not offered. */
    CHECK_EQ(idx(23), (uint8_t)OUTBIND_PINS);
    CHECK_EQ(idx(24), (uint8_t)OUTBIND_PINS);
    CHECK_EQ(idx(25), (uint8_t)OUTBIND_PINS);
    CHECK_EQ(idx(29), (uint8_t)OUTBIND_PINS);
    CHECK(idx(0) < OUTBIND_PINS);
    CHECK(idx(28) < OUTBIND_PINS);

    /* In GPIO order, so a list drawn from it reads the way a pinout does. */
    for (uint8_t i = 1; i < OUTBIND_PINS; ++i) {
        if (p[i].gpio <= p[i-1].gpio) {
            T_FAIL("entry %u (GP%u) does not follow GP%u",
                   i, p[i].gpio, p[i-1].gpio);
        }
    }
}

TEST_CASE(the_reserved_set_is_the_safety_line_and_the_can_bus)
{
    const uint64_t m = outbind_reserved_mask();
    const uint64_t want = (1ull << 3) | (1ull << 8) | (1ull << 9)
                        | (1ull << 10) | (1ull << 11) | (1ull << 12);
    /* Pinned exactly: the coprocessor hands this to outputs_reserve_pins(),
     * so a pin that quietly leaves this set becomes an output on the
     * heartbeat line or the CAN bus. */
    CHECK_EQ(m, want);

    const outbind_pin_t *p = outbind_pins();
    for (uint8_t i = 0; i < OUTBIND_PINS; ++i) {
        const bool in = (m & (1ull << p[i].gpio)) != 0u;
        CHECK_EQ(in, p[i].reserved);
        /* A reserved pin says what has it, so the screen can too. */
        CHECK(!p[i].reserved || p[i].held_by != NULL);
    }
}

TEST_CASE(the_pad_numbers_match_the_ones_printed_on_the_board)
{
    /* The four corners of the header, which the silkscreen names. */
    CHECK_EQ(outbind_pins()[idx(0)].pad, 1);
    CHECK_EQ(outbind_pins()[idx(15)].pad, 20);
    CHECK_EQ(outbind_pins()[idx(16)].pad, 21);
    CHECK_EQ(outbind_pins()[idx(3)].pad, 5);
}

/* ------------------------------------------------------------- selecting */

TEST_CASE(a_reserved_pin_cannot_be_chosen)
{
    outbind_t b;
    outbind_init(&b);
    outbind_set_proto(&b, proto_named("SERVO PWM"));

    CHECK(!outbind_can_add(&b, idx(3)));
    CHECK(!outbind_toggle(&b, idx(3)));
    CHECK(!outbind_toggle(&b, idx(10)));
    CHECK_EQ(outbind_chosen(&b), 0);

    CHECK(outbind_toggle(&b, idx(0)));
    CHECK_EQ(outbind_chosen(&b), 1);
}

TEST_CASE(nothing_can_be_chosen_while_the_protocol_is_off)
{
    outbind_t b;
    outbind_init(&b);
    CHECK(!outbind_can_add(&b, idx(0)));
    CHECK(!outbind_toggle(&b, idx(0)));
}

TEST_CASE(a_pin_can_always_be_taken_back)
{
    outbind_t b;
    outbind_init(&b);
    outbind_set_proto(&b, proto_named("PPM"));
    CHECK(outbind_toggle(&b, idx(0)));
    /* PPM is full at one pin, but undoing the one that filled it must work
     * or the operator is stuck with the choice they just made. */
    CHECK(!outbind_can_add(&b, idx(1)));
    CHECK(outbind_toggle(&b, idx(0)));
    CHECK_EQ(outbind_chosen(&b), 0);
}

TEST_CASE(ppm_takes_one_pin_and_pwm_takes_the_bank)
{
    outbind_t b;
    outbind_init(&b);
    outbind_set_proto(&b, proto_named("SERVO PWM"));
    static const uint8_t gp[9] = { 0,1,2,4,5,6,7,13,14 };
    for (unsigned i = 0; i < 8u; ++i) {
        CHECK(outbind_toggle(&b, idx(gp[i])));
    }
    CHECK_EQ(outbind_chosen(&b), 8);
    /* Eight slots is the whole bank; a ninth has nowhere to go. */
    CHECK(!outbind_toggle(&b, idx(gp[8])));

    outbind_init(&b);
    outbind_set_proto(&b, proto_named("PPM"));
    CHECK(outbind_toggle(&b, idx(0)));
    CHECK(!outbind_toggle(&b, idx(1)));
    CHECK_EQ(outbind_chosen(&b), 1);
}

TEST_CASE(switching_protocol_drops_the_pins_that_no_longer_fit)
{
    outbind_t b;
    outbind_init(&b);
    outbind_set_proto(&b, proto_named("SERVO PWM"));
    for (uint8_t g = 0; g < 3u; ++g) {
        CHECK(outbind_toggle(&b, idx(g)));      /* GP0, GP1, GP2 */
    }
    CHECK_EQ(outbind_chosen(&b), 3);

    /* The protocol is what was asked for; the pins are what gives. */
    outbind_set_proto(&b, proto_named("PPM"));
    CHECK_EQ(outbind_chosen(&b), 1);
    CHECK((b.pins & (1u << idx(0))) != 0u);     /* the first one is kept */

    outbind_set_proto(&b, proto_named("DSHOT600"));
    CHECK_EQ(outbind_chosen(&b), 1);
}

/* ---------------------------------------------------------------- pages */

static uint16_t slot_reg(const uint16_t *regs, uint8_t slot, uint8_t field)
{
    return regs[(size_t)slot * LINK_OS_STRIDE + field];
}

TEST_CASE(each_pin_becomes_one_slot_in_pin_order)
{
    outbind_t b;
    outbind_init(&b);
    outbind_set_proto(&b, proto_named("SERVO PWM"));
    /* Ticked out of order; the page is written in pin order regardless, so
     * the lowest pin is channel 0 whatever order the screen was touched. */
    CHECK(outbind_toggle(&b, idx(13)));
    CHECK(outbind_toggle(&b, idx(0)));
    CHECK(outbind_toggle(&b, idx(5)));

    uint16_t regs[LINK_OS_COUNT];
    CHECK_EQ(outbind_to_slots(&b, regs), 3);
    static const uint8_t want[3] = { 0, 5, 13 };
    for (uint8_t s = 0; s < 3u; ++s) {
        CHECK_EQ(slot_reg(regs, s, LINK_OS_DRIVER), LINK_DRIVER_PWM);
        CHECK_EQ(slot_reg(regs, s, LINK_OS_PIN), want[s]);
        CHECK_EQ(slot_reg(regs, s, LINK_OS_RATE_HZ), 50);
        CHECK_EQ(LINK_OS_FIRST(slot_reg(regs, s, LINK_OS_RANGE)), s);
        CHECK_EQ(LINK_OS_CHANNELS(slot_reg(regs, s, LINK_OS_RANGE)), 1);
    }
    /* And every slot the selection did not use says so. */
    for (uint8_t s = 3; s < LINK_OUT_SLOTS; ++s) {
        CHECK_EQ(slot_reg(regs, s, LINK_OS_DRIVER), LINK_DRIVER_NONE);
    }
}

TEST_CASE(a_page_written_from_a_selection_carries_nothing_from_the_last_one)
{
    outbind_t b;
    uint16_t regs[LINK_OS_COUNT];

    outbind_init(&b);
    outbind_set_proto(&b, proto_named("SERVO PWM"));
    for (uint8_t g = 0; g < 3u; ++g) { (void)outbind_toggle(&b, idx(g)); }
    CHECK_EQ(outbind_to_slots(&b, regs), 3);

    outbind_init(&b);
    outbind_set_proto(&b, proto_named("DSHOT600"));
    CHECK(outbind_toggle(&b, idx(7)));
    CHECK_EQ(outbind_to_slots(&b, regs), 1);
    CHECK_EQ(slot_reg(regs, 0, LINK_OS_PIN), 7);
    for (uint8_t s = 1; s < LINK_OUT_SLOTS; ++s) {
        CHECK_EQ(slot_reg(regs, s, LINK_OS_DRIVER), LINK_DRIVER_NONE);
    }
}

TEST_CASE(ppm_claims_the_whole_channel_range_on_its_one_pin)
{
    outbind_t b;
    outbind_init(&b);
    outbind_set_proto(&b, proto_named("PPM"));
    CHECK(outbind_toggle(&b, idx(2)));

    uint16_t regs[LINK_OS_COUNT];
    CHECK_EQ(outbind_to_slots(&b, regs), 1);
    CHECK_EQ(slot_reg(regs, 0, LINK_OS_DRIVER), LINK_DRIVER_PPM);
    CHECK_EQ(LINK_OS_CHANNELS(slot_reg(regs, 0, LINK_OS_RANGE)), 8);
}

TEST_CASE(the_two_dshot_drivers_are_told_apart_on_the_wire)
{
    outbind_t b;
    uint16_t regs[LINK_OS_COUNT];

    outbind_init(&b);
    outbind_set_proto(&b, proto_named("DSHOT600"));
    CHECK(outbind_toggle(&b, idx(4)));
    (void)outbind_to_slots(&b, regs);
    CHECK_EQ(slot_reg(regs, 0, LINK_OS_DRIVER), LINK_DRIVER_DSHOT);
    CHECK_EQ(slot_reg(regs, 0, LINK_OS_RATE_HZ), 600);

    outbind_set_proto(&b, proto_named("DSHOT300 BIDIR"));
    (void)outbind_to_slots(&b, regs);
    CHECK_EQ(slot_reg(regs, 0, LINK_OS_DRIVER), LINK_DRIVER_DSHOT_BIDIR);
    CHECK_EQ(slot_reg(regs, 0, LINK_OS_RATE_HZ), 300);
}

TEST_CASE(a_throttle_protocol_makes_throttles_and_a_pulse_one_makes_surfaces)
{
    outbind_t b;
    uint16_t cc[LINK_CC_COUNT];

    /* The role decides where a channel goes when it stops being commanded.
     * A throttle that rests centred is a motor at half power. */
    outbind_init(&b);
    outbind_set_proto(&b, proto_named("DSHOT600"));
    CHECK(outbind_toggle(&b, idx(0)));
    CHECK(outbind_toggle(&b, idx(1)));
    outbind_to_chan_cfg(&b, cc, 1000u, 2000u);
    CHECK_EQ(cc[0 * LINK_CC_STRIDE + LINK_CC_ROLE], LINK_CC_ROLE_THROTTLE);
    CHECK_EQ(cc[1 * LINK_CC_STRIDE + LINK_CC_ROLE], LINK_CC_ROLE_THROTTLE);
    /* An untouched channel keeps the schema's default. */
    CHECK_EQ(cc[2 * LINK_CC_STRIDE + LINK_CC_ROLE], LINK_CC_ROLE_SURFACE);

    outbind_init(&b);
    outbind_set_proto(&b, proto_named("SERVO PWM"));
    CHECK(outbind_toggle(&b, idx(0)));
    outbind_to_chan_cfg(&b, cc, 1100u, 1900u);
    CHECK_EQ(cc[0 * LINK_CC_STRIDE + LINK_CC_ROLE], LINK_CC_ROLE_SURFACE);
    CHECK_EQ(cc[0 * LINK_CC_STRIDE + LINK_CC_MIN_US], 1100);
    CHECK_EQ(cc[0 * LINK_CC_STRIDE + LINK_CC_MAX_US], 1900);
}

TEST_CASE(endpoints_a_servo_cannot_take_are_ignored_rather_than_written)
{
    outbind_t b;
    uint16_t cc[LINK_CC_COUNT];
    outbind_init(&b);
    outbind_set_proto(&b, proto_named("SERVO PWM"));
    CHECK(outbind_toggle(&b, idx(0)));

    /* The page refuses these with BAD_VALUE, so writing them would make the
     * whole write fail and take the roles down with it. */
    outbind_to_chan_cfg(&b, cc, 100u, 9000u);
    CHECK_EQ(cc[0 * LINK_CC_STRIDE + LINK_CC_MIN_US], LINK_CC_DEFAULT_MIN);
    CHECK_EQ(cc[0 * LINK_CC_STRIDE + LINK_CC_MAX_US], LINK_CC_DEFAULT_MAX);
    outbind_to_chan_cfg(&b, cc, 2000u, 1000u);      /* inverted */
    CHECK_EQ(cc[0 * LINK_CC_STRIDE + LINK_CC_MIN_US], LINK_CC_DEFAULT_MIN);
}

TEST_CASE(what_this_writes_is_what_the_bank_accepts)
{
    /* The page and the bank are the two halves that must agree: a selection
     * the screen allowed and the bank refuses is an output that reads back
     * as configured and never moves. */
    outbind_t b;
    outbind_init(&b);
    outbind_set_proto(&b, proto_named("SERVO PWM"));
    static const uint8_t gp[4] = { 0, 1, 20, 28 };
    for (unsigned i = 0; i < 4u; ++i) { CHECK(outbind_toggle(&b, idx(gp[i]))); }

    uint16_t slots[LINK_OS_COUNT];
    const uint8_t n = outbind_to_slots(&b, slots);

    outputs_t o;
    outputs_init(&o, 0u);
    outputs_reserve_pins(&o, outbind_reserved_mask());
    outputs_slots_apply(&o, slots);
    for (uint8_t s = 0; s < n; ++s) {
        if (o.slot[s].driver == OUT_DRIVER_NONE) {
            T_FAIL("slot %u was written and the bank refused it", s);
        }
        CHECK_EQ(o.slot[s].pin, gp[s]);
    }
}

/* ------------------------------------------------------- reading it back */

TEST_CASE(a_selection_survives_the_round_trip_through_the_page)
{
    /* The panel asks the coprocessor what its outputs are rather than
     * remembering: what comes back has to be what was sent. */
    static const char *const names[] = { "SERVO PWM", "PPM", "DSHOT300",
                                         "DSHOT600", "DSHOT300 BIDIR",
                                         "DSHOT600 BIDIR" };
    for (unsigned k = 0; k < sizeof(names) / sizeof(names[0]); ++k) {
        outbind_t a, back;
        outbind_init(&a);
        outbind_set_proto(&a, proto_named(names[k]));
        static const uint8_t gp[3] = { 0, 7, 22 };
        for (unsigned i = 0; i < 3u; ++i) { (void)outbind_toggle(&a, idx(gp[i])); }

        uint16_t regs[LINK_OS_COUNT];
        (void)outbind_to_slots(&a, regs);
        if (!outbind_from_slots(&back, regs)) {
            T_FAIL("%s did not read back at all", names[k]);
        }
        if (back.proto != a.proto || back.pins != a.pins) {
            T_FAIL("%s came back as proto %u pins %08lX, not %u / %08lX",
                   names[k], back.proto, (unsigned long)back.pins,
                   a.proto, (unsigned long)a.pins);
        }
    }
}

TEST_CASE(an_empty_page_reads_back_as_nothing_configured)
{
    uint16_t regs[LINK_OS_COUNT];
    outputs_slots_defaults(regs);
    outbind_t b;
    CHECK(outbind_from_slots(&b, regs));
    CHECK_EQ(b.proto, 0);
    CHECK_EQ(outbind_chosen(&b), 0);
}

TEST_CASE(a_page_this_screen_cannot_describe_is_refused_rather_than_guessed)
{
    uint16_t regs[LINK_OS_COUNT];
    outbind_t b;

    /* Two protocols at once: the screen holds one, and showing either would
     * be showing a bench that is not there. */
    outputs_slots_defaults(regs);
    regs[0 * LINK_OS_STRIDE + LINK_OS_DRIVER]  = LINK_DRIVER_PWM;
    regs[0 * LINK_OS_STRIDE + LINK_OS_PIN]     = 0;
    regs[0 * LINK_OS_STRIDE + LINK_OS_RATE_HZ] = 50;
    regs[1 * LINK_OS_STRIDE + LINK_OS_DRIVER]  = LINK_DRIVER_DSHOT;
    regs[1 * LINK_OS_STRIDE + LINK_OS_PIN]     = 1;
    regs[1 * LINK_OS_STRIDE + LINK_OS_RATE_HZ] = 600;
    CHECK(!outbind_from_slots(&b, regs));
    CHECK_EQ(outbind_chosen(&b), 0);

    /* A rate no entry offers. */
    outputs_slots_defaults(regs);
    regs[LINK_OS_DRIVER]  = LINK_DRIVER_PWM;
    regs[LINK_OS_PIN]     = 0;
    regs[LINK_OS_RATE_HZ] = 137;
    CHECK(!outbind_from_slots(&b, regs));

    /* A pin that is not on the header, so the screen has no cell for it. */
    outputs_slots_defaults(regs);
    regs[LINK_OS_DRIVER]  = LINK_DRIVER_PWM;
    regs[LINK_OS_PIN]     = 24;
    regs[LINK_OS_RATE_HZ] = 50;
    CHECK(!outbind_from_slots(&b, regs));
}

TEST_CASE(a_reserved_pin_on_the_page_is_refused_on_the_way_back)
{
    /*
     * The page stores what was written; the bank refuses a reserved pin only
     * at apply.  So a page can name GP10 with nothing driving it, and reading
     * that back as ticked would show a binding the bank never accepted -- and
     * one the screen would not let the operator tick again.
     */
    uint16_t regs[LINK_OS_COUNT];
    outputs_slots_defaults(regs);
    regs[LINK_OS_DRIVER]  = LINK_DRIVER_PWM;
    regs[LINK_OS_PIN]     = 10;               /* CAN SCK */
    regs[LINK_OS_RANGE]   = LINK_OS_RANGE_OF(0, 1);
    regs[LINK_OS_RATE_HZ] = 50;

    outbind_t b;
    CHECK(!outbind_from_slots(&b, regs));
    CHECK_EQ(outbind_chosen(&b), 0);
    CHECK_EQ(b.proto, 0);

    regs[LINK_OS_PIN] = 3;                    /* the heartbeat line */
    CHECK(!outbind_from_slots(&b, regs));
}

TEST_CASE(a_pin_wider_than_a_pin_is_refused_before_it_is_narrowed)
{
    /* The register is sixteen bits and a pin is eight.  Narrowed first,
     * 0x0100 becomes GP0, and a page naming a pin that cannot exist would
     * read back as a binding on the first pin of the header. */
    uint16_t regs[LINK_OS_COUNT];
    outputs_slots_defaults(regs);
    regs[LINK_OS_DRIVER]  = LINK_DRIVER_PWM;
    regs[LINK_OS_RANGE]   = LINK_OS_RANGE_OF(0, 1);
    regs[LINK_OS_RATE_HZ] = 50;

    outbind_t b;
    static const uint16_t bad[] = { 0x0100u, 0x0103u, 64u, 0xFFFFu };
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        regs[LINK_OS_PIN] = bad[i];
        if (outbind_from_slots(&b, regs)) {
            T_FAIL("pin 0x%04X was accepted, as %u pin(s)",
                   bad[i], outbind_chosen(&b));
        }
    }
    /* And the widest pin that is real still works. */
    regs[LINK_OS_PIN] = 28u;
    CHECK(outbind_from_slots(&b, regs));
    CHECK_EQ(outbind_chosen(&b), 1);
}

TEST_CASE(a_page_that_does_not_render_back_to_itself_is_refused)
{
    uint16_t regs[LINK_OS_COUNT];
    outbind_t b;

    /* PPM carries eight channels on its pin.  A slot claiming one is a page
     * this screen cannot show: it would draw PPM and mean something else. */
    outputs_slots_defaults(regs);
    regs[LINK_OS_DRIVER]  = LINK_DRIVER_PPM;
    regs[LINK_OS_PIN]     = 0;
    regs[LINK_OS_RANGE]   = LINK_OS_RANGE_OF(0, 1);
    regs[LINK_OS_RATE_HZ] = 50;
    CHECK(!outbind_from_slots(&b, regs));

    /* ... and with the right count it is fine. */
    regs[LINK_OS_RANGE] = LINK_OS_RANGE_OF(0, 8);
    CHECK(outbind_from_slots(&b, regs));
    CHECK_EQ(outbind_chosen(&b), 1);

    /* Two PPM slots: more pins than the protocol takes, so a binding the
     * operator could never have made and cannot reproduce. */
    regs[LINK_OS_STRIDE + LINK_OS_DRIVER]  = LINK_DRIVER_PPM;
    regs[LINK_OS_STRIDE + LINK_OS_PIN]     = 1;
    regs[LINK_OS_STRIDE + LINK_OS_RANGE]   = LINK_OS_RANGE_OF(8, 8);
    regs[LINK_OS_STRIDE + LINK_OS_RATE_HZ] = 50;
    CHECK(!outbind_from_slots(&b, regs));
    CHECK_EQ(outbind_chosen(&b), 0);

    /* A first-channel field that does not follow the slot order. */
    outputs_slots_defaults(regs);
    for (uint8_t k = 0; k < 2u; ++k) {
        uint16_t *r = &regs[(size_t)k * LINK_OS_STRIDE];
        r[LINK_OS_DRIVER]  = LINK_DRIVER_PWM;
        r[LINK_OS_PIN]     = k;
        r[LINK_OS_RATE_HZ] = 50;
        r[LINK_OS_RANGE]   = LINK_OS_RANGE_OF(k, 1);
    }
    CHECK(outbind_from_slots(&b, regs));
    regs[LINK_OS_STRIDE + LINK_OS_RANGE] = LINK_OS_RANGE_OF(5, 1);
    CHECK(!outbind_from_slots(&b, regs));
}

TEST_CASE(null_arguments_are_refused_rather_than_dereferenced)
{
    uint16_t regs[LINK_OS_COUNT];
    CHECK_EQ(outbind_chosen(NULL), 0);
    CHECK(!outbind_toggle(NULL, 0));
    CHECK(!outbind_can_add(NULL, 0));
    outbind_init(NULL);
    outbind_set_proto(NULL, 1);
    CHECK_EQ(outbind_to_slots(NULL, regs), 0);
    CHECK_EQ(outbind_to_slots(NULL, NULL), 0);
    outbind_to_chan_cfg(NULL, NULL, 1000u, 2000u);
    outbind_t b;
    CHECK(!outbind_from_slots(NULL, regs));
    CHECK(!outbind_from_slots(&b, NULL));
}

int main(void)
{
    RUN(the_catalogue_is_the_header_and_nothing_else);
    RUN(the_reserved_set_is_the_safety_line_and_the_can_bus);
    RUN(the_pad_numbers_match_the_ones_printed_on_the_board);
    RUN(a_reserved_pin_cannot_be_chosen);
    RUN(nothing_can_be_chosen_while_the_protocol_is_off);
    RUN(a_pin_can_always_be_taken_back);
    RUN(ppm_takes_one_pin_and_pwm_takes_the_bank);
    RUN(switching_protocol_drops_the_pins_that_no_longer_fit);
    RUN(each_pin_becomes_one_slot_in_pin_order);
    RUN(a_page_written_from_a_selection_carries_nothing_from_the_last_one);
    RUN(ppm_claims_the_whole_channel_range_on_its_one_pin);
    RUN(the_two_dshot_drivers_are_told_apart_on_the_wire);
    RUN(a_throttle_protocol_makes_throttles_and_a_pulse_one_makes_surfaces);
    RUN(endpoints_a_servo_cannot_take_are_ignored_rather_than_written);
    RUN(what_this_writes_is_what_the_bank_accepts);
    RUN(a_selection_survives_the_round_trip_through_the_page);
    RUN(an_empty_page_reads_back_as_nothing_configured);
    RUN(a_page_this_screen_cannot_describe_is_refused_rather_than_guessed);
    RUN(a_reserved_pin_on_the_page_is_refused_on_the_way_back);
    RUN(a_pin_wider_than_a_pin_is_refused_before_it_is_narrowed);
    RUN(a_page_that_does_not_render_back_to_itself_is_refused);
    RUN(null_arguments_are_refused_rather_than_dereferenced);
    return test_summary("outbind");
}
