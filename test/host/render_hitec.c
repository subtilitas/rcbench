/**
 * @file render_hitec.c
 * @brief The Hitec programmer's views for render_screen.c (render_hitec.h).
 *
 * The bench is hitec_prog_rig.h's: the panel's link module, the IOmcu's
 * HITEC page and the servo model, the control task's pass every 5ms and
 * the render loop's frame every 16ms feeding the overlay's model.  The
 * taps go through the router, as a finger's do.
 *
 * Hitec is a name or trademark of its owner; it is used here only to say
 * which servos these views show.
 *
 * SPDX-License-Identifier: MIT
 */
#include "render_hitec.h"

/* The rig's checks come with the suites' harness (greatest.h), whose
 * runner this file does not call. */
#pragma GCC diagnostic ignored "-Wunused-function"
#include "hitec_prog_rig.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "servo_screen.h"
#include "supply_screen.h"
#include "servo_spots.h"
#include "ui_layout.h"
#include "ui_screen.h"

/**
 * @brief A tap at (@p x, @p y), panel pixels.
 * @param x the x
 * @param y the y
 */
static void htap_at(int x, int y)
{
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = (int16_t)x,
                                   .y = (int16_t)y, .strength = 40 } };

    ui_router_event(&e);
    e.type = TOUCH_EVENT_UP;
    ui_router_event(&e);
    ui_router_tick(0.02f);
}

/**
 * @brief A tap on an overlay control, body pixels.
 * @param r the control; one of no size names none and ends the render
 */
static void htap(gfx_rect_t r)
{
    if (r.w <= 0 || r.h <= 0) {
        fprintf(stderr, "render_hitec: a tap names no control\n");
        exit(1);
    }
    htap_at(r.x + r.w / 2, UI_BAND_H + r.y + r.h / 2);
}

/**
 * @brief The hold page's button held 2.1s, the bench running meanwhile.
 */
static void hhold(void)
{
    const gfx_rect_t r = hitec_panel_apply_rect();
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1,
                                   .x = (int16_t)(r.x + r.w / 2),
                                   .y = (int16_t)(UI_BAND_H + r.y
                                                  + r.h / 2),
                                   .strength = 40 } };

    ui_router_event(&e);
    for (int i = 0; i < 105; ++i) {
        ui_router_tick(0.02f);
        prun(20u);
    }
    e.type = TOUCH_EVENT_UP;
    ui_router_event(&e);
}

/**
 * @brief Run the bench until the model is in @p s, or end the render.
 * @param s the state
 */
static void hwait(hitec_prog_state_t s)
{
    for (unsigned i = 0; i < 60000u && pstate() != s; ++i) {
        pms();
    }
    if (pstate() != s) {
        fprintf(stderr, "render_hitec: state %d not reached, at %d\n",
                (int)s, (int)pstate());
        exit(1);
    }
    ui_router_tick(0.02f);
}

/**
 * @brief The SERIES row's list, and its entry @p i taken; -1 leaves it
 *        open.
 * @param i the entry
 */
static void hseries(int i)
{
    htap(hitec_panel_value_rect(HITEC_PANEL_SERVO, 1, 0));
    if (i >= 0) {
        htap(hitec_panel_choice_rect(i));
    }
}

/**
 * @brief CONNECT, and the bench run until the port is held and idle.
 */
static void hconnect(void)
{
    htap(hitec_panel_value_rect(HITEC_PANEL_SERVO, 0, 0));
    for (unsigned i = 0; i < 60000u
                         && (pstate() == HITEC_PROG_OPENING
                             || pstate() == HITEC_PROG_ENTERING
                             || pstate() == HITEC_PROG_READING);
         ++i) {
        pms();
    }
    hwait(HITEC_PROG_HELD);
    /* The set points the IOmcu's SUPPLY page took from the session, as
     * supply_link_adopted() hands them to the screen. */
    if (g_hw.v.online) {
        supply_screen_adopt((float)g_hw.v.set_mv / 1000.0f,
                            (float)g_hw.v.set_ma / 1000.0f);
    }
}

/**
 * @brief Whether @p v is one of the views of the classic HS-5645MG.
 * @param v the view's name past "servo-hitec"
 * @return true for one
 */
static bool classic_view(const char *v)
{
    return strncmp(v, "-classic", 8) == 0 || strcmp(v, "-nopd") == 0;
}

void render_hitec_view(const char *view)
{
    const char *v = view + strlen("servo-hitec");
    const bool c_view = classic_view(v);

    rig_at(strcmp(v, "-nopd") == 0 ? SUPPLY_NONE : SUPPLY_ON, true);
    if (c_view) {
        servo_hs5645();
    } else {
        servo_d485();
    }
    ui_router_goto(SCREEN_SERVO);
    /* The tester on PROG PORT 1 at GP13, as the control task reports it. */
    servo_screen_set_drive(1u, PIN, true);
    htap_at(SV_TAB_PROGRAM_X, UI_BAND_H + SV_TAB_Y);
    htap_at(SV_PROG_ROW_X, UI_BAND_H + SV_PROG_ROW_Y(1));
    prun(50u);
    ui_router_tick(0.02f);
    if (!hitec_panel_is_open()) {
        fprintf(stderr, "render_hitec: the overlay did not open\n");
        exit(1);
    }
    if (v[0] == '\0') {
        return;
    }
    if (strcmp(v, "-series") == 0) {
        hseries(-1);
        return;
    }
    if (strcmp(v, "-more") == 0) {
        htap(hitec_panel_value_rect(HITEC_PANEL_SERVO, 2, 0));
        return;
    }
    if (strcmp(v, "-other") == 0 || strcmp(v, "-release") == 0
        || strcmp(v, "-released") == 0) {
        /* The bench console's session on port 1, as the panel sees it. */
        (void)open_as(CH, HITEC_D, HITEC_DRIVE_OPEN_DRAIN);
        hwait(HITEC_PROG_OTHER);
        if (strcmp(v, "-other") == 0) {
            return;
        }
        htap(hitec_panel_value_rect(HITEC_PANEL_SERVO, 0, 0));
        if (strcmp(v, "-release") == 0) {
            return;
        }
        hhold();
        hwait(HITEC_PROG_PWM);
        return;
    }
    if (c_view) {
        hseries(1);
    }
    if (strcmp(v, "-nopd") == 0) {
        htap(hitec_panel_value_rect(HITEC_PANEL_SERVO, 0, 0));
        prun(50u);
        return;
    }
    if (strcmp(v, "-reading") == 0) {
        htap(hitec_panel_value_rect(HITEC_PANEL_SERVO, 0, 0));
        hwait(HITEC_PROG_READING);
        prun(160u);
        ui_router_tick(0.02f);
        return;
    }
    hconnect();
    if (strcmp(v, "-read") == 0 || strcmp(v, "-classic") == 0) {
        return;
    }
    if (strcmp(v, "-stopped") == 0) {
        press_stop();
        prun(200u);
        hwait(HITEC_PROG_HELD);
        return;
    }
    if (strcmp(v, "-leave") == 0 || strcmp(v, "-classic-leave") == 0) {
        htap(hitec_panel_close_rect());
        return;
    }
    if (strcmp(v, "-motion") == 0) {
        htap(hitec_panel_tab_rect(HITEC_PANEL_MOTION));
    } else if (strcmp(v, "-travel") == 0
               || strcmp(v, "-classic-travel") == 0) {
        htap(hitec_panel_tab_rect(HITEC_PANEL_TRAVEL));
    } else if (strcmp(v, "-protect") == 0
               || strcmp(v, "-classic-protect") == 0) {
        htap(hitec_panel_tab_rect(HITEC_PANEL_PROTECT));
    } else if (strcmp(v, "-info") == 0 || strcmp(v, "-classic-info") == 0) {
        htap(hitec_panel_tab_rect(HITEC_PANEL_INFO));
    } else {
        fprintf(stderr, "render_hitec: no view %s\n", view);
        exit(1);
    }
}

void render_hitec_feed(bool held)
{
    rig_at(SUPPLY_ON, true);
    if (held) {
        (void)open_as(CH, HITEC_D, HITEC_DRIVE_OPEN_DRAIN);
        hwait(HITEC_PROG_OTHER);
    }
}
