/*
 * Feetech SCS and STS serial bus servos: one bus master, driven by the
 * caller's clock.
 *
 * Implements the serial bus protocol of Feetech SCS and STS servos.
 * Feetech is a name or trademark of its owner; it is used here only to say
 * which servos this module works with (README.md, "Trademarks").
 *
 * The session runs one operation at a time on one wire and, between
 * operations, the periodic read of one servo's live block (the watch):
 *
 *   SCAN        a PING to each ID of a range; an answer from the ID asked
 *               marks it found.  Answers that do not decode are counted:
 *               two servos with one ID answer at once.
 *   READ        1 to 16 bytes from one servo.
 *   WRITE       1 to 16 bytes to one servo, answered by a status frame.
 *               A servo whose response level (register 8) is 0 does not
 *               answer a WRITE: FEETECH_RES_NO_REPLY.
 *   WRITE_EE    a write kept over a power cycle: the lock flag to 0, the
 *               write, the lock flag to 1, a read of the bytes written.
 *               A write that moves the servo's ID or baud rate is
 *               followed: the lock and the read go to the new ID at the
 *               new rate.  When neither the write nor that lock is
 *               answered, the write counts as lost: the line goes back to
 *               the old rate, the lock to the old ID, and the operation
 *               ends with the write's result.  A write of the lock flag
 *               alone is that one write and its read.
 *   TORQUE_OFF  the torque switch 0 to the broadcast ID; no answer.
 *
 * feetech_bus_stop() ends the operation and the watch.  A frame on the
 * wire is finished; no frame of the operation follows.  Then the torque
 * switch goes to 0 by broadcast, and a servo the operation left with its
 * lock flag at 0 gets it at 1.  When the frame on the wire was an EEPROM
 * write that moves the servo's rate, the line follows it and the torque
 * switch goes to 0 at both rates; one that moves the ID gets the lock at
 * the new ID.  A frame of the stop the driver does not start is tried
 * FEETECH_BUS_STOP_TRIES times, one pass apart; then the stop goes on
 * without it and stop_res reads FEETECH_RES_DRIVER.
 *
 * Timing.  A transaction is the frame out, then bytes in until the reply
 * is complete or FEETECH_BUS_REPLY_US plus twice the reply's own time has
 * passed after the frame.  FEETECH_BUS_GAP_US separate two transactions.
 * Neither is measured on a servo.  A driver that has not ended a
 * transaction FEETECH_BUS_GRACE_US after it should have ends the operation
 * with FEETECH_RES_DRIVER.
 *
 * The session never waits: feetech_bus_step() polls the driver, starts
 * what is due at @p now_us and returns.  Call it every 1 ms or faster;
 * the clock may wrap.
 *
 * The module includes nothing outside the C standard headers and holds no
 * global state.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FEETECH_BUS_H
#define FEETECH_BUS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "feetech_reg.h"
#include "feetech_wire.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Time the reply has after the frame's last stop bit, beyond its own
 *  bytes.  SCS servos wait up to 508 us (register 7); what an STS servo
 *  waits is not measured. */
#define FEETECH_BUS_REPLY_US 1000u
/** Silence between the end of one transaction and the next frame. */
#define FEETECH_BUS_GAP_US   100u
/** A transaction the driver has not ended this long after it should have
 *  ended the operation with FEETECH_RES_DRIVER. */
#define FEETECH_BUS_GRACE_US 5000u
/** The watch reads its servo this often. */
#define FEETECH_BUS_WATCH_US 50000u
/** Starts a frame of the stop gets before the stop goes on without it. */
#define FEETECH_BUS_STOP_TRIES 3u

/** Bytes a driver keeps of one reply window. */
#define FEETECH_CAPTURE_MAX FEETECH_FRAME_MAX

/** What a driver hands back of one transaction. */
typedef struct {
    uint8_t bytes[FEETECH_CAPTURE_MAX];
    uint8_t n;
    uint8_t framing;   /**< bytes whose stop bit was low                 */
    uint8_t lost;      /**< more bytes came than were kept               */
} feetech_capture_t;

/** The pin driver. */
typedef struct {
    void *ctx;
    /**
     * Send @p n_tx bytes, then keep the bytes that come until @p n_rx came
     * or @p wait_us after the last stop bit.  @p n_rx counts the reply's
     * bytes only: a driver that hears its own frame hands it back ahead of
     * the reply and keeps @p n_tx + @p n_rx bytes.  @p n_rx 0: no reply;
     * the transaction ends with the last stop bit.  Returns at once; false
     * when it cannot start.
     */
    bool (*start)(void *ctx, const uint8_t *tx, unsigned n_tx, unsigned n_rx,
                  uint32_t wait_us);
    /** False while the transaction runs; true once at its end, with what
     *  came. */
    bool (*poll)(void *ctx, feetech_capture_t *out);
    /** The line to @p baud for the next transaction; false when the
     *  driver cannot make it.  Never called while a transaction runs. */
    bool (*set_baud)(void *ctx, uint32_t baud);
} feetech_driver_t;

/** Operations. */
typedef enum {
    FEETECH_OP_NONE = 0,
    FEETECH_OP_SCAN,
    FEETECH_OP_READ,
    FEETECH_OP_WRITE,
    FEETECH_OP_WRITE_EE,
    FEETECH_OP_TORQUE_OFF,
    FEETECH_OP_COUNT
} feetech_op_t;

/** How an operation went, and why one did not start. */
typedef enum {
    FEETECH_RES_OK = 0,
    FEETECH_RES_BUSY = 1,        /**< an operation runs                  */
    FEETECH_RES_NO_REPLY = 2,    /**< no byte came                       */
    FEETECH_RES_TRUNCATED = 3,   /**< a reply shorter than its LEN       */
    FEETECH_RES_CHECKSUM = 4,    /**< a reply whose checksum is wrong    */
    FEETECH_RES_WRONG_ID = 5,    /**< a good reply from another ID       */
    FEETECH_RES_LENGTH = 6,      /**< a good reply with another count of
                                      bytes than asked                   */
    FEETECH_RES_NOISE = 7,       /**< bytes and no frame                 */
    FEETECH_RES_FRAMING = 8,     /**< no frame and a stop bit low        */
    FEETECH_RES_VERIFY = 9,      /**< the bytes read back are not the
                                      bytes written                      */
    FEETECH_RES_DRIVER = 10,     /**< the driver did not start, did not
                                      end or did not take the baud rate */
    FEETECH_RES_ABORTED = 11,    /**< feetech_bus_stop()                 */
    FEETECH_RES_ARG = 12,        /**< an ID, address, count or range the
                                      operation does not take            */
    FEETECH_RES_COUNT
} feetech_res_t;

/** The session.  The caller keeps it; the fields are the session's. */
typedef struct {
    feetech_driver_t drv;
    uint32_t baud;
    uint32_t t_us;        /**< when the last transaction ended or started */
    uint32_t due_us;      /**< the watch's next read                      */
    uint32_t limit_us;    /**< the transaction in flight ends by then     */
    uint8_t  op;          /**< feetech_op_t running, or NONE              */
    uint8_t  last_op;     /**< the last one started                       */
    uint8_t  step;
    uint8_t  result;      /**< feetech_res_t of the last operation ended  */
    uint8_t  flight;      /**< 0 none, 1 the operation's, 2 the watch's,
                               3 the stop's                               */
    uint8_t  timed;       /**< t_us is meaningful                         */
    uint8_t  stop_due;    /**< bit 0 torque off, bit 1 lock owed, bit 2
                               the rate an EEPROM write moved            */
    uint8_t  follow_baud; /**< that rate's index                          */
    uint8_t  stop_res;    /**< FEETECH_RES_OK, or FEETECH_RES_DRIVER when
                               the last stop could not send a frame       */
    uint8_t  stop_tries;  /**< starts refused of the stop's next frame    */
    uint8_t  unlocked;    /**< a servo's lock flag is 0 by this session   */
    uint8_t  unlocked_id;
    uint8_t  unlocked_series;
    /* The operation's request. */
    uint8_t  id;
    uint8_t  addr;
    uint8_t  n;
    uint8_t  series;
    uint8_t  new_id;      /**< WRITE_EE: the ID after the write           */
    uint8_t  new_baud;    /**< WRITE_EE: the baud index after it; 0xFF same */
    uint8_t  step_res;    /**< WRITE_EE: how the write went               */
    uint8_t  old_id;      /**< WRITE_EE: the ID before a write that moves
                               the ID or the rate and went unanswered,
                               0xFF none                                  */
    uint8_t  write_res;   /**< that write's result                        */
    uint32_t old_baud;    /**< and the rate before it                     */
    uint8_t  data[FEETECH_DATA_MAX];
    /* The scan. */
    uint8_t  scan_from, scan_to, scan_at;
    uint8_t  found_n;
    uint8_t  bad_n;       /**< IDs whose answer did not decode            */
    uint8_t  found[32];   /**< bit n of byte k: ID 8 k + n answered       */
    /* What came back. */
    uint8_t  rx_id;       /**< the ID of the last good reply, 0xFF none   */
    uint8_t  rx_error;    /**< its ERR byte                               */
    uint8_t  read_n;
    uint8_t  read[FEETECH_DATA_MAX];
    /* The watch. */
    uint8_t  watch_id;    /**< 0xFF none                                  */
    uint8_t  watch_series;
    uint8_t  watch_res;   /**< feetech_res_t of its last read             */
    uint8_t  watch_error; /**< the ERR byte of its last good reply        */
    uint8_t  watch_now;   /**< the next read is due at once               */
    uint8_t  watch_drop;  /**< the read on the wire is for a watch since
                               changed: its reply is not this one's       */
    uint8_t  live_n;
    uint8_t  live[FEETECH_LIVE_STS];
    uint16_t watch_ok;    /**< reads that decoded, wrapping               */
    uint16_t watch_bad;   /**< reads that did not, wrapping               */
    uint8_t  want_id;
    uint8_t  want_params;
    feetech_frame_t tx;
    feetech_capture_t cap;
} feetech_bus_t;

/** Prepare a session at @p baud, no operation, no watch.  False for a
 *  driver without one of its functions. */
bool feetech_bus_init(feetech_bus_t *b, const feetech_driver_t *d,
                      uint32_t baud);

/** The line to another rate: FEETECH_RES_BUSY while an operation or a
 *  stop runs, a transaction is on the wire or a lock is owed (the lock
 *  goes at the rate its servo answers at: feetech_bus_stop() sends it),
 *  FEETECH_RES_ARG for 0, FEETECH_RES_DRIVER when the driver refuses it
 *  (the rate stays). */
feetech_res_t feetech_bus_set_baud(feetech_bus_t *b, uint32_t baud);

/** Start a scan of @p from to @p to.  FEETECH_RES_ARG for @p to above
 *  FEETECH_ID_MAX or below @p from. */
feetech_res_t feetech_bus_scan(feetech_bus_t *b, uint8_t from, uint8_t to);

feetech_res_t feetech_bus_read(feetech_bus_t *b, uint8_t id, uint8_t addr,
                               uint8_t n);

/** RAM or EEPROM alike: no lock is touched. */
feetech_res_t feetech_bus_write(feetech_bus_t *b, uint8_t id, uint8_t addr,
                                const uint8_t *data, uint8_t n);

/** A write kept over a power cycle, in @p series' table: an address below
 *  FEETECH_RAM_FIRST, or the lock flag alone.  A lock the session owes to
 *  another servo goes out first; when it is not answered the write ends
 *  with its result and nothing else is sent.  A write of the lock flag
 *  alone settles what the session owes that servo: 1 locks it, 0 leaves it
 *  open on purpose. */
feetech_res_t feetech_bus_write_ee(feetech_bus_t *b, feetech_series_t series,
                                   uint8_t id, uint8_t addr,
                                   const uint8_t *data, uint8_t n);

feetech_res_t feetech_bus_torque_off(feetech_bus_t *b);

/** Read @p id's live block every FEETECH_BUS_WATCH_US between operations;
 *  an ID above FEETECH_ID_MAX ends the watch.  The block holds the last
 *  read's bytes; a read that fails empties it (live_n 0). */
void feetech_bus_watch(feetech_bus_t *b, uint8_t id, feetech_series_t series);

/** End the operation and the watch; torque off by broadcast, and the lock
 *  flag at 1 again where an operation left it at 0. */
void feetech_bus_stop(feetech_bus_t *b);

/** One pass at @p now_us.  FEETECH_RES_BUSY while an operation or a stop
 *  runs, else the result of the last operation. */
feetech_res_t feetech_bus_step(feetech_bus_t *b, uint32_t now_us);

/** An operation or a stop runs. */
bool feetech_bus_busy(const feetech_bus_t *b);

/** A transaction is on the wire. */
bool feetech_bus_on_wire(const feetech_bus_t *b);

/** Whether @p id answered the last scan. */
bool feetech_bus_found(const feetech_bus_t *b, uint8_t id);

#ifdef __cplusplus
}
#endif

#endif /* FEETECH_BUS_H */
