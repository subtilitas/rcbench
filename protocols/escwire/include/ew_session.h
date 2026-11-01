/**
 * @file ew_session.h
 * @brief One ESC (electronic speed controller) on one wire: holding the line,
 *        first contact, identification, reads, writes with read-back, leaving.
 *        A state machine that never waits.
 *
 * ```c
 * ew_session_init(&s, &driver);
 * ew_session_hold(&s);            // the line driven high, the supply still off
 * // the caller switches the supply on
 * ew_session_enter(&s, 3000u);    // and from here on:
 * while (ew_session_step(&s) == EW_SES_BUSY) { }      // call every 1ms
 * ew_session_read(&s);
 * while (ew_session_step(&s) == EW_SES_BUSY) { }
 * memcpy(draft, s.orig, s.id.len);
 * draft[0x15] = 2u;
 * ew_session_write(&s, draft, s.id.len);
 * while (ew_session_step(&s) == EW_SES_BUSY) { }
 * ew_session_leave(&s);
 * while (ew_session_step(&s) == EW_SES_BUSY) { }
 * ```
 *
 * The caller's part: stop every other use of the pin, keep the bench SAFE,
 * switch the supply, and call ew_session_stop() or ew_session_abort() on a
 * stop.  No function here makes a throttle signal.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef EW_SESSION_H
#define EW_SESSION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ew_a.h"
#include "ew_guard.h"
#include "ew_id.h"
#include "ew_map.h"
#include "ew_wire.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The shortest hold before a first contact, ms. */
#define EW_HOLD_MIN_MS 500u
/** @brief The longest hold before a first contact, ms. */
#define EW_HOLD_MAX_MS 120000u
/** @brief How long a leave drives the line low afterwards, ms: above the
 *         250ms and 20ms after which the bootloaders leave by themselves. */
#define EW_LEAVE_LOW_MS 300u
/** @brief Tries of an address set, a read or a buffer. */
#define EW_SES_TRIES 3u
/** @brief Times the whole write procedure runs at most. */
#define EW_SES_ATTEMPTS 3u
/** @brief Idle bit times between an announcement and its data: 520us, for every
 *         firmware.  AM32 ends a message on more than 234us of idle line and
 *         loses data that come sooner, with no answer; an EFM8 takes any gap. */
#define EW_SES_PAUSE_BITS 10u
/** @brief The line stays idle this long after a leave frame, us: AM32 acts on a
 *         message only after its idle line. */
#define EW_SES_LEAVE_IDLE_US 500u
/** @brief A driver that has not ended a transaction this long after its
 *         longest time has failed, us. */
#define EW_SES_GRACE_US 5000u

/** @brief What a session says. */
typedef enum {
    EW_SES_OK = 0,         /**< the operation ended as asked                 */
    EW_SES_BUSY,           /**< call ew_session_step() again                 */
    EW_SES_ERR_ARG,        /**< a NULL or a value out of range               */
    EW_SES_ERR_STATE,      /**< not possible in this state                   */
    EW_SES_ERR_NOT_WRITABLE, /**< a write to a page that is read-only        */
    EW_SES_ERR_NO_READ,    /**< a write with no read this session            */
    EW_SES_ERR_MUST_READ,  /**< a write failed: a read comes first           */
    EW_SES_ERR_DIRTY,      /**< a page is half written: only its write goes  */
    EW_SES_ERR_ERASED,     /**< AM32 settings erased: only a backup restores */
    EW_SES_ERR_BACKUP,     /**< a backup of another ESC                      */
    EW_SES_ERR_REFUSED,    /**< the guard refused: @c refuse says why        */
    EW_SES_ERR_SILENT,     /**< no answer to the first contact               */
    EW_SES_ERR_CONTACT,    /**< another answer than a first contact has      */
    EW_SES_ERR_NOT_KNOWN,  /**< an ESC this module does not take: @c id.why  */
    EW_SES_ERR_READ,       /**< a read failed; nothing was erased            */
    EW_SES_ERR_CHANGED,    /**< the page is not what was read; nothing erased */
    EW_SES_ERR_WRITE_FAILED, /**< the write did not take; the page is as read */
    EW_SES_ERR_TORN,       /**< the page is neither as read nor as intended  */
    EW_SES_ERR_LOST,       /**< the ESC stopped answering during a write     */
    EW_SES_ERR_ABORTED,    /**< cut by ew_session_abort()                    */
    EW_SES_ERR_STOPPED,    /**< cut by ew_session_stop()                     */
    EW_SES_ERR_DRIVER,     /**< the driver refused, or did not end           */
} ew_ses_t;

/** @brief The identity a backup was read from. */
typedef struct {
    uint8_t pin_code;   /**< byte 3 of the first contact's answer */
    uint8_t flash_code; /**< byte 4                               */
    uint8_t protocol;   /**< byte 7                               */
    char    name[EW_ID_NAME_MAX + 1u]; /**< the name, "" for none */
} ew_backup_id_t;

/** @brief A session.  Not to be moved while it runs. */
typedef struct {
    ew_driver_t  d;           /**< the driver                                */
    ew_guard_t   guard;       /**< what may go out                           */
    ew_contact_t contact;     /**< the first contact's answer                */
    ew_ident_t   id;          /**< what identification found                 */
    ew_xfer_t    x;           /**< the running transaction                   */
    const uint8_t *seq;       /**< the actions of the operation              */
    uint8_t      phase;       /**< internal                                  */
    uint8_t      op;          /**< internal                                  */
    uint8_t      pc;          /**< internal: the next action                 */
    uint8_t      tries;       /**< internal: tries of the running step       */
    uint8_t      attempts;    /**< whole write procedures started            */
    uint8_t      refuse;      /**< an ew_refuse_t, the guard's last refusal  */
    uint8_t      last_ans;    /**< an ew_ans_t, the last answer judged       */
    bool         in_mode;     /**< the ESC answered and has not left         */
    bool         have_read;   /**< a read of the whole page succeeded        */
    bool         dirty;       /**< an erase or program went, no verify yet   */
    bool         reread;      /**< dirty, and a read came since              */
    bool         writing;     /**< this operation sent an erase or program   */
    bool         stop_wanted; /**< a stop waits for the write's end          */
    bool         stopped;     /**< a stop parked the line                    */
    bool         restoring;   /**< the operation writes a backup             */
    ew_ses_t     result;      /**< the last operation's result               */
    uint16_t     bad_off;     /**< the byte the guard refused                */
    uint32_t     t0;          /**< when the wait or transaction started      */
    uint32_t     wait_us;     /**< the wait, or the transaction's longest    */
    uint8_t      name_region[EW_ID_NAME_REGION]; /**< as read                */
    uint8_t      record[EW_ID_RECORD_LEN];       /**< as read                */
    uint8_t      orig[EW_PAGE_MAX];   /**< the page as read: a write's start */
    uint8_t      target[EW_PAGE_MAX]; /**< the page being written            */
    uint8_t      page[EW_PAGE_MAX];   /**< the page as last read             */
    uint8_t      tx[EW_TX_MAX];       /**< the transaction's bytes           */
    uint8_t      rx[EW_RX_MAX];       /**< the answer                        */
} ew_session_t;

/**
 * @brief Start over on a driver; the line is not touched.
 * @param s the session
 * @param d the driver, copied
 * @return EW_SES_OK, or EW_SES_ERR_ARG for a NULL or a driver with no hook
 */
ew_ses_t ew_session_init(ew_session_t *s, const ew_driver_t *d);

/**
 * @brief Forget the ESC and drive the line high, before the supply comes on.
 * @param s the session
 * @return EW_SES_OK; EW_SES_ERR_STATE while an operation runs
 */
ew_ses_t ew_session_hold(ew_session_t *s);

/**
 * @brief The supply came on: hold the line @p hold_ms longer, then first
 *        contact and identification.  Ends EW_SES_OK, or EW_SES_ERR_NOT_KNOWN
 *        with the line still up, or EW_SES_ERR_SILENT or EW_SES_ERR_CONTACT.
 * @param s       the session, after ew_session_hold()
 * @param hold_ms EW_HOLD_MIN_MS to EW_HOLD_MAX_MS
 * @return EW_SES_BUSY when it started
 */
ew_ses_t ew_session_enter(ew_session_t *s, uint32_t hold_ms);

/**
 * @brief Read the whole page: into @c orig and @c page, or into @c page alone
 *        while a page is half written.
 * @param s the session
 * @return EW_SES_BUSY when it started
 */
ew_ses_t ew_session_read(ew_session_t *s);

/**
 * @brief Write a draft: read again and compare, erase where the firmware needs
 *        it, send, program, read back.  A draft equal to the page read ends
 *        EW_SES_OK with no frame sent.
 * @param s     the session
 * @param draft the page, @c id.len bytes
 * @param n     bytes at @p draft
 * @return EW_SES_BUSY when it started, EW_SES_OK when nothing differs, else
 *         the refusal, with @c refuse and @c bad_off set for
 *         EW_SES_ERR_REFUSED
 */
ew_ses_t ew_session_write(ew_session_t *s, const uint8_t *draft, size_t n);

/**
 * @brief Write a backup to an AM32 whose settings read erased.  A restore left
 *        half written takes the same backup again, after a read, and no other.
 * @param s    the session
 * @param from the identity the backup was read from
 * @param page the backup, EW_MAP_LEN_AM bytes
 * @param n    bytes at @p page
 * @return EW_SES_BUSY when it started, else the refusal
 */
ew_ses_t ew_session_restore(ew_session_t *s, const ew_backup_id_t *from,
                            const uint8_t *page, size_t n);

/**
 * @brief The ESC's identity as a backup's file records it.
 * @param s   the session, after an entry
 * @param out takes it
 */
void ew_session_backup_id(const ew_session_t *s, ew_backup_id_t *out);

/**
 * @brief Leave to the application, then drive the line low EW_LEAVE_LOW_MS and
 *        park it.  Refused while a page is half written and on erased settings.
 * @param s the session
 * @return EW_SES_BUSY when it started, else the refusal
 */
ew_ses_t ew_session_leave(ew_session_t *s);

/**
 * @brief A stop the write may finish: outside a write, as ew_session_abort()
 *        with EW_SES_ERR_STOPPED; from a write's first erase or program on the
 *        procedure runs to its read-back, starts no other, and then parks.
 * @param s the session
 */
void ew_session_stop(ew_session_t *s);

/**
 * @brief Cut what runs at once and park the line.  The ESC counts as gone.
 * @param s the session
 */
void ew_session_abort(ew_session_t *s);

/**
 * @brief Whether a write is under way that the supply should not be cut in.
 * @param s the session
 * @return true from the operation's first erase or program to its end
 */
bool ew_session_writing(const ew_session_t *s);

/**
 * @brief Run the session; reads the clock, polls the driver and returns.
 * @param s the session
 * @return EW_SES_BUSY while an operation runs, then its result
 */
ew_ses_t ew_session_step(ew_session_t *s);

#ifdef __cplusplus
}
#endif

#endif /* EW_SESSION_H */
