/*
 * SPDX-License-Identifier: MIT
 */

#include "outputs_screen.h"
#include "picker_screen.h"
#include "ui_screen.h"

#include <stdio.h>
#include <string.h>

#include "log_viewer_screen.h"
#include "motor_screen.h"
#include "analyser_screen.h"
#include "balance_screen.h"
#include "battery_screen.h"
#include "programmer_screen.h"
#include "servo_screen.h"
#include "settings_screen.h"
#include "overview_screen.h"
#include "splash_screen.h"
#include "stub_screen.h"
#include "ui_band.h"
#include "ui_theme.h"
#include "ui_watermark.h"
#include "ui_widgets.h"

#define PANEL_W 800
#define PANEL_H 480

static struct {
    ui_screen_id_t    current;
    ui_bench_status_t status;
    char              alert[UI_ALERT_MAX];
    bool              has_alert;
    bool              stop_latched;

    /* Which contact owns a press on the band.  The GT911 reports up to five,
     * and every event carries its track id, so a second finger or a resting
     * palm cannot steal the release the first one is waiting for. */
    bool    band_press;
    uint8_t band_id;
    bool    on_stop;      /**< the press started on STOP rather than home   */
} s;

static const ui_screen_t *screen_for(ui_screen_id_t id)
{
    switch (id) {
    case SCREEN_SPLASH:   return splash_screen();
    case SCREEN_OVERVIEW: return overview_screen();
    case SCREEN_MOTOR:    return motor_screen();
    case SCREEN_SERVO:    return servo_screen();
    case SCREEN_ANALYSER: return analyser_screen();
    case SCREEN_BALANCE:  return balance_screen();
    case SCREEN_BATTERY:  return battery_screen();
    case SCREEN_PROGRAMMER: return programmer_screen();
    case SCREEN_LOGS:     return log_viewer_screen();
    case SCREEN_SETUP:    return settings_screen();
    case SCREEN_OUTPUTS:  return outputs_screen();
    case SCREEN_PICKER:   return picker_screen();
    default:              return stub_screen(id);
    }
}

const char *ui_router_title(ui_screen_id_t id)
{
    const ui_screen_t *s_ = screen_for(id);
    return (s_ != NULL && s_->title != NULL) ? s_->title : "";
}

void ui_router_init(void)
{
    memset(&s, 0, sizeof(s));
    for (int id = 0; id < SCREEN_COUNT; ++id) {
        const ui_screen_t *scr = screen_for((ui_screen_id_t)id);
        if (scr != NULL && scr->reset != NULL) {
            scr->reset();
        }
    }
    s.current = SCREEN_SPLASH;
    const ui_screen_t *scr = screen_for(s.current);
    if (scr != NULL && scr->enter != NULL) {
        scr->enter();
    }
    ui_router_invalidate();
}

void ui_router_goto(ui_screen_id_t id)
{
    if (id < 0 || id >= SCREEN_COUNT || id == s.current) {
        return;
    }
    const ui_screen_t *old = screen_for(s.current);
    if (old != NULL && old->leave != NULL) {
        old->leave();
    }
    s.current = id;
    const ui_screen_t *now = screen_for(id);
    if (now != NULL && now->enter != NULL) {
        now->enter();
    }
    /* A press that began on the old screen must not land on the new one. */
    s.band_press = false;
    ui_router_invalidate();
}

ui_screen_id_t ui_router_current(void) { return s.current; }

void ui_router_invalidate(void)
{
    splash_invalidate();
    overview_invalidate();
    stub_invalidate();
    motor_invalidate();
    log_viewer_invalidate();
    settings_screen_invalidate();
    analyser_invalidate();
    balance_invalidate();
    battery_invalidate();
    programmer_invalidate();
    servo_invalidate();
    outputs_screen_invalidate();
    picker_screen_invalidate();
}

void ui_router_tick(float dt_s)
{
    const ui_screen_t *scr = screen_for(s.current);
    if (scr != NULL && scr->tick != NULL) {
        scr->tick(dt_s);
    }
}

void ui_router_set_status(const ui_bench_status_t *status)
{
    if (status == NULL) {
        return;
    }
    /*
     * Screens cache their chrome per framebuffer, so a status change that a
     * screen draws has to invalidate them.  The watermark covers the whole
     * canvas, and the menu's tile badges come from the capability bitmap, so
     * a change of either repaints everything.
     */
    const bool     was  = s.status.simulated;
    const uint16_t caps = s.status.capabilities;
    s.status = *status;
    if (was != s.status.simulated || caps != s.status.capabilities) {
        ui_router_invalidate();
    }
}

const ui_bench_status_t *ui_router_status(void) { return &s.status; }

bool ui_router_take_stop(void)
{
    const bool was = s.stop_latched;
    s.stop_latched = false;
    return was;
}

/*
 * Every screen except the splash carries the band, the overview included: the
 * bench can be armed while the menu is showing.  The home tag is on every
 * screen except the splash and the overview.
 */
static bool has_band(ui_screen_id_t id) { return id != SCREEN_SPLASH; }

/*
 * Whether a STOP button is on the screen at all.  The splash carries no band
 * and therefore no STOP, and anything hit-testing the band's rectangle
 * without asking would latch a stop on a tap that pressed nothing.
 */
bool ui_router_stop_live(void) { return has_band(s.current); }
static bool has_home(ui_screen_id_t id)
{
    return id != SCREEN_SPLASH && id != SCREEN_OVERVIEW;
}

void ui_router_event(const touch_event_t *evt)
{
    if (evt == NULL) {
        return;
    }
    const ui_screen_t *scr = screen_for(s.current);

    if (has_band(s.current)) {
        const gfx_rect_t stop = ui_band_stop_rect();
        const gfx_rect_t home = ui_home_tag_rect(ui_router_title(s.current));
        const bool in_stop = gfx_rect_contains(stop, evt->point.x,
                                               evt->point.y);
        const bool in_home = has_home(s.current)
                             && gfx_rect_contains(home, evt->point.x,
                                                  evt->point.y);

        if (evt->type == TOUCH_EVENT_DOWN && (in_stop || in_home)) {
            s.band_press = true;
            s.band_id    = evt->point.id;
            s.on_stop    = in_stop;
            return;   /* consumed before the screen sees it */
        }
        if (s.band_press && evt->point.id == s.band_id) {
            if (evt->type == TOUCH_EVENT_UP) {
                s.band_press = false;
                if (s.on_stop && in_stop) {
                    /* Latched rather than dispatched: the application loop
                     * that drives the heartbeat drains it, so a stop also
                     * stops the line. */
                    s.stop_latched = true;
                } else if (!s.on_stop && in_home) {
                    ui_router_goto(SCREEN_OVERVIEW);
                }
            }
            return;
        }
        if (evt->point.y < UI_BAND_H) {
            /* Any other event on the band is consumed here, so no screen
             * receives an event with a negative y. */
            return;
        }
    }

    if (scr != NULL && scr->event != NULL) {
        /* Screens work in their own coordinates; the band's height is
         * removed here. */
        touch_event_t local = *evt;
        if (has_band(s.current)) {
            local.point.y = (int16_t)(local.point.y - UI_BAND_H);
        }
        scr->event(&local);
    }
}

void ui_router_set_alert(const char *text)
{
    if (text == NULL) {
        s.has_alert = false;
        s.alert[0]  = '\0';
        return;
    }
    snprintf(s.alert, sizeof(s.alert), "%s", text);
    s.has_alert = true;
}

const char *ui_router_alert(void)
{
    return s.has_alert ? s.alert : NULL;
}

#define ALERT_H 34

static void draw_alert(gfx_canvas_t *c, const char *text)
{
    const int y = PANEL_H - ALERT_H;
    gfx_fill_rect(c, 0, y, PANEL_W, ALERT_H, ui_theme_color(UI_C_DANGER));
    gfx_text(c, 12, y + 9, text, &gfx_font_8x16, ui_theme_color(UI_C_TEXT), 1);
}

void ui_router_render(gfx_canvas_t *c, int buffer_index)
{
    const ui_screen_t *scr = screen_for(s.current);
    if (scr == NULL || scr->render == NULL || c == NULL) {
        return;
    }

    if (!has_band(s.current)) {
        scr->render(c, buffer_index);
        if (s.status.simulated) {
            ui_watermark(c);
        }
        return;
    }

    ui_band_render(c, ui_router_title(s.current), has_home(s.current),
                   &s.status, s.band_press && s.on_stop,
                   s.band_press && !s.on_stop);

    /*
     * A sub-canvas clipped to the body, so a screen cannot draw over the band
     * or STOP whatever offsets it uses.
     */
    const gfx_rect_t body = { 0, UI_BAND_H, PANEL_W,
                              (int16_t)(PANEL_H - UI_BAND_H) };
    gfx_canvas_t sub;
    if (gfx_canvas_sub(c, body, &sub)) {
        scr->render(&sub, buffer_index);
    }

    if (s.has_alert) {
        draw_alert(c, s.alert);
    }

    /*
     * Last, over the band and the alert, so no screen can paint over the
     * watermark.
     */
    if (s.status.simulated) {
        ui_watermark(c);
    }
}
