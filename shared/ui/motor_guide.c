/**
 * @file
 * @brief MOTOR & ESC's guide pages (ui_help.h): LIVE's or PROGRAM's page on
 *        the left column, then the rail's; over the OPTIONS list the list's
 *        two (ui_setlist_help_page()); over ESC STICK its own
 *        (esc_stick_help_page()).
 *
 * The left column's page takes the dock on the right and marks the column
 * in the live half, x 0 to 528; a control that runs past x 528 is marked
 * up to there (ui_help_add()).  The rail's page takes the dock on the left
 * and keeps the TARGET row in view.  Either dock covers part of ARM and
 * DISARM in the footer's middle slot, x 348 to 676, so no page is offered
 * while the bench is not SAFE as this screen sees it: ARMED, an ARM asked
 * and unanswered, or a DISARM not yet answered.  RENAME LOG's keyboard
 * and the signal-wire session have no pages.
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include "esc_stick_panel.h"
#include "ui_footer.h"
#include "ui_help.h"
#include "ui_setlist.h"
#include "ui_text.h"

#include "motor_live_int.h"

/** @brief The most pages a state offers: the column's and the rail's. */
#define GUIDE_MAX 2

/** @brief The most PROGRAM rows a page marks: the two of the registry. */
#define GUIDE_ROWS 2

/** @brief A page's builder: fills @p out, a zeroed page. */
typedef void (*guide_fn)(ui_help_page_t *out);

/**
 * @brief A left column's page: the dock on the right, not for ARMED.
 * @param p     The page.
 * @param title Its title.
 */
static void column_page(ui_help_page_t *p, ui_text_id_t title)
{
    p->title = title;
    p->side = UI_HELP_DOCK_RIGHT;
    p->armed_ok = false;
    (void)ui_help_add(p, TR(WB_LIVE), TX_HG_MO_TABS, mwb_strip_area());
}

/**
 * @brief RENAME LOG in the strip row, right of LIVE | PROGRAM.
 * @return Its rect.
 */
static gfx_rect_t log_rect(void)
{
    return (gfx_rect_t){ MO_LOG_X, MO_STRIP_Y, MO_LOG_W, MO_STRIP_H };
}

/** @brief LIVE: the tabs, PLOT | TABLE, the throttle and RENAME LOG.
 *
 *  PLOT | TABLE is marked 8px wider than its two tabs, so the badge at its
 *  right end sits past TABLE's label and left of the poll rate at x 232.
 *  The throttle's mark is its row of keys and track, the badge at the
 *  live half's edge over the track's empty end, clear of the card's tag
 *  and of the readout above it.
 *  @param p The page. */
static void page_live(ui_help_page_t *p)
{
    column_page(p, TX_HG_MO_TITLE_LIVE);
    gfx_rect_t tabs = s.tabs.rect[0];
    const gfx_rect_t last = s.tabs.rect[s.tabs.count - 1];
    tabs.w = (int16_t)(last.x + last.w + 8 - tabs.x);
    (void)ui_help_add_at(p, TR(MO_TAB_PLOT), TX_HG_MO_PLOT, tabs,
                         UI_HELP_AT_RIGHT);
    (void)ui_help_add_at(p, TR(MO_THROTTLE), TX_HG_MO_THROTTLE,
                         (gfx_rect_t){ s.down_rect.x, s.down_rect.y,
                                       (int16_t)(s.up_rect.x + s.up_rect.w
                                                 - s.down_rect.x),
                                       s.down_rect.h },
                         UI_HELP_AT_RIGHT);
    (void)ui_help_add(p, TR(MO_LOG_RENAME), TX_HG_MO_LOG, log_rect());
}

/** @brief PROGRAM: the tabs, each row of the list and RENAME LOG.
 *  @param p The page. */
static void page_program(ui_help_page_t *p)
{
    column_page(p, TX_HG_MO_TITLE_PROG);
    for (int k = 0; k < GUIDE_ROWS; ++k) {
        gfx_rect_t r;
        const prog_reg_row_t *row = NULL;
        if (!mpp_row(k, &r, &row)) {
            break;
        }
        /* The badge at the row's right end, clear of its name. */
        (void)ui_help_add_at(p, ui_tr(row->name),
                             (row->page_set == &esc_stick_pages)
                                 ? TX_HG_MO_STICK : TX_HG_MO_WIRE,
                             r, UI_HELP_AT_RIGHT);
    }
    (void)ui_help_add(p, TR(MO_LOG_RENAME), TX_HG_MO_LOG, log_rect());
}

/** @brief The rail: TARGET, the readings, RESET PEAKS, and the footer's
 *         ARM and OPTIONS; the dock on the left, TARGET kept in view.
 *  @param p The page. */
static void page_control(ui_help_page_t *p)
{
    p->title = TX_HG_MO_TITLE_CTL;
    p->side = UI_HELP_DOCK_LEFT;
    p->armed_ok = false;
    p->must_show = mwb_target_rect();
    (void)ui_help_add(p, TR(SV_TARGET), TX_HG_MO_TARGET, p->must_show);
    (void)ui_help_add(p, TR(MO_VOLT), TX_HG_MO_READ,
                      (gfx_rect_t){ RIGHT_X, HERO_Y0, RIGHT_W,
                                    4 * HERO_H + 3 * HERO_GAP });
    (void)ui_help_add(p, TR(MO_RESET_PEAKS), TX_HG_MO_RESET, s.reset_rect);
    (void)ui_help_add(p, "ARM", TX_HG_MO_ARM, s.arm_rect);
    (void)ui_help_add(p, TR(FOOT_SETTINGS), TX_HG_MO_OPTIONS,
                      ui_footer_rect(UI_FOOTER_SETTINGS));
}

/**
 * @brief The pages LIVE and PROGRAM's list offer as they are now, in
 *        order.
 * @param out The builders, GUIDE_MAX at most.
 * @return Their count, 0 under RENAME LOG's keyboard or off SAFE.
 */
static int pages_now(guide_fn out[GUIDE_MAX])
{
    if (mln_shown() || !mo_bench_safe()) {
        return 0;
    }
    out[0] = (mwb_task() == UI_WB_PROGRAM) ? page_program : page_live;
    out[1] = page_control;
    return GUIDE_MAX;
}

/**
 * @brief Whether ESC STICK is the session open on PROGRAM.
 * @return True while it is.
 */
static bool stick_shown(void)
{
    return mpp_shown() == &esc_stick_pages;
}

int mo_help_count(void)
{
    if (msp_shown()) {
        return ui_setlist_help_count(msp_list());
    }
    if (mpp_shown() != NULL) {
        return stick_shown() ? esc_stick_help_count() : 0;
    }
    guide_fn f[GUIDE_MAX];
    return pages_now(f);
}

bool mo_help_page(int i, ui_help_page_t *out)
{
    if (msp_shown()) {
        return ui_setlist_help_page(msp_list(), i, out);
    }
    if (mpp_shown() != NULL) {
        return stick_shown() && esc_stick_help_page(i, out);
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
