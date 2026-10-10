/*
 * Which meter measures the servo rail: the INA3221's CH1, the PD mini (the
 * WeAct PD Power Mini supply) or the panel's model.
 *
 * One answer, decided here once per poll by the control task.  A consumer
 * reads the answer and never tests the conditions itself.
 *
 * The INA3221 is the meter while all of these hold:
 *
 *   1. SETUP INTERFACES has the INA3221 on with CH1 among its channels;
 *   2. the coprocessor speaks protocol 4.11 or newer, the one with the
 *      window ring (SERVO_WIN) and SENSE's RESETS;
 *   3. the coprocessor's SENSE page holds that set-up: registers 0 to 11
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
 * no coprocessor answering the meter is the model.
 *
 * A clipped reading and a negative current are readings: neither is a
 * condition here.
 *
 * Two things are said to the operator, each once (servo_source_event()):
 * the INA3221 is on with CH1 and the coprocessor has SENSE and is older
 * than 4.11, at the link-up and when the part is switched on while such a
 * coprocessor answers; and the INA3221's reset count moved.
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

/** A SENSE read, and the newest window's number, count for this long. */
#define SERVO_SOURCE_FRESH_MS  200u
/** Conditions 1 to 6 hold this long before the INA3221 is the meter. */
#define SERVO_SOURCE_SETTLE_MS 1000u
/** One event has the alert band this long before the next is handed out. */
#define SERVO_SOURCE_EVENT_GAP_MS 5000u

/** The servo rail's meter. */
typedef enum {
    SERVO_SOURCE_PDMINI  = 0,
    SERVO_SOURCE_INA3221 = 1,
    SERVO_SOURCE_MODEL   = 2,   /**< no coprocessor answers */
} servo_source_id_t;

/** Why the INA3221 is not the meter: the first condition that fails. */
typedef enum {
    SERVO_SOURCE_WHY_NONE = 0,   /**< it is the meter                     */
    SERVO_SOURCE_WHY_NO_LINK,    /**< no coprocessor answers              */
    SERVO_SOURCE_WHY_OFF,        /**< 1: not on with CH1 in SETUP         */
    SERVO_SOURCE_WHY_OLD,        /**< 2: the coprocessor is older than 4.11 */
    SERVO_SOURCE_WHY_NOT_HELD,   /**< 3: the page does not hold the set-up */
    SERVO_SOURCE_WHY_SILENT,     /**< 4: no fresh SENSE read with the part
                                      online, identified, the bus free    */
    SERVO_SOURCE_WHY_NO_WINDOW,  /**< 5: no fresh window with current     */
    SERVO_SOURCE_WHY_RESET,      /**< 6: the reset count moved            */
    SERVO_SOURCE_WHY_SETTLING,   /**< 7: 1 to 6 hold, not yet for long enough */
} servo_source_why_t;

/** Things the caller tells the operator about, each once. */
enum {
    /** The INA3221 is on with CH1 and the coprocessor is older than 4.11:
     *  the PD mini is the meter. */
    SERVO_SOURCE_EV_OLD   = 0x01,
    /** The INA3221 reset itself: its reset count moved. */
    SERVO_SOURCE_EV_RESET = 0x02,
};

typedef struct {
    servo_source_id_t  id;
    servo_source_why_t why;
    bool     up;            /**< a coprocessor answered at the last step  */
    bool     good;          /**< conditions 1 to 6 held at the last step  */
    uint32_t good_ms;       /**< since when                               */
    bool     resets_known;  /**< resets was read under set-up setups      */
    uint8_t  resets;        /**< the INA3221's reset count, as last read  */
    uint16_t setups;        /**< sense_link_meter_t.setups, likewise      */
    bool     old_told;      /**< the older coprocessor is said            */
    uint32_t events;
    bool     event_given;
    uint32_t event_ms;
} servo_source_t;

/** The meter is the model until a step says otherwise. */
void servo_source_init(servo_source_t *s);

/**
 * One poll: the conditions judged at @p now_ms from @p link_up, the
 * coprocessor's protocol minor @p minor and @p m, sense_link_meter()'s
 * answer of the same poll.  Returns the meter.
 */
servo_source_id_t servo_source_step(servo_source_t *s, uint32_t now_ms,
                                    bool link_up, uint16_t minor,
                                    const sense_link_meter_t *m);

/** The meter as of the last step. */
servo_source_id_t servo_source_id(const servo_source_t *s);

/** Why the INA3221 is not the meter, as of the last step. */
servo_source_why_t servo_source_why(const servo_source_t *s);

/**
 * One event for the alert band, the older coprocessor before the reset,
 * cleared; 0 while none waits or while the last one handed out is younger
 * than SERVO_SOURCE_EVENT_GAP_MS at @p now_ms.
 */
uint32_t servo_source_event(servo_source_t *s, uint32_t now_ms);

/**
 * Put back an event servo_source_event() handed out that never reached
 * the band, so it is handed out again after the gap.  Not once the link
 * has gone: both events are about the coprocessor that answered.
 */
void servo_source_event_back(servo_source_t *s, uint32_t ev);

#ifdef __cplusplus
}
#endif
