/**
 * @file
 * @brief Which meter measures the servo rail: the INA3221's CH1, the PD mini
 *        (the WeAct PD Power Mini supply) or the panel's model.
 *
 * One answer, decided here once per poll by the control task.  A consumer
 * reads the answer and never tests the conditions itself.
 *
 * The INA3221 is the meter while all of these hold:
 *
 *   1. SETUP INTERFACES has the INA3221 on with CH1 among its channels;
 *   2. the IOmcu speaks protocol 4.11 or newer, the one with the
 *      window ring (SERVO_WIN) and SENSE's RESETS;
 *   3. the IOmcu's SENSE page holds that set-up: registers 0 to 11
 *      equal, no write owed, no frame refused;
 *   4. the last SENSE read is younger than SERVO_SOURCE_FRESH_MS and its
 *      FLAGS have the INA3221 ONLINE and ID_OK and the bus not stuck;
 *   5. SERVO_WIN's newest CH1 window holds current samples and its number
 *      moved less than SERVO_SOURCE_FRESH_MS ago, four windows;
 *   6. the INA3221's reset count has not moved since the step before;
 *   7. conditions 1 to 6 have held for SERVO_SOURCE_SETTLE_MS without a
 *      break, 20 windows.
 *
 * The meter drops to the PD mini in the first step in which one of 1 to 6
 * fails, and returns SERVO_SOURCE_SETTLE_MS after they all hold again: a
 * part on a loose wire does not change the meter 20 times a second.  With
 * no IOmcu answering the meter is the model.
 *
 * A clipped reading and a negative current are readings: neither is a
 * condition here.
 *
 * Why the INA3221 last stopped being the meter is kept with the change
 * count it happened at (servo_source_dropped()), so a consumer that saw
 * the meter change after the step that changed it still learns the
 * condition: a reset shows as a condition for one step only.  A reset
 * count that moved in that step is the reason kept, whichever condition
 * failed first: the IOmcu takes a part it found reset offline in the
 * read that counts the reset.
 *
 * Two things are said to the operator, each once (servo_source_event()):
 * the INA3221 is on with CH1 and the IOmcu has SENSE and is older
 * than 4.11, at the link-up and when the part is switched on while such an
 * IOmcu answers; and the INA3221's reset count moved.
 *
 * Pure C, no link and no clock of its own.  Control task only.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "sense_link.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief A SENSE read, and the newest window's number, count for this long. */
#define SERVO_SOURCE_FRESH_MS  200u
/** @brief Conditions 1 to 6 hold this long before the INA3221 is the meter. */
#define SERVO_SOURCE_SETTLE_MS 1000u
/**
 * @brief One event has the alert band this long before the next is handed out.
 */
#define SERVO_SOURCE_EVENT_GAP_MS 5000u

/** @brief The servo rail's meter. */
typedef enum {
    SERVO_SOURCE_PDMINI  = 0,
    SERVO_SOURCE_INA3221 = 1,
    SERVO_SOURCE_MODEL   = 2,   /**< no IOmcu answers */
} servo_source_id_t;

/** @brief Why the INA3221 is not the meter: the first condition that fails. */
typedef enum {
    SERVO_SOURCE_WHY_NONE = 0,   /**< it is the meter                     */
    SERVO_SOURCE_WHY_NO_LINK,    /**< no IOmcu answers              */
    SERVO_SOURCE_WHY_OFF,        /**< 1: not on with CH1 in SETUP         */
    SERVO_SOURCE_WHY_OLD,        /**< 2: the IOmcu is older than 4.11 */
    SERVO_SOURCE_WHY_NOT_HELD,   /**< 3: the page does not hold the set-up */
    SERVO_SOURCE_WHY_SILENT,     /**< 4: no fresh SENSE read with the part
                                      online, identified, the bus free    */
    SERVO_SOURCE_WHY_NO_WINDOW,  /**< 5: no fresh window with current     */
    SERVO_SOURCE_WHY_RESET,      /**< 6: the reset count moved            */
    SERVO_SOURCE_WHY_SETTLING,   /**< 7: 1 to 6 hold, not yet for long enough */
} servo_source_why_t;

/** @brief Things the caller tells the operator about, each once. */
enum {
    /**
     * @brief The INA3221 is on with CH1 and the IOmcu is older than 4.11:  the
     *        PD mini is the meter.
     */
    SERVO_SOURCE_EV_OLD   = 0x01,
    /** @brief The INA3221 reset itself: its reset count moved. */
    SERVO_SOURCE_EV_RESET = 0x02,
};

/**
 * @brief The state of which meter measures the servo rail.
 */
typedef struct {
    servo_source_id_t  id;
    servo_source_why_t why;
    uint32_t changes;       /**< times id has changed, modulo 2^32        */
    servo_source_why_t dropped;  /**< why the INA3221 last stopped being
                                      the meter; NONE while it never did  */
    uint32_t dropped_at;    /**< changes as that step left it             */
    bool     up;            /**< an IOmcu answered at the last step  */
    bool     good;          /**< conditions 1 to 6 held at the last step  */
    uint32_t good_ms;       /**< since when                               */
    bool     resets_known;  /**< resets was read under set-up setups      */
    bool     reset_step;    /**< the reset count moved in the last step   */
    uint8_t  resets;        /**< the INA3221's reset count, as last read  */
    uint16_t setups;        /**< sense_link_meter_t.setups, likewise      */
    bool     old_told;      /**< the older IOmcu is said            */
    uint32_t events;
    bool     event_given;
    uint32_t event_ms;
} servo_source_t;

/**
 * @brief The meter is the model until a step says otherwise.
 *
 * @param s The source state.
 */
void servo_source_init(servo_source_t *s);

/**
 * @brief One poll: the conditions judged at @p now_ms from @p link_up, the
 *        IOmcu's protocol minor @p minor and @p m, sense_link_meter()'s answer
 *        of the same poll.
 *
 * Returns the meter.
 *
 * @param s       The source state.
 * @param now_ms  The time, in ms.
 * @param link_up true while the IOmcu link is up.
 * @param minor   The IOmcu's protocol minor version.
 * @param m       The sense_link_meter() answer of this poll.
 * @return The meter, a servo_source_id_t.
 */
servo_source_id_t servo_source_step(servo_source_t *s, uint32_t now_ms,
                                    bool link_up, uint16_t minor,
                                    const sense_link_meter_t *m);

/**
 * @brief The meter as of the last step.
 *
 * @param s The source state.
 * @return The meter as of the last step.
 */
servo_source_id_t servo_source_id(const servo_source_t *s);

/**
 * @brief How many times the meter has changed since servo_source_init(), modulo
 *        2^32.
 *
 * It orders two answers taken at different polls: of two counts the
 * one that is ahead by less than 2^31 is the later.
 *
 * @param s The source state.
 * @return The change count, modulo 2^32.
 */
uint32_t servo_source_changes(const servo_source_t *s);

/**
 * @brief Why the INA3221 is not the meter, as of the last step.
 *
 * @param s The source state.
 * @return Why the INA3221 is not the meter, as of the last step.
 */
servo_source_why_t servo_source_why(const servo_source_t *s);

/**
 * @brief Why the INA3221 last stopped being the meter: SERVO_SOURCE_WHY_RESET
 *        when its reset count moved in that step, else the condition that
 *        failed in it, or SERVO_SOURCE_WHY_NO_LINK.
 *
 * @p at, when not NULL, takes
 * servo_source_changes() as that step left it.  SERVO_SOURCE_WHY_NONE while
 * the INA3221 has not stopped being the meter since servo_source_init().
 *
 * @param s  The source state.
 * @param at Receives the change count of that step, or NULL.
 * @return The reason the INA3221 last stopped being the meter, or
 *         SERVO_SOURCE_WHY_NONE.
 */
servo_source_why_t servo_source_dropped(const servo_source_t *s,
                                        uint32_t *at);

/**
 * @brief One event for the alert band, the older IOmcu before the reset,
 *        cleared; 0 while none waits or while the last one handed out is
 *        younger than SERVO_SOURCE_EVENT_GAP_MS at @p now_ms.
 *
 * @param s      The source state.
 * @param now_ms The time, in ms.
 * @return The event for the alert band, or 0 for none.
 */
uint32_t servo_source_event(servo_source_t *s, uint32_t now_ms);

/**
 * @brief Put back an event servo_source_event() handed out that never reached
 *        the band, so it is handed out again after the gap.
 *
 * Not once the link has gone: both events are about the IOmcu that answered.
 *
 * @param s  The source state.
 * @param ev The event to put back.
 */
void servo_source_event_back(servo_source_t *s, uint32_t ev);

#ifdef __cplusplus
}
#endif
