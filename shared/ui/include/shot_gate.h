/**
 * @file
 * @brief When the screenshot popup may stand: one function, true only when
 *        every fact says the bench is known idle.
 *
 * A screenshot is taken at once whatever the bench does.  Its popup waits
 * for this gate, and closes the moment the gate turns false; the picture
 * then waits again (ui_shot.h).  Each fact is named for what it means when
 * true, so a fact nobody filled in, false in a zeroed struct, keeps the
 * popup shut.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief What the bench is now, each fact true when it is idle. */
typedef struct {
    bool link_known;     /**< the link answers, or the bench is simulated:
                              with neither, its state is unknown          */
    bool safe;           /**< SAFE, and no arm is waiting                 */
    bool stop_served;    /**< no STOP request waits for the control task  */
    bool supply_off;     /**< the supply is off and no ON is on its way   */
    bool prog5v_off;     /**< the ports' 5V is off and no ON on its way   */
    bool port_free;      /**< no programmer and no page holds a port      */
    bool no_session;     /**< no KST, Hitec or ESC STICK session is open  */
    bool no_program_io;  /**< no programmer's card job or record is out   */
    bool no_motion;      /**< no sweep, pause or commanded motion         */
    bool no_test;        /**< no automatic test or limit run              */
    bool no_stick_run;   /**< no ESC STICK run                            */
    bool no_update;      /**< no panel or IOmcu update, install or
                              download                                    */
    bool no_fault;       /**< no FAULT bit is up                          */
    bool no_card_fault;  /**< no fault log write is out                   */
    bool no_recording;   /**< no run log, test file or capture is open    */
} shot_facts_t;

/** @brief The first fact that keeps the popup shut. */
typedef enum {
    SHOT_IDLE = 0,          /**< none: the bench is idle                  */
    SHOT_WAIT_LINK,
    SHOT_WAIT_ARMED,
    SHOT_WAIT_STOP,
    SHOT_WAIT_SUPPLY,
    SHOT_WAIT_PROG5V,
    SHOT_WAIT_PORT,
    SHOT_WAIT_SESSION,
    SHOT_WAIT_PROGRAM_IO,
    SHOT_WAIT_MOTION,
    SHOT_WAIT_TEST,
    SHOT_WAIT_STICK,
    SHOT_WAIT_UPDATE,
    SHOT_WAIT_FAULT,
    SHOT_WAIT_CARD_FAULT,
    SHOT_WAIT_RECORDING,
    SHOT_WAIT_COUNT
} shot_wait_t;

/**
 * @brief What only the panel's tasks know of the bench, read each frame.
 *        Named for what each means, so false is the idle side; @c read false
 *        keeps every fact false whatever the rest says.
 */
typedef struct {
    bool     read;           /**< the frame loop filled this in             */
    bool     link_up;        /**< the snapshot's link                       */
    bool     simulated;      /**< the numbers are the panel's model         */
    bool     armed;          /**< the snapshot's ARMED                      */
    bool     arm_wanted;     /**< the snapshot's arm waiting                */
    bool     stop_request;   /**< a STOP waits for the control task         */
    bool     supply_on;      /**< the snapshot's supply output              */
    bool     supply_sample;  /**< the newest sample's output                */
    bool     supply_coming;  /**< supply_live_or_coming()                   */
    bool     prog5v_on;      /**< the PROG 5V box's output                  */
    bool     prog5v_coming;  /**< supplies_live_or_coming()                 */
    bool     hitec_holds;    /**< the HITEC page holds a port               */
    bool     card_job_out;   /**< a programmer's file or card job is out    */
    bool     testing;        /**< s_servo_testing                           */
    bool     cu_active;      /**< the IOmcu's update runs                   */
    bool     update_busy;    /**< panel_update_busy()                       */
    uint16_t faults;         /**< the snapshot's FAULT bits                 */
    bool     fault_log_out;  /**< log_fault_busy()                          */
    bool     run_open;       /**< panel_run_open()                          */
} shot_bench_t;

/**
 * @brief The facts from @p b and from what the screens say of themselves:
 *        a port held, a session open, a sweep, a test, a stick run.
 * @param b    the bench; NULL or @c read false leaves every fact false
 * @param out  takes the facts
 */
void shot_facts_read(const shot_bench_t *b, shot_facts_t *out);

/**
 * @brief Why the popup waits.
 * @param f  the facts; NULL waits as for no link
 * @return SHOT_IDLE, or the first fact that is false, in the enum's order
 */
shot_wait_t shot_gate(const shot_facts_t *f);

/**
 * @brief Whether the popup may stand.
 * @param f  the facts; NULL is false
 * @return true only when shot_gate() answers SHOT_IDLE
 */
static inline bool shot_gate_idle(const shot_facts_t *f)
{
    return shot_gate(f) == SHOT_IDLE;
}

#ifdef __cplusplus
}
#endif
