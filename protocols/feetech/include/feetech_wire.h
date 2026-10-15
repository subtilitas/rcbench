/*
 * Feetech SCS and STS serial bus servos: instruction frames and the status
 * decoder.
 *
 * Implements the serial bus protocol of Feetech SCS and STS servos.
 * Feetech is a name or trademark of its owner; it is used here only to say
 * which servos this module works with (README.md, "Trademarks").
 *
 * The bus is one signal wire shared by the master and every servo: half
 * duplex, asynchronous, 8 data bits, no parity, 1 stop bit, least
 * significant bit first.  The master sends an instruction frame; the
 * servo it names answers with a status frame.  A frame to the broadcast ID
 * 0xFE has no answer, except PING.
 *
 *   instruction  FF FF ID LEN INSTR P1 .. Pn CHK
 *   status       FF FF ID LEN ERR   P1 .. Pn CHK
 *
 * LEN is n + 2.  CHK is the bitwise NOT of the low byte of the sum of ID,
 * LEN, INSTR (or ERR) and the parameters.  A frame is LEN + 4 bytes.  There
 * is no byte stuffing: the header can appear inside the data, so the
 * decoder frames on the header, the length and the checksum, and on a
 * mismatch drops the first byte and looks again.
 *
 * Source: Feetech, "Serial Bus Smart Control servo, Communication Protocol
 * Manual", V1.01, 2019-02-19 (README.md, "Sources").
 *
 * The module includes nothing outside the C standard headers and holds no
 * global state.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FEETECH_WIRE_H
#define FEETECH_WIRE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** The highest ID a servo takes; IDs 0 to 253 address one servo. */
#define FEETECH_ID_MAX       253u
/** The broadcast ID: every servo acts, none answers but to PING. */
#define FEETECH_ID_BROADCAST 0xFEu

/** Instructions. */
#define FEETECH_PING       0x01u
#define FEETECH_READ       0x02u
#define FEETECH_WRITE      0x03u
#define FEETECH_REG_WRITE  0x04u
#define FEETECH_ACTION     0x05u
#define FEETECH_RESET      0x06u
#define FEETECH_SYNC_WRITE 0x83u

/** Bytes of a frame around its parameters: header 2, ID, LEN, INSTR or
 *  ERR, CHK. */
#define FEETECH_FRAME_OVERHEAD 6u
/** Data bytes a READ asks for and a WRITE carries at most, and with them
 *  the parameters of a status frame the decoder takes. */
#define FEETECH_DATA_MAX   16u
/** The longest frame this module builds, in bytes: a SYNC WRITE to 8
 *  servos of 6 bytes each is 64. */
#define FEETECH_FRAME_MAX  64u
/** Parameters of the longest frame. */
#define FEETECH_PARAMS_MAX (FEETECH_FRAME_MAX - FEETECH_FRAME_OVERHEAD)

/** Bits on the wire per byte: start, 8 data, stop. */
#define FEETECH_BITS_PER_BYTE 10u

/** One frame, as built for the wire. */
typedef struct {
    uint8_t bytes[FEETECH_FRAME_MAX];
    uint8_t n;          /**< bytes used; 0 for a frame that was refused */
} feetech_frame_t;

/** The checksum of @p n bytes from ID on: the NOT of their sum's low
 *  byte. */
uint8_t feetech_checksum(const uint8_t *from_id, size_t n);

/**
 * An instruction frame for @p id with @p n_params parameters.  False, with
 * out->n 0, for an ID above 0xFE, more than FEETECH_PARAMS_MAX parameters,
 * or NULL parameters with @p n_params above 0.
 */
bool feetech_build(feetech_frame_t *out, uint8_t id, uint8_t instr,
                   const uint8_t *params, unsigned n_params);

/** PING: answered by a servo with @p id, by every servo for 0xFE. */
bool feetech_ping(feetech_frame_t *out, uint8_t id);
/** READ DATA: @p count bytes from @p addr, 1 to FEETECH_DATA_MAX. */
bool feetech_read(feetech_frame_t *out, uint8_t id, uint8_t addr,
                  uint8_t count);
/** WRITE DATA: @p n bytes to @p addr, 1 to FEETECH_DATA_MAX. */
bool feetech_write(feetech_frame_t *out, uint8_t id, uint8_t addr,
                   const uint8_t *data, unsigned n);
/** REG WRITE: as WRITE; the servo keeps it until ACTION. */
bool feetech_reg_write(feetech_frame_t *out, uint8_t id, uint8_t addr,
                       const uint8_t *data, unsigned n);
/** ACTION: the REG WRITEs kept take effect. */
bool feetech_action(feetech_frame_t *out, uint8_t id);
/** RESET: the servo's table back to the factory values. */
bool feetech_reset(feetech_frame_t *out, uint8_t id);
/**
 * SYNC WRITE to the broadcast ID: @p len bytes at @p addr for each of
 * @p n_ids servos.  @p blocks holds n_ids blocks of 1 + len bytes, an ID
 * followed by its data.  LEN is (len + 1) * n_ids + 4.  False when the
 * frame would be longer than FEETECH_FRAME_MAX, for len 0 and for no ID.
 */
bool feetech_sync_write(feetech_frame_t *out, uint8_t addr, uint8_t len,
                        const uint8_t *blocks, unsigned n_ids);

/** What the decoder made of the bytes after a request. */
typedef enum {
    FEETECH_RX_OK = 0,      /**< a status frame from the servo asked     */
    FEETECH_RX_NONE,        /**< no byte came                            */
    FEETECH_RX_TRUNCATED,   /**< a header and fewer bytes than its LEN    */
    FEETECH_RX_CHECKSUM,    /**< frames whose checksum does not add up,
                                 and no good one                         */
    FEETECH_RX_WRONG_ID,    /**< a good frame from another ID             */
    FEETECH_RX_LENGTH,      /**< a good frame from the ID asked with
                                 another number of parameters than asked */
    FEETECH_RX_NOISE,       /**< bytes and no frame                       */
    FEETECH_RX_FRAMING,     /**< no good frame, and a byte with a stop
                                 bit low                                 */
    FEETECH_RX_COUNT
} feetech_rx_t;

/** A decoded status frame. */
typedef struct {
    uint8_t id;
    uint8_t error;       /**< the ERR byte; 0 no error                   */
    uint8_t n_params;
    uint8_t params[FEETECH_DATA_MAX];
    uint8_t skipped;     /**< bytes dropped before the frame             */
} feetech_status_t;

/**
 * Find the status frame from @p want_id with @p want_params parameters in
 * @p n bytes.  @p framing is the number of received bytes whose stop bit
 * was low.  The first good frame decides: a good frame from another ID is
 * FEETECH_RX_WRONG_ID, one with another parameter count
 * FEETECH_RX_LENGTH; out holds it in both cases.  A checksum that does not
 * add up drops the first header byte and the search goes on.  A header
 * whose LEN is below 2 or above FEETECH_DATA_MAX + 2 is no frame.
 * @p want_params above FEETECH_DATA_MAX takes any count.
 */
feetech_rx_t feetech_parse(const uint8_t *bytes, unsigned n, unsigned framing,
                           uint8_t want_id, unsigned want_params,
                           feetech_status_t *out);

/**
 * Bytes of @p rx before the reply: @p n_tx when @p rx starts with the
 * frame @p tx as sent, as a driver that hears its own transmission gives
 * it, else 0.
 */
unsigned feetech_echo(const uint8_t *rx, unsigned n_rx, const uint8_t *tx,
                      unsigned n_tx);

/** Microseconds @p n_bytes take on the wire at @p baud, rounded up. */
uint32_t feetech_bytes_us(uint32_t baud, unsigned n_bytes);

#ifdef __cplusplus
}
#endif

#endif /* FEETECH_WIRE_H */
