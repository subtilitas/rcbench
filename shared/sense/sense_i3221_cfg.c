/*
 * The INA3221 map's helpers.  See sense_sched.h.  A file of its own so the
 * SENSE page links them without the schedule.
 *
 * SPDX-License-Identifier: MIT
 */

#include "sense_sched.h"

#include <string.h>

void sense_i3221_one(sense_i3221_cfg_t *m, bool en, uint8_t addr,
                     uint32_t shunt_uohm, uint8_t channels)
{
    memset(m, 0, sizeof(*m));
    m->fast = SENSE_FAST_NONE;
    if (!en || addr < INA3221_ADDR_MIN || addr > INA3221_ADDR_MAX) {
        return;
    }
    const unsigned a = addr - INA3221_ADDR_MIN;
    m->fitted = (uint8_t)(1u << a);
    for (unsigned ch = 1u; ch <= INA3221_CHANNELS; ++ch) {
        if ((channels & (1u << (ch - 1u))) != 0u) {
            m->shunt_uohm[SENSE_CH(a, ch)] = shunt_uohm;
        }
    }
    if ((channels & 1u) != 0u) {
        m->fast = (uint8_t)SENSE_CH(a, 1u);
    }
}
