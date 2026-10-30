/**
 * @file sense_limit_link.h
 * @brief The panel's half of SENSE_LIMIT (0x36, protocol 4.27): a current
 *        limit per INA3221 channel, and the channels whose 50ms window
 *        passed it.
 *
 * An alert only: a limit passed stops nothing and drives nothing.
 *
 *   1. At link-up the page is read whole, registers 0 to 15.  An IOmcu
 *      older than 4.27 is sent nothing; one that names 4.27 or later and
 *      refuses the page with BAD_PAGE is sent nothing more until the next
 *      link-up, and that is said as a refusal.
 *   2. The limits asked (SETUP > SENSORS' LIMIT rows, in mA) are written,
 *      registers 0 to 11 in one write, SENSE_LIMIT_LINK_SETTLE_MS after
 *      the last edit, in any bank state, and only where the page holds
 *      something else.  While the page holds a limit, one OVER read goes
 *      first, so the bits judged under the old limits are taken with them.
 *      A refused write is said once and not written again until the next
 *      edit.
 *   3. While the page holds a limit that is not 0, OVER and OVER_COUNT
 *      (registers 12 and 13) are read every SENSE_LIMIT_LINK_READ_MS.  A
 *      read clears OVER on the IOmcu, so its bits are taken here once,
 *      each with the limit the page held when it was read.
 *
 * What the band is told, at most one alert every
 * SENSE_LIMIT_LINK_GAP_MS:
 *   - a channel over its limit, once per SENSE_LIMIT_LINK_REPEAT_MS while
 *     it keeps passing it: the bits of a channel told within that time
 *     are dropped.  Channels take turns from the one after the channel
 *     told last, so twelve that keep passing are each told within twelve
 *     gaps;
 *   - OVER_COUNT moved with no bit to show for it: a reply carrying the
 *     bits was lost.  An unanswered exchange takes the link down, so the
 *     count is kept across a lost link and compared at the link-up read:
 *     OVER 0 and a count that moved and is not 0.  A count of 0 is an
 *     IOmcu that started again with no window over since; one that had
 *     windows over since names them in OVER;
 *   - the limits refused, by a write or by BAD_PAGE.
 *
 * Pure C, no link of its own: the caller makes each exchange and reports
 * how it went.  Control task only.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "link_pages.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief A limit edit rests this long before it is written, ms. */
#define SENSE_LIMIT_LINK_SETTLE_MS  500u
/** @brief OVER is read this often while a limit is held, ms. */
#define SENSE_LIMIT_LINK_READ_MS    200u
/** @brief At most one alert this often, ms. */
#define SENSE_LIMIT_LINK_GAP_MS     5000u
/** @brief A channel's alert again at most this often while it keeps
 *         passing, ms. */
#define SENSE_LIMIT_LINK_REPEAT_MS  30000u

/** @brief sense_limit_link_done()'s results besides a NACK's reason: the
 *         exchange answered, or not answered. */
#define SENSE_LIMIT_LINK_ACK       0
/** @brief sense_limit_link_done()'s result for an exchange not answered. */
#define SENSE_LIMIT_LINK_NO_ANSWER (-1)

/** @brief The exchanges the page is served with. */
typedef enum {
    SENSE_LIMIT_LINK_OP_NONE = 0,
    SENSE_LIMIT_LINK_OP_READ,     /**< registers 0 to 15                */
    SENSE_LIMIT_LINK_OP_WRITE,    /**< registers 0 to 11                */
    SENSE_LIMIT_LINK_OP_OVER,     /**< registers 12 and 13              */
} sense_limit_link_op_kind_t;

/** @brief One exchange for the caller to make. */
typedef struct {
    sense_limit_link_op_kind_t kind;   /**< which exchange          */
    bool            write;    /**< a write; a read otherwise            */
    uint8_t         off;      /**< the first register                   */
    uint8_t         n;        /**< registers, from off                  */
    const uint16_t *regs;     /**< a write's registers, n of them       */
} sense_limit_link_op_t;

/** @brief What sense_limit_link_event() hands out. */
typedef enum {
    SENSE_LIMIT_LINK_EV_NONE = 0,
    SENSE_LIMIT_LINK_EV_OVER,     /**< channel *ch passed its limit     */
    SENSE_LIMIT_LINK_EV_LOST,     /**< a channel passed; which was lost */
    SENSE_LIMIT_LINK_EV_REFUSED,  /**< the limits were refused          */
} sense_limit_link_ev_t;

/** @brief What SETUP > SENSORS shows of the limits
 *         (sensors_rows_set_limits()). */
typedef enum {
    SENSE_LIMIT_ST_UNKNOWN = 0,   /**< no link, or not read yet          */
    SENSE_LIMIT_ST_HELD,          /**< the page holds the limits asked   */
    SENSE_LIMIT_ST_PENDING,       /**< a write is owed                   */
    SENSE_LIMIT_ST_REFUSED,       /**< the page refused them             */
    SENSE_LIMIT_ST_OLD,           /**< the IOmcu has no SENSE_LIMIT      */
} sense_limit_st_t;

/** @brief The service's state; zeroed by sense_limit_link_init(). */
typedef struct {
    bool     up;              /**< an IOmcu answers                     */
    bool     page;            /**< it has SENSE_LIMIT (4.27)            */
    bool     known;           /**< the page has been read since link-up */

    bool     want_set;        /**< want holds the settings' limits      */
    uint16_t want[LINK_SL_KEPT]; /**< the limits asked, mA              */
    uint32_t want_ms;         /**< when want last changed               */
    uint16_t held[LINK_SL_KEPT]; /**< the limits the page holds, mA     */
    bool     refused;         /**< the page refused want                */

    sense_limit_link_op_kind_t pending; /**< the exchange under way      */
    bool     asked;           /**< OVER has been read since link-up     */
    uint32_t asked_ms;        /**< when OVER was last read              */
    bool     pre_read;        /**< the OVER read pending goes before a
                                   write                                */
    bool     flushed;         /**< that read is done for the write owed  */
    bool     bad_page;        /**< a 4.27 IOmcu refused the page        */
    bool     have_count;      /**< kept across a lost link              */
    uint16_t count;           /**< OVER_COUNT as last read              */

    uint16_t over;            /**< bits not told yet                    */
    uint16_t trip;            /**< bits read since the last
                                   sense_limit_link_take_trip()          */
    uint16_t ch_lim[LINK_SL_KEPT]; /**< the limit each bit was judged by */
    bool     lost;            /**< a channel passed; which was lost     */
    bool     ev_refused;      /**< the refusal is still to be told      */
    bool     told;            /**< an alert went out at told_ms         */
    uint32_t told_ms;         /**< when the last alert went out         */
    uint16_t told_ch;         /**< bit c: told at ch_ms[c]              */
    uint32_t ch_ms[LINK_SL_KEPT]; /**< when each channel was last told  */
    uint8_t  turn;            /**< the channel the next search starts at */
} sense_limit_link_t;

/**
 * @brief Clears the service: no link, no limits asked.
 * @param s the service; NULL does nothing.
 */
void sense_limit_link_init(sense_limit_link_t *s);

/**
 * @brief The link went: what was read is no longer the IOmcu's.
 *
 * The limits asked stay, and OVER_COUNT as last read, for the link-up
 * read.
 * @param s the service; NULL does nothing.
 */
void sense_limit_link_lost(sense_limit_link_t *s);

/**
 * @brief An IOmcu answered.
 * @param s      the service; NULL does nothing.
 * @param minor  its protocol minor; below LINK_MINOR_SENSE_LIMIT (27) the
 *               page is sent nothing.
 * @param now_ms the time, ms; a limit asked before rests from it.
 */
void sense_limit_link_came_up(sense_limit_link_t *s, uint16_t minor,
                              uint32_t now_ms);

/**
 * @brief The limits SETUP > SENSORS asks.  A change clears a refusal.
 * @param s      the service; NULL does nothing.
 * @param ma     LIMIT_MA[0] to [11], mA, 0 for none; above 32767 taken as
 *               32767.  NULL does nothing.
 * @param now_ms the time, ms; a change rests SENSE_LIMIT_LINK_SETTLE_MS
 *               from it.
 */
void sense_limit_link_want(sense_limit_link_t *s, const uint16_t *ma,
                           uint32_t now_ms);

/**
 * @brief The next exchange owed.
 * @param s      the service.
 * @param now_ms the time, ms.
 * @param op     where the exchange goes; zeroed for none.
 * @return true for an exchange to make and report to
 *         sense_limit_link_done(); false for none, and for a NULL @p s or
 *         @p op.
 */
bool sense_limit_link_next(sense_limit_link_t *s, uint32_t now_ms,
                           sense_limit_link_op_t *op);

/**
 * @brief How the exchange sense_limit_link_next() named went.
 * @param s      the service; NULL does nothing.
 * @param result SENSE_LIMIT_LINK_ACK, a NACK's reason (LINK_NACK_*), or
 *               SENSE_LIMIT_LINK_NO_ANSWER.
 * @param regs   for an answered read, the registers it asked for; else
 *               NULL.
 * @param now_ms the time, ms.
 */
void sense_limit_link_done(sense_limit_link_t *s, int result,
                           const uint16_t *regs, uint32_t now_ms);

/**
 * @brief The next band alert.
 * @param s      the service.
 * @param now_ms the time, ms.
 * @param ch     for EV_OVER the channel, 0 to 11; may be NULL.
 * @param ma     for EV_OVER the limit the window was judged by, mA; may be
 *               NULL.
 * @return the alert; EV_NONE for none, for a NULL @p s and while the last
 *         is younger than SENSE_LIMIT_LINK_GAP_MS.
 */
sense_limit_link_ev_t sense_limit_link_event(sense_limit_link_t *s,
                                             uint32_t now_ms, unsigned *ch,
                                             uint16_t *ma);

/**
 * @brief The channels whose window passed its limit since the last call, as
 *        OVER read them, and cleared: every bit at once, not held back by
 *        the band's repeat times.
 *
 * What the panel stops the bench for (error_stop.h).  A link lost clears
 * them: a read after the link-up says what passed since.
 *
 * @param s the service; NULL gives 0.
 * @return OVER's bits read since the last call, bit c channel c.
 */
uint16_t sense_limit_link_take_trip(sense_limit_link_t *s);

/**
 * @brief The limit channel @p ch's last OVER bit was judged by: the limit
 *        the page held when the read brought the bit, kept when a new
 *        limit is written after it.  For the console line of a trip.
 * @param s  the service.
 * @param ch the channel, 0 to 11.
 * @return the limit, mA; 0 before the channel's first bit, for @p ch out
 *         of range and for a NULL @p s.
 */
uint16_t sense_limit_link_trip_ma(const sense_limit_link_t *s, unsigned ch);

/**
 * @brief A channel's limit the page holds.
 * @param s  the service.
 * @param ch the channel, 0 to 11.
 * @return the limit, mA; 0 for none, not read, or @p ch out of range.
 */
uint16_t sense_limit_link_held_ma(const sense_limit_link_t *s, unsigned ch);

/**
 * @brief What SETUP > SENSORS shows of the limits.
 * @param s the service.
 * @return the status; SENSE_LIMIT_ST_UNKNOWN with no link or a NULL @p s.
 */
sense_limit_st_t sense_limit_link_status(const sense_limit_link_t *s);

/**
 * @brief The channels whose LIMIT row SETUP marks.
 * @param s the service.
 * @return bit c for channel c: on an IOmcu without the page every limit
 *         asked that is not 0; else, once the page is read, every limit
 *         the page holds otherwise than asked, owed or refused.  0 with no
 *         link.
 */
uint16_t sense_limit_link_unheld(const sense_limit_link_t *s);

#ifdef __cplusplus
}
#endif
