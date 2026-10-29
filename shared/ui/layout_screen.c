/**
 * @file
 * @brief LAYOUT.  See layout_screen.h.
 *
 * Three columns of 320px under the band: J1 and J2 (the board's signal
 * connectors, outbind_roles_t::conns) and the other connectors.  A
 * position is a row of 72px: its number in 20 x 36, its role in 16 x 28,
 * the GPIO and the net on its + in 8 x 16 under the role, and what it
 * carries now on the right in 10 x 20, or 8 x 16 where that is too wide.
 * Under the columns one line says that a rev 1 board needs work by hand;
 * the help page lists it.
 *
 * SPDX-License-Identifier: MIT
 */

#include "layout_screen.h"

#include <stdio.h>
#include <string.h>

#include "bind_link.h"
#include "gfx.h"
#include "out_bind.h"
#include "outputs_screen.h"
#include "ui_board.h"
#include "ui_layout.h"
#include "ui_text.h"
#include "ui_theme.h"

/** @brief The body's width, 1024px. */
#define SCREEN_W UI_PANEL_W
/** @brief The framebuffers the panel turns over. */
#define MAX_FBS  3

#define LY_FONT_NUM   (&gfx_font_20x36)  /**< a position's number: 20 x 36 */
#define LY_FONT_ROLE  (&gfx_font_16x28)  /**< a role's name: 16 x 28 */
#define LY_FONT_LABEL (&gfx_font_10x20)  /**< headings, states: 10 x 20 */
#define LY_FONT_FINE  (&gfx_font_8x16)   /**< GPIO and net: 8 x 16 */
#define LY_FONT_BODY  (&gfx_font_12x24)  /**< the note: 12 x 24 */

#define COL_W   320   /**< a column's width, px */
#define COL_GAP 16    /**< between columns and at the edges, px */
#define HEAD_Y  16    /**< a column's heading */
#define ROWS_Y  56    /**< its first row */
#define ROW_H   72    /**< a connector position's row */
#define ROW_P   80    /**< row pitch: 8px between rows */
#define OTHER_H 48    /**< another connector's row */
#define OTHER_P 52    /**< its pitch */
#define ROLE_X  48    /**< a role's left edge in its row */
#define PAD     12    /**< a row's inner margin */
#define NOTE_Y  (ROWS_Y + 3 * ROW_P + ROW_H + 24) /**< the rev 1 line */

/** @brief What the picture shows; a frame that finds it moved repaints. */
typedef struct {
    uint16_t    board;                  /**< ui_board()                   */
    bind_read_t read;                   /**< outputs_screen_read_state()  */
    uint8_t     group[2][OUTBIND_CONN_POSITIONS]; /**< each position's protocol */
} layout_seen_t;

/** @brief The screen's state. */
typedef struct {
    bool          valid[MAX_FBS]; /**< each framebuffer holds the picture */
    layout_seen_t seen;           /**< what it shows                     */
} layout_state_t;

/** @brief The one instance. */
static layout_state_t s;

void layout_screen_invalidate(void)
{
    memset(s.valid, 0, sizeof(s.valid));
}

/**
 * @brief The protocol bound on @p gpio, as OUTPUTS holds it.
 * @param board the recorded board
 * @param gpio the pin
 * @return the protocol number, 0 for OFF; 0 while the binding is not one
 *         of this board
 */
static uint8_t group_of(uint16_t board, uint8_t gpio)
{
    const outbind_t *b = outputs_screen_binding();
    if (b == NULL || b->board != board) {
        return 0u;
    }
    return outbind_group_of(b, outbind_index_of(board, gpio));
}

/**
 * @brief Whether OUTPUTS holds a binding of @p board read off the IOmcu.
 * @param board the recorded board
 * @return true when the states are known
 */
static bool read_ok(uint16_t board)
{
    const outbind_t *b = outputs_screen_binding();
    return outputs_screen_read_state() == BIND_READ_OK && b != NULL
           && b->board == board;
}

/**
 * @brief The role name of programming port or servo output @p gpio.
 * @param r the board's roles
 * @param kind outbind_conn_kind_t
 * @param gpio the pin
 * @param buf at least 8 bytes
 * @param cap its size
 */
static void role_name(const outbind_roles_t *r, uint8_t kind, uint8_t gpio,
                      char *buf, size_t cap)
{
    _Static_assert(OUTBIND_FIXED_OUTS == OUTBIND_FIXED_PROGS,
                   "one loop walks both role lists");
    const uint8_t *pins = (kind == (uint8_t)OUTBIND_CONN_PROG) ? r->prog
                                                                : r->out;
    const char *stem = (kind == (uint8_t)OUTBIND_CONN_PROG) ? "PROGP" : "PWM";
    for (unsigned n = 0; n < OUTBIND_FIXED_OUTS; ++n) {
        if (pins[n] == gpio) {
            (void)snprintf(buf, cap, "%s%02u", stem, n + 1u);
            return;
        }
    }
    (void)snprintf(buf, cap, "GPIO%u", (unsigned)gpio);
}

bool layout_screen_pos(int conn, int pos, layout_pos_t *out)
{
    const uint16_t board = ui_board();
    const outbind_roles_t *r = outbind_roles(board);
    if (out == NULL || r == NULL || conn < 0 || conn >= (int)r->conn_count) {
        return false;
    }
    const outbind_conn_t *c = &r->conns[conn];
    if (pos < 0 || pos >= (int)c->count) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->gpio = c->gpio[pos];
    out->rail = c->rail[pos];
    role_name(r, c->kind, out->gpio, out->role, sizeof(out->role));
    if (c->kind == (uint8_t)OUTBIND_CONN_PROG) {
        (void)snprintf(out->state, sizeof(out->state), "%s",
                       TR(OUT_PORT_FN_PROG));
    } else if (!read_ok(board)) {
        (void)snprintf(out->state, sizeof(out->state), "%s", TR(PK_NOT_READ));
    } else {
        (void)snprintf(out->state, sizeof(out->state), "%s",
                       outbind_protos()[group_of(board, out->gpio)].name);
    }
    return true;
}

/**
 * @brief What the picture shows now.
 * @param seen filled
 */
static void look(layout_seen_t *seen)
{
    memset(seen, 0, sizeof(*seen));
    seen->board = ui_board();
    seen->read = outputs_screen_read_state();
    const outbind_roles_t *r = outbind_roles(seen->board);
    for (int k = 0; r != NULL && k < (int)r->conn_count && k < 2; ++k) {
        for (int p = 0; p < (int)r->conns[k].count; ++p) {
            seen->group[k][p] = group_of(seen->board, r->conns[k].gpio[p]);
        }
    }
}

/**
 * @brief Whether two looks show the same picture.
 * @param a one look
 * @param b another
 * @return true when board, reading and every position's protocol agree
 */
static bool same_look(const layout_seen_t *a, const layout_seen_t *b)
{
    return a->board == b->board && a->read == b->read
           && memcmp(a->group, b->group, sizeof(a->group)) == 0;
}

/**
 * @brief Text right-aligned at @p right on line @p y: 10 x 20, or 8 x 16
 *        where 10 x 20 is wider than @p room.
 * @param c the canvas
 * @param right one past its last pixel
 * @param y its top
 * @param room the width it may take
 * @param text the text
 * @param color its colour
 */
static void text_right(gfx_canvas_t *c, int right, int y, int room,
                       const char *text, gfx_color_t color)
{
    const gfx_font_t *f = LY_FONT_LABEL;
    if (gfx_text_width(f, text, 1) > room) {
        f = LY_FONT_FINE;
        y += 2;
    }
    (void)gfx_text(c, right - gfx_text_width(f, text, 1), y, text, f, color,
                   1);
}

/**
 * @brief A column's heading: the connector's name and what it carries.
 * @param c the canvas
 * @param x the column's left edge
 * @param name "J1", or "" for none
 * @param what the heading
 */
static void heading(gfx_canvas_t *c, int x, const char *name, const char *what)
{
    int tx = x + PAD;
    if (name[0] != '\0') {
        tx += gfx_text(c, tx, HEAD_Y, name, LY_FONT_ROLE, UI_ACCENT, 1) + 12;
    }
    (void)gfx_text(c, tx, HEAD_Y + 6, what, LY_FONT_LABEL, UI_TEXT_FAINT, 1);
}

/**
 * @brief One position's row.
 * @param c the canvas
 * @param x the column's left edge
 * @param y the row's top
 * @param n the position, from 1
 * @param p the position as layout_screen_pos() gave it
 */
static void draw_pos(gfx_canvas_t *c, int x, int y, int n,
                     const layout_pos_t *p)
{
    gfx_fill_chamfer_rect_ex(c, x, y, COL_W, ROW_H, 8, 0, 8, 0, UI_PANEL);
    gfx_draw_chamfer_rect_ex(c, x, y, COL_W, ROW_H, 8, 0, 8, 0, UI_EDGE);
    char num[4];
    (void)snprintf(num, sizeof(num), "%d", n);
    (void)gfx_text(c, x + PAD, y + 18, num, LY_FONT_NUM, UI_TEXT_FAINT, 1);
    const int rw = gfx_text(c, x + ROLE_X, y + 8, p->role, LY_FONT_ROLE,
                            UI_TEXT, 1);
    char line[40];
    (void)snprintf(line, sizeof(line), "GPIO %u  %s", (unsigned)p->gpio,
                   p->rail);
    (void)gfx_text(c, x + ROLE_X, y + 46, line, LY_FONT_FINE, UI_TEXT_FAINT,
                   1);
    const gfx_color_t col = (strcmp(p->state, outbind_protos()[0].name) == 0)
                                ? UI_TEXT_FAINT : UI_ACCENT;
    text_right(c, x + COL_W - PAD, y + 12, COL_W - ROLE_X - rw - 2 * PAD,
               p->state, col);
}

/**
 * @brief Another connector's row: its name, what it carries, its pins.
 * @param c the canvas
 * @param r the board's roles
 * @param o the connector
 * @param x the column's left edge
 * @param y the row's top
 */
static void draw_other(gfx_canvas_t *c, const outbind_roles_t *r,
                       const outbind_other_t *o, int x, int y)
{
    gfx_fill_chamfer_rect_ex(c, x, y, COL_W, OTHER_H, 8, 0, 8, 0, UI_PANEL);
    gfx_draw_chamfer_rect_ex(c, x, y, COL_W, OTHER_H, 8, 0, 8, 0, UI_EDGE);
    (void)gfx_text(c, x + PAD, y + 4, (o->name[0] != '\0') ? o->name : "-",
                   LY_FONT_LABEL, UI_ACCENT, 1);
    (void)gfx_text(c, x + ROLE_X, y + 4, o->what, LY_FONT_LABEL, UI_TEXT, 1);
    char line[24];
    if (o->gpio[1] != OUTBIND_NO_GPIO) {
        (void)snprintf(line, sizeof(line), "GPIO %u, %u",
                       (unsigned)o->gpio[0], (unsigned)o->gpio[1]);
    } else {
        (void)snprintf(line, sizeof(line), "GPIO %u", (unsigned)o->gpio[0]);
    }
    (void)gfx_text(c, x + ROLE_X, y + 28, line, LY_FONT_FINE, UI_TEXT_FAINT,
                   1);
    if (o->gpio[0] == r->sda2) {
        text_right(c, x + COL_W - PAD, y + 24, COL_W - ROLE_X - 112,
                   TR(LAYOUT_NOT_DRIVEN), UI_TEXT_FAINT);
    }
}

/**
 * @brief The panel shown on a board whose pins are chosen.
 * @param c the canvas
 */
static void draw_none(gfx_canvas_t *c)
{
    const int pw = 720, ph = 120;  /* 58 cells of 12 x 24 inside 24px margins */
    const int px = (SCREEN_W - pw) / 2, py = (UI_BODY_H - ph) / 2;
    gfx_fill_chamfer_rect_ex(c, px, py, pw, ph, 12, 0, 12, 0, UI_PANEL);
    gfx_draw_chamfer_rect_ex(c, px, py, pw, ph, 12, 0, 12, 0, UI_EDGE);
    (void)gfx_text(c, px + 24, py + 24, TR(LAYOUT_NONE), LY_FONT_BODY,
                   UI_TEXT, 1);
    (void)gfx_text(c, px + 24, py + 64, TR(LAYOUT_NONE_WHERE), LY_FONT_BODY,
                   UI_TEXT_FAINT, 1);
}

/**
 * @brief Draw the screen, where the picture moved or this framebuffer
 *        does not hold it.
 * @param c the canvas, the body under the band
 * @param buffer_index the framebuffer, or -1 for one not cached
 */
static void render(gfx_canvas_t *c, int buffer_index)
{
    layout_seen_t now;
    look(&now);
    if (!same_look(&now, &s.seen)) {
        s.seen = now;
        layout_screen_invalidate();
    }
    if (buffer_index >= 0 && buffer_index < MAX_FBS && s.valid[buffer_index]) {
        return;
    }
    gfx_fill_rect(c, 0, 0, c->width, c->height, UI_BG);
    const outbind_roles_t *r = outbind_roles(now.board);
    if (r == NULL) {
        draw_none(c);
    } else {
        int x = COL_GAP;
        for (int k = 0; k < (int)r->conn_count && k < 2; ++k) {
            const outbind_conn_t *cn = &r->conns[k];
            heading(c, x, cn->name, (cn->kind == (uint8_t)OUTBIND_CONN_PROG)
                                        ? TR(LAYOUT_PROG) : TR(LAYOUT_OUT));
            for (int p = 0; p < (int)cn->count; ++p) {
                layout_pos_t pos;
                if (layout_screen_pos(k, p, &pos)) {
                    draw_pos(c, x, ROWS_Y + p * ROW_P, p + 1, &pos);
                }
            }
            x += COL_W + COL_GAP;
        }
        heading(c, x, "", TR(LAYOUT_OTHER));
        for (int i = 0; i < (int)r->other_count && i < 6; ++i) {
            draw_other(c, r, &r->others[i], x, ROWS_Y + i * OTHER_P);
        }
        (void)gfx_text(c, COL_GAP + PAD, NOTE_Y, TR(LAYOUT_REWORK),
                       LY_FONT_BODY, UI_TEXT_FAINT, 1);
    }
    if (buffer_index >= 0 && buffer_index < MAX_FBS) {
        s.valid[buffer_index] = true;
    }
}

/** @brief A visit opens on a fresh picture. */
static void enter(void)
{
    layout_screen_invalidate();
}

/** @brief The state of a fresh start. */
static void reset(void)
{
    memset(&s, 0, sizeof(s));
}

/** @brief The screen's table for the router. */
static const ui_screen_t k_screen = {
    .title  = "LAYOUT",
    .reset  = reset,
    .enter  = enter,
    .render = render,
};

const ui_screen_t *layout_screen(void)
{
    return &k_screen;
}
