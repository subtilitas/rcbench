/**
 * @file
 * @brief The pixels the rasteriser's loops visit, counted for the host
 *        suite.
 *
 * Each pixel a loop examines or writes counts once, kept or clipped away:
 * a filled rectangle by its clipped area, a glyph by its cells, an
 * antialiased shape by the pixels of each row it walks, an arc by its
 * steps, a single pixel by itself.  The count is a frame's drawing work in
 * a measure that does not depend on the machine (test_frame_work,
 * test_gfx_clip).  Compiled in only when GFX_WORK_COUNT is defined, as
 * test/host/CMakeLists.txt builds the library; the panel builds without it
 * and GFX_VISIT() is then nothing.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#ifdef GFX_WORK_COUNT
/** @brief The visits gfx_work() returns; gfx.c holds it. */
extern unsigned long gfx_work_visits;

/**
 * @brief The pixels the primitives have visited since start-up.
 * @return the visits, wrapping at the width of unsigned long
 */
unsigned long gfx_work(void);

/** @brief Count @p n pixels visited. */
#define GFX_VISIT(n) (gfx_work_visits += (unsigned long)(n))
#else
/** @brief Count @p n pixels visited: nothing, in a build without
 *         GFX_WORK_COUNT. */
#define GFX_VISIT(n) ((void)0)
#endif

#ifdef __cplusplus
}
#endif
