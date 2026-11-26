/**
 * @file render_about.c
 * @brief ABOUT's views for tools/render_ui.py (render_about.h).
 *
 * setup-about: ABOUT at its top, the IOmcu running the panel's own
 * release.  setup-about-licences: scrolled 8 rows, from LICENCES over the
 * trademark notice to the first components of the panel firmware.
 * setup-about-end: scrolled to its end, the last rows of the Apache License
 * 2.0.
 *
 * SPDX-License-Identifier: MIT
 */
#include "render_about.h"

#include <string.h>

#include "about_screen.h"
#include "rcbench_version.h"
#include "upd_screen.h"

bool render_about_view(const char *view)
{
    return strncmp(view, "setup-about", 11) == 0;
}

void render_about_pose(const char *view)
{
    if (!render_about_view(view)) {
        return;
    }
    upd_cu_view_t cu = *upd_screen_coproc();
    cu.known = true;
    cu.running = (cu_version_t){ RCBENCH_VERSION_MAJOR,
                                 RCBENCH_VERSION_MINOR,
                                 RCBENCH_VERSION_PATCH };
    upd_screen_set_coproc(&cu);
    about_screen_open();
    if (strcmp(view, "setup-about-licences") == 0) {
        about_screen_scroll(8);
    } else if (strcmp(view, "setup-about-end") == 0) {
        about_screen_scroll(about_screen_rows());
    }
}
