/**
 * @file upd_settings.c
 * @brief UPDATES' SETTINGS list (ui_setlist.h), opened by the footer's
 *        SETTINGS: Reminder at start (SET_UPD_REMIND) and Releases
 *        (SET_UPD_CHANNEL), the two settings of SET_CAT_UPDATE.
 *
 * SETUP's list: 64px rows at a 70px pitch, "<" and ">" on the switch and
 * the choice, and the strip [UPDATES] [RESET UPDATES] [SAVE] at the
 * footer's place.  A change applies at once, as the settings' observer
 * publishes it: a check started next looks at the releases chosen.  SAVE
 * asks for a write of the whole store, as on SETUP.  The home tag closes
 * the list, through the leave warning while something is unsaved and no
 * save is asked for.  The footer opens the list while no check or download
 * runs (upd_settings_free()), and the list closes when one starts under
 * it.
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include "settings.h"
#include "ui_setlist.h"
#include "ui_text.h"
#include "upd_screen.h"
#include "upd_settings_int.h"

/** @brief The list's rows in their order. */
static const ui_setlist_row_t k_rows[] = {
    UI_SL_ROW_SETTING(SET_UPD_REMIND),
    UI_SL_ROW_SETTING(SET_UPD_CHANNEL),
};
/** @brief The rows' count. */
#define ROW_COUNT ((int)(sizeof(k_rows) / sizeof(k_rows[0])))

/** @brief The list's state. */
static struct {
    ui_setlist_t sl;          /**< the list                            */
    uint32_t     rev;         /**< bumped when the list's look changes */
} p; /**< the list's state */

/** @brief The rows' count. @return ROW_COUNT */
static int rows_count(void) { return ROW_COUNT; }

/**
 * @brief Row @p i of k_rows.
 * @param i The row.
 * @return The row.
 */
static ui_setlist_row_t rows_row(int i)
{
    return ui_setlist_table_row(k_rows, ROW_COUNT, i);
}

/** @brief The strip's name: the screen's title, English always.
 *  @return The name. */
static const char *list_name(void) { return "UPDATES"; }

/** @brief The list's look changed. */
static void list_invalidate(void) { ++p.rev; }

/** @brief The leave warning's SAVE and DISCARD: the list closes. */
static void list_way_out(void) { upd_list_close(); }

/** @brief UPDATES' side of its list. */
static const ui_setlist_host_t k_host = {
    .count      = rows_count,
    .row        = rows_row,
    .name       = list_name,
    .invalidate = list_invalidate,
    .way_out    = list_way_out,
};

void upd_list_reset(void)
{
    memset(&p, 0, sizeof(p));
    ui_setlist_init(&p.sl, &k_host);
}

void upd_list_open(void)
{
    ui_setlist_open(&p.sl);
    ++p.rev;
    upd_screen_invalidate();
}

void upd_list_close(void)
{
    if (p.sl.open || ui_setlist_covered(&p.sl)) {
        ui_setlist_close(&p.sl);
        ++p.rev;
        upd_screen_invalidate();
    }
}

bool upd_list_shown(void) { return p.sl.open; }

bool upd_list_back(void)
{
    if (!p.sl.open) {
        return false;
    }
    if (ui_setlist_back(&p.sl) || ui_setlist_warn(&p.sl)) {
        return true;
    }
    upd_list_close();
    return true;
}

void upd_list_event(const touch_event_t *evt)
{
    ui_setlist_event(&p.sl, evt);
}

void upd_list_cancel(void)
{
    ui_setlist_cancel(&p.sl);
}

void upd_list_tick(float dt_s)
{
    ui_setlist_tick(&p.sl, dt_s);
}

void upd_list_render(gfx_canvas_t *c)
{
    ui_setlist_render(&p.sl, c);
}

uint32_t upd_list_revision(void) { return p.rev; }

const ui_setlist_t *upd_screen_settings_list(void) { return &p.sl; }
