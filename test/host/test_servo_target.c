/**
 * @file test_servo_target.c
 * @brief The pins the servo screen says it drives: the target row, the target line
 * and the port's pulse.
 *
 * Cases:
 * - the_encoders_angle_replaces_the_dashes_in_the_measured_row
 * - an_enabled_encoder_without_a_reading_does_not_fall_back_to_the_feedback
 * - outputs_prog_port_entry_sets_what_the_tester_drives_at_rest
 * - the_servo_screen_names_the_pins_the_tester_drives
 * - the_target_line_lists_the_pins_that_fit_and_counts_the_rest
 * - the_target_row_stays_in_sight_under_the_settings_and_a_keypad
 * - the_port_pulse_is_not_switched_off_under_a_test_or_a_sweep
 * - the_tester_target_and_pulse_stay_while_the_port_is_held
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_screen_rig.h"

/* ------------------------------------------------- the output encoder */

/* The DUT page's rows: AS5600 on row 2 and ENC CENTRE on row 3 of the left
 * column. */
TEST_CASE(the_encoders_angle_replaces_the_dashes_in_the_measured_row)
{
    fresh();
    scr->render(&cv, 0);
    gfx_color_t *none = malloc((size_t)W * H * sizeof(gfx_color_t));
    REQUIRE_ALLOC(none);
    memcpy(none, fb, (size_t)W * H * sizeof(gfx_color_t));
    const servo_test_enc_t e = { true, 3100u, 0u, 1000u, false, false, false, false };

    /* AS5600 off in SETUP: the reading is ignored. */
    CHECK(!settings_get_bool(SET_ENC_EN));
    servo_screen_encoder(&e);
    scr->render(&cv, 0);
    CHECK_EQ(memcmp(none, fb, (size_t)W * H * sizeof(gfx_color_t)), 0);

    /* On: the MEASURED row shows the angle from the centre count. */
    settings_set(SET_ENC_EN, 1.0f);
    settings_set(SET_ENC_CENTRE, 3000.0f);
    servo_invalidate();
    servo_screen_encoder(&e);
    scr->render(&cv, 0);
    CHECK(memcmp(none, fb, (size_t)W * H * sizeof(gfx_color_t)) != 0);
    gfx_color_t *shown = malloc((size_t)W * H * sizeof(gfx_color_t));
    REQUIRE_ALLOC(shown);
    memcpy(shown, fb, (size_t)W * H * sizeof(gfx_color_t));

    /* Another count, another angle; the same count, the same picture. */
    const servo_test_enc_t e2 = { true, 3200u, 0u, 1020u, false, false, false, false };
    servo_screen_encoder(&e2);
    scr->render(&cv, 0);
    CHECK(memcmp(shown, fb, (size_t)W * H * sizeof(gfx_color_t)) != 0);
    servo_screen_encoder(&e);
    scr->render(&cv, 0);
    CHECK_EQ(memcmp(shown, fb, (size_t)W * H * sizeof(gfx_color_t)), 0);

    /* A reading that is not valid is the dashes again. */
    const servo_test_enc_t gone = { false, 0u, 0u, 1040u, false, false, false, false };
    servo_screen_encoder(&gone);
    scr->render(&cv, 0);
    CHECK_EQ(memcmp(none, fb, (size_t)W * H * sizeof(gfx_color_t)), 0);
    servo_screen_encoder(NULL);
    free(none);
    free(shown);
}

/* LIVE's MEASURED reading, its numerals in the left card's second
 * column, copied out of the frame: 20 rows of SV_RCARD_W px from its
 * left edge. */
static void measured_row(gfx_color_t *out)
{
    for (int y = 0; y < 20; ++y) {
        memcpy(out + y * SV_RCARD_W,
               fb + (size_t)(476 + 12 + y) * W + UI_MARGIN + 12 + 198,
               SV_RCARD_W * sizeof(gfx_color_t));
    }
}

/* With the encoder on in SETUP and no valid reading, the MEASURED row shows
 * the dashes, not the angle of the pulse position. */
TEST_CASE(an_enabled_encoder_without_a_reading_does_not_fall_back_to_the_feedback)
{
    fresh();
    gfx_color_t *a = malloc(20 * SV_RCARD_W * sizeof(gfx_color_t));
    REQUIRE_ALLOC(a);
    gfx_color_t *row_b = malloc(20 * SV_RCARD_W * sizeof(gfx_color_t));
    REQUIRE_ALLOC(row_b);

    /* Encoder off: the feedback's angle is on show and follows it. */
    servo_screen_feedback(1700u, 0.2f, true);
    scr->render(&cv, 0);
    measured_row(a);
    servo_screen_feedback(1300u, 0.2f, true);
    scr->render(&cv, 0);
    measured_row(row_b);
    CHECK(memcmp(a, row_b, 20 * SV_RCARD_W * sizeof(gfx_color_t)) != 0);

    /* Encoder on, no reading: dashes, whatever the feedback says. */
    settings_set(SET_ENC_EN, 1.0f);
    servo_invalidate();
    servo_screen_feedback(1700u, 0.2f, true);
    scr->render(&cv, 0);
    measured_row(a);
    servo_screen_feedback(1300u, 0.2f, true);
    scr->render(&cv, 0);
    measured_row(row_b);
    CHECK_EQ(memcmp(a, row_b, 20 * SV_RCARD_W * sizeof(gfx_color_t)), 0);

    /* A reading puts the encoder's angle there. */
    const servo_test_enc_t e = { true, 3100u, 0u, 1000u, false, false, false, false };
    servo_screen_encoder(&e);
    scr->render(&cv, 0);
    measured_row(row_b);
    CHECK(memcmp(a, row_b, 20 * SV_RCARD_W * sizeof(gfx_color_t)) != 0);
    free(a);
    free(row_b);
}

/*
 * SETUP > OUTPUTS' PROG PORT entry, which the TARGET row opens: its SERVO
 * TESTER segment (OUTPUTS | PROG PORT, left column x 19 to 279, y 204 to
 * 240) and its PORT PWM segment (ON | OFF, y 274 to 310), two tabs of
 * 128 px 4 px apart.  Mirrored from outputs_screen.c.
 */
#define OSEG_X(i)      (19 + 64 + (i) * 132)

#define OSEG_TESTER_Y  (242 + 20 + 18)

#define OSEG_PWM_Y     (306 + 20 + 18)

static void out_ev(int x, int y, touch_event_type_t type)
{
    const touch_event_t e = { .type = type,
                              .point = { .id = 2, .x = (int16_t)x,
                                         .y = (int16_t)y, .strength = 40 } };
    outputs_screen()->event(&e);
}

static void out_tap(int x, int y)
{
    out_ev(x, y, TOUCH_EVENT_DOWN);
    out_ev(x, y, TOUCH_EVENT_UP);
}

/* OUTPUTS on its PROG PORT entry, as the TARGET row opens it. */
static void out_open_port(void)
{
    outputs_screen()->reset();
    outputs_screen_set_prog_ports(1u, (const uint8_t[]){ 13u, 0xFFu, 0xFFu,
                                                         0xFFu });
    outputs_screen_open_on_port(0u);
    outputs_screen()->enter();
    CHECK_EQ(outputs_screen_port_shown(), 0);
}

/* What the tester drives is set on OUTPUTS' PROG PORT entry, on a bench at
 * rest: SERVO TESTER, the TARGET setting, and PORT PWM, which has no port
 * to switch while the tester drives the outputs.  SETTINGS' DUT page holds
 * no PORT PWM row: its right column's row 1 takes no tap. */
TEST_CASE(outputs_prog_port_entry_sets_what_the_tester_drives_at_rest)
{
    fresh();
    out_open_port();
    open_settings();
    tap(TAB_X(PAGE_DUT), TAB_Y);               /* DUT */
    tap(ROW_R_X, ROW_Y(1));                    /* where PORT PWM was */
    CHECK(settings_get_bool(SET_SERVO_PORT_PWM));
    CHECK_EQ(settings_get_int(SET_SERVO_TARGET), 0);
    /* With the outputs as target PORT PWM has no port to switch. */
    CHECK(servo_screen_port_pwm_locked());
    out_tap(OSEG_X(1), OSEG_PWM_Y);            /* OFF */
    CHECK(settings_get_bool(SET_SERVO_PORT_PWM));

    CHECK(!servo_screen_tester_target_locked());
    out_tap(OSEG_X(1), OSEG_TESTER_Y);         /* PORT 1 */
    CHECK_EQ(settings_get_int(SET_SERVO_TARGET), 1);
    CHECK(settings_save_asked());
    out_tap(OSEG_X(1), OSEG_PWM_Y);            /* OFF */
    CHECK(!settings_get_bool(SET_SERVO_PORT_PWM));
    out_tap(OSEG_X(0), OSEG_PWM_Y);            /* ON */
    CHECK(settings_get_bool(SET_SERVO_PORT_PWM));

    /* Armed: the target stays. */
    servo_screen_set_armed(true);
    CHECK(servo_screen_target_locked());
    CHECK(servo_screen_tester_target_locked());
    out_tap(OSEG_X(0), OSEG_TESTER_Y);         /* OUTPUTS */
    CHECK_EQ(settings_get_int(SET_SERVO_TARGET), 1);
    /* The pulse goes off under an armed bench and does not come back. */
    out_tap(OSEG_X(1), OSEG_PWM_Y);
    CHECK(!settings_get_bool(SET_SERVO_PORT_PWM));
    out_tap(OSEG_X(0), OSEG_PWM_Y);
    CHECK(!settings_get_bool(SET_SERVO_PORT_PWM));
    /* Disarmed it does, and the target goes back. */
    servo_screen_set_armed(false);
    CHECK(!servo_screen_tester_target_locked());
    out_tap(OSEG_X(0), OSEG_PWM_Y);
    CHECK(settings_get_bool(SET_SERVO_PORT_PWM));

    /* The bench ARMED for the motor, which this screen does not arm: the
     * target stays, the pulse goes off and stays off. */
    servo_screen_set_bench_armed(true);
    CHECK(!servo_screen_target_locked());
    CHECK(servo_screen_tester_target_locked());
    out_tap(OSEG_X(0), OSEG_TESTER_Y);
    CHECK_EQ(settings_get_int(SET_SERVO_TARGET), 1);
    out_tap(OSEG_X(1), OSEG_PWM_Y);
    CHECK(!settings_get_bool(SET_SERVO_PORT_PWM));
    out_tap(OSEG_X(0), OSEG_PWM_Y);
    CHECK(!settings_get_bool(SET_SERVO_PORT_PWM));
    servo_screen_set_bench_armed(false);
    out_tap(OSEG_X(0), OSEG_PWM_Y);
    CHECK(settings_get_bool(SET_SERVO_PORT_PWM));
    out_tap(OSEG_X(0), OSEG_TESTER_Y);
    CHECK_EQ(settings_get_int(SET_SERVO_TARGET), 0);
    /* OUTPUTS again: PORT PWM is locked and keeps its value. */
    CHECK(servo_screen_port_pwm_locked());
    out_tap(OSEG_X(1), OSEG_PWM_Y);
    CHECK(settings_get_bool(SET_SERVO_PORT_PWM));
}

#define TGT_Y 308

#define TGT_H 48

/* The target line's box at the foot of the left card before the TARGET
 * row: left 6 + 200, top (H - UI_BAND_H) - 6 - 36, 278 x 26 px. */
#define OLD_TGT_X 206

#define OLD_TGT_Y ((H - UI_BAND_H) - 6 - 36)

static void target_box(gfx_color_t *out)
{
    for (int y = 0; y < TGT_H; ++y) {
        memcpy(out + y * TGT_W, fb + (TGT_Y + y) * W + TGT_X,
               TGT_W * sizeof(gfx_color_t));
    }
}

/* One state of the target row. */
typedef struct {
    int      target;        /* SET_SERVO_TARGET */
    uint8_t  drive;         /* the drive's target: 0, 1 + n for port n,
                               SERVO_SCREEN_TARGET_UNKNOWN or _NO_LINK */
    uint64_t pins;          /* GPIOs bound as SERVO PWM */
    bool     known;
    uint8_t  prog_pin;
    bool     pwm;           /* the port renders its pulse */
    bool     warn;          /* the warning colour */
} tgt_state_t;

#define PINS_40_47 (0xFFull << 40)

#define T_UNKNOWN SERVO_SCREEN_TARGET_UNKNOWN

#define T_NO_LINK SERVO_SCREEN_TARGET_NO_LINK

static const tgt_state_t k_tgt[] = {
    { 0, 0, PIN(2),                              true,  0xFFu, false, false },
    { 0, 0, PIN(4),                              true,  0xFFu, false, false },
    { 0, 0, PIN(2) | PIN(4),                     true,  0xFFu, false, false },
    { 0, 0, PIN(2) | PIN(4) | PIN(5),            true,  0xFFu, false, false },
    { 0, 0, PIN(12) | PIN(13) | PIN(14) | PIN(15), true, 0xFFu, false, false },
    { 0, 0, PIN(2) | PIN(4) | PIN(5) | PIN(6) | PIN(7) | PIN(8), true,
      0xFFu, false, false },
    { 0, 0, PINS_40_47,                          true,  0xFFu, false, false },
    { 0, 0, 0,                                   true,  0xFFu, false, true  },
    { 0, 0, 0,                                   false, 0xFFu, false, true  },
    { 1, 1, PIN(2),                              true,  0xFFu, true,  true  },
    { 1, 1, PIN(2),                              true,  13u,   true,  true  },
    /* The port's pulse off: the longest values, with a two-digit pin. */
    { 1, 1, PIN(2),                              true,  63u,   false, true  },
    { 1, 1, PIN(2),                              true,  0xFFu, false, true  },
    /* Not known and no link, with GP2 bound: the tester sends nothing. */
    { 1, T_UNKNOWN, PIN(2),                      true,  0xFFu, false, true  },
    { 1, T_NO_LINK, PIN(2),                      true,  0xFFu, false, true  },
};

#define TGT_N ((int)(sizeof(k_tgt) / sizeof(k_tgt[0])))

#define TGT_NONE   7        /* k_tgt: no SERVO PWM pin */

#define TGT_PORT   9        /* k_tgt: the port, its pin not known */

#define TGT_PORT_PIN 10     /* k_tgt: the port on GP13, its pulse on */

#define TGT_PORT_OFF 11     /* k_tgt: the port on GP63, its pulse off */

#define TGT_UNKNOWN 13      /* k_tgt: the target not known */

#define TGT_NO_LINK 14      /* k_tgt: no link */

/* The exact text of each k_tgt state's value, in English and in German,
 * written out here and not taken from servo_target_line(). */
static const char *const k_tgt_txt[2][TGT_N] = {
    { "SERVO PWM GP2", "SERVO PWM GP4", "SERVO PWM GP2 GP4",
      "SERVO PWM GP2 GP4 GP5", "SERVO PWM GP12 GP13 GP14 +1",
      "SERVO PWM GP2 GP4 GP5 +3", "SERVO PWM GP40 GP41 GP42 +5",
      "NO SERVO PWM PIN",
      "PINS NOT READ", "PROG PORT 1", "PROG PORT 1 GP13",
      "PORT 1 GP63 PWM OFF", "PROG PORT 1 PWM OFF", "UNKNOWN", "NO LINK" },
    { "SERVO PWM GP2", "SERVO PWM GP4", "SERVO PWM GP2 GP4",
      "SERVO PWM GP2 GP4 GP5", "SERVO PWM GP12 GP13 GP14 +1",
      "SERVO PWM GP2 GP4 GP5 +3", "SERVO PWM GP40 GP41 GP42 +5",
      "KEIN SERVO-PWM-PIN",
      "PINS NICHT AUSGELESEN", "PROG-PORT 1", "PROG-PORT 1 GP13",
      "PORT 1 GP63 PWM AUS", "PROG-PORT 1 PWM AUS", "UNBEKANNT",
      "KEINE VERBINDUNG" },
};

/* The row a label and a value give, drawn over the screen's by the row
 * widget (ui_target_row.h, whose own pixels test_widgets holds) at the
 * row's place: the label at SV_RC_X, the value in the box 64 px further, in
 * the warning colour for @p warn, the box drawn locked for @p locked. */
static void target_reference_as(gfx_color_t *out, const char *label,
                                const char *text, bool warn, bool locked)
{
    gfx_fill_rect(&cv, TGT_X, TGT_Y, TGT_W, TGT_H,
                  ui_theme_color(UI_C_PANEL));
    ui_target_row_t row;
    ui_target_row_init(&row, (gfx_rect_t){ TGT_X, TGT_Y, TGT_W, TGT_H });
    ui_target_row_render(&row, &cv, label, text, warn, locked);
    target_box(out);
}

static void target_reference(gfx_color_t *out, const char *label,
                             const char *text, bool warn)
{
    target_reference_as(out, label, text, warn, false);
}

static int warn_pixels(const gfx_color_t *bx)
{
    int n = 0;
    for (int i = 0; i < TGT_W * TGT_H; ++i) {
        n += (bx[i] == ui_theme_color(UI_C_WARN)) ? 1 : 0;
    }
    return n;
}

/* The active PWM target is on the SERVO screen's right card in both
 * languages: the pins bound as SERVO PWM for OUTPUTS, the programming port
 * with its pin, and the warning colour for the port, for no pin, for a
 * binding not read, for a target not known and for no link.  Every state
 * reads differently, and no value runs out of its box: the longest are the
 * protocol with three two-digit pins and a count, 27 of the 27 cells, and
 * the port on a two-digit pin with its pulse off, 19. */
TEST_CASE(the_servo_screen_names_the_pins_the_tester_drives)
{
    gfx_color_t *box[2 * TGT_N];
    for (int i = 0; i < 2 * TGT_N; ++i) {
        box[i] = malloc(TGT_W * TGT_H * sizeof(gfx_color_t));
        REQUIRE_ALLOC(box[i]);
    }
    for (int lang = 0; lang < 2; ++lang) {
        for (int i = 0; i < TGT_N; ++i) {
            const tgt_state_t *st = &k_tgt[i];
            fresh();
            ui_text_set_language(lang == 0 ? UI_LANG_EN : UI_LANG_DE);
            settings_set(SET_SERVO_TARGET, (float)st->target);
            servo_screen_set_pins(st->pins, st->known);
            servo_screen_set_drive(st->drive, st->prog_pin, st->pwm);
            scr->render(&cv, 0);
            gfx_color_t *bx = box[lang * TGT_N + i];
            target_box(bx);
            CHECK_EQ(warn_pixels(bx) > 0, st->warn);
            /* The pixels of the label and the value the state must give. */
            static gfx_color_t want_box[TGT_W * TGT_H];
            target_reference(want_box, lang == 0 ? "TARGET" : "ZIEL",
                             k_tgt_txt[lang][i], st->warn);
            CHECK_EQ(memcmp(want_box, bx, sizeof(want_box)), 0);
            /* The value fits the 27 cells of its box. */
            CHECK(gfx_text_cells(k_tgt_txt[lang][i]) <= 27);
            /* The foot of the left card carries no second target line. */
            CHECK_EQ(fb[(OLD_TGT_Y + 2) * W + OLD_TGT_X + 4],
                     ui_theme_color(UI_C_PANEL));
        }
    }
    ui_text_set_language(UI_LANG_EN);
    for (int i = 0; i < 2 * TGT_N; ++i) {
        for (int j = i + 1; j < 2 * TGT_N; ++j) {
            /* The pin lists read the same in both languages; only the
             * label tells them apart. */
            CHECK(memcmp(box[i], box[j], TGT_W * TGT_H
                                         * sizeof(gfx_color_t)) != 0);
        }
    }

    /* Rebinding GP2 to GP4 changes the row, without a tap; and back. */
    fresh();
    servo_screen_set_pins(PIN(2), true);
    scr->render(&cv, 0);
    static gfx_color_t a[TGT_W * TGT_H];
    target_box(a);
    servo_screen_set_pins(PIN(4), true);
    scr->render(&cv, 0);
    target_box(box[0]);
    CHECK(memcmp(a, box[0], TGT_W * TGT_H * sizeof(gfx_color_t)) != 0);
    servo_screen_set_pins(PIN(2), true);
    scr->render(&cv, 0);
    target_box(box[0]);
    CHECK_EQ(memcmp(a, box[0], TGT_W * TGT_H * sizeof(gfx_color_t)), 0);

    /* The row follows the drive, not the setting.  PROG PORT chosen while
     * the drive still uses the outputs shows the pins the outputs carry, as
     * GP2 does; with no such pin it is the warning, as no pin does. */
    static gfx_color_t d[TGT_W * TGT_H];
    fresh();
    settings_set(SET_SERVO_TARGET, 1.0f);
    servo_screen_set_pins(PIN(2), true);
    servo_screen_set_drive(0u, 13u, true);
    scr->render(&cv, 0);
    target_box(d);
    CHECK_EQ(memcmp(d, box[0], TGT_W * TGT_H * sizeof(gfx_color_t)), 0);
    servo_screen_set_pins(0u, true);
    scr->render(&cv, 0);
    target_box(d);
    CHECK_EQ(memcmp(d, box[TGT_NONE], TGT_W * TGT_H * sizeof(gfx_color_t)),
             0);
    /* The port driven while the setting still reads OUTPUTS. */
    fresh();
    settings_set(SET_SERVO_TARGET, 0.0f);
    servo_screen_set_pins(PIN(2), true);
    servo_screen_set_drive(1u, 13u, true);
    scr->render(&cv, 0);
    target_box(d);
    CHECK_EQ(memcmp(d, box[TGT_PORT_PIN], TGT_W * TGT_H * sizeof(gfx_color_t)),
             0);
    /* Only the port's pulse turned off: the row follows, without a tap. */
    servo_screen_set_drive(1u, 63u, true);
    scr->render(&cv, 0);
    servo_screen_set_drive(1u, 63u, false);
    scr->render(&cv, 0);
    target_box(d);
    CHECK_EQ(memcmp(d, box[TGT_PORT_OFF], TGT_W * TGT_H * sizeof(gfx_color_t)),
             0);
    /* The port driven with no pin known (none, refused, or a port the page
     * does not have): still the port, not the GP2 the outputs carry. */
    servo_screen_set_drive(1u, SERVO_SCREEN_NO_PIN, true);
    scr->render(&cv, 0);
    target_box(d);
    CHECK_EQ(memcmp(d, box[TGT_PORT], TGT_W * TGT_H * sizeof(gfx_color_t)),
             0);
    CHECK(memcmp(d, box[0], TGT_W * TGT_H * sizeof(gfx_color_t)) != 0);
    /* Only the pin changed, port and pulse kept: the row follows. */
    servo_screen_set_drive(1u, 13u, true);
    scr->render(&cv, 0);
    target_box(d);
    CHECK_EQ(memcmp(d, box[TGT_PORT_PIN], TGT_W * TGT_H * sizeof(gfx_color_t)),
             0);
    servo_screen_set_drive(1u, 14u, true);
    scr->render(&cv, 0);
    target_box(d);
    CHECK(memcmp(d, box[TGT_PORT_PIN], TGT_W * TGT_H * sizeof(gfx_color_t))
          != 0);
    /* The link lost with the binding still held: no link, not the GP2 the
     * outputs carry; then the page not known after the link comes back;
     * then the outputs once it is read. */
    servo_screen_set_drive(SERVO_SCREEN_TARGET_NO_LINK, 13u, true);
    scr->render(&cv, 0);
    target_box(d);
    CHECK_EQ(memcmp(d, box[TGT_NO_LINK], TGT_W * TGT_H * sizeof(gfx_color_t)),
             0);
    servo_screen_set_drive(SERVO_SCREEN_TARGET_UNKNOWN, 13u, true);
    scr->render(&cv, 0);
    target_box(d);
    CHECK_EQ(memcmp(d, box[TGT_UNKNOWN], TGT_W * TGT_H * sizeof(gfx_color_t)),
             0);
    servo_screen_set_drive(0u, SERVO_SCREEN_NO_PIN, false);
    scr->render(&cv, 0);
    target_box(d);
    CHECK_EQ(memcmp(d, box[0], TGT_W * TGT_H * sizeof(gfx_color_t)), 0);
    for (int i = 0; i < 2 * TGT_N; ++i) {
        free(box[i]);
    }
}

/*
 * The row is on the right card, which the settings, the keypads they open
 * and the servo programmer leave uncovered: while they cover the left card
 * the row still names the pin, and a change of the drive under them still
 * reaches it.
 */
TEST_CASE(the_target_row_stays_in_sight_under_the_settings_and_a_keypad)
{
    static gfx_color_t want[TGT_W * TGT_H], got[TGT_W * TGT_H];
    fresh();
    servo_screen_set_pins(PIN(4), true);
    scr->render(&cv, 0);
    target_box(want);
    target_reference(got, "TARGET", "SERVO PWM GP4", false);
    CHECK_EQ(memcmp(want, got, sizeof(want)), 0);

    open_settings();
    scr->render(&cv, 0);
    target_box(got);
    CHECK_EQ(memcmp(want, got, sizeof(want)), 0);
    /* The drive moves to the port while the settings are open. */
    servo_screen_set_drive(1u, 13u, true);
    scr->render(&cv, 0);
    target_box(got);
    static gfx_color_t port[TGT_W * TGT_H];
    target_reference(port, "TARGET", "PROG PORT 1 GP13", true);
    scr->render(&cv, 0);
    target_box(got);
    CHECK_EQ(memcmp(port, got, sizeof(port)), 0);
    servo_screen_set_drive(0u, SERVO_SCREEN_NO_PIN, false);

    /* PULSE MIN's keypad over the left card. */
    tap(ROW_L_X, ROW_Y(2));
    scr->render(&cv, 0);
    target_box(got);
    CHECK_EQ(memcmp(want, got, sizeof(want)), 0);
    key(UI_KEY_CANCEL);
    close_settings();
}

/* One value of the target row and its colour for a drive and a set of
 * pins. */
static void check_line(uint8_t target, uint8_t gpio, bool pwm, uint64_t pins,
                       bool known, const char *want, bool warn)
{
    char line[40];
    memset(line, 'x', sizeof(line));
    CHECK_EQ(servo_target_line(line, sizeof(line), target, gpio, pwm, pins,
                               known),
             warn);
    CHECK_STR_EQ(line, want);
}

/* The target row's value: as many pins as the 27 cells hold, then the
 * count of the rest, the port with its pin or without and with its pulse on
 * or off, the target not known and no link, in both languages. */
TEST_CASE(the_target_line_lists_the_pins_that_fit_and_counts_the_rest)
{
    ui_text_set_language(UI_LANG_EN);
    check_line(0u, SERVO_SCREEN_NO_PIN, false, PIN(2), true, "SERVO PWM GP2",
               false);
    check_line(0u, SERVO_SCREEN_NO_PIN, false, PIN(2) | PIN(4) | PIN(5), true,
               "SERVO PWM GP2 GP4 GP5", false);
    check_line(0u, SERVO_SCREEN_NO_PIN, false,
               PIN(12) | PIN(13) | PIN(14) | PIN(15), true,
               "SERVO PWM GP12 GP13 GP14 +1", false);
    check_line(0u, SERVO_SCREEN_NO_PIN, false,
               PIN(2) | PIN(4) | PIN(5) | PIN(6) | PIN(7), true,
               "SERVO PWM GP2 GP4 GP5 +2", false);
    /* The list ends exactly at the 27 cells, the protocol's 10 and 17 of
     * pins: no count. */
    check_line(0u, SERVO_SCREEN_NO_PIN, false,
               PIN(1) | PIN(2) | PIN(10) | PIN(11), true,
               "SERVO PWM GP1 GP2 GP10 GP11", false);
    /* One pin more overflows it: 3 pins and " +2". */
    check_line(0u, SERVO_SCREEN_NO_PIN, false,
               PIN(1) | PIN(2) | PIN(10) | PIN(11) | PIN(12), true,
               "SERVO PWM GP1 GP2 GP10 +2", false);
    check_line(0u, SERVO_SCREEN_NO_PIN, false,
               PIN(10) | PIN(11) | PIN(12) | PIN(13) | PIN(14), true,
               "SERVO PWM GP10 GP11 GP12 +2", false);
    check_line(0u, SERVO_SCREEN_NO_PIN, false, PINS_40_47, true,
               "SERVO PWM GP40 GP41 GP42 +5", false);
    check_line(0u, SERVO_SCREEN_NO_PIN, false, ~0ull, true,
               "SERVO PWM GP0 GP1 GP2 +61", false);
    check_line(0u, SERVO_SCREEN_NO_PIN, false, PIN(63), true,
               "SERVO PWM GP63", false);
    check_line(0u, SERVO_SCREEN_NO_PIN, false, 0u, true,
               "NO SERVO PWM PIN", true);
    check_line(0u, SERVO_SCREEN_NO_PIN, false, PIN(2), false,
               "PINS NOT READ", true);
    /* With the outputs as target a port pin names nothing. */
    check_line(0u, 13u, false, PIN(2), true, "SERVO PWM GP2", false);
    /* A port target is the port, its pin known or not, pins bound or not. */
    check_line(1u, 13u, true, PIN(2), true, "PROG PORT 1 GP13", true);
    check_line(1u, SERVO_SCREEN_NO_PIN, true, PIN(2), true, "PROG PORT 1",
               true);
    check_line(2u, SERVO_SCREEN_NO_PIN, true, 0u, false, "PROG PORT 2", true);
    check_line(4u, 47u, true, 0u, false, "PROG PORT 4 GP47", true);
    /* The pulse off: the port is still the target, and renders nothing.
     * Port 4 on GP63 is the longest, 19 of the 27 cells the value
     * holds. */
    check_line(1u, 13u, false, PIN(2), true, "PORT 1 GP13 PWM OFF", true);
    check_line(1u, 63u, false, PIN(2), true, "PORT 1 GP63 PWM OFF", true);
    check_line(4u, 63u, false, PIN(2), true, "PORT 4 GP63 PWM OFF", true);
    check_line(1u, SERVO_SCREEN_NO_PIN, false, PIN(2), true,
               "PROG PORT 1 PWM OFF", true);
    /* Not known and no link name no pin and no port, pins bound or not;
     * a pin and a pulse passed with them are ignored. */
    check_line(SERVO_SCREEN_TARGET_UNKNOWN, 13u, true, PIN(2), true,
               "UNKNOWN", true);
    check_line(SERVO_SCREEN_TARGET_UNKNOWN, SERVO_SCREEN_NO_PIN, false, 0u,
               false, "UNKNOWN", true);
    check_line(SERVO_SCREEN_TARGET_NO_LINK, 13u, true, PIN(2), true,
               "NO LINK", true);
    check_line(SERVO_SCREEN_TARGET_NO_LINK, SERVO_SCREEN_NO_PIN, false, 0u,
               false, "NO LINK", true);
    ui_text_set_language(UI_LANG_DE);
    /* The protocol is a name: the same in German. */
    check_line(0u, SERVO_SCREEN_NO_PIN, false, PIN(2) | PIN(4) | PIN(5), true,
               "SERVO PWM GP2 GP4 GP5", false);
    check_line(0u, SERVO_SCREEN_NO_PIN, false, PINS_40_47, true,
               "SERVO PWM GP40 GP41 GP42 +5", false);
    check_line(0u, SERVO_SCREEN_NO_PIN, false, 0u, true,
               "KEIN SERVO-PWM-PIN", true);
    check_line(1u, 13u, true, PIN(2), true, "PROG-PORT 1 GP13", true);
    check_line(1u, SERVO_SCREEN_NO_PIN, true, PIN(2), true, "PROG-PORT 1",
               true);
    check_line(1u, 63u, false, PIN(2), true, "PORT 1 GP63 PWM AUS", true);
    check_line(3u, SERVO_SCREEN_NO_PIN, false, PIN(2), true,
               "PROG-PORT 3 PWM AUS", true);
    check_line(SERVO_SCREEN_TARGET_UNKNOWN, SERVO_SCREEN_NO_PIN, false,
               PIN(2), true, "UNBEKANNT", true);
    check_line(SERVO_SCREEN_TARGET_NO_LINK, SERVO_SCREEN_NO_PIN, false,
               PIN(2), true, "KEINE VERBINDUNG", true);
    ui_text_set_language(UI_LANG_EN);
    /* A short buffer is cut, not overrun; none is no line. */
    char small[8];
    memset(small, 'x', sizeof(small));
    CHECK(servo_target_line(small, sizeof(small), 1u, 13u, true, 0u, true));
    CHECK_STR_EQ(small, "PROG PO");
    memset(small, 'x', sizeof(small));
    CHECK(!servo_target_line(small, sizeof(small), 0u, SERVO_SCREEN_NO_PIN,
                             false, PINS_40_47, true));
    CHECK_STR_EQ(small, "SERVO P");
    CHECK(!servo_target_line(NULL, 8u, 1u, 13u, true, 0u, true));
    CHECK(!servo_target_line(small, 0u, 1u, 13u, true, 0u, true));
    CHECK_STR_EQ(small, "SERVO P");
}

/* A test or a sweep under way keeps the port's pulse: switched off, the run
 * would go on stepping and recording a servo that gets no pulse. */
TEST_CASE(the_port_pulse_is_not_switched_off_under_a_test_or_a_sweep)
{
    /* A sweep. */
    fresh();
    out_open_port();
    settings_set(SET_SERVO_TARGET, 1.0f);
    settings_set(SET_SERVO_PORT_PWM, 1.0f);
    servo_screen_set_armed(true);
    servo_screen_set_sweep(true);
    tap(SWEEP_X, BTN_Y);
    CHECK(servo_screen_sweeping());
    CHECK(servo_screen_port_pwm_locked());
    out_tap(OSEG_X(1), OSEG_PWM_Y);            /* PORT PWM OFF */
    CHECK(settings_get_bool(SET_SERVO_PORT_PWM));
    CHECK(servo_screen_sweeping());

    /* A test. */
    bench_fresh();
    out_open_port();
    settings_set(SET_SERVO_TARGET, 1.0f);
    settings_set(SET_SERVO_PORT_PWM, 1.0f);
    hold_start(2.3f);
    CHECK(servo_screen_testing());
    out_tap(OSEG_X(1), OSEG_PWM_Y);
    CHECK(settings_get_bool(SET_SERVO_PORT_PWM));
    CHECK(servo_screen_testing());
    /* The run over, armed still: the pulse goes off. */
    open_test();
    tap(START_X, START_Y);                     /* STOP */
    CHECK(!servo_screen_testing());
    out_tap(OSEG_X(1), OSEG_PWM_Y);
    CHECK(!settings_get_bool(SET_SERVO_PORT_PWM));
    settings_set(SET_SERVO_TARGET, 0.0f);
    settings_set(SET_SERVO_PORT_PWM, 1.0f);
}

static bool s_port_held;

static bool port_hold(setting_id_t id)
{
    return s_port_held
        && (id == SET_SERVO_TARGET || id == SET_SERVO_PORT_PWM);
}

/* While the programmer has the port, neither OUTPUTS' SERVO TESTER nor its
 * PORT PWM changes what the tester drives or the port's pulse -- the pulse
 * is not switched off either -- and a press begun before the hold takes no
 * choice on its release.  The TARGET row opens nothing. */
TEST_CASE(the_tester_target_and_pulse_stay_while_the_port_is_held)
{
    for (int pwm = 0; pwm < 2; ++pwm) {
        for (int target = 0; target < 2; ++target) {
            fresh();
            out_open_port();
            settings_set_hold(port_hold);
            s_port_held = false;
            settings_set(SET_SERVO_TARGET, (float)target);
            settings_set(SET_SERVO_PORT_PWM, (float)pwm);
            s_port_held = true;
            CHECK(servo_screen_target_locked());
            CHECK(servo_screen_tester_target_locked());
            CHECK(servo_screen_port_pwm_locked());
            char why[96];
            CHECK(outputs_screen_port_reason(why, sizeof(why)));
            CHECK_STR_EQ(why, TR(OUT_PORT_HELD));
            out_tap(OSEG_X(1 - target), OSEG_TESTER_Y);
            out_tap(OSEG_X(pwm), OSEG_PWM_Y);  /* the other value */
            CHECK_EQ(settings_get_int(SET_SERVO_TARGET), target);
            CHECK_EQ((int)settings_get_bool(SET_SERVO_PORT_PWM), pwm);
            /* A press begun before the port was taken. */
            s_port_held = false;
            out_ev(OSEG_X(1 - target), OSEG_TESTER_Y, TOUCH_EVENT_DOWN);
            s_port_held = true;
            out_ev(OSEG_X(1 - target), OSEG_TESTER_Y, TOUCH_EVENT_UP);
            CHECK_EQ(settings_get_int(SET_SERVO_TARGET), target);
            /* The hold over, both take their taps. */
            s_port_held = false;
            CHECK(!servo_screen_target_locked());
            /* The hold's line goes; another may stand in its place. */
            CHECK(!outputs_screen_port_reason(why, sizeof(why))
                  || strcmp(why, TR(OUT_PORT_HELD)) != 0);
            out_tap(OSEG_X(1), OSEG_TESTER_Y);
            CHECK_EQ(settings_get_int(SET_SERVO_TARGET), 1);
            out_tap(OSEG_X(pwm), OSEG_PWM_Y);
            CHECK_EQ((int)settings_get_bool(SET_SERVO_PORT_PWM), 1 - pwm);
            settings_set_hold(NULL);
        }
    }
}

int main(void)
{
    RUN(the_encoders_angle_replaces_the_dashes_in_the_measured_row);
    RUN(an_enabled_encoder_without_a_reading_does_not_fall_back_to_the_feedback);
    RUN(outputs_prog_port_entry_sets_what_the_tester_drives_at_rest);
    RUN(the_servo_screen_names_the_pins_the_tester_drives);
    RUN(the_target_line_lists_the_pins_that_fit_and_counts_the_rest);
    RUN(the_target_row_stays_in_sight_under_the_settings_and_a_keypad);
    RUN(the_port_pulse_is_not_switched_off_under_a_test_or_a_sweep);
    RUN(the_tester_target_and_pulse_stay_while_the_port_is_held);
    return test_summary("servo_target");
}
