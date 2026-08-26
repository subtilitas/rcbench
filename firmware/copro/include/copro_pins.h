/*
 * Coprocessor pin map, v0: the module build.
 *
 * Provisional and marked as such -- the RP2350B module has not arrived, and
 * the real constraint on this file is not preference but silicon.  A PIO block
 * sees 32 pins with a base of **only 0 or 16**, for all four of its state
 * machines, changeable only while the block holds no program.  So one block
 * cannot serve a pin below 16 and one above 31 at once, and the whole map has
 * to be partitioned by block before a schematic is drawn.
 *
 * Only the link and the safety line are settled here.  Everything with a
 * deadline lands when its PIO program does.
 */
#ifndef RCBENCH_COPRO_PINS_H
#define RCBENCH_COPRO_PINS_H

/*
 * The link.  UART0 on GPIO0/1, which is the default pair on every RP2350
 * module and therefore the one least likely to be wrong on whichever board
 * arrives.
 *
 * Unlike the panel, this end has an explicit direction pin: the breakout is a
 * MAX485 with DE and /RE brought out together.  That makes this the easier
 * half -- the turnaround is under firmware control rather than a property of
 * an RC circuit -- with one trap, which link_uart.c is written around.
 */
#define COPRO_LINK_UART      uart0
#define COPRO_LINK_PIN_TX    0
#define COPRO_LINK_PIN_RX    1
#define COPRO_LINK_PIN_DE    2   /**< DE and /RE, tied together on the board */

/*
 * The safety heartbeat from the panel's J8, through the retriggerable
 * monostable.  An input here: the coprocessor gates its own outputs from it
 * and checks the period in firmware as well, because noise can fake a
 * heartbeat and the monostable is the crude backstop rather than the whole
 * mechanism.
 */
#define COPRO_HEARTBEAT_PIN  3

#endif /* RCBENCH_COPRO_PINS_H */
