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
 * cell, a data line; OU_FONT_HEAD (20 x 36) for a pin's GP name and the
 * last write.  The protocol list's face and rows pick their own face
 * (ui_choice_font()).
 */
#define OU_FONT_LABEL (&gfx_font_10x20)
#define OU_FONT_FINE  (&gfx_font_8x16)   /**< 8 x 16: a cell's sub-line. */
#define OU_FONT_HEAD  (&gfx_font_20x36)  /**< 20 x 36: GP names, result. */
#define OU_CELL       10 /**< OU_FONT_LABEL's cell width, px. */
#define OU_LINE       22 /**< The pitch of a two-line reason, px. */

/**
 * @brief The lowest row the screen draws or takes a press on, in the
 *        screen's coordinates: 4 px above the alert band, body y 492.
 */
#define OU_FOOT (UI_ALERT_Y - UI_BAND_H - 4)

/**
 * @brief The left column: the protocol and what the choice adds up to.
 *
 * 300 px from x 16, so 30 cells of OU_FONT_LABEL: the longest reason
 * breaks into two lines of 30 cells at most in every language
 * (test_outputs_screen_draw).  The protocol list's face is 300 x 64 px
 * under the PROTOCOL label; the longest protocol name, "DSHOT600 BIDIR",
 * draws in the 16 x 28 face there (224 px of the 245 px left of the
 * chevron), every other name in 20 x 36.
 */
#define COL_X    16
#define COL_W    300   /**< The column's width, px. */
#define COL_Y    16    /**< The PROTOCOL label's top. */

#define DD_Y     44    /**< The protocol list's face: its top. */
#define DD_H     64    /**< Its height, px (9.6 mm). */

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
