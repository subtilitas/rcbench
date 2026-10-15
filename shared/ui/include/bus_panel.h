/*
 * The BUS page of the SERVO screen: serial bus servos on the programming
 * port.  A page of the screen's settings overlay, beside OUTPUT, TEST,
 * LIMITS and DUT.
 *
 * The page draws the BUS link page's readout (bus_link.h) and turns
 * touches into its commands, one at a time.  The protocols come from the
 * table of bus_proto.h; the page holds no protocol's numbers.  Its
 * controls are the screen's: rows with a value button, the number keypad,
 * the choice list, a slider, and a warning page whose button is held
 * UI_HOLD_S.
 *
 * The rows, two columns:
 *
 *   PROTOCOL  | BAUD          the line; BAUD while open moves the line only
 *   SERIES    | BUS           SCS or STS/SMS per servo; OPEN or CLOSE
 *   SCAN FROM | SCAN TO       the IDs a scan pings, 1 to 20 at start
 *   FOUND n   | ID            SCAN, STOP while it runs; the servo driven
 *   TORQUE    | SPEED         torque on or off; the goal's speed
 *   ACC       | EEPROM        the goal's acceleration (STS); EEPROM writes
 *   POSITION  (slider)        the goal position, written as it moves
 *
 * Under the rows four lines: the pin the bus drives and its state, the
 * watched servo's readings or the scan, its error flags, and what the last
 * command came to.
 *
 * The bus is open only while the page is shown.  A page that is not shown
 * closes it: torque off, the lock flag set, the pin back to PWM (pulse-width
 * modulation).  An OPEN is not sent while the bench is armed or asked to
 * arm, and the bench takes no arm while the bus holds the port.
 *
 * EEPROM writes (ID, baud, angle limits, mode, lock flag) go through the
 * warning page and its 2 s hold, with the old value read first.  Each one
 * written is kept, up to BUS_PANEL_BACKUPS, until a restart, and RESTORE
 * writes them back, newest first, behind its own hold.  RAM registers
 * (torque, goal, speed, acceleration) are written without a hold.
 *
 * STOP ends a command not yet sent and any restore; the control task sends
 * the page its STOP (bus_link_stop()).
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "bus_link.h"
#include "gfx.h"
#include "touch_types.h"
#include "ui_keypad.h"

#ifdef __cplusplus
extern "C" {
#endif

/** EEPROM writes kept for RESTORE. */
#define BUS_PANEL_BACKUPS 8u
/** The scan's range at start. */
#define BUS_PANEL_FROM 1u
#define BUS_PANEL_TO   20u
/** A command the control task has not answered in this long is given up. */
#define BUS_PANEL_ANSWER_MS 3000u
/** Lines a message or a status line takes at most, in bytes. */
#define BUS_PANEL_LINE 96
/** Entries of a choice list at most. */
#define BUS_PANEL_CHOICES 10

/** What the control task hands the page every frame. */
typedef struct {
    bus_link_readout_t rd;
    uint32_t now_ms;
    bool     armed;            /**< the bench is armed or asked to arm  */
    uint32_t stops;            /**< STOPs latched, counting             */
    uint8_t  port;             /**< the programming port the bus takes  */
    uint8_t  pin;              /**< its pin by the PROG page, 0xFF none */
} bus_panel_feed_t;

/** Start over: closed, nothing kept.  @p area is the overlay's. */
void bus_panel_init(gfx_rect_t area);

/** Whether the page is on the glass.  Not shown, it closes the bus. */
void bus_panel_show(bool shown);
bool bus_panel_is_shown(void);

/** The bus holds the port by the last read, or an OPEN is on its way. */
bool bus_panel_holds_port(void);

/** Touch events were lost: the press and its hold are dropped. */
void bus_panel_cancel(void);
/** The page goes: presses and editors with it. */
void bus_panel_leave(void);

void bus_panel_tick(float dt_s);
bool bus_panel_has_press(uint8_t id);
/** A press below the overlay's tabs, or anywhere while an editor is open,
 *  or the rest of one it owns. */
void bus_panel_event(const touch_event_t *evt);
/** The page under the tabs, or the open editor over the whole area. */
void bus_panel_render(gfx_canvas_t *c);
/** Changes whenever a render would show something else. */
uint32_t bus_panel_revision(void);
/** Changes while a warning's button fills; bus_panel_apply_rect(). */
uint32_t bus_panel_hold_revision(void);

/** What is open over the page: 0 none, 1 keypad, 2 choice list,
 *  3 warning. */
int bus_panel_editor(void);

/* --- the control task's side --------------------------------------------- */

void bus_panel_feed(const bus_panel_feed_t *f);
/** The next command; false for none. */
bool bus_panel_take(bus_link_cmd_t *out);

/* --- for the tests ------------------------------------------------------- */

gfx_rect_t bus_panel_value_rect(int row, int col);
gfx_rect_t bus_panel_slider_rect(void);
gfx_rect_t bus_panel_choice_rect(int i);
gfx_rect_t bus_panel_choice_cancel_rect(void);
gfx_rect_t bus_panel_apply_rect(void);
gfx_rect_t bus_panel_cancel_rect(void);
ui_keypad_t *bus_panel_keypad(void);
/** Line @p i, 0 to 3, under the rows. */
const char *bus_panel_line(int i);
/** The text on the button of the row at @p row, @p col. */
const char *bus_panel_button(int row, int col);
/** The label of the row at @p row, @p col as it is drawn now. */
const char *bus_panel_label(int row, int col);
/** The control of the row at @p row, @p col takes a tap now. */
bool bus_panel_row_live(int row, int col);
const char *bus_panel_choice_label(int i);
const char *bus_panel_choice_title(void);
/** Line @p i of the open warning page; NULL past the last. */
const char *bus_panel_warning_line(int i);
unsigned bus_panel_backups(void);
/** The selected ID, -1 for none. */
int bus_panel_id(void);
/** The series of the selected ID. */
uint8_t bus_panel_series(void);

#ifdef __cplusplus
}
#endif
