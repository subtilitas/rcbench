/**
 * @file
 * @brief A pin's name and a set of pins as text for a screen label or a
 *        report: "GPIO 13", "GPIO 2 GPIO 4 +2".
 *
 * Pure functions, no drawing and no I/O (input/output).  Every screen and
 * report that names a pin formats it here, so a pin reads the same
 * everywhere.  A text table entry that names a pin writes the same form,
 * "GPIO %u": test_text fails on any entry of ui_text.def, English or
 * German, with "GP" directly followed by a digit or a conversion.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Pins the formatter can name: bit n of the mask is GPIO n,
 *         n = 0 to 63. */
#define UI_PINLIST_PINS 64u

/** @brief The word a pin's name starts with, the same in every language. */
#define UI_PIN_WORD "GPIO"

/** @brief The bytes the longest pin name takes, "GPIO 63" and its NUL. */
#define UI_PIN_NAME_SIZE 8u

/**
 * @brief Writes the name of pin @p gpio into @p out: the pin word, a space
 *        and the number, "GPIO 13".
 *
 * The text is cut to @p size - 1 characters, as snprintf() cuts it.
 *
 * @param out   Where the text goes; NULL writes nothing.
 * @param size  The bytes at @p out; 0 writes nothing.
 * @param gpio  The pin, 0 to UI_PINLIST_PINS - 1.  A higher number (a
 *              screen's NO_PIN, 0xFF) is no pin and writes "".
 * @return The length of the text written, not counting the terminator;
 *         0 for no pin, a NULL @p out or a @p size of 0.
 */
size_t ui_pin_name(char *out, size_t size, unsigned gpio);

/**
 * @brief Writes the pins of @p mask, lowest first, as "GPIO 2 GPIO 4
 *        GPIO 5", each as ui_pin_name() names it, into @p out.
 *
 * The text is at most @p cells characters (one character is one cell of
 * the monospaced face the caller draws it in: a TARGET row in 8 x 16 or
 * 10 x 20, ui_target_row_cells()) and at most @p size - 1 characters.
 * When all pins do not fit, the list shows as many leading pins as fit,
 * then "+N" with N the pins left out: "GPIO 2 GPIO 4 +2".  When not even one
 * pin and the count fit, the text is "+N" alone.  When "+N" itself is
 * wider than @p cells, the text is cut to @p cells characters.
 *
 * @param out   Where the text goes; NULL writes nothing.
 * @param size  The bytes at @p out; 0 writes nothing.
 * @param mask  The pins, bit n for GPIO n; 0 writes "".
 * @param cells The cells the text may take.
 * @return The length of the text written, not counting the terminator;
 *         0 for an empty mask, a NULL @p out or a @p size of 0.
 */
size_t ui_pinlist(char *out, size_t size, uint64_t mask, unsigned cells);

/**
 * @brief The number of pins in @p mask.
 * @param mask The pins, bit n for GPIO n.
 * @return Its set bits.
 */
unsigned ui_pinlist_count(uint64_t mask);

#ifdef __cplusplus
}
#endif
