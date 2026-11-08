/**
 * @file
 * @brief RUDDER AUTO's gate and the rows of its READY card, one table for
 *        both: the checks rauto_blocked() makes, in its order, and the row
 *        each check's text belongs to.
 *
 * The gate is a list of checks, each a part of what a START asks of the
 * bench (check()).  rauto_blocked() is the first check's text that is not
 * NULL, as before the card.  The card evaluates every check, each on its
 * own, and shows on a row the first text that belongs to it
 * (rauto_row_of()): so a row turns green when the condition that lets the
 * run start is met, and every text of the gate is on exactly one row.
 *
 * A text the ARM + START hold fixes itself -- PROG 5V off, the PD mini's
 * output off, PORT PWM off, not ARMED -- reads `START SWITCHES IT ON` or
 * `START ARMS` in amber on a SAFE bench (rauto_seq_fixes()); every other
 * one is red and refuses the hold (rauto_hold_blocked()).
 *
 * SPDX-License-Identifier: MIT
 */

#include <math.h>
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

/** @brief The gate's checks, in rauto_blocked()'s order. */
enum {
    CK_SAFE_5V = 0, /**< not ARMED, on the carrier's port with 5V off   */
    CK_NOT_ARMED,   /**< not ARMED                                     */
    CK_TEST,        /**< the automatic test runs                       */
    CK_LIMIT,       /**< a limit run runs                              */
    CK_BUSY,        /**< a report still on its way to the card         */
    CK_NO_TARGET,   /**< the tester drives OUTPUTS                     */
    CK_METER,       /**< the INA3221 channel watching the target       */
    CK_NOT_PORT,    /**< the target is no PROG PORT read               */
    CK_5V,          /**< the carrier's PROG 5V off                     */
    CK_PWM,         /**< the port's pulse off, or a session holding it */
    CK_RANGE,       /**< PULSE MIN to MAX short of 1000 to 2000us      */
    CK_PROG,        /**< the programmer, and the supply it switches    */
    CK_PD_LOW,      /**< the PD mini set under 4.8V: lim_prog_why()
                         names it only once its output is on            */
    CK_PD_HV,       /**< the PD mini off, set past 6.0V: only OUTPUT ON's
                         HV warning switches it on                      */
    CK_WRITE,       /**< a write able to act                           */
    CK_ANGLE,       /**< a reading of the angle source                 */
    CK_ZERO,        /**< ZERO taken                                    */
    CK_COUNT        /**< the count                                     */
};

/** @brief Whether the PD mini answers with its output on.
 *  @return true then */
static bool pd_output_on(void)
{
    return s.have_sup && s.sup.online && s.sup.output;
}

/**
 * @brief Check @p k: why a START cannot run now on its account.
 * @param k the check
 * @return the text; NULL when the check is met
 */
static const char *check(int k)
{
    switch (k) {
    case CK_SAFE_5V:
        /* The rbmod carrier's port: the PROG 5V before ARM, as the port's
         * pulse comes on only with it and only in SAFE (lim_pwm_why()). */
        return (!value_live() && prog_choice_needs_5v())
                   ? ui_servo_str(SERVO_STR_START_PROG_5V) : NULL;
    case CK_NOT_ARMED:
        return !value_live() ? ui_servo_str(SERVO_STR_START_NOT_ARMED)
                             : NULL;
    case CK_TEST:
        return servo_test_running(&s.test)
                   ? ui_servo_str(SERVO_STR_START_TEST_RUNS) : NULL;
    case CK_LIMIT:
        return servo_limrun_running(&s.lim)
                   ? ui_servo_str(SERVO_STR_START_LIMIT_RUNS) : NULL;
    case CK_BUSY:
        return (!servo_test_drained(&s.test) || !servo_limrun_drained(&s.lim)
                || !servo_rauto_drained(&s.ra.run) || s.logger_busy)
                   ? ui_servo_str(SERVO_STR_START_BUSY) : NULL;
    case CK_NO_TARGET:
        /* The programming port before the meter: the meter is the channel
         * watching the target, and on the rbmod carrier the bench's
         * outputs have none until one is bound as SERVO PWM. */
        return (s.drive_target == 0u) ? TR(RA_PROG_PORT) : NULL;
    case CK_METER:
        return lim_meter_why();
    case CK_NOT_PORT:
        return !lim_reads() ? TR(RA_PROG_PORT) : NULL;
    case CK_5V:
        return prog_choice_needs_5v() ? ui_servo_str(SERVO_STR_START_PROG_5V)
                                      : NULL;
    case CK_PWM:
        /* A programmer holding the port before PORT PWM: its session is
         * why no pulse runs, whatever the setting says. */
        return (!s.drive_pwm || prog_reg_servo_holds()) ? lim_pwm_why()
                                                        : NULL;
    case CK_RANGE:
        /* The end pulses lie inside the bench's own range. */
        return (s.min_us > 1000u || s.max_us < 2000u
                || !(s.center_us > 1000u && s.center_us < 2000u))
                   ? TR(RA_RANGE) : NULL;
    case CK_PROG:
        /* The programmer free, and a written value able to act: a PD
         * mini, or the carrier's PROG 5V, switches the servo off and on. */
        return lim_prog_why();
    case CK_PD_LOW:
        return (lim_pd_feeds()
                && supply_screen_set_v() * 1000.0f
                       < (float)KST_PORT_SUPPLY_MIN_MV - 0.5f)
                   ? TR(SV_LIM_PROG_LOW) : NULL;
    case CK_PD_HV:
        /* As OUTPUT ON: an output off whose set point is past a standard
         * servo's rating goes on only through the HV warning
         * (ask_hv_on()), never by the hold. */
        return (lim_pd_feeds() && !pd_output_on() && hv_set_point())
                   ? TR(SV_TAP_HV) : NULL;
    case CK_WRITE:
        return lim_write_why();
    case CK_ANGLE: {
        float d = 0.0f;
        return !rudder_defl(&d) ? TR(RA_NO_ANGLE) : NULL;
    }
    case CK_ZERO:
        return !s.rud.geo.zeroed ? TR(RA_NO_ZERO) : NULL;
    default:
        return NULL;
    }
}

const char *rauto_blocked(void)
{
    for (int k = 0; k < CK_COUNT; ++k) {
        const char *why = check(k);
        if (why != NULL) {
            return why;
        }
    }
    return NULL;
}

/* -------------------------------------------------------------- the rows */

rauto_row_t rauto_row_of(const char *why)
{
    if (why == NULL) {
        return RAR_COUNT;
    }
    /* Each text of the gate, by the row it belongs to.  A text is the
     * table's own string, so the pointers compare. */
    const char *const port[] = { TR(RA_PROG_PORT), TR(SV_LIM_PROG_PORT) };
    const char *const meter[] = { TR(SV_LIM_INA_SETUP), TR(SV_LIM_INA_OFFLINE),
                                  TR(SV_LIM_INA_NO_WIN) };
    const char *const prog[] = { TR(OUT_PORT_HELD), TR(SV_LIM_PROG_BUSY) };
    const char *const supply[] = {
        ui_servo_str(SERVO_STR_START_PROG_5V), TR(HP_NEED_PD_C),
        TR(SV_LIM_PROG_PD_OFF), TR(SV_LIM_PROG_LOW), TR(SV_LIM_PROG_RAIL),
        TR(SV_LIM_PROG_NO_PD), TR(SV_TAP_HV),
    };
    const char *const pwm[] = { TR(SV_LIM_PWM_SAFE), TR(SV_LIM_PWM_OFF),
                                TR(SV_LIM_PWM_OFF_ARMED) };
    const char *const arm[] = {
        ui_servo_str(SERVO_STR_START_NOT_ARMED),
        ui_servo_str(SERVO_STR_START_TEST_RUNS),
        ui_servo_str(SERVO_STR_START_LIMIT_RUNS),
        ui_servo_str(SERVO_STR_START_BUSY),
    };
    const struct {
        const char *const *t;
        size_t n;
        rauto_row_t row;
    } k_own[] = {
        { port, sizeof(port) / sizeof(port[0]), RAR_PORT },
        { meter, sizeof(meter) / sizeof(meter[0]), RAR_METER },
        { prog, sizeof(prog) / sizeof(prog[0]), RAR_PROG },
        { supply, sizeof(supply) / sizeof(supply[0]), RAR_SUPPLY },
        { pwm, sizeof(pwm) / sizeof(pwm[0]), RAR_PWM },
        { arm, sizeof(arm) / sizeof(arm[0]), RAR_ARM },
    };
    for (size_t g = 0; g < sizeof(k_own) / sizeof(k_own[0]); ++g) {
        for (size_t i = 0; i < k_own[g].n; ++i) {
            if (why == k_own[g].t[i]) {
                return k_own[g].row;
            }
        }
    }
    if (why == TR(RA_NO_ANGLE)) {
        return RAR_ANGLE;
    }
    if (why == TR(RA_NO_ZERO)) {
        return RAR_ZERO;
    }
    if (why == TR(RA_RANGE)) {
        return RAR_RANGE;
    }
    return RAR_COUNT;
}

/**
 * @brief Whether the INA3221's last window reads the servo rail dead:
 *        under SERVO_LIMRUN_RAIL_MIN_MV (3.0V), as an output that is off
 *        reads.  A rail that reads more with the PD mini's output off is
 *        fed from elsewhere.
 * @return true then; false without a window
 */
static bool rail_dead(void)
{
    return s.have_win && s.win.voltage
           && s.win.mean_mv < (uint16_t)SERVO_LIMRUN_RAIL_MIN_MV;
}

/** @brief Whether the bench is SAFE and nothing arms it: the steps the
 *         hold switches are taken only then. */
static bool safe_now(void)
{
    return !s.gate.armed && !s.gate.arm_asked && !bench_arming();
}

bool rauto_seq_fixes(const char *why)
{
    if (why == NULL) {
        return false;
    }
    if (why == ui_servo_str(SERVO_STR_START_NOT_ARMED)) {
        return true;
    }
    if (!safe_now()) {
        return false;
    }
    if (why == ui_servo_str(SERVO_STR_START_PROG_5V)) {
        /* Its box switches only while no port holds the supply. */
        return prog5v_box_live();
    }
    if (why == TR(SV_LIM_PWM_OFF)) {
        return !target_locked_by_bench(SET_SERVO_PORT_PWM);
    }
    /* The pulse comes on while SAFE once PROG 5V reads ON: the read-back
     * waits for it. */
    if (why == TR(SV_LIM_PWM_SAFE)) {
        return true;
    }
    /* The PD mini's output off: the hold switches it on only on a rail
     * that reads dead.  One fed from elsewhere stays the operator's. */
    return why == TR(SV_LIM_PROG_PD_OFF) && rail_dead();
}

/**
 * @brief Every check's text, each on its own, and the row of each.
 * @param why receives the texts
 * @param row receives their rows
 */
static void checks_now(const char *why[CK_COUNT], rauto_row_t row[CK_COUNT])
{
    for (int k = 0; k < CK_COUNT; ++k) {
        why[k] = check(k);
        row[k] = rauto_row_of(why[k]);
    }
}

const char *rauto_hold_blocked(rauto_row_t *row)
{
    for (int k = 0; k < CK_COUNT; ++k) {
        const char *why = check(k);
        if (why != NULL && !rauto_seq_fixes(why)) {
            if (row != NULL) {
                *row = rauto_row_of(why);
            }
            return why;
        }
    }
    return NULL;
}

/** @brief Each row's label. */
static const ui_text_id_t k_label[RAR_COUNT] = {
    TX_SVH_PORT,  TX_SVH_SERVO, TX_RDY_METER, TX_RDY_ANGLE, TX_RDY_ZERO,
    TX_RDY_RANGE, TX_RDY_PROG,  TX_RDY_SUPPLY, TX_RDY_PWM,  TX_COUNT,
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
 * @brief A met row's value: what it holds now.
 * @param i the row
 * @param b where it goes
 * @param n its size
 * @param st receives RDY_LIVE where an output is live through it
 */
static void met_value(int i, char *b, size_t n, uint8_t *st)
{
    b[0] = '\0';
    switch ((rauto_row_t)i) {
    case RAR_PORT:
        (void)svs_port_line(b, n);
        break;
    case RAR_SERVO: {
        snprintf(b, n, "%s",
                 svs_family_name(servo_rauto_running(&s.ra.run)
                                     ? s.ra.run.cfg.family
                                     : svs_family_of(SERVO_FN_RAUTO)));
        break;
    }
    case RAR_METER:
        (void)sensors_rows_meter(b, n);
        break;
    case RAR_ANGLE: {
        char name[24], deg[16];
        float d = 0.0f;
        (void)enc_name(name, sizeof(name));
        if (rudder_defl(&d)) {
            snprintf(b, n, "%s %s°", name,
                     svs_angle_text(deg, sizeof(deg), d, true));
        } else {
            snprintf(b, n, "%s", name);
        }
        break;
    }
    case RAR_ZERO:
        snprintf(b, n, "%s", TR(RDY_SET));
        break;
    case RAR_RANGE:
        snprintf(b, n, "%u-%uus", (unsigned)s.min_us, (unsigned)s.max_us);
        break;
    case RAR_PROG:
        snprintf(b, n, "%s", TR(RDY_FREE));
        break;
    case RAR_SUPPLY:
        if (lim_fixed_supply() || lim_pd_feeds()) {
            snprintf(b, n, "%s", ui_on_off(1));
            *st = (uint8_t)RDY_LIVE;
        } else if (s.win.voltage) {
            snprintf(b, n, "%u.%02uV", (unsigned)(s.win.mean_mv / 1000u),
                     (unsigned)((s.win.mean_mv % 1000u) / 10u));
        }
        break;
    case RAR_PWM:
        snprintf(b, n, "%s", ui_on_off(1));
        if (value_live()) {
            *st = (uint8_t)RDY_LIVE;
        }
        break;
    case RAR_ARM:
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

/** @brief Whether row @p i waits on the PORT row: without a PROG PORT the
 *         port's pulse, its programmer and its supply mean nothing. */
static bool waits_on_port(int i)
{
    return i == RAR_PROG || i == RAR_SUPPLY || i == RAR_PWM
           || i == RAR_RANGE;
}

bool rauto_ready_row(int i, rdy_row_t *r)
{
    if (r == NULL || i < 0 || i >= (int)RAR_COUNT) {
        return false;
    }
    memset(r, 0, sizeof(*r));
    r->label = (i == RAR_SUPPLY) ? supply_label()
               : (i == RAR_ARM)    ? "ARM"
                                   : ui_tr(k_label[i]);
    const char *why[CK_COUNT];
    rauto_row_t row[CK_COUNT];
    checks_now(why, row);
    bool port_bad = false;
    /* The row's first text that refuses the hold, else its first the hold
     * does: an amber text never hides a red one of the same row. */
    const char *bad = NULL, *seq = NULL;
    for (int k = 0; k < CK_COUNT; ++k) {
        if (row[k] == RAR_PORT && why[k] != NULL) {
            port_bad = true;
        }
        if (why[k] == NULL || row[k] != (rauto_row_t)i) {
            continue;
        }
        if (!rauto_seq_fixes(why[k])) {
            if (bad == NULL) {
                bad = why[k];
            }
        } else if (seq == NULL) {
            seq = why[k];
        }
    }
    const char *mine = (bad != NULL) ? bad : seq;
    /* A run under way: its rows as it started, the ARM row its own. */
    if (servo_rauto_running(&s.ra.run)) {
        mine = NULL;
    }
    if (port_bad && waits_on_port(i)) {
        /* Without a PROG PORT the row means nothing yet; its text stays
         * for a tap. */
        r->st  = (uint8_t)RDY_IDLE;
        r->why = mine;
        snprintf(r->value, sizeof(r->value), "--");
    } else if (mine != NULL && rauto_seq_fixes(mine)) {
        r->st  = (uint8_t)RDY_SEQ;
        r->why = mine;
        const bool arm = (i == RAR_ARM);
        snprintf(r->value, sizeof(r->value), "%s",
                 arm ? (s.gate.arm_asked ? TR(RDY_ARMING) : TR(RDY_SEQ_ARM))
                     : TR(RDY_SEQ_ON));
    } else if (mine != NULL) {
        r->st  = (uint8_t)RDY_BAD;
        r->why = mine;
        snprintf(r->value, sizeof(r->value), "%s", mine);
    } else if (i == RAR_PWM && !s.drive_pwm && safe_now()) {
        /* PORT PWM on, the pulse not yet: it comes with the PROG 5V the
         * hold switches, which the read-back waits for. */
        r->st = (uint8_t)RDY_SEQ;
        snprintf(r->value, sizeof(r->value), "%s", TR(RDY_SEQ_ON));
    } else {
        r->st = (uint8_t)RDY_OK;
        met_value(i, r->value, sizeof(r->value), &r->st);
    }
    /* The rows a tap fixes: PORT and SERVO always, ANGLE and METER on
     * SETUP, ZERO with a reading. */
    r->fix = (i == RAR_PORT || i == RAR_SERVO)
             || ((i == RAR_ANGLE || i == RAR_METER || i == RAR_ZERO)
                 && r->st == (uint8_t)RDY_BAD)
             || (i == RAR_SUPPLY && r->st == (uint8_t)RDY_BAD
                 && r->why == TR(SV_TAP_HV));
    return true;
}

void rauto_ready_tap(int i)
{
    rdy_row_t r;
    if (!rauto_ready_row(i, &r)) {
        return;
    }
    /* Nothing changes under a run or a sequence: the rows as they
     * started. */
    if (servo_rauto_running(&s.ra.run) || svs_seq_busy()
        || r.st == (uint8_t)RDY_SEQ) {
        return;
    }
    switch ((rauto_row_t)i) {
    case RAR_PORT:
        port_list_open();
        return;
    case RAR_SERVO:
        /* The three families over the left column (servo_pick.c). */
        svs_family_open();
        return;
    case RAR_ZERO:
        if (r.st == (uint8_t)RDY_BAD && enc_shown()) {
            rudder_zero();
            return;
        }
        break;
    case RAR_ANGLE:
        if (r.st == (uint8_t)RDY_BAD) {
            wb_open_encoder();
            return;
        }
        break;
    case RAR_METER:
        if (r.st == (uint8_t)RDY_BAD) {
            wb_open_meter();
            return;
        }
        break;
    case RAR_SUPPLY:
        /* The PD mini off past 6.0V: OUTPUT ON's HV warning, whose own
         * hold switches it on; the hold here never does. */
        if (r.st == (uint8_t)RDY_BAD && r.why == TR(SV_TAP_HV)
            && !s.ov_have) {
            ask_hv_on();
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
