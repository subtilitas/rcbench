/**
 * @file
 * @brief The focus layer: the M5Stack Dial navigates the panel's lists.
 *
 * A screen that offers focus names its focusable items for what it shows
 * now (ui_focus_collect()): each item's rectangle, the point a SELECT taps
 * and a label for the Dial's NAV face.  The layer keeps the focused index,
 * moves it by the Dial's detents and wraps at either end, and draws a 2px
 * ring in the accent colour round the focused item on the panel.
 *
 * Every SELECT is a synthetic tap: a DOWN and an UP of touch id
 * UI_FOCUS_TOUCH_ID at the item's tap point, handed to ui_router_event() in
 * one call, after the focused row is scrolled into view and its geometry
 * collected again; a tap point outside the item or out of sight is never
 * tapped.  No tick runs between the two, so no 2s hold on the panel
 * completes from the Dial: a hold button answers it with the hint it shows
 * for any tap.  A HOLD on the Dial is BACK: what a tap on the home tag does
 * (ui_router_home()).
 *
 * SET: a number the keypad takes is set by turning.
 *
 * - A SELECT on its row taps the value, which opens the row's keypad.  While
 *   a keypad is open the turn moves the value by the setting's step within
 *   its range and writes it into the entry, one detent at a time: from OFF
 *   one detent reaches the lowest value on (off_below).  PRESS taps OK,
 *   HOLD taps CANCEL.
 *   The keypad's OK takes the value as typed, refusal and all.
 * - A setting row without a keypad (an on/off or a list of options) enters
 *   step SET on SELECT: each detent taps its "-" or "+" key, PRESS keeps,
 *   HOLD taps the other key back as often as a tap changed the value.
 *
 * The layer shows nothing and takes nothing (ui_focus_shown() false):
 *
 * - while the Dial is OFF or not linked (ui_focus_set_link());
 * - while the bench is ARMED: the Dial then drives the output on a bench
 *   screen as before, and nothing moves a focus away from it;
 * - while the screen guide or the screenshot popup is open;
 * - on a screen that offers no focus, or offers none in its present state.
 *
 * The ring costs no repaint of the screen: the pixels under it are kept per
 * frame buffer and put back where the ring still shows when it moves.  A
 * pixel the screen painted over since is the screen's and stays.
 *
 * Pure C; the panel's frame (firmware/panel/main/panel_render.c) and the
 * router (ui_router.c) call it.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx.h"
#include "settings.h"
#include "ui_keypad.h"
#include "ui_layout.h"
#include "ui_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Bytes of an item's label, the terminator included.  The Dial
 *         shows the first DK_ITEM_MAX (15) bytes, cut at a character. */
#define UI_FOCUS_LABEL 32
/** @brief The most items one list names; rows past it take no focus. */
#define UI_FOCUS_MAX 64
/** @brief The touch id of the Dial's synthetic taps: above the GT911's 0 to
 *         4 and the touch injector's ids, so no finger shares it. */
#define UI_FOCUS_TOUCH_ID 0xD1u
/** @brief The ring's width, px. */
#define UI_FOCUS_RING 2
/** @brief The frame buffers the ring keeps pixels for. */
#define UI_FOCUS_FBS 3
/** @brief The pixels one ring covers at most: 2px round the whole body. */
#define UI_FOCUS_RING_PX (2 * UI_FOCUS_RING * (UI_PANEL_W + UI_BODY_H))
/** @brief The most detents one turn applies in step SET: a turn of the
 *         Dial is 16 detents. */
#define UI_FOCUS_STEPS_MAX 16

/** @brief The most detents one turn moves a keypad's value, one by one. */
#define UI_FOCUS_KP_DETENTS_MAX 256

/** @brief What a SELECT on an item does. */
typedef enum {
    UI_FOCUS_TAP = 0, /**< a synthetic tap at the tap point */
    UI_FOCUS_STEP,    /**< step SET on a setting without a keypad */
} ui_focus_kind_t;

/** @brief One focusable item of a screen, in the screen's coordinates. */
typedef struct {
    gfx_rect_t   rect;   /**< the ring goes round it */
    int16_t      tap_x;  /**< where a SELECT taps, x */
    int16_t      tap_y;  /**< where a SELECT taps, y */
    uint8_t      kind;   /**< ui_focus_kind_t */
    int16_t      row;    /**< its row of the list's ui_setlist_t, or -1 */
    setting_id_t id;     /**< UI_FOCUS_STEP: the setting */
    gfx_rect_t   minus;  /**< UI_FOCUS_STEP: its "-" key */
    gfx_rect_t   plus;   /**< UI_FOCUS_STEP: its "+" key */
    gfx_rect_t   clip;   /**< where its ring may draw; empty for the body */
    char         label[UI_FOCUS_LABEL]; /**< the Dial's line for it */
} ui_focus_item_t;

struct ui_setlist;

/** @brief What a screen offers now: its items, or an open keypad. */
typedef struct {
    uint32_t           list;   /**< which list: a change starts the focus
                                    on start again */
    int                count;  /**< items in item[] */
    int                start;  /**< the item a new list starts on */
    struct ui_setlist *sl;     /**< the list whose rows the items' row
                                    names, scrolled to the focus; NULL */
    ui_keypad_t       *keypad; /**< an open keypad: SET; NULL none */
    float              step;   /**< the keypad's step; 0 is one unit of
                                    its last decimal */
    float              off_below; /**< the keypad's setting takes no value
                                       between 0 and this; 0 none */
    ui_focus_item_t    item[UI_FOCUS_MAX]; /**< the items */
} ui_focus_list_t;

/** @brief A value the Dial sets, for its VALUE face. */
typedef struct {
    const char *name;     /**< the setting's name */
    const char *unit;     /**< its unit; "" none */
    int         decimals; /**< digits after the point */
    float       min;      /**< the lowest value */
    float       max;      /**< the highest value */
    float       step;     /**< one detent */
    float       value;    /**< the value now */
    float       was;      /**< the value on entry to SET */
    bool        limit;    /**< the last turn stopped at an end */
} ui_focus_value_t;

/**
 * @brief The Dial's state as this frame sees it.  Call once a frame before
 *        the input and the model; the tests call it to stand in for the
 *        Dial.
 * @param linked The Dial is ON and its link is up or degraded.
 * @param armed  The bench is ARMED, or an arm is under way.
 */
void ui_focus_set_link(bool linked, bool armed);

/** @brief Forget the focus, SET and the kept pixels: a start of the
 *         screens (ui_router_init()). */
void ui_focus_reset(void);

/**
 * @brief Whether the layer navigates the screen on top now: linked, SAFE,
 *        no guide or popup, and a screen that offers focus or an open
 *        keypad.  Collects the screen's items.
 * @return true while the Dial's turn, PRESS and HOLD are the layer's.
 */
bool ui_focus_shown(void);

/**
 * @brief A turn: the focus moves @p detents items and wraps; in SET the
 *        value moves.
 * @param detents Clockwise positive.
 * @return true when the layer took it.
 */
bool ui_focus_turn(int detents);

/**
 * @brief PRESS: SELECT on the focused item; in SET, OK or keep.
 * @return true when the layer took it.
 */
bool ui_focus_press(void);

/**
 * @brief HOLD: BACK; in SET, CANCEL.
 * @return true when the layer took it.
 */
bool ui_focus_hold(void);

/** @brief The focused item, 0 for none; looks at the screen first.
 *  @return its index */
int ui_focus_index(void);

/** @brief The items of the screen's list; looks at the screen first.
 *  @return their count, 0 none */
int ui_focus_count(void);

/**
 * @brief An item's label, as the last look found it (ui_focus_index(),
 *        ui_focus_count(), ui_focus_shown()).
 * @param i The item.
 * @return The label; "" past either end.
 */
const char *ui_focus_label(int i);

/** @brief Where a SELECT taps, in the screen's coordinates. */
typedef struct {
    int16_t    x;    /**< the tap point's x */
    int16_t    y;    /**< its y */
    gfx_rect_t rect; /**< the focused item's rectangle */
    gfx_rect_t seen; /**< where the item can be seen: its pane, in the body */
} ui_focus_target_t;

/**
 * @brief The focused item's tap point, the item brought into view first as
 *        a SELECT brings it: a list's row scrolled to and the items
 *        collected again.  A SELECT taps only a point inside the item's
 *        rectangle and inside where it can be seen; where the row cannot
 *        be brought into view it taps nothing.
 * @param out Receives it.
 * @return false while nothing would be tapped: no focus, SET, or the item
 *         out of sight.
 */
bool ui_focus_target(ui_focus_target_t *out);

/**
 * @brief The value SET moves, while a keypad is open or step SET runs.
 * @param out Receives it.
 * @return false while nothing is set.
 */
bool ui_focus_value(ui_focus_value_t *out);

/**
 * @brief Before the screen draws: the layer watches what it paints.
 * @param body The body canvas the screen draws into.
 */
void ui_focus_render_begin(const gfx_canvas_t *body);

/**
 * @brief After the screen drew into @p body: the ring put back where it
 *        moved from and drawn where the focus is.
 * @param body         The body canvas.
 * @param buffer_index The frame buffer.
 */
void ui_focus_render_end(const gfx_canvas_t *body, int buffer_index);

/**
 * @brief The pixels the ring wrote in the last ui_focus_render_end(), for
 *        the frame-cost tests.
 * @return The count.
 */
uint32_t ui_focus_ring_writes(void);

/**
 * @brief The items of screen @p route as it shows now (ui_focus_screens.c).
 * @param route The screen.
 * @param out   Receives them; count 0 and no keypad leave no focus.
 * @return false where the screen offers none now.
 */
bool ui_focus_collect(ui_screen_id_t route, ui_focus_list_t *out);

/**
 * @brief Add the rows of @p sl and its strip, or what covers it: the open
 *        choice list, the leave warning's three buttons, or its keypad.
 * @param out The list.
 * @param sl  The setting list, open.
 * @return false while its keyboard is open: no focus then.
 */
bool ui_focus_add_setlist(ui_focus_list_t *out, struct ui_setlist *sl);

/**
 * @brief Add one item.
 * @param out   The list; past UI_FOCUS_MAX nothing is added.
 * @param rect  Its rectangle; the tap goes to its middle.
 * @param label Its label; NULL is "".
 * @return The item, or NULL when the list is full.
 */
ui_focus_item_t *ui_focus_add(ui_focus_list_t *out, gfx_rect_t rect,
                              const char *label);

#ifdef __cplusplus
}
#endif
