/**
 * @file
 * @brief The screen guide: numbered marks on a screen's controls and a dock
 *        that says what each one does.
 *
 * The "?" chip of the band opens the guide of the screen on top.  A page
 * marks up to UI_HELP_ITEMS controls with a numbered badge and 2px
 * brackets at their corners and lists them in a dock of 480 x 488px on one
 * half of the body.
 * The other half is the screen's, drawn live.  PREV and NEXT step the
 * pages, CLOSE and a tap anywhere else close the guide.  The router owns
 * the guide's state and touches (ui_router.c); this file holds its pages'
 * shape, its geometry and its drawing.
 *
 * A screen gives its pages through ui_screen_t's help_count and help_page.
 * A page is data: a title, the side the dock takes, the items, whether it
 * may show while the bench is ARMED, and a rect the dock must leave in
 * view.  Item rects are the screen's own coordinates, as its layout
 * functions return them; a rect above y 0 is in the band.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gfx.h"
#include "ui_screen.h"
#include "ui_text.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The most items a page marks: 5. */
#define UI_HELP_ITEMS 5

/** @brief The most rows of the dock's text an item takes: 3. */
#define UI_HELP_ROWS 3

/** @brief The side of the body the dock takes. */
typedef enum {
    UI_HELP_DOCK_LEFT = 0,  /**< x 8 to 488; the screen draws from x 496  */
    UI_HELP_DOCK_RIGHT,     /**< x 536 to 1016; the screen to x 528       */
} ui_help_side_t;

/** @brief One control a page marks. */
typedef struct {
    /** @brief The control's name as the screen draws it; English where
     *         the control's label is. */
    const char  *label;
    ui_text_id_t text;      /**< what it does, in the language showing    */
    /** @brief Where it is, in the screen's coordinates; y below 0 is the
     *         band. */
    gfx_rect_t   rect;
} ui_help_item_t;

/** @brief One page of a screen's guide. */
typedef struct ui_help_page {
    ui_text_id_t   title;       /**< the dock's header                   */
    ui_help_side_t side;        /**< the half the dock takes             */
    /** @brief Shown while the bench is ARMED: the dock covers no control
     *         that stops an output and no must_show rect. */
    bool           armed_ok;
    uint8_t        count;       /**< items, 1 to UI_HELP_ITEMS            */
    ui_help_item_t item[UI_HELP_ITEMS]; /**< the items, by their number  */
    /** @brief A rect the dock leaves in view, in the screen's
     *         coordinates, such as the TARGET row that names the pin an
     *         armed screen drives; w 0 for none. */
    gfx_rect_t     must_show;
} ui_help_page_t;

/**
 * @brief The pages of @p scr the guide offers for the bench state.
 *
 * Every page while SAFE; while ARMED only those marked armed_ok.
 * @param scr   The screen, or NULL.
 * @param armed The bench is ARMED.
 * @return The count, 0 for a screen without pages in this state.
 */
int ui_help_count(const ui_screen_t *scr, bool armed);

/**
 * @brief Page @p n of those ui_help_count() offers.
 * @param scr   The screen.
 * @param armed The bench is ARMED.
 * @param n     The page, from 0.
 * @param out   The page, filled.
 * @return False past the last page or for a screen without pages.
 */
bool ui_help_page(const ui_screen_t *scr, bool armed, int n,
                  ui_help_page_t *out);

/**
 * @brief Whether the first page says that ARMED left pages out: the bench
 *        is ARMED and the screen has pages that are not armed_ok.
 * @param scr   The screen.
 * @param armed The bench is ARMED.
 * @return True when the note shows.
 */
bool ui_help_armed_note(const ui_screen_t *scr, bool armed);

/**
 * @brief A number for the page set ui_help_count() offers: the count and
 *        each page's title.  The router closes the guide when it moves.
 * @param scr   The screen.
 * @param armed The bench is ARMED.
 * @return A 32-bit FNV-1a hash.
 */
uint32_t ui_help_set_key(const ui_screen_t *scr, bool armed);

/**
 * @brief The dock of a page on @p side, in panel coordinates: 480 x 488px
 *        from y 64, 8px in from the panel's edge.
 * @param side The side.
 * @return The rect.
 */
gfx_rect_t ui_help_dock_rect(ui_help_side_t side);

/**
 * @brief The half of the body the screen draws while the dock is on
 *        @p side, in panel coordinates: x 496 to 1024 or 0 to 528, the
 *        body's height.
 * @param side The dock's side.
 * @return The rect.
 */
gfx_rect_t ui_help_live_rect(ui_help_side_t side);

/** @brief What a point of the guide is. */
typedef enum {
    UI_HELP_HIT_NONE = 0,   /**< not the guide's                          */
    UI_HELP_HIT_DOCK,       /**< the dock, or a badge                     */
    UI_HELP_HIT_PREV,       /**< PREV                                     */
    UI_HELP_HIT_NEXT,       /**< NEXT                                     */
    UI_HELP_HIT_CLOSE,      /**< CLOSE                                    */
} ui_help_hit_t;

/**
 * @brief The PREV, NEXT or CLOSE button of a dock on @p side, in panel
 *        coordinates: 140 x 48px each, 10px apart, from y 496.
 * @param side The dock's side.
 * @param hit  UI_HELP_HIT_PREV, _NEXT or _CLOSE.
 * @return The rect; empty for another @p hit.
 */
gfx_rect_t ui_help_button_rect(ui_help_side_t side, ui_help_hit_t hit);

/**
 * @brief The badge of item @p i of @p p, in panel coordinates: 28 x 28px,
 *        4px inside the control's top-left corner; 4px left of a control
 *        less than 56px tall or wide, centred on it, where the live half
 *        has the room; under the band below the left end of a control in
 *        the band.
 * @param p The page.
 * @param i The item.
 * @return The badge's rect.
 */
gfx_rect_t ui_help_badge_rect(const ui_help_page_t *p, int i);

/**
 * @brief What the point (@p x, @p y), in panel coordinates, is on a guide
 *        showing @p p: a button, the dock or a badge, or none.
 * @param p The page.
 * @param x The point's x.
 * @param y The point's y.
 * @return The part.
 */
ui_help_hit_t ui_help_hit(const ui_help_page_t *p, int x, int y);

/**
 * @brief The rows item @p i takes in the dock: its label, two spaces and
 *        its text, broken at spaces.
 * @param p The page.
 * @param i The item.
 * @return The rows, at most UI_HELP_ROWS on every page that passes
 *         test_help_pages.
 */
int ui_help_item_rows(const ui_help_page_t *p, int i);

/**
 * @brief Where the dock's text of @p p ends, in panel y: the header, the
 *        ARMED note when @p note, and every item.  The web line and the
 *        buttons are under it.
 * @param p    The page.
 * @param note The ARMED note shows.
 * @return The y of the last row's bottom.
 */
int ui_help_text_bottom(const ui_help_page_t *p, bool note);

/**
 * @brief The y the dock's items end by at the latest, in panel
 *        coordinates: above the web line.
 * @return The y.
 */
int ui_help_text_limit(void);

/**
 * @brief A number for what ui_help_render_dock() draws from the same
 *        arguments, the language and the theme in force.
 * @param p     The page.
 * @param n     Its number, from 0.
 * @param count The pages.
 * @param note  The ARMED note shows.
 * @param btn   The button held down, or UI_HELP_HIT_NONE.
 * @return A 32-bit FNV-1a hash.
 */
uint32_t ui_help_dock_key(const ui_help_page_t *p, int n, int count,
                          bool note, ui_help_hit_t btn);

/**
 * @brief Paint the half of the body the dock takes: the background, the
 *        dock with its header, items, web line and buttons.
 * @param c     The panel's canvas.
 * @param p     The page.
 * @param n     Its number, from 0.
 * @param count The pages.
 * @param note  The ARMED note shows.
 * @param btn   The button held down, or UI_HELP_HIT_NONE.
 */
void ui_help_render_dock(gfx_canvas_t *c, const ui_help_page_t *p, int n,
                         int count, bool note, ui_help_hit_t btn);

/**
 * @brief Draw the corner brackets and the numbered badges of @p p's items
 *        over the screen, clipped to the band and the live half.  Drawn
 *        every frame, as the screen draws under them: brackets, not a
 *        full outline: a 2px outline's sides cost a cache line a row,
 *        and four tiles' outlines cost 4,370 lines a frame in
 *        frame_cost.py's model.
 * @param c The panel's canvas.
 * @param p The page.
 */
void ui_help_render_marks(gfx_canvas_t *c, const ui_help_page_t *p);

#ifdef __cplusplus
}
#endif
