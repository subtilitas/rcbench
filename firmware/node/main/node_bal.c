/**
 * @file node_bal.c
 * @brief The balance role, a stub: it advertises as `rcbench-bal-XXXX`,
 *        serves STATUS and INFO, takes BLADES and RPM, and sends no
 *        report.  It has no timer wake: asleep, it sleeps until the RESET
 *        key.  The balance slice fills it: the ADC (analogue-to-digital
 *        converter) stream of the vibration sensor and the index sensor,
 *        and the demodulation of shared/vibe into RCNODE_T_BAL_REPORT
 *        every RCNODE_BAL_MS.
 *
 * SPDX-License-Identifier: MIT
 */
#include "node.h"

#include <stdio.h>


/** @brief The blade count and the rpm the panel wrote. @{ */
static uint8_t  s_blades = 2u;
static uint16_t s_rpm;
/** @} */

/** @brief Nothing to start yet. */
static void bal_start(void)
{
}

/**
 * @brief BLADES and RPM are kept for the demodulator.
 *
 * @param c The write.
 */
static void bal_control(const rcnode_ctl_t *c)
{
    if (c->op == RCNODE_OP_BLADES) {
        s_blades = c->blades;
    } else if (c->op == RCNODE_OP_RPM) {
        s_rpm = c->rpm;
    }
}

/**
 * @brief No sensor yet.
 *
 * @param s The status.
 */
static void bal_status(rcnode_status_t *s)
{
    s->sensor = RCNODE_SENSOR_NONE;
}

/**
 * @brief No role part in INFO yet.
 *
 * @param i The info.
 */
static void bal_info(rcnode_info_t *i)
{
    (void)i;
}

/**
 * @brief The role's part of the console line.
 *
 * @param buf The buffer.
 * @param n Its size.
 */
static void bal_line(char *buf, size_t n)
{
    snprintf(buf, n, "no sensor yet, blades %u, rpm %u", (unsigned)s_blades,
             (unsigned)s_rpm);
}

/** @brief Nothing holds a balance node awake but the link.
 *  @return false. */
static bool bal_busy(void)
{
    return false;
}

/** @brief Nothing to put to sleep. */
static void bal_sleep(void)
{
}

/**
 * @brief Not called: a balance node has no timer wake.
 *
 * @param vbus_uv Receives 0.
 * @return false.
 */
static bool bal_wake(int32_t *vbus_uv)
{
    *vbus_uv = 0;
    return false;
}

const node_role_t node_role = {
    .role    = RCNODE_ROLE_BALANCE,
    .word    = "bal",
    .timer_wake = false,
    .start   = bal_start,
    .control = bal_control,
    .status  = bal_status,
    .info    = bal_info,
    .line    = bal_line,
    .busy    = bal_busy,
    .sleep   = bal_sleep,
    .wake    = bal_wake,
};
