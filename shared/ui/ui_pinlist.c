/**
 * @file
 * @brief A pin's name and a set of pins as text: see ui_pinlist.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "ui_pinlist.h"

#include <stdio.h>
#include <string.h>

size_t ui_pin_name(char *out, size_t size, unsigned gpio)
{
    if (out == NULL || size == 0u) {
        return 0u;
    }
    out[0] = '\0';
    if (gpio >= UI_PINLIST_PINS) {
        return 0u;
    }
    const int n = snprintf(out, size, UI_PIN_WORD " %u", gpio);
    const size_t len = (n > 0) ? (size_t)n : 0u;
    return (len < size) ? len : size - 1u;
}

unsigned ui_pinlist_count(uint64_t mask)
{
    unsigned n = 0u;
    for (; mask != 0u; mask &= mask - 1u) {
        ++n;
    }
    return n;
}

/**
 * @brief The decimal digits of @p v.
 * @param v  the number
 * @return its digits, 1 for 0
 */
static unsigned digits(unsigned v)
{
    unsigned d = 1u;
    while (v >= 10u) {
        v /= 10u;
        ++d;
    }
    return d;
}

/**
 * @brief The characters pin @p gpio's name takes in the list ("GPIO 13",
 *        ui_pin_name()), with the space before it unless it is the first.
 * @param gpio   the pin
 * @param index  its place in the list, 0 for the first
 * @return the characters
 */
static unsigned pin_width(unsigned gpio, unsigned index)
{
    return (unsigned)strlen(UI_PIN_WORD) + 1u + digits(gpio)
           + (index != 0u ? 1u : 0u);
}

size_t ui_pinlist(char *out, size_t size, uint64_t mask, unsigned cells)
{
    if (out == NULL || size == 0u) {
        return 0u;
    }
    out[0] = '\0';
    const unsigned n = ui_pinlist_count(mask);
    if (n == 0u) {
        return 0u;
    }
    unsigned limit = cells;
    if (limit > size - 1u) {
        limit = (unsigned)(size - 1u);
    }

    /* The longest prefix of k pins whose text, plus " +N" when pins are
     * left out, fits.  k == n is the whole list and has no suffix. */
    unsigned width = 0u;     /* width of the first k pins                  */
    unsigned best  = 0u;     /* pins shown                                 */
    unsigned k     = 0u;
    for (unsigned g = 0u; g < UI_PINLIST_PINS; ++g) {
        if (((mask >> g) & 1u) == 0u) {
            continue;
        }
        width += pin_width(g, k);
        ++k;
        const unsigned left = n - k;
        const unsigned need = (left == 0u)
                                  ? width
                                  : width + 1u + 1u + digits(left);
        if (need <= limit) {
            best = k;
        }
    }

    size_t len = 0u;
    unsigned shown = 0u;
    for (unsigned g = 0u; g < UI_PINLIST_PINS && shown < best; ++g) {
        if (((mask >> g) & 1u) == 0u) {
            continue;
        }
        if (shown != 0u && len + 1u < size) {
            out[len++] = ' ';
            out[len] = '\0';
        }
        len += ui_pin_name(out + len, size - len, g);
        ++shown;
    }
    if (best < n) {
        len += (size_t)snprintf(out + len, size - len, "%s+%u",
                                best != 0u ? " " : "", n - best);
    }
    if (len > limit) {
        len = limit;
        out[len] = '\0';
    }
    return len;
}
