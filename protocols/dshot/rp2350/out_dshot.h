/**
 * @file
 * @brief DShot out, and the reply back on the same wire.
 *
 * Implements the DShot protocol of ESCs.  DShot is the name of a protocol; it
 * is used here only to say which ESCs this module works with.  The project
 * claims no right in the name (NOTICE, "Trademarks").
 *
 * Plain DShot is one state machine holding the line low between frames.
 * Bidirectional DShot is two: a transmitter that releases the line when the
 * frame ends and raises a flag, and a receiver that starts on that flag and
 * samples the reply.  The turnaround is about 30us after a frame that is
 * itself 27us long, so it happens in the PIO (programmable input/output)
 * block and not in a loop on the processor.
 *
 * A pin that sends no frames is driven low in both modes: from the bind
 * until the first frame, and from out_dshot_stop() until the next one.  A
 * bidirectional pin idles high between the frames of a run; out_dshot_wake()
 * takes it from the low rest to that idle.
 *
 * A frame is sent when the caller asks.  Nothing here has a rate of its own:
 * how often an ESC (electronic speed controller) is told what to do is a
 * property of the project, not of the protocol, and it is the caller's with
 * everything else that is timed against the loop.
 *
 * Extended telemetry is asked for by the caller, not here: this file has no
 * frame rate of its own and a command has to be repeated on one.  The caller
 * sends DSHOT_CMD_EDT_ENABLE on the schedule dshot_edt_frame() sets and
 * passes edt to out_dshot_poll(), which is the only thing that tells an
 * extended frame from an electrical period.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_OUT_DSHOT_H
#define RCBENCH_OUT_DSHOT_H

#include <stdbool.h>
#include <stdint.h>

#include "dshot.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Pins that can be bound at one time.
 *
 * A project that needs another number defines it for the whole build.
 */
#ifndef OUT_DSHOT_MAX_PINS
#define OUT_DSHOT_MAX_PINS 8u
#endif

/**
 * @brief Take @p pin at @p rate_kbit kbit/s.
 *
 * @p bidirectional inverts the line and claims a second state machine, which
 * has to be the one above the transmitter's in the same block: the two
 * rendezvous on a flag relative to their own numbers, so an ESC on one block
 * cannot start another's receiver.  A bind that cannot get that pair is
 * refused rather than falling back to a one-way output that would look like
 * an ESC with no telemetry.
 *
 * A pin that is already bound with the same rate and mode stays as it is and
 * the call returns true.  With a different rate or mode it returns false and
 * the old binding keeps running; release the pin first to change it.
 *
 * @param pin            GPIO number of the output.
 * @param rate_kbit      Bit rate in kbit/s; 0 is refused.
 * @param bidirectional  true inverts the line and claims a receiver.
 * @return               true when bound, or already bound the same way;
 *                       false otherwise.
 */
bool out_dshot_bind(uint8_t pin, uint16_t rate_kbit, bool bidirectional);

/**
 * @brief Give back the pin and the state machines.
 *
 * @param pin  GPIO number of a bound output; other values do nothing.
 */
void out_dshot_release(uint8_t pin);

/**
 * @brief Send one frame.
 *
 * @p value is 0..2047 and is used as given, so a caller can send a command as
 * readily as a throttle; dshot_throttle() is what maps travel onto it.  The
 * frame is dropped rather than queued if the previous one has not gone yet,
 * because a queue of stale throttle values is worse than a missed update.
 *
 * On a bidirectional pin this also arms the receiver for the reply.  A
 * bidirectional pin at its low rest is woken instead (out_dshot_wake()) and
 * this call sends nothing, so no frame starts from a low line; call
 * out_dshot_wake() one frame period ahead to keep the frame.
 *
 * @param pin        GPIO number of a bound output; other values do nothing.
 * @param value      Frame value, 0..2047, used as given.
 * @param telemetry  true asks the ESC to answer on its telemetry wire.
 */
void out_dshot_send(uint8_t pin, uint16_t value, bool telemetry);

/**
 * @brief Stop sending and drive the pin low.
 *
 * No frames means no edges, which is what the output bank means by not
 * driving.  An ESC stops on silence after its own timeout; sending explicit
 * zeros instead would keep it armed and waiting, which is not what a disarmed
 * bench should look like from the ESC's side.
 *
 * The pin is driven low in both modes and stays low until the next frame.  A
 * bidirectional line released to its pull-up would sit high with no frames
 * on it, which is the bootloader entry condition of the family S, Bluejay
 * and AM32 ESC firmware.  A frame in flight is cut.  A pin at rest already
 * is left as it is.
 *
 * @param pin  GPIO number of a bound output; other values do nothing.
 */
void out_dshot_stop(uint8_t pin);

/**
 * @brief Take a bidirectional pin from its low rest to the high idle its
 *        frames start from.
 *
 * The line is released onto its pull-up and no frame is sent.  Call it one
 * frame period before the first frame of a run: the ESC then sees the line
 * high for as long as it sees it between any two frames of the run.  A pin
 * not at rest, a plain DShot pin and a pin not bound are left as they are.
 *
 * @param pin  GPIO number of a bound output; other values do nothing.
 * @return     true when the line was released here, so the first frame is
 *             one frame period away; false when nothing changed.
 */
bool out_dshot_wake(uint8_t pin);

/**
 * @brief Take the last reply, if one arrived and decoded.
 *
 * Returns false on a bidirectional pin that has not answered, on a reply that
 * failed its checksum, and always on a plain DShot pin.  A false is not an
 * error to report: an ESC that does not do bidirectional DShot simply never
 * answers, and that is what it looks like.
 *
 * @p edt says whether this ESC was asked for extended telemetry and is
 * expected to have accepted it.  It cannot be read off the wire, and getting
 * it wrong only matters for an ESC that does not normalise its exponent: one
 * that does either sets the mantissa's top bit, which reads as a period, or
 * leaves the type nibble at zero, which reads as a period too.
 *
 * @param pin  GPIO number of a bound output.
 * @param edt  true when extended telemetry was asked for and accepted.
 * @param out  Receives the decoded reply when true is returned.
 * @return     true for a decoded reply; false for none, a bad checksum, or a
 *             plain DShot pin.
 */
bool out_dshot_poll(uint8_t pin, bool edt, dshot_telem_t *out);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_OUT_DSHOT_H */
