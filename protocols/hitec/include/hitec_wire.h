/**
 * @file
 * @brief Hitec servo programming protocols: frames, transactions, the reply
 *        decoders and the driver interface.
 *
 * Implements the signal-wire programming protocols of Hitec servos.  Hitec
 * is a name or trademark of its owner; it is used here only to say which
 * servos this module works with (README.md, "Legal notice and disclaimer").
 *
 * Two wire families share one driver:
 *
 *   - Classic, the HS-5xxx and HS-7xxx servos: 19 200 bit/s, 8N1, least
 *     significant bit first, normal polarity (the line idles high, a start
 *     bit is low).  The servo makes the high level; the master pulls low.
 *     A command is 4 bytes `cmd p1 p2 sum` whose sum is 0 modulo 256; the
 *     servo answers 2 bytes and starts its first one inside the stop bit of
 *     the master's last byte.
 *   - D series: 115 200 bit/s, 8N1, least significant bit first, inverted
 *     (inside a frame the line rests low, a start bit is high).  A write is
 *     7 bytes `96 ID addr 02 lo hi sum`, a read 5 bytes `96 ID addr 00 sum`,
 *     the answer 7 bytes `69 ID addr 02 lo hi sum`; the sum is the low byte
 *     of the sum of every byte after the header.  A write is not answered.
 *
 * This file has no hardware access.  A transaction (hitec_xfer_t) says what
 * to send and how long to listen; a driver puts it on the wire and hands
 * back the received bytes with the time of each start bit
 * (hitec_capture_t).  The decoders check a capture against what the
 * transaction asked for and give one result code per failed check.
 *
 * The timings are measured on an HS-5645MG, an HS-7955TG and a D485HW, with
 * the HPP-21 Plus and HFP-30 programmers, at a 1.4 V threshold.
 *
 * The module includes nothing outside the C standard headers and holds no
 * global state.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef HITEC_WIRE_H
#define HITEC_WIRE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- families ------------------------------------------------------------- */

/** @brief Wire family of a servo. */
typedef enum {
    HITEC_CLASSIC = 0,   /**< HS-5xxx, HS-7xxx: 19 200 bit/s, normal polarity */
    HITEC_D       = 1,   /**< D series: 115 200 bit/s, inverted */
} hitec_family_t;

/** @brief How the master drives the line.  Classic is open drain only. */
typedef enum {
    /** The pad drives low and is released for high: classic toward the
     *  servo's high level, D series toward a pull-up. */
    HITEC_DRIVE_OPEN_DRAIN = 0,
    /** D series only: the pad drives both levels while it sends. */
    HITEC_DRIVE_PUSH_PULL  = 1,
} hitec_drive_t;

/** @brief Bit rate of the classic family, in bit/s. */
#define HITEC_C_BIT_RATE 19200u
/** @brief Bit rate of the D series family, in bit/s. */
#define HITEC_D_BIT_RATE 115200u

/**
 * @brief Bit rate of a family.
 *
 * @param family  hitec_family_t value.
 * @return Bit rate of @p family in bit/s; 0 for a family this module does not
 *         have.
 */
uint32_t hitec_bit_rate(uint8_t family);

/**
 * @brief Duration of a number of bit times.
 *
 * @param family  hitec_family_t value.
 * @param bits    Number of bit times.
 * @return @p bits bit times of @p family in ns, rounded to nearest; 0 for an
 *         unknown family.
 */
uint32_t hitec_bits_ns(uint8_t family, uint32_t bits);

/* --- classic frames ------------------------------------------------------- */

/** @brief Bytes in a classic command frame. */
#define HITEC_C_FRAME_LEN 4u

/**
 * @brief Classic command bytes.
 *
 * 0x63 is described for the HS-5xxx and not seen on the wire; the others are
 * measured.
 */
#define HITEC_C_READ_EEPROM   0x61u   /**< address; reply value, `+` */
#define HITEC_C_WRITE_EEPROM  0x62u   /**< address, value; reply `+ +` */
#define HITEC_C_READ_RAM      0x63u   /**< address; reply value, `+` */
#define HITEC_C_WRITE_RAM     0x64u   /**< address, value; reply `+ +` */
#define HITEC_C_READ_POSITION 0x65u   /**< unused; reply high, low */
#define HITEC_C_MOVE          0x66u   /**< target high, low; reply `+ +` */
#define HITEC_C_FIRST         0x67u   /**< the first command; reply value, `+` */

/** @brief Classic acknowledge byte, `+`. */
#define HITEC_C_ACK 0x2Bu   /**< `+` */
/** @brief Classic negative acknowledge byte, `-`. */
#define HITEC_C_NAK 0x2Du   /**< `-`: described as the reply to a bad sum */

/**
 * @brief Compute the check byte of a classic command.
 *
 * @param cmd  Command byte.
 * @param p1   First parameter byte.
 * @param p2   Second parameter byte.
 * @return The byte that makes `cmd p1 p2 sum` sum to 0 modulo 256.
 */
uint8_t hitec_c_sum(uint8_t cmd, uint8_t p1, uint8_t p2);

/**
 * @brief Build a classic command frame.
 *
 * @param cmd  Command byte.
 * @param p1   First parameter byte.
 * @param p2   Second parameter byte.
 * @param out  Receives the 4 bytes of the frame.
 */
void hitec_c_frame(uint8_t cmd, uint8_t p1, uint8_t p2,
                   uint8_t out[HITEC_C_FRAME_LEN]);

/**
 * @brief Check a classic command frame.
 *
 * @param frame  The 4 bytes to check.
 * @return true when the 4 bytes at @p frame sum to 0 modulo 256.
 */
bool hitec_c_frame_ok(const uint8_t frame[HITEC_C_FRAME_LEN]);

/* --- D series frames ------------------------------------------------------ */

/** @brief Header byte of a D series request. */
#define HITEC_D_HEADER_REQUEST 0x96u
/** @brief Header byte of a D series answer. */
#define HITEC_D_HEADER_ANSWER  0x69u
/** @brief Bytes in a D series read request. */
#define HITEC_D_READ_LEN       5u
/** @brief Bytes in a D series write request. */
#define HITEC_D_WRITE_LEN      7u
/** @brief Bytes in a D series answer. */
#define HITEC_D_ANSWER_LEN     7u
/** @brief The length byte of a write and of an answer: 2 data bytes. */
#define HITEC_D_DATA_LEN       2u

/**
 * @brief Compute the D series check byte.
 *
 * @param bytes  Bytes to sum.
 * @param n      Number of bytes.
 * @return The low byte of the sum of the @p n bytes at @p bytes.
 */
uint8_t hitec_d_sum(const uint8_t *bytes, size_t n);

/**
 * @brief Build a D series read request.
 *
 * @param id    Servo ID.
 * @param addr  Register address.
 * @param out   Receives `96 id addr 00 sum`.
 */
void hitec_d_read_frame(uint8_t id, uint8_t addr,
                        uint8_t out[HITEC_D_READ_LEN]);

/**
 * @brief Build a D series write request.
 *
 * @param id     Servo ID.
 * @param addr   Register address.
 * @param value  Data word, sent little endian.
 * @param out    Receives `96 id addr 02 lo hi sum`.
 */
void hitec_d_write_frame(uint8_t id, uint8_t addr, uint16_t value,
                         uint8_t out[HITEC_D_WRITE_LEN]);

/* --- transactions --------------------------------------------------------- */

/** @brief Kind of a transaction. */
typedef enum {
    /** Send 1 to HITEC_TX_MAX bytes, then receive. */
    HITEC_XFER_SEND = 0,
    /** Receive only; the first byte may be answered with one byte. */
    HITEC_XFER_LISTEN,
    /** Hold the line at one level for a time, then release it. */
    HITEC_XFER_LEVEL,
} hitec_kind_t;

/** @brief Bytes one transaction sends. */
#define HITEC_TX_MAX   8u
/** @brief Idle bit times between two sent bytes, at most. */
#define HITEC_GAP_MAX  6u
/** @brief Bytes one capture holds. */
#define HITEC_RX_MAX  16u
/** @brief Longest lead, window and level time, 4 s. */
#define HITEC_TIME_MAX_US 4000000u

/**
 * @brief One transaction.
 *
 * Outside a transaction the driver leaves the pad an input: classic with a
 * pull-down, D series with a pull-up.
 *
 * SEND: the line rests at its idle level for @p lead_us (classic: released;
 * D series: driven low, the inside-frame rest level), then the bytes go out
 * with @p gap_bits idle bit times between two of them.  Classic: the pad is
 * released at the start of the last byte's stop bit, where the receiver
 * starts.  D series: the last stop bit is sent, the pad is released, and
 * the receiver starts one bit time later (HITEC_D_SETTLE_BITS).  With
 * @p window_us 0 nothing is received.
 *
 * LISTEN: the receiver starts at once.  With @p answer set, the byte
 * @p answer_byte goes out at the end of the stop bit of the first byte
 * received.
 *
 * The receiver runs until @p window_us of waiting for a start bit have
 * passed or @p max_rx bytes have come.  The time spent inside bytes and in
 * the answer does not count against the window.
 *
 * LEVEL: the line is held at @p level (0 low, 1 high) for @p level_us and
 * then released.  An open-drain line drives low only.
 */
typedef struct {
    uint8_t  kind;          /**< hitec_kind_t */
    uint8_t  family;        /**< hitec_family_t */
    uint8_t  n_tx;          /**< SEND: 1 to HITEC_TX_MAX */
    uint8_t  gap_bits;      /**< SEND: 0 to HITEC_GAP_MAX */
    uint8_t  tx[HITEC_TX_MAX];
    uint8_t  max_rx;        /**< 1 to HITEC_RX_MAX; 0 with no window */
    uint8_t  answer;        /**< LISTEN: 1 sends answer_byte */
    uint8_t  answer_byte;
    uint8_t  level;         /**< LEVEL: 0 or 1 */
    uint32_t lead_us;       /**< SEND: 0 to HITEC_TIME_MAX_US */
    uint32_t window_us;     /**< SEND, LISTEN: 0 to HITEC_TIME_MAX_US */
    uint32_t level_us;      /**< LEVEL: 1 to HITEC_TIME_MAX_US */
} hitec_xfer_t;

/**
 * @brief Check that a transaction is one a driver runs.
 *
 * Every field is in its range, LISTEN has a window, LEVEL has a time.
 * Whether the drive can make the level is the driver's check.
 *
 * @param x  Transaction to check.
 * @return true when @p x is valid.
 */
bool hitec_xfer_valid(const hitec_xfer_t *x);

/* --- classic transactions ------------------------------------------------- */

/**
 * @brief Idle bit times between two classic command bytes.
 *
 * 11 bit times start to start, 572.9 us.  The HPP-21 Plus sends 572 us start
 * to start, the HFP-30 521 us.
 */
#define HITEC_C_GAP_BITS 1u
/**
 * @brief Bit times from the start of a classic command byte to the receiver's
 *        start.
 *
 * The receiver starts at the start of the last byte's stop bit, 9 bit times
 * after the start of that byte.
 */
#define HITEC_C_ARM_BITS 9u
/**
 * @brief Accepted window for the first classic reply byte, lower bound, in ns.
 *
 * The first reply byte's start bit, from the start of the master's last
 * byte.  Measured 508 to 517 us after reads, up to 614 us after writes
 * and after the first read of a session; accepted 494.8 us (the middle of
 * the master's stop bit) to 1 ms.
 */
#define HITEC_C_REPLY_MIN_NS  494792u
/** @brief Accepted window for the first classic reply byte, upper bound, in ns. */
#define HITEC_C_REPLY_MAX_NS 1000000u
/** @brief Classic receive window after a command, waiting time, in us. */
#define HITEC_C_WINDOW_US 1500u
/** @brief Bytes of a classic reply. */
#define HITEC_C_REPLY_LEN 2u

/**
 * @brief Build a classic command and its 2-byte reply.
 *
 * 4 bytes, HITEC_C_GAP_BITS between them, HITEC_C_WINDOW_US, 2 bytes.
 *
 * @param cmd  Command byte.
 * @param p1   First parameter byte.
 * @param p2   Second parameter byte.
 * @param out  Receives the transaction.
 */
void hitec_c_xfer(uint8_t cmd, uint8_t p1, uint8_t p2, hitec_xfer_t *out);

/* --- D series transactions ------------------------------------------------ */

/**
 * @brief Time the line is driven low before the first D series start bit, in us.
 *
 * As the HFP-30 does from a released line: 88 us.
 */
#define HITEC_D_LEAD_US 88u
/**
 * @brief Released bit times between the last stop bit and the receiver's
 *        start.
 */
#define HITEC_D_SETTLE_BITS 1u
/**
 * @brief Accepted window for the D series answer, lower bound, in ns.
 *
 * The answer's first start bit, from the end of the request's last stop
 * bit.  Measured 5.9 to 6.5 ms with the HFP-30, and 5.93 to 6.52 ms over
 * 153 reads by PRECHARGE (D485HW, 2026-10-20); 15.2 ms is reported with
 * another programmer.  Accepted 2 ms to 20 ms.
 */
#define HITEC_D_ANSWER_MIN_NS  2000000u
/** @brief Accepted window for the D series answer, upper bound, in ns. */
#define HITEC_D_ANSWER_MAX_NS 20000000u
/** @brief D series receive window after a read, waiting time, in us. */
#define HITEC_D_WINDOW_US 25000u
/**
 * @brief Bytes a D series read receives.
 *
 * The 7 of the answer and the byte 0x00 the D485HW sends after it.
 */
#define HITEC_D_READ_RX 8u

/**
 * @brief Build a D series read and its answer.
 *
 * Lead, 5 bytes back to back, HITEC_D_WINDOW_US, HITEC_D_READ_RX bytes.
 *
 * @param id    Servo ID.
 * @param addr  Register address.
 * @param out   Receives the transaction.
 */
void hitec_d_read_xfer(uint8_t id, uint8_t addr, hitec_xfer_t *out);

/**
 * @brief Build a D series write.
 *
 * Lead, 7 bytes back to back, nothing received.
 *
 * @param id     Servo ID.
 * @param addr   Register address.
 * @param value  Data word, sent little endian.
 * @param out    Receives the transaction.
 */
void hitec_d_write_xfer(uint8_t id, uint8_t addr, uint16_t value,
                        hitec_xfer_t *out);

/* --- listen and level ----------------------------------------------------- */

/**
 * @brief Build a LISTEN transaction.
 *
 * With @p answer set the first byte is answered with @p answer_byte.
 *
 * @param family       hitec_family_t value.
 * @param window_us    Waiting time for a start bit, in us.
 * @param max_rx       Bytes to receive, at most HITEC_RX_MAX.
 * @param answer       true to answer the first received byte.
 * @param answer_byte  Byte sent as the answer.
 * @param out          Receives the transaction; untouched when false is
 *                     returned.
 * @return false when the result would not be valid.
 */
bool hitec_listen_xfer(uint8_t family, uint32_t window_us, uint8_t max_rx,
                       bool answer, uint8_t answer_byte, hitec_xfer_t *out);

/**
 * @brief Build a LEVEL transaction.
 *
 * @param family   hitec_family_t value.
 * @param level    Line level to hold, 0 low or 1 high.
 * @param time_us  Hold time, in us.
 * @param out      Receives the transaction; untouched when false is returned.
 * @return false when the result would not be valid.
 */
bool hitec_level_xfer(uint8_t family, uint8_t level, uint32_t time_us,
                      hitec_xfer_t *out);

/* --- capture -------------------------------------------------------------- */

/** @brief One received byte with the time of its start bit. */
typedef struct {
    /** The start bit's leading edge, in ns from the receiver's start: the
     *  release after a SEND (classic: the start of the last stop bit; D
     *  series: HITEC_D_SETTLE_BITS after the end of the last stop bit), the
     *  start of a LISTEN. */
    uint32_t at_ns;
    uint8_t  value;
    uint8_t  framing;   /**< 1: the stop bit was sampled at the start level */
} hitec_rx_byte_t;

/** @brief What the receiver took, in the order the bytes came. */
typedef struct {
    hitec_rx_byte_t byte[HITEC_RX_MAX];
    uint32_t window_ns;  /**< the window as the transaction asked */
    uint8_t  family;
    uint8_t  n;          /**< bytes held */
    uint8_t  full;       /**< 1: max_rx bytes came and ended the receive */
    uint8_t  answered;   /**< 1: the answer went out whole after byte 0;
                          *   0 when a restart cut it */
    uint8_t  overflow;   /**< 1: a stamp no count gives, or the driver ended
                          *   a receiver that ran past its time */
    uint8_t  aborted;    /**< 1: the transaction was cut */
} hitec_capture_t;

/* --- decoders ------------------------------------------------------------- */

/**
 * @brief Result of a decoder.
 *
 * One code per failed check, in the order the decoders check.
 */
typedef enum {
    HITEC_RX_OK = 0,
    HITEC_RX_ERR_ARG,       /**< NULL, another family, an unknown expectation */
    HITEC_RX_ERR_ABORTED,   /**< the transaction was cut */
    HITEC_RX_ERR_OVERFLOW,  /**< the capture is not whole */
    HITEC_RX_ERR_SILENT,    /**< no byte */
    HITEC_RX_ERR_EARLY,     /**< the first byte before the accept window */
    HITEC_RX_ERR_LATE,      /**< the first byte after it */
    HITEC_RX_ERR_NAK,       /**< classic: `-` and nothing after it */
    HITEC_RX_ERR_LENGTH,    /**< fewer bytes than the reply has; classic: more */
    HITEC_RX_ERR_FRAMING,   /**< a byte of the reply with a bad stop bit */
    HITEC_RX_ERR_GAP,       /**< a byte of the reply too soon or too late
                             *   after the one before */
    HITEC_RX_ERR_ACK,       /**< classic: no `+` where the reply has one */
    HITEC_RX_ERR_HEADER,    /**< D: not 0x69, or a length byte other than 2 */
    HITEC_RX_ERR_ADDRESS,   /**< D: the answer of another register */
    HITEC_RX_ERR_SUM,       /**< D: the sum does not fit */
} hitec_rx_t;

/**
 * @brief Accepted gap between two reply bytes, lower bound, in half bit times.
 *
 * A byte of a reply starts 9.5 to 15 bit times after the start of the one
 * before.  Measured: classic 516 to 528 us (10 bit times are 520.8 us),
 * D series 86 to 87 us (86.8 us).
 */
#define HITEC_NEXT_MIN_HALF_BITS 19u
/** @brief Accepted gap between two reply bytes, upper bound, in half bit times. */
#define HITEC_NEXT_MAX_HALF_BITS 30u

/** @brief Classic reply shape that a decoder expects. */
typedef enum {
    HITEC_C_EXPECT_VALUE = 0,   /**< value, `+` */
    HITEC_C_EXPECT_ACK,         /**< `+ +` */
    HITEC_C_EXPECT_WORD,        /**< high, low */
} hitec_c_expect_t;

/**
 * @brief Look up the reply a classic command has.
 *
 * @param cmd  Command byte.
 * @param out  Receives the expected reply shape.
 * @return false for a command byte this module does not know.
 */
bool hitec_c_expect_of(uint8_t cmd, hitec_c_expect_t *out);

/** @brief Decoded reply. */
typedef struct {
    uint16_t value;     /**< classic: the value or the word; D: the data */
    uint8_t  id;        /**< D: the answer's second byte, not checked */
    uint8_t  n;         /**< bytes in the capture */
    /** The first byte's start bit, from the start of the master's last
     *  byte (classic), the end of its last stop bit (D series) or the start
     *  of the listen. */
    uint32_t delay_ns;
} hitec_reply_t;

/**
 * @brief Decode a classic reply.
 *
 * Checks in this order: the arguments, abort, overflow, a byte at all, the
 * first byte's time (HITEC_C_REPLY_MIN_NS to HITEC_C_REPLY_MAX_NS), `-` alone,
 * 2 bytes, both stop bits, the second byte's time, the `+` bytes.  @p out is
 * filled in every case as far as the checks went.
 *
 * @param cap     Capture to decode.
 * @param expect  Reply shape to check against.
 * @param out     Receives the decoded reply.
 * @return HITEC_RX_OK, or the code of the first failed check.
 */
hitec_rx_t hitec_c_decode(const hitec_capture_t *cap, hitec_c_expect_t expect,
                          hitec_reply_t *out);

/**
 * @brief Decode a D series answer to a read.
 *
 * Checks in this order: the arguments, abort, overflow, a byte at all, the
 * first byte's time (HITEC_D_ANSWER_MIN_NS to HITEC_D_ANSWER_MAX_NS), 7 bytes,
 * their stop bits, their times, the header and length byte, the address, the
 * sum.  Bytes after the seventh are not checked: the D485HW sends 0x00 after
 * every answer, and the release of the line reads as a byte with a bad stop
 * bit.  The ID byte is reported and not checked.
 *
 * @param cap   Capture to decode.
 * @param addr  Register address the read asked for.
 * @param out   Receives the decoded reply.
 * @return HITEC_RX_OK, or the code of the first failed check.
 */
hitec_rx_t hitec_d_decode(const hitec_capture_t *cap, uint8_t addr,
                          hitec_reply_t *out);

/**
 * @brief Decode a listen for one classic byte.
 *
 * Checks the byte @p want, as the classic entry's `+`: abort, overflow, a
 * byte at all, its stop bit, its value, no byte after it.  delay_ns is the
 * first byte's time from the start of the listen.  A listen for more bytes
 * reads them from the capture.
 *
 * @param cap   Capture to decode.
 * @param want  Byte expected as the first received byte.
 * @param out   Receives the decoded reply.
 * @return HITEC_RX_OK, or the code of the first failed check.
 */
hitec_rx_t hitec_listen_decode(const hitec_capture_t *cap, uint8_t want,
                               hitec_reply_t *out);

/* --- driver --------------------------------------------------------------- */

/**
 * @brief A pin driver.
 *
 * Non-blocking; there is no delay function.
 *
 * - start() begins @p xfer and returns at once; false when the line is not
 *   open, a transaction runs, @p xfer is not valid or the line cannot make
 *   it (another family, a level its drive does not make).
 * - poll() returns false while the transaction runs.  Once it has ended it
 *   fills @p out and returns true, once.
 * - now_us() is a free-running microsecond clock.  It may wrap; compare
 *   times with hitec_elapsed_us().
 */
typedef struct {
    void *ctx;
    bool (*start)(void *ctx, const hitec_xfer_t *xfer);
    bool (*poll)(void *ctx, hitec_capture_t *out);
    uint32_t (*now_us)(void *ctx);
} hitec_driver_t;

/**
 * @brief Elapsed time on the wrapping microsecond clock.
 *
 * @param now    Current time, in us.
 * @param since  Earlier time, in us.
 * @return Microseconds from @p since to @p now on a clock that wraps at 2^32.
 */
uint32_t hitec_elapsed_us(uint32_t now, uint32_t since);

/**
 * @brief Check whether a span has passed on the wrapping clock.
 *
 * @param now      Current time, in us.
 * @param since    Earlier time, in us.
 * @param span_us  Span to test, in us.
 * @return true when @p span_us or more have passed from @p since to @p now,
 *         also across the wrap of the clock.  A span is shorter than 2^31 us.
 */
bool hitec_due(uint32_t now, uint32_t since, uint32_t span_us);

#ifdef __cplusplus
}
#endif

#endif /* HITEC_WIRE_H */
