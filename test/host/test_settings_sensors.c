/*
 * SETUP > SENSORS' settings: the INA3221 parts' rows of the schema, a
 * shunt's 0 below 5.0mOhm, the seeds from the one INA3221 of 0.21, the
 * map the rows make (settings_sensors.h), and which rows SETUP lists
 * (sensors_rows.h).
 *
 * SPDX-License-Identifier: MIT
 */
#include "settings_rig.h"

#include "sense_limit_link.h"
#include "sensors_rows.h"
#include "settings_sensors.h"
#include "ui_screen.h"

/* ------------------------------------------------------------ the rows */

/* The bench's walk: INA3221 address minus at 0x40 went to 0x43.  The
 * monitors' addresses are numbers in a list and stop at its ends, both
 * ways, by single steps, held steps and steps past the whole list; the
 * observer hears nothing at an end.  The INA3221's address is one of the
 * four kept keys of 0.21 now, on no screen, and keeps the rule. */
static unsigned g_addr_heard;
static void addr_heard(setting_id_t id)
{
    (void)id;
    ++g_addr_heard;
}

TEST_CASE(the_monitor_addresses_stop_at_their_ends)
{
    static const setting_id_t k_ids[] = { SET_INA228_ADDR, SET_INA3221_ADDR };
    for (size_t k = 0u; k < sizeof(k_ids) / sizeof(k_ids[0]); ++k) {
        fresh_model();
        const setting_id_t id = k_ids[k];
        const setting_def_t *d = settings_def(id);
        const int last = d->option_count - 1;
        CHECK(!settings_cycles(id));
        settings_set(id, 0.0f);
        settings_set_observer(addr_heard);
        g_addr_heard = 0u;
        settings_adjust(id, -1);            /* 0x40 minus */
        CHECK_EQ(settings_get_int(id), 0);
        settings_adjust(id, -1000);
        CHECK_EQ(settings_get_int(id), 0);
        CHECK_EQ(g_addr_heard, 0u);
        settings_adjust(id, 1);
        CHECK_EQ(settings_get_int(id), 1);
        settings_adjust(id, 1000);          /* past the end */
        CHECK_EQ(settings_get_int(id), last);
        settings_adjust(id, 1);             /* the last plus */
        CHECK_EQ(settings_get_int(id), last);
        settings_adjust(id, 2147483647);
        CHECK_EQ(settings_get_int(id), last);
        settings_adjust(id, -2147483647 - 1);
        CHECK_EQ(settings_get_int(id), 0);
        CHECK_EQ(g_addr_heard, 3u);         /* 0->1, 1->last, last->0 */
        settings_set_observer(NULL);
    }
    /* A switch and a choice still go round, and say so. */
    CHECK(settings_cycles(SET_LANGUAGE));
    CHECK(settings_cycles(SET_INA228_EN));
    CHECK(settings_cycles(SET_INA3221_CH));
    CHECK(!settings_cycles(SET_OUT_MIN_US));
    CHECK(!settings_cycles(SET_SENSE_SDA));
    CHECK(!settings_cycles((setting_id_t)SETTING_COUNT));
    CHECK(!settings_cycles((setting_id_t)-1));
}


TEST_CASE(every_row_names_its_part_and_has_its_own_key)
{
    fresh_model();
    for (int a = 0; a < SET_INA_PARTS; ++a) {
        CHECK_EQ(settings_sensors_part(SET_INA_FIT(a)), a);
        char want[32];
        snprintf(want, sizeof(want), "ina_fit_%d", a);
        CHECK_STR_EQ(settings_def(SET_INA_FIT(a))->key, want);
        for (int ch = 0; ch < 3; ++ch) {
            const int c = 3 * a + ch;
            CHECK_EQ(settings_sensors_part(SET_INA_W(c)), a);
            CHECK_EQ(settings_sensors_part(SET_INA_SH(c)), a);
            snprintf(want, sizeof(want), "ina_w_%d%d", a, ch + 1);
            CHECK_STR_EQ(settings_def(SET_INA_W(c))->key, want);
            snprintf(want, sizeof(want), "ina_sh_%d%d", a, ch + 1);
            CHECK_STR_EQ(settings_def(SET_INA_SH(c))->key, want);
            CHECK_EQ(settings_def(SET_INA_W(c))->cat, SET_CAT_SENSORS);
            CHECK_EQ(settings_def(SET_INA_W(c))->option_count, SENS_W_COUNT);
        }
    }
    /* Not a SENSORS row: no part, and the kept keys are rows of no
     * category. */
    CHECK_EQ(settings_sensors_part(SET_HITEC_RX), -1);
    CHECK_EQ(settings_sensors_part((setting_id_t)SETTING_COUNT), -1);
    CHECK(!settings_sensors_row(SET_INA3221_EN));
    CHECK_EQ(settings_def(SET_INA3221_EN)->cat, SET_CAT_COUNT);
    CHECK_STR_EQ(settings_def(SET_INA3221_MOHM)->key, "ina3221_mohm");
    CHECK(settings_sensors_def(SET_HITEC_RX) == NULL);
    setting_id_t ids[48];
    CHECK_EQ(settings_in_category(SET_CAT_SENSORS, ids, 48),
             SET_INA_PARTS + SET_INA_CHANNELS * SET_INA_CH_ROWS);
}

TEST_CASE(every_watch_option_is_the_page_code)
{
    CHECK_EQ(settings_sensors_watch_code(SENS_W_NONE), LINK_SM_WATCH_NONE);
    for (int n = 0; n < 8; ++n) {
        CHECK_EQ(settings_sensors_watch_code(SENS_W_OUT_1 + n),
                 LINK_SM_WATCH_OUT(n));
    }
    for (int n = 0; n < 4; ++n) {
        CHECK_EQ(settings_sensors_watch_code(SENS_W_PORT_1 + n),
                 LINK_SM_WATCH_PORT(n));
    }
    CHECK_EQ(settings_sensors_watch_code(SENS_W_SUPPLY), LINK_SM_WATCH_SUPPLY);
    /* Out of the list: nothing watched. */
    CHECK_EQ(settings_sensors_watch_code(-1), LINK_SM_WATCH_NONE);
    CHECK_EQ(settings_sensors_watch_code(SENS_W_COUNT), LINK_SM_WATCH_NONE);
}

/*
 * A shunt takes 0, off, or 5.0 to 1000.0mOhm: + from 0 is 5.0 and - from
 * 5.0 is 0, a held key runs on from there, and a stored word between 0 and
 * 5.0 is the nearer of the two.
 */
TEST_CASE(a_shunt_is_off_or_from_five_milliohm)
{
    fresh_model();
    const setting_id_t id = SET_INA_SH(4);
    settings_set(id, 0.0f);
    CHECK_NEAR(settings_get(id), 0.0f, 1e-6);
    settings_adjust(id, 1);
    CHECK_NEAR(settings_get(id), 5.0f, 1e-4);
    settings_adjust(id, 1);
    CHECK_NEAR(settings_get(id), 5.1f, 1e-4);
    settings_adjust(id, -1);
    settings_adjust(id, -1);
    CHECK_NEAR(settings_get(id), 0.0f, 1e-6);
    settings_adjust(id, -1);                    /* 0 minus */
    CHECK_NEAR(settings_get(id), 0.0f, 1e-6);
    settings_adjust(id, 3);                     /* a held key from 0 */
    CHECK_NEAR(settings_get(id), 5.2f, 1e-4);
    settings_set(id, 100.0f);
    settings_adjust(id, -1000);                 /* past 5.0 down */
    CHECK_NEAR(settings_get(id), 0.0f, 1e-6);
    settings_set(id, 1000.0f);
    settings_adjust(id, 1);
    CHECK_NEAR(settings_get(id), 1000.0f, 1e-3);
    /* Words between: the nearer end. */
    settings_set(id, 2.4f);
    CHECK_NEAR(settings_get(id), 0.0f, 1e-6);
    settings_set(id, 2.5f);
    CHECK_NEAR(settings_get(id), 5.0f, 1e-4);
    settings_set(id, 4.99f);
    CHECK_NEAR(settings_get(id), 5.0f, 1e-4);
    settings_set(id, -3.0f);
    CHECK_NEAR(settings_get(id), 0.0f, 1e-6);
    CHECK_EQ(settings_sensors_dmohm(4), 0u);
    settings_set(id, 50.0f);
    CHECK_EQ(settings_sensors_dmohm(4), 500u);
    CHECK_EQ(settings_sensors_dmohm(-1), 0u);
    CHECK_EQ(settings_sensors_dmohm(SET_INA_CHANNELS), 0u);
}

/* ------------------------------------------------------- the migration */

/* A store that holds some keys and not others, as NVS does after an
 * update: only the ids marked are written by the load. */
static bool  s_has_key[SETTING_COUNT];
static float s_key_val[SETTING_COUNT];

static bool keys_load(float *values, int count)
{
    bool any = false;
    for (int i = 0; i < count && i < SETTING_COUNT; ++i) {
        if (s_has_key[i]) {
            values[i] = s_key_val[i];
            any = true;
        }
    }
    return any;
}

static bool keys_save(const float *values, int count)
{
    for (int i = 0; i < count && i < SETTING_COUNT; ++i) {
        s_has_key[i] = true;
        s_key_val[i] = values[i];
    }
    return true;
}

static const settings_store_t s_keys_store = { keys_load, keys_save, NULL,
                                               NULL };

static void key(setting_id_t id, float v)
{
    s_has_key[id] = true;
    s_key_val[id] = v;
}

/* A 0.21 panel's store: the one INA3221 at 0x40 + @p adr, on or off, its
 * shunt, CH1 or all three, and the servo tester's target. */
static void old_panel(bool en, int adr, float mohm, bool all, int target)
{
    memset(s_has_key, 0, sizeof(s_has_key));
    key(SET_INA3221_EN, en ? 1.0f : 0.0f);
    key(SET_INA3221_ADDR, (float)adr);
    key(SET_INA3221_MOHM, mohm);
    key(SET_INA3221_CH, all ? 1.0f : 0.0f);
    key(SET_SERVO_TARGET, (float)target);
    key(SET_THEME, 1.0f);
}

static void load(void)
{
    settings_set_observer(NULL);
    settings_set_store(&s_keys_store);
    settings_init();
}

TEST_CASE(a_fresh_panel_starts_on_the_defaults)
{
    fresh_model();
    for (int a = 0; a < SET_INA_PARTS; ++a) {
        CHECK(!settings_get_bool(SET_INA_FIT(a)));
    }
    for (int c = 0; c < SET_INA_CHANNELS; ++c) {
        CHECK_NEAR(settings_get(SET_INA_SH(c)), 100.0f, 1e-4);
        CHECK_EQ(settings_get_int(SET_INA_W(c)),
                 (c == 0) ? SENS_W_OUT_1 : SENS_W_NONE);
    }
    CHECK(!settings_dirty());
    /* A store with nothing in it is the same panel. */
    memset(s_has_key, 0, sizeof(s_has_key));
    load();
    CHECK_EQ(settings_get_int(SET_INA_W(0)), SENS_W_OUT_1);
    CHECK_EQ(settings_sensors_fitted(), 0u);
    CHECK(!settings_dirty());
}

/* 0.21 with the INA3221 at 0x41 on 50mOhm, CH1: the part is fitted, its
 * CH1 on the old shunt and watching the outputs, CH2 and CH3 off; every
 * other part as a fresh panel has it. */
TEST_CASE(an_old_set_up_on_ch1_seeds_its_part)
{
    old_panel(true, 1, 50.0f, false, 0);
    load();
    CHECK_EQ(settings_sensors_fitted(), 0x02u);
    CHECK_NEAR(settings_get(SET_INA_SH(3)), 50.0f, 1e-4);
    CHECK_NEAR(settings_get(SET_INA_SH(4)), 0.0f, 1e-6);
    CHECK_NEAR(settings_get(SET_INA_SH(5)), 0.0f, 1e-6);
    CHECK_EQ(settings_get_int(SET_INA_W(3)), SENS_W_OUT_1);
    CHECK_EQ(settings_get_int(SET_INA_W(0)), SENS_W_NONE);
    CHECK_NEAR(settings_get(SET_INA_SH(0)), 100.0f, 1e-4);
    CHECK_NEAR(settings_get(SET_INA_SH(11)), 100.0f, 1e-4);
    /* Seeded as a default is: nothing to save. */
    CHECK(!settings_dirty());
    CHECK(!settings_save_asked());
    /* The old keys stay as stored, for a panel taken back to 0.21. */
    CHECK(settings_get_bool(SET_INA3221_EN));
    CHECK_EQ(settings_get_int(SET_INA3221_ADDR), 1);
    /* The bench reads as before: 0x41, 50mOhm, CH1, the meter on it. */
    settings_sensors_map_t m;
    settings_sensors_map(&m);
    CHECK_EQ(m.fast, 3u);
    CHECK_EQ(m.mirror, 1u);
    CHECK(m.one_on);
    CHECK_EQ(m.one_addr, 0x41u);
    CHECK_EQ(m.one_dmohm, 500u);
    CHECK_EQ(m.one_ch, 0x01u);
}

TEST_CASE(an_old_set_up_on_all_three_channels_seeds_them_all)
{
    old_panel(true, 3, 20.0f, true, 0);
    load();
    CHECK_EQ(settings_sensors_fitted(), 0x08u);
    for (int c = 9; c < 12; ++c) {
        CHECK_NEAR(settings_get(SET_INA_SH(c)), 20.0f, 1e-4);
    }
    settings_sensors_map_t m;
    settings_sensors_map(&m);
    CHECK_EQ(m.one_addr, 0x43u);
    CHECK_EQ(m.one_ch, 0x07u);
    CHECK_EQ(m.one_dmohm, 200u);
}

/* Owner decision 1: CH1 watches PORT 1 when the stored target was the
 * programming port, OUT 1 otherwise; with the INA3221 off, 0x40's CH1. */
TEST_CASE(ch1_watches_the_port_the_tester_drove)
{
    old_panel(true, 2, 100.0f, false, 1);
    load();
    CHECK_EQ(settings_get_int(SET_INA_W(6)), SENS_W_PORT_1);
    CHECK_EQ(settings_get_int(SET_INA_W(0)), SENS_W_NONE);
    old_panel(false, 2, 100.0f, false, 1);
    load();
    CHECK_EQ(settings_sensors_fitted(), 0u);
    CHECK_EQ(settings_get_int(SET_INA_W(0)), SENS_W_PORT_1);
    CHECK_EQ(settings_get_int(SET_INA_W(6)), SENS_W_NONE);
    /* Off, its old address and shunt seed nothing: every shunt 100. */
    CHECK_NEAR(settings_get(SET_INA_SH(6)), 100.0f, 1e-4);
    CHECK_NEAR(settings_get(SET_INA_SH(7)), 100.0f, 1e-4);
}

/* A key the store holds is never seeded over; one it lacks is, and once a
 * save has written them all an edit of the old keys moves nothing. */
TEST_CASE(a_stored_sensors_key_wins_over_its_seed)
{
    old_panel(true, 0, 50.0f, false, 0);
    key(SET_INA_FIT(2), 1.0f);
    key(SET_INA_SH(0), 30.0f);
    key(SET_INA_W(0), (float)SENS_W_SUPPLY);
    load();
    CHECK_EQ(settings_sensors_fitted(), 0x05u);
    CHECK_NEAR(settings_get(SET_INA_SH(0)), 30.0f, 1e-4);
    CHECK_EQ(settings_get_int(SET_INA_W(0)), SENS_W_SUPPLY);
    CHECK_NEAR(settings_get(SET_INA_SH(1)), 0.0f, 1e-6);   /* seeded */
    /* A stored word off the grid is coerced as any other. */
    key(SET_INA_SH(0), 3.0f);
    key(SET_INA_W(0), 99.0f);
    load();
    CHECK_NEAR(settings_get(SET_INA_SH(0)), 5.0f, 1e-4);
    CHECK_EQ(settings_get_int(SET_INA_W(0)), SENS_W_OUT_1);
    /* Saved: every key is the store's, and the old ones stay. */
    settings_set(SET_INA_SH(1), 7.0f);
    CHECK(settings_save());
    key(SET_INA3221_EN, 0.0f);
    key(SET_INA3221_CH, 1.0f);
    load();
    CHECK_EQ(settings_sensors_fitted(), 0x05u);
    CHECK_NEAR(settings_get(SET_INA_SH(1)), 7.0f, 1e-4);
    CHECK_NEAR(settings_get(SET_INA_SH(2)), 0.0f, 1e-6);
    fresh_model();
}

/* ------------------------------------------------------------- the map */

TEST_CASE(nothing_fitted_maps_nothing_and_a_frame_the_page_takes)
{
    fresh_model();
    settings_sensors_map_t m;
    settings_sensors_map(&m);
    CHECK_EQ(m.parts, 0u);
    CHECK_EQ(m.dark, 0u);
    CHECK_EQ(m.fast, LINK_SM_FAST_NONE);
    CHECK_EQ(m.mirror, 0xFFu);
    CHECK(!m.one_on);
    CHECK_EQ(m.one_addr, 0x40u);
    CHECK_EQ(m.one_dmohm, 1000u);
    CHECK_EQ(m.one_ch, 0x01u);
    CHECK_EQ(m.regs[LINK_SM_ENABLE], 0u);
    CHECK_EQ(m.regs[LINK_SM_FAST], LINK_SM_FAST_NONE);
    /* The channels' values go as they are, fitted or not. */
    CHECK_EQ(m.regs[LINK_SM_SHUNT(11)], 1000u);
    CHECK_EQ(m.regs[LINK_SM_WATCH(0)], LINK_SM_WATCH_OUT(0));
    CHECK_EQ(settings_sensors_meter_dmohm(), 0u);
}

/* An older IOmcu's frame carries one shunt for every channel it reads: a
 * channel on another shunt is left out of it, an OFF one as well. */
TEST_CASE(the_one_part_frame_reads_the_channels_on_its_shunt)
{
    fresh_model();
    settings_set(SET_INA_FIT(1), 1.0f);
    settings_set(SET_INA_SH(3), 100.0f);
    settings_set(SET_INA_SH(4), 20.0f);
    settings_set(SET_INA_SH(5), 100.0f);
    settings_sensors_map_t m;
    settings_sensors_map(&m);
    CHECK(m.one_on);
    CHECK_EQ(m.one_addr, 0x41u);
    CHECK_EQ(m.one_dmohm, 1000u);
    CHECK_EQ(m.one_ch, 0x05u);
    /* CH1 OFF: the first shunt not OFF is the frame's. */
    settings_set(SET_INA_SH(3), 0.0f);
    settings_sensors_map(&m);
    CHECK_EQ(m.one_dmohm, 200u);
    CHECK_EQ(m.one_ch, 0x02u);
    /* The map itself keeps every channel's own shunt. */
    CHECK_EQ(m.regs[LINK_SM_SHUNT(5)], 1000u);
}

/*
 * FAST is the lowest channel with a shunt on a fitted part watching the
 * tester's target: an output for OUTPUTS, port n for PORT n.  The mirror
 * part is FAST's, else the lowest fitted.
 */
TEST_CASE(fast_is_the_lowest_channel_watching_the_target)
{
    fresh_model();
    settings_set(SET_INA_FIT(1), 1.0f);
    settings_set(SET_INA_FIT(2), 1.0f);
    settings_set(SET_INA_W(3), (float)SENS_W_SUPPLY);
    settings_set(SET_INA_W(4), (float)(SENS_W_OUT_1 + 7));   /* OUT 8 */
    settings_set(SET_INA_W(5), (float)SENS_W_OUT_1);
    settings_set(SET_INA_W(8), (float)(SENS_W_PORT_1 + 1));  /* PORT 2 */
    settings_sensors_map_t m;
    settings_sensors_map(&m);
    /* 0x40 CH1 watches OUT 1 and is not fitted. */
    CHECK_EQ(m.parts, 0x06u);
    CHECK_EQ(m.fast, 4u);
    CHECK_EQ(m.mirror, 1u);
    CHECK_EQ(m.regs[LINK_SM_ENABLE], 0x06u);
    CHECK_EQ(m.regs[LINK_SM_FAST], 4u);
    CHECK_EQ(m.regs[LINK_SM_WATCH(3)], LINK_SM_WATCH_SUPPLY);
    CHECK_EQ(m.regs[LINK_SM_WATCH(4)], LINK_SM_WATCH_OUT(7));
    CHECK_EQ(m.regs[LINK_SM_WATCH(8)], LINK_SM_WATCH_PORT(1));
    CHECK_EQ(settings_sensors_fast(), 4u);
    CHECK_EQ(settings_sensors_meter_dmohm(), 1000u);
    /* A channel off is passed over. */
    settings_set(SET_INA_SH(4), 0.0f);
    CHECK_EQ(settings_sensors_fast(), 5u);
    /* The tester on PORT 2: 0x42 CH3. */
    settings_set(SET_SERVO_TARGET, 1.0f);
    settings_sensors_map(&m);
    CHECK_EQ(m.fast, LINK_SM_FAST_NONE);       /* PORT 1: none watches */
    CHECK_EQ(m.mirror, 1u);                    /* the lowest fitted */
    settings_set(SET_INA_W(8), (float)SENS_W_PORT_1);
    settings_sensors_map(&m);
    CHECK_EQ(m.fast, 8u);
    CHECK_EQ(m.mirror, 2u);
    CHECK_EQ(m.one_addr, 0x42u);
    settings_set(SET_SERVO_TARGET, 0.0f);
}

TEST_CASE(a_fitted_part_with_every_channel_off_is_dark)
{
    fresh_model();
    settings_set(SET_INA_FIT(0), 1.0f);
    settings_set(SET_INA_FIT(3), 1.0f);
    for (int c = 9; c < 12; ++c) {
        settings_set(SET_INA_SH(c), 0.0f);
    }
    /* And 0x41, not fitted, dark too: no matter. */
    for (int c = 3; c < 6; ++c) {
        settings_set(SET_INA_SH(c), 0.0f);
    }
    settings_sensors_map_t m;
    settings_sensors_map(&m);
    CHECK_EQ(m.dark, 0x08u);
    CHECK_EQ(m.parts, 0x09u);
    CHECK_EQ(m.fast, 0u);
    /* The one-part frame: CH1 off, CH2 on 50mOhm, CH3 on 100 -- a
     * shunt the frame does not carry, so CH2 alone. */
    settings_set(SET_INA_SH(0), 0.0f);
    settings_set(SET_INA_SH(1), 50.0f);
    settings_sensors_map(&m);
    CHECK_EQ(m.fast, LINK_SM_FAST_NONE);
    CHECK_EQ(m.mirror, 0u);
    CHECK(m.one_on);
    CHECK_EQ(m.one_dmohm, 500u);
    CHECK_EQ(m.one_ch, 0x02u);
    CHECK_EQ(settings_sensors_meter_dmohm(), 500u);
    /* The mirror part dark: no part to read on an older IOmcu. */
    settings_set(SET_INA_SH(1), 0.0f);
    settings_set(SET_INA_SH(2), 0.0f);
    settings_sensors_map(&m);
    CHECK(!m.one_on);
    CHECK_EQ(m.dark, 0x09u);
    CHECK_EQ(settings_sensors_meter_dmohm(), 0u);
}

/* ------------------------------------------------------- SETUP's list */

TEST_CASE(sensors_lists_a_fitted_parts_rows_under_its_heading)
{
    fresh_model();
    /* Nothing fitted: the heading and the four FITTED rows. */
    CHECK_EQ(settings_screen_rows(SET_CAT_SENSORS), 5);
    CHECK(sensors_rows_listed(SET_INA_FIT(3)));
    CHECK(!sensors_rows_listed(SET_INA_W(0)));
    CHECK(sensors_rows_listed(SET_HITEC_RX));
    CHECK_EQ(sensors_rows_heading(SET_INA_W(0)), -1);
    settings_set(SET_INA_FIT(0), 1.0f);
    settings_set(SET_INA_FIT(2), 1.0f);
    /* Two parts: a heading and nine rows each. */
    CHECK_EQ(settings_screen_rows(SET_CAT_SENSORS), 5 + 2 * 10);
    CHECK_EQ(sensors_rows_heading(SET_INA_W(0)), 0);
    CHECK_EQ(sensors_rows_heading(SET_INA_SH(0)), -1);
    CHECK_EQ(sensors_rows_heading(SET_INA_W(6)), 2);
    CHECK_EQ(sensors_rows_heading(SET_INA_W(3)), -1);
    /* The fifth setting listed is 0x40 CH1 WATCHES, under its heading. */
    CHECK_EQ(settings_screen_row_of(SET_CAT_SENSORS, 0), 1);
    CHECK_EQ(settings_screen_row_of(SET_CAT_SENSORS, 4), 6);
    /* By schema index: 0x42 CH1 WATCHES under its heading, 0x41's rows
     * not listed. */
    CHECK_EQ(settings_screen_row_of(SET_CAT_SENSORS, 4 + 18), 16);
    CHECK_EQ(settings_screen_row_of(SET_CAT_SENSORS, 4 + 9), -1);
    CHECK_EQ(settings_screen_row_of(SET_CAT_SENSORS, -1), -1);
    CHECK_EQ(settings_screen_row_of(SET_CAT_SENSORS, 40), -1);
    char buf[24];
    CHECK_STR_EQ(sensors_rows_title(2, buf, sizeof(buf)), "INA3221 0x42");
    CHECK_STR_EQ(sensors_rows_title(2, NULL, 0u), "");
}

TEST_CASE(a_part_the_iomcu_does_not_read_marks_its_rows)
{
    fresh_model();
    sensors_rows_set_status(0u);
    CHECK_EQ(sensors_rows_status(0), SENS_PART_UNKNOWN);
    CHECK_STR_EQ(sensors_rows_state(0), "NOT READ YET");
    sensors_rows_set_status((uint16_t)(SENS_PART_ONLINE
                                       | (SENS_PART_OLD << 4)
                                       | (SENS_PART_DARK << 8)
                                       | (SENS_PART_SAME_ADDR << 12)));
    CHECK_EQ(sensors_rows_status(1), SENS_PART_OLD);
    CHECK_EQ(sensors_rows_status(-1), SENS_PART_UNKNOWN);
    CHECK_EQ(sensors_rows_status(4), SENS_PART_UNKNOWN);
    CHECK_STR_EQ(sensors_rows_state(0), "ONLINE");
    CHECK(!sensors_rows_unheld(SET_INA_W(0)));
    CHECK(sensors_rows_unheld(SET_INA_W(3)));
    CHECK(sensors_rows_unheld(SET_INA_SH(8)));
    CHECK(sensors_rows_unheld(SET_INA_SH(11)));
    /* A FITTED row is never marked: it is how a part is taken out. */
    CHECK(!sensors_rows_unheld(SET_INA_FIT(1)));
    CHECK(!sensors_rows_unheld(SET_HITEC_RX));
    CHECK_STR_EQ(sensors_rows_why(SET_INA_W(3)),
                 "IOmcu older than 0.22.0: one INA3221, CH1 at 1kHz.");
    CHECK_STR_EQ(sensors_rows_why(SET_INA_W(6)),
                 "Every channel off. The IOmcu takes no such part.");
    CHECK_STR_EQ(sensors_rows_why(SET_INA_W(9)),
                 "The INA228 uses this address. Neither is read.");
    sensors_rows_set_status((uint16_t)SENS_PART_REFUSED);
    CHECK(sensors_rows_unheld(SET_INA_W(0)));
    CHECK(settings_screen_unheld(SET_INA_W(0)));
    CHECK_STR_EQ(sensors_rows_why(SET_INA_W(0)), "Not accepted by the IOmcu.");
    sensors_rows_set_status((uint16_t)SENS_PART_NO_BUS);
    CHECK(sensors_rows_unheld(SET_INA_SH(2)));
    CHECK_STR_EQ(sensors_rows_why(SET_INA_SH(2)),
                 "Sensor SDA or SCL unset. Set both on INTERFACES.");
    sensors_rows_set_status(0u);
}

/* A LIMIT row the IOmcu does not hold is marked, and only that row; on an
 * IOmcu older than 0.22.0 (protocol 4.27) the line says no limit is judged.  A part's own
 * reason comes first. */
TEST_CASE(a_limit_the_iomcu_does_not_hold_marks_its_row)
{
    fresh_model();
    sensors_rows_set_status((uint16_t)SENS_PART_ONLINE);
    sensors_rows_set_limits((uint16_t)((SENSE_LIMIT_ST_PENDING << 12)
                                       | (1u << 1)));
    CHECK(sensors_rows_unheld(SET_INA_LIM(1)));
    CHECK(!sensors_rows_unheld(SET_INA_LIM(0)));
    CHECK(!sensors_rows_unheld(SET_INA_SH(1)));
    CHECK(!sensors_rows_unheld(SET_INA_W(2)));
    CHECK_STR_EQ(sensors_rows_why(SET_INA_LIM(1)),
                 "Not accepted by the IOmcu.");
    sensors_rows_set_limits((uint16_t)((SENSE_LIMIT_ST_OLD << 12)
                                       | (1u << 11)));
    CHECK(sensors_rows_unheld(SET_INA_LIM(11)));
    CHECK_STR_EQ(sensors_rows_why(SET_INA_LIM(11)),
                 "IOmcu older than 0.22.0: no limit is judged.");
    CHECK_STR_EQ(sensors_rows_why(SET_INA_W(11)),
                 "Not accepted by the IOmcu.");
    CHECK_STR_EQ(sensors_rows_why(SET_INA_FIT(0)),
                 "Not accepted by the IOmcu.");
    sensors_rows_set_status((uint16_t)(SENS_PART_DARK << 12));
    CHECK_STR_EQ(sensors_rows_why(SET_INA_LIM(11)),
                 "Every channel off. The IOmcu takes no such part.");
    /* The row reads OFF at 0 and mA as the page takes them. */
    settings_set(SET_INA_LIM(3), 1250.0f);
    CHECK_EQ(settings_sensors_limit_ma(3), 1250u);
    settings_set(SET_INA_LIM(3), 40000.0f);
    CHECK_EQ(settings_sensors_limit_ma(3), 32767u);
    settings_adjust(SET_INA_LIM(3), 1);
    CHECK_EQ(settings_sensors_limit_ma(3), 32767u);
    settings_set(SET_INA_LIM(3), 10.0f);
    settings_adjust(SET_INA_LIM(3), -1);          /* the step below 10: OFF */
    CHECK_EQ(settings_sensors_limit_ma(3), 0u);
    settings_adjust(SET_INA_LIM(3), 1);
    CHECK_EQ(settings_sensors_limit_ma(3), 10u);
    settings_set(SET_INA_LIM(3), 0.0f);
    CHECK_EQ(settings_sensors_limit_ma(12), 0u);
    CHECK_EQ(settings_sensors_limit_ma(-1), 0u);
    CHECK_EQ(settings_sensors_watch_option(LINK_SM_WATCH_PORT(2)),
             SENS_W_PORT_1 + 2);
    CHECK_EQ(settings_sensors_watch_option(0x7Fu), SENS_W_NONE);
    sensors_rows_set_limits(0u);
    sensors_rows_set_status(0u);
}

/* A FITTED edit or a reset shortens SENSORS' list under a scrolled view:
 * the next frame draws it from a scroll the shorter list has. */
TEST_CASE(the_scroll_follows_a_list_that_shortens)
{
    fresh_hub();
    for (int a = 0; a < SET_INA_PARTS; ++a) {
        settings_set(SET_INA_FIT(a), 1.0f);
    }
    open_category(SET_CAT_SENSORS);
    ui_router_render(&s_c, 0);
    int max = 0;
    (void)settings_screen_scroll(&max);
    CHECK(max > 0);
    for (int k = 0; k < 40 && settings_screen_scroll(NULL) < max; ++k) {
        swipe(XL, LIST_Y + LIST_H - 20, XL, LIST_Y + 20, 4);
    }
    CHECK_EQ(settings_screen_scroll(NULL), max);
    settings_reset(SET_CAT_SENSORS);
    ui_router_render(&s_c, 0);
    CHECK_EQ(settings_screen_scroll(&max), 0);
    CHECK_EQ(max, 0);
}

/* Every state has its words and its colour: ONLINE the ok colour, the
 * three that wait dim, the rest the warning colour. */
TEST_CASE(every_part_state_has_its_words_and_colour)
{
    static const struct { sens_part_t st; const char *en; int color; } k[] = {
        { SENS_PART_UNKNOWN,   "NOT READ YET",              UI_C_TEXT_DIM },
        { SENS_PART_PENDING,   "NOT WRITTEN YET",           UI_C_TEXT_DIM },
        { SENS_PART_SEARCHING, "LOOKING FOR THE PART",      UI_C_TEXT_DIM },
        { SENS_PART_ONLINE,    "ONLINE",                    UI_C_OK },
        { SENS_PART_SILENT,    "NOT ANSWERING",             UI_C_WARN },
        { SENS_PART_WRONG,     "ANOTHER ID ANSWERS",        UI_C_WARN },
        { SENS_PART_REFUSED,   "NOT ACCEPTED BY THE IOMCU", UI_C_WARN },
        { SENS_PART_DARK,      "EVERY CHANNEL OFF",         UI_C_WARN },
        { SENS_PART_SAME_ADDR, "INA228 ON THIS ADDRESS",    UI_C_WARN },
        { SENS_PART_OLD,       "IOMCU OLDER THAN 0.22.0: NOT READ", UI_C_WARN },
        { SENS_PART_NO_BUS,    "SENSOR SDA OR SCL UNSET",   UI_C_WARN },
    };
    for (size_t i = 0u; i < sizeof(k) / sizeof(k[0]); ++i) {
        sensors_rows_set_status((uint16_t)((unsigned)k[i].st << 8));
        CHECK_EQ(sensors_rows_status(2), k[i].st);
        CHECK_STR_EQ(sensors_rows_state(2), k[i].en);
        CHECK_EQ(sensors_rows_state_color(2),
                 ui_theme_color((ui_color_id_t)k[i].color));
    }
    /* A word past the states the build knows reads as not read yet. */
    sensors_rows_set_status((uint16_t)(0x0Fu << 4));
    CHECK_STR_EQ(sensors_rows_state(1), "NOT READ YET");
    ui_text_set_language(UI_LANG_DE);
    sensors_rows_set_status((uint16_t)SENS_PART_SILENT);
    CHECK_STR_EQ(sensors_rows_state(0), "ANTWORTET NICHT");
    char buf[24];
    CHECK_STR_EQ(sensors_rows_title(3, buf, sizeof(buf)), "INA3221 0x43");
    ui_text_set_language(UI_LANG_EN);
    sensors_rows_set_status(0u);
}

/* The band alert names the part, the channel, what it watches and the
 * limit it passed in amperes, rounded to 10mA, in both languages. */
TEST_CASE(an_over_alert_names_its_channel_target_and_limit)
{
    static const struct {
        unsigned ch; uint16_t ma; int watch; const char *en;
    } k[] = {
        { 0u, 10u, SENS_W_NONE,
          "INA3221 0x40 CH1 over its 0.01A limit." },
        { 4u, 990u, SENS_W_SUPPLY,
          "INA3221 0x41 CH2 (SUPPLY) over its 0.99A limit." },
        { 8u, 32760u, SENS_W_PORT_1,
          "INA3221 0x42 CH3 (PORT 1) over its 32.76A limit." },
        { 11u, 32767u, SENS_W_OUT_1 + 7,
          "INA3221 0x43 CH3 (OUT 8) over its 32.77A limit." },
    };
    char buf[UI_ALERT_MAX];
    for (size_t i = 0u; i < sizeof(k) / sizeof(k[0]); ++i) {
        CHECK_STR_EQ(sensors_rows_alert(buf, sizeof(buf),
                                        SENSE_LIMIT_LINK_EV_OVER, k[i].ch,
                                        k[i].ma, k[i].watch),
                     k[i].en);
    }
    CHECK_STR_EQ(sensors_rows_alert(buf, sizeof(buf),
                                    SENSE_LIMIT_LINK_EV_LOST, 0u, 0u, 0),
                 "An INA3221 channel passed its limit; which one was lost "
                 "on the link.");
    CHECK_STR_EQ(sensors_rows_alert(buf, sizeof(buf),
                                    SENSE_LIMIT_LINK_EV_REFUSED, 0u, 0u, 0),
                 "INA3221 limits rejected. Check SETUP > SENSORS.");
    /* No line for nothing, a channel past the twelfth or no buffer. */
    CHECK_STR_EQ(sensors_rows_alert(buf, sizeof(buf),
                                    SENSE_LIMIT_LINK_EV_NONE, 0u, 0u, 0), "");
    CHECK_STR_EQ(sensors_rows_alert(buf, sizeof(buf),
                                    SENSE_LIMIT_LINK_EV_OVER, 12u, 10u, 0),
                 "");
    CHECK_STR_EQ(sensors_rows_alert(NULL, 0u, SENSE_LIMIT_LINK_EV_OVER, 0u,
                                    10u, 0), "");
    ui_text_set_language(UI_LANG_DE);
    CHECK_STR_EQ(sensors_rows_alert(buf, sizeof(buf),
                                    SENSE_LIMIT_LINK_EV_OVER, 0u, 10u,
                                    SENS_W_NONE),
                 "INA3221 0x40 CH1 \xc3\xbc"
                 "ber der Grenze von 0.01A.");
    CHECK_STR_EQ(sensors_rows_alert(buf, sizeof(buf),
                                    SENSE_LIMIT_LINK_EV_OVER, 4u, 990u,
                                    SENS_W_SUPPLY),
                 "INA3221 0x41 CH2 (NETZTEIL) \xc3\xbc"
                 "ber der Grenze von 0.99A.");
    CHECK_STR_EQ(sensors_rows_alert(buf, sizeof(buf),
                                    SENSE_LIMIT_LINK_EV_OVER, 8u, 32760u,
                                    SENS_W_PORT_1),
                 "INA3221 0x42 CH3 (PORT 1) \xc3\xbc"
                 "ber der Grenze von 32.76A.");
    CHECK_STR_EQ(sensors_rows_alert(buf, sizeof(buf),
                                    SENSE_LIMIT_LINK_EV_OVER, 11u, 32767u,
                                    SENS_W_SUPPLY),
                 "INA3221 0x43 CH3 (NETZTEIL) \xc3\xbc"
                 "ber der Grenze von 32.77A.");
    CHECK_STR_EQ(sensors_rows_alert(buf, sizeof(buf),
                                    SENSE_LIMIT_LINK_EV_REFUSED, 0u, 0u, 0),
                 "INA3221-Grenzen abgelehnt. SETUP > SENSORS pr"
                 "\xc3\xbc" "fen.");
    ui_text_set_language(UI_LANG_EN);
}

/*
 * The console line of a trip: in English whatever the panel's language,
 * the channel's part and number, what it watches, the limit and the
 * highest window read, both to 0.001A; "no window read" without one; nothing for
 * a channel past the twelfth or no buffer.
 */
TEST_CASE(a_trip_line_names_its_channel_reading_and_limit)
{
    char buf[96];
    CHECK_STR_EQ(sensors_rows_trip_line(buf, sizeof(buf), 0u, SENS_W_PORT_1,
                                        true, 153, 10u),
                 "INA3221 0x40 CH1 (PORT 1) over its limit of 0.010A: "
                 "highest window read 0.153A");
    CHECK_STR_EQ(sensors_rows_trip_line(buf, sizeof(buf), 11u, SENS_W_NONE,
                                        true, 32767, 32767u),
                 "INA3221 0x43 CH3 over its limit of 32.767A: highest "
                 "window read 32.767A");
    CHECK_STR_EQ(sensors_rows_trip_line(buf, sizeof(buf), 4u, SENS_W_SUPPLY,
                                        true, -32768, 1u),
                 "INA3221 0x41 CH2 (SUPPLY) over its limit of 0.001A: "
                 "highest window read -32.768A");
    CHECK_STR_EQ(sensors_rows_trip_line(buf, sizeof(buf), 8u, SENS_W_NONE,
                                        false, 999, 1000u),
                 "INA3221 0x42 CH3 over its limit of 1.000A: no window "
                 "read");
    /* A WATCHES value past the list is said as none. */
    CHECK_STR_EQ(sensors_rows_trip_line(buf, sizeof(buf), 1u, SENS_W_COUNT,
                                        true, 0, 5u),
                 "INA3221 0x40 CH2 over its limit of 0.005A: highest window "
                 "read 0.000A");
    ui_text_set_language(UI_LANG_DE);
    CHECK_STR_EQ(sensors_rows_trip_line(buf, sizeof(buf), 4u, SENS_W_SUPPLY,
                                        true, 990, 500u),
                 "INA3221 0x41 CH2 (SUPPLY) over its limit of 0.500A: "
                 "highest window read 0.990A");
    ui_text_set_language(UI_LANG_EN);
    CHECK_STR_EQ(sensors_rows_trip_line(buf, sizeof(buf), 12u, SENS_W_NONE,
                                        true, 1, 1u), "");
    CHECK_STR_EQ(sensors_rows_trip_line(NULL, 0u, 0u, SENS_W_NONE, true, 1,
                                        1u), "");
    /* 96 bytes hold the longest: every WATCHES option, the widest
     * figures. */
    for (int w = SENS_W_NONE; w < SENS_W_COUNT; ++w) {
        char wide[200];
        (void)sensors_rows_trip_line(wide, sizeof(wide), 11u, w, true,
                                     -32768, 32767u);
        CHECK(strlen(wide) < sizeof(buf));
        CHECK(strstr(wide, "-32.768A") != NULL);
    }
    /* A short buffer is cut, not overrun. */
    char small[8];
    CHECK_STR_EQ(sensors_rows_trip_line(small, sizeof(small), 0u, SENS_W_NONE,
                                        true, 1, 1u), "INA3221");
}

int main(void)
{
    RUN(the_monitor_addresses_stop_at_their_ends);
    RUN(every_row_names_its_part_and_has_its_own_key);
    RUN(every_watch_option_is_the_page_code);
    RUN(a_shunt_is_off_or_from_five_milliohm);
    RUN(a_fresh_panel_starts_on_the_defaults);
    RUN(an_old_set_up_on_ch1_seeds_its_part);
    RUN(an_old_set_up_on_all_three_channels_seeds_them_all);
    RUN(ch1_watches_the_port_the_tester_drove);
    RUN(a_stored_sensors_key_wins_over_its_seed);
    RUN(nothing_fitted_maps_nothing_and_a_frame_the_page_takes);
    RUN(the_one_part_frame_reads_the_channels_on_its_shunt);
    RUN(fast_is_the_lowest_channel_watching_the_target);
    RUN(a_fitted_part_with_every_channel_off_is_dark);
    RUN(sensors_lists_a_fitted_parts_rows_under_its_heading);
    RUN(a_part_the_iomcu_does_not_read_marks_its_rows);
    RUN(a_limit_the_iomcu_does_not_hold_marks_its_row);
    RUN(every_part_state_has_its_words_and_colour);
    RUN(the_scroll_follows_a_list_that_shortens);
    RUN(an_over_alert_names_its_channel_target_and_limit);
    RUN(a_trip_line_names_its_channel_reading_and_limit);
    return test_summary("settings_sensors");
}
