/*
 * A fake brand on the servo programmers' frame (sprog.h), for the frame's
 * suites: 2 tabs and 9 rows on SERVO's left card, 620x428 at x 8, y 68.
 *
 *   SERVO (tab 0)                       TUNE (tab 1, shown with M_TUNE)
 *   0 HEAD, wide                        7 DEAD BAND, wide, a step
 *   1 SERVO: READ ALL | 2 TYPE (M_SLOT) 8 SPEED, a value
 *   3 FILES           | 4 MORE
 *   5 RESTORE         | 6 WRITE n VALUES, 220px
 *
 * rig_view() fills the frame's view from the rig's per-row settings, as a
 * brand's view fill does before each draw, event and tick.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "sprog.h"
#include "ui_text.h"
#include "ui_theme.h"

enum { R_HEAD = 0, R_READ, R_TYPE, R_FILES, R_MORE, R_RESTORE, R_WRITE,
       R_DEAD, R_SPEED, R_COUNT };

#define M_TUNE 0x01u          /**< the TUNE tab shows   */
#define M_SLOT 0x04u          /**< the TYPE row shows   */
#define M_ALL  (M_TUNE | M_SLOT)

static const ui_text_id_t rig_tabs[] = { TX_KP_TAB_SERVO, TX_KP_TAB_TUNE };
static const uint8_t rig_tab_show[] = { 0u, M_TUNE };

static const sprog_row_t rig_rows[R_COUNT] = {
    { 0, 1, SPROG_K_HEAD, 0, 0, 0, -1, "PORT" },
    { 0, 0, SPROG_K_ACT, 1, 0, 0, -1, "SERVO" },
    { 0, 0, SPROG_K_ACT, 2, M_SLOT, 0, -1, "TYPE" },
    { 0, 0, SPROG_K_ACT, 3, 0, 0, -1, "FILES" },
    { 0, 0, SPROG_K_ACT, 4, 0, 0, -1, "MORE" },
    { 0, 0, SPROG_K_ACT, 5, 0, 0, -1, "RESTORE" },
    { 0, 0, SPROG_K_ACT, 6, 0, 220, -1, "" },
    { 1, 1, SPROG_K_STEP, 7, 0, 0, -1, "DEAD BAND" },
    { 1, 0, SPROG_K_VALUE, 8, 0, 0, -1, "SPEED" },
};

static const sprog_def_t rig_def = {
    rig_tabs, rig_tab_show, 2, rig_rows, R_COUNT, TX_SPROG_EDIT_NOTE,
};

/* The canvas: SERVO's left card and room around it. */
#define RIG_W 640
#define RIG_H 520

/** @brief What the brand shows of a row, set by a case before rig_view(). */
typedef struct {
    bool        set;          /* use the fields below            */
    uint8_t     kind, press, press2, fill, fill2, fill_to, look;
    const char *value, *value2, *why, *label;
    int16_t     hint;
} rig_cell_t;

static struct {
    sprog_t     f;
    uint8_t     mask;
    rig_cell_t  cell[R_COUNT];
    const char *head[2];
    bool        head_warn, port_pick;
    const char *note[SPROG_NOTES];
    uint8_t     notes;
    const char *bottom;
    uint32_t    sig;
    gfx_color_t *fb;
    gfx_canvas_t cv;
} rig;

/** @brief The view, from the rig, as a brand fills it. */
static inline void rig_view(void)
{
    sprog_view_begin(&rig.f, rig.mask);
    for (int r = 0; r < R_COUNT; ++r) {
        sprog_cell_t *c = sprog_cell(&rig.f, r);
        const rig_cell_t *s = &rig.cell[r];

        if (c == NULL) {
            continue;
        }
        snprintf(c->value, sizeof(c->value), "%s",
                 s->value != NULL ? s->value : "CHOOSE");
        if (!s->set) {
            continue;
        }
        c->kind = s->kind;
        c->press = s->press;
        c->press2 = s->press2;
        c->fill = s->fill;
        c->fill2 = s->fill2;
        c->fill_to = s->fill_to;
        c->look = s->look;
        c->value2 = s->value2;
        c->why = s->why;
        c->label = s->label;
        c->hint = s->hint;
    }
    rig.f.view.head[0] = rig.head[0];
    rig.f.view.head[1] = rig.head[1];
    rig.f.view.head_warn = rig.head_warn;
    rig.f.view.port_pick = rig.port_pick;
    for (int i = 0; i < rig.notes; ++i) {
        sprog_note(&rig.f, rig.note[i], SPROG_LV_TEXT);
    }
    rig.f.view.bottom = rig.bottom;
    rig.f.view.sig = rig.sig;
}

/** @brief Sets row @p r's cell: its kind and how it takes a press. */
static inline rig_cell_t *rig_cell(int r, uint8_t kind, uint8_t press)
{
    rig_cell_t *c = &rig.cell[r];

    c->set = true;
    c->kind = kind;
    c->press = press;
    c->press2 = SPROG_P_TAP;
    c->hint = -1;
    return c;
}

/** @brief A fresh frame on the rig, open, the mask M_ALL, the view filled. */
static inline void rig_start(void)
{
    if (rig.fb == NULL) {
        rig.fb = calloc((size_t)RIG_W * RIG_H, sizeof(gfx_color_t));
        REQUIRE_ALLOC(rig.fb);
    }
    memset(rig.fb, 0, (size_t)RIG_W * RIG_H * sizeof(gfx_color_t));
    gfx_canvas_init(&rig.cv, rig.fb, RIG_W, RIG_H, RIG_W);
    ui_theme_set(UI_THEME_DARK);
    ui_text_set_language(UI_LANG_EN);
    memset(&rig.cell, 0, sizeof(rig.cell));
    rig.head[0] = "PROG PORT 1 GPIO 13";
    rig.head[1] = "PD MINI  5.0V 1000mA";
    rig.head_warn = false;
    rig.port_pick = false;
    rig.notes = 0;
    rig.bottom = NULL;
    rig.sig = 0;
    rig.mask = M_ALL;
    rig_cell(R_HEAD, SPROG_K_HEAD, SPROG_P_HOLD)->hint = TX_HOLD_HINT_CONNECT;
    rig.cell[R_HEAD].value = "CONNECT";
    rig.cell[R_HEAD].fill = SPROG_F_GO;
    rig.cell[R_HEAD].fill_to = SPROG_F_ACTIVE;
    rig_cell(R_WRITE, SPROG_K_ACT, SPROG_P_HOLD)->hint = TX_HOLD_HINT_WRITE;
    rig.cell[R_WRITE].value = "WRITE 2 VALUES";
    rig.cell[R_WRITE].fill = SPROG_F_GO;
    rig.cell[R_WRITE].fill_to = SPROG_F_WAITING;
    sprog_init(&rig.f, &rig_def, gfx_rect_make(8, 68, 620, 428));
    rig.f.open = true;
    rig_view();
}

/** @brief One touch event, the view filled first. */
static inline sprog_result_t rig_ev(touch_event_type_t type, int x, int y,
                             uint8_t id)
{
    touch_event_t e;

    memset(&e, 0, sizeof(e));
    e.type = type;
    e.point.id = id;
    e.point.x = (int16_t)x;
    e.point.y = (int16_t)y;
    rig_view();
    return sprog_event(&rig.f, &e);
}

/** @brief The middle of @p r. */
static inline int rig_cx(gfx_rect_t r)
{
    return r.x + r.w / 2;
}

/** @brief The middle of @p r. */
static inline int rig_cy(gfx_rect_t r)
{
    return r.y + r.h / 2;
}

/** @brief A DOWN in the middle of @p r by contact @p id. */
static inline sprog_result_t rig_down(gfx_rect_t r, uint8_t id)
{
    return rig_ev(TOUCH_EVENT_DOWN, rig_cx(r), rig_cy(r), id);
}

/** @brief An UP in the middle of @p r by contact @p id. */
static inline sprog_result_t rig_up(gfx_rect_t r, uint8_t id)
{
    return rig_ev(TOUCH_EVENT_UP, rig_cx(r), rig_cy(r), id);
}

/** @brief A tap in the middle of @p r: the UP's result. */
static inline sprog_result_t rig_tap(gfx_rect_t r)
{
    (void)rig_down(r, 1);
    return rig_up(r, 1);
}

/** @brief One tick of @p dt seconds, the view filled first. */
static inline sprog_result_t rig_tick(float dt)
{
    rig_view();
    return sprog_tick(&rig.f, dt);
}

/** @brief Ticks for @p seconds in frames of 50ms; the first result that is
 *         not NONE, or NONE. */
static inline sprog_result_t rig_run(float seconds)
{
    sprog_result_t got = { 0 };

    for (float t = 0.0f; t < seconds - 0.001f; t += 0.05f) {
        const sprog_result_t r = rig_tick(0.05f);

        if (got.kind == SPROG_R_NONE) {
            got = r;
        }
    }
    return got;
}

/** @brief Draws the frame on the rig's canvas, the view filled first. */
static inline void rig_render(void)
{
    rig_view();
    sprog_render(&rig.f, &rig.cv);
}

/** @brief The pixel at (@p x, @p y) of the last render. */
static inline gfx_color_t rig_px(int x, int y)
{
    return rig.fb[(size_t)y * RIG_W + (size_t)x];
}
