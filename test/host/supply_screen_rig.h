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
#include "supply_spots.h"

#include "settings.h"
#include "supply.h"
#include "supply_screen.h"
#include "ui_keypad.h"
#include "ui_setlist.h"
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
 * Geometry from supply_spots.h, screen-local: the router strips the band
 * before a screen sees an event.  The short names the suites use.
 */
#define OUT_X     SU_OUT_X     /* OUTPUT ON / OFF, the footer's middle */
#define OUT_Y     SU_OUT_Y
#define RESET_X   SU_RESET_X   /* RESET PEAKS */
#define RESET_Y   SU_RESET_Y
#define V_DOWN_X  SU_DOWN_X    /* the set points' fine steps */
#define V_UP_X    SU_UP_X
#define V_ROW_Y   SU_V_ROW_Y
#define I_ROW_Y   SU_I_ROW_Y
#define TRACK_X   SU_TRACK_X   /* both tracks, TRACK_W px from it */
#define TRACK_W   SU_TRACK_W
#define OFF_X     SU_PLOT_X    /* the plot, clear of every control */
#define OFF_Y     SU_PLOT_Y

/* The task strip, SETTINGS' value rows, the rail's cards, the set points'
 * values, and the question's two buttons. */
#define SETTINGS_X SU_SETTINGS_X /* the footer's SETTINGS */
#define SETTINGS_Y SU_SETTINGS_Y
#define CARD_X    SU_CARD_X
#define CARD_V_Y  SU_CARD_Y(0)
#define CARD_I_Y  SU_CARD_Y(1)
#define TEXT_X    SU_VAL_X     /* the set points' values */
#define TEXT_V_Y  SU_V_ROW_Y
#define TEXT_I_Y  SU_I_ROW_Y
#define APPLY_X   SU_APPLY_X
#define DISCARD_X SU_DISCARD_X
#define ASK_Y     SU_ASK_Y     /* inside both, and on no LIVE control */

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
    ui_keypad_open(&probe, SU_OV_AREA, "", "", 0.0f, 0.0f, 1.0f, 2);
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

/** @brief Taps the footer's SETTINGS: the list, with the output off. */
static inline void open_settings(void) { tap(SETTINGS_X, SETTINGS_Y); }

/** @brief Drags the SETTINGS list by @p dy px: up (negative) scrolls
 *         towards its end. */
static inline void drag_list(int dy)
{
    ev(SU_LIST_DRAG_X, SU_LIST_DRAG_Y, TOUCH_EVENT_DOWN, 1);
    for (int k = 1; k <= 6; ++k) {
        ev(SU_LIST_DRAG_X, SU_LIST_DRAG_Y + dy * k / 6, TOUCH_EVENT_MOVE, 1);
    }
    ev(SU_LIST_DRAG_X, SU_LIST_DRAG_Y + dy, TOUCH_EVENT_UP, 1);
}

/** @brief The list's row @p at whole in its pane, dragged there as a finger
 *         drags it. */
static inline void show_row(int at)
{
    const ui_setlist_t *sl = supply_settings_list();
    const gfx_rect_t pane = ui_setlist_pane(sl);
    for (int k = 0; k < 12; ++k) {
        const gfx_rect_t r = ui_setlist_row_rect(sl, at);
        if (r.y < pane.y) {
            drag_list(pane.y - r.y + 8 < 200 ? pane.y - r.y + 8 : 200);
        } else if (r.y + r.h > pane.y + pane.h) {
            const int d = r.y + r.h - (pane.y + pane.h) + 8;
            drag_list(-(d < 200 ? d : 200));
        } else {
            return;
        }
    }
}

/** @brief Where setting @p id's value is in the list, the row shown first. */
static inline gfx_rect_t value_box(setting_id_t id)
{
    const int at = supply_settings_row(id);
    show_row(at);
    return ui_setlist_row_part(supply_settings_list(), at, UI_SL_PART_VALUE);
}

/** @brief Taps setting @p id's value: its keypad, or the next choice. */
static inline void tap_value(setting_id_t id)
{
    const gfx_rect_t b = value_box(id);
    tap(b.x + b.w / 2, b.y + b.h / 2);
}

/** @brief Taps setting @p id's "+": a switch flips, a number steps up. */
static inline void tap_plus(setting_id_t id)
{
    const int at = supply_settings_row(id);
    show_row(at);
    const gfx_rect_t b =
        ui_setlist_row_part(supply_settings_list(), at, UI_SL_PART_PLUS);
    tap(b.x + b.w / 2, b.y + b.h / 2);
}

/** @brief A key of the list's keypad, over the body. */
static inline void list_key(ui_key_t k)
{
    ui_keypad_t probe;
    memset(&probe, 0, sizeof(probe));
    ui_keypad_open(&probe, SU_LIST_KP_AREA, "", "", 0.0f, 0.0f, 1.0f, 0);
    const gfx_rect_t r = ui_keypad_key_rect(&probe, k);
    tap(r.x + r.w / 2, r.y + r.h / 2);
}

/** @brief Types @p text on the list's keypad, then OK. */
static inline void list_keys(const char *text)
{
    static const ui_key_t digit[10] = {
        UI_KEY_0, UI_KEY_1, UI_KEY_2, UI_KEY_3, UI_KEY_4,
        UI_KEY_5, UI_KEY_6, UI_KEY_7, UI_KEY_8, UI_KEY_9,
    };
    for (const char *p = text; *p != '\0'; ++p) {
        list_key((*p == '.') ? UI_KEY_DOT : digit[*p - '0']);
    }
    list_key(UI_KEY_OK);
}

/** @brief Taps the list's SAVE. */
static inline void list_save(void) { tap(SU_LIST_SAVE_X, SU_LIST_STRIP_Y); }

/** @brief Holds OUTPUT ON for the hold time plus 5 ticks, then releases. */
static inline void hold_on(void)
{
    ev(OUT_X, OUT_Y, TOUCH_EVENT_DOWN, 1);
    tick_for(HOLD_TICKS);
    ev(OUT_X, OUT_Y, TOUCH_EVENT_UP, 1);
}

/** @brief Pixels that differ in the header, right of the task strip, above
 * the page, between the two buffers. */
static inline int header_differs(void)
{
    int differ = 0;
    for (int y = 0; y < SU_INFO_B; ++y) {
        for (int x = SU_INFO_X; x < W; ++x) {
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
