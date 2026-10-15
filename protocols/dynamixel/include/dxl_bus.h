/**
 * @file dxl_bus.h
 * @brief One Protocol 2.0 transaction at a time on a half-duplex bus of
 *        ROBOTIS Dynamixel servos, and the ID scan built on it.  Caller
 *        driven: nothing here waits.
 *
 * Implements the published Protocol 2.0 of ROBOTIS Dynamixel servos.
 * ROBOTIS and Dynamixel are names or trademarks of their owner; they are
 * used here only to say which servos this module works with (README.md,
 * "Trademarks").
 *
 * A transaction sends one instruction packet and, where one is due, takes
 * one status packet back.  dxl_bus_step() polls the driver, parses what came
 * and ends the transaction on a whole reply, a broken one or the deadline:
 *
 *   deadline = DXL_LEAD_BITS + DXL_TX_BITS_PER_BYTE x bytes sent  (bit times)
 *            + the return delay the bus assumes
 *            + 10 x bytes of the longest reply, stuffing included  (bit times)
 *            + DXL_MARGIN_US
 *
 * from the start() call.  The return delay is the servo's Return Delay Time
 * (address 9, 2 us a step, 500 us by default); the bus assumes 508 us, the
 * longest, until the caller sets the servo's own.  The servo's processing
 * time is not published; DXL_MARGIN_US is a choice and not measured.
 *
 * A reply is accepted when it is a status packet, its CRC fits, it comes from
 * the ID asked (after a write to ID, also from the new ID) and it carries
 * the parameter count the instruction asks for.  An ERR byte with an error
 * number ends the transaction with DXL_ERR_STATUS; the alert bit alone does
 * not.  Every check that fails has its own result code.
 *
 * The module includes nothing outside the C standard headers, uses no heap
 * and no floating point, and holds no global state: two buses on two drivers
 * run side by side.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef DXL_BUS_H
#define DXL_BUS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dxl_packet.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Bit times of idle high a driver sends at most before the first start
 *  bit of a packet. */
#define DXL_LEAD_BITS          20u
/** Bit times a driver takes at most per byte sent: 10, and stop bits
 *  stretched by up to 1 bit time. */
#define DXL_TX_BITS_PER_BYTE   11u
/** Time added to every deadline for the servo's processing and the
 *  driver's start.  Chosen, not measured. */
#define DXL_MARGIN_US          1000u
/** The longest Return Delay Time: 254 steps of 2 us. */
#define DXL_RETURN_DELAY_MAX_US 508u

/**
 * The pin driver.  One transaction at a time.
 *
 * - set_baud(): the bit rate for the next transactions; false when the
 *   driver cannot make it.  Not called while a transaction runs.
 * - start(): send @p n bytes 8N1 at the rate, idle high, then release the
 *   line and collect what comes back.  Returns at once; false when it
 *   cannot start.  A driver sends at most DXL_LEAD_BITS of idle high before
 *   the first start bit and DXL_TX_BITS_PER_BYTE bit times per byte.
 * - poll(): copy up to @p cap bytes received since the last poll into
 *   @p out, return the count.  Bytes with a broken stop bit are dropped.
 * - stop(): end the transaction; the line stays released.
 * - now_us(): a free-running microsecond clock.  It may wrap.
 */
typedef struct {
    void *ctx;
    bool (*set_baud)(void *ctx, uint32_t baud);
    bool (*start)(void *ctx, const uint8_t *tx, size_t n);
    size_t (*poll)(void *ctx, uint8_t *out, size_t cap);
    void (*stop)(void *ctx);
    uint32_t (*now_us)(void *ctx);
} dxl_driver_t;

typedef enum {
    DXL_OK = 0,
    DXL_BUSY,            /**< the transaction runs: call dxl_bus_step() */
    DXL_ERR_ARG,         /**< refused before anything was sent */
    DXL_ERR_STATE,       /**< a transaction runs */
    DXL_ERR_DRIVER,      /**< the driver refused the rate or the start */
    DXL_ERR_TIMEOUT,     /**< no byte by the deadline */
    DXL_ERR_TRUNCATED,   /**< bytes, but no whole packet by the deadline */
    DXL_ERR_CRC,         /**< a whole packet whose CRC does not fit */
    DXL_ERR_LENGTH,      /**< Length too short or too long */
    DXL_ERR_STUFFING,    /**< broken byte stuffing */
    DXL_ERR_NOT_STATUS,  /**< a packet that is not a status packet */
    DXL_ERR_ID,          /**< a status packet from another ID */
    DXL_ERR_PARAMS,      /**< not the parameter count asked for */
    DXL_ERR_STATUS,      /**< the ERR byte has an error number */
    DXL_ERR_ABORTED,     /**< dxl_bus_abort() */
} dxl_res_t;

typedef struct {
    dxl_driver_t drv;
    uint32_t baud;               /**< bit/s; 0 before the first set */
    uint16_t return_delay_us;    /**< assumed; DXL_RETURN_DELAY_MAX_US at init */
    uint8_t  status_return;      /**< Status Return Level assumed, 2 at init */
    uint8_t  running;
    uint8_t  id;                 /**< asked */
    uint8_t  alt_id;             /**< also accepted, when has_alt */
    uint8_t  has_alt;
    uint8_t  reply;              /**< a reply is due */
    uint8_t  linger;             /**< collect until the deadline after a reply */
    uint8_t  got;                /**< a whole reply is in */
    uint16_t want_params;        /**< parameter count of the reply */
    uint32_t t_start;            /**< now_us() at start() */
    uint32_t timeout_us;         /**< deadline from t_start */
    uint32_t t_reply_us;         /**< reply complete, from t_start, as polled */
    uint32_t extra;              /**< bytes after the reply, linger only */
    uint32_t noise;              /**< bytes before a header, dropped */
    dxl_res_t result;
    uint16_t n_tx;
    uint16_t n_rx;
    dxl_packet_t status;         /**< the reply of the last transaction */
    uint8_t tx[DXL_WIRE_MAX];
    uint8_t rx[DXL_WIRE_MAX];
} dxl_bus_t;

/** Bus with @p driver, no rate set, no transaction. */
void dxl_bus_init(dxl_bus_t *bus, const dxl_driver_t *driver);

/** Set the rate: one of the 8 of the Baud Rate register.  DXL_ERR_ARG for
 *  another rate, DXL_ERR_STATE while a transaction runs, DXL_ERR_DRIVER
 *  when the driver refuses it. */
dxl_res_t dxl_bus_set_baud(dxl_bus_t *bus, uint32_t baud);

/** The Return Delay Time to assume, in 2 us steps (0 to 254). */
void dxl_bus_set_return_delay(dxl_bus_t *bus, uint8_t raw);

/** The Status Return Level to assume: 0 PING only, 1 also READ, 2 all. */
void dxl_bus_set_status_return(dxl_bus_t *bus, uint8_t level);

/** The deadline of a transaction, in us from start(): see the file comment. */
uint32_t dxl_deadline_us(uint32_t baud, uint32_t return_delay_us,
                         size_t n_tx, size_t n_reply_max);

/** The longest reply, in wire bytes, that carries @p n_params parameters. */
size_t dxl_reply_max(size_t n_params);

/* Transactions.  Each returns DXL_BUSY when the packet went to the driver,
 * then dxl_bus_step() runs it; or an error with nothing sent. */
dxl_res_t dxl_bus_ping(dxl_bus_t *bus, uint8_t id);
dxl_res_t dxl_bus_read(dxl_bus_t *bus, uint8_t id, uint16_t addr,
                       uint16_t len);
/** A write to address 7, 1 byte, takes a reply from the old or the new ID.
 *  A write to the broadcast ID gets no reply. */
dxl_res_t dxl_bus_write(dxl_bus_t *bus, uint8_t id, uint16_t addr,
                        const uint8_t *data, size_t n);
dxl_res_t dxl_bus_reg_write(dxl_bus_t *bus, uint8_t id, uint16_t addr,
                            const uint8_t *data, size_t n);
dxl_res_t dxl_bus_action(dxl_bus_t *bus, uint8_t id);
dxl_res_t dxl_bus_reboot(dxl_bus_t *bus, uint8_t id);
/** See dxl_factory_reset(): @p confirm must be DXL_FACTORY_RESET_CONFIRM. */
dxl_res_t dxl_bus_factory_reset(dxl_bus_t *bus, uint8_t id, uint8_t option,
                                uint32_t confirm);
/** SYNC_WRITE: no reply. */
dxl_res_t dxl_bus_sync_write(dxl_bus_t *bus, uint16_t addr, uint16_t len,
                             const uint8_t *ids, const uint8_t *data,
                             size_t n_ids);

/** Run the transaction: DXL_BUSY while it runs, then its result once and
 *  after that again until the next transaction starts.  Without a
 *  transaction since init: DXL_ERR_STATE. */
dxl_res_t dxl_bus_step(dxl_bus_t *bus);

/** End a running transaction with DXL_ERR_ABORTED. */
void dxl_bus_abort(dxl_bus_t *bus);

bool dxl_bus_busy(const dxl_bus_t *bus);

/* ------------------------------------------------------------------ scan */

/** Servos one scan lists at most; more are counted in `overflow`. */
#define DXL_SCAN_FOUND_MAX 16u

/** Found flags. */
#define DXL_FOUND_GARBLED 0x01u /**< bytes came, no valid reply: two servos
                                     with one ID, or a wrong rate */
#define DXL_FOUND_EXTRA   0x02u /**< bytes after a valid reply */
#define DXL_FOUND_ALERT   0x04u /**< the ERR byte's alert bit */

typedef struct {
    uint8_t  id;
    uint8_t  baud_index;  /**< Baud Rate register index of the rate */
    uint8_t  firmware;
    uint8_t  flags;       /**< DXL_FOUND_* */
    uint16_t model;       /**< Model Number; 0 when garbled */
    uint8_t  result;      /**< dxl_res_t of the ping */
} dxl_found_t;

typedef struct {
    dxl_bus_t *bus;
    uint8_t first;
    uint8_t last;
    uint8_t id;            /**< the ID pinged now */
    uint8_t baud_mask;     /**< bit i: scan at dxl_baud_rate(i) */
    uint8_t baud_index;    /**< the rate scanned now */
    uint8_t refused_mask;  /**< rates the driver refused, skipped */
    uint8_t running;
    uint8_t n_found;
    uint16_t overflow;     /**< found beyond DXL_SCAN_FOUND_MAX */
    uint16_t pinged;       /**< pings sent */
    dxl_res_t result;
    dxl_found_t found[DXL_SCAN_FOUND_MAX];
} dxl_scan_t;

/**
 * Ping every ID from @p first to @p last at every rate of @p baud_mask, in
 * rate index order, ID by ID.  @p first and @p last are 0 to 252, first <=
 * last; the broadcast ID 254 and the non-IDs 253 and 255 are refused
 * (DXL_ERR_ARG), as are an empty mask and a running transaction
 * (DXL_ERR_STATE).  Each ping waits for its deadline after a reply, so
 * bytes from a second servo show as DXL_FOUND_EXTRA.  The bus is left at
 * the last rate scanned.
 */
dxl_res_t dxl_scan_start(dxl_scan_t *scan, dxl_bus_t *bus, uint8_t first,
                         uint8_t last, uint8_t baud_mask);

/** DXL_BUSY while the scan runs, then DXL_OK, or DXL_ERR_DRIVER when the
 *  driver refused a start.  A rate the driver refuses is skipped and set
 *  in `refused_mask`. */
dxl_res_t dxl_scan_step(dxl_scan_t *scan);

/** Stop the scan; the bus's transaction is aborted. */
void dxl_scan_abort(dxl_scan_t *scan);

#ifdef __cplusplus
}
#endif

#endif /* DXL_BUS_H */
