/**
 * @file
 * @brief The MCP2515's register map and frame layout, from the datasheet.
 *
 * Everything here is arithmetic or a constant: no SPI (Serial Peripheral
 * Interface), no pins, no delays.  It is host-tested, and the driver that
 * touches hardware stays thin.
 *
 * The identifier is the part that needs the tests.  A 29-bit extended
 * identifier is spread across four registers with a three-bit gap in the
 * second: the register layout is the 11-bit one, with the extended bits
 * fitted around it.  A wrong packing puts the frame on the bus under a
 * different identifier, and on a bus that arbitrates by identifier the wrong
 * frame wins.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_MCP2515_H
#define RCBENCH_MCP2515_H

#include <stdbool.h>
#include <stdint.h>

#include "link_can.h"

#ifdef __cplusplus
extern "C" {
#endif

/* SPI commands. */
#define MCP2515_CMD_RESET       0xC0u /**< reset instruction */
#define MCP2515_CMD_READ        0x03u /**< read registers */
#define MCP2515_CMD_WRITE       0x02u /**< write registers */
/** @brief Request to send for transmit buffer 0. */
#define MCP2515_CMD_RTS_TXB0    0x81u
/**
 * @brief Request to send, with the buffers in bits 0 to 2: 0x81 TXB0, 0x82
 *        TXB1, 0x84 TXB2, 0x87 all three in one command.
 */
#define MCP2515_CMD_RTS         0x80u
#define MCP2515_CMD_READ_STATUS 0xA0u /**< read status */
#define MCP2515_CMD_RX_STATUS   0xB0u /**< RX status */
#define MCP2515_CMD_BIT_MODIFY  0x05u /**< bit modify */

/* Registers this driver touches. */
/** @brief CANSTAT: operation mode and interrupt code. */
#define MCP2515_CANSTAT   0x0Eu
#define MCP2515_CANCTRL   0x0Fu /**< CANCTRL: request mode and clock output */
#define MCP2515_CANINTE   0x2Bu /**< CANINTE: interrupt enable */
#define MCP2515_CANINTF   0x2Cu /**< CANINTF: interrupt flags */
#define MCP2515_EFLG      0x2Du /**< EFLG: error flags */
#define MCP2515_TEC       0x1Cu /**< TEC: transmit error counter */
#define MCP2515_REC       0x1Du /**< REC: receive error counter */

#define MCP2515_TXB0CTRL  0x30u /**< TXB0 control */
#define MCP2515_TXB0SIDH  0x31u   /**< then SIDL, EID8, EID0, DLC, D0..D7 */
#define MCP2515_TXB1CTRL  0x40u /**< TXB1 control, then its SIDH to D7 */
#define MCP2515_TXB2CTRL  0x50u /**< TXB2 control, then its SIDH to D7 */
/** @brief Transmit buffers of the controller: TXB0, TXB1 and TXB2. */
#define MCP2515_TX_BUFFERS 3u
/** @brief Bytes from TXBnCTRL to D7: CTRL, SIDH, SIDL, EID8, EID0, DLC and
 *         8 data bytes. */
#define MCP2515_TXB_LOAD_BYTES 14u

/* TXBnCTRL bits. */
#define MCP2515_TXB_TXREQ    0x08u /**< transmission requested, not yet sent */
#define MCP2515_TXB_TXP_MASK 0x03u /**< the buffer's priority, 3 highest     */

/* The byte READ STATUS (0xA0) answers. */
#define MCP2515_STATUS_RX0IF   0x01u /**< receive buffer 0 holds a frame  */
#define MCP2515_STATUS_RX1IF   0x02u /**< receive buffer 1 holds a frame  */
#define MCP2515_STATUS_TX0REQ  0x04u /**< TXB0's TXREQ                    */
#define MCP2515_STATUS_TX1REQ  0x10u /**< TXB1's TXREQ                    */
#define MCP2515_STATUS_TX2REQ  0x40u /**< TXB2's TXREQ                    */
/** @brief The three TXREQ bits of READ STATUS. */
#define MCP2515_STATUS_TXREQ \
    (MCP2515_STATUS_TX0REQ | MCP2515_STATUS_TX1REQ | MCP2515_STATUS_TX2REQ)
#define MCP2515_RXB0CTRL  0x60u /**< RXB0 control */
/** @brief RXB0 SIDH, then SIDL, EID8, EID0, DLC, D0..D7. */
#define MCP2515_RXB0SIDH  0x61u
#define MCP2515_RXB1CTRL  0x70u /**< RXB1 control */
/** @brief RXB1 SIDH, then SIDL, EID8, EID0, DLC, D0..D7. */
#define MCP2515_RXB1SIDH  0x71u
#define MCP2515_RXM0SIDH  0x20u /**< receive mask 0, SIDH */
#define MCP2515_RXF0SIDH  0x00u /**< receive filter 0, SIDH */

/* CANCTRL request modes, in the top three bits. */
#define MCP2515_MODE_NORMAL   0x00u /**< normal operation */
#define MCP2515_MODE_SLEEP    0x20u /**< sleep */
#define MCP2515_MODE_LOOPBACK 0x40u /**< loopback */
#define MCP2515_MODE_LISTEN   0x60u /**< listen only */
#define MCP2515_MODE_CONFIG   0x80u /**< configuration */
#define MCP2515_MODE_MASK     0xE0u /**< mask of the mode bits */

/* CANINTF bits worth naming. */
#define MCP2515_INTF_RX0  0x01u /**< receive buffer 0 holds a frame */
#define MCP2515_INTF_RX1  0x02u /**< receive buffer 1 holds a frame */
#define MCP2515_INTF_TX0  0x04u /**< transmit buffer 0 is empty */
#define MCP2515_INTF_ERR  0x20u /**< error interrupt */

/* EFLG.  The two overflow bits are set when a frame arrives with both receive
 * buffers full; they are sticky and the MCU (microcontroller unit) clears
 * them.  They are the record of a frame lost with nothing wrong on the
 * wire. */
#define MCP2515_EFLG_RX0OVR 0x40u /**< receive buffer 0 overflowed */
#define MCP2515_EFLG_RX1OVR 0x80u /**< receive buffer 1 overflowed */
#define MCP2515_EFLG_TXBO   0x20u /**< transmit bus-off */
/** @brief Either receive buffer overflowed. */
#define MCP2515_EFLG_OVR    (MCP2515_EFLG_RX0OVR | MCP2515_EFLG_RX1OVR)

/* RXB0CTRL / RXB1CTRL receive-mode field. */
#define MCP2515_RXM_ANY      0x60u  /**< accept everything, filters off */
#define MCP2515_RXM_EXT_ONLY 0x40u  /**< extended identifiers only */
/** RXB0CTRL only: roll a frame over to buffer 1 when buffer 0 is full. */
#define MCP2515_BUKT         0x04u

/** Bit 3 of SIDL: this identifier is a 29-bit extended one. */
#define MCP2515_SIDL_EXIDE 0x08u

/**
 * @brief Pack a 29-bit extended identifier into SIDH, SIDL, EID8, EID0.
 *
 * @param id   The identifier.
 * @param out  Receives the four register values.
 * @return false if the identifier does not fit in 29 bits: a caller bug, and
 *         truncating it would put a frame on the bus under an unintended
 *         identifier.  true otherwise.
 */
bool mcp2515_pack_id(uint32_t id, uint8_t out[4]);

/**
 * @brief Recover the identifier from those four registers.
 *
 * @param in  The four register values, SIDH, SIDL, EID8, EID0.
 * @param id  Receives the identifier.
 * @return false when EXIDE is clear: the frame carried an 11-bit identifier,
 *         which this link never sends, and reading it as though it were
 *         extended would invent twenty-nine bits out of eleven.  true
 *         otherwise.
 */
bool mcp2515_unpack_id(const uint8_t in[4], uint32_t *id);

/**
 * @brief The bytes one WRITE puts into a transmit buffer, from TXBnCTRL on:
 *        CTRL with TXREQ clear and the priority, the identifier's four
 *        registers, DLC and the payload, padded with zeros to 8 bytes.
 *
 * @param f    The frame. A DLC above 8 is refused.
 * @param txp  The buffer's priority, 0 to 3; masked to 2 bits.
 * @param out  Receives MCP2515_TXB_LOAD_BYTES bytes.
 * @return false when @p f or @p out is NULL, the DLC is above 8 or the
 *         identifier does not fit 29 bits; @p out is then unwritten.
 */
bool mcp2515_tx_load(const link_can_frame_t *f, uint8_t txp,
                     uint8_t out[MCP2515_TXB_LOAD_BYTES]);

/** Receive buffers of the controller: RXB0 and RXB1. */
#define MCP2515_RX_BUFFERS 2u

/**
 * Take one receive buffer, consume it and copy its 13 registers (SIDH to D7)
 * to @p buf.  False when no buffer holds a frame.
 */
typedef bool (*mcp2515_fetch_fn)(void *ctx, uint8_t buf[13]);

/**
 * @brief The next frame this link carries.
 *
 * A buffer @p fetch hands over and mcp2515_decode_rx() refuses is consumed
 * and the next buffer is tried, up to MCP2515_RX_BUFFERS, so a valid frame in
 * RXB1 does not wait behind a refused one in RXB0.
 *
 * Each refused buffer adds 1 to @p refused, modulo 2^32; NULL counts
 * nothing.
 *
 * @param fetch    Takes one receive buffer.
 * @param ctx      Handed to @p fetch.
 * @param f        Receives the frame.
 * @param refused  Counts the refused buffers; NULL counts nothing.
 * @return true when a buffer held a valid frame; false when none did.
 */
bool mcp2515_recv_next(mcp2515_fetch_fn fetch, void *ctx, link_can_frame_t *f,
                       uint32_t *refused);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_MCP2515_H */
