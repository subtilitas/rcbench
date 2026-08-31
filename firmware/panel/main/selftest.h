/*
 * The CAN bring-up self-test, lifted out of the main loop.
 *
 * It is a lot of code -- the echo run, the both-ends comparison, the
 * error-counter reading -- and it runs only when the firmware is built with
 * -DRCBENCH_CAN_SELFTEST, so it was dead weight in main.c on every ordinary
 * build.  Here it is its own translation unit, called from one place under the
 * same flag; the loop it left is shorter for it.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>

/**
 * Echo probes across the bus for @p seconds and report both ends' counters.
 *
 * The one question it answers, and deliberately not two: do frames cross this
 * bus intact?  Nothing above the wire is involved.  See docs/Bringup.md.
 */
void can_selftest_run(uint32_t seconds);
