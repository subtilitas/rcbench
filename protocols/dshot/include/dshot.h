/**
 * @file
 * @brief DShot: sixteen bits per frame to an ESC (electronic speed
 *        controller), and the GCR (group-coded recording) burst that comes
 *        back.
 *
 * Implements the DShot protocol of ESCs.  DShot is the name of a protocol; it
 * is used here only to say which ESCs this module works with.  The project
 * claims no right in the name (NOTICE, "Trademarks").
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
 * IOmcu.
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

#define DSHOT_FRAME_BITS      16u   /**< bits in one frame                 */
#define DSHOT_VALUE_MAX     2047u   /**< eleven bits                       */
#define DSHOT_CMD_MAX         47u   /**< above this the value is throttle  */
#define DSHOT_THROTTLE_MIN    48u   /**< lowest throttle value             */
#define DSHOT_THROTTLE_MAX  2047u   /**< highest throttle value            */

/**
 * @brief How many times a command has to be repeated before an ESC acts
 *        on it.
 *
 * A command is an ordinary frame; the ESC tells a command from a glitch by
 * counting repeats.  Sending one once does nothing at all, which looks
 * exactly like a driver that is not working.
 */
#define DSHOT_CMD_REPEATS     10u

/**
 * @brief The command range, 0 to 47.
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
 * @brief Build a frame.
 *
 * @p value is 0..2047 and is used as given: mapping travel onto it is
 * dshot_throttle()'s job, and a caller that wants to send command 12 must be
 * able to say 12.  @p telemetry asks the ESC to answer on its telemetry wire
 * (the separate serial one, not the signal line).  @p inverted selects
 * bidirectional DShot, whose checksum is the complement of the ordinary one
 * so that a receiver cannot mistake one protocol for the other.
 *
 * @param value      Frame value, 0..2047, used as given.
 * @param telemetry  true asks the ESC to answer on its telemetry wire.
 * @param inverted   true selects bidirectional DShot (complemented checksum).
 * @return           The 16-bit frame. 0 for a value above 2047: zero is
 *                   motor-stop, which is the safe reading of a number this
 *                   end could not encode.
 */
uint16_t dshot_frame(uint16_t value, bool telemetry, bool inverted);

/**
 * @brief Map a command of 0..@p span onto the throttle range.
 *
 * Zero is DSHOT_CMD_MOTOR_STOP and everything above it lands in 48..2047, so
 * the command range is never entered by a throttle that is merely low.  The
 * discontinuity at the bottom is the protocol's, not this function's: an ESC
 * has no idea of "barely turning".
 *
 * A @p command above @p span returns DSHOT_CMD_MOTOR_STOP, the same reading
 * of a fault that dshot_frame() applies to a value it cannot encode.  A
 * @p command equal to @p span is the top of the range and returns 2047.
 *
 * @param command  Command or travel step, 0 to @p span.
 * @param span     Largest command, the top of the travel.
 * @return         DSHOT_CMD_MOTOR_STOP for 0 or a command above @p span;
 *                 otherwise a value from 48 to 2047.
 */
uint16_t dshot_throttle(uint16_t command, uint16_t span);

/* ------------------------------------------------- the burst coming back */

/** @brief Bits in the response, including the line code's leading one. */
#define DSHOT_TELEM_BITS   21u

/** @brief The response rate, as a fraction of the DShot rate: 5/4. */
#define DSHOT_TELEM_RATE_NUM 5u
#define DSHOT_TELEM_RATE_DEN 4u

/**
 * @brief Units of the extended telemetry payload, per count of its eight bits
 *        (0 to 255).
 *
 * Voltage is 0.25 V per count, current 1 A per count, temperature 1 degree
 * Celsius per count.
 */
#define DSHOT_EDT_MV_PER_COUNT      250u
#define DSHOT_EDT_A_PER_COUNT       1u
#define DSHOT_EDT_DEGC_PER_COUNT    1u

/**
 * @brief Millivolts for a DSHOT_TELEM_VOLTAGE payload.
 *
 * At most 255 * 250 = 63,750 mV.
 *
 * @param count  Payload count, 0 to 255.
 * @return       Millivolts.
 */
uint32_t dshot_edt_millivolts(uint16_t count);

/**
 * @brief Amperes for a DSHOT_TELEM_CURRENT payload.
 *
 * @param count  Payload count, 0 to 255.
 * @return       Amperes.
 */
uint32_t dshot_edt_amps(uint16_t count);

/**
 * @brief Degrees Celsius for a DSHOT_TELEM_TEMPERATURE payload.
 *
 * @param count  Payload count, 0 to 255.
 * @return       Degrees Celsius.
 */
uint32_t dshot_edt_celsius(uint16_t count);

/**
 * @brief What one response carried.
 *
 * An ESC that has been sent DSHOT_CMD_EDT_ENABLE interleaves the other kinds
 * between eRPM frames.  One that has not sends nothing but eRPM, so a decoder
 * that only understands eRPM still works and simply never sees the rest.
 */
typedef enum {
    DSHOT_TELEM_ERPM = 0,     /**< an electrical period, in microseconds  */
    DSHOT_TELEM_TEMPERATURE,  /**< degrees Celsius                        */
    DSHOT_TELEM_VOLTAGE,      /**< DSHOT_EDT_MV_PER_COUNT millivolts      */
    DSHOT_TELEM_CURRENT,      /**< DSHOT_EDT_A_PER_COUNT amperes          */
    DSHOT_TELEM_DEBUG1,       /**< type nibble 0x08; payload not decoded  */
    DSHOT_TELEM_DEBUG2,       /**< type nibble 0x0A; payload not decoded  */
    DSHOT_TELEM_STRESS,       /**< type nibble 0x0C; payload not decoded  */
    DSHOT_TELEM_STATUS,       /**< type nibble 0x0E; payload not decoded  */
    /** @brief How many kinds there are, for anything holding one slot per
     *         kind. */
    DSHOT_TELEM_KINDS,
} dshot_telem_kind_t;

/**
 * @brief One decoded response.
 */
typedef struct {
    dshot_telem_kind_t kind;
    uint16_t payload;        /**< the twelve bits, whatever they meant    */
    uint32_t period_us;      /**< kind == ERPM: one electrical period     */
    uint32_t erpm;           /**< kind == ERPM: 0 when it is not turning  */
    uint8_t  value;          /**< every other kind: the low eight bits    */
} dshot_telem_t;

/**
 * @brief Undo the line code and the group code: 21 line bits to a 16-bit
 *        value.
 *
 * The line is differential -- each bit is the previous one exclusive-ored
 * with the data -- so a run of identical bits becomes a level that does not
 * change, which is what keeps the ESC's clock and this end's from having to
 * agree for long.  Undoing it is `x ^ (x >> 1)`, and the leading bit falls
 * off the top.  The 20 bits below are four quintets, each one nibble.
 *
 * @param line  The 21 captured line bits.
 * @param out   Receives the 16-bit value.
 * @return      false for a quintet that is not in the table, which is what a
 *              missed or extra sample looks like; true otherwise.
 */
bool dshot_gcr_decode(uint32_t line, uint16_t *out);

/**
 * @brief Encode a 16-bit value as line bits, for the suite and for anything
 *        that fakes an ESC.
 *
 * @param value  The 16-bit value, checksum included.
 * @return       The 21 line bits.
 */
uint32_t dshot_gcr_encode(uint16_t value);

/**
 * @brief A 16-bit value to what it meant, checksum first.
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
 *
 * @param value  The 16-bit frame value.
 * @param edt    true when extended telemetry is on for this ESC.
 * @param out    Receives the decoded reading.
 * @return       true for a valid checksum, false otherwise.
 */
bool dshot_telem_from_value(uint16_t value, bool edt, dshot_telem_t *out);

/**
 * @brief The two steps above, together: 21 line bits to a reading.
 *
 * @param line  The 21 captured line bits.
 * @param edt   true when extended telemetry is on for this ESC.
 * @param out   Receives the decoded reading.
 * @return      true when both steps succeed.
 */
bool dshot_telem_decode(uint32_t line, bool edt, dshot_telem_t *out);

/**
 * @brief Build a response, for the suite and for anything that fakes an ESC.
 *
 * @param kind     Kind of reading to build.
 * @param payload  Payload; the low eight bits are used for the extended
 *                 kinds.
 * @return         The 16-bit response value, checksum included.
 */
uint16_t dshot_telem_value(dshot_telem_kind_t kind, uint16_t payload);

/**
 * @brief Electrical rpm to mechanical rpm.
 *
 * @p pole_pairs is half the magnet count, and it is the one number the wire
 * does not carry: an ESC reports electrical periods and has no idea what it
 * is bolted to.  Zero pole pairs returns zero rather than dividing by it.
 *
 * @param erpm        Electrical rpm.
 * @param pole_pairs  Pole pairs of the motor.
 * @return            Mechanical rpm, or 0 when @p pole_pairs is 0.
 */
uint32_t dshot_rpm(uint32_t erpm, uint8_t pole_pairs);

/* ---------------------------------------- asking for extended telemetry */

/** @brief From the last repeat of an ask that got no answer to the start of
 *         the next. */
#define DSHOT_EDT_RETRY_MS  500u

/** @brief Asks per run before an ESC that never answers is left alone. */
#define DSHOT_EDT_ASKS       10u

/**
 * @brief When to send DSHOT_CMD_EDT_ENABLE, and whether the ESC has answered.
 *
 * One ask is DSHOT_CMD_REPEATS frames of the command in a row.  An ESC only
 * acts on a command while its motor is stopped, and AM32 only once it has
 * armed itself on about a second of zero throttle.  Every run on this bench
 * starts from silence -- a disarmed bench sends nothing, and AM32 restarts
 * after 0.5 s without a signal -- so an ask made only in a run's first 10 ms
 * lands before AM32 can take it and extended telemetry never comes on.
 *
 * So the ask is repeated, every DSHOT_EDT_RETRY_MS while the throttle is at
 * zero, until an extended frame arrives or DSHOT_EDT_ASKS have gone without
 * one.  An ask only starts, and only continues, on a frame whose throttle is
 * zero: a throttle the operator has asked for goes out on that frame, and an
 * ask it interrupts is abandoned and started again from its first repeat.
 *
 * Two lifetimes.  What a run has asked and heard is owed again on every run,
 * because an ESC forgets the setting when it restarts.  @c asked follows the
 * binding: from the first repeat of the first ask, replies are read as
 * extended telemetry, because an ESC that kept power kept the setting and
 * AM32 answers the command once, on a reply after its sixth repeat.
 */
typedef struct {
    uint8_t  left;      /**< repeats still to send in the ask under way   */
    uint8_t  asks;      /**< asks completed this run                      */
    uint32_t due_ms;    /**< when the next ask may start                  */
    bool     answered;  /**< an extended frame arrived this run           */
    bool     asked;     /**< an ask has started since the binding         */
} dshot_edt_t;

/**
 * @brief A new binding: nothing asked, nothing heard, an ask due at once.
 *
 * @param e       Ask state to bind.
 * @param now_ms  Current time, in milliseconds.
 */
void dshot_edt_bind(dshot_edt_t *e, uint32_t now_ms);

/**
 * @brief Not driving: the next run owes the ask again, due from its first
 *        frame.
 *
 * Called on every service while disarmed, so it is idempotent; @c asked is
 * kept.
 *
 * @param e       Ask state.
 * @param now_ms  Current time, in milliseconds.
 */
void dshot_edt_idle(dshot_edt_t *e, uint32_t now_ms);

/**
 * @brief Whether the frame about to go out is a repeat of DSHOT_CMD_EDT_ENABLE.
 *
 * @p stopped says the throttle this frame would carry is zero.  False means
 * send the throttle.
 *
 * @param e        Ask state.
 * @param stopped  true when the throttle this frame would carry is zero.
 * @param now_ms   Current time, in milliseconds.
 * @return         true when the frame is a repeat of the ask.
 */
bool dshot_edt_frame(dshot_edt_t *e, bool stopped, uint32_t now_ms);

/**
 * @brief A reply arrived; an extended kind after an ask ends the asking.
 *
 * @param e     Ask state.
 * @param kind  Kind of the reply that arrived.
 */
void dshot_edt_heard(dshot_edt_t *e, dshot_telem_kind_t kind);

/* --------------------------------------------------- the captured samples */

/**
 * @brief Turn an oversampled capture of the line into the 21 bits of the
 *        response.
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
 * @param samples     Capture, one bit per sample, MSB of word 0 first.
 * @param words       Number of 32-bit words in @p samples.
 * @param oversample  Samples per response bit, 2 or more.
 * @param line        Receives the 21 response bits.
 * @return            false when the capture holds no falling edge, or ends
 *                    before 21 bits have been read; true otherwise.
 */
bool dshot_rx_bits(const uint32_t *samples, size_t words, uint8_t oversample,
                   uint32_t *line);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_DSHOT_H */
