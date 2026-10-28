/**
 * @file
 * @brief A transaction queue for one I2C (Inter-Integrated Circuit) bus
 *        master: descriptors, earliest-deadline selection, the not-before
 *        time, timeouts, the idle-line check, the abort, the bus clear and
 *        the trial after it.
 *
 * This file has no hardware access.  A transaction (i2c_txq_xfer_t) is an
 * address, up to I2C_TXQ_WR_MAX bytes to write, up to I2C_TXQ_RD_MAX bytes
 * to read after a repeated START, and a STOP.  It fits a 16-entry transmit
 * FIFO (first in, first out) whole, STOP included, so a driver hands it to
 * the controller in one go and the controller finishes it without the CPU.
 * A driver implements i2c_txq_ops_t and reports the end of each transaction
 * with i2c_txq_complete().
 *
 * One transaction is on the wire at a time.  Of the queued ones whose
 * not-before time has come, a hard one goes before every soft one; inside a
 * class the earliest deadline goes first, and on equal deadlines the one
 * submitted first.  A soft transaction whose deadline has passed is dropped
 * (I2C_TXQ_DROPPED), never sent late.  A hard one is sent late and flagged.
 * A soft transaction waits while its computed end plus guard_us lies after
 * the not-before time of a hard one already queued, so a hard one released
 * at its time finds the bus free.
 *
 * Times are microseconds on a 64-bit clock the caller passes in.  Every
 * comparison is made on the difference of two times, so the order holds
 * across the wrap of the clock as long as the times compared are less than
 * 2^63us apart.  "At once" is a not-before time of the present time, not
 * 0: seen from a clock past 2^63, 0 lies in the future.
 *
 * Nothing waits.  Every function returns at once; a step that needs time
 * (the idle-line settle, the pull-up probe, each half clock of the bus
 * clear) is taken on a later i2c_txq_poll().
 *
 * The module includes nothing outside the C standard headers and holds no
 * global state.  Every function of one queue is called from one context at
 * a time; a driver that completes from an interrupt masks that interrupt
 * around its other calls.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef I2C_TXQ_H
#define I2C_TXQ_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Transactions a queue holds, the one on the wire not counted. */
#define I2C_TXQ_DEPTH 16u
/**
 * @brief Polls the outermost queue call runs for done callbacks that polled
 *        inside it.
 */
#define I2C_TXQ_REPOLLS 16u
/** @brief Bytes a transaction writes at most: a register pointer and 4. */
#define I2C_TXQ_WR_MAX 5u
/** @brief Bytes a transaction reads at most. */
#define I2C_TXQ_RD_MAX 6u
/** @brief Entries of the controller's transmit FIFO a transaction fits. */
#define I2C_TXQ_FIFO 16u

/** @brief The bus clock the defaults are for: fast mode, in Hz. */
#define I2C_TXQ_HZ 400000u
/** @brief A transaction not finished this long after its start ends, in us. */
#define I2C_TXQ_TIMEOUT_US 1000u
/** @brief A line low before a start may take this long to rise, in us. */
#define I2C_TXQ_SETTLE_US 10u
/**
 * @brief A line still low after the settle gets the pad pull-ups for this
 *        long, in us: one that rises on them is held by nothing.
 */
#define I2C_TXQ_PROBE_US 50u
/** @brief Half a clock of the bus clear, in us: 100kHz. */
#define I2C_TXQ_CLEAR_HALF_US 5u
/** @brief SCL may be held low by a part this long per clear clock, in us. */
#define I2C_TXQ_CLEAR_STRETCH_US 100u
/**
 * @brief A completion stamped this much later than the computed end of the
 *        transaction takes the computed end, in us.
 */
#define I2C_TXQ_LATE_US 100u
/**
 * @brief A soft transaction does not start when its computed end plus this
 *        lies after a queued hard one's not-before time, in us: room for
 *        the controller's gaps, which the bus time does not count.  Chosen,
 *        not measured.
 */
#define I2C_TXQ_GUARD_US 20u

/**
 * @brief The bits of the DesignWare IC_TX_ABRT_SOURCE register the queue
 *        reads (RP2350 datasheet, I2C chapter).
 *
 * A driver for another controller maps its own causes to these.
 */
#define I2C_TXQ_ABRT_ADDR_NOACK  0x00000001u /**< 7-bit address not acked */
#define I2C_TXQ_ABRT_DATA_NOACK  0x00000008u /**< a data byte not acked   */
#define I2C_TXQ_ABRT_GCALL_NOACK 0x00000010u /**< general call not acked  */
#define I2C_TXQ_ABRT_ARB_LOST    0x00001000u /**< arbitration lost        */
#define I2C_TXQ_ABRT_USER        0x00010000u /**< the abort the driver set */
/** @brief The cause bits of IC_TX_ABRT_SOURCE: bits 0 to 16. */
#define I2C_TXQ_ABRT_CAUSES      0x0001FFFFu

#define I2C_TXQ_SDA 0u /**< @brief The data line, as i2c_txq_ops_t names it. */
#define I2C_TXQ_SCL 1u /**< @brief The clock line, as i2c_txq_ops_t names it. */

/** @brief What a transaction came to. */
typedef enum {
    I2C_TXQ_OK = 0,      /**< sent, acknowledged, every byte read          */
    I2C_TXQ_NACK_ADDR,   /**< the address not acknowledged: nothing there  */
    I2C_TXQ_NACK_DATA,   /**< a written byte not acknowledged              */
    I2C_TXQ_ARB_LOST,    /**< the controller lost arbitration: a line held */
    I2C_TXQ_TIMEOUT,     /**< not finished in timeout_us; the block reset  */
    I2C_TXQ_BUS_LOW,     /**< a line low before the start, also on the pad
                              pull-ups: a part holds it                   */
    I2C_TXQ_NO_PULLUP,   /**< a line low before the start that rose on the
                              pad pull-ups: no pull-up and no part         */
    I2C_TXQ_DROPPED,     /**< soft, its deadline passed before it started  */
    I2C_TXQ_ABORTED,     /**< taken back by i2c_txq_abort(), a clear or a
                              flush                                        */
    I2C_TXQ_FAULT,       /**< another abort cause, or fewer bytes read     */
    I2C_TXQ_RESULTS      /**< the number of results                        */
} i2c_txq_res_t;

/** @brief What i2c_txq_submit() answers. */
typedef enum {
    I2C_TXQ_E_OK = 0,    /**< queued                                        */
    I2C_TXQ_E_ARG,       /**< NULL, a length out of range, a reserved
                              address, an unknown class                    */
    I2C_TXQ_E_FULL,      /**< I2C_TXQ_DEPTH transactions queued             */
    I2C_TXQ_E_BUSY,      /**< this descriptor is queued or on the wire      */
    I2C_TXQ_E_CLOSED,    /**< the queue is shut (i2c_txq_shut())             */
} i2c_txq_err_t;

/** @brief Where a descriptor is. */
typedef enum {
    I2C_TXQ_ST_IDLE = 0, /**< never submitted                               */
    I2C_TXQ_ST_QUEUED,   /**< waiting                                       */
    I2C_TXQ_ST_RUNNING,  /**< picked: line check, or on the wire            */
    I2C_TXQ_ST_DONE,     /**< result, flags and stamps are set              */
} i2c_txq_state_t;

/** @brief Classes. */
#define I2C_TXQ_SOFT 0u  /**< dropped once its deadline has passed */
#define I2C_TXQ_HARD 1u  /**< before every soft one, never dropped */

/** @brief Flags of a finished descriptor. */
#define I2C_TXQ_F_WIRE       0x01u /**< it reached the controller          */
#define I2C_TXQ_F_TRIAL      0x02u /**< the first one after a bus clear    */
#define I2C_TXQ_F_LATE_START 0x04u /**< hard, started past its deadline    */
#define I2C_TXQ_F_LATE_STAMP 0x08u /**< done_us is the computed end        */
#define I2C_TXQ_F_ABORT      0x10u /**< i2c_txq_abort() asked for its end  */

/**
 * @brief One transaction.
 *
 * The caller owns the descriptor and fills the first block; from the submit
 * to the end the queue holds a pointer to it and the caller leaves it alone.
 * The queue fills the second block.
 */
typedef struct i2c_txq_xfer {
    uint64_t not_before_us; /**< not started before this time            */
    uint64_t deadline_us;   /**< the last time it may start              */
    uint8_t  addr;          /**< 7-bit address, 0x08 to 0x77             */
    uint8_t  wr_len;        /**< bytes to write, 0 to I2C_TXQ_WR_MAX     */
    uint8_t  rd_len;        /**< bytes to read, 0 to I2C_TXQ_RD_MAX      */
    uint8_t  cls;           /**< I2C_TXQ_SOFT or I2C_TXQ_HARD            */
    uint8_t  wr[I2C_TXQ_WR_MAX]; /**< the bytes written, first first     */
    void    *user;          /**< the caller's, not read                  */

    uint8_t  rd[I2C_TXQ_RD_MAX]; /**< the bytes read, on I2C_TXQ_OK      */
    uint8_t  state;         /**< i2c_txq_state_t                         */
    uint8_t  result;        /**< i2c_txq_res_t, once done                */
    uint8_t  flags;         /**< I2C_TXQ_F_*                             */
    uint32_t seq;           /**< submit order                            */
    uint64_t start_us;      /**< the time it went to the controller      */
    uint64_t done_us;       /**< the time it ended                       */
} i2c_txq_xfer_t;

/**
 * @brief The driver: the controller and the two pads.
 *
 * Each function returns at once.
 */
typedef struct {
    /** @brief The level of a line: true for high. */
    bool (*get)(void *ctx, unsigned line);
    /**
     * @brief Hand @p x to the controller, STOP included, and report its end
     *        with i2c_txq_complete().
     */
    void (*start)(void *ctx, const i2c_txq_xfer_t *x);
    /**
     * @brief Ask the controller to end the transaction on the wire after the
     *        byte under way, with a STOP; its end is reported as usual.
     */
    void (*abort)(void *ctx);
    /**
     * @brief Reset the controller and set it up again, now: whatever it was
     *        doing ends, and nothing is reported.
     */
    void (*reset)(void *ctx);
    /** @brief The pads' own pull-ups on both lines, on or off. */
    void (*pullups)(void *ctx, bool on);
    /**
     * @brief On: the controller reset and both pins plain GPIO (general
     *        purpose input/output), released.  Off: the controller set up
     *        again and the pins back on it.
     */
    void (*gpio)(void *ctx, bool on);
    /** @brief In GPIO mode: drive a line low, or release it. */
    void (*line)(void *ctx, unsigned line, bool high);
} i2c_txq_ops_t;

/**
 * @brief The done callback: @p x has its result.
 *
 * Called from the context that ended it -- the driver's interrupt, or the
 * caller's poll, submit or abort.  It may submit @p x again, and poll; the
 * poll runs after the callback returns (i2c_txq_poll()).  It may shut the
 * queue: no driver function is called after that, and the queue is left
 * idle.  It must not call i2c_txq_init().
 */
typedef void (*i2c_txq_done_fn)(void *user, i2c_txq_xfer_t *x);

/** @brief Timings and the done callback. */
typedef struct {
    uint32_t hz;               /**< bus clock, for the computed bus time  */
    uint32_t timeout_us;       /**< I2C_TXQ_TIMEOUT_US                    */
    uint32_t settle_us;        /**< I2C_TXQ_SETTLE_US                     */
    uint32_t probe_us;         /**< I2C_TXQ_PROBE_US                      */
    uint32_t clear_half_us;    /**< I2C_TXQ_CLEAR_HALF_US                 */
    uint32_t clear_stretch_us; /**< I2C_TXQ_CLEAR_STRETCH_US              */
    uint32_t late_us;          /**< I2C_TXQ_LATE_US                       */
    uint32_t guard_us;         /**< I2C_TXQ_GUARD_US                      */
    i2c_txq_done_fn done;      /**< NULL for none                         */
    void    *user;             /**< handed to done                        */
} i2c_txq_cfg_t;

/** @brief Counters, from the init on; they wrap at 2^32. */
typedef struct {
    uint32_t submitted;   /**< accepted by i2c_txq_submit()              */
    uint32_t ok;          /**< ended I2C_TXQ_OK                          */
    uint32_t failed;      /**< ended with a wire result: NACK to FAULT   */
    uint32_t dropped;     /**< ended I2C_TXQ_DROPPED                     */
    uint32_t aborted;     /**< ended I2C_TXQ_ABORTED                     */
    uint32_t timeouts;    /**< ended I2C_TXQ_TIMEOUT                     */
    uint32_t late_starts; /**< hard ones started past their deadline     */
    uint32_t late_stamps; /**< done_us set to the computed end           */
    uint32_t clears;      /**< bus clears finished                       */
    uint32_t spurious;    /**< i2c_txq_complete() with nothing running   */
    uint64_t busy_us;     /**< computed bus time of every start          */
    uint8_t  max_queued;  /**< the most transactions queued at once      */
} i2c_txq_stats_t;

/** @brief A queue: one bus.  The fields are the queue's. */
typedef struct {
    i2c_txq_ops_t    ops;      /**< the driver                             */
    void            *ctx;      /**< handed to the driver                   */
    i2c_txq_cfg_t    cfg;      /**< timings and the callback               */
    i2c_txq_stats_t  st;       /**< counters                               */
    i2c_txq_xfer_t  *slot[I2C_TXQ_DEPTH + 1u]; /**< the queued ones,
                                    unordered; one more than a submit fills,
                                    for a picked one put back             */
    i2c_txq_xfer_t  *run;      /**< the picked one; NULL for none          */
    uint64_t         phase_at; /**< when the phase began, or the next clear
                                    step is due                           */
    uint32_t         seq;      /**< the next submit number                 */
    uint8_t          n;        /**< slots in use                           */
    uint8_t          phase;    /**< idle, settle, probe, wire or clear     */
    uint8_t          step;     /**< the next step of the bus clear         */
    uint8_t          clear_result; /**< i2c_txq_res_t of the last clear    */
    bool             hold;     /**< nothing starts                         */
    bool             trial;    /**< the next one picked is the trial       */
    bool             scl_up;   /**< the clear has released SCL and waits   */
    uint8_t          depth;    /**< queue calls under way: a poll at 1 or
                                    more is noted and run by the outermost */
    bool             repoll;   /**< a callback polled inside a queue call  */
    bool             closed;   /**< shut: submits and polls refused        */
} i2c_txq_t;

/**
 * @brief Fill @p cfg with the defaults above, at I2C_TXQ_HZ, no callback.
 *
 * @param cfg  Receives the defaults.
 */
void i2c_txq_cfg_default(i2c_txq_cfg_t *cfg);

/**
 * @brief Set up an empty queue over a driver.
 *
 * @param q    The queue.
 * @param ops  The driver; copied.  Every function must be set.
 * @param ctx  Handed to every driver function.
 * @param cfg  Copied; NULL for the defaults.
 * Transactions still queued or running on @p q are not ended: shut or
 * flush it first.
 *
 * @return False, and @p q untouched, on a NULL @p q or @p ops, a missing
 *         driver function or a bus clock of 0.
 */
bool i2c_txq_init(i2c_txq_t *q, const i2c_txq_ops_t *ops, void *ctx,
                  const i2c_txq_cfg_t *cfg);

/**
 * @brief Queue @p x.  Nothing starts here: the next i2c_txq_poll() picks.
 *
 * @param q  The queue.
 * @param x  The transaction; its first block filled.
 * @return I2C_TXQ_E_OK, or why it was refused.  A refused descriptor is not
 *         changed.
 */
i2c_txq_err_t i2c_txq_submit(i2c_txq_t *q, i2c_txq_xfer_t *x);

/**
 * @brief Take @p x back.
 *
 * Queued, or picked and not yet on the wire: it ends I2C_TXQ_ABORTED now.
 * On the wire: the driver's abort() is called and the end comes as usual;
 * a transaction that does not end I2C_TXQ_OK ends I2C_TXQ_ABORTED.
 *
 * @param q    The queue.
 * @param x    The transaction.
 * @param now  The present time, in us.
 * @return False when @p x is neither queued nor running on @p q.
 */
bool i2c_txq_abort(i2c_txq_t *q, i2c_txq_xfer_t *x, uint64_t now);

/**
 * @brief Everything ends I2C_TXQ_ABORTED: the queued ones, and the running
 *        one with the driver's reset().  A bus clear under way is cut and
 *        the driver's gpio(false) called.  The hold and trial stay.
 *
 * @param q    The queue.
 * @param now  The present time, in us.
 */
void i2c_txq_flush(i2c_txq_t *q, uint64_t now);

/**
 * @brief Shut the queue: i2c_txq_flush(), with every submit refused from
 *        before the first callback on, so nothing stays queued.  A shut
 *        queue takes no submit (I2C_TXQ_E_CLOSED), poll or recovery until
 *        the next i2c_txq_init().
 *
 * @param q    The queue.
 * @param now  The present time, in us.
 */
void i2c_txq_shut(i2c_txq_t *q, uint64_t now);

/**
 * @brief Whether the queue is shut.
 *
 * @param q  The queue.
 * @return True after i2c_txq_shut() and for NULL.
 */
bool i2c_txq_closed(const i2c_txq_t *q);

/**
 * @brief The queue's step: timeouts, the line check, the bus clear, the
 *        drop of soft transactions past their deadline, and the start of
 *        the next one.
 *
 * Call it after every completion and at least as often as the shortest
 * wait the caller wants to resolve: a step that is due is taken at the
 * first poll at or after its time.  A poll made while another queue call
 * is under way -- from a done callback -- does not run there: the
 * outermost call runs it after its own work, up to I2C_TXQ_REPOLLS times
 * while callbacks keep polling.  So queue calls never nest deeper than one,
 * and a callback that submits an expired transaction on every call ends at
 * most 1 + I2C_TXQ_REPOLLS of them per outer call.  Nothing is done on a
 * shut queue.
 *
 * @param q    The queue.
 * @param now  The present time, in us.
 */
void i2c_txq_poll(i2c_txq_t *q, uint64_t now);

/**
 * @brief The driver's report: the transaction it was handed has ended on
 *        the wire, with the STOP.
 *
 * @param q     The queue.
 * @param now   The time of the end, in us.
 * @param abrt  The controller's abort causes, I2C_TXQ_ABRT_*; 0 for none.
 * @param rx    The bytes read; may be NULL when @p n is 0.
 * @param n     Bytes in @p rx.
 */
void i2c_txq_complete(i2c_txq_t *q, uint64_t now, uint32_t abrt,
                      const uint8_t *rx, size_t n);

/**
 * @brief Start a bus clear: 9 clocks on SCL as GPIO, then a STOP (NXP
 *        UM10204 section 3.1.16), each step half a clock after the last.
 *
 * A running transaction ends I2C_TXQ_ABORTED first (the driver's reset()
 * when it is on the wire).  Nothing starts until the clear is over; the
 * first transaction picked after it carries I2C_TXQ_F_TRIAL.  The trial
 * is spent by any end but I2C_TXQ_ABORTED; after an abort the next one
 * picked is the trial.
 *
 * @param q    The queue.
 * @param now  The present time, in us.
 * @return False when a clear is already under way.
 */
bool i2c_txq_recover(i2c_txq_t *q, uint64_t now);

/**
 * @brief Hold the queue: nothing starts while held.  Soft transactions past
 *        their deadline are still dropped.  A transaction picked while a
 *        line was low and not yet on the wire goes back to the queue, with
 *        its submit number, when the line rises; a line check under way
 *        ends with its probe result as usual.
 *
 * @param q   The queue.
 * @param on  True to hold, false to let go.
 */
void i2c_txq_hold(i2c_txq_t *q, bool on);

/**
 * @brief Whether both lines read high now, by the driver's get().
 *
 * @param q  The queue.
 * @return True when SDA and SCL read high; false for NULL.
 */
bool i2c_txq_lines_high(const i2c_txq_t *q);

/**
 * @brief Whether a bus clear is under way.
 *
 * @param q  The queue.
 * @return True from i2c_txq_recover() to the end of the clear.
 */
bool i2c_txq_clearing(const i2c_txq_t *q);

/**
 * @brief What the last bus clear came to.
 *
 * @param q  The queue.
 * @return I2C_TXQ_OK when both lines were high at its end, I2C_TXQ_BUS_LOW
 *         when one was low, I2C_TXQ_ABORTED when it was cut by a flush;
 *         I2C_TXQ_OK before the first.
 */
i2c_txq_res_t i2c_txq_clear_result(const i2c_txq_t *q);

/**
 * @brief Whether nothing runs: no transaction picked and no clear.
 *
 * @param q  The queue.
 * @return True when no transaction is picked and no clear is under way.
 */
bool i2c_txq_idle(const i2c_txq_t *q);

/**
 * @brief Transactions queued, the running one not counted.
 *
 * @param q  The queue.
 * @return 0 to I2C_TXQ_DEPTH.
 */
size_t i2c_txq_queued(const i2c_txq_t *q);

/**
 * @brief The counters.
 *
 * @param q  The queue.
 * @return The queue's counters.
 */
const i2c_txq_stats_t *i2c_txq_stats(const i2c_txq_t *q);

/**
 * @brief The clocks @p x takes on the wire: 9 per byte (address bytes
 *        included) and 1 each for the START, the repeated START and the
 *        STOP.
 *
 * A register read of N bytes is 9 * (3 + N) + 3, a write of a pointer and
 * N bytes 9 * (2 + N) + 2.  Gaps the controller leaves between bytes are
 * not counted.
 *
 * @param x  The transaction.
 * @return The clock count; 0 for NULL.
 */
uint32_t i2c_txq_clocks(const i2c_txq_xfer_t *x);

/**
 * @brief i2c_txq_clocks() at @p hz, in us, rounded up.
 *
 * @param x   The transaction.
 * @param hz  The bus clock.
 * @return The bus time in us; 0 for NULL or a clock of 0.
 */
uint32_t i2c_txq_bus_us(const i2c_txq_xfer_t *x, uint32_t hz);

/**
 * @brief The result the controller's abort causes give.
 *
 * @param abrt  I2C_TXQ_ABRT_* bits.
 * @return I2C_TXQ_OK for none; the address NACK before the data NACK before
 *         the lost arbitration before the abort the driver set; any other
 *         cause I2C_TXQ_FAULT.
 */
i2c_txq_res_t i2c_txq_abrt_result(uint32_t abrt);

/**
 * @brief The name of a result, for a console.
 *
 * @param r  The result.
 * @return A constant string such as "nack-addr"; "?" for a value out of
 *         range.
 */
const char *i2c_txq_res_name(unsigned r);

#ifdef __cplusplus
}
#endif

#endif /* I2C_TXQ_H */
