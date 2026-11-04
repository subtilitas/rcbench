/**
 * @file
 * @brief The SERVO workbench's RUDDER task: the control surface's
 *        deflection in ° and mm, LENGTH, the buttons that drive 1000us,
 *        CENTRE and 2000us, ZERO, the MAX of each side, the angle
 *        source's state line, and the task's guide pages.
 *
 * See servo_rudder_int.h for the layout.  The angle is the one the screen
 * takes in (servo_screen_encoder(), enc_shown(), enc_deg_now()), in the
 * direction of the commanded angle as LIVE's MEASURED reading is; the zero
 * and the MAX are kept in the source's own sign, so REVERSE flips what is
 * shown and keeps the zero.  The zero is taken from the source's count 0,
 * not from ENC CENTRE, so a new ENC CENTRE moves no deflection after ZERO.  The buttons post as the output card's CENTRE
 * does, through the screen's own seam: the output, ARM and the TARGET row
 * that names it are the output card's, the same on every task.
 *
 * SPDX-License-Identifier: MIT
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "settings.h"
#include "ui_help.h"
#include "ui_keypad.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

/* Last: its layout macros have short names (W, H, PAD), which no header
 * included after it may meet. */
#include "servo_screen_core_int.h"

/** @brief The card's inner column: from x 18, 500px wide. */
#define RUD_X      (PAD + 10)
#define RUD_W      500                    /**< Its width, px. */
#define RUD_MM_X   (RUD_X + 250)          /**< The mm reading's column. */
#define RUD_LBL_Y  (CARD_Y + 8)           /**< The readings' labels. */
#define RUD_NUM_Y  (CARD_Y + 30)          /**< Their numerals, 44px. */
#define RUD_MAX_Y  (CARD_Y + 82)          /**< The MAX lines. */
#define RUD_LEN_Y  (CARD_Y + 112)         /**< LENGTH's row. */
#define RUD_BTN_Y  (CARD_Y + 172)         /**< 1000us, CENTRE, 2000us. */
#define RUD_BTN_W  160                    /**< One of them, px. */
#define RUD_GAP    10                     /**< Between two, px. */
#define RUD_ZERO_Y (CARD_Y + 232)         /**< ZERO and RESET MAX. */
#define RUD_ZERO_W 245                    /**< One of them, px. */
#define RUD_ST_Y   (CARD_Y + 292)         /**< The first state line. */
#define RUD_ST_P   24                     /**< The state lines' pitch. */
#define RUD_LEN_VAL_W 200                 /**< LENGTH's value, px. */

_Static_assert(3 * RUD_BTN_W + 2 * RUD_GAP == RUD_W,
               "the three buttons fill the column");
_Static_assert(2 * RUD_ZERO_W + RUD_GAP == RUD_W,
               "ZERO and RESET MAX fill the column");
_Static_assert(RUD_X + RUD_W <= PAD + LCARD_W - 10,
               "the column ends inside the card");
_Static_assert(RUD_ST_Y + RUD_LINES * RUD_ST_P <= STOP_FLOOR,
               "the state lines end above the alert band");
_Static_assert(RUD_NUM_Y + READ_NUM_H <= RUD_MAX_Y - 4,
               "the numerals end above the MAX lines");

/* ------------------------------------------------------------ the state */

/* The rest of `rud` is the zeroes reset() cleared the state to: no press,
 * and buffers that have drawn no card yet repaint it whole. */
void rudder_init(void)
{
    servo_rudder_init(&s.rud.geo);
}

/**
 * @brief The source's angle now, in its own sign, from its count 0: what
 *        ZERO takes, so ENC CENTRE has no part in a deflection after it.
 * @return degrees, -180 up to 180
 */
static float src_abs_now(void)
{
    return servo_test_enc_deg(s.enc_raw, 0u);
}

/**
 * @brief The deflection now, in the source's own sign: from the zero once
 *        taken, else the angle from the source's centre (enc_deg_now()
 *        without REVERSE).
 * @return degrees
 */
static float src_defl_now(void)
{
    if (s.rud.geo.zeroed) {
        return servo_rudder_defl(&s.rud.geo, src_abs_now());
    }
    const float d = enc_deg_now();
    return s.reverse ? -d : d;
}

/**
 * @brief The source in force as rudder_t's geo_src keeps it: the AS5600,
 *        or the IMU with its AXIS and SIGN.
 * @return 1 the AS5600, 2 + 2 x AXIS + SIGN the IMU
 */
static uint32_t src_key(void)
{
    if (enc_src() != SERVO_ANGLE_IMU) {
        return 1u;
    }
    /* A new IMU zero moves every IMU angle: it is another source too. */
    return 2u + 2u * (s.imu_axis & 3u) + (s.imu_neg ? 1u : 0u)
           + 16u * (s.imu_zero_gen & 0x0FFFFFFFu);
}

/**
 * @brief The zero and the MAX dropped when the source in force is not the
 *        one they were taken from: another sensor's counts, or the IMU's
 *        other AXIS or SIGN, have no relation to them.
 */
static void geo_follow_source(void)
{
    const uint32_t k = src_key();
    if (s.rud.geo_src != k) {
        servo_rudder_init(&s.rud.geo);
        s.rud.geo_src = k;
    }
}

void rudder_angle(void)
{
    geo_follow_source();
    if (!enc_shown()) {
        return;
    }
    servo_rudder_track(&s.rud.geo, src_defl_now());
}

const servo_rudder_t *rudder_geo(void)
{
    geo_follow_source();
    return &s.rud.geo;
}

bool rudder_shown(void)
{
    return wb_task() == UI_WB_RUDDER && !modal_open() && !prog_shown();
}

rudder_src_t rudder_source(void)
{
    /* SETUP's AS5600 switch holds for the AS5600 alone: the IMU's angle
     * comes in valid only while the IMU is on and connected. */
    if (enc_src() == SERVO_ANGLE_AS5600 && !settings_get_bool(SET_ENC_EN)) {
        return RUD_SRC_OFF;
    }
    if (!s.link_up) {
        return RUD_SRC_NO_LINK;
    }
    if (s.enc_no_magnet) {
        return RUD_SRC_NO_MAGNET;
    }
    return enc_shown() ? RUD_SRC_LIVE : RUD_SRC_STALE;
}

bool rudder_defl(float *deg)
{
    geo_follow_source();
    if (deg == NULL || !enc_shown()) {
        return false;
    }
    const float d = src_defl_now();
    *deg = s.reverse ? -d : d;
    return true;
}

/**
 * @brief The MAX of the side shown as @p k, in the commanded direction.
 * @param k 0 the negative side as shown, 1 the positive
 * @param mag its magnitude, degrees
 * @return false while that side has none
 */
static bool max_shown(int k, float *mag)
{
    const int side = s.reverse ? 1 - k : k;
    if (!s.rud.geo.have[side]) {
        return false;
    }
    *mag = s.rud.geo.max_deg[side];
    return true;
}

/**
 * @brief LENGTH in force, mm.
 * @return the setting; 0 while it is not set
 */
static float len_now(void)
{
    const float l = settings_get(SET_RUDDER_LEN_MM);
    return (l >= SERVO_RUDDER_LEN_MIN_MM) ? l : 0.0f;
}

/* ----------------------------------------------------------- the layout */

gfx_rect_t rudder_rect(rudder_ctl_t k)
{
    switch (k) {
    case RUD_C_LENGTH:
        return (gfx_rect_t){ RUD_X, RUD_LEN_Y, RUD_W, UI_TAP_MIN };
    case RUD_C_1000:
    case RUD_C_CENTRE:
    case RUD_C_2000: {
        const int i = (int)k - (int)RUD_C_1000;
        return (gfx_rect_t){ (int16_t)(RUD_X + i * (RUD_BTN_W + RUD_GAP)),
                             RUD_BTN_Y, RUD_BTN_W, UI_TAP_MIN };
    }
    case RUD_C_ZERO:
    case RUD_C_RESET: {
        const int i = (int)k - (int)RUD_C_ZERO;
        return (gfx_rect_t){ (int16_t)(RUD_X + i * (RUD_ZERO_W + RUD_GAP)),
                             RUD_ZERO_Y, RUD_ZERO_W, UI_TAP_MIN };
    }
    default:
        return (gfx_rect_t){ 0, 0, 0, 0 };
    }
}

gfx_rect_t rudder_readout_rect(void)
{
    return (gfx_rect_t){ RUD_X, (int16_t)(RUD_LBL_Y - 2), RUD_W,
                         (int16_t)(RUD_MAX_Y + 24 - RUD_LBL_Y + 2) };
}

gfx_rect_t rudder_state_rect(void)
{
    return (gfx_rect_t){ RUD_X, (int16_t)(RUD_ST_Y - 2), RUD_W,
                         (int16_t)(RUD_LINES * RUD_ST_P + 2) };
}

/**
 * @brief LENGTH's value box, right in its row.
 * @return the box
 */
static gfx_rect_t len_value_rect(void)
{
    return (gfx_rect_t){ (int16_t)(RUD_X + RUD_W - RUD_LEN_VAL_W), RUD_LEN_Y,
                         RUD_LEN_VAL_W, UI_TAP_MIN };
}

/* -------------------------------------------------------- the presses */

/**
 * @brief 1000us, CENTRE or 2000us: a position, refused while the value is
 *        not live, as the output card's CENTRE is.  It takes the horn from
 *        a run and a sweep.  1000us and 2000us are angles of the profile
 *        in force, held to TRAVEL and the pulse range; the PULSE line says
 *        the pulse driven.
 * @param k the button
 */
static void drive(rudder_ctl_t k)
{
    if (!value_live()) {
        return;
    }
    test_end_now(SERVO_TEST_AB_OPERATOR);
    stop_sweep();
    if (k == RUD_C_CENTRE) {
        s.commanded_deg = 0.0f;
        post(SERVO_CMD_CENTRE, svs_deg_to_us(0.0f));
        ++s.dial_rev;
    } else {
        (void)svs_command(us_to_deg((k == RUD_C_1000) ? 1000u : 2000u));
    }
    ++s.ctrl_rev;
}

/**
 * @brief LENGTH's keypad over the left column: 0 or 10 to 500mm, its OK
 *        taken by the settings' keypad rule (keypad_done()).
 */
static void open_length(void)
{
    const setting_def_t *d = settings_def(SET_RUDDER_LEN_MM);
    ui_keypad_open(&s.kp, overlay_area(), TR(RUD_LENGTH), d->unit,
                   settings_get(SET_RUDDER_LEN_MM), d->min, d->max,
                   decimals_of(SET_RUDDER_LEN_MM));
    s.kp_target  = KT_SETTING;
    s.kp_setting = SET_RUDDER_LEN_MM;
}

/**
 * @brief A tap released on @p k: LENGTH's keypad, ZERO (taken only from a
 *        reading on show), RESET MAX.
 * @param k the control
 */
static void act(rudder_ctl_t k)
{
    geo_follow_source();
    switch (k) {
    case RUD_C_LENGTH:
        open_length();
        break;
    case RUD_C_ZERO:
        if (enc_shown()) {
            servo_rudder_zero(&s.rud.geo, src_abs_now());
        }
        break;
    case RUD_C_RESET:
        servo_rudder_reset_max(&s.rud.geo);
        break;
    default:
        break;
    }
}

void rudder_down(const touch_event_t *evt)
{
    if (evt == NULL || !rudder_shown()) {
        return;
    }
    for (int k = RUD_C_LENGTH; k < RUD_C_COUNT; ++k) {
        if (!gfx_rect_contains(rudder_rect((rudder_ctl_t)k), evt->point.x,
                               evt->point.y)) {
            continue;
        }
        if (k == RUD_C_1000 || k == RUD_C_CENTRE || k == RUD_C_2000) {
            drive((rudder_ctl_t)k);
        } else if (s.rud.pressed == RUD_C_NONE) {
            s.rud.pressed  = (uint8_t)k;
            s.rud.press_id = evt->point.id;
            ++s.ctrl_rev;
        }
        return;
    }
}

bool rudder_rest(const touch_event_t *evt)
{
    if (evt == NULL || s.rud.pressed == RUD_C_NONE
        || evt->point.id != s.rud.press_id) {
        return false;
    }
    if (evt->type == TOUCH_EVENT_UP) {
        const rudder_ctl_t k = (rudder_ctl_t)s.rud.pressed;
        s.rud.pressed = RUD_C_NONE;
        ++s.ctrl_rev;
        if (touch_event_is_tap_up(evt) && rudder_shown()
            && gfx_rect_contains(rudder_rect(k), evt->point.x,
                                 evt->point.y)) {
            act(k);
        }
    }
    return true;
}

void rudder_cancel(void)
{
    if (s.rud.pressed != RUD_C_NONE) {
        s.rud.pressed = RUD_C_NONE;
        ++s.ctrl_rev;
    }
}

/* ------------------------------------------------------------ the texts */

/**
 * @brief A deflection's text: signed tenths, or the dashes.
 * @param buf where it goes
 * @param n its size
 * @param have a value is on show
 * @param v the value
 */
static void num_text(char *buf, size_t n, bool have, float v)
{
    if (have) {
        snprintf(buf, n, "%+.1f", (double)v);
    } else {
        snprintf(buf, n, "---");
    }
}

/**
 * @brief A MAX line: MAX, then the negative side and the positive, in °
 *        or with LENGTH in mm.
 * @param buf where it goes
 * @param n its size
 * @param mm the line of the mm reading
 * @return @p buf
 */
static const char *max_line(char *buf, size_t n, bool mm)
{
    char side[2][20];
    for (int k = 0; k < 2; ++k) {
        float mag = 0.0f;
        if (!max_shown(k, &mag)) {
            snprintf(side[k], sizeof(side[k]), "---");
            continue;
        }
        const float v = mm ? servo_rudder_mm(mag, len_now()) : mag;
        snprintf(side[k], sizeof(side[k]), "%c%.1f%s", (k == 0) ? '-' : '+',
                 (double)v, mm ? "mm" : "°");
    }
    snprintf(buf, n, "%s %s  %s", TR(RUD_MAX), side[0], side[1]);
    return buf;
}

const char *rudder_line(int i, char *buf, size_t n)
{
    if (buf == NULL || n == 0u) {
        return "";
    }
    buf[0] = '\0';
    geo_follow_source();
    switch (i) {
    case 0: {
        char name[12];
        (void)enc_name(name, sizeof(name));
        switch (rudder_source()) {
        case RUD_SRC_OFF:
            snprintf(buf, n, TR(RUD_SRC_OFF), name);
            break;
        case RUD_SRC_NO_LINK:
            snprintf(buf, n, "%s", TR(RUD_SRC_NO_LINK));
            break;
        case RUD_SRC_NO_MAGNET:
            snprintf(buf, n, TR(RUD_SRC_NO_MAGNET), name);
            break;
        case RUD_SRC_STALE:
            snprintf(buf, n, TR(RUD_SRC_STALE), name);
            break;
        default:
            snprintf(buf, n, TR(RUD_SRC_LIVE), name);
            break;
        }
        break;
    }
    case 1:
        snprintf(buf, n, TR(RUD_PULSE), servo_screen_commanded_text());
        break;
    case 2:
        if (!s.rud.geo.zeroed) {
            snprintf(buf, n, "%s", TR(RUD_NOT_ZEROED));
        }
        break;
    case 3:
        if (!value_live()) {
            snprintf(buf, n, "%s", ui_servo_str(SERVO_STR_START_NOT_ARMED));
        }
        break;
    default:
        break;
    }
    return buf;
}

/* ---------------------------------------------------------- the drawing */

/**
 * @brief The numerals of a reading in the 32 x 44 face, then the unit in
 *        the 20 x 36 face on their foot.  -500.0mm, the widest reading,
 *        takes 236px of the column's 250 (test_rudder_layout).
 * @param c the canvas
 * @param x the column's left edge
 * @param t the numerals
 * @param unit the unit
 * @param col their colour
 */
static void reading(gfx_canvas_t *c, int x, const char *t, const char *unit,
                    gfx_color_t col)
{
    const int w = gfx_text_width(SV_FONT_NUM, t, 1);
    gfx_text(c, x, RUD_NUM_Y, t, SV_FONT_NUM, col, 1);
    gfx_text(c, x + w + 4, RUD_NUM_Y + READ_NUM_H - SV_FONT_HEAD->height,
             unit, SV_FONT_HEAD, ui_theme_color(UI_C_TEXT_DIM), 1);
}

/**
 * @brief The readout: DEFLECTION in °, and with LENGTH the trailing edge's
 *        travel in mm, else the line that asks for LENGTH; the MAX of each
 *        side under each.
 * @param c the canvas
 */
static void draw_readout(gfx_canvas_t *c)
{
    const gfx_rect_t r = rudder_readout_rect();
    gfx_fill_rect(c, r.x, r.y, r.w, r.h, ui_theme_color(UI_C_PANEL));
    float defl = 0.0f;
    const bool have = rudder_defl(&defl);
    const gfx_color_t num = have ? ui_theme_color(UI_C_ACCENT)
                                 : ui_theme_color(UI_C_TEXT_DIM);
    char t[24], line[64];
    gfx_text(c, RUD_X, RUD_LBL_Y, TR(RUD_DEFL), SV_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_DIM), 1);
    num_text(t, sizeof(t), have, defl);
    reading(c, RUD_X, t, "°", num);
    gfx_text(c, RUD_X, RUD_MAX_Y, max_line(line, sizeof(line), false),
             SV_FONT_LABEL, ui_theme_color(UI_C_TEXT), 1);
    const float len = len_now();
    if (len <= 0.0f) {
        gfx_text(c, RUD_MM_X, RUD_NUM_Y + 12, TR(RUD_NO_LENGTH),
                 SV_FONT_LABEL, ui_theme_color(UI_C_TEXT_DIM), 1);
        return;
    }
    gfx_text(c, RUD_MM_X, RUD_LBL_Y, TR(RUD_EDGE), SV_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_DIM), 1);
    num_text(t, sizeof(t), have, servo_rudder_mm(defl, len));
    reading(c, RUD_MM_X, t, "mm", num);
    gfx_text(c, RUD_MM_X, RUD_MAX_Y, max_line(line, sizeof(line), true),
             SV_FONT_LABEL, ui_theme_color(UI_C_TEXT), 1);
}

/**
 * @brief The state lines, one under the other, those with nothing to say
 *        left out: the source, in the warning colour while it gives no
 *        reading; the pulse; the zero not taken; ARM FIRST while SAFE, in
 *        the warning colour.
 * @param c the canvas
 */
static void draw_state(gfx_canvas_t *c)
{
    const gfx_rect_t r = rudder_state_rect();
    gfx_fill_rect(c, r.x, r.y, r.w, r.h, ui_theme_color(UI_C_PANEL));
    int y = RUD_ST_Y;
    for (int i = 0; i < RUD_LINES; ++i) {
        char line[64];
        if (rudder_line(i, line, sizeof(line))[0] == '\0') {
            continue;
        }
        const bool warn = (i == 0 && rudder_source() != RUD_SRC_LIVE)
                          || i == 3;
        gfx_text_in(c, (gfx_rect_t){ RUD_X, (int16_t)y, RUD_W, 20 },
                    line, SV_FONT_LABEL,
                    warn ? ui_theme_color(UI_C_WARN)
                         : ui_theme_color(UI_C_TEXT_DIM),
                    1, GFX_ALIGN_LEFT);
        y += RUD_ST_P;
    }
}

/**
 * @brief LENGTH's row: its label, and its value as the settings' rows draw
 *        one.
 * @param c the canvas
 */
static void draw_length(gfx_canvas_t *c)
{
    const gfx_rect_t rr = rudder_rect(RUD_C_LENGTH);
    gfx_text(c, rr.x, rr.y + 14, TR(RUD_LENGTH), SV_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_DIM), 1);
    char v[24];
    const float len = len_now();
    if (len > 0.0f) {
        snprintf(v, sizeof(v), "%.0fmm", (double)len);
    } else {
        snprintf(v, sizeof(v), "%s", TR(RUD_LEN_NOT_SET));
    }
    ui_button_font(c, len_value_rect(), v, ui_theme_color(UI_C_PANEL_SUNK),
                   s.rud.pressed == RUD_C_LENGTH, true, SV_FONT_LABEL);
}

/**
 * @brief The buttons: 1000us, CENTRE and 2000us dimmed while the value is
 *        not live; ZERO dimmed without a reading.
 * @param c the canvas
 */
static void draw_buttons(gfx_canvas_t *c)
{
    static const char *const k_us[2] = { "1000us", "2000us" };
    const gfx_color_t fill = ui_theme_color(UI_C_PANEL_HI);
    const bool live = value_live();
    ui_button_font(c, rudder_rect(RUD_C_1000), k_us[0], fill, false, live,
                   SV_FONT_LABEL);
    ui_button_font(c, rudder_rect(RUD_C_CENTRE), TR(SV_CENTRE_BTN), fill,
                   false, live, SV_FONT_LABEL);
    ui_button_font(c, rudder_rect(RUD_C_2000), k_us[1], fill, false, live,
                   SV_FONT_LABEL);
    ui_button_font(c, rudder_rect(RUD_C_ZERO), TR(RUD_ZERO), fill,
                   s.rud.pressed == RUD_C_ZERO, enc_shown(), SV_FONT_LABEL);
    ui_button_font(c, rudder_rect(RUD_C_RESET), TR(RUD_RESET_MAX), fill,
                   s.rud.pressed == RUD_C_RESET, true, SV_FONT_LABEL);
}

/**
 * @brief An FNV-1a hash of @p n words.
 * @param v the words
 * @param n their count
 * @return the hash
 */
static uint32_t hash_of(const uint32_t *v, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; ++i) {
        h = (h ^ v[i]) * 16777619u;
    }
    return h;
}

/**
 * @brief One number for what the controls show: a change repaints the
 *        card whole.  A change of language or theme is the router's,
 *        which repaints every screen (ui_router_invalidate()).
 * @return the number
 */
static uint32_t card_sig(void)
{
    const uint32_t v[] = { value_live() ? 1u : 0u, enc_shown() ? 1u : 0u,
                           (uint32_t)lroundf(len_now()), s.rud.pressed };
    return hash_of(v, sizeof(v) / sizeof(v[0]));
}

/**
 * @brief One number for what the readout and the state lines show, at the
 *        tenths they are drawn in: a change repaints them alone.
 * @return the number
 */
static uint32_t read_sig(void)
{
    float defl = 0.0f;
    const bool have = rudder_defl(&defl);
    float m0 = 0.0f, m1 = 0.0f;
    const bool h0 = max_shown(0, &m0), h1 = max_shown(1, &m1);
    const float len = len_now();
    const uint32_t v[] = {
        have ? 1u : 0u,
        have ? (uint32_t)lroundf(defl * 10.0f) : 0u,
        have ? (uint32_t)lroundf(servo_rudder_mm(defl, len_now()) * 10.0f)
             : 0u,
        h0 ? (uint32_t)lroundf(m0 * 10.0f) + 1u : 0u,
        h1 ? (uint32_t)lroundf(m1 * 10.0f) + 1u : 0u,
        /* The MAX in mm too: two maxima a tenth of a degree alike can
         * stand a tenth of a mm apart at 500mm. */
        h0 ? (uint32_t)lroundf(servo_rudder_mm(m0, len) * 10.0f) : 0u,
        h1 ? (uint32_t)lroundf(servo_rudder_mm(m1, len) * 10.0f) : 0u,
        (uint32_t)rudder_source(),
        s.rud.geo.zeroed ? 1u : 0u,
        (uint32_t)svs_deg_to_us(s.commanded_deg),
        prog_holds() ? 1u : 0u,
        card_sig(),
    };
    return hash_of(v, sizeof(v) / sizeof(v[0]));
}

void rudder_render(gfx_canvas_t *c, int buf)
{
    geo_follow_source();
    const gfx_rect_t a = page_area();
    gfx_fill_rect(c, a.x + 1, a.y + 1, a.w - 2, a.h - 2,
                  ui_theme_color(UI_C_PANEL));
    draw_readout(c);
    draw_length(c);
    draw_buttons(c);
    draw_state(c);
    s.rud.drawn_card[buf & 1] = card_sig();
    s.rud.drawn_read[buf & 1] = read_sig();
}

/**
 * @brief @p fn drawn into @p c clipped to @p r.
 * @param c the canvas
 * @param r the clip
 * @param fn what draws
 */
static void clipped_to(gfx_canvas_t *c, gfx_rect_t r,
                       void (*fn)(gfx_canvas_t *))
{
    const gfx_rect_t old = c->clip;
    if (gfx_clip_set(c, r)) {
        fn(c);
    }
    c->clip = old;
}

void rudder_render_part(gfx_canvas_t *c, int buf)
{
    geo_follow_source();
    const int b = buf & 1;
    if (s.rud.drawn_card[b] != card_sig()) {
        const gfx_rect_t old = c->clip;
        if (gfx_clip_set(c, page_area())) {
            rudder_render(c, b);
        }
        c->clip = old;
        return;
    }
    const uint32_t sig = read_sig();
    if (s.rud.drawn_read[b] != sig) {
        clipped_to(c, rudder_readout_rect(), draw_readout);
        clipped_to(c, rudder_state_rect(), draw_state);
        s.rud.drawn_read[b] = sig;
    }
}

/* ------------------------------------------------------------ the guide */

int rudder_help_count(void)
{
    return rudder_shown() ? 1 : 0;
}

/*
 * One page, the output card's: the dock on the left over RUDDER's card,
 * the TARGET row and ARM in the live half, ARMED too.  A page of the card
 * itself would take the dock on the right, whose live half ends at x 528
 * and cuts the strip's SETTINGS tab (505 to 625), and its badges would sit
 * on the controls' labels at x 18: the card is the web help's
 * (servo-rudder.md).
 */
bool rudder_help_page(int i, ui_help_page_t *out)
{
    if (out == NULL || i < 0 || i >= rudder_help_count()) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    const gfx_rect_t target = { RC_X, TARGET_Y, RC_W, TARGET_H };
    out->title     = TX_HG_RUD_TITLE_OUT;
    out->side      = UI_HELP_DOCK_LEFT;
    out->armed_ok  = true;
    out->must_show = target;
    out->item[out->count++] = (ui_help_item_t){ TR(SV_TARGET),
                                                TX_HG_RUD_TARGET, target };
    out->item[out->count++] = (ui_help_item_t){ "ARM", TX_HG_RUD_ARM,
                                                s.arm_btn };
    return true;
}
