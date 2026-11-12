/**
 * @file
 * @brief The SERVO screens' guide pages (ui_help.h): the hub's (servo_hub.c),
 *        a SETTINGS list's (ui_setlist.h), or on a function screen a page or
 *        two for its function on the left card, then its READY card's.
 *
 * A function's page takes the dock on the right and marks the left card in
 * the live half, x 0 to 528; a control that runs past x 528 is marked up
 * to there, the part the screen shows (ui_help_add()).  The right card's
 * pages take the dock on the left, over the left card, and keep the PORT
 * button in view: on RUDDER AUTO, TEST and LIMIT RUN the READY card's
 * page, on LIVE, SWEEP and RUDDER the READY rows' page and the controls'
 * page under them.  On KST and HITEC, whose session fills the left card,
 * the session's pages come first, the dock on the right: on the SERVO tab
 * two, CONNECT and the read, then the files, the write switch and WRITE;
 * on another tab one, its values.  They are not offered while the READY
 * card's supply row switches a live output off, which their dock would
 * cover, or while an editor of the session is open.  The READY card's
 * page with WRITE n VALUES follows.  While Hitec's MOVE SERVO drives the
 * horn no page is offered: its ON, x 468 to 618 on TRAVEL, lies under
 * the right dock from x 536 and under the left dock's last 20px to x 488.  No page is offered while ARMED: either dock covers part of DISARM in the footer's
 * middle slot.  A page whose dock would cover a control that stops an
 * output is not offered while that control shows: a READY card's page
 * while a run, a limit run or AUTO runs, or while ARM's steps run (the
 * footer's hold stops them).  A keypad, a keyboard, a choice list, a
 * warning, a question and a page of rows take the guide away: none of
 * them has pages.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include "hitec_move.h"
#include "hitec_panel.h"
#include "kst_panel.h"
#include "settings.h"
#include "ui_footer.h"
#include "ui_help.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

/* Last: its layout macros have short names (W, H, PAD), which no header
 * included after it may meet. */
#include "servo_screen_core_int.h"

/** @brief The most pages a state offers: a task's two and the card's two. */
#define GUIDE_MAX 4

/** @brief The SERVO tab of either programmer: KST's page 0, HITEC_PANEL_SERVO. */
#define PRG_TAB_SERVO 0

/** @brief The rows a programmer's tab holds at most: 5. */
#define PRG_ROWS 5

/** @brief The tabs of either programmer: 5, KST's LOCKED included. */
#define PRG_TABS 5

/** @brief A session's page: CONNECT and the read, the files and WRITE, or a
 *         tab's values. */
enum { PRG_PG_CONNECT = 0, PRG_PG_WRITE, PRG_PG_VALUES, PRG_PG_COUNT };

/** @brief The bytes a session's label is kept in: 40, a German row label
 *         of 29 characters and its umlauts. */
#define PRG_LABEL 40

/**
 * @brief The labels of the session's pages, copied: KST's and HITEC's
 *        accessors return a buffer their next call writes over, and a page
 *        holds its labels while the guide shows it.  A slot per page and
 *        item, so building one page leaves another's labels as they were.
 */
static char s_prg_label[PRG_PG_COUNT][UI_HELP_ITEMS][PRG_LABEL];

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
 * @brief Whether a run, a limit run or AUTO runs: its STOP is on the left
 *        card, where the output card's pages put the dock.
 * @return True while one runs.
 */
static bool run_live(void)
{
    return servo_test_running(&s.test) || servo_limrun_running(&s.lim)
           || servo_rauto_running(&s.ra.run);
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

/** @brief LIVE and SWEEP: the horn's angle, the dial and the readings.
 *  @param p The page. */
static void page_live(ui_help_page_t *p)
{
    task_page(p, (s.fn == SERVO_FN_SWEEP) ? TX_HG_SV_TITLE_SWEEP
                                          : TX_HG_SV_TITLE_LIVE);
    /* The angle's box at its widest, -888.8, and a badge's width more,
     * so the badge at its right end clears "deg". */
    const gfx_seg_style_t seg = ui_seg_hero();
    const int aw = gfx_seg_width("-888.8", &seg) + 6
                   + gfx_text_width(SV_FONT_LABEL, "deg", 1) + 4 + 36;
    (void)ui_help_add_at(p, TR(HG_SV_ANGLE_NAME), TX_HG_SV_ANGLE,
                         (gfx_rect_t){ SEG_X - 2, SEG_Y - 2, (int16_t)aw,
                                       (int16_t)(seg.digit_h + 8) },
                         UI_HELP_AT_RIGHT);
    (void)ui_help_add(p, TR(HG_SV_DIAL_NAME), TX_HG_SV_DIAL,
                      (gfx_rect_t){ SHAFT_X, SHAFT_Y - ARC_R - 10, ARC_R + 40,
                                    2 * ARC_R + 20 });
    /* From 34px over the labels, where the card is empty: the badge sits
     * there, clear of COMMANDED. */
    (void)ui_help_add(p, TR(HG_SV_READ_NAME), TX_HG_SV_READ,
                      (gfx_rect_t){ READ_X(0), READ_LBL_Y - 34,
                                    READ_W0 + READ_W1 + READ_W2,
                                    READ_NUM_Y + READ_NUM_H - READ_LBL_Y
                                        + 34 });
}

/** @brief TEST: the run card's lines, the caps and the rail's plot.
 *  @param p The page. */
static void page_test(ui_help_page_t *p)
{
    task_page(p, TX_HG_SV_TITLE_TEST);
    (void)ui_help_add(p, TR(SV_AUTO_TEST), TX_HG_SV_START, test_card_rect());
    (void)ui_help_add(p, TR(HG_SV_CAPS_NAME), TX_HG_SV_CAPS, caps_open_rect());
    (void)ui_help_add(p, TR(MO_TAB_PLOT), TX_HG_SV_PLOT, test_plot_rect());
}

/** @brief LIMIT RUN: LOW END and HIGH END, and the state lines.
 *  @param p The page. */
static void page_limrun(ui_help_page_t *p)
{
    task_page(p, TX_HG_SV_TITLE_LIMRUN);
    (void)ui_help_add(p, TR(LR_LOW_END), TX_HG_SV_LR_ENDS,
                      lim_ends_rect());
    (void)ui_help_add_at(p, TR(HG_SV_LR_LINES_NAME), TX_HG_SV_LR_LINES,
                         lim_lines_rect(), UI_HELP_AT_RIGHT);
}

/** @brief RUDDER: LENGTH, the drive buttons, ZERO, RESET MAX and, without
 *         a knob that answers, -10us and +10us.
 *  @param p The page. */
static void page_rudder(ui_help_page_t *p)
{
    task_page(p, TX_HG_SV_TITLE_RUDDER);
    (void)ui_help_add(p, TR(RUD_LENGTH), TX_HG_SV_RUD_LEN,
                      rudder_len_value_rect());
    (void)ui_help_add(p, "1000us", TX_HG_SV_RUD_DRIVE,
                      both(rudder_rect(RUD_C_1000), rudder_rect(RUD_C_2000)));
    (void)ui_help_add(p, TR(RUD_ZERO), TX_HG_SV_RUD_ZERO,
                      rudder_rect(RUD_C_ZERO));
    (void)ui_help_add(p, TR(RUD_RESET_MAX), TX_HG_SV_RUD_RESET,
                      rudder_rect(RUD_C_RESET));
    if (rudder_steps_shown()) {
        (void)ui_help_add(p, TR(RUD_STEP_MINUS), TX_HG_SV_RUD_STEP,
                          both(rudder_rect(RUD_C_MINUS),
                               rudder_rect(RUD_C_PLUS)));
    }
}

/** @brief RUDDER's readings: DEFLECTION, TRAILING EDGE and the state
 *         lines.
 *  @param p The page. */
static void page_rudder_read(ui_help_page_t *p)
{
    task_page(p, TX_HG_SV_TITLE_RUD_READ);
    const gfx_rect_t ro = rudder_readout_rect();
    const int half = ro.w / 2;
    (void)ui_help_add_at(p, TR(RUD_DEFL), TX_HG_SV_RUD_DEFL,
                         (gfx_rect_t){ ro.x, ro.y, (int16_t)(half - 10),
                                       ro.h },
                         UI_HELP_AT_RIGHT);
    (void)ui_help_add_at(p, TR(RUD_EDGE), TX_HG_SV_RUD_EDGE,
                         (gfx_rect_t){ (int16_t)(ro.x + half), ro.y,
                                       (int16_t)(ro.w - half), ro.h },
                         UI_HELP_AT_RIGHT);
    if (!s.ra.view) {
        (void)ui_help_add_at(p, TR(HG_SV_ANGLE_NAME), TX_HG_SV_RUD_STATE,
                             rudder_state_rect(), UI_HELP_AT_RIGHT);
    }
}

/** @brief RUDDER AUTO: the targets, MARGIN and TOLERANCE and ZERO.
 *  @param p The page. */
static void page_auto(ui_help_page_t *p)
{
    task_page(p, TX_HG_SV_TITLE_AUTO);
    /* 2000us's box: its badge left of it, clear of both boxes' text. */
    (void)ui_help_add(p, "2000us", TX_HG_SV_RA_TARGET, rauto_rect(RA_C_HI));
    (void)ui_help_add(p, TR(HG_SV_MARGIN_NAME), TX_HG_SV_RA_MARGIN,
                      both(rauto_rect(RA_C_MARGIN), rauto_rect(RA_C_TOL)));
    (void)ui_help_add(p, TR(RUD_ZERO), TX_HG_SV_RUD_ZERO,
                      rudder_rect(RUD_C_ZERO));
}

/** @brief RUDDER AUTO's, TEST's or LIMIT RUN's READY card, its rail line,
 *         ARM + START and SETTINGS: the dock on the left, the PORT button
 *         kept in view.
 *  @param p The page. */
static void page_ready(ui_help_page_t *p)
{
    p->title = TX_HG_SV_TITLE_READY;
    p->side = UI_HELP_DOCK_LEFT;
    p->armed_ok = false;
    p->must_show = port_rect();
    (void)ui_help_add(p, TR(SVH_PORT), TX_HG_SV_TARGET, p->must_show);
    (void)ui_help_add(p, TR(RDY_TITLE), TX_HG_SV_READY,
                      both(ready_row_rect(0),
                           ready_row_rect(ready_count() - 1)));
    (void)ui_help_add(p, TR(RDY_METER), TX_HG_SV_RDY_RAIL, ready_rail_rect());
    (void)ui_help_add(p, "ARM + START",
                      (s.fn == SERVO_FN_TEST) ? TX_HG_SV_TEST_START
                                              : TX_HG_SV_RA_START,
                      ready_hold_rect());
    (void)ui_help_add(p, TR(FOOT_SETTINGS), TX_HG_SV_SETTINGS,
                      ui_footer_rect(UI_FOOTER_SETTINGS));
}

/** @brief KST's and HITEC's READY card, its rail line, the footer's
 *         WRITE n VALUES and SETTINGS: the dock on the left, over the
 *         session, the PORT button kept in view.
 *  @param p The page. */
static void page_prog(ui_help_page_t *p)
{
    p->title = TX_HG_SV_TITLE_PRG;
    p->side = UI_HELP_DOCK_LEFT;
    p->armed_ok = false;
    p->must_show = port_rect();
    (void)ui_help_add(p, TR(SVH_PORT), TX_HG_SV_PRG_PORT, p->must_show);
    (void)ui_help_add(p, TR(RDY_TITLE), TX_HG_SV_PRG_READY,
                      both(ready_row_rect(0),
                           ready_row_rect(prog_ready_count() - 1)));
    (void)ui_help_add(p, TR(RDY_METER), TX_HG_SV_PRG_RAIL, ready_rail_rect());
    (void)ui_help_add(p, TR(HG_SV_PRG_WRITE_NAME), TX_HG_SV_PRG_WRITE,
                      s.arm_btn);
    (void)ui_help_add(p, TR(FOOT_SETTINGS), TX_HG_SV_SETTINGS,
                      ui_footer_rect(UI_FOOTER_SETTINGS));
}

/* ------------------------------------------------- the programmers' pages */

/**
 * @brief Whether the session shown is KST's, not HITEC's.
 * @return True on KST.
 */
static bool prg_kst(void)
{
    return s.fn == SERVO_FN_KST;
}

/**
 * @brief A tab of the session shown, as its accessors give it.
 * @param page The tab.
 * @return Its rect; empty for a tab not shown.
 */
static gfx_rect_t prg_tab(int page)
{
    return prg_kst() ? kst_panel_tab_rect(page) : hitec_panel_tab_rect(page);
}

/**
 * @brief A row's control on a tab of the session shown.
 * @param page The tab.
 * @param row  The row, from the top.
 * @param col  The column, 0 or 1.
 * @return Its rect; empty for no row there.
 */
static gfx_rect_t prg_value(int page, int row, int col)
{
    return prg_kst() ? kst_panel_value_rect(page, row, col)
                     : hitec_panel_value_rect(page, row, col);
}

/**
 * @brief The text on a row's button, or the value it shows.
 * @param page The tab.
 * @param row  The row.
 * @param col  The column.
 * @return The text; "" for no row there.
 */
static const char *prg_button(int page, int row, int col)
{
    const char *t = prg_kst() ? kst_panel_button(page, row, col)
                              : hitec_panel_value(page, row, col);
    return (t != NULL) ? t : "";
}

/**
 * @brief A row's label as it is drawn.
 * @param page The tab.
 * @param row  The row.
 * @param col  The column.
 * @return The label; "" for no row there.
 */
static const char *prg_label(int page, int row, int col)
{
    const char *t = prg_kst() ? kst_panel_label(page, row, col)
                              : hitec_panel_label(page, row, col);
    return (t != NULL) ? t : "";
}

/**
 * @brief The tab shown of the session shown.
 * @return The tab: PRG_TAB_SERVO, or a tab of values.
 */
static int prg_page(void)
{
    return prg_kst() ? kst_panel_page() : hitec_panel_page();
}

/**
 * @brief Whether the READY card's supply row reads ON: a tap on it
 *        switches a live output off (prog_ready_stops_at()).
 * @return True then.
 */
static bool prg_supply_live(void)
{
    rdy_row_t r;
    return prog_ready_row(prog_ready_at(PRR_SUPPLY), &r)
           && r.st == (uint8_t)RDY_LIVE;
}

/**
 * @brief Whether HITEC's MOVE SERVO drives the horn: its ON, the tap that
 *        stops it, spans x 468 to 618 on TRAVEL, under either dock.
 * @return True while the loop runs on HITEC.
 */
static bool prg_moving(void)
{
    return !prg_kst() && hitec_prog_move_on(hitec_panel_model());
}

/**
 * @brief Whether the session's own pages are offered: its overlay open
 *        with no keypad, list, page or keyboard over it; and no output
 *        live through the READY card's supply row, the PD mini or PROG
 *        5V, whose OFF the dock on the right covers.
 * @return True when they are.
 */
static bool prg_guided(void)
{
    if (prg_kst() ? (!kst_panel_is_open() || kst_panel_editor() != 0)
                  : (!hitec_panel_is_open() || hitec_panel_editor() != 0)) {
        return false;
    }
    return !prg_supply_live() && !supply_screen_output_live()
           && !(prog5v_box_shown() && prog5v_box_on());
}

/**
 * @brief A session's page: the dock on the right, over the READY card,
 *        not for ARMED.
 * @param p     The page.
 * @param title Its title.
 */
static void prg_page_init(ui_help_page_t *p, ui_text_id_t title)
{
    p->title = title;
    p->side = UI_HELP_DOCK_RIGHT;
    p->armed_ok = false;
}

/**
 * @brief Add an item to session page @p pg, its label copied to the
 *        page's slot for it, its badge in the corner @p at.
 * @param p     The page.
 * @param pg    The page's slots, PRG_PG_*.
 * @param label The control's name.
 * @param text  What it does.
 * @param r     Where it is.
 * @param at    The badge's corner: UI_HELP_AT_RIGHT for the tabs and the
 *              notes, whose text starts at their left edge.
 */
static void prg_add_at(ui_help_page_t *p, int pg, const char *label,
                       ui_text_id_t text, gfx_rect_t r, ui_help_at_t at)
{
    if (p->count >= UI_HELP_ITEMS || label == NULL || label[0] == '\0') {
        return;
    }
    char *slot = s_prg_label[pg][p->count];
    snprintf(slot, PRG_LABEL, "%s", label);
    (void)ui_help_add_at(p, slot, text, r, at);
}

/**
 * @brief prg_add_at() with the badge in the top-left corner.
 * @param p     The page.
 * @param pg    The page's slots, PRG_PG_*.
 * @param label The control's name.
 * @param text  What it does.
 * @param r     Where it is.
 */
static void prg_add(ui_help_page_t *p, int pg, const char *label,
                    ui_text_id_t text, gfx_rect_t r)
{
    prg_add_at(p, pg, label, text, r, UI_HELP_AT_LEFT);
}

/**
 * @brief The tabs, as one rect across the tabs shown.
 * @param p  The page.
 * @param pg The page's slots, PRG_PG_*.
 */
static void prg_add_tabs(ui_help_page_t *p, int pg)
{
    gfx_rect_t all = prg_tab(PRG_TAB_SERVO);
    for (int t = PRG_TAB_SERVO + 1; t < PRG_TABS; ++t) {
        const gfx_rect_t r = prg_tab(t);
        if (r.w > 0) {
            all = both(all, r);
        }
    }
    prg_add_at(p, pg, TR(HG_PRG_TABS_NAME), TX_HG_PRG_TABS, all,
               UI_HELP_AT_RIGHT);
}

/**
 * @brief The notes and the message under the rows, where any is drawn.
 * @param p  The page.
 * @param pg The page's slots, PRG_PG_*.
 */
static void prg_add_notes(ui_help_page_t *p, int pg)
{
    const gfx_rect_t r = prg_kst() ? kst_panel_notes_drawn()
                                   : hitec_panel_notes_drawn();
    if (r.w > 0 && r.h > 0) {
        prg_add_at(p, pg, TR(HG_PRG_NOTES_NAME), TX_HG_PRG_NOTES, r,
                   UI_HELP_AT_RIGHT);
    }
}

/**
 * @brief A control of the SERVO tab, under the name @p label.
 * @param p     The page.
 * @param pg    The page's slots, PRG_PG_*.
 * @param label The name.
 * @param text  What it does.
 * @param row   The row on the SERVO tab.
 * @param col   The column.
 */
static void prg_add_row(ui_help_page_t *p, int pg, const char *label,
                        ui_text_id_t text, int row, int col)
{
    const gfx_rect_t r = prg_value(PRG_TAB_SERVO, row, col);
    if (r.w > 0) {
        prg_add(p, pg, label, text, r);
    }
}

/** @brief KST's or HITEC's SERVO tab: the tabs, CONNECT, the read, TYPE or
 *         SERIES (FILES on HITEC once its CONNECT opened the port), the
 *         notes.
 *  @param p The page. */
static void page_prg_connect(ui_help_page_t *p)
{
    const bool kst = prg_kst();
    prg_page_init(p, kst ? TX_HG_KST_TITLE_CONNECT : TX_HG_HT_TITLE_CONNECT);
    prg_add_tabs(p, PRG_PG_CONNECT);
    prg_add_row(p, PRG_PG_CONNECT, prg_button(PRG_TAB_SERVO, 0, 0),
                TX_HG_PRG_CONNECT, 0, 0);
    if (kst) {
        prg_add_row(p, PRG_PG_CONNECT, prg_button(PRG_TAB_SERVO, 1, 0),
                    TX_HG_PRG_READ, 1, 0);
        prg_add_row(p, PRG_PG_CONNECT, prg_label(PRG_TAB_SERVO, 1, 1),
                    TX_HG_KST_TYPE, 1, 1);
    } else {
        /* SERIES until this panel's CONNECT opened the port, FILES in its
         * place then: the text follows the row shown. */
        prg_add_row(p, PRG_PG_CONNECT, prg_label(PRG_TAB_SERVO, 1, 0),
                    hitec_panel_files_shown() ? TX_HG_HT_FILES
                                              : TX_HG_HT_SERIES, 1, 0);
        prg_add_row(p, PRG_PG_CONNECT, prg_button(PRG_TAB_SERVO, 1, 1),
                    TX_HG_PRG_READ, 1, 1);
    }
    prg_add_notes(p, PRG_PG_CONNECT);
}

/** @brief KST's or HITEC's SERVO tab, its lower rows: FILES and MORE on
 *         KST, MORE and AFTER STOP on HITEC; the write switch, RESTORE and
 *         WRITE n VALUES.
 *  @param p The page. */
static void page_prg_write(ui_help_page_t *p)
{
    const bool kst = prg_kst();
    prg_page_init(p, kst ? TX_HG_KST_TITLE_WRITE : TX_HG_HT_TITLE_WRITE);
    if (kst) {
        prg_add_row(p, PRG_PG_WRITE, prg_label(PRG_TAB_SERVO, 2, 0),
                    TX_HG_KST_FILES, 2, 0);
        prg_add_row(p, PRG_PG_WRITE, prg_label(PRG_TAB_SERVO, 2, 1),
                    TX_HG_KST_MORE, 2, 1);
    } else {
        prg_add_row(p, PRG_PG_WRITE, prg_label(PRG_TAB_SERVO, 2, 0),
                    TX_HG_HT_MORE, 2, 0);
        prg_add_row(p, PRG_PG_WRITE, prg_label(PRG_TAB_SERVO, 2, 1),
                    TX_HG_HT_CLEAR, 2, 1);
    }
    prg_add_row(p, PRG_PG_WRITE, prg_label(PRG_TAB_SERVO, 3, 0),
                TX_HG_PRG_ENABLE, 3, 0);
    prg_add_row(p, PRG_PG_WRITE, prg_label(PRG_TAB_SERVO, 4, 0),
                kst ? TX_HG_KST_RESTORE : TX_HG_HT_RESTORE, 4, 0);
    prg_add_row(p, PRG_PG_WRITE, prg_button(PRG_TAB_SERVO, 4, 1),
                TX_HG_PRG_WRITE, 4, 1);
}

/** @brief A tab of values: the tabs, its first value shown on a row
 *         without keys, the keys of its first stepped row on KST, MOVE
 *         SERVO on HITEC's TRAVEL, the notes.  A stepped row is marked
 *         whole, keys and value, so its badge sits clear of the value's.
 *  @param p The page. */
static void page_prg_values(ui_help_page_t *p)
{
    const bool kst = prg_kst();
    const int tab = prg_page();
    prg_page_init(p, kst ? TX_HG_KST_TITLE_VALUES : TX_HG_HT_TITLE_VALUES);
    prg_add_tabs(p, PRG_PG_VALUES);
    bool value = false, keys = false;
    for (int k = 0; k < 2 * PRG_ROWS; ++k) {
        const int row = k / 2, col = k % 2;
        const gfx_rect_t r = prg_value(tab, row, col);
        if (r.w <= 0 || (!kst && tab == HITEC_PANEL_TRAVEL && row == 4)) {
            continue;
        }
        /* A stepped row, wide in column 0, has its value between its
         * keys, right of the minus key; any other row's value starts
         * where the keys would. */
        const gfx_rect_t minus = (kst && col == 0)
                                     ? kst_panel_step_rect(tab, row, false)
                                     : r;
        if (minus.x < r.x) {
            if (!keys) {
                prg_add(p, PRG_PG_VALUES, "- +", TX_HG_PRG_STEP,
                        both(minus, kst_panel_step_rect(tab, row, true)));
                keys = true;
            }
        } else if (!value) {
            prg_add(p, PRG_PG_VALUES, prg_label(tab, row, col),
                    TX_HG_PRG_VALUE, r);
            value = true;
        }
    }
    if (!kst && tab == HITEC_PANEL_TRAVEL) {
        const gfx_rect_t mv = hitec_panel_value_rect(tab, 4, 0);
        if (mv.w > 0) {
            prg_add(p, PRG_PG_VALUES, prg_label(tab, 4, 0), TX_HG_HT_MOVE,
                    mv);
        }
    }
    prg_add_notes(p, PRG_PG_VALUES);
}

/**
 * @brief The session's pages offered now, before the READY card's.
 * @param out The builders, two at most.
 * @return Their count.
 */
static int prg_pages(guide_fn out[2])
{
    if (!prg_guided()) {
        return 0;
    }
    if (prg_page() == PRG_TAB_SERVO) {
        out[0] = page_prg_connect;
        out[1] = page_prg_write;
        return 2;
    }
    out[0] = page_prg_values;
    return 1;
}

/* ------------------------------------------------- the output card's pages */

/** @brief A right card's page: the dock on the left, the PORT button
 *         kept in view.  Not while ARMED: either dock covers part of
 *         DISARM in the footer's middle slot.
 *  @param p The page. @param title Its title. */
static void card_page(ui_help_page_t *p, ui_text_id_t title)
{
    p->title = title;
    p->side = UI_HELP_DOCK_LEFT;
    p->armed_ok = false;
    p->must_show = port_rect();
}

/** @brief LIVE's, SWEEP's and RUDDER's READY rows, the rail line, ARM
 *         and SETTINGS: the dock on the left, the PORT button kept in
 *         view.  @param p The page. */
static void page_drive_ready(ui_help_page_t *p)
{
    card_page(p, TX_HG_SV_TITLE_DRV_READY);
    (void)ui_help_add(p, TR(SVH_PORT), TX_HG_SV_TARGET, p->must_show);
    (void)ui_help_add(p, TR(RDY_TITLE), TX_HG_SV_DRV_READY,
                      both(ready_row_rect(0),
                           ready_row_rect(ready_count() - 1)));
    (void)ui_help_add(p, TR(RDY_METER), TX_HG_SV_DRV_RAIL,
                      ready_rail_rect());
    (void)ui_help_add(p, "ARM", TX_HG_SV_DRV_ARM, s.arm_btn);
    if (svs_list_offered()) {
        (void)ui_help_add(p, TR(FOOT_SETTINGS), TX_HG_SV_SETTINGS,
                          ui_footer_rect(UI_FOOTER_SETTINGS));
    }
}

/** @brief The controls under the rows: SET V and SET A and the plot on
 *         all three; SPEED, the type pill and the buttons on LIVE and
 *         SWEEP; the pill and the side view on RUDDER, which the live half
 *         on this side shows from x 496.
 *  @param p The page. */
static void page_drive_ctl(ui_help_page_t *p)
{
    card_page(p, TX_HG_SV_TITLE_CARD);
    /* SET V and SET A where the PD mini feeds the servo; on the carrier's
     * programming port the row reads the fixed 5V and sets nothing. */
    if (drive_set_rect(0).w > 0) {
        (void)ui_help_add(p, "SET V", TX_HG_SV_DRV_SET,
                          both(drive_set_rect(0), drive_set_rect(1)));
    }
    (void)ui_help_add(p, TR(MO_TAB_PLOT), TX_HG_SV_DRV_PLOT,
                      drive_plot_rect());
    if (rudder_shown()) {
        (void)ui_help_add(p, TR(SV_ROW_TYPE), TX_HG_SV_TYPE,
                          drive_tag_rect());
        (void)ui_help_add(p, TR(HG_SV_SIDE_NAME), TX_HG_SV_RUD_SIDE,
                          rudder_flap_rect());
        return;
    }
    (void)ui_help_add(p, TR(SV_SPEED), TX_HG_SV_SPEED, drive_speed_rect());
    (void)ui_help_add(p, TR(SV_ROW_TYPE), TX_HG_SV_TYPE, drive_tag_rect());
    (void)ui_help_add(p, TR(SV_CENTER_BTN), TX_HG_SV_BUTTONS,
                      both(s.fn == SERVO_FN_SWEEP ? s.sweep_btn
                                                  : s.center_btn,
                           s.release_btn));
}

/* ------------------------------------------------------------- the set */

/**
 * @brief The pages SERVO offers as it is now, in order.
 * @param out The builders, GUIDE_MAX at most.
 * @return Their count, 0 under a level the guide does not explain.
 */
static int pages_now(guide_fn out[GUIDE_MAX])
{
    if (modal_open() || rows_page() >= PG_COUNT) {
        return 0;
    }
    int n = 0;
    /* KST and HITEC: the session's pages, then the READY card's with
     * WRITE n VALUES; none while MOVE SERVO's ON, which stops the horn,
     * sits under either dock. */
    if (prog_ready_shown()) {
        if (prg_moving()) {
            return 0;
        }
        n = prg_pages(out);
        out[n++] = page_prog;
        return n;
    }
    /* RUDDER AUTO, TEST and LIMIT RUN: their own pages, none while the
     * hold stops a sequence or a run (either dock covers part of it). */
    if (ready_shown()) {
        if (!run_live() && !svs_seq_busy()) {
            if (s.fn == SERVO_FN_TEST) {
                out[n++] = page_test;
            } else if (s.fn == SERVO_FN_LIMRUN) {
                out[n++] = page_limrun;
            } else {
                out[n++] = page_auto;
                out[n++] = page_rudder_read;
            }
            out[n++] = page_ready;
        }
        return n;
    }
    /* LIVE, SWEEP and RUDDER: none while ARM's steps run, which a press
     * on the hold stops (either dock covers part of it). */
    if (drive_shown() && svs_seq_busy()) {
        return 0;
    }
    /* The function's pages dock on the right, over the right card, which
     * on LIVE, SWEEP and RUDDER stops no output. */
    if ((drive_shown() || (!supply_screen_output_live()
                           && !(prog5v_box_shown() && prog5v_box_on())))
        && !prog_shown()) {
        switch ((servo_fn_t)s.fn) {
        case SERVO_FN_LIVE:
        case SERVO_FN_SWEEP:
            out[n++] = page_live;
            break;
        case SERVO_FN_RUDDER:
            out[n++] = page_rudder;
            out[n++] = page_rudder_read;
            break;
        default:
            break;
        }
    }
    if (drive_shown()) {
        out[n++] = page_drive_ready;
        out[n++] = page_drive_ctl;
    }
    return n;
}

int servo_help_count(void)
{
    if (s.fn == SERVO_FN_HUB) {
        return hub_help_count();
    }
    if (svs_list_shown()) {
        return ui_setlist_help_count(&s.sl);
    }
    guide_fn f[GUIDE_MAX];
    return pages_now(f);
}

bool servo_help_page(int i, ui_help_page_t *out)
{
    if (s.fn == SERVO_FN_HUB) {
        return hub_help_page(i, out);
    }
    if (svs_list_shown()) {
        return ui_setlist_help_page(&s.sl, i, out);
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
