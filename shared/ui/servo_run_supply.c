/**
 * @file servo_run_supply.c
 * @brief The 5.0V every LIMIT RUN and RUDDER AUTO run programs at, and the
 *        current limit TEST, LIMIT RUN and RUDDER AUTO runs take.
 *
 * A run's programmer sessions, KST/CHASERVO and Hitec alike, ask for
 * SERVO_LIMRUN_SUPPLY_MV (5.0V) and never read the KST and HITEC pages'
 * Programming voltage (SET_SERVO_PROG_V).  Where a PD mini feeds the servo,
 * START sets it to 5.0V before the steps switch it on, keeping the current
 * limit; it stays at 5.0V after the run, however the run ended.  An error
 * switches every output off as it always does (servo_error_safe).
 *
 * A PD mini whose caps (SUPPLY's, narrowed by its LIMITS) leave out 5.0V
 * refuses the hold, and the READY card's supply row names it; a tap there
 * opens SUPPLY's LIMITS.  A PD mini that took no 5.0V at START names that
 * on the run's state lines, and no step is taken.  The start sequence's
 * read-back waits for the PD mini's reading to hold 5.0V with its output
 * on: one that does not answer with it in 1000ms fails the step.  The PD
 * mini reads its output ON before the output has risen, so a rail read
 * under 3.0V after that is judged again on each window until the same
 * 1000ms are up, and fails the step only then (servo_start_seq.h).
 *
 * The current limit: TEST's steps set the PD mini at SUPPLY's SET A; a
 * LIMIT RUN or RUDDER AUTO session runs at SET A where a PD mini feeds the
 * servo (rsup_ma()).  Without one the session's limit is stored and not
 * applied: the IOmcu switches no supply it does not hold (kst_port.c
 * low_begin()), and the rbmod carrier's PROG 5V takes no limit
 * (kst_hw_supply_set()); the servo's own supply limits it.  The READY
 * cards show SET A on the supply row (`ON 2.00A`, `5.0V 2.00A`, `ON AT
 * START 2.00A`), and a tap there opens its keypad; LIMIT RUN's and RUDDER
 * AUTO's lists hold it as CURRENT (`SET A 2.00A`), locked with the reason
 * where no PD mini feeds the servo.  The keypad is the SET row's of LIVE,
 * SWEEP and RUDDER (svs_set_point_open()): SUPPLY's caps narrowed by its
 * OUTPUT LIMITS, SUPPLY's question first while the output is live and
 * SUPPLY asks one for a typed set point.  It
 * opens neither while a run or its start steps are under way, nor while
 * a programming port holds the supply.
 *
 * SPDX-License-Identifier: MIT
 */

#include <math.h>
#include <stdio.h>

#include "esc_wire_panel.h"
#include "servo_limrun.h"
#include "settings.h"
#include "supply.h"
#include "supply_screen.h"
#include "ui_text.h"

/* Last: its layout macros have short names (W, H, PAD), which no header
 * included after it may meet. */
#include "servo_screen_int.h"

/** @brief The run's voltage, V. */
#define RUN_V ((float)SERVO_LIMRUN_SUPPLY_MV / 1000.0f)
/** @brief Two voltages closer than this are one set point, V. */
#define SAME_V 0.005f

/**
 * @brief Whether @p v is the run's 5.0V.
 * @param v a voltage, V
 * @return true within SAME_V
 */
static bool at_run_v(float v)
{
    return fabsf(v - RUN_V) < SAME_V;
}

bool rsup_pd_used(void)
{
    return svs_seq_need().pd_mini;
}

const char *rsup_why(void)
{
    if (!rsup_pd_used()) {
        return NULL;
    }
    const supply_caps_t c = supply_screen_caps();
    return (c.v_max < RUN_V - SAME_V || c.v_min > RUN_V + SAME_V)
               ? TR(SV_RUN_PD_5V) : NULL;
}

bool rsup_begin(void)
{
    if (!rsup_pd_used()) {
        return true;
    }
    if (rsup_why() != NULL) {
        return false;
    }
    if (!at_run_v(supply_screen_set_v())) {
        supply_screen_put(RUN_V, supply_screen_set_i());
    }
    /* SUPPLY takes no set point while a port holds it, for one. */
    return at_run_v(supply_screen_set_v());
}

bool rsup_read_back(float set_v)
{
    if (s.fn != SERVO_FN_LIMRUN && s.fn != SERVO_FN_RAUTO) {
        return true;
    }
    return at_run_v(set_v);
}

uint16_t rsup_ma(void)
{
    const float a = rsup_pd_used() ? supply_screen_set_i()
                                   : settings_get(SET_SERVO_PROG_I);
    return (uint16_t)lroundf(a * 1000.0f);
}

void rsup_text(char *b, size_t n)
{
    snprintf(b, n, "%u.%uV", SERVO_LIMRUN_SUPPLY_MV / 1000u,
             SERVO_LIMRUN_SUPPLY_MV % 1000u / 100u);
}


/* ------------------------------------------------------- the current limit */

bool rcur_set_a(void)
{
    switch ((servo_fn_t)s.fn) {
    case SERVO_FN_TEST:
        return true;
    case SERVO_FN_LIMRUN:
    case SERVO_FN_RAUTO:
        return rsup_pd_used();
    default:
        return false;
    }
}

void rcur_amps(char *b, size_t n, float a)
{
    const long ma = lroundf(a * 1000.0f);
    const unsigned u = (ma > 0) ? (unsigned)ma : 0u;
    if (u % 10u == 0u) {
        snprintf(b, n, "%u.%02uA", u / 1000u, u % 1000u / 10u);
    } else {
        snprintf(b, n, "%u.%03uA", u / 1000u, u % 1000u);
    }
}

void rcur_join(char *b, size_t n, const char *head, bool set_a)
{
    if (!set_a) {
        snprintf(b, n, "%s", head);
        return;
    }
    char a[16];
    rcur_amps(a, sizeof(a), supply_screen_set_i());
    snprintf(b, n, "%s%s%s", head, head[0] != '\0' ? " " : "", a);
}

void rcur_seq(char *b, size_t n, bool set_a)
{
    if (!set_a) {
        snprintf(b, n, "%s", TR(RDY_SEQ_ON));
        return;
    }
    char a[16];
    rcur_amps(a, sizeof(a), supply_screen_set_i());
    snprintf(b, n, TR(RDY_SEQ_ON_AT), a);
}

const char *rcur_why(void)
{
    /* A run reads its limit at its start (TEST) or at each session's
     * CONNECT (LIMIT RUN, RUDDER AUTO): none changes under one. */
    if (servo_test_running(&s.test)
        || (svs_seq_busy() && s.fn == SERVO_FN_TEST)) {
        return ui_servo_str(SERVO_STR_START_TEST_RUNS);
    }
    if (servo_limrun_running(&s.lim)
        || (svs_seq_busy() && s.fn == SERVO_FN_LIMRUN)) {
        return ui_servo_str(SERVO_STR_START_LIMIT_RUNS);
    }
    if (servo_rauto_running(&s.ra.run) || svs_seq_busy()) {
        return ui_servo_str(SERVO_STR_START_AUTO_RUNS);
    }
    if (!rcur_set_a()) {
        return TR(SVL_CURRENT_NO_PD);
    }
    /* SUPPLY's own keypad takes nothing then either. */
    if (supply_screen_port_held()) {
        return esc_wire_panel_holds_port() ? TR(SUP_HELD_REFUSED_ESC)
                                           : TR(SUP_HELD_REFUSED);
    }
    return NULL;
}

void rcur_edit(void)
{
    const char *why = rcur_why();
    if (why != NULL) {
        ui_action_refused(why);
        return;
    }
    /* The SET row's keypad and question (servo_settings_panel.c). */
    svs_set_point_open(KT_SUP_I);
}

/**
 * @brief CURRENT's value: `SET A 2.00A`, or `--` where no PD mini feeds
 *        the servo and the run sets no limit.
 * @param out takes it
 * @param n its size
 */
static void row_value(char *out, size_t n)
{
    if (rcur_set_a()) {
        rcur_join(out, n, "SET A", true);
    } else {
        snprintf(out, n, "--");
    }
}

const ui_setlist_custom_t rcur_row = {
    .label = TX_SV_CURRENT, .help = TX_SVL_CURRENT_HELP,
    .value_text = row_value, .tap = rcur_edit, .locked = rcur_why,
};
