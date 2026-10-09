/*
 * The TONE link page at the coprocessor (shared/outputs/tone_page.c).
 *
 * Under test: the set-up as a page starts; every value held to its range
 * at both ends, whether the tap is enabled or not; the combinations the
 * detector refuses (a gap shorter than the lowest tone's period); the pin
 * refused past the bank, on a reserved pin, an output's, SENSE's or
 * SUPPLY's, and on an ADC pin, while enabled; the pin reserved from the
 * outputs while the tap runs; a refused write storing nothing; read-only
 * registers and the page end; the 64-beep ring read without consuming,
 * numbered from 1 to 65535 and round again, and a beep that has left it;
 * a new capture emptying it; core 1's status taken only under the set-up
 * and the capture in force; the counters; the order built from the page;
 * the set-up kept in flash restored through the page's own checks.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "link_msg.h"
#include "link_pages.h"
#include "outputs.h"
#include "tone_page.h"

static outputs_t    o;
static tone_page_t  pg;

#define BIT(pin) ((uint64_t)1u << (pin))

/* A bank with the heartbeat on GP3 reserved and a PWM output on GP4. */
static void fresh(void)
{
    outputs_init(&o, 0u);
    outputs_reserve_pins(&o, BIT(3));
    const out_slot_t pwm = { .driver = OUT_DRIVER_PWM, .first_channel = 0,
                             .channels = 1, .pin = 4, .rate_hz = 50 };
    CHECK(outputs_configure(&o, 0, &pwm));
    tone_page_init(&pg);
}

static uint16_t reg(unsigned i)
{
    uint16_t v = 0xFFFFu;
    tone_page_read(&pg, (uint8_t)i, 1u, &v);
    return v;
}

/* The set-up's seven registers, written whole. */
static uint8_t setup(uint16_t en, uint16_t pin, uint16_t fmin, uint16_t fmax,
                     uint16_t split, uint16_t gap, uint16_t periods,
                     uint64_t taken)
{
    const uint16_t r[7] = { en, pin, fmin, fmax, split, gap, periods };
    return tone_page_write(&pg, LINK_TN_ENABLE, 7u, r, &o, taken);
}

static uint8_t on(uint16_t pin)
{
    return setup(1u, pin, 400u, 6500u, 8u, 3u, 3u, 0u);
}

static tone_status_t status(uint16_t gen, uint16_t cap)
{
    tone_status_t st;
    memset(&st, 0, sizeof(st));
    st.gen = gen;
    st.cap_gen = cap;
    st.running = true;
    return st;
}

static tone_rec_t rec(uint32_t start_ms, uint16_t freq)
{
    tone_rec_t r;
    memset(&r, 0, sizeof(r));
    r.start_ms = start_ms;
    r.len_dms = 1234u;
    r.freq_dhz = freq;
    r.bursts = 40u;
    r.carrier_hhz = 240u;
    r.flags = LINK_TN_EVT_AFTER_CHANGE;
    return r;
}

/* Beeps with numbers from the next to come, @p n of them, frequencies
 * counting up from 1000 dHz. */
static void give(unsigned n)
{
    tone_rec_t r[8];
    tone_status_t st = status(pg.gen, pg.cap_gen);
    while (n != 0u) {
        const unsigned k = n < 8u ? n : 8u;
        for (unsigned i = 0; i < k; ++i) {
            r[i] = rec((uint32_t)((pg.n + i + 1u) * 10u),
                       (uint16_t)(1000u + pg.n + i));
        }
        tone_page_publish(&pg, &st, r, k);
        n -= k;
    }
}

TEST_CASE(a_page_starts_with_the_tap_off_on_gp22)
{
    fresh();
    CHECK_EQ(reg(LINK_TN_ENABLE), 0u);
    CHECK_EQ(reg(LINK_TN_PIN), 22u);
    CHECK_EQ(reg(LINK_TN_F_MIN_HZ), 400u);
    CHECK_EQ(reg(LINK_TN_F_MAX_HZ), 6500u);
    CHECK_EQ(reg(LINK_TN_SPLIT_PCT), 8u);
    CHECK_EQ(reg(LINK_TN_GAP_MS), 3u);
    CHECK_EQ(reg(LINK_TN_MIN_PERIODS), 3u);
    CHECK_EQ(reg(LINK_TN_RESERVED_7), 0u);
    for (unsigned i = LINK_TN_FLAGS; i < LINK_TN_COUNT; ++i) {
        CHECK_EQ(reg(i), 0u);
    }
    CHECK(!tone_page_enabled(&pg));
    CHECK_EQ(tone_page_pin(&pg), 22u);
    CHECK_EQ(tone_page_pins(&pg), 0u);
    uint16_t cfg[LINK_TN_CONFIG_COUNT];
    tone_page_defaults(cfg);
    CHECK_EQ(cfg[LINK_TN_PIN], 22u);
    tone_page_defaults(NULL);
}

TEST_CASE(a_set_up_is_taken_whole_and_read_back)
{
    fresh();
    CHECK_EQ(setup(1u, 22u, 500u, 5000u, 10u, 5u, 4u, 0u), 0u);
    CHECK_EQ(reg(LINK_TN_ENABLE), 1u);
    CHECK_EQ(reg(LINK_TN_F_MIN_HZ), 500u);
    CHECK_EQ(reg(LINK_TN_F_MAX_HZ), 5000u);
    CHECK_EQ(reg(LINK_TN_SPLIT_PCT), 10u);
    CHECK_EQ(reg(LINK_TN_GAP_MS), 5u);
    CHECK_EQ(reg(LINK_TN_MIN_PERIODS), 4u);
    CHECK(tone_page_enabled(&pg));
    CHECK_EQ(tone_page_pins(&pg), BIT(22));
    /* A frame at a time: the second of two. */
    const uint16_t f2[4] = { 20u, 7u, 3u, 0u };
    CHECK_EQ(tone_page_write(&pg, LINK_TN_SPLIT_PCT, 4u, f2, &o, 0u), 0u);
    CHECK_EQ(reg(LINK_TN_SPLIT_PCT), 20u);
    CHECK_EQ(reg(LINK_TN_GAP_MS), 7u);
    CHECK_EQ(reg(LINK_TN_MIN_PERIODS), 3u);
}

TEST_CASE(every_value_is_held_to_its_range_at_both_ends)
{
    fresh();
    CHECK_EQ(setup(0u, 22u, 49u, 6500u, 8u, 3u, 3u, 0u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(setup(0u, 22u, 50u, 6500u, 8u, 20u, 3u, 0u), 0u);
    CHECK_EQ(setup(0u, 22u, 2000u, 6500u, 8u, 3u, 3u, 0u), 0u);
    CHECK_EQ(setup(0u, 22u, 2001u, 6500u, 8u, 3u, 3u, 0u),
             LINK_NACK_BAD_VALUE);
    /* F_MAX above F_MIN, to 6900. */
    CHECK_EQ(setup(0u, 22u, 400u, 400u, 8u, 3u, 3u, 0u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(setup(0u, 22u, 400u, 401u, 8u, 3u, 3u, 0u), 0u);
    CHECK_EQ(setup(0u, 22u, 400u, 6900u, 8u, 3u, 3u, 0u), 0u);
    CHECK_EQ(setup(0u, 22u, 400u, 6901u, 8u, 3u, 3u, 0u),
             LINK_NACK_BAD_VALUE);
    CHECK_EQ(setup(0u, 22u, 400u, 6500u, 0u, 3u, 3u, 0u), 0u);
    CHECK_EQ(setup(0u, 22u, 400u, 6500u, 50u, 3u, 3u, 0u), 0u);
    CHECK_EQ(setup(0u, 22u, 400u, 6500u, 51u, 3u, 3u, 0u),
             LINK_NACK_BAD_VALUE);
    CHECK_EQ(setup(0u, 22u, 400u, 6500u, 8u, 0u, 3u, 0u),
             LINK_NACK_BAD_VALUE);
    CHECK_EQ(setup(0u, 22u, 2000u, 6500u, 8u, 1u, 3u, 0u), 0u);
    CHECK_EQ(setup(0u, 22u, 400u, 6500u, 8u, 100u, 3u, 0u), 0u);
    CHECK_EQ(setup(0u, 22u, 400u, 6500u, 8u, 101u, 3u, 0u),
             LINK_NACK_BAD_VALUE);
    CHECK_EQ(setup(0u, 22u, 400u, 6500u, 8u, 3u, 0u, 0u),
             LINK_NACK_BAD_VALUE);
    CHECK_EQ(setup(0u, 22u, 400u, 6500u, 8u, 3u, 1u, 0u), 0u);
    CHECK_EQ(setup(0u, 22u, 400u, 6500u, 8u, 3u, 64u, 0u), 0u);
    CHECK_EQ(setup(0u, 22u, 400u, 6500u, 8u, 3u, 65u, 0u),
             LINK_NACK_BAD_VALUE);
    CHECK_EQ(setup(2u, 22u, 400u, 6500u, 8u, 3u, 3u, 0u),
             LINK_NACK_BAD_VALUE);
    CHECK_EQ(setup(0u, 64u, 400u, 6500u, 8u, 3u, 3u, 0u),
             LINK_NACK_BAD_VALUE);
    CHECK_EQ(setup(0u, 63u, 400u, 6500u, 8u, 3u, 3u, 0u), 0u);
    const uint16_t r7 = 1u;
    CHECK_EQ(tone_page_write(&pg, LINK_TN_RESERVED_7, 1u, &r7, &o, 0u),
             LINK_NACK_BAD_VALUE);
    const uint16_t z = 0u;
    CHECK_EQ(tone_page_write(&pg, LINK_TN_RESERVED_7, 1u, &z, &o, 0u), 0u);
}

TEST_CASE(a_gap_shorter_than_the_lowest_tones_period_is_refused)
{
    fresh();
    /* 50 Hz is 20 ms a period: the gap is at least that. */
    CHECK_EQ(setup(0u, 22u, 50u, 6500u, 8u, 19u, 3u, 0u),
             LINK_NACK_BAD_VALUE);
    CHECK_EQ(setup(0u, 22u, 50u, 6500u, 8u, 20u, 3u, 0u), 0u);
    /* 400 Hz is 2.5 ms: 2 ms is short, 3 ms is not. */
    CHECK_EQ(setup(0u, 22u, 400u, 6500u, 8u, 2u, 3u, 0u),
             LINK_NACK_BAD_VALUE);
    CHECK_EQ(setup(0u, 22u, 400u, 6500u, 8u, 3u, 3u, 0u), 0u);
    /* Raising F_MIN alone is refused when it leaves the gap short, and
     * stores nothing. */
    CHECK_EQ(setup(0u, 22u, 100u, 6500u, 8u, 10u, 3u, 0u), 0u);
    const uint16_t lo = 50u;
    CHECK_EQ(tone_page_write(&pg, LINK_TN_F_MIN_HZ, 1u, &lo, &o, 0u),
             LINK_NACK_BAD_VALUE);
    CHECK_EQ(reg(LINK_TN_F_MIN_HZ), 100u);
}

TEST_CASE(the_pin_is_refused_where_something_else_holds_it)
{
    fresh();
    /* Past the bank is refused disabled or not. */
    CHECK_EQ(on(64u), LINK_NACK_BAD_VALUE);
    /* The heartbeat's reservation, an output's pin. */
    CHECK_EQ(on(3u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(on(4u), LINK_NACK_BAD_VALUE);
    /* SENSE's and SUPPLY's, as the caller names them. */
    CHECK_EQ(setup(1u, 16u, 400u, 6500u, 8u, 3u, 3u, BIT(16) | BIT(17)),
             LINK_NACK_BAD_VALUE);
    CHECK_EQ(setup(1u, 17u, 400u, 6500u, 8u, 3u, 3u, BIT(16) | BIT(17)),
             LINK_NACK_BAD_VALUE);
    CHECK_EQ(setup(1u, 18u, 400u, 6500u, 8u, 3u, 3u, BIT(16) | BIT(17)), 0u);
    /* The ADC pins of the RP2350A and of the RP2354B. */
    for (unsigned pin = 0; pin <= 63u; ++pin) {
        fresh();
        const bool adc = (pin >= 26u && pin <= 29u) || (pin >= 40u && pin <= 47u);
        const uint8_t want = (pin == 3u || pin == 4u || adc)
                                 ? LINK_NACK_BAD_VALUE : 0u;
        const uint8_t got = on((uint16_t)pin);
        if (got != want) {
            T_FAIL("pin %u: got %u, want %u", pin, got, want);
        }
    }
    /* Disabled, the same pins are only held to the bank. */
    fresh();
    CHECK_EQ(setup(0u, 27u, 400u, 6500u, 8u, 3u, 3u, 0u), 0u);
    CHECK_EQ(setup(0u, 4u, 400u, 6500u, 8u, 3u, 3u, 0u), 0u);
    /* ... and enabling that set-up is the refusal. */
    const uint16_t en = 1u;
    CHECK_EQ(tone_page_write(&pg, LINK_TN_ENABLE, 1u, &en, &o, 0u),
             LINK_NACK_BAD_VALUE);
    CHECK_EQ(reg(LINK_TN_ENABLE), 0u);
}

TEST_CASE(a_refused_write_stores_none_of_its_registers)
{
    fresh();
    CHECK_EQ(on(22u), 0u);
    uint16_t before[LINK_TN_CONFIG_COUNT];
    tone_page_read(&pg, 0u, LINK_TN_CONFIG_COUNT, before);
    const uint16_t gen = pg.gen;
    CHECK_EQ(setup(1u, 22u, 600u, 5000u, 8u, 3u, 99u, 0u),
             LINK_NACK_BAD_VALUE);
    CHECK_EQ(setup(1u, 3u, 600u, 5000u, 8u, 3u, 3u, 0u), LINK_NACK_BAD_VALUE);
    uint16_t after[LINK_TN_CONFIG_COUNT];
    tone_page_read(&pg, 0u, LINK_TN_CONFIG_COUNT, after);
    CHECK_EQ(memcmp(before, after, sizeof(before)), 0);
    CHECK_EQ(pg.gen, gen);
}

TEST_CASE(the_pin_is_the_taps_while_it_runs_and_no_slot_binds_it)
{
    fresh();
    CHECK_EQ(on(22u), 0u);
    CHECK_EQ(tone_page_pins(&pg), BIT(22));
    /* The bank holds it reserved; the tap may write the same pin again. */
    outputs_reserve_pins(&o, BIT(3) | tone_page_pins(&pg));
    CHECK_EQ(on(22u), 0u);
    CHECK_EQ(setup(1u, 22u, 500u, 6500u, 8u, 3u, 3u, 0u), 0u);
    /* But an output may not take it. */
    uint16_t slots[LINK_OS_COUNT];
    memset(slots, 0, sizeof(slots));
    CHECK_EQ(tone_page_slots_check(&pg, slots), 0u);
    slots[LINK_OS_STRIDE * 2 + LINK_OS_DRIVER] = LINK_DRIVER_PWM;
    slots[LINK_OS_STRIDE * 2 + LINK_OS_PIN] = 21u;
    CHECK_EQ(tone_page_slots_check(&pg, slots), 0u);
    slots[LINK_OS_STRIDE * 2 + LINK_OS_PIN] = 22u;
    CHECK_EQ(tone_page_slots_check(&pg, slots), LINK_NACK_BAD_VALUE);
    /* Disabled, the pin is free again. */
    const uint16_t off = 0u;
    CHECK_EQ(tone_page_write(&pg, LINK_TN_ENABLE, 1u, &off, &o, 0u), 0u);
    CHECK_EQ(tone_page_pins(&pg), 0u);
    CHECK_EQ(tone_page_slots_check(&pg, slots), 0u);
    CHECK_EQ(tone_page_slots_check(&pg, NULL), LINK_NACK_BAD_VALUE);
}

TEST_CASE(a_write_of_the_set_up_in_force_changes_nothing)
{
    fresh();
    CHECK_EQ(on(22u), 0u);
    const uint16_t gen = pg.gen;
    CHECK_EQ(on(22u), 0u);
    CHECK_EQ(pg.gen, gen);
    CHECK_EQ(setup(1u, 22u, 401u, 6500u, 8u, 3u, 3u, 0u), 0u);
    CHECK(pg.gen != gen);
}

TEST_CASE(a_pin_the_wiring_could_not_take_is_refused_and_tried_again)
{
    fresh();
    uint16_t was[LINK_TN_CONFIG_COUNT];
    tone_page_read(&pg, 0u, LINK_TN_CONFIG_COUNT, was);
    CHECK_EQ(on(22u), 0u);
    /* The wiring has no PIO state machine for it: put back what was. */
    tone_page_revert(&pg, was, false);
    CHECK_EQ(reg(LINK_TN_ENABLE), 0u);
    CHECK_EQ(tone_page_pins(&pg), 0u);
    /* Refused after the fact (a kept set-up at boot): enabled, not run,
     * not holding the pin. */
    CHECK_EQ(on(22u), 0u);
    tone_page_refuse(&pg);
    CHECK(tone_page_enabled(&pg));
    CHECK(!tone_page_wanted(&pg));
    CHECK_EQ(tone_page_pins(&pg), 0u);
    CHECK((reg(LINK_TN_FLAGS) & LINK_TN_PIN_REFUSED) != 0u);
    /* The same write is tried again rather than taken as no change. */
    const uint16_t gen = pg.gen;
    CHECK_EQ(on(22u), 0u);
    CHECK(pg.gen != gen);
    CHECK(tone_page_wanted(&pg));
    CHECK_EQ(reg(LINK_TN_FLAGS) & LINK_TN_PIN_REFUSED, 0u);
    /* A tap that is not enabled is not refused. */
    const uint16_t off = 0u;
    CHECK_EQ(tone_page_write(&pg, LINK_TN_ENABLE, 1u, &off, &o, 0u), 0u);
    tone_page_refuse(&pg);
    CHECK_EQ(reg(LINK_TN_FLAGS) & LINK_TN_PIN_REFUSED, 0u);
    tone_page_refuse(NULL);
    tone_page_revert(NULL, was, false);
    tone_page_revert(&pg, NULL, false);
}

TEST_CASE(the_set_up_kept_in_flash_is_restored_through_the_pages_checks)
{
    fresh();
    uint16_t kept[LINK_TN_CONFIG_COUNT] = { 1u, 22u, 500u, 5000u, 10u, 5u, 4u };
    tone_page_restore(&pg, kept, &o, 0u);
    CHECK(tone_page_wanted(&pg));
    CHECK_EQ(reg(LINK_TN_F_MIN_HZ), 500u);
    CHECK_EQ(tone_page_pins(&pg), BIT(22));
    /* A set-up that is no longer valid starts as the defaults. */
    kept[LINK_TN_F_MAX_HZ] = 9999u;
    tone_page_restore(&pg, kept, &o, 0u);
    CHECK(!tone_page_enabled(&pg));
    CHECK_EQ(reg(LINK_TN_F_MAX_HZ), 6500u);
    CHECK_EQ(reg(LINK_TN_PIN), 22u);
    /* Enabled on a pin the board reserves (GP3, the heartbeat): kept, refused,
     * and not held as the tap's own, so the pin stays the board's. */
    kept[LINK_TN_F_MAX_HZ] = 5000u;
    kept[LINK_TN_PIN] = 3u;
    tone_page_restore(&pg, kept, &o, 0u);
    CHECK(tone_page_enabled(&pg));
    CHECK(!tone_page_wanted(&pg));
    CHECK_EQ(tone_page_pins(&pg), 0u);
    CHECK((reg(LINK_TN_FLAGS) & LINK_TN_PIN_REFUSED) != 0u);
    CHECK_EQ(reg(LINK_TN_PIN), 3u);
    /* Enabled on a pin an output has since been bound to: kept, refused,
     * and not reserved. */
    kept[LINK_TN_F_MAX_HZ] = 5000u;
    kept[LINK_TN_PIN] = 4u;
    tone_page_restore(&pg, kept, &o, 0u);
    CHECK(tone_page_enabled(&pg));
    CHECK(!tone_page_wanted(&pg));
    CHECK_EQ(tone_page_pins(&pg), 0u);
    CHECK((reg(LINK_TN_FLAGS) & LINK_TN_PIN_REFUSED) != 0u);
    /* And on a pin SENSE holds. */
    kept[LINK_TN_PIN] = 17u;
    tone_page_restore(&pg, kept, &o, BIT(17));
    CHECK(!tone_page_wanted(&pg));
    tone_page_restore(NULL, kept, &o, 0u);
    tone_page_restore(&pg, NULL, &o, 0u);
}

TEST_CASE(the_pull_down_is_for_a_pin_the_tap_may_have)
{
    fresh();
    CHECK(tone_page_pin_free(&pg, &o, 0u));
    CHECK(!tone_page_pin_free(&pg, &o, BIT(22)));
    CHECK_EQ(setup(0u, 4u, 400u, 6500u, 8u, 3u, 3u, 0u), 0u);
    CHECK(!tone_page_pin_free(&pg, &o, 0u));
    CHECK_EQ(setup(0u, 27u, 400u, 6500u, 8u, 3u, 3u, 0u), 0u);
    CHECK(!tone_page_pin_free(&pg, &o, 0u));
    CHECK(!tone_page_pin_free(NULL, &o, 0u));
}

TEST_CASE(read_only_registers_and_the_page_end_are_refused)
{
    fresh();
    const uint16_t v = 1u;
    for (unsigned r = LINK_TN_FLAGS; r < LINK_TN_COUNT; ++r) {
        if (r == LINK_TN_EVT_SEL) {
            continue;
        }
        CHECK_EQ(tone_page_write(&pg, (uint8_t)r, 1u, &v, &o, 0u),
                 LINK_NACK_READ_ONLY);
    }
    /* A write that starts in the set-up and runs on is read only too, and
     * so is EVT_SEL with a register beside it. */
    const uint16_t two[9] = { 1u, 22u, 400u, 6500u, 8u, 3u, 3u, 0u, 0u };
    CHECK_EQ(tone_page_write(&pg, 0u, 9u, two, &o, 0u), LINK_NACK_READ_ONLY);
    CHECK_EQ(reg(LINK_TN_ENABLE), 0u);
    CHECK_EQ(tone_page_write(&pg, LINK_TN_EVT_SEL, 2u, two, &o, 0u),
             LINK_NACK_READ_ONLY);
    CHECK_EQ(tone_page_write(&pg, LINK_TN_COUNT, 1u, &v, &o, 0u),
             LINK_NACK_BAD_RANGE);
    CHECK_EQ(tone_page_write(&pg, 0u, 25u, two, &o, 0u), LINK_NACK_BAD_RANGE);
    CHECK_EQ(tone_page_write(&pg, 0u, 0u, two, &o, 0u), 0u);
    CHECK_EQ(tone_page_write(NULL, 0u, 1u, &v, &o, 0u), LINK_NACK_BAD_RANGE);
    CHECK_EQ(tone_page_write(&pg, 0u, 1u, NULL, &o, 0u), LINK_NACK_BAD_RANGE);
    CHECK_EQ(tone_page_write(&pg, 0u, 1u, &v, NULL, 0u), LINK_NACK_BAD_RANGE);
    /* Reads past the end leave the buffer alone. */
    uint16_t out[4] = { 0xAAAAu, 0xAAAAu, 0xAAAAu, 0xAAAAu };
    tone_page_read(&pg, 22u, 4u, out);
    CHECK_EQ(out[0], 0xAAAAu);
    tone_page_read(NULL, 0u, 1u, out);
    tone_page_read(&pg, 0u, 1u, NULL);
}

TEST_CASE(beeps_are_read_from_the_ring_without_consuming_them)
{
    fresh();
    give(3u);
    CHECK_EQ(reg(LINK_TN_BEEP_HEAD), 3u);
    const uint16_t sel = 2u;
    CHECK_EQ(tone_page_write(&pg, LINK_TN_EVT_SEL, 1u, &sel, &o, 0u), 0u);
    uint16_t a[LINK_TN_COUNT];
    uint16_t b[LINK_TN_COUNT];
    tone_page_read(&pg, 0u, LINK_TN_COUNT, a);
    tone_page_read(&pg, 0u, LINK_TN_COUNT, b);
    CHECK_EQ(memcmp(a, b, sizeof(a)), 0);
    CHECK_EQ(a[LINK_TN_EVT_SEL], 2u);
    CHECK_EQ(a[LINK_TN_EVT_SEQ], 2u);
    CHECK_EQ(a[LINK_TN_EVT_START_LO], 20u);
    CHECK_EQ(a[LINK_TN_EVT_START_HI], 0u);
    CHECK_EQ(a[LINK_TN_EVT_LEN_DMS], 1234u);
    CHECK_EQ(a[LINK_TN_EVT_FREQ_DHZ], 1001u);
    CHECK_EQ(a[LINK_TN_EVT_BURSTS], 40u);
    CHECK_EQ(a[LINK_TN_EVT_CARRIER_HHZ], 240u);
    CHECK_EQ(a[LINK_TN_EVT_FLAGS], LINK_TN_EVT_AFTER_CHANGE);
    /* The same beep again, and its neighbours. */
    const uint16_t s3 = 3u;
    tone_page_write(&pg, LINK_TN_EVT_SEL, 1u, &s3, &o, 0u);
    CHECK_EQ(reg(LINK_TN_EVT_FREQ_DHZ), 1002u);
    CHECK_EQ(reg(LINK_TN_BEEP_HEAD), 3u);
    const uint16_t s1 = 1u;
    tone_page_write(&pg, LINK_TN_EVT_SEL, 1u, &s1, &o, 0u);
    CHECK_EQ(reg(LINK_TN_EVT_FREQ_DHZ), 1000u);
}

TEST_CASE(a_start_beyond_65535_ms_uses_both_registers)
{
    fresh();
    tone_status_t st = status(pg.gen, pg.cap_gen);
    const tone_rec_t r = rec(0x0001ABCDu, 3000u);
    tone_page_publish(&pg, &st, &r, 1u);
    const uint16_t s = 1u;
    tone_page_write(&pg, LINK_TN_EVT_SEL, 1u, &s, &o, 0u);
    CHECK_EQ(reg(LINK_TN_EVT_START_LO), 0xABCDu);
    CHECK_EQ(reg(LINK_TN_EVT_START_HI), 0x0001u);
}

TEST_CASE(a_beep_that_has_left_the_ring_or_not_come_reads_as_none)
{
    fresh();
    give(70u);
    CHECK_EQ(reg(LINK_TN_BEEP_HEAD), 70u);
    uint16_t sel = 6u;                    /* the 64 are 7 to 70 */
    tone_page_write(&pg, LINK_TN_EVT_SEL, 1u, &sel, &o, 0u);
    CHECK_EQ(reg(LINK_TN_EVT_SEL), 6u);
    CHECK_EQ(reg(LINK_TN_EVT_SEQ), 0u);
    CHECK_EQ(reg(LINK_TN_EVT_FREQ_DHZ), 0u);
    CHECK_EQ(reg(LINK_TN_EVT_START_LO), 0u);
    sel = 7u;
    tone_page_write(&pg, LINK_TN_EVT_SEL, 1u, &sel, &o, 0u);
    CHECK_EQ(reg(LINK_TN_EVT_SEQ), 7u);
    CHECK_EQ(reg(LINK_TN_EVT_FREQ_DHZ), 1006u);
    sel = 70u;
    tone_page_write(&pg, LINK_TN_EVT_SEL, 1u, &sel, &o, 0u);
    CHECK_EQ(reg(LINK_TN_EVT_SEQ), 70u);
    sel = 71u;                            /* not yet */
    tone_page_write(&pg, LINK_TN_EVT_SEL, 1u, &sel, &o, 0u);
    CHECK_EQ(reg(LINK_TN_EVT_SEQ), 0u);
    sel = 0u;                             /* no beep has the number 0 */
    tone_page_write(&pg, LINK_TN_EVT_SEL, 1u, &sel, &o, 0u);
    CHECK_EQ(reg(LINK_TN_EVT_SEQ), 0u);
}

TEST_CASE(beep_numbers_run_to_65535_and_go_round_without_a_zero)
{
    fresh();
    /* Take 65530 beeps at once, then 10 more. */
    pg.n = 65529u;
    pg.first = 1u;
    give(1u);
    CHECK_EQ(reg(LINK_TN_BEEP_HEAD), 65530u);
    pg.n = 65534u;
    give(1u);
    CHECK_EQ(reg(LINK_TN_BEEP_HEAD), 65535u);
    give(1u);
    CHECK_EQ(reg(LINK_TN_BEEP_HEAD), 1u);
    give(1u);
    CHECK_EQ(reg(LINK_TN_BEEP_HEAD), 2u);
    uint16_t sel = 65535u;
    tone_page_write(&pg, LINK_TN_EVT_SEL, 1u, &sel, &o, 0u);
    CHECK_EQ(reg(LINK_TN_EVT_SEQ), 65535u);
    sel = 1u;
    tone_page_write(&pg, LINK_TN_EVT_SEL, 1u, &sel, &o, 0u);
    CHECK_EQ(reg(LINK_TN_EVT_SEQ), 1u);
    CHECK_EQ(reg(LINK_TN_EVT_FREQ_DHZ), (uint16_t)(1000u + 65535u));
    sel = 0u;
    tone_page_write(&pg, LINK_TN_EVT_SEL, 1u, &sel, &o, 0u);
    CHECK_EQ(reg(LINK_TN_EVT_SEQ), 0u);
}

TEST_CASE(a_new_capture_empties_the_ring_and_the_numbers_go_on)
{
    fresh();
    give(5u);
    tone_page_capture(&pg);
    CHECK_EQ(reg(LINK_TN_BEEP_HEAD), 5u);
    uint16_t sel = 5u;
    tone_page_write(&pg, LINK_TN_EVT_SEL, 1u, &sel, &o, 0u);
    CHECK_EQ(reg(LINK_TN_EVT_SEQ), 0u);
    give(1u);
    CHECK_EQ(reg(LINK_TN_BEEP_HEAD), 6u);
    sel = 6u;
    tone_page_write(&pg, LINK_TN_EVT_SEL, 1u, &sel, &o, 0u);
    CHECK_EQ(reg(LINK_TN_EVT_SEQ), 6u);
    sel = 4u;
    tone_page_write(&pg, LINK_TN_EVT_SEL, 1u, &sel, &o, 0u);
    CHECK_EQ(reg(LINK_TN_EVT_SEQ), 0u);
    tone_page_capture(NULL);
}

TEST_CASE(a_capture_restarted_after_a_refusal_keeps_the_ring)
{
    fresh();
    give(5u);
    tone_page_recapture(&pg, 0u);
    CHECK_EQ(reg(LINK_TN_BEEP_HEAD), 5u);
    uint16_t sel = 3u;
    tone_page_write(&pg, LINK_TN_EVT_SEL, 1u, &sel, &o, 0u);
    CHECK_EQ(reg(LINK_TN_EVT_SEQ), 3u);
    give(1u);
    sel = 6u;
    tone_page_write(&pg, LINK_TN_EVT_SEL, 1u, &sel, &o, 0u);
    CHECK_EQ(reg(LINK_TN_EVT_SEQ), 6u);
    tone_page_recapture(NULL, 0u);
}

TEST_CASE(a_refused_restart_keeps_the_time_base_and_says_the_cut)
{
    fresh();
    give(2u);
    CHECK_EQ(reg(LINK_TN_FLAGS) & LINK_TN_OVERRUN, 0u);
    tone_page_recapture(&pg, 1000u);
    CHECK(reg(LINK_TN_FLAGS) & LINK_TN_OVERRUN);
    tone_rec_t r = rec(30u, 1234u);
    tone_status_t st = status(pg.gen, pg.cap_gen);
    tone_page_publish(&pg, &st, &r, 1u);
    uint16_t sel = 3u;
    tone_page_write(&pg, LINK_TN_EVT_SEL, 1u, &sel, &o, 0u);
    CHECK_EQ(reg(LINK_TN_EVT_SEQ), 3u);
    CHECK_EQ(reg(LINK_TN_EVT_START_LO), 1030u);
    /* A real restart starts both again. */
    tone_page_capture(&pg);
    CHECK_EQ(reg(LINK_TN_FLAGS) & LINK_TN_OVERRUN, 0u);
    CHECK_EQ(pg.ms_shift, 0u);
}

TEST_CASE(the_beep_index_does_not_wrap_at_32_bits)
{
    fresh();
    /* Four billion beeps in, after a restart. */
    pg.n = 0xFFFFFFFEull;
    pg.first = pg.n + 1u;
    give(4u);
    const uint16_t head = reg(LINK_TN_BEEP_HEAD);
    CHECK(head != 0u);
    uint16_t sel = head;
    tone_page_write(&pg, LINK_TN_EVT_SEL, 1u, &sel, &o, 0u);
    CHECK_EQ(reg(LINK_TN_EVT_SEQ), head);
}

TEST_CASE(a_status_is_taken_only_under_the_set_up_and_capture_in_force)
{
    fresh();
    CHECK_EQ(on(22u), 0u);
    tone_page_capture(&pg);
    tone_status_t st = status(pg.gen, pg.cap_gen);
    st.beep = true;
    st.tone = true;
    st.overrun = true;
    st.window = 4321u;
    st.win_freq_dhz = 20000u;
    st.win_periods = 9u;
    st.lost = 3u;
    st.glitches = 17u;
    const tone_rec_t r = rec(5u, 20000u);
    tone_page_publish(&pg, &st, &r, 1u);
    CHECK_EQ(reg(LINK_TN_FLAGS),
             LINK_TN_RUNNING | LINK_TN_OVERRUN | LINK_TN_BEEP | LINK_TN_TONE);
    CHECK_EQ(reg(LINK_TN_WINDOW), 4321u);
    CHECK_EQ(reg(LINK_TN_WIN_FREQ_DHZ), 20000u);
    CHECK_EQ(reg(LINK_TN_WIN_PERIODS), 9u);
    CHECK_EQ(reg(LINK_TN_LOST), 3u);
    CHECK_EQ(reg(LINK_TN_GLITCHES), 17u);
    CHECK_EQ(reg(LINK_TN_BEEP_HEAD), 1u);

    /* Under an earlier set-up or capture: ignored, beeps included. */
    tone_status_t old = st;
    old.gen = (uint16_t)(pg.gen - 1u);
    old.beep = false;
    tone_page_publish(&pg, &old, &r, 1u);
    CHECK((reg(LINK_TN_FLAGS) & LINK_TN_BEEP) != 0u);
    CHECK_EQ(reg(LINK_TN_BEEP_HEAD), 1u);
    old = st;
    old.cap_gen = (uint16_t)(pg.cap_gen - 1u);
    tone_page_publish(&pg, &old, &r, 1u);
    CHECK_EQ(reg(LINK_TN_BEEP_HEAD), 1u);

    /* A new set-up forgets what the old one read, and keeps the counts. */
    CHECK_EQ(setup(1u, 22u, 500u, 6500u, 8u, 3u, 3u, 0u), 0u);
    CHECK_EQ(reg(LINK_TN_FLAGS), 0u);
    CHECK_EQ(reg(LINK_TN_WINDOW), 0u);
    CHECK_EQ(reg(LINK_TN_WIN_FREQ_DHZ), 0u);
    CHECK_EQ(reg(LINK_TN_LOST), 3u);
    CHECK_EQ(reg(LINK_TN_BEEP_HEAD), 1u);
    /* Its detector starts its own counts again. */
    st = status(pg.gen, pg.cap_gen);
    st.lost = 2u;
    st.glitches = 1u;
    tone_page_publish(&pg, &st, NULL, 0u);
    CHECK_EQ(reg(LINK_TN_LOST), 5u);
    CHECK_EQ(reg(LINK_TN_GLITCHES), 18u);
    st.lost = 4u;
    tone_page_publish(&pg, &st, NULL, 0u);
    CHECK_EQ(reg(LINK_TN_LOST), 7u);

    /* A service that is not running reads as nothing. */
    st.running = false;
    st.beep = true;
    tone_page_publish(&pg, &st, &r, 1u);
    CHECK_EQ(reg(LINK_TN_FLAGS), 0u);
    CHECK_EQ(reg(LINK_TN_BEEP_HEAD), 1u);
    tone_page_publish(NULL, &st, &r, 1u);
    tone_page_publish(&pg, NULL, &r, 1u);
}

TEST_CASE(beeps_handed_over_alone_are_taken_under_the_capture_in_force)
{
    fresh();
    const tone_rec_t r = rec(7u, 4000u);
    tone_page_beeps(&pg, pg.gen, pg.cap_gen, &r, 1u);
    CHECK_EQ(reg(LINK_TN_BEEP_HEAD), 1u);
    /* Finished under the set-up before a change of the range: taken. */
    tone_page_beeps(&pg, (uint16_t)(pg.gen - 1u), pg.cap_gen, &r, 1u);
    CHECK_EQ(reg(LINK_TN_BEEP_HEAD), 2u);
    /* From another capture: dropped. */
    tone_page_beeps(&pg, pg.gen, (uint16_t)(pg.cap_gen + 1u), &r, 1u);
    tone_page_beeps(NULL, pg.gen, pg.cap_gen, &r, 1u);
    tone_page_beeps(&pg, pg.gen, pg.cap_gen, NULL, 1u);
    CHECK_EQ(reg(LINK_TN_BEEP_HEAD), 2u);
}

TEST_CASE(the_counters_wrap_at_65536_and_beeps_lost_between_the_cores_count)
{
    fresh();
    tone_status_t st = status(pg.gen, pg.cap_gen);
    st.lost = 65530u;
    st.glitches = 70000u;
    tone_page_publish(&pg, &st, NULL, 0u);
    CHECK_EQ(reg(LINK_TN_LOST), 65530u);
    CHECK_EQ(reg(LINK_TN_GLITCHES), 70000u - 65536u);
    tone_page_dropped(&pg, 10u);
    CHECK_EQ(reg(LINK_TN_LOST), 4u);
    tone_page_dropped(NULL, 1u);
}

TEST_CASE(the_order_for_core_1_carries_the_page)
{
    fresh();
    CHECK_EQ(setup(1u, 22u, 500u, 5000u, 10u, 5u, 4u, 0u), 0u);
    tone_page_capture(&pg);
    tone_cmd_t c;
    tone_page_cmd(&pg, true, &c);
    CHECK(c.run);
    CHECK_EQ(c.gen, pg.gen);
    CHECK_EQ(c.cap_gen, pg.cap_gen);
    CHECK_EQ(c.f_min_hz, 500u);
    CHECK_EQ(c.f_max_hz, 5000u);
    CHECK_EQ(c.split_pct, 10u);
    CHECK_EQ(c.gap_ms, 5u);
    CHECK_EQ(c.min_periods, 4u);
    /* Not while the wiring has not started the capture, nor refused, nor
     * disabled. */
    tone_page_cmd(&pg, false, &c);
    CHECK(!c.run);
    tone_page_refuse(&pg);
    tone_page_cmd(&pg, true, &c);
    CHECK(!c.run);
    const uint16_t off = 0u;
    CHECK_EQ(tone_page_write(&pg, LINK_TN_ENABLE, 1u, &off, &o, 0u), 0u);
    tone_page_cmd(&pg, true, &c);
    CHECK(!c.run);
    tone_page_cmd(NULL, true, &c);
    tone_page_cmd(&pg, true, NULL);
    /* The order is what the detector takes. */
    tone_cfg_t cfg;
    tone_t d;
    tone_page_cmd(&pg, true, &c);
    tone_svc_cfg(&c, &cfg);
    CHECK(tone_init(&d, &cfg));
}

int main(void)
{
    RUN(a_page_starts_with_the_tap_off_on_gp22);
    RUN(a_set_up_is_taken_whole_and_read_back);
    RUN(every_value_is_held_to_its_range_at_both_ends);
    RUN(a_gap_shorter_than_the_lowest_tones_period_is_refused);
    RUN(the_pin_is_refused_where_something_else_holds_it);
    RUN(a_refused_write_stores_none_of_its_registers);
    RUN(the_pin_is_the_taps_while_it_runs_and_no_slot_binds_it);
    RUN(a_write_of_the_set_up_in_force_changes_nothing);
    RUN(a_pin_the_wiring_could_not_take_is_refused_and_tried_again);
    RUN(the_set_up_kept_in_flash_is_restored_through_the_pages_checks);
    RUN(the_pull_down_is_for_a_pin_the_tap_may_have);
    RUN(read_only_registers_and_the_page_end_are_refused);
    RUN(beeps_are_read_from_the_ring_without_consuming_them);
    RUN(a_start_beyond_65535_ms_uses_both_registers);
    RUN(a_beep_that_has_left_the_ring_or_not_come_reads_as_none);
    RUN(beep_numbers_run_to_65535_and_go_round_without_a_zero);
    RUN(a_new_capture_empties_the_ring_and_the_numbers_go_on);
    RUN(a_capture_restarted_after_a_refusal_keeps_the_ring);
    RUN(a_refused_restart_keeps_the_time_base_and_says_the_cut);
    RUN(the_beep_index_does_not_wrap_at_32_bits);
    RUN(a_status_is_taken_only_under_the_set_up_and_capture_in_force);
    RUN(beeps_handed_over_alone_are_taken_under_the_capture_in_force);
    RUN(the_counters_wrap_at_65536_and_beeps_lost_between_the_cores_count);
    RUN(the_order_for_core_1_carries_the_page);
    return test_summary("tone_page");
}
