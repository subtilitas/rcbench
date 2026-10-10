/*
 * The SENSE_TRACE build: CH1's 1 ms samples on the USB console.
 *
 * Compiled only with -DSENSE_TRACE=ON; no released image holds it.  The
 * rules -- what triggers a trace, what a line is, what happens when the
 * ring is full -- are shared/sense/sense_trace.h.  This file is the ring's
 * memory, the console and the PWM slice's counter.
 *
 * Core 1 calls sense_trace_hw_feed() after each tick's step: it reads the
 * schedule's memory and writes at most 4 records, 12 bytes each, into a
 * ring of SENSE_TRACE_HW_RING.  No bus transaction, no console.
 *
 * Core 0 calls the rest from the main loop.  sense_trace_hw_pass() writes
 * to the console only what its transmit buffer has room for at that
 * moment, in whole lines, and nothing while no terminal is connected, so
 * the write does not wait for the host.  Nothing here is called from, and
 * nothing here changes, the arm gate, STOP, the heartbeat or the outputs:
 * the outputs are read after they are rendered.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_SENSE_TRACE_HW_H
#define RCBENCH_SENSE_TRACE_HW_H

#include <stdint.h>

#include "outputs.h"
#include "sense_svc.h"

/** Records the ring holds: 49,152 bytes, 3.9 s of CH1 at 1000 samples
 *  and 50 voltages a second. */
#define SENSE_TRACE_HW_RING 4096u

/** Core 0, before core 1 starts. */
void sense_trace_hw_init(void);

/** Core 1, after each step of @p svc. */
void sense_trace_hw_feed(const sense_svc_t *svc);

/**
 * Core 0, straight after outputs_hw_service(): each bound PWM slot's
 * pulse of this pass, no longer than the slice's frame.  A changed one is
 * a command (sense_trace_pulse()),
 * timed at the start of the frame that first carries it: now plus what
 * the slice's counter has left of its frame, 1 µs a count.  The counter
 * is read after the level was written, so a wrap between the two -- some
 * tens of µs in a frame of 2,500 to 25,000 µs -- puts the time one frame
 * late.
 */
void sense_trace_hw_outputs(const outputs_t *o);

/** Core 0: the capture's PWM edge, as outputs_hw_edge() stamped it. */
void sense_trace_hw_edge(uint64_t edge_us);

/** Core 0, once a pass: a character from the console, and the lines the
 *  console has room for. */
void sense_trace_hw_pass(void);

#endif /* RCBENCH_SENSE_TRACE_HW_H */
