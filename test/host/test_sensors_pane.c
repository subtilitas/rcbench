/*
 * SETUP > SENSORS' split pane (sensors_pane.h, settings_screen.c,
 * settings_draw.c).
 *
 * Under test: the 8 entries and the rows each shows, on and off; vibration
 * and the optical tacho on no entry with their keys kept; every state word;
 * RESET of the selected entry alone, hidden rows included; the left list's
 * press and release: a tap, a slide off, a jump, a second finger, a lost
 * press, a press under the keypad; the scroll each entry keeps; unsaved edits
 * across a change of entry; the carrier's held rows.
 *
 * SPDX-License-Identifier: MIT
 */
#include "settings_rig.h"

#include "sensors_rows.h"
#include "ui_board.h"
#include "ui_setting_keypad.h"

#define RBMOD  ((uint16_t)OUTBIND_BOARD_RBMOD)
#define MODULE ((uint16_t)OUTBIND_BOARD_PICO_HEADER)
#define PANE_X 240

/* SENSORS open at the defaults, the touch feed ready. */
static void pane(void)
{
    fresh_hub();
    open_category(SET_CAT_SENSORS);
    feed_reset();
    feed_tick_per_report(REPORT_S);
    snap();
}

/* The state of @p e, its colour into @p ink when not NULL. */
static const char *state_of(int e, gfx_color_t *ink)
{
    static char buf[48];
    gfx_color_t c = 0;
    (void)sensors_pane_state(e, buf, sizeof(buf), &c);
    if (ink != NULL) {
        *ink = c;
    }
    return buf;
}

/* ------------------------------------------------------------ the entries */

TEST_CASE(the_list_names_8_entries_in_order)
{
    fresh_hub();
    static const char *const en[SENS_ENTRY_COUNT] = {
        "SENSOR BUS", "INA228", "INA3221 0x40", "INA3221 0x41",
        "INA3221 0x42", "INA3221 0x43", "AS5600", "PHASE TAP",
    };
    static const char *const de[SENS_ENTRY_COUNT] = {
        "SENSORBUS", "INA228", "INA3221 0x40", "INA3221 0x41",
        "INA3221 0x42", "INA3221 0x43", "AS5600", "PHASENABGRIFF",
    };
    CHECK_EQ(SENS_ENTRY_COUNT, 8);
    char buf[24];
    for (int e = 0; e < SENS_ENTRY_COUNT; ++e) {
        ui_text_set_language(UI_LANG_EN);
        CHECK_STR_EQ(sensors_pane_name(e, buf, sizeof(buf)), en[e]);
        ui_text_set_language(UI_LANG_DE);
        CHECK_STR_EQ(sensors_pane_name(e, buf, sizeof(buf)), de[e]);
    }
    ui_text_set_language(UI_LANG_EN);
    CHECK_STR_EQ(sensors_pane_name(-1, buf, sizeof(buf)), "");
    CHECK_STR_EQ(sensors_pane_name(SENS_ENTRY_COUNT, buf, sizeof(buf)), "");
    CHECK_STR_EQ(sensors_pane_name(0, NULL, 0u), "");
    CHECK_STR_EQ(sensors_pane_name(0, buf, 0u), "");
}

/* Each row of SET_CAT_SENSORS on one entry, but for the two with no firmware
 * behind them; their keys stay. */
TEST_CASE(every_sensor_row_has_one_entry_and_balance_has_none)
{
    fresh_hub();
    setting_id_t ids[64];
    const int n = settings_in_category(SET_CAT_SENSORS, ids, 64);
    CHECK_EQ(n, 58);
    int on_entry = 0;
    for (int i = 0; i < n; ++i) {
        const int e = sensors_pane_entry_of(ids[i]);
        if (e < 0) {
            CHECK(ids[i] == SET_VIBE_EN || ids[i] == SET_OPTICAL_EN
                  || ids[i] == SET_OPTICAL_PIN);
        } else {
            ++on_entry;
        }
    }
    CHECK_EQ(on_entry, 55);
    CHECK_EQ(sensors_pane_entry_of(SET_HITEC_RX), -1);
    CHECK_STR_EQ(settings_def(SET_VIBE_EN)->key, "vibe_en");
    CHECK_STR_EQ(settings_def(SET_OPTICAL_EN)->key, "optical_en");
    CHECK_STR_EQ(settings_def(SET_OPTICAL_PIN)->key, "optical_pin");
    /* The PD mini's wiring is SUPPLY's PD MINI tab's. */
    CHECK_EQ(settings_in_category(SET_CAT_PDMINI, ids, 64), 4);
    for (int i = 0; i < 4; ++i) {
        CHECK(ids[i] >= SET_PDMINI_EN && ids[i] <= SET_PDMINI_BAUD);
    }
}

/* The moved rows' stored keys are the ones a panel in the field holds. */
TEST_CASE(a_moved_row_keeps_its_key)
{
    static const struct { setting_id_t id; const char *key; } k[] = {
        { SET_INA228_EN, "ina228_en" }, { SET_INA228_ADDR, "ina228_adr" },
        { SET_INA228_UOHM, "ina228_uohm" }, { SET_INA228_MAX_A, "ina228_max_a" },
        { SET_SENSE_SDA, "sns_sda" }, { SET_SENSE_SCL, "sns_scl" },
        { SET_ENC_EN, "enc_en" }, { SET_ENC_CENTRE, "enc_centre" },
        { SET_TONE_EN, "tone_en" }, { SET_TONE_PIN, "tone_pin" },
        { SET_TONE_F_MIN, "tone_fmin" }, { SET_TONE_F_MAX, "tone_fmax" },
        { SET_TONE_SPLIT, "tone_split" }, { SET_TONE_GAP, "tone_gap" },
        { SET_TONE_PERIODS, "tone_periods" },
    };
    for (size_t i = 0u; i < sizeof(k) / sizeof(k[0]); ++i) {
        CHECK_STR_EQ(settings_def(k[i].id)->key, k[i].key);
        CHECK_EQ(settings_def(k[i].id)->cat, SET_CAT_SENSORS);
    }
}

/* The enable row alone while off, the sensor's rows under it while on; the
 * bus has no enable row. */
TEST_CASE(rows_appear_when_a_sensor_is_enabled)
{
    static const struct { int e; setting_id_t en; int on; } k[] = {
        { SENS_ENTRY_INA228, SET_INA228_EN, 4 },
        { SENS_ENTRY_INA3221_0, SET_INA_FIT_0, 10 },
        { SENS_ENTRY_INA3221_0 + 3, (setting_id_t)(SET_INA_FIT_0 + 3), 10 },
        { SENS_ENTRY_AS5600, SET_ENC_EN, 2 },
        { SENS_ENTRY_TAP, SET_TONE_EN, 7 },
    };
    pane();
    CHECK_EQ(settings_screen_rows(SET_CAT_SENSORS), 2);
    for (size_t i = 0u; i < sizeof(k) / sizeof(k[0]); ++i) {
        tap(entry_x(k[i].e), entry_y(k[i].e));
        CHECK_EQ(sensors_pane_selected(), k[i].e);
        CHECK_EQ(settings_screen_rows(SET_CAT_SENSORS), 1);
        /* The tap on the enable row's ">" brings the others. */
        tap(XP, LIST_Y + ROW_H / 2);
        CHECK(settings_get_bool(k[i].en));
        CHECK_EQ(settings_screen_rows(SET_CAT_SENSORS), k[i].on);
        tap(XM, LIST_Y + ROW_H / 2);
        CHECK(!settings_get_bool(k[i].en));
        CHECK_EQ(settings_screen_rows(SET_CAT_SENSORS), 1);
    }
    CHECK_EQ(sensors_pane_rows(-1, NULL, 0), 0);
    CHECK_EQ(sensors_pane_rows(SENS_ENTRY_COUNT, NULL, 0), 0);
    /* Out of range selects nothing. */
    sensors_pane_select(-1);
    sensors_pane_select(SENS_ENTRY_COUNT);
    CHECK_EQ(sensors_pane_selected(), SENS_ENTRY_TAP);
}

/* The pane's rows lie right of the left list, the keys where they are on
 * every list; another list keeps the full width. */
TEST_CASE(the_pane_stands_beside_the_list_and_the_keys_stay)
{
    pane();
    CHECK_EQ(settings_screen_list_rect().x, PANE_X);
    CHECK_EQ(settings_screen_list_rect().w, 754);
    CHECK_EQ(settings_screen_row_rect(0).x, PANE_X);
    CHECK_EQ(settings_screen_row_part(0, SETTINGS_ROW_PLUS).x, PLUS_X);
    CHECK_EQ(settings_screen_row_part(0, SETTINGS_ROW_MINUS).x, MINUS_X);
    gfx_rect_t last = settings_screen_entry_rect(SENS_ENTRY_COUNT - 1);
    CHECK(last.x + last.w < PANE_X);
    CHECK_EQ(last.y + last.h, 8 + 428);   /* where the list ends */
    open_category(SET_CAT_APP);
    CHECK_EQ(settings_screen_list_rect().x, 10);
    CHECK_EQ(settings_screen_list_rect().w, 984);
    CHECK_EQ(settings_screen_row_part(0, SETTINGS_ROW_PLUS).x, PLUS_X);
    /* PANEL has no left list: a tap there is on a row's label. */
    snap();
    tap(entry_x(0), entry_y(3));
    CHECK_EQ(changed(), 0);
}

/* What a screen shows in place of a count: what the tile holds. */
TEST_CASE(a_setup_tile_line_names_what_sensors_holds)
{
    fresh_hub();
    ui_router_render(&s_c, 0);
    CHECK_STR_EQ(TR(SET_HUB_SENSORS), "CURRENT, ANGLE, PHASE TAP");
    /* The selection does not change what a tile says. */
    static gfx_color_t first[W * H];
    memcpy(first, s_fb, sizeof(first));
    sensors_pane_select(SENS_ENTRY_INA3221_0);
    settings_screen_invalidate();
    ui_router_render(&s_c, 0);
    CHECK(memcmp(first, s_fb, sizeof(first)) == 0);
}

/* ------------------------------------------------------------- the states */

TEST_CASE(every_entry_says_its_state)
{
    fresh_hub();
    gfx_color_t ink = 0;

    /* The bus: its pins; soldered words are the carrier's. */
    CHECK_STR_EQ(state_of(SENS_ENTRY_BUS, &ink), "GPIO 16 / 17");
    CHECK_EQ(ink, UI_OK);
    settings_set(SET_SENSE_SDA, -1.0f);
    CHECK_STR_EQ(state_of(SENS_ENTRY_BUS, &ink), "NO PINS SET");
    CHECK_EQ(ink, UI_WARN);
    settings_set(SET_SENSE_SDA, 16.0f);
    settings_screen_set_unheld(SENSE_LINK_ROW_PINS, 0u);
    CHECK_STR_EQ(state_of(SENS_ENTRY_BUS, &ink), "NOT ACCEPTED BY THE IOMCU");
    CHECK_EQ(ink, UI_WARN);
    settings_screen_set_unheld(0u, 0u);

    /* Off: every sensor. */
    for (int e = SENS_ENTRY_INA228; e < SENS_ENTRY_COUNT; ++e) {
        CHECK_STR_EQ(state_of(e, &ink), "OFF");
        CHECK_EQ(ink, UI_TEXT_DIM);
    }

    /* The INA228: the words of a part, and the INA3221 it shares with. */
    settings_set(SET_INA228_EN, 1.0f);
    static const struct { sens_part_t st; const char *en; } k[] = {
        { SENS_PART_UNKNOWN, "NOT READ YET" },
        { SENS_PART_PENDING, "NOT WRITTEN YET" },
        { SENS_PART_SEARCHING, "LOOKING FOR THE PART" },
        { SENS_PART_ONLINE, "ONLINE" },
        { SENS_PART_SILENT, "NOT ANSWERING" },
        { SENS_PART_WRONG, "ANOTHER ID ANSWERS" },
        { SENS_PART_REFUSED, "NOT ACCEPTED BY THE IOMCU" },
        { SENS_PART_NO_BUS, "NO BUS PINS" },
        { SENS_PART_SAME_ADDR, "SAME ADDRESS AS INA3221" },
    };
    for (size_t i = 0u; i < sizeof(k) / sizeof(k[0]); ++i) {
        sensors_rows_set_i228(k[i].st);
        CHECK_EQ(sensors_rows_i228(), k[i].st);
        CHECK_STR_EQ(state_of(SENS_ENTRY_INA228, &ink), k[i].en);
        CHECK_EQ(ink, sensors_rows_word_color(k[i].st));
    }
    sensors_rows_set_i228(SENS_PART_UNKNOWN);

    /* A part: its own word, 4 bits of the status word. */
    settings_set(SET_INA_FIT(1), 1.0f);
    sensors_rows_set_status((uint16_t)(SENS_PART_SILENT << 4));
    CHECK_STR_EQ(state_of(SENS_ENTRY_INA3221_0 + 1, &ink), "NOT ANSWERING");
    CHECK_EQ(ink, UI_WARN);
    sensors_rows_set_status(0u);

    /* The AS5600 and the tap: ON, or NOT ACCEPTED while the IOmcu does not
     * hold a row of theirs, whichever row. */
    settings_set(SET_ENC_EN, 1.0f);
    settings_set(SET_TONE_EN, 1.0f);
    CHECK_STR_EQ(state_of(SENS_ENTRY_AS5600, &ink), "ON");
    CHECK_EQ(ink, UI_OK);
    CHECK_STR_EQ(state_of(SENS_ENTRY_TAP, &ink), "ON");
    settings_screen_set_unheld(SENSE_LINK_ROW_ENC, 0u);
    CHECK_STR_EQ(state_of(SENS_ENTRY_AS5600, &ink), "NOT ACCEPTED BY THE IOMCU");
    CHECK_STR_EQ(state_of(SENS_ENTRY_TAP, &ink), "ON");
    settings_screen_set_unheld(0u, TONE_LINK_ROWS_BEEP);
    CHECK_STR_EQ(state_of(SENS_ENTRY_TAP, &ink), "NOT ACCEPTED BY THE IOMCU");
    CHECK_EQ(ink, UI_WARN);
    settings_screen_set_unheld(0u, 0u);

    /* Out of range and no room. */
    CHECK_STR_EQ(state_of(-1, &ink), "");
    CHECK_STR_EQ(state_of(SENS_ENTRY_COUNT, &ink), "");
    char buf[8];
    CHECK_STR_EQ(sensors_pane_state(0, NULL, 0u, &ink), "");
    CHECK_STR_EQ(sensors_pane_state(0, buf, sizeof(buf), NULL), "");
}

/* A state that changes repaints the screen, as the parts' do. */
TEST_CASE(the_ina228_state_repaints_the_list)
{
    pane();
    settings_set(SET_INA228_EN, 1.0f);
    settings_screen_invalidate();
    ui_router_render(&s_c, 0);
    static gfx_color_t was[W * H];
    memcpy(was, s_fb, sizeof(was));
    sensors_rows_set_i228(SENS_PART_ONLINE);
    ui_router_render(&s_c, 0);
    CHECK(memcmp(was, s_fb, sizeof(was)) != 0);
    /* Said again: nothing to draw. */
    memset(s_fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    ui_router_render(&s_c, 0);
    int blank = 0;
    for (int i = 0; i < W * H; ++i) {
        blank += (s_fb[i] == 0) ? 1 : 0;
    }
    CHECK(blank > (W * H) / 2);
    sensors_rows_set_i228(SENS_PART_UNKNOWN);
}

/* -------------------------------------------------------------- the reset */

TEST_CASE(reset_takes_the_selected_entry_alone_hidden_rows_too)
{
    pane();
    settings_set(SET_TONE_EN, 1.0f);
    settings_set(SET_TONE_F_MIN, 800.0f);
    settings_set(SET_INA228_EN, 1.0f);
    settings_set(SET_INA228_UOHM, 400.0f);
    settings_set(SET_INA228_EN, 0.0f);       /* the shunt's row hides */
    settings_set(SET_SENSE_SDA, 14.0f);
    settings_set(SET_ENC_CENTRE, 100.0f);

    tap(entry_x(SENS_ENTRY_INA228), entry_y(SENS_ENTRY_INA228));
    tap(RESET_X + 100, RESET_Y + RESET_H / 2);
    CHECK_EQ(settings_get_int(SET_INA228_UOHM), 200);
    CHECK_EQ(settings_get_int(SET_TONE_F_MIN), 800);
    CHECK_EQ(settings_get_int(SET_SENSE_SDA), 14);
    CHECK_EQ(settings_get_int(SET_ENC_CENTRE), 100);
    CHECK_EQ(settings_screen_category(), (int)SET_CAT_SENSORS);

    tap(entry_x(SENS_ENTRY_BUS), entry_y(SENS_ENTRY_BUS));
    tap(RESET_X + 100, RESET_Y + RESET_H / 2);
    CHECK_EQ(settings_get_int(SET_SENSE_SDA), 16);
    CHECK_EQ(settings_get_int(SET_SENSE_SCL), 17);
    CHECK_EQ(settings_get_int(SET_TONE_F_MIN), 800);

    tap(entry_x(SENS_ENTRY_TAP), entry_y(SENS_ENTRY_TAP));
    tap(RESET_X + 100, RESET_Y + RESET_H / 2);
    CHECK(!settings_get_bool(SET_TONE_EN));
    CHECK_EQ(settings_get_int(SET_TONE_F_MIN), 400);
    CHECK_EQ(settings_screen_rows(SET_CAT_SENSORS), 1);
    CHECK_EQ(settings_get_int(SET_ENC_CENTRE), 100);
}

/* The label names the entry, in both languages, and fits its button. */
TEST_CASE(the_reset_label_names_the_entry_and_fits)
{
    fresh_hub();
    const int room = HUB_COL_W - 16;
    char name[24], label[48];
    for (int lang = 0; lang < UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        for (int e = 0; e < SENS_ENTRY_COUNT; ++e) {
            snprintf(label, sizeof(label), TR(SET_RESET_ONE),
                     sensors_pane_name(e, name, sizeof(name)));
            CHECK(gfx_text_width(&gfx_font_10x20, label, 1) <= room);
        }
    }
    ui_text_set_language(UI_LANG_DE);
    snprintf(label, sizeof(label), TR(SET_RESET_ONE),
             sensors_pane_name(SENS_ENTRY_TAP, name, sizeof(name)));
    CHECK_STR_EQ(label, "PHASENABGRIFF ZURÜCKSETZEN");
    ui_text_set_language(UI_LANG_EN);
    snprintf(label, sizeof(label), TR(SET_RESET_ONE),
             sensors_pane_name(SENS_ENTRY_INA228, name, sizeof(name)));
    CHECK_STR_EQ(label, "RESET INA228");
}

/* Only a change is an edit: a reset of defaults keeps SAVED and a failure;
 * a held row stays through a reset; NULL and out of range do nothing. */
TEST_CASE(a_reset_with_nothing_to_change_is_no_edit)
{
    pane();
    settings_set_observer(observer);
    s_observed = 0;
    sensors_pane_reset(SENS_ENTRY_AS5600);
    sensors_pane_reset(-1);
    sensors_pane_reset(SENS_ENTRY_COUNT);
    settings_reset_rows(NULL, 3);
    const setting_id_t bad[2] = { (setting_id_t)-1, (setting_id_t)SETTING_COUNT };
    settings_reset_rows(bad, 2);
    CHECK_EQ(s_observed, 0);
    CHECK(!settings_dirty());
    settings_set(SET_ENC_CENTRE, 5.0f);
    CHECK_EQ(s_observed, 1);
    CHECK(settings_dirty());
    sensors_pane_reset(SENS_ENTRY_AS5600);
    CHECK_EQ(s_observed, 2);
    CHECK_EQ(settings_get_int(SET_ENC_CENTRE), 0);
    settings_set_observer(NULL);
}

/* ------------------------------------------------------------ the left list */

TEST_CASE(a_press_arms_an_entry_and_its_release_selects_it)
{
    pane();
    const int x = entry_x(SENS_ENTRY_AS5600), y = entry_y(SENS_ENTRY_AS5600);
    finger(FEED_LONE, x, y);
    idle(1.0f);
    CHECK_EQ(sensors_pane_selected(), SENS_ENTRY_BUS);   /* armed only */
    lift(FEED_LONE);
    CHECK_EQ(sensors_pane_selected(), SENS_ENTRY_AS5600);
    CHECK_EQ(changed(), 0);
    /* Every entry, its first and last px; the gap between two is none. */
    for (int e = 0; e < SENS_ENTRY_COUNT; ++e) {
        const gfx_rect_t r = settings_screen_entry_rect(e);
        feed_tap(FEED_LONE, r.x, UI_BAND_H + r.y);
        CHECK_EQ(sensors_pane_selected(), e);
        sensors_pane_select(SENS_ENTRY_TAP);
        feed_tap(FEED_LONE, r.x + r.w - 1, UI_BAND_H + r.y + r.h - 1);
        CHECK_EQ(sensors_pane_selected(), e);
        if (e + 1 < SENS_ENTRY_COUNT) {
            for (int g = r.y + r.h; g < r.y + 54; ++g) {
                feed_tap(FEED_LONE, r.x + 10, UI_BAND_H + g);
                CHECK_EQ(sensors_pane_selected(), e);
            }
        }
    }
    /* Beside the list, before the pane: nothing. */
    feed_tap(FEED_LONE, 235, entry_y(0));
    CHECK_EQ(sensors_pane_selected(), SENS_ENTRY_COUNT - 1);
}

TEST_CASE(sliding_off_an_entry_selects_nothing)
{
    pane();
    const int x = entry_x(SENS_ENTRY_INA228), y = entry_y(SENS_ENTRY_INA228);
    /* 8px is a tap, 9px is not, down, up and to the side. */
    finger(FEED_LONE, x, y);
    glide(FEED_LONE, x, y + 8, 1);
    lift(FEED_LONE);
    CHECK_EQ(sensors_pane_selected(), SENS_ENTRY_INA228);
    sensors_pane_select(SENS_ENTRY_BUS);
    static const int off[][2] = { { 0, 9 }, { 0, -9 }, { 9, 0 }, { -9, 0 } };
    for (size_t i = 0u; i < 4u; ++i) {
        finger(FEED_LONE, x, y);
        glide(FEED_LONE, x + off[i][0], y + off[i][1], 1);
        lift(FEED_LONE);
        CHECK_EQ(sensors_pane_selected(), SENS_ENTRY_BUS);
    }
    /* Out and back: the press has ended. */
    finger(FEED_LONE, x, y);
    glide(FEED_LONE, x, y + 40, 4);
    glide(FEED_LONE, x, y, 4);
    lift(FEED_LONE);
    CHECK_EQ(sensors_pane_selected(), SENS_ENTRY_BUS);
    /* From an entry to the next one: neither. */
    finger(FEED_LONE, x, y);
    glide(FEED_LONE, x, entry_y(SENS_ENTRY_INA3221_0), 4);
    lift(FEED_LONE);
    CHECK_EQ(sensors_pane_selected(), SENS_ENTRY_BUS);
    /* From an entry onto a row's key: nothing steps. */
    finger(FEED_LONE, x, y);
    glide(FEED_LONE, XP, LIST_Y + ROW_H / 2, 40);
    lift(FEED_LONE);
    CHECK_EQ(changed(), 0);
    CHECK_EQ(sensors_pane_selected(), SENS_ENTRY_BUS);
}

/* A drag on the left list scrolls no pane, and a drag that starts on a row
 * and ends on the list selects nothing. */
TEST_CASE(a_drag_on_the_list_scrolls_nothing)
{
    gesture_screen(SET_CAT_SENSORS);
    const int max_y = entry_y(SENS_ENTRY_COUNT - 1);
    swipe(entry_x(0), max_y, entry_x(0), entry_y(0), 9);
    CHECK_EQ(settings_screen_scroll(NULL), 0);
    CHECK_EQ(changed(), 0);
    CHECK_EQ(sensors_pane_selected(), SENS_ENTRY_INA3221_0);
    swipe(XL, LIST_Y + 300, entry_x(1), LIST_Y + 100, 9);
    CHECK_EQ(settings_screen_scroll(NULL), 200);
    CHECK_EQ(sensors_pane_selected(), SENS_ENTRY_INA3221_0);
    CHECK_EQ(changed(), 0);
}

/* A flick the tracker splits lands as a DOWN flagged JUMP, and a release the
 * finger did not make: neither selects. */
TEST_CASE(a_jump_onto_an_entry_selects_nothing)
{
    pane();
    finger(FEED_LONE, XL, LIST_Y + 340);
    finger(FEED_LONE, entry_x(2), LIST_Y + 340 - 110);
    lift(FEED_LONE);
    CHECK_EQ(sensors_pane_selected(), SENS_ENTRY_BUS);
    /* Pressed, then next seen 300px on. */
    finger(FEED_LONE, entry_x(2), entry_y(2));
    finger(FEED_LONE, XL, entry_y(2) + 300);
    lift(FEED_LONE);
    CHECK_EQ(sensors_pane_selected(), SENS_ENTRY_BUS);
}

/* A flick down the list whose next report lands on RESET arrives as a DOWN
 * flagged JUMP there and its lift as a tap: it arms nothing. */
TEST_CASE(a_flick_into_the_strip_resets_nothing)
{
    gesture_screen(SET_CAT_SENSORS);
    finger(FEED_LONE, RESET_X + 100, LIST_Y + 300);
    finger(FEED_LONE, RESET_X + 100, RESET_Y + 10);
    glide(FEED_LONE, RESET_X + 100, RESET_Y + 30, 4);
    lift(FEED_LONE);
    idle(0.5f);
    CHECK_EQ(changed(), 0);
    CHECK(settings_get_bool(SET_INA_FIT(0)));

    /* A tap on it still acts. */
    feed_tap(FEED_LONE, RESET_X + 100, RESET_Y + 10);
    CHECK(!settings_get_bool(SET_INA_FIT(0)));
}

/* The warning's DISCARD throws every edit away: a flick arms it no more. */
TEST_CASE(a_flick_onto_the_warning_answers_nothing)
{
    fresh_hub();
    settings_set_store(&s_mem_store);
    settings_set(SET_ENC_CENTRE, 5.0f);
    feed_reset();
    feed_tick_per_report(REPORT_S);
    tap(home_x(), home_y());
    CHECK(settings_screen_warning());
    finger(FEED_LONE, RESET_X + 100, LIST_Y + 300);
    finger(FEED_LONE, RESET_X + 100, RESET_Y + 10);
    lift(FEED_LONE);
    CHECK(settings_screen_warning());
    CHECK_EQ(settings_get_int(SET_ENC_CENTRE), 5);
    CHECK_EQ(ui_router_current(), SCREEN_SETUP);
}

TEST_CASE(a_second_finger_and_a_lost_press_select_nothing)
{
    pane();
    finger(0, XL, LIST_Y + 100);
    finger(1, entry_x(4), entry_y(4));
    lift(1);
    CHECK_EQ(sensors_pane_selected(), SENS_ENTRY_BUS);
    lift(0);

    /* The list's press holds the screen: another finger's row key waits. */
    finger(0, entry_x(4), entry_y(4));
    finger(1, XP, LIST_Y + ROW_H / 2);
    lift(1);
    CHECK_EQ(changed(), 0);
    lift(0);
    CHECK_EQ(sensors_pane_selected(), 4);

    finger(0, entry_x(5), entry_y(5));
    ui_router_cancel_gestures();
    lift(0);
    CHECK_EQ(sensors_pane_selected(), 4);
    /* And the next touch is taken. */
    feed_tap(1, entry_x(5), entry_y(5));
    CHECK_EQ(sensors_pane_selected(), 5);

    /* Left under the finger and entered again: nothing. */
    finger(0, entry_x(1), entry_y(1));
    ui_router_goto(SCREEN_OVERVIEW);
    ui_router_goto(SCREEN_SETUP);
    lift(0);
    CHECK_EQ(sensors_pane_selected(), 5);
}

/* An edit survives a change of entry: unsaved, in SAVE and in the warning. */
TEST_CASE(a_change_of_entry_keeps_what_is_unsaved)
{
    fresh_hub();
    settings_set_store(&s_mem_store);
    s_save_calls = 0;
    open_category(SET_CAT_SENSORS);
    const unsigned clean = save_face();
    tap(XP, row_y(row_of(SET_SENSE_SDA)));
    CHECK_EQ(settings_get_int(SET_SENSE_SDA), 18);
    const unsigned dirty = save_face();
    CHECK(dirty != clean);

    tap(entry_x(SENS_ENTRY_TAP), entry_y(SENS_ENTRY_TAP));
    CHECK_EQ(sensors_pane_selected(), SENS_ENTRY_TAP);
    CHECK_EQ(settings_get_int(SET_SENSE_SDA), 18);
    CHECK(settings_dirty());
    CHECK_EQ(s_save_calls, 0);
    CHECK_EQ(save_face(), dirty);

    /* The home tag climbs to the tiles, then holds. */
    tap(home_x(), home_y());
    CHECK_EQ(settings_screen_category(), -1);
    CHECK(!settings_screen_warning());
    tap(home_x(), home_y());
    CHECK(settings_screen_warning());
    char line[128];
    CHECK(settings_screen_warn_line(0, line, sizeof(line)));
    CHECK_STR_EQ(line, "Sensor SDA: GPIO 16 -> GPIO 18");

    /* DISCARD takes the value on an entry not showing, and leaves. */
    const gfx_rect_t d = gfx_rect_make(RESET_X, RESET_Y, 100, RESET_H);
    tap(d.x + 50, d.y + 20);
    CHECK_EQ(settings_get_int(SET_SENSE_SDA), 16);
    CHECK(!settings_dirty());
    CHECK(ui_router_current() != SCREEN_SETUP);
}

/* SAVE keeps an edit from any entry; a reload finds it. */
TEST_CASE(save_on_the_pane_keeps_a_moved_row)
{
    fresh_hub();
    settings_set_store(&s_mem_store);
    s_save_calls = 0;
    open_sensor(SET_TONE_F_MIN);
    settings_set(SET_TONE_F_MIN, 800.0f);
    tap(SAVE_X + 100, SAVE_Y + SAVE_H / 2);
    CHECK(settings_save_asked());
    CHECK(settings_save_tick(true));
    CHECK_EQ(s_save_calls, 1);
    settings_init();
    CHECK_EQ(settings_get_int(SET_TONE_F_MIN), 800);
    CHECK(settings_get_bool(SET_TONE_EN));
}

/* The keypad covers the left list: nothing under it presses. */
TEST_CASE(the_left_list_does_not_press_under_the_keypad)
{
    gesture_screen(SET_CAT_SENSORS);
    const gfx_rect_t v = settings_screen_row_part(2, SETTINGS_ROW_VALUE);
    tap(v.x + v.w / 2, UI_BAND_H + v.y + v.h / 2);
    CHECK(settings_screen_keypad()->open);
    CHECK_EQ(settings_screen_keypad_setting(), (int)KEY_ID);
    for (int e = 0; e < SENS_ENTRY_COUNT; ++e) {
        finger(FEED_LONE, entry_x(e), entry_y(e));
        lift(FEED_LONE);
        CHECK_EQ(sensors_pane_selected(), SENS_ENTRY_INA3221_0);
    }
    CHECK_EQ(settings_screen_keypad_setting(), (int)KEY_ID);
    tap(home_x(), home_y());
    CHECK(!settings_screen_keypad()->open);
    CHECK_EQ(settings_screen_category(), (int)SET_CAT_SENSORS);
    CHECK_EQ(changed(), 0);
    /* A press on an entry opens no keypad either, and after it a row's does. */
    tap(entry_x(SENS_ENTRY_INA3221_0), entry_y(SENS_ENTRY_INA3221_0));
    CHECK(!settings_screen_keypad()->open);
    tap(v.x + v.w / 2, UI_BAND_H + v.y + v.h / 2);
    CHECK(settings_screen_keypad()->open);
}

/* Each entry keeps its scroll, through another entry, the tiles and another
 * category; one whose list shortened comes back inside its range. */
TEST_CASE(each_entry_keeps_its_scroll)
{
    gesture_screen(SET_CAT_SENSORS);
    settings_set(SET_TONE_EN, 1.0f);
    swipe(XL, LIST_Y + 300, XL, LIST_Y + 100, 9);
    CHECK_EQ(settings_screen_scroll(NULL), 200);

    /* A list that fits starts at its top. */
    tap(entry_x(SENS_ENTRY_BUS), entry_y(SENS_ENTRY_BUS));
    CHECK_EQ(settings_screen_scroll(NULL), 0);
    /* The tap's 7 rows have 56px of scroll. */
    tap(entry_x(SENS_ENTRY_TAP), entry_y(SENS_ENTRY_TAP));
    CHECK_EQ(settings_screen_scroll(NULL), 0);
    swipe(XL, LIST_Y + 300, XL, LIST_Y + 100, 9);
    CHECK_EQ(settings_screen_scroll(NULL), 56);

    tap(entry_x(SENS_ENTRY_INA3221_0), entry_y(SENS_ENTRY_INA3221_0));
    CHECK_EQ(settings_screen_scroll(NULL), 200);
    tap(entry_x(SENS_ENTRY_TAP), entry_y(SENS_ENTRY_TAP));
    CHECK_EQ(settings_screen_scroll(NULL), 56);

    /* Up to the tiles and back; to another category and back. */
    tap(home_x(), home_y());
    CHECK_EQ(settings_screen_category(), -1);
    open_category(SET_CAT_SENSORS);
    CHECK_EQ(sensors_pane_selected(), SENS_ENTRY_TAP);
    CHECK_EQ(settings_screen_scroll(NULL), 56);
    open_category(SET_CAT_APP);
    CHECK_EQ(settings_screen_scroll(NULL), 0);
    open_category(SET_CAT_SENSORS);
    CHECK_EQ(sensors_pane_selected(), SENS_ENTRY_TAP);
    CHECK_EQ(settings_screen_scroll(NULL), 56);

    /* Off under a scrolled view: the scroll follows on the next frame, and
     * from a shortened list an entry keeps what it has. */
    settings_set(SET_TONE_EN, 0.0f);
    ui_router_render(&s_c, 0);
    CHECK_EQ(settings_screen_scroll(NULL), 0);
    tap(entry_x(SENS_ENTRY_INA3221_0), entry_y(SENS_ENTRY_INA3221_0));
    settings_set(SET_INA_FIT(0), 0.0f);
    tap(entry_x(SENS_ENTRY_BUS), entry_y(SENS_ENTRY_BUS));
    tap(entry_x(SENS_ENTRY_INA3221_0), entry_y(SENS_ENTRY_INA3221_0));
    int max = -1;
    CHECK_EQ(settings_screen_scroll(&max), 0);
    CHECK_EQ(max, 0);
}

/* Set to the screen's defaults on a reset of the screen. */
TEST_CASE(the_router_reset_selects_the_first_entry)
{
    pane();
    tap(entry_x(3), entry_y(3));
    CHECK_EQ(sensors_pane_selected(), 3);
    ui_router_init();
    CHECK_EQ(sensors_pane_selected(), SENS_ENTRY_BUS);
}

/* ------------------------------------------------------------ the carrier */

TEST_CASE(the_carrier_holds_its_soldered_rows_on_the_pane)
{
    fresh_hub();
    ui_board_set(RBMOD);
    (void)ui_board_apply_pins(NULL);
    open_category(SET_CAT_SENSORS);
    gfx_color_t ink = 0;
    CHECK_STR_EQ(state_of(SENS_ENTRY_BUS, &ink), "GPIO 16 / 17 SOLDERED");
    CHECK_EQ(ink, UI_TEXT_DIM);
    ui_text_set_language(UI_LANG_DE);
    CHECK_STR_EQ(state_of(SENS_ENTRY_BUS, &ink), "GPIO 16 / 17 VERLÖTET");
    ui_text_set_language(UI_LANG_EN);
    snap();
    feed_reset();
    /* The bus's keys: none. */
    tap(XP, row_y(row_of(SET_SENSE_SDA)));
    CHECK_EQ(changed(), 0);

    /* 0x40 is the board's: FITTED held with no keys, its rails with none,
     * its shunts and limits editable, a reset keeps what is soldered. */
    tap(entry_x(SENS_ENTRY_INA3221_0), entry_y(SENS_ENTRY_INA3221_0));
    CHECK_EQ(settings_screen_rows(SET_CAT_SENSORS), 10);
    tap(XP, LIST_Y + ROW_H / 2);
    tap(XM, LIST_Y + ROW_H / 2);
    tap(XP, LIST_Y + ROW_PITCH + ROW_H / 2);
    CHECK_EQ(changed(), 0);
    CHECK(settings_get_bool(SET_INA_FIT(0)));
    tap(XP, LIST_Y + 2 * ROW_PITCH + ROW_H / 2);
    CHECK_EQ(changed(), 1);                 /* CH1's shunt */
    tap(RESET_X + 100, RESET_Y + RESET_H / 2);
    CHECK_EQ(changed(), 0);
    CHECK(settings_get_bool(SET_INA_FIT(0)));
    CHECK_EQ(settings_screen_rows(SET_CAT_SENSORS), 10);

    /* 0x41 to 0x43 stay offered. */
    tap(entry_x(SENS_ENTRY_INA3221_0 + 1), entry_y(SENS_ENTRY_INA3221_0 + 1));
    CHECK_EQ(settings_screen_rows(SET_CAT_SENSORS), 1);
    tap(XP, LIST_Y + ROW_H / 2);
    CHECK(settings_get_bool(SET_INA_FIT(1)));

    /* The tap's pin is soldered, its other 5 rows editable. */
    settings_set(SET_TONE_EN, 1.0f);
    tap(entry_x(SENS_ENTRY_TAP), entry_y(SENS_ENTRY_TAP));
    CHECK_EQ(settings_screen_rows(SET_CAT_SENSORS), 7);
    CHECK(settings_fixed(SET_TONE_PIN));
    CHECK_STR_EQ(settings_screen_help(SET_TONE_PIN), TR(SET_PIN_SOLDERED));
    CHECK(!settings_fixed(SET_TONE_F_MIN));

    ui_board_set(MODULE);
    (void)ui_board_apply_pins(NULL);
    CHECK_STR_EQ(state_of(SENS_ENTRY_BUS, &ink), "GPIO 16 / 17");
}

int main(void)
{
    RUN(the_list_names_8_entries_in_order);
    RUN(every_sensor_row_has_one_entry_and_balance_has_none);
    RUN(a_moved_row_keeps_its_key);
    RUN(rows_appear_when_a_sensor_is_enabled);
    RUN(the_pane_stands_beside_the_list_and_the_keys_stay);
    RUN(a_setup_tile_line_names_what_sensors_holds);
    RUN(every_entry_says_its_state);
    RUN(the_ina228_state_repaints_the_list);
    RUN(reset_takes_the_selected_entry_alone_hidden_rows_too);
    RUN(the_reset_label_names_the_entry_and_fits);
    RUN(a_reset_with_nothing_to_change_is_no_edit);
    RUN(a_press_arms_an_entry_and_its_release_selects_it);
    RUN(sliding_off_an_entry_selects_nothing);
    RUN(a_drag_on_the_list_scrolls_nothing);
    RUN(a_jump_onto_an_entry_selects_nothing);
    RUN(a_flick_into_the_strip_resets_nothing);
    RUN(a_flick_onto_the_warning_answers_nothing);
    RUN(a_second_finger_and_a_lost_press_select_nothing);
    RUN(a_change_of_entry_keeps_what_is_unsaved);
    RUN(save_on_the_pane_keeps_a_moved_row);
    RUN(the_left_list_does_not_press_under_the_keypad);
    RUN(each_entry_keeps_its_scroll);
    RUN(the_router_reset_selects_the_first_entry);
    RUN(the_carrier_holds_its_soldered_rows_on_the_pane);
    return test_summary("sensors_pane");
}
