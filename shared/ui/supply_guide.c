/**
 * @file
 * @brief The SUPPLY screen's guide pages (ui_help.h): the task's page on
 *        the left column, then the rail's two pages.
 *
 * A task's page takes the dock on the right and marks the left column in
 * the live half, x 0 to 528; a control that runs past x 528 is marked up
 * to there, the part the screen shows.  Its dock covers the rail with
 * OUTPUT OFF, so it is not offered while the output is on or an ON is on
 * its way.  The rail's pages take the dock on the left, over the left
 * column, and show on LIVE and SETTINGS alike, ARMED too.  The keypad and
 * the question about a live output take the guide away: neither has
 * pages.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include "settings.h"
#include "ui_help.h"
#include "ui_text.h"
#include "ui_widgets.h"

#include "supply_screen_int.h"

/** @brief The most pages a state offers: a task's two and the rail's two. */
#define GUIDE_MAX 4

/** @brief A page's builder: fills @p out, a zeroed page. */
typedef void (*guide_fn)(ui_help_page_t *out);

/**
 * @brief Append an item to @p p.
 * @param p     The page.
 * @param label The control's name.
 * @param text  What it does.
 * @param r     Where it is.
 * @param at    The badge's corner.
 */
static void item(ui_help_page_t *p, const char *label, ui_text_id_t text,
                 gfx_rect_t r, ui_help_at_t at)
{
    if (p->count < UI_HELP_ITEMS && r.w > 0 && r.h > 0) {
        p->item[p->count++] = (ui_help_item_t){ label, text, r, at };
    }
}

/**
 * @brief @p r cut at the right edge of the live half a dock on the right
 *        leaves, x 528: the part of a control the screen shows there.
 * @param r The control.
 * @return The part, empty when none shows.
 */
static gfx_rect_t shown_part(gfx_rect_t r)
{
    const gfx_rect_t live = ui_help_live_rect(UI_HELP_DOCK_RIGHT);
    const int right = live.x + live.w;
    if (r.x + r.w > right) {
        r.w = (int16_t)((right > r.x) ? right - r.x : 0);
    }
    return r;
}

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
 * @brief SETTINGS' row that edits @p id: its value box.
 * @param id The setting.
 * @return The box, empty for a setting the page does not show.
 */
static gfx_rect_t value_of(setting_id_t id)
{
    for (int i = 0; i < ROW_COUNT; ++i) {
        if (k_rows[i].id == id) {
            return sup_row_value_rect(i);
        }
    }
    return (gfx_rect_t){ 0, 0, 0, 0 };
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

/** @brief LIVE: the strip, PLOT and TABLE, and the two set points.
 *  @param p The page. */
static void page_live(ui_help_page_t *p)
{
    task_page(p, TX_HG_SU_TITLE_LIVE);
    item(p, TR(HG_SV_TASKS_NAME), TX_HG_SU_TASKS,
         shown_part(sup_strip_area()), UI_HELP_AT_LEFT);
    /* Without PROG 5V beside them, 40px of the empty row more, where the
     * badge sits clear of PLOT and TABLE. */
    gfx_rect_t tabs = both(s.tabs.rect[0], s.tabs.rect[s.tabs.count - 1]);
    if (!sup_p5_shown()) {
        tabs.w = (int16_t)(tabs.w + 40);
    }
    item(p, TR(MO_TAB_PLOT), TX_HG_SU_PLOT, tabs,
         sup_p5_shown() ? UI_HELP_AT_LEFT : UI_HELP_AT_RIGHT);
    item(p, TR(SUP_VOLTAGE), TX_HG_SU_VOLTAGE, track(0), UI_HELP_AT_RIGHT);
    item(p, TR(SUP_CURRENT_LIMIT), TX_HG_SU_CURRENT, track(1),
         UI_HELP_AT_RIGHT);
}

/** @brief SETTINGS > LIMITS, the left column: the tabs, the caps, the
 *         start values and RESET PD MINI.
 *  @param p The page. */
static void page_limits(ui_help_page_t *p)
{
    task_page(p, TX_HG_SU_TITLE_LIMITS);
    item(p, TR(SUP_TAB_LIMITS), TX_HG_SU_TABS,
         shown_part(both(s.ov_tabs.rect[0],
                         s.ov_tabs.rect[s.ov_tabs.count - 1])),
         UI_HELP_AT_LEFT);
    item(p, TR(SUP_CAPS), TX_HG_SU_CAPS,
         both(value_of(SET_SUPPLY_V_MAX), value_of(SET_SUPPLY_I_MAX)),
         UI_HELP_AT_LEFT);
    item(p, TR(SUP_AFTER_RESTART), TX_HG_SU_START,
         both(value_of(SET_SUPPLY_V_START), value_of(SET_SUPPLY_I_START)),
         UI_HELP_AT_LEFT);
    if (!s.model) {
        item(p, TR(SUP_RESET_PDMINI), TX_HG_SU_RESET_PD, s.modreset_btn,
             UI_HELP_AT_LEFT);
    }
}

/** @brief SETTINGS > LIMITS, the right column: the trips and the
 *         questions, the dock on the left over the left column.
 *  @param p The page. */
static void page_trips(ui_help_page_t *p)
{
    p->title = TX_HG_SU_TITLE_TRIPS;
    p->side = UI_HELP_DOCK_LEFT;
    p->armed_ok = true;
    item(p, TR(SUP_ROW_TRIP_I), TX_HG_SU_TRIP_I, value_of(SET_SUPPLY_TRIP_I),
         UI_HELP_AT_LEFT);
    item(p, TR(SUP_ROW_TRIP_V), TX_HG_SU_TRIP_V, value_of(SET_SUPPLY_TRIP_V),
         UI_HELP_AT_LEFT);
    item(p, TR(SUP_ROW_TRIP_MS), TX_HG_SU_TRIP_MS,
         value_of(SET_SUPPLY_TRIP_MS), UI_HELP_AT_LEFT);
    item(p, TR(SUP_ROW_CONFIRM_SLIDE), TX_HG_SU_ASK_SLIDE,
         value_of(SET_SUPPLY_CONFIRM_SLIDE), UI_HELP_AT_LEFT);
    item(p, TR(SUP_ROW_CONFIRM_KEYS), TX_HG_SU_ASK_KEYS,
         value_of(SET_SUPPLY_CONFIRM_KEYS), UI_HELP_AT_LEFT);
}

/** @brief The PD MINI tab's rows' settings, in sup_pd_row_rect()'s order. */
static const setting_id_t k_pd[SUP_PD_ROWS] = {
    SET_PDMINI_EN, SET_PDMINI_TX, SET_PDMINI_RX, SET_PDMINI_BAUD,
};

/**
 * @brief PD MINI row @p i's controls as far as the screen shows them: its
 *        - key to its + key, or its value alone for the switch and a pin
 *        soldered on the board.
 * @param i The row.
 * @return The rect.
 */
static gfx_rect_t pd_controls(int i)
{
    gfx_rect_t r = sup_pd_value_rect(i);
    if (sup_pd_has_keys(i)) {
        r = both(sup_pd_key_rect(i, false), sup_pd_key_rect(i, true));
        /* From 10px into the - key, so the badge left of it clears the
         * row's help line, which runs to x 354 in German. */
        r.x = (int16_t)(r.x + 10);
        r.w = (int16_t)(r.w - 10);
    }
    return shown_part(r);
}

/** @brief SETTINGS > PD MINI: the module and its wiring.
 *  @param p The page. */
static void page_pdmini(ui_help_page_t *p)
{
    task_page(p, TX_HG_SU_TITLE_PDMINI);
    static const ui_text_id_t k_text[SUP_PD_ROWS] = {
        TX_HG_SU_PD_EN, TX_HG_SU_PD_TX, TX_HG_SU_PD_RX, TX_HG_SU_PD_BAUD,
    };
    for (int i = 0; i < SUP_PD_ROWS; ++i) {
        item(p, ui_setting_label(k_pd[i]), k_text[i], pd_controls(i),
             UI_HELP_AT_LEFT);
    }
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
    item(p, TR(HG_SU_RANGE_NAME), TX_HG_SU_RANGE, range_rect(),
         UI_HELP_AT_LEFT);
    static const ui_text_id_t k_name[4] = { TX_MO_VOLT, TX_MO_CURR,
                                            TX_MO_PWR, TX_SUP_MODE };
    static const ui_text_id_t k_text[4] = { TX_HG_SU_VOLT, TX_HG_SU_CURR,
                                            TX_HG_SU_PWR, TX_HG_SU_MODE };
    for (int k = 0; k < 3; ++k) {
        gfx_rect_t r = sup_card_rect(k);
        r.x = (int16_t)(r.x - 6);
        r.w = (int16_t)(r.w + 6);
        r.h = 55;
        item(p, ui_tr(k_name[k]), k_text[k], r, UI_HELP_AT_LEFT);
    }
    gfx_rect_t mode = sup_card_rect(3);
    mode.w = 300;
    item(p, ui_tr(k_name[3]), k_text[3], mode, UI_HELP_AT_RIGHT);
}

/** @brief The rail's CONTROL card and, on LIVE, the set points' values.
 *  @param p The page. */
static void page_control(ui_help_page_t *p)
{
    p->title = TX_HG_SU_TITLE_CONTROL;
    p->side = UI_HELP_DOCK_LEFT;
    p->armed_ok = true;
    if (sup_on_live()) {
        item(p, TR(SUP_SET_POINTS), TX_HG_SU_VALUES,
             both(sup_set_value_rect(0), sup_set_value_rect(1)),
             UI_HELP_AT_LEFT);
    }
    item(p, TR(MO_RESET_PEAKS), TX_HG_SU_RESET, s.reset_rect,
         UI_HELP_AT_LEFT);
    item(p, s.on ? TR(SUP_OUTPUT_OFF) : TR(SUP_OUTPUT_ON), TX_HG_SU_OUTPUT,
         s.out_rect, UI_HELP_AT_LEFT);
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
    int n = 0;
    /* OUTPUT OFF is under a dock on the right. */
    const bool right_ok = !s.on && !s.on_asked;
    if (sup_on_live()) {
        if (right_ok) {
            out[n++] = page_live;
        }
    } else if (sup_on_pdmini()) {
        if (right_ok) {
            out[n++] = page_pdmini;
        }
    } else {
        if (right_ok) {
            out[n++] = page_limits;
        }
        out[n++] = page_trips;
    }
    out[n++] = page_readings;
    out[n++] = page_control;
    return n;
}

int supply_help_count(void)
{
    guide_fn f[GUIDE_MAX];
    return pages_now(f);
}

bool supply_help_page(int i, ui_help_page_t *out)
{
    guide_fn f[GUIDE_MAX];
    const int n = pages_now(f);
    if (out == NULL || i < 0 || i >= n) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    f[i](out);
    return true;
}
