/**
 * @file
 * @brief The panel's half of the SENSE_MAP page (0x35, protocol 4.25): up to
 *        four INA3221 parts as SETUP > SENSORS has them, written to the IOmcu,
 *        and what each part is doing, for SENSORS and the alert band.
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

/** @brief A map rests this long after its last edit before it is written. */
#define SENSE_MAP_LINK_SETTLE_MS SENSE_LINK_SETTLE_MS
/**
 * @brief The page's IDs and FLAGS are read this often while a part is fitted.
 */
#define SENSE_MAP_LINK_READ_MS 500u
/** @brief An event handed out has the alert band this long before the next. */
#define SENSE_MAP_LINK_EVENT_GAP_MS SENSE_LINK_EVENT_GAP_MS

#define SENSE_MAP_LINK_ACK       SENSE_LINK_ACK
#define SENSE_MAP_LINK_NO_ANSWER SENSE_LINK_NO_ANSWER

/** @brief Things the caller tells the operator about, each once. */
enum {
    /** @brief The page refused the map. */
    SENSE_MAP_LINK_EV_REFUSED = 0x01,
    /** @brief A fitted part has every channel off: no map is written. */
    SENSE_MAP_LINK_EV_DARK    = 0x02,
    /** @brief A fitted part does not answer at its address. */
    SENSE_MAP_LINK_EV_SILENT  = 0x04,
    /**
     * @brief Something answers at a fitted part's address with another
     *        identity.
     */
    SENSE_MAP_LINK_EV_WRONG   = 0x08,
};

/**
 * @brief What a fitted part is doing, for its heading on SETUP > SENSORS; 4
 *        bits each in sense_map_link_status().
 */
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

/** @brief The map SENSORS asks for (settings_sensors_map()). */
typedef struct {
    uint16_t regs[LINK_SM_KEPT];  /**< SENSE_MAP 0 to 25                    */
    uint8_t  dark;                /**< fitted parts with every shunt 0      */
    /** @brief The INA228's address while it is on, 0 while it is off. */
    uint8_t  i228_addr;
    /**
     * @brief The sensor bus's SDA or SCL is unset: no part is enabled, as SENSE
     *        enables no monitor then.
     */
    bool     nobus;
} sense_map_want_t;

/**
 * @brief The kinds of exchange of the SENSE_MAP link.
 */
typedef enum {
    SENSE_MAP_LINK_OP_NONE = 0,
    SENSE_MAP_LINK_OP_READ,     /**< SENSE_MAP 0 to 31                      */
    SENSE_MAP_LINK_OP_WRITE,    /**< SENSE_MAP 0 to 25                      */
    SENSE_MAP_LINK_OP_STATUS,   /**< SENSE_MAP 26 to 30: IDs and FLAGS      */
} sense_map_link_op_kind_t;

/**
 * @brief One exchange the SENSE_MAP link asks the caller to make.
 */
typedef struct {
    sense_map_link_op_kind_t kind;
    bool     write;
    uint8_t  page;
    uint8_t  off, n;
    const uint16_t *regs;     /**< a write's registers, n of them           */
} sense_map_link_op_t;

/**
 * @brief The panel's state of the SENSE_MAP link page.
 */
typedef struct {
    bool     up;              /**< an IOmcu answers                         */
    bool     page;            /**< it has SENSE_MAP (4.25)                  */
    bool     known;           /**< the page has been read since link-up     */

    bool     want_set;
    uint16_t want[LINK_SM_KEPT];  /**< as it is to be written               */
    uint8_t  dark;                      /**< parts left out because the settings leave them dark */
    uint8_t  clash;           /**< parts left out for the INA228's address  */
    uint8_t  fitted;          /**< parts fitted, enabled or not             */
    bool     nobus;           /**< every part left out: no sensor bus       */
    uint32_t want_ms;                   /**< when want was set, ms */

    uint16_t held[LINK_SM_KEPT];
    bool     refused;         /**< the page refused want                    */
    bool     reread;          /**< a write was taken: SENSE to be read again */
    bool     forced;          /**< FLAGS' IN_FORCE as last read, or a write
                                   taken since                              */
    uint32_t setup_ms;        /**< when the map in force was taken or read  */

    sense_map_link_op_kind_t pending;
    bool     asked;
    uint32_t asked_ms;
    bool     have_status;     /**< IDs and FLAGS read under this map        */
    uint16_t ids[LINK_SM_PARTS];        /**< per part, the ID read */
    uint16_t wrong_id[LINK_SM_PARTS];  /**< the ID a WRONG event names    */
    uint16_t flags;                     /**< FLAGS as last read */
    uint16_t was_flags;                 /**< FLAGS before that read */

    bool     silent_told[LINK_SM_PARTS];
    bool     online_seen[LINK_SM_PARTS];
    uint8_t  ev_parts[4];     /**< per event bit, the parts it names        */
    uint8_t  events;                    /**< events pending, a bit each */
    bool     told;            /**< an event was handed out at told_ms       */
    uint32_t told_ms;                   /**< when the event was handed out, ms */
} sense_map_link_t;

/**
 * @brief Sets up the SENSE_MAP link state.
 *
 * @param s The link state to set up.
 */
void sense_map_link_init(sense_map_link_t *s);

/**
 * @brief The link went: nothing on the page is known.
 *
 * @param s The link state.
 */
void sense_map_link_lost(sense_map_link_t *s);

/**
 * @brief An IOmcu answered with protocol minor @p minor; below
 *        LINK_MINOR_SENSE_MAP nothing is sent to it.
 *
 * @param s      The link state.
 * @param minor  The IOmcu's protocol minor version.
 * @param now_ms The time, in ms.
 */
void sense_map_link_came_up(sense_map_link_t *s, uint16_t minor,
                            uint32_t now_ms);

/**
 * @brief The map SENSORS asks for now.
 *
 * @param s      The link state.
 * @param w      The map to ask for.
 * @param now_ms The time, in ms.
 */
void sense_map_link_want(sense_map_link_t *s, const sense_map_want_t *w,
                         uint32_t now_ms);

/**
 * @brief The next exchange owed at @p now_ms into @p op; false for none.
 *
 * The
 *  write goes only while @p idle.
 *
 * @param s      The link state.
 * @param now_ms The time, in ms.
 * @param idle   true while the bus is idle.
 * @param op     Receives the exchange owed.
 * @return true when an exchange is owed.
 */
bool sense_map_link_next(sense_map_link_t *s, uint32_t now_ms, bool idle,
                         sense_map_link_op_t *op);

/**
 * @brief How it went: SENSE_MAP_LINK_ACK with the registers read (NULL for a
 *        write), a NACK's reason, or SENSE_MAP_LINK_NO_ANSWER.
 *
 * @param s      The link state.
 * @param result The result: an acknowledgement, a NACK's reason or
 *               SENSE_MAP_LINK_NO_ANSWER.
 * @param regs   The registers read, or NULL for a write.
 * @param now_ms The time, in ms.
 */
void sense_map_link_done(sense_map_link_t *s, int result,
                         const uint16_t *regs, uint32_t now_ms);

/**
 * @brief Whether the IOmcu has the page: up, 4.25 or later, and no read
 *        refused.
 *
 * @param s The link state.
 * @return true when the IOmcu has the page.
 */
bool sense_map_link_page(const sense_map_link_t *s);

/**
 * @brief The shunt in 0.1mOhm of CH @p ch (1 to 3) of the part SENSE mirrors,
 *        as the page holds the map: the part SENSE's CLIPPED flag and
 *        SERVO_SENSE are about.
 *
 * 0 without a map read, or for no such part.
 *
 * @param s  The link state.
 * @param ch The channel, 1 to 3.
 * @return The shunt in 0.1mOhm, or 0 without a map read or for no such part.
 */
uint16_t sense_map_link_mirror_dmohm(const sense_map_link_t *s, unsigned ch);

/**
 * @brief Whether the map waits for SENSE's pins: it enables a part while  SENSE
 *        has not been read since link-up or the sensor bus's pins it holds  are
 *        not the ones asked.
 *
 * The page judges
 *  a map against the pins it holds, so SENSE writes them first.
 *
 * @param s  The link state.
 * @param sl The SENSE link.
 * @return true when the map waits for SENSE's pins.
 */
bool sense_map_link_waits_for_pins(const sense_map_link_t *s,
                                   const sense_link_t *sl);

/**
 * @brief Whether the page holds the map asked: read since link-up, not
 *        refused, and nothing owed.
 *
 * False on an IOmcu older than 4.25.
 *
 * @param s The link state.
 * @return true when the page holds the map asked.
 */
bool sense_map_link_held(const sense_map_link_t *s);

/**
 * @brief Whether the IOmcu goes by a map written to it, not the one derived
 *        from  SENSE: FLAGS' IN_FORCE as last read, or a write taken since.
 *
 * False
 *  before the page is read and on an IOmcu older than 4.25.
 *
 * @param s The link state.
 * @return true when the IOmcu goes by the map written to it.
 */
bool sense_map_link_in_force(const sense_map_link_t *s);

/**
 * @brief Whether a write was taken since the last call: SENSE's set-up is to be
 *        read again.
 *
 * Cleared.
 *
 * @param s The link state.
 * @return true when a write was taken since the last call.
 */
bool sense_map_link_take_reread(sense_map_link_t *s);

/**
 * @brief The events since the last call, SENSE_MAP_LINK_EV_*, the lowest bit
 *        first, one at a time; 0 for none, and 0 while the last one handed out
 *        is younger than SENSE_MAP_LINK_EVENT_GAP_MS at @p now_ms.
 *
 * @p part is
 *  the lowest part it names, 0 to 3 (0 for REFUSED), with its ID for WRONG
 *  in @p id.  SILENT and WRONG are handed out a part at a time.
 *
 * @param s      The link state.
 * @param now_ms The time, in ms.
 * @param part   Receives the lowest part the event names, 0 to 3.
 * @param id     Receives the ID for WRONG.
 * @return The event, a SENSE_MAP_LINK_EV_* bit, or 0 for none.
 */
uint8_t sense_map_link_event(sense_map_link_t *s, uint32_t now_ms,
                             uint8_t *part, uint16_t *id);

/**
 * @brief Whether the cause of event @p ev for part @p part still holds: the map
 *        refused, the part dark, or, for SILENT and WRONG, the part fitted on a
 *        link that answers.
 *
 * @param s    The link state.
 * @param ev   The event, a SENSE_MAP_LINK_EV_* bit.
 * @param part The part, 0 to 3.
 * @return true while the cause of the event holds.
 */
bool sense_map_link_event_stands(const sense_map_link_t *s, uint8_t ev,
                                 uint8_t part);

/**
 * @brief Put back an event sense_map_link_event() handed out that never reached
 *        the band, with the part it named.
 *
 * Not once its cause is over
 *  (sense_map_link_event_stands()).
 *
 * @param s    The link state.
 * @param ev   The event, a SENSE_MAP_LINK_EV_* bit.
 * @param part The part, 0 to 3.
 */
void sense_map_link_event_back(sense_map_link_t *s, uint8_t ev,
                               uint8_t part);

/**
 * @brief What fitted part @p a is doing at @p now_ms, a sens_part_t.
 *
 * @p sl is
 * SENSE's link, which reads the mirror part @p mirror (0 to 3, 0xFF none)
 * on an IOmcu older than 4.25.
 *
 * @param s      The link state.
 * @param sl     The SENSE link.
 * @param a      The fitted part, 0 to 3.
 * @param mirror The part SENSE mirrors, 0 to 3, or 0xFF for none.
 * @param now_ms The time, in ms.
 * @return What fitted part a is doing, as sens_part_t.
 */
sens_part_t sense_map_link_part(const sense_map_link_t *s,
                                const sense_link_t *sl, unsigned a,
                                unsigned mirror, uint32_t now_ms);

/**
 * @brief The four parts' sense_map_link_part(), 4 bits each, part 0 lowest;
 *        SENS_PART_UNKNOWN for a part not fitted.
 *
 * @param s      The link state.
 * @param sl     The SENSE link.
 * @param mirror The part SENSE mirrors, 0 to 3, or 0xFF for none.
 * @param now_ms The time, in ms.
 * @return The four parts' sense_map_link_part() values, 4 bits each, part 0
 *         lowest.
 */
uint16_t sense_map_link_status(const sense_map_link_t *s,
                               const sense_link_t *sl, unsigned mirror,
                               uint32_t now_ms);

/** @brief Part @p a's field of a status word. */
#define SENS_PART_OF(word, a) ((sens_part_t)(((word) >> (4u * (a))) & 0x0Fu))

#ifdef __cplusplus
}
#endif
