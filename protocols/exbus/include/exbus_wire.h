/**
 * @file exbus_wire.h
 * @brief EX Bus packets as the master builds and reads them: the channel
 * packet, the telemetry request, the reply check and the CRC-16.
 *
 * Implements the EX Bus protocol of JETI receivers and devices.  JETI and
 * EX Bus are names or trademarks of their respective owner; they are used
 * here only to say which devices this module works with (README.md, "Legal
 * notice and disclaimer").
 *
 * Source: "EX Bus communication protocol", JETI model, version 1.21 of
 * 2013-12-18 (S1 in the module README).  The bench is the master: it sends
 * channel packets and telemetry requests and reads telemetry replies.  It
 * never answers as a device.
 *
 * A packet is: 2 header bytes, LEN (the whole packet in bytes, CRC
 * included), Packet_ID, DATA_ID, SUB_LEN, SUB_LEN data bytes, CRC-16 low
 * byte first.  Multi-byte values are little-endian.  The CRC-16 is
 * CRC-16/KERMIT: polynomial 0x1021 reflected (0x8408), initial value 0, no
 * final XOR, over every byte from the first header byte to the byte before
 * the CRC.
 *
 * The module includes nothing outside the C standard headers and holds no
 * global state.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef EXBUS_WIRE_H
#define EXBUS_WIRE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** First header byte of a channel packet. */
#define EXBUS_HEAD_CHANNELS 0x3Eu
/** First header byte of a request (telemetry or JETIBOX menu). */
#define EXBUS_HEAD_REQUEST  0x3Du
/** First header byte of a device's reply. */
#define EXBUS_HEAD_REPLY    0x3Bu
/** Second header byte: the master releases the line after the packet. */
#define EXBUS_HEAD_RELEASE  0x01u
/** Second header byte: no release; a packet with EXBUS_HEAD_RELEASE follows. */
#define EXBUS_HEAD_HOLD     0x03u

/** DATA_ID of channel values. */
#define EXBUS_ID_CHANNELS   0x31u
/** DATA_ID of EX telemetry: a request with SUB_LEN 0, a reply with the
 *  block. */
#define EXBUS_ID_TELEMETRY  0x3Au
/** DATA_ID of the JETIBOX menu.  The bench sends no such request. */
#define EXBUS_ID_JETIBOX    0x3Bu

/** Bytes of a packet beyond its data: header 2, LEN, Packet_ID, DATA_ID,
 *  SUB_LEN, CRC 2. */
#define EXBUS_OVERHEAD      8u

/** Channels in one packet.  The specification states no limit; 24 is the
 *  largest count a JETI document names (module README). */
#define EXBUS_MAX_CHANNELS  24u
/** The longest channel packet, 24 channels: 56 bytes. */
#define EXBUS_CHANNEL_PACKET_MAX (EXBUS_OVERHEAD + 2u * EXBUS_MAX_CHANNELS)
/** A telemetry request: 8 bytes. */
#define EXBUS_REQUEST_LEN   EXBUS_OVERHEAD
/** The longest transmission of one cycle: a channel packet of 24 channels
 *  followed by a request, 64 bytes. */
#define EXBUS_TX_MAX        (EXBUS_CHANNEL_PACKET_MAX + EXBUS_REQUEST_LEN)

/** The longest EX telemetry block a reply carries, from its 0xNF byte to its
 *  CRC8: 28 bytes (the EX telemetry specification allows 29 bytes with the
 *  0x7E separator, which EX Bus leaves out). */
#define EXBUS_TM_BLOCK_MAX  28u
/** The longest telemetry reply: 36 bytes. */
#define EXBUS_REPLY_MAX     (EXBUS_OVERHEAD + EXBUS_TM_BLOCK_MAX)

/** Channel values are in 1/8 us: 8 counts per microsecond. */
#define EXBUS_COUNTS_PER_US 8u
/** The range the encoder clamps a channel to, in counts: 400 us to
 *  2500 us.  The specification gives no range; this is the range of the
 *  bench's own pulse outputs. */
#define EXBUS_CH_MIN        (400u * EXBUS_COUNTS_PER_US)
#define EXBUS_CH_MAX        (2500u * EXBUS_COUNTS_PER_US)

/** Bits on the wire per byte: start, 8 data bits, stop (8N1). */
#define EXBUS_BITS_PER_BYTE 10u

/** After a received byte the master keeps the line released until it has
 *  been high this long: 20 bits, the time of 2 bytes.  Chosen; the
 *  specification states no gap between the bytes of a reply. */
#define EXBUS_QUIET_BITS    20u

/** CRC-16/KERMIT of @p n bytes, starting from 0.  "123456789" gives
 *  0x2189. */
uint16_t exbus_crc16(const uint8_t *data, size_t n);

/** One byte into a running CRC-16/KERMIT: the specification's update
 *  function. */
uint16_t exbus_crc16_update(uint16_t crc, uint8_t byte);

/** A pulse width in microseconds as channel counts, before the clamp:
 *  @p us times 8, saturated at 0xFFFF. */
uint16_t exbus_us_to_counts(uint32_t us);

/** @p counts clamped to EXBUS_CH_MIN..EXBUS_CH_MAX. */
uint16_t exbus_clamp_counts(uint16_t counts);

/**
 * A channel packet of @p n channels into @p out (8 + 2 * n bytes).  Each
 * value is in counts of 1/8 us and is clamped to EXBUS_CH_MIN..EXBUS_CH_MAX;
 * the first value is channel 1.  @p release true gives the second header
 * byte 0x01 (the line is released after it), false 0x03 (a request
 * follows).  Returns the packet's length, or 0 for no output, no values,
 * or @p n outside 1..EXBUS_MAX_CHANNELS.  @p clamped, when not NULL,
 * receives the number of values the clamp changed.
 */
size_t exbus_build_channels(const uint16_t *counts, unsigned n,
                            uint8_t packet_id, bool release, uint8_t *out,
                            unsigned *clamped);

/** A telemetry request into @p out: 8 bytes, second header byte 0x01.
 *  Returns 8, or 0 for no output. */
size_t exbus_build_request(uint8_t packet_id, uint8_t *out);

/** What exbus_reply_parse() found. */
typedef enum {
    EXBUS_RX_OK = 0,
    EXBUS_RX_NONE,        /**< no byte received */
    EXBUS_RX_HEADER,      /**< no 0x3B 0x01 header in the bytes */
    EXBUS_RX_TRUNCATED,   /**< fewer bytes than the header or LEN says */
    EXBUS_RX_LENGTH,      /**< LEN below 8, or LEN and SUB_LEN disagree */
    EXBUS_RX_TOO_LONG,    /**< LEN above EXBUS_REPLY_MAX */
    EXBUS_RX_CRC,         /**< the CRC-16 does not match */
    EXBUS_RX_DATA_ID,     /**< a DATA_ID other than EX telemetry */
    EXBUS_RX_PACKET_ID,   /**< the Packet_ID is not the request's */
    /* The two below come from the capture's timing and stop bits; the
     * scheduler sets them, exbus_reply_parse() does not. */
    EXBUS_RX_LATE,        /**< the reply started after the window */
    EXBUS_RX_FRAMING,     /**< a byte of the reply had a low stop bit */
    EXBUS_RX_COUNT
} exbus_rx_t;

/** A telemetry reply as exbus_reply_parse() took it. */
typedef struct {
    uint8_t packet_id;
    uint8_t block_len;                  /**< SUB_LEN: 0 is "no telemetry" */
    uint8_t block[EXBUS_TM_BLOCK_MAX];  /**< the EX telemetry block */
    uint8_t skipped;                    /**< bytes before the header */
    uint8_t extra;                      /**< bytes after the packet */
} exbus_reply_t;

/**
 * Check @p n received bytes as the reply to a request with @p packet_id.
 * The header 0x3B 0x01 is searched for; bytes before it count as skipped.
 * The checks run in the order of the codes: header, length, CRC, DATA_ID,
 * Packet_ID.  @p out, when not NULL, is filled on EXBUS_RX_OK and holds
 * the skipped count otherwise.
 */
exbus_rx_t exbus_reply_parse(const uint8_t *bytes, size_t n,
                             uint8_t packet_id, exbus_reply_t *out);

/** The air time of @p bytes bytes at @p baud, in us, rounded up; 0 for a
 *  baud of 0. */
uint32_t exbus_air_us(uint32_t bytes, uint32_t baud);

/** Bytes a capture keeps: the longest reply and 12 more. */
#define EXBUS_RX_MAX        48u
/** A received byte whose stop bit was low. */
#define EXBUS_RXB_FRAMING   0x01u

/** One byte the line received. */
typedef struct {
    uint8_t  value;
    uint8_t  flags;     /**< EXBUS_RXB_FRAMING */
    uint16_t at_us;     /**< its start bit, in us after the release */
} exbus_rx_byte_t;

/** What the line received while the master had released it. */
typedef struct {
    uint8_t  n;           /**< bytes kept */
    uint8_t  overflow;    /**< more bytes than kept, a time no run gives, or
                               a transaction the driver had to cut */
    uint16_t release_us;  /**< how long the line was released */
    exbus_rx_byte_t bytes[EXBUS_RX_MAX];
} exbus_capture_t;

#ifdef __cplusplus
}
#endif

#endif /* EXBUS_WIRE_H */
