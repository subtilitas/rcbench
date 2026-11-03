/**
 * @file picker_cat.c
 * @brief OUTPUTS' board view's cat: the two spots it offers, what keeps a spot
 *        clear, the pick on the first frame of a visit, and the drawing
 *        with the screen's static parts.
 *
 * A spot is clear when its box, grown by UI_CAT_GAP, meets nothing the
 * screen draws in the state it is in: the head, the PINS table to its last
 * row, the IN USE table to its last line, the board with every key and the
 * traces between them, or the open list.  The PROG PORT entry's column, LAST
 * WRITE and UNBIND ALL PINS count too; the grid view draws no cat.  The pose
 * is picked on a visit's first frame from the roll entering took, and again
 * from that roll when another protocol or port is picked and the cat no
 * longer fits: the entry's column covers the left spot.  In one selection a
 * row or a line that grows into the box hides the cat until it shrinks back.
 * A tap on the cat lands on nothing the screen hit-tests, so it does nothing.
 *
 * SPDX-License-Identifier: MIT
 */

#include "picker_screen.h"
#include "ui_text.h"

#include "picker_screen_int.h"

/**
 * @brief The spots: bottom left under the PINS table, its left edge on the
 *        table's, and bottom right under the IN USE table, 2px in from the
 *        panel's edge; both standing on FOOT.
 */
static const ui_cat_spot_t k_spots[] = {
    { UI_CAT_SPOT_PICKER_LEFT,  PINS_X,       FOOT, UI_CAT_FROM_LEFT },
    { UI_CAT_SPOT_PICKER_RIGHT, SCREEN_W - 2, FOOT, UI_CAT_FROM_RIGHT },
};
/** @brief How many spots, 2. */
#define SPOTS ((int)(sizeof(k_spots) / sizeof(k_spots[0])))

/**
 * @brief Whether @p a and @p b share a pixel.
 * @param a a box
 * @param b another
 * @return true when they meet
 */
static bool meets(gfx_rect_t a, gfx_rect_t b)
{
    gfx_rect_t out;
    return gfx_rect_intersect(a, b, &out);
}

/**
 * @brief The smallest box holding @p a and @p b; an empty one counts as
 *        nothing.
 * @param a a box
 * @param b another
 * @return their union's bounding box
 */
static gfx_rect_t hull(gfx_rect_t a, gfx_rect_t b)
{
    if (gfx_rect_empty(a)) { return b; }
    if (gfx_rect_empty(b)) { return a; }
    const int x0 = (a.x < b.x) ? a.x : b.x;
    const int y0 = (a.y < b.y) ? a.y : b.y;
    const int x1 = (a.x + a.w > b.x + b.w) ? a.x + a.w : b.x + b.w;
    const int y1 = (a.y + a.h > b.y + b.h) ? a.y + a.h : b.y + b.h;
    return gfx_rect_make(x0, y0, x1 - x0, y1 - y0);
}

/**
 * @brief The board, every key and the traces from pad to key: the box
 *        holding the board and every key, which holds the traces too.
 * @return the box
 */
static gfx_rect_t board_and_keys(void)
{
    int bx, by, bw, bh;
    pk_board_box(&bx, &by, &bw, &bh);
    gfx_rect_t r = gfx_rect_make(bx, by, bw, bh);
    const uint8_t n = outbind_pin_count(s.bind.board);
    for (int i = 0; i < (int)n; ++i) {
        gfx_rect_t k;
        if (pk_button_at(i, &k, NULL)) {
            r = hull(r, k);
        }
    }
    return r;
}

/**
 * @brief The PINS table as drawn: its heading and its rows, or the line
 *        that says none is bound; empty when OFF draws no table.
 * @return the box
 */
static gfx_rect_t pins_table(void)
{
    if (outbind_protos()[pk_proto_shown()].max_pins == 0u || s.port >= 0) {
        return gfx_rect_make(0, 0, 0, 0);
    }
    int rows = 0;
    while (PINS_TOP + (rows + 1) * ROW_H <= FOOT
           && picker_screen_row((uint8_t)rows, NULL)) {
        ++rows;
    }
    int w = PINS_W;
    int bottom = PINS_TOP + rows * ROW_H;
    if (rows == 0) {
        const int tw = gfx_text_width(PK_FONT_LABEL, TR(PK_NONE_YET), 1);
        w = (tw > w) ? tw : w;
        bottom = PINS_TOP + gfx_text_height(PK_FONT_LABEL, 1);
    }
    return gfx_rect_make(PINS_X, PINS_Y, w, bottom - PINS_Y);
}

/**
 * @brief The IN USE table as drawn: its heading to its last line, or to
 *        the line that says none, out to the panel's edge.
 * @return the box
 */
static gfx_rect_t use_table(void)
{
    int bottom = USE_TOP + gfx_text_height(PK_FONT_LABEL, 1);
    picker_use_t u;
    for (uint8_t k = 0; picker_screen_in_use(k, &u); ++k) {
        bottom = u.rect.y + u.rect.h;
    }
    return gfx_rect_make(USE_X, USE_Y, SCREEN_W - USE_X, bottom - USE_Y);
}

/**
 * @brief The open list with the 4px shadow ui_choice_list() draws right
 *        of it and under it.
 * @return the box
 */
static gfx_rect_t list_and_shadow(void)
{
    const gfx_rect_t p = pk_pop_rect();
    return gfx_rect_make(p.x, p.y, p.w + 4, p.h + 4);
}

/**
 * @brief Whether a cat may cover @p r: inside the body above FOOT, and
 *        nothing the screen draws within UI_CAT_GAP of it.
 * @param r the box
 * @return true when it is clear
 */
static bool clear(gfx_rect_t r)
{
    if (gfx_rect_empty(r) || r.x < 0 || r.y < 0 || r.x + r.w > SCREEN_W
        || r.y + r.h > FOOT) {
        return false;
    }
    const gfx_rect_t g = gfx_rect_make(r.x - UI_CAT_GAP, r.y - UI_CAT_GAP,
                                       r.w + 2 * UI_CAT_GAP,
                                       r.h + 2 * UI_CAT_GAP);
    if (!picker_screen_can_draw()) {
        return false;
    }
    /* The entry's rows and its FUNCTION list, down to the foot. */
    if (s.port >= 0 && meets(g, gfx_rect_make(OB_COL_X, OB_FN_Y, OB_LIST_W,
                                              FOOT - OB_FN_Y))) {
        return false;
    }
    if (pk_last_shown() && meets(g, pk_last_rect())) {
        return false;
    }
    if (outputs_unbind_offered() && meets(g, outputs_unbind_rect())) {
        return false;
    }
    const int head_w = (HEAD_W > FACE_W) ? HEAD_W : FACE_W;
    return !meets(g, gfx_rect_make(HEAD_X, HEAD_Y, head_w, HEAD_H))
           && !meets(g, board_and_keys())
           && !meets(g, pins_table())
           && !meets(g, use_table())
           && !(s.open && meets(g, list_and_shadow()));
}

void pk_cat_reset(void)
{
    s.cat_pending = false;
    s.cat = UI_CAT_PICK_NONE;
    s.cat_drawn = gfx_rect_make(0, 0, 0, 0);
    s.cat_view = -1;
}

/**
 * @brief The selection shown, as one number: a protocol, or a port past them.
 * @return the number
 */
static int view_key(void)
{
    return (s.port >= 0) ? (int)OUTBIND_PROTOS + s.port : (int)pk_proto_shown();
}

void pk_cat_enter(void)
{
    pk_cat_reset();
    s.cat_pending = true;
    s.cat_roll = ui_cats_roll();
}

void pk_cat_render(gfx_canvas_t *c)
{
    /* A new selection keeps a cat that still fits: it does not jump about
     * on every pick in the list. */
    const bool moved = view_key() != s.cat_view
                       && (s.cat.pose < 0
                           || !clear(ui_cat_rect(s.cat.pose,
                                                 &k_spots[s.cat.spot])));
    if (s.cat_pending || (s.cat_view >= 0 && moved)) {
        s.cat_pending = false;
        s.cat = ui_cats_pick(k_spots, SPOTS, clear, s.cat_roll);
    }
    if (s.cat_view >= 0 || view_key() >= 0) {
        s.cat_view = view_key();
    }
    s.cat_drawn = gfx_rect_make(0, 0, 0, 0);
    if (s.cat.pose < 0 || ui_cats_forced() == UI_CAT_NONE
        || !clear(ui_cat_rect(s.cat.pose, &k_spots[s.cat.spot]))) {
        return;
    }
    s.cat_drawn = ui_cats_draw(c, s.cat, k_spots, SPOTS);
}

bool picker_screen_cat(gfx_rect_t *box, int *pose)
{
    const bool shown = !gfx_rect_empty(s.cat_drawn);
    if (box != NULL) { *box = s.cat_drawn; }
    if (pose != NULL) { *pose = shown ? s.cat.pose : UI_CAT_NONE; }
    return shown;
}
