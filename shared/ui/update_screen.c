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

#define U_CLOSE gfx_rect_make(24, UI_PANEL_H - 18 - 56, 240, 56)

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
    gfx_fill_rect(c, 0, 0, UI_PANEL_W, 56, head);
    char line[96];
    snprintf(line, sizeof(line), fetch ? TR(UPD_DL_TITLE) : TR(UP_TITLE),
             u.v.version);
    gfx_text_in(c, gfx_rect_make(24, 13, 752, 30), line, UI_FONT_HEAD,
                ui_is_light(head) ? UI_TEXT_ON_LIGHT : GFX_WHITE, 1,
                GFX_ALIGN_LEFT);
    const unsigned pct = (u.v.total > 0u)
        ? (unsigned)((uint64_t)u.v.written * 100u / u.v.total) : 0u;
    if (!fetch || u.v.total > 0u) {
        /* A download names its bytes once the image's size is known. */
        snprintf(line, sizeof(line), TR(UP_BYTES),
                 (unsigned long)u.v.written, (unsigned long)u.v.total, pct);
        gfx_text_in(c, gfx_rect_make(24, 96, 752, 30), line, UI_FONT_HEAD,
                    UI_TEXT, 1, GFX_ALIGN_LEFT);
    }
    ui_bar(c, gfx_rect_make(24, 140, 752, 24),
           (u.v.total > 0u) ? (float)u.v.written / (float)u.v.total : 0.0f,
           -1.0f, failed ? UI_DANGER : UI_ACCENT);
    int y = 200;
    if (u.v.state == UPDATE_FETCHING) {
        gfx_text_in(c, gfx_rect_make(24, y, 752, 30), u.v.what, UI_FONT_HEAD,
                    UI_TEXT, 1, GFX_ALIGN_LEFT);
        gfx_text_in(c, gfx_rect_make(24, y + 40, 752, 18), u.v.why,
                    UI_FONT_LABEL, UI_TEXT_DIM, 1, GFX_ALIGN_LEFT);
        gfx_text_in(c, gfx_rect_make(24, y + 80, 752, 18), TR(UPD_DL_NOTE_1),
                    UI_FONT_LABEL, UI_TEXT, 1, GFX_ALIGN_LEFT);
        gfx_text_in(c, gfx_rect_make(24, y + 104, 752, 18),
                    TR(UPD_DL_NOTE_2), UI_FONT_LABEL, UI_TEXT_DIM, 1,
                    GFX_ALIGN_LEFT);
        ui_button(c, U_CLOSE, TR(CANCEL), UI_PANEL_HI, u.pressed, true);
    } else if (u.v.state == UPDATE_FETCH_FAILED) {
        gfx_text_in(c, gfx_rect_make(24, y, 752, 30), TR(UPD_DL_FAILED),
                    UI_FONT_HEAD, UI_DANGER, 1, GFX_ALIGN_LEFT);
        y += 44;
        gfx_text_in(c, gfx_rect_make(24, y, 752, 18), u.v.what,
                    UI_FONT_LABEL, UI_WARN, 1, GFX_ALIGN_LEFT);
        if (u.v.why[0] != '\0') {
            y += 24;
            gfx_text_in(c, gfx_rect_make(24, y, 752, 18), u.v.why,
                        UI_FONT_LABEL, UI_TEXT_DIM, 1, GFX_ALIGN_LEFT);
        }
        y += 32;
        gfx_text_in(c, gfx_rect_make(24, y, 752, 18), TR(UP_FAILED_2),
                    UI_FONT_LABEL, UI_TEXT_DIM, 1, GFX_ALIGN_LEFT);
        ui_button(c, U_CLOSE, TR(UP_CLOSE), UI_PANEL_HI, u.pressed, true);
    } else if (u.v.state == UPDATE_RESTARTING) {
        /* Nothing under it: a line longer than the 47 cells breaks onto
         * the next, 40 px lower. */
        (void)ui_text_wrap(c, gfx_rect_make(24, y, 752, 30), 40,
                           TR(UP_RESTART), UI_FONT_HEAD, UI_OK);
    } else if (failed) {
        gfx_text_in(c, gfx_rect_make(24, y, 752, 30), TR(UP_FAILED),
                    UI_FONT_HEAD, UI_DANGER, 1, GFX_ALIGN_LEFT);
        y += 44;
        gfx_text_in(c, gfx_rect_make(24, y, 752, 18), TR(UP_FAILED_2),
                    UI_FONT_LABEL, UI_TEXT_DIM, 1, GFX_ALIGN_LEFT);
        ui_button(c, U_CLOSE, TR(UP_CLOSE), UI_PANEL_HI, u.pressed, true);
    } else {
        gfx_text_in(c, gfx_rect_make(24, y, 752, 18), TR(UP_NOTE_1),
                    UI_FONT_LABEL, UI_TEXT, 1, GFX_ALIGN_LEFT);
        gfx_text_in(c, gfx_rect_make(24, y + 24, 752, 18), TR(UP_NOTE_2),
                    UI_FONT_LABEL, UI_TEXT_DIM, 1, GFX_ALIGN_LEFT);
        gfx_text_in(c, gfx_rect_make(24, y + 48, 752, 18), TR(WF_Q_NOTE_2),
                    UI_FONT_LABEL, UI_TEXT_DIM, 1, GFX_ALIGN_LEFT);
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
