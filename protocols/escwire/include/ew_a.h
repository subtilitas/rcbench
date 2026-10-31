/**
 * @file ew_a.h
 * @brief Protocol A: the binary bootloader protocol on the signal wire. Frame
 *        builders, the CRC (cyclic redundancy check), and the answers.
 *
 * Bytes on the wire at 52us a bit.  A frame is a command, an argument, more
 * bytes and a CRC-16 sent low byte first.  Builders write to a buffer the
 * caller holds and return the frame's length.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef EW_A_H
#define EW_A_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ew_wire.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EW_A_CMD_LEAVE     0x00u /**< leave or restart                    */
#define EW_A_CMD_PROGRAM   0x01u /**< program the announced data          */
#define EW_A_CMD_ERASE     0x02u /**< erase the page of the address       */
#define EW_A_CMD_READ      0x03u /**< read from the address               */
#define EW_A_CMD_KEEP      0xFDu /**< keep alive, answered EW_A_REF_UNKNOWN */
#define EW_A_CMD_ANNOUNCE  0xFEu /**< data follow                         */
#define EW_A_CMD_ADDRESS   0xFFu /**< set the address                     */

#define EW_A_ACK           0x30u /**< success                             */
#define EW_A_REF_VERIFY    0xC0u /**< verification error                  */
#define EW_A_REF_UNKNOWN   0xC1u /**< unknown or refused command          */
#define EW_A_REF_CRC       0xC2u /**< bad CRC                             */
#define EW_A_REF_PROGRAM   0xC5u /**< programming refused                 */

#define EW_A_CONTACT_LEN   17u   /**< the first contact string, bytes     */
#define EW_A_ANSWER_LEN    9u    /**< its answer, bytes                   */
#define EW_A_SHORT_FRAME   4u    /**< a command, an argument and the CRC  */
#define EW_A_LONG_FRAME    6u    /**< an address or announcement frame    */
#define EW_A_READ_MAX      256u  /**< the most bytes one read asks for    */

#define EW_A_CONTACT_US    100000u  /**< the wait for the contact's answer */
#define EW_A_CONTACT_TRIES 3u       /**< tries of the first contact        */
#define EW_A_ADDRESS_US    4000u    /**< the wait for an address answer    */
#define EW_A_READ_US       80000u   /**< the wait for a read's first byte  */
#define EW_A_GAP_US        2000u    /**< the wait between two bytes        */
#define EW_A_BUFFER_US     80000u   /**< the wait after a data buffer      */
#define EW_A_PROGRAM_US    500000u  /**< the wait after a program          */
#define EW_A_ERASE_US      3000000u /**< the wait after an erase           */

/** @brief What an answer says. */
typedef enum {
    EW_ANS_OK = 0,   /**< as asked                                          */
    EW_ANS_SILENT,   /**< no byte                                           */
    EW_ANS_ABORTED,  /**< the transaction was cut or its capture not whole  */
    EW_ANS_FRAMING,  /**< a byte with a low stop bit                        */
    EW_ANS_LENGTH,   /**< another number of bytes than the answer has       */
    EW_ANS_CRC,      /**< a read's CRC does not fit                         */
    EW_ANS_NO_ACK,   /**< a read's last byte is not EW_A_ACK                */
    EW_ANS_VERIFY,   /**< the ESC answered EW_A_REF_VERIFY                  */
    EW_ANS_UNKNOWN,  /**< the ESC answered EW_A_REF_UNKNOWN                 */
    EW_ANS_BAD_CRC,  /**< the ESC answered EW_A_REF_CRC                     */
    EW_ANS_REFUSED,  /**< the ESC answered EW_A_REF_PROGRAM                 */
    EW_ANS_OTHER,    /**< any other single byte                             */
} ew_ans_t;

/**
 * @brief One step of the CRC: polynomial 0xA001 in reflected form.
 * @param crc  the CRC so far, 0 to start
 * @param byte the next byte
 * @return the CRC with the byte
 */
uint16_t ew_crc16_add(uint16_t crc, uint8_t byte);

/**
 * @brief The CRC of a block.
 * @param p the bytes, may be NULL with @p n 0
 * @param n their count
 * @return the CRC; 0 for no bytes
 */
uint16_t ew_crc16(const uint8_t *p, size_t n);

/**
 * @brief The first contact string.
 * @param out takes EW_A_CONTACT_LEN bytes
 * @return EW_A_CONTACT_LEN
 */
size_t ew_a_contact(uint8_t *out);

/**
 * @brief An address frame.
 * @param out  takes EW_A_LONG_FRAME bytes
 * @param addr the address
 * @return EW_A_LONG_FRAME
 */
size_t ew_a_address(uint8_t *out, uint16_t addr);

/**
 * @brief A read frame.
 * @param out takes EW_A_SHORT_FRAME bytes
 * @param n   bytes wanted, 1 to EW_A_READ_MAX; 256 is sent as 0
 * @return EW_A_SHORT_FRAME, or 0 for an @p n out of range
 */
size_t ew_a_read(uint8_t *out, uint16_t n);

/**
 * @brief An announcement of data.
 * @param out   takes EW_A_LONG_FRAME bytes
 * @param count bytes that follow, 1 to EW_A_READ_MAX
 * @return EW_A_LONG_FRAME, or 0 for a @p count out of range
 */
size_t ew_a_announce(uint8_t *out, uint16_t count);

/**
 * @brief A block of data and its CRC.
 * @param out  takes @p n + 2 bytes
 * @param data the bytes
 * @param n    their count
 * @return @p n + 2
 */
size_t ew_a_data(uint8_t *out, const uint8_t *data, size_t n);

/**
 * @brief A frame of a command and its argument: program, erase, keep alive,
 *        leave.
 * @param out takes EW_A_SHORT_FRAME bytes
 * @param cmd the command byte
 * @param arg the argument
 * @return EW_A_SHORT_FRAME
 */
size_t ew_a_short(uint8_t *out, uint8_t cmd, uint8_t arg);

/**
 * @brief The answer of a command answered with one byte.
 * @param c  the capture
 * @param rx the bytes received
 * @return EW_ANS_OK for EW_A_ACK, else what went wrong
 */
ew_ans_t ew_a_one(const ew_capture_t *c, const uint8_t *rx);

/**
 * @brief The answer of a read of @p n bytes: the data, their CRC, EW_A_ACK.
 *        A single byte is judged as ew_a_one() judges it, so EW_A_ACK alone is
 *        EW_ANS_LENGTH.
 * @param c  the capture
 * @param rx the bytes received
 * @param n  the bytes asked for
 * @return EW_ANS_OK, else what went wrong
 */
ew_ans_t ew_a_read_reply(const ew_capture_t *c, const uint8_t *rx, uint16_t n);

/**
 * @brief The answer of the first contact: 9 bytes starting `471`.
 * @param c  the capture
 * @param rx the bytes received
 * @return EW_ANS_OK; EW_ANS_OTHER for 9 bytes with another start
 */
ew_ans_t ew_a_contact_reply(const ew_capture_t *c, const uint8_t *rx);

#ifdef __cplusplus
}
#endif

#endif /* EW_A_H */
