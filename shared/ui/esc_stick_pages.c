/*
 * ESC STICK as a page set (prog_registry.h): the session MOTOR & ESC opens
 * from the STICK MENU row of PROGRAM's list, full width.
 *
 * This file is the session's frame: the state the pages share (pg), the
 * controls in their places, the tap walk every control but three acts
 * through, the way back one level at a time, and the hooks the host calls.
 * The pages themselves are esc_stick_panel.c's; the layout and the walk's
 * rules are esc_stick_int.h's.
 *
 * The session draws from PROG_FULL_Y down; the host draws the output strip
 * above it.  It holds no programming port, so it never holds the host's
 * way out: leaving MOTOR & ESC ends a run, switches the bench to SAFE and
 * closes the session.
 *
 * SPDX-License-Identifier: MIT
 */

#include "esc_stick_panel.h"

#include <string.h>

#include "esc_stick_int.h"
#include "ui_text.h"
#include "ui_theme.h"

/* The steppers, right of the control column. */
#define STEP_DN_X 636
#define STEP_UP_X 748

/* The part esc_stick_panel.c reads and writes as well. */
stick_shared_t pg;

int esc_stick_panel_stage(void) { return (int)pg.stage; }

/* ------------------------------------------------------------ the layout */

/* Start over: closed, nothing held, every control in its place.  The area
 * is the body under the output strip; the pages' layout is fixed to it
 * (esc_stick_int.h). */
static void esc_stick_ps_init(gfx_rect_t area)
{
    (void)area;
    memset(&pg, 0, sizeof(pg));
    pg.stage = STAGE_CLOSED;
    pg.back      = (gfx_rect_t){ (int16_t)(PAD + 12), CRUMB_Y, 96, 30 };
    pg.read_btn  = (gfx_rect_t){ (int16_t)(W - PAD - 12 - 350),
                                 (int16_t)BTN_Y, 168, 34 };
    pg.write_btn = (gfx_rect_t){ (int16_t)(W - PAD - 12 - 168),
                                 (int16_t)BTN_Y, 168, 34 };
    pg.page_up   = (gfx_rect_t){ (int16_t)(W - PAD - 12 - 76),
                                 (int16_t)(PARM_Y + 6), STEP_W, 24 };
    pg.page_dn   = (gfx_rect_t){ (int16_t)(W - PAD - 12 - 38),
                                 (int16_t)(PARM_Y + 6), STEP_W, 24 };
    for (int i = 0; i < ROWS_MAX; ++i) {
        const int y = ROW_Y0 + i * ROW_H;
        pg.row[i]  = (gfx_rect_t){ (int16_t)(PAD + 8), (int16_t)(y - 4),
                                   (int16_t)(CTRL_X - PAD - 16), ROW_H - 2 };
        pg.down[i] = (gfx_rect_t){ STEP_DN_X, (int16_t)(y - 2), STEP_W, 24 };
        pg.up[i]   = (gfx_rect_t){ STEP_UP_X, (int16_t)(y - 2), STEP_W, 24 };
    }
    esc_stick_panel_reset();
}

/* ------------------------------------------------------------- the walk */

/* What decides which control lies under a point.  A press and its release
 * with a different answer are not a tap: the control has changed under the
 * finger. */
static uint32_t showing(void)
{
    const uint32_t h = (2166136261u ^ (uint32_t)pg.stage) * 16777619u;
    return esc_stick_panel_showing(h);
}

/* The controls under the point of @p evt (see hit()).  BACK climbs one
 * level, as the band's home tag does; the pages take the rest. */
static void press_at(const touch_event_t *evt)
{
    if (pg.stage != STAGE_CLOSED) {
        (void)esc_stick_panel_down(evt);
    }
}

/* The rest of the press that may become a tap: its moves and its release. */
static void tap_follow(const touch_event_t *evt)
{
    if (!pg.tap.live || evt->point.id != pg.tap.id) {
        return;
    }
    int dx = evt->point.x - pg.tap.x, dy = evt->point.y - pg.tap.y;
    if (dx < 0) { dx = -dx; }
    if (dy < 0) { dy = -dy; }
    const bool on = dx <= TAP_SLOP && dy <= TAP_SLOP
                    && gfx_rect_contains(pg.tap.rect, evt->point.x,
                                         evt->point.y);
    if (evt->type == TOUCH_EVENT_MOVE) {
        if (!on) {
            pg.tap.live = false;     /* moved off: the press ends */
        }
        return;
    }
    pg.tap.live = false;
    if (!on || !touch_event_is_tap_up(evt) || showing() != pg.tap.showing) {
        return;
    }
    touch_event_t press = *evt;
    press.type    = TOUCH_EVENT_DOWN;
    press.point.x = pg.tap.x;
    press.point.y = pg.tap.y;
    pg.pass = PASS_TAP;
    press_at(&press);
    pg.pass = PASS_PRESS;
}

static void esc_stick_ps_event(const touch_event_t *evt)
{
    if (evt == NULL || pg.stage == STAGE_CLOSED) {
        return;
    }
    if (evt->type != TOUCH_EVENT_DOWN) {
        /* The search's keys, the warning's hold and a press that may
         * become a tap follow a finger. */
        esc_stick_panel_follow(evt);
        tap_follow(evt);
        return;
    }
    /* A DOWN with the id of the press held says that contact has gone and
     * its release with it. */
    if (pg.tap.live && evt->point.id == pg.tap.id) {
        pg.tap.live = false;
    }
    pg.pass  = PASS_PRESS;
    pg.found = false;
    press_at(evt);
    /* One press at a time: a second contact arms nothing.  The controls
     * that act on the press have acted above, whichever contact it is. */
    if (pg.found && !pg.tap.live) {
        pg.tap.live    = true;
        pg.tap.id      = evt->point.id;
        pg.tap.x       = evt->point.x;
        pg.tap.y       = evt->point.y;
        pg.tap.rect    = pg.found_rect;
        pg.tap.showing = showing();
    }
}

/* ------------------------------------------------------------ the hooks */

static void esc_stick_ps_entry(void)
{
    esc_stick_panel_enter();
}

static void esc_stick_ps_open(void)
{
    esc_stick_panel_open();
}

static bool esc_stick_ps_is_open(void)
{
    return pg.stage != STAGE_CLOSED;
}

/* One level up, BACK's and the home tag's: what covers the page closes, a
 * run ends, or the session climbs; from the makers it closes. */
static void esc_stick_ps_back(void)
{
    if (pg.stage != STAGE_CLOSED) {
        pg.tap.live = false;
        (void)esc_stick_panel_back();
    }
}

/* No programming port: a run holds the bench, and leaving ends it. */
static bool esc_stick_ps_holds(void)
{
    return false;
}

/* The bench's arm reaches the panel from the application
 * (esc_stick_panel_bench()). */
static void esc_stick_ps_armed(bool armed)
{
    (void)armed;
}

static void esc_stick_ps_tick(float dt_s)
{
    esc_stick_panel_tick(dt_s);
}

static bool esc_stick_ps_has_press(uint8_t id)
{
    return (pg.tap.live && pg.tap.id == id) || esc_stick_panel_holds_press(id);
}

/* The body under the output strip, cleared, then the page. */
static void esc_stick_ps_render(gfx_canvas_t *c)
{
    gfx_fill_rect(c, 0, PROG_FULL_Y, W, H - PROG_FULL_Y,
                  ui_theme_color(UI_C_BG));
    if (pg.stage != STAGE_CLOSED) {
        esc_stick_panel_render(c);
    }
}

static uint32_t esc_stick_ps_revision(void)
{
    return pg.rev;
}

/* A hold's fill moves the revision itself: the warning's HOLD TO RUN is
 * the page's. */
static uint32_t esc_stick_ps_hold_revision(void)
{
    return 0u;
}

static gfx_rect_t esc_stick_ps_hold_rect(void)
{
    return (gfx_rect_t){ 0, 0, 0, 0 };
}

/* The host is left: a run ends, the supply it switched on goes off, and
 * the session closes; the search keeps what was typed. */
static void esc_stick_ps_leave(void)
{
    esc_stick_panel_leave();
    pg.stage = STAGE_CLOSED;
    ++pg.rev;
}

static void esc_stick_ps_cancel(void)
{
    esc_stick_panel_cancel();
}

static uint8_t esc_stick_ps_pin(void)
{
    return PROG_REG_NO_PIN;
}

const prog_page_set_t esc_stick_pages = {
    .init          = esc_stick_ps_init,
    .entry         = esc_stick_ps_entry,
    .open          = esc_stick_ps_open,
    .is_open       = esc_stick_ps_is_open,
    .back          = esc_stick_ps_back,
    .holds_port    = esc_stick_ps_holds,
    .armed         = esc_stick_ps_armed,
    .tick          = esc_stick_ps_tick,
    .has_press     = esc_stick_ps_has_press,
    .event         = esc_stick_ps_event,
    .render        = esc_stick_ps_render,
    .revision      = esc_stick_ps_revision,
    .hold_revision = esc_stick_ps_hold_revision,
    .hold_rect     = esc_stick_ps_hold_rect,
    .leave         = esc_stick_ps_leave,
    .cancel        = esc_stick_ps_cancel,
    .pin           = esc_stick_ps_pin,
};
