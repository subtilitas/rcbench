/**
 * @file
 * @brief The panel's half of the HITEC (0x34) link page, protocol 4.23 on:
 *        one step of a session with a servo by Hitec turned into page
 *        frames, and the page's answer turned back into its result.
 *
 * Hitec is a name or trademark of its owner; it is used here only to say
 * which servos this module works with.
 *
 * The session runs on the panel (protocols/hitec/include/hitec_session.h).
 * The page holds no session: it runs one raw transaction at a time
 * (shared/outputs/hitec_port.h).  This module takes one request at a time:
 *
 *   - hitec_link_open(): OPEN, the port taken from PWM for one family and
 *     drive; the outcome comes when the page has left STOPPING;
 *   - hitec_link_step(): a session step, XFER (SEND, LISTEN or LEVEL) or
 *     POWER; the outcome carries the step's hitec_ses_result_t;
 *   - hitec_link_clear(): CLEAR, the STOPPED flag cleared;
 *   - hitec_link_close(): CLOSE, of any port the page holds, one another
 *     writer opened included.
 *
 * A request goes out in this order:
 *
 *   1. OPEN and a step: the staged registers 4 to 21 and VIEW (register 22,
 *      always LINK_HITEC_VIEW_RUN), 5 frames of 4, 4, 4, 4 and 3 registers;
 *   2. the command, registers 0 to 3, one frame, with SEQ one above the
 *      page's, the CRC of the staged registers and, for a step that puts a
 *      byte on the wire, LINK_HITEC_WRITE_KEY.  A command without an
 *      answer is sent again as it was, up to HITEC_LINK_RETRIES times; the
 *      page takes it once.  After the last try registers 0 to 15 are read:
 *      the page's SEQ, and a state the command leaves, tell whether it
 *      took the command.  A SEQ whose state does not fit is another
 *      writer's command: HITEC_LINK_OUT_RESTARTED;
 *   3. registers 0 to 15 are read: the command started the operation or
 *      the page says why not (LINK_HT_RESULT's high byte);
 *   4. registers 0 to 15 are read every HITEC_LINK_POLL_MS until the page
 *      is out of STOPPING, BUSY and POWER_OFF.  The last read holds the
 *      capture: STATE, RESULT, CAP, DEC, VALUE and the 16 bytes;
 *   5. registers 0 to 31 are read once: registers 16 to 31 are the run's
 *      (the decoder's delay, the times, the line and the pin) when the
 *      same read shows VIEW at LINK_HITEC_VIEW_RUN and the SEQ, the
 *      operation and the capture of step 4; otherwise the outcome has no
 *      run.
 *
 * hitec_link_outcome() then hands out how it went, once.
 *
 * A step that runs HITEC_LINK_RUN_MAX_MS after its command was taken is
 * aborted from here: longer than the longest POWER the page runs, 3000ms
 * for the supply to read off, 10,000ms off and 10,000ms of a transaction.
 *
 * hitec_link_abort() is for STOP and for the operator's cancel: a request
 * whose command has not gone is dropped, and the command ABORT follows
 * whenever the page is known.  A command sent without an answer is never
 * sent again after it; the page's SEQ tells whether it was taken.
 *
 * Between requests registers 0 to 15 are read every HITEC_LINK_HELD_MS
 * while the page holds a port and every HITEC_LINK_IDLE_MS while it holds
 * none.  An IOmcu whose protocol minor is below HITEC_LINK_MINOR has no
 * HITEC page and is sent nothing.
 *
 * Pure C, no link of its own: the caller makes each exchange and reports
 * how it went (hitec_link_next() and hitec_link_done(), or
 * hitec_link_service() with the caller's exchange function).  One caller
 * only.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_HITEC_LINK_H
#define RCBENCH_HITEC_LINK_H

#include <stdbool.h>
#include <stdint.h>

#include "hitec_session.h"
#include "hitec_wire.h"
#include "link_pages.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The protocol minor whose IOmcu has the HITEC page. */
#define HITEC_LINK_MINOR LINK_MINOR_HITEC

/** @brief Registers 0 to 15 are due this long after the last read while a
 *         request runs: one 5ms pass of the panel's control task. */
#define HITEC_LINK_POLL_MS 5u
/** @brief And this long after it while the page holds a port. */
#define HITEC_LINK_HELD_MS 100u
/** @brief And this long after it while the page holds none. */
#define HITEC_LINK_IDLE_MS 500u
/** @brief Exchanges without an answer, in a row, after which a request is
 *         given up. */
#define HITEC_LINK_RETRIES 3u
/** @brief Exchanges one hitec_link_service() call makes at most. */
#define HITEC_LINK_PASS_MAX 4u
/** @brief A request still running this long after its command was taken
 *         is aborted, ms. */
#define HITEC_LINK_RUN_MAX_MS 25000u
/** @brief LINK_HT_I_PIN while the driver holds no pin. */
#define HITEC_LINK_NO_PIN 0xFFFFu

/** @brief An exchange without an answer. */
#define HITEC_LINK_NO_ANSWER (-1)
/** @brief An exchange answered with ACK or DATA; a NACK's reason is above
 *         0. */
#define HITEC_LINK_ACK       0

/** @brief Where the module is. */
typedef enum {
    HITEC_LINK_DOWN = 0,   /**< no IOmcu answers */
    HITEC_LINK_NO_PAGE,    /**< the IOmcu has no HITEC page */
    HITEC_LINK_SYNC,       /**< the page has not been read yet */
    HITEC_LINK_READY,      /**< a request is taken */
    HITEC_LINK_SENDING,    /**< the staged registers and the command go out */
    HITEC_LINK_RUNNING,    /**< the page runs the request */
    HITEC_LINK_FETCHING,   /**< registers 0 to 31 are read */
} hitec_link_phase_t;

/** @brief What a request answers. */
typedef enum {
    HITEC_LINK_START_OK = 0,
    HITEC_LINK_START_DOWN,     /**< no IOmcu answers */
    HITEC_LINK_START_NO_PAGE,  /**< the IOmcu has no HITEC page */
    HITEC_LINK_START_BUSY,     /**< the phase is not HITEC_LINK_READY, or
                                    an exchange is still out */
    HITEC_LINK_START_ARG,      /**< no such port, family, drive or step */
} hitec_link_start_t;

/** @brief How a request went. */
typedef enum {
    HITEC_LINK_OUT_NONE = 0,
    HITEC_LINK_OUT_DONE,       /**< it ran; result says how it ended */
    HITEC_LINK_OUT_REFUSED,    /**< the page took the command and started
                                    nothing; refusal says why */
    HITEC_LINK_OUT_NACK,       /**< the page refused a frame; nack says why */
    HITEC_LINK_OUT_NO_ANSWER,  /**< HITEC_LINK_RETRIES exchanges unanswered,
                                    or the link went */
    HITEC_LINK_OUT_RESTARTED,  /**< the page's SEQ is not the panel's, or
                                    its operation went back to NONE: the
                                    IOmcu started again, or another writer
                                    sent a command */
    HITEC_LINK_OUT_DROPPED,    /**< hitec_link_abort() before the page took
                                    the command; nothing of it ran */
} hitec_link_outcome_kind_t;

/** @brief The outcome of a request that ended. */
typedef struct {
    hitec_link_outcome_kind_t kind; /**< how it went */
    link_hitec_op_t op;         /**< OPEN, CLOSE, CLEAR, SEND, LISTEN,
                                     LEVEL or POWER */
    uint8_t  port;              /**< the port the request named */
    uint32_t id;                /**< the caller's number of the request */
    link_hitec_refusal_t refusal; /**< HITEC_LINK_OUT_REFUSED */
    uint8_t  nack;              /**< HITEC_LINK_OUT_NACK: link_nack_t */
    bool     taken;             /**< the page took the command: it may
                                     have run */
    uint8_t  result;            /**< HITEC_LINK_OUT_DONE:
                                     link_hitec_result_t */
    /**
     * @brief The step's result for hitec_session_feed(): RAN for a request
     *        that ended LINK_HITEC_RESULT_DONE, with the decoder's code and
     *        value; CUT for one that ended LINK_HITEC_RESULT_CUT; FAILED for
     *        every other outcome.
     */
    hitec_ses_result_t ses;
    bool     have_cap;          /**< a step's capture is below */
    uint16_t cap;               /**< LINK_HT_CAP as read */
    uint8_t  answer_id;         /**< LINK_HT_DEC's high byte: the D series
                                     answer's ID */
    uint8_t  n;                 /**< bytes held, 0 to HITEC_RX_MAX */
    uint8_t  bytes[HITEC_RX_MAX]; /**< the bytes, in the order they came */
    bool     have_run;          /**< registers 16 to 31 were read with
                                     VIEW 2, for this capture */
    uint32_t delay_ns;          /**< the decoder's delay, ns */
    uint32_t t_us;              /**< from the start to the capture, us */
    uint32_t window_us;         /**< the window the capture asked, us */
    uint16_t framing;           /**< bit k: byte k's stop bit was bad */
} hitec_link_outcome_t;

/** @brief The screen's copy of the page: a plain value. */
typedef struct {
    hitec_link_phase_t phase; /**< where the module is */
    uint16_t minor;          /**< the IOmcu's protocol minor; 0 while
                                  none answers */
    bool     hold_supply;    /**< the IOmcu holds SUPPLY from OPEN to
                                  CLOSE (LINK_MINOR_HITEC_HOLD) */
    bool     known;          /**< the fields below were read */
    link_hitec_state_t state; /**< where the page's port is */
    uint8_t  port_no;        /**< which port: the one held, or the last */
    uint16_t flags;          /**< LINK_HT_F_*: STOPPED, RAIL, CAPTURE, KEY,
                                  PRECHARGE */
    bool     held;           /**< the page holds a port: not FREE */
    bool     stopped;        /**< LINK_HT_F_STOPPED */
    bool     ours;           /**< this module opened the held port since
                                  the link came up; false for a port
                                  another writer holds, and for one held
                                  over a link loss or a panel restart */
    uint16_t seq;            /**< the page's SEQ */
    link_hitec_op_t op;      /**< the last operation the page started */
    uint16_t count;          /**< transactions since the port was taken,
                                  to 4095 */
    bool     have_line;      /**< the fields below were read since OPEN */
    uint16_t pin;            /**< the port's GPIO; HITEC_LINK_NO_PIN */
    uint16_t line;           /**< LINK_HT_I_LINE: family, drive,
                                  LINK_HT_LINE_PRECHARGE */
    uint16_t bit_ns;         /**< the bit time of the open line, ns */
} hitec_link_readout_t;

/** @brief One exchange for the caller to make. */
typedef struct {
    bool     write;          /**< a write of regs; otherwise a read */
    uint8_t  page;           /**< LINK_PAGE_HITEC */
    uint8_t  off;            /**< first register of the page */
    uint8_t  n;              /**< registers, 1 to 32; a write 1 to 4 */
    uint16_t regs[4];        /**< a write's registers */
} hitec_link_xfer_t;

/**
 * @brief The caller's exchange: makes @p x and fills @p regs with the
 *        x->n registers of a read that was answered with DATA.
 *
 * @param ctx   The caller's context.
 * @param x     The exchange to make.
 * @param regs  Receives a read's registers, 32 at most; NULL for a write.
 * @return HITEC_LINK_ACK, a NACK's reason (LINK_NACK_*), or
 *         HITEC_LINK_NO_ANSWER.
 */
typedef int (*hitec_link_xchg_fn)(void *ctx, const hitec_link_xfer_t *x,
                                  uint16_t *regs);

/** @brief Staged registers 4 to 21 and VIEW, as they go out. */
#define HITEC_LINK_STAGED (LINK_HT_W_STAGED + 1u)

/** @brief The state of the HITEC link module. */
typedef struct {
    uint8_t  up;             /**< an IOmcu answers */
    uint8_t  page;           /**< it has the page */
    uint16_t minor;          /**< its protocol minor */
    uint8_t  known;          /**< status was read since the link came up */
    uint8_t  phase;          /**< hitec_link_phase_t */
    uint8_t  pending;        /**< the exchange in flight */
    uint8_t  tries;          /**< exchanges unanswered in a row */
    uint8_t  polled;         /**< status_ms is meaningful */
    uint8_t  poll_now;       /**< a status read is owed at once */
    uint32_t status_ms;      /**< when status was last asked */
    uint32_t now_ms;         /**< the time hitec_link_next() last had */
    uint16_t seq;            /**< the page's SEQ */
    uint16_t status[LINK_HT_DATA]; /**< registers 0 to 15 as last read */
    uint8_t  ours;           /**< this module opened the held port */
    uint8_t  have_line;      /**< line[] was read since the OPEN */
    uint16_t line[3];        /**< LINK_HT_I_BIT_NS, _LINE, _PIN */

    /* The request. */
    uint8_t  active;         /**< a request is on its way */
    uint8_t  op;             /**< link_hitec_op_t */
    uint8_t  port;           /**< the port it names */
    uint32_t id;             /**< the caller's number of it */
    uint8_t  stage_at;       /**< staged windows written */
    uint16_t staged[HITEC_LINK_STAGED]; /**< registers 4 to 22 as they go
                                             out */
    uint16_t cmd_proto[LINK_HT_W_CMD_FRAME]; /**< the command less its SEQ */
    uint8_t  verdict;        /**< the command was taken; the status that
                                  says what became of it is owed */
    uint8_t  taken;          /**< the page took the command */
    uint32_t taken_ms;       /**< when the page took it */
    uint8_t  limit_cut;      /**< HITEC_LINK_RUN_MAX_MS asked an abort */

    /* The command on its way: the request's, or an abort. */
    uint8_t  cmd_live;       /**< it goes, or goes again */
    uint8_t  cmd_abort;      /**< it is an ABORT */
    uint8_t  cmd_tries;      /**< tries of it without an answer */
    uint8_t  cmd_doubt;      /**< it went without an answer and is not
                                  sent again; the next status read says
                                  whether the page took it */
    uint8_t  doubt_abort;    /**< the doubt came from an abort, not from
                                  HITEC_LINK_RETRIES unanswered tries */
    uint8_t  doubt_nack;     /**< the doubt came from a NACK of a try
                                  after one without an answer: its
                                  reason */
    uint16_t cmd[LINK_HT_W_CMD_FRAME]; /**< its registers 0 to 3 */
    uint8_t  abort_wanted;   /**< an ABORT is owed */

    uint16_t end[LINK_HT_DATA]; /**< registers 0 to 15 as the request
                                     ended */
    uint8_t  outcome_new;    /**< outcome is not handed out yet */
    hitec_link_outcome_t outcome; /**< how the last request went */
} hitec_link_t;

/**
 * @brief Sets up the module: no IOmcu, nothing owed.
 *
 * @param l The module.
 */
void hitec_link_init(hitec_link_t *l);

/**
 * @brief The link went: a request on its way ends as
 *        HITEC_LINK_OUT_NO_ANSWER and nothing is read or written until it
 *        comes up again.
 *
 * The page's port, when held, is no longer this module's (ours).
 *
 * @param l The module.
 */
void hitec_link_lost(hitec_link_t *l);

/**
 * @brief An IOmcu answered with protocol minor @p minor.
 *
 * Below HITEC_LINK_MINOR it has no page and nothing is sent to it.
 *
 * @param l     The module.
 * @param minor The IOmcu's protocol minor.
 */
void hitec_link_came_up(hitec_link_t *l, uint16_t minor);

/**
 * @brief Whether the IOmcu has the page.
 *
 * @param l The module.
 * @return true while an IOmcu answers and has the page.
 */
bool hitec_link_has_page(const hitec_link_t *l);

/**
 * @brief OPEN @p port for @p family and @p drive.
 *
 * The outcome comes when the page has left STOPPING: result DONE with the
 * port in OPEN, NO_LINE, or CUT by a stop.  The line's pin is read then.
 *
 * @param l       The module.
 * @param port    The programming port, 0 to LINK_PG_PORTS_MAX - 1.
 * @param family  hitec_family_t.
 * @param drive   hitec_drive_t.
 * @param id      The caller's number of the request.
 * @return HITEC_LINK_START_OK when the request is on its way, else why not;
 *         nothing is sent then.
 */
hitec_link_start_t hitec_link_open(hitec_link_t *l, uint8_t port,
                                   uint8_t family, uint8_t drive,
                                   uint32_t id);

/**
 * @brief CLOSE @p port: any port the page holds, one another writer
 *        opened included.  The supply is left as it is.
 *
 * @param l    The module.
 * @param port The programming port, 0 to LINK_PG_PORTS_MAX - 1.
 * @param id   The caller's number of the request.
 * @return As hitec_link_open().
 */
hitec_link_start_t hitec_link_close(hitec_link_t *l, uint8_t port,
                                    uint32_t id);

/**
 * @brief CLEAR the STOPPED flag of @p port.
 *
 * The page refuses it with LINK_HITEC_REF_UNSAFE while its heartbeat is
 * not trusted or its link is silent.
 *
 * @param l    The module.
 * @param port The programming port, 0 to LINK_PG_PORTS_MAX - 1.
 * @param id   The caller's number of the request.
 * @return As hitec_link_open().
 */
hitec_link_start_t hitec_link_clear(hitec_link_t *l, uint8_t port,
                                    uint32_t id);

/**
 * @brief Run one session step on @p port: an XFER as SEND, LISTEN or LEVEL
 *        by its kind, or a POWER at @p mv and @p ma with the step's off
 *        time and its LISTEN.
 *
 * The step's decoder and its argument go to the page.  The key goes with
 * every SEND and with a LISTEN or POWER that answers.  The set points and
 * the off time are not checked here: the page refuses them with
 * LINK_HITEC_REF_SUPPLY_RANGE.
 *
 * @param l     The module.
 * @param port  The programming port, 0 to LINK_PG_PORTS_MAX - 1.
 * @param st    The step, XFER or POWER; its transaction valid
 *              (hitec_xfer_valid()), a POWER's a LISTEN.
 * @param mv    POWER: the supply voltage, mV.
 * @param ma    POWER: its current limit, mA.
 * @param id    The caller's number of the request.
 * @return As hitec_link_open().
 */
hitec_link_start_t hitec_link_step(hitec_link_t *l, uint8_t port,
                                   const hitec_ses_step_t *st, uint16_t mv,
                                   uint16_t ma, uint32_t id);

/**
 * @brief STOP, or the operator's cancel.
 *
 * A request whose command has not gone is dropped
 * (HITEC_LINK_OUT_DROPPED).  A command that went without an answer is not
 * sent again: a read of registers 0 to 15 follows, and a page at the SEQ
 * before the command did not take it, which drops the request too.  The
 * command ABORT is sent as soon as the page is known; a request that runs
 * ends with LINK_HITEC_RESULT_CUT, a step's capture marked aborted.  An
 * ABORT refused with LINK_NACK_BAD_VALUE goes again after a read of the
 * page, with its SEQ.
 *
 * @param l The module.
 */
void hitec_link_abort(hitec_link_t *l);

/**
 * @brief The next exchange owed at @p now_ms, into @p x; false for none.
 *
 * The caller makes it and reports with hitec_link_done().
 *
 * @param l      The module.
 * @param now_ms The time, ms.
 * @param x      Receives the exchange.
 * @return true when an exchange is owed.
 */
bool hitec_link_next(hitec_link_t *l, uint32_t now_ms, hitec_link_xfer_t *x);

/**
 * @brief How the exchange hitec_link_next() gave went.
 *
 * @param l      The module.
 * @param result HITEC_LINK_ACK, a NACK's reason or HITEC_LINK_NO_ANSWER.
 * @param regs   A read's registers, x.n of them; NULL for a write.
 */
void hitec_link_done(hitec_link_t *l, int result, const uint16_t *regs);

/**
 * @brief Up to HITEC_LINK_PASS_MAX exchanges through @p xchg: one pass.
 *
 * Stops after an exchange without an answer.
 *
 * @param l      The module.
 * @param now_ms The time, ms.
 * @param xchg   The caller's exchange.
 * @param ctx    Its context.
 * @return The exchanges made.
 */
unsigned hitec_link_service(hitec_link_t *l, uint32_t now_ms,
                            hitec_link_xchg_fn xchg, void *ctx);

/**
 * @brief Where the module is.
 *
 * @param l The module.
 * @return The phase; HITEC_LINK_DOWN for NULL.
 */
hitec_link_phase_t hitec_link_phase(const hitec_link_t *l);

/**
 * @brief How the last request went, once.
 *
 * @param l   The module.
 * @param out Receives the outcome.
 * @return true when a new outcome was written to @p out.
 */
bool hitec_link_outcome(hitec_link_t *l, hitec_link_outcome_t *out);

/**
 * @brief The screen's copy of the page.
 *
 * @param l The module.
 * @param r Receives the copy.
 */
void hitec_link_readout(const hitec_link_t *l, hitec_link_readout_t *r);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_HITEC_LINK_H */
