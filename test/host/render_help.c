/**
 * @file render_help.c
 * @brief The screen guide's views for tools/render_ui.py (render_help.h).
 *
 * help-overview is the menu's guide at page 1, help-overview-2 and
 * help-overview-3 its other pages, each opened and turned by taps through
 * the router as a person does.  help-band-worst is MOTOR & ESC's band with
 * the widest row of chips test_nav_bubble finds: a clock of 10 hours,
 * ARMED MOTOR, FAULT 7F, BENCH and LINK.
 *
 * SPDX-License-Identifier: MIT
 */
#include "render_help.h"

#include <stdlib.h>
#include <string.h>

#include "ui_band.h"
#include "ui_help.h"

/**
 * @brief A tap of contact 1 at the middle of @p r, in panel coordinates.
 * @param r the rect
 */
static void tap_mid(gfx_rect_t r)
{
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1,
                                   .x = (int16_t)(r.x + r.w / 2),
                                   .y = (int16_t)(r.y + r.h / 2),
                                   .strength = 40 } };
    ui_router_event(&e);
    e.type = TOUCH_EVENT_UP;
    ui_router_event(&e);
}

bool render_help_view(const char *view)
{
    return strncmp(view, "help-", 5) == 0;
}

void render_help_pose(const char *view, ui_bench_status_t *st)
{
    if (strcmp(view, "help-band-worst") == 0) {
        st->simulated   = false;
        st->link_up     = true;
        st->mode        = ui_band_mode(true);
        st->armed       = true;
        st->armed_roles = UI_ARMED_MOTOR;
        st->faults      = 0x7Fu;
        st->run_seconds = 10u * 3600u;
        ui_router_set_status(st);
        return;
    }
    /* help-<screen>-<n>: the page after the last dash, 1 without one. */
    const char *dash = strrchr(view, '-');
    const int page = (dash != NULL && dash[1] >= '2' && dash[1] <= '9')
                         ? atoi(dash + 1) : 1;
    tap_mid(ui_band_guide_rect());
    for (int k = 1; k < page; ++k) {
        ui_help_page_t p;
        if (!ui_router_guide_now(&p)) {
            return;
        }
        tap_mid(ui_help_button_rect(p.side, UI_HELP_HIT_NEXT));
    }
}
