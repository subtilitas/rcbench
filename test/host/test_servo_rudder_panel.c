/**
 * @file test_servo_rudder_panel.c
 * @brief SERVO's RUDDER task: the fifth tab, the readout in ° and mm,
 *        LENGTH, the buttons that drive 1000us, CENTRE and 2000us only
 *        while ARMED, ZERO and RESET MAX, the angle source's state line,
 *        the knob, leaving to SAFE, the TARGET row the buttons drive, the
 *        guide's page and the strings in their fields.
 *
 * The AS5600 is on with its centre at count 2048; 256 counts are 22.5°.
 *
 * Cases:
 * - the_strip_has_five_tabs_and_rudder_opens
 * - the_readout_shows_degrees_and_mm_with_a_length_and_degrees_alone_without
 * - reverse_flips_the_readout_and_keeps_the_zero
 * - the_buttons_drive_1000_1500_2000us_only_while_armed
 * - zero_takes_the_current_angle_and_reset_clears_the_max
 * - a_tap_that_slides_off_or_loses_its_release_does_nothing
 * - length_takes_0_or_10_to_500mm_and_refuses_the_gap
 * - the_state_line_names_no_source_stale_and_connected
 * - leaving_servo_from_rudder_disarms
 * - the_target_row_names_the_pin_the_buttons_drive
 * - the_knob_moves_the_horn_on_rudder_while_armed
 * - a_reading_repaints_the_readout_alone_and_as_a_whole_frame_would
 * - the_help_page_leaves_the_target_row_in_view
 * - every_string_fits
 *
 * SPDX-License-Identifier: MIT
 */
#include "servo_screen_rig.h"

#include "servo_rudder_int.h"
#include "ui_help.h"
#include "ui_workbench.h"

/** @brief The AS5600's centre count in these cases. */
#define CENTRE_COUNT 2048u
/** @brief The readings' numerals' height: the 32 x 44 face's, px. */
#define NUM_H 44
/** @brief The degrees' column's left edge, px. */
#define DEG_X 18
/** @brief The TARGET row's top on the output card, y 308 to 356. */
#define TARGET_ROW_Y 308
/** @brief Tab @p t's last column: tabs are 120px from x 9, 124px apart. */
#define TAB_LAST_X(t) (9 + 124 * (t) + 119)

/** @brief The AS5600 on, its centre at CENTRE_COUNT. */
static void encoder_on(void)
{
    settings_set(SET_ENC_EN, 1.0f);
    settings_set(SET_ENC_CENTRE, (float)CENTRE_COUNT);
}

/**
 * @brief A reading at @p counts from the centre, as the panel hands it.
 * @param counts the counts, signed
 */
static void reading(int counts)
{
    static uint32_t at_ms = 1000u;
    at_ms += 40u;
    const servo_test_enc_t e = {
        .valid = true,
        .raw = (uint16_t)(((int)CENTRE_COUNT + counts) & 4095),
        .taken_ms = at_ms };
    servo_screen_encoder(&e);
}

/** @brief A tap on RUDDER's tab. */
static void to_rudder(void) { tap(SV_TAB_RUDDER_X, SV_TAB_Y); }

/**
 * @brief A tap at the middle of control @p k.
 * @param k the control
 */
static void press(rudder_ctl_t k)
{
    const gfx_rect_t r = rudder_rect(k);
    tap(r.x + r.w / 2, r.y + r.h / 2);
}

/** @brief The command waiting, drained. @return its kind and pulse */
static servo_cmd_t taken(void)
{
    servo_cmd_t c = { .kind = SERVO_CMD_NONE };
    servo_cmd_t next;
    while (servo_screen_take(&next)) {
        c = next;
    }
    return c;
}

/**
 * @brief Whether the box (@p x, @p y, @p w, @p h) of the body holds the
 *        same pixels in fb as in the reference canvas.
 */
static bool box_same(int x, int y, int w, int h)
{
    for (int r = y; r < y + h; ++r) {
        if (memcmp(fb + (size_t)r * W + x, ref + (size_t)r * W + x,
                   (size_t)w * sizeof(gfx_color_t)) != 0) {
            return false;
        }
    }
    return true;
}

/* ------------------------------------------------------------- the tab */

/* Five tabs, RUDDER the fourth: each tap selects its own; RUDDER shows its
 * card and takes presses; a tile's entry opens LIVE again. */
TEST_CASE(the_strip_has_five_tabs_and_rudder_opens)
{
    fresh();
    scr->enter();
    static const struct { int x; ui_wb_task_t task; } k[] = {
        { SV_TAB_RUDDER_X, UI_WB_RUDDER }, { SV_TAB_LIVE_X, UI_WB_LIVE },
        { SV_TAB_TEST_X, UI_WB_TEST }, { SV_TAB_PROGRAM_X, UI_WB_PROGRAM },
        { SV_TAB_SETTINGS_X, UI_WB_SETTINGS }, { SV_TAB_RUDDER_X, UI_WB_RUDDER },
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        tap(k[i].x, SV_TAB_Y);
        CHECK_EQ(servo_screen_task(), (int)k[i].task);
        CHECK_EQ(rudder_shown(), k[i].task == UI_WB_RUDDER);
    }
    /* The tabs' edges: x 9 + 124k to 128 + 124k. */
    for (int t = 0; t < (int)UI_WB_TASKS; ++t) {
        tap(9 + 124 * t + 1, SV_TAB_Y);
        CHECK_EQ(servo_screen_task(), t);
        tap(TAB_LAST_X(t), SV_TAB_Y);
        CHECK_EQ(servo_screen_task(), t);
    }
    to_rudder();
    scr->render(&cv, 0);
    scr->leave();
    scr->enter();
    CHECK_EQ(servo_screen_task(), (int)UI_WB_LIVE);
    CHECK(!rudder_shown());
}

/* ---------------------------------------------------------- the readout */

/**
 * @brief The reference canvas: the readout's numerals @p t at column @p x,
 *        in the accent, over the card.
 */
static void numerals_ref(int x, const char *t, gfx_color_t col)
{
    gfx_fill_rect(&cref, x, SV_RUD_NUM_Y, 250, NUM_H,
                  ui_theme_color(UI_C_PANEL));
    gfx_text(&cref, x, SV_RUD_NUM_Y, t, &gfx_font_num_32x44, col, 1);
}

/* Degrees from ZERO in the 32 x 44 numerals; with LENGTH the mm of
 * L x sin; without it the line that asks for it in the mm column; the
 * dashes with no reading. */
TEST_CASE(the_readout_shows_degrees_and_mm_with_a_length_and_degrees_alone_without)
{
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        fresh();
        ui_text_set_language((ui_lang_t)lang);
        encoder_on();
        settings_set(SET_RUDDER_LEN_MM, 100.0f);
        two_buffers();
        to_rudder();
        reading(0);
        press(RUD_C_ZERO);
        reading(256);                       /* +22.5deg */
        float d = 0.0f;
        CHECK(rudder_defl(&d));
        CHECK_NEAR(d, 22.5f, 1e-3f);
        scr->render(&cv, 0);
        const gfx_color_t acc = ui_theme_color(UI_C_ACCENT);
        const int wd = gfx_text_width(&gfx_font_num_32x44, "+22.5", 1);
        numerals_ref(DEG_X, "+22.5", acc);
        CHECK(box_same(DEG_X, SV_RUD_NUM_Y, wd, NUM_H));
        /* 100mm x sin(22.5deg) = 38.27mm. */
        const int wm = gfx_text_width(&gfx_font_num_32x44, "+38.3", 1);
        numerals_ref(SV_RUD_MM_X, "+38.3", acc);
        CHECK(box_same(SV_RUD_MM_X, SV_RUD_NUM_Y, wm, NUM_H));

        /* LENGTH not set: degrees alone; the mm column asks for LENGTH. */
        settings_set(SET_RUDDER_LEN_MM, 0.0f);
        scr->render(&cv, 0);
        CHECK(box_same(DEG_X, SV_RUD_NUM_Y, wd, NUM_H));
        gfx_fill_rect(&cref, SV_RUD_MM_X, SV_RUD_NUM_Y, 250, NUM_H,
                      ui_theme_color(UI_C_PANEL));
        gfx_text(&cref, SV_RUD_MM_X, SV_RUD_NUM_Y + 12, TR(RUD_NO_LENGTH),
                 &gfx_font_10x20, ui_theme_color(UI_C_TEXT_DIM), 1);
        CHECK(box_same(SV_RUD_MM_X, SV_RUD_NUM_Y, 250, NUM_H));

        /* No reading: the dashes. */
        settings_set(SET_ENC_EN, 0.0f);
        CHECK(!rudder_defl(&d));
        scr->render(&cv, 0);
        const int wn = gfx_text_width(&gfx_font_num_32x44, "---", 1);
        numerals_ref(DEG_X, "---", ui_theme_color(UI_C_TEXT_DIM));
        CHECK(box_same(DEG_X, SV_RUD_NUM_Y, wn, NUM_H));
    }
    ui_text_set_language(UI_LANG_EN);
    CHECK(!rudder_defl(NULL));
}

/* REVERSE flips the deflection shown, as it flips LIVE's MEASURED; the
 * zero stays the surface's neutral and MAX swaps sides with it. */
TEST_CASE(reverse_flips_the_readout_and_keeps_the_zero)
{
    fresh();
    encoder_on();
    to_rudder();
    reading(100);
    press(RUD_C_ZERO);
    reading(100 + 256);
    float d = 0.0f;
    CHECK(rudder_defl(&d));
    CHECK_NEAR(d, 22.5f, 1e-3f);
    CHECK(rudder_geo()->have[1] && !rudder_geo()->have[0]);
    open_settings();
    tap(TAB_X(PAGE_OUTPUT), TAB_Y);
    tap(ROW_R_X, ROW_Y(4));                     /* REVERSE */
    to_rudder();
    CHECK(rudder_defl(&d));
    CHECK_NEAR(d, -22.5f, 1e-3f);
    /* Kept in the source's sign: the side MAX holds is still side 1. */
    CHECK(rudder_geo()->have[1] && !rudder_geo()->have[0]);
    reading(100);
    CHECK(rudder_defl(&d));
    CHECK_NEAR(d, 0.0f, 1e-3f);
    scr->render(&cv, 0);
}

/* --------------------------------------------------------- the buttons */

/* SAFE: the three buttons post nothing.  ARMED: 1000us, CENTRE at PULSE
 * CENTRE plus TRIM and 2000us, on the press; a disarm makes them refuse
 * again. */
TEST_CASE(the_buttons_drive_1000_1500_2000us_only_while_armed)
{
    fresh();
    to_rudder();
    taken();
    static const rudder_ctl_t k[3] = { RUD_C_1000, RUD_C_CENTRE, RUD_C_2000 };
    for (int i = 0; i < 3; ++i) {
        press(k[i]);
        CHECK_EQ(taken().kind, SERVO_CMD_NONE);
    }
    fresh_armed();
    to_rudder();
    static const struct { servo_cmd_kind_t kind; uint16_t us; } want[3] = {
        { SERVO_CMD_POSITION, 1000u }, { SERVO_CMD_CENTRE, 1500u },
        { SERVO_CMD_POSITION, 2000u },
    };
    for (int i = 0; i < 3; ++i) {
        const gfx_rect_t r = rudder_rect(k[i]);
        ev(r.x + 5, r.y + 5, TOUCH_EVENT_DOWN, 1);
        const servo_cmd_t c = taken();
        CHECK_EQ(c.kind, want[i].kind);
        CHECK_EQ(c.value_us, want[i].us);
        CHECK_EQ(servo_screen_commanded(), want[i].us);
        ev(r.x + 5, r.y + 5, TOUCH_EVENT_UP, 1);
        CHECK_EQ(taken().kind, SERVO_CMD_NONE);
    }
    char line[64];
    CHECK_STR_EQ(rudder_line(1, line, sizeof(line)), "PULSE 2000us");
    servo_screen_set_armed(false);
    taken();
    for (int i = 0; i < 3; ++i) {
        press(k[i]);
        CHECK_EQ(taken().kind, SERVO_CMD_NONE);
    }
    CHECK_EQ(servo_screen_commanded(), 2000u);
}

/* ZERO takes the angle now as the neutral and clears MAX; MAX then holds
 * the largest each side, on another task too; RESET MAX clears it and
 * keeps the zero.  ZERO without a reading takes nothing. */
TEST_CASE(zero_takes_the_current_angle_and_reset_clears_the_max)
{
    fresh();
    encoder_on();
    to_rudder();
    reading(-40);
    CHECK(rudder_geo()->have[0]);               /* MAX before a zero */
    reading(100);
    press(RUD_C_ZERO);
    CHECK(rudder_geo()->zeroed);
    CHECK_NEAR(rudder_geo()->zero_deg, 100.0f * 360.0f / 4096.0f, 1e-3f);
    CHECK(!rudder_geo()->have[0] && !rudder_geo()->have[1]);
    float d = 1.0f;
    CHECK(rudder_defl(&d));
    CHECK_NEAR(d, 0.0f, 1e-4f);
    reading(156);                               /* +4.92deg */
    tap(SV_TAB_LIVE_X, SV_TAB_Y);
    reading(-100);                              /* -17.58deg, on LIVE */
    to_rudder();
    CHECK(rudder_geo()->have[0] && rudder_geo()->have[1]);
    CHECK_NEAR(rudder_geo()->max_deg[1], 56.0f * 360.0f / 4096.0f, 1e-3f);
    CHECK_NEAR(rudder_geo()->max_deg[0], 200.0f * 360.0f / 4096.0f, 1e-3f);
    scr->render(&cv, 0);
    press(RUD_C_RESET);
    CHECK(!rudder_geo()->have[0] && !rudder_geo()->have[1]);
    CHECK(rudder_geo()->zeroed);
    /* No reading: ZERO takes nothing, and nothing is tracked. */
    settings_set(SET_ENC_EN, 0.0f);
    reading(400);
    press(RUD_C_ZERO);
    CHECK_NEAR(rudder_geo()->zero_deg, 100.0f * 360.0f / 4096.0f, 1e-3f);
    CHECK(!rudder_geo()->have[0] && !rudder_geo()->have[1]);
    scr->render(&cv, 0);
}

/* A tap is a release over the control it began on: one that slides off,
 * one released by the router (no tap), one whose release went missing and
 * a second finger take nothing. */
TEST_CASE(a_tap_that_slides_off_or_loses_its_release_does_nothing)
{
    fresh();
    encoder_on();
    to_rudder();
    reading(100);
    const gfx_rect_t z = rudder_rect(RUD_C_ZERO);
    ev(z.x + 10, z.y + 10, TOUCH_EVENT_DOWN, 1);
    ev(z.x + 10, z.y + 200, TOUCH_EVENT_MOVE, 1);
    ev(z.x + 10, z.y + 200, TOUCH_EVENT_UP, 1);
    CHECK(!rudder_geo()->zeroed);

    ev(z.x + 10, z.y + 10, TOUCH_EVENT_DOWN, 1);
    scr->cancel();
    ev(z.x + 10, z.y + 10, TOUCH_EVENT_UP, 1);
    CHECK(!rudder_geo()->zeroed);

    const touch_event_t no_tap = {
        .type = TOUCH_EVENT_UP, .flags = TOUCH_FLAG_NO_TAP,
        .point = { .id = 1, .x = (int16_t)(z.x + 10),
                   .y = (int16_t)(z.y + 10), .strength = 40 } };
    ev(z.x + 10, z.y + 10, TOUCH_EVENT_DOWN, 1);
    scr->event(&no_tap);
    CHECK(!rudder_geo()->zeroed);

    /* A second finger on RESET MAX while the first holds ZERO. */
    ev(z.x + 10, z.y + 10, TOUCH_EVENT_DOWN, 1);
    const gfx_rect_t r = rudder_rect(RUD_C_RESET);
    ev(r.x + 10, r.y + 10, TOUCH_EVENT_DOWN, 2);
    ev(r.x + 10, r.y + 10, TOUCH_EVENT_UP, 2);
    ev(z.x + 10, z.y + 10, TOUCH_EVENT_UP, 1);
    CHECK(rudder_geo()->zeroed);

    /* A press that the task switch takes with it. */
    servo_rudder_reset_max((servo_rudder_t *)rudder_geo());
    reading(300);
    ev(r.x + 10, r.y + 10, TOUCH_EVENT_DOWN, 1);
    tap(SV_TAB_LIVE_X, SV_TAB_Y);
    to_rudder();
    ev(r.x + 10, r.y + 10, TOUCH_EVENT_UP, 1);
    CHECK(rudder_geo()->have[1]);
    /* And a press on the card's empty part is nobody's. */
    tap(590, 400);
    CHECK(rudder_geo()->have[1]);
}

/* LENGTH's keypad: titled LENGTH, in mm, 0 to 500; 5mm is refused with
 * the gap's reason and the keypad stays; 0 and 120 are taken. */
TEST_CASE(length_takes_0_or_10_to_500mm_and_refuses_the_gap)
{
    fresh();
    to_rudder();
    press(RUD_C_LENGTH);
    const ui_keypad_t *k = servo_screen_keypad();
    REQUIRE_ALLOC(k);
    CHECK(k->open);
    CHECK_STR_EQ(k->title, "LENGTH");
    CHECK_STR_EQ(k->unit, "mm");
    CHECK_NEAR(k->min, 0.0f, 1e-6f);
    CHECK_NEAR(k->max, 500.0f, 1e-6f);
    CHECK_EQ(rudder_help_count(), 0);
    keys("5");
    CHECK(k->open);
    CHECK_STR_EQ(k->reason, "0 or 10 to 500mm");
    CHECK_EQ(settings_get(SET_RUDDER_LEN_MM), 0.0f);
    key(UI_KEY_CLR);
    keys("120");
    CHECK(!k->open);
    CHECK_EQ(settings_get(SET_RUDDER_LEN_MM), 120.0f);
    CHECK(settings_save_asked());
    press(RUD_C_LENGTH);
    key(UI_KEY_CLR);
    keys("0");
    CHECK(!k->open);
    CHECK_EQ(settings_get(SET_RUDDER_LEN_MM), 0.0f);
    scr->render(&cv, 0);
}

/* ------------------------------------------------------ the state line */

/* The source off, on with no reading, without its magnet, live, and the
 * link gone, in both languages; the pulse; the zero not taken; ARM FIRST
 * while SAFE and nothing while ARMED. */
TEST_CASE(the_state_line_names_no_source_stale_and_connected)
{
    static const char *const want[2][5] = {
        { "ANGLE: AS5600 OFF IN SETUP", "ANGLE: AS5600 NOT ANSWERING",
          "ANGLE: AS5600 SEES NO MAGNET", "ANGLE: AS5600", "ANGLE: NO LINK" },
        { "WINKEL: AS5600 AUS IN SETUP", "WINKEL: AS5600 ANTWORTET NICHT",
          "WINKEL: AS5600 SIEHT KEINEN MAGNETEN", "WINKEL: AS5600",
          "WINKEL: KEINE VERBINDUNG" },
    };
    char line[64];
    for (int lang = 0; lang < 2; ++lang) {
        fresh();
        ui_text_set_language((ui_lang_t)lang);
        to_rudder();
        CHECK_EQ(rudder_source(), RUD_SRC_OFF);
        CHECK_STR_EQ(rudder_line(0, line, sizeof(line)), want[lang][0]);
        encoder_on();
        CHECK_EQ(rudder_source(), RUD_SRC_STALE);
        CHECK_STR_EQ(rudder_line(0, line, sizeof(line)), want[lang][1]);
        const servo_test_enc_t none = { .valid = false, .no_magnet = true,
                                        .taken_ms = 900u };
        servo_screen_encoder(&none);
        CHECK_EQ(rudder_source(), RUD_SRC_NO_MAGNET);
        CHECK_STR_EQ(rudder_line(0, line, sizeof(line)), want[lang][2]);
        reading(10);
        CHECK_EQ(rudder_source(), RUD_SRC_LIVE);
        CHECK_STR_EQ(rudder_line(0, line, sizeof(line)), want[lang][3]);
        scr->render(&cv, 0);
        servo_screen_set_link(false);
        CHECK_EQ(rudder_source(), RUD_SRC_NO_LINK);
        CHECK_STR_EQ(rudder_line(0, line, sizeof(line)), want[lang][4]);
        scr->render(&cv, 0);
        servo_screen_set_link(true);
        CHECK_EQ(rudder_source(), RUD_SRC_STALE);
    }
    ui_text_set_language(UI_LANG_EN);
    fresh();
    to_rudder();
    CHECK_STR_EQ(rudder_line(1, line, sizeof(line)), "PULSE 1500us");
    CHECK_STR_EQ(rudder_line(2, line, sizeof(line)),
                 "NO ZERO: FROM THE SENSOR'S CENTRE");
    CHECK_STR_EQ(rudder_line(3, line, sizeof(line)), "ARM FIRST");
    CHECK_STR_EQ(rudder_line(4, line, sizeof(line)), "");
    CHECK_STR_EQ(rudder_line(-1, line, sizeof(line)), "");
    CHECK_STR_EQ(rudder_line(0, NULL, 8u), "");
    CHECK_STR_EQ(rudder_line(0, line, 0u), "");
    encoder_on();
    reading(0);
    press(RUD_C_ZERO);
    CHECK_STR_EQ(rudder_line(2, line, sizeof(line)), "");
    servo_screen_set_armed(true);
    CHECK_STR_EQ(rudder_line(3, line, sizeof(line)), "");
}

/* ---------------------------------------------------- the active output */

/* Leaving SERVO from RUDDER on an armed bench disarms, and a button after
 * the return posts nothing until the bench is ARMED again. */
TEST_CASE(leaving_servo_from_rudder_disarms)
{
    fresh_armed();
    to_rudder();
    press(RUD_C_2000);
    CHECK_EQ(taken().kind, SERVO_CMD_POSITION);
    scr->leave();
    CHECK_EQ(taken().kind, SERVO_CMD_DISARM);
    scr->enter();
    to_rudder();
    press(RUD_C_1000);
    CHECK_EQ(taken().kind, SERVO_CMD_NONE);
}

/* The TARGET row on RUDDER names what the drive path reports the
 * tester's commands go to, and the buttons' command is posted for it:
 * the outputs' pin, a port with its pin, no link. */
TEST_CASE(the_target_row_names_the_pin_the_buttons_drive)
{
    static const struct {
        uint64_t pins;
        uint8_t  drive, pin;
        const char *text;
        bool warn;
    } k[] = {
        { 1ull << 4, 0u, SERVO_SCREEN_NO_PIN, "SERVO PWM GPIO 4", false },
        { 1ull << 4, 1u, 13u, "PROG PORT 1 GPIO 13", true },
        { 1ull << 4, SERVO_SCREEN_TARGET_NO_LINK, 13u, "NO LINK", true },
    };
    static gfx_color_t want[SV_RC_W * 48], got[SV_RC_W * 48];
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        fresh_armed();
        servo_screen_set_pins(k[i].pins, true);
        servo_screen_set_drive(k[i].drive, k[i].pin, true);
        to_rudder();
        scr->render(&cv, 0);
        for (int y = 0; y < 48; ++y) {
            memcpy(got + y * SV_RC_W, fb + (size_t)(TARGET_ROW_Y + y) * W
                                          + SV_RC_X,
                   SV_RC_W * sizeof(gfx_color_t));
        }
        gfx_fill_rect(&cv, SV_RC_X, TARGET_ROW_Y, SV_RC_W, 48,
                      ui_theme_color(UI_C_PANEL));
        ui_target_row_t row;
        ui_target_row_init(&row, (gfx_rect_t){ SV_RC_X, TARGET_ROW_Y,
                                               SV_RC_W, 48 });
        ui_target_row_render(&row, &cv, "TARGET", k[i].text, k[i].warn,
                             true);
        for (int y = 0; y < 48; ++y) {
            memcpy(want + y * SV_RC_W, fb + (size_t)(TARGET_ROW_Y + y) * W
                                           + SV_RC_X,
                   SV_RC_W * sizeof(gfx_color_t));
        }
        CHECK_EQ(memcmp(want, got, sizeof(want)), 0);
        char line[32];
        CHECK_EQ(servo_target_line(line, sizeof(line), k[i].drive, k[i].pin,
                                   true, k[i].pins, true),
                 k[i].warn);
        CHECK_STR_EQ(line, k[i].text);
        press(RUD_C_2000);
        const servo_cmd_t c = taken();
        CHECK_EQ(c.kind, SERVO_CMD_POSITION);
        CHECK_EQ(c.value_us, 2000u);
    }
}

/* The knob moves the horn on RUDDER while ARMED, as on LIVE; SAFE it
 * moves nothing, and on TEST it is not RUDDER's. */
TEST_CASE(the_knob_moves_the_horn_on_rudder_while_armed)
{
    fresh();
    to_rudder();
    servo_screen_knob_frame();
    servo_screen_knob(0.25f);
    CHECK_EQ(taken().kind, SERVO_CMD_NONE);

    fresh_armed();
    to_rudder();
    servo_screen_knob_frame();
    servo_screen_knob(0.25f);
    const servo_cmd_t c = taken();
    CHECK_EQ(c.kind, SERVO_CMD_POSITION);
    CHECK(c.value_us > 1500u);
    char line[64], want[32];
    snprintf(want, sizeof(want), "PULSE %uus", (unsigned)c.value_us);
    CHECK_STR_EQ(rudder_line(1, line, sizeof(line)), want);

    open_test();
    servo_screen_knob_frame();
    servo_screen_knob(0.25f);
    CHECK_EQ(taken().kind, SERVO_CMD_NONE);
    /* A keypad over RUDDER's card takes the knob too. */
    to_rudder();
    press(RUD_C_LENGTH);
    servo_screen_knob_frame();
    servo_screen_knob(0.25f);
    CHECK_EQ(taken().kind, SERVO_CMD_NONE);
    key(UI_KEY_CANCEL);
    servo_screen_knob_frame();
    servo_screen_knob(-0.25f);
    CHECK_EQ(taken().kind, SERVO_CMD_POSITION);
}

/* ---------------------------------------------------------- the repaint */

/* A new reading repaints the readout and the state lines alone, in both
 * buffers, and each ends as a frame drawn whole would; a disarm repaints
 * the card. */
TEST_CASE(a_reading_repaints_the_readout_alone_and_as_a_whole_frame_would)
{
    fresh_armed();
    encoder_on();
    settings_set(SET_RUDDER_LEN_MM, 200.0f);
    to_rudder();
    reading(0);
    press(RUD_C_ZERO);
    two_buffers();
    CHECK(both_whole());
    memcpy(ref, fb, FB_BYTES);
    reading(300);
    scr->render(&cv, 0);
    const gfx_rect_t ro = rudder_readout_rect();
    const gfx_rect_t st = rudder_state_rect();
    int outside = 0, inside = 0;
    for (int y = 0; y < UI_BODY_H; ++y) {
        for (int x = 0; x < W; ++x) {
            const bool in = gfx_rect_contains(ro, x, y)
                            || gfx_rect_contains(st, x, y);
            const bool moved = fb[(size_t)y * W + x] != ref[(size_t)y * W + x];
            outside += (!in && moved) ? 1 : 0;
            inside += (in && moved) ? 1 : 0;
        }
    }
    CHECK_EQ(outside, 0);
    CHECK(inside > 0);
    scr->render(&cv1, 1);
    CHECK(both_whole());
    /* The same reading again: nothing to repaint. */
    reading(300);
    CHECK(both_whole());
    servo_screen_set_armed(false);
    CHECK(both_whole());
    reading(-300);
    CHECK(both_whole());
}

/* ------------------------------------------------------------ the guide */

/* One page on RUDDER, SAFE and ARMED: the output card's, the dock on the
 * left over RUDDER's card, the TARGET row and DISARM in view.  Every item
 * in the live half, at most 3 rows, in both languages.  No page on LIVE or
 * under a panel. */
TEST_CASE(the_help_page_leaves_the_target_row_in_view)
{
    fresh();
    CHECK_EQ(scr->help_count(), 0);
    to_rudder();
    CHECK_EQ(scr->help_count(), 1);
    CHECK_EQ(ui_help_count(scr, false), 1);
    CHECK_EQ(ui_help_count(scr, true), 1);
    ui_help_page_t p;
    CHECK(ui_help_page(scr, true, 0, &p));
    CHECK(p.armed_ok);
    CHECK_EQ(p.title, TX_HG_RUD_TITLE_OUT);
    CHECK_EQ(p.must_show.x, SV_RC_X);
    CHECK_EQ(p.must_show.y, TARGET_ROW_Y);
    CHECK_EQ(p.must_show.w, SV_RC_W);
    CHECK_EQ(p.must_show.h, 48);
    gfx_rect_t m = p.must_show;
    m.y = (int16_t)(m.y + UI_BAND_H);
    const gfx_rect_t dock = ui_help_dock_rect(p.side);
    CHECK(m.x >= dock.x + dock.w || m.x + m.w <= dock.x);
    /* DISARM out from under the dock. */
    CHECK(p.item[1].rect.x >= dock.x + dock.w);
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        for (int i = 0; i < 1; ++i) {
            CHECK(scr->help_page(i, &p));
            const gfx_rect_t live = ui_help_live_rect(p.side);
            const gfx_rect_t d = ui_help_dock_rect(p.side);
            CHECK(p.count >= 1 && p.count <= UI_HELP_ITEMS);
            for (int k = 0; k < p.count; ++k) {
                gfx_rect_t r = p.item[k].rect;
                r.y = (int16_t)(r.y + UI_BAND_H);
                CHECK(r.x >= live.x && r.x + r.w <= live.x + live.w);
                CHECK(r.y >= live.y && r.y + r.h <= live.y + live.h);
                CHECK(r.x >= d.x + d.w || r.x + r.w <= d.x);
                const int rows = ui_help_item_rows(&p, k);
                CHECK(rows >= 1 && rows <= UI_HELP_ROWS);
                CHECK(p.item[k].label != NULL && p.item[k].label[0] != '\0');
            }
            CHECK(ui_help_text_bottom(&p, true) <= ui_help_text_limit());
        }
    }
    ui_text_set_language(UI_LANG_EN);
    CHECK(!scr->help_page(1, &p));
    CHECK(!scr->help_page(-1, &p));
    CHECK(!scr->help_page(0, NULL));
    press(RUD_C_LENGTH);
    CHECK_EQ(scr->help_count(), 0);
    CHECK(!scr->help_page(0, &p));
    key(UI_KEY_CANCEL);
    CHECK_EQ(scr->help_count(), 1);
    tap(SV_TAB_LIVE_X, SV_TAB_Y);
    CHECK_EQ(scr->help_count(), 0);
}

/* ---------------------------------------------------------- the strings */

/* Every string RUDDER draws fits its field in both languages at its
 * longest: the tab, the buttons, LENGTH's value, the labels, the widest
 * readings with their units, the MAX lines and every state line. */
TEST_CASE(every_string_fits)
{
    const gfx_font_t *f = &gfx_font_10x20;
    for (int lang = 0; lang < (int)UI_LANG_COUNT; ++lang) {
        ui_text_set_language((ui_lang_t)lang);
        CHECK(ui_text_fits(f, TR(WB_RUDDER), 120, 40, 6));
        static const ui_text_id_t k_btn[] = { TX_SV_CENTRE_BTN, TX_RUD_ZERO,
                                              TX_RUD_RESET_MAX };
        static const rudder_ctl_t k_ctl[] = { RUD_C_CENTRE, RUD_C_ZERO,
                                              RUD_C_RESET };
        for (int i = 0; i < 3; ++i) {
            const gfx_rect_t r = rudder_rect(k_ctl[i]);
            CHECK(ui_text_fits(f, ui_tr(k_btn[i]), r.w, r.h, 8));
        }
        CHECK(ui_text_fits(f, "1000us", 160, 48, 8));
        CHECK(ui_text_fits(f, TR(RUD_LEN_NOT_SET), 200, 48, 8));
        CHECK(ui_text_fits(f, "500mm", 200, 48, 8));
        CHECK(gfx_text_width(f, TR(RUD_LENGTH), 1) <= 300 - 10);
        CHECK(gfx_text_width(f, TR(RUD_DEFL), 1) <= 240);
        CHECK(gfx_text_width(f, TR(RUD_EDGE), 1) <= 240);
        CHECK(gfx_text_width(f, TR(RUD_NO_LENGTH), 1) <= 240);
        CHECK(gfx_text_width(f, TR(RUD_NOT_ZEROED), 1) <= 500);
        char line[80];
        snprintf(line, sizeof(line), "%s -180.0°  +180.0°", TR(RUD_MAX));
        CHECK(gfx_text_width(f, line, 1) <= 240);
        snprintf(line, sizeof(line), "%s -500.0mm  +500.0mm", TR(RUD_MAX));
        CHECK(gfx_text_width(f, line, 1) <= 240);
        snprintf(line, sizeof(line), TR(RUD_PULSE), "2500us");
        CHECK(gfx_text_width(f, line, 1) <= 500);
        static const ui_text_id_t k_src[] = {
            TX_RUD_SRC_OFF, TX_RUD_SRC_NO_LINK, TX_RUD_SRC_NO_MAGNET,
            TX_RUD_SRC_STALE, TX_RUD_SRC_LIVE };
        for (size_t i = 0; i < sizeof(k_src) / sizeof(k_src[0]); ++i) {
            snprintf(line, sizeof(line), ui_tr(k_src[i]), "AS5600");
            CHECK(gfx_text_width(f, line, 1) <= 500);
        }
        CHECK(gfx_text_width(f, ui_servo_str(SERVO_STR_START_NOT_ARMED), 1)
              <= 500);
    }
    ui_text_set_language(UI_LANG_EN);
    /* The widest readings with their units in a column of 250px. */
    CHECK(gfx_text_width(&gfx_font_num_32x44, "-500.0", 1) + 4
              + gfx_text_width(&gfx_font_20x36, "mm", 1)
          <= 240);
    CHECK(gfx_text_width(&gfx_font_num_32x44, "-180.0", 1) + 4
              + gfx_text_width(&gfx_font_20x36, "°", 1)
          <= 240);
}

int main(void)
{
    RUN(the_strip_has_five_tabs_and_rudder_opens);
    RUN(the_readout_shows_degrees_and_mm_with_a_length_and_degrees_alone_without);
    RUN(reverse_flips_the_readout_and_keeps_the_zero);
    RUN(the_buttons_drive_1000_1500_2000us_only_while_armed);
    RUN(zero_takes_the_current_angle_and_reset_clears_the_max);
    RUN(a_tap_that_slides_off_or_loses_its_release_does_nothing);
    RUN(length_takes_0_or_10_to_500mm_and_refuses_the_gap);
    RUN(the_state_line_names_no_source_stale_and_connected);
    RUN(leaving_servo_from_rudder_disarms);
    RUN(the_target_row_names_the_pin_the_buttons_drive);
    RUN(the_knob_moves_the_horn_on_rudder_while_armed);
    RUN(a_reading_repaints_the_readout_alone_and_as_a_whole_frame_would);
    RUN(the_help_page_leaves_the_target_row_in_view);
    RUN(every_string_fits);
    return test_summary("servo_rudder_panel");
}
