/**
 * @file
 * @brief The panel's half of the YGE_PROG and YGE_DATA link pages, protocol
 *        4.42 on: one operation of a session with a YGE ESC (electronic speed
 *        controller) on its telemetry wire, asked and followed.
 *
 * The session and its rules run on the IOmcu (shared/outputs/yge_port.h,
 * docs/LinkYge.md).  A panel names an operation and hands over a draft; there
 * is no raw transaction.  This module takes one request at a time:
 *
 *   - yge_link_open(), yge_link_enter(), yge_link_hand_done(),
 *     yge_link_read(), yge_link_write(), yge_link_leave(), yge_link_clear()
 *     and yge_link_close();
 *   - yge_link_outcome() hands out how it went, once.
 *
 * A request goes out in this order:
 *
 *   1. WRITE: the draft's blocks, LINK_YD_DRAFT and, for a table of more than
 *      32 entries, the one after it: 9 frames a block;
 *   2. OPEN, ENTER and WRITE: the staged registers 4 to 11, 2 frames;
 *   3. the command with SEQ one above the page's, sent again as it was up to
 *      YGE_LINK_RETRIES times without an answer: the page takes it once.  Only
 *      ENTER and WRITE carry LINK_YGE_KEY.  A caller asks them only at the end
 *      of the operator's 2s hold (yge_prog.h);
 *   4. registers 0 to 22 every YGE_LINK_POLL_MS until the page has left a state
 *      that runs.
 *
 * yge_link_stop() and yge_link_abort() go ahead of everything: a request whose
 * command has not gone is dropped, and the operation goes as soon as the page
 * is known and holds a pin.  A BAD_VALUE, a SEQ another writer moved, is
 * followed by a read and the operation again, YGE_LINK_URGENT_TRIES times at
 * most.  It is dropped on a free page only after a read answered since the
 * ask.  It stays over a lost link.
 *
 * YGE_LINK_STATUS_FAILS failed reads of the page in a row end a request.  With
 * a hold in the last read that is an error, and the STOP goes.
 *
 * **After a panel restart** the page may hold a pin nobody here knows of: the
 * first read that shows one is followed by CLOSE, again every
 * YGE_LINK_CLOSE_RETRY_MS while the page answers WRITING or NOT_BACK.  No
 * request is taken until it is free.  A NOT_BACK after any CLOSE retries the
 * same way.  A link that came back without a restart closes nothing:
 * readout.ours is false, and the caller decides.
 *
 * **Errors.** yge_link_faults() counts each rising LINK_YP_F_FAULT, each
 * arrival at LINK_YGE_NOT_BACK and each request given up over a hold.  A FAULT
 * that stands on the first read after the link came up is not counted: the
 * IOmcu has switched everything off, and no error happens now.
 * yge_link_not_back() hands out once that a pin did not come back, with its
 * port.
 *
 * **The read copy.** Between requests registers 0 to 22 are read every
 * YGE_LINK_HELD_MS while the page holds a pin and every YGE_LINK_IDLE_MS while
 * it holds none.  A read copy that changed is fetched by itself, block
 * LINK_YD_READ and for more than 32 entries the one after it, and its
 * oy_guard_table_crc() is held against LINK_YP_READ_CRC.  A copy with another
 * CRC is fetched again, YGE_LINK_FETCH_TRIES times in all, then given up until
 * the page names another copy.  yge_link_image() gives it, with its number.
 * An IOmcu below YGE_LINK_MINOR is sent nothing.
 *
 * Pure C, no link of its own: the caller makes each exchange and reports how it
 * went (yge_link_next() and yge_link_done(), or yge_link_service()).  One
 * caller only.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_YGE_LINK_H
#define RCBENCH_YGE_LINK_H

#include <stdbool.h>
#include <stdint.h>

#include "link_pages.h"
#include "openyge.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The protocol minor whose IOmcu has the pages. */
#define YGE_LINK_MINOR LINK_MINOR_YGE
/** @brief Registers 0 to 22 are due this long after the last read while a
 *         request runs: one 5ms pass of the panel's control task. */
#define YGE_LINK_POLL_MS 5u
/** @brief And this long after it while the page holds a pin. */
#define YGE_LINK_HELD_MS 100u
/** @brief And this long after it while the page holds none. */
#define YGE_LINK_IDLE_MS 500u
/** @brief Exchanges without an answer, in a row, after which a request is
 *         given up. */
#define YGE_LINK_RETRIES 3u
/** @brief Exchanges one yge_link_service() call makes at most. */
#define YGE_LINK_PASS_MAX 4u
/** @brief A STOP or ABORT is sent again this many times after a BAD_VALUE. */
#define YGE_LINK_URGENT_TRIES 6u
/** @brief The CLOSE after a panel restart goes again this long after the page
 *         did not free its pin, ms. */
#define YGE_LINK_CLOSE_RETRY_MS 1000u
/** @brief A request running this long after its command was taken is cut from
 *         here with ABORT, ms.  The longest is a WRITE: a read of 10000ms at
 *         most, 31 writes of 3 tries of 400ms, and a read of 10000ms after,
 *         57200ms; an ENTER is 3000ms + 3000ms + 20000ms + 3000ms and its
 *         requests. */
#define YGE_LINK_RUN_MAX_MS 90000u
/** @brief Failed reads of the page in a row after which a request is given up:
 *         50ms at YGE_LINK_POLL_MS. */
#define YGE_LINK_STATUS_FAILS 10u
/** @brief Fetches of a read copy, the first included, before one with the
 *         wrong CRC is given up. */
#define YGE_LINK_FETCH_TRIES 3u
/** @brief Values of the read copy and the draft at most: 2 blocks. */
#define YGE_LINK_VALUES_MAX (2u * LINK_YD_COUNT)

_Static_assert(YGE_LINK_VALUES_MAX == OPENYGE_MAX_PARAMS,
               "two blocks hold the largest table");

/** @brief An exchange without an answer. */
#define YGE_LINK_NO_ANSWER (-1)
/** @brief An exchange answered with ACK or DATA; a NACK's reason is above 0. */
#define YGE_LINK_ACK 0

/** @brief Where the module is. */
typedef enum {
    YGE_LINK_DOWN = 0,   /**< no IOmcu answers                                */
    YGE_LINK_NO_PAGE,    /**< the IOmcu has no YGE_PROG page                  */
    YGE_LINK_SYNC,       /**< the page has not been read yet, or the CLOSE
                              after a panel restart is under way              */
    YGE_LINK_READY,      /**< a request is taken                              */
    YGE_LINK_SENDING,    /**< the draft, the staged registers and the command
                              go out                                          */
    YGE_LINK_RUNNING,    /**< the page runs the request                       */
    YGE_LINK_FETCHING,   /**< the read copy comes                             */
} yge_link_phase_t;

/** @brief What a request answers. */
typedef enum {
    YGE_LINK_START_OK = 0,
    YGE_LINK_START_DOWN,    /**< no IOmcu answers                             */
    YGE_LINK_START_NO_PAGE, /**< the IOmcu has no page                        */
    YGE_LINK_START_BUSY,    /**< the phase is not YGE_LINK_READY, or an
                                 exchange is still out                        */
    YGE_LINK_START_ARG,     /**< a NULL, or a draft of no length that fits    */
} yge_link_start_t;

/** @brief How a request went. */
typedef enum {
    YGE_LINK_OUT_NONE = 0,
    YGE_LINK_OUT_DONE,      /**< it ran; result says how it ended             */
    YGE_LINK_OUT_REFUSED,   /**< the page took the command and started
                                 nothing; refusal says why                    */
    YGE_LINK_OUT_NACK,      /**< the page refused a frame; nack says why      */
    YGE_LINK_OUT_NO_ANSWER, /**< YGE_LINK_RETRIES exchanges unanswered, or the
                                 link went                                    */
    YGE_LINK_OUT_RESTARTED, /**< the page's SEQ is not the panel's: the IOmcu
                                 started again, or another writer sent a
                                 command                                      */
    YGE_LINK_OUT_DROPPED,   /**< a stop or abort before the page took the
                                 command; nothing of it ran                   */
} yge_link_outcome_kind_t;

/** @brief The outcome of a request that ended. */
typedef struct {
    yge_link_outcome_kind_t kind; /**< how it went                            */
    uint8_t  op;      /**< a link_yge_op_t                                    */
    uint32_t id;      /**< the caller's number of the request                 */
    uint8_t  result;  /**< YGE_LINK_OUT_DONE: a link_yge_result_t             */
    uint8_t  refusal; /**< YGE_LINK_OUT_REFUSED: a link_yge_ref_t             */
    uint8_t  nack;    /**< YGE_LINK_OUT_NACK: a link_nack_t                   */
    uint8_t  state;   /**< the page's state at the end, a link_yge_state_t    */
    uint8_t  ses;     /**< the session's last result, an oy_res_t             */
    uint8_t  guard;   /**< the guard's last refusal, an oy_ref_t              */
    bool     taken;   /**< the page took the command: it may have run         */
} yge_link_outcome_t;

/** @brief The caller's copy of the page: a plain value. */
typedef struct {
    yge_link_phase_t phase; /**< where the module is                          */
    uint16_t minor;     /**< the IOmcu's protocol minor; 0 while none answers */
    bool     known;     /**< the fields below were read                       */
    uint8_t  state;     /**< a link_yge_state_t                               */
    uint16_t flags;     /**< LINK_YP_F_*                                      */
    bool     held;      /**< the page holds a pin, or takes one: not FREE     */
    bool     ours;      /**< this module opened it since the link came up     */
    uint8_t  port;      /**< the programming port held, 0xFF when free        */
    uint8_t  line;      /**< a link_yge_line_t                                */
    uint8_t  gpio;      /**< the GPIO; 0xFF for none                          */
    uint8_t  op;        /**< the last operation                               */
    uint8_t  result;    /**< its link_yge_result_t                            */
    uint8_t  refusal;   /**< the last command's link_yge_ref_t                */
    uint8_t  ses;       /**< the session's last result                        */
    uint8_t  guard;     /**< the guard's last refusal                         */
    uint16_t bad_index; /**< the index the guard refused                      */
    uint16_t type;      /**< the read copy's type code, index 11              */
    uint32_t firmware;  /**< its firmware, 103555 for V1.03555                */
    uint32_t serial;    /**< its serial number                                */
    uint16_t entries;   /**< entries of the read copy; 0 before a read        */
    uint16_t reads;     /**< LINK_YP_READS                                    */
    uint16_t read_crc;  /**< LINK_YP_READ_CRC                                 */
    uint8_t  seen;      /**< indices of the table being read seen so far      */
    uint8_t  writing;   /**< the index being written, 0xFF none               */
    uint16_t t_ms;      /**< ms in this state, stops at 65535                 */
    uint8_t  ses_state; /**< the session's state, an oy_state_t               */
    uint8_t  ro_why;    /**< why the read copy is read-only, an oy_ref_t      */
    uint16_t misses;    /**< requests and writes not answered in time         */
    uint16_t dropped;   /**< frames dropped                                   */
} yge_link_readout_t;

/** @brief The ESC's table as the IOmcu holds it. */
typedef struct {
    uint32_t gen;      /**< counts the copies; 0 for none yet                 */
    uint16_t reads;    /**< LINK_YP_READS it belongs to                       */
    uint16_t read_crc; /**< LINK_YP_READ_CRC it has                           */
    uint16_t entries;  /**< values of @c value                                */
    uint16_t value[YGE_LINK_VALUES_MAX]; /**< the table, index 0 first; 0 past
                                              @c entries                      */
} yge_link_image_t;

/** @brief What ENTER stages. */
typedef struct {
    uint8_t  power;   /**< a link_yge_power_t                                 */
    uint16_t mv;      /**< the supply's voltage, mV                           */
    uint16_t ma;      /**< the supply's limit, mA                             */
    uint16_t boot_ms; /**< the boot wait, ms                                  */
} yge_link_power_t;

/** @brief One exchange for the caller to make. */
typedef struct {
    bool     write;   /**< a write of regs; otherwise a read                  */
    uint8_t  page;    /**< LINK_PAGE_YGE_PROG or LINK_PAGE_YGE_DATA           */
    uint8_t  off;     /**< first register of the page                         */
    uint8_t  n;       /**< registers, 1 to 32; a write 1 to 4                 */
    uint16_t regs[4]; /**< a write's registers                                */
} yge_link_xfer_t;

/**
 * @brief The caller's exchange: makes @p x and fills @p regs with the x->n
 *        registers of a read that was answered with DATA.
 *
 * @param ctx  The caller's context.
 * @param x    The exchange to make.
 * @param regs Receives a read's registers, 32 at most; NULL for a write.
 * @return YGE_LINK_ACK, a NACK's reason, or YGE_LINK_NO_ANSWER.
 */
typedef int (*yge_link_xchg_fn)(void *ctx, const yge_link_xfer_t *x,
                                uint16_t *regs);

/** @brief The state of the module. */
typedef struct {
    uint8_t  up;         /**< an IOmcu answers                                */
    uint8_t  has_page;   /**< it has the page                                 */
    uint16_t minor;      /**< its protocol minor                              */
    uint8_t  known;      /**< status was read since the link came up          */
    uint8_t  polled;     /**< status_ms is meaningful                         */
    uint8_t  poll_now;   /**< a status read is owed at once                   */
    uint8_t  pending;    /**< the exchange in flight                          */
    uint8_t  tries;      /**< exchanges unanswered in a row                   */
    uint8_t  status_fails; /**< reads of the page failed in a row             */
    uint32_t status_ms;  /**< when status was last asked                      */
    uint32_t now_ms;     /**< the time yge_link_next() last had               */
    uint16_t status[LINK_YP_COUNT]; /**< the page as last read                */
    uint8_t  ours;       /**< this module opened the pin held                 */
    uint32_t faults;     /**< errors counted, modulo 2^32                     */
    uint8_t  not_back;   /**< a NOT_BACK not handed out                       */
    uint8_t  not_back_port; /**< its port                                     */

    /* The CLOSE after a restart. */
    uint8_t  close_owed; /**< the first read is to free what it finds         */
    uint8_t  close_timed; /**< close_at is meaningful                         */
    uint32_t close_at;   /**< when it goes again                              */
    uint8_t  internal;   /**< the request is that CLOSE: no outcome           */

    /* The request. */
    uint8_t  active;     /**< a request is on its way                         */
    uint8_t  op;         /**< a link_yge_op_t                                 */
    uint32_t id;         /**< the caller's number of it                       */
    uint16_t send_at;    /**< frames of it gone                               */
    uint16_t staged[LINK_YP_W_STAGED]; /**< registers 4 to 11 as they go out  */
    uint16_t draft[YGE_LINK_VALUES_MAX]; /**< the draft                       */
    uint16_t draft_n;    /**< its values                                      */
    uint8_t  cmd_gone;   /**< the command went, answered or not               */
    uint8_t  verdict;    /**< acknowledged; the status that says what became
                              of it is owed                                   */
    uint8_t  doubt;      /**< no answer YGE_LINK_RETRIES times: a read says
                              whether the page took it                        */
    uint8_t  taken;      /**< the page took it                                */
    uint32_t taken_ms;   /**< when                                            */
    uint8_t  limit_cut;  /**< YGE_LINK_RUN_MAX_MS asked an ABORT              */

    /* STOP or ABORT. */
    uint8_t  urgent;     /**< the op owed, 0 for none                         */
    uint8_t  urgent_tries; /**< tries of it                                   */
    uint8_t  urgent_read; /**< a read of the page was answered since the ask */
    uint32_t stops_seen; /**< the stop count yge_link_stop_follow() served    */
    uint16_t last_seq;   /**< the SEQ of the last command sent                */

    /* The read copy on its way. */
    uint8_t  fetching;   /**< it comes                                        */
    uint8_t  fetch_at;   /**< blocks of it done                               */
    uint8_t  fetch_tries; /**< fetches of it that ended with the wrong CRC    */
    uint8_t  fetch_given_up; /**< of the READS and CRC of fetch_reads, _crc   */
    uint16_t fetch_reads; /**< LINK_YP_READS it was asked for                 */
    uint16_t fetch_crc;  /**< LINK_YP_READ_CRC it was asked for               */
    uint16_t fetch_n;    /**< LINK_YP_ENTRIES it was asked for                */
    uint8_t  block_set;  /**< LINK_YP_W_BLOCK has fetch_at's block            */
    uint16_t fetch_buf[YGE_LINK_VALUES_MAX]; /**< the blocks as they come     */
    yge_link_image_t image; /**< the copy                                     */

    uint8_t  outcome_new; /**< outcome is not handed out yet                  */
    yge_link_outcome_t outcome; /**< how the last request went                */
} yge_link_t;

/**
 * @brief Sets up the module at a panel's start: no IOmcu, and a CLOSE owed for
 *        a pin the first read finds held.
 * @param l The module.
 */
void yge_link_init(yge_link_t *l);

/**
 * @brief The link went: a request on its way ends as YGE_LINK_OUT_NO_ANSWER,
 *        and nothing is read or written until it comes up again.
 * @param l The module.
 */
void yge_link_lost(yge_link_t *l);

/**
 * @brief An IOmcu answered with protocol minor @p minor.
 * @param l     The module.
 * @param minor The IOmcu's protocol minor.
 */
void yge_link_came_up(yge_link_t *l, uint16_t minor);

/**
 * @brief Whether the IOmcu has the page.
 * @param l The module.
 * @return true while an IOmcu answers and has it.
 */
bool yge_link_has_page(const yge_link_t *l);

/**
 * @brief OPEN the programming port @p port: the outcome comes when the page
 *        has left STOPPING.
 * @param l    The module.
 * @param port The port, 0 to 3; the page judges it.
 * @param id   The caller's number of the request.
 * @return YGE_LINK_START_OK when it is on its way, else why not; nothing is
 *         sent then.
 */
yge_link_start_t yge_link_open(yge_link_t *l, uint8_t port, uint32_t id);

/**
 * @brief ENTER, with the key: the outcome comes at READY, at HAND_WAIT or at
 *        its end.
 * @param l  The module.
 * @param p  What it stages; the values are the page's to judge.
 * @param id The caller's number of the request.
 * @return As yge_link_open().
 */
yge_link_start_t yge_link_enter(yge_link_t *l, const yge_link_power_t *p,
                                uint32_t id);

/**
 * @brief HAND_DONE, BY HAND: the ESC has its power.
 * @param l  The module.
 * @param id The caller's number of the request.
 * @return As yge_link_open().
 */
yge_link_start_t yge_link_hand_done(yge_link_t *l, uint32_t id);

/**
 * @brief READ the ESC's table.
 * @param l  The module.
 * @param id The caller's number of the request.
 * @return As yge_link_open().
 */
yge_link_start_t yge_link_read(yge_link_t *l, uint32_t id);

/**
 * @brief WRITE, with the key, a draft made from the read copy READS and
 *        READ_CRC name.
 * @param l        The module.
 * @param draft    The draft, index 0 first.
 * @param n        Its values, LINK_YP_ENTRIES's, 1 to YGE_LINK_VALUES_MAX.
 * @param reads    LINK_YP_READS it was made from.
 * @param read_crc LINK_YP_READ_CRC it was made from.
 * @param id       The caller's number of the request.
 * @return As yge_link_open().
 */
yge_link_start_t yge_link_write(yge_link_t *l, const uint16_t *draft,
                                uint16_t n, uint16_t reads, uint16_t read_crc,
                                uint32_t id);

/**
 * @brief LEAVE: the session ends, supply off; the pin stays held.
 * @param l  The module.
 * @param id The caller's number of the request.
 * @return As yge_link_open().
 */
yge_link_start_t yge_link_leave(yge_link_t *l, uint32_t id);

/**
 * @brief CLEAR a stop or fault.
 * @param l  The module.
 * @param id The caller's number of the request.
 * @return As yge_link_open().
 */
yge_link_start_t yge_link_clear(yge_link_t *l, uint32_t id);

/**
 * @brief CLOSE: the pin back.
 * @param l  The module.
 * @param id The caller's number of the request.
 * @return As yge_link_open().
 */
yge_link_start_t yge_link_close(yge_link_t *l, uint32_t id);

/**
 * @brief The STOP key, or any stop of the panel's: the STOP operation, which
 *        cuts at once, a write too.
 * @param l The module.
 */
void yge_link_stop(yge_link_t *l);

/**
 * @brief The panel's stop count: a new one is yge_link_stop().  Taken only
 *        while the IOmcu has the page, so a stop in a gap of the link reaches
 *        it after.
 * @param l     The module.
 * @param stops The stops counted so far.
 */
void yge_link_stop_follow(yge_link_t *l, uint32_t stops);

/**
 * @brief The ABORT operation: cut at once, also in a write.
 * @param l The module.
 */
void yge_link_abort(yge_link_t *l);

/**
 * @brief The next exchange owed at @p now_ms, into @p x; false for none.
 * @param l      The module.
 * @param now_ms The time, ms.
 * @param x      Receives the exchange.
 * @return true when one is owed.
 */
bool yge_link_next(yge_link_t *l, uint32_t now_ms, yge_link_xfer_t *x);

/**
 * @brief How the exchange yge_link_next() gave went.
 * @param l      The module.
 * @param result YGE_LINK_ACK, a NACK's reason or YGE_LINK_NO_ANSWER.
 * @param regs   A read's registers, x.n of them; NULL for a write.
 */
void yge_link_done(yge_link_t *l, int result, const uint16_t *regs);

/**
 * @brief Up to YGE_LINK_PASS_MAX exchanges through @p xchg: one pass.  Stops
 *        after an exchange without an answer.
 * @param l      The module.
 * @param now_ms The time, ms.
 * @param xchg   The caller's exchange.
 * @param ctx    Its context.
 * @return The exchanges made.
 */
unsigned yge_link_service(yge_link_t *l, uint32_t now_ms, yge_link_xchg_fn xchg,
                          void *ctx);

/**
 * @brief Where the module is.
 * @param l The module.
 * @return The phase; YGE_LINK_DOWN for NULL.
 */
yge_link_phase_t yge_link_phase(const yge_link_t *l);

/**
 * @brief How the last request went, once.
 * @param l   The module.
 * @param out Receives it.
 * @return true when a new outcome was written.
 */
bool yge_link_outcome(yge_link_t *l, yge_link_outcome_t *out);

/**
 * @brief The caller's copy of the page.
 * @param l The module.
 * @param r Receives it.
 */
void yge_link_readout(const yge_link_t *l, yge_link_readout_t *r);

/**
 * @brief The read copy.
 * @param l The module.
 * @return It; gen 0 for none yet, NULL for a NULL module.
 */
const yge_link_image_t *yge_link_image(const yge_link_t *l);

/**
 * @brief The errors the page reported: each counts as an error of the bench.
 * @param l The module.
 * @return The count, modulo 2^32.
 */
uint32_t yge_link_faults(const yge_link_t *l);

/**
 * @brief That a pin did not come back, once.
 * @param l    The module.
 * @param port Receives the port.
 * @return true when one is new.
 */
bool yge_link_not_back(yge_link_t *l, uint8_t *port);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_YGE_LINK_H */
