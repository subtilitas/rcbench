/**
 * @file outputs_screen_rig.h
 * @brief The outputs screen's cases as test_outputs_screen.c and
 * test_outputs_screen_*.c share them.
 *
 * Only static inline helpers and the buffers and counters they use.
 *
 * Used by:
 * - test_outputs_screen.c
 * - test_outputs_screen_draw.c
 * - test_outputs_screen_off.c
 * - test_outputs_screen_port.c
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <string.h>

#include "greatest.h"
#include "guard_canvas.h"

#include "link_pages.h"
#include "outputs_pages.h"
#include "outputs_screen.h"
#include "port_view.h"
#include "servo_screen.h"
#include "settings.h"
#include "sensors_rows.h"
#include "settings_sensors.h"
#include "settings_screen.h"
#include "sense_link.h"
#include "tone_link.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "touch_feed.h"

/* The screen's body, as the router hands it over. */
#define SCREEN_W UI_PANEL_W
#define SCREEN_H UI_BODY_H

/* Geometry the screen draws to; a test that hard-codes it is a test that
 * notices when the layout moves under the hit testing. */
#define COL_X  16
#define DD_X   16
#define DD_Y   44
#define DD_W   266
#define DD_H   54
/* 46 px: the 8 protocols and 1 PROG PORT entry fit the screen's height at
 * the full row. */
#define POP_ROW 46
#define GRID_X 288
#define GRID_Y 16
#define CELL_GAP 4
#define CELL_W ((SCREEN_W - 16 - GRID_X - 3 * CELL_GAP) / 4)
#define CELL_H 53
#define GRID_ROWS 7

static int s_applied __attribute__((unused));
static outbind_t s_last __attribute__((unused));

/** @brief Apply seam: counts the call and keeps the binding it was given. */
static inline void on_apply(const outbind_t *b)
{
    ++s_applied;
    s_last = *b;
}

/** @brief The outputs screen under test. */
static inline const ui_screen_t *scr(void) { return outputs_screen(); }

/** @brief Resets the screen and the apply seam and tells it which board answered. */
static inline void fresh(void)
{
    ui_theme_set(UI_THEME_DARK);
    outputs_screen_set_apply(on_apply);
    scr()->reset();
    outputs_screen_set_apply(on_apply);   /* reset clears the seam */
    {   /* the screen shows nothing until it is told which board answered */
        outbind_t b;
        outbind_init(&b);
        outbind_set_board(&b, OUTBIND_BOARD_PICO_HEADER);
        outputs_screen_set_binding(&b);
    }
    s_applied = 0;
}

/** @brief Presses and releases at (@p x, @p y). */
static inline void tap(int x, int y)
{
    touch_event_t d = { TOUCH_EVENT_DOWN, { 0, (int16_t)x, (int16_t)y, 40 }, 0u };
    touch_event_t u = { TOUCH_EVENT_UP,   { 0, (int16_t)x, (int16_t)y, 40 }, 0u };
    scr()->event(&d);
    scr()->event(&u);
}

/** @brief Hands the screen a press at (@p x, @p y). */
static inline void press_at(int x, int y)
{
    touch_event_t d = { TOUCH_EVENT_DOWN, { 0, (int16_t)x, (int16_t)y, 40 }, 0u };
    scr()->event(&d);
}

/** @brief Hands the screen a release at (@p x, @p y). */
static inline void release_at(int x, int y)
{
    touch_event_t u = { TOUCH_EVENT_UP, { 0, (int16_t)x, (int16_t)y, 40 }, 0u };
    scr()->event(&u);
}

/** @brief Centre of the pin grid cell of @p gpio on the Pico header board. */
static inline void cell_centre(uint8_t gpio, int *x, int *y)
{
    const uint8_t i = outbind_index_of(OUTBIND_BOARD_PICO_HEADER, gpio);
    const int col = i / GRID_ROWS, row = i % GRID_ROWS;
    *x = GRID_X + col * (CELL_W + CELL_GAP) + CELL_W / 2;
    *y = GRID_Y + row * (CELL_H + CELL_GAP) + CELL_H / 2;
}

/** @brief Opens the protocol list and taps entry @p index. */
static inline void choose_proto(int index)
{
    tap(DD_X + DD_W / 2, DD_Y + DD_H / 2);              /* open  */
    tap(DD_X + 40, DD_Y + 4 + index * POP_ROW + POP_ROW / 2);
}

/** @brief By name, because the row a protocol sits on is the catalogue's business:
 * an entry added between two others must not silently retarget a test at a
 * protocol it was not written for. */
static inline int proto_row(const char *name)
{
    const outbind_proto_t *k = outbind_protos();
    for (int i = 0; i < (int)OUTBIND_PROTOS; ++i) {
        if (strcmp(k[i].name, name) == 0) {
            return i;
        }
    }
    T_FAIL("the catalogue offers no %s", name);
    return 0;
}

/** @brief Opens the protocol list and taps the entry called @p name. */
static inline void choose_named(const char *name) { choose_proto(proto_row(name)); }

/** @brief Taps the grid cell of @p gpio. */
static inline void tap_pin(uint8_t gpio)
{
    int x, y;
    cell_centre(gpio, &x, &y);
    tap(x, y);
}

/** @brief Index of @p gpio on the Pico header board. */
static inline uint8_t idx(uint8_t gpio)
{
    return outbind_index_of(OUTBIND_BOARD_PICO_HEADER, gpio);
}

/** @brief The bench of the report that OFF is for: GP0 DSHOT600 BIDIR, GP1 MOTOR
 * PWM, GP2 and GP13 SERVO PWM. */
static inline void three_protocols(outbind_t *b)
{
    outbind_init(b);
    outbind_set_board(b, OUTBIND_BOARD_PICO_HEADER);
    outbind_set_proto(b, (uint8_t)proto_row("DSHOT600 BIDIR"));
    (void)outbind_toggle(b, idx(0));
    outbind_set_proto(b, (uint8_t)proto_row("MOTOR PWM"));
    (void)outbind_toggle(b, idx(1));
    outbind_set_proto(b, (uint8_t)proto_row("SERVO PWM"));
    (void)outbind_toggle(b, idx(2));
    (void)outbind_toggle(b, idx(13));
}

/** @brief The same binding as the panel reads it: off the two pages it renders to,
 * so the protocol it names is the one a page names. */
static inline void read_back(const outbind_t *b, bind_reading_t *r)
{
    uint16_t slots[LINK_OS_COUNT], cc[LINK_CC_COUNT];
    (void)outbind_to_slots(b, slots);
    outbind_to_chan_cfg(b, cc, 1000u, 2000u);
    (void)bind_link_classify(r, b->board, slots, cc);
}

/** @brief The reason line the screen gives, in a static buffer. */
static inline const char *reason(void)
{
    static char buf[64];
    (void)outputs_screen_reason(buf, sizeof(buf));
    return buf;
}

/** @brief The strip under the protocol where the reason is drawn, as a sum: a line
 * there is ink, no line is the background. */
static inline unsigned reason_ink(gfx_canvas_t *c)
{
    unsigned long sum = 0;
    for (int y = DD_Y + DD_H + 56; y < DD_Y + DD_H + 76; ++y) {
        for (int x = COL_X; x < COL_X + 210; ++x) {
            sum += (unsigned long)c->pixels[y * c->stride + x];
        }
    }
    return (unsigned)(sum & 0xffffffffUL);
}
