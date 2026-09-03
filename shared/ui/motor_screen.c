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
#define INNER     10                   /* a card's own padding */

#define TAB_Y     8
#define TAB_H     28
#define CARD_Y    44
#define CARD_H    196
#define HERO_Y    248
#define HERO_H    88
/*
 * The throttle owns the bottom of the screen: the track carries tick marks
 * at the quarters, and the readout, ARM and RESET share the row above it.
 */
#define ROW_Y     342                  /* the readout, ARM and RESET share it */
#define ROW_H     30
#define CTRL_Y    380
#define TRACK_H   36
/* The readout's own width: the label at PAD, the numerals right-aligned on
 * 386 - 22, and the per-cent sign that follows them.  ARM starts at 400. */
#define READOUT_W 396

/* The legend takes a row at the top of the card and the plot takes the rest
 * of it. */
#define LEG_Y     (CARD_Y + 8)
#define LEG_H     16
#define PLOT_X    (PAD + INNER)
#define PLOT_W    (W - PAD - INNER - PLOT_X)
#define PLOT_Y    (LEG_Y + LEG_H + 6)
#define PLOT_H    (CARD_Y + CARD_H - 10 - PLOT_Y)

/* Four across the full width, with one gutter between each. */
#define HERO_GAP  8
#define HERO_W    ((W - 2 * PAD - 3 * HERO_GAP) / 4)

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
    gfx_rect_t    arm_rect;
    gfx_rect_t    reset_rect;
    int           pressed;      /**< 0 none, 1 arm, 2 reset */
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
    ui_plot_init(&s.plot, k_series, S_COUNT,
                 (float)PLOT_W / SAMPLE_HZ);
    ui_tabs_init(&s.tabs, k_tab_labels, MOTOR_PANE_COUNT,
                 (gfx_rect_t){ PAD + 3, TAB_Y, 234, TAB_H });

    const gfx_rect_t track = { PAD, CTRL_Y, W - 2 * PAD, TRACK_H };
    ui_slider_init(&s.slider, track, 0.0f, 100.0f, 0);
    ui_slider_set_ticks(&s.slider, 4);   /* tick marks at the quarters */

    s.arm_rect   = (gfx_rect_t){ 400, ROW_Y, 180, (int16_t)(ROW_H - 2) };
    s.reset_rect = (gfx_rect_t){ 594, ROW_Y, 200, (int16_t)(ROW_H - 2) };
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
        if (gfx_rect_contains(s.arm_rect, x, y)) {
            s.have_press = true; s.press_id = evt->point.id; s.pressed = 1;
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
        if (was == 1 && gfx_rect_contains(s.arm_rect, x, y)) {
            post(s.armed ? MOTOR_CMD_DISARM : MOTOR_CMD_ARM, 0.0f);
        } else if (was == 2 && gfx_rect_contains(s.reset_rect, x, y)) {
            post(MOTOR_CMD_RESET_PEAKS, 0.0f);
        }
    }
}

/* ----------------------------------------------------------------- drawing */

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

    gfx_text(c, 12, PLOT_Y + 6, "CHANNEL        NOW            PEAK",
             &gfx_font_8x16, ui_theme_color(UI_C_TEXT_DIM), 1);
    ui_rule(c, 12, PLOT_Y + 26, W - 24, ui_theme_color(UI_C_EDGE));

    for (int i = 0; i < S_COUNT; ++i) {
        const int y = PLOT_Y + 38 + i * 34;
        char v[24], p[24];
        ui_fmt(v, sizeof(v), now[i], k_series[i].decimals);
        ui_fmt(p, sizeof(p), pk[i], k_series[i].decimals);
        gfx_text(c, 12, y, rows[i], &gfx_font_8x16,
                 s.plot.series[i].color, 1);
        gfx_text(c, 140, y, v, &gfx_font_8x16, ui_theme_color(UI_C_TEXT), 1);
        gfx_text(c, 260, y, k_series[i].unit, &gfx_font_8x16,
                 ui_theme_color(UI_C_TEXT_DIM), 1);
        char line[40];
        snprintf(line, sizeof(line), "%s %s", pk_label[i], p);
        gfx_text(c, 360, y, line, &gfx_font_8x16,
                 ui_theme_color(UI_C_TEXT_FAINT), 1);
    }

    char energy[48];
    snprintf(energy, sizeof(energy), "%.0f mAh    %.1f Wh",
             (double)s.bench.charge_mah, (double)s.bench.energy_wh);
    gfx_text(c, 12, PLOT_Y + 38 + S_COUNT * 34 + 8, energy, &gfx_font_8x16,
             ui_theme_color(UI_C_TEXT), 1);
}

static void render(gfx_canvas_t *c, int buffer_index)
{
    const unsigned bit = 1u << (buffer_index & 1);
    bind_colours();

    /* Chrome per framebuffer, not per frame; the rest only when its counter
     * moves.  tools/frame_cost.py measures the two cases as the `chrome` and
     * `frame-idle` modes. */
    if ((s.drawn_mask & bit) == 0) {
        gfx_clear(c, ui_theme_color(UI_C_BG));
        /* The card shell is chrome: it never changes, so it is painted per
         * framebuffer and the plot repaints only its own interior. */
        ui_card(c, (gfx_rect_t){ PAD, CARD_Y, W - 2 * PAD, CARD_H },
                ui_theme_color(UI_C_PANEL_SUNK));
        s.drawn_mask |= bit;
    }

    if (s.drawn_ctrl[buffer_index & 1] != s.ctrl_rev
        || s.drawn_push[buffer_index & 1] != s.plot.pushes) {
        ui_tabs_render(&s.tabs, c);
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
    const int buf = buffer_index & 1;
    if (s.drawn_push[buf] != s.plot.pushes) {
        s.drawn_push[buf] = s.plot.pushes;

        if (s.tabs.selected == MOTOR_PANE_PLOT) {
            /* Cleared first: the legend prints each channel's full scale,
             * that scale autoranges, and a shorter number drawn over a
             * longer one leaves the tail of the old one behind. */
            gfx_fill_rect(c, PLOT_X, LEG_Y, PLOT_W, LEG_H,
                          ui_theme_color(UI_C_PANEL_SUNK));
            ui_plot_render_legend(&s.plot, c,
                                  (gfx_rect_t){ PLOT_X, LEG_Y, PLOT_W, LEG_H });
            ui_plot_render(&s.plot, c,
                           (gfx_rect_t){ PLOT_X, PLOT_Y, PLOT_W, PLOT_H });
        } else {
            gfx_fill_rect(c, PAD + 1, CARD_Y + 1, W - 2 * PAD - 2,
                          CARD_H - 2, ui_theme_color(UI_C_PANEL_SUNK));
            draw_table(c);
        }

        /* The heroes: live value, its extreme, its unit. */
        gfx_fill_rect(c, 0, HERO_Y, W, HERO_H, ui_theme_color(UI_C_BG));
        const float now[S_COUNT] = { s.bench.voltage, s.bench.current,
                                     s.bench.power, s.bench.rpm };
        const float pk[S_COUNT]  = { s.bench.voltage_min, s.bench.current_max,
                                     s.bench.power_max, s.bench.rpm_max };
        for (int i = 0; i < S_COUNT; ++i) {
            const ui_hero_def_t def = { k_series[i].name, k_series[i].unit,
                                        s.plot.series[i].color,
                                        k_series[i].decimals, k_extreme[i] };
            ui_hero_render(c, (gfx_rect_t){
                               (int16_t)(PAD + i * (HERO_W + HERO_GAP)),
                               HERO_Y, HERO_W, HERO_H },
                           &def, s.bench.valid ? now[i] : NAN,
                           s.bench.valid ? pk[i] : NAN);
        }
    }

    /*
     * The controls change when somebody touches them, not when a sample
     * arrives, so they get their own revision counter.  On a frame where
     * neither the numbers nor a control moved, this screen paints nothing,
     * which on a panel whose refresh outruns its sample rate is most frames.
     */
    const bool ctrl_moved = (s.drawn_ctrl[buf] != s.ctrl_rev);
    const bool thr_moved  = (s.drawn_thr[buf] != s.thr_rev);
    if (!ctrl_moved && !thr_moved) {
        return;
    }
    s.drawn_ctrl[buf] = s.ctrl_rev;
    s.drawn_thr[buf]  = s.thr_rev;

    /*
     * A throttle that moved on its own repaints the readout and the track,
     * not the buttons beside them: the row is 800 x 90 px against the
     * readout's 396 x 30, and a drag asks for it on every frame.  The track
     * needs no clear either, because the trough is painted opaque over it.
     */
    if (ctrl_moved) {
        gfx_fill_rect(c, 0, ROW_Y, W, H - ROW_Y, ui_theme_color(UI_C_BG));
    } else {
        gfx_fill_rect(c, 0, ROW_Y, READOUT_W, ROW_H,
                      ui_theme_color(UI_C_BG));
    }

    /*
     * The readout has its own line above the track.  Printed over the bar it
     * would sit on whatever colour the fill reaches, at high throttle its own
     * accent.
     */
    gfx_text(c, PAD, ROW_Y + 8, "THROTTLE", &gfx_font_8x16,
             ui_theme_color(UI_C_TEXT_DIM), 1);

    /* Set in the numeral face the readings above use: the throttle is a
     * reading too, and the one the operator's hand is on. */
    char pct[16];
    snprintf(pct, sizeof(pct), "%.1f", (double)s.slider.value);
    const gfx_seg_style_t seg = ui_seg_hero();
    const int pw = gfx_seg_width(pct, &seg);
    const int px = 386 - 22 - pw;
    gfx_seg_text(c, px, ROW_Y, pct, &seg, ui_theme_color(UI_C_ACCENT),
                 gfx_lerp(ui_theme_color(UI_C_BG),
                          ui_theme_color(UI_C_ACCENT), 34));
    gfx_text(c, 386 - 16, ROW_Y + 14, "%", &gfx_font_8x16,
             ui_theme_color(UI_C_TEXT_DIM), 1);

    ui_slider_render(&s.slider, c);

    if (!ctrl_moved) {
        return;
    }

    ui_button(c, s.arm_rect, s.armed ? "DISARM" : "ARM",
              s.armed ? ui_theme_color(UI_C_WARN) : ui_theme_color(UI_C_OK),
              s.pressed == 1, true);
    ui_button(c, s.reset_rect, "RESET PEAKS", ui_theme_color(UI_C_PANEL),
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
}

static const ui_screen_t k_screen = {
    .title  = "MOTOR & ESC",
    .reset  = reset,
    .enter  = NULL,
    .leave  = leave,
    .tick   = NULL,
    .event  = event,
    .render = render,
};

const ui_screen_t *motor_screen(void) { return &k_screen; }
