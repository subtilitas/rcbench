/*
 * When the panel may write its own flash.  See flash_gate.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "flash_gate.h"

bool flash_gate_open(bool armed, bool supply)
{
    return !armed && !supply;
}
