/**
 * @file
 * @brief The SERVO screen's guide pages (ui_help.h): a page or two for the
 *        task on the left card, then the output card's two pages.
 *
 * A task's page takes the dock on the right and marks the left card in the
 * live half, x 0 to 528; a control that runs past x 528 is marked up to
 * there, the part the screen shows (ui_help_add()).  The output card's
 * pages take the dock on the left, over the left card, and keep the TARGET
 * row and ARM or DISARM in view: they are the pages offered while ARMED.  A
 * page whose dock would cover a control that stops an output is not
 * offered while that control shows: a task's page while the supply's
 * output is on (OUTPUT OFF on the SET row), the output card's while a run,
 * a limit run or AUTO runs (its STOP on the left card).  A keypad, a
 * keyboard, a list, a warning, a question, a programmer and a run's
 * settings take the guide away: none of them has pages.
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include "settings.h"
#include "ui_help.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

/* Last: its layout macros have short names (W, H, PAD), which no header
 * included after it may meet. */
#include "servo_screen_core_int.h"

/** @brief The most pages a state offers: a task's two and the card's two. */
#define GUIDE_MAX 4

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
 * @brief The row of k_rows on SETTINGS' page @p page that edits @p kind,
 *        and for R_SETTING @p id.
 * @param page PG_*.
 * @param kind R_*.
 * @param id   The setting, for R_SETTING.
 * @return Its rect, empty when the page has no such row shown.
 */
static gfx_rect_t row_at(int page, int kind, setting_id_t id)
{
    for (int i = 0; i < k_row_count; ++i) {
        const ov_row_t *r = &k_rows[i];
        if (r->page == page && r->kind == kind
            && (kind != R_SETTING || r->id == id) && !row_hidden(r)) {
            return row_rect(i);
        }
    }
    return (gfx_rect_t){ 0, 0, 0, 0 };
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

/** @brief LIVE: the strip, the horn's angle, the dial and the readings.
 *  @param p The page. */
static void page_live(ui_help_page_t *p)
{
    task_page(p, TX_HG_SV_TITLE_LIVE);
    (void)ui_help_add(p, TR(HG_SV_TASKS_NAME), TX_HG_SV_TASKS, strip_area());
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

/** @brief TEST: the two run cards' STARTs and SETTINGS, and the caps.
 *  @param p The page. */
static void page_test(ui_help_page_t *p)
{
    task_page(p, TX_HG_SV_TITLE_TEST);
    (void)ui_help_add(p, TR(SV_START_TEST), TX_HG_SV_START, test_start_rect());
    (void)ui_help_add(p, TR(SUP_SETTINGS), TX_HG_SV_RUN_SET, run_set_rect(0));
    (void)ui_help_add(p, TR(SV_LIM_START), TX_HG_SV_LIM, lim_start_rect());
    (void)ui_help_add(p, TR(HG_SV_CAPS_NAME), TX_HG_SV_CAPS, caps_open_rect());
}

/** @brief PROGRAM: the list of programmers, and SEARCH on its bar.
 *  @param p The page. */
static void page_prog(ui_help_page_t *p)
{
    task_page(p, TX_HG_SV_TITLE_PROG);
    prog_list_t l;
    prog_list_now(&l);
    const int shown = prog_view_shown(&s.prog_view, &l);
    gfx_rect_t rows = { 0, 0, 0, 0 };
    for (int k = 0; k < shown; ++k) {
        const gfx_rect_t r =
            prog_view_entry_rect(&s.prog_view, &l, s.prog_view.top + k);
        if (r.w > 0) {
            rows = (rows.w > 0) ? both(rows, r) : r;
        }
    }
    /* The rows' names, to x 380: the badge at their top right, between a
     * name and the port its row names at the right end. */
    if (rows.w > 0) {
        rows.w = (int16_t)(380 - rows.x);
    }
    (void)ui_help_add_at(p, TR(HG_SV_PROG_NAME), TX_HG_SV_PROG, rows,
                         UI_HELP_AT_RIGHT);
    if (prog_view_has_bar(&s.prog_view, &l)) {
        (void)ui_help_add(p, TR(SP_FIND), TX_HG_SV_FIND,
                          prog_view_search_rect(&s.prog_view, &l));
    }
}

/** @brief RUDDER: LENGTH, the drive buttons, ZERO, RESET MAX and AUTO.
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
    (void)ui_help_add(p, TR(RA_AUTO), TX_HG_SV_RUD_AUTO,
                      rudder_rect(RUD_C_AUTO));
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

/** @brief RUDDER with AUTO's box: the targets, MARGIN and TOLERANCE,
 *         START and MANUAL.
 *  @param p The page. */
static void page_auto(ui_help_page_t *p)
{
    task_page(p, TX_HG_SV_TITLE_AUTO);
    /* 2000us's box: its badge left of it, clear of both boxes' text. */
    (void)ui_help_add(p, "2000us", TX_HG_SV_RA_TARGET, rauto_rect(RA_C_HI));
    (void)ui_help_add(p, TR(HG_SV_MARGIN_NAME), TX_HG_SV_RA_MARGIN,
                      both(rauto_rect(RA_C_MARGIN), rauto_rect(RA_C_TOL)));
    (void)ui_help_add(p, TR(RA_START), TX_HG_SV_RA_START,
                      rauto_rect(RA_C_START));
    (void)ui_help_add(p, TR(RA_MANUAL), TX_HG_SV_RA_MANUAL,
                      rudder_rect(RUD_C_AUTO));
}

/** @brief SETTINGS > OUTPUT: the pages' tabs, TYPE, FRAME RATE and the
 *         ends of the pulse.
 *  @param p The page. */
static void page_output(ui_help_page_t *p)
{
    task_page(p, TX_HG_SV_TITLE_OUTPUT);
    (void)ui_help_add(p, TR(HG_SV_PAGES_NAME), TX_HG_SV_PAGES,
                      (gfx_rect_t){ OV_X + 10, OV_TAB_Y, OV_TAB_W, OV_TAB_H });
    (void)ui_help_add_at(p, TR(SV_ROW_TYPE), TX_HG_SV_ROW_TYPE,
                         row_at(PG_OUTPUT, R_TYPE, SETTING_COUNT),
                         UI_HELP_AT_RIGHT);
    (void)ui_help_add_at(p, TR(SV_ROW_RATE), TX_HG_SV_ROW_RATE,
                         row_at(PG_OUTPUT, R_RATE, SETTING_COUNT),
                         UI_HELP_AT_RIGHT);
    (void)ui_help_add_at(p, TR(SV_ROW_MIN), TX_HG_SV_ROW_MIN,
                         row_at(PG_OUTPUT, R_MIN, SETTING_COUNT),
                         UI_HELP_AT_RIGHT);
    (void)ui_help_add_at(p, TR(SV_ROW_MAX), TX_HG_SV_ROW_MAX,
                         row_at(PG_OUTPUT, R_MAX, SETTING_COUNT),
                         UI_HELP_AT_RIGHT);
}

/** @brief SETTINGS > OUTPUT's other rows: PULSE CENTRE, TRIM, TRAVEL and
 *         REVERSE.
 *  @param p The page. */
static void page_output_2(ui_help_page_t *p)
{
    task_page(p, TX_HG_SV_TITLE_OUTPUT_2);
    (void)ui_help_add(p, TR(SV_ROW_CENTRE), TX_HG_SV_ROW_CENTRE,
                      row_at(PG_OUTPUT, R_CENTRE, SETTING_COUNT));
    (void)ui_help_add(p, TR(SV_ROW_TRIM), TX_HG_SV_ROW_TRIM,
                      row_at(PG_OUTPUT, R_TRIM, SETTING_COUNT));
    (void)ui_help_add_at(p, TR(SV_ROW_TRAVEL), TX_HG_SV_ROW_TRAVEL,
                         row_at(PG_OUTPUT, R_TRAVEL, SETTING_COUNT),
                         UI_HELP_AT_RIGHT);
    (void)ui_help_add_at(p, TR(SV_ROW_REVERSE), TX_HG_SV_ROW_REVERSE,
                         row_at(PG_OUTPUT, R_REVERSE, SETTING_COUNT),
                         UI_HELP_AT_RIGHT);
}

/** @brief SETTINGS > SWEEP: CURVE, SPEED and RANGE.
 *  @param p The page. */
static void page_sweep(ui_help_page_t *p)
{
    task_page(p, TX_HG_SV_TITLE_SWEEP);
    (void)ui_help_add_at(p, TR(SV_ROW_CURVE), TX_HG_SV_ROW_CURVE,
                         row_at(PG_SWEEP, R_SETTING, SET_SERVO_CURVE),
                         UI_HELP_AT_RIGHT);
    (void)ui_help_add_at(p, TR(SV_ROW_SPEED), TX_HG_SV_ROW_SPEED,
                         row_at(PG_SWEEP, R_SETTING, SET_SERVO_TEST_HZ),
                         UI_HELP_AT_RIGHT);
    (void)ui_help_add_at(p, TR(SV_ROW_RANGE), TX_HG_SV_ROW_RANGE,
                         row_at(PG_SWEEP, R_SETTING, SET_SERVO_TEST_RANGE),
                         UI_HELP_AT_RIGHT);
}

/** @brief SETTINGS > DUT: NAME, REPORT, ENC CENTRE and the two openers.
 *  @param p The page. */
static void page_dut(ui_help_page_t *p)
{
    task_page(p, TX_HG_SV_TITLE_DUT);
    (void)ui_help_add_at(p, TR(SV_ROW_NAME), TX_HG_SV_ROW_NAME,
                         row_at(PG_DUT, R_TEXT, SETTING_COUNT),
                         UI_HELP_AT_RIGHT);
    (void)ui_help_add_at(p, TR(SV_ROW_REPORT), TX_HG_SV_ROW_REPORT,
                         row_at(PG_DUT, R_SETTING, SET_SERVO_REPORT),
                         UI_HELP_AT_RIGHT);
    (void)ui_help_add_at(p, TR(SV_ROW_ENC_CENTRE), TX_HG_SV_ROW_ENC,
                         row_at(PG_DUT, R_ENC_CENTRE, SETTING_COUNT),
                         UI_HELP_AT_RIGHT);
    (void)ui_help_add(p, "AS5600", TX_HG_SV_OPEN_ENC, enc_open_rect());
    (void)ui_help_add(p, TR(HG_SV_CAPS_NAME), TX_HG_SV_CAPS, caps_open_rect());
}

/* ------------------------------------------------- the output card's pages */

/** @brief An output card's page: the dock on the left, for ARMED too,
 *         the TARGET row kept in view.
 *  @param p The page. @param title Its title. */
static void card_page(ui_help_page_t *p, ui_text_id_t title)
{
    p->title = title;
    p->side = UI_HELP_DOCK_LEFT;
    p->armed_ok = true;
    p->must_show = (gfx_rect_t){ RC_X, TARGET_Y, RC_W, TARGET_H };
}

/** @brief The output card's top: the type, the plot or PROG 5V, SET and
 *         SPEED.
 *  @param p The page. */
static void page_card(ui_help_page_t *p)
{
    card_page(p, TX_HG_SV_TITLE_CARD);
    (void)ui_help_add(p, TR(SV_ROW_TYPE), TX_HG_SV_TYPE,
                      (gfx_rect_t){ RC_X, TAG_Y, RC_W, TAG_H });
    if (prog5v_box_shown()) {
        (void)ui_help_add(p, "PROG 5V", TX_HG_SV_P5, s.p5.r);
    } else {
        (void)ui_help_add(p, TR(MO_TAB_PLOT), TX_HG_SV_PLOT,
                          (gfx_rect_t){ RC_X, PWR_Y, RC_W, PWR_H });
    }
    (void)ui_help_add(p, TR(SV_SET), TX_HG_SV_SET,
                      (gfx_rect_t){ RC_X, SUP_Y, RC_W, SUP_H });
    (void)ui_help_add(p, TR(SV_SPEED), TX_HG_SV_SPEED,
                      (gfx_rect_t){ RC_X, SLIDER_Y, RC_W, SLIDER_H });
}

/** @brief The output card's foot: TARGET, CENTRE, SWEEP and RELEASE, and
 *         ARM or DISARM; on RUDDER also the side view, which the live half
 *         on this side shows from x 496.
 *  @param p The page. */
static void page_drive(ui_help_page_t *p)
{
    card_page(p, TX_HG_SV_TITLE_DRIVE);
    (void)ui_help_add(p, TR(SV_TARGET), TX_HG_SV_TARGET, p->must_show);
    (void)ui_help_add(p, TR(SV_CENTRE_BTN), TX_HG_SV_BUTTONS,
                      both(s.centre_btn, s.release_btn));
    (void)ui_help_add(p, s.gate.armed ? "DISARM" : "ARM", TX_HG_SV_ARM,
                      s.arm_btn);
    if (rudder_shown()) {
        (void)ui_help_add(p, TR(HG_SV_SIDE_NAME), TX_HG_SV_RUD_SIDE,
                          rudder_flap_rect());
    }
}

/* ------------------------------------------------------------- the set */

/**
 * @brief The pages SERVO offers as it is now, in order.
 * @param out The builders, GUIDE_MAX at most.
 * @return Their count, 0 under a level the guide does not explain.
 */
static int pages_now(guide_fn out[GUIDE_MAX])
{
    if (modal_open() || prog_shown() || rows_page() >= PG_COUNT) {
        return 0;
    }
    int n = 0;
    /* The task pages dock on the right, over OUTPUT OFF and PROG 5V OFF. */
    if (!supply_screen_output_live()
        && !(prog5v_box_shown() && prog5v_box_on())) {
        switch (wb_task()) {
        case UI_WB_LIVE:
            out[n++] = page_live;
            break;
        case UI_WB_TEST:
            out[n++] = page_test;
            break;
        case UI_WB_PROGRAM:
            out[n++] = page_prog;
            break;
        case UI_WB_RUDDER:
            out[n++] = rauto_shown() ? page_auto : page_rudder;
            out[n++] = page_rudder_read;
            break;
        case UI_WB_SETTINGS:
            if (s.tabs.selected == PG_OUTPUT) {
                out[n++] = page_output;
                out[n++] = page_output_2;
            } else if (s.tabs.selected == PG_SWEEP) {
                out[n++] = page_sweep;
            } else {
                out[n++] = page_dut;
            }
            break;
        default:
            break;
        }
    }
    if (!run_live()) {
        out[n++] = page_card;
        out[n++] = page_drive;
    }
    return n;
}

int servo_help_count(void)
{
    guide_fn f[GUIDE_MAX];
    return pages_now(f);
}

bool servo_help_page(int i, ui_help_page_t *out)
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
