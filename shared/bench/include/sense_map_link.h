/*
 * The panel's half of the SENSE_MAP page (0x35, protocol 4.25): up to four
 * INA3221 parts as SETUP > SENSORS has them, written to the IOmcu, and
 * what each part is doing, for SENSORS and the alert band.
 *
 * The map is registers 0 to 25 of the page (link_pages.h): ENABLE, FAST,
 * twelve shunts and twelve WATCH values, all kept in the IOmcu's flash.
 * The page judges the whole map at every write, so the panel writes it
 * whole, in one frame of 26 registers:
 *
 *   1. the page is read whole, 32 registers, once per link-up, and nothing
 *      is written that it already holds;
 *   2. the map asked goes out once it has rested SENSE_MAP_LINK_SETTLE_MS
 *      since its last edit -- each write is a flash save at the IOmcu --
 *      and only while the caller says the bank is idle: the page refuses
 *      any change while it drives;
 *   3. a map the page refuses is not written again until the map asked
 *      changes, and says so once (SENSE_MAP_LINK_EV_REFUSED);
 *   4. after a write is taken the caller reads SENSE's set-up again
 *      (sense_link_reread()): SENSE's ENABLE bit 1 and registers 8 to 10
 *      mirror the map from then on.
 *
 * Two maps are not written, and said once per edit: one with a fitted
 * part whose three shunts are 0 (SENSE_MAP_LINK_EV_DARK), which the page
 * refuses, and none for a part fitted on the INA228's address while the
 * INA228 is on: that part is left out of ENABLE, as SENSE leaves both
 * monitors off, and SENSE_LINK_EV_SAME_ADDR says it.  With the sensor
 * bus's SDA or SCL unset every part is left out, and FAST with them: SENSE
 * then clears its pins and enables nothing (SENSE_LINK_EV_PINS_UNSET).
 *
 * While a part is fitted the page's ID and FLAGS registers are read every
 * SENSE_MAP_LINK_READ_MS.  A fitted part that is not online
 * SENSE_LINK_GRACE_MS after the map was taken, or that was online and
 * stops, is said not to answer (SENSE_MAP_LINK_EV_SILENT); one that
 * answers with another identity likewise (SENSE_MAP_LINK_EV_WRONG); each
 * once, for the parts other than the mirror part, which SENSE's own reads
 * already report (sense_link.h).
 *
 * An IOmcu older than 4.25 has no SENSE_MAP: nothing is sent to it, and
 * SENSE's one-part frame carries the mirror part (settings_sensors.h).
 * sense_map_link_part() then says OLD for every other fitted part.
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
#include "sense_link.h"

#ifdef __cplusplus
extern "C" {
#endif

/** A map rests this long after its last edit before it is written. */
#define SENSE_MAP_LINK_SETTLE_MS SENSE_LINK_SETTLE_MS
/** The page's IDs and FLAGS are read this often while a part is fitted. */
#define SENSE_MAP_LINK_READ_MS 500u

#define SENSE_MAP_LINK_ACK       SENSE_LINK_ACK
#define SENSE_MAP_LINK_NO_ANSWER SENSE_LINK_NO_ANSWER

/** Things the caller tells the operator about, each once. */
enum {
    /** The page refused the map. */
    SENSE_MAP_LINK_EV_REFUSED = 0x01,
    /** A fitted part has every channel off: no map is written. */
    SENSE_MAP_LINK_EV_DARK    = 0x02,
    /** A fitted part does not answer at its address. */
    SENSE_MAP_LINK_EV_SILENT  = 0x04,
    /** Something answers at a fitted part's address with another
     *  identity. */
    SENSE_MAP_LINK_EV_WRONG   = 0x08,
};

/** What a fitted part is doing, for its heading on SETUP > SENSORS; 4
 *  bits each in sense_map_link_status(). */
typedef enum {
    SENS_PART_UNKNOWN = 0,  /**< no IOmcu answers, or not read yet        */
    SENS_PART_PENDING,      /**< the map asked is not written yet          */
    SENS_PART_SEARCHING,    /**< taken; the IOmcu is looking for the part  */
    SENS_PART_ONLINE,       /**< answering as an INA3221                   */
    SENS_PART_SILENT,       /**< not answering at its address              */
    SENS_PART_WRONG,        /**< another identity answers there            */
    SENS_PART_REFUSED,      /**< the page refused the map                  */
    SENS_PART_DARK,         /**< every channel off: no map is written      */
    SENS_PART_SAME_ADDR,    /**< the INA228 is on at its address           */
    SENS_PART_OLD,          /**< an IOmcu older than 4.25 reads one part
                                 only, and not this one                    */
    SENS_PART_NO_BUS,       /**< the sensor bus's SDA or SCL is unset      */
    SENS_PART_COUNT
} sens_part_t;

/** The map SENSORS asks for (settings_sensors_map()). */
typedef struct {
    uint16_t regs[LINK_SM_KEPT];  /**< SENSE_MAP 0 to 25                    */
    uint8_t  dark;                /**< fitted parts with every shunt 0      */
    /** The INA228's address while it is on, 0 while it is off. */
    uint8_t  i228_addr;
    /** The sensor bus's SDA or SCL is unset: no part is enabled, as SENSE
     *  enables no monitor then. */
    bool     nobus;
} sense_map_want_t;

typedef enum {
    SENSE_MAP_LINK_OP_NONE = 0,
    SENSE_MAP_LINK_OP_READ,     /**< SENSE_MAP 0 to 31                      */
    SENSE_MAP_LINK_OP_WRITE,    /**< SENSE_MAP 0 to 25                      */
    SENSE_MAP_LINK_OP_STATUS,   /**< SENSE_MAP 26 to 30: IDs and FLAGS      */
} sense_map_link_op_kind_t;

typedef struct {
    sense_map_link_op_kind_t kind;
    bool     write;
    uint8_t  page;
    uint8_t  off, n;
    const uint16_t *regs;     /**< a write's registers, n of them           */
} sense_map_link_op_t;

typedef struct {
    bool     up;              /**< an IOmcu answers                         */
    bool     page;            /**< it has SENSE_MAP (4.25)                  */
    bool     known;           /**< the page has been read since link-up     */

    bool     want_set;
    uint16_t want[LINK_SM_KEPT];  /**< as it is to be written               */
    uint8_t  dark;
    uint8_t  clash;           /**< parts left out for the INA228's address  */
    uint8_t  fitted;          /**< parts fitted, enabled or not             */
    bool     nobus;           /**< every part left out: no sensor bus       */
    uint32_t want_ms;

    uint16_t held[LINK_SM_KEPT];
    bool     refused;         /**< the page refused want                    */
    bool     reread;          /**< a write was taken: SENSE to be read again */
    uint32_t setup_ms;        /**< when the map in force was taken or read  */

    sense_map_link_op_kind_t pending;
    bool     asked;
    uint32_t asked_ms;
    bool     have_status;     /**< IDs and FLAGS read under this map        */
    uint16_t ids[LINK_SM_PARTS];
    uint16_t flags;
    uint16_t was_flags;

    bool     silent_told[LINK_SM_PARTS];
    bool     online_seen[LINK_SM_PARTS];
    uint8_t  ev_parts[4];     /**< per event bit, the parts it names        */
    uint8_t  events;
} sense_map_link_t;

void sense_map_link_init(sense_map_link_t *s);

/** The link went: nothing on the page is known. */
void sense_map_link_lost(sense_map_link_t *s);

/** An IOmcu answered with protocol minor @p minor; below
 *  LINK_MINOR_SENSE_MAP nothing is sent to it. */
void sense_map_link_came_up(sense_map_link_t *s, uint16_t minor,
                            uint32_t now_ms);

/** The map SENSORS asks for now. */
void sense_map_link_want(sense_map_link_t *s, const sense_map_want_t *w,
                         uint32_t now_ms);

/** The next exchange owed at @p now_ms into @p op; false for none.  The
 *  write goes only while @p idle. */
bool sense_map_link_next(sense_map_link_t *s, uint32_t now_ms, bool idle,
                         sense_map_link_op_t *op);

/** How it went: SENSE_MAP_LINK_ACK with the registers read (NULL for a
 *  write), a NACK's reason, or SENSE_MAP_LINK_NO_ANSWER. */
void sense_map_link_done(sense_map_link_t *s, int result,
                         const uint16_t *regs, uint32_t now_ms);

/** Whether the IOmcu has the page: up, 4.25 or later, and no read
 *  refused. */
bool sense_map_link_page(const sense_map_link_t *s);

/** Whether the page holds the map asked: read since link-up, not
 *  refused, and nothing owed.  False on an IOmcu older than 4.25. */
bool sense_map_link_held(const sense_map_link_t *s);

/** Whether a write was taken since the last call: SENSE's set-up is to be
 *  read again.  Cleared. */
bool sense_map_link_take_reread(sense_map_link_t *s);

/** The events since the last call, SENSE_MAP_LINK_EV_*, the lowest bit
 *  first, one at a time; 0 for none.  @p part is the lowest part it names,
 *  0 to 3 (0 for REFUSED), with its ID for WRONG in @p id.  SILENT and
 *  WRONG are handed out a part at a time. */
uint8_t sense_map_link_event(sense_map_link_t *s, uint8_t *part,
                             uint16_t *id);

/** Put back an event sense_map_link_event() handed out that never reached
 *  the band, with the part it named.  Not once the link has gone. */
void sense_map_link_event_back(sense_map_link_t *s, uint8_t ev,
                               uint8_t part);

/**
 * What fitted part @p a is doing at @p now_ms, a sens_part_t.  @p sl is
 * SENSE's link, which reads the mirror part @p mirror (0 to 3, 0xFF none)
 * on an IOmcu older than 4.25.
 */
sens_part_t sense_map_link_part(const sense_map_link_t *s,
                                const sense_link_t *sl, unsigned a,
                                unsigned mirror, uint32_t now_ms);

/** The four parts' sense_map_link_part(), 4 bits each, part 0 lowest;
 *  SENS_PART_UNKNOWN for a part not fitted. */
uint16_t sense_map_link_status(const sense_map_link_t *s,
                               const sense_link_t *sl, unsigned mirror,
                               uint32_t now_ms);

/** Part @p a's field of a status word. */
#define SENS_PART_OF(word, a) ((sens_part_t)(((word) >> (4u * (a))) & 0x0Fu))

#ifdef __cplusplus
}
#endif
