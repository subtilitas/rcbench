/**
 * @file
 * @brief The seam between the output bank and the pins.
 *
 * shared/outputs answers what every output should be doing -- role, rest,
 * slew, clamp, arming, the silence timeout -- and knows nothing about a pin.
 * This turns the answer into edges, by binding a backend to each configured
 * slot and rendering the bank into it every pass.
 *
 * Nothing here decides anything.  If it looks like a policy, it belongs in
 * shared/outputs where the host suite can hold it: the one question this file
 * answers on its own is whether the silicon could do what was asked, and it
 * answers that by refusing a slot rather than by driving something else.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_OUTPUTS_HW_H
#define RCBENCH_OUTPUTS_HW_H

#include <stdbool.h>
#include <stdint.h>

#include "dshot.h"
#include "outputs.h"

/**
 * @brief Forget every binding.
 *
 * Call once, before the first apply.
 */
void outputs_hw_init(void);

/**
 * @brief Make the bindings match the bank's slot table, each slot at the
 * rate in @p rate_hz (OUT_MAX_SLOTS entries, from outputs_slot_rates())
 * rather than its own.
 *
 * Call after anything that changes a slot, a role or a rate.  A slot that
 * has not changed is left alone rather than rebuilt, so reconfiguring one
 * output does not interrupt another; a slot the silicon cannot serve is left
 * unbound, and outputs_hw_bound() says so.
 *
 * @param o  The output bank.
 * @param rate_hz  OUT_MAX_SLOTS entries, from outputs_slot_rates().
 */
void outputs_hw_apply(const outputs_t *o, const uint16_t *rate_hz);

/**
 * @brief As outputs_hw_apply(), with only the slots in @p may_bind bound; a
 * slot outside it is released and left unbound.
 *
 * A refused OUTPUTS write puts
 * back exactly the set that was bound before it, so a slot unbound then
 * cannot take the resource a slot bound then needs.  When a slot that runs
 * on PIO (DShot, PPM) is bound anew, every DShot programming port is let go
 * first, so the slots take the state machines before the ports; the caller
 * binds the ports again with outputs_hw_ports() after.
 *
 * @param o  The output bank.
 * @param rate_hz  OUT_MAX_SLOTS entries, from outputs_slot_rates().
 * @param may_bind  Bit mask of the slots that may be bound. The others are
 *                  released and left unbound.
 */
void outputs_hw_apply_only(const outputs_t *o, const uint16_t *rate_hz,
                           uint8_t may_bind);

/**
 * @brief Bind one slot left out of an apply, as the bank has it, and move nothing
 *        else: outputs_hw_apply_only() lets every DShot port go for a slot on PIO,
 *        and here the line that had the pin has just given back what it took.
 *
 * @param o        The bank.
 * @param rate_hz  OUT_MAX_SLOTS entries, from outputs_slot_rates().
 * @param slot     The slot.
 * @return True when it is bound; false for a bad argument or a bind that failed.
 */
bool outputs_hw_return_slot(const outputs_t *o, const uint16_t *rate_hz,
                            uint8_t slot);

/**
 * @brief Render the bank onto the pins.
 *
 * Call every pass.
 *
 * Nothing edges unless outputs_driving() is true.  DShot has a send rate of
 * its own, because its frame rate is not the bank's business and the OUTPUTS
 * page's rate field is a bit rate for it rather than a frame rate.
 *
 * @param o  The output bank.
 */
void outputs_hw_service(const outputs_t *o);

/**
 * @brief The programming ports' pins: @p pin[n] for port n, LINK_PG_PORTS_MAX
 * entries, a value above OUT_MAX_PIN for a port without one.
 *
 * A port in
 * @p parked (bit n, FUNC NONE) holds its pin as an input with its pull-down and
 * renders nothing.  A port in @p dshot (bit n, FUNC DSHOT150 to DSHOT600) is
 * bound to a DShot state machine at @p rate_hz[n] kbit/s and rendered as a
 * plain DShot slot is: a frame every 1ms while the bank is armed for its
 * channel's role, the throttle, and none while it is not. Any other port's pin
 * is bound to the PWM generator at @p rate_hz[n] frames a second (SERVO PWM).
 * A port whose pin, rate or function moved is released and held again; one that
 * did not is left running.  A port is no slot: it is rendered from bank channel
 * @p channel0 + n.  A pin the chip does not have, or a DShot pin no state
 * machine is left for, is not held.
 *
 * @param pin  Pin of each port, one entry per port.
 * @param rate_hz  Rate of each port: kbit/s for DShot, frames per second
 *                 for PWM.
 * @param parked  Bit n set when port n is parked.
 * @param dshot  Bit n set when port n runs DShot.
 * @param channel0  Bank channel of port 0.
 */
void outputs_hw_ports(const uint8_t *pin, const uint16_t *rate_hz,
                      uint8_t parked, uint8_t dshot, uint8_t channel0);

/**
 * @brief The ports whose function holds their pin, bit n for port n: bound
 * to the generator or to DShot, or parked.
 *
 * @return Bit n set for port n.
 */
uint8_t outputs_hw_ports_bound(void);

/**
 * @brief Which bound ports render their channel's pulse while the bank
 * drives, bit n for port n.
 *
 * A port left out is written a pulse of 0, as a disarmed
 * bank writes it: the pulse in progress completes and the pin rests low.
 * None until told.
 *
 * @param ports  Bit n set for port n.
 */
void outputs_hw_ports_drive(uint8_t ports);

/**
 * @brief Watch output channel @p ch for a move capture's edge; a negative @p
 * ch stops watching.
 *
 * The first later pass that renders a pulse other than
 * the one before it on @p ch's PWM slot or programming port writes it stamped
 * (out_pwm_write_stamped()) and ends the watch: the start of the frame
 * that first carries the new pulse, on time_us_64().  A pulse of 0, the
 * bank stopping, is not an edge.  A slot that is not PWM never stamps.
 *
 * @param ch  Output channel to watch, or a negative value to stop watching.
 */
void outputs_hw_watch(int ch);

/**
 * @brief The edge stamped since the last call, once: true with its time in
 * @p us.
 *
 * @param us  Receives the time of the edge, in microseconds.
 * @return True when an edge was stamped since the last call.
 */
bool outputs_hw_edge(uint64_t *us);

/**
 * @brief Whether slot @p slot is configured and bound to real silicon.
 *
 * @param slot  Slot index.
 * @return True when the slot is bound.
 */
bool outputs_hw_bound(uint8_t slot);

/**
 * @brief The last electrical rpm a bidirectional DShot ESC (electronic speed
 * controller) reported, and how long ago in milliseconds.
 *
 * Electrical rather than mechanical: an ESC reports periods and has no idea
 * what it is bolted to, and the pole count arrives separately.  Returns false
 * when no ESC has ever answered.
 *
 * @param erpm  Receives the electrical rpm.
 * @param age_ms  Receives the time since the reading, in milliseconds.
 * @return True when an ESC has answered. False when none has ever answered.
 */
bool outputs_hw_erpm(uint32_t *erpm, uint32_t *age_ms);

/**
 * @brief The last extended-telemetry reading of @p kind, and how long ago
 * in milliseconds.
 *
 * Raw: the payload byte as the ESC sent it, because the units belong to the
 * frame type and not to this file.  Returns false for a kind no reply has
 * carried, which is every kind on an ESC that does not do extended telemetry
 * and all of them until one has been asked and has answered.
 *
 * Each kind carries its own age.  They do not arrive together: speed comes
 * back on every frame and the rest are interleaved between them.
 *
 * @param kind  The telemetry frame type.
 * @param value  Receives the payload byte, as the ESC sent it.
 * @param age_ms  Receives the time since the reading, in milliseconds.
 * @return True when a reply of @p kind has been received. False otherwise.
 */
bool outputs_hw_edt(dshot_telem_kind_t kind, uint16_t *value, uint32_t *age_ms);

#endif /* RCBENCH_OUTPUTS_HW_H */
