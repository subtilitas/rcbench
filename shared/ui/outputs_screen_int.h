/**
 * @file outputs_screen_int.h
 * @brief What the OUTPUTS screen (outputs_screen.c) and its PROG PORT entry
 *        (outputs_port_entry.c, outputs_port.c) share: the left column's
 *        layout, the faces they draw in and the entry's hooks.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx.h"
#include "touch_types.h"
#include "ui_layout.h"

/**
 * @brief The faces of the 1024 x 600 type scale OUTPUTS draws in.
 *
 * OU_FONT_LABEL (10 x 20) for every label, count, reason and the PIN
 * button; OU_FONT_FINE (8 x 16) for the line under a pin's name in its
 * cell, a data line; OU_FONT_PIN (16 x 28) for a pin's name ("GPIO 28",
 * 112px); OU_FONT_HEAD (20 x 36) for the last write.  The protocol list's
 * face and rows pick their own face (ui_choice_font()).
 */
#define OU_FONT_LABEL (&gfx_font_10x20)
#define OU_FONT_FINE  (&gfx_font_8x16)   /**< 8 x 16: a cell's sub-line. */
#define OU_FONT_PIN   (&gfx_font_16x28)  /**< 16 x 28: a cell's pin name. */
#define OU_FONT_HEAD  (&gfx_font_20x36)  /**< 20 x 36: the result. */
#define OU_CELL       10 /**< OU_FONT_LABEL's cell width, px. */
#define OU_LINE       22 /**< The pitch of a two-line reason, px. */

/**
 * @brief The lowest row the screen draws or takes a press on, in the
 *        screen's coordinates: 4px above the alert band, body y 492.
 */
#define OU_FOOT (UI_ALERT_Y - UI_BAND_H - 4)

/**
 * @brief The left column: the protocol and what the choice adds up to.
 *
 * 300px from x 16, so 30 cells of OU_FONT_LABEL: the longest reason
 * breaks into two lines of 30 cells at most in every language
 * (test_outputs_screen_draw).  The protocol list's face is 300 x 64px
 * under the PROTOCOL label; the longest protocol name, "DSHOT600 BIDIR",
 * draws in the 16 x 28 face there (224px of the 245px left of the
 * chevron), every other name in 20 x 36.
 */
#define COL_X    16
#define COL_W    300   /**< The column's width, px. */
#define COL_Y    16    /**< The PROTOCOL label's top. */

#define DD_Y     44    /**< The protocol list's face: its top. */
#define DD_H     64    /**< Its height, px (9.6mm). */

/**
 * @brief The board view's PROG PORT entry (picker_draw.c): the FUNCTION face
 *        under the list's face, and the rows in the column left of the board.
 *
 * The column is the PINS table's, 171px from x 8, so it ends 8px left of any
 * key a board can place (picker_screen_int.h holds that).  The face is 196 x
 * 48px (UI_TAP_MIN) right of its FUNCTION label, ending where the list's face
 * ends; the FUNCTION list opens under them, as wide as both, over the column.
 * The reason has 100px down to the foot: four
 * lines of OU_FONT_LABEL 22px apart, or five of OU_FONT_FINE 18px apart.
 */
#define OB_COL_X     8
#define OB_COL_W     171  /**< The column's width, px: 17 cells of the label. */
#define OB_FN_Y      68   /**< The FUNCTION face's top. */
#define OB_FN_X      92   /**< Its left edge: 8 cells of label from x 8. */
#define OB_FN_W      196  /**< Its width, px. */
#define OB_LIST_W    280  /**< The FUNCTION list's width, px. */
#define OB_PIN_Y     122  /**< PIN GPIO n's first line, 12 x 24. */
#define OB_PIN_PITCH 26   /**< Between its two lines, px. */
#define OB_WATCH_Y   174  /**< WATCHED BY's first of three lines. */
#define OB_TESTER_Y  242  /**< SERVO TESTER's label. */
#define OB_PWM_Y     316  /**< PORT PWM's label. */
#define OB_WHY_Y     392  /**< The reason's first line. */
#define OB_WHY_FINE  18   /**< Its pitch in OU_FONT_FINE, px. */

_Static_assert(OB_FN_Y + UI_TAP_MIN + 6 <= OB_PIN_Y,
               "PIN GPIO n starts under the FUNCTION face");
_Static_assert(OB_WATCH_Y + 3 * OU_LINE <= OB_TESTER_Y,
               "WATCHED BY's three lines end above SERVO TESTER");
_Static_assert(OB_TESTER_Y + 22 + UI_TAP_MIN + 4 <= OB_PWM_Y,
               "SERVO TESTER's segments end above PORT PWM");
_Static_assert(OB_PWM_Y + 22 + UI_TAP_MIN + 4 <= OB_WHY_Y,
               "PORT PWM's segments end above the reason");
_Static_assert(OB_WHY_Y + 3 * OU_LINE + 20 <= OU_FOOT
                   && OB_WHY_Y + 4 * OB_WHY_FINE + 16 <= OU_FOOT,
               "the reason's last line ends above the alert band");
_Static_assert(OB_FN_Y + UI_TAP_MIN + 4 + 5 * UI_TAP_MIN + 8 + 4 <= OU_FOOT,
               "the FUNCTION list and its shadow end above the alert band");

/**
 * @brief The board view's UNBIND ALL PINS: under the IN USE table, 168 x
 *        80px from x 848, standing on the foot; up to four lines of
 *        OU_FONT_FINE.  The table ends 8px over it while it is offered.
 */
#define OB_CLR_X     848
#define OB_CLR_W     168  /**< Its width, px. */
#define OB_CLR_H     80   /**< Its height, px. */
#define OB_CLR_Y     (OU_FOOT - OB_CLR_H) /**< Its top. */

/**
 * @brief Which view of OUTPUTS is drawn (outputs_view.c): the board, or the
 *        grid.  The PROG PORT entry's rows and UNBIND ALL PINS move with it;
 *        a press on them under way is dropped.
 * @param board true for the board view
 */
void outputs_screen_set_board_view(bool board);

/**
 * @brief Counts every change of what the grid view and the entry draw; the
 *        board view repaints on a change of it.
 * @return the count
 */
uint32_t outputs_screen_revision(void);

/**
 * @brief Whether UNBIND ALL PINS is offered: the pages read describe no
 *        binding and a protocol, not a PROG PORT entry, is shown.
 * @return true when it is drawn and takes a hold
 */
bool outputs_unbind_offered(void);

/**
 * @brief The key's rectangle in the view drawn.
 * @return the rectangle
 */
gfx_rect_t outputs_unbind_rect(void);

/**
 * @brief A touch event for the key: a press on it begins the hold, a slide
 *        off it or a release ends the hold.
 * @param evt the event
 * @return true when the event was the key's
 */
bool outputs_unbind_event(const touch_event_t *evt);

/**
 * @brief Draws the key, filling towards danger red as it is held; nothing
 *        while it is not offered.
 * @param c canvas drawn on
 */
void outputs_unbind_draw(gfx_canvas_t *c);

/**
 * @brief The word for the last write.
 * @return the text in the language in force
 */
const char *outputs_result_text(void);

/**
 * @brief The colour of that word.
 * @return the colour
 */
gfx_color_t outputs_result_color(void);

/* The entry's state laid out and dropped, as the screen's reset() does:
 * no entry shown. */
void outputs_port_reset(void);

/* No entry shown: a protocol was picked.  A segment's press goes too. */
void outputs_port_hide(void);

/* A segment's press under way is dropped: touch events went missing, or
 * the screen is left. */
void outputs_port_cancel(void);

/* A touch event while the protocol list is shut: whether a segment of
 * the entry shown took it. */
bool outputs_port_event(const touch_event_t *evt);

/* Once a frame: what the settings and the SERVO screen's locks show is
 * compared with what was drawn. */
void outputs_port_tick(void);

/* Whether the entry's FUNCTION list is open. */
bool outputs_port_fn_open(void);

/* The home tag: an open FUNCTION list shuts and the tag is taken (true);
 * with none open the screen is left (false). */
bool outputs_port_back(void);

/* Bumped by every change of what the entry draws; the screen repaints on
 * a change of it as on one of its own.  outputs_port_touched() bumps it. */
uint32_t outputs_port_revision(void);
void outputs_port_touched(void);

/* The entry's left column, under the dropdown @p dd drawn @p pressed and
 * @p open. */
void outputs_port_draw(gfx_canvas_t *c, gfx_rect_t dd, bool pressed,
                       bool open);

/**
 * @brief Where the entry's controls lie: under the grid's list, or in the
 *        board view's column.  A press under way is dropped.
 * @param board true for the board view
 */
void outputs_port_set_view(bool board);

/**
 * @brief The entry's rows in the board view: FUNCTION, PIN and its pad,
 *        WATCHED BY, SERVO TESTER, PORT PWM and @p why under them.  No PIN
 *        button: the board's keys are the pin control.
 * @param c canvas drawn on
 * @param why the amber line, or NULL or "" for none
 */
void outputs_port_draw_board(gfx_canvas_t *c, const char *why);

/**
 * @brief The open FUNCTION list, over whatever the view drew under it;
 *        nothing while it is shut.
 * @param c canvas drawn on
 */
void outputs_port_draw_list(gfx_canvas_t *c);

/* Entry @p port's name in the list and on the dropdown: PROG PORT, or
 * PROG PORT n where the list holds several. */
void outputs_port_name(uint8_t port, char *buf, size_t cap);

/* Each function's pins now (outputs_iface_pins()), for the screens'
 * repaint: a row stepped on SETUP, or a page read, moves a pin or its
 * owner without a touch on the screen. */
void outputs_iface_read(uint64_t pins[OUTPUTS_IFACE_COUNT]);

/* Whether a programming port the list shows holds GPIO @p gpio. */
bool outputs_port_holds(uint8_t gpio);

/* The entry the screen opens on: what outputs_screen_open_on() asked for,
 * once, or the overview; selected. */
void outputs_port_open(void);

/* What outputs_screen_open_on() asked for is dropped, as at a reset. */
void outputs_port_forget_open(void);

/* Every programming port's pin, LINK_PG_PORTS_MAX of them, OUTPUTS_NO_PIN
 * past the entries: what outbind_pwm_clash_ports() checks a pin beside. */
/* The ports' pins that hold a PWM slice, for the slice and compare
 * rules: @p pins (LINK_PG_PORTS_MAX of them) with a NONE port's pin taken
 * out, as the IOmcu's prog_port_slots_check() does.  A NONE port's pin
 * stays taken as a pin.  One static copy, UI task only. */
const uint8_t *outputs_pwm_pins(const uint8_t *pins);
const uint8_t *outputs_port_pins(void);
