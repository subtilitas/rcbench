/**
 * @file
 * @brief LOGS' screen guide (log_viewer_int.h): two pages for the file
 *        list, two for the import view, one for the plot.
 *
 * The list: the folder tabs, a row, RESCAN and WI-FI with the dock on the
 * right; FILES, PREV and NEXT, the scroll bar, DELETE and OPEN with the
 * dock on the left.  The import view: DETECTED, SEP and NUM with the dock
 * on the right; the columns, BACK and PLOT with the dock on the left.  The
 * plot: a series chip, the plot, BACK and the cursor's keys with the dock
 * on the right.  The DELETE question leaves no page.  LOGS drives no
 * output, so every page shows while ARMED as well.
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include "logs_strip.h"
#include "ui_help.h"

#include "log_viewer_int.h"

int lv_help_count(void)
{
    if (s.doomed[0] != '\0') {
        return 0;
    }
    return (s.view == VIEW_PLOT) ? 1 : 2;
}

/**
 * @brief The rect from the left of @p a to the right of @p b, one row.
 * @param a The left rect.
 * @param b The right rect.
 * @return Both and what is between them.
 */
static gfx_rect_t span(gfx_rect_t a, gfx_rect_t b)
{
    return gfx_rect_make(a.x, a.y, b.x + b.w - a.x, a.h);
}

/**
 * @brief The file list's page @p i.
 * @param i The page.
 * @param p The page, its side set.
 */
static void browse_page(int i, ui_help_page_t *p)
{
    if (i == 0) {
        p->title = TX_HG_LOG_TITLE_LIST;
        (void)ui_help_add(p, TR(HG_LOG_TABS_NAME), TX_HG_LOG_TABS, BR_TABS);
        if (s.scroll < s.n_files) {
            (void)ui_help_add(p, TR(HG_SET_ROW_NAME), TX_HG_LOG_ROW,
                              gfx_rect_make(BR_ROW_X, BR_LIST_Y + 30,
                                            BR_ROW_W, BR_ROW_H - 4));
        }
        (void)ui_help_add(p, TR(LOG_RESCAN), TX_HG_LOG_RESCAN, BR_RESCAN);
        (void)ui_help_add(p, TR(LOG_WIFI), TX_HG_LOG_WIFI, BR_WIFI);
        return;
    }
    p->title = TX_HG_LOG_TITLE_OPEN;
    (void)ui_help_add(p, ui_router_title(SCREEN_FILES), TX_HG_LOG_FILES,
                      logs_strip_tab_rect(SCREEN_FILES));
    if (lv_paged()) {
        (void)ui_help_add(p, TR(HG_LOG_PAGES_NAME), TX_HG_LOG_PAGES,
                          span(BR_PREV, BR_NEXT));
    }
    if (s.n_files > 0) {
        (void)ui_help_add(p, TR(HG_LOG_BAR_NAME), TX_HG_LOG_BAR, BR_BAR);
    }
    (void)ui_help_add(p, TR(LOG_DELETE), TX_HG_LOG_DELETE, BR_DELETE);
    (void)ui_help_add(p, TR(LOG_OPEN), TX_HG_LOG_OPEN, BR_OPEN);
}

/**
 * @brief The import view's page @p i.
 * @param i The page.
 * @param p The page, its side set.
 */
static void import_page(int i, ui_help_page_t *p)
{
    if (i == 0) {
        p->title = TX_HG_LOG_TITLE_READ;
        (void)ui_help_add(p, TR(LOG_DETECTED), TX_HG_LOG_DETECTED, IM_LEFT);
        (void)ui_help_add(p, TR(HG_LOG_SEP_NAME), TX_HG_LOG_SEP, IM_BTN(0));
        (void)ui_help_add(p, TR(HG_LOG_NUM_NAME), TX_HG_LOG_NUM, IM_BTN(1));
        return;
    }
    p->title = TX_HG_LOG_TITLE_COLS;
    (void)ui_help_add(p, TR(HG_LOG_COLS_NAME), TX_HG_LOG_COLS, IM_RIGHT);
    (void)ui_help_add(p, TR(LOG_BACK), TX_HG_LOG_BACK_LIST, IM_BTN(2));
    (void)ui_help_add(p, TR(LOG_PLOT), TX_HG_LOG_PLOT, IM_BTN(3));
}

/**
 * @brief The plot's page: a series chip, the plot, BACK, < and >.
 * @param p The page, its side set.
 */
static void plot_page(ui_help_page_t *p)
{
    p->title = TX_HG_LOG_TITLE_PLOT;
    if (s.n_picked > 0) {
        (void)ui_help_add(p, TR(HG_LOG_CHIP_NAME), TX_HG_LOG_CHIP,
                          gfx_rect_make(CHIP_X, CHIP_Y, CHIP_W, CHIP_H));
    }
    (void)ui_help_add(p, TR(LOG_PLOT), TX_HG_LOG_AREA, PV_PANEL);
    (void)ui_help_add(p, TR(LOG_BACK), TX_HG_LOG_BACK_IMPORT, PV_BACK);
    (void)ui_help_add(p, "< >", TX_HG_LOG_STEP, span(PV_PREV, PV_NEXT));
}

bool lv_help_page(int i, ui_help_page_t *out)
{
    if (out == NULL || i < 0 || i >= lv_help_count()) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->armed_ok = true;
    out->side = (i == 0) ? UI_HELP_DOCK_RIGHT : UI_HELP_DOCK_LEFT;
    if (s.view == VIEW_PLOT) {
        plot_page(out);
    } else if (s.view == VIEW_IMPORT) {
        import_page(i, out);
    } else {
        browse_page(i, out);
    }
    return true;
}
