/*
 * What the wire costs, and therefore what the poll schedule can afford.
 *
 * The framing in link_frame.h is rate-agnostic on purpose -- it is the same
 * protocol at every baud.  This header is the arithmetic that decides how
 * often it can be used, which is not the same question and does not have the
 * same answer on the bench as on the finished board.
 *
 * ------------------------------------------------------- two different wires
 *
 * The finished board runs the panel's own RS485 interface at 1.5 Mbaud with
 * automatic direction control.  The module build -- where the protocol
 * firmware is actually being written -- runs a breakout board at 128 or 256
 * kbaud with an explicit direction pin.  That is a twelvefold difference in
 * throughput, so anything that merely "fits" at 1.5 Mbaud has to be checked
 * against the bring-up rate before it is relied on.
 *
 * 8N1 is ten bits per byte -- one start, eight data, one stop:
 *
 *      128,000 baud  ->  12,800 B/s  ->  78.1 us per byte
 *      256,000 baud  ->  25,600 B/s  ->  39.1 us per byte
 *    1,500,000 baud  -> 187,500 B/s  ->   5.3 us per byte
 *
 * A whole-page poll is an eight-byte request and a 72-byte reply:
 *
 *      128 kbaud   6.25 ms    ->  160 whole-page polls per second
 *      256 kbaud   3.13 ms    ->  320
 *      1.5 Mbaud   0.53 ms    -> 1875
 *
 * ------------------------------------------------ what that rules out, and why
 *
 * The research budgeted four channels of current and voltage sampled at 1 kHz
 * and batched into 100 Hz packets.  Sent raw that is 4,000 registers a second:
 * about 125 whole pages, which is 78% of the wire at 128 kbaud and leaves
 * nothing for telemetry.  At 1.5 Mbaud the same stream is 7% and nobody would
 * notice.  So it fits on the finished board and not on the
 * bench, and firmware written against the comfortable case would have to be
 * redesigned at exactly the wrong moment.
 *
 * So it is not sent raw at either rate.  The architecture's own rule --
 * nothing raw crosses the link -- applies here as it does to DShot bit timing:
 * the coprocessor accumulates minimum, maximum and mean per batch, and reads
 * charge and energy out of the INA228's hardware accumulators, and reports
 * those with the rest of the bench state for almost no extra bytes.
 *
 * Raw samples cross only during a *bounded capture* the panel asks for by
 * name.  One second of four channels at 1 kHz is 8 kB of samples, 10 kB framed:
 * about 0.78 s of wire at 128 kbaud and 67 ms at 1.5 Mbaud.  Every
 * measurement that wants raw samples --
 * a load step, a brown-out ramp, a BEC droop test -- is a burst of exactly
 * that shape, so this costs nothing that was wanted.
 *
 * ------------------------------------------------------------- the turnaround
 *
 * Not modelled here, because it is a property of a part rather than of
 * arithmetic.  A breakout with an explicit direction pin puts the turnaround
 * under firmware control, and the trap is releasing the driver before the last
 * stop bit has left the shift register.  An automatic-direction circuit holds
 * the driver enabled for a fixed time after the last edge instead, which puts
 * a floor under bus turnaround that has to be measured rather than assumed.
 * The finished board has the second kind; the bench has the first.
 */
#ifndef RCBENCH_LINK_WIRE_H
#define RCBENCH_LINK_WIRE_H

#include "link_frame.h"

/** 8N1: one start bit, eight data, one stop. */
#define LINK_BITS_PER_BYTE 10u

/** The finished board's own RS485 interface. */
#define LINK_BAUD_TARGET 1500000u

/**
 * The module build: the rate the protocol firmware is written and debugged at,
 * and therefore the rate the poll schedule is budgeted against.  What fits
 * here fits everywhere.
 *
 * 256 kbaud rather than 128, on the schematic's evidence.  Both were on the
 * table and 128 is the worse choice: it clears LINK_BAUD_FLOOR by two percent,
 * and the failure it risks is not a slow link but a driver that switches off
 * partway through a frame.  256 clears the floor by a factor of two and still
 * budgets the schedule pessimistically against the finished board's 1.5 Mbaud.
 */
#define LINK_BAUD_BRINGUP 256000u

/* Both macros are written without casts so that the #if below can evaluate
 * them: the preprocessor has no types, and a (uint32_t) in the expression is
 * a syntax error there rather than a no-op.  Every argument passed to them is
 * an unsigned constant, so the promotion is the same either way. */

/** Microseconds to put @p bytes on the wire at @p baud. */
#define LINK_WIRE_US(bytes, baud) \
    (((bytes) * LINK_BITS_PER_BYTE * 1000000u) / (baud))

/** A request plus a whole-page reply -- the most expensive transaction. */
#define LINK_POLL_BYTES ((LINK_HEADER_BYTES + 2u) + LINK_MAX_FRAME)

/**
 * Whole-page polls per second the wire can carry, turnaround excluded.  The
 * schedule derives its rates from this rather than hardcoding them, so moving
 * from the bench to the finished board is a constant and not a rewrite.
 */
#define LINK_POLLS_PER_SEC(baud) \
    ((baud) / (LINK_BITS_PER_BYTE * LINK_POLL_BYTES))

/*
 * Telemetry is 20 Hz because that is one sample per panel frame, and the
 * schedule has to leave room for everything else.  If a future page grows the
 * frame past what that allows, this stops the build rather than quietly
 * eating the margin the capture mode is relying on.
 */
#if LINK_POLLS_PER_SEC(LINK_BAUD_BRINGUP) < 100u
#error "a whole-page poll no longer fits the bring-up link's budget"
#endif

/* A rate below the floor does not run slowly, it corrupts frames. */
#if LINK_BAUD_BRINGUP < LINK_BAUD_FLOOR
#error "the bring-up baud is below the direction circuit's floor"
#endif
#if LINK_BAUD_TARGET < LINK_BAUD_FLOOR
#error "the target baud is below the direction circuit's floor"
#endif

#endif /* RCBENCH_LINK_WIRE_H */
