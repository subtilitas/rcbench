#include "motor_screen.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ui_hero.h"
#include "ui_plot.h"
#include "ui_slider.h"
#include "ui_tabs.h"
#include "ui_theme.h"
#include "ui_widgets.h"

#define W 800
#define H (480 - UI_BAND_H)   /* the router owns the band */

/* Horizontal bands, not columns.  Seventeen full-height vertical lines cost
 * about eight thousand cache-line fills on this panel and the same pixels
 * drawn horizontally cost none, so the layout stratifies. */
#define TAB_Y     6
#define TAB_H     28
#define PLOT_Y    40
#define PLOT_H    212
#define HERO_Y    258
#define HERO_H    84
#define CTRL_Y    348
#define TRACK_H   34
#define PRESET_H  30

#define RAIL_X    6
#define RAIL_W    26
#define PLOT_X    (RAIL_X + RAIL_W)
#define PLOT_W    (W - PLOT_X - 6)

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
 * the width rather than a round number chosen independently of it.  Fixing
 * the span at 24 s left a third of a 762-column plot permanently empty.
 */
#define SAMPLE_HZ 20.0f

static const float k_presets[] = { 0.0f, 25.0f, 50.0f, 75.0f, 100.0f };
static const char *const k_preset_labels[] = { "0", "25", "50", "75", "100" };
static const char *const k_tab_labels[] = { "PLOT", "TABLE" };

static struct {
    ui_plot_t     plot;
    ui_slider_t   slider;
    ui_tabs_t     tabs;
    bench_state_t bench;
    bool          armed;
    motor_cmd_t   pending;
    unsigned      drawn_mask;
    gfx_rect_t    arm_rect;
    gfx_rect_t    reset_rect;
    int           pressed;      /**< 0 none, 1 arm, 2 reset */
    uint8_t       press_id;
    bool          have_press;
} s;

void motor_invalidate(void) { s.drawn_mask = 0; }

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
    ui_plot_init(&s.plot, k_series, S_COUNT,
                 (float)PLOT_W / SAMPLE_HZ);
    ui_tabs_init(&s.tabs, k_tab_labels, MOTOR_PANE_COUNT,
                 (gfx_rect_t){ 6, TAB_Y, 260, TAB_H });

    const gfx_rect_t track = { 6, CTRL_Y, W - 12, TRACK_H };
    ui_slider_init(&s.slider, track, 0.0f, 100.0f, 0);
    ui_slider_set_presets(&s.slider, k_presets, k_preset_labels, 5,
                          (gfx_rect_t){ 6, CTRL_Y + TRACK_H + 6, 380,
                                        PRESET_H });

    s.arm_rect   = (gfx_rect_t){ 400, (int16_t)(CTRL_Y + TRACK_H + 6), 180,
                                 PRESET_H };
    s.reset_rect = (gfx_rect_t){ 594, (int16_t)(CTRL_Y + TRACK_H + 6), 200,
                                 PRESET_H };
    bind_colours();
}

/*
 * One slot, coalescing.  The pending command is overwritten rather than
 * queued, because the application drains it every frame and a backlog of
 * throttle positions is worse than the latest one.
 *
 * With one exception: a pending DISARM survives everything.  Two taps inside
 * one drain is barely possible and exactly the case that must not go wrong --
 * an ARM landing on top of a DISARM would re-arm a bench that had just been
 * stopped, and the operator would have watched it stop.
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

void motor_screen_set_armed(bool armed) { s.armed = armed; }
float motor_screen_throttle(void) { return s.slider.value; }
void motor_screen_set_throttle(float pct) { ui_slider_set(&s.slider, pct); }

/* ------------------------------------------------------------------ events */

static void event(const touch_event_t *evt)
{
    if (evt == NULL) {
        return;
    }
    if (ui_tabs_event(&s.tabs, evt)) {
        s.drawn_mask = 0;
    }
    if (ui_slider_event(&s.slider, evt)) {
        post(MOTOR_CMD_THROTTLE, s.slider.value);
    }

    const int x = evt->point.x, y = evt->point.y;
    if (evt->type == TOUCH_EVENT_DOWN) {
        if (gfx_rect_contains(s.arm_rect, x, y)) {
            s.have_press = true; s.press_id = evt->point.id; s.pressed = 1;
        } else if (gfx_rect_contains(s.reset_rect, x, y)) {
            s.have_press = true; s.press_id = evt->point.id; s.pressed = 2;
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

    /* Chrome once per framebuffer; everything below it every frame.  That is
     * the difference between 736 fills and 14,687. */
    if ((s.drawn_mask & bit) == 0) {
        gfx_clear(c, ui_theme_color(UI_C_BG));
        ui_rule(c, 0, CTRL_Y - 8, W, ui_theme_color(UI_C_EDGE));
        s.drawn_mask |= bit;
    }

    ui_tabs_render(&s.tabs, c);

    if (s.tabs.selected == MOTOR_PANE_PLOT) {
        ui_plot_render(&s.plot, c,
                       (gfx_rect_t){ PLOT_X, PLOT_Y, PLOT_W, PLOT_H });
        ui_plot_render_rails(&s.plot, c,
                             (gfx_rect_t){ RAIL_X, PLOT_Y, RAIL_W, PLOT_H });
    } else {
        gfx_fill_rect(c, 0, PLOT_Y, W, PLOT_H, ui_theme_color(UI_C_BG));
        draw_table(c);
    }

    /* The heroes: live value, its peak, its unit. */
    gfx_fill_rect(c, 0, HERO_Y, W, HERO_H, ui_theme_color(UI_C_BG));
    const float now[S_COUNT] = { s.bench.voltage, s.bench.current,
                                 s.bench.power, s.bench.rpm };
    const float pk[S_COUNT]  = { s.bench.voltage_min, s.bench.current_max,
                                 s.bench.power_max, s.bench.rpm_max };
    for (int i = 0; i < S_COUNT; ++i) {
        const ui_hero_def_t def = { k_series[i].name, k_series[i].unit,
                                    s.plot.series[i].color,
                                    k_series[i].decimals, k_extreme[i] };
        ui_hero_render(c, (gfx_rect_t){ (int16_t)(8 + i * 196),
                                        HERO_Y, 190, HERO_H },
                       &def, s.bench.valid ? now[i] : NAN,
                       s.bench.valid ? pk[i] : NAN);
    }

    /* The controls. */
    gfx_fill_rect(c, 0, CTRL_Y, W, H - CTRL_Y, ui_theme_color(UI_C_BG));
    ui_slider_render(&s.slider, c);

    char pct[16];
    snprintf(pct, sizeof(pct), "%.1f %%", (double)s.slider.value);
    gfx_text_in(c, (gfx_rect_t){ (int16_t)(W - 200), (int16_t)(CTRL_Y + 8),
                                 190, 20 },
                pct, &gfx_font_8x16, ui_theme_color(UI_C_TEXT), 1,
                GFX_ALIGN_RIGHT);

    ui_button(c, s.arm_rect, s.armed ? "DISARM" : "ARM",
              s.armed ? ui_theme_color(UI_C_WARN) : ui_theme_color(UI_C_OK),
              s.pressed == 1, true);
    ui_button(c, s.reset_rect, "RESET PEAKS", ui_theme_color(UI_C_PANEL),
              s.pressed == 2, true);
}

/*
 * Leaving disarms.  Navigating away from an armed bench must not leave a
 * propeller spinning behind a screen you can no longer see it on -- and the
 * command carries the disarm rather than relying on the application to infer
 * it from the navigation.
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
