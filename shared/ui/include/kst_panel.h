/**
 * @file kst_panel.h
 * @brief The KST and Chaservo programmer's overlay on SERVO: its interface.
 *
 * The programmer for KST and Chaservo servos on the SERVO screen: a
 * session over the screen's left column, opened from its row on SERVO >
 * PROGRAM (prog_registry.h) one level down.
 *
 * The overlay draws kst_prog.h's model and turns touches into its calls.  It
 * holds no rule of its own: ranges, limits and warnings come from the model,
 * which asks protocols/kst.  Its controls are the screen's: tabs, rows, the
 * number keypad, the choice list, plus and minus keys that repeat while
 * held, the keyboard for a name, and a warning page whose button is held
 * UI_HOLD_S.
 *
 * Five tabs.  SERVO: the port, read, the servo files on the card,
 * restore, the write switch and the write.  TUNE, PULSE and FAILSAFE: the editable fields.
 * LOCKED: the fields that are shown and never written by an edit.
 *
 * The overlay stays open while the port holds its pin: PWM and
 * programming are exclusive states of that pin, and CLOSE asks for the
 * way back first.
 *
 * The panel's control task owns the link.  It hands this a feed every frame
 * and takes the commands; the files go to the card through the caller.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx.h"
#include "kst_prog.h"
#include "prog_registry.h"
#include "touch_types.h"
#include "ui_keypad.h"
#include "ui_textkey.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The highest supply voltage a standard servo is rated for, mV: a
 *  programming voltage above it needs HV SERVO and a 2s hold. */
#define KST_PANEL_STD_MV 6000u
/** Characters of a file name typed on the keyboard at most.  A file with a
 *  longer name, up to 32 characters, is listed and loaded; a line that
 *  names it shows its first 14 characters and "..". */
#define KST_PANEL_NAME_TYPED 16
/** Lines a message takes at most, and the bytes of one. */
#define KST_PANEL_MSG_LINES 3
#define KST_PANEL_LINE      96

/** Start over: closed, no servo, nothing stored.  @p area is the overlay's. */
void kst_panel_init(gfx_rect_t area);

/** The SERVO screen was entered: ENABLE WRITE TO SERVO goes off. */
void kst_panel_screen_entry(void);

/**
 * @brief Opens the overlay and reads the programmer's options from the
 *        settings.
 */
void kst_panel_open(void);
/**
 * @brief Returns whether the overlay is open.
 * @return true when open
 */
bool kst_panel_is_open(void);

/**
 * CLOSE: closes while the tester has the port, asks for the way back to
 * PWM while it is in programming mode, and does nothing while it moves.
 */
void kst_panel_close(void);

/**
 * The home tag or BACK in the session: an editor open in it (keypad,
 * list, warning, keyboard) closes as its CANCEL does; otherwise as
 * kst_panel_close(), so a port in programming asks for LEAVE first.
 */
void kst_panel_back(void);

/** The port is in programming, or moves: the overlay is shown. */
bool kst_panel_holds_port(void);

/** The GPIO the port the overlay works on drives, as the feed says, or
 *  PROG_REG_NO_PIN. */
uint8_t kst_panel_pin(void);

/** None of the session's controls stops what the bench drives: it works
 *  on a SAFE bench only (prog_page_set_t's safe_at).  Always false. */
bool kst_panel_safe_at(int x, int y);

/** The session's hooks for its row on SERVO > PROGRAM. */
extern const prog_page_set_t kst_panel_pages;

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

/**
 * @brief Gives the model the link task's feed for this frame and updates the
 *        overlay's revision.
 * @param f the feed
 */
void kst_panel_feed(const kst_prog_feed_t *f);
/** HV SERVO of the SERVO screen: off, the programming voltage stays at or
 *  below KST_PANEL_STD_MV, and one set above it goes back to
 *  KST_PROG_SUPPLY_MV.  On, it allows a voltage above KST_PANEL_STD_MV
 *  through the warning page and its hold; no such voltage is stored or
 *  loaded. */
void kst_panel_set_hv(bool on);
/** The bench is armed or asked to arm: CONNECT and every operation that
 *  sends frames are refused with KST_PROG_MSG_NEED_DISARM.  No screen
 *  takes an arm while kst_panel_holds_port(): ARM on the SERVO and MOTOR
 *  screens takes no press, and the ESC stick programmer starts no run. */
void kst_panel_bench_armed(bool armed);
/** True once after the overlay was opened or one of its actions tapped
 *  with a feed: the caller, which has the card, looks at the slot, mounts
 *  a card put in or swapped and lets one taken out go. */
bool kst_panel_card_look(void);
/**
 * @brief Takes the next command for the link task.
 * @param out the command
 * @return true when a command was taken
 */
bool kst_panel_take(kst_prog_cmd_t *out);
/** The gate the task that owns the link module claims each START at
 *  (kst_prog_gate_set()).  Kept over kst_panel_init().  NULL for none. */
void kst_panel_gate(kst_prog_gate_t *g);
/** The servo files: a request for the task that has the card, and what it
 *  made of it (kst_card_run()). */
bool kst_panel_card_take(kst_prog_card_req_t *out);
/**
 * @brief Gives the model the outcome of a card operation.
 * @param done the outcome
 */
void kst_panel_card_done(const kst_prog_card_done_t *done);
/**
 * @brief Takes the name of the file the card is to hold; the text itself is
 *        copied into @p out.
 * @param out buffer written to
 * @param n size of @p out, bytes
 * @return the length written
 */
size_t kst_panel_file_take(char *out, size_t n);
/**
 * @brief Gives the model the result of the file write the card made.
 * @param ok true when the card wrote the file
 */
void kst_panel_file_done(bool ok);
/**
 * @brief Loads the text of a servo file the card read into the draft.
 * @param text the file's text
 * @param len the length of @p text, bytes
 * @return true when the model took the file
 */
bool kst_panel_file_load(const char *text, size_t len);
/** The same for the file of a card mounted after the start.  A record the
 *  model holds already stands, before and after the read that follows the
 *  interrupted write: the text is not used then, and false is returned
 *  with nothing said. */
bool kst_panel_file_late(const char *text, size_t len);

/* --- the limit run's sessions (servo_limrun.h) ------------------------------- */

/*
 * A limit run lowers LEFT ANGLE and RIGHT ANGLE through the model the
 * overlay holds, a session at a time: CONNECT with its read, one write,
 * LEAVE.  Each call starts the step and kst_panel_run_poll() says how it
 * ended.  The run's start, held for 2s, is the operator's consent: no
 * warning page is shown and the soft rules a value breaks are confirmed.
 * The rest is the model's: the hard rules, the file the first write of a
 * session saves (none for a later session of the run: kst_prog_run_hold()),
 * the journal, the read back of every write.
 */

typedef enum {
    KST_PANEL_RUN_NONE = 0,  /**< no step asked, or its end taken          */
    KST_PANEL_RUN_BUSY,
    KST_PANEL_RUN_OK,        /**< once: the step ended as asked           */
    KST_PANEL_RUN_FAILED,    /**< once: it did not                         */
} kst_panel_run_t;

/** The programmer's port is in PWM with no operation under way: a run's
 *  CONNECT can start. */
bool kst_panel_run_ready(void);
/** A run begins: no write of it yet. */
void kst_panel_run_begin(void);
/** CONNECT on the SERVO screen's port.  With @p supply, a PD mini answers
 *  and the session asks it for @p mv and @p ma: its set point as it
 *  stands, so the ENTER changes nothing.  KST_PROG_MSG_NONE when taken. */
kst_prog_msg_id_t kst_panel_run_connect(bool supply, uint16_t mv,
                                        uint16_t ma);
/** Write LEFT ANGLE @p left and RIGHT ANGLE @p right and nothing else.
 *  The run's first write holds the servo as read (kst_prog_run_hold()). */
kst_prog_msg_id_t kst_panel_run_write(uint16_t left, uint16_t right);
/**
 * @brief Write PULSE LOWER and PULSE UPPER and nothing else; as
 *        kst_panel_run_write() otherwise.
 * @param lower_dus PULSE LOWER, 0.1us
 * @param upper_dus PULSE UPPER, 0.1us
 * @return as kst_panel_run_write()
 */
kst_prog_msg_id_t kst_panel_run_pulses(uint16_t lower_dus, uint16_t upper_dus);
/** LEAVE. */
kst_prog_msg_id_t kst_panel_run_leave(void);
kst_panel_run_t kst_panel_run_poll(void);
/**
 * @brief Whether the CONNECT of kst_panel_run_connect() ended with no servo
 *        answering.
 *
 * True when the programmer's last message is KST_SES_ERR_NO_SERVO and was
 * said after that call.  A message left from before it does not count.
 *
 * @return True for a CONNECT of a servo that is no KST or Chaservo, or not
 *         powered; false otherwise.
 */
bool kst_panel_run_no_servo(void);
/**
 * @brief The servo as read.
 * @param angle out: its two angles; filled also when false is returned,
 *        where a servo was read
 * @param lo out: the value both angles take by the hard rules, at least
 * @param hi out: and at most
 * @param reversion out: REVERSION
 * @param type out: its type number
 * @param pulse_dus out: NEUTRAL, PULSE LOWER and PULSE UPPER in 0.1us, 0
 *        for one not shown
 * @return false with none read, or one that is not editable (no KST or
 *         Chaservo servo, in doubt)
 */
bool kst_panel_run_servo(uint16_t angle[2], uint16_t *lo, uint16_t *hi,
                         bool *reversion, unsigned *type,
                         uint16_t pulse_dus[3]);
/** The file that holds the servo as the run found it; "" for none. */
const char *kst_panel_run_backup(void);
/** The run is over: the programming voltage and current go back to the
 *  operator's.  The servo as the run found it stays held for RESTORE. */
void kst_panel_run_end(void);

/* --- for the tests ------------------------------------------------------------ */

kst_prog_t *kst_panel_model(void);

/**
 * A message as the overlay writes it, in the language in force.  Returns
 * the lines used, 0 for no message.
 */
int kst_panel_message_lines(const kst_prog_msg_t *m,
                            char out[KST_PANEL_MSG_LINES][KST_PANEL_LINE]);

/**
 * @brief The rows of 10 x 20 cells a line of a message, a note or a
 *        warning page takes, wrapped at a space to the overlay's width less
 *        10px a side, one cell a row at least (before kst_panel_init(),
 *        say): 60 cells a row on SERVO's left card of 620px.
 * @param s the line
 * @return its rows, 0 for an empty line
 */
int kst_panel_text_rows(const char *s);

/** The line a rule's refusal or warning shows. */
const char *kst_panel_rule_text(unsigned rule);

/*
 * Where things are, for a test's taps.  Rows by page, row and column.  A
 * coordinate no row has gives an empty rectangle (value, step), false (live)
 * or NULL (button, label).  Every control is UI_TAP_MIN (48px) tall or
 * more and a control at the foot ends 4px above the alert band.
 */
/**
 * @brief A tab, one fifth of the row left of CLOSE, 48px tall.
 * @param page the tab, PG_SERVO to PG_LOCKED; another is the first
 * @return its rectangle
 */
gfx_rect_t kst_panel_tab_rect(int page);
/** @brief CLOSE, 120 x 48px at the top right.
 *  @return its rectangle */
gfx_rect_t kst_panel_close_rect(void);
/**
 * @brief A row's value: 150px, a wide row's 260px less its keys.
 * @param page the tab
 * @param row the row, from the top
 * @param col the column, 0 or 1
 * @return its rectangle; empty for no such row
 */
gfx_rect_t kst_panel_value_rect(int page, int row, int col);
/**
 * @brief A stepped row's minus or plus key, 56 x 48px.
 * @param page the tab
 * @param row the row, from the top; column 0
 * @param up the plus key; else the minus key
 * @return its rectangle; empty for no such row
 */
gfx_rect_t kst_panel_step_rect(int page, int row, bool up);
/**
 * @brief A list's entry, 48px tall at a 56px pitch under the title.
 * @param i the entry, from 0
 * @return its rectangle
 */
gfx_rect_t kst_panel_choice_rect(int i);
/** @brief A list's CANCEL, 150 x 48px at the foot's right.
 *  @return its rectangle */
gfx_rect_t kst_panel_choice_cancel_rect(void);
/** @brief A warning page's hold button, 300 x 56px at the foot's left.
 *  @return its rectangle */
gfx_rect_t kst_panel_apply_rect(void);
/** @brief A warning page's CANCEL, 180 x 56px at the foot's right.
 *  @return its rectangle */
gfx_rect_t kst_panel_cancel_rect(void);
ui_keypad_t *kst_panel_keypad(void);
ui_textkey_t *kst_panel_textkey(void);

/** What is open over the page: 0 none, 1 keypad, 2 choice list, 3 warning,
 *  4 the keyboard for a file's name. */
int kst_panel_editor(void);

/** The tab shown: 0 SERVO, 1 TUNE, 2 PULSE, 3 FAILSAFE, 4 LOCKED. */
int kst_panel_page(void);

/** Line @p i of the open warning page, entry @p i of the open list; NULL
 *  past the last and while none is open. */
const char *kst_panel_warning_line(int i);
/** The level line @p i of the open warning page is drawn in: a
 *  kst_prog_level_t; KST_PROG_INFO past the last. */
int kst_panel_warning_level(int i);
/** Line @p i, 0 or 1, of what stands on the SERVO tab until it is dealt
 *  with; NULL where there is none. */
const char *kst_panel_standing_line(int i);
/** The control of the row at @p page, @p row, @p col takes a tap now. */
bool kst_panel_row_live(int page, int row, int col);
/** The SERVO tab's line @p i under its rows: 0 the port's state, 1 what
 *  explains it; NULL where the tab draws none. */
const char *kst_panel_status_line(int i);
/** The text on the button of the row at @p page, @p row, @p col. */
const char *kst_panel_button(int page, int row, int col);
/** The label of the row at @p page, @p row, @p col as it is drawn now. */
const char *kst_panel_label(int page, int row, int col);
const char *kst_panel_choice_label(int i);
/** The open list's title; NULL while none is open. */
const char *kst_panel_choice_title(void);

/**
 * @brief The page each of @p count notes takes in @p room rows.
 *
 * Every note is on page 0 when they fit, else on pages that keep their
 * last row for the page count, a note never split.  A note marked in
 * @p standing is on page 0, and the others fill the rows left after the
 * standing ones.  With no row left beside the standing notes for the
 * count, they yield: every note pages in pages of @p room - 1 rows, the
 * standing ones first, and a note taller than that gets page -1, shown on
 * no page until the room grows.
 * @param rows each note's rows
 * @param standing whether each note stands
 * @param count the notes
 * @param room the rows they have
 * @param page out: each note's page, -1 for none
 * @return the number of pages
 */
int kst_panel_pack_notes(const int *rows, const bool *standing, int count,
                         int room, int *page);

/** The pages the open tab's notes take: 1 when they fit above the message;
 *  a tap on the notes shows the next. */
int kst_panel_note_pages(void);

/** The notes' area, a tap on which shows the next page; empty while the
 *  notes take one page. */
gfx_rect_t kst_panel_notes_rect(void);

/** The notes' page shown, from 0. */
int kst_panel_note_page(void);

/** The @p i-th note on the page shown, from the top; NULL past the last. */
const char *kst_panel_shown_note(int i);

/** The lines of the last message drawn under the notes. */
int kst_panel_shown_message_lines(void);

/** The rows the notes and the message of the page shown are drawn in,
 *  across the area; empty for none. */
gfx_rect_t kst_panel_notes_drawn(void);

#ifdef __cplusplus
}
#endif
