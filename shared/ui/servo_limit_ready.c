/**
 * @file
 * @brief LIMIT RUN's gate and the rows of its READY card, one table for
 *        both: the checks lim_blocked() makes, in its order, and the row
 *        each check's text belongs to.
 *
 * The gate is a list of checks, each a part of what a run asks of the
 * bench (check()).  lim_blocked() is the first check's text that is not
 * NULL.  The card evaluates every check on its own and shows on a row the
 * first text that belongs to it (lim_row_of()), so every text of the gate
 * is on exactly one row, and a row turns green once its part is met.
 *
 * A text the ARM + START hold fixes itself reads `START SWITCHES IT ON` or
 * `START ARMS` in amber on a SAFE bench (lim_seq_fixes()): RUDDER AUTO's
 * parts (rauto_seq_fixes()), and a servo rail under 3.0V that the supply
 * the hold switches on feeds -- the carrier's PROG 5V on its port, the PD
 * mini's output elsewhere.  Every other text is red and refuses the hold
 * (lim_hold_blocked()).  A PD mini whose output is off on a rail fed from
 * elsewhere stays the operator's: on a PROG PORT the gate names it, on
 * OUTPUTS the hold leaves it off.  On the carrier's port an input supply
 * the PD mini reads low (prog5v_box_input_why()) is the first check, red
 * on the supply row as `INPUT 5.1V < 15V`: M7 makes no 5V from it.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include "kst_port.h"
#include "prog5v_box.h"
#include "prog_choice.h"
#include "prog_registry.h"
#include "sensors_rows.h"
#include "servo_limrun.h"
#include "settings.h"
#include "supply_screen.h"
#include "ui_setlist.h"
#include "ui_text.h"

/* Last: its layout macros have short names (W, H, PAD), which no header
 * included after it may meet. */
#include "servo_screen_core_int.h"

/* ------------------------------------------------------------ the checks */

/** @brief The gate's checks, in lim_blocked()'s order. */
enum {
    LK_INPUT = 0,   /**< the carrier's port with its input supply low  */
    LK_SAFE_5V,     /**< not ARMED, on the carrier's port with 5V off   */
    LK_NOT_ARMED,   /**< not ARMED                                     */
    LK_TEST,        /**< the automatic test runs                       */
    LK_AUTO,        /**< RUDDER AUTO's run runs                        */
    LK_BUSY,        /**< a report still on its way to the card         */
    LK_TARGET,      /**< OUTPUTS with no SERVO PWM output bound        */
    LK_METER,       /**< the INA3221 channel watching the target       */
    LK_5V,          /**< the carrier's PROG 5V off                     */
    LK_RAIL,        /**< the servo rail under 3.0V or not read         */
    LK_PWM,         /**< the port's pulse off, or a session holding it */
    LK_CENTER,      /**< PULSE CENTER not between PULSE MIN and MAX    */
    LK_HITEC,       /**< a Hitec model's travel past PULSE MIN or MAX  */
    LK_PROG,        /**< the programmer, and the supply it switches    */
    LK_PD_5V,       /**< the PD mini's caps leave out the run's 5.0V
                         (servo_run_supply.c)                           */
    LK_COUNT        /**< the count                                     */
};

/** @brief LK_HITEC's text, with the model's pulses in it. */
static char s_hitec_why[72];

/** @brief Whether the PD mini answers with its output on.
 *  @return true then */
static bool pd_output_on(void)
{
    return s.have_sup && s.sup.online && s.sup.output;
}

/**
 * @brief Check @p k: why a run cannot start now on its account.
 * @param k the check
 * @return the text; NULL when the check is met
 */
static const char *check(int k)
{
    switch (k) {
    case LK_INPUT:
        return lim_fixed_supply() ? prog5v_box_input_why() : NULL;
    case LK_SAFE_5V:
        /* The rbmod carrier's port: the PROG 5V before ARM, as the port's
         * pulse comes on only with it and only in SAFE (lim_pwm_why()). */
        return (!value_live() && prog_choice_needs_5v())
                   ? ui_servo_str(SERVO_STR_START_PROG_5V) : NULL;
    case LK_NOT_ARMED:
        return !value_live() ? ui_servo_str(SERVO_STR_START_NOT_ARMED)
                             : NULL;
    case LK_TEST:
        return servo_test_running(&s.test)
                   ? ui_servo_str(SERVO_STR_START_TEST_RUNS) : NULL;
    case LK_AUTO:
        return servo_rauto_running(&s.ra.run)
                   ? ui_servo_str(SERVO_STR_START_AUTO_RUNS) : NULL;
    case LK_BUSY:
        return (!servo_test_drained(&s.test) || !servo_limrun_drained(&s.lim)
                || !servo_rauto_drained(&s.ra.run) || s.logger_busy)
                   ? ui_servo_str(SERVO_STR_START_BUSY) : NULL;
    case LK_TARGET:
        /* The bench's own outputs: a SERVO PWM output bound to carry the
         * pulse.  Before the meter: on the rbmod carrier the channel read
         * is the rail of that output, and no output is no channel
         * watching the target. */
        if (s.drive_target == 0u && (!s.pins_known || s.pins == 0u)) {
            return s.pins_known ? TR(SV_TARGET_NONE) : TR(SV_TARGET_UNREAD);
        }
        return NULL;
    case LK_METER:
        return lim_meter_why();
    case LK_5V:
        /* The carrier's programming port: its pulse needs the PROG 5V,
         * which is also the rail the run reads. */
        return prog_choice_needs_5v() ? ui_servo_str(SERVO_STR_START_PROG_5V)
                                      : NULL;
    case LK_RAIL:
        /* A powered rail: the window's voltage read, and at least
         * SERVO_LIMRUN_RAIL_MIN_MV. */
        return (!s.win.voltage || s.win.mean_mv < SERVO_LIMRUN_RAIL_MIN_MV)
                   ? TR(SV_LIM_RAIL_LOW) : NULL;
    case LK_PWM:
        /* A programmer holding the port before PORT PWM: its session is
         * why no pulse runs, whatever the setting says. */
        return (s.drive_target != 0u
                && (!s.drive_pwm || prog_reg_servo_holds()))
                   ? lim_pwm_why() : NULL;
    case LK_CENTER:
        return !(s.min_us < s.center_us && s.center_us < s.max_us)
                   ? TR(SV_LIM_REFUSED) : NULL;
    case LK_HITEC: {
        /* A Hitec servo is searched out to its model's full travel: PULSE
         * MIN and MAX must take it, or the read ends the run as RANGE. */
        uint16_t p[3];
        if (lim_reads() && servo_limrun_family_pulses(lim_family(), p)
            && (s.min_us * 10u > p[SERVO_LIMRUN_P_LOWER]
                || s.max_us * 10u < p[SERVO_LIMRUN_P_UPPER])) {
            snprintf(s_hitec_why, sizeof(s_hitec_why), TR(SV_LIM_HITEC_RANGE),
                     (unsigned)(p[SERVO_LIMRUN_P_LOWER] / 10u),
                     (unsigned)(p[SERVO_LIMRUN_P_UPPER] / 10u));
            return s_hitec_why;
        }
        return NULL;
    }
    case LK_PROG:
        /* On the programming port every run reads the servo first: the
         * programmer must be free to. */
        return lim_reads() ? lim_prog_why() : NULL;
    case LK_PD_5V:
        return rsup_why();
    default:
        return NULL;
    }
}

/* Why a limit run cannot start now, before any hold, or NULL. */
const char *lim_blocked(void)
{
    for (int k = 0; k < LK_COUNT; ++k) {
        const char *why = check(k);
        if (why != NULL) {
            return why;
        }
    }
    return NULL;
}

/* -------------------------------------------------------------- the rows */

limrun_row_t lim_row_of(const char *why)
{
    if (why == NULL) {
        return LRR_COUNT;
    }
    if (prog5v_box_input_is(why)) {
        return LRR_SUPPLY;
    }
    /* Each text of the gate, by the row it belongs to.  A text is the
     * table's own string, so the pointers compare. */
    const char *const port[] = { TR(SV_TARGET_NONE), TR(SV_TARGET_UNREAD),
                                 TR(SV_LIM_PROG_PORT) };
    const char *const meter[] = { TR(SV_LIM_INA_SETUP), TR(SV_LIM_INA_OFFLINE),
                                  TR(SV_LIM_INA_NO_WIN) };
    const char *const rail[] = { TR(SV_LIM_RAIL_LOW), TR(SV_LIM_PROG_RAIL) };
    const char *const range[] = { TR(SV_LIM_REFUSED), s_hitec_why };
    const char *const prog[] = { TR(OUT_PORT_HELD), TR(SV_LIM_PROG_BUSY) };
    const char *const supply[] = {
        ui_servo_str(SERVO_STR_START_PROG_5V), TR(HP_NEED_PD_C),
        TR(SV_LIM_PROG_PD_OFF), TR(SV_LIM_PROG_NO_PD), TR(SV_RUN_PD_5V),
    };
    const char *const pwm[] = { TR(SV_LIM_PWM_SAFE), TR(SV_LIM_PWM_OFF),
                                TR(SV_LIM_PWM_OFF_ARMED) };
    const char *const arm[] = {
        ui_servo_str(SERVO_STR_START_NOT_ARMED),
        ui_servo_str(SERVO_STR_START_TEST_RUNS),
        ui_servo_str(SERVO_STR_START_AUTO_RUNS),
        ui_servo_str(SERVO_STR_START_BUSY),
    };
    const struct {
        const char *const *t;
        size_t n;
        limrun_row_t row;
    } k_own[] = {
        { port, sizeof(port) / sizeof(port[0]), LRR_PORT },
        { meter, sizeof(meter) / sizeof(meter[0]), LRR_METER },
        { rail, sizeof(rail) / sizeof(rail[0]), LRR_RAIL },
        { range, sizeof(range) / sizeof(range[0]), LRR_RANGE },
        { prog, sizeof(prog) / sizeof(prog[0]), LRR_PROG },
        { supply, sizeof(supply) / sizeof(supply[0]), LRR_SUPPLY },
        { pwm, sizeof(pwm) / sizeof(pwm[0]), LRR_PWM },
        { arm, sizeof(arm) / sizeof(arm[0]), LRR_ARM },
    };
    for (size_t g = 0; g < sizeof(k_own) / sizeof(k_own[0]); ++g) {
        for (size_t i = 0; i < k_own[g].n; ++i) {
            if (why == k_own[g].t[i]) {
                return k_own[g].row;
            }
        }
    }
    return LRR_COUNT;
}

rdy_kind_t lim_kind_of(const char *why)
{
    switch (lim_row_of(why)) {
    case LRR_SUPPLY:
    case LRR_RAIL:
        /* A rail still dead once the supply reads ON: the supply's. */
        return RDY_K_SUPPLY;
    case LRR_PWM:
        return RDY_K_PWM;
    case LRR_ARM:
        return RDY_K_ARM;
    default:
        return RDY_K_OTHER;
    }
}

/** @brief Whether the bench is SAFE and nothing arms it: the steps the
 *         hold switches are taken only then. */
static bool safe_now(void)
{
    return !s.gate.armed && !s.gate.arm_asked && !bench_arming();
}

bool lim_seq_fixes(const char *why)
{
    if (rauto_seq_fixes(why)) {
        return true;
    }
    if (why == NULL || why != TR(SV_LIM_RAIL_LOW) || !safe_now()) {
        return false;
    }
    /* A dead rail the hold's supply feeds: the read-back waits for a
     * window begun after it reads ON. */
    if (lim_fixed_supply()) {
        return !prog5v_box_on() && prog5v_box_live();
    }
    /* START sets the PD mini to 5.0V before the steps switch it on. */
    return lim_pd_feeds() && !pd_output_on();
}

const char *lim_hold_blocked(limrun_row_t *row)
{
    for (int k = 0; k < LK_COUNT; ++k) {
        const char *why = check(k);
        if (why != NULL && !lim_seq_fixes(why)) {
            if (row != NULL) {
                *row = lim_row_of(why);
            }
            return why;
        }
    }
    return NULL;
}

const char *lim_write_why_held(void)
{
    const char *w = lim_write_why();
    /* The carrier's PROG 5V off is the hold's to switch. */
    if (w == ui_servo_str(SERVO_STR_START_PROG_5V) && prog5v_box_live()) {
        return NULL;
    }
    return w;
}

/** @brief Each row's label; the supply's and ARM's are set apart. */
static const ui_text_id_t k_label[LRR_COUNT] = {
    TX_SVH_PORT,  TX_SVH_SERVO, TX_RDY_METER, TX_RDY_RAIL, TX_RDY_RANGE,
    TX_RDY_PROG,  TX_RDY_SUPPLY, TX_RDY_PWM,  TX_COUNT,
};

/**
 * @brief The supply row's label: PROG 5V on the carrier's port, PD MINI
 *        where it feeds the servo, SUPPLY otherwise.
 * @return the label
 */
static const char *supply_label(void)
{
    if (lim_fixed_supply()) {
        return "PROG 5V";
    }
    return lim_pd_feeds() ? "PD MINI" : TR(RDY_SUPPLY);
}

/**
 * @brief The rail's voltage as the INA3221's last window read it.
 * @param b where it goes
 * @param n its size
 */
static void rail_text(char *b, size_t n)
{
    if (s.win.voltage) {
        snprintf(b, n, "%u.%02uV", (unsigned)(s.win.mean_mv / 1000u),
                 (unsigned)((s.win.mean_mv % 1000u) / 10u));
    } else {
        snprintf(b, n, "--");
    }
}

/** @brief Whether the run under way or one started now reads the servo on
 *         a PROG PORT.  @return true then */
static bool reads_now(void)
{
    return servo_limrun_running(&s.lim) ? s.lim.cfg.read : lim_reads();
}

/**
 * @brief A met row's value: what it holds now.
 * @param i the row
 * @param b where it goes
 * @param n its size
 * @param st receives RDY_LIVE where an output is live through it, RDY_IDLE
 *        where the row means nothing for this target
 */
static void met_value(int i, char *b, size_t n, uint8_t *st)
{
    b[0] = '\0';
    switch ((limrun_row_t)i) {
    case LRR_PORT:
        (void)svs_port_line(b, n);
        break;
    case LRR_SERVO: {
        if (!reads_now()) {
            /* OUTPUTS: the run searches and reads no servo. */
            snprintf(b, n, "%s", TR(SV_LIM_SEARCH));
            *st = (uint8_t)RDY_IDLE;
            break;
        }
        snprintf(b, n, "%s", svs_family_name(lim_family_now()));
        break;
    }
    case LRR_METER:
        (void)sensors_rows_meter(b, n);
        break;
    case LRR_RAIL:
        rail_text(b, n);
        break;
    case LRR_RANGE:
        snprintf(b, n, "%u-%uus", (unsigned)s.min_us, (unsigned)s.max_us);
        break;
    case LRR_PROG:
        if (reads_now()) {
            snprintf(b, n, "%s", TR(RDY_FREE));
        } else {
            snprintf(b, n, "--");
            *st = (uint8_t)RDY_IDLE;
        }
        break;
    case LRR_SUPPLY:
        /* The run's 5.0V where the bench sets the supply: the carrier's
         * PROG 5V, or the PD mini START sets; the rail as read else. */
        if (lim_fixed_supply() || rsup_pd_used()) {
            rsup_text(b, n);
            if (lim_fixed_supply() || pd_output_on()) {
                *st = (uint8_t)RDY_LIVE;
            }
        } else {
            rail_text(b, n);
        }
        break;
    case LRR_PWM:
        if (s.drive_target == 0u) {
            /* OUTPUTS: the pulse comes with ARM, no PORT PWM to switch. */
            snprintf(b, n, "--");
            *st = (uint8_t)RDY_IDLE;
            break;
        }
        snprintf(b, n, "%s", ui_on_off(1));
        if (value_live()) {
            *st = (uint8_t)RDY_LIVE;
        }
        break;
    case LRR_ARM:
        /* The bench as it is: a run under way goes SAFE for each
         * session of the programmer, and the band reads SAFE with it. */
        if (value_live()) {
            snprintf(b, n, "ARMED");
            *st = (uint8_t)RDY_LIVE;
        } else {
            snprintf(b, n, "%s", TR(BAND_SAFE));
        }
        break;
    default:
        break;
    }
}

bool lim_ready_row(int i, rdy_row_t *r)
{
    if (r == NULL || i < 0 || i >= (int)LRR_COUNT) {
        return false;
    }
    memset(r, 0, sizeof(*r));
    r->label = (i == LRR_SUPPLY) ? supply_label()
               : (i == LRR_ARM)    ? "ARM"
                                   : ui_tr(k_label[i]);
    bool port_bad = false;
    /* The row's first text that refuses the hold, else its first the hold
     * does: an amber text never hides a red one of the same row. */
    const char *bad = NULL, *seq = NULL;
    for (int k = 0; k < LK_COUNT; ++k) {
        const char *why = check(k);
        if (why == NULL) {
            continue;
        }
        const limrun_row_t row = lim_row_of(why);
        if (row == LRR_PORT) {
            port_bad = true;
        }
        if (row != (limrun_row_t)i) {
            continue;
        }
        if (!lim_seq_fixes(why)) {
            if (bad == NULL) {
                bad = why;
            }
        } else if (seq == NULL) {
            seq = why;
        }
    }
    const char *mine = (bad != NULL) ? bad : seq;
    /* A run under way: its rows as it started, the ARM row its own. */
    if (servo_limrun_running(&s.lim)) {
        mine = NULL;
    }
    if (port_bad && i != LRR_PORT && i != LRR_ARM) {
        /* Without an output to drive the row means nothing yet; its text
         * stays for a tap. */
        r->st  = (uint8_t)RDY_IDLE;
        r->why = mine;
        snprintf(r->value, sizeof(r->value), "--");
    } else if (mine != NULL && lim_seq_fixes(mine)) {
        r->st  = (uint8_t)RDY_SEQ;
        r->why = mine;
        const bool arm = (i == LRR_ARM);
        snprintf(r->value, sizeof(r->value), "%s",
                 arm ? (s.gate.arm_asked ? TR(RDY_ARMING) : TR(RDY_SEQ_ARM))
                     : TR(RDY_SEQ_ON));
    } else if (mine != NULL) {
        r->st  = (uint8_t)RDY_BAD;
        r->why = mine;
        if (prog5v_box_input_is(mine)) {
            (void)prog5v_box_input_tag(r->value, sizeof(r->value));
        } else {
            snprintf(r->value, sizeof(r->value), "%s", mine);
        }
    } else {
        r->st = (uint8_t)RDY_OK;
        met_value(i, r->value, sizeof(r->value), &r->st);
    }
    /* The rows a tap fixes: PORT and SERVO always, METER on SETUP, the
     * PD mini's caps on SUPPLY's LIMITS. */
    r->fix = (i == LRR_PORT || i == LRR_SERVO)
             || (i == LRR_METER && r->st == (uint8_t)RDY_BAD)
             || (i == LRR_SUPPLY && r->st == (uint8_t)RDY_BAD
                 && r->why == TR(SV_RUN_PD_5V));
    return true;
}

void lim_ready_tap(int i)
{
    rdy_row_t r;
    if (!lim_ready_row(i, &r)) {
        return;
    }
    /* Nothing changes under a run or a sequence: the rows as they
     * started. */
    if (servo_limrun_running(&s.lim) || svs_seq_busy()
        || r.st == (uint8_t)RDY_SEQ) {
        return;
    }
    switch ((limrun_row_t)i) {
    case LRR_PORT:
        port_list_open();
        return;
    case LRR_SERVO:
        if (!lim_reads()) {
            /* SERVO is a PROG PORT's: the band says so. */
            r.why = TR(SVL_PORT_ONLY);
            break;
        }
        /* The three families over the left column (servo_pick.c). */
        svs_family_open();
        return;
    case LRR_METER:
        if (r.st == (uint8_t)RDY_BAD) {
            wb_open_meter();
            return;
        }
        break;
    case LRR_SUPPLY:
        /* The PD mini's caps leave out 5.0V: SUPPLY's LIMITS. */
        if (r.st == (uint8_t)RDY_BAD && r.why == TR(SV_RUN_PD_5V)) {
            wb_open_caps();
            return;
        }
        break;
    default:
        break;
    }
    if (r.why != NULL) {
        char line[128];
        snprintf(line, sizeof(line), TR(RDY_BAND), r.label, r.why);
        ui_router_set_alert(line);
    }
}
