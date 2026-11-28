/**
 * @file
 * @brief The third-party table and the notice texts ABOUT shows: rows that
 *        tools/gen_licenses.py writes into about_gen.c from its one list.
 *
 * The rows come in 4 parts, in the order of the enum below: the panel
 * firmware's components, the IOmcu firmware's, the design and protocol
 * references, and the notice texts.  A row holds at most ABOUT_GEN_CELLS
 * cells of the faces' characters and carries one style letter:
 *
 * | Letter | Row |
 * | --- | --- |
 * | `N` | a component's or a reference's name and version |
 * | `D` | its licence (SPDX identifier) and copyright lines |
 * | `S` | the heading of a notice block |
 * | `T` | a row of a licence text |
 * | `B` | an empty row |
 *
 * docs/Licenses.md holds the same table; `tools/gen_licenses.py --check`
 * fails when either has drifted from the list.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The parts, each a run of rows. */
enum {
    ABOUT_GEN_PANEL = 0, /**< the panel firmware's components  */
    ABOUT_GEN_IOMCU,     /**< the IOmcu firmware's components  */
    ABOUT_GEN_REFS,      /**< the design and protocol references */
    ABOUT_GEN_NOTICES,   /**< the notice and licence texts       */
    ABOUT_GEN_PARTS
};

/** @brief The most cells a row holds: 920px of the 10 x 20 face. */
#define ABOUT_GEN_CELLS 92

/** @brief Each part's first row; the last entry is the row count. */
extern const uint16_t about_gen_part[ABOUT_GEN_PARTS + 1];

/** @brief Each row's style letter, `N`, `D`, `S`, `T` or `B`. */
extern const char about_gen_style[];

/** @brief Each row's text, UTF-8. */
extern const char *const about_gen_text[];

#ifdef __cplusplus
}
#endif
