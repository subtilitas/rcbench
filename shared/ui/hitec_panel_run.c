/**
 * @file hitec_panel_run.c
 * @brief A run's sessions on the programmer for servos by Hitec: the
 *        CONNECT, the writes and the LEAVE a run asks of the overlay's
 *        model, and the poll that reports each step's end.  See
 *        hitec_panel_run.h.
 *
 * Hitec is a name or trademark of its owner; it is used here only to say
 * which servos this overlay works with.  The overlay's state and seam are
 * hitec_panel_int.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_panel_run.h"

#include <stdio.h>
#include <string.h>

#include "hitec_panel.h"
#include "hitec_reg.h"
#include "settings.h"

/* Last: its layout macros and hp, the state, which no header included
 * after it may meet. */
#include "hitec_panel_int.h"

/** @brief The step of the programmer a run asked. */
enum { RUN_OP_NONE = 0, RUN_OP_CONNECT, RUN_OP_WRITE, RUN_OP_LEAVE };

/** @brief A run's step and what the run keeps of the operator's. */
typedef struct {
    uint8_t  op;        /**< RUN_OP_*                                    */
    bool     kept;      /**< the operator's supply values are kept       */
    uint16_t keep_mv;   /**< the kept programming voltage, mV            */
    uint16_t keep_ma;   /**< the kept current limit, mA                  */
    uint16_t want[2];   /**< the write's travel, left and right          */
    uint32_t said;      /**< the model's message count at the CONNECT    */
    char     backup[HITEC_CARD_NAME_MAX]; /**< the run's backup file      */
} run_state_t;

/** @brief The run's state; hitec_panel_run_begin() clears it. */
static run_state_t s_run;

/**
 * @brief Whatever the overlay has open goes: the run's step is no
 *        operator's.
 */
static void run_close_editors(void)
{
    hpn_close_editors();
    ++hp.rev;
}

/**
 * @brief The run's backup file, kept once a session names it: the LEAVE of
 *        that session drops the model's name.
 */
static void keep_backup(void)
{
    const char *n = hitec_prog_backup_name(&hp.prog);

    if (s_run.backup[0] == '\0' && n[0] != '\0') {
        (void)snprintf(s_run.backup, sizeof(s_run.backup), "%s", n);
    }
}

bool hitec_panel_run_ready(void)
{
    return hitec_prog_state(&hp.prog) == HITEC_PROG_PWM;
}

void hitec_panel_run_begin(void)
{
    memset(&s_run, 0, sizeof(s_run));
    hitec_prog_run_begin(&hp.prog);
}

hitec_prog_msg_id_t hitec_panel_run_connect(bool supply, uint16_t mv,
                                            uint16_t ma)
{
    hitec_prog_t *p = &hp.prog;
    hitec_prog_msg_id_t m;

    if (hitec_prog_state(p) != HITEC_PROG_PWM) {
        return HITEC_PROG_MSG_BUSY;
    }
    run_close_editors();
    /* The series and the operator's voltage and current from the
     * settings, as the overlay's opening takes them. */
    hpn_options_load();
    if (!s_run.kept) {
        s_run.kept = true;
        s_run.keep_mv = p->supply_mv;
        s_run.keep_ma = p->supply_ma;
    }
    if (supply) {
        /* The PD mini's set point as it stands: the entry changes nothing
         * of it. */
        hitec_prog_supply_set(p, mv, ma);
    }
    hpn_supply_caps();
    hp.card_look = true;
    s_run.said = hitec_prog_message(p)->seq;
    m = hitec_prog_connect(p);
    s_run.op = m == HITEC_PROG_MSG_NONE ? (uint8_t)RUN_OP_CONNECT
                                        : (uint8_t)RUN_OP_NONE;
    return m;
}

hitec_prog_msg_id_t hitec_panel_run_write(uint16_t left, uint16_t right)
{
    hitec_prog_msg_id_t m;

    run_close_editors();
    hp.card_look = true;
    m = hitec_prog_run_write(&hp.prog, left, right);
    if (m == HITEC_PROG_MSG_NONE) {
        s_run.op = RUN_OP_WRITE;
        s_run.want[0] = left;
        s_run.want[1] = right;
    }
    return m;
}

hitec_prog_msg_id_t hitec_panel_run_leave(void)
{
    hitec_prog_t *p = &hp.prog;
    hitec_prog_msg_id_t m;

    run_close_editors();
    (void)hitec_prog_write_enable(p, false);
    hitec_prog_discard(p);
    m = hitec_prog_leave(p);
    s_run.op = m == HITEC_PROG_MSG_NONE ? (uint8_t)RUN_OP_LEAVE
                                        : (uint8_t)RUN_OP_NONE;
    return m;
}

/**
 * @brief Whether the model is in a state an operation runs in.
 *
 * @param st The state.
 * @return true while something runs.
 */
static bool running(hitec_prog_state_t st)
{
    switch (st) {
    case HITEC_PROG_OPENING:
    case HITEC_PROG_ENTERING:
    case HITEC_PROG_READING:
    case HITEC_PROG_CLEARING:
    case HITEC_PROG_LEAVING:
    case HITEC_PROG_RELEASING:
    case HITEC_PROG_RECORDING:
    case HITEC_PROG_WRITING:
    case HITEC_PROG_BACKUP:
    case HITEC_PROG_MOVING:
        return true;
    default:
        return false;
    }
}

hitec_panel_run_t hitec_panel_run_poll(void)
{
    hitec_prog_t *p = &hp.prog;
    const hitec_prog_state_t st = hitec_prog_state(p);
    uint16_t v[2] = { 0u, 0u };
    bool ok;

    if (s_run.op == RUN_OP_NONE) {
        return HITEC_PANEL_RUN_NONE;
    }
    if (running(st)) {
        return HITEC_PANEL_RUN_BUSY;
    }
    keep_backup();
    switch (s_run.op) {
    case RUN_OP_CONNECT:
        /* The read the entry chains is part of it. */
        ok = st == HITEC_PROG_HELD && hitec_prog_in_mode(p)
             && hitec_prog_servo(p) != NULL;
        break;
    case RUN_OP_WRITE:
        ok = st == HITEC_PROG_HELD
             && hitec_prog_message(p)->id == (uint8_t)HITEC_PROG_MSG_WRITTEN
             && hitec_prog_run_values(p, v) && v[0] == s_run.want[0]
             && v[1] == s_run.want[1];
        /* Every write of a run is switched on for itself. */
        (void)hitec_prog_write_enable(p, false);
        break;
    default:
        ok = st == HITEC_PROG_PWM;
        break;
    }
    s_run.op = RUN_OP_NONE;
    ++hp.rev;
    return ok ? HITEC_PANEL_RUN_OK : HITEC_PANEL_RUN_FAILED;
}

bool hitec_panel_run_no_servo(void)
{
    const hitec_prog_msg_t *m = hitec_prog_message(&hp.prog);

    return (m->id == (uint8_t)HITEC_PROG_MSG_NO_D
            || m->id == (uint8_t)HITEC_PROG_MSG_NO_C)
           && m->seq != s_run.said;
}

bool hitec_panel_run_servo(hitec_run_servo_t *out)
{
    return hitec_prog_run_servo(&hp.prog, out);
}

bool hitec_panel_run_values(uint16_t value[2])
{
    return hitec_prog_run_values(&hp.prog, value);
}

const char *hitec_panel_run_backup(void)
{
    keep_backup();
    return s_run.backup;
}

void hitec_panel_run_end(void)
{
    hitec_prog_t *p = &hp.prog;

    s_run.op = RUN_OP_NONE;
    hitec_prog_run_end(p);
    if (s_run.kept) {
        s_run.kept = false;
        hitec_prog_supply_set(p, s_run.keep_mv, s_run.keep_ma);
    }
    ++hp.rev;
}
