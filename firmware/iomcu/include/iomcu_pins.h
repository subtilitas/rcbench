/*
 * Coprocessor pin map for the module build: a Waveshare RP2350-CAN (RP2350A,
 * 4 MB flash) with its XL2515 CAN (Controller Area Network) controller.  The
 * final board is an RP2350B; this map moves with it.
 *
 * The constraint on the map is silicon: a PIO (programmable input/output)
 * block sees 32 pins with a base of 0 or 16 only, for all four of its state
 * machines, changeable only while the block holds no program.  One block
 * cannot serve a pin below 16 and one above 31 at the same time, so the map
 * is partitioned by block before a schematic is drawn.
 *
 * Only the CAN controller and the safety line are assigned.  Everything with
 * a deadline gets its pin when its PIO program is written.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_IOMCU_PINS_H
#define RCBENCH_IOMCU_PINS_H

/*
 * The safety heartbeat from the panel's J8, through the retriggerable
 * monostable.  An input here: the coprocessor gates its own outputs from it
 * and checks the period in firmware as well, because noise can fake a
 * heartbeat and the monostable is the backstop rather than the whole
 * mechanism.
 */
#define IOMCU_HEARTBEAT_PIN  3

/*
 * The CAN controller: the module's XL2515 (MCP2515-compatible) behind a
 * SIT65HVD230 transceiver, on the pins the vendor's driver uses.  They do
 * not collide with the safety line on GP3.
 *
 * The crystal is 16 MHz, and the bandwidth budget rests on it: the part
 * halves its crystal before the prescaler and a bit needs 8 quanta, so an
 * 8 MHz crystal caps the bus at 500 kbit/s.  The vendor's driver carries CNF
 * triples for ten standard rates, and each decodes to its advertised rate at
 * 16 MHz and at no other crystal; test_can_timing pins it.
 */
#define IOMCU_CAN_SPI        spi1
#define IOMCU_CAN_PIN_SCK    10
#define IOMCU_CAN_PIN_MOSI   11
#define IOMCU_CAN_PIN_MISO   12
#define IOMCU_CAN_PIN_CS     9
#define IOMCU_CAN_PIN_INT    8   /**< active low, wants a pull-up */
#define IOMCU_CAN_CRYSTAL_HZ 16000000u
/* The vendor's driver clocks SPI (Serial Peripheral Interface) at 10 MHz;
 * the part is rated to 10 MHz. */
#define IOMCU_CAN_SPI_HZ     10000000u

/*
 * The bit rate both ends must agree on.  1 Mbit/s is what the 16 MHz crystal
 * reaches exactly; docs/Link.md has the bus budget at that rate.
 */
#define IOMCU_CAN_BITRATE    1000000u

#endif /* RCBENCH_IOMCU_PINS_H */
