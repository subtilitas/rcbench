/**
 * @file
 * @brief The SERVO screens' guide pages (ui_help.h): the hub's (servo_hub.c),
 *        a SETTINGS list's (ui_setlist.h), or on a function screen a page or
 *        two for its function on the left card, then the output card's two
 *        pages.
 *
 * A function's page takes the dock on the right and marks the left card in
 * the live half, x 0 to 528; a control that runs past x 528 is marked up
 * to there, the part the screen shows (ui_help_add()).  The output card's
 * pages take the dock on the left, over the left card, and keep the PORT
 * button in view.  On RUDDER AUTO the READY card's page takes their place;
 * on KST and HITEC, whose session fills the left card, the READY card's
 * page with WRITE n VALUES is the only one.  No page is offered while ARMED: either dock covers
 * part of DISARM in the footer's middle slot.  A page whose dock would cover a control that stops an output is
 * not offered while that control shows: a function's page while the
 * supply's output is on (OUTPUT OFF on the SET row), the output card's
 * while a run, a limit run or AUTO runs (its STOP on the left card).  A
 * keypad, a keyboard, a choice list, a warning, a question and a page of
 * rows take the guide away: none of them has pages.
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

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

/** @brief TEST: the run card's START and SETTINGS, and the caps.
 *  @param p The page. */
static void page_test(ui_help_page_t *p)
{
    task_page(p, TX_HG_SV_TITLE_TEST);
    (void)ui_help_add(p, TR(SV_START_TEST), TX_HG_SV_START, test_start_rect());
    (void)ui_help_add(p, TR(SUP_SETTINGS), TX_HG_SV_RUN_SET, run_set_rect(0));
    (void)ui_help_add(p, TR(HG_SV_CAPS_NAME), TX_HG_SV_CAPS, caps_open_rect());
}

/** @brief LIMIT RUN: the run card's START and SETTINGS.
 *  @param p The page. */
static void page_limrun(ui_help_page_t *p)
{
    task_page(p, TX_HG_SV_TITLE_LIMRUN);
    (void)ui_help_add(p, TR(SV_LIM_START), TX_HG_SV_LIM, lim_start_rect());
    (void)ui_help_add(p, TR(SUP_SETTINGS), TX_HG_SV_RUN_SET, run_set_rect(1));
}

/** @brief RUDDER: LENGTH, the drive buttons, ZERO and RESET MAX.
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

/** @brief RUDDER AUTO's READY card, its rail line, ARM + START and
 *         SETTINGS: the dock on the left, the PORT button kept in view.
 *  @param p The page. */
static void page_ready(ui_help_page_t *p)
{
    p->title = TX_HG_SV_TITLE_READY;
    p->side = UI_HELP_DOCK_LEFT;
    p->armed_ok = false;
    p->must_show = port_rect();
    (void)ui_help_add(p, TR(SVH_PORT), TX_HG_SV_TARGET, p->must_show);
    (void)ui_help_add(p, TR(RDY_TITLE), TX_HG_SV_READY,
                      both(ready_row_rect(0), ready_row_rect(RAR_COUNT - 1)));
    (void)ui_help_add(p, TR(RDY_METER), TX_HG_SV_RDY_RAIL, ready_rail_rect());
    (void)ui_help_add(p, "ARM + START", TX_HG_SV_RA_START,
                      rauto_rect(RA_C_START));
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
    (void)ui_help_add(p, TR(SVH_PORT), TX_HG_SV_TARGET, p->must_show);
    (void)ui_help_add(p, TR(RDY_TITLE), TX_HG_SV_PRG_READY,
                      both(ready_row_rect(0), ready_row_rect(PRR_COUNT - 1)));
    (void)ui_help_add(p, TR(RDY_METER), TX_HG_SV_RDY_RAIL, ready_rail_rect());
    (void)ui_help_add(p, TR(HG_SV_PRG_WRITE_NAME), TX_HG_SV_PRG_WRITE,
                      s.arm_btn);
    (void)ui_help_add(p, TR(FOOT_SETTINGS), TX_HG_SV_SETTINGS,
                      ui_footer_rect(UI_FOOTER_SETTINGS));
}

/* ------------------------------------------------- the output card's pages */

/** @brief An output card's page: the dock on the left, the PORT button
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

/** @brief The PORT button, CENTRE, SWEEP and RELEASE, ARM in the
 *         footer and SETTINGS; on RUDDER also the side view, which the
 *         live half on this side shows from x 496.
 *  @param p The page. */
static void page_drive(ui_help_page_t *p)
{
    card_page(p, TX_HG_SV_TITLE_DRIVE);
    (void)ui_help_add(p, TR(SVH_PORT), TX_HG_SV_TARGET, p->must_show);
    (void)ui_help_add(p, TR(SV_CENTRE_BTN), TX_HG_SV_BUTTONS,
                      both(s.centre_btn, s.release_btn));
    (void)ui_help_add(p, "ARM", TX_HG_SV_ARM, s.arm_btn);
    if (svs_list_offered()) {
        (void)ui_help_add(p, TR(FOOT_SETTINGS), TX_HG_SV_SETTINGS,
                          ui_footer_rect(UI_FOOTER_SETTINGS));
    }
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
    if (modal_open() || rows_page() >= PG_COUNT) {
        return 0;
    }
    int n = 0;
    /* RUDDER AUTO: its own pages, none while the hold stops a sequence or
     * a run (either dock covers part of it). */
    if (prog_ready_shown()) {
        out[n++] = page_prog;   /* KST's and HITEC's card and WRITE */
        return n;
    }
    if (ready_shown()) {
        if (!run_live() && !svs_seq_busy()) {
            out[n++] = page_auto;
            out[n++] = page_rudder_read;
            out[n++] = page_ready;
        }
        return n;
    }
    /* The function's pages dock on the right, over OUTPUT OFF and PROG 5V
     * OFF; KST's and HITEC's session has none of its own here. */
    if (!supply_screen_output_live()
        && !(prog5v_box_shown() && prog5v_box_on()) && !prog_shown()) {
        switch ((servo_fn_t)s.fn) {
        case SERVO_FN_LIVE:
        case SERVO_FN_SWEEP:
            out[n++] = page_live;
            break;
        case SERVO_FN_TEST:
            out[n++] = page_test;
            break;
        case SERVO_FN_LIMRUN:
            out[n++] = page_limrun;
            break;
        case SERVO_FN_RUDDER:
            out[n++] = page_rudder;
            out[n++] = page_rudder_read;
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
