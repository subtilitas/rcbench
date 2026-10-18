/*
 * The TARGET row of a workbench's output card: what the screen drives now,
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

/** The label's share of the row; the value box takes the rest. */
#define UI_TARGET_LABEL_W 64

typedef struct {
    gfx_rect_t rect;
    bool       down;     /**< a press on the value box is under way */
    uint8_t    id;       /**< its contact                           */
} ui_target_row_t;

/** What a touch event was to the row. */
typedef enum {
    UI_TARGET_NONE = 0,  /**< not the row's                          */
    UI_TARGET_TAKEN,     /**< the row's press; nothing to do yet      */
    UI_TARGET_TAP,       /**< a tap on the value box: open the choice */
} ui_target_ev_t;

void ui_target_row_init(ui_target_row_t *t, gfx_rect_t row);

/** The value box: the row less the label's UI_TARGET_LABEL_W px. */
gfx_rect_t ui_target_row_value_rect(const ui_target_row_t *t);

/** How many 8 px cells of text the value box holds. */
unsigned ui_target_row_cells(const ui_target_row_t *t);

/**
 * A touch event.  A DOWN on the value box while not @p locked starts the
 * row's press; its UP is a tap when it lands on the box as a tap
 * (touch_event_is_tap_up()) and the row is still not locked.  A DOWN while
 * locked is taken and does nothing, so no control under the row sees it.
 * A second contact does not take over a press under way.
 */
ui_target_ev_t ui_target_row_event(ui_target_row_t *t,
                                   const touch_event_t *evt, bool locked);

/** Drop a press under way, as touch events went missing. */
void ui_target_row_cancel(ui_target_row_t *t);

/**
 * The row: @p label at the left, @p value centred in its box.  @p warn
 * draws the label and the value in the warning colour; @p locked draws the
 * box as a control that takes no press, the value still legible.
 */
void ui_target_row_render(const ui_target_row_t *t, gfx_canvas_t *c,
                          const char *label, const char *value, bool warn,
                          bool locked);

/**
 * `protocol pins` into @p out of @p size bytes, at most @p cells
 * characters: "SERVO PWM GP2 GP4", with as many pins as fit after the
 * protocol and a space, then "+N" (ui_pinlist()).  With no pin, or no room
 * for one after the protocol, the protocol alone; with a NULL or empty
 * @p protocol, the pins alone.  A NULL @p out or a @p size of 0 writes
 * nothing.  Returns the length written.
 */
size_t ui_target_text(char *out, size_t size, const char *protocol,
                      uint64_t pins, unsigned cells);

#ifdef __cplusplus
}
#endif
