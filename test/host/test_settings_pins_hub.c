/*
 * The sensor bus pins and the hub of tiles.
 *
 * SPDX-License-Identifier: MIT
 */
#include "settings_rig.h"

/* ------------------------------------------------ the sensor bus's pins */

/* Whether the coprocessor's page, on a bank reserved as the firmware
 * reserves it at boot, takes a part enabled on @p sda and the GPIO after. */
static bool page_takes(unsigned sda)
{
    static outputs_t    o;
    static sense_page_t pg;
    outputs_init(&o, 0u);
    outputs_reserve_pins(&o, outbind_reserved_mask(IOMCU_BOARD_ID)
                                 | IOMCU_RESERVED_PINS);
    sense_page_init(&pg);
    const uint16_t bus[4] = { LINK_SN_EN_I3221, (uint16_t)sda,
                              (uint16_t)(sda + 1u), LINK_SN_KHZ_BUS };
    return sense_page_write(&pg, LINK_SN_ENABLE, 4u, bus, &o, 0u) == 0u;
}

/* The pins Sensor SDA steps through, in order, after -1. */
static const int k_sda[] = { 0, 4, 6, 14, 16, 18, 20, 26 };
#define N_SDA ((int)(sizeof(k_sda) / sizeof(k_sda[0])))

/* Both sides, for every GPIO: the setting takes a pin exactly when the
 * page takes the bus on it. */
TEST_CASE(sensor_sda_takes_the_pins_the_page_takes_and_no_other)
{
    int n = 0;
    for (unsigned gpio = 0u; gpio < 64u; ++gpio) {
        const bool page = page_takes(gpio);
        if (settings_sense_sda_valid((int)gpio) != page) {
            T_FAIL("GP%u: the setting %d, the page %d", gpio,
                   (int)settings_sense_sda_valid((int)gpio), (int)page);
        }
        if (page) {
            CHECK(n < N_SDA);
            CHECK_EQ((int)gpio, k_sda[n < N_SDA ? n : 0]);
            ++n;
        }
    }
    CHECK_EQ(n, N_SDA);
    /* The reserved pins and the ones the module does not bring out. */
    CHECK(!settings_sense_sda_valid(2));     /* GP3, the heartbeat */
    CHECK(!settings_sense_sda_valid(8));     /* GP8 to GP12, the CAN part */
    CHECK(!settings_sense_sda_valid(10));
    CHECK(!settings_sense_sda_valid(12));
    CHECK(!settings_sense_sda_valid(22));    /* GP23 */
    CHECK(!settings_sense_sda_valid(24));
    CHECK(!settings_sense_sda_valid(28));    /* GP29 */
    CHECK(!settings_sense_sda_valid(30));
    CHECK(!settings_sense_sda_valid(15));
    CHECK(!settings_sense_sda_valid(17));
    /* -1 is the pins not set; nothing else below 0 or past the bank. */
    CHECK(settings_sense_sda_valid(-1));
    CHECK(!settings_sense_sda_valid(-2));
    CHECK(!settings_sense_sda_valid(62));
    CHECK(!settings_sense_sda_valid(63));
    CHECK(!settings_sense_sda_valid(64));
    CHECK(!settings_sense_sda_valid(1000));
}

/* From every value, one step either way lands on the next value the bus
 * can have, and stops at -1 and at GP26.  SCL is the GPIO after, or -1. */
TEST_CASE(sensor_sda_steps_from_pin_to_pin_and_scl_follows)
{
    fresh_model();
    int seq[N_SDA + 1];
    seq[0] = -1;
    for (int i = 0; i < N_SDA; ++i) {
        seq[i + 1] = k_sda[i];
    }
    for (int i = 0; i <= N_SDA; ++i) {
        const int up   = seq[(i < N_SDA) ? i + 1 : N_SDA];
        const int down = seq[(i > 0) ? i - 1 : 0];
        settings_set(SET_SENSE_SDA, (float)seq[i]);
        CHECK_EQ(settings_get_int(SET_SENSE_SDA), seq[i]);
        CHECK_EQ(settings_get_int(SET_SENSE_SCL),
                 (seq[i] < 0) ? -1 : seq[i] + 1);
        settings_adjust(SET_SENSE_SDA, 1);
        CHECK_EQ(settings_get_int(SET_SENSE_SDA), up);
        CHECK_EQ(settings_get_int(SET_SENSE_SCL), (up < 0) ? -1 : up + 1);
        CHECK(settings_sense_sda_valid(settings_get_int(SET_SENSE_SDA)));
        settings_set(SET_SENSE_SDA, (float)seq[i]);
        settings_adjust(SET_SENSE_SDA, -1);
        CHECK_EQ(settings_get_int(SET_SENSE_SDA), down);
        CHECK_EQ(settings_get_int(SET_SENSE_SCL),
                 (down < 0) ? -1 : down + 1);
        CHECK(settings_sense_sda_valid(settings_get_int(SET_SENSE_SDA)));
    }
    /* Several steps at once, and more than there are. */
    settings_set(SET_SENSE_SDA, 16.0f);
    settings_adjust(SET_SENSE_SDA, 2);
    CHECK_EQ(settings_get_int(SET_SENSE_SDA), 20);
    settings_adjust(SET_SENSE_SDA, -3);
    CHECK_EQ(settings_get_int(SET_SENSE_SDA), 14);
    settings_adjust(SET_SENSE_SDA, 100);
    CHECK_EQ(settings_get_int(SET_SENSE_SDA), 26);
    CHECK_EQ(settings_get_int(SET_SENSE_SCL), 27);
    settings_adjust(SET_SENSE_SDA, -100);
    CHECK_EQ(settings_get_int(SET_SENSE_SDA), -1);
    CHECK_EQ(settings_get_int(SET_SENSE_SCL), -1);
    /* A value set that the bus cannot have is the default, as a stored
     * one is. */
    settings_set(SET_SENSE_SDA, 15.0f);
    CHECK_EQ(settings_get_int(SET_SENSE_SDA), 16);
    CHECK_EQ(settings_get_int(SET_SENSE_SCL), 17);
    settings_set(SET_SENSE_SDA, 26.0f);
    settings_set(SET_SENSE_SDA, 48.0f);
    CHECK_EQ(settings_get_int(SET_SENSE_SDA), 16);
    settings_set(SET_SENSE_SDA, -7.0f);
    CHECK_EQ(settings_get_int(SET_SENSE_SDA), 16);
}

/* Sensor SCL is not set: neither by a call nor by a step. */
TEST_CASE(sensor_scl_is_derived_and_takes_no_value_of_its_own)
{
    fresh_model();
    CHECK(settings_derived(SET_SENSE_SCL));
    CHECK(!settings_derived(SET_SENSE_SDA));
    CHECK(!settings_derived(SETTING_COUNT));
    int derived = 0;
    for (int i = 0; i < SETTING_COUNT; ++i) {
        derived += settings_derived((setting_id_t)i) ? 1 : 0;
    }
    CHECK_EQ(derived, 1);
    settings_set_observer(observer);
    s_observed = 0;
    settings_set(SET_SENSE_SCL, 5.0f);
    settings_set(SET_SENSE_SCL, -1.0f);
    settings_adjust(SET_SENSE_SCL, 1);
    settings_adjust(SET_SENSE_SCL, -4);
    CHECK_EQ(settings_get_int(SET_SENSE_SCL), 17);
    CHECK_EQ(s_observed, 0);
    CHECK(!settings_dirty());
    /* A step of SDA tells the observer both, SDA first: the panel
     * publishes the pair on either. */
    settings_adjust(SET_SENSE_SDA, 1);
    CHECK_EQ(s_observed, 2);
    CHECK_EQ(s_last_observed, SET_SENSE_SCL);
    CHECK_EQ(settings_get_int(SET_SENSE_SCL), 19);
    CHECK(settings_dirty());
    /* A step that stops at the end tells nobody. */
    settings_set(SET_SENSE_SDA, 26.0f);
    s_observed = 0;
    settings_adjust(SET_SENSE_SDA, 1);
    CHECK_EQ(s_observed, 0);
    /* RESET CATEGORY puts both back. */
    settings_reset(SET_CAT_IFACE);
    CHECK_EQ(settings_get_int(SET_SENSE_SDA), 16);
    CHECK_EQ(settings_get_int(SET_SENSE_SCL), 17);
    settings_set_observer(NULL);
}

/* What settings_nvs.c keeps and gives back: int32 milli-units a key. */
static float nvs_round_trip(float v)
{
    float milli = v * 1000.0f;
    /* A record holds an int32; a value past its range saturates. */
    if (milli > 2147483520.0f) {
        milli = 2147483520.0f;
    } else if (milli < -2147483648.0f) {
        milli = -2147483648.0f;
    }
    return (float)(int32_t)milli / 1000.0f;
}

/* A record as a panel build with two free pin settings saved it. */
static void load_pins(float sda, float scl)
{
    fresh_model();
    for (int i = 0; i < SETTING_COUNT; ++i) {
        s_saved[i] = nvs_round_trip(settings_def((setting_id_t)i)->def);
    }
    s_saved[SET_SENSE_SDA] = nvs_round_trip(sda);
    s_saved[SET_SENSE_SCL] = nvs_round_trip(scl);
    s_saved[SET_INA3221_MOHM] = nvs_round_trip(99.9f);
    s_has_saved = true;
    settings_set_store(&s_mem_store);
    settings_init();
}

/*
 * Records of 0.14.0 and 0.15.0, where SDA and SCL were two settings from
 * -1 to 47: the stored SCL is not read, and a stored SDA the bus cannot
 * have loads as the default.  No load is an edit or an error, and the
 * other keys load as stored.
 */
TEST_CASE(a_stored_pin_pair_of_an_older_build_loads_as_a_pair)
{
    static const struct { int sda, scl, want; } k[] = {
        { 16, 17, 16 },     /* the default pair */
        { 18, 19, 18 },     /* another pair */
        { 26, 27, 26 },     /* the last */
        {  0,  1,  0 },     /* the first */
        { 15, 17, 16 },     /* SDA odd: one stray "-" from the default */
        { 17, 18, 16 },
        { 16, 19, 16 },     /* SCL not the GPIO after: SCL is derived */
        { 18, 17, 18 },
        { 16, -1, 16 },     /* SCL -1: the bus was off for it */
        { -1, 17, -1 },     /* SDA -1 */
        { -1, -1, -1 },
        {  2,  3, 16 },     /* GP3 is reserved */
        { 22, 23, 16 },     /* GP23 is not brought out */
        { 46, 47, 16 },     /* past the module's pins */
        { 47, -1, 16 },
    };
    for (size_t i = 0u; i < sizeof(k) / sizeof(k[0]); ++i) {
        load_pins((float)k[i].sda, (float)k[i].scl);
        const int scl = (k[i].want < 0) ? -1 : k[i].want + 1;
        if (settings_get_int(SET_SENSE_SDA) != k[i].want
            || settings_get_int(SET_SENSE_SCL) != scl) {
            T_FAIL("stored %d/%d loads as %d/%d", k[i].sda, k[i].scl,
                   settings_get_int(SET_SENSE_SDA),
                   settings_get_int(SET_SENSE_SCL));
        }
        CHECK(settings_sense_sda_valid(settings_get_int(SET_SENSE_SDA)));
        CHECK(!settings_dirty());
        CHECK(!settings_save_failed());
        CHECK(!settings_save_asked());
        CHECK_NEAR(settings_get(SET_INA3221_MOHM), 99.9f, 1e-4);
        CHECK_EQ(settings_get_int(SET_INA228_ADDR), 5);
    }
    /* Values no build wrote. */
    load_pins(-5.0f, 300.0f);
    CHECK_EQ(settings_get_int(SET_SENSE_SDA), 16);
    CHECK_EQ(settings_get_int(SET_SENSE_SCL), 17);
    load_pins(1.0e9f, -1.0e9f);
    CHECK_EQ(settings_get_int(SET_SENSE_SDA), 16);
    CHECK_EQ(settings_get_int(SET_SENSE_SCL), 17);
}

/* The SCL key stays and holds the derived pin, so a build that reads both
 * keys reads a pair. */
TEST_CASE(the_scl_key_is_saved_with_the_derived_pin)
{
    CHECK_STR_EQ(settings_def(SET_SENSE_SDA)->key, "sns_sda");
    CHECK_STR_EQ(settings_def(SET_SENSE_SCL)->key, "sns_scl");
    load_pins(15.0f, 17.0f);
    settings_adjust(SET_SENSE_SDA, -1);
    CHECK(settings_save());
    CHECK_NEAR(s_saved[SET_SENSE_SDA], 14.0f, 1e-6);
    CHECK_NEAR(s_saved[SET_SENSE_SCL], 15.0f, 1e-6);
    settings_adjust(SET_SENSE_SDA, -100);
    CHECK(settings_save());
    CHECK_NEAR(s_saved[SET_SENSE_SDA], -1.0f, 1e-6);
    CHECK_NEAR(s_saved[SET_SENSE_SCL], -1.0f, 1e-6);
    settings_init();
    CHECK_EQ(settings_get_int(SET_SENSE_SDA), -1);
    CHECK_EQ(settings_get_int(SET_SENSE_SCL), -1);
}

/* INTERFACES scrolled so Sensor SDA and Sensor SCL are both on the glass. */
static void pins_screen(void)
{
    gesture_screen(SET_CAT_IFACE);
    swipe(XL, LIST_Y + LIST_H - 8, XL, LIST_Y + LIST_H - 132, 4);
    swipe(XL, LIST_Y + LIST_H - 8, XL, LIST_Y + LIST_H - 132, 4);
    CHECK_EQ(settings_screen_scroll(NULL), 232);
    snap();
}

static int scrolled_y(setting_id_t id)
{
    return row_y(row_of(id)) - settings_screen_scroll(NULL);
}

/* The keys of Sensor SDA, by taps as the panel makes them. */
TEST_CASE(a_tap_on_sensor_sda_steps_to_the_next_pin_and_scl_with_it)
{
    pins_screen();
    const int y = scrolled_y(SET_SENSE_SDA);
    feed_tap(FEED_LONE, XM, y);
    CHECK_EQ(settings_get_int(SET_SENSE_SDA), 14);
    CHECK_EQ(settings_get_int(SET_SENSE_SCL), 15);
    CHECK_EQ(changed(), 2);
    feed_tap(FEED_LONE, XP, y);
    feed_tap(FEED_LONE, XP, y);
    CHECK_EQ(settings_get_int(SET_SENSE_SDA), 18);
    CHECK_EQ(settings_get_int(SET_SENSE_SCL), 19);
    /* Held on "-": every step a pin the bus can have, to -1 and no
     * further. */
    finger(FEED_LONE, XM, y);
    for (int i = 0; i < 300; ++i) {
        idle(REPORT_S);
        if (!settings_sense_sda_valid(settings_get_int(SET_SENSE_SDA))) {
            T_FAIL("held at %d", settings_get_int(SET_SENSE_SDA));
        }
        const int sda = settings_get_int(SET_SENSE_SDA);
        CHECK_EQ(settings_get_int(SET_SENSE_SCL), (sda < 0) ? -1 : sda + 1);
    }
    lift(FEED_LONE);
    CHECK_EQ(settings_get_int(SET_SENSE_SDA), -1);
    CHECK_EQ(settings_get_int(SET_SENSE_SCL), -1);
    feed_tap(FEED_LONE, XM, y);
    CHECK_EQ(settings_get_int(SET_SENSE_SDA), -1);
    feed_tap(FEED_LONE, XP, y);
    CHECK_EQ(settings_get_int(SET_SENSE_SDA), 0);
    CHECK_EQ(settings_get_int(SET_SENSE_SCL), 1);
    /* Held on "+": to 26, the last pin, and no further. */
    finger(FEED_LONE, XP, y);
    for (int i = 0; i < 300; ++i) {
        idle(REPORT_S);
        const int sda = settings_get_int(SET_SENSE_SDA);
        if (!settings_sense_sda_valid(sda) || sda < 0) {
            T_FAIL("held at %d", sda);
        }
        CHECK_EQ(settings_get_int(SET_SENSE_SCL), sda + 1);
    }
    lift(FEED_LONE);
    CHECK_EQ(settings_get_int(SET_SENSE_SDA), 26);
    CHECK_EQ(settings_get_int(SET_SENSE_SCL), 27);
    feed_tap(FEED_LONE, XP, y);
    CHECK_EQ(settings_get_int(SET_SENSE_SDA), 26);
    feed_tap(FEED_LONE, XM, y);
    CHECK_EQ(settings_get_int(SET_SENSE_SDA), 20);
    CHECK_EQ(settings_get_int(SET_SENSE_SCL), 21);
}

/* Sensor SCL's row has no keys: a tap or a hold where the other rows have
 * theirs changes nothing, and a drag from there scrolls the list. */
TEST_CASE(a_tap_on_sensor_scls_row_changes_nothing)
{
    pins_screen();
    const int y = scrolled_y(SET_SENSE_SCL);
    static const int xs[] = { MINUS_X, XM, MINUS_X + BTN_W - 1,
                              PLUS_X, XP, PLUS_X + BTN_W - 1, XL, 660 };
    for (size_t i = 0u; i < sizeof(xs) / sizeof(xs[0]); ++i) {
        feed_tap(FEED_LONE, xs[i], y);
        feed_tap(FEED_LONE, xs[i], y - ROW_H / 2 + 1);
        feed_tap(FEED_LONE, xs[i], y + ROW_H / 2 - 1);
        finger(FEED_LONE, xs[i], y);
        idle(3.0f);
        lift(FEED_LONE);
        idle(0.5f);
    }
    CHECK_EQ(changed(), 0);
    CHECK_EQ(settings_get_int(SET_SENSE_SDA), 16);
    CHECK_EQ(settings_get_int(SET_SENSE_SCL), 17);
    CHECK(!settings_dirty());
    /* The row above still steps, one px inside its lower edge. */
    feed_tap(FEED_LONE, XP, scrolled_y(SET_SENSE_SDA) + ROW_H / 2 - 1);
    CHECK_EQ(settings_get_int(SET_SENSE_SDA), 18);
    snap();
    /* And a drag from where a key would be scrolls. */
    const int from = settings_screen_scroll(NULL);
    swipe(XM, scrolled_y(SET_SENSE_SCL), XM,
          scrolled_y(SET_SENSE_SCL) - 60, 4);
    CHECK_EQ(settings_screen_scroll(NULL), from + 52);
    CHECK_EQ(changed(), 0);
}

/* The mark: each row of the sensor bus and the phase tap follows its own
 * bit, and no other row follows any. */
TEST_CASE(a_row_the_coprocessor_does_not_hold_is_marked)
{
    static const struct { setting_id_t id; uint16_t sense; uint8_t tone; }
        k[] = {
        { SET_INA228_EN,    SENSE_LINK_ROW_I228, 0u },
        { SET_INA228_ADDR,  SENSE_LINK_ROW_I228_ADDR, 0u },
        { SET_INA228_UOHM,  SENSE_LINK_ROW_I228_SHUNT, 0u },
        { SET_INA228_MAX_A, SENSE_LINK_ROW_I228_MAX, 0u },
        { SET_INA3221_EN,   SENSE_LINK_ROW_I3221, 0u },
        { SET_INA3221_ADDR, SENSE_LINK_ROW_I3221_ADDR, 0u },
        { SET_INA3221_MOHM, SENSE_LINK_ROW_I3221_SHUNT, 0u },
        { SET_INA3221_CH,   SENSE_LINK_ROW_I3221_CH, 0u },
        { SET_SENSE_SDA,    SENSE_LINK_ROW_PINS, 0u },
        { SET_SENSE_SCL,    SENSE_LINK_ROW_PINS, 0u },
        { SET_ENC_EN,       SENSE_LINK_ROW_ENC, 0u },
        { SET_TONE_EN,      0u, TONE_LINK_ROWS_TAP },
        { SET_TONE_PIN,     0u, TONE_LINK_ROWS_TAP },
        { SET_TONE_F_MIN,   0u, TONE_LINK_ROWS_TAP },
        { SET_TONE_F_MAX,   0u, TONE_LINK_ROWS_TAP },
        { SET_TONE_SPLIT,   0u, TONE_LINK_ROWS_BEEP },
        { SET_TONE_GAP,     0u, TONE_LINK_ROWS_BEEP },
        { SET_TONE_PERIODS, 0u, TONE_LINK_ROWS_BEEP },
    };
    const size_t n = sizeof(k) / sizeof(k[0]);
    fresh_screen();
    for (int id = 0; id < SETTING_COUNT; ++id) {
        CHECK(!settings_screen_unheld((setting_id_t)id));
    }
    /* Every bit there is, one at a time. */
    for (unsigned bit = 0u; bit < 24u; ++bit) {
        const uint16_t sense = (bit < 16u) ? (uint16_t)(1u << bit) : 0u;
        const uint8_t  tone  = (bit < 16u) ? 0u : (uint8_t)(1u << (bit - 16u));
        settings_screen_set_unheld(sense, tone);
        for (int id = 0; id < SETTING_COUNT; ++id) {
            bool want = false;
            for (size_t i = 0u; i < n; ++i) {
                if (k[i].id == (setting_id_t)id
                    && ((k[i].sense & sense) != 0u
                        || (k[i].tone & tone) != 0u)) {
                    want = true;
                }
            }
            if (settings_screen_unheld((setting_id_t)id) != want) {
                T_FAIL("bit %u, %s", bit,
                       settings_def((setting_id_t)id)->key);
            }
        }
    }
    settings_screen_set_unheld(0u, 0u);
    /* Every bit of both links names a row. */
    uint16_t sense_all = 0u;
    uint8_t  tone_all = 0u;
    for (size_t i = 0u; i < n; ++i) {
        sense_all |= k[i].sense;
        tone_all  |= k[i].tone;
    }
    CHECK_EQ(sense_all, 0x03FFu);
    CHECK_EQ(tone_all, TONE_LINK_ROWS_TAP | TONE_LINK_ROWS_BEEP);
}

/* The mark is drawn when it is set and gone when it is cleared, with no
 * touch in between: the control task sets it. */
TEST_CASE(the_mark_repaints_the_row_when_it_comes_and_when_it_goes)
{
    pins_screen();
    static gfx_color_t plain[W * H];
    static gfx_color_t marked[W * H];
    ui_router_tick(0.05f);
    ui_router_render(&s_c, 0);
    memcpy(plain, s_fb, sizeof(plain));

    settings_screen_set_unheld(SENSE_LINK_ROW_PINS, 0u);
    ui_router_tick(0.05f);
    ui_router_render(&s_c, 0);
    memcpy(marked, s_fb, sizeof(marked));
    /* The two pin rows differ, and no pixel outside them. */
    const int top = scrolled_y(SET_SENSE_SDA) - ROW_H / 2;
    const int bottom = scrolled_y(SET_SENSE_SCL) + ROW_H / 2;
    int inside = 0, outside = 0;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            if (plain[y * W + x] != marked[y * W + x]) {
                if (y >= top && y < bottom && x >= LIST_X) {
                    ++inside;
                } else {
                    ++outside;
                }
            }
        }
    }
    CHECK(inside > 200);
    CHECK_EQ(outside, 0);

    /* Set again the same: nothing to repaint.  Cleared: as before. */
    settings_screen_set_unheld(SENSE_LINK_ROW_PINS, 0u);
    ui_router_render(&s_c, 0);
    CHECK(memcmp(marked, s_fb, sizeof(marked)) == 0);
    settings_screen_set_unheld(0u, 0u);
    ui_router_tick(0.05f);
    ui_router_render(&s_c, 0);
    CHECK(memcmp(plain, s_fb, sizeof(plain)) == 0);
    /* A mark survives the screen being left and entered. */
    settings_screen_set_unheld(SENSE_LINK_ROW_PINS, 0u);
    ui_router_goto(SCREEN_OVERVIEW);
    ui_router_goto(SCREEN_SETUP);
    CHECK(settings_screen_unheld(SET_SENSE_SDA));
    settings_screen_set_unheld(0u, 0u);
}


/* ------------------------------------------------------------------- hub */

/*
 * The hub is one page of three by three tiles.  The tile table fills them
 * in order; a tenth entry has no slot, and the build stops at it
 * (_Static_assert in settings_screen.c) as this test does.
 */
TEST_CASE(the_hub_has_nine_slots_and_no_tenth)
{
    CHECK_EQ(SETTINGS_HUB_SLOTS, 9);
    CHECK(settings_hub_tiles() >= SET_CAT_SETUP_COUNT + 3);
    CHECK(settings_hub_tiles() <= SETTINGS_HUB_SLOTS);
    gfx_rect_t r[SETTINGS_HUB_SLOTS];
    for (int i = 0; i < SETTINGS_HUB_SLOTS; ++i) {
        CHECK(settings_hub_slot(i, &r[i]));
        CHECK(settings_hub_slot(i, NULL));
        /* Inside the body, above the SAVE strip. */
        CHECK(r[i].x >= 0 && r[i].x + r[i].w <= W);
        CHECK(r[i].y >= 0 && UI_BAND_H + r[i].y + r[i].h <= STRIP_Y);
        CHECK(r[i].w >= 200 && r[i].h >= 100);
        for (int j = 0; j < i; ++j) {
            const bool apart = r[i].x >= r[j].x + r[j].w ||
                               r[j].x >= r[i].x + r[i].w ||
                               r[i].y >= r[j].y + r[j].h ||
                               r[j].y >= r[i].y + r[i].h;
            if (!apart) {
                T_FAIL("slots %d and %d overlap", j, i);
            }
        }
    }
    gfx_rect_t untouched = gfx_rect_make(1, 2, 3, 4);
    CHECK(!settings_hub_slot(SETTINGS_HUB_SLOTS, &untouched));
    CHECK(!settings_hub_slot(-1, &untouched));
    CHECK_EQ(untouched.x, 1);
    CHECK_EQ(untouched.h, 4);
}

TEST_CASE(each_category_tile_opens_its_list_and_the_home_tag_returns)
{
    for (int c = 0; c < SET_CAT_SETUP_COUNT; ++c) {
        fresh_hub();
        CHECK_EQ(settings_screen_category(), -1);
        tap(tile_x(c), tile_y(c));
        CHECK_EQ(ui_router_current(), SCREEN_SETUP);
        CHECK_EQ(settings_screen_category(), c);
        CHECK_EQ(settings_screen_scroll(NULL), 0);
        tap(home_x(), home_y());
        CHECK_EQ(ui_router_current(), SCREEN_SETUP);
        CHECK_EQ(settings_screen_category(), -1);
    }
    /* From the tiles, the home tag leaves SETUP. */
    tap(home_x(), home_y());
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
}

TEST_CASE(a_free_slot_opens_nothing)
{
    fresh_hub();
    for (int i = settings_hub_tiles(); i < SETTINGS_HUB_SLOTS; ++i) {
        tap(tile_x(i), tile_y(i));
        CHECK_EQ(ui_router_current(), SCREEN_SETUP);
        CHECK_EQ(settings_screen_category(), -1);
    }
    /* Nor does the place of RESET CATEGORY, which the tiles do not show. */
    settings_set(SET_PACK_CELLS, 4.0f);
    tap(RESET_X + 100, RESET_Y + RESET_H / 2);
    CHECK_EQ(settings_get_int(SET_PACK_CELLS), 4);
    CHECK_EQ(settings_screen_category(), -1);
}

TEST_CASE(entering_setup_shows_the_tiles_and_the_list_keeps_its_place)
{
    fresh_hub();
    open_category(SET_CAT_IFACE);
    finger(FEED_LONE, XL, LIST_Y + 300);
    glide(FEED_LONE, XL, LIST_Y + 200, 10);
    lift(FEED_LONE);
    const int scrolled = settings_screen_scroll(NULL);
    CHECK(scrolled > 0);
    ui_router_goto(SCREEN_OVERVIEW);
    ui_router_goto(SCREEN_SETUP);
    CHECK_EQ(settings_screen_category(), -1);
    open_category(SET_CAT_IFACE);
    CHECK_EQ(settings_screen_scroll(NULL), scrolled);
}

/*
 * Unsaved values are one state for the whole screen: the tiles show the
 * same SAVE button the lists do, in the same colours, and it asks for the
 * same save.
 */
TEST_CASE(the_tiles_show_and_save_what_a_list_left_unsaved)
{
    fresh_hub();
    settings_set_store(&s_mem_store);
    s_save_calls = 0;
    const unsigned clean = save_face();

    open_category(SET_CAT_ESC);
    const unsigned list_clean = save_face();
    CHECK_EQ(list_clean, clean);
    tap(XP, row_y(0));
    CHECK(settings_dirty());
    const unsigned list_dirty = save_face();
    CHECK(list_dirty != clean);

    to_hub();
    CHECK_EQ(settings_screen_category(), -1);
    CHECK(settings_dirty());
    CHECK_EQ(save_face(), list_dirty);

    tap(SAVE_X + 100, SAVE_Y + SAVE_H / 2);
    CHECK(settings_save_asked());
    CHECK_EQ(s_save_calls, 0);
    CHECK(settings_save_tick(true));
    CHECK_EQ(s_save_calls, 1);
    CHECK(!settings_dirty());
    CHECK_EQ(save_face(), clean);

    /* A save asked for on the tiles outlives the screen as well. */
    open_category(SET_CAT_APP);
    tap(XP, row_y(0));
    to_hub();
    tap(SAVE_X + 100, SAVE_Y + SAVE_H / 2);
    tap(home_x(), home_y());
    CHECK_EQ(ui_router_current(), SCREEN_OVERVIEW);
    CHECK(settings_save_asked());
    CHECK(settings_save_tick(true));
    CHECK_EQ(s_save_calls, 2);
    settings_set_store(NULL);
}

/*
 * Where ink is in rows y0 to y1 - 1 of @p r, inside its edge: the leftmost
 * and the rightmost column, and the first and the last row.  False when
 * nothing is drawn there.
 */
typedef struct { int left, right, top, bottom; } ink_t;

static bool ink(gfx_rect_t r, int y0, int y1, gfx_color_t bg, ink_t *out)
{
    *out = (ink_t){ W, -1, -1, -1 };
    for (int y = y0; y < y1; ++y) {
        for (int x = r.x + 3; x < r.x + r.w - 3; ++x) {
            if (gfx_pixel_get(&s_c, x, y) == bg) {
                continue;
            }
            out->left = (x < out->left) ? x : out->left;
            out->right = (x > out->right) ? x : out->right;
            out->top = (out->top < 0) ? y : out->top;
            out->bottom = y;
        }
    }
    return out->right >= 0;
}

/*
 * A tile's icon sits over its name.  The icon's drawn ink, from its
 * leftmost to its rightmost pixel, is centred on the name's line of text,
 * which ui_tile() centres in the tile, to 1 px; the name's own ink lies in
 * that line (its glyphs leave up to 2 px of their 8 px cells blank, so the
 * ink of "MOTOR & ESC" is centred 1.5 px left of its line).  The icon's ink
 * stays in its 30 px box, clear of the name under it.  @p r and @p icon_dy
 * are in canvas coordinates, on a tile already drawn.
 */
static void icon_over_name(gfx_rect_t r, int icon_dy, const char *name,
                           const char *what)
{
    const gfx_color_t bg = gfx_pixel_get(&s_c, r.x + r.w / 2, r.y + 2);
    ink_t icon, label;
    /* Every row from under the edge to the name's top. */
    if (!ink(r, r.y + 3, r.y + icon_dy + 40, bg, &icon)
        || !ink(r, r.y + icon_dy + 40, r.y + icon_dy + 62, bg, &label)) {
        T_FAIL("%s: no icon or no name drawn", what);
        return;
    }
    const int tw = gfx_text_width(&gfx_font_8x16, name, 1);
    const int line_x = r.x + (r.w - tw) / 2;
    if (abs((icon.left + icon.right + 1) - (2 * line_x + tw)) > 2) {
        T_FAIL("%s: icon centred at %.1f, its name's line at %.1f", what,
               (icon.left + icon.right + 1) / 2.0, line_x + tw / 2.0);
    }
    if (label.left < line_x || label.right >= line_x + tw) {
        T_FAIL("%s: name inked at %d to %d, its line %d to %d", what,
               label.left, label.right, line_x, line_x + tw - 1);
    }
    if (icon.top < r.y + icon_dy
        || icon.bottom >= r.y + icon_dy + UI_TILE_ICON) {
        T_FAIL("%s: icon rows %d to %d, box %d to %d", what, icon.top,
               icon.bottom, r.y + icon_dy, r.y + icon_dy + UI_TILE_ICON - 1);
    }
}

/* Every icon a tile draws, the menu's and SETUP's, in the tiles' sizes. */
TEST_CASE(every_tile_icon_is_centred_over_its_name)
{
    fresh_hub();
    static const struct { ui_icon_fn icon; const char *name; } menu[] = {
        { ui_icon_motor, "MOTOR & ESC" }, { ui_icon_servo, "SERVO" },
        { ui_icon_supply, "SUPPLY" },     { ui_icon_chart, "ANALYSER" },
        { ui_icon_record, "LOGS" },       { ui_icon_sliders, "SETUP" },
        { ui_icon_battery, "BATTERY" },   { ui_icon_balance, "BALANCE" },
        { ui_icon_chip, "PROGRAMMER" },   { ui_icon_list, "OUTPUTS" },
        { ui_icon_board, "PICK A PIN" },  { ui_icon_wifi, "WI-FI" },
    };
    /* The menu's tile: 150 x 202 px, the icon 64 px down. */
    for (size_t i = 0; i < sizeof(menu) / sizeof(menu[0]); ++i) {
        const gfx_rect_t r = gfx_rect_make(8, 8, 150, 202);
        memset(s_fb, 0, (size_t)W * H * sizeof(gfx_color_t));
        ui_tile(&s_c, r, 64, menu[i].icon, menu[i].name, "", false);
        icon_over_name(r, 64, menu[i].name, menu[i].name);
    }
    /* SETUP's, as the screen draws them: the categories, then OUTPUTS,
     * PICK A PIN, WI-FI and UPDATES.  The icon's box starts 16 px down. */
    static const ui_screen_id_t screens[] = { SCREEN_OUTPUTS, SCREEN_PICKER,
                                              SCREEN_WIFI, SCREEN_UPDATES };
    CHECK_EQ(settings_hub_tiles(), SET_CAT_SETUP_COUNT + 4);
    ui_router_render(&s_c, 0);
    for (int i = 0; i < settings_hub_tiles(); ++i) {
        gfx_rect_t r = gfx_rect_make(0, 0, 0, 0);
        CHECK(settings_hub_slot(i, &r));
        r.y = (int16_t)(r.y + UI_BAND_H);
        const char *name = (i < SET_CAT_SETUP_COUNT)
            ? ui_setting_category((setting_cat_t)i)
            : ui_router_title(screens[i - SET_CAT_SETUP_COUNT]);
        icon_over_name(r, 16, name, name);
    }
}

/*
 * The router's alert band covers the bottom UI_ALERT_H px of the panel
 * while an alert shows, and takes the touches there.  Nothing of SETUP
 * that acts is under it: no tile, no row, no RESET CATEGORY, no SAVE.  A
 * tap on every 4th px of the band's rectangle, with no alert showing, on
 * the tiles and on each list, with a value unsaved, does nothing.
 */
TEST_CASE(nothing_of_setup_that_acts_is_under_the_alert_band)
{
    for (int i = 0; i < SETTINGS_HUB_SLOTS; ++i) {
        gfx_rect_t r = gfx_rect_make(0, 0, 0, 0);
        CHECK(settings_hub_slot(i, &r));
        CHECK(UI_BAND_H + r.y + r.h <= UI_ALERT_Y);
    }
    for (int view = -1; view < SET_CAT_SETUP_COUNT; ++view) {
        fresh_hub();
        settings_set_store(&s_mem_store);
        if (view >= 0) {
            open_category((setting_cat_t)view);
            /* A value unsaved in this category, for RESET to take back. */
            tap(XP, row_y(0));
        } else {
            settings_set(SET_PACK_CELLS, 4.0f);
        }
        CHECK(settings_dirty());
        snap();
        const int scroll = settings_screen_scroll(NULL);
        for (int y = UI_ALERT_Y; y < H; y += 4) {
            for (int x = 0; x < W; x += 4) {
                tap(x, y);
            }
        }
        CHECK_EQ(ui_router_current(), SCREEN_SETUP);
        CHECK_EQ(settings_screen_category(), view);
        CHECK_EQ(settings_screen_scroll(NULL), scroll);
        CHECK_EQ(changed(), 0);
        CHECK(!settings_save_asked());
        settings_cancel_save();
        settings_set_store(NULL);
    }
}

int main(void)
{
    RUN(sensor_sda_takes_the_pins_the_page_takes_and_no_other);
    RUN(sensor_sda_steps_from_pin_to_pin_and_scl_follows);
    RUN(sensor_scl_is_derived_and_takes_no_value_of_its_own);
    RUN(a_stored_pin_pair_of_an_older_build_loads_as_a_pair);
    RUN(the_scl_key_is_saved_with_the_derived_pin);
    RUN(a_tap_on_sensor_sda_steps_to_the_next_pin_and_scl_with_it);
    RUN(a_tap_on_sensor_scls_row_changes_nothing);
    RUN(a_row_the_coprocessor_does_not_hold_is_marked);
    RUN(the_mark_repaints_the_row_when_it_comes_and_when_it_goes);
    RUN(the_hub_has_nine_slots_and_no_tenth);
    RUN(each_category_tile_opens_its_list_and_the_home_tag_returns);
    RUN(a_free_slot_opens_nothing);
    RUN(entering_setup_shows_the_tiles_and_the_list_keeps_its_place);
    RUN(the_tiles_show_and_save_what_a_list_left_unsaved);
    RUN(every_tile_icon_is_centred_over_its_name);
    RUN(nothing_of_setup_that_acts_is_under_the_alert_band);
    return test_summary("settings_pins_hub");
}
