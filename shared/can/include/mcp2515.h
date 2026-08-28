/*
 * The MCP2515's register map and frame layout, as facts from the datasheet.
 *
 * Everything here is arithmetic or a constant -- no SPI, no pins, no delays --
 * so the awkward half of the driver is testable and the half that touches
 * hardware is thin enough to read in one go.
 *
 * The awkward half is the identifier.  A 29-bit extended identifier is spread
 * across four registers with a three-bit gap in the middle of the second,
 * because the layout was designed for 11-bit identifiers and extended ones
 * were fitted around them.  Getting it wrong does not fail loudly: the frame
 * goes out with a different identifier from the one intended, which on a bus
 * that arbitrates by identifier means the wrong thing wins.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_MCP2515_H
#define RCBENCH_MCP2515_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* SPI commands. */
#define MCP2515_CMD_RESET       0xC0u
#define MCP2515_CMD_READ        0x03u
#define MCP2515_CMD_WRITE       0x02u
#define MCP2515_CMD_RTS_TXB0    0x81u
#define MCP2515_CMD_READ_STATUS 0xA0u
#define MCP2515_CMD_RX_STATUS   0xB0u
#define MCP2515_CMD_BIT_MODIFY  0x05u

/* Registers this driver touches. */
#define MCP2515_CANSTAT   0x0Eu
#define MCP2515_CANCTRL   0x0Fu
#define MCP2515_CANINTE   0x2Bu
#define MCP2515_CANINTF   0x2Cu
#define MCP2515_EFLG      0x2Du
#define MCP2515_TEC       0x1Cu
#define MCP2515_REC       0x1Du

#define MCP2515_TXB0CTRL  0x30u
#define MCP2515_TXB0SIDH  0x31u   /**< then SIDL, EID8, EID0, DLC, D0..D7 */
#define MCP2515_RXB0CTRL  0x60u
#define MCP2515_RXB0SIDH  0x61u
#define MCP2515_RXB1CTRL  0x70u
#define MCP2515_RXB1SIDH  0x71u
#define MCP2515_RXM0SIDH  0x20u
#define MCP2515_RXF0SIDH  0x00u

/* CANCTRL request modes, in the top three bits. */
#define MCP2515_MODE_NORMAL   0x00u
#define MCP2515_MODE_SLEEP    0x20u
#define MCP2515_MODE_LOOPBACK 0x40u
#define MCP2515_MODE_LISTEN   0x60u
#define MCP2515_MODE_CONFIG   0x80u
#define MCP2515_MODE_MASK     0xE0u

/* CANINTF bits worth naming. */
#define MCP2515_INTF_RX0  0x01u
#define MCP2515_INTF_RX1  0x02u
#define MCP2515_INTF_TX0  0x04u
#define MCP2515_INTF_ERR  0x20u

/* EFLG.  The two overflow bits are the ones worth naming: they are set when a
 * frame arrives with both receive buffers still full, they are sticky, and the
 * MCU has to clear them -- so they are the record of a frame that was lost
 * without anything going wrong on the wire. */
#define MCP2515_EFLG_RX0OVR 0x40u
#define MCP2515_EFLG_RX1OVR 0x80u
#define MCP2515_EFLG_TXBO   0x20u
#define MCP2515_EFLG_OVR    (MCP2515_EFLG_RX0OVR | MCP2515_EFLG_RX1OVR)

/* RXB0CTRL / RXB1CTRL receive-mode field. */
#define MCP2515_RXM_ANY      0x60u  /**< accept everything, filters off */
#define MCP2515_RXM_EXT_ONLY 0x40u  /**< extended identifiers only */
/** RXB0CTRL only: roll a frame over to buffer 1 when buffer 0 is full. */
#define MCP2515_BUKT         0x04u

/** Bit 3 of SIDL: this identifier is a 29-bit extended one. */
#define MCP2515_SIDL_EXIDE 0x08u

/**
 * Pack a 29-bit extended identifier into SIDH, SIDL, EID8, EID0.
 *
 * False if the identifier does not fit in 29 bits, which is a caller bug --
 * silently truncating it would put a frame on the bus under an identifier
 * nobody chose.
 */
bool mcp2515_pack_id(uint32_t id, uint8_t out[4]);

/**
 * Recover the identifier from those four registers.
 *
 * False when EXIDE is clear: the frame carried an 11-bit identifier, which
 * this link never sends, and reading it as though it were extended would
 * invent twenty-nine bits out of eleven.
 */
bool mcp2515_unpack_id(const uint8_t in[4], uint32_t *id);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_MCP2515_H */
