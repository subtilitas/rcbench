/*
 * The panel's half of SENSE_LIMIT (0x36, protocol 4.27): a current limit
 * per INA3221 channel, and the channels whose 50 ms window passed it.  An
 * alert only: a limit passed stops nothing and drives nothing.
 *
 *   1. At link-up the page is read whole, registers 0 to 15.  An IOmcu
 *      older than 4.27 is sent nothing; one that names 4.27 and refuses
 *      the page with BAD_PAGE is sent nothing more until the next link-up.
 *   2. The limits asked (SETUP > SENSORS' LIMIT rows, in mA) are written,
 *      registers 0 to 11 in one write, SENSE_LIMIT_LINK_SETTLE_MS after
 *      the last edit, in any bank state, and only where the page holds
 *      something else.  A refused write is said once and not written
 *      again until the next edit.
 *   3. While the page holds a limit that is not 0, OVER and OVER_COUNT
 *      (registers 12 and 13) are read every SENSE_LIMIT_LINK_READ_MS.  A
 *      read clears OVER on the IOmcu, so its bits are taken here once.
 *
 * What the band is told, at most one alert every
 * SENSE_LIMIT_LINK_GAP_MS:
 *   - a channel over its limit, once per SENSE_LIMIT_LINK_REPEAT_MS while
 *     it keeps passing it: the bits of a channel told within that time
 *     are dropped;
 *   - OVER_COUNT moved with no bit to show for it: a reply carrying the
 *     bits was lost on the link, and which channel passed is not known;
 *   - the limits refused.
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

/** A limit edit rests this long before it is written. */
#define SENSE_LIMIT_LINK_SETTLE_MS  500u
/** OVER is read this often while a limit is held. */
#define SENSE_LIMIT_LINK_READ_MS    200u
/** At most one alert this often. */
#define SENSE_LIMIT_LINK_GAP_MS     5000u
/** A channel's alert again at most this often while it keeps passing. */
#define SENSE_LIMIT_LINK_REPEAT_MS  30000u

#define SENSE_LIMIT_LINK_ACK       0
#define SENSE_LIMIT_LINK_NO_ANSWER (-1)

typedef enum {
    SENSE_LIMIT_LINK_OP_NONE = 0,
    SENSE_LIMIT_LINK_OP_READ,     /**< registers 0 to 15                */
    SENSE_LIMIT_LINK_OP_WRITE,    /**< registers 0 to 11                */
    SENSE_LIMIT_LINK_OP_OVER,     /**< registers 12 and 13              */
} sense_limit_link_op_kind_t;

typedef struct {
    sense_limit_link_op_kind_t kind;
    bool            write;
    uint8_t         off;
    uint8_t         n;
    const uint16_t *regs;     /**< a write's registers, n of them       */
} sense_limit_link_op_t;

/** What sense_limit_link_event() hands out. */
typedef enum {
    SENSE_LIMIT_LINK_EV_NONE = 0,
    SENSE_LIMIT_LINK_EV_OVER,     /**< channel *ch passed its limit     */
    SENSE_LIMIT_LINK_EV_LOST,     /**< a channel passed; which was lost */
    SENSE_LIMIT_LINK_EV_REFUSED,  /**< the limits were refused          */
} sense_limit_link_ev_t;

/** What SETUP > SENSORS shows of the limits (sensors_rows_set_limits()). */
typedef enum {
    SENSE_LIMIT_ST_UNKNOWN = 0,   /**< no link, or not read yet          */
    SENSE_LIMIT_ST_HELD,          /**< the page holds the limits asked   */
    SENSE_LIMIT_ST_PENDING,       /**< a write is owed                   */
    SENSE_LIMIT_ST_REFUSED,       /**< the page refused them             */
    SENSE_LIMIT_ST_OLD,           /**< the IOmcu has no SENSE_LIMIT      */
} sense_limit_st_t;

typedef struct {
    bool     up;              /**< an IOmcu answers                     */
    bool     page;            /**< it has SENSE_LIMIT (4.27)            */
    bool     known;           /**< the page has been read since link-up */

    bool     want_set;
    uint16_t want[LINK_SL_KEPT];
    uint32_t want_ms;
    uint16_t held[LINK_SL_KEPT];
    bool     refused;

    sense_limit_link_op_kind_t pending;
    bool     asked;
    uint32_t asked_ms;
    bool     have_count;
    uint16_t count;           /**< OVER_COUNT as last read              */

    uint16_t over;            /**< bits not told yet                    */
    bool     lost;
    bool     ev_refused;
    bool     told;            /**< an alert went out at told_ms         */
    uint32_t told_ms;
    uint16_t told_ch;         /**< bit c: told at ch_ms[c]              */
    uint32_t ch_ms[LINK_SL_KEPT];
} sense_limit_link_t;

void sense_limit_link_init(sense_limit_link_t *s);

/** The link went: what was read is no longer the IOmcu's.  The limits
 *  asked stay. */
void sense_limit_link_lost(sense_limit_link_t *s);

/** An IOmcu answered with protocol minor @p minor. */
void sense_limit_link_came_up(sense_limit_link_t *s, uint16_t minor,
                              uint32_t now_ms);

/** The limits SETUP > SENSORS asks, LIMIT_MA[0] to [11], at @p now_ms; a
 *  change clears a refusal. */
void sense_limit_link_want(sense_limit_link_t *s, const uint16_t *ma,
                           uint32_t now_ms);

/** The next exchange owed at @p now_ms into @p op; false for none. */
bool sense_limit_link_next(sense_limit_link_t *s, uint32_t now_ms,
                           sense_limit_link_op_t *op);

/** How it went: SENSE_LIMIT_LINK_ACK with the registers a read asked for,
 *  a NACK's reason, or SENSE_LIMIT_LINK_NO_ANSWER. */
void sense_limit_link_done(sense_limit_link_t *s, int result,
                           const uint16_t *regs, uint32_t now_ms);

/** The next alert at @p now_ms, and for EV_OVER its channel, 0 to 11, in
 *  @p ch; EV_NONE while the last is younger than SENSE_LIMIT_LINK_GAP_MS. */
sense_limit_link_ev_t sense_limit_link_event(sense_limit_link_t *s,
                                             uint32_t now_ms, unsigned *ch);

/** The channel's limit the page holds, mA; 0 for none or not read. */
uint16_t sense_limit_link_held_ma(const sense_limit_link_t *s, unsigned ch);

/** What SETUP > SENSORS shows of the limits. */
sense_limit_st_t sense_limit_link_status(const sense_limit_link_t *s);

/** The channels whose LIMIT row SETUP marks, bit c for channel c: on an
 *  IOmcu older than 4.27 every limit asked that is not 0; else, once the
 *  page is read, every limit the page holds otherwise than asked, owed or
 *  refused.  0 with no link. */
uint16_t sense_limit_link_unheld(const sense_limit_link_t *s);

#ifdef __cplusplus
}
#endif
