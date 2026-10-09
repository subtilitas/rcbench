/*
 * Protocol on the left, pins on the right.  See outputs_screen.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "outputs_screen.h"

#include <stdio.h>
#include <string.h>

#include "link_pages.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

#define SCREEN_H (480 - UI_BAND_H)
#define MAX_FBS  3

/*
 * Left column: the protocol and what the choice adds up to.
 *
 * 266 px is the longest protocol name in the head face, 14 cells of 16 px
 * ("DSHOT600 BIDIR", 224 px), with 10 px before it and the chevron's 28 px
 * column and a 4 px gap after it.
 */
#define COL_X    16
#define COL_W    266
#define COL_Y    16

#define DD_Y     44
#define DD_H     54
#define DD_TEXT_X 10

/* Shown only while the pages read describe no binding: held for UI_HOLD_S,
 * it writes a binding with no pin. */
#define CLR_Y    (DD_Y + DD_H + 92)
#define CLR_H    48

/*
 * The pins: four columns of seven, filled down each column so a column is a
 * run of consecutive GPIOs.  Twenty-six pins leave the last two cells empty,
 * which is better than a grid that has to be read across.
 *
 * Seven rows of 53 plus their gaps is 407 of the 432 the router hands over.
 * Nine rows did not fit, and the cell has to be tall enough for the name and
 * what holds the pin underneath it.
 *
 * A cell is 121 px wide.  The line under the name starts 5 px in and is at
 * most the longest protocol name in the label face, 14 cells of 8 px
 * (112 px), so it ends 4 px inside the cell.
 */
#define GRID_X   288
#define GRID_W   496
#define GRID_Y   16
#define GRID_COLS 4
#define GRID_ROWS 7
#define CELL_GAP 4
#define CELL_W   ((GRID_W - (GRID_COLS - 1) * CELL_GAP) / GRID_COLS)
#define CELL_H   53
#define BOX      20
#define CELL_LABEL_X 5

/* The dropdown, when it is open, covers the pins. */
#define POP_X    COL_X
#define POP_W    300
#define POP_Y    DD_Y
#define POP_ROW  46

enum { HIT_NONE = 0, HIT_DD, HIT_POP, HIT_CELL, HIT_CLEAR };

static struct {
    outbind_t bind;
    /* The last binding a read confirmed: what the screen goes back to when
     * a read fails after an edit. */
    outbind_t confirmed;
    bind_read_t read;       /* what the last reading was */
    /* OFF was picked in the list, and is kept across readings until another
     * entry is picked or the screen is left. */
    bool      off_picked;
    bool      open;
    int       hit_kind;
    int       hit_index;
    ui_hold_t clear;        /* the hold on UNBIND ALL PINS */
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

/* The hold on UNBIND ALL PINS, and the press that began it, are over. */
static void drop_clear_hold(void)
{
    ui_hold_reset(&s.clear);
    if (s.hit_kind == HIT_CLEAR) {
        s.hit_kind = HIT_NONE;
    }
}

void outputs_screen_set_binding(const outbind_t *b)
{
    if (b == NULL) {
        return;
    }
    /*
     * Which protocol is selected is this screen's, and the wire does not
     * carry it.
     *
     * A page carries pins, and outbind_from_slots() names a protocol from
     * them -- the lowest one holding a pin, or OFF when none does.  A
     * protocol with no pins yet is not on that page at all, and OFF never
     * is.  A reading that landed whole would move the selection off a
     * protocol whose first pin is about to be ticked: with nothing bound to
     * OFF, which takes no pins, and with one protocol bound to that one.
     *
     * So a protocol equal to what the pins already say is not a claim about
     * which one is selected, and the screen keeps its own: any protocol
     * other than OFF, and OFF when the operator picked it in the list.  Any
     * other protocol in @p b is somebody naming one -- how a caller poses
     * the screen -- and it lands.
     */
    const uint16_t had_board = s.bind.board;
    const uint8_t  chosen    = s.bind.proto;
    /*
     * Except across a change of board, where nothing carries over.  A pin
     * index means a different pin in another catalogue, which is why
     * outbind_set_board() clears the selection, and a protocol chosen for the
     * hardware that was there is no better than the pins were.
     */
    const bool keep = (chosen != 0u || s.off_picked)
                   && (b->board == had_board)
                   && (b->proto == outbind_wire_proto(b));

    s.bind = *b;
    /* Read off the wire or restored, so it is trimmed before it is drawn
     * rather than trusted to mean something on this board. */
    outbind_trim(&s.bind);
    if (keep) {
        outbind_set_proto(&s.bind, chosen);
    } else {
        s.off_picked = false;
    }
    s.confirmed = s.bind;
    s.read      = BIND_READ_OK;
    drop_clear_hold();
    touched();
}

void outputs_screen_set_reading(const bind_reading_t *r)
{
    if (r == NULL) {
        return;
    }
    if (r->state == BIND_READ_OK) {
        outputs_screen_set_binding(&r->bind);
        return;
    }
    if (r->bind.board != s.bind.board || r->state == BIND_READ_NO_BOARD) {
        /* Another board, or one with no pin map: nothing read earlier is a
         * binding of it. */
        s.bind = r->bind;
        outbind_trim(&s.bind);
        s.confirmed  = s.bind;
        s.off_picked = false;
    } else {
        /*
         * The pins go back to the last binding a read confirmed.  An edit
         * made since is a write whose outcome is not known, and drawing it
         * would show a binding nobody has read.  The selection stays.
         */
        const uint8_t chosen = s.bind.proto;
        s.bind = s.confirmed;
        outbind_set_proto(&s.bind, chosen);
    }
    s.read = r->state;
    /* A press begun while the binding could be edited does not act on its
     * release, and a hold on UNBIND ALL PINS ends where its state does. */
    if (s.hit_kind == HIT_CELL) {
        s.hit_kind = HIT_NONE;
    }
    if (s.read != BIND_READ_ODD) {
        drop_clear_hold();
    }
    touched();
}

bind_read_t outputs_screen_read_state(void) { return s.read; }

bool outputs_screen_editable(void) { return s.read == BIND_READ_OK; }

void outputs_screen_set_apply(outputs_apply_fn fn) { s.apply = fn; }

void outputs_screen_set_result(outputs_result_t r)
{
    if (s.result != r) {
        s.result = r;
        touched();
    }
}

/* Every change of the binding goes out at once.  There is no APPLY key: a
 * screen with an unapplied choice on it is a screen that disagrees with the
 * bench, and the operator has no way to see which of the two is driving.
 * Which protocol is selected is not part of the binding and calls nothing. */
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

static gfx_rect_t clear_rect(void)
{
    return gfx_rect_make(COL_X, CLR_Y, COL_W, CLR_H);
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
    /* Once, not per pin: the count is a board-table lookup and this runs on
     * every touch event. */
    const int n = (int)outbind_pin_count(s.bind.board);
    for (int i = 0; i < n; ++i) {
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
    s.confirmed = s.bind;
    s.read = BIND_READ_OK;
    ui_hold_reset(&s.clear);
    touched();
}

static void enter(void)
{
    s.open = false;
    s.hit_kind = HIT_NONE;
    ui_hold_reset(&s.clear);
    touched();
}

static void leave(void)
{
    s.open = false;
    drop_clear_hold();
    /*
     * A picked OFF ends with the visit.  The pin picker joins its taps to
     * the protocol selected here, and left on OFF it could add no pin; the
     * next entry opens on the lowest-numbered protocol that holds one.
     */
    if (s.off_picked) {
        s.off_picked = false;
        outbind_set_proto(&s.bind, outbind_wire_proto(&s.bind));
        touched();
    }
}

/* UNBIND ALL PINS, held for UI_HOLD_S: the one write the screen asks for
 * while the pages read describe no binding, because it is the way out of
 * that state.  It binds nothing, so it cannot drive a pin. */
static void unbind_all(void)
{
    const uint8_t chosen = s.bind.proto;
    const uint16_t board = s.bind.board;
    outbind_init(&s.bind);
    outbind_set_board(&s.bind, board);
    outbind_set_proto(&s.bind, chosen);
    changed();
}

static void tick(float dt_s)
{
    if (s.hit_kind != HIT_CLEAR) {
        return;
    }
    if (s.read != BIND_READ_ODD) {
        drop_clear_hold();
        touched();
        return;
    }
    if (ui_hold_tick(&s.clear, dt_s)) {
        unbind_all();
    }
    touched();      /* the fill follows the hold */
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
        if (s.read == BIND_READ_ODD && inside(clear_rect(), x, y)) {
            s.hit_kind = HIT_CLEAR; ui_hold_begin(&s.clear); touched();
            return;
        }
        /* A binding that is not confirmed takes no press on a pin: the
         * reason is under the protocol. */
        const int c = outputs_screen_editable() ? cell_at(x, y) : -1;
        if (c >= 0) {
            s.hit_kind = HIT_CELL; s.hit_index = c; touched();
        }
        return;
    }

    if (evt->type == TOUCH_EVENT_MOVE) {
        /* A finger that slides off the key abandons the hold, as on every
         * held control (ui_hold_leave()). */
        if (s.hit_kind == HIT_CLEAR && !inside(clear_rect(), x, y)
            && ui_hold_leave(&s.clear)) {
            s.hit_kind = HIT_NONE;
            touched();
        }
        return;
    }

    if (evt->type != TOUCH_EVENT_UP) {
        return;
    }
    const int kind = s.hit_kind, index = s.hit_index;
    s.hit_kind = HIT_NONE;

    /* A release the finger did not make ends the press and picks
     * nothing. */
    const bool tap = touch_event_is_tap_up(evt);

    if (tap && kind == HIT_DD && inside(dd_rect(), x, y)) {
        s.open = true; touched();
    } else if (tap && kind == HIT_POP
               && inside(pop_row_rect(index), x, y)) {
        /* A pick says which set is shown and which the next tick joins.  It
         * changes no page, so nothing is written. */
        s.open = false;
        outbind_set_proto(&s.bind, (uint8_t)index);
        s.off_picked = (index == 0);
        touched();
    } else if (tap && kind == HIT_CELL
               && inside(cell_rect(index), x, y)) {
        /* A refused tick is not silent: the cell flashes nothing, but the
         * count under the protocol does not move and the reason is printed
         * in the cell or under the protocol. */
        if (outputs_screen_editable()
            && outbind_toggle(&s.bind, (uint8_t)index)) {
            changed();
        }
    } else if (kind == HIT_CLEAR) {
        /* The hold acts when it completes, not on the release. */
        (void)ui_hold_end(&s.clear);
        touched();
    } else {
        touched();
    }
}

/* ------------------------------------------------------------------ render */

static const char *result_text(void)
{
    switch (s.result) {
    case OUTPUTS_OK:      return TR(OUT_WRITTEN);
    case OUTPUTS_NO_LINK: return TR(OUT_NO_LINK);
    case OUTPUTS_REFUSED: return TR(OUT_REFUSED);
    case OUTPUTS_IDLE:
    default:              return TR(OUT_NOT_WRITTEN);
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

/*
 * Why no pin can be ticked, when none can.
 *
 * The rules are in out_bind and they are right, but a board drawn entirely
 * in grey with no reason beside it is a screen that looks broken: PPM
 * carries eight channels on one pin, so a single servo pin already bound
 * leaves seven free and greys the whole board -- correct, and
 * indistinguishable from a fault without this line.
 *
 * A binding that is not confirmed comes first, because then no rule of the
 * protocol is what stops the tap.  OFF says what it is for.  Otherwise only
 * when the protocol takes pins and none can be added, so the line appears
 * when it explains something and not otherwise.
 */
bool outputs_screen_reason(char *buf, size_t cap)
{
    if (buf == NULL || cap == 0u) {
        return false;
    }
    buf[0] = '\0';
    if (s.read == BIND_READ_NONE) {
        snprintf(buf, cap, "%s", TR(OUT_NOT_READ));
        return true;
    }
    if (s.read == BIND_READ_ODD) {
        snprintf(buf, cap, "%s", TR(OUT_NOT_MAPPED));
        return true;
    }
    const outbind_proto_t *p = chosen_proto();
    if (p->max_pins == 0u) {
        snprintf(buf, cap, "%s", TR(OUT_OFF_VIEW));
        return true;
    }
    const uint8_t n = outbind_chosen(&s.bind);
    const uint8_t lim = (p->max_pins < OUT_MAX_SLOTS)
                            ? p->max_pins : (uint8_t)OUT_MAX_SLOTS;
    const uint8_t used_ch = outbind_channels_used(&s.bind);
    const uint8_t used_sl = outbind_chosen_total(&s.bind);
    if (n >= lim) {
        snprintf(buf, cap,
                 (lim == 1u) ? TR(OUT_TAKES_PIN) : TR(OUT_TAKES_PINS),
                 p->name, (unsigned)lim);
        return true;
    }
    if (used_sl >= (uint8_t)LINK_OUT_SLOTS) {
        snprintf(buf, cap, TR(OUT_ALL_SLOTS), (unsigned)LINK_OUT_SLOTS);
        return true;
    }
    if ((unsigned)used_ch + p->channels > (unsigned)LINK_OUT_CHANNELS) {
        snprintf(buf, cap,
                 (p->channels == 1u) ? TR(OUT_NEEDS_CHANNEL)
                                     : TR(OUT_NEEDS_CHANNELS),
                 (unsigned)p->channels,
                 (unsigned)(LINK_OUT_CHANNELS - used_ch));
        return true;
    }
    return false;
}

outputs_cell_t outputs_screen_cell(uint8_t index, char *label, size_t cap)
{
    if (label != NULL && cap != 0u) {
        label[0] = '\0';
    }
    if (index >= outbind_pin_count(s.bind.board)) {
        return OUTPUTS_CELL_NONE;
    }
    const outbind_pin_t *pin = &outbind_pins(s.bind.board)[index];
    const uint8_t proto = (s.bind.proto < OUTBIND_PROTOS) ? s.bind.proto : 0u;
    const uint8_t held = outbind_group_of(&s.bind, index);
    /* What holds the pin, or the pad number printed on the board, so an
     * operator counting pads and one reading GPIOs both find it.  A pin
     * ticked in the selected protocol shows its pad: the protocol is the
     * one named in the list. */
    if (pin->reserved) {
        if (label != NULL) {
            snprintf(label, cap, "%s", pin->held_by);
        }
        return OUTPUTS_CELL_RESERVED;
    }
    if (held != 0u && held != proto) {
        if (label != NULL) {
            snprintf(label, cap, "%s", outbind_protos()[held].name);
        }
        return OUTPUTS_CELL_HELD;
    }
    if (label != NULL) {
        snprintf(label, cap, "PAD %u", (unsigned)pin->pad);
    }
    return (held != 0u) ? OUTPUTS_CELL_TICKED : OUTPUTS_CELL_FREE;
}

static void draw_left(gfx_canvas_t *c)
{
    const outbind_proto_t *p = chosen_proto();

    gfx_text(c, COL_X, COL_Y, TR(OUT_PROTOCOL), UI_FONT_LABEL, UI_TEXT_FAINT,
             1);

    gfx_rect_t r = dd_rect();
    const bool down = (s.hit_kind == HIT_DD);
    gfx_fill_chamfer_rect_ex(c, r.x, r.y, r.w, r.h, 10, 0, 10, 0,
                             down ? UI_PANEL_HI : UI_PANEL);
    gfx_draw_chamfer_rect_ex(c, r.x, r.y, r.w, r.h, 10, 0, 10, 0,
                             s.open ? UI_ACCENT : UI_EDGE);
    gfx_text(c, r.x + DD_TEXT_X, r.y + 13, p->name, UI_FONT_HEAD, UI_TEXT,
             1);
    /* The chevron says this opens rather than steps, which is the whole
     * difference between this and a settings row. */
    const int cx = r.x + r.w - 22, cy = r.y + r.h / 2;
    for (int i = 0; i < 7; ++i) {
        gfx_fill_rect(c, cx - 6 + i, cy - 3 + (i < 4 ? i : 6 - i), 1, 2,
                      UI_TEXT_DIM);
    }

    char buf[64];
    const uint8_t n = outbind_chosen(&s.bind);
    const uint8_t cap = (p->max_pins < OUT_MAX_SLOTS) ? p->max_pins
                                                      : (uint8_t)OUT_MAX_SLOTS;
    snprintf(buf, sizeof(buf), TR(OUT_PINS_OF), (unsigned)n, (unsigned)cap);
    gfx_text(c, COL_X, DD_Y + DD_H + 16, buf, UI_FONT_LABEL, UI_TEXT_DIM, 1);

    if (p->channels > 1u) {
        snprintf(buf, sizeof(buf), TR(OUT_CHANNELS_ON_PIN),
                 (unsigned)p->channels);
        gfx_text(c, COL_X, DD_Y + DD_H + 38, buf, UI_FONT_LABEL,
                 UI_TEXT_FAINT, 1);
    }

    if (outputs_screen_reason(buf, sizeof(buf))) {
        gfx_text(c, COL_X, DD_Y + DD_H + 60, buf, UI_FONT_LABEL, UI_WARN, 1);
    }

    if (s.read == BIND_READ_ODD) {
        /* Danger red as it is held, as the other held controls fade to the
         * colour of what they are about to do. */
        const gfx_rect_t k = clear_rect();
        const gfx_color_t fill = ui_hold_fill(UI_PANEL, UI_DANGER,
                                              s.clear.held_s);
        gfx_fill_chamfer_rect_ex(c, k.x, k.y, k.w, k.h, 10, 0, 10, 0, fill);
        gfx_draw_chamfer_rect_ex(c, k.x, k.y, k.w, k.h, 10, 0, 10, 0,
                                 UI_DANGER);
        const char *label = TR(OUT_HOLD_UNBIND);
        const int lw = gfx_text_width(UI_FONT_LABEL, label, 1);
        gfx_text(c, k.x + (k.w - lw) / 2, k.y + (k.h - 16) / 2, label,
                 UI_FONT_LABEL, UI_TEXT, 1);
    }

    gfx_text(c, COL_X, SCREEN_H - 92, TR(OUT_LAST_WRITE), UI_FONT_LABEL,
             UI_TEXT_FAINT, 1);
    gfx_text(c, COL_X, SCREEN_H - 70, result_text(), UI_FONT_HEAD,
             result_color(), 1);
}

static void draw_cell(gfx_canvas_t *c, int i)
{
    const outbind_pin_t *pin = &outbind_pins(s.bind.board)[i];
    const gfx_rect_t r = cell_rect(i);
    const uint8_t proto = (s.bind.proto < OUTBIND_PROTOS) ? s.bind.proto : 0u;
    const uint8_t held = outbind_group_of(&s.bind, (uint8_t)i);
    const bool on = (held != 0u && held == proto);
    /* Held by a protocol that is not the one being edited.  That is a choice
     * somebody made and can undo from the other protocol, so it is drawn
     * grey; the coprocessor's reserved pins stay red. */
    const bool elsewhere = (held != 0u && held != proto);
    const bool down = (s.hit_kind == HIT_CELL && s.hit_index == i);
    /*
     * A binding no read has confirmed is drawn the way a pin that cannot be
     * ticked is: no accent, dim ink, and its ticks filled like the pins of
     * another protocol.  It is shown, and not offered.
     */
    const bool live = outputs_screen_editable();
    const bool can = live && outbind_can_add(&s.bind, (uint8_t)i);
    const bool lit = live && on;

    gfx_fill_chamfer_rect_ex(c, r.x, r.y, r.w, r.h, 8, 0, 8, 0,
                             down ? UI_PANEL_HI : UI_PANEL);
    gfx_draw_chamfer_rect_ex(c, r.x, r.y, r.w, r.h, 8, 0, 8, 0,
                             lit ? UI_ACCENT : UI_EDGE);

    /* The box: filled when chosen, hollow when free, and struck through when
     * the pin is spoken for.  It sits beside the name, above the line that
     * says what holds the pin. */
    const int bx = r.x + 7, by = r.y + 7;
    gfx_color_t edge = pin->reserved ? UI_DANGER
                                     : ((lit || can) ? UI_ACCENT : UI_EDGE_HI);
    gfx_draw_chamfer_rect_ex(c, bx, by, BOX, BOX, 4, 0, 4, 0, edge);
    if (lit) {
        gfx_fill_chamfer_rect_ex(c, bx + 4, by + 4, BOX - 8, BOX - 8,
                                 3, 0, 3, 0, UI_ACCENT);
    } else if (elsewhere || on) {
        /* Filled, so it reads as taken rather than free, and dim, so it does
         * not read as this protocol's. */
        gfx_fill_chamfer_rect_ex(c, bx + 4, by + 4, BOX - 8, BOX - 8,
                                 3, 0, 3, 0, UI_EDGE_HI);
    } else if (pin->reserved) {
        for (int k = 4; k < BOX - 4; ++k) {
            gfx_fill_rect(c, bx + k, by + k, 2, 2, UI_DANGER);
        }
    }

    char name[8];
    snprintf(name, sizeof(name), "GP%u", (unsigned)pin->gpio);
    gfx_color_t ink = pin->reserved ? UI_TEXT_FAINT
                                    : ((lit || can) ? UI_TEXT : UI_TEXT_DIM);
    gfx_text(c, bx + BOX + 8, r.y + 3, name, UI_FONT_HEAD, ink, 1);

    /* Under the box and the name, from the cell's left edge: the longest
     * protocol name is 112 px of the cell's 121. */
    char label[24];
    gfx_color_t lc = UI_TEXT_FAINT;
    switch (outputs_screen_cell((uint8_t)i, label, sizeof(label))) {
    case OUTPUTS_CELL_RESERVED: lc = UI_DANGER;   break;
    case OUTPUTS_CELL_HELD:     lc = UI_TEXT_DIM; break;
    default:                                      break;
    }
    gfx_text(c, r.x + CELL_LABEL_X, r.y + 33, label, UI_FONT_LABEL, lc, 1);
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
    const int cells = (int)outbind_pin_count(s.bind.board);
    for (int i = 0; i < cells; ++i) {
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

/*
 * Touch events were lost between two frames, so this screen's record of what
 * is on the glass cannot be trusted.  A hit held open acts on its release,
 * and the GT911 reuses track ids: a later contact that began somewhere else
 * would be taken for this one's release and apply a change nobody asked for.
 */
static void cancel(void)
{
    s.hit_kind  = HIT_NONE;
    s.hit_index = -1;
    /* And a hold completes on a timer: one whose release went missing would
     * finish with nothing on the glass. */
    ui_hold_reset(&s.clear);
    outputs_screen_invalidate();
}

static const ui_screen_t s_screen = {
    .title  = "OUTPUTS",
    .reset  = reset,
    .enter  = enter,
    .leave  = leave,
    .tick   = tick,
    .event  = event,
    .cancel = cancel,
    .render = render,
};

const ui_screen_t *outputs_screen(void) { return &s_screen; }
