/*
 * The placeholder for a routed screen id that has no screen of its own.
 *
 * It lists what the screen will do and the one part or decision that blocks
 * it; the note turns green when nothing blocks it.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "ui_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

void stub_invalidate(void);

/**
 * The widest a line of stub copy may be, in pixels.
 *
 * gfx_text clips at the canvas edge rather than wrapping, so a longer line is
 * cut mid-word.  test_nav holds every line to this.
 */
#define STUB_COPY_MAX_W 732

/** Every line of copy for @p id, NULL-terminated; for tests. */
const char *const *stub_copy_lines(ui_screen_id_t id);
/** The blocker line for @p id, or NULL when nothing is blocking it. */
const char *stub_copy_blocker(ui_screen_id_t id);

/** One instance serves every stub; the id selects its copy. */
const ui_screen_t *stub_screen(ui_screen_id_t id);

#ifdef __cplusplus
}
#endif
