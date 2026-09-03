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

#include "outputs.h"

/** Forget every binding.  Call once, before the first apply. */
void outputs_hw_init(void);

/**
 * Make the bindings match the bank's slot table.
 *
 * Call after anything that changes a slot.  A slot that has not changed is
 * left alone rather than rebuilt, so reconfiguring one output does not
 * interrupt another; a slot the silicon cannot serve is left unbound, and
 * outputs_hw_bound() says so.
 */
void outputs_hw_apply(const outputs_t *o);

/**
 * Render the bank onto the pins.  Call every pass.
 *
 * Nothing edges unless outputs_driving() is true.  DShot has a send rate of
 * its own, because its frame rate is not the bank's business and the OUTPUTS
 * page's rate field is a bit rate for it rather than a frame rate.
 */
void outputs_hw_service(const outputs_t *o);

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

#endif /* RCBENCH_OUTPUTS_HW_H */
