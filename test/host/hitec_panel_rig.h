/*
 * The bench the Hitec programmer's overlay suites run on: the overlay open
 * on SERVO's left card over the loopback of hitec_prog_rig.h, a canvas,
 * touches and holds fed to the overlay with the bench running, and the
 * readers of what it shows.
 *
 * For test_hitec_panel.c and test_hitec_panel_states.c.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "hitec_prog_rig.h"

#include <stdlib.h>

#include "gfx.h"
#include "settings.h"
#include "supply_screen.h"
#include "ui_layout.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

/* The SERVO screen's left card, as servo_screen.c hands it over. */
static const gfx_rect_t AREA = { UI_MARGIN, UI_MARGIN, 620,
                                  UI_BODY_H - 2 * UI_MARGIN };

/* The alert band's top in body pixels, and the 4px a control at the foot
 * keeps above it. */
#define BAND_TOP (UI_ALERT_Y - UI_BAND_H)
#define FOOT     (BAND_TOP - 4)

static gfx_color_t *fb;
static gfx_canvas_t cv;

static inline void show(void)
{
    hitec_panel_render(&cv);
}

/* The overlay open on a fresh bench at @p supply, in @p lang. */
static inline void open_at(supply_kind_t supply, ui_lang_t lang)
{
    if (fb == NULL) {
        fb = calloc((size_t)UI_PANEL_W * UI_PANEL_H, sizeof(gfx_color_t));
        REQUIRE_ALLOC(fb);
    }
    gfx_canvas_init(&cv, fb, UI_PANEL_W, UI_PANEL_H, UI_PANEL_W);
    ui_theme_set(UI_THEME_DARK);
    ui_text_set_language(lang);
    settings_set_store(NULL);
    settings_init();
    supply_screen()->reset();
    supply_screen_settings_loaded();
    hitec_panel_init(AREA);
    hitec_panel_open();
    rig_at(supply, true);
    show();
}

static inline void open_en(void)
{
    open_at(SUPPLY_ON, UI_LANG_EN);
}

static inline void ev(int x, int y, touch_event_type_t t)
{
    const touch_event_t e = { .type = t,
                              .point = { .id = 1, .x = (int16_t)x,
                                         .y = (int16_t)y, .strength = 40 } };

    hitec_panel_event(&e);
}

static inline int cx(gfx_rect_t r) { return r.x + r.w / 2; }
static inline int cy(gfx_rect_t r) { return r.y + r.h / 2; }

static inline void tap(gfx_rect_t r)
{
    ev(cx(r), cy(r), TOUCH_EVENT_DOWN);
    ev(cx(r), cy(r), TOUCH_EVENT_UP);
    show();
}

/* The hold page's button held @p secs, the bench running meanwhile. */
static inline void hold(float secs)
{
    const gfx_rect_t r = hitec_panel_apply_rect();
    const int steps = (int)(secs * 50.0f + 0.5f);

    ev(cx(r), cy(r), TOUCH_EVENT_DOWN);
    for (int i = 0; i < steps; ++i) {
        hitec_panel_tick(0.02f);
        prun(20u);
    }
    ev(cx(r), cy(r), TOUCH_EVENT_UP);
    show();
}

static inline gfx_rect_t val(int page, int row, int col)
{
    return hitec_panel_value_rect(page, row, col);
}

/* The first line of the message, "" for none. */
static inline const char *said(void)
{
    static char t[HITEC_PANEL_MSG_LINES][HITEC_PANEL_LINE];

    return hitec_panel_message_lines(hitec_prog_message(model()), t) > 0
               ? t[0]
               : "";
}

/* CONNECT by its button, run to HELD. */
static inline void tap_connect(void)
{
    CHECK_EQ(strcmp(hitec_panel_value(HITEC_PANEL_SERVO, 0, 0),
                    TR(KP_CONNECT)),
             0);
    tap(val(HITEC_PANEL_SERVO, 0, 0));
    CHECK(pstate() != HITEC_PROG_PWM);
    settled();
    show();
}

