/**
 * @file
 * @brief WI-FI's screen guide (wifi_screen_int.h): three pages with the
 *        radio off, one or two with it on.
 *
 * Radio off: ACCESS POINT, JOIN, CHOOSE and FORGET HOME NETWORK and ACCESS
 * CODE ON or OFF with the dock on the right; HOTSPOT PASSWORD and ACCESS
 * CODE RANDOM or KEPT with the dock on the right; the card with the dock on
 * the left.  Radio on: the rows, the lines under them and WI-FI OFF with
 * the dock on the right; the browser writes' button, while it stands, with
 * the dock on the left.  The network list, a keyboard, a kept value's view
 * and the question about an uploaded image leave no page.  WI-FI drives no
 * output, so every page shows while ARMED as well.
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include "ui_help.h"
#include "ui_text.h"
#include "wifi_screen.h"

#include "wifi_screen_int.h"

/** @brief The radio-off buttons' count: ACCESS POINT to ACCESS CODE. */
#define WF_BUTTONS 7

/** @brief The rows of the running view while the page is served: three
 *         and the access code's. */
#define WF_ROWS 4

/**
 * @brief Button @p i of the radio-off view.
 * @param i 0 ACCESS POINT to 6 ACCESS CODE RANDOM or KEPT.
 * @return Its rect, in the body.
 */
static gfx_rect_t button(int i)
{
    return gfx_rect_make(BX, BY + i * BPITCH, BW, BH);
}

/**
 * @brief Whether the page is served: the access point up or the network
 *        joined, so the rows and the access code's row show.
 * @param v The view.
 * @return True then.
 */
static bool served(const wifi_view_t *v)
{
    return v->radio == WIFI_RADIO_AP || v->radio == WIFI_RADIO_JOINED;
}

int wifi_help_count(void)
{
    switch (wifi_guide_view()) {
    case WIFI_GUIDE_OFF:
        return 3;
    case WIFI_GUIDE_ON:
        return wifi_guide_writes() ? 2 : 1;
    case WIFI_GUIDE_NONE:
    default:
        return 0;
    }
}

/**
 * @brief A page of the radio-off view: the first five buttons, the two
 *        kept values' buttons, or the card.
 * @param i The page.
 * @param v The view.
 * @param p The page, zeroed.
 */
static void off_page(int i, const wifi_view_t *v, ui_help_page_t *p)
{
    if (i == 0) {
        static const ui_text_id_t k_label[5] = { TX_WF_AP, TX_WF_JOIN,
                                                 TX_WF_CHOOSE, TX_WF_FORGET,
                                                 TX_WF_CODE_ON };
        static const ui_text_id_t k_text[5] = { TX_HG_WF_AP, TX_HG_WF_JOIN,
                                                TX_HG_WF_CHOOSE,
                                                TX_HG_WF_FORGET,
                                                TX_HG_WF_CODE };
        p->title = TX_HG_WF_TITLE_START;
        p->side = UI_HELP_DOCK_RIGHT;
        for (int k = 0; k < 5; ++k) {
            const ui_text_id_t label = (k == 4 && !v->code_on)
                                           ? TX_WF_CODE_OFF : k_label[k];
            (void)ui_help_add(p, ui_tr(label), k_text[k], button(k));
        }
        return;
    }
    if (i == 1) {
        p->title = TX_HG_WF_TITLE_KEPT;
        p->side = UI_HELP_DOCK_RIGHT;
        (void)ui_help_add(p, wifi_guide_kept(true)
                                 ? TR(WF_KEPT_PASS_SET)
                                 : TR(WF_KEPT_PASS_RANDOM),
                          TX_HG_WF_PASS, button(WF_BUTTONS - 2));
        (void)ui_help_add(p, wifi_guide_kept(false)
                                 ? TR(WF_KEPT_CODE_SET)
                                 : TR(WF_KEPT_CODE_RANDOM),
                          TX_HG_WF_KEPT_CODE, button(WF_BUTTONS - 1));
        return;
    }
    p->title = TX_HG_WF_TITLE_STATE;
    p->side = UI_HELP_DOCK_LEFT;
    (void)ui_help_add_at(p, TR(HG_WF_CARD_NAME), TX_HG_WF_CARD,
                         gfx_rect_make(RX, RY, RW, RH), UI_HELP_AT_RIGHT);
}

/**
 * @brief A page of the radio-on view: the rows, the lines and WI-FI OFF,
 *        or the browser writes' button.
 * @param i The page.
 * @param v The view.
 * @param p The page, zeroed.
 */
static void on_page(int i, const wifi_view_t *v, ui_help_page_t *p)
{
    if (i == 1) {
        p->title = TX_HG_WF_TITLE_WRITES;
        p->side = UI_HELP_DOCK_LEFT;
        (void)ui_help_add(p, v->writes_open ? TR(WF_WRITES_STOP)
                                            : TR(WF_HOLD_WRITES),
                          TX_HG_WF_WRITES,
                          gfx_rect_make(WR_X, OFF_Y, BW, BH));
        return;
    }
    p->title = TX_HG_WF_TITLE_ON;
    p->side = UI_HELP_DOCK_RIGHT;
    /* The lines start under the rows while the page is served, under the
     * line that names the start or the join otherwise. */
    int lines_y = ROW_Y;
    if (served(v)) {
        (void)ui_help_add_at(p, TR(WF_NETWORK), TX_HG_WF_ROWS,
                             gfx_rect_make(LBL_X, ROW_Y, W - 2 * LBL_X,
                                           WF_ROWS * ROW_P),
                             UI_HELP_AT_RIGHT);
        lines_y = ROW_Y + WF_ROWS * ROW_P + 4;
    }
    (void)ui_help_add_at(p, TR(HG_WF_LINES_NAME), TX_HG_WF_LINES,
                         gfx_rect_make(LBL_X, lines_y, W - 2 * LBL_X,
                                       OFF_Y - 8 - lines_y),
                         UI_HELP_AT_RIGHT);
    (void)ui_help_add(p, TR(WF_OFF), TX_HG_WF_OFF,
                      gfx_rect_make(BX, OFF_Y, BW, BH));
}

bool wifi_help_page(int i, ui_help_page_t *out)
{
    if (out == NULL || i < 0 || i >= wifi_help_count()) {
        return false;
    }
    const wifi_view_t *v = wifi_screen_view();
    memset(out, 0, sizeof(*out));
    out->armed_ok = true;
    if (wifi_guide_view() == WIFI_GUIDE_OFF) {
        off_page(i, v, out);
    } else {
        on_page(i, v, out);
    }
    return true;
}
