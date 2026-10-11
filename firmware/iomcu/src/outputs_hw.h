/*
 * The seam between the output bank and the pins.
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

/** Forget every binding.  Call once, before the first apply. */
void outputs_hw_init(void);

/**
 * Make the bindings match the bank's slot table, each slot at the rate in
 * @p rate_hz (OUT_MAX_SLOTS entries, from outputs_slot_rates()) rather than
 * its own.
 *
 * Call after anything that changes a slot, a role or a rate.  A slot that
 * has not changed is left alone rather than rebuilt, so reconfiguring one
 * output does not interrupt another; a slot the silicon cannot serve is left
 * unbound, and outputs_hw_bound() says so.
 */
void outputs_hw_apply(const outputs_t *o, const uint16_t *rate_hz);

/*
 * As outputs_hw_apply(), with only the slots in @p may_bind bound; a slot
 * outside it is released and left unbound.  A refused OUTPUTS write puts
 * back exactly the set that was bound before it, so a slot unbound then
 * cannot take the resource a slot bound then needs.
 */
void outputs_hw_apply_only(const outputs_t *o, const uint16_t *rate_hz,
                           uint8_t may_bind);

/**
 * Render the bank onto the pins.  Call every pass.
 *
 * Nothing edges unless outputs_driving() is true.  DShot has a send rate of
 * its own, because its frame rate is not the bank's business and the OUTPUTS
 * page's rate field is a bit rate for it rather than a frame rate.
 */
void outputs_hw_service(const outputs_t *o);

/**
 * The slots that render no PWM (pulse-width modulation) pulse, one bit
 * each, whatever the bank asks: a held slot is written a pulse of 0, as a
 * disarmed bank writes it, so the pulse in progress completes and the pin
 * rests low.  The KST programming port holds its channel's slot this way
 * (shared/outputs/kst_port.h).  Other drivers are not held.
 */
void outputs_hw_hold(uint8_t slots);

/**
 * Watch output channel @p ch for a move capture's edge; a negative @p ch
 * stops watching.  The first later pass that renders a pulse other than
 * the one before it on @p ch's PWM slot writes it stamped
 * (out_pwm_write_stamped()) and ends the watch: the start of the frame
 * that first carries the new pulse, on time_us_64().  A pulse of 0, the
 * bank stopping, is not an edge.  A slot that is not PWM never stamps.
 */
void outputs_hw_watch(int ch);

/** The edge stamped since the last call, once: true with its time in
 *  @p us. */
bool outputs_hw_edge(uint64_t *us);

/** Whether slot @p slot is configured and bound to real silicon. */
bool outputs_hw_bound(uint8_t slot);

/**
 * The last electrical rpm a bidirectional DShot ESC (electronic speed
 * controller) reported, and how long ago in milliseconds.
 *
 * Electrical rather than mechanical: an ESC reports periods and has no idea
 * what it is bolted to, and the pole count arrives separately.  Returns false
 * when no ESC has ever answered.
 */
bool outputs_hw_erpm(uint32_t *erpm, uint32_t *age_ms);

/**
 * The last extended-telemetry reading of @p kind, and how long ago in
 * milliseconds.
 *
 * Raw: the payload byte as the ESC sent it, because the units belong to the
 * frame type and not to this file.  Returns false for a kind no reply has
 * carried, which is every kind on an ESC that does not do extended telemetry
 * and all of them until one has been asked and has answered.
 *
 * Each kind carries its own age.  They do not arrive together: speed comes
 * back on every frame and the rest are interleaved between them.
 */
bool outputs_hw_edt(dshot_telem_kind_t kind, uint16_t *value, uint32_t *age_ms);

#endif /* RCBENCH_OUTPUTS_HW_H */
