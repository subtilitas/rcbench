/**
 * @file servo_run_supply.c
 * @brief The 5.0V every LIMIT RUN and RUDDER AUTO run programs at.
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
 * SPDX-License-Identifier: MIT
 */

#include <math.h>
#include <stdio.h>

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
