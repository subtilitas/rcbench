/**
 * @file
 * @brief ESC STICK: an ESC (electronic speed controller) programmed through its
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
#include "settings.h"
#include "supply.h"
#include "tone_link.h"
#include "ui_keypad.h"
#include "ui_screen.h"

/** The page set MOTOR & ESC opens: the registry's STICK MENU row. */
extern const prog_page_set_t esc_stick_pages;

/**
 * @brief Where the session is, for tests: closed, on the list, or on a
 *  profile's page (esc_stick_int.h's STAGE_*).
 *
 * @return STAGE_CLOSED, STAGE_LIST or STAGE_PAGE, as an int
 */
int esc_stick_panel_stage(void);

/**
 * @brief The bench as this frame found it, before the frame's touch: the time,
 * whether it is armed, how many stops have been counted and how many of
 * those an operator pressed (arming_pressed_count()), and whether the
 * IOmcu answers.  A stop count that moved ends the warning's hold,
 * and a run: with STOP when every new stop was pressed, else with BENCH
 * STOPPED.
 *
 * @param now_ms the frame's time, ms
 * @param armed true when the bench is armed
 * @param stops the stops counted
 * @param pressed how many of them an operator pressed
 * @param link_up true when the IOmcu answers
 */
void esc_stick_panel_bench(uint32_t now_ms, bool armed, uint32_t stops,
                             uint32_t pressed, bool link_up);

/**
 * @brief A STOP latch was released by its reset: what a stop ends, this
 *        ends.  The warning's hold under way drops, and a run that queued
 *        an ARM the application has not taken ends with STOP, so a hold
 *        begun while the STOP stood never arms after the release.
 */
void esc_stick_panel_stop_released(void);

/**
 * @brief The M5Stack Dial's HOLD on MOTOR & ESC (motor_screen_dial_hold()):
 *        a run under way ends as ABORT ends it, an ARM it queued and the
 *        application has not taken included, so a run in ARMING never
 *        arms after the HOLD.
 * @return true when a run ended.
 */
bool esc_stick_panel_dial_hold(void);

/**
 * @brief The output binding, every frame: whether one is read
 * (BIND_READ_OK), and the channels it binds as a throttle
 * (outputs_role_channels(), one bit each).  RUN is refused
 * while none is read or it binds no throttle, and while the supply is the
 * panel's model (supply_screen_model()); the note names which.  Zero at
 * start: nothing is known until the application says.
 *
 * @param read true when a binding is read
 * @param channels the throttle channels, one bit each
 */
void esc_stick_panel_set_throttle(bool read, uint8_t channels);

/**
 * @brief One supply sample, every one, in the order they were taken.
 *
 * @param st the sample
 */
void esc_stick_panel_supply(const supply_state_t *st);

/**
 * @brief The phase tap as the control task last read it (tone_link_readout()),
 *        every frame.
 *
 * While the tap is enabled on SETUP a stick run's page
 * shows it: its state, the last window's pitch, the last beeps, and the
 * counts of beeps lost and beeps not read.  The pitch and the lost count
 * read "--" while tone_readout_t.read is false.
 *
 * A run started while the capture runs (TONE_STATE_RUNNING, read) counts
 * its beeps from the tap: each frame hands the run the tap's state and the
 * beeps that came in since the last frame, oldest first.  More than
 * TONE_LINK_SHOWN beeps between two frames leave a number out, and the run
 * ends with BEEP LOST.
 *
 * @param r  The readout; NULL changes nothing.
 */
void esc_stick_panel_tone(const tone_readout_t *r);

/** @brief The INA3221's window period, ms (SENSE_WIN_A and SENSE_WIN_B). */
#define ESC_STICK_PANEL_WINDOW_MS 50u

/**
 * @brief One window of the INA3221 channel in the ESC's supply line, each
 *        window once, in order: the lowest channel of a fitted part with a
 *        shunt that watches SUPPLY on SETUP > SENSORS.
 *
 * A run started with no running tap counts its beeps from these windows
 * while one has come in within ESC_STICK_STALE_MS; with neither source RUN
 * is refused.  The windows are timed by their number, ESC_STICK_PANEL_
 * WINDOW_MS apart on the IOmcu's clock.
 *
 * @param channel     The channel, 0 to 11: 0x40 + channel / 3, CH channel % 3 + 1.
 * @param window      The window's number, modulo 65536.
 * @param max_ma      Its highest current, mA; a sample at the top of the
 *                    range is given as INT16_MAX.
 * @param current_ok  It holds current samples.
 */
void esc_stick_panel_meter(unsigned channel, uint16_t window, int32_t max_ma,
                           bool current_ok);

/**
 * @brief A command a stick run asks for, oldest first; true when there was
 *   one.
 *
 * @param out receives the command
 * @return true when there was one
 */
bool esc_stick_panel_poll_cmd(motor_cmd_t *out);

/**
 * @brief The profile of a stick run under way, for the simulated ESC on the
 *  panel's modelled supply; NULL when none is.
 *
 * @return the profile; NULL when none is under way
 */
const esc_profile_t *esc_stick_panel_profile(void);

/**
 * @brief How many stick runs have started, so a new one starts a fresh
 *  simulation.
 *
 * @return the count
 */
uint32_t esc_stick_panel_runs(void);

/**
 * @brief The stick run, under way or ended, for tests.
 *
 * @return the stick run
 */
const esc_stick_t *esc_stick_panel_stick(void);

/**
 * @brief The ESC STICK search as typed, "" for none; for tests.
 *
 * @return the search text
 */
const char *esc_stick_panel_search(void);
/**
 * @brief Whether the search's keyboard is open; for tests.
 *
 * @return true while it is open
 */
bool esc_stick_panel_typing(void);

/**
 * @brief The SETTINGS list's keypad, which a tap on a row's value opens
 *        over the list; for tests and the renderer.
 * @return The keypad, open or closed.
 */
const ui_keypad_t *esc_stick_panel_keypad(void);

/**
 * @brief The setting the SETTINGS list's open keypad types; for tests.
 * @return Its setting_id_t, or -1 while the keypad is closed.
 */
int esc_stick_panel_keypad_setting(void);

struct ui_setlist;
/**
 * @brief The SETTINGS list the footer opens on a profile's page: the 14
 *        settings of SET_CAT_STICK (esc_stick_settings.c); for tests and
 *        the renderer.
 * @return The list, open or closed.
 */
const struct ui_setlist *esc_stick_settings_list(void);

struct ui_help_page;
/**
 * @brief The guide's pages over the session as it is now
 *        (esc_stick_guide.c): the list's two, a profile's two, or the
 *        OPTIONS list's.
 * @return The pages, 0 over the keyboard, the warning, the manual steps,
 *         a run and its result.
 */
int esc_stick_help_count(void);
/**
 * @brief Page @p i of those esc_stick_help_count() counts.
 * @param i   The page, from 0.
 * @param out The page, filled.
 * @return False past the last page.
 */
bool esc_stick_help_page(int i, struct ui_help_page *out);

/**
 * @brief The SETTINGS list covers the body, MOTOR & ESC's output strip
 *        included; MOTOR & ESC draws no strip under it.
 * @return Whether the list is open.
 */
bool esc_stick_settings_shown(void);

/**
 * @brief The list's row of setting @p id; for tests.
 * @param id The setting.
 * @return The row, from 0, headings counted; -1 for a setting not listed.
 */
int esc_stick_settings_row(setting_id_t id);

/**
 * @brief The list scrolled so setting @p id's row shows whole; for tests
 *        and the renderer.
 * @param id The setting; one not listed leaves the scroll.
 */
void esc_stick_settings_show(setting_id_t id);

/**
 * @brief How many profiles the list holds under the search, and the index in it
 *  of the top row shown; for tests.
 *
 * @param top receives the index of the top row shown; may be NULL
 * @return the profiles held
 */
int esc_stick_panel_listed(int *top);

/**
 * @brief The list's level; for tests.
 *
 * @return 0 the makers, 1 one maker's models
 */
int esc_stick_panel_level(void);
/**
 * @brief The maker open on level 1; for tests.
 *
 * @return the maker; "" on level 0
 */
const char *esc_stick_panel_maker(void);
/**
 * @brief The model index of the page open; for tests.
 *
 * @return the index; -1 for none
 */
int esc_stick_panel_model(void);
/**
 * @brief Maker @p i of level 0; for tests.
 *
 * @param i the maker, from 0
 * @return its name; NULL past the last
 */
const char *esc_stick_panel_maker_at(int i);
/**
 * @brief Row @p i of a maker's models; for tests.
 *
 * @param i the row, from 0
 * @param model receives the row's model index; may be NULL
 * @return the profile; NULL past the last
 */
const esc_profile_t *esc_stick_panel_row(int i, int *model);

/**
 * @brief Why row @p i of a maker's models does not run, in the language
 *  showing, or NULL when it runs or there is no such row; for tests.
 *
 * @param i the row, from 0
 * @return the reason; NULL when it runs or there is no such row
 */
const char *esc_stick_panel_row_why(int i);

/**
 * @brief The item row @p i of the page shows, as an index into the profile's
 *  items; -1 past the last.  For tests.
 *
 * @param i the row, from 0
 * @return the item's index; -1 past the last
 */
int esc_stick_panel_item_at(int i);

/**
 * @brief The warning row @p i shows while it runs: the ESC's voltage rating
 *  unknown; NULL for none.  For tests.
 *
 * @param i the row, from 0
 * @return the warning; NULL for none
 */
const char *esc_stick_panel_row_warn(int i);

/**
 * @brief The result's count of the selections made, as drawn now; for tests.
 *
 * @return the line
 */
const char *esc_stick_panel_result_head(void);
/**
 * @brief The result's line for the changes past the fourth, "" when all show;
 *         for tests.
 *
 * @return the line
 */
const char *esc_stick_panel_result_more(void);

/**
 * @brief The prompt's line on what no DONE does, as the prompt would draw it
 *  now; for tests.
 *
 * @return the line
 */
const char *esc_stick_panel_left_text(void);

/**
 * @brief The note the page shows under RUN: why a run cannot start, "" for
 *  none; for tests.
 *
 * @return the note; "" for none
 */
const char *esc_stick_panel_note(void);

/**
 * @brief The first refusal that stands against every run as the bench is
 *        now, as a short tag: the supply the panel's model (no PD mini),
 *        the output binding not read, no throttle pin, no beep source.  In
 *        the order RUN's note names them.  MOTOR & ESC > PROGRAM shows it on
 *        the ESC STICK row and the maker list in its header, so the bench's
 *        lack is known before a profile is picked.
 * @return The tag in the language showing, at most 22 cells; NULL while
 *         none stands.
 */
const char *esc_stick_panel_bench_tag(void);

/**
 * @brief The profile whose page is open, or NULL on the list; for tests.
 *
 * @return the profile; NULL on the list
 */
const esc_profile_t *esc_stick_panel_page(void);

/**
 * @brief A manual step's text in the language showing: its action_de where the
 *  profile gives one and German shows, else its English action.
 *
 * @param m the manual step
 * @return its text
 */
const char *esc_stick_panel_step_text(const esc_manual_t *m);

/**
 * @brief A profile's family in the language showing: its family_de where the
 *        profile gives one and German shows, else its English family.
 * @param p The profile, or NULL.
 * @return The text, valid as long as the profile; "" for NULL.
 */
const char *esc_stick_panel_family_text(const esc_profile_t *p);

/**
 * @brief The profile page's crumb trail: ESC STICK, the brand, the model
 *        where @p model names one, and the family in the language showing
 *        (esc_stick_panel_family_text()), in @p cells cells at most.
 *
 * The family shows whole where it fits; else its whole words that fit with
 * " .." after them, never part of a word; else not at all.  A brand and
 * model wider than @p cells alone end in "..", with no family.
 *
 * @param p     The profile, or NULL for "".
 * @param model The model's index in @p p, or -1 for none.
 * @param cells The trail's room, in cells.
 * @param out   Takes the trail.
 * @param n     Bytes of @p out.
 */
void esc_stick_panel_crumb(const esc_profile_t *p, int model, int cells,
                           char *out, size_t n);

/**
 * @brief Why the manual steps' pop-up says its profile does not run, for the
 *  model it was opened for; NULL when it runs or no pop-up is open.  For
 *  tests.
 *
 * @return the reason; NULL when it runs or no pop-up is open
 */
const char *esc_stick_panel_hand_why(void);

/**
 * @brief The entry time the profile's page shows, ms; for tests.
 *
 * @return the time, ms
 */
uint32_t esc_stick_panel_entry_shown(void);

/**
 * @brief Whether the supply reads off now, by the rule a run holds it to: its
 *  own state off, the current at or under ESC_STICK_OFF_MA for
 *  ESC_STICK_OFF_SETTLE_MS, in a reading no older than ESC_STICK_STALE_MS.
 *  For tests.
 *
 * @return true when the supply reads off
 */
bool esc_stick_panel_supply_reads_off(void);

/**
 * @brief Whether the manual steps' pop-up is open; for tests.
 *
 * @return true while it is open
 */
bool esc_stick_panel_hand_shown(void);

/**
 * @brief What the stack light shows; for tests.
 *
 * @param red receives whether the red light shows
 * @param green receives whether the green light shows
 */
void esc_stick_panel_lights(bool *red, bool *green);

/**
 * @brief A refusal of the stick engine (esc_stick_kind(), esc_stick_check(),
 * esc_stick_start()) in the language showing, found by the engine's
 * words; one the screen does not know comes back as given, and NULL as
 * "refused".
 *
 * @param why the engine's refusal
 * @return the text in the language showing
 */
const char *esc_stick_panel_why_text(const char *why);

/**
 * @brief The lines @p text takes broken as the screen breaks it, in lines of
 * @p cells, one cell at least; for tests.
 *
 * @param text the text
 * @param cells a line's width in cells
 * @return the lines
 */
int esc_stick_panel_wrap_lines(const char *text, int cells);
