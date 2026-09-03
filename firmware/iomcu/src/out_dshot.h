/*
 * DShot out, and the reply back on the same wire.
 *
 * Plain DShot is one state machine holding the line low between frames.
 * Bidirectional DShot is two: a transmitter that releases the line when the
 * frame ends and raises a flag, and a receiver that starts on that flag and
 * samples the reply.  The turnaround is about 30 us after a frame that is
 * itself 27 us long, so it happens in the PIO (programmable input/output)
 * block and not in a loop on the processor.
 *
 * A frame is sent when the caller asks.  Nothing here has a rate of its own:
 * how often an ESC (electronic speed controller) is told what to do is a
 * property of the bench, not of the protocol, and it is in outputs_hw.c with
 * everything else that is timed against the loop.
 *
 * Extended telemetry is never enabled, so every reply is read as an
 * electrical period.  Turning it on means sending DSHOT_CMD_EDT_ENABLE ten
 * times and then telling the decoder, and nothing does either yet.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_OUT_DSHOT_H
#define RCBENCH_OUT_DSHOT_H

#include <stdbool.h>
#include <stdint.h>

#include "dshot.h"

/**
 * Take @p pin at @p rate_kbit kbit/s.
 *
 * @p bidirectional inverts the line and claims a second state machine, which
 * has to be the one above the transmitter's in the same block: the two
 * rendezvous on a flag relative to their own numbers, so an ESC on one block
 * cannot start another's receiver.  A bind that cannot get that pair is
 * refused rather than falling back to a one-way output that would look like
 * an ESC with no telemetry.
 */
bool out_dshot_bind(uint8_t pin, uint16_t rate_kbit, bool bidirectional);

/** Give back the pin and the state machines. */
void out_dshot_release(uint8_t pin);

/**
 * Send one frame.
 *
 * @p value is 0..2047 and is used as given, so a caller can send a command as
 * readily as a throttle; dshot_throttle() is what maps travel onto it.  The
 * frame is dropped rather than queued if the previous one has not gone yet,
 * because a queue of stale throttle values is worse than a missed update.
 *
 * On a bidirectional pin this also arms the receiver for the reply.
 */
void out_dshot_send(uint8_t pin, uint16_t value, bool telemetry);

/**
 * Stop sending.
 *
 * No frames means no edges, which is what the output bank means by not
 * driving.  An ESC stops on silence after its own timeout; sending explicit
 * zeros instead would keep it armed and waiting, which is not what a disarmed
 * bench should look like from the ESC's side.
 */
void out_dshot_stop(uint8_t pin);

/**
 * Take the last reply, if one arrived and decoded.
 *
 * Returns false on a bidirectional pin that has not answered, on a reply that
 * failed its checksum, and always on a plain DShot pin.  A false is not an
 * error to report: an ESC that does not do bidirectional DShot simply never
 * answers, and that is what it looks like.
 */
bool out_dshot_poll(uint8_t pin, dshot_telem_t *out);

#endif /* RCBENCH_OUT_DSHOT_H */
