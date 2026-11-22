/**
 * @file
 * @brief IOmcu pin map for the module build: a Waveshare RP2350-CAN (RP2350A, 4
 * MB flash) with its XL2515 CAN (Controller Area Network) controller.
 *
 * The final board is an RP2350B; this map moves with it.
 *
 * The constraint on the map is silicon: a PIO (programmable input/output)
 * block sees 32 pins with a base of 0 or 16 only, for all four of its state
 * machines, changeable only while the block holds no program.  One block
 * cannot serve a pin below 16 and one above 31 at the same time, so the map
 * is partitioned by block before a schematic is drawn.
 *
 * Only the CAN controller and the safety line are assigned here.  On the
 * module wired by hand every output pin arrives from the panel over the
 * OUTPUTS page instead, because which lead is
 * on which pin is a property of the bench in front of the operator rather
 * than of the firmware.  What is fixed here is the other half of that: the
 * pins an output may never be given.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_IOMCU_PINS_H
#define RCBENCH_IOMCU_PINS_H

#include "out_bind.h"

/**
 * @brief Which board this image is built for.
 *
 * It is a build-time fact, not a runtime one: the pin map is compiled in,
 * so an image knows what it is and says so on the identity page.  The panel
 * reads it and shows that board's pins and no other.  CMake's IOMCU_BOARD
 * chooses it: rp2350_can (the default) builds the module wired by hand,
 * rbmod the carrier, whose role pins are in shared/outputs/out_board_rbmod.c
 * (IOMCU_BOARD_RBMOD=1).  Both boards hold the same module, so the CAN and
 * heartbeat pins below are the same on both.  rsn builds the remote servo
 * node's Core2350B (IOMCU_BOARD_RSN=1, shared/outputs/out_board_rsn.c and
 * rsn_pins.h): its heartbeat is GPIO 3 as well, and it has no CAN
 * controller.  A further board appends an entry to k_boards in
 * shared/outputs/out_bind.c and a value here.
 */
#if defined(IOMCU_BOARD_RSN) && IOMCU_BOARD_RSN
#define IOMCU_BOARD_ID  ((uint16_t)OUTBIND_BOARD_RSN)
#elif defined(IOMCU_BOARD_RBMOD) && IOMCU_BOARD_RBMOD
#define IOMCU_BOARD_ID  ((uint16_t)OUTBIND_BOARD_RBMOD)
#else
#define IOMCU_BOARD_ID  ((uint16_t)OUTBIND_BOARD_PICO_HEADER)
#endif

/**
 * @brief Whether the board carries the XL2515 CAN controller: 1 on the
 *        RP2350-CAN module and the rbmod carrier, 0 on the remote servo
 *        node, whose GPIO 8 to 12 are port switch enables.
 *
 * Without it can_start() leaves the controller's pins alone and the link
 * has no transport in this image.
 */
#if defined(IOMCU_BOARD_RSN) && IOMCU_BOARD_RSN
#define IOMCU_HAS_CAN 0
#else
#define IOMCU_HAS_CAN 1
#endif

/**
 * @brief The safety heartbeat from the panel's PANEL_HEARTBEAT_PIN, by a
 * direct wire: no board carries the retriggerable monostable
 * (docs/Safety.md).
 *
 * An input here: the IOmcu gates its own outputs from it and checks the
 * period in firmware, because noise can fake a heartbeat.
 */
#define IOMCU_HEARTBEAT_PIN  3

/**
 * @brief The CAN controller: the module's XL2515 (MCP2515-compatible) behind
 * a SIT65HVD230 transceiver, on the pins the vendor's driver uses.
 *
 * They do not collide with the safety line on GP3.
 *
 * The crystal is 16MHz, and the bandwidth budget rests on it: the part
 * halves its crystal before the prescaler and a bit needs 8 quanta, so an
 * 8MHz crystal caps the bus at 500kbit/s.  The vendor's driver carries CNF
 * triples for ten standard rates, and each decodes to its advertised rate at
 * 16MHz and at no other crystal; test_can_timing pins it.
 */
#define IOMCU_CAN_SPI        spi1
#define IOMCU_CAN_PIN_SCK    10 /**< SPI1 clock, GPIO 10 */
#define IOMCU_CAN_PIN_MOSI   11 /**< SPI1 data out (MOSI), GPIO 11 */
#define IOMCU_CAN_PIN_MISO   12 /**< SPI1 data in (MISO), GPIO 12 */
#define IOMCU_CAN_PIN_CS     9 /**< chip select, GPIO 9 */
#define IOMCU_CAN_PIN_INT    8   /**< active low, wants a pull-up */
#define IOMCU_CAN_CRYSTAL_HZ 16000000u /**< the XL2515's crystal, Hz */
/**
 * @brief The vendor's driver clocks SPI (Serial Peripheral Interface) at 10
 * MHz; the part is rated to 10MHz.
 */
#define IOMCU_CAN_SPI_HZ     10000000u

/**
 * @brief The bit rate both ends must agree on.
 *
 * 1Mbit/s is what the 16MHz crystal
 * reaches exactly; docs/Link.md has the bus budget at that rate.
 */
#define IOMCU_CAN_BITRATE    1000000u

/**
 * @brief The pins no output may be bound to, one bit per pin, handed to the
 * output bank at boot.
 *
 * The pin in an OUTPUTS-page slot is whatever an operator typed into the
 * panel's settings, so something has to hold the list of pins that are
 * already spoken for.  Getting this wrong is quiet: an output on GP3 drives
 * the safety line from inside, and the interlock stops meaning anything with
 * nothing on any screen to say so.
 */
#if IOMCU_HAS_CAN
#define IOMCU_RESERVED_PINS                     \
    ((1ull << IOMCU_HEARTBEAT_PIN)              \
     | (1ull << IOMCU_CAN_PIN_SCK)              \
     | (1ull << IOMCU_CAN_PIN_MOSI)             \
     | (1ull << IOMCU_CAN_PIN_MISO)             \
     | (1ull << IOMCU_CAN_PIN_CS)               \
     | (1ull << IOMCU_CAN_PIN_INT))
#else
/* The remote servo node: the heartbeat, the UART to the ES3C28P, the rail
 * and port switch enables, the strap, the critical alert, the LED and the
 * PSRAM's chip select (rsn_pins.h). */
#define IOMCU_RESERVED_PINS                                         \
    ((1ull << IOMCU_HEARTBEAT_PIN) | (1ull << 0) | (1ull << 1)      \
     | (1ull << 2) | (0x1FFull << 6) | (1ull << 15) | (1ull << 36)  \
     | (1ull << 39) | (1ull << 47))
#endif

#endif /* RCBENCH_IOMCU_PINS_H */
