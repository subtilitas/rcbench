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
 * Read off the panel's schematic rather than guessed.  Its automatic-direction
 * circuit is an RC one-shot: an SN74LVC1G125 buffer follows the transmit line
 * and charges C51 (1 nF) through R76 (200 kOhm); the resulting gate voltage
 * turns on Q1, which pulls DE and /RE low against R79's 1 kOhm pull-up, and a
 * Schottky across R76 dumps the gate the instant the transmit line falls.
 *
 * So the driver enables on the first start bit and releases only once the line
 * has stayed high long enough for the gate to reach the FET's threshold:
 *
 *      t = -R76 * C51 * ln(1 - Vth / 3.3)
 *
 *      Vth = 1.0 V  ->   72 us
 *      Vth = 1.5 V  ->  121 us
 *      Vth = 2.0 V  ->  179 us
 *
 * Two consequences, and the first is the opposite of what was assumed.
 *
 * THERE IS A MINIMUM BAUD RATE, NOT A MAXIMUM.  The longest run of high bits
 * inside an 8N1 frame is nine bit times -- eight data bits and the stop bit,
 * with the next start bit low.  If that run outlasts t, the gate crosses the
 * threshold mid-frame, the driver switches off, and the rest of the
 * transmission never reaches the bus.  Nine bit times must fit inside the
 * worst-case 72 us, which puts the floor at about 125,000 baud.  1.5 Mbaud is
 * twelve times clear of it; 256 kbaud is twice clear; 128 kbaud clears it by
 * two percent, which is not a margin.
 *
 * THE BUS IS HELD AFTER THE LAST EDGE.  The far end must not answer until the
 * driver has released, and the hold starts from the last *falling* edge rather
 * than from the end of the frame -- a final byte of 0xFF begins its hold nine
 * bit times early.  Waiting a conservative 200 us after the last received byte
 * covers every threshold and every trailing byte.
 *
 * One number is not on the schematic: Q1's threshold.  R76, C51, D7 and R79
 * are named; the FET is not, so the floor is quoted from the pessimistic end
 * of a plausible range.  One measurement settles it -- scope DE against TX at
 * 256 kbaud and read the release directly.
 */
#ifndef RCBENCH_LINK_WIRE_H
#define RCBENCH_LINK_WIRE_H

#include "link_frame.h"

/** 8N1: one start bit, eight data, one stop. */
#define LINK_BITS_PER_BYTE 10u

/** The finished board's own RS485 interface. */
#define LINK_BAUD_TARGET 1500000u

/**
 * How long to wait after receiving a frame before answering it.  Covers the
 * slowest plausible release of the panel's direction circuit; see above.
 */
#define LINK_TURNAROUND_US 200u

/**
 * Below this, a run of high bits inside a frame outlasts the direction
 * circuit's hold and the driver drops mid-transmission.  Not a recommendation
 * -- a floor.
 */
#define LINK_BAUD_FLOOR 125000u

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

/*
 * A rate below the floor does not run slowly, it corrupts frames.
 *
 * The #ifdef is not belt-and-braces.  These two guards were once written
 * against a LINK_BAUD_FLOOR that had failed to land in this file, and the
 * preprocessor treats an undefined identifier as 0 -- so `128000 < 0` was
 * false, both guards passed, and the check silently protected nothing.  A
 * missing constant now breaks the build instead of disarming the test.
 */
#ifndef LINK_BAUD_FLOOR
#error "LINK_BAUD_FLOOR is not defined; the baud guards below would be inert"
#endif
#if LINK_BAUD_BRINGUP < LINK_BAUD_FLOOR
#error "the bring-up baud is below the direction circuit's floor"
#endif
#if LINK_BAUD_TARGET < LINK_BAUD_FLOOR
#error "the target baud is below the direction circuit's floor"
#endif

#endif /* RCBENCH_LINK_WIRE_H */
