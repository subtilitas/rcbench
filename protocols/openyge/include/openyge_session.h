/**
 * @file openyge_session.h
 * @brief One OpenYGE ESC (electronic speed controller) on one telemetry
 *        wire: boot wait, listening, takeover, table read, writes with a
 *        read before and after.  A state machine that never waits.
 *
 * ```c
 * oy_session_init(&s, &driver);
 * // the caller switches the ESC's supply on
 * oy_session_enter(&s, OY_BOOT_MS);               // WAIT, LISTEN, TAKE
 * while (oy_session_step(&s) == OY_BUSY) { }      // call every 1ms
 * oy_session_read(&s);                            // READ
 * while (oy_session_step(&s) == OY_BUSY) { }
 * memcpy(draft, s.orig, s.count * sizeof(draft[0]));
 * draft[21] = 13u;
 * oy_session_write(&s, draft, s.count, s.guard.read_no, s.guard.read_crc);
 * while (oy_session_step(&s) == OY_BUSY) { }      // READ, WRITE, VERIFY
 * ```
 *
 * Once taken over, the session sends a telemetry request
 * OY_REQ_PERIOD_MS after each reply, also between operations.  Every frame
 * passes oy_guard_frame() right before the driver.  Each try of a request
 * or a write carries a new sequence number, so a reply that comes after its
 * request was sent again is dropped.
 *
 * A result other than OY_OK that an operation reaches ends the session:
 * nothing more is sent until oy_session_enter().  A command refused before
 * any frame changes nothing and keeps the session.  The caller's part:
 * switch the supply, park the line, and switch every output off on any
 * result other than OY_OK.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef OPENYGE_SESSION_H
#define OPENYGE_SESSION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "openyge.h"
#include "openyge_guard.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Timing, docs/OpenYGE.md section 10. */

/** @brief Boot wait after the supply reads on, ms (the default). */
#define OY_BOOT_MS 5000u
/** @brief Shortest boot wait, ms. */
#define OY_BOOT_MS_MIN 1000u
/** @brief Longest boot wait, ms. */
#define OY_BOOT_MS_MAX 20000u
/** @brief Listening for a first frame after the boot wait, ms (chosen). */
#define OY_LISTEN_MS 3000u
/** @brief From a reply to the next request or write, ms (chosen). */
#define OY_REQ_PERIOD_MS 40u
/** @brief A telemetry request's reply counts up to this long after the
 *         send, ms. */
#define OY_READ_TIMEOUT_MS 200u
/** @brief A write's reply counts up to this long after the send, ms. */
#define OY_WRITE_TIMEOUT_MS 400u
/** @brief Sends of one request or write before it counts as missed. */
#define OY_TRIES 3u
/** @brief Longest whole-table read, ms (chosen). */
#define OY_TABLE_CAP_MS 10000u
/** @brief The address used when no v3 frame came while listening. */
#define OY_ADDR_DEFAULT 1u

/** @brief What a session says. */
typedef enum {
    OY_OK = 0,      /**< the operation ended as asked                       */
    OY_BUSY,        /**< call oy_session_step() again                       */
    OY_SILENT,      /**< no frame and no answer, or the ESC went quiet      */
    OY_NO_TABLE,    /**< no complete table: a count of 0 or over 64, the
                         read cap ran out, or a write before a read         */
    OY_NOT_KNOWN,   /**< a write to a table that is read-only: @c refuse    */
    OY_CHANGED,     /**< the table read before a write differs from the
                         read copy; nothing was written                     */
    OY_NOT_TAKEN,   /**< the table after the write equals the read copy     */
    OY_PARTIAL,     /**< the table after the write is neither, or could not
                         be read again after a write went out               */
    OY_GUARD,       /**< the guard refused: @c refuse and @c bad_index      */
    OY_ABORTED,     /**< cut by oy_session_abort()                          */
    OY_ERR_ARG,     /**< a NULL or a value out of range                     */
    OY_ERR_STATE,   /**< not possible in this state                         */
} oy_res_t;

/** @brief Where a session is. */
typedef enum {
    OY_ST_IDLE = 0, /**< no ESC                                             */
    OY_ST_WAIT,     /**< the boot wait: received bytes are dropped          */
    OY_ST_LISTEN,   /**< listening for a first frame                        */
    OY_ST_TAKE,     /**< requests until one is answered                     */
    OY_ST_READY,    /**< taken over: requests between operations; a legacy
                         ESC is only listened to                            */
    OY_ST_READ,     /**< reading the whole table                            */
    OY_ST_RECHECK,  /**< a write's read before any frame of it              */
    OY_ST_WRITE,    /**< one write per changed index, rising                */
    OY_ST_VERIFY,   /**< the table withdrawn and read again                 */
} oy_state_t;

/**
 * @brief The driver of the half-duplex wire.
 *
 * - tx() sends one whole frame and returns at once; false when it cannot.
 *   The driver removes the echo of its own bytes.
 * - rx() hands over the bytes received since its last call, at most @p cap,
 *   and returns their count.
 * - now_us() is a free-running clock that may wrap.
 */
typedef struct {
    void *ctx;                                                 /**< the driver's */
    bool (*tx)(void *ctx, const uint8_t *frame, size_t n);    /**< see above    */
    size_t (*rx)(void *ctx, uint8_t *buf, size_t cap);        /**< see above    */
    uint32_t (*now_us)(void *ctx);                            /**< see above    */
} oy_driver_t;

/** @brief A session.  Not to be moved while it runs. */
typedef struct {
    oy_driver_t       d;       /**< the driver                               */
    oy_guard_t        guard;   /**< what may go out                          */
    openyge_decoder_t dec;     /**< the receive side                         */
    openyge_params_t  params;  /**< the table being read                     */
    openyge_frame_t   fr;      /**< the frame being handled                  */
    uint16_t orig[OPENYGE_MAX_PARAMS];   /**< the read copy                  */
    uint16_t target[OPENYGE_MAX_PARAMS]; /**< the table being written        */
    uint16_t count;      /**< entries of the read copy, 0 before a read      */
    uint16_t bad_index;  /**< the index the guard refused                    */
    uint16_t widx;       /**< the index being written                        */
    uint8_t  state;      /**< an oy_state_t                                  */
    uint8_t  refuse;     /**< an oy_ref_t: the guard's last refusal          */
    uint8_t  addr;       /**< the ESC's address                              */
    uint8_t  seq;        /**< the sequence number of the last frame sent     */
    uint8_t  tries;      /**< sends of the request outstanding               */
    bool     legacy;     /**< the ESC sends legacy headers: listen only      */
    bool     awaiting;   /**< a request or write is outstanding              */
    bool     wrote;      /**< a write frame of this operation went out       */
    bool     cut;        /**< an abort cut a write after its first frame     */
    oy_res_t result;     /**< the last operation's result                    */
    uint32_t t_op;       /**< when the state or its cap started, us          */
    uint32_t t_req;      /**< when the outstanding frame went, us            */
    uint32_t t_reply;    /**< when the last reply came, us                   */
    uint32_t wait_us;    /**< the boot wait, us                              */
    uint32_t dropped;    /**< frames dropped: sequence, type, late, auto     */
    uint32_t misses;     /**< sends that timed out                           */
} oy_session_t;

/**
 * @brief Start over on a driver; nothing is sent.
 * @param s the session
 * @param d the driver, copied
 * @return OY_OK, or OY_ERR_ARG for a NULL or a driver with no hook
 */
oy_res_t oy_session_init(oy_session_t *s, const oy_driver_t *d);

/**
 * @brief The supply reads on: wait @p boot_ms, listen up to OY_LISTEN_MS
 *        and take the address of the first v3 frame (OY_ADDR_DEFAULT when
 *        none came), then request until answered.  A legacy frame ends it
 *        OY_OK with the session listening only.  Ends OY_OK or OY_SILENT.
 * @param s       the session, idle
 * @param boot_ms OY_BOOT_MS_MIN to OY_BOOT_MS_MAX
 * @return OY_BUSY when it started
 */
oy_res_t oy_session_enter(oy_session_t *s, uint32_t boot_ms);

/**
 * @brief Read the whole table into @c orig.  Ends OY_OK, read-only or not
 *        (@c guard.ro says why), or OY_NO_TABLE or OY_SILENT.
 * @param s the session, ready
 * @return OY_BUSY when it started
 */
oy_res_t oy_session_read(oy_session_t *s);

/**
 * @brief Write a draft of the read copy: read the table again and compare,
 *        one write per changed index, then read it all again.  Ends OY_OK,
 *        OY_CHANGED, OY_NOT_TAKEN, OY_PARTIAL, OY_SILENT, OY_NO_TABLE or
 *        OY_GUARD.
 * @param s        the session, ready
 * @param draft    the table to write, @c count entries
 * @param n        entries at @p draft
 * @param read_no  the read number the draft was made from
 * @param read_crc that read's CRC
 * @return OY_BUSY when it started; OY_OK when nothing differs, with no
 *         frame sent; OY_NO_TABLE, OY_NOT_KNOWN or OY_GUARD for a refusal
 */
oy_res_t oy_session_write(oy_session_t *s, const uint16_t *draft, size_t n,
                          uint16_t read_no, uint16_t read_crc);

/**
 * @brief Cut what runs at once: STOP, a fault or a link loss.  Nothing is
 *        sent after it.  A write cut after its first frame leaves the ESC's
 *        table not known; oy_session_cut_write() says so.
 * @param s the session
 */
void oy_session_abort(oy_session_t *s);

/**
 * @brief Whether a write is under way that a cut leaves half done.
 * @param s the session
 * @return true from a write's first frame to the end of its read after
 */
bool oy_session_writing(const oy_session_t *s);

/**
 * @brief Whether the last operation was a write cut after its first frame
 *        and before its read after.
 * @param s the session
 * @return true when the ESC's table is not known
 */
bool oy_session_cut_write(const oy_session_t *s);

/**
 * @brief Run the session: reads the clock, drains the driver, sends at most
 *        one frame, returns.
 * @param s the session
 * @return OY_BUSY while an operation runs, then its result
 */
oy_res_t oy_session_step(oy_session_t *s);

#ifdef __cplusplus
}
#endif

#endif /* OPENYGE_SESSION_H */
