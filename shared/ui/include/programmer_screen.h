/*
 * The programmer screen: one renderer for several protocols.
 *
 * The ESC STICK class runs the stick programmer (esc_stick.h).  Like the
 * MOTOR screen it owns no hardware: the application hands it the bench's
 * state and the supply's samples, and takes back the ARM, DISARM and
 * THROTTLE commands it posts.  The supply is switched through the SUPPLY
 * screen's own ON and OFF.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esc_stick.h"
#include "motor_screen.h"
#include "supply.h"
#include "ui_screen.h"

/** Drop the cached chrome, so the next frame repaints it. */
void programmer_invalidate(void);

const ui_screen_t *programmer_screen(void);

/** Which protocol is selected, for the application and for tests. */
int  programmer_screen_protocol(void);
/** True after a device has answered on the selected protocol. */
bool programmer_screen_connected(void);
/** The staged value of a parameter, or -1 if there is no such row. */
int  programmer_screen_value(int param);
/** How many staged values differ from what was read off the device. */
int  programmer_screen_dirty(void);

/**
 * The bench as this frame found it, before the frame's touch: the time,
 * whether it is armed, how many stops have been counted, and whether the
 * coprocessor answers.  A stop count that moved ends the warning's hold.
 */
void programmer_screen_bench(uint32_t now_ms, bool armed, uint32_t stops,
                             bool link_up);

/** One supply sample, every one, in the order they were taken. */
void programmer_screen_supply(const supply_state_t *st);

/** A command a stick run asks for, oldest first; true when there was one. */
bool programmer_screen_poll_cmd(motor_cmd_t *out);

/** The profile of a stick run under way, for the simulated ESC on the
 *  panel's modelled supply; NULL when none is. */
const esc_profile_t *programmer_screen_stick_profile(void);

/** How many stick runs have started, so a new one starts a fresh
 *  simulation. */
uint32_t programmer_screen_stick_runs(void);

/** The stick run, under way or ended, for tests. */
const esc_stick_t *programmer_screen_stick(void);
