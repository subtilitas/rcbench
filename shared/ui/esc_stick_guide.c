/**
 * @file
 * @brief ESC STICK's guide pages (ui_help.h), which MOTOR & ESC offers
 *        while the session is open: two on the list of makers or models,
 *        two on a profile's page, the OPTIONS list's two over it.
 *
 * The list's first page takes the dock on the right and marks BACK and a
 * row; the second takes the left and marks SEARCH, "^" and "v" and the
 * count under the rows.  Neither page covers a control that stops an
 * output, so both show while ARMED.  A profile's first page takes the
 * dock on the right and marks BACK, an item and the note that says why
 * RUN does not start; the second takes the left and marks "-" and "+",
 * the side column's "^" and "v", MANUAL INTERVENTION REQUIRED, RUN and
 * OPTIONS.  Either dock covers part of RUN's slot, which a run turns into
 * ABORT: no page is offered while a run is under way or its result shows,
 * and none while ARMED.  The search's keyboard, the RUN warning and the
 * manual steps have no pages.
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include "esc_stick_panel.h"
#include "ui_help.h"
#include "ui_setlist.h"

#include "esc_stick_panel_int.h"

/** @brief The pages a state offers. */
#define GUIDE_PAGES 2

/**
 * @brief The smallest rect that holds @p a and @p b.
 * @param a A rect.
 * @param b Another.
 * @return Both.
 */
static gfx_rect_t both(gfx_rect_t a, gfx_rect_t b)
{
    const int x0 = (a.x < b.x) ? a.x : b.x;
    const int y0 = (a.y < b.y) ? a.y : b.y;
    const int x1 = (a.x + a.w > b.x + b.w) ? a.x + a.w : b.x + b.w;
    const int y1 = (a.y + a.h > b.y + b.h) ? a.y + a.h : b.y + b.h;
    return (gfx_rect_t){ (int16_t)x0, (int16_t)y0, (int16_t)(x1 - x0),
                         (int16_t)(y1 - y0) };
}

/**
 * @brief Whether the pages of the list or of a profile show: the session
 *        open on either, nothing over it, no run under way or shown.
 * @return True when they do.
 */
static bool pages_shown(void)
{
    const stick_t *t = &s.st;
    if (pg.stage == STAGE_CLOSED || t->hand_open || t->warn) {
        return false;
    }
    if (pg.stage == STAGE_LIST) {
        return !t->tk.open;
    }
    return t->p != NULL && !esc_stick_running(&t->run) && !t->shown;
}

/**
 * @brief The list's first page: BACK and the first row, the dock on the
 *        right.
 * @param p The page.
 */
static void list_page(ui_help_page_t *p)
{
    p->title = TX_HG_ES_TITLE_LIST;
    p->side = UI_HELP_DOCK_RIGHT;
    (void)ui_help_add(p, TR(LOG_BACK), TX_HG_ES_BACK, pg.back);
    if (s.st.count > 0) {
        (void)ui_help_add(p, TR(HG_SET_ROW_NAME),
                          (s.st.level == 0) ? TX_HG_ES_MAKER
                                            : TX_HG_ES_MODEL,
                          sp_row(0));
    }
}

/**
 * @brief The list's second page: SEARCH, "^" and "v" and the count under
 *        the rows, the dock on the left.
 * @param p The page.
 */
static void find_page(ui_help_page_t *p)
{
    p->title = TX_HG_ES_TITLE_FIND;
    p->side = UI_HELP_DOCK_LEFT;
    (void)ui_help_add(p, TR(SP_FIND), TX_HG_ES_FIND, s.st.find_box);
    (void)ui_help_add(p, "^ v", TX_HG_ES_PAGES,
                      both(s.st.list_up, s.st.list_dn));
    (void)ui_help_add(p, TR(HG_ES_COUNT_NAME), TX_HG_ES_COUNT,
                      (gfx_rect_t){ (int16_t)(W - PAD - 12 - SP_COUNT_W),
                                    SP_FOOT_Y, SP_COUNT_W, 20 });
}

/**
 * @brief A profile's first page: BACK, the first item and the note, the
 *        dock on the right.
 * @param p The page.
 */
static void items_page(ui_help_page_t *p)
{
    p->title = TX_HG_ES_TITLE_ITEMS;
    p->side = UI_HELP_DOCK_RIGHT;
    (void)ui_help_add(p, TR(LOG_BACK), TX_HG_ES_BACK, pg.back);
    if (sp_items() > 0) {
        (void)ui_help_add_at(p, TR(HG_SET_ROW_NAME), TX_HG_ES_ITEM,
                             pg.row[0], UI_HELP_AT_RIGHT);
    }
    (void)ui_help_add_at(p, TR(HG_ES_NOTE_NAME), TX_HG_ES_NOTE,
                         (gfx_rect_t){ (int16_t)(PAD + 12), NOTE_Y,
                                       (int16_t)(W - 2 * PAD - 24),
                                       2 * LINE_P },
                         UI_HELP_AT_RIGHT);
}

/**
 * @brief A profile's second page: the first item's "-" and "+", the side
 *        column's "^" and "v", MANUAL INTERVENTION REQUIRED where the
 *        profile has manual steps, RUN and OPTIONS, the dock on the left.
 *        A profile with no item offered has neither steppers nor RUN.
 * @param p The page.
 */
static void run_page(ui_help_page_t *p)
{
    p->title = TX_HG_ES_TITLE_RUN;
    p->side = UI_HELP_DOCK_LEFT;
    const bool items = sp_items() > 0;
    if (items) {
        (void)ui_help_add(p, "- +", TX_HG_ES_STEP,
                          both(pg.down[0], pg.up[0]));
    }
    (void)ui_help_add(p, "^ v", TX_HG_ES_SCROLL,
                      both(pg.page_up, pg.page_dn));
    if (s.st.p->manual_count > 0u) {
        (void)ui_help_add(p, TR(SP_HAND_BTN), TX_HG_ES_HAND, s.st.hand_btn);
    }
    if (items) {
        (void)ui_help_add(p, TR(SP_RUN_BTN), TX_HG_ES_RUN, pg.write_btn);
    }
    (void)ui_help_add(p, TR(FOOT_SETTINGS), TX_HG_ES_OPTIONS,
                      ui_footer_rect(UI_FOOTER_SETTINGS));
}

int esc_stick_help_count(void)
{
    if (sp_set_shown()) {
        return ui_setlist_help_count(esc_stick_settings_list());
    }
    return pages_shown() ? GUIDE_PAGES : 0;
}

bool esc_stick_help_page(int i, ui_help_page_t *out)
{
    if (sp_set_shown()) {
        return ui_setlist_help_page(esc_stick_settings_list(), i, out);
    }
    if (out == NULL || i < 0 || i >= esc_stick_help_count()) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    if (pg.stage == STAGE_LIST) {
        out->armed_ok = true;
        if (i == 0) {
            list_page(out);
        } else {
            find_page(out);
        }
        return true;
    }
    if (i == 0) {
        items_page(out);
    } else {
        run_page(out);
    }
    return true;
}
