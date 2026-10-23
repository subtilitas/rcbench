/**
 * @file
 * @brief The RECEIVER screen: what a receiver is actually sending.  The
 *        code keeps the name analyser; the screen's title and tile are
 *        RECEIVER.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "sbus.h"
#include "ui_screen.h"

/** @brief The left card's panes, in the tabs' order. */
typedef enum {
    ANALYSER_PANE_CHANNELS = 0, /**< the sixteen lanes */
    ANALYSER_PANE_RAW,          /**< the last frame's bytes */
    ANALYSER_PANE_COUNT,        /**< how many */
} analyser_pane_t;

/** Drop the cached chrome, so the next frame repaints it. */
void analyser_invalidate(void);

/**
 * @brief The RECEIVER screen.
 * @return The screen.
 */
const ui_screen_t *analyser_screen(void);

/**
 * @brief A decoded frame, with the decoder's own counters and the bytes it
 *        came from -- the raw view is not a separate capture, it is this
 *        same frame.
 * @param frame   The frame; NULL does nothing.
 * @param dec     The decoder after it; NULL does nothing.
 * @param raw     Its bytes, or NULL to keep the last.
 * @param raw_len Their count; at most SBUS_FRAME_BYTES are kept.
 * @param now_ms  Its arrival, ms.
 */
void analyser_screen_push(const sbus_frame_t *frame,
                          const sbus_decoder_t *dec,
                          const uint8_t *raw, unsigned raw_len,
                          uint32_t now_ms);

/**
 * @brief No frame has arrived for long enough that the link is presumed
 *        dead.
 * @param now_ms The time, ms.
 */
void analyser_screen_silent(uint32_t now_ms);

/**
 * @brief The lines a state's line takes on the state card, in the 12 x 24
 *        face broken after a space; the card shows three.
 * @param why The line, as the screen shows it.
 * @return The lines, 0 for NULL or an empty line.
 */
int analyser_screen_why_lines(const char *why);
