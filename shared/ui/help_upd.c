/**
 * @file
 * @brief UPDATES' screen guide (upd_screen_int.h): two pages, the
 *        OPTIONS list's while it is open, or one page while the wireless
 *        devices' page is open.
 *
 * CHECK FOR UPDATES (CANCEL while a check runs), the IOmcu section, its
 * hold and the WIRELESS DEVICES entry with the dock on the right; the
 * card, the C6 radio's hold, the download's hold while a check found a
 * newer release, and OPTIONS with the dock on the left.  The devices'
 * page: its three rows, CANCEL UPDATE while an update runs, and BACK, the
 * dock on the left.  UPDATES drives no
 * output, so every page shows while ARMED as well.
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

/**
 * @brief The C6 radio's hold as the page names it: its label, or its
 *        progress while an update runs (upd_c6_label()).  Static: a page
 *        holds a pointer to its labels while the guide shows it.
 */
static char s_c6_label[32];


int upd_help_count(void)
{
    const ui_setlist_t *sl = upd_screen_settings_list();
    if (sl->open) {
        return ui_setlist_help_count(sl);
    }
    return upd_dev_shown() ? 1 : 2;
}

/**
 * @brief Page 1: CHECK, the IOmcu section and its hold, the DIAL FIRMWARE
 *        row, the dock on the right.
 * @param p The page, zeroed.
 */
static void check_page(ui_help_page_t *p)
{
    p->title = TX_HG_UPD_TITLE_CHECK;
    p->side = UI_HELP_DOCK_RIGHT;
    (void)ui_help_add(p, upd_guide_cancels() ? TR(CANCEL) : TR(UPD_CHECK),
                      TX_HG_UPD_CHECK, gfx_rect_make(BX, RY, BW, BH));
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
 * @brief Page 2: the card, the C6 radio's hold, the download's hold while
 *        it stands, OPTIONS; the dock on the left.
 * @param p The page, zeroed.
 */
static void card_page(ui_help_page_t *p)
{
    p->title = TX_HG_UPD_TITLE_CARD;
    p->side = UI_HELP_DOCK_LEFT;
    (void)ui_help_add_at(p, TR(HG_UPD_CARD_NAME), TX_HG_UPD_CARD,
                         gfx_rect_make(RX, RY, RW, RH), UI_HELP_AT_RIGHT);
    upd_c6_label(upd_c6_view(), s_c6_label, sizeof(s_c6_label));
    (void)ui_help_add(p, s_c6_label, TX_HG_UPD_C6, upd_c6_rect());
    if (upd_guide_newer()) {
        (void)ui_help_add(p, TR(UPD_HOLD_DL), TX_HG_UPD_DL,
                          ui_footer_rect(UI_FOOTER_HOLD));
    }
    /* OPTIONS' text starts at its left edge, beside the download's
     * hold: the badge in its top-right corner. */
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
    } else if (i == 0) {
        check_page(out);
    } else {
        card_page(out);
    }
    return true;
}
