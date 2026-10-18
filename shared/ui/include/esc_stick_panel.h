/*
 * ESC STICK: an ESC (electronic speed controller) programmed through its
 * throttle-stick menu, the session MOTOR & ESC > PROGRAM opens from the
 * STICK MENU row of its list (prog_registry.h).
 *
 * The session runs the stick programmer (esc_stick.h).  Like the MOTOR &
 * ESC screen it owns no hardware: the application hands it the bench's
 * state and the supply's samples, and takes back the ARM, DISARM and
 * THROTTLE commands it posts.  The supply is switched through the SUPPLY
 * screen's own ON and OFF.
 *
 * The session is a full-width page set: it lays out over the whole body
 * under MOTOR & ESC's strip row, where the host draws the output strip
 * (PROG_FULL_*).  Its levels, each one home tag up: TIMING or a run on a
 * profile's page, the page, one maker's models, the makers, then out of
 * the session to PROGRAM's list.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esc_stick.h"
#include "motor_screen.h"
#include "prog_registry.h"
#include "supply.h"
#include "tone_link.h"
#include "ui_screen.h"

/** The page set MOTOR & ESC opens: the registry's STICK MENU row. */
extern const prog_page_set_t esc_stick_pages;

/** Where the session is, for tests: closed, on the list, or on a
 *  profile's page (esc_stick_int.h's STAGE_*). */
int esc_stick_panel_stage(void);

/**
 * The bench as this frame found it, before the frame's touch: the time,
 * whether it is armed, how many stops have been counted and how many of
 * those an operator pressed (arming_pressed_count()), and whether the
 * IOmcu answers.  A stop count that moved ends the warning's hold,
 * and a run: with STOP when every new stop was pressed, else with BENCH
 * STOPPED.
 */
void esc_stick_panel_bench(uint32_t now_ms, bool armed, uint32_t stops,
                             uint32_t pressed, bool link_up);

/**
 * The output binding, every frame: whether one is read
 * (BIND_READ_OK), and the channels it binds as a throttle
 * (outputs_role_channels(), one bit each).  RUN is refused
 * while none is read or it binds no throttle, and while the supply is the
 * panel's model (supply_screen_model()); the note names which.  Zero at
 * start: nothing is known until the application says.
 */
void esc_stick_panel_set_throttle(bool read, uint8_t channels);

/** One supply sample, every one, in the order they were taken. */
void esc_stick_panel_supply(const supply_state_t *st);

/**
 * The phase tap as the control task last read it (tone_link_readout()),
 * every frame.  While the tap is enabled on SETUP a stick run's page
 * shows it: its state, the last window's pitch, the last beeps, and the
 * counts of beeps lost and beeps not read.  The pitch and the lost count
 * read "--" while tone_readout_t.read is false.  Read only: the run still
 * counts its beeps from the supply current.
 */
void esc_stick_panel_tone(const tone_readout_t *r);

/** A command a stick run asks for, oldest first; true when there was one. */
bool esc_stick_panel_poll_cmd(motor_cmd_t *out);

/** The profile of a stick run under way, for the simulated ESC on the
 *  panel's modelled supply; NULL when none is. */
const esc_profile_t *esc_stick_panel_profile(void);

/** How many stick runs have started, so a new one starts a fresh
 *  simulation. */
uint32_t esc_stick_panel_runs(void);

/** The stick run, under way or ended, for tests. */
const esc_stick_t *esc_stick_panel_stick(void);

/** The ESC STICK search as typed, "" for none, and whether its keyboard
 *  is open; for tests. */
const char *esc_stick_panel_search(void);
bool esc_stick_panel_typing(void);

/** How many profiles the list holds under the search, and the index in it
 *  of the top row shown; for tests. */
int esc_stick_panel_listed(int *top);

/** The list's level: 0 the makers, 1 one maker's models; the maker open
 *  ("" on level 0); the page's model index (-1 for none); and the rows of
 *  each level, NULL past the end.  For tests. */
int esc_stick_panel_level(void);
const char *esc_stick_panel_maker(void);
int esc_stick_panel_model(void);
const char *esc_stick_panel_maker_at(int i);
const esc_profile_t *esc_stick_panel_row(int i, int *model);

/** Why row @p i of a maker's models does not run, in the language
 *  showing, or NULL when it runs or there is no such row; for tests. */
const char *esc_stick_panel_row_why(int i);

/** The item row @p i of the page shows, as an index into the profile's
 *  items; -1 past the last.  For tests. */
int esc_stick_panel_item_at(int i);

/** The warning row @p i shows while it runs: the ESC's voltage rating
 *  unknown; NULL for none.  For tests. */
const char *esc_stick_panel_row_warn(int i);

/** The result's count of the selections made, and its line for the
 *  changes past the fourth ("" when all show), as drawn now; for tests. */
const char *esc_stick_panel_result_head(void);
const char *esc_stick_panel_result_more(void);

/** The prompt's line on what no DONE does, as the prompt would draw it
 *  now; for tests. */
const char *esc_stick_panel_left_text(void);

/** The note the page shows under RUN: why a run cannot start, "" for
 *  none; for tests. */
const char *esc_stick_panel_note(void);

/** The profile whose page is open, or NULL on the list; for tests. */
const esc_profile_t *esc_stick_panel_page(void);

/** A manual step's text in the language showing: its action_de where the
 *  profile gives one and German shows, else its English action. */
const char *esc_stick_panel_step_text(const esc_manual_t *m);

/** Why the manual steps' pop-up says its profile does not run, for the
 *  model it was opened for; NULL when it runs or no pop-up is open.  For
 *  tests. */
const char *esc_stick_panel_hand_why(void);

/** The entry time the profile's page shows, ms; for tests. */
uint32_t esc_stick_panel_entry_shown(void);

/** Whether the supply reads off now, by the rule a run holds it to: its
 *  own state off, the current at or under ESC_STICK_OFF_MA for
 *  ESC_STICK_OFF_SETTLE_MS, in a reading no older than ESC_STICK_STALE_MS.
 *  For tests. */
bool esc_stick_panel_supply_reads_off(void);

/** Whether the manual steps' pop-up is open; for tests. */
bool esc_stick_panel_hand_shown(void);

/** What the stack light shows; for tests. */
void esc_stick_panel_lights(bool *red, bool *green);

/**
 * A refusal of the stick engine (esc_stick_kind(), esc_stick_check(),
 * esc_stick_start()) in the language showing, found by the engine's
 * words; one the screen does not know comes back as given, and NULL as
 * "refused".
 */
const char *esc_stick_panel_why_text(const char *why);

/**
 * The lines @p text takes broken as the screen breaks it, in lines of
 * @p cells, one cell at least; for tests.
 */
int esc_stick_panel_wrap_lines(const char *text, int cells);
