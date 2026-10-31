/**
 * @file
 * @brief Protocol on the left, pins on the right.  See outputs_screen.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "outputs_screen.h"

#include "outputs_screen_int.h"

#include <stdio.h>
#include <string.h>

#include "link_pages.h"
#include "ui_pinlist.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

/** @brief The board view is drawn, not this grid (outputs_view.c). */
static bool s_board_view;

/** @brief The screen's width: the panel's 1024px. */
#define SCREEN_W UI_PANEL_W
#define SCREEN_H UI_BODY_H    /**< The body under the band: 540px. */
#define MAX_FBS  3     /**< Framebuffers the chrome is cached for. */

/** @brief The count under the protocol list ("4 OF 8 PINS"), and under it
 *  the channels a pin carries, for a protocol of more than one. */
#define COUNT_Y  (DD_Y + DD_H + 8)
#define CHAN_Y   (COUNT_Y + 24) /**< The channels line. */

/** @brief Why no pin can be ticked: two lines OU_LINE apart under the
 *  counts. */
#define REASON_Y (CHAN_Y + 26)

/**
 * @brief UNBIND ALL PINS, shown only while the pages read describe no
 *        binding: held for UI_HOLD_S, it writes a binding with no pin.
 *        Below the reason's second line, 300 x 56px; its label on two
 *        lines where it needs them.
 */
#define CLR_Y    (REASON_Y + 2 * OU_LINE + 8)
#define CLR_H    UI_BTN_H_TALL /**< Its height, px (8.4mm). */

/** @brief LAST WRITE, and under it the result in OU_FONT_HEAD on one line,
 *  or two RESULT_PITCH apart where it is wider than the column; the last
 *  ends at OU_FOOT.  LAST_Y is the label's top over two lines, the
 *  highest it goes. */
#define RESULT_PITCH 40       /**< The result's line pitch, px. */
/** @brief The result's first of two lines. */
#define RESULT_Y (OU_FOOT - RESULT_PITCH - 36)
#define LAST_ABOVE 26  /**< LAST WRITE's top over the result's. */
#define LAST_Y   (RESULT_Y - LAST_ABOVE) /**< LAST WRITE over two lines. */

_Static_assert(CLR_Y + CLR_H + 8 <= LAST_Y,
               "UNBIND ALL PINS ends above LAST WRITE");

/*
 * The pins: four columns of seven, filled down each column so a column is a
 * run of consecutive GPIOs.  Twenty-six pins leave the last two cells empty,
 * which is better than a grid that has to be read across.
 *
 * A cell is 167 x 64px (25.1 x 9.6mm): seven rows 6px apart from y 8
 * end at body y 492, 4px above the alert band; four columns 8px apart
 * from x 324 end 8px from the screen's right edge.  The tick box is
 * 24px, the pin name beside it in OU_FONT_PIN ("GPIO 28", 112px, ends
 * 154px in), and the line under them in OU_FONT_FINE from 8px in holds
 * 18 cells (144px) inside the cell: "PWM CH OF GPIO 0", "KANAL VON
 * GPIO 13" and "DSHOT600 BIDIR" whole.
 */
#define GRID_X   (COL_X + COL_W + 8) /**< The grid's left edge. */
/** @brief The pin grid's width, to 8px from the right edge: 692px. */
#define GRID_W   (SCREEN_W - 8 - GRID_X)
#define GRID_Y   8            /**< Its top. */
#define GRID_COLS 4           /**< Columns of cells. */
#define GRID_ROWS 7           /**< Cells down a column. */
#define CELL_GAP 8     /**< Between columns, px. */
#define ROW_GAP  6     /**< Between rows, px. */
/** @brief A cell's width: 167px. */
#define CELL_W   ((GRID_W - (GRID_COLS - 1) * CELL_GAP) / GRID_COLS)
#define CELL_H   64           /**< Its height, px (9.6mm). */
#define BOX      24           /**< The tick box, px. */
#define CELL_PAD 8     /**< The tick box and the sub-line from the edge. */
#define CELL_LABEL_Y 44 /**< The sub-line's top in the cell. */
#define CELL_CELLS 18  /**< The sub-line's cells of OU_FONT_FINE. */

_Static_assert(CELL_PAD + BOX + 10 + 7 * 16 + 4 <= CELL_W,
               "a pin name (GPIO 28) ends 4px inside its cell");

_Static_assert(GRID_Y + GRID_ROWS * CELL_H + (GRID_ROWS - 1) * ROW_GAP
                   <= OU_FOOT,
               "the grid's last row ends above the alert band");
_Static_assert(CELL_H >= UI_TAP_MIN && CELL_W >= UI_TAP_MIN,
               "a cell is a finger wide and tall");
_Static_assert(CELL_PAD + CELL_CELLS * 8 < CELL_W - 8,
               "the sub-line ends inside the cell, left of its chamfer");

/*
 * The dropdown, when it is open, covers the pins: the protocols in a
 * column of 48px rows (UI_TAP_MIN) from the face's top, and the PROG PORT
 * entries in a second column of the same rows beside it, the list
 * 600 x 392px with them.  With its 4px margins and 4px shadow the list
 * ends at body y 440, above the alert band.
 */
#define POP_X    COL_X        /**< The open list's left edge. */
#define POP_W    COL_W        /**< A column's width, px. */
#define POP_Y    DD_Y         /**< Its top: the face's. */
#define POP_ROW  UI_TAP_MIN   /**< A row, px. */

_Static_assert(POP_Y + (int)OUTBIND_PROTOS * POP_ROW + 8 + 4 <= OU_FOOT,
               "the protocol list ends above the alert band");
_Static_assert(LINK_PG_PORTS_MAX <= OUTBIND_PROTOS,
               "the PROG PORT column is no taller than the protocols'");

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
    /* The pin whose tap a PWM clash refused last, or -1: the line under the
     * protocol says why, until another pick or tick. */
    int       refused;
    outputs_result_t result;

    outputs_apply_fn apply;

    /* The pins SENSORS and PD MINI held at the last frame. */
    uint64_t  iface[OUTPUTS_IFACE_COUNT];

    bool      chrome_valid[MAX_FBS];
    uint32_t  drawn_gen;
    uint32_t  gen;          /* bumped by anything that changes the picture */
} s;

static void touched(void)
{
    ++s.gen;
}

static const outbind_proto_t *chosen_proto(void);

/* Whether a programming port has pin @p index: it is a port's and no
 * protocol holds it.  A pin a stored slot holds is that slot's -- the
 * IOmcu gives the port no pin then -- and can be unticked. */
static bool prog_taken(uint8_t index)
{
    if (index >= outbind_pin_count(s.bind.board)
        || outbind_group_of(&s.bind, index) != 0u) {
        return false;
    }
    return outputs_port_holds(outbind_pins(s.bind.board)[index].gpio);
}

/* What SENSORS or PD MINI holds pin @p index for, or OUTPUTS_IFACE_NONE:
 * no protocol and no port holds it, and the IOmcu does not reserve it.  A
 * pin a protocol holds is that protocol's, as against a port: the IOmcu
 * refused the interface its pin then, and SETUP says so. */
static outputs_iface_t iface_taken(uint8_t index)
{
    if (index >= outbind_pin_count(s.bind.board)
        || outbind_pins(s.bind.board)[index].reserved
        || outbind_group_of(&s.bind, index) != 0u || prog_taken(index)) {
        return OUTPUTS_IFACE_NONE;
    }
    return outputs_iface_at(outbind_pins(s.bind.board)[index].gpio);
}

/* The pin of the PROG PORT entry shown, or OUTPUTS_NO_PIN. */
static uint8_t shown_port_pin(void)
{
    const int port = outputs_screen_port_shown();
    return (port >= 0) ? outputs_screen_port_pin((uint8_t)port)
                       : OUTPUTS_NO_PIN;
}

/* Whether the IOmcu would refuse pin @p index to the protocol being
 * edited: the programming port's slice, or a bound PWM pin's compare
 * register.  The port's own pin is prog_taken(), not this. */
static bool clashes(uint8_t index)
{
    return outbind_pwm_clash_ports(&s.bind, index, outputs_port_pins(),
                                   LINK_PG_PORTS_MAX, NULL)
           != OUTBIND_CLASH_NONE;
}

void outputs_screen_invalidate(void)
{
    memset(s.chrome_valid, 0, sizeof(s.chrome_valid));
}

const outbind_t *outputs_screen_binding(void) { return &s.bind; }

uint32_t outputs_screen_revision(void)
{
    return s.gen + outputs_port_revision();
}

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
     * Except across a change of board, where a protocol chosen for the
     * hardware that was there carries over no better than the pins do: a
     * pin index means a different pin in another catalogue, which is why
     * outbind_set_board() clears the selection.  The selection goes to OFF,
     * picked, whatever it was: a board that answers during a visit -- the
     * first link-up, a swapped IOmcu -- must not hand the next tap a
     * protocol nobody picked for it, the wire's or the one before.
     */
    const bool named = (b->proto != outbind_wire_proto(b));
    const bool keep = (chosen != 0u || s.off_picked)
                   && (b->board == had_board) && !named;
    const bool to_off = (b->board != had_board) && !named;

    s.bind = *b;
    /* Read off the wire or restored, so it is trimmed before it is drawn
     * rather than trusted to mean something on this board. */
    outbind_trim(&s.bind);
    if (keep) {
        outbind_set_proto(&s.bind, chosen);
    } else if (to_off) {
        outbind_set_proto(&s.bind, 0u);
        s.off_picked = true;
    } else {
        s.off_picked = false;
    }
    if (s.bind.board != had_board) {
        s.refused = -1;         /* an index into the catalogue before */
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
         * binding of it.  The reading names no protocol, and the selection
         * goes to OFF, picked, as in set_binding(), so the next binding
         * read keeps it. */
        s.bind = r->bind;
        outbind_trim(&s.bind);
        outbind_set_proto(&s.bind, 0u);
        s.confirmed  = s.bind;
        s.off_picked = true;
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

/* A pick says which set is shown and which the next tick joins.  It changes
 * no page, so nothing is written. */
void outputs_screen_select(uint8_t proto)
{
    if (proto >= OUTBIND_PROTOS) {
        return;
    }
    outbind_set_proto(&s.bind, proto);
    s.off_picked = (proto == 0u);
    s.refused = -1;
    outputs_port_hide();
    touched();
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

/* The list's rows: the protocols, then one PROG PORT entry a port. */
static int pop_rows(void)
{
    return (int)OUTBIND_PROTOS + (int)outputs_screen_port_entries();
}

/* The list: one column of POP_W, two with PROG PORT entries. */
static gfx_rect_t pop_rect(void)
{
    const int cols = (outputs_screen_port_entries() > 0u) ? 2 : 1;
    return gfx_rect_make(POP_X, POP_Y, cols * POP_W,
                         (int)OUTBIND_PROTOS * POP_ROW + 8);
}

/* Row @p i: a protocol in the first column, PROG PORT entry
 * i - OUTBIND_PROTOS in the second, from the same top. */
static gfx_rect_t pop_row_rect(int i)
{
    const int col = (i < (int)OUTBIND_PROTOS) ? 0 : 1;
    const int row = (col == 0) ? i : i - (int)OUTBIND_PROTOS;
    return gfx_rect_make(POP_X + col * POP_W + 4, POP_Y + 4 + row * POP_ROW,
                         POP_W - 8, POP_ROW);
}

static gfx_rect_t clear_rect(void)
{
    return s_board_view ? gfx_rect_make(OB_CLR_X, OB_CLR_Y, OB_CLR_W, OB_CLR_H)
                        : gfx_rect_make(COL_X, CLR_Y, COL_W, CLR_H);
}

gfx_rect_t outputs_unbind_rect(void) { return clear_rect(); }

/* UNBIND ALL PINS is drawn, and so takes a hold: a binding read that is
 * not one, and a protocol's column, not a PROG PORT entry's. */
static bool clear_offered(void)
{
    return s.read == BIND_READ_ODD && outputs_screen_port_shown() < 0;
}

bool outputs_unbind_offered(void) { return clear_offered(); }

/**
 * @brief The grid position of catalogue pin @p i: its index, or on a board
 *        that solders its pins its servo output's place
 *        (outbind_cell_index()).
 * @param i catalogue index
 * @return the grid position; @p i itself for a pin with no cell, which
 *         no caller hands in (they walk the cells)
 */
static int cell_pos(int i)
{
    const uint8_t n = outbind_cell_count(s.bind.board);
    for (uint8_t k = 0; k < n; ++k) {
        if ((int)outbind_cell_index(s.bind.board, k) == i) {
            return (int)k;
        }
    }
    return i;
}

static gfx_rect_t cell_rect(int i)
{
    const int pos = cell_pos(i);
    const int col = pos / GRID_ROWS, row = pos % GRID_ROWS;
    return gfx_rect_make(GRID_X + col * (CELL_W + CELL_GAP),
                         GRID_Y + row * (CELL_H + ROW_GAP),
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
    const int n = (int)outbind_cell_count(s.bind.board);
    for (int k = 0; k < n; ++k) {
        const int i = (int)outbind_cell_index(s.bind.board, (uint8_t)k);
        if (inside(cell_rect(i), x, y)) {
            return i;
        }
    }
    return -1;
}

/* ------------------------------------------------------------------- input */

void outputs_screen_set_board_view(bool board)
{
    if (board != s_board_view) {
        s_board_view = board;
        drop_clear_hold();
        s.open = false;
        s.hit_kind = HIT_NONE;
        outputs_port_set_view(board);
        outputs_screen_invalidate();
        touched();
    }
}

static void reset(void)
{
    memset(&s, 0, sizeof(s));
    s_board_view = false;
    outbind_init(&s.bind);
    s.confirmed = s.bind;
    s.read = BIND_READ_OK;
    s.refused = -1;
    outputs_port_reset();
    ui_hold_reset(&s.clear);
    outputs_port_forget_open();
    touched();
}

/*
 * A visit from SETUP opens on the overview: OFF selected, so every bound
 * pin names its protocol and no tap edits.  The operator picks the
 * protocol to edit, and picking OFF again goes back to the overview.  A
 * visit from a workbench's TARGET row opens on the entry that workbench
 * drives (outputs_screen_open_on()), once.  Selecting writes nothing, so
 * opening the screen sends no page.  The pin picker opens on OFF.
 */
static void enter(void)
{
    s.open = false;
    s.hit_kind = HIT_NONE;
    ui_hold_reset(&s.clear);
    outputs_port_open();
}

static void leave(void)
{
    s.open = false;
    drop_clear_hold();
    outputs_port_cancel();
}

/* The home tag closes an open protocol list without choosing; with none
 * open it leaves the screen. */
static bool back(void)
{
    if (!s.open) {
        return outputs_port_back();   /* the entry's FUNCTION list */
    }
    s.open = false;
    s.hit_kind = HIT_NONE;
    touched();
    return true;
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
    outputs_port_tick();
    if (s.hit_kind != HIT_CLEAR) {
        return;
    }
    if (!clear_offered()) {
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

    if (!s.open && (outputs_port_event(evt) || outputs_unbind_event(evt))) {
        return;
    }

    if (evt->type == TOUCH_EVENT_DOWN) {
        if (s.open) {
            for (int i = 0; i < pop_rows(); ++i) {
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
        /* A binding that is not confirmed takes no press on a pin, and
         * neither does OFF, which edits none: the reason is under the
         * protocol, and a cell that lit under the finger would say a tap
         * there acts. */
        const int c = (outputs_screen_editable()
                       && chosen_proto()->max_pins != 0u)
                          ? cell_at(x, y) : -1;
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

    /* A release the finger did not make ends the press and picks
     * nothing. */
    const bool tap = touch_event_is_tap_up(evt);

    if (tap && kind == HIT_DD && inside(dd_rect(), x, y)) {
        s.open = true; touched();
    } else if (tap && kind == HIT_POP
               && inside(pop_row_rect(index), x, y)) {
        s.open = false;
        if (index < (int)OUTBIND_PROTOS) {
            outputs_screen_select((uint8_t)index);
        } else {
            outputs_screen_select_port(
                (uint8_t)(index - (int)OUTBIND_PROTOS));
        }
    } else if (tap && kind == HIT_CELL
               && inside(cell_rect(index), x, y)) {
        /* A refused tick is not silent: the cell flashes nothing, but the
         * count under the protocol does not move and the reason is printed
         * in the cell or under the protocol.  A PWM clash names the pin
         * tapped there. */
        if (outputs_screen_editable() && clashes((uint8_t)index)) {
            s.refused = index;
            touched();
        } else if (outputs_screen_editable() && !prog_taken((uint8_t)index)
                   && iface_taken((uint8_t)index) == OUTPUTS_IFACE_NONE
                   && outbind_toggle(&s.bind, (uint8_t)index)) {
            s.refused = -1;
            changed();
        }
    } else {
        touched();
    }
}

bool outputs_unbind_event(const touch_event_t *evt)
{
    const int x = evt->point.x, y = evt->point.y;
    if (evt->type == TOUCH_EVENT_DOWN) {
        if (!clear_offered() || !inside(clear_rect(), x, y)) {
            return false;
        }
        s.hit_kind = HIT_CLEAR;
        ui_hold_begin(&s.clear, TX_HOLD_HINT_UNBIND);
        touched();
        return true;
    }
    if (s.hit_kind != HIT_CLEAR) {
        return false;
    }
    if (evt->type == TOUCH_EVENT_MOVE) {
        /* A finger that slides off the key abandons the hold, as on every
         * held control (ui_hold_leave()). */
        if (!inside(clear_rect(), x, y) && ui_hold_leave(&s.clear)) {
            s.hit_kind = HIT_NONE;
            touched();
        }
    } else if (evt->type == TOUCH_EVENT_UP) {
        s.hit_kind = HIT_NONE;
        /* The hold acts when it completes, not on the release. */
        (void)ui_hold_end(&s.clear, evt, clear_rect());
        touched();
    }
    return true;
}

/* ------------------------------------------------------------------ render */

/**
 * @brief Whether the word after the spaces at @p brk is digits alone.
 *
 * @param brk  A break: a space, and the rest of the text after it.
 * @return true when one or more digits follow the spaces and end at a
 *         space or at the end of the text.
 */
static bool number_follows(const char *brk)
{
    while (*brk == ' ') {
        ++brk;
    }
    const char *d = brk;
    while (*d >= '0' && *d <= '9') {
        ++d;
    }
    return d > brk && (*d == ' ' || *d == '\0');
}

int outputs_wrap(gfx_canvas_t *c, gfx_rect_t box, int pitch, const char *text,
                 int max_lines, const gfx_font_t *font, gfx_color_t ink,
                 bool centre)
{
    const int cell = gfx_text_width(font, " ", 1);
    const int cells = (cell > 0) ? box.w / cell : 0;
    int lines = 0;
    const char *at = (text != NULL) ? text : "";
    while (*at == ' ') {
        ++at;
    }
    while (*at != '\0' && lines < max_lines) {
        size_t len = strlen(at);
        if (lines + 1 < max_lines && gfx_text_cells(at) > cells) {
            /* Up to the last space in the line, or the whole width where
             * none falls in it. */
            const size_t fit = gfx_text_prefix(at, cells);
            size_t cut = fit;
            while (cut > 0u && at[cut] != ' ') {
                --cut;
            }
            /* A number goes down with the word before it: "PORT" over "1"
             * would leave the number alone on the next line. */
            if (cut > 0u && number_follows(at + cut)) {
                size_t word = cut - 1u;
                while (word > 0u && at[word] != ' ') {
                    --word;
                }
                cut = (word > 0u) ? word : cut;
            }
            len = (cut > 0u) ? cut : fit;
        }
        if (c != NULL) {
            /* The line, cut to the buffer where it is longer. */
            char buf[96];
            snprintf(buf, sizeof(buf), "%.*s", (int)len, at);
            const int x = centre
                              ? box.x + (box.w
                                         - gfx_text_width(font, buf, 1)) / 2
                              : box.x;
            gfx_text(c, x, box.y + lines * pitch, buf, font, ink, 1);
        }
        at += len;
        while (*at == ' ') {
            ++at;
        }
        ++lines;
    }
    return lines;
}

const char *outputs_result_text(void)
{
    switch (s.result) {
    case OUTPUTS_OK:      return TR(OUT_WRITTEN);
    case OUTPUTS_NO_LINK: return TR(OUT_NO_LINK);
    case OUTPUTS_REFUSED: return TR(OUT_REFUSED);
    case OUTPUTS_IDLE:
    default:              return TR(OUT_NOT_WRITTEN);
    }
}

gfx_color_t outputs_result_color(void)
{
    switch (s.result) {
    case OUTPUTS_OK:      return UI_OK;
    case OUTPUTS_NO_LINK: return UI_TEXT_FAINT;
    case OUTPUTS_REFUSED: return UI_DANGER;
    case OUTPUTS_IDLE:
    default:              return UI_TEXT_FAINT;
    }
}

/**
 * @brief The pins protocol @p p can hold on @p b's board: its own cap and
 *        the page's slots, and no more than the grid has cells (four on a
 *        board that solders its pins).
 * @param b the binding, for its board
 * @param p the protocol
 * @return the most pins @p p takes here
 */
static uint8_t pins_cap(const outbind_t *b, const outbind_proto_t *p)
{
    uint8_t cap = (p->max_pins < OUT_MAX_SLOTS) ? p->max_pins
                                                : (uint8_t)OUT_MAX_SLOTS;
    const uint8_t cells = outbind_cell_count(b->board);
    if (outbind_roles(b->board) != NULL && cells < cap) {
        cap = cells;
    }
    return cap;
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
    if (outputs_screen_port_shown() >= 0) {
        return outputs_screen_port_reason(buf, cap);
    }
    if (s.read == BIND_READ_NONE) {
        snprintf(buf, cap, "%s", TR(OUT_NOT_READ));
        return true;
    }
    if (s.read == BIND_READ_ODD) {
        snprintf(buf, cap, "%s", TR(OUT_NOT_MAPPED));
        return true;
    }
    return outputs_reason_of(&s.bind, buf, cap)
           || outputs_clash_reason(&s.bind, s.refused, outputs_port_pins(),
                                   LINK_PG_PORTS_MAX, buf, cap);
}

bool outputs_reason_of(const outbind_t *b, char *buf, size_t cap)
{
    if (buf == NULL || cap == 0u) {
        return false;
    }
    buf[0] = '\0';
    if (b == NULL) {
        return false;
    }
    const outbind_proto_t *p =
        &outbind_protos()[(b->proto < OUTBIND_PROTOS) ? b->proto : 0u];
    if (p->max_pins == 0u) {
        snprintf(buf, cap, "%s", TR(OUT_OFF_VIEW));
        return true;
    }
    const uint8_t n = outbind_chosen(b);
    const uint8_t lim = pins_cap(b, p);
    const uint8_t used_ch = outbind_channels_used(b);
    const uint8_t used_sl = outbind_chosen_total(b);
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
            snprintf(label, cap, "%s", ui_pin_holder(pin->held_by));
        }
        return OUTPUTS_CELL_RESERVED;
    }
    if (prog_taken(index)) {
        if (label != NULL) {
            snprintf(label, cap, "%s", TR(OUT_PROG_PORT));
        }
        return OUTPUTS_CELL_RESERVED;
    }
    const outputs_iface_t iface = iface_taken(index);
    if (iface != OUTPUTS_IFACE_NONE) {
        if (label != NULL) {
            snprintf(label, cap, "%s", outputs_iface_name(iface));
        }
        return OUTPUTS_CELL_RESERVED;
    }
    if (held != 0u && held != proto) {
        if (label != NULL) {
            snprintf(label, cap, "%s", outbind_protos()[held].name);
        }
        return OUTPUTS_CELL_HELD;
    }
    /* Free, and the PWM generator cannot take it beside what it carries:
     * held by the pin it clashes with, which the line names. */
    if (clashes(index)) {
        if (label != NULL) {
            (void)outputs_clash_label(&s.bind, index, outputs_port_pins(),
                                      LINK_PG_PORTS_MAX, label, cap);
        }
        return OUTPUTS_CELL_HELD;
    }
    if (label != NULL) {
        /* A soldered output is named by its role, so the line under it is
         * its GPIO, the pin the lead is on. */
        char role[8];
        if (outputs_role_name(s.bind.board, pin->gpio, role, sizeof(role))) {
            (void)ui_pin_name(label, cap, pin->gpio);
        } else {
            snprintf(label, cap, "PAD %u", (unsigned)pin->pad);
        }
    }
    return (held != 0u) ? OUTPUTS_CELL_TICKED : OUTPUTS_CELL_FREE;
}

void outputs_unbind_draw(gfx_canvas_t *c)
{
    if (!clear_offered()) {
        return;
    }
    /* Danger red as it is held, as the other held controls fade to the
     * colour of what they are about to do. */
    const gfx_rect_t k = clear_rect();
    const gfx_color_t fill = ui_hold_fill(UI_PANEL, UI_DANGER, s.clear.held_s);
    gfx_fill_chamfer_rect_ex(c, k.x, k.y, k.w, k.h, 10, 0, 10, 0, fill);
    gfx_draw_chamfer_rect_ex(c, k.x, k.y, k.w, k.h, 10, 0, 10, 0, UI_DANGER);
    /* Centred, 10px in from either end: on the grid in the label face, two
     * lines at most; on the narrower board view's key in 8 x 16, four. */
    const char *label = TR(OUT_HOLD_UNBIND);
    const gfx_font_t *font = s_board_view ? OU_FONT_FINE : OU_FONT_LABEL;
    const int pitch = s_board_view ? OB_WHY_FINE : OU_LINE;
    const int most = s_board_view ? 4 : 2;
    const int h = gfx_text_height(font, 1);
    const gfx_rect_t in = gfx_rect_make(k.x + 10, k.y, k.w - 20, h);
    const int lines = outputs_wrap(NULL, in, pitch, label, most, font, UI_TEXT,
                                   true);
    (void)outputs_wrap(c,
                       gfx_rect_make(in.x, k.y + (k.h - h) / 2
                                               - (lines - 1) * pitch / 2,
                                     in.w, h),
                       pitch, label, most, font, UI_TEXT, true);
}

static void draw_left(gfx_canvas_t *c)
{
    if (outputs_screen_port_shown() >= 0) {
        outputs_port_draw(c, dd_rect(), s.hit_kind == HIT_DD, s.open);
        return;
    }
    const outbind_proto_t *p = chosen_proto();

    gfx_text(c, COL_X, COL_Y, TR(OUT_PROTOCOL), OU_FONT_LABEL, UI_TEXT_FAINT,
             1);

    ui_choice_face(c, dd_rect(), p->name, s.hit_kind == HIT_DD, s.open);

    char buf[96];
    /* OFF takes no pin, so a count of its pins says nothing. */
    if (p->max_pins != 0u) {
        const uint8_t n = outbind_chosen(&s.bind);
        const uint8_t cap = pins_cap(&s.bind, p);
        snprintf(buf, sizeof(buf), TR(OUT_PINS_OF), (unsigned)n,
                 (unsigned)cap);
        gfx_text(c, COL_X, COUNT_Y, buf, OU_FONT_LABEL, UI_TEXT_DIM, 1);
    }

    if (p->channels > 1u) {
        snprintf(buf, sizeof(buf), TR(OUT_CHANNELS_ON_PIN),
                 (unsigned)p->channels);
        gfx_text(c, COL_X, CHAN_Y, buf, OU_FONT_LABEL, UI_TEXT_FAINT, 1);
    }

    if (outputs_screen_reason(buf, sizeof(buf))) {
        (void)outputs_wrap(c, gfx_rect_make(COL_X, REASON_Y, COL_W, 20),
                           OU_LINE, buf, 2, OU_FONT_LABEL, UI_WARN, false);
    }

    outputs_unbind_draw(c);

    /* The result in the head face, on a second line where it is wider
     * than the column, its last line ending on the column's foot; LAST
     * WRITE right above its first. */
    const int lines = outputs_wrap(NULL, gfx_rect_make(COL_X, 0, COL_W, 36),
                                   RESULT_PITCH, outputs_result_text(), 2,
                                   OU_FONT_HEAD, outputs_result_color(), false);
    const int ry = RESULT_Y + (2 - lines) * RESULT_PITCH;
    gfx_text(c, COL_X, ry - LAST_ABOVE, TR(OUT_LAST_WRITE), OU_FONT_LABEL,
             UI_TEXT_FAINT, 1);
    (void)outputs_wrap(c, gfx_rect_make(COL_X, ry, COL_W, 36), RESULT_PITCH,
                       outputs_result_text(), 2, OU_FONT_HEAD, outputs_result_color(),
                       false);
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
     * grey; the IOmcu's reserved pins stay red. */
    const bool elsewhere = (held != 0u && held != proto);
    const bool down = (s.hit_kind == HIT_CELL && s.hit_index == i);
    /*
     * A binding no read has confirmed is drawn the way a pin that cannot be
     * ticked is: no accent, dim ink, and its ticks filled like the pins of
     * another protocol.  It is shown, and not offered.
     */
    const bool live = outputs_screen_editable();
    /* The pin of the PROG PORT entry shown: lit, as a ticked pin is, and
     * read-only. */
    const bool port_lit = shown_port_pin() != OUTPUTS_NO_PIN
                          && pin->gpio == shown_port_pin()
                          && prog_taken((uint8_t)i);
    const bool reserved = pin->reserved
                          || (prog_taken((uint8_t)i) && !port_lit)
                          || iface_taken((uint8_t)i) != OUTPUTS_IFACE_NONE;
    const bool can = live && !reserved && !clashes((uint8_t)i)
                     && outbind_can_add(&s.bind, (uint8_t)i);
    const bool lit = (live && on) || port_lit;

    gfx_fill_chamfer_rect_ex(c, r.x, r.y, r.w, r.h, 8, 0, 8, 0,
                             down ? UI_PANEL_HI : UI_PANEL);
    gfx_draw_chamfer_rect_ex(c, r.x, r.y, r.w, r.h, 8, 0, 8, 0,
                             lit ? UI_ACCENT : UI_EDGE);

    /* The box: filled when chosen, hollow when free, and struck through when
     * the pin is spoken for.  It sits beside the name, above the line that
     * says what holds the pin. */
    const int bx = r.x + CELL_PAD, by = r.y + CELL_PAD;
    gfx_color_t edge = reserved ? UI_DANGER
                                     : ((lit || can) ? UI_ACCENT : UI_EDGE_HI);
    gfx_draw_chamfer_rect_ex(c, bx, by, BOX, BOX, 4, 0, 4, 0, edge);
    if (lit) {
        gfx_fill_chamfer_rect_ex(c, bx + 5, by + 5, BOX - 10, BOX - 10,
                                 3, 0, 3, 0, UI_ACCENT);
    } else if (elsewhere || on) {
        /* Filled, so it reads as taken rather than free, and dim, so it does
         * not read as this protocol's. */
        gfx_fill_chamfer_rect_ex(c, bx + 5, by + 5, BOX - 10, BOX - 10,
                                 3, 0, 3, 0, UI_EDGE_HI);
    } else if (reserved) {
        for (int k = 5; k < BOX - 5; ++k) {
            gfx_fill_rect(c, bx + k, by + k, 2, 2, UI_DANGER);
        }
    }

    char name[UI_PIN_NAME_SIZE];
    if (!outputs_role_name(s.bind.board, pin->gpio, name, sizeof(name))) {
        (void)ui_pin_name(name, sizeof(name), pin->gpio);
    }
    gfx_color_t ink = reserved ? UI_TEXT_FAINT
                                    : ((lit || can) ? UI_TEXT : UI_TEXT_DIM);
    gfx_text(c, bx + BOX + 10, r.y + 6, name, OU_FONT_PIN, ink, 1);

    /* Under the box and the name, from the box's left edge: 18 cells,
     * 144px of the cell's 167. */
    char label[32];
    gfx_color_t lc = UI_TEXT_FAINT;
    switch (outputs_screen_cell((uint8_t)i, label, sizeof(label))) {
    case OUTPUTS_CELL_RESERVED: lc = port_lit ? UI_TEXT : UI_DANGER; break;
    case OUTPUTS_CELL_HELD:
        /* The overview (OFF) edits no protocol, so what holds each bound
         * pin is the thing it shows: in full ink, apart from free pins and
         * PWM clashes.  Dim again while the binding is not confirmed. */
        lc = (live && proto == 0u && held != 0u) ? UI_TEXT : UI_TEXT_DIM;
        break;
    default:
        break;
    }
    gfx_text(c, r.x + CELL_PAD, r.y + CELL_LABEL_Y, label, OU_FONT_FINE, lc,
             1);
}

static void draw_popup(gfx_canvas_t *c)
{
    ui_choice_list(c, pop_rect());
    const outbind_proto_t *p = outbind_protos();
    for (int i = 0; i < (int)OUTBIND_PROTOS; ++i) {
        ui_choice_row(c, pop_row_rect(i), p[i].name,
                      outputs_screen_port_shown() < 0
                          && &p[i] == chosen_proto(),
                      s.hit_kind == HIT_POP && s.hit_index == i);
    }
    for (int n = 0; n < (int)outputs_screen_port_entries(); ++n) {
        const int i = (int)OUTBIND_PROTOS + n;
        char name[24];
        outputs_port_name((uint8_t)n, name, sizeof(name));
        ui_choice_row(c, pop_row_rect(i), name,
                      outputs_screen_port_shown() == n,
                      s.hit_kind == HIT_POP && s.hit_index == i);
    }
}

static void render(gfx_canvas_t *c, int buffer_index)
{
    /* A row stepped on SETUP moves an interface's pin without a touch
     * here. */
    uint64_t iface[OUTPUTS_IFACE_COUNT];
    outputs_iface_read(iface);
    if (memcmp(iface, s.iface, sizeof(iface)) != 0) {
        memcpy(s.iface, iface, sizeof(iface));
        touched();
    }
    bool stale = true;
    if (buffer_index >= 0 && buffer_index < MAX_FBS) {
        stale = !s.chrome_valid[buffer_index]
                || s.drawn_gen != s.gen + outputs_port_revision();
    }
    if (!stale) {
        return;
    }

    gfx_fill_rect(c, 0, 0, c->width, c->height, UI_BG);
    draw_left(c);
    const int cells = (int)outbind_cell_count(s.bind.board);
    for (int k = 0; k < cells; ++k) {
        draw_cell(c, (int)outbind_cell_index(s.bind.board, (uint8_t)k));
    }
    if (s.open) {
        draw_popup(c);
    }

    if (buffer_index >= 0 && buffer_index < MAX_FBS) {
        s.chrome_valid[buffer_index] = true;
        s.drawn_gen = s.gen + outputs_port_revision();
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
    /* And a segment acts on its release, as a pin does. */
    outputs_port_cancel();
    /* And a hold completes on a timer: one whose release went missing would
     * finish with nothing on the glass. */
    ui_hold_reset(&s.clear);
    outputs_screen_invalidate();
}

/**
 * @brief ui_screen_t's covered: the protocol list or the FUNCTION list.
 * @return true while one is up
 */
static bool covered(void) { return s.open || outputs_port_fn_open(); }

static const ui_screen_t s_screen = {
    .covered = covered,
    .title  = "OUTPUTS",
    .reset  = reset,
    .enter  = enter,
    .leave  = leave,
    .tick   = tick,
    .event  = event,
    .cancel = cancel,
    .render = render,
    .back   = back,
};

const ui_screen_t *outputs_screen(void) { return &s_screen; }
