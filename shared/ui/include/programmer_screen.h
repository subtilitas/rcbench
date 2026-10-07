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
 * whether it is armed, how many stops have been counted and how many of
 * those an operator pressed (arming_pressed_count()), and whether the
 * coprocessor answers.  A stop count that moved ends the warning's hold,
 * and a run: with STOP when every new stop was pressed, else with BENCH
 * STOPPED.
 */
void programmer_screen_bench(uint32_t now_ms, bool armed, uint32_t stops,
                             uint32_t pressed, bool link_up);

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

/** The ESC STICK search as typed, "" for none, and whether its keyboard
 *  is open; for tests. */
const char *programmer_screen_stick_search(void);
bool programmer_screen_stick_typing(void);

/** How many profiles the list holds under the search, and the index in it
 *  of the top row shown; for tests. */
int programmer_screen_stick_listed(int *top);

/** The list's level: 0 the makers, 1 one maker's models; the maker open
 *  ("" on level 0); the page's model index (-1 for none); and the rows of
 *  each level, NULL past the end.  For tests. */
int programmer_screen_stick_level(void);
const char *programmer_screen_stick_maker(void);
int programmer_screen_stick_model(void);
const char *programmer_screen_stick_maker_at(int i);
const esc_profile_t *programmer_screen_stick_row(int i, int *model);

/** The profile whose page is open, or NULL on the list; for tests. */
const esc_profile_t *programmer_screen_stick_page(void);

/** A manual step's text in the language showing: its action_de where the
 *  profile gives one and German shows, else its English action. */
const char *programmer_screen_step_text(const esc_manual_t *m);

/** Why the manual steps' pop-up says its profile does not run, for the
 *  model it was opened for; NULL when it runs or no pop-up is open.  For
 *  tests. */
const char *programmer_screen_stick_hand_why(void);

/** Whether the supply reads off now, by the rule a run holds it to: its
 *  own state off, the current at or under ESC_STICK_OFF_MA for
 *  ESC_STICK_OFF_SETTLE_MS, in a reading no older than ESC_STICK_STALE_MS.
 *  For tests. */
bool programmer_screen_stick_supply_reads_off(void);

/** Whether the manual steps' pop-up is open; for tests. */
bool programmer_screen_stick_hand_shown(void);

/** What the stack light shows; for tests. */
void programmer_screen_stick_lights(bool *red, bool *green);

/**
 * A refusal of the stick engine (esc_stick_kind(), esc_stick_check(),
 * esc_stick_start()) in the language showing, found by its English; one the
 * screen does not know comes back as given, and NULL as "refused".
 */
const char *programmer_screen_why_text(const char *why);
