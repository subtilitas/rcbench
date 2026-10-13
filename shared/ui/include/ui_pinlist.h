/*
 * A set of GPIO pins as text for a screen label: "GP2 GP4 GP5 +2".
 *
 * Pure function, no drawing and no I/O (input/output).  Every screen that
 * names the pins an output drives formats them here, so the same pin set
 * reads the same on every screen.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Pins the formatter can name: bit n of the mask is GPIO n, n = 0 to 63. */
#define UI_PINLIST_PINS 64u

/**
 * Writes the pins of @p mask, lowest first, as "GP2 GP4 GP5", into @p out.
 *
 * The text is at most @p cells characters (one character is one cell of the
 * 8x16 face) and at most @p size - 1 characters.  When all pins do not fit,
 * the list shows as many leading pins as fit, then "+N" with N the pins left
 * out: "GP2 GP4 GP5 +2".  When not even one pin and the count fit, the text is
 * "+N" alone.  When "+N" itself is wider than @p cells, the text is cut to
 * @p cells characters.
 *
 * An empty mask writes "" and returns 0.  A NULL @p out or a @p size of 0
 * writes nothing and returns 0.
 *
 * @return the length of the text written, not counting the terminator.
 */
size_t ui_pinlist(char *out, size_t size, uint64_t mask, unsigned cells);

/** The number of pins in @p mask. */
unsigned ui_pinlist_count(uint64_t mask);

#ifdef __cplusplus
}
#endif
