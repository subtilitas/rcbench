/**
 * @file
 * @brief Link frames on a UART (universal asynchronous receiver-transmitter):
 * COBS (consistent overhead byte stuffing) framing with a CRC-16 (cyclic
 * redundancy check).
 *
 * One UART frame carries one `link_can_frame_t` unchanged, so
 * link_can_encode() and link_can_decode_counted() run on top of it as they
 * run on CAN (Controller Area Network).  Before COBS a frame is:
 *
 *     bytes 0..3       the 29-bit identifier, little-endian
 *     byte  4          the DLC (data length code), 0..8
 *     bytes 5..4+DLC   the data
 *     2 bytes          CRC-16 over identifier, DLC and data, little-endian
 *
 * The CRC is link_crc() from the seed LINK_CRC_INIT (polynomial 0x1021,
 * seed 0xFFFF, CRC-16/CCITT-FALSE).  COBS takes every 0x00 out of those 7
 * to 15 bytes and adds 1 byte; a 0x00 ends the frame.  A frame is 9 to 17
 * bytes on the wire: 90 to 170µs at 1Mbaud, 8N1.
 *
 * The decoder takes one byte at a time, so a frame split across any number
 * of reads, or several frames in one read, decode the same.  It holds at
 * most LINK_UART_COBS_MAX bytes.  Everything before the first 0x00 it sees
 * is dropped: a receiver that starts in the middle of a frame, or behind an
 * ESP32-S3 ROM boot message at 115,200 baud, starts clean at the next 0x00.
 * Two 0x00 in a row are idle line, not a frame, and count nothing.
 *
 * Each frame the decoder drops adds 1 to the caller's refused-frame counter,
 * the counter the IOmcu publishes in STATUS registers 8 and 9
 * (LINK_ST_REFUSED_LO and _HI) for CAN frames refused by the receive buffer
 * and by link_can_decode_counted().  A frame that passes here and is refused
 * by link_can_decode_counted() counts once, there.
 *
 * Plain C, no includes outside `shared/link`; also compiles as C++.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_LINK_UART_H
#define RCBENCH_LINK_UART_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "link_can.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The byte that ends every frame; COBS keeps it out of the frame. */
#define LINK_UART_DELIM 0x00u

/** @brief Identifier bytes before the DLC. */
#define LINK_UART_ID_BYTES 4u

/** @brief Bytes before the data: identifier and DLC. */
#define LINK_UART_HEAD_BYTES (LINK_UART_ID_BYTES + 1u)

/** @brief CRC bytes after the data. */
#define LINK_UART_CRC_BYTES 2u

/** @brief The highest identifier: 29 bits. */
#define LINK_UART_ID_MAX 0x1FFFFFFFuL

/** @brief Bytes before COBS of a frame with DLC 0: 7. */
#define LINK_UART_RAW_MIN (LINK_UART_HEAD_BYTES + LINK_UART_CRC_BYTES)

/** @brief Bytes before COBS of a frame with DLC 8: 15. */
#define LINK_UART_RAW_MAX (LINK_UART_RAW_MIN + 8u)

/** @brief COBS bytes of the longest frame, the delimiter not counted: 16. */
#define LINK_UART_COBS_MAX (LINK_UART_RAW_MAX + 1u)

/** @brief Bytes on the wire of a frame with DLC 0, delimiter counted: 9. */
#define LINK_UART_WIRE_MIN (LINK_UART_RAW_MIN + 2u)

/** @brief Bytes on the wire of a frame with DLC 8, delimiter counted: 17. */
#define LINK_UART_WIRE_MAX (LINK_UART_RAW_MAX + 2u)

/**
 * @brief The decoder's counts since link_uart_rx_init(), each modulo 2^32.
 *
 * Each dropped frame adds 1 to exactly one of the refusal counts, checked in
 * this order: garbage, overlong, bad_cobs, bad_len (short), bad_crc,
 * bad_dlc, bad_len (DLC disagrees), bad_id.
 */
typedef struct {
    uint32_t frames;   /**< frames taken and handed out                     */
    uint32_t garbage;  /**< bytes before the first 0x00, counted once       */
    uint32_t overlong; /**< more than LINK_UART_COBS_MAX bytes, counted once */
    uint32_t bad_cobs; /**< a COBS code byte pointing past the frame's end  */
    uint32_t bad_len;  /**< under 7 bytes after COBS, or 7 + DLC disagreeing
                            with the length while the CRC holds              */
    uint32_t bad_crc;  /**< the CRC-16 does not match                       */
    uint32_t bad_dlc;  /**< a DLC over 8 while the CRC holds                */
    uint32_t bad_id;   /**< an identifier above 29 bits while the CRC holds */
} link_uart_counts_t;

/**
 * @brief The streaming decoder: the bytes of the frame under way and the
 * counts.  Set up with link_uart_rx_init(); no other field is for the caller
 * to write.
 */
typedef struct {
    uint8_t buf[LINK_UART_COBS_MAX]; /**< COBS bytes since the last 0x00   */
    uint8_t len;                     /**< bytes held in buf                */
    bool    synced;   /**< a 0x00 has been seen since link_uart_rx_init()  */
    bool    dirty;    /**< bytes arrived before the first 0x00             */
    bool    overlong; /**< the frame under way passed LINK_UART_COBS_MAX:
                           its bytes are dropped up to the next 0x00        */
    link_uart_counts_t counts; /**< what the decoder took and dropped      */
} link_uart_rx_t;

/**
 * @brief Render one link frame as the bytes a UART sends, 0x00 included.
 *
 * @param f The frame: DLC 0 to 8, identifier at most LINK_UART_ID_MAX.
 * @param out Receives LINK_UART_WIRE_MIN + f->dlc bytes, the last one 0x00
 *        and no other 0x00.
 * @param cap Bytes out can hold.
 * @return The bytes written, 9 to 17; 0 for a NULL argument, a DLC over 8,
 *         an identifier above 29 bits or a cap too small for the frame.
 */
size_t link_uart_encode(const link_can_frame_t *f, uint8_t *out, size_t cap);

/**
 * @brief Clear a decoder: no bytes held, not synced, every count 0.
 *
 * @param rx The decoder; NULL does nothing.
 */
void link_uart_rx_init(link_uart_rx_t *rx);

/**
 * @brief Drop the frame under way: bytes were lost between the last one fed
 * and the next one, so what the decoder holds cannot end in a good frame.
 *
 * The bytes up to the next 0x00 are dropped and count nothing; the counts
 * stay.  The caller counts the loss where it found it (a receive ring that
 * was overwritten, for one).  A decoder not yet synced stays so.
 *
 * @param rx The decoder; NULL does nothing.
 */
void link_uart_rx_drop(link_uart_rx_t *rx);

/**
 * @brief Take one received byte; at a 0x00 that ends a good frame, hand the
 * frame out.
 *
 * At a 0x00 that ends a frame the checks refuse, the frame is dropped, the
 * matching count in rx->counts rises by 1 and @p refused rises by 1, modulo
 * 2^32.  A frame that grows past LINK_UART_COBS_MAX bytes is refused at the
 * byte that makes it too long, and its remaining bytes up to the next 0x00
 * are dropped without a further count.
 *
 * @param rx The decoder.
 * @param byte The byte, as received.
 * @param out Receives the frame when true is returned; untouched otherwise.
 * @param refused The refused-frame counter, for STATUS registers 8 and 9;
 *        NULL counts nothing there.
 * @return true when @p out holds a frame; false for every other byte, and
 *         for a NULL @p rx or @p out, which takes no byte and counts
 *         nothing.
 */
bool link_uart_feed(link_uart_rx_t *rx, uint8_t byte, link_can_frame_t *out,
                    uint32_t *refused);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_LINK_UART_H */
