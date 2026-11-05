/**
 * @file
 * @brief OUTPUTS' screen guide (outputs_screen_int.h): the board view's
 *        two pages and the grid view's one.
 *
 * The board view: the left column and the keys left of the board with the
 * dock on the right, then IN USE and the keys right of it with the dock on
 * the left.  A PROG PORT entry puts its rows in the left column in place of
 * the protocol's.  The grid view: its left column and the first cell with
 * the dock on the right.  OUTPUTS drives no output and names no pin an
 * armed screen drives, so every page shows while ARMED as well.  An open
 * protocol or FUNCTION list leaves no page.
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include "outputs_screen.h"
#include "ui_help.h"
#include "ui_text.h"

#include "picker_screen_int.h"

/** @brief The name of the PROG PORT entry shown, the label of its face. */
static char s_port_name[24];

int outputs_help_count(void)
{
    if (outputs_view_screen()->covered()) {
        return 0;
    }
    return outputs_view_is_board() ? 2 : 1;
}

/**
 * @brief The entry's rows, PIN, FUNCTION, SERVO TESTER and PORT PWM, under
 *        the face that names the port.
 * @param p    The page.
 * @param face The face, in the screen's coordinates.
 * @param r    The rows' rects.
 */
static void add_port(ui_help_page_t *p, gfx_rect_t face,
                     const outputs_port_rects_t *r)
{
    outputs_port_name((uint8_t)outputs_screen_port_shown(), s_port_name,
                      sizeof(s_port_name));
    p->title = TX_HG_OUT_TITLE_PORT;
    (void)ui_help_add(p, s_port_name, TX_HG_OUT_PORT, face);
    (void)ui_help_add(p, "PIN", outputs_view_is_board() ? TX_HG_OUT_PIN
                                                        : TX_HG_OUT_PIN_GRID,
                      r->pin);
    (void)ui_help_add(p, TR(OUT_PORT_FUNCTION), TX_HG_OUT_FUNCTION, r->fn);
    (void)ui_help_add(p, TR(OUT_PORT_TESTER), TX_HG_OUT_TESTER, r->tester);
    (void)ui_help_add(p, TR(OUT_PORT_PWM), TX_HG_OUT_PWM, r->pwm);
}

/**
 * @brief The board's key the guide marks on a page: of the keys whole in
 *        the live half, the leftmost for the dock on the right, the
 *        rightmost for the dock on the left; the lower of two at one x.
 * @param side The dock's side.
 * @param out  The key's rect, in the screen's coordinates.
 * @return False for a board without keys in that half.
 */
static bool pick_key(ui_help_side_t side, gfx_rect_t *out)
{
    const gfx_rect_t live = ui_help_live_rect(side);
    bool found = false;
    const int n = (int)outbind_pin_count(s.bind.board);
    for (int i = 0; i < n; ++i) {
        gfx_rect_t r;
        int tier = 0;
        if (!pk_button_at(i, &r, &tier) || r.x < live.x
            || r.x + r.w > live.x + live.w) {
            continue;
        }
        const bool better =
            !found
            || ((side == UI_HELP_DOCK_RIGHT) ? r.x < out->x : r.x > out->x)
            || (r.x == out->x && r.y > out->y);
        if (better) {
            *out = r;
            found = true;
        }
    }
    return found;
}

/**
 * @brief Page @p i of the board view.
 * @param i The page: 0 the left column, 1 IN USE.
 * @param p The page, its side set.
 */
static void board_page(int i, ui_help_page_t *p)
{
    const bool port = outputs_screen_port_shown() >= 0;
    const ui_text_id_t key_text = port ? TX_HG_OUT_KEY_PORT : TX_HG_OUT_KEY;
    gfx_rect_t key;
    if (i == 0) {
        const gfx_rect_t face = gfx_rect_make(HEAD_X, HEAD_Y, FACE_W, FACE_H);
        outputs_port_rects_t r;
        if (port && outputs_port_rects(&r)) {
            add_port(p, face, &r);
            return;
        }
        p->title = TX_HG_OUT_TITLE_PINS;
        (void)ui_help_add(p, TR(OUT_PROTOCOL), TX_HG_OUT_PROTO, face);
        if (pk_last_shown()) {
            (void)ui_help_add(p, TR(OUT_LAST_WRITE), TX_HG_OUT_LAST,
                              pk_last_rect());
        }
        if (outbind_protos()[pk_proto_shown()].max_pins != 0u) {
            (void)ui_help_add(p, TR(PK_PINS), TX_HG_OUT_PINS,
                              gfx_rect_make(PINS_X, PINS_Y, PINS_W,
                                            PINS_TOP + ROW_H - PINS_Y));
        }
    } else {
        p->title = TX_HG_OUT_TITLE_BOARD;
        (void)ui_help_add(p, TR(PK_IN_USE), TX_HG_OUT_INUSE,
                          gfx_rect_make(USE_X, USE_Y, USE_W,
                                        pk_use_foot() - USE_Y));
        if (outputs_unbind_offered()) {
            (void)ui_help_add(p, TR(HG_OUT_UNBIND_NAME), TX_HG_OUT_UNBIND,
                              outputs_unbind_rect());
        }
        if (pk_last_shown()) {
            /* At the foot of the IN USE column on a board with a key
             * beside the face; ui_help_add() drops it elsewhere. */
            (void)ui_help_add(p, TR(OUT_LAST_WRITE), TX_HG_OUT_LAST,
                              pk_last_rect());
        }
    }
    if (pick_key(p->side, &key)) {
        (void)ui_help_add(p, TR(HG_OUT_KEY_NAME), key_text, key);
    }
}

/**
 * @brief The grid view's page: its left column and the first cell.
 * @param p The page, its side set.
 */
static void grid_page(ui_help_page_t *p)
{
    const gfx_rect_t face = outputs_grid_rect(OUTPUTS_GRID_FACE);
    outputs_port_rects_t r;
    if (outputs_screen_port_shown() >= 0 && outputs_port_rects(&r)) {
        add_port(p, face, &r);
        return;
    }
    p->title = TX_HG_OUT_TITLE_PINS;
    (void)ui_help_add(p, TR(OUT_PROTOCOL), TX_HG_OUT_PROTO, face);
    (void)ui_help_add(p, TR(HG_OUT_CELL_NAME), TX_HG_OUT_CELL,
                      outputs_grid_rect(OUTPUTS_GRID_CELL));
    if (outputs_unbind_offered()) {
        (void)ui_help_add(p, TR(HG_OUT_UNBIND_NAME), TX_HG_OUT_UNBIND,
                          outputs_unbind_rect());
    }
    (void)ui_help_add(p, TR(OUT_LAST_WRITE), TX_HG_OUT_LAST,
                      outputs_grid_rect(OUTPUTS_GRID_LAST));
}

bool outputs_help_page(int i, ui_help_page_t *out)
{
    if (out == NULL || i < 0 || i >= outputs_help_count()) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->armed_ok = true;
    out->side = (i == 0) ? UI_HELP_DOCK_RIGHT : UI_HELP_DOCK_LEFT;
    if (outputs_view_is_board()) {
        board_page(i, out);
    } else {
        grid_page(out);
    }
    return true;
}
