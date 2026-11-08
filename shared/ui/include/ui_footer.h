/**
 * @file
 * @brief The footer every function screen shares: three slots of 328 x 48px
 *        across the body's foot, 4px above the alert band.
 *
 * Left, the screen's name with a line in the 10 x 20 face under it: the pin
 * or port the screen drives now, in the warning colour while it drives
 * none, or no line.  Middle, the screen's one primary 2s hold (ARM,
 * OUTPUT ON); the screen draws it there with its own hold code and keeps
 * its colours.  Right, SETTINGS: a neutral button with SETUP's sliders
 * icon that opens the screen's settings list (ui_setlist.h), or nothing on
 * a screen with nothing to set.
 *
 * The slots are SETUP's strip columns (settings_screen_int.h): x 10, 348
 * and 686, body y 444 to 492, panel y 504 to 552.  The alert band covers
 * panel y 556 to 600.  A screen with a footer keeps its own controls above
 * UI_FOOTER_FLOOR, 8px over the footer.
 *
 * Stateless: rects, a hit test and two drawers.  The screen keeps its own
 * press on a slot.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "gfx.h"
#include "ui_layout.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The margin left of, between and right of the slots, px: 10. */
#define UI_FOOTER_M 10
/** @brief A slot's width, px: 328, a third of the body less the margins. */
#define UI_FOOTER_W ((UI_PANEL_W - 4 * UI_FOOTER_M) / 3)
/** @brief A slot's height, px: UI_BTN_H, 48 (7.2mm). */
#define UI_FOOTER_H UI_BTN_H
/** @brief The slots' top in the body, px: 444, 4px above the alert band. */
#define UI_FOOTER_Y (UI_BODY_H - UI_ALERT_H - 4 - UI_FOOTER_H)
/** @brief The lowest body row a screen's own controls reach, px: 436. */
#define UI_FOOTER_FLOOR (UI_FOOTER_Y - 8)

/** @brief The three slots, left to right. */
typedef enum {
    UI_FOOTER_NAME = 0, /**< the name and the line of the port it drives */
    UI_FOOTER_HOLD,     /**< the primary hold                            */
    UI_FOOTER_SETTINGS, /**< SETTINGS, or nothing                        */
    UI_FOOTER_SLOTS
} ui_footer_slot_t;

/**
 * @brief Slot @p slot's rectangle in the body.
 * @param slot The slot.
 * @return Its rectangle; an empty one at (0, 0) for a slot out of range.
 */
gfx_rect_t ui_footer_rect(ui_footer_slot_t slot);

/**
 * @brief The slot under a point of the body.
 * @param x The point's x, px.
 * @param y Its y, px.
 * @return The slot; -1 off every slot.
 */
int ui_footer_hit(int x, int y);

/**
 * @brief A name and its line in @p r: with no line the name in the head
 *        face (20 x 36) where it fits 16px short of the slot, else in the
 *        body face (12 x 24), centered in the slot's height; with a line the
 *        name in the body face at the slot's top and the line in the label
 *        face (10 x 20) under it.  16px from the slot's left edge.
 * @param c    The canvas.
 * @param r    The slot.
 * @param name The name.
 * @param line The line; NULL or "" for none.
 * @param warn Whether the line takes the warning colour: the screen drives
 *             no pin or port.
 */
void ui_footer_name(gfx_canvas_t *c, gfx_rect_t r, const char *name,
                    const char *line, bool warn);

/**
 * @brief SETTINGS / OPTIONEN in the right slot: the neutral fill, the
 *        sliders icon left of the label, both centered.
 * @param c       The canvas.
 * @param pressed Whether a finger is on it.
 * @param enabled Whether it takes a press; grey with a faint label when not.
 */
void ui_footer_settings(gfx_canvas_t *c, bool pressed, bool enabled);

/**
 * @brief The footer's ground: the background over the three slots and the
 *        gaps between them.
 * @param c The canvas.
 */
void ui_footer_clear(gfx_canvas_t *c);

#ifdef __cplusplus
}
#endif
