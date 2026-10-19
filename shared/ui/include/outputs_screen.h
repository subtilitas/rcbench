/*
 * The outputs screen: choose a protocol, then tick the pins it drives.
 *
 * A visit from SETUP opens on the overview: OFF selected, every bound pin
 * named by its protocol, and a tap on a pin changes nothing until a
 * protocol is picked.  Selecting OFF in the list goes back to the overview.
 * The pin picker opens on it too.  A visit from a workbench's TARGET row
 * opens on what that workbench drives (outputs_screen_open_on()).
 *
 * The list ends in one PROG PORT entry per programming port the IOmcu
 * reports (outputs_screen_set_prog_ports()).  A port is an output of its
 * own kind, no slot and no protocol: its entry shows the port's pin lit and
 * read-only, what the port carries, and for port 1 the two settings that
 * make it the SERVO tester's output -- SERVO TESTER (SET_SERVO_TARGET) and
 * PORT PWM (SET_SERVO_PORT_PWM), each a segment, locked under the SERVO
 * screen's rules.  Neither writes a page from here: the control task takes
 * them to the IOmcu on a bench at rest.
 *
 * One protocol and a set of pins rather than eight independent slots, because
 * that is the shape of the job in front of the operator -- four servo leads,
 * or one ESC (electronic speed controller).  out_bind.c turns the choice into
 * the OUTPUTS and CHAN_CFG pages; this file is only the touching.
 *
 * The screen writes nothing to the wire.  It calls the apply function it was
 * given whenever the binding changes, and the application does the writing,
 * so the whole screen renders and is driven on the host with no link at all.
 * Which protocol is selected is this screen's own and is not a change: a
 * pick in the list calls nothing.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "bind_link.h"
#include "out_bind.h"
#include "ui_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

const ui_screen_t *outputs_screen(void);

/** Repaint the cached chrome in every framebuffer. */
void outputs_screen_invalidate(void);

/** The binding shown: the last one read, plus edits not yet read back. */
const outbind_t *outputs_screen_binding(void);

/**
 * A binding read from the IOmcu, or one the screen is posed with.
 * The screen then holds a confirmed binding and takes edits.  A binding for
 * another board than the one shown selects OFF, whatever was selected,
 * unless @p b names a protocol its pins do not.
 */
void outputs_screen_set_binding(const outbind_t *b);

/**
 * What a read of the two output pages gave (bind_link_read()).
 *
 * BIND_READ_OK is outputs_screen_set_binding() of the binding read.
 * BIND_READ_NONE and BIND_READ_ODD leave the screen on the last binding a
 * read confirmed, with any edit made since taken back, drawn dimmed with
 * the reason under the protocol; no pin can be ticked or unticked until a
 * read is BIND_READ_OK again.  A reading for another board than the one
 * shown has no last binding to keep, shows that board with nothing bound,
 * and selects OFF.  BIND_READ_NO_BOARD shows no pins and selects OFF.
 */
void outputs_screen_set_reading(const bind_reading_t *r);

/** The state of the last reading; BIND_READ_OK after a set_binding(). */
bind_read_t outputs_screen_read_state(void);

/** Whether a tap on a pin can change the binding: the last reading was
 *  BIND_READ_OK. */
bool outputs_screen_editable(void);

/**
 * Called with the binding to write after a pin is ticked or unticked, and
 * with a binding that holds no pin when UNBIND ALL PINS is held for
 * UI_HOLD_S (2 s) in the BIND_READ_ODD state.  Never for a protocol pick.
 *
 * The screen does not know whether a link exists or whether the far end took
 * the pages; outputs_screen_set_result() is how it is told, so the operator
 * sees a refusal on the screen that caused it.
 */
typedef void (*outputs_apply_fn)(const outbind_t *b);
void outputs_screen_set_apply(outputs_apply_fn fn);

/** What became of the last write: the text is shown under the protocol. */
typedef enum {
    OUTPUTS_IDLE = 0,   /**< nothing written yet, or a write not sent     */
    OUTPUTS_OK,         /**< the IOmcu took it                      */
    OUTPUTS_NO_LINK,    /**< nothing answered                             */
    OUTPUTS_REFUSED,    /**< it answered and would not have it            */
} outputs_result_t;

void outputs_screen_set_result(outputs_result_t r);

/** What a pin's cell shows. */
typedef enum {
    OUTPUTS_CELL_NONE = 0,  /**< no such pin on this board                  */
    OUTPUTS_CELL_FREE,      /**< bound to nothing: "PAD n"                  */
    OUTPUTS_CELL_TICKED,    /**< bound in the selected protocol: "PAD n"    */
    OUTPUTS_CELL_HELD,      /**< bound in another protocol: its name; or a
                                 PWM pin the IOmcu refuses: the pin
                                 it clashes with (outputs_clash_label())   */
    OUTPUTS_CELL_RESERVED,  /**< the IOmcu's own: what holds it       */
} outputs_cell_t;

/**
 * The state of the cell of catalogue entry @p index and the line printed
 * under its GPIO name, as the screen draws them.  @p label may be NULL.
 */
outputs_cell_t outputs_screen_cell(uint8_t index, char *label, size_t cap);

/** No pin, for outputs_screen_set_prog_pin(). */
#define OUTPUTS_NO_PIN 0xFFu

/**
 * The GPIO the IOmcu's programming port is bound to, as its page reads
 * it, or OUTPUTS_NO_PIN (the start value).  While no protocol holds that pin
 * its cell reads OUTPUTS_CELL_RESERVED with the port's name and takes no
 * tick, and a PWM protocol is not offered the other pin of its slice.  It
 * stays across a reset of the screen.
 */
void outputs_screen_set_prog_pin(uint8_t gpio);

/** outputs_screen_set_prog_ports()'s count while the ports are not known
 *  (PROG_LINK_COUNT_UNKNOWN). */
#define OUTPUTS_PORTS_UNKNOWN 0xFFu

/**
 * The programming ports the IOmcu reports, @p count (OUTPUTS_PORTS_UNKNOWN
 * while not known, the start value; 0 for an IOmcu without the PROG page),
 * and their pins, @p pins[0] to @p pins[count - 1] (OUTPUTS_NO_PIN for a
 * port without one); @p pins[0] is outputs_screen_set_prog_pin()'s, and
 * stays its own while the count is not known.  The list shows one PROG
 * PORT entry a port, at most LINK_PG_PORTS_MAX; none for a count of 0; and
 * one for port 1 while the count is not known, so the tester's target can
 * be set before the IOmcu answers.  An entry shown that the count drops
 * goes back to the overview.  It stays across a reset of the screen.
 */
void outputs_screen_set_prog_ports(uint8_t count, const uint8_t *pins);

/** The PROG PORT entries the list shows: 0 to LINK_PG_PORTS_MAX. */
uint8_t outputs_screen_port_entries(void);

/** Port @p port's pin as the screen holds it, or OUTPUTS_NO_PIN. */
uint8_t outputs_screen_port_pin(uint8_t port);

/**
 * Select the PROG PORT entry of port @p port (0 for port 1), as a pick in
 * the list does: the overview's pins with the port's lit, none taking a
 * tick.  A port past outputs_screen_port_entries() is ignored.
 * outputs_screen_select() leaves it.
 */
void outputs_screen_select_port(uint8_t port);

/** The PROG PORT entry shown, 0 for port 1, or -1 for a protocol. */
int outputs_screen_port_shown(void);

/**
 * The next entry to this screen opens on protocol @p proto, or with
 * outputs_screen_open_on_port() on the PROG PORT entry of port @p port,
 * instead of the overview; once, then entries open on the overview again.
 * A protocol past the table or a port the list does not show opens the
 * overview.  The TARGET rows of MOTOR & ESC and SERVO call it before they
 * open the screen.
 */
void outputs_screen_open_on(uint8_t proto);
void outputs_screen_open_on_port(uint8_t port);

/**
 * Why the PROG PORT entry's segments take no press, as the screen draws it
 * under them, or an empty string and false when they take one or the
 * entry shown has none: the servo programmer holds the port, the bench is
 * ARMED or a test or sweep runs, or PORT PWM with OUTPUTS as the tester's
 * target.  outputs_screen_reason() returns the same while an entry is
 * shown.
 */
bool outputs_screen_port_reason(char *buf, size_t cap);

/**
 * The line under the protocol that says why no pin can be ticked, or why the
 * PWM pin last tapped cannot be (outputs_clash_reason()), until another pick
 * or tick, as the screen draws it in the language in force; an empty string
 * when there is none.  Returns whether there is one.
 */
bool outputs_screen_reason(char *buf, size_t cap);

/**
 * The part of that line the protocol's rules give, for binding @p b: OFF
 * edits nothing, the protocol has all its pins, all 8 slots are in use, or
 * the protocol needs more channels than are free.  The pin picker draws the
 * same line from its own binding.  An empty string and false when a pin can
 * still be added.
 */
bool outputs_reason_of(const outbind_t *b, char *buf, size_t cap);

/**
 * The line under a pin's GPIO name for a pin the IOmcu refuses the
 * PWM generator (outbind_pwm_clash_ports() with the @p count programming
 * port pins @p ports): "SLICE OF GP13" beside a programming port's pin,
 * "PWM CH OF GP0" beside a pin bound as PWM.  An empty string and false
 * for any other pin.  The pin picker names the holder with the same line.
 */
bool outputs_clash_label(const outbind_t *b, uint8_t index,
                         const uint8_t *ports, uint8_t count, char *buf,
                         size_t cap);

/**
 * The line under the protocol for such a pin, catalogue entry @p index:
 * "GP28: PWM SLICE OF PROG PORT GP13", "GP16: SAME PWM CHANNEL AS GP0".  An
 * empty string and false when that pin is not refused, or @p index is no
 * entry (-1).  Both screens pass the pin whose tap was refused last, and
 * show it after outputs_reason_of(): a protocol that can take no pin at all
 * says that first.  The cell names the holder before any tap.
 */
bool outputs_clash_reason(const outbind_t *b, int index,
                          const uint8_t *ports, uint8_t count, char *buf,
                          size_t cap);

/**
 * What SETUP > INTERFACES has on the IOmcu's pins, as its settings stand:
 * the sensor bus, the PD mini's UART and the phase tap.  A pin is held
 * while the IOmcu reserves it from the outputs for that function: Sensor
 * SDA and the SCL after it while the INA228, the INA3221 or the AS5600 is
 * on (sense_page_pins()); PD mini TX and RX while the PD mini is on with
 * both set and apart, as the panel writes its page ON
 * (supply_page_pins()); Tap pin while the phase tap is on
 * (tone_page_pins()).  Sensor SDA at -1 holds nothing, nor does the PD
 * mini with either pin at -1.  The pins follow the
 * settings: a row stepped on SETUP frees the old pin and holds the new.
 *
 * OUTPUTS and PICK A PIN show such a pin taken and named, as a
 * programming port's pin is, and take no tick on it; a protocol that holds
 * it already keeps it, as against a port.
 */
typedef enum {
    OUTPUTS_IFACE_NONE = 0,
    OUTPUTS_IFACE_SENSE,    /**< Sensor SDA and SCL: SENSOR BUS          */
    OUTPUTS_IFACE_PDMINI,   /**< PD mini TX and RX: PD MINI              */
    OUTPUTS_IFACE_TAP,      /**< Tap pin: PHASE TAP                      */
    OUTPUTS_IFACE_COUNT
} outputs_iface_t;

/** The pins function @p which holds now, bit n GPIO n; 0 for none or
 *  OUTPUTS_IFACE_NONE. */
uint64_t outputs_iface_pins(outputs_iface_t which);

/** The function that holds GPIO @p gpio now, or OUTPUTS_IFACE_NONE.  Two
 *  set to one pin: the first in the order of outputs_iface_t. */
outputs_iface_t outputs_iface_at(uint8_t gpio);

/** The name a taken pin's cell and PICK A PIN's IN USE table give
 *  function @p which, in the language in force; "" for none. */
const char *outputs_iface_name(outputs_iface_t which);

/**
 * @p text in lines of @p font no wider than @p box, from its top down by
 * @p pitch, broken after the last space that fits or inside a word where
 * none does, left-aligned or with @p centre centred in the box.  At most
 * @p max_lines lines, the last taking the rest.  Returns the lines it
 * takes; a NULL @p c only counts them.  Both screens draw the line under
 * the protocol with it.
 */
int outputs_wrap(gfx_canvas_t *c, gfx_rect_t box, int pitch, const char *text,
                 int max_lines, const gfx_font_t *font, gfx_color_t ink,
                 bool centre);

/**
 * Select protocol @p proto, as a pick in the list does: the set shown and
 * the group the next tick joins.  Writes nothing and calls nothing.  OFF
 * (0) is the overview: entering this screen or the pin picker selects it,
 * and so does a change of board.  It stays selected across readings until
 * another protocol is selected.  An index past the table is ignored.  The
 * pin picker's list selects through this, so the two screens show one
 * selection.
 */
void outputs_screen_select(uint8_t proto);

#ifdef __cplusplus
}
#endif
