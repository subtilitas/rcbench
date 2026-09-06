/*
 * The CAN (Controller Area Network) bring-up self-test: the echo run, the
 * comparison of both ends' counters and the error-counter reading.
 *
 * It runs at every start-up, before the bench is usable.  The fault it
 * reports is invisible from every other screen -- a bus that does not carry
 * frames looks exactly like a coprocessor that is not there, and both look
 * like a bench that simply shows no numbers -- so the panel says so itself
 * rather than leaving it to be worked out.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "busfault_screen.h"

/**
 * Echo probes across the bus for @p ms and fill @p out with what both ends
 * saw.  True when the verdict is CAN_SELFTEST_OK.
 *
 * It answers one question: do frames cross this bus intact?  Nothing above
 * the wire is involved.  See docs/Bringup.md.
 */
bool can_selftest_run(uint32_t ms, busfault_report_t *out);
