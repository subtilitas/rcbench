/**
 * @file
 * @brief SEARCH's state and geometry, shared by wl_search.c (the model,
 *        the touches and the screen's table) and wl_search_draw.c (the
 *        drawing, the guide and the Dial's focus items).  Private to those
 *        two files.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ui_help.h"
#include "ui_layout.h"
#include "wl_search.h"

/*
 * The body below the band, 1024 x 540px, everything over the alert band's
 * 44px and 4px above it.
 *
 * Left, the list: the scan's state line at y 12 in the body face (12 x
 * 24), 53 cells across 640px; then up to 8 rows of 640 x 48px at a 52px
 * pitch from y 44 on the choice list's ground, the last ending at y 456.
 *
 * Right, from x 680, 328px wide: the device's kind in the heading face
 * (16 x 28) at y 12, its link state in the label face (10 x 20) at y 52,
 * BOUND at y 88 and the bound device in the body face at y 112, SCAN AGAIN
 * 328 x 56px at y 156.
 *
 * The question: a box of 700 x 280px at (162, 100), the kind in its title,
 * the device's name at y 144, its address and RSSI at y 180, the bound one
 * it takes the place of at y 214 and the note at y 244; CANCEL and BIND
 * 300 x 56px at y 304, CANCEL on the left.
 */
#define WLS_LX      16   /**< the rows' left edge, px */
#define WLS_LW      640  /**< a row's width, px */
#define WLS_LH      48   /**< a row's height, px */
#define WLS_LP      52   /**< the rows' pitch, px */
#define WLS_LY      44   /**< the first row's top, px */
#define WLS_HEAD_Y  12   /**< the state line's top, px */
#define WLS_SX      680  /**< the right column's left edge, px */
#define WLS_SW      328  /**< its width, px */
#define WLS_KIND_Y  12   /**< the kind's top, px */
#define WLS_WORD_Y  52   /**< the link state's top, px */
#define WLS_BOUND_Y 88   /**< BOUND's top, px */
#define WLS_NAME_Y  112  /**< the bound device's top, px */
#define WLS_AGAIN_Y 156  /**< SCAN AGAIN's top, px */
#define WLS_BTN_H   56   /**< SCAN AGAIN's and the question's buttons'
                              height, px */
#define WLS_BOX_X   162  /**< the question's left edge, px */
#define WLS_BOX_Y   100  /**< its top, px */
#define WLS_BOX_W   700  /**< its width, px */
#define WLS_BOX_H   280  /**< its height, px */
#define WLS_Q_PAD   24   /**< its text's inset from its left edge, px */
#define WLS_Q_NAME_Y  44 /**< the name's top in the box, px */
#define WLS_Q_ADDR_Y  80 /**< the address' top in the box, px */
#define WLS_Q_WAS_Y   114 /**< the replaced device's top in the box, px */
#define WLS_Q_NOTE_Y  144 /**< the note's top in the box, px */
#define WLS_Q_BTN_Y   304 /**< the buttons' top, px */
#define WLS_Q_BTN_W   300 /**< their width, px */
#define WLS_Q_CANCEL_X 186 /**< CANCEL's left edge, px */
#define WLS_Q_BIND_X  538 /**< BIND's left edge, px */
/** @brief The body row 4px above the alert band: every control ends over
 *         it. */
#define WLS_FLOOR   (UI_ALERT_Y - UI_BAND_H - 4)

_Static_assert(WLS_LY + (WLS_FOUND_MAX - 1) * WLS_LP + WLS_LH <= WLS_FLOOR,
               "the eighth row ends over the alert band");
_Static_assert(WLS_Q_BTN_Y + WLS_BTN_H <= WLS_BOX_Y + WLS_BOX_H
               && WLS_BOX_Y + WLS_BOX_H <= WLS_FLOOR,
               "the question's buttons sit in its box, over the alert band");

/** @brief What a press landed on: a control, or WLS_HIT_DEV0 + a row. */
enum {
    WLS_HIT_NONE = -1,  /**< nothing that takes a press */
    WLS_HIT_AGAIN = 0,  /**< SCAN AGAIN */
    WLS_HIT_CANCEL,     /**< the question's CANCEL */
    WLS_HIT_BIND,       /**< the question's BIND */
    WLS_HIT_DEV0 = 100, /**< found device 0; device i is WLS_HIT_DEV0 + i */
};

/** @brief The screen's state. */
typedef struct {
    const wls_device_t *dev;     /**< the device shown, NULL none */
    bool       asking;           /**< the bind question is open */
    uint8_t    ask_i;            /**< the row its tap took */
    wls_dev_t  ask;              /**< the device as its tap took it */
    bool       have_press;       /**< a contact pressed a control */
    uint8_t    press_id;         /**< that contact */
    int        hit;              /**< WLS_HIT_* */
    char       press_addr[WLS_ADDR_MAX]; /**< the address of the row the
                                              press took, for its UP */
    unsigned   drawn_mask;       /**< framebuffers drawn since the last
                                      event */
    uint32_t   drawn_key[2];     /**< the view as each framebuffer last
                                      drew it, as one number */
} wls_state_t;

/** @brief The state, wl_search.c's. */
extern wls_state_t wls;

/**
 * @brief The screen drawn into framebuffer @p buffer_index: whole after an
 *        event or a view that changed, else nothing.
 * @param c            The canvas.
 * @param buffer_index The framebuffer.
 */
void wls_render(gfx_canvas_t *c, int buffer_index);

/**
 * @brief The guide's pages: the list, then the card and SCAN AGAIN; none
 *        while the question is open.
 * @return The count, 0 or 2.
 */
int wls_help_count(void);

/**
 * @brief Page @p i of the guide.
 * @param i   The page, from 0.
 * @param out The page, filled.
 * @return False past the last page.
 */
bool wls_help_page(int i, ui_help_page_t *out);
