/*
 * The CAN (Controller Area Network) bring-up self-test: the echo run, the
 * comparison of both ends' counters and the error-counter reading.  It is
 * compiled only with -DRCBENCH_CAN_SELFTEST and called from one place in
 * main.c under the same flag, so an ordinary build carries none of it.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>

/**
 * Echo probes across the bus for @p seconds and report both ends' counters.
 *
 * It answers one question: do frames cross this bus intact?  Nothing above
 * the wire is involved.  See docs/Bringup.md.
 */
void can_selftest_run(uint32_t seconds);
