/*
 * The INA228's shunt and maximum from SETUP to the page: the rule the rows
 * name against the driver's for every shunt the page takes, a value edited
 * with the INA3221 parts on SENSE_MAP reaching the real page, and what the
 * operator reads when the page refuses it.
 *
 * Under test: the highest maximum of a shunt and the largest shunt of a
 * maximum at the driver's own edge; 199uOhm, 201uOhm and 204.9A beside the
 * 200uOhm and 204.8A defaults; the mirror bit never written; a pair past
 * 163.84mV named with its numbers and the bus pins not named; a page that
 * keeps its parts while the bank drives named as that, until the next edit;
 * the two rows' help lines and the mark's line.
 *
 * SPDX-License-Identifier: MIT
 */
#include "sense_link_fix.h"

#include <stdio.h>

#include "ina228.h"
#include "sense_map.h"
#include "sensors_rows.h"
#include "settings.h"
#include "settings_screen.h"
#include "ui_screen.h"
#include "ui_text.h"

/* Whether the driver takes the pair, as the page asks it. */
static bool driver_takes(unsigned uohm, unsigned da)
{
    ina228_cal_t cal;
    return ina228_calibrate(uohm, da * 100u, &cal) == INA228_SETUP_OK;
}

/* Each shunt the page takes: the maximum named is taken and one step more,
 * below the page's own 300.0A, is not.  Then every maximum the same way. */
TEST_CASE(the_rule_the_rows_name_is_the_drivers_at_every_shunt)
{
    for (unsigned u = LINK_SN_I228_UOHM_MIN; u <= LINK_SN_I228_UOHM_MAX; ++u) {
        const unsigned top = link_sn_i228_max_da(u);
        CHECK(top >= LINK_SN_I228_DA_MIN);
        CHECK(top <= LINK_SN_I228_DA_MAX);
        CHECK(driver_takes(u, LINK_SN_I228_DA_MIN));
        CHECK(driver_takes(u, top));
        if (top < LINK_SN_I228_DA_MAX) {
            CHECK(!driver_takes(u, top + 1u));
        }
    }
    for (unsigned d = LINK_SN_I228_DA_MIN; d <= LINK_SN_I228_DA_MAX; ++d) {
        const unsigned top = link_sn_i228_max_uohm(d);
        CHECK(top >= LINK_SN_I228_UOHM_MIN);
        CHECK(driver_takes(top, d));
        if (top < LINK_SN_I228_UOHM_MAX) {
            CHECK(!driver_takes(top + 1u, d));
        }
    }
    CHECK_EQ(link_sn_i228_max_da(0u), 0u);
    CHECK_EQ(link_sn_i228_max_uohm(0u), 0u);
}

/* The numbers: the driver chooses the range, so a shunt beside the 200uOhm
 * default keeps its maximum, and neither step is at the 2000A limit. */
TEST_CASE(the_defaults_are_a_pair_and_a_step_either_way_is_too)
{
    ina228_cal_t cal;
    CHECK_EQ(ina228_calibrate(200u, 204800u, &cal), INA228_SETUP_OK);
    CHECK_EQ(cal.adcrange, 1u);                 /* 40.96mV exactly */
    CHECK_EQ(ina228_calibrate(199u, 204800u, &cal), INA228_SETUP_OK);
    CHECK_EQ(cal.adcrange, 1u);
    CHECK_EQ(ina228_calibrate(201u, 204800u, &cal), INA228_SETUP_OK);
    CHECK_EQ(cal.adcrange, 0u);                 /* 163.84mV, 4 times coarser */
    CHECK_EQ(ina228_calibrate(200u, 204900u, &cal), INA228_SETUP_OK);
    CHECK_EQ(cal.adcrange, 0u);
    /* 201uOhm keeps the page's whole range, to 300.0A. */
    CHECK_EQ(link_sn_i228_max_da(201u), 3000u);
    CHECK_EQ(link_sn_i228_max_da(546u), 3000u);
    CHECK_EQ(link_sn_i228_max_da(547u), 2995u);
    CHECK_EQ(link_sn_i228_max_da(1000u), 1638u);
    CHECK_EQ(link_sn_i228_max_da(20000u), 81u);
    CHECK_EQ(link_sn_i228_max_uohm(2048u), 800u);
    /* The smallest full scale the driver's 2000A limit could see, 819.2A on
     * 50uOhm, is under it: only the 163.84mV top refuses a pair. */
    CHECK(driver_takes(LINK_SN_I228_UOHM_MIN, LINK_SN_I228_DA_MAX));
}

/* Both monitors on, the INA3221 by SENSE_MAP in force on the real page. */
static void both_on_the_map(sense_setup_t *w)
{
    fresh(LINK_MINOR_SENSE_MAP);
    sense_link_set_map(&sl, true);
    pg.sense[LINK_SN_SDA_PIN] = 16u;
    pg.sense[LINK_SN_SCL_PIN] = 17u;
    uint16_t map[LINK_SM_KEPT];
    memset(map, 0, sizeof(map));
    map[LINK_SM_ENABLE] = 0x01u;
    map[LINK_SM_FAST] = LINK_SM_FAST_NONE;
    map[LINK_SM_SHUNT(0)] = 1000u;
    CHECK_EQ(sense_map_write(&pg, 0u, (uint8_t)LINK_SM_KEPT, map, &o, 0u), 0u);
    CHECK((pg.sense[LINK_SN_ENABLE] & LINK_SN_EN_I3221) != 0u);
    *w = setup_default();
    w->i228 = true;
    w->i3221 = true;
    w->i3221_parts = 0x01u;
    want(w);
    polls(20);
    CHECK_EQ(pg.sense[LINK_SN_ENABLE], LINK_SN_EN_I228 | LINK_SN_EN_I3221);
    CHECK_EQ(sense_link_unheld(&sl), 0u);
    CHECK_EQ(sense_link_events(&sl) & SENSE_LINK_EV_SETUP, 0u);
}

/* The polls after an edit: the mirror bit stands at every one of them. */
static void polls_mirror_stands(int n)
{
    for (int i = 0; i < n; ++i) {
        poll_once();
        CHECK((pg.sense[LINK_SN_ENABLE] & LINK_SN_EN_I3221) != 0u);
    }
}

TEST_CASE(a_shunt_or_maximum_edit_reaches_the_page_with_the_map_in_force)
{
    static const struct { uint16_t uohm, da; } k_pairs[] = {
        { 199u, 2048u }, { 201u, 2048u }, { 200u, 2049u },
        { 200u, 2048u }, { 50u, 3000u }, { 20000u, 81u },
    };
    sense_setup_t w;
    both_on_the_map(&w);
    for (size_t i = 0u; i < sizeof(k_pairs) / sizeof(k_pairs[0]); ++i) {
        w.i228_uohm = k_pairs[i].uohm;
        w.i228_max_da = k_pairs[i].da;
        want(&w);
        polls_mirror_stands(20);
        CHECK_EQ(pg.sense[LINK_SN_I228_SHUNT_UOHM], k_pairs[i].uohm);
        CHECK_EQ(pg.sense[LINK_SN_I228_MAX_DA], k_pairs[i].da);
        CHECK_EQ(pg.sense[LINK_SN_ENABLE], LINK_SN_EN_I228 | LINK_SN_EN_I3221);
        CHECK_EQ(sense_link_unheld(&sl), 0u);
        CHECK_EQ(sense_link_events(&sl) & SENSE_LINK_EV_SETUP, 0u);
    }
}

/* The encoder shares the bus and stays on through the INA228's frame. */
TEST_CASE(the_encoder_stays_on_through_an_ina228_edit_on_the_map)
{
    sense_setup_t w;
    both_on_the_map(&w);
    w.as5600 = true;
    want(&w);
    polls(20);
    CHECK((pg.sense[LINK_SN_ENABLE] & LINK_SN_EN_AS5600) != 0u);
    w.i228_uohm = 201u;
    want(&w);
    for (int i = 0; i < 20; ++i) {
        poll_once();
        CHECK((pg.sense[LINK_SN_ENABLE] & LINK_SN_EN_AS5600) != 0u);
    }
    CHECK_EQ(pg.sense[LINK_SN_I228_SHUNT_UOHM], 201u);
    CHECK_EQ(pg.sense[LINK_SN_ENABLE], LINK_SN_EN_ALL);
}

/* Without the map the frame still goes with both monitors off. */
TEST_CASE(without_the_map_both_monitors_go_off_for_a_frame)
{
    fresh(LINK_MINOR_SENSE_MAP - 1u);
    sense_setup_t w = setup_default();
    w.i228 = true;
    w.i3221 = true;
    want(&w);
    polls(20);
    CHECK_EQ(pg.sense[LINK_SN_ENABLE], LINK_SN_EN_I228 | LINK_SN_EN_I3221);
    w.i228_uohm = 201u;
    want(&w);
    now += SENSE_LINK_SETTLE_MS;
    sense_link_op_t op;
    CHECK(sense_link_next(&sl, now, true, &op));
    CHECK_EQ(op.kind, SENSE_LINK_OP_OFF);
    CHECK_EQ(op.regs[0], 0u);
    sl.pending = SENSE_LINK_OP_NONE;
    polls(20);
    CHECK_EQ(pg.sense[LINK_SN_I228_SHUNT_UOHM], 201u);
}

/* A pair past the top: the INA228's frame is what is said, not the pins; the
 * INA3221 stays; stepping back ends it. */
TEST_CASE(a_pair_past_the_top_is_the_ina228s_refusal_not_the_pins)
{
    sense_setup_t w;
    both_on_the_map(&w);
    w.i228_uohm = 1000u;
    want(&w);
    polls_mirror_stands(20);
    const uint32_t ev = sense_link_events(&sl);
    CHECK((ev & SENSE_LINK_EV_I228_REFUSED) != 0u);
    CHECK_EQ(ev & (SENSE_LINK_EV_BUS_REFUSED | SENSE_LINK_EV_KEPT), 0u);
    CHECK_EQ(pg.sense[LINK_SN_I228_SHUNT_UOHM], 200u);
    CHECK_EQ(pg.sense[LINK_SN_ENABLE], LINK_SN_EN_I3221);
    CHECK((sense_link_unheld(&sl) & SENSE_LINK_ROW_I228_SHUNT) != 0u);
    w.i228_uohm = 200u;
    want(&w);
    polls_mirror_stands(20);
    CHECK_EQ(pg.sense[LINK_SN_ENABLE], LINK_SN_EN_I228 | LINK_SN_EN_I3221);
    CHECK_EQ(sense_link_unheld(&sl), 0u);
}

/* The bank driving at the IOmcu while the panel thinks it idle refuses the
 * frame that takes the monitors off.  Pins are not what it judged. */
TEST_CASE(a_page_that_keeps_its_parts_is_not_said_to_refuse_the_pins)
{
    sense_setup_t w;
    both_on_the_map(&w);
    outputs_arm(&o, true, 0u);
    CHECK(outputs_driving(&o));
    w.i228_uohm = 201u;
    want(&w);
    polls(20);
    const uint32_t ev = sense_link_events(&sl);
    CHECK((ev & SENSE_LINK_EV_KEPT) != 0u);
    CHECK_EQ(ev & SENSE_LINK_EV_BUS_REFUSED, 0u);
    CHECK(sense_link_event_stands(&sl, SENSE_LINK_EV_KEPT));
    CHECK_EQ(pg.sense[LINK_SN_I228_SHUNT_UOHM], 200u);
    /* Written once, not again until the next edit. */
    const unsigned before = writes;
    polls(20);
    CHECK_EQ(writes, before);
    outputs_arm(&o, false, 0u);
    polls(20);
    CHECK_EQ(writes, before);
    CHECK(sense_link_event_stands(&sl, SENSE_LINK_EV_KEPT));
    /* The next edit asks again, and that ends it. */
    w.i228_uohm = 202u;
    want(&w);
    CHECK(!sense_link_event_stands(&sl, SENSE_LINK_EV_KEPT));
    polls(20);
    CHECK_EQ(pg.sense[LINK_SN_I228_SHUNT_UOHM], 202u);
    CHECK_EQ(sense_link_events(&sl) & SENSE_LINK_EV_KEPT, 0u);
}

/* A link gone takes the event with it; one put back after is not kept. */
TEST_CASE(a_kept_event_goes_with_the_link)
{
    sense_setup_t w;
    both_on_the_map(&w);
    outputs_arm(&o, true, 0u);
    w.i228_uohm = 201u;
    want(&w);
    polls(20);
    sense_link_lost(&sl);
    CHECK_EQ(sense_link_event(&sl, now + SENSE_LINK_EVENT_GAP_MS), 0u);
    sense_link_event_back(&sl, SENSE_LINK_EV_KEPT);
    CHECK_EQ(sense_link_event(&sl, now + SENSE_LINK_EVENT_GAP_MS), 0u);
}

TEST_CASE(the_alert_names_the_pair_and_its_numbers)
{
    char line[120];
    ui_text_set_language(UI_LANG_EN);
    CHECK_STR_EQ(sensors_rows_i228_alert(line, sizeof(line), 1000u, 2048u),
                 "INA228 shunt x maximum is over 163.84mV: on 1000uΩ up to "
                 "163.8A. Check SENSORS.");
    /* At the top it is not this pair: the general words. */
    CHECK_STR_EQ(sensors_rows_i228_alert(line, sizeof(line), 800u, 2048u),
                 "INA228 shunt or maximum current rejected. Check SENSORS.");
    /* One past it, the first pair it names. */
    CHECK_STR_EQ(sensors_rows_i228_alert(line, sizeof(line), 801u, 2048u),
                 "INA228 shunt x maximum is over 163.84mV: on 801uΩ up to "
                 "204.5A. Check SENSORS.");
    CHECK_STR_EQ(sensors_rows_i228_alert(NULL, 0u, 1000u, 2048u), "");
    /* The longest in each language holds the band's two rows. */
    for (int lang = 0; lang < 2; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        CHECK(ui_router_alert_rows(sensors_rows_i228_alert(
                  line, sizeof(line), 20000u, 3000u)) <= 2);
        CHECK(ui_router_alert_rows(TR(ALERT_SENSE_KEPT)) <= 2);
    }
    ui_text_set_language(UI_LANG_DE);
    CHECK_STR_EQ(sensors_rows_i228_alert(line, sizeof(line), 1000u, 2048u),
                 "INA228 Shunt x Höchststrom über 163.84mV: an 1000uΩ bis "
                 "163.8A. SENSORS prüfen.");
    ui_text_set_language(UI_LANG_EN);
}

/* The rows say what works with the other row before anything is written. */
TEST_CASE(the_rows_name_what_works_with_the_other_row)
{
    ui_text_set_language(UI_LANG_EN);
    settings_set_store(NULL);
    settings_init();
    settings_screen_set_unheld(0u, 0u);
    /* The defaults are a working pair. */
    CHECK_EQ(settings_get_int(SET_INA228_UOHM), 200);
    CHECK(!sensors_rows_i228_over());
    CHECK(driver_takes(200u, 2048u));
    CHECK_STR_EQ(settings_screen_help(SET_INA228_MAX_A),
                 "Up to 300.0A on 200uΩ (163.84mV)");
    CHECK_STR_EQ(settings_screen_help(SET_INA228_UOHM),
                 "Up to 800uΩ at 204.8A (163.84mV)");
    settings_set(SET_INA228_UOHM, 800.0f);
    CHECK(!sensors_rows_i228_over());
    settings_set(SET_INA228_UOHM, 801.0f);
    CHECK(sensors_rows_i228_over());
    /* A pair past the top, marked: the same line, in the mark's colour. */
    settings_screen_set_unheld(SENSE_LINK_ROW_I228_SHUNT
                               | SENSE_LINK_ROW_I228_MAX, 0u);
    CHECK_STR_EQ(settings_screen_help(SET_INA228_MAX_A),
                 "Up to 204.5A on 801uΩ (163.84mV)");
    /* A good pair marked, a write that waits: the general line. */
    settings_set(SET_INA228_UOHM, 200.0f);
    CHECK_STR_EQ(settings_screen_help(SET_INA228_UOHM),
                 "Not accepted by the IOmcu.");
    settings_screen_set_unheld(0u, 0u);
    /* The ends of the rows. */
    settings_set(SET_INA228_UOHM, 20000.0f);
    CHECK_STR_EQ(settings_screen_help(SET_INA228_MAX_A),
                 "Up to 8.1A on 20000uΩ (163.84mV)");
    settings_set(SET_INA228_MAX_A, 1.0f);
    CHECK_STR_EQ(settings_screen_help(SET_INA228_UOHM),
                 "Up to 20000uΩ at 1.0A (163.84mV)");
    CHECK_STR_EQ(sensors_rows_i228_line(SET_INA228_EN), "");
    /* Other rows keep their help. */
    CHECK_STR_EQ(settings_screen_help(SET_INA228_ADDR),
                 ui_setting_help(SET_INA228_ADDR));
    ui_text_set_language(UI_LANG_DE);
    settings_set(SET_INA228_UOHM, 200.0f);
    settings_set(SET_INA228_MAX_A, 204.8f);
    CHECK_STR_EQ(settings_screen_help(SET_INA228_MAX_A),
                 "Bis 300.0A an 200uΩ (163.84mV)");
    CHECK_STR_EQ(settings_screen_help(SET_INA228_UOHM),
                 "Bis 800uΩ bei 204.8A (163.84mV)");
    ui_text_set_language(UI_LANG_EN);
}

int main(void)
{
    RUN(the_rule_the_rows_name_is_the_drivers_at_every_shunt);
    RUN(the_defaults_are_a_pair_and_a_step_either_way_is_too);
    RUN(a_shunt_or_maximum_edit_reaches_the_page_with_the_map_in_force);
    RUN(the_encoder_stays_on_through_an_ina228_edit_on_the_map);
    RUN(without_the_map_both_monitors_go_off_for_a_frame);
    RUN(a_pair_past_the_top_is_the_ina228s_refusal_not_the_pins);
    RUN(a_page_that_keeps_its_parts_is_not_said_to_refuse_the_pins);
    RUN(a_kept_event_goes_with_the_link);
    RUN(the_alert_names_the_pair_and_its_numbers);
    RUN(the_rows_name_what_works_with_the_other_row);
    return test_summary("sense_i228_setup");
}
