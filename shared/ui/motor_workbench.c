/**
 * @file
 * @brief MOTOR & ESC as a workbench (ui_workbench.h): the task strip at the top,
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
#include "port_view.h"
#include "ui_pinlist.h"
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

/**
 * @brief The strip's tabs laid out again at MOTOR & ESC's sizes, MO_TAB_W x
 *        MO_STRIP_H from (MO_STRIP_X, MO_STRIP_Y): the tasks, their order
 *        and their labels as ui_wb_strip_init() set them.
 */
static void strip_widen(void)
{
    ui_tabs_t *t = &w.wb.tabs;
    const int n = t->count;
    ui_text_id_t ids[UI_TABS_MAX] = { (ui_text_id_t)0 };
    for (int i = 0; i < n; ++i) {
        ids[i] = (ui_text_id_t)t->text[i];
    }
    ui_tabs_init_text(t, ids, n,
                      (gfx_rect_t){ MO_STRIP_X, MO_STRIP_Y,
                                    (int16_t)(n * MO_TAB_W
                                              + (n - 1) * MO_TAB_GAP),
                                    MO_STRIP_H });
}

/** @brief The strip and the TARGET row laid out; LIVE. */
void mwb_reset(void)
{
    memset(&w, 0, sizeof(w));
    ui_wb_strip_init(&w.wb, MOTOR_TASKS, MO_STRIP_X, MO_STRIP_Y);
    strip_widen();
    ui_target_row_init(&w.trow, (gfx_rect_t){ RC_X, TARGET_Y, RC_W,
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

uint8_t motor_dshot_ports(void)
{
    const port_view_t *v = port_view_get();
    uint8_t m = 0u;
    for (uint8_t n = 0; n < LINK_PG_PORTS_MAX; ++n) {
        if (port_view_dshot(n) && v->pin[n] != PORT_VIEW_NO_PIN
            && (v->refused & (1u << n)) == 0u) {
            m |= (uint8_t)(1u << n);
        }
    }
    return m;
}

/* Append @p item to @p out (length @p *n) after a space, when it fits in
 * @p cells with @p reserve cells kept free after it. */
static bool put_item(char *out, size_t size, size_t *n, const char *item,
                     unsigned cells, size_t reserve)
{
    const size_t sep = (*n > 0u) ? 1u : 0u;
    const size_t len = strlen(item);
    if (*n + sep + len + reserve > cells || *n + sep + len >= size) {
        return false;
    }
    if (sep != 0u) {
        out[(*n)++] = ' ';
    }
    memcpy(out + *n, item, len + 1u);
    *n += len;
    return true;
}

/* Item @p i of the list the TARGET text names, into @p buf: the slot
 * pins in order, then the ports as PORT n. */
static void item_text(unsigned i, uint64_t pins, uint8_t ports, char *buf,
                      size_t cap)
{
    for (unsigned g = 0; g < UI_PINLIST_PINS; ++g) {
        if ((pins & ((uint64_t)1u << g)) != 0u && i-- == 0u) {
            snprintf(buf, cap, "GP%u", g);
            return;
        }
    }
    for (unsigned k = 0; k < LINK_PG_PORTS_MAX; ++k) {
        if ((ports & (1u << k)) != 0u && i-- == 0u) {
            snprintf(buf, cap, TR(OUT_TESTER_PORT), k + 1u);
            return;
        }
    }
    buf[0] = '\0';
}

static unsigned bits(uint64_t m)
{
    unsigned n = 0u;
    for (; m != 0u; m &= m - 1u) {
        ++n;
    }
    return n;
}

/*
 * The protocol, the slot pins and the DShot programming ports in @p ports,
 * in @p cells: the pins first, then each port as PORT n, and what does not
 * fit as "+N".  A single port with no pin beside it is named in full,
 * PROG PORT n GPx, where that fits.
 */
static void target_with_ports(char *out, size_t size, const char *name,
                              uint64_t pins, uint8_t ports, unsigned cells)
{
    const unsigned count = bits(pins) + bits(ports);
    size_t n = 0u;
    out[0] = '\0';
    if (name != NULL) {
        (void)put_item(out, size, &n, name, cells, 0u);
    }
    if (pins == 0u && bits(ports) == 1u) {
        unsigned k = 0u;
        while ((ports & (1u << k)) == 0u) {
            ++k;
        }
        char full[32];
        snprintf(full, sizeof(full), TR(SV_TARGET_PN_PIN), k + 1u,
                 (unsigned)port_view_get()->pin[k]);
        if (put_item(out, size, &n, full, cells, 0u)) {
            return;
        }
    }
    for (unsigned i = 0; i < count; ++i) {
        char item[16];
        /* "+" and an unsigned of up to 10 digits. */
        char more[12];
        item_text(i, pins, ports, item, sizeof(item));
        const unsigned left = count - i - 1u;
        snprintf(more, sizeof(more), "+%u", left);
        const size_t reserve = (left > 0u) ? 1u + strlen(more) : 0u;
        if (!put_item(out, size, &n, item, cells, reserve)) {
            snprintf(more, sizeof(more), "+%u", count - i);
            (void)put_item(out, size, &n, more, cells, 0u);
            return;
        }
    }
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
    const uint8_t ports = motor_dshot_ports();
    if (pins == 0u && ports == 0u) {
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
    if (ports == 0u) {
        (void)ui_target_text(out, size, name, pins, cells);
        return false;
    }
    /* A DShot port's function by its catalogue name: one name for the
     * slots and the ports, or MIXED. */
    const char *pname = NULL;
    for (uint8_t k = 0; k < LINK_PG_PORTS_MAX; ++k) {
        if ((ports & (1u << k)) == 0u) {
            continue;
        }
        const char *f = port_view_fn_name(port_view_func(k));
        if (pname == NULL) {
            pname = f;
        } else if (strcmp(pname, f) != 0) {
            pname = TR(MO_TGT_MIXED);
        }
    }
    /* Slot pins at a rate no protocol has beside a port are two rates. */
    if (pins == 0u) {
        name = pname;
    } else if (name == NULL || strcmp(name, pname) != 0) {
        name = TR(MO_TGT_MIXED);
    }
    target_with_ports(out, size, name, pins, ports, cells);
    return false;
}
