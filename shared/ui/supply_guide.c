/**
 * @file
 * @brief The SUPPLY screen's guide pages (ui_help.h): LIVE's page on the
 *        left column, then the rail's two pages; over the SETTINGS list the
 *        list's two (ui_setlist_help_page()).
 *
 * LIVE's page takes the dock on the right and marks the left column in
 * the live half, x 0 to 528; a control that runs past x 528 is marked up
 * to there, the part the screen shows (ui_help_add()).  The rail's pages
 * take the dock on the left, x 8 to 488, ARMED too; CONTROL's marks the
 * footer's OUTPUT ON and SETTINGS.  OUTPUT OFF, the footer's middle slot
 * at x 348 to 676, lies under either dock, so no page is offered while the
 * output is on or an ON is on its way.  The keypad and the question about
 * a live output take the guide away: neither has pages.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include "settings.h"
#include "supply_screen.h"
#include "ui_footer.h"
#include "ui_help.h"
#include "ui_setlist.h"
#include "ui_text.h"
#include "ui_widgets.h"

#include "supply_screen_int.h"

/** @brief The most pages a state offers: LIVE's and the rail's two. */
#define GUIDE_MAX 3

/** @brief A page's builder: fills @p out, a zeroed page. */
typedef void (*guide_fn)(ui_help_page_t *out);

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
 * @brief Set point @p row's track, between its step keys.
 * @param row 0 the voltage, 1 the current limit.
 * @return The track.
 */
static gfx_rect_t track(int row)
{
    const gfx_rect_t v = sup_set_value_rect(row);
    return (gfx_rect_t){ SET_TRACK_X, v.y, SET_TRACK_W, v.h };
}

/**
 * @brief The set point range at the strip's right end, as the header
 *        draws it, right-aligned at the body's margin.
 * @return Its rect, the strip's height.
 */
static gfx_rect_t range_rect(void)
{
    char range[64];
    snprintf(range, sizeof(range), "%.1f-%.1fV  %.2f-%.2fA",
             (double)s.eff.v_min, (double)s.eff.v_max,
             (double)s.eff.i_min, (double)s.eff.i_max);
    const int w = gfx_text_width(SU_FONT_FINE, range, 1) + 8;
    return (gfx_rect_t){ (int16_t)(W - PAD - w), SU_STRIP_Y, (int16_t)w,
                         SU_STRIP_H };
}

/* ------------------------------------------------------- the task pages */

/** @brief A task's page: the dock on the right, not for ARMED.
 *  @param p The page. @param title Its title. */
static void task_page(ui_help_page_t *p, ui_text_id_t title)
{
    p->title = title;
    p->side = UI_HELP_DOCK_RIGHT;
    p->armed_ok = false;
}

/** @brief LIVE: PLOT and TABLE and the two set points.
 *  @param p The page. */
static void page_live(ui_help_page_t *p)
{
    task_page(p, TX_HG_SU_TITLE_LIVE);
    /* Without PROG 5V beside them, 40px of the empty row more, where the
     * badge sits clear of PLOT and TABLE. */
    gfx_rect_t tabs = both(s.tabs.rect[0], s.tabs.rect[s.tabs.count - 1]);
    if (!sup_p5_shown()) {
        tabs.w = (int16_t)(tabs.w + 40);
    }
    (void)ui_help_add_at(p, TR(MO_TAB_PLOT), TX_HG_SU_PLOT, tabs,
                         sup_p5_shown() ? UI_HELP_AT_LEFT : UI_HELP_AT_RIGHT);
    (void)ui_help_add_at(p, TR(SUP_VOLTAGE), TX_HG_SU_VOLTAGE, track(0),
                         UI_HELP_AT_RIGHT);
    (void)ui_help_add_at(p, TR(SUP_CURRENT_LIMIT), TX_HG_SU_CURRENT, track(1),
                         UI_HELP_AT_RIGHT);
}

/* ---------------------------------------------------------- the rail's */

/** @brief The rail's readings: the range in force and the four cards.
 *         VOLT, CURR and PWR are marked from 6px left of the card and 55px
 *         tall, so the badge sits left of it, inside the left column's
 *         edge; MODE's mark ends left of CV with the badge at its top
 *         right, clear of the plot's END beside it.
 *  @param p The page. */
static void page_readings(ui_help_page_t *p)
{
    p->title = TX_HG_SU_TITLE_READ;
    p->side = UI_HELP_DOCK_LEFT;
    p->armed_ok = true;
    (void)ui_help_add(p, TR(HG_SU_RANGE_NAME), TX_HG_SU_RANGE, range_rect());
    static const ui_text_id_t k_name[4] = { TX_MO_VOLT, TX_MO_CURR,
                                            TX_MO_PWR, TX_SUP_MODE };
    static const ui_text_id_t k_text[4] = { TX_HG_SU_VOLT, TX_HG_SU_CURR,
                                            TX_HG_SU_PWR, TX_HG_SU_MODE };
    for (int k = 0; k < 3; ++k) {
        gfx_rect_t r = sup_card_rect(k);
        r.x = (int16_t)(r.x - 6);
        r.w = (int16_t)(r.w + 6);
        r.h = 55;
        (void)ui_help_add(p, ui_tr(k_name[k]), k_text[k], r);
    }
    gfx_rect_t mode = sup_card_rect(3);
    mode.w = 300;
    (void)ui_help_add_at(p, ui_tr(k_name[3]), k_text[3], mode,
                         UI_HELP_AT_RIGHT);
}

/** @brief The rail's CONTROL card, the set points' values and the footer's
 *         OUTPUT ON and SETTINGS.
 *  @param p The page. */
static void page_control(ui_help_page_t *p)
{
    p->title = TX_HG_SU_TITLE_CONTROL;
    p->side = UI_HELP_DOCK_LEFT;
    p->armed_ok = true;
    (void)ui_help_add(p, TR(SUP_SET_POINTS), TX_HG_SU_VALUES,
                      both(sup_set_value_rect(0), sup_set_value_rect(1)));
    (void)ui_help_add(p, TR(MO_RESET_PEAKS), TX_HG_SU_RESET, s.reset_rect);
    (void)ui_help_add(p, s.on ? TR(SUP_OUTPUT_OFF) : TR(SUP_OUTPUT_ON),
                      TX_HG_SU_OUTPUT, s.out_rect);
    (void)ui_help_add(p, TR(FOOT_SETTINGS), TX_HG_SU_SETTINGS,
                      ui_footer_rect(UI_FOOTER_SETTINGS));
}

/* ------------------------------------------------------------- the set */

/**
 * @brief The pages SUPPLY offers as it is now, in order.
 * @param out The builders, GUIDE_MAX at most.
 * @return Their count, 0 under the keypad or the question.
 */
static int pages_now(guide_fn out[GUIDE_MAX])
{
    if (s.kp.open || s.confirm_open) {
        return 0;
    }
    /* OUTPUT OFF, the footer's middle slot, lies under either dock: no
     * page while the output is on or an ON is on its way. */
    if (s.on || s.on_asked) {
        return 0;
    }
    int n = 0;
    /* PROG 5V OFF is under a dock on the right. */
    if (!(sup_p5_shown() && prog5v_box_on())) {
        out[n++] = page_live;
    }
    out[n++] = page_readings;
    out[n++] = page_control;
    return n;
}

int supply_help_count(void)
{
    if (sup_list_shown()) {
        return ui_setlist_help_count(supply_settings_list());
    }
    guide_fn f[GUIDE_MAX];
    return pages_now(f);
}

bool supply_help_page(int i, ui_help_page_t *out)
{
    if (sup_list_shown()) {
        return ui_setlist_help_page(supply_settings_list(), i, out);
    }
    guide_fn f[GUIDE_MAX];
    const int n = pages_now(f);
    if (out == NULL || i < 0 || i >= n) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    f[i](out);
    return true;
}
