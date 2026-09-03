/*
 * DShot: sixteen bits per frame to an ESC (electronic speed controller), and
 * the GCR (group-coded recording) burst that comes back.
 *
 * A frame is eleven bits of value, one bit asking for telemetry and four bits
 * of checksum, sent most significant bit first.  There is no start bit, no
 * stop bit and no idle pattern: the bit period is the whole synchronisation,
 * so both ends agree on the rate by configuration and a frame is located by
 * the gap before it.  A one is high for 75% of the bit and a zero for 37.5%.
 *
 * The value is not a throttle everywhere in its range.  Zero stops the motor,
 * 1 to 47 are commands, and only 48 to 2047 are throttle.  A driver that maps
 * a proportion of travel straight onto 0..2047 sends beeps and direction
 * changes on its way up from idle.
 *
 * Bidirectional DShot inverts the line and the checksum.  The ESC answers
 * about 30 us after the frame with 21 bits at 5/4 of the DShot rate, GCR
 * coded, carrying an electrical period rather than a speed.  Nothing about
 * that burst is timed here: this file turns a captured sample stream into
 * bits, bits into a value, and a value into rpm (revolutions per minute).
 * The capture itself is a PIO (programmable input/output) program on the
 * coprocessor.
 *
 * What is written from the published description and has not been confirmed
 * against an ESC on this bench: the extended-telemetry frame types and their
 * units, and the 5/4 response rate.  The eRPM path is exercised end to end by
 * the host suite against frames this file also builds, which proves the
 * arithmetic and not the wire.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_DSHOT_H
#define RCBENCH_DSHOT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------- the frame */

#define DSHOT_FRAME_BITS      16u
#define DSHOT_VALUE_MAX     2047u   /**< eleven bits                       */
#define DSHOT_CMD_MAX         47u   /**< above this the value is throttle  */
#define DSHOT_THROTTLE_MIN    48u
#define DSHOT_THROTTLE_MAX  2047u

/**
 * How many times a command has to be repeated before an ESC acts on it.
 *
 * A command is an ordinary frame; the ESC tells a command from a glitch by
 * counting repeats.  Sending one once does nothing at all, which looks
 * exactly like a driver that is not working.
 */
#define DSHOT_CMD_REPEATS     10u

/**
 * The command range, 0 to 47.
 *
 * Only the ones this bench sends are named.  The rest are passed through as
 * numbers: naming a command implies it was tried, and the programmer screen
 * sends codes the ESC's own documentation lists.
 */
typedef enum {
    DSHOT_CMD_MOTOR_STOP        = 0,
    DSHOT_CMD_BEEP1             = 1,
    DSHOT_CMD_BEEP2             = 2,
    DSHOT_CMD_BEEP3             = 3,
    DSHOT_CMD_BEEP4             = 4,
    DSHOT_CMD_BEEP5             = 5,
    DSHOT_CMD_ESC_INFO          = 6,
    DSHOT_CMD_SPIN_DIRECTION_1  = 7,
    DSHOT_CMD_SPIN_DIRECTION_2  = 8,
    DSHOT_CMD_3D_MODE_OFF       = 9,
    DSHOT_CMD_3D_MODE_ON        = 10,
    DSHOT_CMD_SETTINGS_REQUEST  = 11,
    DSHOT_CMD_SAVE_SETTINGS     = 12,
    DSHOT_CMD_EDT_ENABLE        = 13,
    DSHOT_CMD_EDT_DISABLE       = 14,
    DSHOT_CMD_SPIN_NORMAL       = 20,
    DSHOT_CMD_SPIN_REVERSED     = 21,
} dshot_cmd_t;

/**
 * Build a frame.
 *
 * @p value is 0..2047 and is used as given: mapping travel onto it is
 * dshot_throttle()'s job, and a caller that wants to send command 12 must be
 * able to say 12.  @p telemetry asks the ESC to answer on its telemetry wire
 * (the separate serial one, not the signal line).  @p inverted selects
 * bidirectional DShot, whose checksum is the complement of the ordinary one
 * so that a receiver cannot mistake one protocol for the other.
 *
 * Returns 0 for a value above 2047: zero is motor-stop, which is the safe
 * reading of a number this end could not encode.
 */
uint16_t dshot_frame(uint16_t value, bool telemetry, bool inverted);

/**
 * Map a command of 0..@p span onto the throttle range.
 *
 * Zero is DSHOT_CMD_MOTOR_STOP and everything above it lands in 48..2047, so
 * the command range is never entered by a throttle that is merely low.  The
 * discontinuity at the bottom is the protocol's, not this function's: an ESC
 * has no idea of "barely turning".
 */
uint16_t dshot_throttle(uint16_t command, uint16_t span);

/* ------------------------------------------------- the burst coming back */

/** Bits in the response, including the leading one the line code produces. */
#define DSHOT_TELEM_BITS   21u

/** The response rate, as a fraction of the DShot rate: 5/4. */
#define DSHOT_TELEM_RATE_NUM 5u
#define DSHOT_TELEM_RATE_DEN 4u

/**
 * What one response carried.
 *
 * An ESC that has been sent DSHOT_CMD_EDT_ENABLE interleaves the other kinds
 * between eRPM frames.  One that has not sends nothing but eRPM, so a decoder
 * that only understands eRPM still works and simply never sees the rest.
 */
typedef enum {
    DSHOT_TELEM_ERPM = 0,     /**< an electrical period, in microseconds  */
    DSHOT_TELEM_TEMPERATURE,  /**< degrees Celsius                        */
    DSHOT_TELEM_VOLTAGE,      /**< 0.25 V per count                       */
    DSHOT_TELEM_CURRENT,      /**< 1 A per count                          */
    DSHOT_TELEM_DEBUG1,
    DSHOT_TELEM_DEBUG2,
    DSHOT_TELEM_STRESS,
    DSHOT_TELEM_STATUS,
} dshot_telem_kind_t;

typedef struct {
    dshot_telem_kind_t kind;
    uint16_t payload;        /**< the twelve bits, whatever they meant    */
    uint32_t period_us;      /**< kind == ERPM: one electrical period     */
    uint32_t erpm;           /**< kind == ERPM: 0 when it is not turning  */
    uint8_t  value;          /**< every other kind: the low eight bits    */
} dshot_telem_t;

/**
 * Undo the line code and the group code: 21 line bits to a 16-bit value.
 *
 * The line is differential -- each bit is the previous one exclusive-ored
 * with the data -- so a run of identical bits becomes a level that does not
 * change, which is what keeps the ESC's clock and this end's from having to
 * agree for long.  Undoing it is `x ^ (x >> 1)`, and the leading bit falls
 * off the top.  The 20 bits below are four quintets, each one nibble.
 *
 * Returns false for a quintet that is not in the table, which is what a
 * missed or extra sample looks like.
 */
bool dshot_gcr_decode(uint32_t line, uint16_t *out);

/** The other direction, for the suite and for anything that fakes an ESC. */
uint32_t dshot_gcr_encode(uint16_t value);

/**
 * A 16-bit value to what it meant, checksum first.
 *
 * The checksum is the complement of the frame's, so a correct value leaves
 * 0x0F rather than 0 when it is folded; this returns false otherwise.
 *
 * @p edt says whether extended telemetry was turned on and accepted for this
 * ESC.  It cannot be inferred from the bits: the type nibble that marks an
 * extended frame is an ordinary exponent and mantissa in an eRPM one, and the
 * two are only told apart by the guarantee an extended-telemetry ESC makes
 * about normalising its exponent.  Declaring it false reads every frame as
 * eRPM, which is correct for every ESC that was not asked.
 */
bool dshot_telem_from_value(uint16_t value, bool edt, dshot_telem_t *out);

/** The two steps above, together: 21 line bits to a reading. */
bool dshot_telem_decode(uint32_t line, bool edt, dshot_telem_t *out);

/** Build a response, for the suite and for anything that fakes an ESC. */
uint16_t dshot_telem_value(dshot_telem_kind_t kind, uint16_t payload);

/**
 * Electrical rpm to mechanical rpm.
 *
 * @p pole_pairs is half the magnet count, and it is the one number the wire
 * does not carry: an ESC reports electrical periods and has no idea what it
 * is bolted to.  Zero pole pairs returns zero rather than dividing by it.
 */
uint32_t dshot_rpm(uint32_t erpm, uint8_t pole_pairs);

/* --------------------------------------------------- the captured samples */

/**
 * Turn an oversampled capture of the line into the 21 bits of the response.
 *
 * The capture is a bit per sample, most significant bit of word 0 first, at
 * @p oversample samples per response bit.  Sampling at a fixed rate and
 * dividing is not enough on its own: the ESC's bit rate is its own crystal's,
 * a percent or two from this end's, and 21 bits is long enough for that to
 * walk a sample point off the end of a bit.  The phase is therefore reset at
 * every transition, the same way a UART (universal asynchronous
 * receiver-transmitter) resynchronises on a start bit, and each bit is read
 * from the middle of its window.
 *
 * The response is located by the first falling edge: bidirectional DShot
 * idles high, so everything before that edge is idle line and not data.
 *
 * Returns false when the capture holds no falling edge, or ends before 21
 * bits have been read.
 */
bool dshot_rx_bits(const uint32_t *samples, size_t words, uint8_t oversample,
                   uint32_t *line);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_DSHOT_H */
