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

/** One instance serves every stub; the id selects its copy. */
const ui_screen_t *stub_screen(ui_screen_id_t id);

#ifdef __cplusplus
}
#endif
