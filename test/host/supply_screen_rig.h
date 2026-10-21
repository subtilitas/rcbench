/**
 * @file supply_screen_rig.h
 * @brief The supply screen's cases as test_supply_screen.c and
 * test_supply_screen_*.c share them.
 *
 * Only static inline helpers and the buffers and counters they use.
 *
 * Used by:
 * - test_supply_screen.c
 * - test_supply_screen_tasks.c
 * - test_supply_screen_home.c
 * - test_supply_screen_draw.c
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"
#include "touch_feed.h"

#include "settings.h"
#include "supply.h"
#include "supply_screen.h"
#include "ui_keypad.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "ui_workbench.h"

#define W UI_PANEL_W
#define H UI_PANEL_H

static gfx_color_t *fb __attribute__((unused));
static gfx_color_t *fb2 __attribute__((unused));
static gfx_canvas_t cv __attribute__((unused));
static gfx_canvas_t cv2 __attribute__((unused));
static const ui_screen_t *scr __attribute__((unused));

/** @brief Takes every queued supply command. */
static inline void drain(void)
{
    supply_cmd_t junk;
    while (supply_screen_poll_cmd(&junk)) { }
}

/** @brief Clears both canvases and resets the settings and the supply screen. */
static inline void fresh(void)
{
    if (fb == NULL) {
        fb  = calloc((size_t)W * H, sizeof(gfx_color_t));
        REQUIRE_ALLOC(fb);
        fb2 = calloc((size_t)W * H, sizeof(gfx_color_t));
        REQUIRE_ALLOC(fb2);
    }
    memset(fb, 0, (size_t)W * H * sizeof(gfx_color_t));
    memset(fb2, 0, (size_t)W * H * sizeof(gfx_color_t));
    gfx_canvas_init(&cv, fb, W, H, W);
    gfx_canvas_init(&cv2, fb2, W, H, W);
    ui_theme_set(UI_THEME_DARK);
    settings_set_store(NULL);
    settings_init();
    scr = supply_screen();
    scr->reset();
    supply_screen_settings_loaded();
    drain();
}

/** @brief Hands the screen one touch event of contact @p id at (@p x, @p y). */
static inline void ev(int x, int y, touch_event_type_t t, uint8_t id)
{
    const touch_event_t e = { .type = t,
                              .point = { .id = id, .x = (int16_t)x,
                                         .y = (int16_t)y, .strength = 40 } };
    scr->event(&e);
}
/** @brief Presses and releases contact 1 at (@p x, @p y). */
static inline void tap(int x, int y) { ev(x, y, TOUCH_EVENT_DOWN, 1);
                                ev(x, y, TOUCH_EVENT_UP, 1); }

/*
 * Geometry mirrored from supply_screen.c, screen-local: the router strips the
 * band before a screen sees an event.
 */
#define OUT_X     676          /* OUTPUT ON / OFF, right rail */
#define OUT_Y     318
#define RESET_X   676          /* RESET PEAKS */
#define RESET_Y   402
#define V_DOWN_X  39           /* the set points' fine steps */
#define V_UP_X    519
#define V_ROW_Y   335
#define I_ROW_Y   397
#define TRACK_X   72           /* both tracks: x 72..485 */
#define TRACK_W   414
#define OFF_X     300          /* the plot, clear of every control */
#define OFF_Y     120

/* The task strip, from ui_workbench.h: LIVE x 9..121, SETTINGS x 125..237,
 * y 6..30.  Then SETTINGS' value rows, the rail's cards, the set points'
 * values, and the question's two buttons, from supply_screen.c. */
#define LIVE_TAB_X 65
#define SET_TAB_X 181
#define STRIP_Y   18
#define ROW_L_X   144          /* the left column's rows */
#define ROW_R_X   413          /* the right column's */
#define VMAX_Y    94
#define ISTART_Y  254
#define TRIPI_Y   94
#define CONF_SL_Y 316
#define CARD_X    676
#define CARD_V_Y  65
#define CARD_I_Y  123
#define TEXT_X    412
#define TEXT_V_Y  305
#define TEXT_I_Y  367
#define APPLY_X   151
#define DISCARD_X 407
#define ASK_Y     353          /* inside both, and on no LIVE control */

static inline int header_differs(void);
static inline supply_state_t reading(float v, float i);

#define TICK_S     0.05f
#define HOLD_TICKS ((int)(UI_HOLD_S / TICK_S) + 5)

/** @brief Ticks the screen @p steps times at TICK_S. */
static inline void tick_for(int steps)
{
    for (int i = 0; i < steps; ++i) {
        scr->tick(TICK_S);
    }
}

/** @brief A key of the keypad, where the screen opens it: over the left column. */
static inline void key(ui_key_t k)
{
    ui_keypad_t probe;
    memset(&probe, 0, sizeof(probe));
    ui_keypad_open(&probe, (gfx_rect_t){ 6, 38, 546, 388 }, "", "", 0.0f,
                   0.0f, 1.0f, 2);
    const gfx_rect_t r = ui_keypad_key_rect(&probe, k);
    tap(r.x + r.w / 2, r.y + r.h / 2);
}

/** @brief Types @p text on the keypad, digits and a dot, then OK. */
static inline void keys(const char *text)
{
    static const ui_key_t digit[10] = {
        UI_KEY_0, UI_KEY_1, UI_KEY_2, UI_KEY_3, UI_KEY_4,
        UI_KEY_5, UI_KEY_6, UI_KEY_7, UI_KEY_8, UI_KEY_9,
    };
    for (const char *p = text; *p != '\0'; ++p) {
        key((*p == '.') ? UI_KEY_DOT : digit[*p - '0']);
    }
    key(UI_KEY_OK);
}

/** @brief Holds OUTPUT ON for the hold time plus 5 ticks, then releases. */
static inline void hold_on(void)
{
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 1);
    tick_for(HOLD_TICKS);
    ev(OUT_X, OUT_Y, TOUCH_EVENT_UP, 1);
}

/** @brief Pixels that differ in the header, right of the task strip, y 0..33,
 * between the two buffers. */
static inline int header_differs(void)
{
    int differ = 0;
    for (int y = 0; y < 34; ++y) {
        for (int x = 240; x < W; ++x) {
            differ += (fb[y * W + x] != fb2[y * W + x]) ? 1 : 0;
        }
    }
    return differ;
}

/* ------------------------------------------------------------------ plot */

/** @brief A supply state of @p v volts and @p i amps. */
static inline supply_state_t reading(float v, float i)
{
    supply_state_t st;
    memset(&st, 0, sizeof(st));
    st.v = v; st.i = i; st.p = v * i;
    st.ok = SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT;
    st.online = true;
    st.mode = SUPPLY_MODE_CV;
    return st;
}
