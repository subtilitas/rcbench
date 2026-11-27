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

/* CONNECT, a 2s hold: pressed for 2.5s, the hold and its 300ms flash,
 * and let go. */
static inline void hold_connect(void)
{
    const gfx_rect_t r = val(HITEC_PANEL_SERVO, 0, 0);

    ev(cx(r), cy(r), TOUCH_EVENT_DOWN);
    for (int i = 0; i < 125; ++i) {
        hitec_panel_tick(0.02f);
    }
    ev(cx(r), cy(r), TOUCH_EVENT_UP);
    show();
}

/* WRITE n VALUES on the SERVO tab pressed for @p secs, the servo run
 * beside, and let go: its 2s hold writes. */
static inline void press_write(float secs)
{
    const int steps = (int)(secs * 50.0f + 0.5f);
    gfx_rect_t r;

    tap(hitec_panel_tab_rect(HITEC_PANEL_SERVO));
    r = val(HITEC_PANEL_SERVO, 3, 1);
    ev(cx(r), cy(r), TOUCH_EVENT_DOWN);
    for (int i = 0; i < steps; ++i) {
        hitec_panel_tick(0.02f);
        prun(20u);
    }
    ev(cx(r), cy(r), TOUCH_EVENT_UP);
    show();
}

/* WRITE n VALUES held 2.5s: the hold, the write asked for on the tick
 * after it, and the flash. */
static inline void hold_write(void)
{
    press_write(2.5f);
}

/* The first line of the message, "" for none. */
static inline const char *said(void)
{
    static char t[HITEC_PANEL_MSG_LINES][HITEC_PANEL_LINE];

    return hitec_panel_message_lines(hitec_prog_message(model()), t) > 0
               ? t[0]
               : "";
}

/* PORT's button as it takes a press: CONNECT, in PWM, held 2s where it
 * switches a supply on; LEAVE, RELEASE, ABORT, CONNECT of a servo the
 * operator powers, and a button that takes nothing, tapped. */
static inline void press_port(void)
{
    prog_session_t ps;

    hitec_panel_session(&ps);
    if (pstate() == HITEC_PROG_PWM && ps.supply != (uint8_t)PROG_SES_AS_IS) {
        hold_connect();
    } else {
        tap(val(HITEC_PANEL_SERVO, 0, 0));
    }
}

/* The open list stands clear of CANCEL: each entry 48px or more, its
 * label inside it, none over another or over CANCEL, the last 8px above
 * CANCEL.  Returns its entries. */
static inline int list_clear(void)
{
    const gfx_rect_t cancel = hitec_panel_choice_cancel_rect();
    int n = 0;

    while (hitec_panel_choice_label(n) != NULL) {
        const gfx_rect_t r = hitec_panel_choice_rect(n);

        CHECK(r.w >= UI_TAP_MIN && r.h >= UI_TAP_MIN);
        CHECK(r.x >= AREA.x && r.x + r.w <= AREA.x + AREA.w);
        CHECK(gfx_text_cells(hitec_panel_choice_label(n)) * 10 + 8 <= r.w);
        CHECK(r.y + r.h <= cancel.y - 8);
        for (int j = 0; j < n; ++j) {
            const gfx_rect_t o = hitec_panel_choice_rect(j);

            CHECK(!(r.x < o.x + o.w && o.x < r.x + r.w && r.y < o.y + o.h
                    && o.y < r.y + r.h));
        }
        ++n;
    }
    return n;
}

/* The servo read as the session names it: the READY card's SERVO row. */
static inline const char *servo_name(void)
{
    static prog_session_t ps;

    hitec_panel_session(&ps);
    return ps.servo;
}

/* CONNECT by its button, run to HELD. */
static inline void tap_connect(void)
{
    CHECK_EQ(strcmp(hitec_panel_value(HITEC_PANEL_SERVO, 0, 0),
                    TR(KP_CONNECT)),
             0);
    hold_connect();
    CHECK(pstate() != HITEC_PROG_PWM);
    settled();
    show();
}

