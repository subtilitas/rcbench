/*
 * The outputs screen: choose a protocol, then tick the pins it drives.
 *
 * One protocol and a set of pins rather than eight independent slots, because
 * that is the shape of the job in front of the operator -- four servo leads,
 * or one ESC (electronic speed controller).  out_bind.c turns the choice into
 * the OUTPUTS and CHAN_CFG pages; this file is only the touching.
 *
 * The screen writes nothing to the wire.  It calls the apply function it was
 * given whenever the choice changes, and the application does the writing, so
 * the whole screen renders and is driven on the host with no link at all.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>

#include "out_bind.h"
#include "ui_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

const ui_screen_t *outputs_screen(void);

/** Repaint the cached chrome in every framebuffer. */
void outputs_screen_invalidate(void);

/** The current choice, for anything that has to write it. */
const outbind_t *outputs_screen_binding(void);

/** Set the choice, at boot from what was saved. */
void outputs_screen_set_binding(const outbind_t *b);

/**
 * Called after any change the operator makes.
 *
 * The screen does not know whether a link exists or whether the far end took
 * the pages; outputs_screen_set_result() is how it is told, so the operator
 * sees a refusal on the screen that caused it.
 */
typedef void (*outputs_apply_fn)(const outbind_t *b);
void outputs_screen_set_apply(outputs_apply_fn fn);

/** What became of the last write: the text is shown under the protocol. */
typedef enum {
    OUTPUTS_IDLE = 0,   /**< nothing written yet                          */
    OUTPUTS_OK,         /**< the coprocessor took it                      */
    OUTPUTS_NO_LINK,    /**< nothing answered                             */
    OUTPUTS_REFUSED,    /**< it answered and would not have it            */
} outputs_result_t;

void outputs_screen_set_result(outputs_result_t r);

#ifdef __cplusplus
}
#endif
