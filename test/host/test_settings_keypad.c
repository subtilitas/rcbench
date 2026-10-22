/**
 * @file test_settings_keypad.c
 * @brief SETUP's keypad: a tap on a number's value opens it; OK sets the
 *        value through settings_set(), the call a "-" or "+" step ends in,
 *        so SAVE keeps what a step would have kept; a value outside the
 *        range, or one the steps never reach, is refused with the keypad
 *        open, the second with its reason in place of the range until the
 *        next key; CANCEL, the home tag and STOP change nothing.  The helper
 *        every setting's keypad uses (ui_setting_keypad.h) is held here too.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "settings_rig.h"

#include "sensors_rows.h"
#include "settings_sensors.h"
#include "ui_keypad.h"
#include "ui_setting_keypad.h"

/* The value between a row's "-" and "+" keys, in panel coordinates. */
#define XV ((MINUS_X + BTN_W + PLUS_X) / 2)

/**
 * @brief The keypad's key @p key tapped, in panel coordinates.
 * @param key The key.
 */
static void key(ui_key_t key)
{
    const gfx_rect_t r = ui_keypad_key_rect(settings_screen_keypad(), key);
    tap(r.x + r.w / 2, UI_BAND_H + r.y + r.h / 2);
}

/**
 * @brief @p text typed on the keypad, digit by digit.
 * @param text Digits; '.' is the point.
 */
static void type(const char *text)
{
    static const ui_key_t k_digit[10] = {
        UI_KEY_0, UI_KEY_1, UI_KEY_2, UI_KEY_3, UI_KEY_4,
        UI_KEY_5, UI_KEY_6, UI_KEY_7, UI_KEY_8, UI_KEY_9,
    };
    for (const char *p = text; *p != '\0'; ++p) {
        key((*p == '.') ? UI_KEY_DOT : k_digit[*p - '0']);
    }
}

/**
 * @brief A drag up the list by about @p dy px, from the label column.
 * @param dy The drag, px; the first 8 px are the slop and scroll nothing.
 */
static void scroll_by(int dy)
{
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = XL,
                                   .y = (int16_t)(LIST_Y + LIST_H - 20) } };
    ui_router_event(&e);
    e.type = TOUCH_EVENT_MOVE;
    for (int moved = 6; moved <= dy; moved += 6) {
        e.point.y = (int16_t)(LIST_Y + LIST_H - 20 - moved);
        ui_router_event(&e);
    }
    e.type = TOUCH_EVENT_UP;
    ui_router_event(&e);
}

/**
 * @brief The row of @p id in the open list, scrolled into view.
 * @param id The setting.
 * @return The row's middle y in panel coordinates; -1 for a row the list
 *         does not show.
 */
static int show(setting_id_t id)
{
    const setting_cat_t cat = settings_def(id)->cat;
    setting_id_t ids[64];
    const int n = settings_in_category(cat, ids, 64);
    int row = -1;
    for (int i = 0; i < n; ++i) {
        if (ids[i] == id) {
            row = settings_screen_row_of(cat, i);
        }
    }
    if (row < 0) {
        return -1;
    }
    for (int k = 0; k < 40; ++k) {
        const int top = LIST_Y + row * ROW_PITCH - settings_screen_scroll(NULL);
        if (top >= LIST_Y && top + ROW_H <= LIST_Y + LIST_H) {
            return top + ROW_H / 2;
        }
        scroll_by(96);
    }
    return -1;
}

/**
 * @brief SETUP on @p cat's list, at the defaults, with the memory store.
 * @param cat The category, opened from its tile.
 */
static void list_of(setting_cat_t cat)
{
    fresh_hub();
    settings_set_store(&s_mem_store);
    s_save_calls = 0;
    s_has_saved = false;
    open_category(cat);
}

/**
 * @brief The keypad of @p id, opened by a tap on its value.
 * @param id The setting.
 */
static void open_on(setting_id_t id)
{
    const int y = show(id);
    CHECK(y > 0);
    tap(XV, y);
}

/**
 * @brief The bench ARMED or SAFE, as the control task reports it.
 * @param armed Whether it is ARMED.
 */
static void set_armed(bool armed)
{
    ui_bench_status_t st = *ui_router_status();
    st.armed = armed;
    ui_router_set_status(&st);
}

/* ---------------------------------------------------------------- opening */

TEST_CASE(a_tap_on_a_value_opens_its_keypad)
{
    list_of(SET_CAT_IFACE);
    const setting_id_t id = SET_INA228_MAX_A;
    open_on(id);
    const ui_keypad_t *k = settings_screen_keypad();
    CHECK(k->open);
    CHECK_EQ(settings_screen_keypad_setting(), (int)id);
    CHECK_STR_EQ(k->title, ui_setting_label(id));
    CHECK_STR_EQ(k->unit, "A");
    CHECK_NEAR(k->min, 1.0f, 1e-6f);
    CHECK_NEAR(k->max, 300.0f, 1e-6f);
    CHECK_EQ(k->decimals, 1);
    CHECK_NEAR(k->value, settings_get(id), 1e-6f);
    /* Over the list and the strip, its foot 4 px above the alert band. */
    CHECK_EQ(k->area.x, 10);
    CHECK_EQ(k->area.y, 10);
    CHECK_EQ(k->area.w, UI_PANEL_W - 20);
    CHECK_EQ(k->area.y + k->area.h, UI_BODY_H - UI_ALERT_H - 4);
    /* Drawn: the frame carries its OK key's accent. */
    ui_router_render(&s_c, 0);
    const gfx_rect_t ok = ui_keypad_key_rect(k, UI_KEY_OK);
    CHECK_EQ(s_fb[(UI_BAND_H + ok.y + ok.h / 2) * W + ok.x + 4],
             ui_theme_color(UI_C_ACCENT));
}

TEST_CASE(a_tap_beside_the_value_opens_nothing)
{
    list_of(SET_CAT_IFACE);
    snap();
    /* The label, a switch's value, and a press that turns into a scroll. */
    tap(XL, show(SET_INA228_MAX_A));
    CHECK(!settings_screen_keypad()->open);
    tap(XV, show(SET_INA228_EN));
    CHECK(!settings_screen_keypad()->open);
    /* A choice: the INA228 address is a list, stepped. */
    tap(XV, show(SET_INA228_ADDR));
    CHECK(!settings_screen_keypad()->open);
    const int y = show(SET_INA228_MAX_A);
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = XV, .y = (int16_t)y } };
    ui_router_event(&e);
    e.type = TOUCH_EVENT_MOVE;
    for (int i = 1; i <= 6; ++i) {
        e.point.y = (int16_t)(y - 6 * i);
        ui_router_event(&e);
    }
    e.type = TOUCH_EVENT_UP;
    ui_router_event(&e);
    CHECK(!settings_screen_keypad()->open);
    /* Sensor SCL follows SDA: no keys, no keypad. */
    tap(XV, show(SET_SENSE_SCL));
    CHECK(!settings_screen_keypad()->open);
    CHECK_EQ(changed(), 0);
}

/* ----------------------------------------------------------- the entries */

TEST_CASE(the_range_ends_are_taken_and_a_value_past_them_refused)
{
    list_of(SET_CAT_IFACE);
    const setting_id_t id = SET_INA228_MAX_A;
    open_on(id);
    type("1");
    key(UI_KEY_OK);
    CHECK(!settings_screen_keypad()->open);
    CHECK_NEAR(settings_get(id), 1.0f, 1e-6f);

    open_on(id);
    type("300");
    key(UI_KEY_OK);
    CHECK_NEAR(settings_get(id), 300.0f, 1e-4f);

    /* Above the maximum: refused, the keypad open with the entry kept,
     * the value as it was. */
    open_on(id);
    type("300.1");
    key(UI_KEY_OK);
    CHECK(settings_screen_keypad()->open);
    CHECK(settings_screen_keypad()->refused);
    CHECK_STR_EQ(settings_screen_keypad()->entry, "300.1");
    CHECK_NEAR(settings_get(id), 300.0f, 1e-4f);
    key(UI_KEY_CLR);
    type("0.9");                         /* under the minimum */
    key(UI_KEY_OK);
    CHECK(settings_screen_keypad()->refused);
    CHECK_NEAR(settings_get(id), 300.0f, 1e-4f);
    key(UI_KEY_CANCEL);
    CHECK(!settings_screen_keypad()->open);
}

TEST_CASE(a_digit_past_the_steps_decimals_is_not_typed)
{
    list_of(SET_CAT_IFACE);
    const setting_id_t id = SET_INA228_MAX_A;     /* steps of 0.1 A */
    open_on(id);
    type("12.345");
    CHECK_STR_EQ(settings_screen_keypad()->entry, "12.3");
    key(UI_KEY_OK);
    CHECK_NEAR(settings_get(id), 12.3f, 1e-4f);
    /* A whole number takes no point at all. */
    open_on(SET_INA228_UOHM);
    type("4.5");
    CHECK_STR_EQ(settings_screen_keypad()->entry, "45");
    key(UI_KEY_OK);                                 /* under its 50 */
    CHECK(settings_screen_keypad()->refused);
    CHECK_EQ(settings_get_int(SET_INA228_UOHM), 200);
    type("5");
    key(UI_KEY_OK);
    CHECK_EQ(settings_get_int(SET_INA228_UOHM), 455);
}

TEST_CASE(cancel_and_the_home_tag_change_nothing)
{
    list_of(SET_CAT_IFACE);
    snap();
    open_on(SET_INA228_UOHM);
    type("1234");
    key(UI_KEY_CANCEL);
    CHECK(!settings_screen_keypad()->open);
    CHECK_EQ(changed(), 0);
    CHECK(!settings_dirty());
    /* OK on nothing typed is CANCEL. */
    open_on(SET_INA228_UOHM);
    key(UI_KEY_OK);
    CHECK(!settings_screen_keypad()->open);
    CHECK_EQ(changed(), 0);
    /* The home tag over the keypad closes it, and the list stays. */
    open_on(SET_INA228_UOHM);
    type("999");
    tap(home_x(), home_y());
    CHECK(!settings_screen_keypad()->open);
    CHECK_EQ(settings_screen_category(), (int)SET_CAT_IFACE);
    CHECK_EQ(changed(), 0);
    CHECK(!settings_dirty());
    /* Leaving SETUP takes the keypad with it. */
    open_on(SET_INA228_UOHM);
    type("999");
    ui_router_goto(SCREEN_OVERVIEW);
    CHECK(!settings_screen_keypad()->open);
    CHECK_EQ(changed(), 0);
}

TEST_CASE(nothing_under_the_keypad_presses)
{
    list_of(SET_CAT_IFACE);
    snap();
    open_on(SET_INA228_UOHM);
    /* The first row's "+" key lies under the keypad's title, and a row's
     * value under its entry: neither steps nor opens anything. */
    tap(PLUS_X + BTN_W / 2, row_y(0));
    const gfx_rect_t a = settings_screen_keypad()->area;
    tap(XV, UI_BAND_H + a.y + 30 + 6 + 26);
    CHECK(settings_screen_keypad()->open);
    CHECK_EQ(settings_screen_keypad_setting(), (int)SET_INA228_UOHM);
    CHECK_EQ(changed(), 0);
    CHECK(!settings_dirty());
}

/* ------------------------------------------------- the same as a step */

/* The station's walk on 0.22.0: 0x40 CH1's shunt from 100.0 to 44.4 mOhm
 * took 556 steps of 0.1 mOhm.  The keypad's 44.4 is the value those steps
 * reach, set through the same call, and SAVE writes the same word. */
TEST_CASE(a_typed_shunt_saves_what_556_steps_save)
{
    const setting_id_t id = SET_INA_SH(0);
    list_of(SET_CAT_SENSORS);
    settings_set(SET_INA_FIT(0), 1.0f);
    ui_router_render(&s_c, 0);
    open_on(id);
    CHECK_STR_EQ(settings_screen_keypad()->title, "0x40 CH1 shunt");
    CHECK_STR_EQ(settings_screen_keypad()->unit, "mOhm");
    CHECK_NEAR(settings_screen_keypad()->min, 0.0f, 1e-6f);
    CHECK_NEAR(settings_screen_keypad()->max, 1000.0f, 1e-6f);
    type("44.4");
    key(UI_KEY_OK);
    const float typed = settings_get(id);
    CHECK(settings_dirty());
    tap(SAVE_X + 100, SAVE_Y + SAVE_H / 2);
    CHECK(settings_save_tick(true));
    const float saved_typed = s_saved[id];

    list_of(SET_CAT_SENSORS);
    settings_set(SET_INA_FIT(0), 1.0f);
    ui_router_render(&s_c, 0);
    const int y = show(id);
    CHECK(y > 0);
    for (int i = 0; i < 556; ++i) {
        tap(MINUS_X + BTN_W / 2, y);
    }
    const float stepped = settings_get(id);
    tap(SAVE_X + 100, SAVE_Y + SAVE_H / 2);
    CHECK(settings_save_tick(true));

    CHECK_NEAR(typed, 44.4f, 1e-4f);
    CHECK(typed == stepped);
    CHECK(saved_typed == s_saved[id]);
}

/* A value between two steps is set onto the nearer, as settings_set()
 * puts any value; a PANEL row reaches the interface as a step does. */
TEST_CASE(a_value_between_steps_takes_the_nearer_step)
{
    list_of(SET_CAT_APP);
    settings_set(SET_BRIGHTNESS, 70.0f);
    open_on(SET_BRIGHTNESS);                       /* steps of 5 % */
    type("73");
    key(UI_KEY_OK);
    CHECK_EQ(settings_get_int(SET_BRIGHTNESS), 75);
    CHECK_EQ(ui_theme_brightness(), 75);
    settings_set(SET_BRIGHTNESS, 70.0f);
    tap(PLUS_X + BTN_W / 2, show(SET_BRIGHTNESS));
    CHECK_EQ(settings_get_int(SET_BRIGHTNESS), 75);
}

/* ------------------------------------------- what the steps never reach */

TEST_CASE(a_shunt_between_off_and_five_milliohm_is_refused)
{
    const setting_id_t id = SET_INA_SH(0);
    list_of(SET_CAT_SENSORS);
    settings_set(SET_INA_FIT(0), 1.0f);
    ui_router_render(&s_c, 0);
    open_on(id);
    type("4.9");
    key(UI_KEY_OK);
    CHECK(settings_screen_keypad()->refused);
    CHECK_NEAR(settings_get(id), 100.0f, 1e-4f);
    key(UI_KEY_CLR);
    type("0");                                     /* off */
    key(UI_KEY_OK);
    CHECK(!settings_screen_keypad()->open);
    CHECK_EQ(settings_get(id), 0.0f);
    open_on(id);
    type("5");
    key(UI_KEY_OK);
    CHECK_NEAR(settings_get(id), 5.0f, 1e-4f);
}

TEST_CASE(a_pin_the_row_does_not_take_is_refused)
{
    list_of(SET_CAT_IFACE);
    /* Sensor SDA takes 0, 4, 6, 14, 16, 18, 20 and 26. */
    open_on(SET_SENSE_SDA);
    type("5");
    key(UI_KEY_OK);
    CHECK(settings_screen_keypad()->refused);
    CHECK_EQ(settings_get_int(SET_SENSE_SDA), 16);
    key(UI_KEY_CLR);
    type("14");
    key(UI_KEY_OK);
    CHECK(!settings_screen_keypad()->open);
    CHECK_EQ(settings_get_int(SET_SENSE_SDA), 14);
    CHECK_EQ(settings_get_int(SET_SENSE_SCL), 15);
    /* PD mini TX never on RX's pin. */
    settings_set(SET_PDMINI_RX, 9.0f);
    open_on(SET_PDMINI_TX);
    type("9");
    key(UI_KEY_OK);
    CHECK(settings_screen_keypad()->refused);
    CHECK_EQ(settings_get_int(SET_PDMINI_TX), -1);
    key(UI_KEY_CLR);
    type("8");
    key(UI_KEY_OK);
    CHECK_EQ(settings_get_int(SET_PDMINI_TX), 8);
}

/* ------------------------------------------------ why a value is refused */

/**
 * @brief @p text typed on @p id's keypad and OK pressed, refused.
 * @param id The setting, its list open.
 * @param text The digits typed.
 * @return The line the keypad shows in place of the range.
 */
static const char *refused_why(setting_id_t id, const char *text)
{
    open_on(id);
    type(text);
    key(UI_KEY_OK);
    CHECK(settings_screen_keypad()->open);
    CHECK(settings_screen_keypad()->refused);
    return settings_screen_keypad()->reason;
}

/* Each value the steps never reach names its reason, in EN and DE, and the
 * next key brings the range back.  A value outside the range names none. */
TEST_CASE(a_refusal_inside_the_range_says_why_until_the_next_key)
{
    static const struct {
        ui_lang_t lang;
        const char *shunt, *limit, *sda, *pdmini;
    } k_why[] = {
        { UI_LANG_EN, "0 or 5.0 to 1000.0 mOhm", "0 or 10 to 32767 mA",
          "Pins the bus takes: 0 4 6 14 16 18 20 26",
          "The other PD mini pin holds this GPIO" },
        { UI_LANG_DE, "0 oder 5.0 bis 1000.0 mOhm", "0 oder 10 bis 32767 mA",
          "Pins, die der Bus nimmt: 0 4 6 14 16 18 20 26",
          "Diesen GPIO hält der andere PD-mini-Pin" },
    };
    for (size_t i = 0; i < sizeof(k_why) / sizeof(k_why[0]); ++i) {
        ui_text_set_language(k_why[i].lang);
        list_of(SET_CAT_SENSORS);
        settings_set(SET_INA_FIT(0), 1.0f);
        ui_router_render(&s_c, 0);
        CHECK_STR_EQ(refused_why(SET_INA_SH(0), "3"), k_why[i].shunt);
        key(UI_KEY_DEL);                            /* the range again */
        CHECK(!settings_screen_keypad()->refused);
        CHECK_STR_EQ(settings_screen_keypad()->reason, "");
        key(UI_KEY_CANCEL);
        /* A limit of 5 mA: OFF is 0, the next step up 10 mA. */
        CHECK_STR_EQ(refused_why(SET_INA_LIM(0), "5"), k_why[i].limit);
        key(UI_KEY_CANCEL);
        CHECK_EQ(settings_get_int(SET_INA_LIM(0)), 0);

        list_of(SET_CAT_IFACE);
        CHECK_STR_EQ(refused_why(SET_SENSE_SDA, "5"), k_why[i].sda);
        key(UI_KEY_1);
        CHECK(!settings_screen_keypad()->refused);
        CHECK_STR_EQ(settings_screen_keypad()->reason, "");
        key(UI_KEY_CANCEL);
        CHECK_EQ(settings_get_int(SET_SENSE_SDA), 16);

        settings_set(SET_PDMINI_RX, 9.0f);
        CHECK_STR_EQ(refused_why(SET_PDMINI_TX, "9"), k_why[i].pdmini);
        key(UI_KEY_CLR);
        CHECK_STR_EQ(settings_screen_keypad()->reason, "");
        key(UI_KEY_CANCEL);
        CHECK_EQ(settings_get_int(SET_PDMINI_TX), -1);

        /* Past the range: the range itself, no reason. */
        list_of(SET_CAT_IFACE);
        CHECK_STR_EQ(refused_why(SET_SENSE_SDA, "48"), "");
        key(UI_KEY_CANCEL);
    }
    ui_text_set_language(UI_LANG_EN);
}

/* The SDA list is built from settings_sense_sda_valid(): every GPIO it
 * takes is listed, and no other. */
TEST_CASE(the_sda_reason_lists_the_pins_the_bus_takes)
{
    char pins[64];
    CHECK_STR_EQ(ui_setting_keypad_sda_pins(pins, sizeof(pins)),
                 "0 4 6 14 16 18 20 26");
    for (int gpio = 0; gpio < 64; ++gpio) {
        char one[8];
        snprintf(one, sizeof(one), " %d ", gpio);
        char padded[70];
        snprintf(padded, sizeof(padded), " %s ", pins);
        CHECK_EQ(strstr(padded, one) != NULL, settings_sense_sda_valid(gpio));
    }
    /* A short buffer keeps whole pins only; none is nothing. */
    char shortbuf[6];
    CHECK_STR_EQ(ui_setting_keypad_sda_pins(shortbuf, sizeof(shortbuf)),
                 "0 4 6");
    CHECK_STR_EQ(ui_setting_keypad_sda_pins(NULL, 8), "");
    CHECK_STR_EQ(ui_setting_keypad_sda_pins(pins, 0), "");
    /* A pin the bus takes, and -1 (no bus), name no reason. */
    char why[64];
    CHECK(ui_setting_keypad_why(SET_SENSE_SDA, 14.0f, why, sizeof(why))
          == NULL);
    CHECK(ui_setting_keypad_why(SET_SENSE_SDA, -1.0f, why, sizeof(why))
          == NULL);
    CHECK_STR_EQ(ui_setting_keypad_why(SET_SENSE_SDA, 5.0f, NULL, 0), "");
    CHECK(ui_setting_keypad_why((setting_id_t)SETTING_COUNT, 1.0f, why,
                                sizeof(why)) == NULL);
}

/* A buffer shorter than the whole reason takes the text and the pins that
 * fit after it whole, in EN (20-byte text) and DE (25 bytes): 8 bytes of
 * room hold "0 4 6", not "0 4 6 14" and its end. */
TEST_CASE(a_short_sda_reason_keeps_whole_pins)
{
    static const struct {
        ui_lang_t lang;
        const char *text, *cut;
    } k_lang[] = {
        { UI_LANG_EN, "Pins the bus takes: ", "Pins the bus takes: 0 4 6" },
        { UI_LANG_DE, "Pins, die der Bus nimmt: ",
          "Pins, die der Bus nimmt: 0 4 6" },
    };
    for (size_t i = 0; i < sizeof(k_lang) / sizeof(k_lang[0]); ++i) {
        ui_text_set_language(k_lang[i].lang);
        const size_t fixed = strlen(k_lang[i].text);
        char why[64];
        CHECK_STR_EQ(ui_setting_keypad_why(SET_SENSE_SDA, 5.0f, why,
                                           fixed + 8u), k_lang[i].cut);
        /* One byte more holds "14" whole. */
        char want[64];
        snprintf(want, sizeof(want), "%s0 4 6 14", k_lang[i].text);
        CHECK_STR_EQ(ui_setting_keypad_why(SET_SENSE_SDA, 5.0f, why,
                                           fixed + 9u), want);
        /* No room after the text: the text, no pin. */
        CHECK_STR_EQ(ui_setting_keypad_why(SET_SENSE_SDA, 5.0f, why,
                                           fixed + 1u), k_lang[i].text);
        /* The keypad's 64 bytes hold the whole list. */
        snprintf(want, sizeof(want), "%s0 4 6 14 16 18 20 26",
                 k_lang[i].text);
        CHECK_STR_EQ(ui_setting_keypad_why(SET_SENSE_SDA, 5.0f, why,
                                           sizeof(why)), want);
        /* A larger buffer holds no more: the list is the whole list. */
        char big[128];
        CHECK_STR_EQ(ui_setting_keypad_why(SET_SENSE_SDA, 5.0f, big,
                                           sizeof(big)), want);
    }
    ui_text_set_language(UI_LANG_EN);
}

/* The off gap's reason is built from the row: every row with an off gap
 * is a shunt of 0 or 5.0 to 1000.0 mOhm in 0.1 steps or a limit of 0 or
 * 10 to 32767 mA in 10 mA steps, and its reason says so.  A NULL buffer
 * gives an empty text. */
TEST_CASE(the_off_gap_reason_is_what_the_schema_holds)
{
    int shunts = 0;
    int limits = 0;
    char why[64];
    for (int i = 0; i < (int)SETTING_COUNT; ++i) {
        const setting_id_t id = (setting_id_t)i;
        const setting_def_t *d = settings_def(id);
        if (d == NULL || d->off_below <= 0.0f) {
            continue;
        }
        CHECK_NEAR(d->min, 0.0f, 1e-6f);
        if (strcmp(d->unit, "mOhm") == 0) {
            ++shunts;
            CHECK_NEAR(d->off_below, 5.0f, 1e-6f);
            CHECK_NEAR(d->max, 1000.0f, 1e-6f);
            CHECK_EQ(settings_decimals(id), 1);
            CHECK_STR_EQ(ui_setting_keypad_why(id, 4.9f, why, sizeof(why)),
                         "0 or 5.0 to 1000.0 mOhm");
        } else {
            ++limits;
            CHECK_STR_EQ(d->unit, "mA");
            CHECK_NEAR(d->off_below, 10.0f, 1e-6f);
            CHECK_NEAR(d->max, 32767.0f, 1e-6f);
            CHECK_EQ(settings_decimals(id), 0);
            CHECK_STR_EQ(ui_setting_keypad_why(id, 9.0f, why, sizeof(why)),
                         "0 or 10 to 32767 mA");
        }
        CHECK_STR_EQ(ui_setting_keypad_why(id, 1.0f, NULL, 0u), "");
        CHECK(ui_setting_keypad_why(id, 0.0f, why, sizeof(why)) == NULL);
        CHECK(ui_setting_keypad_why(id, d->off_below, why, sizeof(why))
              == NULL);
    }
    CHECK_EQ(shunts, SET_INA_CHANNELS);
    CHECK_EQ(limits, SET_INA_CHANNELS);
}

/* ------------------------------------------------------ ARMED and held */

static setting_id_t s_held = SETTING_COUNT;

/**
 * @brief The hold under test: one setting, s_held.
 * @param id The setting asked about.
 * @return Whether it is held.
 */
static bool hold_one(setting_id_t id)
{
    return id == s_held;
}

/* SETUP's steps are not refused while the bench is ARMED, and neither is
 * the keypad; a row held has no keys and opens no keypad, and a keypad
 * open when its row is held closes on the next frame with nothing set. */
TEST_CASE(armed_steps_and_keypad_alike_and_a_held_row_refuses_both)
{
    list_of(SET_CAT_IFACE);
    set_armed(true);
    open_on(SET_INA228_UOHM);
    type("300");
    key(UI_KEY_OK);
    CHECK_EQ(settings_get_int(SET_INA228_UOHM), 300);
    tap(PLUS_X + BTN_W / 2, show(SET_INA228_UOHM));
    CHECK_EQ(settings_get_int(SET_INA228_UOHM), 301);
    set_armed(false);

    s_held = SET_INA228_UOHM;
    settings_set_hold(hold_one);
    snap();
    tap(XV, show(SET_INA228_UOHM));
    CHECK(!settings_screen_keypad()->open);
    tap(PLUS_X + BTN_W / 2, show(SET_INA228_UOHM));
    CHECK_EQ(changed(), 0);

    settings_set_hold(NULL);
    open_on(SET_INA228_UOHM);
    type("777");
    settings_set_hold(hold_one);
    ui_router_tick(0.016f);
    CHECK(!settings_screen_keypad()->open);
    CHECK_EQ(changed(), 0);
    settings_set_hold(NULL);
    s_held = SETTING_COUNT;
}

/* A press on a key the touch stream lost is let go; what was typed
 * stays. */
TEST_CASE(a_lost_press_keeps_what_was_typed)
{
    list_of(SET_CAT_IFACE);
    open_on(SET_INA228_UOHM);
    type("12");
    const gfx_rect_t r = ui_keypad_key_rect(settings_screen_keypad(),
                                            UI_KEY_3);
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 2, .x = (int16_t)(r.x + r.w / 2),
                                   .y = (int16_t)(UI_BAND_H + r.y + r.h / 2) } };
    ui_router_event(&e);
    CHECK(settings_screen_keypad()->pressed >= 0);
    ui_router_cancel_gestures();
    CHECK(settings_screen_keypad()->pressed < 0);
    CHECK(settings_screen_keypad()->open);
    CHECK_STR_EQ(settings_screen_keypad()->entry, "12");
}

/* The range's ends of a shunt: 1000.0 taken, 1000.1 refused. */
TEST_CASE(a_shunt_takes_1000_0_and_refuses_1000_1)
{
    const setting_id_t id = SET_INA_SH(0);
    list_of(SET_CAT_SENSORS);
    settings_set(SET_INA_FIT(0), 1.0f);
    ui_router_render(&s_c, 0);
    open_on(id);
    type("1000.1");
    key(UI_KEY_OK);
    CHECK(settings_screen_keypad()->refused);
    CHECK_NEAR(settings_get(id), 100.0f, 1e-4f);
    key(UI_KEY_DEL);
    type("0");
    CHECK_STR_EQ(settings_screen_keypad()->entry, "1000.0");
    key(UI_KEY_OK);
    CHECK(!settings_screen_keypad()->open);
    CHECK_NEAR(settings_get(id), 1000.0f, 1e-3f);
}

/* A pin's -1 is "not set", not a sign: its keypad has no +/- key and
 * takes 0, the lowest pin. */
TEST_CASE(a_pin_from_minus_1_takes_0_on_an_unsigned_keypad)
{
    list_of(SET_CAT_IFACE);
    CHECK_EQ(settings_get_int(SET_OPTICAL_PIN), -1);
    open_on(SET_OPTICAL_PIN);
    CHECK(!settings_screen_keypad()->sign);
    CHECK_NEAR(settings_screen_keypad()->min, -1.0f, 1e-6f);
    CHECK_EQ(ui_keypad_key_rect(settings_screen_keypad(), UI_KEY_SIGN).w, 0);
    type("0");
    key(UI_KEY_OK);
    CHECK_EQ(settings_get_int(SET_OPTICAL_PIN), 0);
    tap(MINUS_X + BTN_W / 2, show(SET_OPTICAL_PIN));     /* back to -1 */
    CHECK_EQ(settings_get_int(SET_OPTICAL_PIN), -1);
}

/* STOP in the band works over the keypad, and the keypad stays as it was:
 * open, what was typed kept, nothing set. */
TEST_CASE(stop_over_the_keypad_stops_and_sets_nothing)
{
    list_of(SET_CAT_IFACE);
    snap();
    open_on(SET_INA228_UOHM);
    type("123");
    (void)ui_router_take_stop();
    const gfx_rect_t r = ui_band_stop_rect();
    tap(r.x + r.w / 2, r.y + r.h / 2);
    CHECK(ui_router_take_stop());
    CHECK(settings_screen_keypad()->open);
    CHECK_STR_EQ(settings_screen_keypad()->entry, "123");
    CHECK_EQ(changed(), 0);
    key(UI_KEY_CANCEL);
    CHECK_EQ(changed(), 0);
}

/* ------------------------------------------------------------ the helper */

TEST_CASE(the_helper_takes_its_decimals_from_the_step)
{
    fresh_model();
    CHECK_EQ(ui_setting_keypad_decimals(SET_INA228_UOHM), 0);    /* 1    */
    CHECK_EQ(ui_setting_keypad_decimals(SET_INA228_MAX_A), 1);   /* 0.1  */
    CHECK_EQ(ui_setting_keypad_decimals(SET_SUPPLY_I_MAX), 2);   /* 0.05 */
    CHECK_EQ(ui_setting_keypad_decimals(SET_SUPPLY_V_MAX), 2);   /* 0.02 */
    CHECK_EQ(ui_setting_keypad_decimals(SET_SERVO_IDLE_MAX), 2); /* 0.01 */
    CHECK_EQ(ui_setting_keypad_decimals(SET_SERVO_PROG_I), 3);   /* 0.001 */
    CHECK_EQ(ui_setting_keypad_decimals(SET_STICK_TIMEOUT), 0);  /* 5000 */
    CHECK_EQ(ui_setting_keypad_decimals((setting_id_t)SETTING_COUNT), 0);
    CHECK(ui_setting_keypad_offered(SET_STICK_V));
    CHECK(!ui_setting_keypad_offered(SET_LANGUAGE));
    CHECK(!ui_setting_keypad_offered(SET_LOG_AUTO));
    CHECK(!ui_setting_keypad_offered(SET_SENSE_SCL));
    CHECK(!ui_setting_keypad_offered((setting_id_t)-1));
    CHECK(!ui_setting_keypad_takes((setting_id_t)SETTING_COUNT, 1.0f));
    /* Opened for a row it does not offer, it stays shut. */
    ui_keypad_t k;
    memset(&k, 0, sizeof(k));
    ui_setting_keypad_open(&k, gfx_rect_make(0, 0, 400, 300), SET_LANGUAGE);
    CHECK(!k.open);
    ui_setting_keypad_open(NULL, gfx_rect_make(0, 0, 400, 300), SET_STICK_V);
    CHECK_EQ(ui_setting_keypad_ok(&k, SET_LANGUAGE, 1.0f), UI_SETKEY_LOCKED);
}

int main(void)
{
    RUN(a_tap_on_a_value_opens_its_keypad);
    RUN(a_tap_beside_the_value_opens_nothing);
    RUN(the_range_ends_are_taken_and_a_value_past_them_refused);
    RUN(a_digit_past_the_steps_decimals_is_not_typed);
    RUN(cancel_and_the_home_tag_change_nothing);
    RUN(nothing_under_the_keypad_presses);
    RUN(a_typed_shunt_saves_what_556_steps_save);
    RUN(a_value_between_steps_takes_the_nearer_step);
    RUN(a_shunt_between_off_and_five_milliohm_is_refused);
    RUN(a_pin_the_row_does_not_take_is_refused);
    RUN(a_refusal_inside_the_range_says_why_until_the_next_key);
    RUN(the_sda_reason_lists_the_pins_the_bus_takes);
    RUN(a_short_sda_reason_keeps_whole_pins);
    RUN(the_off_gap_reason_is_what_the_schema_holds);
    RUN(armed_steps_and_keypad_alike_and_a_held_row_refuses_both);
    RUN(a_lost_press_keeps_what_was_typed);
    RUN(a_shunt_takes_1000_0_and_refuses_1000_1);
    RUN(a_pin_from_minus_1_takes_0_on_an_unsigned_keypad);
    RUN(stop_over_the_keypad_stops_and_sets_nothing);
    RUN(the_helper_takes_its_decimals_from_the_step);
    return test_summary("settings_keypad");
}
