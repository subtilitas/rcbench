/**
 * @file
 * @brief UPDATES' screen guide (upd_screen_int.h): two pages on UPDATES
 *        and on SINGLE UPDATES, the OPTIONS list's while it is open, or one
 *        page while the wireless devices' page is open.
 *
 * UPDATES: CHECK FOR UPDATES (CANCEL while a check runs) and the step
 * list with the dock on the right; the card, the current step's control,
 * SKIP THIS STEP or SHOW SKIPPED STEPS, CANCEL UPDATE while a device's
 * transfer runs, and OPTIONS with the dock on the left.  SINGLE UPDATES: the
 * IOmcu section, its hold and the WIRELESS DEVICES entry with the dock on
 * the right; the heading, the C6 radio's hold and BACK with the dock on
 * the left.  The devices' page: its three rows, CANCEL UPDATE while an
 * update runs, and BACK, the dock on the left.  UPDATES drives no output,
 * so every page shows while ARMED as well.
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include "ui_footer.h"
#include "ui_help.h"
#include "ui_setlist.h"
#include "ui_text.h"
#include "upd_c6.h"
#include "upd_dev.h"
#include "upd_screen.h"

#include "upd_screen_int.h"
#include "upd_single.h"
#include "upd_wiz_view.h"

/**
 * @brief The C6 radio's hold as the page names it: its label, or its
 *        progress while an update runs (upd_c6_label()).  Static: a page
 *        holds a pointer to its labels while the guide shows it.
 */
static char s_c6_label[32];

/**
 * @brief The current step's control as the page names it: its label, its
 *        phase while its update runs, or the button that opens WI-FI.
 */
static char s_act_label[96];


int upd_help_count(void)
{
    const ui_setlist_t *sl = upd_screen_settings_list();
    if (sl->open) {
        return ui_setlist_help_count(sl);
    }
    return upd_dev_shown() ? 1 : 2;
}

/**
 * @brief UPDATES' page 1: CHECK and the step list, the dock on the right.
 * @param p The page, zeroed.
 */
static void check_page(ui_help_page_t *p)
{
    p->title = TX_HG_UPD_TITLE_CHECK;
    p->side = UI_HELP_DOCK_RIGHT;
    (void)ui_help_add(p, upd_guide_cancels() ? TR(CANCEL) : TR(UPD_CHECK),
                      TX_HG_UPD_CHECK, gfx_rect_make(BX, RY, BW, BH));
    const gfx_rect_t first = upd_wiz_row_rect(0u);
    const gfx_rect_t last = upd_wiz_row_rect(UPD_WIZ_STEPS - 1u);
    (void)ui_help_add_at(p, TR(HG_UPD_STEPS_NAME), TX_HG_UPD_STEPS,
                         gfx_rect_make(first.x, first.y, first.w,
                                       last.y + last.h - first.y),
                         UI_HELP_AT_RIGHT);
}

/**
 * @brief SINGLE UPDATES' page 1: the IOmcu section and its hold, the
 *        WIRELESS DEVICES entry, the dock on the right.
 * @param p The page, zeroed.
 */
static void single_page(ui_help_page_t *p)
{
    p->title = TX_HG_UPD_TITLE_SINGLE;
    p->side = UI_HELP_DOCK_RIGHT;
    (void)ui_help_add_at(p, TR(UPD_CU_HEAD), TX_HG_UPD_CU,
                         gfx_rect_make(BX, CU_HEAD_Y, BW,
                                       UPD_DEV_HEAD_Y - 8 - CU_HEAD_Y),
                         UI_HELP_AT_RIGHT);
    (void)ui_help_add(p, TR(UPD_CU_HOLD), TX_HG_UPD_CU_HOLD, CU_BTN);
    (void)ui_help_add_at(p, TR(UPD_DEVS_HEAD), TX_HG_UPD_DEVS,
                         gfx_rect_make(UPD_DEV_X, UPD_DEV_HEAD_Y, UPD_DEV_W,
                                       UPD_DEV_BTN_Y + UPD_DEV_BTN_H
                                           - UPD_DEV_HEAD_Y),
                         UI_HELP_AT_RIGHT);
}

/**
 * @brief The devices' page: each row by its heading, around its heading,
 *        line and hold, and BACK; the dock on the left.
 * @param p The page, zeroed.
 */
static void devs_page(ui_help_page_t *p)
{
    static const ui_text_id_t k_head[UPD_DEV_COUNT] = {
        TX_UPD_DIAL_HEAD, TX_UPD_PNODE_HEAD, TX_UPD_BAL_HEAD,
    };
    static const ui_text_id_t k_text[UPD_DEV_COUNT] = {
        TX_HG_UPD_DIAL, TX_HG_UPD_PNODE, TX_HG_UPD_BAL,
    };
    p->title = TX_HG_UPD_TITLE_DEVS;
    p->side = UI_HELP_DOCK_LEFT;
    for (unsigned i = 0; i < UPD_DEV_COUNT; ++i) {
        const gfx_rect_t h = upd_dev_rect((upd_dev_id_t)i);
        (void)ui_help_add_at(p, ui_tr(k_head[i]), k_text[i],
                             gfx_rect_make(UPD_DEVP_TEXT_X, h.y,
                                           h.x + h.w - UPD_DEVP_TEXT_X, h.h),
                             UI_HELP_AT_RIGHT);
    }
    if (upd_dev_running()) {
        (void)ui_help_add(p, TR(UPD_DEVS_CANCEL), TX_HG_UPD_DEVS_CANCEL,
                          upd_dev_cancel_rect());
    }
    (void)ui_help_add(p, TR(UPD_DEVS_BACK), TX_HG_UPD_DEVS_BACK,
                      upd_dev_back_rect());
}

/**
 * @brief SINGLE UPDATES' page 2: the heading, the C6 radio's hold, BACK;
 *        the dock on the left.
 * @param p The page, zeroed.
 */
static void single_page_2(ui_help_page_t *p)
{
    p->title = TX_HG_UPD_TITLE_SINGLE_2;
    p->side = UI_HELP_DOCK_LEFT;
    (void)ui_help_add_at(p, TR(HG_UPD_SINGLE_NAME), TX_HG_UPD_SINGLE_CARD,
                         gfx_rect_make(RX, RY, RW, RH), UI_HELP_AT_RIGHT);
    upd_c6_label(upd_c6_view(), s_c6_label, sizeof(s_c6_label));
    (void)ui_help_add(p, s_c6_label, TX_HG_UPD_C6, upd_c6_rect());
    (void)ui_help_add(p, TR(UPD_DEVS_BACK), TX_HG_UPD_SINGLE_BACK,
                      upd_single_back_rect());
}

/**
 * @brief UPDATES' page 2: the card, the current step's control, SKIP or
 *        SHOW SKIPPED STEPS, CANCEL UPDATE while it stands, OPTIONS; the
 *        dock on the left.
 * @param p The page, zeroed.
 */
static void card_page(ui_help_page_t *p)
{
    upd_wiz_t w;
    upd_wiz_now(&w);
    p->title = TX_HG_UPD_TITLE_CARD;
    p->side = UI_HELP_DOCK_LEFT;
    (void)ui_help_add_at(p, TR(HG_UPD_CARD_NAME), TX_HG_UPD_CARD,
                         gfx_rect_make(UPD_WIZ_CARD_X, UPD_WIZ_CARD_Y,
                                       UPD_WIZ_CARD_W, UPD_WIZ_ACT_Y - 8
                                                           - UPD_WIZ_CARD_Y),
                         UI_HELP_AT_RIGHT);
    if (w.act != UPD_WIZ_ACT_NONE) {
        upd_wiz_act_label(&w, s_act_label, sizeof(s_act_label));
        (void)ui_help_add(p, s_act_label, TX_HG_UPD_ACT, upd_wiz_act_rect());
    } else if (upd_wiz_net_shown(&w)) {
        (void)ui_help_add(p, TR(UPD_OPEN_WIFI), TX_HG_UPD_NET,
                          upd_wiz_act_rect());
    }
    if (w.can_skip) {
        (void)ui_help_add(p, TR(UPD_WIZ_SKIP), TX_HG_UPD_SKIP,
                          upd_wiz_skip_rect());
    } else if (w.show_skipped) {
        (void)ui_help_add(p, TR(UPD_WIZ_SHOW_SKIPPED), TX_HG_UPD_SHOW_SKIPPED,
                          upd_wiz_skip_rect());
    }
    if (w.cancel) {
        (void)ui_help_add(p, TR(UPD_DEVS_CANCEL), TX_HG_UPD_DEVS_CANCEL,
                          ui_footer_rect(UI_FOOTER_HOLD));
    }
    /* OPTIONS' text starts at its left edge, beside CANCEL UPDATE's
     * slot: the badge in its top-right corner. */
    (void)ui_help_add_at(p, TR(FOOT_SETTINGS), TX_HG_UPD_OPTIONS,
                         ui_footer_rect(UI_FOOTER_SETTINGS),
                         UI_HELP_AT_RIGHT);
}

bool upd_help_page(int i, ui_help_page_t *out)
{
    const ui_setlist_t *sl = upd_screen_settings_list();
    if (sl->open) {
        return ui_setlist_help_page(sl, i, out);
    }
    if (out == NULL || i < 0 || i >= upd_help_count()) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->armed_ok = true;
    if (upd_dev_shown()) {
        devs_page(out);
    } else if (upd_single_shown()) {
        if (i == 0) {
            single_page(out);
        } else {
            single_page_2(out);
        }
    } else if (i == 0) {
        check_page(out);
    } else {
        card_page(out);
    }
    return true;
}
