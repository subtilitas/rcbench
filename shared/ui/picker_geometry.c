/**
 * @file picker_geometry.c
 * @brief PICK A PIN's geometry: the box the board takes, where each pad sits,
 *        the buttons above and below the board, the marks inside it, and the
 *        protocol list's rows.  picker_screen.c holds the state, the touch
 *        handling and the screen's interface (picker_screen.h); picker_draw.c
 *        draws; picker_screen_int.h is what the three share.
 *
 * SPDX-License-Identifier: MIT
 */

#include "board_drawing.h"
#include "link_pages.h"
#include "outputs_screen.h"
#include "outputs_screen_int.h"
#include "picker_screen.h"
#include "port_view.h"

#include "picker_screen_int.h"

/* ------------------------------------------------------------ board art */

/*
 * Everything that depends on what the board looks like, as against where
 * its pads are, is in this section and in picker_draw.c: the box the board
 * occupies, here, and draw_board(), there.  The buttons, the marks and the
 * tables take pad positions from pk_pad_xy() and nothing from the
 * drawing.
 */

/*
 * The size an outline of @p ow x @p oh draws at: BOARD_W wide and the
 * height its aspect gives, or, for an outline taller than BOARD_MAX_H at
 * that width, BOARD_MAX_H high and the width its aspect gives.  Either way
 * the board is to scale and never stretched.
 */
void picker_board_fit(uint16_t ow, uint16_t oh, int *w, int *h)
{
    int bw = BOARD_W;
    int bh = BOARD_MAX_H;
    if (ow != 0u && oh != 0u) {
        bh = (int)(((uint32_t)BOARD_W * oh) / ow);
        if (bh > BOARD_MAX_H) {
            bh = BOARD_MAX_H;
            bw = (int)(((uint32_t)BOARD_MAX_H * ow) / oh);
            if (bw < 1) { bw = 1; }
        }
    }
    *w = bw;
    *h = bh;
}

/*
 * The box the board occupies, from the drawing's outline or else the
 * shape's, centred on the screen.  The drawing is drawn to scale into it;
 * its outline is the shape's whenever the board has one
 * (board_drawing_agrees()).
 */
void pk_board_box(int *x, int *y, int *w, int *h)
{
    const link_drawing_t *d = pk_drawing();
    const outbind_shape_t *sh = pk_shape();
    uint16_t ow = 0u, oh = 0u;
    if (d != NULL) {
        ow = d->width;
        oh = d->height;
    } else if (sh != NULL) {
        ow = sh->width_cmm;
        oh = sh->height_cmm;
    }
    picker_board_fit(ow, oh, w, h);
    *x = (SCREEN_W - *w) / 2;
    *y = (SCREEN_H - *h) / 2;
}

/* ------------------------------------------------------------- geometry */

/*
 * Where a pad sits on screen, from where the board says it sits on itself:
 * the drawing's pad record, else the shape's grid.  Scaled the way
 * board_drawing_render() scales a ring, so a trace ends on its ring.
 */
bool pk_pad_xy(uint8_t pad, int *px, int *py)
{
    const link_drawing_t *d = pk_drawing();
    uint16_t xc, yc, ow, oh;
    if (d != NULL) {
        link_draw_pad_t p;
        if (!link_drawing_find_pad(d, pad, &p)) {
            return false;
        }
        xc = p.x;
        yc = p.y;
        ow = d->width;
        oh = d->height;
    } else {
        const outbind_shape_t *sh = pk_shape();
        if (sh == NULL || !outbind_pad_xy(sh, pad, &xc, &yc)) {
            return false;
        }
        ow = sh->width_cmm;
        oh = sh->height_cmm;
    }
    int bx, by, bw, bh;
    pk_board_box(&bx, &by, &bw, &bh);
    *px = bx + board_drawing_scale(xc, bw, ow);
    *py = by + board_drawing_scale(yc, bh, oh);
    return true;
}

/* Which of the two pad rows a pad is in: the near one is the lower. */
bool pk_pad_is_top(uint8_t pad)
{
    int x, y;
    int bx, by, bw, bh;
    pk_board_box(&bx, &by, &bw, &bh);
    return pk_pad_xy(pad, &x, &y) && y < by + bh / 2;
}

/*
 * The button for catalogue entry @p i and its tier, or false when it has
 * none.  Tier 0 is the row nearer the board.
 *
 * Only a pin an output may have gets one: a reserved pin is crossed on the
 * pad, which says the pin exists and is spoken for, and a button under it
 * would say it could be pressed.
 */
bool pk_button_at(int i, gfx_rect_t *out, int *tier_out)
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
    if (!pk_pad_xy(pins[i].pad, &px, &py)) {
        return false;
    }

    /*
     * The tier alternates along the row rather than by catalogue index, so
     * neighbouring pads never land in the same tier and the traces do not
     * cross.  Counting the buttons before this one in the same row is what
     * gives that, and it is cheap: a board has at most a few dozen pins.
     */
    const bool top = pk_pad_is_top(pins[i].pad);
    int before = 0;
    for (int k = 0; k < (int)n; ++k) {
        if (k == i || pins[k].reserved) { continue; }
        int qx, qy;
        if (!pk_pad_xy(pins[k].pad, &qx, &qy)) { continue; }
        if (pk_pad_is_top(pins[k].pad) != top) { continue; }
        if (qx < px) { ++before; }
    }
    const int tier = before % TIERS;

    int bx, by, bw, bh;
    pk_board_box(&bx, &by, &bw, &bh);
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
    if (tier_out != NULL) { *tier_out = tier; }
    return out->h > 0;
}

bool picker_screen_button_rect(uint8_t index, gfx_rect_t *out)
{
    gfx_rect_t r;
    if (!picker_screen_can_draw() || !pk_button_at(index, &r, NULL)) {
        return false;
    }
    if (out != NULL) { *out = r; }
    return true;
}

/*
 * The centre and radius of the marker of pad-list entry @p e, or false for
 * a pad that has none.
 *
 * A rail's depth is the parity of the rails before it in its row, counted by
 * position and not by list order, so neighbouring rails always alternate.
 */
bool pk_marker_at(uint8_t e, int *mx, int *my, int *r)
{
    const outbind_pad_t *pads = outbind_pads(s.bind.board);
    const uint8_t n = outbind_pad_count(s.bind.board);
    if (pads == NULL || e >= n) {
        return false;
    }
    const uint8_t kind = pads[e].kind;
    if (kind != (uint8_t)LINK_PAD_GROUND && kind != (uint8_t)LINK_PAD_POWER) {
        return false;
    }
    int px, py;
    if (!pk_pad_xy(pads[e].pad, &px, &py)) {
        return false;
    }
    const bool top = pk_pad_is_top(pads[e].pad);
    int drop = GND_DROP;
    *r = GND_R;
    if (kind == (uint8_t)LINK_PAD_POWER) {
        int before = 0;
        for (uint8_t k = 0; k < n; ++k) {
            int qx, qy;
            if (k == e || pads[k].kind != (uint8_t)LINK_PAD_POWER
                || !pk_pad_xy(pads[k].pad, &qx, &qy)
                || pk_pad_is_top(pads[k].pad) != top) {
                continue;
            }
            if (qx < px) { ++before; }
        }
        drop = (before & 1) ? RAIL_DROP_1 : RAIL_DROP_0;
        *r = RAIL_R;
    }
    *mx = px;
    *my = top ? (py + drop) : (py - drop);
    return true;
}

bool picker_screen_marker_rect(uint8_t entry, gfx_rect_t *out)
{
    int mx, my, r;
    if (!picker_screen_can_draw() || !pk_marker_at(entry, &mx, &my, &r)) {
        return false;
    }
    if (out != NULL) {
        *out = gfx_rect_make(mx - r - RING, my - r - RING,
                             2 * (r + RING), 2 * (r + RING));
    }
    return true;
}

gfx_rect_t picker_screen_area(picker_area_t area)
{
    switch (area) {
    case PICKER_AREA_PINS:
        return gfx_rect_make(PINS_X, PINS_Y, PINS_W, SCREEN_H - PINS_Y - 4);
    case PICKER_AREA_IN_USE:
        return gfx_rect_make(USE_X, USE_Y, USE_W, SCREEN_H - USE_Y - 4);
    case PICKER_AREA_HEAD:
    default:
        return gfx_rect_make(HEAD_X, HEAD_Y, HEAD_W, HEAD_H);
    }
}

gfx_rect_t picker_screen_list_face(void)
{
    return gfx_rect_make(HEAD_X, HEAD_Y, FACE_W, FACE_H);
}

/* The list's rows: the protocols, then one PROG PORT entry a port, as on
 * OUTPUTS, each POP_ROW tall or less where they would run past the
 * screen. */
int pk_pop_rows(void)
{
    return (int)OUTBIND_PROTOS + (int)outputs_screen_port_entries();
}

static int pop_row_h(void)
{
    const int fit = (SCREEN_H - HEAD_Y - 16) / pk_pop_rows();
    return (fit < POP_ROW) ? fit : POP_ROW;
}

gfx_rect_t pk_pop_rect(void)
{
    return gfx_rect_make(HEAD_X, HEAD_Y, POP_W, pk_pop_rows() * pop_row_h() + 8);
}

gfx_rect_t picker_screen_list_row(uint8_t proto)
{
    return gfx_rect_make(HEAD_X + 4, HEAD_Y + 4 + (int)proto * pop_row_h(),
                         POP_W - 8, pop_row_h());
}

bool picker_screen_list_open(void) { return s.open; }
