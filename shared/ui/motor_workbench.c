/*
 * MOTOR & ESC as a workbench (ui_workbench.h): the task strip at the top,
 * and the CONTROL card's TARGET row.
 *
 * The strip shows LIVE (the plot or the table, and the throttle), PROGRAM
 * (the registry's MOTOR rows, motor_program_page.c) and SETTINGS (the ESC
 * / BENCH rows, motor_settings_page.c).  A tile opens LIVE; a tab switch is
 * no navigation level.  Leaving the screen switches
 * the bench to SAFE whichever task is shown (motor_screen.c's leave()).
 *
 * The TARGET row reads what the throttle drives as `protocol pins`
 * ("DSHOT300 GP0 GP1") and is the control that changes it: a tap opens
 * SETUP's OUTPUTS screen, where the motor's pins are bound, and the home tag
 * there comes back to this screen on the task it left.  The row is locked
 * while the bench is not SAFE (an ARM held, asked or in force, or a DISARM
 * not yet answered) and while the servo programmer holds its port, which
 * is when ARM is locked too.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include "motor_screen.h"
#include "out_bind.h"
#include "ui_target_row.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_workbench.h"

/* Last: its layout macros have short names. */
#include "motor_screen_int.h"

/* MOTOR & ESC's tasks.  TEST has no page and is not drawn. */
#define MOTOR_TASKS (UI_WB_HAS(UI_WB_LIVE) | UI_WB_HAS(UI_WB_PROGRAM) \
                     | UI_WB_HAS(UI_WB_SETTINGS))

_Static_assert(MOTOR_PROTO_MIXED == OUTBIND_PROTO_MIXED,
               "the screen's MIXED is the one outbind_role_proto() returns");

static struct {
    ui_wb_strip_t   wb;
    ui_target_row_t trow;
    uint32_t        rev;
    /* ui_router_navigations() just after the TARGET row opened OUTPUTS, or
     * 0: the entry one navigation later is the way back. */
    uint32_t        outputs_nav;
} w;

void mwb_reset(void)
{
    memset(&w, 0, sizeof(w));
    ui_wb_strip_init(&w.wb, MOTOR_TASKS, UI_WB_STRIP_X, UI_WB_STRIP_Y);
    ui_target_row_init(&w.trow, (gfx_rect_t){ (int16_t)(RIGHT_X + INNER),
                                              TARGET_Y,
                                              (int16_t)(RIGHT_W - 2 * INNER),
                                              TARGET_H });
}

void mwb_enter(void)
{
    /* Back from OUTPUTS, which the TARGET row opened, with no screen in
     * between: the task it was opened from.  Any other entry is a tile's,
     * which opens LIVE. */
    const bool back = w.outputs_nav != 0u
                      && ui_router_navigations() == w.outputs_nav + 1u;
    w.outputs_nav = 0u;
    if (!back) {
        ui_wb_strip_open(&w.wb);
    }
    ui_wb_strip_cancel(&w.wb);
    ui_target_row_cancel(&w.trow);
    ++w.rev;
}

void mwb_cancel(void)
{
    ui_wb_strip_cancel(&w.wb);
    ui_target_row_cancel(&w.trow);
    ++w.rev;
}

void mwb_opened_outputs(void)
{
    w.outputs_nav = ui_router_navigations();
}

ui_wb_task_t mwb_task(void) { return ui_wb_strip_task(&w.wb); }

uint32_t mwb_revision(void) { return w.rev; }

gfx_rect_t mwb_strip_area(void)
{
    const gfx_rect_t r = ui_wb_strip_rect(&w.wb);
    return (gfx_rect_t){ (int16_t)(r.x - 3), (int16_t)(r.y - 3),
                         (int16_t)(r.w + 6), (int16_t)(r.h + 6) };
}

gfx_rect_t mwb_target_rect(void) { return w.trow.rect; }

bool mwb_pressed(void)
{
    return ui_wb_strip_pressed(&w.wb) || w.trow.down;
}

mwb_ev_t mwb_event(const touch_event_t *evt, bool strip, bool locked)
{
    if (evt == NULL) {
        return MWB_NONE;
    }
    /* The rest of a press on the strip: its UP switches the task. */
    if (ui_wb_strip_pressed(&w.wb) && evt->point.id == w.wb.tabs.press_id) {
        if (evt->type != TOUCH_EVENT_UP && evt->type != TOUCH_EVENT_DOWN) {
            return MWB_TAKEN;
        }
        const bool changed = strip && ui_wb_strip_event(&w.wb, evt);
        if (!strip) {
            ui_wb_strip_cancel(&w.wb);
        }
        ++w.rev;
        return changed ? MWB_TASK : MWB_TAKEN;
    }
    /* A DOWN on the strip, unless the keypad covers it. */
    if (evt->type == TOUCH_EVENT_DOWN && strip
        && gfx_rect_contains(mwb_strip_area(), evt->point.x, evt->point.y)) {
        if (!ui_wb_strip_pressed(&w.wb)) {
            (void)ui_wb_strip_event(&w.wb, evt);
            ++w.rev;
        }
        return MWB_TAKEN;
    }
    const ui_target_ev_t r = ui_target_row_event(&w.trow, evt, locked);
    if (r == UI_TARGET_NONE) {
        return MWB_NONE;
    }
    ++w.rev;
    return (r == UI_TARGET_TAP) ? MWB_TARGET : MWB_TAKEN;
}

void mwb_render_strip(gfx_canvas_t *c)
{
    const gfx_rect_t a = mwb_strip_area();
    gfx_fill_rect(c, a.x, a.y, a.w, a.h, ui_theme_color(UI_C_BG));
    ui_wb_strip_render(&w.wb, c);
}

void mwb_render_target(gfx_canvas_t *c, const char *value, bool warn,
                       bool locked)
{
    const gfx_rect_t r = w.trow.rect;
    gfx_fill_rect(c, r.x, r.y, r.w, r.h, ui_theme_color(UI_C_PANEL));
    ui_target_row_render(&w.trow, c, TR(SV_TARGET), value, warn, locked);
}

/* ------------------------------------------------------- the TARGET text */

unsigned motor_target_cells(void)
{
    return ui_target_row_cells(&w.trow);
}

bool motor_throttle_target(char *out, size_t size, uint8_t proto,
                           uint64_t pins, bool known, unsigned cells)
{
    if (out == NULL || size == 0u) {
        return false;
    }
    if (!known) {
        snprintf(out, size, "%s", TR(MO_TGT_UNREAD));
        return true;
    }
    if (pins == 0u) {
        snprintf(out, size, "%s", TR(MO_TGT_NONE));
        return true;
    }
    /* One protocol by its catalogue name, the same in every language;
     * several as MIXED; none known (a binding no entry describes), the
     * pins alone. */
    const char *name = NULL;
    if (proto == MOTOR_PROTO_MIXED) {
        name = TR(MO_TGT_MIXED);
    } else if (proto > 0u && proto < OUTBIND_PROTOS) {
        name = outbind_protos()[proto].name;
    }
    (void)ui_target_text(out, size, name, pins, cells);
    return false;
}
