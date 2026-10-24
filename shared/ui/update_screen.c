/**
 * @file update_screen.c
 * @brief PANEL UPDATE, the screen that writes an uploaded or downloaded image:
 *        wifi_screen.h says what it is for and declares its interface.
 *
 * SPDX-License-Identifier: MIT
 */

#include "wifi_screen.h"

#include <stdio.h>
#include <string.h>

#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

static bool in(gfx_rect_t r, int x, int y) { return gfx_rect_contains(r, x, y); }

static struct {
    update_view_t v;
    bool          close;
    bool          cancel;
    bool          have_press;
    bool          pressed;
    uint8_t       press_id;
    unsigned      drawn_mask;
} u;

/*
 * PANEL UPDATE on the whole panel:
 * A 64px title bar; the bytes in U_FONT_HEAD (20 x 36) over a bar 36px
 * tall, 976px wide; from U_LINES_Y the step or the verdict in U_FONT_HEAD
 * and the lines under it in U_FONT_BODY (12 x 24), 28px apart; the one
 * button 240 x 56px at the bottom left, 18px above the panel's edge.
 */
/** @brief The title, the bytes and the step: 20 x 36. */
#define U_FONT_HEAD  (&gfx_font_20x36)
/** @brief The lines under them: 12 x 24. */
#define U_FONT_BODY  UI_FONT_BODY
/** @brief The button: 10 x 20. */
#define U_FONT_LABEL (&gfx_font_10x20)
/** @brief The title bar's height, px. */
#define U_BAR_H      64
/** @brief The column's left edge. */
#define U_X          24
#define U_W          (UI_PANEL_W - 2 * U_X)  /**< 976px: 48 cells of
                                                  U_FONT_HEAD, 81 of BODY */
/** @brief The bytes' top. */
#define U_BYTES_Y    88
/** @brief The bar, 36px tall. */
#define U_PBAR       gfx_rect_make(U_X, 140, U_W, 36)
/** @brief The step's or the verdict's top. */
#define U_LINES_Y    208
/** @brief A U_FONT_BODY line's pitch, px. */
#define U_PITCH      28
/** @brief CLOSE or CANCEL, at the bottom left. */
#define U_CLOSE gfx_rect_make(24, UI_PANEL_H - 18 - 56, 240, 56)

_Static_assert(56 >= UI_TAP_MIN, "CLOSE and CANCEL are a finger tall");

static void u_reset(void)
{
    memset(&u, 0, sizeof(u));
}

void update_screen_set(const update_view_t *v)
{
    /* As wifi_screen_set(): padding costs a repaint at most. */
    /* NOLINTNEXTLINE(bugprone-suspicious-memory-comparison) */
    if (v != NULL && memcmp(v, &u.v, sizeof(u.v)) != 0) {
        u.v = *v;
        u.drawn_mask = 0u;
    }
}

void update_screen_invalidate(void) { u.drawn_mask = 0u; }

bool update_screen_take_close(void)
{
    const bool was = u.close;
    u.close = false;
    return was;
}

bool update_screen_take_cancel(void)
{
    const bool was = u.cancel;
    u.cancel = false;
    return was;
}

/* The one button: CLOSE after a failure, CANCEL while downloading. */
static bool u_has_button(void)
{
    return u.v.state == UPDATE_FAILED || u.v.state == UPDATE_FETCH_FAILED
           || u.v.state == UPDATE_FETCHING;
}

static void u_event(const touch_event_t *evt)
{
    if (evt == NULL || !u_has_button()) {
        return;
    }
    const bool on = in(U_CLOSE, evt->point.x, evt->point.y);
    if (evt->type == TOUCH_EVENT_DOWN) {
        if (!u.have_press && on) {
            u.have_press = true;
            u.pressed = true;
            u.press_id = evt->point.id;
            u.drawn_mask = 0u;
        }
        return;
    }
    if (!u.have_press || evt->point.id != u.press_id
        || evt->type != TOUCH_EVENT_UP) {
        return;
    }
    u.have_press = false;
    u.pressed = false;
    u.drawn_mask = 0u;
    if (touch_event_is_tap_up(evt) && on) {
        if (u.v.state == UPDATE_FETCHING) {
            u.cancel = true;
        } else {
            u.close = true;
        }
    }
}

static void u_cancel(void)
{
    u.have_press = false;
    u.pressed = false;
    u.drawn_mask = 0u;
}

/**
 * @brief One line of @p t in @p f from (U_X, @p y), as wide as the column.
 * @param c   Canvas, the whole panel.
 * @param y   The line's top.
 * @param t   The text.
 * @param f   Its face.
 * @param col Its colour.
 */
static void u_line(gfx_canvas_t *c, int y, const char *t, const gfx_font_t *f,
                   gfx_color_t col)
{
    gfx_text_in(c, gfx_rect_make(U_X, y, U_W, gfx_text_height(f, 1)), t, f,
                col, 1, GFX_ALIGN_LEFT);
}

/**
 * @brief PANEL UPDATE, drawn whole once per framebuffer and again after
 *        every change.
 * @param c            Canvas, the whole panel.
 * @param buffer_index The framebuffer drawn into.
 */
static void u_render(gfx_canvas_t *c, int buffer_index)
{
    const unsigned bit = 1u << (buffer_index & 1);
    if ((u.drawn_mask & bit) != 0u) {
        return;
    }
    /* The whole panel and no band: nothing on the bench runs while the
     * flash is written, so there is nothing for a STOP to stop.  The
     * download before it runs with the bench SAFE, the supply off and no
     * log run; anything else ends it. */
    gfx_clear(c, UI_BG);
    const bool fetch = (u.v.state == UPDATE_FETCHING
                        || u.v.state == UPDATE_FETCH_FAILED);
    const bool failed = (u.v.state == UPDATE_FAILED
                         || u.v.state == UPDATE_FETCH_FAILED);
    const gfx_color_t head = failed ? UI_DANGER : UI_ACCENT;
    gfx_fill_rect(c, 0, 0, UI_PANEL_W, U_BAR_H, head);
    char line[96];
    snprintf(line, sizeof(line), fetch ? TR(UPD_DL_TITLE) : TR(UP_TITLE),
             u.v.version);
    u_line(c, (U_BAR_H - gfx_text_height(U_FONT_HEAD, 1)) / 2, line,
           U_FONT_HEAD, ui_is_light(head) ? UI_TEXT_ON_LIGHT : GFX_WHITE);
    const unsigned pct = (u.v.total > 0u)
        ? (unsigned)((uint64_t)u.v.written * 100u / u.v.total) : 0u;
    if (!fetch || u.v.total > 0u) {
        /* A download names its bytes once the image's size is known. */
        snprintf(line, sizeof(line), TR(UP_BYTES),
                 (unsigned long)u.v.written, (unsigned long)u.v.total, pct);
        u_line(c, U_BYTES_Y, line, U_FONT_HEAD, UI_TEXT);
    }
    ui_bar(c, U_PBAR,
           (u.v.total > 0u) ? (float)u.v.written / (float)u.v.total : 0.0f,
           -1.0f, failed ? UI_DANGER : UI_ACCENT);
    /* The step or the verdict in the heading face, the lines under it
     * 48px lower and U_PITCH apart. */
    const int y = U_LINES_Y, y2 = U_LINES_Y + 48;
    if (u.v.state == UPDATE_FETCHING) {
        u_line(c, y, u.v.what, U_FONT_HEAD, UI_TEXT);
        u_line(c, y2, u.v.why, U_FONT_BODY, UI_TEXT_DIM);
        u_line(c, y2 + 2 * U_PITCH, TR(UPD_DL_NOTE_1), U_FONT_BODY,
               UI_TEXT);
        u_line(c, y2 + 3 * U_PITCH, TR(UPD_DL_NOTE_2), U_FONT_BODY,
               UI_TEXT_DIM);
        ui_button_font(c, U_CLOSE, TR(CANCEL), UI_PANEL_HI, u.pressed, true,
                       U_FONT_LABEL);
    } else if (u.v.state == UPDATE_FETCH_FAILED) {
        u_line(c, y, TR(UPD_DL_FAILED), U_FONT_HEAD, UI_DANGER);
        int ly = y2;
        u_line(c, ly, u.v.what, U_FONT_BODY, UI_WARN);
        if (u.v.why[0] != '\0') {
            ly += U_PITCH;
            u_line(c, ly, u.v.why, U_FONT_BODY, UI_TEXT_DIM);
        }
        u_line(c, ly + U_PITCH + 12, TR(UP_FAILED_2), U_FONT_BODY,
               UI_TEXT_DIM);
        ui_button_font(c, U_CLOSE, TR(UP_CLOSE), UI_PANEL_HI, u.pressed,
                       true, U_FONT_LABEL);
    } else if (u.v.state == UPDATE_RESTARTING) {
        /* Nothing under it: a line longer than the 48 cells breaks onto
         * the next, 48px lower. */
        (void)ui_text_wrap(c, gfx_rect_make(U_X, y, U_W,
                                            gfx_text_height(U_FONT_HEAD, 1)),
                           48, TR(UP_RESTART), U_FONT_HEAD, UI_OK);
    } else if (failed) {
        u_line(c, y, TR(UP_FAILED), U_FONT_HEAD, UI_DANGER);
        u_line(c, y2, TR(UP_FAILED_2), U_FONT_BODY, UI_TEXT_DIM);
        ui_button_font(c, U_CLOSE, TR(UP_CLOSE), UI_PANEL_HI, u.pressed,
                       true, U_FONT_LABEL);
    } else {
        u_line(c, y, TR(UP_NOTE_1), U_FONT_BODY, UI_TEXT);
        u_line(c, y + U_PITCH, TR(UP_NOTE_2), U_FONT_BODY, UI_TEXT_DIM);
        u_line(c, y + 2 * U_PITCH, TR(WF_Q_NOTE_2), U_FONT_BODY,
               UI_TEXT_DIM);
    }
    u.drawn_mask |= bit;
}

static const ui_screen_t k_update = {
    .title  = "PANEL UPDATE",
    .reset  = u_reset,
    .enter  = NULL,
    .leave  = NULL,
    .tick   = NULL,
    .event  = u_event,
    .cancel = u_cancel,
    .render = u_render,
};

const ui_screen_t *update_screen(void) { return &k_update; }
