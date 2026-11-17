/**
 * @file upd_run.c
 * @brief The update run on the screens. See upd_run.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "upd_run.h"

#include "coproc_update_screen.h"
#include "ui_screen.h"
#include "upd_screen.h"
#include "upd_wiz_view.h"
#include "upd_wizard.h"

upd_run_wait_t upd_run_wait(const upd_run_feed_t *f)
{
    if (f == NULL) {
        return UPD_RUN_WAIT_NONE;
    }
    if (f->unsafe) {
        return UPD_RUN_WAIT_SAFE;
    }
    if (f->radio_up) {
        return UPD_RUN_WAIT_RADIO;
    }
    if (f->installing) {
        return UPD_RUN_WAIT_INSTALL;
    }
    return UPD_RUN_WAIT_NONE;
}

ui_text_id_t upd_run_wait_text(upd_run_wait_t w)
{
    switch (w) {
    case UPD_RUN_WAIT_RADIO:   return TX_ALERT_RUN_WAIT_WIFI;
    case UPD_RUN_WAIT_INSTALL: return TX_ALERT_RUN_WAIT_INSTALL;
    case UPD_RUN_WAIT_SAFE:
    case UPD_RUN_WAIT_NONE:
    default:                   return TX_ALERT_RUN_WAIT_SAFE;
    }
}

/**
 * @brief Whether no step blocks: the run has nothing left to lead to.
 * @return true when the steps have no current one
 */
static bool nothing_blocks(void)
{
    static upd_wiz_t w;    /* off the render task's stack */
    upd_wiz_now(&w);
    return w.current == UPD_WIZ_NONE;
}

upd_run_end_t upd_run_drive(upd_run_t *r, const upd_run_feed_t *f)
{
    /* Drained every frame: a home tag from before a run ends no run. */
    const bool left = upd_screen_take_left();
    if (r == NULL || f == NULL || !f->run) {
        return UPD_RUN_GOES_ON;
    }
    if (!f->restart_due) {
        if (left) {
            return UPD_RUN_END_LEFT;
        }
        if (f->known && nothing_blocks()) {
            return UPD_RUN_END_DONE;
        }
    }
    /* UPDATES shows, or an IOmcu offer opened it in this start before the
     * run was read: the run's opening is spent, UPDATES opens once. */
    if (!r->opened && (ui_router_current() == SCREEN_UPDATES
                       || coproc_update_opened())) {
        r->opened = true;
    }
    if (r->opened) {
        return UPD_RUN_GOES_ON;
    }
    const upd_run_wait_t w = upd_run_wait(f);
    if (w == UPD_RUN_WAIT_NONE) {
        if (ui_router_current() == SCREEN_OVERVIEW) {
            r->opened = true;
            ui_router_goto(SCREEN_UPDATES);
        }
    } else if ((uint8_t)w != r->said && ui_router_stop_live()) {
        /* Once, and again when the reason changes: a band shows it. */
        ui_router_set_alert(ui_tr(upd_run_wait_text(w)));
        r->said = (uint8_t)w;
    }
    return UPD_RUN_GOES_ON;
}

bool upd_run_waits(const upd_run_t *r, const upd_run_feed_t *f)
{
    return r != NULL && f != NULL && f->run && !r->opened;
}
