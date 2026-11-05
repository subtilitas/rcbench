/**
 * @file
 * @brief FILES' screen guide (files_int.h): three pages over the panes.
 *
 * The left pane with the dock on the right: the LOGS tab, the pane's
 * folder, a row and the scroll bar; then the footer's left half with the
 * dock on the right: RESCAN, COPY, MOVE and RENAME; then the right pane and
 * the footer's right half with the dock on the left: the pane, DELETE, NEW,
 * INFO and OPEN.  A box, a question, a keyboard or the text viewer leaves
 * no page.  FILES drives no output, so every page shows while ARMED as
 * well.
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include "logs_strip.h"
#include "ui_help.h"

#include "files_int.h"

/** @brief The footer's buttons each page marks, -1 past the last. */
static const int8_t k_foot[3][4] = {
    { -1, -1, -1, -1 },
    { FS_RESCAN, FS_COPY, FS_MOVE, FS_RENAME },
    { FS_DELETE, FS_NEW, FS_INFO, FS_OPEN },
};

/**
 * @var k_foot_text
 * @brief Each footer button's label and what it does.
 */
static const struct {
    ui_text_id_t label; /**< as the button reads */
    ui_text_id_t text;  /**< what it does        */
} k_foot_text[FOOT_N] = {
    [FS_RESCAN] = { TX_LOG_RESCAN, TX_HG_FS_RESCAN },
    [FS_COPY]   = { TX_FM_COPY, TX_HG_FS_COPY },
    [FS_MOVE]   = { TX_FM_MOVE, TX_HG_FS_MOVE },
    [FS_RENAME] = { TX_FM_RENAME, TX_HG_FS_RENAME },
    [FS_DELETE] = { TX_LOG_DELETE, TX_HG_FS_DELETE },
    [FS_NEW]    = { TX_FM_NEW, TX_HG_FS_NEW },
    [FS_INFO]   = { TX_FM_INFO, TX_HG_FS_INFO },
    [FS_OPEN]   = { TX_LOG_OPEN, TX_HG_FS_OPEN },
};

int fs_help_count(void)
{
    return (s.view == FILES_VIEW_PANES) ? 3 : 0;
}

bool fs_help_page(int i, ui_help_page_t *out)
{
    if (out == NULL || i < 0 || i >= fs_help_count()) {
        return false;
    }
    static const ui_text_id_t k_title[3] = { TX_HG_FS_TITLE_LEFT,
                                             TX_HG_FS_TITLE_BUTTONS,
                                             TX_HG_FS_TITLE_RIGHT };
    memset(out, 0, sizeof(*out));
    out->armed_ok = true;
    out->title = k_title[i];
    out->side = (i < 2) ? UI_HELP_DOCK_RIGHT : UI_HELP_DOCK_LEFT;
    if (i == 0) {
        (void)ui_help_add(out, ui_router_title(SCREEN_LOGS), TX_HG_FS_LOGS,
                          logs_strip_tab_rect(SCREEN_LOGS));
        (void)ui_help_add(out, TR(HG_FS_FOLDER_NAME), TX_HG_FS_FOLDER,
                          FS_UP(0));
        if (s.scroll[0] < s.pane[0].held) {
            (void)ui_help_add(out, TR(HG_SET_ROW_NAME), TX_HG_FS_ROW,
                              gfx_rect_make(FS_PANE_X(0) + FS_ROW_IN,
                                            FS_LIST_Y + FS_HEAD_H, FS_ROW_W,
                                            FS_ROW_H - 4));
        }
        (void)ui_help_add(out, TR(HG_LOG_BAR_NAME), TX_HG_LOG_BAR, FS_BAR(0));
        return true;
    }
    if (i == 2) {
        (void)ui_help_add(out, TR(HG_FS_PANE_NAME), TX_HG_FS_PANE,
                          FS_LIST(1));
    }
    for (int k = 0; k < 4; ++k) {
        const int b = k_foot[i][k];
        if (b >= 0) {
            (void)ui_help_add(out, ui_tr(k_foot_text[b].label),
                              k_foot_text[b].text, FS_FOOT(b));
        }
    }
    return true;
}
