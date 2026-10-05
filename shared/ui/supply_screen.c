/*
 * The SUPPLY screen.  See supply_screen.h.
 *
 * MOTOR & ESC's frame and its rules for what repaints when: chrome once per
 * framebuffer, the numbers when a sample arrives, the plot when it moves,
 * and each control on its own counter.
 *
 * SPDX-License-Identifier: MIT
 */

#include "supply_screen.h"

#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "bench_state.h"
#include "ui_hero.h"
#include "ui_plot.h"
#include "ui_slider.h"
#include "ui_tabs.h"
#include "ui_theme.h"
#include "ui_widgets.h"

#define W 800
#define H (480 - UI_BAND_H)   /* the router owns the band */

#define PAD       6
#define INNER     6
#define COL_GAP   6
#define LEFT_X    PAD
#define LEFT_W    546
#define RIGHT_X   (LEFT_X + LEFT_W + COL_GAP)
#define RIGHT_W   (W - PAD - RIGHT_X)

#define HDR_Y     2
#define HDR_H     18

#define UP_Y      24
#define UP_H      242
#define CARD_GAP  4
#define CARD_H    ((UP_H - 3 * CARD_GAP) / 4)

#define LO_Y      (UP_Y + UP_H + 6)
#define LO_H      (H - LO_Y - PAD)

#define TITLE_H   20
#define LEG_Y     (UP_Y + TITLE_H + 4)
#define LEG_H     30
#define PLOT_X    (LEFT_X + INNER)
#define PLOT_W    (LEFT_W - 2 * INNER)
#define PLOT_Y    (LEG_Y + LEG_H + 4)
#define PLOT_H    (UP_Y + UP_H - INNER - PLOT_Y)

/*
 * The set points: two rows in the lower left, each a label and its value
 * over a track with a fine step at each end.  The step buttons move by a
 * tenth of a volt or of an amp, which the slider is too coarse to place;
 * the slider covers the range, and both snap to what the supply takes.
 */
#define SET_ROW_H   62
#define SET_Y0      (LO_Y + TITLE_H + 4)
#define SET_TEXT_H  20
#define SET_CTRL_DY 24
#define SET_CTRL_H  30
#define NUDGE_W     54
#define SET_TRACK_X (LEFT_X + INNER + NUDGE_W + 6)
#define SET_TRACK_W (LEFT_W - 2 * INNER - 2 * (NUDGE_W + 6))
#define NUDGE_V     0.1f
#define NUDGE_I     0.1f

#define BTN_H     36
#define OUT_Y     (LO_Y + TITLE_H + 8)
#define TOTALS_Y  (OUT_Y + BTN_H + 10)
#define RESET_Y   (LO_H + LO_Y - PAD - BTN_H)

#define TAB_Y     HDR_Y
#define TAB_H     HDR_H
#define TAB_W     124

/* The sample rate of the panel's loop, which is the plot's time base. */
#define SAMPLE_HZ 20.0f

enum { S_VOLT = 0, S_CURR, S_POWER, S_COUNT };

static const ui_plot_series_t k_series[S_COUNT] = {
    { "VOLT", "V", 0, 2, 5.0f },
    { "CURR", "A", 0, 2, 1.0f },
    { "PWR",  "W", 0, 1, 10.0f },
};

static const char *const k_tab_labels[] = { "PLOT", "TABLE" };

/* Pressed controls, for the press each owns. */
enum { P_NONE = 0, P_OUTPUT, P_RESET, P_V_DOWN, P_V_UP, P_I_DOWN, P_I_UP };

static struct {
    ui_plot_t      plot;
    ui_tabs_t      tabs;
    ui_slider_t    v_slider;
    ui_slider_t    i_slider;
    supply_caps_t  caps;
    supply_state_t sup;
    bool           model;
    /* The output as this screen believes it: what the supply reported, and
     * an OFF this screen has asked for and not yet seen answered. */
    bool           on;
    /* The output as the supply reports it, which bounds a run. */
    bool           out_reported;
    supply_cmd_t   pending;
    unsigned       drawn_mask;
    uint32_t       drawn_push[2];
    uint32_t       drawn_plot[2];
    uint8_t        drawn_title[2];
    uint32_t       ctrl_rev;
    uint32_t       drawn_ctrl[2];
    uint32_t       set_rev;
    uint32_t       drawn_set[2];
    uint32_t       out_rev;
    uint32_t       drawn_out[2];
    ui_hold_t      hold;
    gfx_rect_t     out_rect;
    gfx_rect_t     reset_rect;
    gfx_rect_t     v_down, v_up, i_down, i_up;
    int            pressed;
    uint8_t        press_id;
    bool           have_press;
} s;

static void forget_drawn(void)
{
    s.drawn_mask = 0;
    for (int b = 0; b < 2; ++b) {
        s.drawn_push[b]  = UINT32_MAX;
        s.drawn_plot[b]  = UINT32_MAX;
        s.drawn_ctrl[b]  = UINT32_MAX;
        s.drawn_set[b]   = UINT32_MAX;
        s.drawn_out[b]   = UINT32_MAX;
        s.drawn_title[b] = 0xFFu;
    }
}

void supply_invalidate(void)
{
    forget_drawn();
}

static void bind_colours(void)
{
    s.plot.series[S_VOLT].color  = ui_theme_color(UI_C_VOLT);
    s.plot.series[S_CURR].color  = ui_theme_color(UI_C_CURR);
    s.plot.series[S_POWER].color = ui_theme_color(UI_C_POWER);
    s.v_slider.color = ui_theme_color(UI_C_VOLT);
    s.i_slider.color = ui_theme_color(UI_C_CURR);
}

static gfx_rect_t set_track(int row)
{
    return (gfx_rect_t){ SET_TRACK_X,
                         (int16_t)(SET_Y0 + row * SET_ROW_H + SET_CTRL_DY),
                         SET_TRACK_W, SET_CTRL_H };
}

static gfx_rect_t nudge_rect(int row, bool up)
{
    const int16_t y = (int16_t)(SET_Y0 + row * SET_ROW_H + SET_CTRL_DY);
    return up ? (gfx_rect_t){ (int16_t)(SET_TRACK_X + SET_TRACK_W + 6), y,
                              NUDGE_W, SET_CTRL_H }
              : (gfx_rect_t){ (int16_t)(LEFT_X + INNER), y, NUDGE_W,
                              SET_CTRL_H };
}

/* The sliders take the caps' ranges and steps; their values are kept. */
static void apply_caps(void)
{
    const float v = s.v_slider.value;
    const float i = s.i_slider.value;
    ui_slider_init(&s.v_slider, set_track(0), s.caps.v_min, s.caps.v_max,
                   ui_theme_color(UI_C_VOLT));
    ui_slider_init(&s.i_slider, set_track(1), s.caps.i_min, s.caps.i_max,
                   ui_theme_color(UI_C_CURR));
    /* A set point commands nothing until the output is on, and then only a
     * supply's own regulation: a tap that sets the value under it is safe
     * here where it is not on a throttle. */
    ui_slider_set_tap_to_set(&s.v_slider, true);
    ui_slider_set_tap_to_set(&s.i_slider, true);
    ui_slider_set_ticks(&s.v_slider, 4);
    ui_slider_set_ticks(&s.i_slider, 4);
    ui_slider_set(&s.v_slider, supply_snap(v, s.caps.v_min, s.caps.v_max,
                                           s.caps.v_step));
    ui_slider_set(&s.i_slider, supply_snap(i, s.caps.i_min, s.caps.i_max,
                                           s.caps.i_step));
}

static void reset(void)
{
    memset(&s, 0, sizeof(s));
    forget_drawn();
    const supply_caps_t caps = SUPPLY_CAPS_PPS_DEFAULT;
    s.caps = caps;
    ui_plot_init(&s.plot, k_series, S_COUNT, (float)PLOT_W / SAMPLE_HZ);
    ui_plot_set_running(&s.plot, false);
    ui_tabs_init(&s.tabs, k_tab_labels, SUPPLY_PANE_COUNT,
                 (gfx_rect_t){ LEFT_X, TAB_Y, TAB_W, TAB_H });
    /* Where the panel's model starts, so a screen and its model agree before
     * anything has been set. */
    s.v_slider.value = 6.0f;
    s.i_slider.value = 2.0f;
    apply_caps();
    s.v_down = nudge_rect(0, false);
    s.v_up   = nudge_rect(0, true);
    s.i_down = nudge_rect(1, false);
    s.i_up   = nudge_rect(1, true);
    s.out_rect   = (gfx_rect_t){ (int16_t)(RIGHT_X + INNER), OUT_Y,
                                 (int16_t)(RIGHT_W - 2 * INNER), BTN_H };
    s.reset_rect = (gfx_rect_t){ (int16_t)(RIGHT_X + INNER), RESET_Y,
                                 (int16_t)(RIGHT_W - 2 * INNER), BTN_H };
    bind_colours();
}

/* ---------------------------------------------------------------- commands */

static void post_on(void)
{
    /* An OFF already asked for stands: two touches in one frame must not
     * let an ON land on top of it. */
    if (!s.pending.off) {
        s.pending.on = true;
    }
}

static void post_off(void)
{
    s.pending.off = true;
    s.pending.on  = false;
}

bool supply_screen_poll_cmd(supply_cmd_t *out)
{
    const bool any = s.pending.on || s.pending.off || s.pending.reset;
    if (!any) {
        return false;
    }
    if (out != NULL) {
        *out = s.pending;
    }
    memset(&s.pending, 0, sizeof(s.pending));
    return true;
}

/* -------------------------------------------------------------- from outside */

void supply_screen_push(const supply_state_t *st)
{
    if (st == NULL) {
        return;
    }
    s.sup = *st;
    const float v[S_COUNT] = { st->v, st->i, st->p };
    ui_plot_push(&s.plot, v);
    ui_plot_update_scales(&s.plot, PLOT_W);
}

void supply_screen_set_output(bool on)
{
    if (s.out_reported != on) {
        s.out_reported = on;
        if (on) {
            /* A run's trace is that run's, kept after it until the next. */
            ui_plot_clear(&s.plot);
        }
        ui_plot_set_running(&s.plot, on);
    }
    if (s.on != on) {
        s.on = on;
        if (on) {
            ui_hold_reached(&s.hold);
        } else if (ui_hold_left(&s.hold)) {
            s.pressed = P_NONE;
        }
        ++s.out_rev;
        ++s.ctrl_rev;
    }
}

void supply_screen_set_caps(const supply_caps_t *caps)
{
    if (caps == NULL) {
        return;
    }
    s.caps = *caps;
    apply_caps();
    ++s.set_rev;
}

void supply_screen_set_model(bool model)
{
    if (s.model != model) {
        s.model = model;
        ++s.ctrl_rev;
    }
}

float supply_screen_set_v(void) { return s.v_slider.value; }
float supply_screen_set_i(void) { return s.i_slider.value; }

void supply_screen_cancel_on(void)
{
    bool changed = false;
    if (s.pending.on) {
        s.pending.on = false;
        changed = true;
    }
    if (s.pressed == P_OUTPUT && !s.on) {
        s.pressed = P_NONE;
        s.have_press = false;
        changed = true;
    }
    if (s.hold.down || s.hold.held_s > 0.0f) {
        changed = true;
    }
    if (changed) {
        ui_hold_reset(&s.hold);
        ++s.out_rev;
        ++s.ctrl_rev;
    }
}

/* ------------------------------------------------------------------ events */

/* A set point moved by @p dv volts and @p di amps, snapped to the caps. */
static void nudge(float dv, float di)
{
    ui_slider_set(&s.v_slider, supply_snap(s.v_slider.value + dv, s.caps.v_min,
                                           s.caps.v_max, s.caps.v_step));
    ui_slider_set(&s.i_slider, supply_snap(s.i_slider.value + di, s.caps.i_min,
                                           s.caps.i_max, s.caps.i_step));
    ++s.set_rev;
}

static void event(const touch_event_t *evt)
{
    if (evt == NULL) {
        return;
    }
    if (ui_tabs_event(&s.tabs, evt)) {
        supply_invalidate();
    }
    if (ui_slider_event(&s.v_slider, evt)) {
        ui_slider_set(&s.v_slider, supply_snap(s.v_slider.value, s.caps.v_min,
                                               s.caps.v_max, s.caps.v_step));
        ++s.set_rev;
    }
    if (ui_slider_event(&s.i_slider, evt)) {
        ui_slider_set(&s.i_slider, supply_snap(s.i_slider.value, s.caps.i_min,
                                               s.caps.i_max, s.caps.i_step));
        ++s.set_rev;
    }

    const int x = evt->point.x, y = evt->point.y;
    if (evt->type == TOUCH_EVENT_DOWN) {
        /* One press at a time: a second contact on any button would take
         * over the record of the first, and the first one's release -- an
         * OUTPUT OFF -- would then be ignored. */
        if (s.have_press && evt->point.id != s.press_id) {
            return;
        }
        const struct { gfx_rect_t r; int code; float dv, di; } k_steps[] = {
            { s.v_down, P_V_DOWN, -NUDGE_V, 0.0f },
            { s.v_up,   P_V_UP,    NUDGE_V, 0.0f },
            { s.i_down, P_I_DOWN,  0.0f, -NUDGE_I },
            { s.i_up,   P_I_UP,    0.0f,  NUDGE_I },
        };
        for (size_t k = 0; k < sizeof(k_steps) / sizeof(k_steps[0]); ++k) {
            if (gfx_rect_contains(k_steps[k].r, x, y)) {
                /* On the press: a fine step should feel immediate. */
                nudge(k_steps[k].dv, k_steps[k].di);
                s.have_press = true;
                s.press_id   = evt->point.id;
                s.pressed    = k_steps[k].code;
                ++s.ctrl_rev;
                return;
            }
        }
        if (gfx_rect_contains(s.out_rect, x, y)) {
            if (s.pressed == P_OUTPUT) {
                return;   /* the hold belongs to the contact that began it */
            }
            s.have_press = true;
            s.press_id   = evt->point.id;
            s.pressed    = P_OUTPUT;
            if (!s.on) {
                ui_hold_begin(&s.hold);
            }
            ++s.out_rev;
            ++s.ctrl_rev;
        } else if (gfx_rect_contains(s.reset_rect, x, y)) {
            s.have_press = true;
            s.press_id   = evt->point.id;
            s.pressed    = P_RESET;
            ++s.ctrl_rev;
        }
        return;
    }
    if (!s.have_press || evt->point.id != s.press_id) {
        return;   /* a second finger cannot steal the first one's release */
    }
    if (evt->type == TOUCH_EVENT_MOVE) {
        /* A finger that leaves the switch abandons the hold. */
        if (s.pressed == P_OUTPUT && !s.on
            && !gfx_rect_contains(s.out_rect, x, y)
            && ui_hold_leave(&s.hold)) {
            s.pressed = P_NONE;
            ++s.out_rev;
            ++s.ctrl_rev;
        }
        return;
    }
    if (evt->type == TOUCH_EVENT_UP) {
        const int was = s.pressed;
        s.have_press = false;
        s.pressed = P_NONE;
        if (was != P_NONE) {
            ++s.ctrl_rev;
        }
        if (was == P_OUTPUT) {
            const bool fired = ui_hold_end(&s.hold);
            ++s.out_rev;
            /* Off is a tap; on is a hold that has already sent its command
             * by the time the finger lifts. */
            if (s.on && !fired && gfx_rect_contains(s.out_rect, x, y)) {
                post_off();
            }
        }
        if (was == P_RESET && gfx_rect_contains(s.reset_rect, x, y)) {
            s.pending.reset = true;
        }
    }
}

static void tick(float dt_s)
{
    if (s.pressed == P_OUTPUT && !s.on) {
        ++s.out_rev;
        if (ui_hold_tick(&s.hold, dt_s)) {
            post_on();
        }
    }
    if (s.hold.flash_left > 0) {
        ++s.out_rev;
    }
}

/* ----------------------------------------------------------------- drawing */

static gfx_color_t out_fill(void)
{
    /* Off is the OK green the hold fades from; on is the danger red, so the
     * fade previews the colour the switch is about to hold. */
    gfx_color_t fill = s.on ? ui_theme_color(UI_C_DANGER)
                            : ui_theme_color(UI_C_OK);
    if (s.hold.flash_left > 0) {
        return ui_hold_flash(ui_theme_color(UI_C_DANGER), s.hold.flash_left);
    }
    if (!s.on && s.hold.held_s > 0.0f) {
        fill = ui_hold_fill(fill, ui_theme_color(UI_C_DANGER), s.hold.held_s);
    }
    return fill;
}

static void out_flash_advance(void)
{
    if (s.hold.flash_left > 0) {
        ui_hold_flash_step(&s.hold);
        ++s.out_rev;
    }
}

static void draw_out_button(gfx_canvas_t *c)
{
    ui_button(c, s.out_rect, s.on ? "OUTPUT OFF" : "OUTPUT ON", out_fill(),
              s.pressed == P_OUTPUT, true);
    out_flash_advance();
}

static const char *mode_text(void)
{
    if (!s.sup.online) {
        return "--";
    }
    switch (s.sup.mode) {
    case SUPPLY_MODE_CV: return "CV";
    case SUPPLY_MODE_CC: return "CC";
    case SUPPLY_MODE_OFF:
    default:             return "OFF";
    }
}

/* The strip above both columns: where the numbers come from, and its state. */
static void draw_header(gfx_canvas_t *c)
{
    const int x0 = LEFT_X + TAB_W + 10;
    gfx_fill_rect(c, x0, HDR_Y, W - PAD - x0, HDR_H, ui_theme_color(UI_C_BG));
    char line[64];
    snprintf(line, sizeof(line), "%s   %s",
             s.model ? "SUPPLY MODEL" : "PD MINI",
             s.sup.online ? "ONLINE" : "NOT ANSWERING");
    gfx_text(c, x0, HDR_Y + 1, line, &gfx_font_8x16,
             s.sup.online ? ui_theme_color(UI_C_TEXT_DIM)
                          : ui_theme_color(UI_C_WARN), 1);
    char range[64];
    snprintf(range, sizeof(range), "%.1f-%.0f V  %.1f-%.0f A",
             (double)s.caps.v_min, (double)s.caps.v_max,
             (double)s.caps.i_min, (double)s.caps.i_max);
    gfx_text_in(c, (gfx_rect_t){ (int16_t)x0, HDR_Y + 1,
                                 (int16_t)(W - PAD - x0), 16 },
                range, &gfx_font_8x16, ui_theme_color(UI_C_TEXT_DIM), 1,
                GFX_ALIGN_RIGHT);
}

/* One card of the rail, as MOTOR & ESC draws them. */
static void draw_card(gfx_canvas_t *c, gfx_rect_t r, const char *label,
                      const char *unit, gfx_color_t color, int decimals,
                      float value, const char *extreme, float peak)
{
    gfx_fill_rect(c, r.x, r.y, r.w, r.h, ui_theme_color(UI_C_BG));
    ui_card(c, r, ui_theme_color(UI_C_PANEL));
    gfx_hline(c, r.x + UI_R_CARD, r.y + 1, r.w - 2 * UI_R_CARD, color);
    gfx_fill_round_rect(c, r.x + 8, r.y + 7, 3, 10, 1, color);
    gfx_text(c, r.x + 16, r.y + 6, label, &gfx_font_8x16,
             ui_theme_color(UI_C_TEXT), 1);
    if (extreme != NULL && isfinite(peak)) {
        char pk[24], line[32];
        ui_fmt(pk, sizeof(pk), peak, decimals);
        snprintf(line, sizeof(line), "%s %s", extreme, pk);
        gfx_text(c, r.x + 8, r.y + 26, line, &gfx_font_8x16,
                 ui_theme_color(UI_C_TEXT_FAINT), 1);
    }
    char number[24];
    if (isfinite(value)) {
        ui_fmt(number, sizeof(number), value, decimals);
    } else {
        snprintf(number, sizeof(number), "---");
    }
    const gfx_seg_style_t seg = { .digit_w = 15, .digit_h = 24,
                                  .thickness = 3, .gap = 4, .slant = 2,
                                  .ghost = true };
    const int uw = gfx_text_width(&gfx_font_8x16, unit, 1);
    const int nw = gfx_seg_width(number, &seg);
    const int nx = r.x + r.w - 8 - uw - 6 - nw;
    gfx_seg_text(c, nx, r.y + 16, number, &seg, color,
                 gfx_lerp(ui_theme_color(UI_C_PANEL), color, 34));
    gfx_text(c, nx + nw + 6, r.y + 30, unit, &gfx_font_8x16,
             ui_theme_color(UI_C_TEXT_DIM), 1);
}

static gfx_rect_t card_rect(int i)
{
    return (gfx_rect_t){ RIGHT_X, (int16_t)(UP_Y + i * (CARD_H + CARD_GAP)),
                         RIGHT_W, CARD_H };
}

static void draw_cards(gfx_canvas_t *c)
{
    const bool v_ok = s.sup.online && (s.sup.ok & SUPPLY_OK_VOLTAGE) != 0u;
    const bool i_ok = s.sup.online && (s.sup.ok & SUPPLY_OK_CURRENT) != 0u;
    const bool run = s.sup.output || s.plot.filled > 0;
    draw_card(c, card_rect(0), "VOLT", "V", ui_theme_color(UI_C_VOLT), 2,
              v_ok ? s.sup.v : NAN, "min",
              (v_ok && run && s.sup.sag_seeded) ? s.sup.v_min : NAN);
    draw_card(c, card_rect(1), "CURR", "A", ui_theme_color(UI_C_CURR), 2,
              i_ok ? s.sup.i : NAN, "pk", (i_ok && run) ? s.sup.i_max : NAN);
    draw_card(c, card_rect(2), "PWR", "W", ui_theme_color(UI_C_POWER), 1,
              (v_ok && i_ok) ? s.sup.p : NAN, "pk",
              (v_ok && i_ok && run) ? s.sup.p_max : NAN);

    /* The mode, in letters: which of the two set points the supply is
     * holding.  CC in the warning colour, because a supply at its limit is
     * not giving the load the voltage it was set to. */
    const gfx_rect_t r = card_rect(3);
    gfx_fill_rect(c, r.x, r.y, r.w, r.h, ui_theme_color(UI_C_BG));
    ui_card(c, r, ui_theme_color(UI_C_PANEL));
    const gfx_color_t mc = (s.sup.online && s.sup.mode == SUPPLY_MODE_CC)
                               ? ui_theme_color(UI_C_WARN)
                               : ui_theme_color(UI_C_ACCENT);
    gfx_hline(c, r.x + UI_R_CARD, r.y + 1, r.w - 2 * UI_R_CARD, mc);
    gfx_fill_round_rect(c, r.x + 8, r.y + 7, 3, 10, 1, mc);
    gfx_text(c, r.x + 16, r.y + 6, "MODE", &gfx_font_8x16,
             ui_theme_color(UI_C_TEXT), 1);
    gfx_text(c, r.x + 8, r.y + 26,
             (s.sup.online && s.sup.mode == SUPPLY_MODE_CC) ? "at the limit"
             : (s.sup.online && s.sup.mode == SUPPLY_MODE_CV) ? "at the voltage"
                                                              : "",
             &gfx_font_8x16, ui_theme_color(UI_C_TEXT_FAINT), 1);
    gfx_text_in(c, (gfx_rect_t){ (int16_t)(r.x + 8), (int16_t)(r.y + 12),
                                 (int16_t)(r.w - 16), 32 },
                mode_text(), &gfx_font_8x16, mc, 2, GFX_ALIGN_RIGHT);
}

static void draw_table(gfx_canvas_t *c)
{
    gfx_text(c, PLOT_X, LEG_Y + 4, "CHANNEL        SET            NOW",
             &gfx_font_8x16, ui_theme_color(UI_C_TEXT_DIM), 1);
    ui_rule(c, PLOT_X, LEG_Y + 24, PLOT_W, ui_theme_color(UI_C_EDGE));
    const bool v_ok = s.sup.online && (s.sup.ok & SUPPLY_OK_VOLTAGE) != 0u;
    const bool i_ok = s.sup.online && (s.sup.ok & SUPPLY_OK_CURRENT) != 0u;
    static const char *const rows[S_COUNT] = { "VOLTAGE", "CURRENT", "POWER" };
    const float set[S_COUNT] = { s.v_slider.value, s.i_slider.value, NAN };
    const float now[S_COUNT] = { v_ok ? s.sup.v : NAN, i_ok ? s.sup.i : NAN,
                                 (v_ok && i_ok) ? s.sup.p : NAN };
    for (int i = 0; i < S_COUNT; ++i) {
        const int y = LEG_Y + 34 + i * 30;
        char a[24], b[24];
        if (isfinite(set[i])) {
            ui_fmt(a, sizeof(a), set[i], k_series[i].decimals);
        } else {
            snprintf(a, sizeof(a), "--");
        }
        if (isfinite(now[i])) {
            ui_fmt(b, sizeof(b), now[i], k_series[i].decimals);
        } else {
            snprintf(b, sizeof(b), "--");
        }
        gfx_text(c, PLOT_X, y, rows[i], &gfx_font_8x16,
                 s.plot.series[i].color, 1);
        gfx_text(c, PLOT_X + 128, y, a, &gfx_font_8x16,
                 ui_theme_color(UI_C_TEXT), 1);
        gfx_text(c, PLOT_X + 248, y, b, &gfx_font_8x16,
                 ui_theme_color(UI_C_TEXT), 1);
        gfx_text(c, PLOT_X + 348, y, k_series[i].unit, &gfx_font_8x16,
                 ui_theme_color(UI_C_TEXT_DIM), 1);
    }
}

static void draw_totals(gfx_canvas_t *c)
{
    const gfx_rect_t box = { (int16_t)(RIGHT_X + INNER), TOTALS_Y,
                             (int16_t)(RIGHT_W - 2 * INNER), 16 };
    gfx_fill_rect(c, box.x, box.y, box.w, box.h, ui_theme_color(UI_C_PANEL));
    char mah[16], wh[16];
    if ((s.sup.counted & BENCH_COUNTED_CHARGE) != 0u) {
        snprintf(mah, sizeof(mah), "%.0f", (double)s.sup.charge_mah);
    } else {
        snprintf(mah, sizeof(mah), "--");
    }
    if ((s.sup.counted & BENCH_COUNTED_ENERGY) != 0u) {
        snprintf(wh, sizeof(wh), "%.2f", (double)s.sup.energy_wh);
    } else {
        snprintf(wh, sizeof(wh), "--");
    }
    char line[48];
    snprintf(line, sizeof(line), "%s mAh   %s Wh", mah, wh);
    gfx_text_in(c, box, line, &gfx_font_8x16, ui_theme_color(UI_C_TEXT), 1,
                GFX_ALIGN_RIGHT);
}

/* The two set points: label and value, then the step buttons and the track. */
static void draw_setters(gfx_canvas_t *c)
{
    static const char *const label[2] = { "VOLTAGE", "CURRENT LIMIT" };
    const float value[2] = { s.v_slider.value, s.i_slider.value };
    const char *unit[2] = { "V", "A" };
    const gfx_color_t col[2] = { ui_theme_color(UI_C_VOLT),
                                 ui_theme_color(UI_C_CURR) };
    for (int row = 0; row < 2; ++row) {
        const int y = SET_Y0 + row * SET_ROW_H;
        gfx_fill_rect(c, LEFT_X + INNER, y, LEFT_W - 2 * INNER, SET_TEXT_H,
                      ui_theme_color(UI_C_PANEL));
        gfx_text(c, LEFT_X + INNER, y + 2, label[row], &gfx_font_8x16,
                 ui_theme_color(UI_C_TEXT_DIM), 1);
        char v[24];
        snprintf(v, sizeof(v), "%.2f %s", (double)value[row], unit[row]);
        gfx_text_in(c, (gfx_rect_t){ (int16_t)(LEFT_X + INNER), (int16_t)y,
                                     (int16_t)(LEFT_W - 2 * INNER),
                                     SET_TEXT_H },
                    v, &gfx_font_8x16, col[row], 1, GFX_ALIGN_RIGHT);
        const ui_slider_t *sl = (row == 0) ? &s.v_slider : &s.i_slider;
        const gfx_rect_t pr = ui_slider_painted_rect(sl);
        gfx_fill_rect(c, pr.x, pr.y, pr.w, pr.h, ui_theme_color(UI_C_PANEL));
        ui_slider_render(sl, c);
    }
}

static void draw_steps(gfx_canvas_t *c)
{
    ui_button(c, s.v_down, "-0.1", ui_theme_color(UI_C_PANEL_SUNK),
              s.pressed == P_V_DOWN, true);
    ui_button(c, s.v_up, "+0.1", ui_theme_color(UI_C_PANEL_SUNK),
              s.pressed == P_V_UP, true);
    ui_button(c, s.i_down, "-0.1", ui_theme_color(UI_C_PANEL_SUNK),
              s.pressed == P_I_DOWN, true);
    ui_button(c, s.i_up, "+0.1", ui_theme_color(UI_C_PANEL_SUNK),
              s.pressed == P_I_UP, true);
}

enum { TAG_LIVE = 0, TAG_HELD, TAG_IDLE };
static const char *const k_tag[] = { "LIVE OUTPUT", "OUTPUT HELD", "OUTPUT IDLE" };

static uint8_t output_tag(void)
{
    if (s.tabs.selected != SUPPLY_PANE_PLOT || s.plot.running) {
        return (uint8_t)TAG_LIVE;
    }
    return (uint8_t)((s.plot.filled > 0) ? TAG_HELD : TAG_IDLE);
}

static void render(gfx_canvas_t *c, int buffer_index)
{
    const unsigned bit = 1u << (buffer_index & 1);
    const int buf = buffer_index & 1;
    bind_colours();

    const uint8_t tag = output_tag();
    if ((s.drawn_mask & bit) == 0) {
        gfx_clear(c, ui_theme_color(UI_C_BG));
        ui_panel(c, (gfx_rect_t){ LEFT_X, UP_Y, LEFT_W, UP_H }, k_tag[tag],
                 ui_theme_color(UI_C_ACCENT));
        ui_panel(c, (gfx_rect_t){ LEFT_X, LO_Y, LEFT_W, LO_H }, "SET POINTS",
                 ui_theme_color(UI_C_ACCENT));
        ui_panel(c, (gfx_rect_t){ RIGHT_X, LO_Y, RIGHT_W, LO_H }, "CONTROL",
                 ui_theme_color(UI_C_ACCENT));
        s.drawn_mask |= bit;
        s.drawn_title[buf] = tag;
    }
    if (s.drawn_title[buf] != tag) {
        s.drawn_title[buf] = tag;
        ui_panel_header(c, (gfx_rect_t){ LEFT_X, UP_Y, LEFT_W, UP_H },
                        k_tag[tag], ui_theme_color(UI_C_ACCENT));
    }
    const bool data_moved = (s.drawn_push[buf] != s.plot.pushes);
    const bool ctrl_moved = (s.drawn_ctrl[buf] != s.ctrl_rev);
    const bool set_moved  = (s.drawn_set[buf] != s.set_rev);
    const bool out_moved  = (s.drawn_out[buf] != s.out_rev);

    if (ctrl_moved || data_moved) {
        ui_tabs_render(&s.tabs, c);
        draw_header(c);
    }

    if (s.tabs.selected == SUPPLY_PANE_PLOT) {
        if (s.drawn_plot[buf] != s.plot.revision) {
            s.drawn_plot[buf] = s.plot.revision;
            gfx_fill_rect(c, PLOT_X, LEG_Y, PLOT_W, LEG_H,
                          ui_theme_color(UI_C_PANEL));
            ui_plot_render_legend(&s.plot, c,
                                  (gfx_rect_t){ PLOT_X, LEG_Y, PLOT_W, LEG_H });
            ui_plot_render(&s.plot, c,
                           (gfx_rect_t){ PLOT_X, PLOT_Y, PLOT_W, PLOT_H });
            if (!s.plot.running && s.plot.filled == 0) {
                gfx_text_in(c, (gfx_rect_t){ PLOT_X, PLOT_Y, PLOT_W, PLOT_H },
                            "output not switched on yet", &gfx_font_8x16,
                            ui_theme_color(UI_C_TEXT_FAINT), 1,
                            GFX_ALIGN_CENTER);
            }
        }
    } else if (data_moved || set_moved) {
        gfx_fill_rect(c, LEFT_X + 1, UP_Y + TITLE_H, LEFT_W - 2,
                      UP_H - TITLE_H - 1 - UI_CHAMFER,
                      ui_theme_color(UI_C_PANEL));
        gfx_fill_rect(c, LEFT_X + 1, UP_Y + UP_H - 1 - UI_CHAMFER,
                      LEFT_W - 2 - UI_CHAMFER, UI_CHAMFER,
                      ui_theme_color(UI_C_PANEL));
        draw_table(c);
    }
    if (data_moved) {
        s.drawn_push[buf] = s.plot.pushes;
        draw_cards(c);
        draw_totals(c);
    }

    if (!ctrl_moved && !set_moved && !out_moved) {
        return;
    }
    s.drawn_ctrl[buf] = s.ctrl_rev;
    s.drawn_set[buf]  = s.set_rev;
    s.drawn_out[buf]  = s.out_rev;

    /* The switch animating on its own repaints its own button only. */
    if (out_moved && !ctrl_moved && !set_moved) {
        draw_out_button(c);
        return;
    }
    if (set_moved || ctrl_moved) {
        draw_setters(c);
    }
    if (ctrl_moved) {
        draw_steps(c);
        draw_totals(c);
        ui_button(c, s.reset_rect, "RESET PEAKS",
                  ui_theme_color(UI_C_PANEL_SUNK), s.pressed == P_RESET, true);
    }
    if (ctrl_moved || out_moved) {
        draw_out_button(c);
    }
}

/*
 * Leaving keeps the output as it is.  A supply feeding a servo rail is what
 * the SERVO screen tests against, so walking there must not cut it; STOP
 * does, and so does a supply that stops answering.  The press this screen
 * was routing goes, for the reason MOTOR & ESC's leave() gives.
 */
static void leave(void)
{
    ui_slider_release(&s.v_slider);
    ui_slider_release(&s.i_slider);
    ui_hold_reset(&s.hold);
    s.pressed    = P_NONE;
    s.have_press = false;
    ++s.ctrl_rev;
    ++s.out_rev;
}

/*
 * Touch events were lost.  Every gesture is dropped, which asks for nothing,
 * except a tap on OUTPUT OFF: switching off is a press, so a release lost is
 * an OFF the operator made, and it is sent.  An ON this screen has posted and
 * the application has not collected is dropped.
 */
static void cancel(void)
{
    if (s.on && s.pressed == P_OUTPUT && !s.hold.fired) {
        post_off();
    }
    s.pending.on = false;
    ui_slider_release(&s.v_slider);
    ui_slider_release(&s.i_slider);
    ui_hold_reset(&s.hold);
    ui_tabs_cancel(&s.tabs);
    s.pressed    = P_NONE;
    s.have_press = false;
    ++s.ctrl_rev;
    ++s.out_rev;
}

static const ui_screen_t k_screen = {
    .title  = "SUPPLY",
    .reset  = reset,
    .enter  = NULL,
    .leave  = leave,
    .tick   = tick,
    .event  = event,
    .cancel = cancel,
    .render = render,
};

const ui_screen_t *supply_screen(void) { return &k_screen; }
