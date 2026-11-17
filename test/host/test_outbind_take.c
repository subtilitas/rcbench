/*
 * Moving one pin from the protocol that holds it to another
 * (shared/outputs/out_bind_take.c).
 *
 * Under test: a move keeps every slot and every channel number and changes
 * the moved slot's driver, rate and role only; the last pin of a protocol
 * leaves it empty; a PPM pin frees its 8 channels and PPM takes a pin only
 * when nothing else stays bound; a taker at its cap, a page without a slot
 * and the PWM rules the taker meets refuse the move and name why; a
 * programming port's pin, an interface's pin, a reserved, free or own pin,
 * OFF, a soldered board without roles and an unread binding move nothing;
 * a full bench still moves a pin; moving the pin back restores the binding
 * bit for bit for every pair of the 7 protocols on every pin of the
 * RP2350-CAN; the binding after a move survives outbind_trim(); NULL and
 * out-of-range arguments change nothing.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "link_pages.h"
#include "out_bind.h"
#include "out_bind_take.h"
#include "out_pwm_map.h"

#define PICO    ((uint16_t)OUTBIND_BOARD_PICO_HEADER)
#define CARRIER ((uint16_t)OUTBIND_BOARD_RBMOD)

/* The protocol table's order (the_protocol_table_is_the_one_named_here). */
enum { P_OFF, P_SERVO, P_MOTOR, P_PPM, P_D300, P_D600, P_D300B, P_D600B };

/* A binding that was read, with no port and no interface. */
static const outbind_take_ctx_t k_read = { .read = true };

static uint8_t idx(uint16_t board, uint8_t gpio)
{
    return outbind_index_of(board, gpio);
}

static void init(outbind_t *b, uint16_t board)
{
    outbind_init(b);
    outbind_set_board(b, board);
}

/* Protocol @p proto takes GPIO @p gpio the way a tap does. */
static void hold(outbind_t *b, uint8_t proto, uint8_t gpio)
{
    b->proto = proto;
    if (!outbind_toggle(b, idx(b->board, gpio))) {
        T_FAIL("protocol %u cannot take GP%u", proto, gpio);
    }
}

static bool same_pins(const outbind_t *a, const outbind_t *b)
{
    return a->board == b->board
           && memcmp(a->pins, b->pins, sizeof(a->pins)) == 0;
}

static outbind_take_t take(const outbind_t *b, uint8_t gpio, uint8_t taker,
                           outbind_t *after, outbind_take_info_t *info)
{
    return outbind_take_check(b, idx(b->board, gpio), taker, &k_read, after,
                              info);
}

/* The OUTPUTS and CHAN_CFG pages @p b writes. */
typedef struct {
    uint16_t os[LINK_OS_COUNT];
    uint16_t cc[LINK_CC_COUNT];
} pages_t;

static void pages_of(const outbind_t *b, pages_t *p)
{
    (void)outbind_to_slots(b, p->os);
    outbind_to_chan_cfg(b, p->cc, 1000u, 2000u);
}

static uint16_t os_at(const pages_t *p, uint8_t slot, unsigned reg)
{
    return p->os[(size_t)slot * LINK_OS_STRIDE + reg];
}

static uint16_t role_at(const pages_t *p, uint8_t ch)
{
    return p->cc[(size_t)ch * LINK_CC_STRIDE + LINK_CC_ROLE];
}

/* The slot that drives @p gpio, or LINK_OUT_SLOTS. */
static uint8_t slot_of(const pages_t *p, uint8_t gpio)
{
    for (uint8_t s = 0; s < LINK_OUT_SLOTS; ++s) {
        if (os_at(p, s, LINK_OS_DRIVER) != (uint16_t)LINK_DRIVER_NONE
            && os_at(p, s, LINK_OS_PIN) == gpio) {
            return s;
        }
    }
    return LINK_OUT_SLOTS;
}

/* ------------------------------------------------------------- the move */

TEST_CASE(the_protocol_table_is_the_one_named_here)
{
    static const char *const names[OUTBIND_PROTOS] = {
        "OFF", "SERVO PWM", "MOTOR PWM", "PPM", "DSHOT300", "DSHOT600",
        "DSHOT300 BIDIR", "DSHOT600 BIDIR",
    };
    for (uint8_t i = 0; i < OUTBIND_PROTOS; ++i) {
        CHECK_STR_EQ(outbind_protos()[i].name, names[i]);
    }
}

/*
 * DSHOT600 gives GP6 to SERVO PWM.  Every slot keeps its pin and its
 * channels; the moved slot changes driver and rate and nothing else does.
 */
TEST_CASE(a_pin_of_another_protocol_moves_and_keeps_every_channel_number)
{
    outbind_t b, after;
    init(&b, PICO);
    hold(&b, P_SERVO, 0u);
    hold(&b, P_SERVO, 1u);
    hold(&b, P_D600, 6u);
    hold(&b, P_D600, 7u);
    hold(&b, P_SERVO, 14u);
    const outbind_t was = b;
    outbind_take_info_t in;
    CHECK_EQ(take(&b, 6u, P_SERVO, &after, &in), OUTBIND_TAKE_OK);
    CHECK(same_pins(&b, &was));                       /* the input stays */

    CHECK_EQ(after.proto, P_SERVO);
    CHECK_EQ(after.pins[P_D600], 1u << idx(PICO, 7u));
    CHECK_EQ(after.pins[P_SERVO], was.pins[P_SERVO] | (1u << idx(PICO, 6u)));
    CHECK_EQ(in.gpio, 6u);
    CHECK_EQ(in.pad, 9u);
    CHECK_EQ(in.loser, P_D600);
    CHECK_EQ(in.taker, P_SERVO);
    CHECK_EQ(in.keeps, 1u);
    CHECK_EQ(in.channel, 2u);                   /* GP0 0, GP1 1, GP6 2 */
    CHECK_EQ(in.with, OUTBIND_NO_GPIO);

    pages_t pb, pa;
    pages_of(&b, &pb);
    pages_of(&after, &pa);
    const uint8_t moved = slot_of(&pb, 6u);
    CHECK_EQ(moved, 2u);
    for (uint8_t s = 0; s < LINK_OUT_SLOTS; ++s) {
        CHECK_EQ(os_at(&pa, s, LINK_OS_PIN), os_at(&pb, s, LINK_OS_PIN));
        CHECK_EQ(os_at(&pa, s, LINK_OS_RANGE), os_at(&pb, s, LINK_OS_RANGE));
        if (s != moved) {
            CHECK_EQ(os_at(&pa, s, LINK_OS_DRIVER),
                     os_at(&pb, s, LINK_OS_DRIVER));
            CHECK_EQ(os_at(&pa, s, LINK_OS_RATE_HZ),
                     os_at(&pb, s, LINK_OS_RATE_HZ));
        }
    }
    CHECK_EQ(os_at(&pb, moved, LINK_OS_DRIVER), (uint16_t)LINK_DRIVER_DSHOT);
    CHECK_EQ(os_at(&pa, moved, LINK_OS_DRIVER), (uint16_t)LINK_DRIVER_PWM);
    CHECK_EQ(os_at(&pb, moved, LINK_OS_RATE_HZ), 600u);
    CHECK_EQ(os_at(&pa, moved, LINK_OS_RATE_HZ), 50u);
}

/*
 * The moved channel takes the taker's role and every other channel keeps
 * its own: an ESC pin to a servo protocol, a servo pin to MOTOR PWM (same
 * driver, same rate), and DSHOT600 to DSHOT300 (no role change at all).
 */
TEST_CASE(the_moved_channel_changes_role_and_no_other_does)
{
    outbind_t b, after;
    init(&b, PICO);
    hold(&b, P_SERVO, 0u);
    hold(&b, P_D600, 6u);
    hold(&b, P_D600, 7u);
    pages_t pb, pa;
    pages_of(&b, &pb);
    outbind_take_info_t in;

    CHECK_EQ(take(&b, 7u, P_SERVO, &after, &in), OUTBIND_TAKE_OK);
    CHECK_EQ(in.role_was, OUT_ROLE_THROTTLE);
    CHECK_EQ(in.role_now, OUT_ROLE_SURFACE);
    pages_of(&after, &pa);
    for (uint8_t ch = 0; ch < LINK_OUT_CHANNELS; ++ch) {
        if (ch == in.channel) {
            CHECK_EQ(role_at(&pb, ch), (uint16_t)LINK_CC_ROLE_THROTTLE);
            CHECK_EQ(role_at(&pa, ch), (uint16_t)LINK_CC_ROLE_SURFACE);
        } else {
            CHECK_EQ(role_at(&pa, ch), role_at(&pb, ch));
        }
    }

    /* SERVO PWM to MOTOR PWM: the slots page is the same, the role is not. */
    CHECK_EQ(take(&b, 0u, P_MOTOR, &after, &in), OUTBIND_TAKE_OK);
    CHECK_EQ(in.role_was, OUT_ROLE_SURFACE);
    CHECK_EQ(in.role_now, OUT_ROLE_THROTTLE);
    pages_of(&after, &pa);
    CHECK_EQ(memcmp(pa.os, pb.os, sizeof(pa.os)), 0);
    CHECK_EQ(role_at(&pa, 0u), (uint16_t)LINK_CC_ROLE_THROTTLE);
    for (uint8_t ch = 1; ch < LINK_OUT_CHANNELS; ++ch) {
        CHECK_EQ(role_at(&pa, ch), role_at(&pb, ch));
    }

    /* DSHOT600 to DSHOT300: the rate moves, no role does. */
    CHECK_EQ(take(&b, 6u, P_D300, &after, &in), OUTBIND_TAKE_OK);
    CHECK_EQ(in.role_was, in.role_now);
    pages_of(&after, &pa);
    CHECK_EQ(memcmp(pa.cc, pb.cc, sizeof(pa.cc)), 0);
    CHECK_EQ(os_at(&pa, in.channel, LINK_OS_RATE_HZ), 300u);
}

/* The holder's last pin: it holds none after the move, and no slot of the
 * pages renders a throttle any more. */
TEST_CASE(the_last_pin_leaves_its_protocol_empty)
{
    outbind_t b, after;
    init(&b, PICO);
    hold(&b, P_SERVO, 0u);
    hold(&b, P_D600B, 6u);
    outbind_take_info_t in;
    CHECK_EQ(take(&b, 6u, P_SERVO, &after, &in), OUTBIND_TAKE_OK);
    CHECK_EQ(in.keeps, 0u);
    CHECK_EQ(after.pins[P_D600B], 0u);
    CHECK_EQ(outbind_chosen_total(&after), 2u);
    pages_t pa;
    pages_of(&after, &pa);
    CHECK_EQ(outbind_role_proto(pa.os, pa.cc, OUT_ROLE_THROTTLE), 0u);
    CHECK_EQ(outbind_role_proto(pa.os, pa.cc, OUT_ROLE_SURFACE), P_SERVO);
}

/* PPM's one pin renders 8 channels: moved to SERVO PWM it renders 1. */
TEST_CASE(a_ppm_pin_moves_and_frees_eight_channels)
{
    outbind_t b, after;
    init(&b, PICO);
    hold(&b, P_PPM, 2u);
    CHECK_EQ(outbind_channels_used(&b), 8u);
    outbind_take_info_t in;
    CHECK_EQ(take(&b, 2u, P_SERVO, &after, &in), OUTBIND_TAKE_OK);
    CHECK_EQ(in.free, 8u);
    CHECK_EQ(in.need, 1u);
    CHECK_EQ(in.keeps, 0u);
    CHECK_EQ(in.channel, 0u);
    CHECK_EQ(outbind_channels_used(&after), 1u);
    pages_t pa;
    pages_of(&after, &pa);
    CHECK_EQ(os_at(&pa, 0u, LINK_OS_RANGE), LINK_OS_RANGE_OF(0u, 1u));
    CHECK_EQ(os_at(&pa, 0u, LINK_OS_DRIVER), (uint16_t)LINK_DRIVER_PWM);
    CHECK_EQ(os_at(&pa, 1u, LINK_OS_DRIVER), (uint16_t)LINK_DRIVER_NONE);
}

/* PPM needs every channel: refused while another pin stays bound, taken
 * when the pin it takes is the last one bound. */
TEST_CASE(ppm_takes_nothing_while_another_pin_stays_bound)
{
    outbind_t b, after;
    init(&b, PICO);
    hold(&b, P_SERVO, 0u);
    hold(&b, P_D600, 6u);
    outbind_take_info_t in;
    CHECK_EQ(take(&b, 6u, P_PPM, &after, &in), OUTBIND_TAKE_CHANNELS);
    CHECK_EQ(in.need, 8u);
    CHECK_EQ(in.free, 7u);
    CHECK_EQ(in.channel, OUTBIND_TAKE_UNSET);
    CHECK(same_pins(&after, &b));
    CHECK_EQ(after.proto, b.proto);

    init(&b, PICO);
    hold(&b, P_D600, 6u);
    CHECK_EQ(take(&b, 6u, P_PPM, &after, &in), OUTBIND_TAKE_OK);
    CHECK_EQ(outbind_channels_used(&after), 8u);
    CHECK_EQ(in.cap, 1u);
}

/*
 * The cap and the slots: no binding outbind_trim() passed reaches either
 * (8 pins of one protocol leave no other pin to take), so a binding past
 * the page's budgets stands in for one.
 */
TEST_CASE(a_taker_at_its_cap_is_refused)
{
    outbind_t b, after;
    outbind_take_info_t in;
    init(&b, PICO);
    static const uint8_t servo[8] = { 0u, 1u, 2u, 4u, 5u, 6u, 7u, 13u };
    for (unsigned i = 0; i < 8u; ++i) {
        b.pins[P_SERVO] |= 1u << idx(PICO, servo[i]);
    }
    b.pins[P_D600] = 1u << idx(PICO, 14u);
    CHECK_EQ(take(&b, 14u, P_SERVO, &after, &in), OUTBIND_TAKE_CAP);
    CHECK_EQ(in.cap, 8u);
    CHECK(same_pins(&after, &b));

    /* PPM's cap is its one pin. */
    init(&b, PICO);
    b.pins[P_PPM] = 1u << idx(PICO, 2u);
    b.pins[P_D600] = 1u << idx(PICO, 6u);
    CHECK_EQ(take(&b, 6u, P_PPM, &after, &in), OUTBIND_TAKE_CAP);
    CHECK_EQ(in.cap, 1u);

    /* Under the taker's cap and 9 pins bound: no slot for the taker. */
    init(&b, PICO);
    for (unsigned i = 0; i < 4u; ++i) {
        b.pins[P_SERVO] |= 1u << idx(PICO, servo[i]);
    }
    for (unsigned i = 4u; i < 8u; ++i) {
        b.pins[P_D600] |= 1u << idx(PICO, servo[i]);
    }
    b.pins[P_D600] |= 1u << idx(PICO, 14u);
    CHECK_EQ(take(&b, 14u, P_SERVO, &after, &in), OUTBIND_TAKE_SLOTS);
    CHECK(same_pins(&after, &b));
}

/* The carrier's four servo outputs cap every taker at 4, PPM at 1. */
TEST_CASE(the_carrier_caps_a_taker_at_four_outputs)
{
    outbind_t b, after;
    outbind_take_info_t in;
    init(&b, CARRIER);
    hold(&b, P_SERVO, 2u);                          /* PWM01 */
    hold(&b, P_SERVO, 4u);                          /* PWM02 */
    hold(&b, P_SERVO, 1u);                          /* PWM04 */
    hold(&b, P_D600, 5u);                           /* PWM03 */
    CHECK_EQ(take(&b, 5u, P_SERVO, &after, &in), OUTBIND_TAKE_OK);
    CHECK_EQ(in.cap, 4u);
    CHECK_EQ(in.keeps, 0u);
    CHECK_EQ(in.channel, 2u);                       /* PWM03: slot 2 */
    CHECK_EQ(outbind_chosen(&after), 4u);

    /* And back: DSHOT600 takes PWM03 from a SERVO PWM at its cap. */
    outbind_t back;
    CHECK_EQ(take(&after, 5u, P_D600, &back, &in), OUTBIND_TAKE_OK);
    CHECK(same_pins(&back, &b));

    CHECK_EQ(take(&b, 5u, P_PPM, &after, &in), OUTBIND_TAKE_CHANNELS);
    CHECK_EQ(in.cap, 1u);
}

/* GP16 shares GP0's compare register (slice 0, channel A): no PWM taker,
 * and DShot is under no PWM rule. */
TEST_CASE(a_pwm_taker_on_a_bound_compare_register_is_refused_with_the_pin)
{
    outbind_t b, after;
    outbind_take_info_t in;
    init(&b, PICO);
    hold(&b, P_SERVO, 0u);
    hold(&b, P_D600, 16u);
    CHECK(out_pwm_same_compare(0u, 16u));
    CHECK_EQ(take(&b, 16u, P_MOTOR, &after, &in),
             OUTBIND_TAKE_CLASH_COMPARE);
    CHECK_EQ(in.with, 0u);
    CHECK(same_pins(&after, &b));
    CHECK_EQ(take(&b, 16u, P_SERVO, &after, &in),
             OUTBIND_TAKE_CLASH_COMPARE);
    CHECK_EQ(in.with, 0u);
    CHECK_EQ(take(&b, 16u, P_D300, &after, &in), OUTBIND_TAKE_OK);
    CHECK_EQ(in.with, OUTBIND_NO_GPIO);
}

/* GP28 is on slice 6 with a SERVO PWM port on GP13; a DShot port there is
 * under no PWM rule. */
TEST_CASE(a_pwm_taker_on_a_ports_slice_is_refused_with_the_port_pin)
{
    outbind_t b, after;
    outbind_take_info_t in;
    init(&b, PICO);
    hold(&b, P_D600, 28u);
    const uint8_t ports[LINK_PG_PORTS_MAX] = { 0xFFu, 13u, 0xFFu, 0xFFu };
    const uint8_t dshot[LINK_PG_PORTS_MAX] = { 0xFFu, 0xFFu, 0xFFu, 0xFFu };
    outbind_take_ctx_t ctx = { .read = true, .ports = ports,
                               .pwm_ports = ports,
                               .port_count = LINK_PG_PORTS_MAX };
    CHECK(out_pwm_same_slice(13u, 28u));
    CHECK_EQ(outbind_take_check(&b, idx(PICO, 28u), P_SERVO, &ctx, &after,
                                &in),
             OUTBIND_TAKE_CLASH_PROG_SLICE);
    CHECK_EQ(in.with, 13u);
    CHECK(same_pins(&after, &b));

    ctx.pwm_ports = dshot;
    CHECK_EQ(outbind_take_check(&b, idx(PICO, 28u), P_SERVO, &ctx, &after,
                                &in),
             OUTBIND_TAKE_OK);
}

/* PWM02 (GP4) and PWM03 (GP5) share slice 2: a surface beside a throttle
 * there is refused, a second throttle is not. */
TEST_CASE(the_carrier_refuses_a_surface_beside_a_throttle_on_one_slice)
{
    outbind_t b, after;
    outbind_take_info_t in;
    init(&b, CARRIER);
    hold(&b, P_MOTOR, 4u);
    hold(&b, P_D600, 5u);
    CHECK_EQ(take(&b, 5u, P_SERVO, &after, &in),
             OUTBIND_TAKE_CLASH_OUT_SLICE);
    CHECK_EQ(in.with, 4u);
    CHECK(same_pins(&after, &b));
    CHECK_EQ(take(&b, 5u, P_MOTOR, &after, &in), OUTBIND_TAKE_OK);
}

/* None of these is a move: a plain tap or nothing handles them. */
TEST_CASE(a_free_reserved_own_or_off_pin_is_no_move)
{
    outbind_t b, after;
    outbind_take_info_t in;
    init(&b, PICO);
    hold(&b, P_SERVO, 0u);
    hold(&b, P_D600, 6u);
    CHECK_EQ(take(&b, 14u, P_SERVO, &after, &in), OUTBIND_TAKE_NOT_HELD);
    CHECK_EQ(in.loser, 0u);
    CHECK_EQ(take(&b, 3u, P_SERVO, &after, &in), OUTBIND_TAKE_RESERVED);
    CHECK_EQ(in.gpio, 3u);
    CHECK_EQ(take(&b, 10u, P_D600, &after, &in), OUTBIND_TAKE_RESERVED);
    CHECK_EQ(take(&b, 6u, P_D600, &after, &in), OUTBIND_TAKE_SAME);
    CHECK_EQ(take(&b, 6u, P_OFF, &after, &in), OUTBIND_TAKE_OFF);
    CHECK_EQ(in.loser, P_D600);
    CHECK(same_pins(&after, &b));
}

/* A port's or an interface's pin is refused with its holder named; a pin a
 * protocol holds is that protocol's whatever a port is set to. */
TEST_CASE(a_port_or_interface_pin_is_refused_with_its_holder)
{
    outbind_t b, after;
    outbind_take_info_t in;
    init(&b, PICO);
    hold(&b, P_D600, 6u);
    hold(&b, P_SERVO, 14u);
    const uint8_t ports[LINK_PG_PORTS_MAX] = { 13u, 0xFFu, 6u, 0xFFu };
    const uint64_t ifaces[4] = { 0u, (1ull << 16) | (1ull << 17),
                                 (1ull << 18) | (1ull << 19), 1ull << 22 };
    const outbind_take_ctx_t ctx = { .read = true, .ports = ports,
                                     .port_count = LINK_PG_PORTS_MAX,
                                     .ifaces = ifaces, .iface_count = 4u };
    CHECK_EQ(outbind_take_check(&b, idx(PICO, 13u), P_SERVO, &ctx, &after,
                                &in),
             OUTBIND_TAKE_PORT);
    CHECK_EQ(in.port, 0u);
    CHECK_EQ(in.gpio, 13u);
    CHECK(same_pins(&after, &b));
    static const struct { uint8_t gpio, iface; } at[] = {
        { 16u, 1u }, { 17u, 1u }, { 18u, 2u }, { 19u, 2u }, { 22u, 3u },
    };
    for (unsigned i = 0; i < sizeof(at) / sizeof(at[0]); ++i) {
        CHECK_EQ(outbind_take_check(&b, idx(PICO, at[i].gpio), P_D600, &ctx,
                                    &after, &in),
                 OUTBIND_TAKE_IFACE);
        CHECK_EQ(in.iface, at[i].iface);
        CHECK_EQ(in.port, OUTBIND_TAKE_UNSET);
    }
    /* Port 2 names GP6, which DSHOT600 drives: the IOmcu gives the port no
     * pin, and the pin moves like any other. */
    CHECK_EQ(outbind_take_check(&b, idx(PICO, 6u), P_SERVO, &ctx, &after,
                                &in),
             OUTBIND_TAKE_OK);
    CHECK_EQ(in.port, OUTBIND_TAKE_UNSET);
    /* So does a pin an interface is set to and a protocol drives. */
    const uint64_t on14[4] = { 0u, 1ull << 14, 0u, 0u };
    const outbind_take_ctx_t c14 = { .read = true, .ifaces = on14,
                                     .iface_count = 4u };
    CHECK_EQ(outbind_take_check(&b, idx(PICO, 14u), P_D600, &c14, &after,
                                &in),
             OUTBIND_TAKE_OK);
    /* No ports and no interfaces given: a free pin is just free. */
    CHECK_EQ(take(&b, 13u, P_SERVO, &after, &in), OUTBIND_TAKE_NOT_HELD);
    CHECK_EQ(take(&b, 16u, P_SERVO, &after, &in), OUTBIND_TAKE_NOT_HELD);
}

/* A board soldered without roles offers nothing to move; the carrier, with
 * roles, moves its outputs. */
TEST_CASE(a_soldered_board_without_roles_moves_nothing)
{
    outbind_t b, after;
    outbind_take_info_t in;
    init(&b, PICO);
    hold(&b, P_SERVO, 0u);
    hold(&b, P_D600, 6u);
    outbind_board_t fixed = *outbind_board(PICO);
    fixed.fixed = true;
    CHECK_EQ(outbind_take_check_on(&fixed, &b, idx(PICO, 6u), P_SERVO,
                                   &k_read, &after, &in),
             OUTBIND_TAKE_FIXED);
    CHECK(same_pins(&after, &b));
    CHECK_EQ(outbind_take_check_on(outbind_board(PICO), &b, idx(PICO, 6u),
                                   P_SERVO, &k_read, &after, &in),
             OUTBIND_TAKE_OK);
    CHECK(outbind_board(CARRIER)->fixed);
    init(&b, CARRIER);
    hold(&b, P_D600, 2u);
    CHECK_EQ(take(&b, 2u, P_SERVO, &after, &in), OUTBIND_TAKE_OK);
}

/* Nothing moves on a binding no read has confirmed, and an unfilled
 * context is one. The pin's holder is still named. */
TEST_CASE(a_binding_not_read_yet_moves_nothing)
{
    outbind_t b, after;
    outbind_take_info_t in;
    init(&b, PICO);
    hold(&b, P_D600, 6u);
    const outbind_take_ctx_t unread = { 0 };
    CHECK_EQ(outbind_take_check(&b, idx(PICO, 6u), P_SERVO, &unread, &after,
                                &in),
             OUTBIND_TAKE_UNREAD);
    CHECK_EQ(in.loser, P_D600);
    CHECK_EQ(in.gpio, 6u);
    CHECK(same_pins(&after, &b));
    CHECK_EQ(outbind_take_check(&b, idx(PICO, 6u), P_SERVO, NULL, &after,
                                &in),
             OUTBIND_TAKE_UNREAD);
    /* Before any reserved, port or interface reason. */
    CHECK_EQ(outbind_take_check(&b, idx(PICO, 3u), P_SERVO, &unread, &after,
                                &in),
             OUTBIND_TAKE_UNREAD);
}

/* Eight slots in use: the move changes one slot's protocol and adds none. */
TEST_CASE(a_full_bench_still_moves_a_pin)
{
    outbind_t b, after;
    outbind_take_info_t in;
    init(&b, PICO);
    static const uint8_t servo[5] = { 0u, 1u, 2u, 4u, 5u };
    for (unsigned i = 0; i < 5u; ++i) {
        hold(&b, P_SERVO, servo[i]);
    }
    hold(&b, P_D600, 6u);
    hold(&b, P_D600, 7u);
    hold(&b, P_D600, 13u);
    CHECK_EQ(outbind_chosen_total(&b), 8u);
    /* Nothing is free to tick, and the moves still go. */
    b.proto = P_SERVO;
    CHECK(!outbind_can_add(&b, idx(PICO, 14u)));
    CHECK_EQ(take(&b, 13u, P_SERVO, &after, &in), OUTBIND_TAKE_OK);
    CHECK_EQ(outbind_chosen_total(&after), 8u);
    CHECK_EQ(in.channel, 7u);
    CHECK_EQ(take(&b, 0u, P_D600, &after, &in), OUTBIND_TAKE_OK);
    CHECK_EQ(outbind_chosen_total(&after), 8u);
    CHECK_EQ(in.channel, 0u);
}

/* ------------------------------------------------------------ the sweep */

/*
 * The binding a sweep step starts from: @p loser holds pin @p i and, when
 * neither protocol is PPM, one more pin that shares no compare register
 * with @p i, and a DShot protocol that is neither holds a third pin.  Every
 * move from it is possible.  Returns false when the board has no such pins.
 */
static bool sweep_start(outbind_t *b, uint8_t i, uint8_t loser,
                        uint8_t taker)
{
    const outbind_pin_t *pins = outbind_pins(PICO);
    init(b, PICO);
    b->proto = loser;
    if (!outbind_toggle(b, i)) {
        return false;
    }
    if (loser == P_PPM || taker == P_PPM) {
        return true;
    }
    uint8_t other = P_D300;
    while (other == loser || other == taker) {
        ++other;
    }
    bool second = false, third = false;
    for (uint8_t k = 0; k < outbind_pin_count(PICO); ++k) {
        if (k == i || pins[k].reserved) {
            continue;
        }
        if (!second && !out_pwm_same_compare(pins[k].gpio, pins[i].gpio)) {
            b->proto = loser;
            second = outbind_toggle(b, k);
        } else if (!third) {
            b->proto = other;
            third = outbind_toggle(b, k);
        }
    }
    return second && third;
}

/*
 * Every pair of the 7 protocols on every pin of the RP2350-CAN: the pin
 * moves, and moving it back gives the binding and its pages bit for bit.
 * Every slot keeps its pin and its first channel through both moves.
 */
TEST_CASE(moving_a_pin_back_restores_the_binding_bit_for_bit)
{
    const outbind_pin_t *pins = outbind_pins(PICO);
    unsigned moves = 0u;
    for (uint8_t i = 0; i < outbind_pin_count(PICO); ++i) {
        if (pins[i].reserved) {
            continue;
        }
        for (uint8_t l = 1; l < OUTBIND_PROTOS; ++l) {
            for (uint8_t t = 1; t < OUTBIND_PROTOS; ++t) {
                outbind_t b, after, back;
                if (t == l) {
                    continue;
                }
                if (!sweep_start(&b, i, l, t)) {
                    T_FAIL("no start for GP%u, %u to %u", pins[i].gpio, l, t);
                    continue;
                }
                outbind_take_info_t in;
                const outbind_take_t v =
                    outbind_take_check(&b, i, t, &k_read, &after, &in);
                if (v != OUTBIND_TAKE_OK) {
                    T_FAIL("GP%u %u to %u: verdict %d", pins[i].gpio, l, t,
                           (int)v);
                    continue;
                }
                pages_t pb, pa, pr;
                pages_of(&b, &pb);
                pages_of(&after, &pa);
                for (uint8_t s = 0; s < LINK_OUT_SLOTS; ++s) {
                    CHECK_EQ(os_at(&pa, s, LINK_OS_PIN),
                             os_at(&pb, s, LINK_OS_PIN));
                    CHECK_EQ(LINK_OS_FIRST(os_at(&pa, s, LINK_OS_RANGE)),
                             LINK_OS_FIRST(os_at(&pb, s, LINK_OS_RANGE)));
                }
                CHECK_EQ(in.channel, LINK_OS_FIRST(os_at(
                    &pb, slot_of(&pb, pins[i].gpio), LINK_OS_RANGE)));
                CHECK_EQ(outbind_take_check(&after, i, l, &k_read, &back,
                                            NULL),
                         OUTBIND_TAKE_OK);
                CHECK(same_pins(&back, &b));
                pages_of(&back, &pr);
                CHECK_EQ(memcmp(pr.os, pb.os, sizeof(pr.os)), 0);
                CHECK_EQ(memcmp(pr.cc, pb.cc, sizeof(pr.cc)), 0);
                ++moves;
            }
        }
    }
    /* 20 pins the IOmcu does not reserve, 42 ordered pairs. */
    CHECK_EQ(moves, 20u * 42u);
}

/* What a move writes is a binding outbind_trim() leaves as it is: no pin
 * past a cap, a slot or a channel, so the page carries every pin. */
TEST_CASE(the_binding_after_survives_trim_unchanged)
{
    const outbind_pin_t *pins = outbind_pins(PICO);
    for (uint8_t i = 0; i < outbind_pin_count(PICO); ++i) {
        for (uint8_t l = 1; !pins[i].reserved && l < OUTBIND_PROTOS; ++l) {
            for (uint8_t t = 1; t < OUTBIND_PROTOS; ++t) {
                outbind_t b, after;
                if (t == l || !sweep_start(&b, i, l, t)
                    || outbind_take_check(&b, i, t, &k_read, &after, NULL)
                           != OUTBIND_TAKE_OK) {
                    continue;
                }
                outbind_t trimmed = after;
                outbind_trim(&trimmed);
                CHECK(same_pins(&trimmed, &after));
                CHECK_EQ(trimmed.proto, after.proto);
                /* And the taker could have ticked it, had nobody held it. */
                outbind_t free_pin = b;
                free_pin.pins[l] &= ~(1u << i);
                free_pin.proto = t;
                CHECK(outbind_can_add(&free_pin, i));
            }
        }
    }
}

/* NULL, a board this build does not know, a pin past the catalogue and a
 * taker past the table: refused, nothing written past the copy. */
TEST_CASE(null_arguments_change_nothing)
{
    outbind_t b, after;
    outbind_take_info_t in;
    init(&b, PICO);
    hold(&b, P_D600, 6u);
    memset(&after, 0xA5, sizeof(after));
    const outbind_t sentinel = after;
    CHECK_EQ(outbind_take_check(NULL, 0u, P_SERVO, &k_read, &after, &in),
             OUTBIND_TAKE_INVALID);
    CHECK_EQ(memcmp(&after, &sentinel, sizeof(after)), 0);
    CHECK_EQ(in.gpio, OUTBIND_NO_GPIO);
    CHECK_EQ(in.loser, 0u);
    CHECK_EQ(outbind_take_check_on(NULL, &b, idx(PICO, 6u), P_SERVO,
                                   &k_read, &after, &in),
             OUTBIND_TAKE_INVALID);
    CHECK(same_pins(&after, &b));
    CHECK_EQ(outbind_take_check(&b, outbind_pin_count(PICO), P_SERVO,
                                &k_read, &after, &in),
             OUTBIND_TAKE_INVALID);
    CHECK_EQ(outbind_take_check(&b, idx(PICO, 6u), OUTBIND_PROTOS, &k_read,
                                &after, &in),
             OUTBIND_TAKE_INVALID);
    CHECK_EQ(in.gpio, OUTBIND_NO_GPIO);
    outbind_t unknown;
    outbind_init(&unknown);
    CHECK_EQ(outbind_take_check(&unknown, 0u, P_SERVO, &k_read, &after, &in),
             OUTBIND_TAKE_INVALID);
    /* No after and no info: the verdict alone. */
    CHECK_EQ(outbind_take_check(&b, idx(PICO, 6u), P_SERVO, &k_read, NULL,
                                NULL),
             OUTBIND_TAKE_OK);
    CHECK_EQ(b.pins[P_D600], 1u << idx(PICO, 6u));
}

int main(void)
{
    RUN(the_protocol_table_is_the_one_named_here);
    RUN(a_pin_of_another_protocol_moves_and_keeps_every_channel_number);
    RUN(the_moved_channel_changes_role_and_no_other_does);
    RUN(the_last_pin_leaves_its_protocol_empty);
    RUN(a_ppm_pin_moves_and_frees_eight_channels);
    RUN(ppm_takes_nothing_while_another_pin_stays_bound);
    RUN(a_taker_at_its_cap_is_refused);
    RUN(the_carrier_caps_a_taker_at_four_outputs);
    RUN(a_pwm_taker_on_a_bound_compare_register_is_refused_with_the_pin);
    RUN(a_pwm_taker_on_a_ports_slice_is_refused_with_the_port_pin);
    RUN(the_carrier_refuses_a_surface_beside_a_throttle_on_one_slice);
    RUN(a_free_reserved_own_or_off_pin_is_no_move);
    RUN(a_port_or_interface_pin_is_refused_with_its_holder);
    RUN(a_soldered_board_without_roles_moves_nothing);
    RUN(a_binding_not_read_yet_moves_nothing);
    RUN(a_full_bench_still_moves_a_pin);
    RUN(moving_a_pin_back_restores_the_binding_bit_for_bit);
    RUN(the_binding_after_survives_trim_unchanged);
    RUN(null_arguments_change_nothing);
    return test_summary("outbind_take");
}
