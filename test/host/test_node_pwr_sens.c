/*
 * The power node's INA228 search and shunt setting
 * (firmware/node/main/node_pwr_sens.c), over the modelled bus of
 * fake_ina.h.
 *
 * The failures this is about: a node that finds only the MATEK at 0x45
 * and not a board at 0x40 or 0x4F; a search that stalls the 1130µs tick
 * with more than two transactions, asks outside 0x40 to 0x4F (the display
 * board's touch controller at 0x38 and codec at 0x18) or counts its NACKs
 * as bus errors; a part of another maker, or a TI part of another die,
 * taken for the INA228 or written to; a shunt that does not scale CURRENT
 * and POWER by the datasheet's equations; a shunt out of range taken; a
 * fixed address that asks another address; and a MATEK on the default
 * shunt whose set-up or windows differ by a byte from the fixed 0x45,
 * 200µΩ, 204,800mA set-up the node ran before the search.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "fake_ina.h"
#include "ina228.h"
#include "node_pwr_sens.h"
#include "rcnode.h"
#include "sense_bus.h"

static fake_bus_t  fb;
static sense_bus_t bus;
static psens_t     s;

/** The display board's other parts, outside the search's range. */
static fake_part_t *touch;
static fake_part_t *codec;

/** A bus with the display board's touch controller and codec on it. */
static void display_bus(void)
{
    fake_bus_init(&fb, &bus);
    touch = fake_add(&fb, FAKE_AS5600, 0x38u, 0.0);
    codec = fake_add(&fb, FAKE_AS5600, 0x18u, 0.0);
}

/** Every read and write a part has had. */
static unsigned touched(const fake_part_t *p)
{
    unsigned n = 0u;
    for (size_t i = 0; i < 256; ++i) {
        n += p->reads[i] + p->writes[i];
    }
    return n;
}

/** Writes a part has had. */
static unsigned writes_to(const fake_part_t *p)
{
    unsigned n = 0u;
    for (size_t i = 0; i < 256; ++i) {
        n += p->writes[i];
    }
    return n;
}

/**
 * Step until the part is online or @p max steps pass, each step 1ms on,
 * each search step at most two transactions.
 * @return The steps taken, max + 1 when the part did not come online.
 */
static unsigned step_online(uint32_t *now, unsigned max)
{
    for (unsigned i = 1u; i <= max; ++i) {
        const bool bound = s.bound;
        const unsigned before = fb.transactions;
        const bool on = psens_step(&s, *now);
        if (!bound) {
            CHECK(fb.transactions - before <= 2u);
        }
        *now += 1u;
        if (on) {
            return i;
        }
    }
    return max + 1u;
}

/* ------------------------------------------------------------ search */

TEST_CASE(an_ina228_at_0x40_is_found_at_the_first_address_and_probed_next)
{
    display_bus();
    fake_part_t *p = fake_add(&fb, FAKE_INA228, 0x40u, 200e-6);
    psens_init(&s, &bus, RCNODE_SHUNT_DEF_UOHM);
    CHECK_EQ(psens_state(&s), SENSE_PART_UNPROBED);
    uint32_t now = 100u;
    CHECK_EQ(step_online(&now, 40u), 2u);
    CHECK_EQ(psens_addr(&s), 0x40u);
    CHECK_EQ(psens_state(&s), SENSE_PART_ONLINE);
    CHECK_EQ(p->reg[INA228_SHUNT_CAL], INA228_SHUNT_CAL_ADC);
    CHECK_EQ(p->reg[INA228_CONFIG], INA228_CONFIG_ADCRANGE);
    CHECK_EQ(touched(touch), 0u);
    CHECK_EQ(touched(codec), 0u);
}

TEST_CASE(an_ina228_at_0x4f_is_found_after_15_nacks_one_address_a_step)
{
    display_bus();
    (void)fake_add(&fb, FAKE_INA228, 0x4Fu, 200e-6);
    psens_init(&s, &bus, RCNODE_SHUNT_DEF_UOHM);
    uint32_t now = 0u;
    /* 15 addresses without an answer, a step each, then the find, then
     * the probe. */
    for (unsigned i = 0u; i < 15u; ++i) {
        const unsigned before = fb.transactions;
        CHECK(!psens_step(&s, now++));
        CHECK_EQ(fb.transactions - before, 1u);
        CHECK(!s.bound);
    }
    CHECK(!psens_step(&s, now++));
    CHECK(s.bound);
    CHECK_EQ(psens_addr(&s), 0x4Fu);
    CHECK(psens_step(&s, now++));
    /* A NACK is a missing address, not a bus error. */
    CHECK_EQ(bus.errors, 0u);
    CHECK_EQ(touched(touch), 0u);
    CHECK_EQ(touched(codec), 0u);
}

TEST_CASE(no_ina228_ends_the_search_absent_and_waits_a_second_for_the_next)
{
    display_bus();
    psens_init(&s, &bus, RCNODE_SHUNT_DEF_UOHM);
    uint32_t now = 5000u;
    for (unsigned i = 0u; i < 16u; ++i) {
        CHECK(!psens_step(&s, now++));
    }
    CHECK_EQ(fb.transactions, 16u);
    CHECK_EQ(psens_state(&s), SENSE_PART_ABSENT);
    CHECK_EQ(psens_addr(&s), 0u);
    CHECK_EQ(bus.errors, 0u);
    /* Nothing goes out until SENSE_RETRY_MS after the search's end. */
    const uint32_t end = now - 1u;
    for (; now < end + SENSE_RETRY_MS; now += 7u) {
        CHECK(!psens_step(&s, now));
    }
    CHECK_EQ(fb.transactions, 16u);
    CHECK(!psens_step(&s, end + SENSE_RETRY_MS));
    CHECK_EQ(fb.transactions, 17u);
    CHECK_EQ(touched(touch), 0u);
    CHECK_EQ(touched(codec), 0u);
}

TEST_CASE(a_part_of_another_maker_in_range_is_skipped_and_never_written)
{
    display_bus();
    fake_part_t *other = fake_add(&fb, FAKE_INA228, 0x40u, 200e-6);
    other->maker = 0x1234u;
    (void)fake_add(&fb, FAKE_INA228, 0x41u, 200e-6);
    psens_init(&s, &bus, RCNODE_SHUNT_DEF_UOHM);
    uint32_t now = 0u;
    CHECK_EQ(step_online(&now, 40u), 3u);
    CHECK_EQ(psens_addr(&s), 0x41u);
    CHECK_EQ(writes_to(other), 0u);
}

TEST_CASE(a_ti_part_of_another_die_alone_is_wrong_id_and_never_written)
{
    display_bus();
    fake_part_t *ina238 = fake_add(&fb, FAKE_INA228, 0x40u, 200e-6);
    ina238->device = 0x2381u;
    psens_init(&s, &bus, RCNODE_SHUNT_DEF_UOHM);
    uint32_t now = 0u;
    CHECK_EQ(step_online(&now, 16u), 17u);
    CHECK_EQ(psens_state(&s), SENSE_PART_WRONG_ID);
    CHECK_EQ(psens_addr(&s), 0u);
    CHECK_EQ(writes_to(ina238), 0u);
}

TEST_CASE(a_part_found_reset_is_searched_again_a_second_on_and_found_moved)
{
    display_bus();
    fake_part_t *p = fake_add(&fb, FAKE_INA228, 0x40u, 200e-6);
    psens_init(&s, &bus, RCNODE_SHUNT_DEF_UOHM);
    uint32_t now = 0u;
    CHECK_EQ(step_online(&now, 40u), 2u);
    /* The part resets: the driver's read-back takes it offline.  Then the
     * board leaves and another comes at 0x4A. */
    fake_reset228(p);
    bool lost = false;
    CHECK_EQ(ina228_verify(&s.ina, &lost), SENSE_OK);
    CHECK(lost);
    p->present = false;
    (void)fake_add(&fb, FAKE_INA228, 0x4Au, 200e-6);
    CHECK(!psens_step(&s, now));
    CHECK_EQ(psens_state(&s), SENSE_PART_OFFLINE);
    const unsigned before = fb.transactions;
    CHECK(!psens_step(&s, now + SENSE_RETRY_MS - 1u));
    CHECK_EQ(fb.transactions, before);
    now += SENSE_RETRY_MS;
    CHECK(step_online(&now, 40u) <= 12u);
    CHECK_EQ(psens_addr(&s), 0x4Au);
}

TEST_CASE(a_stuck_bus_holds_the_search_on_its_address_until_the_recovery)
{
    display_bus();
    (void)fake_add(&fb, FAKE_INA228, 0x42u, 200e-6);
    psens_init(&s, &bus, RCNODE_SHUNT_DEF_UOHM);
    fb.low = true;
    CHECK(!psens_step(&s, 0u));
    CHECK(bus.stuck);
    const unsigned at_stuck = fb.transactions;
    CHECK(!psens_step(&s, 1u));
    CHECK_EQ(fb.transactions, at_stuck);
    CHECK_EQ(s.next, 0x40u);
    fb.low = false;
    sense_bus_recovered(&bus, 2u);
    uint32_t now = 3u;
    CHECK_EQ(step_online(&now, 40u), 4u);
    CHECK_EQ(psens_addr(&s), 0x42u);
}

/* ------------------------------------------------------------- shunt */

/** CURRENT_LSB in A by Equation 2 for SHUNT_CAL 4096 at ADCRANGE 1. */
static double lsb_a(double shunt_ohm)
{
    return 4096.0 / (13107.2e6 * shunt_ohm * 4.0);
}

/** A part on @p uohm with @p amps and @p volts, online through the search. */
static fake_part_t *online_on(uint16_t uohm, double amps, double volts)
{
    display_bus();
    fake_part_t *p = fake_add(&fb, FAKE_INA228, 0x40u, uohm * 1e-6);
    p->amps[0] = amps;
    p->volts[0] = volts;
    psens_init(&s, &bus, uohm);
    uint32_t now = 0u;
    CHECK_EQ(step_online(&now, 40u), 2u);
    return p;
}

TEST_CASE(the_shunt_scales_current_and_power_by_the_datasheet_equations)
{
    /* 3.21A on 10000µΩ: 32.1mV, inside the 40.96mV of ADCRANGE 1. */
    static const uint16_t shunts[] = { 50u, 200u, 500u, 1000u, 10000u };
    static const uint32_t fs_ma[] = { 819200u, 204800u, 81920u, 40960u,
                                      4096u };
    static const double amps[] = { 12.345, 12.345, 12.345, 12.345, 3.21 };
    for (size_t k = 0; k < 5u; ++k) {
        const uint16_t uohm = shunts[k];
        const double r = uohm * 1e-6;
        const fake_part_t *p = online_on(uohm, amps[k], 22.2);
        CHECK_EQ(psens_max_ma(uohm), fs_ma[k]);
        CHECK_EQ(s.cal.adcrange, 1u);
        CHECK_EQ(s.cal.shunt_cal, 4096u);
        CHECK_EQ(s.ina.cal.shunt_uohm, uohm);
        /* CURRENT (Equation 4) and POWER (Equation 5) from the codes. */
        const int32_t code = ina228_code20(
            (uint32_t)fake_value228(p, INA228_CURRENT));
        sense_value_t ua;
        CHECK_EQ(ina228_read_current(&s.ina, &ua), SENSE_OK);
        CHECK_NEAR((double)ua.value, code * lsb_a(r) * 1e6, 1.0);
        CHECK_NEAR((double)ua.value, amps[k] * 1e6, lsb_a(r) * 1e6);
        const uint32_t pcode = (uint32_t)fake_value228(p, INA228_POWER);
        uint64_t uw = 0u;
        CHECK_EQ(ina228_read_power(&s.ina, &uw), SENSE_OK);
        CHECK_NEAR((double)uw, pcode * 3.2 * lsb_a(r) * 1e6, 1.0);
        CHECK_NEAR((double)uw, amps[k] * 22.2 * 1e6, 3.2 * lsb_a(r) * 1e6);
    }
}

TEST_CASE(a_shunt_change_sets_the_part_up_again_and_rescales_the_same_code)
{
    fake_part_t *p = online_on(200u, 10.0, 12.0);
    sense_value_t before;
    CHECK_EQ(ina228_read_current(&s.ina, &before), SENSE_OK);
    CHECK(!psens_take_rearm(&s));
    const unsigned rstacc = p->writes[INA228_CONFIG];
    CHECK(psens_set_shunt(&s, 500u));
    CHECK(psens_step(&s, 100u));
    CHECK(psens_take_rearm(&s));
    CHECK(!psens_take_rearm(&s));
    /* The fake bus counts the writes behind psens_step(): cppcheck cannot
     * see that, so it takes the count as unchanged. */
    // cppcheck-suppress knownConditionTrueFalse
    CHECK(p->writes[INA228_CONFIG] > rstacc);   /* RSTACC and back */
    CHECK_EQ(psens_addr(&s), 0x40u);
    /* The same 10A drop read on a 500µΩ setting: 2/5 of it. */
    sense_value_t after;
    CHECK_EQ(ina228_read_current(&s.ina, &after), SENSE_OK);
    CHECK_NEAR((double)after.value, before.value * 200.0 / 500.0, 1.0);
}

TEST_CASE(a_shunt_out_of_range_is_refused_and_changes_nothing)
{
    (void)online_on(200u, 1.0, 12.0);
    CHECK(!psens_set_shunt(&s, 49u));
    CHECK(!psens_set_shunt(&s, 10001u));
    CHECK(!psens_set_shunt(&s, 0u));
    CHECK_EQ(s.shunt_uohm, 200u);
    CHECK(!psens_take_rearm(&s));
    CHECK(psens_set_shunt(&s, 50u));
    CHECK(psens_set_shunt(&s, 10000u));
    CHECK_EQ(psens_max_ma(50u), 819200u);
    CHECK_EQ(psens_max_ma(10000u), 4096u);
    psens_init(&s, &bus, 7u);
    CHECK_EQ(s.shunt_uohm, RCNODE_SHUNT_DEF_UOHM);
}

/* ------------------------------------------------- the MATEK default */

/** Samples from a driver into windows, encoded, for 3 windows. */
static size_t frames_of(ina228_t *d, uint8_t *out, size_t cap)
{
    rcnode_win_t w;
    rcnode_win_init(&w);
    size_t n = 0u;
    for (uint64_t t = 0u; t < 3u * RCNODE_WIN_US + 2000u; t += 1130u) {
        rcnode_sample_t smp;
        memset(&smp, 0, sizeof smp);
        sense_value_t i;
        int32_t v = 0;
        if (ina228_read_current(d, &i) == SENSE_OK
            && ina228_read_vbus(d, &v) == SENSE_OK) {
            smp.ok = true;
            smp.clip = (int8_t)i.clip;
            smp.i_ua = i.value;
            smp.v_uv = v;
        }
        rcnode_pwr_win_t pw;
        if (rcnode_win_add(&w, t, &smp, RCNODE_WF_ONLINE, &pw)
            && n + RCNODE_PKT_MAX <= cap) {
            n += rcnode_enc_pwr_win(&pw, out + n, cap - n);
        }
    }
    return n;
}

TEST_CASE(the_matek_on_the_default_shunt_gives_byte_identical_frames)
{
    /* Before the search: the driver at 0x45 with 200µΩ and 204,800mA. */
    fake_bus_t fb_old;
    sense_bus_t bus_old;
    fake_bus_init(&fb_old, &bus_old);
    fake_part_t *old = fake_add(&fb_old, FAKE_INA228, 0x45u, 200e-6);
    old->amps[0] = 87.6;
    old->volts[0] = 24.1;
    ina228_t ref;
    CHECK_EQ(ina228_init(&ref, &bus_old, 0x45u, 200u, 204800u,
                         INA228_ADC_BENCH), INA228_SETUP_OK);
    CHECK(ina228_step(&ref, 0u));

    /* Now: the default from an empty store, the MATEK found at 0x45. */
    display_bus();
    fake_part_t *p = fake_add(&fb, FAKE_INA228, 0x45u, 200e-6);
    p->amps[0] = 87.6;
    p->volts[0] = 24.1;
    psens_init(&s, &bus, RCNODE_SHUNT_DEF_UOHM);
    uint32_t now = 0u;
    CHECK_EQ(step_online(&now, 40u), 7u);
    CHECK_EQ(psens_addr(&s), 0x45u);

    CHECK_EQ(s.ina.cal.shunt_uohm, ref.cal.shunt_uohm);
    CHECK_EQ(s.ina.cal.max_ma, ref.cal.max_ma);
    CHECK_EQ(s.ina.cal.shunt_cal, ref.cal.shunt_cal);
    CHECK_EQ(s.ina.cal.adcrange, ref.cal.adcrange);
    CHECK_EQ(s.ina.config, ref.config);
    CHECK_EQ(s.ina.adc_config, ref.adc_config);
    CHECK_EQ(memcmp(p->reg, old->reg, sizeof p->reg), 0);
    CHECK_EQ(memcmp(p->writes, old->writes, sizeof p->writes), 0);

    static uint8_t a[4u * RCNODE_PKT_MAX];
    static uint8_t b[4u * RCNODE_PKT_MAX];
    const size_t na = frames_of(&ref, a, sizeof a);
    const size_t nb = frames_of(&s.ina, b, sizeof b);
    CHECK(na >= 3u * RCNODE_PWR_WIN_LEN);
    CHECK_EQ(na, nb);
    CHECK_EQ(memcmp(a, b, na), 0);

    /* INFO's power part: 0x45, ADCRANGE 1, 200µΩ, as the fixed set-up. */
    rcnode_info_t i_old;
    rcnode_info_t i_new;
    memset(&i_old, 0, sizeof i_old);
    i_old.proto = RCNODE_PROTO;
    i_old.role = RCNODE_ROLE_POWER;
    i_new = i_old;
    i_old.i228_addr = 0x45u;
    i_old.adcrange = ref.cal.adcrange;
    i_old.shunt_uohm = 200u;
    i_new.i228_addr = psens_addr(&s);
    i_new.adcrange = s.cal.adcrange;
    i_new.shunt_uohm = s.shunt_uohm;
    uint8_t ia[RCNODE_INFO_LEN];
    uint8_t ib[RCNODE_INFO_LEN];
    CHECK_EQ(rcnode_enc_info(&i_old, ia, sizeof ia), RCNODE_INFO_LEN);
    CHECK_EQ(rcnode_enc_info(&i_new, ib, sizeof ib), RCNODE_INFO_LEN);
    CHECK_EQ(memcmp(ia, ib, sizeof ia), 0);
}

TEST_CASE(the_wake_check_asks_one_address_and_takes_only_an_ina228)
{
    display_bus();
    fake_part_t *other = fake_add(&fb, FAKE_INA228, 0x43u, 200e-6);
    other->device = 0x2381u;
    (void)fake_add(&fb, FAKE_INA228, 0x4Eu, 200e-6);
    bool ti = false;
    CHECK(!psens_is_ina228(&bus, 0x40u, &ti));
    CHECK(!ti);
    CHECK(!psens_is_ina228(&bus, 0x43u, &ti));
    CHECK(ti);
    CHECK(psens_is_ina228(&bus, 0x4Eu, NULL));
    CHECK(!psens_is_ina228(&bus, 0x38u, NULL));
    CHECK(!psens_is_ina228(&bus, 0x18u, NULL));
    CHECK_EQ(touched(touch), 0u);
    CHECK_EQ(touched(codec), 0u);
}

/* Sleep's shutdown write: the part online, or the kept address only when
 * an INA228 still answers there; nothing for no part, another maker's
 * part or a TI part of another die at the kept address. */
TEST_CASE(sleep_writes_shutdown_only_to_an_ina228)
{
    display_bus();
    fake_part_t *ina = fake_add(&fb, FAKE_INA228, 0x40u, 200e-6);
    CHECK_EQ(psens_shut_down(&bus, 0u, 0x41u), 0u);       /* none there */
    CHECK_EQ(writes_to(ina), 0u);
    fake_part_t *pca = fake_add(&fb, FAKE_INA228, 0x41u, 200e-6);
    pca->maker = 0x1234u;                                  /* a PCA9685 */
    CHECK_EQ(psens_shut_down(&bus, 0u, 0x41u), 0u);
    CHECK_EQ(writes_to(pca), 0u);
    pca->maker = INA228_MAKER_ID;
    pca->device = 0x2381u;                                 /* an INA238 */
    CHECK_EQ(psens_shut_down(&bus, 0u, 0x41u), 0u);
    CHECK_EQ(writes_to(pca), 0u);
    CHECK_EQ(psens_shut_down(&bus, 0u, 0x40u), 0x40u);
    CHECK_EQ(ina->reg[INA228_ADC_CONFIG],
             ina228_adc_config(INA228_MODE_SHUTDOWN, 4, 4, 0, 0));
    CHECK_EQ(psens_shut_down(&bus, 0x40u, 0u), 0x40u);     /* online */
    CHECK_EQ(psens_shut_down(&bus, 0u, 0u), 0u);
    CHECK_EQ(touched(touch), 0u);
    CHECK_EQ(touched(codec), 0u);
}

TEST_CASE(a_fixed_address_asks_that_address_alone)
{
    display_bus();
    const fake_part_t *low = fake_add(&fb, FAKE_INA228, 0x40u, 200e-6);
    (void)fake_add(&fb, FAKE_INA228, 0x45u, 200e-6);
    psens_init(&s, &bus, RCNODE_SHUNT_DEF_UOHM);
    CHECK(psens_set_addr(&s, 0x45u));
    CHECK(!psens_set_addr(&s, 0x45u));
    uint32_t now = 0u;
    CHECK_EQ(step_online(&now, 40u), 2u);
    CHECK_EQ(psens_addr(&s), 0x45u);
    /* The part at 0x40, first in the AUTO search, was never asked. */
    CHECK_EQ(touched(low), 0u);
    CHECK_EQ(touched(touch), 0u);
    CHECK_EQ(touched(codec), 0u);
}

TEST_CASE(a_fixed_address_without_an_ina228_is_absent_and_back_to_auto_finds)
{
    display_bus();
    (void)fake_add(&fb, FAKE_INA228, 0x40u, 200e-6);
    psens_init(&s, &bus, RCNODE_SHUNT_DEF_UOHM);
    (void)psens_set_addr(&s, 0x4Au);
    uint32_t now = 0u;
    const unsigned before = fb.transactions;
    CHECK(!psens_step(&s, now++));
    CHECK_EQ(fb.transactions - before, 1u);   /* one NACK */
    CHECK_EQ(psens_state(&s), SENSE_PART_ABSENT);
    CHECK_EQ(psens_addr(&s), 0u);
    /* The next search waits SENSE_RETRY_MS and asks 0x4A again only. */
    for (unsigned i = 0u; i < 50u; ++i) {
        CHECK(!psens_step(&s, now++));
    }
    CHECK_EQ(fb.transactions - before, 1u);
    /* AUTO (a setting out of range is AUTO) searches at once. */
    CHECK(psens_set_addr(&s, 0x77u));
    CHECK_EQ(s.fixed, 0u);
    CHECK(step_online(&now, 40u) <= 3u);
    CHECK_EQ(psens_addr(&s), 0x40u);
    /* A change ends the binding: the part is searched at the new one. */
    CHECK(psens_set_addr(&s, 0x4Au));
    CHECK(!s.bound);
    CHECK_EQ(psens_state(&s), SENSE_PART_UNPROBED);
}

int main(void)
{
    RUN(an_ina228_at_0x40_is_found_at_the_first_address_and_probed_next);
    RUN(an_ina228_at_0x4f_is_found_after_15_nacks_one_address_a_step);
    RUN(no_ina228_ends_the_search_absent_and_waits_a_second_for_the_next);
    RUN(a_part_of_another_maker_in_range_is_skipped_and_never_written);
    RUN(a_ti_part_of_another_die_alone_is_wrong_id_and_never_written);
    RUN(a_part_found_reset_is_searched_again_a_second_on_and_found_moved);
    RUN(a_stuck_bus_holds_the_search_on_its_address_until_the_recovery);
    RUN(the_shunt_scales_current_and_power_by_the_datasheet_equations);
    RUN(a_shunt_change_sets_the_part_up_again_and_rescales_the_same_code);
    RUN(a_shunt_out_of_range_is_refused_and_changes_nothing);
    RUN(the_matek_on_the_default_shunt_gives_byte_identical_frames);
    RUN(the_wake_check_asks_one_address_and_takes_only_an_ina228);
    RUN(sleep_writes_shutdown_only_to_an_ina228);
    RUN(a_fixed_address_asks_that_address_alone);
    RUN(a_fixed_address_without_an_ina228_is_absent_and_back_to_auto_finds);
    return test_summary("node_pwr_sens");
}
