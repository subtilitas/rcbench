/**
 * @file
 * @brief ARM + START: the steps one 2s hold takes from a SAFE bench to a
 *        run on a servo.  Pure: fed what the bench reads back and the
 *        clock, it asks the caller to switch, to arm and to start, as
 *        servo_rudder_auto.h does for its run.
 *
 * The steps, in order:
 *
 *   0. FAILSAFE CLEAR, where need.clear: the IOmcu reported its failsafe
 *      latched at the hold (LINK_FAULT_LINK_SILENT) and trusts the
 *      heartbeat.  The caller writes CLEAR once, the write ARM's own
 *      CLEAR is, while SAFE; the failsafe and the heartbeat both read
 *      clear within SERVO_SEQ_CLEAR_MS (500ms) of it.  The IOmcu refuses
 *      every supply's ON while the failsafe stands, so without this step
 *      the supply's ON is refused and ARM, whose CLEAR would lift it, is
 *      never asked.
 *   1. PORT PWM: the port's pulse switched on, where the run drives a PROG
 *      PORT and the setting reads OFF.
 *   2. PROG 5V or PD MINI: the supply that feeds the servo switched on,
 *      where it reads OFF: the rbmod carrier's PROG 5V for a carrier port,
 *      the PD mini's output where it feeds the servo.
 *   3. READ-BACK: every part asked for reads back ON -- the supply, and
 *      PORT PWM's bit on the IOmcu, which the panel sets only while SAFE
 *      (the pulse itself runs only while ARMED) -- and, where need.settle,
 *      a reading taken wholly after that (in->reading_from_ms), all within
 *      SERVO_SEQ_READBACK_MS (1000ms) of step 2.  The caller's checks are
 *      judged on that reading: a rail the INA3221 measured with the supply
 *      still off refuses nothing.
 *   4. ARM: ARM asked where the bench is not ARMED; ARMED within
 *      SERVO_SEQ_ARMED_MS (500ms).
 *   5. RUN: the caller starts its run and says whether it took it
 *      (servo_seq_started()).
 *
 * A step that fails ends the sequence and names itself (servo_seq_part()):
 * a failsafe still read at 500ms names FAILSAFE CLEAR; a supply still to
 * be switched on, or in its read-back, while the IOmcu reports its
 * failsafe (in->latched or in->line_out) names the supply at once, as the
 * IOmcu refuses its ON or switches it off, and no ON is asked (both
 * SERVO_SEQ_WHY_FAILSAFE); a read-back missing at 1000ms names the part
 * that did not read back, an
 * ARMED missing at 500ms names ARM, a check of the caller's that refuses
 * on the way (in->refused: a lost link, a part offline) names the part
 * the caller gives (in->refused_part), else the step under way, a STOP or
 * a touch loss (servo_seq_abort()) the step under way.  A step's time is
 * judged before what it reads: a part that reads back only after its time
 * fails the step, however long the frame between took.  Every failure
 * asks all_off: DISARM, the supply off, the bench SAFE.
 *
 * Steps 0 to 2 take one call where no CLEAR is asked; a CLEAR is the
 * call's only ask, and its clock runs from it.  The clock of steps 3 and 4
 * runs from step 2.  A part that
 * reads ON already is not asked again, so a bench set up by hand runs the
 * same steps.  The caller calls servo_seq_step() once a frame while
 * servo_seq_busy().
 *
 * Shared by RUDDER AUTO, the limit run and the automatic test.  Host-tested
 * in test_servo_start_seq.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_SERVO_START_SEQ_H
#define RCBENCH_SERVO_START_SEQ_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The read-back's time from the supply's ON, ms. */
#define SERVO_SEQ_READBACK_MS 1000u
/** @brief ARMED's time from ARM asked, ms. */
#define SERVO_SEQ_ARMED_MS 500u
/**
 * @brief The failsafe's time to read clear from the CLEAR asked, ms.  The
 *        panel reads STATUS straight after the CLEAR's acknowledgement,
 *        one exchange, about 1ms on a link that answers.
 */
#define SERVO_SEQ_CLEAR_MS 500u

/** @brief The sequence's steps, in order. */
typedef enum {
    SERVO_SEQ_IDLE = 0,  /**< not under way                        */
    SERVO_SEQ_CLEAR,     /**< 0: FAILSAFE CLEAR                    */
    SERVO_SEQ_PWM,       /**< 1: PORT PWM                          */
    SERVO_SEQ_SUPPLY,    /**< 2: PROG 5V or the PD mini's output   */
    SERVO_SEQ_READBACK,  /**< 3: waiting for every part to read ON */
    SERVO_SEQ_ARM,       /**< 4: ARM asked, waiting for ARMED      */
    SERVO_SEQ_RUN,       /**< 5: the caller starts its run         */
    SERVO_SEQ_STEPS      /**< the count, IDLE included             */
} servo_seq_step_t;

/** @brief What a failed step names. */
typedef enum {
    SERVO_SEQ_PART_NONE = 0, /**< nothing failed                  */
    SERVO_SEQ_PART_PWM,      /**< PORT PWM: no pulse read back     */
    SERVO_SEQ_PART_PROG_5V,  /**< PROG 5V                          */
    SERVO_SEQ_PART_PD_MINI,  /**< the PD mini's output             */
    SERVO_SEQ_PART_ARM,      /**< ARM: no ARMED                    */
    SERVO_SEQ_PART_RUN,      /**< the run refused its start        */
    SERVO_SEQ_PART_CLEAR,    /**< FAILSAFE CLEAR: still latched    */
    SERVO_SEQ_PARTS          /**< the count                        */
} servo_seq_part_t;

/** @brief Why a step failed. */
typedef enum {
    SERVO_SEQ_WHY_NONE = 0,  /**< nothing failed                       */
    SERVO_SEQ_WHY_TIMEOUT,   /**< no read-back or no ARMED in its time */
    SERVO_SEQ_WHY_REFUSED,   /**< a check of the caller's refused      */
    SERVO_SEQ_WHY_STOP,      /**< STOP, a touch loss, an operator's tap */
    SERVO_SEQ_WHY_FAILSAFE,  /**< the IOmcu's failsafe stands         */
} servo_seq_why_t;

/** @brief What the run needs switched on: fixed at the start. */
typedef struct {
    bool port_pwm;  /**< the run drives a PROG PORT: its pulse            */
    bool prog_5v;   /**< the rbmod carrier's PROG 5V feeds the servo      */
    bool pd_mini;   /**< the PD mini's output feeds the servo             */
    bool settle;    /**< READ-BACK waits for a reading taken after every
                         part reads ON before it judges in->refused     */
    bool clear;     /**< FAILSAFE CLEAR first: the IOmcu reported its
                         failsafe latched and the heartbeat trusted at
                         the hold                                      */
} servo_seq_need_t;

/** @brief What the bench reads now, each step. */
typedef struct {
    bool pwm_set;   /**< PORT PWM reads ON in the settings                */
    bool pwm_on;    /**< the port's pulse reads back on                   */
    bool p5_on;     /**< PROG 5V reads back ON                            */
    bool pd_on;     /**< the PD mini's output reads back ON               */
    bool armed;     /**< the bench reads ARMED                            */
    bool refused;   /**< a check of the caller's refuses the run now      */
    uint8_t refused_part; /**< servo_seq_part_t the refusal names;
                               SERVO_SEQ_PART_NONE for the step's own    */
    bool reading;   /**< a reading is in, reading_from_ms set             */
    uint32_t reading_from_ms; /**< when the latest reading began, ms, on
                                   now_ms's clock                       */
    bool latched;   /**< the IOmcu reports its failsafe latched
                         (LINK_FAULT_LINK_SILENT)                      */
    bool line_out;  /**< the IOmcu does not trust the heartbeat
                         (LINK_FAULT_HEARTBEAT)                        */
} servo_seq_in_t;

/** @brief What a step asks of the caller, once each. */
typedef struct {
    bool clear;     /**< write CLEAR once, while SAFE: the IOmcu's
                         failsafe lifted as ARM's CLEAR lifts it         */
    bool set_pwm;   /**< switch PORT PWM on                               */
    bool p5_on;     /**< ask PROG 5V ON                                   */
    bool pd_on;     /**< ask the PD mini's output ON                      */
    bool arm;       /**< ask ARM                                          */
    bool start;     /**< start the run now, then servo_seq_started()      */
    bool all_off;   /**< failed: DISARM, every supply off, SAFE           */
} servo_seq_do_t;

/** @brief The sequence's state. */
typedef struct {
    servo_seq_need_t need;  /**< what the run needs                  */
    uint8_t  step;          /**< servo_seq_step_t                    */
    uint8_t  part;          /**< servo_seq_part_t of the failure     */
    uint8_t  why;           /**< servo_seq_why_t of the failure      */
    bool     clear_asked;   /**< step 0 asked CLEAR                  */
    bool     arm_asked;     /**< step 4 asked ARM                    */
    bool     done;          /**< the last sequence started its run   */
    uint32_t step_ms;       /**< when the step under way began, ms   */
    uint32_t readback_ms;   /**< how long the read-back took, ms     */
    bool     parts_on;      /**< READ-BACK: every part reads ON      */
    uint32_t parts_ms;      /**< since when, ms                      */
} servo_seq_t;

/**
 * @brief A sequence at rest: nothing under way, nothing failed.
 * @param q the sequence; nothing for NULL
 */
void servo_seq_init(servo_seq_t *q);

/**
 * @brief Begin: the hold fired.  A failure before is forgotten.
 * @param q      the sequence
 * @param need   what the run needs switched on
 * @param now_ms the time, ms
 * @return false while one is under way, or for NULL
 */
bool servo_seq_begin(servo_seq_t *q, const servo_seq_need_t *need,
                     uint32_t now_ms);

/**
 * @brief The step under way against the clock and what the bench reads:
 *        FAILSAFE CLEAR's ask alone; else steps 1 and 2 in one call, then
 *        the waits.
 * @param q      the sequence
 * @param now_ms the time, ms
 * @param in     what the bench reads
 * @param d      receives what to do; cleared first
 */
void servo_seq_step(servo_seq_t *q, uint32_t now_ms,
                    const servo_seq_in_t *in, servo_seq_do_t *d);

/**
 * @brief The caller's answer to d->start: the run took it, or refused it
 *        (the sequence fails at RUN, all_off asked).
 * @param q  the sequence
 * @param ok whether the run started
 * @param d  receives what to do; cleared first
 */
void servo_seq_started(servo_seq_t *q, bool ok, servo_seq_do_t *d);

/**
 * @brief End a sequence under way now: STOP, a touch loss, a leave, a tap
 *        on the hold.  It fails at the step under way.
 * @param q   the sequence
 * @param why the reason, SERVO_SEQ_WHY_STOP for a STOP
 * @param d   receives what to do; cleared first; all_off unless idle
 */
void servo_seq_abort(servo_seq_t *q, servo_seq_why_t why,
                     servo_seq_do_t *d);

/** @brief Whether a sequence is under way.  @param q the sequence
 *  @return true from servo_seq_begin() to its end */
bool servo_seq_busy(const servo_seq_t *q);

/** @brief The step under way, as shown: PORT PWM in FAILSAFE CLEAR's
 *         place before the first call where no CLEAR is needed.
 *  @param q the sequence
 *  @return servo_seq_step_t; IDLE when none */
servo_seq_step_t servo_seq_step_now(const servo_seq_t *q);

/** @brief What the last sequence's failure names.  @param q the sequence
 *  @return SERVO_SEQ_PART_NONE when it did not fail */
servo_seq_part_t servo_seq_part(const servo_seq_t *q);

/** @brief Why the last sequence failed.  @param q the sequence
 *  @return SERVO_SEQ_WHY_NONE when it did not fail */
servo_seq_why_t servo_seq_why(const servo_seq_t *q);

/**
 * @brief The steps a sequence of @p need shows, and the number of @p step
 *        among them: FAILSAFE CLEAR, PORT PWM and the supply only where
 *        needed.
 * @param need  what the run needs; NULL counts every step but FAILSAFE
 *              CLEAR
 * @param step  a step
 * @param count receives the count of steps shown; may be NULL
 * @return the number of @p step, from 1; 0 for IDLE or a step not shown
 */
int servo_seq_number(const servo_seq_need_t *need, servo_seq_step_t step,
                     int *count);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_SERVO_START_SEQ_H */
