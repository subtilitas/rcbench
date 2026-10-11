/*
 * The KST and Cha servo programmer on the SERVO screen: an overlay over the
 * screen's left card, opened with PROGRAM.
 *
 * The overlay draws kst_prog.h's model and turns touches into its calls.  It
 * holds no rule of its own: ranges, limits and warnings come from the model,
 * which asks protocols/kst.  Its controls are the screen's: tabs, rows, the
 * number keypad, the choice list, plus and minus keys that repeat while
 * held, and a warning page whose button is held UI_HOLD_S.
 *
 * Five tabs.  SERVO: channel, port, read, backup, restore, the write
 * switch and the write.  TUNE, PULSE and FAILSAFE: the editable fields.
 * LOCKED: the fields that are shown and never written by an edit.
 *
 * The overlay stays open while the port holds a channel: PWM and
 * programming are exclusive states of that channel, and CLOSE asks for the
 * way back first.
 *
 * The panel's control task owns the link.  It hands this a feed every frame
 * and takes the commands; the file goes to the card through the caller.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx.h"
#include "kst_prog.h"
#include "touch_types.h"
#include "ui_keypad.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Lines a message takes at most, and the bytes of one. */
#define KST_PANEL_MSG_LINES 3
#define KST_PANEL_LINE      96

/** Start over: closed, no servo, nothing stored.  @p area is the overlay's. */
void kst_panel_init(gfx_rect_t area);

/** The SERVO screen was entered: ENABLE WRITE TO SERVO goes off. */
void kst_panel_screen_entry(void);

void kst_panel_open(void);
bool kst_panel_is_open(void);

/**
 * CLOSE: closes while the port holds no channel, asks for the way back to
 * PWM while it is in programming mode, and does nothing while it moves.
 */
void kst_panel_close(void);

/** The port holds a channel, or moves: the overlay is shown. */
bool kst_panel_holds_port(void);

/** The screen is left: presses, holds and editors go; the port stays. */
void kst_panel_leave(void);

/** Touch events were lost: the press and its hold are dropped. */
void kst_panel_cancel(void);

void kst_panel_tick(float dt_s);

/** True while a press that began in the overlay is on the glass. */
bool kst_panel_has_press(uint8_t id);

/** A press in the overlay's area, or the rest of one it owns. */
void kst_panel_event(const touch_event_t *evt);

void kst_panel_render(gfx_canvas_t *c);

/** Changes whenever a render would show something else. */
uint32_t kst_panel_revision(void);

/** Changes while a warning's button fills; kst_panel_apply_rect() is where. */
uint32_t kst_panel_hold_revision(void);

/* --- the control task's side ------------------------------------------------ */

void kst_panel_feed(const kst_prog_feed_t *f);
bool kst_panel_take(kst_prog_cmd_t *out);
size_t kst_panel_file_take(char *out, size_t n);
void kst_panel_file_done(bool ok);
bool kst_panel_file_load(const char *text, size_t len);

/* --- for the tests ------------------------------------------------------------ */

kst_prog_t *kst_panel_model(void);

/**
 * A message as the overlay writes it, in the language in force.  Returns
 * the lines used, 0 for no message.
 */
int kst_panel_message_lines(const kst_prog_msg_t *m,
                            char out[KST_PANEL_MSG_LINES][KST_PANEL_LINE]);

/** The line a rule's refusal or warning shows. */
const char *kst_panel_rule_text(unsigned rule);

/** Where things are, for a test's taps.  Rows by page, row and column. */
gfx_rect_t kst_panel_tab_rect(int page);
gfx_rect_t kst_panel_close_rect(void);
gfx_rect_t kst_panel_value_rect(int page, int row, int col);
gfx_rect_t kst_panel_step_rect(int page, int row, bool up);
gfx_rect_t kst_panel_choice_rect(int i);
gfx_rect_t kst_panel_choice_cancel_rect(void);
gfx_rect_t kst_panel_apply_rect(void);
gfx_rect_t kst_panel_cancel_rect(void);
ui_keypad_t *kst_panel_keypad(void);

/** What is open over the page: 0 none, 1 keypad, 2 choice list, 3 warning. */
int kst_panel_editor(void);

/** The tab shown: 0 SERVO, 1 TUNE, 2 PULSE, 3 FAILSAFE, 4 LOCKED. */
int kst_panel_page(void);

/** Line @p i of the open warning page, entry @p i of the open list; NULL
 *  past the last and while none is open. */
const char *kst_panel_warning_line(int i);
const char *kst_panel_choice_label(int i);

#ifdef __cplusplus
}
#endif
