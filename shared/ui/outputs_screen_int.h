/*
 * What the OUTPUTS screen (outputs_screen.c) and its PROG PORT entry
 * (outputs_port.c) share: the left column's layout and the entry's hooks.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx.h"
#include "touch_types.h"

/*
 * Left column: the protocol and what the choice adds up to.
 *
 * 266 px is the longest protocol name in the head face, 14 cells of 16 px
 * ("DSHOT600 BIDIR", 224 px), with 10 px before it and the chevron's 28 px
 * column and a 4 px gap after it.
 */
#define COL_X    16
#define COL_W    266
#define COL_Y    16

#define DD_Y     44
#define DD_H     54

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
const uint8_t *outputs_port_pins(void);
