/**
 * @file outputs_view.c
 * @brief The router's OUTPUTS: one screen with two views of one choice.
 *
 * The board view (picker_screen.c) is what a board with a drawing or a shape
 * gets: the board is the pin control.  The grid view (outputs_screen.c) is
 * what a board with neither gets, and the rbmod carrier, whose pins are
 * soldered and have no pin to pick.  outputs_screen.c holds the selection,
 * the reading and the last write for both, so a view changes nothing when a
 * board answers or goes during a visit: only a press under way is dropped.
 *
 * See outputs_screen.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "outputs_screen.h"

#include "picker_screen.h"
#include "ui_board.h"

#include "outputs_screen_int.h"

/** @brief The board view is the one drawn. */
static bool s_board;

/**
 * @brief The view of the board picker_screen.c holds.
 * @return true for the board view: it can be drawn, and its pins are not
 *         soldered
 */
static bool board_view_wanted(void)
{
    const outbind_board_t *bd = outbind_board(picker_screen_binding()->board);
    return picker_screen_can_draw() && ui_board_roles() == NULL
           && (bd == NULL || !bd->fixed);
}

/** @brief Moves to the view wanted, dropping a press on the one left. */
static void view_sync(void)
{
    const bool want = board_view_wanted();
    if (want != s_board) {
        (s_board ? picker_screen() : outputs_screen())->cancel();
        s_board = want;
        outputs_screen_set_board_view(want);
        outputs_screen_invalidate();
        picker_screen_invalidate();
    }
}

bool outputs_view_is_board(void)
{
    view_sync();
    return s_board;
}

/** @brief Forgets both views; the board view selects through the grid's. */
static void reset(void)
{
    outputs_screen()->reset();
    picker_screen()->reset();
    picker_screen_set_select(outputs_screen_select);
    s_board = false;
}

/**
 * @brief A visit begins, on the overview or on what a TARGET row asked for
 *        (outputs_screen_open_on()), in the view the board wants.
 *
 * The board view's enter selects OFF, so the grid's comes after it and
 * applies what was asked; the board view follows it before the first frame,
 * so that frame and the cat picked on it show the entry opened on.
 */
static void enter(void)
{
    picker_screen_set_binding(outputs_screen_binding());
    picker_screen()->enter();
    outputs_screen()->enter();
    picker_screen_follow(outputs_screen_binding()->proto,
                         outputs_screen_editable());
    view_sync();
    outputs_screen_invalidate();
}

/** @brief The screen is left: no press and no list in either view. */
static void leave(void)
{
    picker_screen()->leave();
    outputs_screen()->leave();
}

/**
 * @brief Once a frame: the view, the board view's selection, the hold on
 *        UNBIND ALL PINS and the entry's locks.
 * @param dt_s seconds since the last tick
 */
static void tick(float dt_s)
{
    view_sync();
    picker_screen_follow(outputs_screen_binding()->proto,
                         outputs_screen_editable());
    outputs_screen()->tick(dt_s);
}

/**
 * @brief A touch event, for the view drawn.
 * @param evt the event
 */
static void event(const touch_event_t *evt)
{
    view_sync();
    (s_board ? picker_screen() : outputs_screen())->event(evt);
}

/** @brief Touch events were lost: both views forget the press under way. */
static void cancel(void)
{
    picker_screen()->cancel();
    outputs_screen()->cancel();
}

/**
 * @brief Draws the view wanted.
 * @param c the screen's canvas
 * @param buffer_index the frame buffer
 */
static void render(gfx_canvas_t *c, int buffer_index)
{
    view_sync();
    (s_board ? picker_screen() : outputs_screen())->render(c, buffer_index);
}

/**
 * @brief The home tag: the view drawn closes what it opened.
 * @return true when it closed something, false to leave the screen
 */
static bool back(void)
{
    return (s_board ? picker_screen() : outputs_screen())->back();
}

/**
 * @brief ui_screen_t's covered: what the view drawn has up.
 * @return true while one is up
 */
static bool covered(void)
{
    return (s_board ? picker_screen() : outputs_screen())->covered();
}

/** @brief The screen's hooks for the router. */
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

const ui_screen_t *outputs_view_screen(void) { return &s_screen; }
