/**
 * @file render_help.c
 * @brief The screen guide's views for tools/render_ui.py (render_help.h).
 *
 * help-overview is the menu's guide at page 1, help-overview-2 to
 * help-overview-4 its other pages, each opened and turned by taps through
 * the router as a person does.  help-setup, help-outputs, help-logs,
 * help-files, help-servo, help-supply and their states' views open the
 * guide over the view render_help_base() names, posed as that view is:
 * SERVO's on a task or SETTINGS' page, SUPPLY's with the output off, as a
 * task's page is offered only then.  help-band-worst is MOTOR & ESC's band
 * with the widest row of chips test_nav_bubble finds: a clock of 10 hours,
 * ARMED MOTOR, FAULT 7F, BENCH and LINK.
 *
 * SPDX-License-Identifier: MIT
 */
#include "render_help.h"

#include <stdlib.h>
#include <string.h>

#include "ui_band.h"
#include "ui_help.h"
#include "ui_screen.h"

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

/** @brief The guide's views posed as another view, each with its pages
 *         help-...-2 and on. */
static const struct {
    const char *help; /**< the guide's view, without a page */
    const char *base; /**< the view it opens over           */
} k_based[] = {
    { "help-setup", "setup" },
    { "help-setup-rbmod", "setup-rbmod" },
    { "help-setup-list", "setup-application" },
    { "help-setup-sensors", "setup-sensors" },
    { "help-outputs", "outputs" },
    { "help-outputs-port", "outputs-port" },
    { "help-outputs-grid", "outputs-grid" },
    { "help-outputs-odd", "outputs-odd" },
    { "help-outputs-rbmod", "outputs-rbmod" },
    { "help-outputs-rbmod-port", "outputs-rbmod-port" },
    { "help-logs", "logs" },
    { "help-logs-pages", "logs-pages" },
    { "help-logs-import", "logs-import" },
    { "help-logs-plot", "logs-plot" },
    { "help-files", "files" },
    { "help-servo", "servo" },
    { "help-servo-test", "servo-test" },
    { "help-servo-program", "servo-program" },
    { "help-servo-rudder", "servo-rudder" },
    { "help-servo-rudder-auto", "servo-rudder-auto-safe" },
    { "help-servo-output", "servo-settings" },
    { "help-servo-sweep", "servo-sweep-page" },
    { "help-servo-dut", "servo-encoder" },
    { "help-supply", "supply-off" },
    { "help-supply-limits", "supply-settings-off" },
    { "help-supply-pdmini", "supply-pdmini" },
};

const char *render_help_base(const char *view)
{
    size_t n = strlen(view);
    if (n > 2 && view[n - 2] == '-' && view[n - 1] >= '2'
        && view[n - 1] <= '9') {
        n -= 2;
    }
    for (size_t i = 0; i < sizeof(k_based) / sizeof(k_based[0]); ++i) {
        if (strlen(k_based[i].help) == n
            && strncmp(view, k_based[i].help, n) == 0) {
            return k_based[i].base;
        }
    }
    return view;
}

bool render_help_view(const char *view)
{
    return strncmp(view, "help-", 5) == 0;
}

void render_help_pose(const char *view)
{
    if (strcmp(view, "help-band-worst") == 0) {
        ui_bench_status_t st = *ui_router_status();
        st.simulated   = false;
        st.link_up     = true;
        st.mode        = ui_band_mode(true);
        st.armed       = true;
        st.armed_roles = UI_ARMED_MOTOR;
        st.faults      = 0x7Fu;
        st.run_seconds = 10u * 3600u;
        ui_router_set_status(&st);
        return;
    }
    /* help-<screen>-<n>: the page after the last dash, 1 without one. */
    const char *dash = strrchr(view, '-');
    const int page = (dash != NULL && dash[1] >= '2' && dash[1] <= '9')
                         ? atoi(dash + 1) : 1;
    tap_mid(ui_band_guide_rect());
    for (int k = 1; k < page; ++k) {
        ui_help_page_t p = { 0 };
        if (!ui_router_guide_now(&p)) {
            return;
        }
        tap_mid(ui_help_button_rect(p.side, UI_HELP_HIT_NEXT));
    }
}
