/*
 * Panel pins that are not the LCD's, read off the board schematic
 * (ESP32-S3-Touch-LCD-7B, Altium, 13 May 2025) rather than inferred from
 * documentation or a forum post.
 *
 * Only the two the link and the safety line need are here.  The rest arrive
 * with the board component.
 */
#ifndef RCBENCH_PANEL_PINS_H
#define RCBENCH_PANEL_PINS_H

/* Self-contained on purpose.  These constants are GPIO_NUM_* and UART_NUM_*,
 * so a header that named them without including their definitions would
 * compile or not depending on the order of the includes above it -- which is
 * a property of the caller rather than of this file, and exactly the sort of
 * thing that works everywhere it is first used and then breaks somewhere
 * else. */
#include "driver/gpio.h"
#include "driver/uart.h"

/*
 * The RS485 link.  U6 is an SP3485EN -- a 3.3 V transceiver, so there is no
 * level shifting to do and no 5 V anywhere near the module.
 *
 * The direction of these two is the opposite of the obvious reading, and the
 * schematic's own pin table does not settle it: that table calls GPIO15
 * "RS485_TX" and GPIO16 "RS485_RX", which is the naming of the *transceiver's*
 * data directions, not the ESP32's.  The connectivity does settle it, twice
 * over:
 *
 *   GPIO15 is on the net that reaches U6 pin 1, RO -- the receiver's *output*.
 *   An output cannot be driven by the ESP32, so GPIO15 is an input: RX.
 *
 *   GPIO16 is on the net that reaches U6 pin 4, DI -- the driver's input --
 *   and also the input of the buffer that operates the direction line.  The
 *   automatic-direction circuit only makes sense watching the line the ESP32
 *   transmits on, so GPIO16 is TX.
 *
 * Getting these the wrong way round costs an afternoon and looks like a dead
 * transceiver, so the reasoning is written down rather than the conclusion.
 */
#define PANEL_LINK_UART_NUM   UART_NUM_1
#define PANEL_LINK_PIN_TX     GPIO_NUM_16
#define PANEL_LINK_PIN_RX     GPIO_NUM_15

/*
 * No direction pin.  The board operates DE and /RE itself -- see
 * PANEL_LINK_TURNAROUND_US in shared/link/include/link_wire.h for what that
 * costs and what it forbids -- so the UART is configured as an ordinary
 * full-duplex port and never asserts RTS.
 */

/*
 * The safety heartbeat.  GPIO6 is on **J8**, a three-pin header carrying
 * 3V3, GND and GPIO6 and nothing else -- so it reaches a connector, which was
 * the open question, and it arrives with a rail and a ground beside it, which
 * is exactly what a retriggerable monostable on a small daughterboard wants.
 *
 * The schematic's pin table lists GPIO6 against no peripheral at all: it is
 * the one genuinely uncommitted fast pin on the board.
 */
#define PANEL_HEARTBEAT_PIN   GPIO_NUM_6

#endif /* RCBENCH_PANEL_PINS_H */
