/**
 * @file
 * @brief Hitec servo programming protocols: the session with one servo, as
 *        a state machine that hands out steps and takes their results.
 *
 * Implements the session of the Hitec servo programming protocols.  Hitec
 * is a name or trademark of its owner; it is used here only to say which
 * servos this module works with (README.md, "Legal notice and disclaimer").
 *
 * The session runs one operation at a time: entry, a read, a write plan
 * with its read-back, leave.  It neither touches the wire nor reads a
 * clock.  The caller asks for the next step with hitec_session_next(),
 * runs it and hands its result to hitec_session_feed():
 *
 *   - XFER: one transaction (hitec_xfer_t), decoded by the named decoder;
 *   - POWER: the servo supply off for off_ms, on, and the transaction, a
 *     LISTEN, from the ON;
 *   - WAIT: nothing to run; ask again after wait_ms;
 *   - DONE: no operation runs; hitec_session_result() gives the last one's
 *     result.
 *
 * The supply's voltage and current limit are the caller's.  So are the
 * port, the refusals before an entry (a classic servo without a supply the
 * bench switches) and the journal; step_index and the plan are there for
 * it.
 *
 * Time: the caller passes the reading of one free-running 32-bit clock,
 * in microseconds or in milliseconds (hitec_ses_clock_t), to every call
 * that takes @p now.  The session measures the gap before a transaction
 * from the result of the one before, by unsigned difference, so the wrap
 * of the clock at 2^32 does not matter.  A gap has passed once the clock
 * has moved by more than the gap in its ticks: one tick more than the gap,
 * since a reading lies up to one tick behind the time.
 *
 * The session object holds all state.  Two sessions on two ports run side
 * by side.  No heap, no global or static mutable state.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef HITEC_SESSION_H
#define HITEC_SESSION_H

#include <stdbool.h>
#include <stdint.h>

#include "hitec_reg.h"
#include "hitec_wire.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- timings -------------------------------------------------------------- */

/** @brief Tries of one transaction that has an answer: the first and 2
 *         more. */
#define HITEC_SES_TRIES 3u
/** @brief Gap after a classic transaction, ms. */
#define HITEC_SES_C_GAP_MS 2u
/** @brief Gap after a D series transaction, ms: the HFP-30's write
 *         spacing. */
#define HITEC_SES_D_GAP_MS HITEC_D_WRITE_GAP_MS
/** @brief Wait after the supply comes on before the first D series frame,
 *         ms: the wait after a reboot. */
#define HITEC_SES_D_ENTRY_WAIT_MS HITEC_D_REBOOT_WAIT_MS
/** @brief Classic entry: the window for the servo's first `+` from the ON,
 *         us. */
#define HITEC_SES_C_ENTRY_WINDOW_US 2000000u
/**
 * @brief Classic entry: the window for the servo's next `+`, us.
 *
 * Measured 250 to 267ms after its first one.
 */
#define HITEC_SES_C_NEXT_WINDOW_US 1000000u
/**
 * @brief The window of the LISTEN of a POWER that waits for no byte, us:
 *        the D series entry and the classic leave.
 */
#define HITEC_SES_POWER_WINDOW_US 1000u
/** @brief Supply off time of the classic entry and leave, ms. */
#define HITEC_SES_OFF_MS 500u
/** @brief Longest supply off time, ms. */
#define HITEC_SES_OFF_MS_MAX 10000u
/** @brief Bytes of an address set: one bit for each of 256 addresses. */
#define HITEC_SES_SET_BYTES 32u

/* --- steps and results ---------------------------------------------------- */

/** @brief The clock the caller passes. */
typedef enum {
    HITEC_SES_CLOCK_US = 0,  /**< microseconds, wraps after 71.6 minutes */
    HITEC_SES_CLOCK_MS = 1,  /**< milliseconds, wraps after 49.7 days */
} hitec_ses_clock_t;

/** @brief What a step asks of the caller. */
typedef enum {
    HITEC_SES_STEP_XFER = 0,  /**< run the transaction */
    HITEC_SES_STEP_POWER,     /**< cycle the supply, then the LISTEN */
    HITEC_SES_STEP_WAIT,      /**< run nothing; ask again later */
    HITEC_SES_STEP_DONE,      /**< no operation runs */
} hitec_ses_kind_t;

/**
 * @brief The decoder of a step's capture.
 *
 * The values are the HITEC link page's (link_hitec_dec_t).
 */
typedef enum {
    HITEC_DEC_NONE = 0,    /**< no decode: the result's rx is not read */
    HITEC_DEC_C = 1,       /**< hitec_c_decode(), argument the command */
    HITEC_DEC_D = 2,       /**< hitec_d_decode(), argument the register */
    HITEC_DEC_LISTEN = 3,  /**< hitec_listen_decode(), argument the byte */
} hitec_dec_t;

/** @brief One step. */
typedef struct {
    uint8_t      kind;     /**< hitec_ses_kind_t */
    uint8_t      decoder;  /**< hitec_dec_t; XFER and POWER */
    uint8_t      dec_arg;  /**< the decoder's argument */
    uint16_t     off_ms;   /**< POWER: the supply off time, ms */
    uint16_t     wait_ms;  /**< WAIT: ms left, rounded up, at most 65535 */
    hitec_xfer_t xfer;     /**< XFER: the transaction; POWER: its LISTEN */
    uint8_t      more;     /**< 1: a write step that more steps of the
                                same write may follow: a plan step before
                                the last, or a try of the last that has
                                tries left; 0 for every other step */
} hitec_ses_step_t;

/** @brief How a step ended. */
typedef enum {
    /** @brief It ran to its end; rx and value are the decoder's. */
    HITEC_SES_RAN = 0,
    /** @brief An abort, a close or a stop cut it. */
    HITEC_SES_CUT,
    /** @brief It did not run: refused, not started, the supply refused,
     *         or the link was lost. */
    HITEC_SES_FAILED,
} hitec_ses_run_t;

/** @brief The result of a step. */
typedef struct {
    uint8_t  run;    /**< hitec_ses_run_t */
    uint8_t  rx;     /**< hitec_rx_t of the step's decoder */
    uint16_t value;  /**< the decoded value */
} hitec_ses_result_t;

/** @brief Result of an operation. */
typedef enum {
    HITEC_SES_OK = 0,           /**< the operation ended without error */
    HITEC_SES_BUSY,             /**< the operation runs */
    HITEC_SES_ERR_BUSY,         /**< another operation runs */
    HITEC_SES_ERR_ARG,          /**< NULL, an argument out of range, a
                                     result with no step out */
    HITEC_SES_ERR_NOT_IN_MODE,  /**< no entry has succeeded */
    HITEC_SES_ERR_NO_SERVO,     /**< no byte on the last try */
    HITEC_SES_ERR_ENTRY,        /**< the entry got bytes, not the ones it
                                     waits for */
    HITEC_SES_ERR_READ,         /**< a read failed on every try */
    HITEC_SES_ERR_WRITE,        /**< a classic write got no `+ +` on any
                                     try */
    HITEC_SES_ERR_VERIFY,       /**< the read-back differs from the
                                     target; diff names the addresses */
    HITEC_SES_ERR_PLAN,         /**< no plan: plan_result tells why, or a
                                     step of the other series */
    HITEC_SES_ERR_STEP,         /**< the decoder refused the step
                                     (HITEC_RX_ERR_ARG) */
    HITEC_SES_ERR_CUT,          /**< a step was cut */
    HITEC_SES_ERR_LINK,         /**< a step did not run */
    HITEC_SES_ERR_ABORTED,      /**< hitec_session_abort() */
} hitec_ses_t;

/** @brief The session with one servo. */
typedef struct {
    /* --- for the caller to read ------------------------------------------- */
    hitec_image_t image;   /**< what the servo confirmed: entry, reads,
                                read-back, classic writes answered
                                `+ +`; never a D series write alone.
                                After a write that failed, what the
                                servo may have changed is not known
                                (hitec_session_write()) */
    hitec_plan_t  plan;    /**< the plan of the running or last write */
    /** @brief Bit a of byte a / 8, bit a % 8: address a differs after a
     *         write's read-back. */
    uint8_t  diff[HITEC_SES_SET_BYTES];
    uint8_t  series;       /**< hitec_family_t */
    uint8_t  clock;        /**< hitec_ses_clock_t */
    uint8_t  in_mode;      /**< 1: an entry succeeded; 0 from a leave */
    uint8_t  result;       /**< hitec_ses_t of the last operation */
    uint8_t  last_rx;      /**< hitec_rx_t of the last decoded step */
    uint8_t  fail_addr;    /**< the address of the read or write that
                                failed */
    uint8_t  tries;        /**< tries of the running or last transaction */
    uint8_t  step_index;   /**< writes of the plan that took */
    uint8_t  plan_result;  /**< hitec_plan_result_t of the last
                                hitec_session_write() */
    uint16_t done;         /**< addresses read by the running read */
    uint16_t total;        /**< addresses it reads */
    uint16_t off_ms;       /**< the supply off time of entry and leave */

    /* --- private ----------------------------------------------------------- */
    hitec_image_t target;  /**< the image a write's read-back must hold */
    hitec_ses_step_t step; /**< the step handed out */
    uint8_t  todo[HITEC_SES_SET_BYTES];  /**< addresses still to read */
    uint32_t t_mark;       /**< clock at the last result */
    uint32_t gap;          /**< ticks from t_mark before the next step */
    uint8_t  op;           /**< the operation that runs */
    uint8_t  phase;        /**< the step it is at */
    uint8_t  out;          /**< 1: step is handed out, no result yet */
    uint8_t  abort;        /**< 1: abort when the result comes */
    uint8_t  gap_on;       /**< 1: gap counts */
    uint8_t  addr;         /**< the address of the step */
    uint8_t  power;        /**< D series entry: cycle the supply */
    uint8_t  compare;      /**< 1: the write compares with target */
    uint8_t  sent;         /**< 1: a plan step was handed out and its
                                result fed, whatever it was */
    uint8_t  unsure;       /**< 1: the plan step at step_index was fed a
                                result and no `+ +` confirmed it */
} hitec_session_t;

/* --- operations ----------------------------------------------------------- */

/**
 * @brief Prepare a session for one series.
 *
 * No operation runs, no step is due, not in mode; off time
 * HITEC_SES_OFF_MS.
 *
 * @param s       Session.
 * @param series  hitec_family_t, chosen by the operator.
 * @param clock   hitec_ses_clock_t of the @p now the caller passes.
 * @return false for NULL, an unknown series or an unknown clock.
 */
bool hitec_session_init(hitec_session_t *s, uint8_t series, uint8_t clock);

/**
 * @brief Enter programming mode.
 *
 * Classic: POWER (off @p off_ms, on, a LISTEN of
 * HITEC_SES_C_ENTRY_WINDOW_US that answers the servo's `+` with `+`), a
 * LISTEN of HITEC_SES_C_NEXT_WINDOW_US for the servo's next `+`, then the
 * first command `67 00 00 99`, whose reply's value is the version.  A
 * listen that gets no `+` ends the entry; the first command has
 * HITEC_SES_TRIES tries.  Classic entry always cycles the supply.
 *
 * D series: with @p power, POWER (off @p off_ms, on, a LISTEN of
 * HITEC_SES_POWER_WINDOW_US that waits for nothing) and
 * HITEC_SES_D_ENTRY_WAIT_MS; without it, on the supply as it is.  Then a
 * read of register 0x00, the model, with HITEC_SES_TRIES tries.
 *
 * The image starts empty and holds the version or the model.  The off
 * time is kept for the leave.
 *
 * @param s       Session.
 * @param power   D series: cycle the supply first; classic: ignored.
 * @param off_ms  Supply off time, 0 to HITEC_SES_OFF_MS_MAX.
 * @return HITEC_SES_BUSY when the entry starts; HITEC_SES_ERR_BUSY,
 *         HITEC_SES_ERR_ARG.
 */
hitec_ses_t hitec_session_enter(hitec_session_t *s, bool power,
                                uint16_t off_ms);

/**
 * @brief Read the series' full set: hitec_d_read_list() or
 *        hitec_c_read_ranges().
 *
 * The image starts empty, the classic version kept.  Addresses go in
 * ascending order, each with HITEC_SES_TRIES tries; one that fails on
 * every try ends the read, and the image keeps what was read before it.
 *
 * @param s  Session.
 * @return HITEC_SES_BUSY when the read starts; HITEC_SES_ERR_BUSY,
 *         HITEC_SES_ERR_ARG, HITEC_SES_ERR_NOT_IN_MODE.
 */
hitec_ses_t hitec_session_read_all(hitec_session_t *s);

/**
 * @brief Read @p n addresses into the image, which keeps the rest.
 *
 * As hitec_session_read_all(), in ascending order, each address once.
 *
 * @param s      Session.
 * @param addrs  Cells, or even D series registers.
 * @param n      Count, 1 to 256.
 * @return HITEC_SES_BUSY when the read starts; HITEC_SES_ERR_BUSY,
 *         HITEC_SES_ERR_ARG (NULL, n 0 or above 256, an odd D series
 *         register), HITEC_SES_ERR_NOT_IN_MODE.
 */
hitec_ses_t hitec_session_read_set(hitec_session_t *s, const uint8_t *addrs,
                                   uint16_t n);

/**
 * @brief Write the servo from the image to @p target, then read it back.
 *
 * The plan is hitec_plan(image, target), kept in plan.  An empty plan
 * ends with HITEC_SES_OK and sends nothing.
 *
 * Classic: each cell and each balance (0x1C) as `62 cell value sum`, with
 * HITEC_SES_TRIES tries for its `+ +`, HITEC_SES_C_GAP_MS apart.  Then a
 * read of cells 0x00 to 0x23.
 *
 * D series: each write, the save and the reboot as a write frame with
 * no answer, each followed by its plan wait (20ms, 100ms, 1000ms), at
 * least HITEC_SES_D_GAP_MS.  Then a read of the full set.
 *
 * The read-back must hold every writable address (hitec_writable()) and
 * the classic balance as @p target holds it, where @p target knows it;
 * diff names the ones that differ.  step_index counts the plan's writes
 * that went out.
 *
 * The image holds only what the servo confirmed.  A classic write
 * answered `+ +` goes into it.  A D series write has no answer and does
 * not change it; the read-back puts the register there.
 *
 * A write that ends with anything but HITEC_SES_OK once a plan step was
 * handed out and its result fed (RAN, CUT or FAILED: a cut or lost step
 * may have reached the servo) marks as not known (hitec_image_forget()) every address the servo
 * may have changed and this write's read-back has not read: D series,
 * every register of the full read and every register of the plan, since a
 * save, a reboot or a factory reset may change registers the plan does
 * not name; classic, the cell of a write whose last try got no `+ +`
 * (a cut or lost try included) and the balance 0x1C.  hitec_plan() then refuses with
 * HITEC_PLAN_ERR_UNKNOWN (HITEC_SES_ERR_PLAN): the caller reads the servo
 * before it writes again.  A write refused or aborted before a step's
 * result was fed, and one
 * whose read-back ended with HITEC_SES_ERR_VERIFY, forget nothing.
 *
 * @param s       Session.
 * @param target  The draft, copied.
 * @return HITEC_SES_BUSY when the write starts, HITEC_SES_OK for an
 *         empty plan; HITEC_SES_ERR_BUSY, HITEC_SES_ERR_ARG (NULL, a
 *         target of the other series), HITEC_SES_ERR_NOT_IN_MODE,
 *         HITEC_SES_ERR_PLAN.
 */
hitec_ses_t hitec_session_write(hitec_session_t *s,
                                const hitec_image_t *target);

/**
 * @brief Run a plan the caller built, such as hitec_d_factory_plan(),
 *        then read back as hitec_session_write() does.
 *
 * @param s       Session.
 * @param plan    The plan, copied; steps of the session's series only.
 * @param expect  The image the read-back must hold, copied; NULL reads
 *                back and compares nothing.
 * @return As hitec_session_write(); HITEC_SES_ERR_PLAN for more than
 *         HITEC_PLAN_MAX steps, a step of the other series, a classic
 *         value above 255 or an odd D series register.
 */
hitec_ses_t hitec_session_run_plan(hitec_session_t *s,
                                   const hitec_plan_t *plan,
                                   const hitec_image_t *expect);

/**
 * @brief Leave programming mode.
 *
 * Classic: POWER (off off_ms, on, a LISTEN of HITEC_SES_POWER_WINDOW_US
 * with no answer).  D series: the reboot, `0x46 = 1`, then
 * HITEC_D_REBOOT_WAIT_MS before the next step of any operation.  in_mode
 * is 0 from the start of the leave.  Taken in or out of mode.
 *
 * @param s  Session.
 * @return HITEC_SES_BUSY when the leave starts; HITEC_SES_ERR_BUSY,
 *         HITEC_SES_ERR_ARG.
 */
hitec_ses_t hitec_session_leave(hitec_session_t *s);

/**
 * @brief End the running operation with HITEC_SES_ERR_ABORTED.
 *
 * With no step out it ends at once.  With a step out it ends when that
 * step's result comes: the caller cuts the step and feeds its result,
 * which the session stores as far as it went.  Until then
 * hitec_session_next() gives the same step.
 *
 * @param s  Session.
 * @return true when a step is out and its result is still owed.
 */
bool hitec_session_abort(hitec_session_t *s);

/* --- stepping ------------------------------------------------------------- */

/**
 * @brief The step to run at @p now.
 *
 * A step stays out, and this call gives it again, until its result is
 * fed.  WAIT while the gap before the next transaction runs; DONE when no
 * operation runs.
 *
 * @param s    Session.
 * @param now  The clock.
 * @param out  Receives the step; NULL takes the kind only.
 * @return The step's hitec_ses_kind_t; DONE for a NULL session.
 */
hitec_ses_kind_t hitec_session_next(hitec_session_t *s, uint32_t now,
                                    hitec_ses_step_t *out);

/**
 * @brief Take the result of the step that is out.
 *
 * CUT ends the operation with HITEC_SES_ERR_CUT, FAILED with
 * HITEC_SES_ERR_LINK.  A step with a decoder: HITEC_RX_OK goes on;
 * HITEC_RX_ERR_ARG ends it with HITEC_SES_ERR_STEP; HITEC_RX_ERR_ABORTED
 * with HITEC_SES_ERR_CUT; every other code is a failed try.  A failed
 * try of a classic entry LISTEN ends the entry.  A transaction with tries
 * left goes again after its gap; one with none ends the operation with
 * HITEC_SES_ERR_NO_SERVO when the last try was HITEC_RX_ERR_SILENT, else
 * with the operation's error: ENTRY, READ or WRITE.
 *
 * Every result starts the gap of the step that ended before anything
 * else, a cut or lost step's included, since its frame may have reached
 * the servo: the series gap, a plan step's longer wait, the D series
 * entry's 1000ms, the 1000ms after a D series leave.  An abort, an error or the
 * end of the operation keeps it; the next operation waits it out.
 *
 * @param s    Session.
 * @param now  The clock when the step ended.
 * @param r    The result.
 * @return HITEC_SES_BUSY while the operation runs, then its result;
 *         HITEC_SES_ERR_ARG, and no change, for NULL or no step out.
 */
hitec_ses_t hitec_session_feed(hitec_session_t *s, uint32_t now,
                               const hitec_ses_result_t *r);

/**
 * @brief The running operation's state.
 *
 * @param s  Session.
 * @return HITEC_SES_BUSY while an operation runs, else the last one's
 *         result; HITEC_SES_ERR_ARG for NULL.
 */
hitec_ses_t hitec_session_result(const hitec_session_t *s);

/**
 * @brief True when address @p addr is in the set @p set.
 *
 * @param set   HITEC_SES_SET_BYTES bytes, bit a % 8 of byte a / 8.
 * @param addr  Address.
 * @return true when the bit is set; false for NULL.
 */
bool hitec_ses_in_set(const uint8_t *set, uint8_t addr);

#ifdef __cplusplus
}
#endif

#endif /* HITEC_SESSION_H */
