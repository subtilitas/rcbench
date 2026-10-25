/**
 * @file test_servo_rail.c
 * @brief The servo screen's tag row, rail line and current row under the supply
 * and the INA3221 as the meter.
 *
 * Cases:
 * - the_tag_row_carries_the_pulse_range
 * - the_rail_line_names_the_pd_mini_and_a_modelled_supply
 * - the_current_row_shows_the_supply_while_it_is_the_meter
 * - the_current_row_shows_ch1s_window_while_the_ina3221_is_the_meter
 * - a_negative_current_is_shown_signed
 * - a_clipped_window_draws_its_current_in_the_warning_colour
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_screen_rig.h"

#define TGT_ROW_Y 308

/*
 * The TAG row carries the pulse range after the type and the frame rate:
 * "STANDARD PWM  50Hz  1000-2000", two spaces between the three parts, the
 * range in microseconds without its unit.  The longest a profile and its
 * keypads allow, a 13-character type, a 3-digit rate and two 4-digit ends,
 * is 32 of the 35 cells the 356px pill holds.  A typed end reaches the row
 * at once.
 */
#define TAG_BOX_Y 20

#define TAG_BOX_H 44

static bool tag_reads(const char *text, bool danger)
{
    scr->render(&cv, 0);
    want_fresh();
    const gfx_rect_t r = { TGT_X, TAG_BOX_Y, TGT_W, TAG_BOX_H };
    gfx_fill_rect(&want_cv, r.x, r.y, r.w, r.h, ui_theme_color(UI_C_PANEL));
    gfx_fill_round_rect(&want_cv, r.x, r.y, r.w, r.h, UI_R_CTL,
                        danger ? ui_theme_color(UI_C_DANGER)
                               : ui_theme_color(UI_C_PANEL_SUNK));
    gfx_text_in(&want_cv, r, text, &gfx_font_10x20,
                danger ? GFX_WHITE : ui_theme_color(UI_C_TEXT), 1,
                GFX_ALIGN_CENTER);
    for (int y = r.y; y < r.y + r.h; ++y) {
        if (memcmp(fb + (size_t)y * W + r.x, want_fb + (size_t)y * W + r.x,
                   (size_t)r.w * sizeof(gfx_color_t)) != 0) {
            return false;
        }
    }
    return true;
}

TEST_CASE(the_tag_row_carries_the_pulse_range)
{
    fresh();
    CHECK(tag_reads("STANDARD PWM  50Hz  1000-2000", false));
    choose_type(1);
    CHECK(tag_reads("NARROW 760  50Hz  660-860", false));
    choose_type(4);
    CHECK(tag_reads("HELI TAIL 760  560Hz  410-1110", true));
    /* PULSE MAX typed: the row follows without another tap. */
    choose_type(0);
    open_settings();
    tap(ROW_L_X, ROW_Y(3));                    /* PULSE MAX */
    keys("2100");
    close_settings();
    CHECK(tag_reads("STANDARD PWM  50Hz  1000-2100", false));
    /* The widest the row can be asked to hold fits. */
    CHECK(gfx_text_width(&gfx_font_10x20, "HELI TAIL 760  560Hz  2500-2500",
                         1) <= TGT_W - 8);
    /* The separate RANGE row is gone: its label is not on the card. */
    want_fresh();
    gfx_fill_rect(&want_cv, TGT_X, TGT_ROW_Y, 80, 24,
                  ui_theme_color(UI_C_PANEL));
    gfx_text(&want_cv, TGT_X, TGT_ROW_Y + 5, "RANGE", UI_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_DIM), 1);
    scr->render(&cv, 0);
    CHECK(!same_in(fb, want_fb, TGT_X, TGT_ROW_Y, 80, 24));
}

/*
 * The supply the rail's line names is the PD mini, or the panel's model of
 * it: then the SIMULATED (SIMULIERT) pill sits on the plot's top-left corner,
 * under the name, 110px or as wide as a longer word and 26px high.  A change of the model reaches it without a tap; with
 * INA3221 CH1 as the meter it is not shown.
 */
#define PILL_X (RAIL_X + 2)

#define PILL_Y (96 + 2)

static int pill_w(const char *word)
{
    const int w = 30 + gfx_text_width(&gfx_font_10x20, word, 1);
    return w < 110 ? 110 : w;
}

/* Whether buffer 0, drawn, carries the pill reading @p word: the area around
 * it, one pixel wider on every side, compared with the plot as drawn
 * without the model and the overview's pill drawn on it.  The fill is the
 * plot's own colour, so the pill's width and height show where it covers a
 * grid line or the plot's edge. */
static bool pill_reads(const char *word)
{
    scr->render(&cv, 0);
    const bool model = supply_screen_model();
    static gfx_color_t seen[(size_t)W * H];
    memcpy(seen, fb, sizeof(seen));
    supply_screen_set_model(false);
    scr->tick(1.0f / 39.0f);
    scr->render(&cv, 0);
    want_fresh();
    memcpy(want_fb, fb, (size_t)W * H * sizeof(gfx_color_t));
    memcpy(fb, seen, sizeof(seen));
    supply_screen_set_model(model);
    scr->tick(1.0f / 39.0f);
    const int w = pill_w(word);
    ui_pill_font(&want_cv, (gfx_rect_t){ PILL_X, PILL_Y, (int16_t)w, 26 },
                 word, ui_theme_color(UI_C_WARN),
                 ui_theme_color(UI_C_PANEL_SUNK), 30, &gfx_font_10x20);
    return same_in(fb, want_fb, PILL_X - 1, PILL_Y - 1, w + 2, 28);
}

TEST_CASE(the_rail_line_names_the_pd_mini_and_a_modelled_supply)
{
    fresh();
    meter(SERVO_SOURCE_PDMINI);
    rail_supply(6.0f, 0.4f);
    CHECK(line_supply("6.00V", "0.40A", "2.4W"));
    CHECK(!pill_reads("SIMULATED"));
    /* The model switched on: the pill, the name kept, no tap needed. */
    supply_screen_set_model(true);
    scr->tick(1.0f / 39.0f);
    CHECK(pill_reads("SIMULATED"));
    CHECK(line_supply("6.00V", "0.40A", "2.4W"));
    /* A new sample repaints the plot, and the pill with it. */
    rail_supply(6.0f, 0.5f);
    CHECK(pill_reads("SIMULATED"));
    /* No IOmcu: the supply's sample, still under PD MINI. */
    meter(SERVO_SOURCE_MODEL);
    scr->tick(1.0f / 39.0f);
    CHECK(pill_reads("SIMULATED"));
    CHECK(line_supply("6.00V", "0.50A", "3.0W"));
    /* In German the word is SIMULIERT; the name stays PD MINI. */
    ui_text_set_language(UI_LANG_DE);
    servo_invalidate();
    CHECK(pill_reads("SIMULIERT"));
    CHECK(line_supply("6.00V", "0.50A", "3.0W"));
    ui_text_set_language(UI_LANG_EN);
    servo_invalidate();
    /* INA3221 CH1 as the meter: its own name and no pill. */
    meter(SERVO_SOURCE_INA3221);
    scr->tick(1.0f / 39.0f);
    CHECK(!pill_reads("SIMULATED"));
    meter(SERVO_SOURCE_PDMINI);
    scr->tick(1.0f / 39.0f);
    CHECK(pill_reads("SIMULATED"));
    /* The model switched off: the pill goes, without a tap. */
    supply_screen_set_model(false);
    scr->tick(1.0f / 39.0f);
    CHECK(!pill_reads("SIMULATED"));
    CHECK(line_supply("6.00V", "0.50A", "3.0W"));
}

/* The supply is the meter until the panel names another, and as the PD
 * mini: the row, the line and the plot are its samples.  The feedback's
 * current is not shown. */
TEST_CASE(the_current_row_shows_the_supply_while_it_is_the_meter)
{
    for (int pass = 0; pass < 3; ++pass) {
        fresh();
        if (pass == 1) {
            meter(SERVO_SOURCE_PDMINI);
        } else if (pass == 2) {
            meter(SERVO_SOURCE_MODEL);
        }
        /* No sample: the dashes. */
        CHECK(row_reads("---", UI_C_TEXT));
        CHECK(line_supply("-- V", "-- A", "-- W"));
        CHECK_EQ(servo_screen_power_points(), 0);
        /* A feedback with a current puts nothing in the row. */
        servo_screen_feedback(1500u, 1.23f, true);
        CHECK(row_reads("---", UI_C_TEXT));

        rail_supply(6.0f, 0.4f);
        CHECK(row_reads("0.40A", UI_C_TEXT));
        CHECK(line_supply("6.00V", "0.40A", "2.4W"));
        CHECK_EQ(servo_screen_power_points(), 1);
        CHECK_NEAR(servo_screen_power_sample(1, 0), 0.4f, 1e-6f);
        servo_screen_feedback(1500u, 1.23f, true);
        CHECK(row_reads("0.40A", UI_C_TEXT));
        servo_screen_feedback(0u, 0.0f, false);
        CHECK(row_reads("0.40A", UI_C_TEXT));

        /* A window of the INA3221 is not this meter's reading. */
        rail_window(1200, 5000u);
        CHECK(row_reads("0.40A", UI_C_TEXT));
        CHECK(line_supply("6.00V", "0.40A", "2.4W"));
        CHECK_EQ(servo_screen_power_points(), 1);

        /* A sample without a current reading, and a supply that does not
         * answer: the dashes, and a gap on the plot. */
        supply_state_t st;
        memset(&st, 0, sizeof(st));
        st.online = true;
        st.ok = SUPPLY_OK_VOLTAGE;
        st.v = 6.0f;
        servo_screen_supply(&st);
        CHECK(row_reads("---", UI_C_TEXT));
        CHECK(line_supply("6.00V", "-- A", "-- W"));
        CHECK_EQ(servo_screen_power_points(), 2);
        CHECK(isnan(servo_screen_power_sample(1, 0)));
        rail_supply(6.0f, 0.4f);
        CHECK(row_reads("0.40A", UI_C_TEXT));
        st.online = false;
        st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
        servo_screen_supply(&st);
        CHECK(row_reads("---", UI_C_TEXT));
        CHECK(line_supply("-- V", "-- A", "-- W"));
        servo_screen_supply(NULL);
        CHECK(row_reads("---", UI_C_TEXT));

        /* A reading that is not a number is no reading, and one beyond any
         * supply is drawn at the 10^8 hundredths the digits are held to. */
        st.online = true;
        st.v = 6.0f;
        st.i = NAN;
        st.p = NAN;
        servo_screen_supply(&st);
        CHECK(row_reads("---", UI_C_TEXT));
        CHECK(line_supply("6.00V", "-- A", "-- W"));
        st.i = INFINITY;
        servo_screen_supply(&st);
        CHECK(row_reads("---", UI_C_TEXT));
        st.v = NAN;
        st.i = 0.4f;
        servo_screen_supply(&st);
        CHECK(row_reads("0.40A", UI_C_TEXT));
        CHECK(line_supply("-- V", "0.40A", "-- W"));
        st.v = 6.0f;
        st.i = 3.0e9f;
        st.p = -3.0e12f;
        servo_screen_supply(&st);
        CHECK(row_reads("1000000.00A", UI_C_TEXT));
        st.i = -3.0e9f;
        servo_screen_supply(&st);
        CHECK(row_reads("-1000000.00A", UI_C_TEXT));
    }
}

/* With the INA3221 as the meter the row, the line and the plot are CH1's
 * windows: mean current, mean bus voltage, their product, under its name. */
TEST_CASE(the_current_row_shows_ch1s_window_while_the_ina3221_is_the_meter)
{
    fresh();
    rail_supply(6.0f, 0.4f);
    meter(SERVO_SOURCE_INA3221);
    /* No window yet: the dashes, whatever the supply reads. */
    CHECK(row_reads("---", UI_C_TEXT));
    CHECK(line_ina("-- V", "-- A", "-- W"));
    CHECK_EQ(servo_screen_power_points(), 0);

    rail_window(412, 5980u);
    CHECK(row_reads("0.41A", UI_C_TEXT));
    CHECK(line_ina("5.98V", "0.41A", "2.5W"));        /* 2.46W */
    CHECK_EQ(servo_screen_power_points(), 1);
    CHECK_NEAR(servo_screen_power_sample(0, 0), 5.98f, 1e-5f);
    CHECK_NEAR(servo_screen_power_sample(1, 0), 0.412f, 1e-6f);
    CHECK_NEAR(servo_screen_power_sample(2, 0), 5.98f * 0.412f, 1e-5f);

    /* The supply's samples and the feedback change none of it. */
    rail_supply(6.0f, 0.9f);
    servo_screen_feedback(1500u, 1.23f, true);
    CHECK(row_reads("0.41A", UI_C_TEXT));
    CHECK(line_ina("5.98V", "0.41A", "2.5W"));
    CHECK_EQ(servo_screen_power_points(), 1);

    /* The range of the registers: every reading whole on the line, with
     * its sign, the voltage in hundredths and the power in tenths, or in
     * whole watts where tenths do not fit. */
    rail_window(32767, 26000u);
    CHECK(row_reads("32.77A", UI_C_TEXT));
    CHECK(line_ina("26.00V", "32.77A", "851.9W"));
    rail_window(-32768, 26000u);
    CHECK(row_reads("-32.77A", UI_C_TEXT));
    CHECK(line_ina("26.00V", "-32.77A", "-852.0W"));
    rail_window(-32768, 65535u);
    CHECK(line_ina("65.54V", "-32.77A", "-2147W"));
    rail_window(32767, 65535u);
    CHECK(line_ina("65.54V", "32.77A", "2147.4W"));
    /* Where the tenths stop fitting: 7 cells. */
    rail_window(1000, 9994u);
    CHECK(line_ina("9.99V", "1.00A", "10.0W"));
    rail_window(-9990, 10000u);
    CHECK(line_ina("10.00V", "-9.99A", "-99.9W"));
    rail_window(-10000, 10000u);
    CHECK(line_ina("10.00V", "-10.00A", "-100.0W"));
    rail_window(9999, 10000u);
    CHECK(line_ina("10.00V", "10.00A", "100.0W"));
    rail_window(10000, 10005u);
    CHECK(line_ina("10.01V", "10.00A", "100.1W"));
    rail_window(-10005, 10000u);                  /* -100.05W */
    CHECK(line_ina("10.00V", "-10.01A", "-100.1W"));
    rail_window(1637, 5000u);
    CHECK(row_reads("1.64A", UI_C_TEXT));
    rail_window(0, 0u);
    CHECK(row_reads("0.00A", UI_C_TEXT));
    CHECK(line_ina("0.00V", "0.00A", "0.0W"));

    /* A window without samples of one quantity: that one is dashes and a
     * gap, and the power with it. */
    sense_link_win_t w = rail_win(300, 5000u);
    w.current = false;
    window(&w);
    CHECK(row_reads("---", UI_C_TEXT));
    CHECK(line_ina("5.00V", "-- A", "-- W"));
    CHECK(isnan(servo_screen_power_sample(1, 0)));
    CHECK(isnan(servo_screen_power_sample(2, 0)));
    CHECK_NEAR(servo_screen_power_sample(0, 0), 5.0f, 1e-6f);
    w = rail_win(300, 5000u);
    w.voltage = false;
    window(&w);
    CHECK(row_reads("0.30A", UI_C_TEXT));
    CHECK(line_ina("-- V", "0.30A", "-- W"));
    CHECK(isnan(servo_screen_power_sample(0, 0)));
    servo_screen_window(NULL, meter_id, meter_changes);
    CHECK(row_reads("0.30A", UI_C_TEXT));
}

/* A negative current is a reading: signed, in hundredths, rounded half away
 * from zero, and never drawn as zero while its digits are not. */
TEST_CASE(a_negative_current_is_shown_signed)
{
    static const struct { int ma; const char *text; } k[] = {
        { -20, "-0.02A" }, { -1640, "-1.64A" }, { -4, "0.00A" },
        { -5, "-0.01A" }, { -6, "-0.01A" }, { 4, "0.00A" },
        { 5, "0.01A" }, { -14, "-0.01A" }, { -15, "-0.02A" },
        { -32768, "-32.77A" }, { -1, "0.00A" }, { 1, "0.00A" },
    };
    fresh();
    meter(SERVO_SOURCE_INA3221);
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        rail_window(k[i].ma, 5000u);
        CHECK(row_reads(k[i].text, UI_C_TEXT));
        /* The plot's point is the reading, not a zero. */
        CHECK_NEAR(servo_screen_power_sample(1, 0), (float)k[i].ma / 1000.0f,
                   1e-6f);
    }
    rail_window(-20, 5000u);
    CHECK(line_ina("5.00V", "-0.02A", "-0.1W"));
    rail_window(-1640, 5000u);
    CHECK(line_ina("5.00V", "-1.64A", "-8.2W"));
    rail_window(-4, 5000u);
    CHECK(line_ina("5.00V", "0.00A", "0.0W"));       /* -0.02W */

    /* The supply's reading keeps its sign as well. */
    meter(SERVO_SOURCE_PDMINI);
    rail_supply(5.0f, -0.02f);
    CHECK(row_reads("-0.02A", UI_C_TEXT));
    CHECK(line_supply("5.00V", "-0.02A", "-0.1W"));
    rail_supply(5.0f, -1.64f);
    CHECK(row_reads("-1.64A", UI_C_TEXT));
    rail_supply(5.0f, -0.004f);
    CHECK(row_reads("0.00A", UI_C_TEXT));
}

/* A window with a sample at an end of the range shows the value it holds,
 * in the warning colour, in the row and on the line. */
TEST_CASE(a_clipped_window_draws_its_current_in_the_warning_colour)
{
    fresh();
    meter(SERVO_SOURCE_INA3221);
    rail_window(1590, 5900u);
    CHECK(row_reads("1.59A", UI_C_TEXT));

    sense_link_win_t w = rail_win(1590, 5900u);
    w.clip_hi = true;
    w.clipped = 7u;
    w.max_ma  = 1638;
    window(&w);
    /* The same digits: the colour alone repaints the row. */
    CHECK(row_reads("1.59A", UI_C_WARN));
    CHECK(line_reads(ina_label(), LAB_INA, "5.90V", "1.59A", UI_C_WARN,
                     "9.4W"));
    CHECK_NEAR(servo_screen_power_sample(1, 0), 1.59f, 1e-6f);

    rail_window(1590, 5900u);
    CHECK(row_reads("1.59A", UI_C_TEXT));
    CHECK(line_ina("5.90V", "1.59A", "9.4W"));

    /* The bottom of the range, with a negative mean. */
    w = rail_win(-1600, 5900u);
    w.clip_lo = true;
    w.clipped = 255u;
    window(&w);
    CHECK(row_reads("-1.60A", UI_C_WARN));
    CHECK(line_reads(ina_label(), LAB_INA, "5.90V", "-1.60A", UI_C_WARN,
                     "-9.4W"));

    /* The count of clipped samples alone marks nothing, and a clipped
     * window without current samples has no current to colour. */
    w = rail_win(700, 5900u);
    w.clipped = 3u;
    window(&w);
    CHECK(row_reads("0.70A", UI_C_TEXT));
    w = rail_win(700, 5900u);
    w.clip_hi = true;
    w.current = false;
    window(&w);
    CHECK(row_reads("---", UI_C_TEXT));
    CHECK(line_ina("5.90V", "-- A", "-- W"));

    /* The supply is never drawn clipped: the mark is the window's. */
    w = rail_win(1590, 5900u);
    w.clip_hi = true;
    window(&w);
    CHECK(row_reads("1.59A", UI_C_WARN));
    meter(SERVO_SOURCE_PDMINI);
    rail_supply(6.0f, 1.59f);
    CHECK(row_reads("1.59A", UI_C_TEXT));
    CHECK(line_supply("6.00V", "1.59A", "9.5W"));
}

int main(void)
{
    RUN(the_tag_row_carries_the_pulse_range);
    RUN(the_rail_line_names_the_pd_mini_and_a_modelled_supply);
    RUN(the_current_row_shows_the_supply_while_it_is_the_meter);
    RUN(the_current_row_shows_ch1s_window_while_the_ina3221_is_the_meter);
    RUN(a_negative_current_is_shown_signed);
    RUN(a_clipped_window_draws_its_current_in_the_warning_colour);
    return test_summary("servo_rail");
}
