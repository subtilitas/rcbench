/*
 * The screens that are routed, rendered and navigable but do not do their job
 * yet -- and say exactly what is missing.
 *
 * A "coming soon" panel teaches nobody anything.  One that names what it will
 * do, and the one decision or part that has to arrive first, is a to-do list
 * somebody can answer -- and the note turns green when nothing is blocking it,
 * which is a different and less comfortable position than being blocked.
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
 * The copy is drawn with gfx_text, which clips at the canvas edge rather than
 * wrapping -- so a line that is too long is silently cut mid-word and looks
 * like a rendering fault rather than an over-long string.  test_nav holds
 * every line to this.
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
