/**
 * @file motor_program_page.c
 * @brief MOTOR & ESC's PROGRAM: the registry's list and the output strip
 *        over a full-width session.
 *
 * MOTOR & ESC's PROGRAM: the list of the registry's MOTOR rows
 * (prog_registry.h) in the left column, and the session a row opens.
 *
 * The list is the registry's view: one group, STICK MENU, with one
 * row that opens ESC STICK.  The output card stays on the right while the
 * list shows.  A live row's session is full width: it covers the left
 * column and the rail, one level down, and the strip row above it holds the
 * output strip in place of the tabs and the card:
 *
 *     TARGET DSHOT300 GP0 GP1   SUPPLY 7.60V  1.00A   SAFE
 *
 * what the throttle drives, the supply's output as it reads, and the bench.
 * The home tag climbs the session one level at a time (its back()); from
 * its top it closes back to the list, and from the list it leaves the
 * screen, which switches the bench to SAFE.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include "motor_screen.h"
#include "prog_registry.h"
#include "supply_screen.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "ui_workbench.h"

/* Last: its layout macros have short names. */
#include "motor_screen_int.h"

static struct {
    prog_view_t view;
    /* Bumped when the list's look changes: a press, a page, a search. */
    uint32_t    rev;
    /* What the rows showed when last drawn (prog_signature()). */
    uint32_t    sig;
} p;

/* The IOmcu's capabilities, which the rows' states follow. */
static uint16_t caps_now(void)
{
    return ui_router_status()->capabilities;
}

static void list_now(prog_list_t *l)
{
    size_t n = 0;
    const prog_reg_row_t *rows = prog_registry(&n);
    prog_list_build(l, rows, n, PROG_DEV_MOTOR, UI_WB_PROGRAM, p.view.query);
}

/* The session's area: the body under the output strip. */
static gfx_rect_t full_area(void)
{
    return (gfx_rect_t){ 0, PROG_FULL_Y, W, (int16_t)(H - PROG_FULL_Y) };
}

void mpp_reset(void)
{
    memset(&p, 0, sizeof(p));
    prog_view_init(&p.view, (gfx_rect_t){ PAGE_X, PAGE_Y, PAGE_W, PAGE_H });
    const prog_page_set_t *set[PROG_LIST_MAX];
    const int k = prog_reg_sets(PROG_DEV_MOTOR, set);
    for (int i = 0; i < k; ++i) {
        prog_ps_init(set[i], full_area());
    }
}

void mpp_enter(void)
{
    const prog_page_set_t *set[PROG_LIST_MAX];
    const int k = prog_reg_sets(PROG_DEV_MOTOR, set);
    for (int i = 0; i < k; ++i) {
        prog_ps_entry(set[i]);
    }
    prog_view_cancel(&p.view);
    ++p.rev;
}

void mpp_leave(void)
{
    const prog_page_set_t *set[PROG_LIST_MAX];
    const int k = prog_reg_sets(PROG_DEV_MOTOR, set);
    for (int i = 0; i < k; ++i) {
        prog_ps_leave(set[i]);
    }
    prog_view_cancel(&p.view);
    ++p.rev;
}

void mpp_cancel(void)
{
    const prog_page_set_t *set[PROG_LIST_MAX];
    const int k = prog_reg_sets(PROG_DEV_MOTOR, set);
    for (int i = 0; i < k; ++i) {
        prog_ps_cancel(set[i]);
    }
    prog_view_cancel(&p.view);
    ++p.rev;
}

void mpp_armed(bool armed)
{
    const prog_page_set_t *set[PROG_LIST_MAX];
    const int k = prog_reg_sets(PROG_DEV_MOTOR, set);
    for (int i = 0; i < k; ++i) {
        prog_ps_armed(set[i], armed);
    }
}

const prog_page_set_t *mpp_shown(void)
{
    if (mwb_task() != UI_WB_PROGRAM) {
        return NULL;
    }
    const prog_page_set_t *set[PROG_LIST_MAX];
    const int k = prog_reg_sets(PROG_DEV_MOTOR, set);
    for (int i = 0; i < k; ++i) {
        if (prog_ps_is_open(set[i])) {
            return set[i];
        }
    }
    return NULL;
}

/* What PROGRAM's rows show, as one number: a change repaints the list. */
static uint32_t prog_signature(void)
{
    size_t n = 0;
    const prog_reg_row_t *rows = prog_registry(&n);
    const uint16_t caps = caps_now();
    uint32_t sig = (uint32_t)caps * 2654435761u;
    for (size_t i = 0; i < n; ++i) {
        if (rows[i].device != PROG_DEV_MOTOR) {
            continue;
        }
        const uint8_t pin = (rows[i].page_set != NULL)
                                ? prog_ps_pin(rows[i].page_set)
                                : PROG_REG_NO_PIN;
        sig = sig * 31u + (uint32_t)prog_reg_state(rows, n, i, caps) * 257u
              + pin + (uint32_t)prog_reg_port(&rows[i]) * 65537u;
    }
    return sig;
}

void mpp_tick(float dt_s)
{
    const prog_page_set_t *set[PROG_LIST_MAX];
    const int k = prog_reg_sets(PROG_DEV_MOTOR, set);
    for (int i = 0; i < k; ++i) {
        prog_ps_tick(set[i], dt_s);
    }
    if (mwb_task() == UI_WB_PROGRAM && mpp_shown() == NULL) {
        const uint32_t sig = prog_signature();
        if (sig != p.sig) {
            p.sig = sig;
            ++p.rev;
        }
    }
}

bool mpp_back(void)
{
    const prog_page_set_t *ps = mpp_shown();
    if (ps == NULL) {
        return false;
    }
    prog_ps_back(ps);
    ++p.rev;
    return true;
}

/* Row @p i tapped: a live row's session opens, one level down; a dimmed
 * row opens nothing, its reason drawn beside it. */
static void row_open(uint8_t i)
{
    size_t n = 0;
    const prog_reg_row_t *rows = prog_registry(&n);
    if (i >= n || rows[i].device != PROG_DEV_MOTOR
        || prog_reg_state(rows, n, i, caps_now()) != PROG_ROW_LIVE) {
        return;
    }
    prog_ps_open(rows[i].page_set);
    ++p.rev;
}

bool mpp_event(const touch_event_t *evt)
{
    if (evt == NULL) {
        return false;
    }
    const bool mine = p.view.pressed != PROG_VIEW_P_NONE
                      && evt->point.id == p.view.id;
    if (evt->type != TOUCH_EVENT_DOWN && !mine) {
        return false;
    }
    prog_list_t l;
    list_now(&l);
    uint8_t row = 0;
    const prog_view_ev_t r = prog_view_event(&p.view, &l, evt, &row);
    if (r == PROG_VIEW_NONE) {
        return false;
    }
    ++p.rev;
    if (r == PROG_VIEW_ROW) {
        row_open(row);
    }
    /* SEARCH is offered over 9 rows, which MOTOR's list does not reach:
     * its keyboard comes with the tenth row. */
    return true;
}

bool mpp_pressed(void)
{
    return p.view.pressed != PROG_VIEW_P_NONE;
}

void mpp_cancel_press(void)
{
    prog_view_cancel(&p.view);
    ++p.rev;
}

uint32_t mpp_revision(void) { return p.rev; }

void mpp_render_page(gfx_canvas_t *c)
{
    gfx_fill_rect(c, PAGE_X, PAGE_Y, PAGE_W, PAGE_H,
                  ui_theme_color(UI_C_PANEL));
    gfx_draw_rect(c, PAGE_X, PAGE_Y, PAGE_W, PAGE_H,
                  ui_theme_color(UI_C_EDGE));
    size_t n = 0;
    const prog_reg_row_t *rows = prog_registry(&n);
    prog_list_t l;
    list_now(&l);
    prog_view_render(&p.view, &l, rows, n, caps_now(), p.view.query, c);
}

/* ---------------------------------------------------- the output strip */

/* The cells the TARGET value takes in the strip: the CONTROL card's 18 and
 * room for two more pins. */
#define STRIP_TARGET_CELLS 26u

static void supply_text(char *out, size_t size)
{
    const supply_state_t *st = supply_screen_reading();
    if (st == NULL || !st->online) {
        snprintf(out, size, "%s", TR(PL_STRIP_SUP_NONE));
        return;
    }
    if (!st->output) {
        snprintf(out, size, "%s", TR(PL_STRIP_SUP_OFF));
        return;
    }
    if ((st->ok & (SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT))
        != (SUPPLY_OK_VOLTAGE | SUPPLY_OK_CURRENT)
        || !(st->v >= 0.0f) || !(st->i >= 0.0f)) {
        snprintf(out, size, "%s", TR(PL_STRIP_SUP_ON));
        return;
    }
    const unsigned mv = (unsigned)(st->v * 1000.0f + 0.5f);
    const unsigned ma = (unsigned)(st->i * 1000.0f + 0.5f);
    snprintf(out, size, TR(PL_STRIP_SUP), mv / 1000u, mv % 1000u / 10u,
             ma / 1000u, ma % 1000u / 10u);
}

size_t motor_output_strip(char *out, size_t size, uint8_t proto,
                          uint64_t pins, bool known, bool armed, bool *warn)
{
    if (out == NULL || size == 0u) {
        return 0u;
    }
    char target[48];
    const bool w = motor_throttle_target(target, sizeof(target), proto, pins,
                                         known, STRIP_TARGET_CELLS);
    if (warn != NULL) {
        *warn = w;
    }
    char sup[40];
    supply_text(sup, sizeof(sup));
    const int n = snprintf(out, size, "%s %s   %s   %s", TR(SV_TARGET),
                           target, sup, armed ? "ARMED" : TR(BAND_SAFE));
    return (n < 0) ? 0u : ((size_t)n < size ? (size_t)n : size - 1u);
}

uint32_t mpp_strip_signature(uint8_t proto, uint64_t pins, bool known,
                             bool armed)
{
    char line[160];
    (void)motor_output_strip(line, sizeof(line), proto, pins, known, armed,
                             NULL);
    uint32_t h = 2166136261u;
    for (const char *q = line; *q != '\0'; ++q) {
        h = (h ^ (uint8_t)*q) * 16777619u;
    }
    return h;
}

/**
 * @brief The strip in its row, over the clear, in PROG_FULL_STRIP_FONT:
 *        TARGET dim, the target in the text colour (the warning colour with
 *        no pin or the binding not read), the supply dim, and the bench
 *        SAFE or, in the danger colour, ARMED.
 * @param c The canvas.
 * @param proto The throttle's protocol.
 * @param pins The pins bound as throttle, one bit each.
 * @param known Whether the binding is read.
 * @param armed Whether the bench is ARMED.
 */
void mpp_render_strip(gfx_canvas_t *c, uint8_t proto, uint64_t pins,
                      bool known, bool armed)
{
    gfx_fill_rect(c, 0, 0, W, PROG_FULL_Y, ui_theme_color(UI_C_BG));
    char target[48];
    const bool warn = motor_throttle_target(target, sizeof(target), proto,
                                            pins, known, STRIP_TARGET_CELLS);
    char sup[40];
    supply_text(sup, sizeof(sup));
    const int y = PROG_FULL_STRIP_Y;
    int x = PROG_FULL_STRIP_X;
    x += gfx_text(c, x, y, TR(SV_TARGET), PROG_FULL_STRIP_FONT,
                  ui_theme_color(UI_C_TEXT_DIM), 1) + 8;
    x += gfx_text(c, x, y, target, PROG_FULL_STRIP_FONT,
                  warn ? ui_theme_color(UI_C_WARN)
                       : ui_theme_color(UI_C_TEXT), 1) + 24;
    x += gfx_text(c, x, y, sup, PROG_FULL_STRIP_FONT,
                  ui_theme_color(UI_C_TEXT_DIM), 1) + 24;
    (void)gfx_text(c, x, y, armed ? "ARMED" : TR(BAND_SAFE), PROG_FULL_STRIP_FONT,
                   armed ? ui_theme_color(UI_C_DANGER)
                         : ui_theme_color(UI_C_OK), 1);
}
