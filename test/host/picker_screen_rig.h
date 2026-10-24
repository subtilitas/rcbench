/**
 * @file picker_screen_rig.h
 * @brief The pin picker's cases as test_picker_screen.c and
 * test_picker_screen_*.c share them.
 *
 * Only static inline helpers and the buffers and counters they use.
 *
 * Used by:
 * - test_picker_screen.c
 * - test_picker_screen_hold.c
 * - test_picker_screen_table.c
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <string.h>

#include "greatest.h"
#include "guard_canvas.h"

#include "drawing_fixture.h"
#include "link_pages.h"
#include "outputs_screen.h"
#include "picker_screen.h"
#include "settings.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "touch_feed.h"

#define BOARD ((uint16_t)OUTBIND_BOARD_PICO_HEADER)
#define SCREEN_W UI_PANEL_W
#define SCREEN_H UI_BODY_H
/** @brief The lowest row the screen draws or takes a press on: 4px above
 *  the alert band, body y 492. */
#define PK_FOOT (UI_ALERT_Y - UI_BAND_H - 4)

static int         s_applied __attribute__((unused));
static outbind_t   s_last __attribute__((unused));
static gfx_color_t s_px[SCREEN_W * SCREEN_H] __attribute__((unused));

/* firmware/iomcu/src/board_drawing_rp2350_can.c: the drawing the
 * IOmcu serves for this board. */
extern const uint8_t  board_drawing_rp2350_can[] __attribute__((unused));
extern const uint32_t board_drawing_rp2350_can_bytes __attribute__((unused));

/** @brief Show the served drawing; whether the picker took it. */
static inline bool serve_drawing(void)
{
    return picker_screen_set_drawing(board_drawing_rp2350_can,
                                     board_drawing_rp2350_can_bytes, NULL);
}

/** @brief Apply seam: counts the call and keeps the binding it was given. */
static inline void on_apply(const outbind_t *b) { ++s_applied; s_last = *b; }

/** @brief The pin picker under test. */
static inline const ui_screen_t *scr(void) { return picker_screen(); }

/** @brief Index of @p gpio on the board the cases use. */
static inline uint8_t idx(uint8_t gpio) { return outbind_index_of(BOARD, gpio); }

/** @brief Resets the picker and the apply seam, binds the board with SERVO PWM picked and opens the visit. */
static inline void fresh(void)
{
    ui_theme_set(UI_THEME_DARK);
    scr()->reset();
    s_applied = 0;
    picker_screen_set_apply(on_apply);
    outbind_t b;
    outbind_init(&b);
    outbind_set_board(&b, BOARD);
    outbind_set_proto(&b, 1u);              /* SERVO PWM */
    picker_screen_set_binding(&b);
    picker_screen_set_drawing(NULL, 0u, NULL);  /* the board from its shape */
    scr()->enter();
    /* The visit opens on OFF; SERVO PWM is picked, as the follow of the
     * Outputs screen brings a pick made there. */
    picker_screen_follow(1u, true);
}

/** @brief Renders the picker onto the case's pixel buffer. */
static inline void render(void)
{
    gfx_canvas_t c = { s_px, SCREEN_W, SCREEN_H, SCREEN_W,
                       { 0, 0, SCREEN_W, SCREEN_H } };
    scr()->render(&c, 0);
}

/**
 * @brief Where the screen puts a pad, worked out the way the screen does: from the
 * board's own shape. A test that repeated the arithmetic would pass with the
 * screen's copy of it wrong in the same way.
 */
static inline bool pad_point_on(uint16_t board, uint8_t gpio, int *x, int *y)
{
    const outbind_board_t *bd = outbind_board(board);
    const uint8_t i = outbind_index_of(board, gpio);
    if (bd == NULL || bd->shape == NULL || i >= bd->count) { return false; }
    uint16_t xc, yc;
    if (!outbind_pad_xy(bd->shape, bd->pins[i].pad, &xc, &yc)) { return false; }
    int bw = 0, bh = 0;
    picker_board_fit(bd->shape->width_cmm, bd->shape->height_cmm, &bw, &bh);
    const int bx = (SCREEN_W - bw) / 2;
    const int by = (PK_FOOT - bh) / 2;
    /* Rounded to the nearest pixel, each from its own position. */
    *x = bx + (int)(((uint32_t)xc * (uint32_t)bw + bd->shape->width_cmm / 2u)
                    / bd->shape->width_cmm);
    *y = by + (int)(((uint32_t)yc * (uint32_t)bh + bd->shape->height_cmm / 2u)
                    / bd->shape->height_cmm);
    return true;
}

/* ------------------------------------------------ the settled design */

/** @brief Presses and releases at (@p x, @p y). */
static inline void tap(int x, int y)
{
    touch_event_t d = { TOUCH_EVENT_DOWN, { 0, (int16_t)x, (int16_t)y, 40 }, 0u };
    touch_event_t u = { TOUCH_EVENT_UP,   { 0, (int16_t)x, (int16_t)y, 40 }, 0u };
    scr()->event(&d);
    scr()->event(&u);
}

/** @brief Taps the centre of @p r. */
static inline void tap_rect(gfx_rect_t r) { tap(r.x + r.w / 2, r.y + r.h / 2); }

/** @brief By name, so an entry added to the catalogue does not retarget a test. */
static inline uint8_t proto_named(const char *name)
{
    const outbind_proto_t *k = outbind_protos();
    for (uint8_t i = 0; i < OUTBIND_PROTOS; ++i) {
        if (strcmp(k[i].name, name) == 0) { return i; }
    }
    T_FAIL("the catalogue offers no %s", name);
    return 0u;
}

/** @brief A pick in the screen's own list: the face, then the row. */
static inline void pick(const char *name)
{
    tap_rect(picker_screen_list_face());
    CHECK(picker_screen_list_open());
    tap_rect(picker_screen_list_row(proto_named(name)));
    CHECK(!picker_screen_list_open());
}

/** @brief Pose the screen with @p gpios bound to @p proto, on top of what it holds. */
static inline void bind_more(const char *proto, const uint8_t *gpios, unsigned n,
                      const char *selected)
{
    outbind_t b = *picker_screen_binding();
    outbind_set_proto(&b, proto_named(proto));
    for (unsigned i = 0; i < n; ++i) {
        if (!outbind_toggle(&b, idx(gpios[i]))) {
            T_FAIL("%s refused GP%u", proto, gpios[i]);
        }
    }
    outbind_set_proto(&b, proto_named(selected));
    picker_screen_set_binding(&b);
}

/** @brief The reason line the picker gives, in a static buffer. */
static inline const char *reason(void)
{
    static char buf[64];
    (void)picker_screen_reason(buf, sizeof(buf));
    return buf;
}

/** @brief The count line the picker gives, in a static buffer. */
static inline const char *count(void)
{
    static char buf[64];
    picker_screen_count(buf, sizeof(buf));
    return buf;
}

/** @brief True when the rectangles @p a and @p b share a pixel. */
static inline bool overlap(gfx_rect_t a, gfx_rect_t b)
{
    return a.x < b.x + b.w && b.x < a.x + a.w
           && a.y < b.y + b.h && b.y < a.y + a.h;
}
