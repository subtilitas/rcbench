/**
 * @file ew_wire.h
 * @brief One transaction on the half-duplex signal wire, and the driver that
 *        runs it.
 *
 * A transaction sends bytes (8N1, least significant bit first, idle high) and
 * then receives until a byte count is reached or a wait runs out.  The driver
 * interface has no delay function: start() and poll() return at once.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef EW_WIRE_H
#define EW_WIRE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The bit time of protocol A, ns. */
#define EW_BIT_A_NS 52000u
/** @brief The bit time at 38400baud, ns, rounded. */
#define EW_BIT_TEXT_NS 26042u
/** @brief The most bytes a transaction sends: a 6-byte announcement, 256
 *         bytes of data and their 2 CRC (cyclic redundancy check) bytes. */
#define EW_TX_MAX 264u
/** @brief The most bytes a transaction receives: 256 data bytes, 2 CRC bytes
 *         and the answer. */
#define EW_RX_MAX 259u
/** @brief The most bytes a transaction at 38400baud receives: a `show` is at
 *         most about 980B, and the margin keeps a longer reply whole. */
#define EW_RX_TEXT_MAX 1536u
/** @brief The most idle bit times between an announcement and its data: 10 x
 *         52us reach the 500us that serve every firmware.  Bytes of one message
 *         go with no gap: on AM32 more than 234us of idle line ends a message. */
#define EW_PAUSE_BITS_MAX 10u
/** @brief The longest wait for a first byte, us. */
#define EW_FIRST_US_MAX 4000000u
/** @brief The longest wait between two received bytes, us. */
#define EW_GAP_US_MAX 100000u

/** @brief The bit rates of the wire. */
typedef enum {
    EW_RATE_A = 0,  /**< 52us a bit                 */
    EW_RATE_TEXT,   /**< 38400baud                  */
    EW_RATES
} ew_rate_t;

/** @brief What the pad does while no transaction runs. */
typedef enum {
    EW_LINE_PARK = 0, /**< an input with its pull-down                    */
    EW_LINE_HIGH,     /**< driven high                                    */
    EW_LINE_UP,       /**< an input with its pull-up: between exchanges   */
    EW_LINE_LOW,      /**< driven low                                     */
} ew_line_t;

/** @brief One transaction. */
typedef struct {
    uint8_t        rate;       /**< an ew_rate_t                            */
    uint8_t        pause_bits; /**< idle bit times after @c n_head bytes     */
    uint16_t       n_tx;       /**< bytes to send, 1 to EW_TX_MAX            */
    uint16_t       n_head;     /**< bytes before the pause, 0 for no pause   */
    uint16_t       max_rx;     /**< bytes to receive, 0 to EW_RX_MAX; to
                                     EW_RX_TEXT_MAX at 38400baud             */
    uint32_t       first_us;   /**< the wait for the first start bit         */
    uint32_t       gap_us;     /**< the wait for each later start bit        */
    const uint8_t *tx;         /**< the bytes, valid until the capture       */
    uint8_t       *rx;         /**< takes the bytes, NULL with max_rx 0      */
} ew_xfer_t;

/** @brief What a transaction brought back. */
typedef struct {
    uint16_t n;        /**< bytes received                                 */
    uint16_t bad_stop; /**< 1 + the first byte with a low stop bit, 0 none */
    bool     aborted;  /**< the driver cut it                              */
    bool     overflow; /**< the capture is not whole                       */
} ew_capture_t;

/**
 * @brief The pin driver.
 *
 * - start() takes the pad from whatever line() left it at, sends the bytes,
 *   releases the pad within one state machine cycle
 *   of the last stop bit and receives; false when it cannot start.  The pad
 *   rests as EW_LINE_UP afterwards, as EW_LINE_PARK after a capture marked
 *   aborted or overflow.
 * - poll() returns false while the transaction runs and fills the capture
 *   once at its end.
 * - abort() cuts a running transaction at once; no capture follows.
 * - line() sets the pad; never called while a transaction runs.
 * - now_us() is a free-running clock that may wrap.
 */
typedef struct {
    void *ctx;                                            /**< the driver's */
    bool (*start)(void *ctx, const ew_xfer_t *x);         /**< see above    */
    bool (*poll)(void *ctx, ew_capture_t *out);           /**< see above    */
    void (*abort)(void *ctx);                             /**< see above    */
    void (*line)(void *ctx, ew_line_t level);             /**< see above    */
    uint32_t (*now_us)(void *ctx);                        /**< see above    */
} ew_driver_t;

/**
 * @brief Whether a driver may be handed this transaction.
 * @param x the transaction, may be NULL
 * @return true when every field is inside its limit
 */
bool ew_xfer_valid(const ew_xfer_t *x);

/**
 * @brief The longest a valid transaction can take: every byte sent, then the
 *        first wait, then for each byte a wait and 12 bit times.
 * @param x a transaction ew_xfer_valid() accepts
 * @return the time in us
 */
uint32_t ew_xfer_span_us(const ew_xfer_t *x);

/**
 * @brief The time between two clock readings, across the clock's wrap.
 * @param now  the later reading, us
 * @param then the earlier reading, us
 * @return the difference, us
 */
uint32_t ew_elapsed_us(uint32_t now, uint32_t then);

/**
 * @brief Whether more than @p wait_us passed since @p then: one tick more
 *        than the wait, since a reading lies up to one tick behind the time.
 * @param now     the clock now, us
 * @param then    the start, us
 * @param wait_us the wait, us
 * @return true when the wait has passed
 */
bool ew_due(uint32_t now, uint32_t then, uint32_t wait_us);

#ifdef __cplusplus
}
#endif

#endif /* EW_WIRE_H */
