/**
 * @file
 * @brief The TARGET row of a workbench's output card: what the screen drives now,
 * read as `protocol pins` ("SERVO PWM GP2 GP4", "PROG PORT GP13"), and the
 * control that picks it.
 *
 * The row is a label on the left and a value box on the right, the shape
 * of a settings row: a tap on the box asks the screen to open its choice
 * of targets.  The row takes no press while it is locked -- ARMED, a run
 * or a sweep under way, a programmer holding the port: the screen says
 * when -- and it still names the target then.  It is drawn in the warning
 * colour when the screen says the target is none or not known.
 *
 * Pure layout, text and touch logic; no state outside the struct.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx.h"
#include "touch_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The label's share of a row under UI_TARGET_TALL_H; the value box
 *         takes the rest. */
#define UI_TARGET_LABEL_W 64

/**
 * @brief The row height from which the row draws its label and its value
 *        in the 10 x 20 face, 40 px (6.0 mm); a shorter row draws them in
 *        UI_FONT_LABEL (8 x 16).  A screen takes the larger face with the
 *        row height its own layout gives.
 */
#define UI_TARGET_TALL_H 40
/** @brief The label's share of a row UI_TARGET_TALL_H tall or taller:
 *         88 px, "TARGET" in 10 x 20 and 28 px clear. */
#define UI_TARGET_LABEL_W_TALL 88

/** @brief A TARGET row: where it is and the press it holds. */
typedef struct {
    gfx_rect_t rect;     /**< the row, label and box                */
    bool       down;     /**< a press on the value box is under way */
    uint8_t    id;       /**< its contact                           */
} ui_target_row_t;

/** @brief What a touch event was to the row. */
typedef enum {
    UI_TARGET_NONE = 0,  /**< not the row's                          */
    UI_TARGET_TAKEN,     /**< the row's press; nothing to do yet      */
    UI_TARGET_TAP,       /**< a tap on the value box: open the choice */
} ui_target_ev_t;

/**
 * @brief The row at @p row, with no press.
 * @param t   The row; NULL does nothing.
 * @param row Where it is: the label at the left, the box the rest.
 */
void ui_target_row_init(ui_target_row_t *t, gfx_rect_t row);

/**
 * @brief The value box: the row less the label's share.
 * @param t The row.
 * @return The row less UI_TARGET_LABEL_W px at its left, or
 *         UI_TARGET_LABEL_W_TALL from UI_TARGET_TALL_H on; an empty
 *         rectangle for NULL or a row no wider than the label.
 */
gfx_rect_t ui_target_row_value_rect(const ui_target_row_t *t);

/**
 * @brief The face the row draws its label and its value in.
 * @param t The row.
 * @return The 10 x 20 face from UI_TARGET_TALL_H on, UI_FONT_LABEL under
 *         it and for NULL.
 */
const gfx_font_t *ui_target_row_font(const ui_target_row_t *t);

/**
 * @brief How many cells of text the value box holds, in the row's face.
 * @param t The row.
 * @return The box's width less 8 px each side over the face's cell width;
 *         0 for NULL or a box narrower than the padding.
 */
unsigned ui_target_row_cells(const ui_target_row_t *t);

/**
 * @brief A touch event.
 *
 * A DOWN on the value box while not @p locked starts the row's press; its
 * UP is a tap when it lands on the box as a tap (touch_event_is_tap_up())
 * and the row is still not locked.  A DOWN while locked is taken and does
 * nothing, so no control under the row sees it.  A second contact does not
 * take over a press under way.
 * @param t      The row.
 * @param evt    The event.
 * @param locked The row takes no press now.
 * @return What the event was to the row; UI_TARGET_NONE for a NULL.
 */
ui_target_ev_t ui_target_row_event(ui_target_row_t *t,
                                   const touch_event_t *evt, bool locked);

/**
 * @brief Drop a press under way, as touch events went missing.
 * @param t The row; NULL does nothing.
 */
void ui_target_row_cancel(ui_target_row_t *t);

/**
 * @brief The row: @p label at the left, @p value centred in its box, both
 *        in ui_target_row_font().
 * @param t      The row.
 * @param c      Canvas.
 * @param label  The label, or NULL for none.
 * @param value  The value, or NULL for none.
 * @param warn   The label and the value in the warning colour.
 * @param locked The box as a control that takes no press, the value still
 *               legible.
 */
void ui_target_row_render(const ui_target_row_t *t, gfx_canvas_t *c,
                          const char *label, const char *value, bool warn,
                          bool locked);

/**
 * @brief `protocol pins` into @p out: "SERVO PWM GP2 GP4", with as many
 *        pins as fit after the protocol and a space, then "+N"
 *        (ui_pinlist()).
 *
 * With no pin, or no room for one after the protocol, the protocol alone;
 * with a NULL or empty @p protocol, the pins alone.
 * @param out      Where the text goes; NULL writes nothing.
 * @param size     The bytes at @p out; 0 writes nothing.
 * @param protocol The protocol's name.
 * @param pins     The pins, bit n for GPIO n.
 * @param cells    The most characters, ui_target_row_cells().
 * @return The length written.
 */
size_t ui_target_text(char *out, size_t size, const char *protocol,
                      uint64_t pins, unsigned cells);

#ifdef __cplusplus
}
#endif
