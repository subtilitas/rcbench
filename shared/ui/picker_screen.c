/*
 * The board, with a button on every pin an output may have.
 * See picker_screen.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "picker_screen.h"

#include <stdio.h>
#include <string.h>

#include "link_pages.h"
#include "ui_theme.h"
#include "ui_widgets.h"

#define SCREEN_W 800
#define SCREEN_H (480 - UI_BAND_H)
#define MAX_FBS  3

/*
 * The board is drawn as wide as the columns beside it leave room for, and as
 * tall as its own outline makes it.  500 is what fits with a table either
 * side; the height follows the aspect so a board is never stretched.
 */
#define BOARD_W  500
#define BOARD_MAX_H 240

/*
 * Two staggered rows of buttons above the board and two below.  A button is
 * 43 wide because that is a fingertip and because twenty of them at 2.54 mm
 * pitch do not fit in one row at any board size that fits the panel.
 */
#define TIERS 2
#define TGAP  5
#define BTNW  43
#define GAP   4

static struct {
    outbind_t         bind;
    picker_apply_fn   apply;
    picker_artwork_fn source;
    const gfx_color_t *art;
    uint16_t          art_w, art_h;

    int      hit;            /**< catalogue index under the finger, or -1  */
    bool     chrome_valid[MAX_FBS];
} s;

/*
 * A change invalidates every framebuffer, and a render validates the one it
 * drew.  Not a counter compared against the last frame drawn: with two
 * buffers alternating, a counter that is satisfied by the first of them
 * leaves the second holding the old picture, and clearing the others on
 * every paint instead makes the screen repaint for ever.
 *
 * That costs more here than anywhere else.  This screen's chrome is a
 * photograph -- 201 kB blitted per repaint, sixteen thousand cache-line
 * fills against nine hundred for a screen that idles -- so a picker that
 * never settles would hold the whole panel at thirteen frames a second for
 * as long as it is open.
 */
static void touched(void) { picker_screen_invalidate(); }

void picker_screen_invalidate(void)
{
    memset(s.chrome_valid, 0, sizeof(s.chrome_valid));
}

const outbind_t *picker_screen_binding(void) { return &s.bind; }

void picker_screen_set_binding(const outbind_t *b)
{
    if (b != NULL) {
        s.bind = *b;
        /* Read off the wire or restored, so it is trimmed before it is drawn
         * rather than trusted to mean something on this board. */
        outbind_trim(&s.bind);
        touched();
    }
}

void picker_screen_set_apply(picker_apply_fn fn) { s.apply = fn; }

void picker_screen_set_artwork_source(picker_artwork_fn fn) { s.source = fn; }

void picker_screen_set_artwork(const gfx_color_t *px, uint16_t w, uint16_t h)
{
    s.art   = (w != 0u && h != 0u) ? px : NULL;
    s.art_w = w;
    s.art_h = h;
    touched();
}

static const outbind_shape_t *shape(void)
{
    const outbind_board_t *bd = outbind_board(s.bind.board);
    return (bd != NULL) ? bd->shape : NULL;
}

bool picker_screen_can_draw(void)
{
    return shape() != NULL && outbind_pin_count(s.bind.board) > 0u;
}

/* ------------------------------------------------------------- geometry */

/*
 * The box the board occupies.  With a photograph it is the photograph's own
 * size, because the picture is cropped to the outline and scaling it would
 * cost a resample for nothing; without one it is BOARD_W and whatever height
 * the outline's aspect gives.
 */
static void board_box(int *x, int *y, int *w, int *h)
{
    const outbind_shape_t *sh = shape();
    int bw = BOARD_W;
    int bh = BOARD_MAX_H;
    if (s.art != NULL) {
        bw = s.art_w;
        bh = s.art_h;
    } else if (sh != NULL && sh->width_cmm != 0u) {
        bh = (int)(((uint32_t)BOARD_W * sh->height_cmm) / sh->width_cmm);
        if (bh > BOARD_MAX_H) { bh = BOARD_MAX_H; }
    }
    *w = bw;
    *h = bh;
    *x = (SCREEN_W - bw) / 2;
    *y = (SCREEN_H - bh) / 2;
}

/* Where a pad sits on screen, from where the board says it sits on itself. */
static bool pad_xy(uint8_t pad, int *px, int *py)
{
    const outbind_shape_t *sh = shape();
    uint16_t xc, yc;
    if (sh == NULL || !outbind_pad_xy(sh, pad, &xc, &yc)) {
        return false;
    }
    int bx, by, bw, bh;
    board_box(&bx, &by, &bw, &bh);
    *px = bx + (int)(((uint32_t)xc * (uint32_t)bw) / sh->width_cmm);
    *py = by + (int)(((uint32_t)yc * (uint32_t)bh) / sh->height_cmm);
    return true;
}

/* Which of the two pad rows a pad is in: the near one is the lower. */
static bool pad_is_top(uint8_t pad)
{
    int x, y;
    int bx, by, bw, bh;
    board_box(&bx, &by, &bw, &bh);
    return pad_xy(pad, &x, &y) && y < by + bh / 2;
}

/*
 * The button for catalogue entry @p i, or false when it has none.
 *
 * Only a pin an output may have gets one: a reserved pin is crossed on the
 * pad, which says the pin exists and is spoken for, and a button under it
 * would say it could be pressed.
 */
static bool button_rect(int i, gfx_rect_t *out)
{
    const uint16_t board = s.bind.board;
    const outbind_pin_t *pins = outbind_pins(board);
    const uint8_t n = outbind_pin_count(board);
    const outbind_board_t *bd = outbind_board(board);
    if (pins == NULL || i < 0 || i >= (int)n || bd == NULL) {
        return false;
    }
    /*
     * A reserved pin gets no button; a soldered board's pins do.
     *
     * Refusing them there would take the screen's answer away on exactly the
     * board that most needs it: an operator holding one still has to find
     * where GP7 is, and outbind_toggle() is what refuses the change. They are
     * drawn as held instead, which is what they are.
     */
    if (pins[i].reserved) {
        return false;
    }
    int px, py;
    if (!pad_xy(pins[i].pad, &px, &py)) {
        return false;
    }

    /*
     * The tier alternates along the row rather than by catalogue index, so
     * neighbouring pads never land in the same tier and the traces do not
     * cross.  Counting the buttons before this one in the same row is what
     * gives that, and it is cheap: a board has at most a few dozen pins.
     */
    const bool top = pad_is_top(pins[i].pad);
    int before = 0;
    for (int k = 0; k < (int)n; ++k) {
        if (k == i || pins[k].reserved) { continue; }
        int qx, qy;
        if (!pad_xy(pins[k].pad, &qx, &qy)) { continue; }
        if (pad_is_top(pins[k].pad) != top) { continue; }
        if (qx < px) { ++before; }
    }
    const int tier = before % TIERS;

    int bx, by, bw, bh;
    board_box(&bx, &by, &bw, &bh);
    /*
     * The room above the board is `by` and no less: the router hands a screen
     * its own body, so the band is already out of these coordinates and
     * taking it off again would shrink every top button by the height of a
     * band it is not sharing the screen with.
     */
    const int th = (((top ? by : (SCREEN_H - (by + bh)))
                     - GAP) - (TIERS - 1) * TGAP) / TIERS;
    const int ty = top ? (by - GAP - (tier + 1) * th - tier * TGAP)
                       : (by + bh + GAP + tier * (th + TGAP));
    int x = px - BTNW / 2;
    if (x < 2) { x = 2; }
    if (x + BTNW > SCREEN_W - 2) { x = SCREEN_W - 2 - BTNW; }

    out->x = (int16_t)x;
    out->y = (int16_t)ty;
    out->w = (int16_t)BTNW;
    out->h = (int16_t)((th > 0) ? th : 0);
    return out->h > 0;
}

/* --------------------------------------------------------------- drawing */

/* The board itself: its photograph, or its outline and pads drawn. */
static void draw_board(gfx_canvas_t *c)
{
    int bx, by, bw, bh;
    board_box(&bx, &by, &bw, &bh);

    if (s.art != NULL) {
        gfx_blit(c, bx, by, s.art, bw, bh, s.art_w);
        return;
    }

    /*
     * No photograph, so the board is drawn: an outline and every pad the
     * shape places.  The pads are what an operator counts along, so all of
     * them are drawn and not only the ones that are GPIOs.
     */
    gfx_fill_round_rect(c, bx, by, bw, bh, 6, GFX_RGB(18, 42, 30));
    gfx_draw_round_rect(c, bx, by, bw, bh, 6, GFX_RGB(60, 110, 84));
    const outbind_shape_t *sh = shape();
    if (sh == NULL) {
        return;
    }
    const unsigned pads = (unsigned)sh->per_side * 2u;
    for (unsigned p = 1; p <= pads; ++p) {
        int px, py;
        if (pad_xy((uint8_t)p, &px, &py)) {
            gfx_fill_circle(c, px, py, 4, GFX_RGB(196, 160, 84));
            gfx_fill_circle(c, px, py, 2, GFX_RGB(24, 24, 28));
        }
    }
}

/*
 * The grounds and the rails, marked inside the board on a short trace.
 *
 * A servo lead has three wires and the buttons describe one of them. These
 * are the other two, and they go inside the outline because that is the only
 * room left: the space beside the board is the buttons', and a mark on the
 * pad itself would be under 40 pixels like the pad is.
 *
 * The depth alternates so a run of rails at one end of a row does not draw
 * one label over the next. Grounds are five pads apart on this form factor
 * and would not collide, but the rails are not.
 */
static void draw_pads_that_are_not_pins(gfx_canvas_t *c)
{
    const outbind_pad_t *pads = outbind_pads(s.bind.board);
    const uint8_t n = outbind_pad_count(s.bind.board);
    if (pads == NULL) {
        return;                 /* this board does not say; nothing is drawn */
    }
    int bx, by, bw, bh;
    board_box(&bx, &by, &bw, &bh);

    int depth = 0;
    for (uint8_t i = 0; i < n; ++i) {
        int px, py;
        if (!pad_xy(pads[i].pad, &px, &py)) { continue; }
        const bool top = pad_is_top(pads[i].pad);

        if (pads[i].kind == (uint8_t)LINK_PAD_OTHER) {
            /* Neither a ground nor a rail, and not what anybody is looking
             * for: marked as spoken for and left unlabelled. */
            gfx_fill_circle(c, px, py, 4, GFX_RGB(70, 74, 82));
            continue;
        }
        if (pads[i].kind != (uint8_t)LINK_PAD_GROUND
            && pads[i].kind != (uint8_t)LINK_PAD_POWER) {
            continue;
        }

        const bool ground = (pads[i].kind == (uint8_t)LINK_PAD_GROUND);
        char label[6];
        if (ground) {
            label[0] = 'G';
            label[1] = '\0';
        } else if (pads[i].decivolts == 0u) {
            /* An input rail follows whatever feeds it, so it carries no
             * number rather than one that is only sometimes true. */
            snprintf(label, sizeof(label), "PWR");
        } else {
            snprintf(label, sizeof(label), "%uV%u",
                     (unsigned)(pads[i].decivolts / 10u),
                     (unsigned)(pads[i].decivolts % 10u));
        }

        const int lw = gfx_text_width(UI_FONT_LABEL, label, 1);
        const int cw = lw + 10, ch = 20;
        const int drop = 24 + (depth & 1) * 26;
        const int cy = top ? (py + drop) : (py - drop);
        depth++;

        const gfx_color_t face = ground ? GFX_RGB(232, 236, 244)
                                        : GFX_RGB(240, 176, 64);
        const gfx_color_t ink  = GFX_RGB(10, 10, 14);
        /* The trace first, so the chip sits on top of its own end. */
        for (int o = -1; o <= 1; ++o) {
            gfx_line(c, px + o, py, px + o, cy, face);
        }
        gfx_fill_chamfer_rect_ex(c, px - cw / 2, cy - ch / 2, cw, ch,
                                 6, 6, 6, 6, face);
        gfx_text(c, px - lw / 2, cy - 8, label, UI_FONT_LABEL, ink, 1);
    }
}

/* A cross on a pad something else already has. */
static void cross(gfx_canvas_t *c, int x, int y, gfx_color_t col)
{
    for (int o = -1; o <= 1; ++o) {
        gfx_line(c, x - 6 + o, y - 6, x + 6 + o, y + 6, col);
        gfx_line(c, x - 6 + o, y + 6, x + 6 + o, y - 6, col);
    }
}

static void trace(gfx_canvas_t *c, int x, int y0, int y1, gfx_color_t col)
{
    const gfx_color_t halo = GFX_RGB(6, 6, 8);
    for (int o = -3; o <= 3; ++o) { gfx_line(c, x + o, y0, x + o, y1, halo); }
    for (int o = -1; o <= 1; ++o) { gfx_line(c, x + o, y0, x + o, y1, col); }
}

/* Channel numbers come off the page, not from counting: they are allocated
 * in pin order across every protocol, so a group's first pin is not its
 * channel 0 unless it also holds the lowest pin. */
static void channels_of(int8_t *out, uint8_t n)
{
    memset(out, -1, n);
    uint16_t regs[LINK_OS_COUNT];
    const uint8_t used = outbind_to_slots(&s.bind, regs);
    const outbind_pin_t *pins = outbind_pins(s.bind.board);
    for (uint8_t sl = 0; sl < used; ++sl) {
        const uint16_t *r = &regs[(size_t)sl * LINK_OS_STRIDE];
        for (uint8_t i = 0; i < n; ++i) {
            if (pins[i].gpio == (uint8_t)r[LINK_OS_PIN]) {
                out[i] = (int8_t)LINK_OS_FIRST(r[LINK_OS_RANGE]);
            }
        }
    }
}

static void draw_pads_and_buttons(gfx_canvas_t *c, const int8_t *chan)
{
    const uint16_t board = s.bind.board;
    const outbind_pin_t *pins = outbind_pins(board);
    const uint8_t n = outbind_pin_count(board);
    const uint8_t proto = (s.bind.proto < OUTBIND_PROTOS) ? s.bind.proto : 0u;
    const outbind_board_t *bd = outbind_board(board);
    const bool locked = (bd != NULL) && bd->fixed;

    for (int i = 0; i < (int)n; ++i) {
        int px, py;
        if (!pad_xy(pins[i].pad, &px, &py)) { continue; }

        const uint8_t held = outbind_group_of(&s.bind, (uint8_t)i);
        const bool on    = (held != 0u && held == proto);
        /* A soldered board's pins are nobody's to press, so they are drawn
         * the way a pin another protocol holds is: shown, and not offered. */
        const bool other = (held != 0u && held != proto) || locked;

        if (pins[i].reserved) {
            cross(c, px, py, UI_DANGER);
            continue;
        }

        gfx_rect_t r;
        if (!button_rect(i, &r)) { continue; }

        const gfx_color_t wire = other ? GFX_RGB(104, 108, 116)
                                 : (on ? UI_ACCENT : GFX_RGB(214, 218, 226));
        int bx, by, bw, bh;
        board_box(&bx, &by, &bw, &bh);
        const bool top = pad_is_top(pins[i].pad);
        const int meet = top ? (r.y + r.h) : r.y;
        trace(c, px, py, meet, wire);
        gfx_fill_circle(c, px, py, 5, GFX_RGB(6, 6, 8));
        gfx_fill_circle(c, px, py, 3, wire);

        const bool down = (s.hit == i);
        const gfx_color_t face = other ? GFX_RGB(52, 55, 62)
                                 : (on ? UI_ACCENT
                                       : (down ? UI_PANEL_HI : UI_PANEL));
        const gfx_color_t ink  = other ? GFX_RGB(132, 136, 144)
                                 : (on ? GFX_RGB(6, 30, 36) : UI_TEXT);
        const gfx_color_t edge = other ? GFX_RGB(78, 82, 90)
                                 : (on ? GFX_RGB(6, 40, 48) : UI_EDGE_HI);
        gfx_fill_chamfer_rect_ex(c, r.x, r.y, r.w, r.h, 8, 8, 8, 8, face);
        gfx_draw_chamfer_rect_ex(c, r.x, r.y, r.w, r.h, 8, 8, 8, 8, edge);

        char nm[6];
        snprintf(nm, sizeof(nm), "%u", (unsigned)pins[i].gpio);
        const int lw = gfx_text_width(UI_FONT_HEAD, nm, 1);
        gfx_text(c, r.x + (r.w - lw) / 2, r.y + (r.h - 28) / 2, nm,
                 UI_FONT_HEAD, ink, 1);
        (void)chan;
    }
}

/* The left column: what this protocol holds, in channel order. */
static void draw_mine(gfx_canvas_t *c, const int8_t *chan)
{
    const uint8_t proto = (s.bind.proto < OUTBIND_PROTOS) ? s.bind.proto : 0u;
    const outbind_pin_t *pins = outbind_pins(s.bind.board);
    const uint8_t n = outbind_pin_count(s.bind.board);
    const int tx = 6;
    int ty = 8;

    gfx_text(c, tx, ty, "PINS", UI_FONT_HEAD, UI_ACCENT, 1);
    ty += 30;
    gfx_fill_rect(c, tx, ty, 120, 2, UI_EDGE_HI);
    ty += 8;
    gfx_text(c, tx, ty, outbind_protos()[proto].name, UI_FONT_LABEL,
             UI_TEXT_FAINT, 1);
    ty += 22;

    int shown = 0;
    for (uint8_t i = 0; i < n && ty < SCREEN_H - 24; ++i) {
        if (outbind_group_of(&s.bind, i) != proto || proto == 0u) { continue; }
        char row[24];
        snprintf(row, sizeof(row), "CH%d", chan[i]);
        gfx_text(c, tx, ty, row, UI_FONT_LABEL, UI_TEXT_DIM, 1);
        snprintf(row, sizeof(row), "GP%u", (unsigned)pins[i].gpio);
        gfx_text(c, tx + 40, ty - 6, row, UI_FONT_HEAD, UI_ACCENT, 1);
        ty += 28;
        ++shown;
    }
    if (shown == 0) {
        gfx_text(c, tx, ty, "NONE YET", UI_FONT_LABEL, UI_TEXT_FAINT, 1);
    }
}

/* The right column: pins another protocol holds, and which one. */
static void draw_theirs(gfx_canvas_t *c, const int8_t *chan)
{
    const uint8_t proto = (s.bind.proto < OUTBIND_PROTOS) ? s.bind.proto : 0u;
    const outbind_pin_t *pins = outbind_pins(s.bind.board);
    const uint8_t n = outbind_pin_count(s.bind.board);
    const int tx = SCREEN_W - 132;
    int ty = 8;

    gfx_text(c, tx, ty, "IN USE", UI_FONT_HEAD, GFX_RGB(150, 154, 162), 1);
    ty += 30;
    gfx_fill_rect(c, tx, ty, SCREEN_W - tx - 6, 2, UI_EDGE_HI);
    ty += 8;
    gfx_text(c, tx, ty, "BY OTHERS", UI_FONT_LABEL, UI_TEXT_FAINT, 1);
    ty += 22;

    /* In the order their first pin falls, so the channels count down the
     * page rather than jumping about with the protocol numbering. */
    uint32_t listed = 0u;
    int shown = 0;
    for (uint8_t first = 0; first < n && ty < SCREEN_H - 24; ++first) {
        const uint8_t g = outbind_group_of(&s.bind, first);
        if (g == 0u || g == proto) { continue; }
        if ((listed & ((uint32_t)1u << g)) != 0u) { continue; }
        listed |= (uint32_t)1u << g;
        gfx_text(c, tx, ty, outbind_protos()[g].name, UI_FONT_LABEL,
                 UI_TEAL, 1);
        ty += 20;
        for (uint8_t i = first; i < n && ty < SCREEN_H - 24; ++i) {
            if (outbind_group_of(&s.bind, i) != g) { continue; }
            char row[24];
            snprintf(row, sizeof(row), "CH%d", chan[i]);
            gfx_text(c, tx + 4, ty, row, UI_FONT_LABEL, UI_TEXT_FAINT, 1);
            snprintf(row, sizeof(row), "GP%u", (unsigned)pins[i].gpio);
            gfx_text(c, tx + 44, ty - 6, row, UI_FONT_HEAD,
                     GFX_RGB(150, 154, 162), 1);
            ty += 28;
            ++shown;
        }
    }
    if (shown == 0) {
        gfx_text(c, tx, ty, "NONE", UI_FONT_LABEL, UI_TEXT_FAINT, 1);
    }
}

/* ----------------------------------------------------------------- input */

static int cell_at(int x, int y)
{
    const uint8_t n = outbind_pin_count(s.bind.board);
    for (int i = 0; i < (int)n; ++i) {
        gfx_rect_t r;
        if (button_rect(i, &r) && gfx_rect_contains(r, x, y)) {
            return i;
        }
    }
    return -1;
}

static void event(const touch_event_t *evt)
{
    if (evt == NULL) {
        return;
    }
    if (evt->type == TOUCH_EVENT_DOWN) {
        s.hit = cell_at(evt->point.x, evt->point.y);
        if (s.hit >= 0) { touched(); }
        return;
    }
    if (evt->type != TOUCH_EVENT_UP) {
        return;
    }
    const int was = s.hit;
    s.hit = -1;
    if (was < 0) {
        return;
    }
    touched();
    if (cell_at(evt->point.x, evt->point.y) != was) {
        return;              /* the finger left the button it pressed */
    }
    if (outbind_toggle(&s.bind, (uint8_t)was) && s.apply != NULL) {
        s.apply(&s.bind);
    }
}

static void reset(void)
{
    memset(&s, 0, sizeof(s));
    outbind_init(&s.bind);
    s.hit = -1;
    touched();
}

static void enter(void)
{
    s.hit = -1;
    /* Asked on the way in, so a photograph that arrived since the last visit
     * is the one shown, and one for a board that has been swapped is not. */
    if (s.source != NULL) {
        s.source(s.bind.board);
    }
    touched();
}
static void leave(void) { s.hit = -1; }

static void render(gfx_canvas_t *c, int buffer_index)
{
    if (buffer_index >= 0 && buffer_index < MAX_FBS
        && s.chrome_valid[buffer_index]) {
        return;
    }

    gfx_fill_rect(c, 0, 0, c->width, c->height, UI_BG);

    if (!picker_screen_can_draw()) {
        /*
         * A board that does not say where its pads are cannot be drawn, and
         * drawing a guessed one would point at the wrong pad as confidently
         * as the right one.
         *
         * Drawn as a panel rather than a line of text: an empty screen reads
         * as a screen that failed, and this one is working -- it is saying
         * what it does not know, and where the pins are still to be had.
         */
        const int pw = 520, ph = 150;
        const int px = (SCREEN_W - pw) / 2, py = (SCREEN_H - ph) / 2;
        gfx_fill_chamfer_rect_ex(c, px, py, pw, ph, 12, 0, 12, 0, UI_PANEL);
        gfx_draw_chamfer_rect_ex(c, px, py, pw, ph, 12, 0, 12, 0, UI_EDGE);
        static const char *const lines[] = {
            "NO PICTURE OF THIS BOARD",
            "IT DOES NOT SAY WHERE ITS PADS ARE,",
            "SO DRAWING ONE WOULD BE GUESSWORK.",
            "ITS PINS ARE ON THE OUTPUTS SCREEN.",
        };
        int ty = py + 26;
        for (unsigned i = 0; i < sizeof(lines) / sizeof(lines[0]); ++i) {
            const gfx_font_t *font = (i == 0u) ? UI_FONT_HEAD : UI_FONT_LABEL;
            const gfx_color_t ink = (i == 0u) ? UI_TEXT : UI_TEXT_FAINT;
            const int w = gfx_text_width(font, lines[i], 1);
            gfx_text(c, px + (pw - w) / 2, ty, lines[i], font, ink, 1);
            ty += (i == 0u) ? 40 : 24;
        }
    } else {
        int8_t chan[OUTBIND_PINS];
        const uint8_t n = outbind_pin_count(s.bind.board);
        channels_of(chan, (n < OUTBIND_PINS) ? n : (uint8_t)OUTBIND_PINS);
        draw_board(c);
        draw_pads_that_are_not_pins(c);
        draw_pads_and_buttons(c, chan);
        draw_mine(c, chan);
        draw_theirs(c, chan);
    }

    if (buffer_index >= 0 && buffer_index < MAX_FBS) {
        s.chrome_valid[buffer_index] = true;
    }
}

static const ui_screen_t s_screen = {
    .title  = "PICK A PIN",
    .reset  = reset,
    .enter  = enter,
    .leave  = leave,
    .tick   = NULL,
    .event  = event,
    .render = render,
};

const ui_screen_t *picker_screen(void) { return &s_screen; }
