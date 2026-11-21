/**
 * @file
 * @brief LOAD SETTINGS' state and geometry, shared by sbk_screen.c (the
 *        state, the touches and the screen's table) and sbk_draw.c (the
 *        drawing).  Private to those two files.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gfx.h"
#include "sbk_screen.h"
#include "ui_layout.h"
#include "ui_widgets.h"

/*
 * The body below the band, 1024 x 540px; every control ends 4px above the
 * alert band.
 *
 * Left, from x 16, 640px wide: the head line at y 12 in the body face
 * (12 x 24), 53 cells; up to 4 file rows of 640 x 64px at a 72px pitch from
 * y 48, the last ending at y 328; the notes from y 344 in the label face
 * (10 x 20), 64 cells, 24px a line.  The answer of a load takes the same
 * column: its head line at y 12, its lines from y 48 at a 26px pitch.
 *
 * Right, from x 680, 328px wide: the chosen file's name in the heading
 * face (16 x 28) at y 12; WRITTEN at y 56 and its date at y 80; PANEL
 * VERSION at y 116 and the version at y 140; why the hold takes no press
 * from y 180; HOLD 2s TO LOAD 328 x 64px at y 336; BACK or SKIP 328 x 56px
 * at y 416.
 */
#define SBK_LX       16   /**< the left column's edge, px */
#define SBK_LW       640  /**< its width, px */
#define SBK_HEAD_Y   12   /**< the head line's top, px */
#define SBK_ROW_Y    48   /**< the first file row's top, px */
#define SBK_ROW_H    64   /**< a file row's height, px */
#define SBK_ROW_P    72   /**< the rows' pitch, px */
#define SBK_NOTE_Y   344  /**< the notes' top, px */
#define SBK_NOTE_P   24   /**< the notes' pitch, px */
#define SBK_RES_P    26   /**< the answer's line pitch, px */
#define SBK_RES_LINES 16  /**< the answer's lines under its head */
#define SBK_SX       680  /**< the right column's edge, px */
#define SBK_SW       328  /**< its width, px */
#define SBK_NAME_Y   12   /**< the chosen name's top, px */
#define SBK_WRIT_Y   56   /**< WRITTEN's top, px */
#define SBK_DATE_Y   80   /**< the date's top, px */
#define SBK_PANEL_Y  116  /**< PANEL VERSION's top, px */
#define SBK_VER_Y    140  /**< the version's top, px */
#define SBK_WHY_Y    180  /**< the hold's reason's top, px */
#define SBK_HOLD_Y   336  /**< HOLD 2s TO LOAD's top, px */
#define SBK_HOLD_H   64   /**< its height, px */
#define SBK_BACK_Y   416  /**< BACK's top, px */
#define SBK_BACK_H   56   /**< its height, px */
/** @brief The body row 4px above the alert band: every control ends over
 *         it. */
#define SBK_FLOOR    (UI_ALERT_Y - UI_BAND_H - 4)

_Static_assert(SBK_ROW_Y + (SBK_ROWS - 1) * SBK_ROW_P + SBK_ROW_H
               < SBK_NOTE_Y, "the notes start under the fourth row");
_Static_assert(SBK_BACK_Y + SBK_BACK_H <= SBK_FLOOR,
               "BACK ends over the alert band");
_Static_assert(SBK_ROW_Y + SBK_RES_LINES * SBK_RES_P <= SBK_FLOOR,
               "the answer's lines end over the alert band");
_Static_assert(SBK_HOLD_H >= UI_TAP_MIN && SBK_BACK_H >= UI_TAP_MIN
               && SBK_ROW_H >= UI_TAP_MIN, "every control is a tap target");

/** @brief What a press landed on: a control, or SBK_HIT_ROW0 + a row. */
enum {
    SBK_HIT_NONE = -1,  /**< nothing that takes a press */
    SBK_HIT_HOLD = 0,   /**< HOLD 2s TO LOAD */
    SBK_HIT_BACK,       /**< BACK or SKIP */
    SBK_HIT_ROW0,       /**< the first file row */
};

/** @brief The screen's state. */
typedef struct {
    bool      offer;        /**< the restore offer shows                */
    int       chosen;       /**< the file chosen, -1 none               */
    int       hit;          /**< the control the press began on         */
    uint8_t   press_id;     /**< the press's contact                    */
    bool      have_press;   /**< a press is on a control                */
    ui_hold_t hold;         /**< HOLD 2s TO LOAD                        */
    unsigned  drawn_mask;   /**< the buffers that hold the current frame */
    int       drawn_why;    /**< sbk_hold_why() as the last tick saw it */
} sbk_view_t;

/** @brief The screen's one instance, in sbk_screen.c. */
extern sbk_view_t sbk_v;

/**
 * @brief The screen drawn whole once per framebuffer and again after every
 *        change and through the hold's flash.
 * @param c            Canvas, the body.
 * @param buffer_index The framebuffer drawn into.
 */
void sbk_render(gfx_canvas_t *c, int buffer_index);

/**
 * @brief Why HOLD 2s TO LOAD takes no press now.
 * @return The reason's ui_text_id_t; TX_COUNT while it takes one.
 */
int sbk_hold_why(void);
