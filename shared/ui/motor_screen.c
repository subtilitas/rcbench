/*
 * The motor and ESC (electronic speed controller) bench screen.
 *
 * SPDX-License-Identifier: MIT
 */

#include "motor_screen.h"

#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "ui_hero.h"
#include "ui_plot.h"
#include "settings.h"
#include "ui_slider.h"
#include "ui_tabs.h"
#include "ui_theme.h"
#include "ui_widgets.h"

#define W 800
#define H (480 - UI_BAND_H)   /* the router owns the band */

/* Horizontal bands, not columns: seventeen full-height vertical lines cost
 * about 8,160 cache-line fills on this panel, and the same pixels drawn as
 * horizontal lines cost none. */
#define PAD       6                    /* the screen's outer margin */
#define INNER     6                    /* a panel's own padding */

/*
 * Two columns.  The plot and the throttle take the left, the readouts and
 * the controls take a rail on the right, so a glance at the numbers and a
 * hand on the throttle do not compete for the same part of the screen.
 */
#define COL_GAP   6
#define LEFT_X    PAD
#define LEFT_W    546
#define RIGHT_X   (LEFT_X + LEFT_W + COL_GAP)
#define RIGHT_W   (W - PAD - RIGHT_X)

/* The strip above both columns: what is driving the outputs, and how hot. */
#define HDR_Y     2
#define HDR_H     18

/* Upper row: the plot on the left, the four readouts stacked on the right. */
#define UP_Y      24
#define UP_H      242
#define HERO_H    ((UP_H - 3 * HERO_GAP) / 4)
#define HERO_GAP  4

/* Lower row: the throttle on the left, arming and the totals on the right. */
#define LO_Y      (UP_Y + UP_H + 6)
#define LO_H      (H - LO_Y - PAD)

/* Inside the telemetry panel: its title strip, the legend, then the plot. */
#define TITLE_H   20
#define LEG_Y     (UP_Y + TITLE_H + 4)
#define LEG_H     30
#define PLOT_X    (LEFT_X + INNER)
#define PLOT_W    (LEFT_W - 2 * INNER)
#define PLOT_Y    (LEG_Y + LEG_H + 4)
#define PLOT_H    (UP_Y + UP_H - INNER - PLOT_Y)

/* Inside the throttle panel: the readout shares the title strip, then the
 * track with a fine adjustment at each end. */
#define ROW_Y     (LO_Y + TITLE_H + 2)
#define ROW_H     34
#define NUDGE_W   54
#define CTRL_Y    (ROW_Y + 42)
#define TRACK_H   40
#define TRACK_X   (LEFT_X + INNER + NUDGE_W + 6)
#define TRACK_W   (LEFT_W - 2 * INNER - 2 * (NUDGE_W + 6))
#define HINT_Y    (CTRL_Y + TRACK_H + 14)

/* The readout's box: from the panel's left edge to where the numerals end.
 * The hero numerals are 32 px tall with 3 px of slant on a 34 px row. */
#define READOUT_W (LEFT_W - 2 * INNER)
#define READOUT_H (CTRL_Y - ROW_Y - 4)

/* Inside the control panel. */
#define BTN_H     36
#define ARM_Y     (LO_Y + TITLE_H + 8)
#define TOTALS_Y  (ARM_Y + BTN_H + 10)
#define RESET_Y   (LO_H + LO_Y - PAD - BTN_H)

/*
 * ARM says what it is about to do while the finger is on it: 350 ms from the
 * OK green to the danger red, held there while the press continues.  The
 * command still goes on release, so the fade is a warning and not a
 * hold-to-arm; a release before it completes arms the bench just the same.
 *
 * Arming then flashes the button once, for 180 ms, because the label and the
 * colour both settle into a steady state that says "armed" and neither marks
 * the instant it became true.
 */
#define ARM_FADE_S  0.35f
#define ARM_FLASH_S 0.18f

/* The tabs move into the header strip: the mockup has no tab row, and the
 * table pane is not worth deleting to match it. */
#define TAB_Y     HDR_Y
#define TAB_H     HDR_H
#define TAB_W     124

enum { S_VOLT = 0, S_CURR, S_POWER, S_RPM, S_COUNT };

static const ui_plot_series_t k_series[S_COUNT] = {
    { "VOLT", "V",   0, 2, 5.0f },
    { "CURR", "A",   0, 1, 1.0f },
    { "PWR",  "W",   0, 0, 50.0f },
    { "RPM",  "RPM", 0, 0, 1000.0f },
};

/* Voltage's interesting extreme is the minimum -- what the pack does under
 * load is how far down it goes -- and every other channel's is the maximum. */
static const char *const k_extreme[S_COUNT] = { "min", "pk", "pk", "pk" };

/*
 * The panel polls at 20 Hz, one sample per plot column, so the time base is
 * the plot width in samples: PLOT_W / 20 s.
 */
#define SAMPLE_HZ 20.0f

static const char *const k_tab_labels[] = { "PLOT", "TABLE" };

static struct {
    ui_plot_t     plot;
    ui_slider_t   slider;
    ui_tabs_t     tabs;
    bench_state_t bench;
    bool          armed;
    motor_cmd_t   pending;
    unsigned      drawn_mask;
    /* The push count last painted into each framebuffer; UINT32_MAX
     * means "no paint recorded", which is not a count push can reach. */
    uint32_t      drawn_push[2];
    /* Bumped by anything that changes a button's or the tab row's
     * appearance.  The throttle carries its own, because a drag moves it on
     * every frame and repainting the buttons with it costs the row's full
     * 800 x 90 px rather than the readout's 396 x 30. */
    uint32_t      ctrl_rev;
    uint32_t      drawn_ctrl[2];
    uint32_t      thr_rev;
    uint32_t      drawn_thr[2];
    /* ARM animates, so it gets a third counter and repaints its own rect. */
    uint32_t      arm_rev;
    uint32_t      drawn_arm[2];
    int           esc_kv;        /**< as reported by the ESC, 0 if it does not */
    float         arm_held_s;    /**< how long the ARM press has been held */
    float         arm_flash_s;   /**< what is left of the flash on arming  */
    gfx_rect_t    arm_rect;
    gfx_rect_t    reset_rect;
    gfx_rect_t    down_rect;    /**< one percentage point down */
    gfx_rect_t    up_rect;      /**< and one up                */
    int           pressed;      /**< 0 none, 1 arm, 2 reset, 3 -1, 4 +1 */
    uint8_t       press_id;
    bool          have_press;
} s;

void motor_invalidate(void)
{
    s.drawn_mask = 0;
    s.drawn_push[0] = UINT32_MAX;
    s.drawn_push[1] = UINT32_MAX;
    s.drawn_ctrl[0] = UINT32_MAX;
    s.drawn_ctrl[1] = UINT32_MAX;
    s.drawn_thr[0]  = UINT32_MAX;
    s.drawn_thr[1]  = UINT32_MAX;
    s.drawn_arm[0]  = UINT32_MAX;
    s.drawn_arm[1]  = UINT32_MAX;
}

/* The palette is not a compile-time constant -- it changes with the theme --
 * so the series colours are bound here rather than in the table above. */
static void bind_colours(void)
{
    s.plot.series[S_VOLT].color  = ui_theme_color(UI_C_VOLT);
    s.plot.series[S_CURR].color  = ui_theme_color(UI_C_CURR);
    s.plot.series[S_POWER].color = ui_theme_color(UI_C_POWER);
    s.plot.series[S_RPM].color   = ui_theme_color(UI_C_RPM);
    s.slider.color = ui_theme_color(UI_C_ACCENT);
}

static void reset(void)
{
    memset(&s, 0, sizeof(s));
    /* Not zero: a zeroed drawn_* must not read as "already drawn revision 0",
     * or the first frame after a reset skips what it is meant to paint. */
    s.drawn_push[0] = UINT32_MAX;
    s.drawn_push[1] = UINT32_MAX;
    s.drawn_ctrl[0] = UINT32_MAX;
    s.drawn_ctrl[1] = UINT32_MAX;
    s.drawn_thr[0]  = UINT32_MAX;
    s.drawn_thr[1]  = UINT32_MAX;
    s.drawn_arm[0]  = UINT32_MAX;
    s.drawn_arm[1]  = UINT32_MAX;
    ui_plot_init(&s.plot, k_series, S_COUNT,
                 (float)PLOT_W / SAMPLE_HZ);
    ui_tabs_init(&s.tabs, k_tab_labels, MOTOR_PANE_COUNT,
                 (gfx_rect_t){ LEFT_X, TAB_Y, TAB_W, TAB_H });

    const gfx_rect_t track = { TRACK_X, CTRL_Y, TRACK_W, TRACK_H };
    ui_slider_init(&s.slider, track, 0.0f, 100.0f, 0);
    ui_slider_set_ticks(&s.slider, 4);   /* tick marks at the quarters */

    /*
     * A percentage point at each end of the track.  The presets the widget
     * can draw are not used: a row of them under the throttle is one more
     * thing to hit by accident on a bench that is about to spin something.
     */
    s.down_rect = (gfx_rect_t){ (int16_t)(LEFT_X + INNER), CTRL_Y,
                                NUDGE_W, TRACK_H };
    s.up_rect   = (gfx_rect_t){ (int16_t)(TRACK_X + TRACK_W + 6), CTRL_Y,
                                NUDGE_W, TRACK_H };

    s.arm_rect   = (gfx_rect_t){ (int16_t)(RIGHT_X + INNER), ARM_Y,
                                 (int16_t)(RIGHT_W - 2 * INNER), BTN_H };
    s.reset_rect = (gfx_rect_t){ (int16_t)(RIGHT_X + INNER), RESET_Y,
                                 (int16_t)(RIGHT_W - 2 * INNER), BTN_H };
    bind_colours();
}

/*
 * One slot, coalescing.  The pending command is overwritten rather than
 * queued: the application drains it every frame, and the latest throttle
 * position is the one that matters.
 *
 * Exception: a pending DISARM survives everything.  Two taps inside one
 * drain must not let an ARM land on top of a DISARM and re-arm a bench that
 * was stopped a moment before.
 */
static void post(motor_cmd_kind_t kind, float value)
{
    if (s.pending.kind == MOTOR_CMD_DISARM && kind != MOTOR_CMD_DISARM) {
        return;
    }
    s.pending.kind  = kind;
    s.pending.value = value;
}

bool motor_screen_poll_cmd(motor_cmd_t *out)
{
    if (s.pending.kind == MOTOR_CMD_NONE) {
        return false;
    }
    if (out != NULL) {
        *out = s.pending;
    }
    s.pending.kind = MOTOR_CMD_NONE;
    return true;
}

void motor_screen_set(const bench_state_t *b)
{
    if (b != NULL) {
        s.bench = *b;
    }
}

void motor_screen_push(const bench_state_t *b)
{
    if (b == NULL) {
        return;
    }
    s.bench = *b;
    const float v[S_COUNT] = { b->voltage, b->current, b->power, b->rpm };
    ui_plot_push(&s.plot, v);
    ui_plot_update_scales(&s.plot, PLOT_W);
}

void motor_screen_set_armed(bool armed)
{
    if (s.armed != armed) {
        s.armed = armed;
        if (armed) {
            s.arm_flash_s = ARM_FLASH_S;
        }
        s.arm_held_s = 0.0f;
        ++s.arm_rev;
        ++s.ctrl_rev;
    }
}
/*
 * The kV the connected ESC reports, or 0 when it reports none.  Preferred
 * over SET_MOTOR_KV: the part under test knows its own rating and a value
 * typed in months ago may belong to a different motor.
 *
 * Nothing calls this yet.  No ESC parameter set is wired to the link: the
 * OpenYGE cache is built and unconnected, and BLHeli_32's parameters are not
 * published.  Until one is, the field is whatever the operator entered.
 */
void motor_screen_set_esc_kv(int kv)
{
    if (kv < 0) {
        kv = 0;
    }
    if (s.esc_kv != kv) {
        s.esc_kv = kv;
        ++s.ctrl_rev;
    }
}

float motor_screen_throttle(void) { return s.slider.value; }
void motor_screen_set_throttle(float pct)
{
    ui_slider_set(&s.slider, pct);
    ++s.thr_rev;
}

/* ------------------------------------------------------------------ events */

static void event(const touch_event_t *evt)
{
    if (evt == NULL) {
        return;
    }
    /*
     * Each control bumps its own counter, rather than every touch bumping
     * one.  A drag delivers a move per frame, and repainting the buttons
     * with the throttle is what put the drag over two panel frames.
     */
    if (ui_tabs_event(&s.tabs, evt)) {
        /* The other pane paints over this one's region, so the record of
         * what was drawn there is void -- not merely the chrome's. */
        motor_invalidate();
    }
    if (ui_slider_event(&s.slider, evt)) {
        post(MOTOR_CMD_THROTTLE, s.slider.value);
        ++s.thr_rev;
    }

    const int x = evt->point.x, y = evt->point.y;
    if (evt->type == TOUCH_EVENT_DOWN) {
        /* One percentage point, on the press rather than the release: this
         * is a fine adjustment and it should feel immediate. */
        if (gfx_rect_contains(s.down_rect, x, y)
            || gfx_rect_contains(s.up_rect, x, y)) {
            const bool up = gfx_rect_contains(s.up_rect, x, y);
            motor_screen_set_throttle(s.slider.value + (up ? 1.0f : -1.0f));
            post(MOTOR_CMD_THROTTLE, s.slider.value);
            s.have_press = true;
            s.press_id   = evt->point.id;
            s.pressed    = up ? 4 : 3;
            ++s.ctrl_rev;
            return;
        }
        if (gfx_rect_contains(s.arm_rect, x, y)) {
            s.have_press = true; s.press_id = evt->point.id; s.pressed = 1;
            s.arm_held_s = 0.0f;
            ++s.arm_rev;
            ++s.ctrl_rev;
        } else if (gfx_rect_contains(s.reset_rect, x, y)) {
            s.have_press = true; s.press_id = evt->point.id; s.pressed = 2;
            ++s.ctrl_rev;
        }
        return;
    }
    if (!s.have_press || evt->point.id != s.press_id) {
        return;   /* a second finger cannot steal the first one's release */
    }
    if (evt->type == TOUCH_EVENT_UP) {
        const int was = s.pressed;
        s.have_press = false;
        s.pressed = 0;
        if (was != 0) {
            ++s.ctrl_rev;   /* the button comes back up */
        }
        if (was == 1) {
            s.arm_held_s = 0.0f;   /* and the fade goes back to green */
            ++s.arm_rev;
        }
        if (was == 1 && gfx_rect_contains(s.arm_rect, x, y)) {
            post(s.armed ? MOTOR_CMD_DISARM : MOTOR_CMD_ARM, 0.0f);
        } else if (was == 2 && gfx_rect_contains(s.reset_rect, x, y)) {
            post(MOTOR_CMD_RESET_PEAKS, 0.0f);
        }
    }
}

/*
 * The two animations ARM carries.  Each advances its own counter, so a frame
 * of either repaints the button's 180 x 28 px and nothing else: the control
 * row is 800 x 90, and an animation that asked for the row would put the
 * screen back where the throttle drag was.
 */
static void tick(float dt_s)
{
    if (s.pressed == 1 && !s.armed && s.arm_held_s < ARM_FADE_S) {
        s.arm_held_s += dt_s;
        if (s.arm_held_s > ARM_FADE_S) {
            s.arm_held_s = ARM_FADE_S;
        }
        ++s.arm_rev;
    }
    if (s.arm_flash_s > 0.0f) {
        s.arm_flash_s -= dt_s;
        if (s.arm_flash_s < 0.0f) {
            s.arm_flash_s = 0.0f;
        }
        ++s.arm_rev;
    }
}

/* ----------------------------------------------------------------- drawing */

/*
 * Green while it will arm, red by the time the fade completes, and white for
 * the flash as the arm takes effect.  With nothing held and nothing flashing
 * this is the plain OK or WARN the button has always carried.
 */
static gfx_color_t arm_fill(void)
{
    /* Armed is the danger red the press fades towards, so the fade previews
     * the colour the button is about to hold. */
    gfx_color_t fill = s.armed ? ui_theme_color(UI_C_DANGER)
                               : ui_theme_color(UI_C_OK);
    if (!s.armed && s.arm_held_s > 0.0f) {
        const uint8_t t =
            (uint8_t)(s.arm_held_s / ARM_FADE_S * 255.0f + 0.5f);
        fill = gfx_lerp(fill, ui_theme_color(UI_C_DANGER), t);
    }
    if (s.arm_flash_s > 0.0f) {
        const uint8_t t =
            (uint8_t)(s.arm_flash_s / ARM_FLASH_S * 255.0f + 0.5f);
        fill = gfx_lerp(fill, GFX_WHITE, t);
    }
    return fill;
}

/* "44C", or "--" when nothing has answered. */
static void temp_text(char *out, size_t n, const char *label, float v,
                      bool ok)
{
    if (ok && isfinite(v)) {
        snprintf(out, n, "%s %dC", label, (int)(v + 0.5f));
    } else {
        snprintf(out, n, "%s --", label);
    }
}

/*
 * The strip above both columns: what is driving the outputs on the left of
 * it, and how hot the three things that get hot on the right.  MCU is the
 * panel's own die, not the coprocessor's.
 */
static void draw_header(gfx_canvas_t *c)
{
    const ui_bench_status_t *st = ui_router_status();
    const int x0 = LEFT_X + TAB_W + 10;
    gfx_fill_rect(c, x0, HDR_Y, W - PAD - x0, HDR_H,
                  ui_theme_color(UI_C_BG));

    /* The band already carries the output mode, so this strip does not
     * repeat it: what it adds is the poll rate and the link's error count. */
    char line[64];
    snprintf(line, sizeof(line), "%d Hz   ERR %lu", (int)SAMPLE_HZ,
             (unsigned long)st->link_errors);
    gfx_text(c, x0, HDR_Y + 1, line, &gfx_font_8x16,
             ui_theme_color(UI_C_TEXT_DIM), 1);

    char esc[16], mot[16], mcu[16], temps[52];
    temp_text(esc, sizeof(esc), "ESC", s.bench.temp_esc, s.bench.valid);
    temp_text(mot, sizeof(mot), "MOT", s.bench.temp_motor, s.bench.valid);
    temp_text(mcu, sizeof(mcu), "MCU", st->mcu_temp_c,
              isfinite(st->mcu_temp_c));
    snprintf(temps, sizeof(temps), "%s  %s  %s", esc, mot, mcu);
    gfx_text_in(c, (gfx_rect_t){ (int16_t)x0, HDR_Y + 1,
                                 (int16_t)(W - PAD - x0), 16 },
                temps, &gfx_font_8x16, ui_theme_color(UI_C_TEXT_DIM), 1,
                GFX_ALIGN_RIGHT);
}

/*
 * The rated kV, what the motor is actually turning per volt, and what the
 * ratio of the two says about the drivetrain.
 *
 * The arithmetic is sound and the number is still an estimate, which is why
 * it is labelled EFF rather than efficiency.  Terminal voltage splits into
 * the resistive drop and the back-EMF, so rpm/V under load is the rated kV
 * scaled by the fraction of the voltage reaching the back-EMF, and in an
 * ideal motor that fraction is the conversion efficiency exactly.
 *
 * This motor is not that one.  The figure counts copper loss only, so iron
 * loss, friction and windage are missing and it is an upper bound rather
 * than a value.  The bench measures pack voltage rather than the motor's
 * terminals, so the ESC's losses are inside it and it describes the
 * drivetrain.  And it is only as good as the rated kV, an error in which
 * maps straight into the percentage.  docs/Screens.md says so in the words
 * an operator reads.
 *
 * The rated value comes from the connected ESC when it reports one, and from
 * SET_MOTOR_KV when it does not.  Nothing is assumed: with neither, the field
 * is drawn empty and no efficiency is shown, because a guessed kV produces a
 * plausible-looking percentage that is wrong.
 */
static void draw_derived(gfx_canvas_t *c)
{
    const gfx_rect_t box = { (int16_t)(LEFT_X + 150), UP_Y + 2,
                             (int16_t)(LEFT_W - 156), 16 };
    gfx_fill_rect(c, box.x, box.y, box.w, box.h, ui_theme_color(UI_C_PANEL));

    /* What the ESC says, or failing that what somebody typed in. */
    const int rated = (s.esc_kv > 0) ? s.esc_kv
                                     : settings_get_int(SET_MOTOR_KV);
    const bool measurable =
        s.bench.valid && s.bench.voltage > 0.5f && s.bench.rpm > 1.0f;
    const float per_volt = measurable ? (s.bench.rpm / s.bench.voltage) : 0.0f;

    char kv[24], rv[24], eff[24];
    if (rated > 0) {
        snprintf(kv, sizeof(kv), "kV %d", rated);
    } else {
        snprintf(kv, sizeof(kv), "kV ----");
    }
    if (measurable) {
        int shown = (int)(per_volt + 0.5f);
        if (shown > 99999) {
            shown = 99999;   /* the field has a width; a reading this high
                              * is a broken sensor, not a motor */
        }
        snprintf(rv, sizeof(rv), "rpm/V %d", shown);
    } else {
        snprintf(rv, sizeof(rv), "rpm/V --");
    }
    if (rated > 0 && measurable) {
        int pct = (int)(per_volt * 100.0f / (float)rated + 0.5f);
        /* Over 100 % means the rated value is wrong, which is worth seeing;
         * it is capped only so the field cannot grow without bound. */
        if (pct > 199) {
            pct = 199;
        }
        snprintf(eff, sizeof(eff), "EFF %d%%", pct);
    } else {
        snprintf(eff, sizeof(eff), "EFF --");
    }

    char line[96];
    snprintf(line, sizeof(line), "%s  %s  %s", kv, rv, eff);
    gfx_text_in(c, box, line, &gfx_font_8x16,
                ui_theme_color(UI_C_TEXT_DIM), 1, GFX_ALIGN_RIGHT);
}

static void draw_table(gfx_canvas_t *c)
{
    static const char *const rows[S_COUNT] = { "VOLTAGE", "CURRENT",
                                               "POWER", "RPM" };
    const float now[S_COUNT] = { s.bench.voltage, s.bench.current,
                                 s.bench.power, s.bench.rpm };
    const float pk[S_COUNT]  = { s.bench.voltage_min, s.bench.current_max,
                                 s.bench.power_max, s.bench.rpm_max };
    /* Voltage's interesting extreme is the minimum, not the maximum: what a
     * pack does under load is how far down it goes. */
    static const char *const pk_label[S_COUNT] = { "min", "max", "max", "max" };
    (void)k_extreme;

    gfx_text(c, PLOT_X, LEG_Y + 4, "CHANNEL        NOW            PEAK",
             &gfx_font_8x16, ui_theme_color(UI_C_TEXT_DIM), 1);
    ui_rule(c, PLOT_X, LEG_Y + 24, PLOT_W, ui_theme_color(UI_C_EDGE));

    for (int i = 0; i < S_COUNT; ++i) {
        const int y = LEG_Y + 34 + i * 30;
        char v[24], p[24];
        ui_fmt(v, sizeof(v), now[i], k_series[i].decimals);
        ui_fmt(p, sizeof(p), pk[i], k_series[i].decimals);
        gfx_text(c, PLOT_X, y, rows[i], &gfx_font_8x16,
                 s.plot.series[i].color, 1);
        gfx_text(c, PLOT_X + 128, y, v, &gfx_font_8x16,
                 ui_theme_color(UI_C_TEXT), 1);
        gfx_text(c, PLOT_X + 248, y, k_series[i].unit, &gfx_font_8x16,
                 ui_theme_color(UI_C_TEXT_DIM), 1);
        char line[40];
        snprintf(line, sizeof(line), "%s %s", pk_label[i], p);
        gfx_text(c, PLOT_X + 348, y, line, &gfx_font_8x16,
                 ui_theme_color(UI_C_TEXT_FAINT), 1);
    }
}

/*
 * One card of the right rail.  ui_hero_render lays its numerals out for an
 * 88 px card and the rail gives 57, so this is the same information in the
 * rail's proportions: the name and the extreme stacked on the left, the live
 * value in a numeral face that fits, right-aligned with its unit.
 */
static void draw_rail_card(gfx_canvas_t *c, gfx_rect_t r,
                           const ui_hero_def_t *def, float value, float peak)
{
    ui_card(c, r, ui_theme_color(UI_C_PANEL));
    gfx_hline(c, r.x + UI_R_CARD, r.y + 1, r.w - 2 * UI_R_CARD, def->color);

    gfx_fill_round_rect(c, r.x + 8, r.y + 7, 3, 10, 1, def->color);
    gfx_text(c, r.x + 16, r.y + 6, def->label, &gfx_font_8x16,
             ui_theme_color(UI_C_TEXT), 1);

    if (isfinite(peak)) {
        char pk[24], line[32];
        ui_fmt(pk, sizeof(pk), peak, def->decimals);
        snprintf(line, sizeof(line), "%s %s",
                 (def->extreme_label != NULL) ? def->extreme_label : "PK", pk);
        gfx_text(c, r.x + 8, r.y + 26, line, &gfx_font_8x16,
                 ui_theme_color(UI_C_TEXT_FAINT), 1);
    }

    char number[24];
    if (isfinite(value)) {
        ui_fmt(number, sizeof(number), value, def->decimals);
    } else {
        snprintf(number, sizeof(number), "---");
    }
    /* Narrower and shorter than the hero face, so four of these stack in the
     * height one hero card wants. */
    const gfx_seg_style_t seg = { .digit_w = 15, .digit_h = 24,
                                  .thickness = 3, .gap = 4, .slant = 2,
                                  .ghost = true };
    const int uw = gfx_text_width(&gfx_font_8x16, def->unit, 1);
    const int nw = gfx_seg_width(number, &seg);
    const int nx = r.x + r.w - 8 - uw - 6 - nw;
    gfx_seg_text(c, nx, r.y + 16, number, &seg, def->color,
                 gfx_lerp(ui_theme_color(UI_C_PANEL), def->color, 34));
    gfx_text(c, nx + nw + 6, r.y + 30, def->unit, &gfx_font_8x16,
             ui_theme_color(UI_C_TEXT_DIM), 1);
}

/* The right rail: one card per channel, live value over its extreme. */
static void draw_heroes(gfx_canvas_t *c)
{
    const float now[S_COUNT] = { s.bench.voltage, s.bench.current,
                                 s.bench.power, s.bench.rpm };
    const float pk[S_COUNT]  = { s.bench.voltage_min, s.bench.current_max,
                                 s.bench.power_max, s.bench.rpm_max };
    for (int i = 0; i < S_COUNT; ++i) {
        const gfx_rect_t r = { RIGHT_X,
                               (int16_t)(UP_Y + i * (HERO_H + HERO_GAP)),
                               RIGHT_W, HERO_H };
        gfx_fill_rect(c, r.x, r.y, r.w, r.h, ui_theme_color(UI_C_BG));
        const ui_hero_def_t def = { k_series[i].name, k_series[i].unit,
                                    s.plot.series[i].color,
                                    k_series[i].decimals, k_extreme[i] };
        draw_rail_card(c, r, &def, s.bench.valid ? now[i] : NAN,
                       s.bench.valid ? pk[i] : NAN);
    }
}

/* What the run has taken out of the pack, under the arming button. */
static void draw_totals(gfx_canvas_t *c)
{
    const gfx_rect_t box = { (int16_t)(RIGHT_X + INNER), TOTALS_Y,
                             (int16_t)(RIGHT_W - 2 * INNER), 16 };
    gfx_fill_rect(c, box.x, box.y, box.w, box.h, ui_theme_color(UI_C_PANEL));
    char line[48];
    snprintf(line, sizeof(line), "%.0f mAh   %.2f Wh",
             (double)s.bench.charge_mah, (double)s.bench.energy_wh);
    gfx_text_in(c, box, line, &gfx_font_8x16, ui_theme_color(UI_C_TEXT), 1,
                GFX_ALIGN_RIGHT);
}

static void render(gfx_canvas_t *c, int buffer_index)
{
    const unsigned bit = 1u << (buffer_index & 1);
    const int buf = buffer_index & 1;
    bind_colours();

    /* Chrome per framebuffer, not per frame; the rest only when its counter
     * moves.  tools/frame_cost.py measures the two cases as the `chrome` and
     * `frame-idle` modes. */
    if ((s.drawn_mask & bit) == 0) {
        gfx_clear(c, ui_theme_color(UI_C_BG));
        ui_panel(c, (gfx_rect_t){ LEFT_X, UP_Y, LEFT_W, UP_H },
                 "LIVE TELEMETRY", ui_theme_color(UI_C_ACCENT));
        ui_panel(c, (gfx_rect_t){ LEFT_X, LO_Y, LEFT_W, LO_H },
                 "THROTTLE", ui_theme_color(UI_C_ACCENT));
        ui_panel(c, (gfx_rect_t){ RIGHT_X, LO_Y, RIGHT_W, LO_H },
                 "CONTROL", ui_theme_color(UI_C_ACCENT));
        s.drawn_mask |= bit;
    }

    if (s.drawn_ctrl[buf] != s.ctrl_rev
        || s.drawn_push[buf] != s.plot.pushes) {
        ui_tabs_render(&s.tabs, c);
        draw_header(c);
    }

    /*
     * Everything that comes from the numbers is painted only when there are
     * new numbers.  The panel refreshes at 39 Hz and samples arrive at 20 Hz,
     * so about half of all frames would otherwise repaint an identical plot
     * and identical readouts into the PSRAM (pseudo-static random-access
     * memory) the LCD (liquid-crystal display) is scanning out of.
     *
     * Per framebuffer, because the panel alternates between two: a buffer
     * whose last paint was two samples ago still needs one even when the
     * other is current.
     */
    if (s.drawn_push[buf] != s.plot.pushes) {
        s.drawn_push[buf] = s.plot.pushes;

        if (s.tabs.selected == MOTOR_PANE_PLOT) {
            /* Cleared first: the legend prints each channel's full scale,
             * that scale autoranges, and a shorter number drawn over a
             * longer one leaves the tail of the old one behind. */
            gfx_fill_rect(c, PLOT_X, LEG_Y, PLOT_W, LEG_H,
                          ui_theme_color(UI_C_PANEL));
            ui_plot_render_legend(&s.plot, c,
                                  (gfx_rect_t){ PLOT_X, LEG_Y, PLOT_W, LEG_H });
            ui_plot_render(&s.plot, c,
                           (gfx_rect_t){ PLOT_X, PLOT_Y, PLOT_W, PLOT_H });
        } else {
            gfx_fill_rect(c, LEFT_X + 1, UP_Y + TITLE_H, LEFT_W - 2,
                          UP_H - TITLE_H - 1, ui_theme_color(UI_C_PANEL));
            draw_table(c);
        }
        draw_derived(c);
        draw_heroes(c);
        draw_totals(c);
    }

    /*
     * The controls change when somebody touches them, not when a sample
     * arrives, so they get their own revision counter.  On a frame where
     * neither the numbers nor a control moved, this screen paints nothing,
     * which on a panel whose refresh outruns its sample rate is most frames.
     */
    const bool ctrl_moved = (s.drawn_ctrl[buf] != s.ctrl_rev);
    const bool thr_moved  = (s.drawn_thr[buf] != s.thr_rev);
    const bool arm_moved  = (s.drawn_arm[buf] != s.arm_rev);
    if (!ctrl_moved && !thr_moved && !arm_moved) {
        return;
    }
    s.drawn_ctrl[buf] = s.ctrl_rev;
    s.drawn_thr[buf]  = s.thr_rev;
    s.drawn_arm[buf]  = s.arm_rev;

    /* ARM animating on its own repaints its own button and nothing else. */
    if (arm_moved && !ctrl_moved && !thr_moved) {
        ui_button(c, s.arm_rect, s.armed ? "DISARM" : "ARM", arm_fill(),
                  s.pressed == 1, true);
        return;
    }

    /*
     * A throttle that moved on its own clears the readout's own box and the
     * slider's painted region, not both panels: a drag asks for it on every
     * frame.  The slider's region is what the widget reports rather than its
     * track, because the thumb and its shadow stand proud of the track.
     */
    if (ctrl_moved) {
        gfx_fill_rect(c, LEFT_X + 1, LO_Y + TITLE_H, LEFT_W - 2,
                      LO_H - TITLE_H - 1, ui_theme_color(UI_C_PANEL));
    } else {
        gfx_fill_rect(c, LEFT_X + INNER, ROW_Y, READOUT_W, READOUT_H,
                      ui_theme_color(UI_C_PANEL));
        const gfx_rect_t sl = ui_slider_painted_rect(&s.slider);
        gfx_fill_rect(c, sl.x, sl.y, sl.w, sl.h, ui_theme_color(UI_C_PANEL));
    }

    /* The throttle in the numeral face the readings use: it is a reading
     * too, and the one the operator's hand is on. */
    char pct[16];
    snprintf(pct, sizeof(pct), "%.1f", (double)s.slider.value);
    const gfx_seg_style_t seg = ui_seg_hero();
    const int pw = gfx_seg_width(pct, &seg);
    const int px = LEFT_X + LEFT_W - INNER - 20 - pw;
    gfx_seg_text(c, px, ROW_Y, pct, &seg, ui_theme_color(UI_C_ACCENT),
                 gfx_lerp(ui_theme_color(UI_C_BG),
                          ui_theme_color(UI_C_ACCENT), 34));
    gfx_text(c, LEFT_X + LEFT_W - INNER - 14, ROW_Y + 14, "%",
             &gfx_font_8x16, ui_theme_color(UI_C_TEXT_DIM), 1);

    ui_button(c, s.down_rect, "-1", ui_theme_color(UI_C_PANEL_SUNK),
              s.pressed == 3, true);
    ui_slider_render(&s.slider, c);
    ui_button(c, s.up_rect, "+1", ui_theme_color(UI_C_PANEL_SUNK),
              s.pressed == 4, true);

    if (!ctrl_moved && !arm_moved) {
        return;
    }
    ui_button(c, s.arm_rect, s.armed ? "DISARM" : "ARM", arm_fill(),
              s.pressed == 1, true);
    if (!ctrl_moved) {
        return;
    }
    gfx_text(c, LEFT_X + INNER, HINT_Y,
             "DRAG THE BAR, OR STEP IT BY ONE POINT AT EITHER END",
             &gfx_font_8x16, ui_theme_color(UI_C_TEXT_FAINT), 1);
    draw_totals(c);
    ui_button(c, s.reset_rect, "RESET PEAKS", ui_theme_color(UI_C_PANEL_SUNK),
              s.pressed == 2, true);
}

/*
 * Leaving disarms.  Navigating away from an armed bench must not leave a
 * propeller spinning behind a screen that is not visible, and the command
 * carries the disarm rather than relying on the application to infer it
 * from the navigation.
 */
static void leave(void)
{
    post(MOTOR_CMD_DISARM, 0.0f);
    s.armed = false;
    /* Neither animation should still be running when the screen comes back. */
    s.arm_held_s  = 0.0f;
    s.arm_flash_s = 0.0f;
    ++s.arm_rev;
}

static const ui_screen_t k_screen = {
    .title  = "MOTOR & ESC",
    .reset  = reset,
    .enter  = NULL,
    .leave  = leave,
    .tick   = tick,
    .event  = event,
    .render = render,
};

const ui_screen_t *motor_screen(void) { return &k_screen; }
