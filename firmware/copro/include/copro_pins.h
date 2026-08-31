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
 * The safety heartbeat from the panel's J8, through the retriggerable
 * monostable.  An input here: the coprocessor gates its own outputs from it
 * and checks the period in firmware as well, because noise can fake a
 * heartbeat and the monostable is the crude backstop rather than the whole
 * mechanism.
 */
#define COPRO_HEARTBEAT_PIN  3

/*
 * The CAN controller: a Waveshare RP2350-CAN, carrying an XL2515
 * (MCP2515-compatible) behind a SIT65HVD230 transceiver.
 *
 * Read off the vendor's own driver rather than guessed, and they do not
 * collide with the link UART on 0-2 or the safety line on 3.
 *
 * THE CRYSTAL IS 16 MHz, which is the number the whole bandwidth budget rests
 * on -- the part halves its crystal before the prescaler and a bit needs eight
 * quanta, so 8 MHz would cap the bus at 500 kbit/s and leave the link short at
 * the top of its traffic range.  It was not taken on trust: the vendor's
 * driver carries CNF triples for ten standard rates, and decoding those back
 * into divisor and quanta gives the advertised rate at 16 MHz and at no other
 * crystal.  test_can_timing pins it.
 */
#define COPRO_CAN_SPI        spi1
#define COPRO_CAN_PIN_SCK    10
#define COPRO_CAN_PIN_MOSI   11
#define COPRO_CAN_PIN_MISO   12
#define COPRO_CAN_PIN_CS     9
#define COPRO_CAN_PIN_INT    8   /**< active low, wants a pull-up */
#define COPRO_CAN_CRYSTAL_HZ 16000000u
/* The vendor's driver clocks the bus at 10 MHz; the part is rated to 10. */
#define COPRO_CAN_SPI_HZ     10000000u

/*
 * The bit rate both ends must agree on.  1 Mbit/s is what the 16 MHz crystal
 * reaches exactly; see docs/Link.md for what the budget looks like at it, and
 * test_can_timing for why nothing slower is forced on us.
 */
#define COPRO_CAN_BITRATE    1000000u

#endif /* RCBENCH_COPRO_PINS_H */
