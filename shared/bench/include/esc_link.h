/**
 * @file
 * @brief The panel's half of the ESC_WIRE and ESC_DATA link pages, protocol 4.34
 *        on: one operation of a session with an ESC (electronic speed
 *        controller) on its signal wire, asked and followed.
 *
 * The session and its rules run on the IOmcu (shared/outputs/esc_port.h,
 * docs/LinkEsc.md).  A panel names an operation and hands over a draft; there is
 * no raw transaction.  This module takes one request at a time:
 *
 *   - esc_link_open(), esc_link_enter(), esc_link_powered(), esc_link_read(),
 *     esc_link_write(), esc_link_restore(), esc_link_leave(), esc_link_clear()
 *     and esc_link_close();
 *   - esc_link_outcome() hands out how it went, once.
 *
 * A request goes out in this order:
 *
 *   1. RESTORE: the backup's identity, block LINK_ED_BACKUP, 9 frames;
 *   2. WRITE and RESTORE: the draft's blocks, 9 frames a block;
 *   3. OPEN, ENTER, WRITE and RESTORE: the staged registers, 3 frames;
 *   4. the command with SEQ one above the page's, sent again as it was up to
 *      ESC_LINK_RETRIES times without an answer: the page takes it once;
 *   5. registers 0 to 20 every ESC_LINK_POLL_MS until the page has left a state
 *      that runs.
 *
 * esc_link_stop() and esc_link_abort() go ahead of everything: a request whose
 * command has not gone is dropped, and the operation goes as soon as the page is
 * known and holds a pin.  A BAD_VALUE, a SEQ another writer moved, is followed by
 * a read and the operation again, ESC_LINK_URGENT_TRIES times at most.
 *
 * **After a panel restart** the page may hold a pin nobody here knows of: the
 * first read that shows one is followed by CLOSE, again every
 * ESC_LINK_CLOSE_RETRY_MS while the page answers WRITING or NOT_BACK.  No request
 * is taken until it is free.  A link that came back without a restart closes
 * nothing: readout.ours is false, and the caller decides.
 *
 * **Errors.** esc_link_faults() counts each rising LINK_EW_F_FAULT and each
 * arrival at LINK_ESC_NOT_BACK.  A FAULT that stands on the first read after the
 * link came up is not counted: the IOmcu has switched everything off, and no
 * error happens now.  esc_link_not_back() hands out once that a pin did not come
 * back, with its target.
 *
 * Between requests registers 0 to 20 are read every ESC_LINK_HELD_MS while the
 * page holds a pin and every ESC_LINK_IDLE_MS while it holds none.  A read copy
 * that changed is fetched by itself and checked against LINK_EW_READ_CRC:
 * esc_link_image() gives it, with its number.  An IOmcu below ESC_LINK_MINOR is
 * sent nothing.
 *
 * Pure C, no link of its own: the caller makes each exchange and reports how it
 * went (esc_link_next() and esc_link_done(), or esc_link_service()).  One caller
 * only.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_ESC_LINK_H
#define RCBENCH_ESC_LINK_H

#include <stdbool.h>
#include <stdint.h>

#include "link_pages.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The protocol minor whose IOmcu has the pages. */
#define ESC_LINK_MINOR LINK_MINOR_ESC_WIRE
/** @brief Registers 0 to 20 are due this long after the last read while a
 *         request runs: one 5ms pass of the panel's control task. */
#define ESC_LINK_POLL_MS 5u
/** @brief And this long after it while the page holds a pin. */
#define ESC_LINK_HELD_MS 100u
/** @brief And this long after it while the page holds none. */
#define ESC_LINK_IDLE_MS 500u
/** @brief Exchanges without an answer, in a row, after which a request is given
 *         up. */
#define ESC_LINK_RETRIES 3u
/** @brief Exchanges one esc_link_service() call makes at most. */
#define ESC_LINK_PASS_MAX 4u
/** @brief A STOP or ABORT is sent again this many times after a BAD_VALUE. */
#define ESC_LINK_URGENT_TRIES 6u
/** @brief The CLOSE after a panel restart goes again this long after the page
 *         did not free its pin, ms. */
#define ESC_LINK_CLOSE_RETRY_MS 1000u
/** @brief A request running this long after its command was taken is cut from
 *         here, ms: the longest hold 120000ms, its supply 3000ms + 10000ms + 3000ms
 *         and an identification under 10000ms. */
#define ESC_LINK_RUN_MAX_MS 150000u
/** @brief Bytes of the read copy and the draft at most: 4 blocks. */
#define ESC_LINK_PAGE_MAX (4u * LINK_ED_BLOCK_BYTES)
/** @brief Fetches of a read copy with the wrong CRC before it is given up. */
#define ESC_LINK_FETCH_TRIES 3u

/** @brief An exchange without an answer. */
#define ESC_LINK_NO_ANSWER (-1)
/** @brief An exchange answered with ACK or DATA; a NACK's reason is above 0. */
#define ESC_LINK_ACK 0

/** @brief Where the module is. */
typedef enum {
    ESC_LINK_DOWN = 0,   /**< no IOmcu answers                                */
    ESC_LINK_NO_PAGE,    /**< the IOmcu has no ESC_WIRE page                  */
    ESC_LINK_SYNC,       /**< the page has not been read yet, or the CLOSE
                              after a panel restart is under way              */
    ESC_LINK_READY,      /**< a request is taken                              */
    ESC_LINK_SENDING,    /**< the draft, the staged registers and the command
                              go out                                          */
    ESC_LINK_RUNNING,    /**< the page runs the request                       */
    ESC_LINK_FETCHING,   /**< the read copy comes                             */
} esc_link_phase_t;

/** @brief What a request answers. */
typedef enum {
    ESC_LINK_START_OK = 0,
    ESC_LINK_START_DOWN,    /**< no IOmcu answers                             */
    ESC_LINK_START_NO_PAGE, /**< the IOmcu has no page                        */
    ESC_LINK_START_BUSY,    /**< the phase is not ESC_LINK_READY, or an exchange
                                 is still out                                 */
    ESC_LINK_START_ARG,     /**< a NULL, or a page of no length that fits     */
} esc_link_start_t;

/** @brief How a request went. */
typedef enum {
    ESC_LINK_OUT_NONE = 0,
    ESC_LINK_OUT_DONE,      /**< it ran; result says how it ended             */
    ESC_LINK_OUT_REFUSED,   /**< the page took the command and started nothing;
                                 refusal says why                             */
    ESC_LINK_OUT_NACK,      /**< the page refused a frame; nack says why      */
    ESC_LINK_OUT_NO_ANSWER, /**< ESC_LINK_RETRIES exchanges unanswered, or the
                                 link went                                    */
    ESC_LINK_OUT_RESTARTED, /**< the page's SEQ is not the panel's: the IOmcu
                                 started again, or another writer sent a command */
    ESC_LINK_OUT_DROPPED,   /**< a stop or abort before the page took the
                                 command; nothing of it ran                   */
} esc_link_outcome_kind_t;

/** @brief The outcome of a request that ended. */
typedef struct {
    esc_link_outcome_kind_t kind; /**< how it went                            */
    uint8_t  op;      /**< a link_esc_op_t                                    */
    uint32_t id;      /**< the caller's number of the request                 */
    uint8_t  result;  /**< ESC_LINK_OUT_DONE: a link_esc_result_t             */
    uint8_t  refusal; /**< ESC_LINK_OUT_REFUSED: a link_esc_ref_t             */
    uint8_t  nack;    /**< ESC_LINK_OUT_NACK: a link_nack_t                   */
    uint8_t  state;   /**< the page's state at the end, a link_esc_state_t    */
    uint8_t  ses;     /**< the session's last result                          */
    uint8_t  guard;   /**< the guard's last refusal                           */
    bool     taken;   /**< the page took the command: it may have run         */
} esc_link_outcome_t;

/** @brief The caller's copy of the page: a plain value. */
typedef struct {
    esc_link_phase_t phase; /**< where the module is                          */
    uint16_t minor;     /**< the IOmcu's protocol minor; 0 while none answers */
    bool     known;     /**< the fields below were read                       */
    uint8_t  state;     /**< a link_esc_state_t                               */
    uint16_t flags;     /**< LINK_EW_F_*                                      */
    bool     held;      /**< the page holds a pin, or takes one: not FREE     */
    bool     ours;      /**< this module opened it since the link came up     */
    uint16_t target;    /**< LINK_EW_TARGET                                   */
    uint8_t  line;      /**< a link_esc_line_t                                */
    uint8_t  gpio;      /**< the GPIO; 0xFF for none                          */
    uint8_t  op;        /**< the last operation                               */
    uint8_t  result;    /**< its link_esc_result_t                            */
    uint8_t  refusal;   /**< the last command's link_esc_ref_t                */
    uint8_t  ses;       /**< the session's last result                        */
    uint8_t  guard;     /**< the guard's last refusal                         */
    uint8_t  fw;        /**< the firmware                                     */
    uint8_t  layout;    /**< the layout or revision                           */
    uint8_t  why;       /**< why it is not written                            */
    uint8_t  cpu;       /**< the processor the first contact named            */
    uint16_t signature; /**< an EFM8's                                        */
    uint16_t len;       /**< bytes of the page                                */
    uint16_t bad_off;   /**< LINK_EW_BAD_OFF                                  */
    uint16_t clipped;   /**< LINK_EW_CLIPPED                                  */
    uint16_t tries;     /**< LINK_EW_TRIES                                    */
    uint16_t reads;     /**< LINK_EW_READS                                    */
    uint16_t read_crc;  /**< LINK_EW_READ_CRC                                 */
    uint16_t t_ms;      /**< ms in this state, stops at 65535                 */
    uint8_t  pin_code;  /**< ARM: the pin code                                */
    uint8_t  flash_code; /**< ARM: the flash code                             */
    uint8_t  protocol;  /**< ARM: the bootloader's protocol                   */
} esc_link_readout_t;

/** @brief The ESC's page as the IOmcu holds it. */
typedef struct {
    uint32_t gen;      /**< counts the copies; 0 for none yet                 */
    uint16_t reads;    /**< LINK_EW_READS it belongs to                       */
    uint16_t read_crc; /**< LINK_EW_READ_CRC it has                           */
    uint16_t len;      /**< bytes of @c page                                  */
    uint8_t  page[ESC_LINK_PAGE_MAX];     /**< the page as read               */
    char     name[LINK_ED_BLOCK_BYTES];   /**< the name or text target        */
    uint8_t  info[2u * LINK_ED_BLOCK_BYTES]; /**< `info`'s revision and values */
    char     music[4u * LINK_ED_BLOCK_BYTES]; /**< `music`, ends in 0         */
} esc_link_image_t;

/** @brief What ENTER stages. */
typedef struct {
    uint8_t  power;   /**< a link_esc_power_t                                 */
    uint16_t mv;      /**< the supply's voltage, mV                           */
    uint16_t ma;      /**< the supply's limit, mA                             */
    uint32_t hold_ms; /**< the hold after ON, ms                              */
    uint16_t off_ms;  /**< the time off before ON, ms                         */
} esc_link_power_t;

/** @brief One exchange for the caller to make. */
typedef struct {
    bool     write;   /**< a write of regs; otherwise a read                  */
    uint8_t  page;    /**< LINK_PAGE_ESC_WIRE or LINK_PAGE_ESC_DATA           */
    uint8_t  off;     /**< first register of the page                         */
    uint8_t  n;       /**< registers, 1 to 32; a write 1 to 4                 */
    uint16_t regs[4]; /**< a write's registers                                */
} esc_link_xfer_t;

/**
 * @brief The caller's exchange: makes @p x and fills @p regs with the x->n
 *        registers of a read that was answered with DATA.
 *
 * @param ctx  The caller's context.
 * @param x    The exchange to make.
 * @param regs Receives a read's registers, 32 at most; NULL for a write.
 * @return ESC_LINK_ACK, a NACK's reason, or ESC_LINK_NO_ANSWER.
 */
typedef int (*esc_link_xchg_fn)(void *ctx, const esc_link_xfer_t *x,
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
    uint32_t status_ms;  /**< when status was last asked                      */
    uint32_t now_ms;     /**< the time esc_link_next() last had               */
    uint16_t status[LINK_EW_COUNT]; /**< the page as last read                */
    uint8_t  ours;       /**< this module opened the pin held                 */
    uint32_t faults;     /**< errors counted, modulo 2^32                     */
    uint8_t  not_back;   /**< a NOT_BACK not handed out                       */
    uint16_t not_back_target; /**< its target                                 */

    /* The CLOSE after a restart. */
    uint8_t  close_owed; /**< the first read is to free what it finds         */
    uint8_t  close_timed; /**< close_at is meaningful                         */
    uint32_t close_at;   /**< when it goes again                              */
    uint8_t  internal;   /**< the request is that CLOSE: no outcome           */

    /* The request. */
    uint8_t  active;     /**< a request is on its way                         */
    uint8_t  op;         /**< a link_esc_op_t                                 */
    uint32_t id;         /**< the caller's number of it                       */
    uint16_t send_at;    /**< frames of it gone                               */
    uint16_t staged[LINK_EW_W_STAGED]; /**< registers 4 to 13 as they go out  */
    uint8_t  draft[ESC_LINK_PAGE_MAX]; /**< the draft                         */
    uint16_t draft_len;  /**< its bytes                                       */
    uint8_t  backup[LINK_ED_BLOCK_BYTES]; /**< RESTORE: the identity          */
    uint8_t  cmd_gone;   /**< the command went, answered or not               */
    uint8_t  verdict;    /**< acknowledged; the status that says what became of
                              it is owed                                      */
    uint8_t  doubt;      /**< no answer ESC_LINK_RETRIES times: a read says
                              whether the page took it                        */
    uint8_t  taken;      /**< the page took it                                */
    uint32_t taken_ms;   /**< when                                            */
    uint8_t  limit_cut;  /**< ESC_LINK_RUN_MAX_MS asked an ABORT              */

    /* STOP or ABORT. */
    uint8_t  urgent;     /**< the op owed, 0 for none                         */
    uint8_t  urgent_tries; /**< tries of it                                   */
    uint16_t last_seq;   /**< the SEQ of the last command sent                */

    /* The read copy on its way. */
    uint8_t  fetching;   /**< it comes                                        */
    uint8_t  fetch_at;   /**< blocks of it done                               */
    uint8_t  fetch_tries; /**< fetches with the wrong CRC                     */
    uint8_t  fetch_given_up; /**< of the READS and CRC of fetch_reads, _crc   */
    uint16_t fetch_reads; /**< LINK_EW_READS it was asked for                 */
    uint16_t fetch_crc;  /**< LINK_EW_READ_CRC it was asked for               */
    uint8_t  block_set;  /**< LINK_EW_W_BLOCK has fetch_at's block            */
    esc_link_image_t image; /**< the copy                                     */

    uint8_t  outcome_new; /**< outcome is not handed out yet                  */
    esc_link_outcome_t outcome; /**< how the last request went                */
} esc_link_t;

/**
 * @brief Sets up the module at a panel's start: no IOmcu, and a CLOSE owed for a
 *        pin the first read finds held.
 * @param l The module.
 */
void esc_link_init(esc_link_t *l);

/**
 * @brief The link went: a request on its way ends as ESC_LINK_OUT_NO_ANSWER, and
 *        nothing is read or written until it comes up again.
 * @param l The module.
 */
void esc_link_lost(esc_link_t *l);

/**
 * @brief An IOmcu answered with protocol minor @p minor.
 * @param l     The module.
 * @param minor The IOmcu's protocol minor.
 */
void esc_link_came_up(esc_link_t *l, uint16_t minor);

/**
 * @brief Whether the IOmcu has the page.
 * @param l The module.
 * @return true while an IOmcu answers and has it.
 */
bool esc_link_has_page(const esc_link_t *l);

/**
 * @brief OPEN @p target: the outcome comes when the page has left STOPPING.
 * @param l      The module.
 * @param target LINK_EW_W_TARGET's value.
 * @param id     The caller's number of the request.
 * @return ESC_LINK_START_OK when it is on its way, else why not; nothing is sent
 *         then.
 */
esc_link_start_t esc_link_open(esc_link_t *l, uint16_t target, uint32_t id);

/**
 * @brief ENTER: the outcome comes at READY, at HAND_WAIT or at its end.
 * @param l  The module.
 * @param p  What it stages; the values are the page's to judge.
 * @param id The caller's number of the request.
 * @return As esc_link_open().
 */
esc_link_start_t esc_link_enter(esc_link_t *l, const esc_link_power_t *p,
                                uint32_t id);

/**
 * @brief DONE, BY HAND: the ESC has its power.
 * @param l  The module.
 * @param id The caller's number of the request.
 * @return As esc_link_open().
 */
esc_link_start_t esc_link_powered(esc_link_t *l, uint32_t id);

/**
 * @brief READ the ESC's page.
 * @param l  The module.
 * @param id The caller's number of the request.
 * @return As esc_link_open().
 */
esc_link_start_t esc_link_read(esc_link_t *l, uint32_t id);

/**
 * @brief WRITE a draft made from the read copy READS and READ_CRC name.
 * @param l        The module.
 * @param draft    The draft.
 * @param len      Its bytes, LINK_EW_LEN's, 1 to ESC_LINK_PAGE_MAX.
 * @param reads    LINK_EW_READS it was made from.
 * @param read_crc LINK_EW_READ_CRC it was made from.
 * @param id       The caller's number of the request.
 * @return As esc_link_open().
 */
esc_link_start_t esc_link_write(esc_link_t *l, const uint8_t *draft,
                                uint16_t len, uint16_t reads,
                                uint16_t read_crc, uint32_t id);

/**
 * @brief RESTORE a backup onto erased settings.
 * @param l        The module.
 * @param draft    The backup.
 * @param len      Its bytes, 1 to ESC_LINK_PAGE_MAX.
 * @param identity Block LINK_ED_BACKUP, LINK_ED_BLOCK_BYTES bytes.
 * @param reads    LINK_EW_READS as it stands.
 * @param read_crc LINK_EW_READ_CRC as it stands.
 * @param id       The caller's number of the request.
 * @return As esc_link_open().
 */
esc_link_start_t esc_link_restore(esc_link_t *l, const uint8_t *draft,
                                  uint16_t len, const uint8_t *identity,
                                  uint16_t reads, uint16_t read_crc,
                                  uint32_t id);

/**
 * @brief LEAVE: to the application, supply off.
 * @param l  The module.
 * @param id The caller's number of the request.
 * @return As esc_link_open().
 */
esc_link_start_t esc_link_leave(esc_link_t *l, uint32_t id);

/**
 * @brief CLEAR a stop or fault.
 * @param l  The module.
 * @param id The caller's number of the request.
 * @return As esc_link_open().
 */
esc_link_start_t esc_link_clear(esc_link_t *l, uint32_t id);

/**
 * @brief CLOSE: the pin back.
 * @param l  The module.
 * @param id The caller's number of the request.
 * @return As esc_link_open().
 */
esc_link_start_t esc_link_close(esc_link_t *l, uint32_t id);

/**
 * @brief The STOP key, or any stop of the panel's: the STOP operation.  A write
 *        runs to its read-back first.
 * @param l The module.
 */
void esc_link_stop(esc_link_t *l);

/**
 * @brief The ABORT operation: cut at once, also in a write.
 * @param l The module.
 */
void esc_link_abort(esc_link_t *l);

/**
 * @brief The next exchange owed at @p now_ms, into @p x; false for none.
 * @param l      The module.
 * @param now_ms The time, ms.
 * @param x      Receives the exchange.
 * @return true when one is owed.
 */
bool esc_link_next(esc_link_t *l, uint32_t now_ms, esc_link_xfer_t *x);

/**
 * @brief How the exchange esc_link_next() gave went.
 * @param l      The module.
 * @param result ESC_LINK_ACK, a NACK's reason or ESC_LINK_NO_ANSWER.
 * @param regs   A read's registers, x.n of them; NULL for a write.
 */
void esc_link_done(esc_link_t *l, int result, const uint16_t *regs);

/**
 * @brief Up to ESC_LINK_PASS_MAX exchanges through @p xchg: one pass.  Stops after
 *        an exchange without an answer.
 * @param l      The module.
 * @param now_ms The time, ms.
 * @param xchg   The caller's exchange.
 * @param ctx    Its context.
 * @return The exchanges made.
 */
unsigned esc_link_service(esc_link_t *l, uint32_t now_ms, esc_link_xchg_fn xchg,
                          void *ctx);

/**
 * @brief Where the module is.
 * @param l The module.
 * @return The phase; ESC_LINK_DOWN for NULL.
 */
esc_link_phase_t esc_link_phase(const esc_link_t *l);

/**
 * @brief How the last request went, once.
 * @param l   The module.
 * @param out Receives it.
 * @return true when a new outcome was written.
 */
bool esc_link_outcome(esc_link_t *l, esc_link_outcome_t *out);

/**
 * @brief The caller's copy of the page.
 * @param l The module.
 * @param r Receives it.
 */
void esc_link_readout(const esc_link_t *l, esc_link_readout_t *r);

/**
 * @brief The read copy.
 * @param l The module.
 * @return It; gen 0 for none yet, NULL for a NULL module.
 */
const esc_link_image_t *esc_link_image(const esc_link_t *l);

/**
 * @brief The errors the page reported: each counts as an error of the bench.
 * @param l The module.
 * @return The count, modulo 2^32.
 */
uint32_t esc_link_faults(const esc_link_t *l);

/**
 * @brief That a pin did not come back, once.
 * @param l      The module.
 * @param target Receives LINK_EW_TARGET's value.
 * @return true when one is new.
 */
bool esc_link_not_back(esc_link_t *l, uint16_t *target);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_ESC_LINK_H */
