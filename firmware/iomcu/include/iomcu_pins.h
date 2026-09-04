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
 * Only the CAN controller and the safety line are assigned.  Every output pin
 * arrives from the panel over the OUTPUTS page instead, because which lead is
 * on which pin is a property of the bench in front of the operator rather
 * than of the firmware.  What is fixed here is the other half of that: the
 * pins an output may never be given.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_IOMCU_PINS_H
#define RCBENCH_IOMCU_PINS_H

#include "out_bind.h"

/*
 * Which board this image is built for.
 *
 * It is a build-time fact, not a runtime one: the pin map below is compiled
 * in, so an image knows what it is and says so on the identity page.  The
 * panel reads it and shows that board's pins and no other.  A second board
 * appends an entry to k_boards in shared/outputs/out_bind.c and builds with
 * its own copy of this header.
 */
#define IOMCU_BOARD_ID  ((uint16_t)OUTBIND_BOARD_PICO_HEADER)

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

/*
 * The pins no output may be bound to, one bit per pin, handed to the output
 * bank at boot.
 *
 * The pin in an OUTPUTS-page slot is whatever an operator typed into the
 * panel's settings, so something has to hold the list of pins that are
 * already spoken for.  Getting this wrong is quiet: an output on GP3 drives
 * the safety line from inside, and the interlock stops meaning anything with
 * nothing on any screen to say so.
 */
#define IOMCU_RESERVED_PINS                     \
    ((1ull << IOMCU_HEARTBEAT_PIN)              \
     | (1ull << IOMCU_CAN_PIN_SCK)              \
     | (1ull << IOMCU_CAN_PIN_MOSI)             \
     | (1ull << IOMCU_CAN_PIN_MISO)             \
     | (1ull << IOMCU_CAN_PIN_CS)               \
     | (1ull << IOMCU_CAN_PIN_INT))

/*
 * And the pin numbers this part does not have.  NUM_BANK0_GPIOS is 30 on the
 * RP2350A the bring-up module carries and 48 on the RP2350B the final board
 * needs, so the same firmware refuses a different set on each.  Without this
 * a pin above the top would be bound, drive nothing, and read back from the
 * page as though it were working.
 */
#define IOMCU_ABSENT_PINS                                       \
    ((NUM_BANK0_GPIOS >= 64) ? 0ull                             \
                             : ~((1ull << NUM_BANK0_GPIOS) - 1ull))

#endif /* RCBENCH_IOMCU_PINS_H */
