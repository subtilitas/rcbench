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

/* Horizontal bands, not columns.  Seventeen full-height vertical lines cost
 * about eight thousand cache-line fills on this panel and the same pixels
 * drawn horizontally cost none, so the layout stratifies. */
#define PAD       6                    /* the screen's outer margin */
#define INNER     10                   /* a card's own padding */

#define TAB_Y     8
#define TAB_H     28
#define CARD_Y    44
#define CARD_H    196
#define HERO_Y    248
#define HERO_H    90
#define LABEL_Y   346
#define CTRL_Y    366
#define TRACK_H   26
#define PRESET_Y  400
#define PRESET_H  28

/* The scale rails live inside the plot card now, as its left gutter.  Loose
 * on the background they were a 26px strip of coloured stripes butted against
 * the screen edge, which read as an artifact rather than as four axes. */
/* The legend takes a row at the top of the card and the plot takes the rest,
 * which is 22px of width back from the rails it replaces. */
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
    /* The push count last painted into each framebuffer; UINT32_MAX
     * means "nothing yet", which is not a count push can reach. */
    uint32_t      drawn_push[2];
    /* Bumped by anything that changes a control's appearance. */
    uint32_t      ctrl_rev;
    uint32_t      drawn_ctrl[2];
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
    ui_plot_init(&s.plot, k_series, S_COUNT,
                 (float)PLOT_W / SAMPLE_HZ);
    ui_tabs_init(&s.tabs, k_tab_labels, MOTOR_PANE_COUNT,
                 (gfx_rect_t){ PAD + 3, TAB_Y, 234, TAB_H });

    const gfx_rect_t track = { PAD, CTRL_Y, W - 2 * PAD, TRACK_H };
    ui_slider_init(&s.slider, track, 0.0f, 100.0f, 0);
    ui_slider_set_presets(&s.slider, k_presets, k_preset_labels, 5,
                          (gfx_rect_t){ PAD, PRESET_Y, 380, PRESET_H });

    s.arm_rect   = (gfx_rect_t){ 400, PRESET_Y, 180, PRESET_H };
    s.reset_rect = (gfx_rect_t){ 594, PRESET_Y, 200, PRESET_H };
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
    ++s.ctrl_rev;
}

/* ------------------------------------------------------------------ events */

static void event(const touch_event_t *evt)
{
    if (evt == NULL) {
        return;
    }
    /*
     * Any touch at all invalidates the controls.  Enumerating which events
     * move a pixel -- a preset going down, a press sliding off, a drag that
     * lands on the value it already had -- is a list that gets one case wrong
     * and leaves a button stuck looking pressed.  Touches arrive at a few per
     * second against 39 frames, so repainting on all of them costs nothing
     * measurable and cannot be incomplete.
     */
    ++s.ctrl_rev;

    if (ui_tabs_event(&s.tabs, evt)) {
        /* The other pane paints over this one's region, so the record of
         * what was drawn there is void -- not merely the chrome's. */
        motor_invalidate();
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
        /* The card shell is chrome: it never changes, so it is painted once
         * per framebuffer and the plot repaints only its own interior. */
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
     * new numbers.  The panel refreshes at 39 Hz and samples arrive at 20, so
     * about half of all frames would otherwise repaint an identical plot and
     * an identical set of readouts into the very PSRAM the LCD is scanning
     * out of -- which is the traffic that starved the first hardware boot.
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
     * neither the numbers nor a control moved, this screen paints nothing at
     * all -- which on a panel whose refresh outruns its sample rate is most
     * frames.
     */
    if (s.drawn_ctrl[buf] == s.ctrl_rev) {
        return;
    }
    s.drawn_ctrl[buf] = s.ctrl_rev;

    gfx_fill_rect(c, 0, LABEL_Y, W, H - LABEL_Y, ui_theme_color(UI_C_BG));

    /*
     * The number moves off the track and onto its own line.  Printed over a
     * full-width bar it sat on whatever colour the fill happened to reach,
     * and at high throttle that was its own accent.
     */
    gfx_text(c, PAD, LABEL_Y, "THROTTLE", &gfx_font_8x16,
             ui_theme_color(UI_C_TEXT_DIM), 1);
    char pct[16];
    snprintf(pct, sizeof(pct), "%.1f %%", (double)s.slider.value);
    gfx_text_in(c, (gfx_rect_t){ (int16_t)(W - PAD - 200), LABEL_Y, 200, 16 },
                pct, &gfx_font_8x16, ui_theme_color(UI_C_TEXT), 1,
                GFX_ALIGN_RIGHT);

    ui_slider_render(&s.slider, c);

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
