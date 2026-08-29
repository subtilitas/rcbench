/*
 * The receiver-bus analyser: what a receiver is actually sending.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "sbus.h"
#include "ui_screen.h"

typedef enum {
    ANALYSER_PANE_CHANNELS = 0,
    ANALYSER_PANE_RAW,
    ANALYSER_PANE_COUNT,
} analyser_pane_t;

const ui_screen_t *analyser_screen(void);

/**
 * A decoded frame, with the decoder's own counters and the bytes it came
 * from -- the raw view is not a separate capture, it is this same frame.
 */
void analyser_screen_push(const sbus_frame_t *frame,
                          const sbus_decoder_t *dec,
                          const uint8_t *raw, unsigned raw_len,
                          uint32_t now_ms);

/** No frame has arrived for long enough that the link is presumed dead. */
void analyser_screen_silent(uint32_t now_ms);
