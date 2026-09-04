/*
 * Protocol on the left, pins on the right.  See outputs_screen.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "outputs_screen.h"

#include <stdio.h>
#include <string.h>

#include "ui_screen.h"
#include "ui_theme.h"
#include "ui_widgets.h"

#define SCREEN_H (480 - UI_BAND_H)
#define MAX_FBS  3

/* Left column: the protocol and what the choice adds up to. */
#define COL_X    16
#define COL_W    208
#define COL_Y    16

#define DD_Y     44
#define DD_H     54

/*
 * The pins: four columns of seven, filled down each column so a column is a
 * run of consecutive GPIOs.  Twenty-six pins leave the last two cells empty,
 * which is better than a grid that has to be read across.
 *
 * Seven rows of 53 plus their gaps is 407 of the 432 the router hands over.
 * Nine rows did not fit, and the cell has to be tall enough for the name and
 * what holds the pin underneath it.
 */
#define GRID_X   236
#define GRID_W   548
#define GRID_Y   16
#define GRID_COLS 4
#define GRID_ROWS 7
#define CELL_GAP 6
#define CELL_W   ((GRID_W - (GRID_COLS - 1) * CELL_GAP) / GRID_COLS)
#define CELL_H   53
#define BOX      20

/* The dropdown, when it is open, covers the pins. */
#define POP_X    COL_X
#define POP_W    300
#define POP_Y    DD_Y
#define POP_ROW  46

enum { HIT_NONE = 0, HIT_DD, HIT_POP, HIT_CELL };

static struct {
    outbind_t bind;
    bool      open;
    int       hit_kind;
    int       hit_index;
    outputs_result_t result;

    outputs_apply_fn apply;

    bool      chrome_valid[MAX_FBS];
    uint32_t  drawn_gen;
    uint32_t  gen;          /* bumped by anything that changes the picture */
} s;

static void touched(void)
{
    ++s.gen;
}

void outputs_screen_invalidate(void)
{
    memset(s.chrome_valid, 0, sizeof(s.chrome_valid));
}

const outbind_t *outputs_screen_binding(void) { return &s.bind; }

void outputs_screen_set_binding(const outbind_t *b)
{
    if (b != NULL) {
        s.bind = *b;
        touched();
    }
}

void outputs_screen_set_apply(outputs_apply_fn fn) { s.apply = fn; }

void outputs_screen_set_result(outputs_result_t r)
{
    if (s.result != r) {
        s.result = r;
        touched();
    }
}

/* Every change goes out at once.  There is no APPLY key: a screen with an
 * unapplied choice on it is a screen that disagrees with the bench, and the
 * operator has no way to see which of the two is driving. */
static void changed(void)
{
    touched();
    if (s.apply != NULL) {
        s.apply(&s.bind);
    }
}

/* ---------------------------------------------------------------- geometry */

static gfx_rect_t dd_rect(void)
{
    return gfx_rect_make(COL_X, DD_Y, COL_W, DD_H);
}

static gfx_rect_t pop_rect(void)
{
    return gfx_rect_make(POP_X, POP_Y, POP_W,
                         (int)(OUTBIND_PROTOS * POP_ROW) + 8);
}

static gfx_rect_t pop_row_rect(int i)
{
    return gfx_rect_make(POP_X + 4, POP_Y + 4 + i * POP_ROW,
                         POP_W - 8, POP_ROW);
}

static gfx_rect_t cell_rect(int i)
{
    const int col = i / GRID_ROWS, row = i % GRID_ROWS;
    return gfx_rect_make(GRID_X + col * (CELL_W + CELL_GAP),
                         GRID_Y + row * (CELL_H + CELL_GAP),
                         CELL_W, CELL_H);
}

static bool inside(gfx_rect_t r, int x, int y)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static int cell_at(int x, int y)
{
    for (int i = 0; i < (int)OUTBIND_PINS; ++i) {
        if (inside(cell_rect(i), x, y)) {
            return i;
        }
    }
    return -1;
}

/* ------------------------------------------------------------------- input */

static void reset(void)
{
    memset(&s, 0, sizeof(s));
    outbind_init(&s.bind);
    touched();
}

static void enter(void)
{
    s.open = false;
    s.hit_kind = HIT_NONE;
    touched();
}

static void leave(void)
{
    s.open = false;
}

static void event(const touch_event_t *evt)
{
    if (evt == NULL) {
        return;
    }
    const int x = evt->point.x, y = evt->point.y;

    if (evt->type == TOUCH_EVENT_DOWN) {
        if (s.open) {
            for (int i = 0; i < (int)OUTBIND_PROTOS; ++i) {
                if (inside(pop_row_rect(i), x, y)) {
                    s.hit_kind = HIT_POP; s.hit_index = i; touched(); return;
                }
            }
            /* A press anywhere else closes it without choosing: an open list
             * must not be a trap. */
            s.open = false; s.hit_kind = HIT_NONE; touched(); return;
        }
        if (inside(dd_rect(), x, y)) {
            s.hit_kind = HIT_DD; touched(); return;
        }
        const int c = cell_at(x, y);
        if (c >= 0) {
            s.hit_kind = HIT_CELL; s.hit_index = c; touched();
        }
        return;
    }

    if (evt->type != TOUCH_EVENT_UP) {
        return;
    }
    const int kind = s.hit_kind, index = s.hit_index;
    s.hit_kind = HIT_NONE;

    if (kind == HIT_DD && inside(dd_rect(), x, y)) {
        s.open = true; touched();
    } else if (kind == HIT_POP && inside(pop_row_rect(index), x, y)) {
        s.open = false;
        outbind_set_proto(&s.bind, (uint8_t)index);
        changed();
    } else if (kind == HIT_CELL && inside(cell_rect(index), x, y)) {
        /* A refused tick is not silent: the cell flashes nothing, but the
         * count under the protocol does not move and the reason is already
         * printed in the cell. */
        if (outbind_toggle(&s.bind, (uint8_t)index)) {
            changed();
        }
    } else {
        touched();
    }
}

/* ------------------------------------------------------------------ render */

static const char *result_text(void)
{
    switch (s.result) {
    case OUTPUTS_OK:      return "WRITTEN";
    case OUTPUTS_NO_LINK: return "NO LINK";
    case OUTPUTS_REFUSED: return "REFUSED";
    case OUTPUTS_IDLE:
    default:              return "NOT WRITTEN";
    }
}

static gfx_color_t result_color(void)
{
    switch (s.result) {
    case OUTPUTS_OK:      return UI_OK;
    case OUTPUTS_NO_LINK: return UI_TEXT_FAINT;
    case OUTPUTS_REFUSED: return UI_DANGER;
    case OUTPUTS_IDLE:
    default:              return UI_TEXT_FAINT;
    }
}

/* The binding can arrive from outside -- read off the wire, or restored --
 * so the index is never trusted to be one this build knows. */
static const outbind_proto_t *chosen_proto(void)
{
    const uint8_t i = (s.bind.proto < OUTBIND_PROTOS) ? s.bind.proto : 0u;
    return &outbind_protos()[i];
}

static void draw_left(gfx_canvas_t *c)
{
    const outbind_proto_t *p = chosen_proto();

    gfx_text(c, COL_X, COL_Y, "PROTOCOL", UI_FONT_LABEL, UI_TEXT_FAINT, 1);

    gfx_rect_t r = dd_rect();
    const bool down = (s.hit_kind == HIT_DD);
    gfx_fill_chamfer_rect_ex(c, r.x, r.y, r.w, r.h, 10, 0, 10, 0,
                             down ? UI_PANEL_HI : UI_PANEL);
    gfx_draw_chamfer_rect_ex(c, r.x, r.y, r.w, r.h, 10, 0, 10, 0,
                             s.open ? UI_ACCENT : UI_EDGE);
    gfx_text(c, r.x + 14, r.y + 13, p->name, UI_FONT_HEAD, UI_TEXT, 1);
    /* The chevron says this opens rather than steps, which is the whole
     * difference between this and a settings row. */
    const int cx = r.x + r.w - 22, cy = r.y + r.h / 2;
    for (int i = 0; i < 7; ++i) {
        gfx_fill_rect(c, cx - 6 + i, cy - 3 + (i < 4 ? i : 6 - i), 1, 2,
                      UI_TEXT_DIM);
    }

    char buf[40];
    const uint8_t n = outbind_chosen(&s.bind);
    const uint8_t cap = (p->max_pins < OUT_MAX_SLOTS) ? p->max_pins
                                                      : (uint8_t)OUT_MAX_SLOTS;
    snprintf(buf, sizeof(buf), "%u OF %u PINS", (unsigned)n, (unsigned)cap);
    gfx_text(c, COL_X, DD_Y + DD_H + 16, buf, UI_FONT_LABEL, UI_TEXT_DIM, 1);

    if (p->channels > 1u) {
        snprintf(buf, sizeof(buf), "%u CHANNELS ON ONE PIN",
                 (unsigned)p->channels);
        gfx_text(c, COL_X, DD_Y + DD_H + 38, buf, UI_FONT_LABEL,
                 UI_TEXT_FAINT, 1);
    }

    gfx_text(c, COL_X, SCREEN_H - 92, "LAST WRITE", UI_FONT_LABEL,
             UI_TEXT_FAINT, 1);
    gfx_text(c, COL_X, SCREEN_H - 70, result_text(), UI_FONT_HEAD,
             result_color(), 1);
}

static void draw_cell(gfx_canvas_t *c, int i)
{
    const outbind_pin_t *pin = &outbind_pins()[i];
    const gfx_rect_t r = cell_rect(i);
    const bool on = (s.bind.pins & ((uint32_t)1u << i)) != 0u;
    const bool down = (s.hit_kind == HIT_CELL && s.hit_index == i);
    const bool can = outbind_can_add(&s.bind, (uint8_t)i);

    gfx_fill_chamfer_rect_ex(c, r.x, r.y, r.w, r.h, 8, 0, 8, 0,
                             down ? UI_PANEL_HI : UI_PANEL);
    gfx_draw_chamfer_rect_ex(c, r.x, r.y, r.w, r.h, 8, 0, 8, 0,
                             on ? UI_ACCENT : UI_EDGE);

    /* The box: filled when chosen, hollow when free, and struck through when
     * the pin is spoken for. */
    const int bx = r.x + 9, by = r.y + (CELL_H - BOX) / 2;
    gfx_color_t edge = pin->reserved ? UI_DANGER
                                     : ((on || can) ? UI_ACCENT : UI_EDGE_HI);
    gfx_draw_chamfer_rect_ex(c, bx, by, BOX, BOX, 4, 0, 4, 0, edge);
    if (on) {
        gfx_fill_chamfer_rect_ex(c, bx + 4, by + 4, BOX - 8, BOX - 8,
                                 3, 0, 3, 0, UI_ACCENT);
    } else if (pin->reserved) {
        for (int k = 4; k < BOX - 4; ++k) {
            gfx_fill_rect(c, bx + k, by + k, 2, 2, UI_DANGER);
        }
    }

    char name[8];
    snprintf(name, sizeof(name), "GP%u", (unsigned)pin->gpio);
    gfx_color_t ink = pin->reserved ? UI_TEXT_FAINT
                                    : ((on || can) ? UI_TEXT : UI_TEXT_DIM);
    const int tx = bx + BOX + 8;
    gfx_text(c, tx, r.y + 5, name, UI_FONT_HEAD, ink, 1);

    /* Under the name: what holds the pin, or the pad number printed on the
     * board, so an operator counting pads and one reading GPIOs both find it. */
    if (pin->reserved) {
        gfx_text(c, tx, r.y + 33, pin->held_by, UI_FONT_LABEL, UI_DANGER, 1);
    } else {
        char pad[10];
        snprintf(pad, sizeof(pad), "PAD %u", (unsigned)pin->pad);
        gfx_text(c, tx, r.y + 33, pad, UI_FONT_LABEL, UI_TEXT_FAINT, 1);
    }
}

static void draw_popup(gfx_canvas_t *c)
{
    const gfx_rect_t r = pop_rect();
    /* A shadow rather than a border, so the list reads as being over the
     * pins rather than beside them. */
    gfx_fill_chamfer_rect_ex(c, r.x + 4, r.y + 4, r.w, r.h, 10, 0, 10, 0,
                             UI_PANEL_SUNK);
    gfx_fill_chamfer_rect_ex(c, r.x, r.y, r.w, r.h, 10, 0, 10, 0, UI_PANEL);
    gfx_draw_chamfer_rect_ex(c, r.x, r.y, r.w, r.h, 10, 0, 10, 0, UI_ACCENT);

    const outbind_proto_t *p = outbind_protos();
    for (int i = 0; i < (int)OUTBIND_PROTOS; ++i) {
        const gfx_rect_t rr = pop_row_rect(i);
        const bool sel = (&p[i] == chosen_proto());
        const bool down = (s.hit_kind == HIT_POP && s.hit_index == i);
        if (sel || down) {
            gfx_fill_chamfer_rect_ex(c, rr.x, rr.y, rr.w, rr.h, 6, 0, 6, 0,
                                     sel ? UI_ACCENT : UI_PANEL_HI);
        }
        gfx_text(c, rr.x + 12, rr.y + 9, p[i].name, UI_FONT_HEAD,
                 sel ? UI_TEXT_ON_LIGHT : UI_TEXT, 1);
    }
}

static void render(gfx_canvas_t *c, int buffer_index)
{
    bool stale = true;
    if (buffer_index >= 0 && buffer_index < MAX_FBS) {
        stale = !s.chrome_valid[buffer_index] || s.drawn_gen != s.gen;
    }
    if (!stale) {
        return;
    }

    gfx_fill_rect(c, 0, 0, c->width, c->height, UI_BG);
    draw_left(c);
    for (int i = 0; i < (int)OUTBIND_PINS; ++i) {
        draw_cell(c, i);
    }
    if (s.open) {
        draw_popup(c);
    }

    if (buffer_index >= 0 && buffer_index < MAX_FBS) {
        s.chrome_valid[buffer_index] = true;
        s.drawn_gen = s.gen;
        /* One framebuffer is now current and the others are not; the next
         * pass into each of them repaints. */
        for (int i = 0; i < MAX_FBS; ++i) {
            if (i != buffer_index) {
                s.chrome_valid[i] = false;
            }
        }
    }
}

static const ui_screen_t s_screen = {
    .title  = "OUTPUTS",
    .reset  = reset,
    .enter  = enter,
    .leave  = leave,
    .tick   = NULL,
    .event  = event,
    .render = render,
};

const ui_screen_t *outputs_screen(void) { return &s_screen; }
