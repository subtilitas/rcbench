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

#define BOARD ((uint16_t)OUTBIND_BOARD_PICO_HEADER)

static uint8_t idx(uint8_t gpio) { return outbind_index_of(BOARD, gpio); }

/* Every binding in this file is for the one board this build knows. */
static void init(outbind_t *b)
{
    outbind_init(b);
    outbind_set_board(b, BOARD);
}

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
    const outbind_pin_t *p = outbind_pins(BOARD);
    /* GP23 to GP25 exist in the part and not on the pads, so they cannot be
     * wired and are not offered. */
    CHECK_EQ(idx(23), outbind_pin_count(BOARD));
    CHECK_EQ(idx(24), outbind_pin_count(BOARD));
    CHECK_EQ(idx(25), outbind_pin_count(BOARD));
    CHECK_EQ(idx(29), outbind_pin_count(BOARD));
    CHECK(idx(0) < outbind_pin_count(BOARD));
    CHECK(idx(28) < outbind_pin_count(BOARD));

    /*
     * In GPIO order, so a list drawn from it reads the way a pinout does.
     *
     * Bounded by this board's own count, not by OUTBIND_PINS.  The two are
     * equal today, which is the only reason the maximum worked here -- and it
     * would read past the catalogue the first time a board had fewer pins.
     */
    for (uint8_t i = 1; i < outbind_pin_count(BOARD); ++i) {
        if (p[i].gpio <= p[i-1].gpio) {
            T_FAIL("entry %u (GP%u) does not follow GP%u",
                   i, p[i].gpio, p[i-1].gpio);
        }
    }
}

TEST_CASE(the_reserved_set_is_the_safety_line_and_the_can_bus)
{
    const uint64_t m = outbind_reserved_mask(BOARD);
    const uint64_t want = (1ull << 3) | (1ull << 8) | (1ull << 9)
                        | (1ull << 10) | (1ull << 11) | (1ull << 12);
    /* Pinned exactly: the coprocessor hands this to outputs_reserve_pins(),
     * so a pin that quietly leaves this set becomes an output on the
     * heartbeat line or the CAN bus. */
    CHECK_EQ(m, want);

    const outbind_pin_t *p = outbind_pins(BOARD);
    for (uint8_t i = 0; i < outbind_pin_count(BOARD); ++i) {
        const bool in = (m & (1ull << p[i].gpio)) != 0u;
        CHECK_EQ(in, p[i].reserved);
        /* A reserved pin says what has it, so the screen can too. */
        CHECK(!p[i].reserved || p[i].held_by != NULL);
    }
}

TEST_CASE(the_pad_numbers_match_the_ones_printed_on_the_board)
{
    /* The four corners of the header, which the silkscreen names. */
    CHECK_EQ(outbind_pins(BOARD)[idx(0)].pad, 1);
    CHECK_EQ(outbind_pins(BOARD)[idx(15)].pad, 20);
    CHECK_EQ(outbind_pins(BOARD)[idx(16)].pad, 21);
    CHECK_EQ(outbind_pins(BOARD)[idx(3)].pad, 5);
}

/* ------------------------------------------------------------- selecting */

TEST_CASE(a_reserved_pin_cannot_be_chosen)
{
    outbind_t b;
    init(&b);
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
    init(&b);
    CHECK(!outbind_can_add(&b, idx(0)));
    CHECK(!outbind_toggle(&b, idx(0)));
}

TEST_CASE(a_pin_can_always_be_taken_back)
{
    outbind_t b;
    init(&b);
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
    init(&b);
    outbind_set_proto(&b, proto_named("SERVO PWM"));
    static const uint8_t gp[9] = { 0,1,2,4,5,6,7,13,14 };
    for (unsigned i = 0; i < 8u; ++i) {
        CHECK(outbind_toggle(&b, idx(gp[i])));
    }
    CHECK_EQ(outbind_chosen(&b), 8);
    /* Eight slots is the whole bank; a ninth has nowhere to go. */
    CHECK(!outbind_toggle(&b, idx(gp[8])));

    init(&b);
    outbind_set_proto(&b, proto_named("PPM"));
    CHECK(outbind_toggle(&b, idx(0)));
    CHECK(!outbind_toggle(&b, idx(1)));
    CHECK_EQ(outbind_chosen(&b), 1);
}

TEST_CASE(switching_protocol_changes_which_set_is_edited)
{
    outbind_t b;
    init(&b);
    const uint8_t pwm = proto_named("SERVO PWM");
    const uint8_t dshot = proto_named("DSHOT600");

    outbind_set_proto(&b, pwm);
    for (uint8_t g = 0; g < 3u; ++g) {
        CHECK(outbind_toggle(&b, idx(g)));      /* GP0, GP1, GP2 */
    }
    CHECK_EQ(outbind_chosen(&b), 3);

    /*
     * The servo pins stay bound.  Binding a second protocol is not a way of
     * unbinding the first, so the set arrived at starts as it was left --
     * empty -- and the one left behind is still there to come back to.
     */
    outbind_set_proto(&b, dshot);
    CHECK_EQ(outbind_chosen(&b), 0);
    CHECK_EQ(outbind_chosen_total(&b), 3);
    for (uint8_t g = 0; g < 3u; ++g) {
        CHECK_EQ(outbind_group_of(&b, idx(g)), pwm);
    }

    /* A pin another protocol holds is that protocol's to give up: it is
     * refused here rather than taken away from it. */
    CHECK(!outbind_can_add(&b, idx(0)));
    CHECK(!outbind_toggle(&b, idx(0)));
    CHECK_EQ(outbind_group_of(&b, idx(0)), pwm);

    CHECK(outbind_toggle(&b, idx(4)));
    CHECK_EQ(outbind_chosen(&b), 1);
    CHECK_EQ(outbind_chosen_total(&b), 4);

    outbind_set_proto(&b, pwm);
    CHECK_EQ(outbind_chosen(&b), 3);            /* as they were left */
    CHECK_EQ(outbind_group_of(&b, idx(4)), dshot);
}

TEST_CASE(the_slots_and_the_channels_are_one_budget_for_every_protocol)
{
    /*
     * Eight slots and eight channels on the page, shared.  PPM renders all
     * eight channels on its single pin, so a bench with PPM on it has room
     * for nothing else -- and that is a refusal to add, not a pin quietly
     * dropped from a page that would not have driven it.
     */
    outbind_t b;
    init(&b);
    outbind_set_proto(&b, proto_named("PPM"));
    CHECK(outbind_toggle(&b, idx(0)));
    CHECK_EQ(outbind_channels_used(&b), LINK_OUT_CHANNELS);

    outbind_set_proto(&b, proto_named("SERVO PWM"));
    CHECK(!outbind_can_add(&b, idx(1)));
    CHECK(!outbind_toggle(&b, idx(1)));
    CHECK_EQ(outbind_chosen_total(&b), 1);

    /* With the PPM pin given back, the same pin is free again. */
    outbind_set_proto(&b, proto_named("PPM"));
    CHECK(outbind_toggle(&b, idx(0)));
    outbind_set_proto(&b, proto_named("SERVO PWM"));
    CHECK(outbind_toggle(&b, idx(1)));

    /* Eight single-channel pins fill both budgets at once. */
    static const uint8_t gp[7] = { 0, 2, 4, 5, 6, 7, 13 };
    for (unsigned i = 0; i < sizeof(gp) / sizeof(gp[0]); ++i) {
        CHECK(outbind_toggle(&b, idx(gp[i])));
    }
    CHECK_EQ(outbind_chosen_total(&b), LINK_OUT_SLOTS);
    CHECK_EQ(outbind_channels_used(&b), LINK_OUT_CHANNELS);
    CHECK(!outbind_can_add(&b, idx(14)));
}

/* ---------------------------------------------------------------- pages */

static uint16_t slot_reg(const uint16_t *regs, uint8_t slot, uint8_t field)
{
    return regs[(size_t)slot * LINK_OS_STRIDE + field];
}

TEST_CASE(each_pin_becomes_one_slot_in_pin_order)
{
    outbind_t b;
    init(&b);
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

    init(&b);
    outbind_set_proto(&b, proto_named("SERVO PWM"));
    for (uint8_t g = 0; g < 3u; ++g) { (void)outbind_toggle(&b, idx(g)); }
    CHECK_EQ(outbind_to_slots(&b, regs), 3);

    init(&b);
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
    init(&b);
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

    init(&b);
    outbind_set_proto(&b, proto_named("DSHOT600"));
    CHECK(outbind_toggle(&b, idx(4)));
    (void)outbind_to_slots(&b, regs);
    CHECK_EQ(slot_reg(regs, 0, LINK_OS_DRIVER), LINK_DRIVER_DSHOT);
    CHECK_EQ(slot_reg(regs, 0, LINK_OS_RATE_HZ), 600);

    /* Given back, then bound again under the other entry.  Choosing a
     * protocol says which set is being edited, so the pin has to leave the
     * first set before the second can have it. */
    CHECK(outbind_toggle(&b, idx(4)));
    outbind_set_proto(&b, proto_named("DSHOT300 BIDIR"));
    CHECK(outbind_toggle(&b, idx(4)));
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
    init(&b);
    outbind_set_proto(&b, proto_named("DSHOT600"));
    CHECK(outbind_toggle(&b, idx(0)));
    CHECK(outbind_toggle(&b, idx(1)));
    outbind_to_chan_cfg(&b, cc, 1000u, 2000u);
    CHECK_EQ(cc[0 * LINK_CC_STRIDE + LINK_CC_ROLE], LINK_CC_ROLE_THROTTLE);
    CHECK_EQ(cc[1 * LINK_CC_STRIDE + LINK_CC_ROLE], LINK_CC_ROLE_THROTTLE);
    /* An untouched channel keeps the schema's default. */
    CHECK_EQ(cc[2 * LINK_CC_STRIDE + LINK_CC_ROLE], LINK_CC_ROLE_SURFACE);

    init(&b);
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
    init(&b);
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
    init(&b);
    outbind_set_proto(&b, proto_named("SERVO PWM"));
    static const uint8_t gp[4] = { 0, 1, 20, 28 };
    for (unsigned i = 0; i < 4u; ++i) { CHECK(outbind_toggle(&b, idx(gp[i]))); }

    uint16_t slots[LINK_OS_COUNT];
    const uint8_t n = outbind_to_slots(&b, slots);

    outputs_t o;
    outputs_init(&o, 0u);
    outputs_reserve_pins(&o, outbind_reserved_mask(BOARD));
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
        init(&a);
        outbind_set_proto(&a, proto_named(names[k]));
        static const uint8_t gp[3] = { 0, 7, 22 };
        for (unsigned i = 0; i < 3u; ++i) { (void)outbind_toggle(&a, idx(gp[i])); }

        uint16_t regs[LINK_OS_COUNT];
        (void)outbind_to_slots(&a, regs);
        if (!outbind_from_slots(&back, BOARD, regs)) {
            T_FAIL("%s did not read back at all", names[k]);
        }
        if (back.proto != a.proto
            || memcmp(back.pins, a.pins, sizeof(a.pins)) != 0) {
            T_FAIL("%s came back as proto %u pins %08lX, not %u / %08lX",
                   names[k], back.proto, (unsigned long)back.pins[back.proto],
                   a.proto, (unsigned long)a.pins[a.proto]);
        }
    }
}

TEST_CASE(two_protocols_at_once_are_the_ordinary_case)
{
    outbind_t a, back;
    init(&a);
    const uint8_t pwm   = proto_named("SERVO PWM");
    const uint8_t dshot = proto_named("DSHOT600");

    /*
     * Servos on GP0 and GP7 with an ESC on GP4 between them.  The ESC's pin
     * sits inside the servos' range, so this page says slots follow pin
     * order across protocols rather than one protocol's pins and then the
     * next.
     */
    outbind_set_proto(&a, pwm);
    CHECK(outbind_toggle(&a, idx(0)));
    CHECK(outbind_toggle(&a, idx(7)));
    outbind_set_proto(&a, dshot);
    CHECK(outbind_toggle(&a, idx(4)));

    uint16_t regs[LINK_OS_COUNT];
    CHECK_EQ(outbind_to_slots(&a, regs), 3);
    CHECK_EQ(slot_reg(regs, 0, LINK_OS_PIN), 0);
    CHECK_EQ(slot_reg(regs, 0, LINK_OS_DRIVER), LINK_DRIVER_PWM);
    CHECK_EQ(slot_reg(regs, 1, LINK_OS_PIN), 4);
    CHECK_EQ(slot_reg(regs, 1, LINK_OS_DRIVER), LINK_DRIVER_DSHOT);
    CHECK_EQ(slot_reg(regs, 2, LINK_OS_PIN), 7);
    CHECK_EQ(slot_reg(regs, 2, LINK_OS_DRIVER), LINK_DRIVER_PWM);

    /* Channels count up with the pins, whichever protocol holds them. */
    for (uint8_t k = 0; k < 3u; ++k) {
        CHECK_EQ(LINK_OS_FIRST(slot_reg(regs, k, LINK_OS_RANGE)), k);
        CHECK_EQ(LINK_OS_CHANNELS(slot_reg(regs, k, LINK_OS_RANGE)), 1);
    }

    CHECK(outbind_from_slots(&back, BOARD, regs));
    CHECK_EQ(memcmp(back.pins, a.pins, sizeof(a.pins)), 0);
    /* Opened on the lowest-numbered protocol the page uses, so the choice
     * does not depend on which slot happened to come first. */
    CHECK_EQ(back.proto, pwm < dshot ? pwm : dshot);

    /*
     * The roles are mixed to match.  A throttle that centres is a motor at
     * half power, so the ESC's channel has to be a throttle while the servo
     * channels either side of it stay surfaces.
     */
    uint16_t cc[LINK_CC_COUNT];
    outbind_to_chan_cfg(&a, cc, 1000, 2000);
    CHECK_EQ(cc[0 * LINK_CC_STRIDE + LINK_CC_ROLE], LINK_CC_ROLE_SURFACE);
    CHECK_EQ(cc[1 * LINK_CC_STRIDE + LINK_CC_ROLE], LINK_CC_ROLE_THROTTLE);
    CHECK_EQ(cc[2 * LINK_CC_STRIDE + LINK_CC_ROLE], LINK_CC_ROLE_SURFACE);
}

TEST_CASE(an_empty_page_reads_back_as_nothing_configured)
{
    uint16_t regs[LINK_OS_COUNT];
    outputs_slots_defaults(regs);
    outbind_t b;
    CHECK(outbind_from_slots(&b, BOARD, regs));
    CHECK_EQ(b.proto, 0);
    CHECK_EQ(outbind_chosen(&b), 0);
}

TEST_CASE(a_page_this_screen_cannot_describe_is_refused_rather_than_guessed)
{
    uint16_t regs[LINK_OS_COUNT];
    outbind_t b;

    /* One pin, two slots: the page names GP0 twice, so no selection renders
     * back to it however the two are read. */
    outputs_slots_defaults(regs);
    regs[0 * LINK_OS_STRIDE + LINK_OS_DRIVER]  = LINK_DRIVER_PWM;
    regs[0 * LINK_OS_STRIDE + LINK_OS_PIN]     = 0;
    regs[0 * LINK_OS_STRIDE + LINK_OS_RANGE]   = LINK_OS_RANGE_OF(0, 1);
    regs[0 * LINK_OS_STRIDE + LINK_OS_RATE_HZ] = 50;
    regs[1 * LINK_OS_STRIDE + LINK_OS_DRIVER]  = LINK_DRIVER_DSHOT;
    regs[1 * LINK_OS_STRIDE + LINK_OS_PIN]     = 0;
    regs[1 * LINK_OS_STRIDE + LINK_OS_RANGE]   = LINK_OS_RANGE_OF(1, 1);
    regs[1 * LINK_OS_STRIDE + LINK_OS_RATE_HZ] = 600;
    CHECK(!outbind_from_slots(&b, BOARD, regs));
    CHECK_EQ(outbind_chosen_total(&b), 0);

    /* A rate no entry offers. */
    outputs_slots_defaults(regs);
    regs[LINK_OS_DRIVER]  = LINK_DRIVER_PWM;
    regs[LINK_OS_PIN]     = 0;
    regs[LINK_OS_RATE_HZ] = 137;
    CHECK(!outbind_from_slots(&b, BOARD, regs));

    /* A pin that is not on the header, so the screen has no cell for it. */
    outputs_slots_defaults(regs);
    regs[LINK_OS_DRIVER]  = LINK_DRIVER_PWM;
    regs[LINK_OS_PIN]     = 24;
    regs[LINK_OS_RATE_HZ] = 50;
    CHECK(!outbind_from_slots(&b, BOARD, regs));
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
    CHECK(!outbind_from_slots(&b, BOARD, regs));
    CHECK_EQ(outbind_chosen(&b), 0);
    CHECK_EQ(b.proto, 0);

    regs[LINK_OS_PIN] = 3;                    /* the heartbeat line */
    CHECK(!outbind_from_slots(&b, BOARD, regs));
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
        if (outbind_from_slots(&b, BOARD, regs)) {
            T_FAIL("pin 0x%04X was accepted, as %u pin(s)",
                   bad[i], outbind_chosen(&b));
        }
    }
    /* And the widest pin that is real still works. */
    regs[LINK_OS_PIN] = 28u;
    CHECK(outbind_from_slots(&b, BOARD, regs));
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
    CHECK(!outbind_from_slots(&b, BOARD, regs));

    /* ... and with the right count it is fine. */
    regs[LINK_OS_RANGE] = LINK_OS_RANGE_OF(0, 8);
    CHECK(outbind_from_slots(&b, BOARD, regs));
    CHECK_EQ(outbind_chosen(&b), 1);

    /* Two PPM slots: more pins than the protocol takes, so a binding the
     * operator could never have made and cannot reproduce. */
    regs[LINK_OS_STRIDE + LINK_OS_DRIVER]  = LINK_DRIVER_PPM;
    regs[LINK_OS_STRIDE + LINK_OS_PIN]     = 1;
    regs[LINK_OS_STRIDE + LINK_OS_RANGE]   = LINK_OS_RANGE_OF(8, 8);
    regs[LINK_OS_STRIDE + LINK_OS_RATE_HZ] = 50;
    CHECK(!outbind_from_slots(&b, BOARD, regs));
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
    CHECK(outbind_from_slots(&b, BOARD, regs));
    regs[LINK_OS_STRIDE + LINK_OS_RANGE] = LINK_OS_RANGE_OF(5, 1);
    CHECK(!outbind_from_slots(&b, BOARD, regs));
}

/* --------------------------------------------------------- more than one */

TEST_CASE(a_board_this_build_does_not_know_offers_nothing)
{
    /* Zero is both "nothing answered" and "answered with an identity this
     * build has never heard of", and the safe response to each is the same. */
    CHECK(outbind_board(OUTBIND_BOARD_UNKNOWN) == NULL);
    CHECK(outbind_board(9999) == NULL);
    CHECK(outbind_pins(9999) == NULL);
    CHECK_EQ(outbind_pin_count(9999), 0);

    outbind_t b;
    outbind_init(&b);
    CHECK_EQ(b.board, (uint16_t)OUTBIND_BOARD_UNKNOWN);
    /* Deliberately the widest catalogue any board has: an unknown board has
     * none, so every index including ones past the end must be refused
     * rather than reaching an array. */
    for (uint8_t i = 0; i < OUTBIND_PINS; ++i) {
        CHECK(!outbind_can_add(&b, i));
        CHECK(!outbind_toggle(&b, i));
    }
    outbind_set_proto(&b, 1);
    CHECK(!outbind_toggle(&b, 0));
    CHECK_EQ(outbind_chosen(&b), 0);

    uint16_t regs[LINK_OS_COUNT];
    CHECK_EQ(outbind_to_slots(&b, regs), 0);
}

TEST_CASE(an_unknown_board_reserves_every_pin_rather_than_none)
{
    /* The direction of the failure is the point.  Reserving everything is a
     * bench that will not drive; reserving nothing is an output on the
     * heartbeat line. */
    CHECK_EQ(outbind_reserved_mask(9999), ~(uint64_t)0u);
    CHECK_EQ(outbind_reserved_mask(OUTBIND_BOARD_UNKNOWN), ~(uint64_t)0u);
    CHECK(outbind_reserved_mask(BOARD) != ~(uint64_t)0u);
}

TEST_CASE(changing_board_drops_the_selection)
{
    /* A bit in `pins` is an index into one board's catalogue.  Carried
     * across, it would bind whatever happens to sit at that index on the
     * board actually connected -- a different pin, or none. */
    outbind_t b;
    init(&b);
    outbind_set_proto(&b, proto_named("SERVO PWM"));
    CHECK(outbind_toggle(&b, idx(0)));
    CHECK(outbind_toggle(&b, idx(5)));
    CHECK_EQ(outbind_chosen(&b), 2);

    outbind_set_board(&b, 9999);
    CHECK_EQ(outbind_chosen(&b), 0);
    CHECK_EQ(b.proto, 0);
    CHECK_EQ(b.board, 9999);

    /* Setting the board it already has is not a change and clears nothing. */
    init(&b);
    outbind_set_proto(&b, proto_named("SERVO PWM"));
    CHECK(outbind_toggle(&b, idx(0)));
    outbind_set_board(&b, BOARD);
    CHECK_EQ(outbind_chosen(&b), 1);
}

TEST_CASE(a_page_is_read_against_the_board_that_sent_it)
{
    outbind_t a, back;
    init(&a);
    outbind_set_proto(&a, proto_named("DSHOT600"));
    CHECK(outbind_toggle(&a, idx(7)));

    uint16_t regs[LINK_OS_COUNT];
    (void)outbind_to_slots(&a, regs);

    CHECK(outbind_from_slots(&back, BOARD, regs));
    CHECK_EQ(back.board, BOARD);
    CHECK_EQ(memcmp(back.pins, a.pins, sizeof(a.pins)), 0);

    /* The same registers against a board this build cannot map are refused,
     * not reinterpreted. */
    CHECK(!outbind_from_slots(&back, 9999, regs));
    CHECK_EQ(outbind_chosen(&back), 0);
    CHECK_EQ(back.board, 9999);
}

TEST_CASE(every_board_in_the_table_is_answerable)
{
    /* The table itself, not a guessed range of identities: a board added
     * later with an id outside such a range would be skipped by exactly the
     * test meant to hold it. */
    CHECK(outbind_board_count() >= 1);
    CHECK(outbind_board_at(outbind_board_count()) == NULL);
    for (uint8_t n = 0; n < outbind_board_count(); ++n) {
        const outbind_board_t *bd = outbind_board_at(n);
        const uint16_t id = (bd != NULL) ? bd->id : 0u;
        if (bd == NULL) { T_FAIL("board %u is not there", n); }
        /* and it is findable by the identity it reports */
        if (outbind_board(id) != bd) {
            T_FAIL("board %u is in the table and not findable by id %u",
                   n, id);
        }
        if (id == OUTBIND_BOARD_UNKNOWN) {
            T_FAIL("board %u claims the unknown identity", n);
        }
        if (bd->name == NULL || bd->pins == NULL || bd->count == 0u) {
            T_FAIL("board %u is in the table but describes nothing", id);
        }
        if (bd->count > OUTBIND_PINS) {
            T_FAIL("board %u has %u pins; OUTBIND_PINS is %u",
                   id, bd->count, (unsigned)OUTBIND_PINS);
        }
        /* The catalogue is in GPIO order and every reserved pin says what
         * holds it, on every board. */
        for (uint8_t i = 0; i < bd->count; ++i) {
            if (i > 0 && bd->pins[i].gpio <= bd->pins[i-1].gpio) {
                T_FAIL("board %u entry %u is out of order", id, i);
            }
            if (bd->pins[i].reserved && bd->pins[i].held_by == NULL) {
                T_FAIL("board %u reserves GP%u without saying what has it",
                       id, bd->pins[i].gpio);
            }
        }
    }
}

TEST_CASE(nothing_can_be_chosen_on_a_board_that_is_soldered)
{
    /*
     * No board in this build is soldered yet, so the rule is held against a
     * board built here rather than one in the table.  That is the whole
     * reason outbind_pin_selectable() takes the board: a rule nothing can
     * exercise is a rule nobody knows is broken.
     */
    outbind_board_t fixed = *outbind_board(BOARD);
    fixed.fixed = true;
    for (uint8_t i = 0; i < fixed.count; ++i) {
        if (outbind_pin_selectable(&fixed, i)) {
            T_FAIL("a soldered board still offered pin %u", i);
        }
    }

    /* The same board, not soldered, offers everything that is not reserved. */
    outbind_board_t open_b = *outbind_board(BOARD);
    open_b.fixed = false;
    uint8_t offered = 0;
    for (uint8_t i = 0; i < open_b.count; ++i) {
        if (outbind_pin_selectable(&open_b, i)) { ++offered; }
        CHECK_EQ(outbind_pin_selectable(&open_b, i), !open_b.pins[i].reserved);
    }
    CHECK_EQ(offered, 20);          /* 26 on the header, six of them taken */

    CHECK(!outbind_pin_selectable(NULL, 0));
    CHECK(!outbind_pin_selectable(&open_b, open_b.count));
}

TEST_CASE(a_soldered_board_refuses_both_directions)
{
    /*
     * Refusing only additions would let an operator untick an output that is
     * physically wired, and the page written from that would take away a
     * driver the board still has a connector for.  There is no soldered
     * board yet, so the rule is held where it is decided.
     */
    outbind_board_t fixed = *outbind_board(BOARD);
    fixed.fixed = true;
    for (uint8_t i = 0; i < fixed.count; ++i) {
        CHECK(!outbind_pin_selectable(&fixed, i));
    }

    /* And on a board that is not soldered, undoing still always works --
     * PPM is full at one pin and the operator must be able to change it. */
    outbind_t b;
    init(&b);
    outbind_set_proto(&b, proto_named("PPM"));
    CHECK(outbind_toggle(&b, idx(0)));
    CHECK(!outbind_can_add(&b, idx(1)));
    CHECK(outbind_toggle(&b, idx(0)));
    CHECK_EQ(outbind_chosen(&b), 0);
}

TEST_CASE(a_bit_above_the_board_cannot_survive)
{
    /*
     * outbind_t is a plain struct and the panel copies one off the wire, so
     * a bit above the board's width can arrive.  The trim walks the board's
     * own width, so anything above it would outlive every protocol change
     * and leave the count and the mask disagreeing.
     */
    outbind_t b;
    init(&b);
    const uint8_t pwm = proto_named("SERVO PWM");
    outbind_set_proto(&b, pwm);
    CHECK(outbind_toggle(&b, idx(0)));

    b.pins[pwm] |= (uint32_t)1u << 30;            /* no such pin on any board */
    outbind_set_proto(&b, proto_named("DSHOT600"));
    CHECK_EQ(b.pins[pwm] & ((uint32_t)1u << 30), 0u);
    CHECK_EQ(outbind_chosen_total(&b), 1);

    /* One pin cannot be two protocols'.  The lower-numbered one keeps it, so
     * what a struct from outside reads back as does not depend on the order
     * the sets happen to be walked in. */
    outbind_t d;
    init(&d);
    const uint8_t dshot = proto_named("DSHOT600");
    d.pins[pwm]   = (uint32_t)1u << idx(0);
    d.pins[dshot] = (uint32_t)1u << idx(0);
    outbind_trim(&d);
    CHECK_EQ(outbind_group_of(&d, idx(0)), pwm < dshot ? pwm : dshot);
    CHECK_EQ(outbind_chosen_total(&d), 1);

    /*
     * And a struct claiming more than the page holds is cut to what would be
     * written.  Eight PWM pins and one DShot pin is within what either
     * protocol takes on its own -- both cap at eight -- and one slot past
     * what the page has.  The ninth pin is the highest, so it is the one the
     * walk runs out of room for.
     */
    outbind_t f;
    init(&f);
    static const uint8_t free_gp[8] = { 0, 1, 2, 4, 5, 6, 7, 13 };
    f.pins[pwm] = 0u;
    for (unsigned i = 0; i < sizeof(free_gp) / sizeof(free_gp[0]); ++i) {
        f.pins[pwm] |= (uint32_t)1u << idx(free_gp[i]);
    }
    f.pins[dshot] = (uint32_t)1u << idx(14);
    outbind_trim(&f);
    CHECK_EQ(outbind_chosen_total(&f), LINK_OUT_SLOTS);
    CHECK_EQ(outbind_channels_used(&f), LINK_OUT_CHANNELS);
    CHECK_EQ(outbind_group_of(&f, idx(14)), 0);   /* no room, so not ticked */
    CHECK_EQ(outbind_group_of(&f, idx(13)), pwm);

    /* What survives is exactly what the page carries. */
    uint16_t fr[LINK_OS_COUNT];
    CHECK_EQ(outbind_to_slots(&f, fr), LINK_OUT_SLOTS);
    outbind_t fb;
    CHECK(outbind_from_slots(&fb, BOARD, fr));
    CHECK_EQ(memcmp(fb.pins, f.pins, sizeof(f.pins)), 0);

    /*
     * The channel budget stops the walk the same way.  PPM on the lowest pin
     * renders all eight channels, so a PWM pin above it has no channel to
     * render into and is not left ticked.
     */
    outbind_t q;
    init(&q);
    q.pins[proto_named("PPM")] = (uint32_t)1u << idx(0);
    q.pins[pwm]                = (uint32_t)1u << idx(1);
    outbind_trim(&q);
    CHECK_EQ(outbind_group_of(&q, idx(0)), proto_named("PPM"));
    CHECK_EQ(outbind_group_of(&q, idx(1)), 0);
    CHECK_EQ(outbind_channels_used(&q), LINK_OUT_CHANNELS);

    /* A reserved pin cannot arrive bound either. */
    outbind_t r;
    init(&r);
    r.pins[pwm] = (uint32_t)1u << idx(3);         /* GP3 is the heartbeat */
    outbind_trim(&r);
    CHECK_EQ(outbind_chosen_total(&r), 0);

    /* Nothing survives a protocol change on a board with no catalogue. */
    outbind_t u;
    outbind_init(&u);
    for (uint8_t g = 0; g < OUTBIND_PROTOS; ++g) {
        u.pins[g] = 0xFFFFFFFFu;
    }
    outbind_set_proto(&u, 1);
    CHECK_EQ(outbind_chosen_total(&u), 0);
}

/* ------------------------------------------------ a board that describes itself */

#define LEARNED ((uint16_t)4242)

TEST_CASE(a_board_can_describe_itself_and_read_back_the_same)
{
    /* The board this build knows, rendered to the page and learned under an
     * identity the build has never heard of: what comes back has to be the
     * same catalogue, because it is the same board. */
    uint16_t regs[LINK_CAT_COUNT];
    outbind_board_to_regs(outbind_board(BOARD), regs);

    CHECK(outbind_board(LEARNED) == NULL);      /* nothing knows it yet */
    CHECK(outbind_learn_board(LEARNED, regs));

    const outbind_board_t *got = outbind_board(LEARNED);
    if (got == NULL) { T_FAIL("a learned board is not answerable"); }
    CHECK_EQ(got->id, LEARNED);
    CHECK_EQ(got->count, outbind_pin_count(BOARD));
    CHECK(!got->fixed);

    const outbind_pin_t *mine = outbind_pins(BOARD);
    for (uint8_t i = 0; i < got->count; ++i) {
        if (got->pins[i].gpio != mine[i].gpio
            || got->pins[i].pad != mine[i].pad
            || got->pins[i].reserved != mine[i].reserved) {
            T_FAIL("pin %u came back as GP%u pad %u reserved %d",
                   i, got->pins[i].gpio, got->pins[i].pad,
                   (int)got->pins[i].reserved);
        }
        /* Reserved pins still say what holds them, in the group's words. */
        if (got->pins[i].reserved && got->pins[i].held_by == NULL) {
            T_FAIL("GP%u is reserved and says nothing holds it",
                   got->pins[i].gpio);
        }
    }

    /* And it behaves like a board: the reserved ones cannot be chosen. */
    outbind_t b;
    outbind_init(&b);
    outbind_set_board(&b, LEARNED);
    outbind_set_proto(&b, proto_named("SERVO PWM"));
    CHECK(outbind_toggle(&b, outbind_index_of(LEARNED, 0)));
    CHECK(!outbind_toggle(&b, outbind_index_of(LEARNED, 3)));   /* heartbeat */
    CHECK_EQ(outbind_chosen(&b), 1);

    outbind_forget_learned();
    CHECK(outbind_board(LEARNED) == NULL);
}

TEST_CASE(a_board_this_build_describes_is_not_the_wires_to_redescribe)
{
    /*
     * Otherwise a coprocessor could rename the pin holding the safety line,
     * or free it.  The compiled catalogue has been read by somebody and
     * cannot change under a running bench.
     */
    uint16_t regs[LINK_CAT_COUNT];
    for (unsigned i = 0; i < LINK_CAT_COUNT; ++i) { regs[i] = 0u; }
    regs[0] = LINK_CAT_OF(0, 1, LINK_PIN_FREE);
    regs[1] = LINK_CAT_OF(3, 5, LINK_PIN_FREE);   /* the heartbeat, freed */
    CHECK(!outbind_learn_board(BOARD, regs));

    /* The board is still the one this build describes. */
    const outbind_board_t *bd = outbind_board(BOARD);
    const uint8_t hb = outbind_index_of(BOARD, 3);
    CHECK(bd->pins[hb].reserved);
    CHECK(!outbind_pin_selectable(bd, hb));
}

TEST_CASE(a_catalogue_that_cannot_be_a_board_is_refused_whole)
{
    uint16_t regs[LINK_CAT_COUNT];

    /* Nothing brought out at all. */
    for (unsigned i = 0; i < LINK_CAT_COUNT; ++i) { regs[i] = 0u; }
    CHECK(!outbind_learn_board(LEARNED, regs));
    CHECK(outbind_board(LEARNED) == NULL);

    /* Out of GPIO order.  The catalogue is walked in order everywhere -- the
     * page is rendered from it and the trim walks it -- so a page that is not
     * ordered is not a catalogue. */
    for (unsigned i = 0; i < LINK_CAT_COUNT; ++i) { regs[i] = 0u; }
    regs[0] = LINK_CAT_OF(5, 1, LINK_PIN_FREE);
    regs[1] = LINK_CAT_OF(2, 2, LINK_PIN_FREE);
    CHECK(!outbind_learn_board(LEARNED, regs));
    CHECK(outbind_board(LEARNED) == NULL);

    /* The same GPIO twice is the same failure: not ascending. */
    for (unsigned i = 0; i < LINK_CAT_COUNT; ++i) { regs[i] = 0u; }
    regs[0] = LINK_CAT_OF(7, 1, LINK_PIN_FREE);
    regs[1] = LINK_CAT_OF(7, 2, LINK_PIN_FREE);
    CHECK(!outbind_learn_board(LEARNED, regs));

    /*
     * The widest GPIO the page can name is 63, because the field is six
     * bits, and OUT_MAX_PIN is 63 -- so no page can name a pin that is not
     * one, and it is learned like any other.
     */
    for (unsigned i = 0; i < LINK_CAT_COUNT; ++i) { regs[i] = 0u; }
    regs[0] = LINK_CAT_OF(OUT_MAX_PIN, 1, LINK_PIN_FREE);
    CHECK(outbind_learn_board(LEARNED, regs));
    CHECK_EQ(outbind_board(LEARNED)->pins[0].gpio, OUT_MAX_PIN);
    outbind_forget_learned();

    /* Learning nothing leaves nothing: a half-read catalogue is a pin map
     * that disagrees with the board that sent it. */
    CHECK(outbind_board(LEARNED) == NULL);

    /* Neither identity nor page is trusted to be there. */
    CHECK(!outbind_learn_board(LEARNED, NULL));
    CHECK(!outbind_learn_board((uint16_t)OUTBIND_BOARD_UNKNOWN, regs));
    CHECK(outbind_board((uint16_t)OUTBIND_BOARD_UNKNOWN) == NULL);
}

TEST_CASE(every_hold_the_page_can_carry_says_what_has_the_pin)
{
    /*
     * A pin an output may not have says so under its name, whichever group
     * holds it.  A code the panel has no word for still says the pin is
     * taken rather than saying nothing, because a pin that is greyed with no
     * reason reads as a bug in the screen.
     */
    static const uint8_t holds[] = {
        LINK_PIN_HEARTBEAT, LINK_PIN_CAN, LINK_PIN_FLASH,
        LINK_PIN_DEBUG, LINK_PIN_SENSOR, LINK_PIN_OTHER, 9u,
    };
    uint16_t regs[LINK_CAT_COUNT];
    for (unsigned i = 0; i < LINK_CAT_COUNT; ++i) { regs[i] = 0u; }
    for (unsigned i = 0; i < sizeof(holds) / sizeof(holds[0]); ++i) {
        regs[i] = LINK_CAT_OF(i, i + 1u, holds[i]);
    }
    /* And one free pin above them, so the board is not entirely reserved. */
    regs[sizeof(holds) / sizeof(holds[0])] =
        LINK_CAT_OF(20, 26, LINK_PIN_FREE);

    CHECK(outbind_learn_board(LEARNED, regs));
    const outbind_board_t *bd = outbind_board(LEARNED);
    for (unsigned i = 0; i < sizeof(holds) / sizeof(holds[0]); ++i) {
        if (!bd->pins[i].reserved) {
            T_FAIL("hold %u left GP%u selectable", holds[i], i);
        }
        if (bd->pins[i].held_by == NULL || bd->pins[i].held_by[0] == '\0') {
            T_FAIL("hold %u says nothing holds GP%u", holds[i], i);
        }
        CHECK(!outbind_pin_selectable(bd, (uint8_t)i));
    }
    /* The free one is still free. */
    CHECK(bd->pins[sizeof(holds) / sizeof(holds[0])].held_by == NULL);
    CHECK(outbind_pin_selectable(bd,
        (uint8_t)(sizeof(holds) / sizeof(holds[0]))));
    outbind_forget_learned();
}

TEST_CASE(rendering_a_catalogue_is_refused_rather_than_dereferenced)
{
    uint16_t regs[LINK_CAT_COUNT];
    for (unsigned i = 0; i < LINK_CAT_COUNT; ++i) { regs[i] = 0xFFFFu; }
    /* No board is not a crash, and the page it renders describes no pins. */
    outbind_board_to_regs(NULL, regs);
    for (unsigned i = 0; i < LINK_CAT_COUNT; ++i) {
        CHECK_EQ(LINK_CAT_PAD(regs[i]), 0);
    }
    outbind_board_to_regs(outbind_board(BOARD), NULL);   /* nor is no page */
}

TEST_CASE(a_pad_of_zero_ends_the_catalogue)
{
    /*
     * There is no count register.  One would have to live in the identity
     * page, and lengthening that page makes every older coprocessor refuse
     * the identity read and the link never come up.  Pads are numbered from
     * one, so zero is the free way to say there is no pin here.
     */
    uint16_t regs[LINK_CAT_COUNT];
    for (unsigned i = 0; i < LINK_CAT_COUNT; ++i) { regs[i] = 0u; }
    regs[0] = LINK_CAT_OF(0, 1, LINK_PIN_FREE);
    regs[1] = LINK_CAT_OF(1, 2, LINK_PIN_FREE);
    /* regs[2] is pad 0, and GP9 below it must not be reached. */
    regs[3] = LINK_CAT_OF(9, 12, LINK_PIN_FREE);
    CHECK(outbind_learn_board(LEARNED, regs));
    const outbind_board_t *got = outbind_board(LEARNED);
    CHECK_EQ(got->count, 2);
    CHECK_EQ(outbind_index_of(LEARNED, 9), got->count);   /* not on it */
    outbind_forget_learned();
}

TEST_CASE(null_arguments_are_refused_rather_than_dereferenced)
{
    uint16_t regs[LINK_OS_COUNT];
    CHECK_EQ(outbind_chosen(NULL), 0);
    CHECK(!outbind_toggle(NULL, 0));
    CHECK(!outbind_can_add(NULL, 0));
    outbind_init(NULL);
    outbind_set_board(NULL, 1);
    outbind_set_proto(NULL, 1);
    CHECK_EQ(outbind_to_slots(NULL, regs), 0);
    CHECK_EQ(outbind_to_slots(NULL, NULL), 0);
    outbind_to_chan_cfg(NULL, NULL, 1000u, 2000u);
    outbind_t b;
    CHECK(!outbind_from_slots(NULL, BOARD, regs));
    CHECK(!outbind_from_slots(&b, BOARD, NULL));
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
    RUN(switching_protocol_changes_which_set_is_edited);
    RUN(the_slots_and_the_channels_are_one_budget_for_every_protocol);
    RUN(each_pin_becomes_one_slot_in_pin_order);
    RUN(a_page_written_from_a_selection_carries_nothing_from_the_last_one);
    RUN(ppm_claims_the_whole_channel_range_on_its_one_pin);
    RUN(the_two_dshot_drivers_are_told_apart_on_the_wire);
    RUN(a_throttle_protocol_makes_throttles_and_a_pulse_one_makes_surfaces);
    RUN(endpoints_a_servo_cannot_take_are_ignored_rather_than_written);
    RUN(what_this_writes_is_what_the_bank_accepts);
    RUN(a_selection_survives_the_round_trip_through_the_page);
    RUN(two_protocols_at_once_are_the_ordinary_case);
    RUN(an_empty_page_reads_back_as_nothing_configured);
    RUN(a_page_this_screen_cannot_describe_is_refused_rather_than_guessed);
    RUN(a_reserved_pin_on_the_page_is_refused_on_the_way_back);
    RUN(a_pin_wider_than_a_pin_is_refused_before_it_is_narrowed);
    RUN(a_page_that_does_not_render_back_to_itself_is_refused);
    RUN(a_board_this_build_does_not_know_offers_nothing);
    RUN(an_unknown_board_reserves_every_pin_rather_than_none);
    RUN(changing_board_drops_the_selection);
    RUN(a_page_is_read_against_the_board_that_sent_it);
    RUN(every_board_in_the_table_is_answerable);
    RUN(nothing_can_be_chosen_on_a_board_that_is_soldered);
    RUN(a_soldered_board_refuses_both_directions);
    RUN(a_bit_above_the_board_cannot_survive);
    RUN(a_board_can_describe_itself_and_read_back_the_same);
    RUN(a_board_this_build_describes_is_not_the_wires_to_redescribe);
    RUN(a_catalogue_that_cannot_be_a_board_is_refused_whole);
    RUN(every_hold_the_page_can_carry_says_what_has_the_pin);
    RUN(rendering_a_catalogue_is_refused_rather_than_dereferenced);
    RUN(a_pad_of_zero_ends_the_catalogue);
    RUN(null_arguments_are_refused_rather_than_dereferenced);
    return test_summary("outbind");
}
