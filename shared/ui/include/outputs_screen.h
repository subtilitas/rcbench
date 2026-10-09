/*
 * The outputs screen: choose a protocol, then tick the pins it drives.
 *
 * One protocol and a set of pins rather than eight independent slots, because
 * that is the shape of the job in front of the operator -- four servo leads,
 * or one ESC (electronic speed controller).  out_bind.c turns the choice into
 * the OUTPUTS and CHAN_CFG pages; this file is only the touching.
 *
 * The screen writes nothing to the wire.  It calls the apply function it was
 * given whenever the binding changes, and the application does the writing,
 * so the whole screen renders and is driven on the host with no link at all.
 * Which protocol is selected is this screen's own and is not a change: a
 * pick in the list calls nothing.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "bind_link.h"
#include "out_bind.h"
#include "ui_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

const ui_screen_t *outputs_screen(void);

/** Repaint the cached chrome in every framebuffer. */
void outputs_screen_invalidate(void);

/** The binding shown: the last one read, plus edits not yet read back. */
const outbind_t *outputs_screen_binding(void);

/**
 * A binding read from the coprocessor, or one the screen is posed with.
 * The screen then holds a confirmed binding and takes edits.
 */
void outputs_screen_set_binding(const outbind_t *b);

/**
 * What a read of the two output pages gave (bind_link_read()).
 *
 * BIND_READ_OK is outputs_screen_set_binding() of the binding read.
 * BIND_READ_NONE and BIND_READ_ODD leave the screen on the last binding a
 * read confirmed, with any edit made since taken back, drawn dimmed with
 * the reason under the protocol; no pin can be ticked or unticked until a
 * read is BIND_READ_OK again.  A reading for another board than the one
 * shown has no last binding to keep and shows that board with nothing
 * bound.  BIND_READ_NO_BOARD shows no pins.
 */
void outputs_screen_set_reading(const bind_reading_t *r);

/** The state of the last reading; BIND_READ_OK after a set_binding(). */
bind_read_t outputs_screen_read_state(void);

/** Whether a tap on a pin can change the binding: the last reading was
 *  BIND_READ_OK. */
bool outputs_screen_editable(void);

/**
 * Called with the binding to write after a pin is ticked or unticked, and
 * with a binding that holds no pin when UNBIND ALL PINS is held for
 * UI_HOLD_S (2 s) in the BIND_READ_ODD state.  Never for a protocol pick.
 *
 * The screen does not know whether a link exists or whether the far end took
 * the pages; outputs_screen_set_result() is how it is told, so the operator
 * sees a refusal on the screen that caused it.
 */
typedef void (*outputs_apply_fn)(const outbind_t *b);
void outputs_screen_set_apply(outputs_apply_fn fn);

/** What became of the last write: the text is shown under the protocol. */
typedef enum {
    OUTPUTS_IDLE = 0,   /**< nothing written yet, or a write not sent     */
    OUTPUTS_OK,         /**< the coprocessor took it                      */
    OUTPUTS_NO_LINK,    /**< nothing answered                             */
    OUTPUTS_REFUSED,    /**< it answered and would not have it            */
} outputs_result_t;

void outputs_screen_set_result(outputs_result_t r);

/** What a pin's cell shows. */
typedef enum {
    OUTPUTS_CELL_NONE = 0,  /**< no such pin on this board                  */
    OUTPUTS_CELL_FREE,      /**< bound to nothing: "PAD n"                  */
    OUTPUTS_CELL_TICKED,    /**< bound in the selected protocol: "PAD n"    */
    OUTPUTS_CELL_HELD,      /**< bound in another protocol: its name        */
    OUTPUTS_CELL_RESERVED,  /**< the coprocessor's own: what holds it       */
} outputs_cell_t;

/**
 * The state of the cell of catalogue entry @p index and the line printed
 * under its GPIO name, as the screen draws them.  @p label may be NULL.
 */
outputs_cell_t outputs_screen_cell(uint8_t index, char *label, size_t cap);

/**
 * The line under the protocol that says why no pin can be ticked, as the
 * screen draws it in the language in force; an empty string when there is
 * none.  Returns whether there is one.
 */
bool outputs_screen_reason(char *buf, size_t cap);

#ifdef __cplusplus
}
#endif
