/**
 * @file servo_limit_seam.c
 * @brief The programmer a SERVO screen limit run's sessions go to: the KST
 *        and Chaservo programmer (kst_panel.h) or the Hitec programmer
 *        (hitec_panel_run.h), by the LIMIT RUN settings' SERVO row.
 *
 * A run -- the limit run (servo_limrun.h) or AUTO on RUDDER
 * (servo_rudder_auto.h) -- asks for CONNECT, a write of its two travel
 * values, KST's pulse write and LEAVE; servo_limit_panel.c and
 * servo_rudder_auto_panel.c do what it asks through the functions here,
 * which pick the seam by the run's family and hand back the servo read in
 * the limit run's terms.  A Hitec servo states no
 * pulses: its NEUTRAL and full-travel pulses are the family's model
 * (servo_limrun_family_pulses()), and its DIRECTION reading CCW stands in
 * the run's REVERSION.
 *
 * KST, Chaservo and Hitec are names or trademarks of their owners; they
 * are used here only to say which servos the run works with.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include "hitec_panel.h"
#include "hitec_panel_run.h"
#include "hitec_reg.h"
#include "kst_panel.h"
#include "servo_limrun.h"
#include "settings.h"

/* Last: its layout macros have short names (W, H, PAD), which no header
 * included after it may meet. */
#include "servo_screen_int.h"

/** @brief The programmer open on PROGRAM when last looked (s.lim_open). */
enum { LIM_OPEN_NONE = 0, LIM_OPEN_KST, LIM_OPEN_HITEC };

uint8_t lim_family(void)
{
    if (!s.lim_hitec) {
        return SERVO_LIMRUN_FAM_KST;
    }
    return settings_get_int(SET_HITEC_SERIES) == (int)HITEC_D
               ? (uint8_t)SERVO_LIMRUN_FAM_HITEC_D
               : (uint8_t)SERVO_LIMRUN_FAM_HITEC_C;
}

uint8_t lim_family_now(void)
{
    return servo_limrun_running(&s.lim) ? s.lim.cfg.family : lim_family();
}

/**
 * @brief The sessions of a run of @p fam go to the Hitec programmer.
 * @param fam servo_limrun_family_t
 * @return true for a Hitec family
 */
static bool hitec(uint8_t fam)
{
    return fam != SERVO_LIMRUN_FAM_KST;
}

bool lim_seam_ready(uint8_t fam)
{
    return hitec(fam) ? hitec_panel_run_ready() : kst_panel_run_ready();
}

void lim_seam_begin(uint8_t fam)
{
    if (hitec(fam)) {
        hitec_panel_run_begin();
    } else {
        kst_panel_run_begin();
    }
}

bool lim_seam_connect(uint8_t fam, bool pd, uint16_t mv, uint16_t ma)
{
    return hitec(fam) ? hitec_panel_run_connect(pd, mv, ma) == HITEC_PROG_MSG_NONE
                   : kst_panel_run_connect(pd, mv, ma) == KST_PROG_MSG_NONE;
}

bool lim_seam_write(uint8_t fam, const uint16_t v[2])
{
    return hitec(fam) ? hitec_panel_run_write(v[0], v[1]) == HITEC_PROG_MSG_NONE
                   : kst_panel_run_write(v[0], v[1]) == KST_PROG_MSG_NONE;
}

bool lim_seam_pulses(uint8_t fam, const uint16_t p[2])
{
    /* A Hitec servo has no pulse registers; the run never asks for them
     * with a Hitec family (servo_limrun_start()). */
    return !hitec(fam) && kst_panel_run_pulses(p[0], p[1]) == KST_PROG_MSG_NONE;
}

bool lim_seam_leave(uint8_t fam)
{
    return hitec(fam) ? hitec_panel_run_leave() == HITEC_PROG_MSG_NONE
                   : kst_panel_run_leave() == KST_PROG_MSG_NONE;
}

bool lim_seam_leaving(uint8_t fam)
{
    if (hitec(fam)) {
        const hitec_prog_state_t st = hitec_prog_state(hitec_panel_model());
        return st == HITEC_PROG_LEAVING || st == HITEC_PROG_RELEASING;
    }
    return kst_prog_state(kst_panel_model()) == KST_PROG_LEAVING;
}

bool lim_seam_poll(uint8_t fam, bool *ok)
{
    if (hitec(fam)) {
        const hitec_panel_run_t r = hitec_panel_run_poll();
        *ok = r == HITEC_PANEL_RUN_OK;
        return r == HITEC_PANEL_RUN_OK || r == HITEC_PANEL_RUN_FAILED;
    }
    const kst_panel_run_t r = kst_panel_run_poll();
    *ok = r == KST_PANEL_RUN_OK;
    return r == KST_PANEL_RUN_OK || r == KST_PANEL_RUN_FAILED;
}

bool lim_seam_no_servo(uint8_t fam)
{
    return hitec(fam) ? hitec_panel_run_no_servo() : kst_panel_run_no_servo();
}

/**
 * @brief The Hitec half of lim_seam_servo().
 * @param fam servo_limrun_family_t, a Hitec one
 * @param sv receives the servo
 * @return sv->ok: a servo of the run's series whose end points edit
 */
static bool hitec_servo(uint8_t fam, servo_limrun_servo_t *sv)
{
    hitec_run_servo_t h;
    const uint8_t series = fam == SERVO_LIMRUN_FAM_HITEC_D
                               ? (uint8_t)HITEC_D : (uint8_t)HITEC_CLASSIC;
    const bool read = hitec_panel_run_servo(&h);
    sv->angle[0] = h.value[0];
    sv->angle[1] = h.value[1];
    sv->lo_raw = h.lo;
    sv->hi_raw = h.hi;
    /* DIRECTION CCW in REVERSION's place: every session reads it the
     * same, and the report names the classic cells by it. */
    sv->reversion = !h.cw;
    sv->type = h.series;
    (void)servo_limrun_family_pulses(fam, sv->pulse_dus);
    sv->ok = read && h.series == series;
    return sv->ok;
}

bool lim_seam_servo(uint8_t fam, servo_limrun_servo_t *sv)
{
    memset(sv, 0, sizeof(*sv));
    if (hitec(fam)) {
        return hitec_servo(fam, sv);
    }
    unsigned type_no = 0u;
    sv->ok = kst_panel_run_servo(sv->angle, &sv->lo_raw, &sv->hi_raw,
                                 &sv->reversion, &type_no, sv->pulse_dus);
    sv->type = type_no;
    return sv->ok;
}

const char *lim_seam_backup(uint8_t fam)
{
    return hitec(fam) ? hitec_panel_run_backup() : kst_panel_run_backup();
}

void lim_seam_end(uint8_t fam)
{
    if (hitec(fam)) {
        hitec_panel_run_end();
    } else {
        kst_panel_run_end();
    }
}

void lim_seam_follow(void)
{
    const uint8_t open = hitec_panel_is_open() ? (uint8_t)LIM_OPEN_HITEC
                         : kst_panel_is_open() ? (uint8_t)LIM_OPEN_KST
                                               : (uint8_t)LIM_OPEN_NONE;
    /* A programmer opened takes the SERVO row with it, for a run started
     * after it; a tap on the row sets it otherwise. */
    if (open != s.lim_open && open != LIM_OPEN_NONE
        && !servo_limrun_running(&s.lim)) {
        s.lim_hitec = open == LIM_OPEN_HITEC;
    }
    s.lim_open = open;
}

void lim_seam_close_pages(void)
{
    if (kst_panel_is_open() && !kst_panel_holds_port()) {
        kst_panel_close();
    }
    if (hitec_panel_is_open() && !hitec_panel_holds_port()) {
        hitec_panel_close();
    }
}

int servo_screen_limit_family(void)
{
    return (int)lim_family();
}

bool servo_screen_limit_pulses(void)
{
    return s.lim_pulses;
}

const char *servo_screen_limit_blocked(void)
{
    return lim_blocked();
}
