/**
 * @file upd_c6.c
 * @brief SETUP > UPDATES' C6 RADIO row. See upd_c6.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "upd_c6.h"

#include <stdio.h>
#include <string.h>

#include "ui_layout.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "upd_screen.h"

/** @brief The row's face: UPDATES' buttons and the IOmcu section's. */
#define C6_FONT (&gfx_font_10x20)

_Static_assert(UPD_C6_BTN_H >= UI_TAP_MIN, "the C6 hold is a finger tall");
_Static_assert(UPD_C6_LINE_Y + 20 + 8 <= UPD_C6_BTN_Y,
               "the versions' line ends over the hold");
_Static_assert(UPD_C6_BTN_Y + UPD_C6_BTN_H <= UI_ALERT_Y - UI_BAND_H - 4,
               "the C6 hold ends over the alert band");
_Static_assert(UPD_C6_X + UPD_C6_W <= UI_PANEL_W - UI_MARGIN,
               "the C6 row keeps the margin");

static upd_c6_view_t s_v;     /**< what the row shows */
static bool          s_req;   /**< the hold completed, not yet taken */

/** @brief The row's press. */
static struct {
    bool      have;     /**< a press is on the row                     */
    uint8_t   id;       /**< its contact                               */
    bool      live;     /**< it began on a live hold                   */
    ui_hold_t hold;     /**< the 2s hold                               */
} s;                    /**< the row's one press                      */

/** @brief Whether the hold may run now. @return true while READY */
static bool live(void) { return s_v.row == FW_C6_ROW_READY; }

gfx_rect_t upd_c6_rect(void)
{
    return gfx_rect_make(UPD_C6_X, UPD_C6_BTN_Y, UPD_C6_W, UPD_C6_BTN_H);
}

void upd_c6_set(const upd_c6_view_t *v)
{
    /* As upd_screen_set(): padding costs a repaint at most. */
    /* NOLINTNEXTLINE(bugprone-suspicious-memory-comparison) */
    if (v == NULL || memcmp(v, &s_v, sizeof(s_v)) == 0) {
        return;
    }
    s_v = *v;
    if (!live() && s.have && s.live) {
        (void)ui_hold_left(&s.hold);
        s.have = false;
    }
    upd_screen_invalidate();
}

const upd_c6_view_t *upd_c6_view(void) { return &s_v; }

bool upd_c6_take(void)
{
    const bool was = s_req;
    s_req = false;
    return was;
}

const char *upd_c6_reason(const upd_c6_view_t *v)
{
    if (v == NULL) {
        return NULL;
    }
    switch (v->row) {
    case FW_C6_ROW_NO_IMAGE:    return TR(C6_WHY_NO_IMAGE);
    case FW_C6_ROW_BAD_IMAGE:   return TR(C6_WHY_BAD_IMAGE);
    case FW_C6_ROW_UPDATING:    return TR(C6_WHY_UPDATING);
    case FW_C6_ROW_LINK_FAILED: return TR(C6_WHY_LINK);
    case FW_C6_ROW_SAME:        return TR(C6_WHY_SAME);
    case FW_C6_ROW_BLOCKED:
        switch (v->why) {
        case FW_C6_WHY_ARMED:   return TR(C6_WHY_ARMED);
        case FW_C6_WHY_SUPPLY:  return TR(C6_WHY_SUPPLY);
        case FW_C6_WHY_RUN:     return TR(C6_WHY_RUN);
        case FW_C6_WHY_DRAWING: return TR(C6_WHY_DRAWING);
        case FW_C6_WHY_IOMCU:   return TR(C6_WHY_IOMCU);
        case FW_C6_WHY_INSTALL: return TR(C6_WHY_INSTALL);
        case FW_C6_WHY_RADIO:   return TR(C6_WHY_RADIO);
        case FW_C6_WHY_ENDING:
        case FW_C6_WHY_NONE:
        default:                return TR(C6_WHY_ENDING);
        }
    case FW_C6_ROW_READY:
    default:
        return NULL;
    }
}

/**
 * @brief A version as text, or @p none's text.
 * @param out  the text
 * @param n    bytes of @p out
 * @param have whether there is a version
 * @param v    the version
 * @param none the text for none
 */
static void ver_text(char *out, size_t n, bool have, fw_version_t v,
                     const char *none)
{
    if (have) {
        snprintf(out, n, "%u.%u.%u", (unsigned)v.major, (unsigned)v.minor,
                 (unsigned)v.patch);
    } else {
        snprintf(out, n, "%s", none);
    }
}

void upd_c6_line(const upd_c6_view_t *v, char *out, size_t n)
{
    char run[24], car[24];
    ver_text(run, sizeof(run), v->known, v->running,
             TR(UPD_C6_NOT_REPORTED));
    ver_text(car, sizeof(car), v->have_image, v->carried, TR(UPD_C6_NONE));
    snprintf(out, n, TR(UPD_C6_LINE), run, car);
}

void upd_c6_label(const upd_c6_view_t *v, char *out, size_t n)
{
    if (v->row == FW_C6_ROW_UPDATING) {
        snprintf(out, n, TR(UPD_C6_PROGRESS),
                 v->percent > 100u ? 100u : v->percent);
    } else {
        snprintf(out, n, "%s", TR(UPD_C6_HOLD));
    }
}

bool upd_c6_pressed(void) { return s.have; }

bool upd_c6_event(const touch_event_t *evt, bool other)
{
    if (evt == NULL) {
        return false;
    }
    const int x = evt->point.x, y = evt->point.y;
    const gfx_rect_t r = upd_c6_rect();
    if (evt->type == TOUCH_EVENT_DOWN) {
        if (other || s.have || !gfx_rect_contains(r, x, y)) {
            return false;
        }
        s.have = true;
        s.id = evt->point.id;
        s.live = live();
        if (s.live) {
            ui_hold_begin(&s.hold, TX_HOLD_HINT_C6_UPDATE);
        }
        upd_screen_invalidate();
        return true;
    }
    if (!s.have || evt->point.id != s.id) {
        return false;
    }
    if (evt->type == TOUCH_EVENT_MOVE) {
        /* Sliding off abandons it, as on every held control. */
        if (!gfx_rect_contains(r, x, y)) {
            if (s.live) {
                (void)ui_hold_leave(&s.hold);
            }
            s.have = false;
            upd_screen_invalidate();
        }
        return true;
    }
    if (evt->type != TOUCH_EVENT_UP) {
        return true;
    }
    s.have = false;
    upd_screen_invalidate();
    if (s.live) {
        /* The hold acts when it completes; the release only ends it. */
        (void)ui_hold_end(&s.hold, evt, r);
        return true;
    }
    /* A tap on a grey hold: why, on the band. */
    if (touch_event_is_tap_up(evt) && gfx_rect_contains(r, x, y)) {
        ui_router_set_notice(upd_c6_reason(&s_v));
    }
    return true;
}

bool upd_c6_tick(float dt_s)
{
    const bool moving = s.hold.down || s.hold.flash_ms > 0;
    ui_hold_flash_step(&s.hold, dt_s);
    if (s.have && s.live && live() && ui_hold_tick(&s.hold, dt_s)) {
        s_req = true;
        ui_hold_reached(&s.hold);
    }
    return moving;
}

bool upd_c6_flashing(void) { return s.hold.flash_ms > 0; }

void upd_c6_drop(void)
{
    if (s.have && s.live) {
        (void)ui_hold_left(&s.hold);
    }
    s.have = false;
    ui_hold_reset(&s.hold);
}

void upd_c6_render(gfx_canvas_t *c)
{
    char line[96];
    upd_c6_line(&s_v, line, sizeof(line));
    gfx_text_in(c, gfx_rect_make(UPD_C6_X, UPD_C6_LINE_Y, UPD_C6_W,
                                 gfx_text_height(C6_FONT, 1)),
                line, C6_FONT,
                (s_v.row == FW_C6_ROW_LINK_FAILED) ? UI_WARN : UI_TEXT, 1,
                GFX_ALIGN_LEFT);
    upd_c6_label(&s_v, line, sizeof(line));
    gfx_color_t fill = UI_PANEL_HI;
    if (live()) {
        fill = (s.hold.flash_ms > 0)
                   ? ui_hold_flash(UI_OK, s.hold.flash_ms)
                   : ui_hold_fill(UI_PANEL_HI, UI_OK, s.hold.held_s);
    }
    ui_button_font(c, upd_c6_rect(), line, fill, false, live(), C6_FONT);
}
