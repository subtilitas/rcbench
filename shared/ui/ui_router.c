/*
 * SPDX-License-Identifier: MIT
 */

#include "busfault_screen.h"
#include "outputs_screen.h"
#include "picker_screen.h"
#include "supply_screen.h"
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
#define ALERT_H 34    /* the alert band, across the bottom */

static struct {
    ui_screen_id_t    current;
    uint32_t          nav_count;      /**< navigations since init           */
    uint32_t          dispatched;     /**< events handed to a screen        */
    ui_bench_status_t status;
    char              alert[UI_ALERT_MAX];
    bool              has_alert;
    float             alert_age_s;
    bool              alert_fresh;    /**< set since the last tick          */
    uint32_t          alert_gen;      /**< counts alerts set                */
    /* The held alert, apart from the passing one: a passing alert shows over
     * it and, once cleared, shows it again. */
    char              held[UI_ALERT_MAX];
    bool              has_held;
    bool              stop_latched;

    /* A press that began on the alert band, by track id, held until its
     * release even if the alert clears meanwhile: the screen beneath never
     * saw the press, so it must not see the release either. */
    bool     alert_press;
    uint8_t  alert_id;
    uint32_t alert_press_gen;   /**< the alert the press began on         */
    /* Every contact that came down on the alert band, owner or not, so a
     * second finger's events reach no screen either. */
    uint8_t  alert_ids[256u / 8u];

    /* Which contact owns a press on the band.  The GT911 reports up to five,
     * and every event carries its track id, so a second finger or a resting
     * palm cannot steal the release the first one is waiting for. */
    bool    band_press;
    uint8_t band_id;
    bool    on_stop;      /**< the press started on STOP rather than home   */

    /*
     * The contacts the screen on top owns: each one whose DOWN it was
     * handed, with the last point it was handed, in panel coordinates.  A
     * screen is handed a MOVE or an UP only for a contact in this table, so
     * a contact has one owner from its DOWN to its UP.  Every other contact
     * is the router's and reaches no screen.
     */
    struct {
        bool    live;
        uint8_t id;
        int16_t x, y;
    } owned[TOUCH_MAX_POINTS];
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
    case SCREEN_SUPPLY:   return supply_screen();
    case SCREEN_BUSFAULT: return busfault_screen();
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
    ++s.nav_count;
    const ui_screen_t *now = screen_for(id);
    if (now != NULL && now->enter != NULL) {
        now->enter();
    }
    /* A press that began on the old screen must not land on the new one:
     * the contacts it owned are nobody's until they lift. */
    s.band_press = false;
    s.alert_press = false;
    memset(s.alert_ids, 0, sizeof(s.alert_ids));
    memset(s.owned, 0, sizeof(s.owned));
    ui_router_invalidate();
}

ui_screen_id_t ui_router_current(void) { return s.current; }
uint32_t ui_router_navigations(void) { return s.nav_count; }
uint32_t ui_router_dispatched(void) { return s.dispatched; }

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
    busfault_screen_invalidate();
    supply_invalidate();
}

static void clear_alert(void)
{
    if (s.has_alert) {
        /* Screens cache what they drew per framebuffer, and the band was
         * drawn over it; without a repaint its red would stay. */
        ui_router_invalidate();
    }
    s.has_alert   = false;
    s.alert_fresh = false;
    s.alert_age_s = 0.0f;
    s.alert[0]    = '\0';
}

void ui_router_tick(float dt_s)
{
    /* The frame an alert arrives in does not count: its interval passed
     * before the alert was set, and after a long stall it would clear the
     * alert before it was ever drawn. */
    if (s.alert_fresh) {
        s.alert_fresh = false;
    } else if (s.has_alert && dt_s > 0.0f) {
        s.alert_age_s += dt_s;
        if (s.alert_age_s >= UI_ALERT_SHOW_S) {
            clear_alert();
        }
    }
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
static bool has_band(ui_screen_id_t id)
{
    /* The bus-fault screen carries none either. Its whole claim is that the
     * bench cannot be driven, and a STOP button on it would offer to stop
     * something that is not running. */
    return id != SCREEN_SPLASH && id != SCREEN_BUSFAULT;
}

/*
 * Whether a STOP button is on the screen at all.  The splash carries no band
 * and therefore no STOP, and anything hit-testing the band's rectangle
 * without asking would latch a stop on a tap that pressed nothing.
 */
bool ui_router_stop_live(void) { return has_band(s.current); }

static bool in_alert(const touch_event_t *evt)
{
    return evt->point.y >= PANEL_H - ALERT_H && evt->point.y < PANEL_H
           && evt->point.x >= 0 && evt->point.x < PANEL_W;
}

static bool has_home(ui_screen_id_t id)
{
    return id != SCREEN_SPLASH && id != SCREEN_OVERVIEW
           && id != SCREEN_BUSFAULT;
}

/* The slot of contact @p id in the screen's table, or -1 when the screen
 * does not own it. */
static int owned_find(uint8_t id)
{
    for (int i = 0; i < TOUCH_MAX_POINTS; ++i) {
        if (s.owned[i].live && s.owned[i].id == id) {
            return i;
        }
    }
    return -1;
}

/*
 * One owner per contact, from its DOWN to its UP.
 *
 * The screen owns a contact whose DOWN it was handed, and is handed nothing
 * of any other.  A contact that came down on the band, on the alert strip,
 * on another screen or before a touch loss is the router's wherever it goes:
 * its MOVE and its UP reach no screen.
 *
 * A contact the screen owns that reaches the band is released at the edge.
 * The screen is handed an UP at the last point it was handed, flagged
 * TOUCH_FLAG_NO_TAP: it ends the press or the drag and activates nothing.
 * The contact is the router's from then on.  The release is not the contact's
 * real UP with its y moved into the body: a slider applies the horizontal
 * distance on its release and would step by the travel made on the band.
 * A screen that never saw the gesture end keeps its drag latched to a track
 * id the controller reuses, and takes a later contact's travel for its own.
 *
 * STOP and the home tag answer to a DOWN on them and to nothing else, so a
 * contact that slides onto STOP from the body presses nothing.
 */
void ui_router_event(const touch_event_t *evt)
{
    if (evt == NULL) {
        return;
    }
    const uint8_t id   = evt->point.id;
    const bool    band = has_band(s.current);

    if (band) {
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
                } else if (!s.on_stop && in_home
                           && touch_event_is_tap_up(evt)) {
                    /* STOP above takes any release over it; the home tag
                     * takes one the finger made. */
                    ui_router_goto(SCREEN_OVERVIEW);
                }
            }
            return;
        }
    }

    const uint8_t bit = (uint8_t)(1u << (id & 7u));
    if ((s.alert_ids[id >> 3] & bit) != 0u) {
        /* Asked before the band takes anything: a contact that came down on
         * the alert strip and lifts on the band ends its press here too. */
        if (evt->type == TOUCH_EVENT_UP) {
            s.alert_ids[id >> 3] &= (uint8_t)~bit;
            /* Only the first contact's lift, and only on the alert that
             * was showing when it came down: one that arrived meanwhile has
             * not been read. */
            if (s.alert_press && id == s.alert_id) {
                s.alert_press = false;
                if (s.has_alert && in_alert(evt)
                    && touch_event_is_tap_up(evt)
                    && s.alert_gen == s.alert_press_gen) {
                    clear_alert();
                }
            }
        }
        return;
    }

    int slot = owned_find(id);
    if (evt->type == TOUCH_EVENT_DOWN && slot >= 0) {
        /* A DOWN is a new contact: the one this id named before is over. */
        s.owned[slot].live = false;
        slot = -1;
    }

    /* What the screen is handed: the event, or the release made for it. */
    touch_event_t local = *evt;

    if (band && evt->point.y < UI_BAND_H) {
        /* The band is the router's, so no screen receives an event with a
         * negative y.  A contact the screen owns ends where the screen last
         * saw it, and is not handed back when it returns to the body. */
        if (slot < 0) {
            return;
        }
        local.type    = TOUCH_EVENT_UP;
        local.flags   = TOUCH_FLAG_NO_TAP;
        local.point.x = s.owned[slot].x;
        local.point.y = s.owned[slot].y;
        s.owned[slot].live = false;
    } else if (evt->type == TOUCH_EVENT_DOWN) {
        if (s.has_alert && band && in_alert(evt)) {
            s.alert_ids[id >> 3] |= bit;
            if (!s.alert_press) {
                s.alert_press     = true;
                s.alert_id        = id;
                s.alert_press_gen = s.alert_gen;
            }
            return;   /* the band covers the screen here, so the screen gets none */
        }
        slot = 0;
        while (slot < TOUCH_MAX_POINTS && s.owned[slot].live) {
            ++slot;
        }
        if (slot == TOUCH_MAX_POINTS) {
            /* More contacts than the controller reports.  One the router
             * cannot follow to its release is handed to no screen. */
            return;
        }
        s.owned[slot].live = true;
        s.owned[slot].id   = id;
    } else if (slot < 0) {
        return;   /* the screen on top never saw this contact come down */
    }
    if (local.type == TOUCH_EVENT_UP) {
        s.owned[slot].live = false;
    } else {
        s.owned[slot].x = evt->point.x;
        s.owned[slot].y = evt->point.y;
    }

    const ui_screen_t *scr = screen_for(s.current);
    if (scr != NULL && scr->event != NULL) {
        /* Screens work in their own coordinates; the band's height is
         * removed here. */
        if (band) {
            local.point.y = (int16_t)(local.point.y - UI_BAND_H);
        }
        ++s.dispatched;
        scr->event(&local);
    }
}

void ui_router_cancel_gestures(void)
{
    /*
     * The band's own press first: HOME and STOP are the router's gesture,
     * not the screen's, and a band press left latched owns its track id.
     * The GT911 reuses ids, so a later contact that began on the screen
     * would be taken for this one's release and act on where it lifts.
     * The contacts the screen owned go the same way: their releases may
     * never come, so what is still on the glass is nobody's until it lifts
     * and comes down again.
     */
    s.band_press = false;
    memset(s.owned, 0, sizeof(s.owned));

    /*
     * Every screen, not the one on top.  The loss is observed after the
     * frame's events were dispatched, and one of those events can have
     * navigated: a HOME release that survived the loss leaves the screen
     * that took the earlier events holding its press, and the screen now on
     * top holding nothing.  A screen off the top keeps its gesture state
     * until it is entered again, so a tab press whose release went missing
     * would meet a recycled id on the next visit.  Cancelling a screen with
     * no gesture in progress asks for nothing, so every screen is told.
     * The alert band's presses go the same way: their releases may never
     * come.
     */
    s.alert_press = false;
    memset(s.alert_ids, 0, sizeof(s.alert_ids));
    for (int id = 0; id < SCREEN_COUNT; ++id) {
        const ui_screen_t *scr = screen_for((ui_screen_id_t)id);
        if (scr != NULL && scr->cancel != NULL) {
            scr->cancel();
        }
    }
}

void ui_router_set_alert(const char *text)
{
    if (text == NULL) {
        clear_alert();
        return;
    }
    snprintf(s.alert, sizeof(s.alert), "%s", text);
    s.has_alert   = true;
    s.alert_fresh = true;
    s.alert_age_s = 0.0f;
    ++s.alert_gen;
}

void ui_router_hold_alert(const char *text)
{
    if (s.has_held && !s.has_alert) {
        ui_router_invalidate();         /* the band's red, as clear_alert() */
    }
    s.has_held = (text != NULL);
    (void)snprintf(s.held, sizeof(s.held), "%s", s.has_held ? text : "");
}

const char *ui_router_alert(void)
{
    if (s.has_alert) {
        return s.alert;
    }
    return s.has_held ? s.held : NULL;
}

static void draw_alert(gfx_canvas_t *c, const char *text, bool closable)
{
    const int y = PANEL_H - ALERT_H;
    gfx_fill_rect(c, 0, y, PANEL_W, ALERT_H, ui_theme_color(UI_C_DANGER));
    gfx_text(c, 12, y + 9, text, &gfx_font_8x16, ui_theme_color(UI_C_TEXT), 1);
    if (closable) {
        gfx_text(c, PANEL_W - 12 - 8, y + 9, "x", &gfx_font_8x16,
                 ui_theme_color(UI_C_TEXT), 1);
    }
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

    if (s.has_alert || s.has_held) {
        draw_alert(c, ui_router_alert(), s.has_alert);
    }

    /*
     * Last, over the band and the alert, so no screen can paint over the
     * watermark.
     */
    if (s.status.simulated) {
        ui_watermark(c);
    }
}
