/*
 * When the panel may write its own flash, and when the coprocessor may
 * not write its own.  See flash_gate.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "flash_gate.h"

bool flash_gate_open(bool armed, bool supply)
{
    return !armed && !supply;
}

bool flash_gate_busy(bool driving, bool supply_on, bool pd_on,
                     bool kst_session)
{
    return driving || supply_on || pd_on || kst_session;
}
