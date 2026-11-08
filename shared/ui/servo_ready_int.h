/**
 * @file
 * @brief The READY card of a SERVO function screen and its ARM + START
 *        sequence (servo_ready.c), the card's rows of RUDDER AUTO
 *        (servo_rudder_auto_ready.c) and of LIMIT RUN
 *        (servo_limit_ready.c): their state in the screen's one object,
 *        the card's places, and the calls the screen's files make of them.
 *
 * The READY card takes the output card's place on RUDDER AUTO, TEST and
 * LIMIT RUN, in the screen's coordinates x 636 to 1016, y 62 to 436:
 *
 *   - READY, the card's title, y 70 to 90;
 *   - up to READY_ROWS_MAX rows of READY_ROW_H (30px) from y 96: a dot,
 *     the label and the state right.  The dot is green for a row met, red
 *     for one that refuses the hold or drives an output now, amber for one
 *     the hold does itself (`START SWITCHES IT ON`), grey for one that
 *     waits on another;
 *   - the rail line, y 404 to 424: the meter the function reads, its volts
 *     and amps.
 *
 * A tap on a row with a fix takes the fix (the PORT choice list, the
 * SETTINGS list on its row, ZERO, SETUP > SENSORS); a tap on any other row
 * not met names it in the alert band.
 *
 * LIVE, SWEEP and RUDDER show their rows the same way over the output
 * card's controls (servo_drive_ready.c): the title and 3 or 5 rows from
 * y 96, SPEED, the type pill and the buttons under them, the rail line at
 * y 404.  Their footer's middle slot is ARM, which switches the supply on
 * first through the same sequence where a part reads off.
 *
 * The footer's middle slot is the function's one hold, HOLD 2s: ARM +
 * START.  It fires the sequence (servo_start_seq.h): PORT PWM, PROG 5V or
 * the PD mini, the read-back within 1000ms, ARM, ARMED within 500ms, the
 * run.  The svs_seq_* calls below are the screen's side of it, the same
 * for every function that starts a run this way: what the run needs, the
 * steps' asks done the way a finger does them, every output off on a
 * failure, and the lines that name the step.
 *
 * The header holds no helper that reads the state: servo_screen_int.h
 * includes it before the state is declared.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx.h"
#include "servo_start_seq.h"
#include "servo_test.h"
#include "touch_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The rows a READY card holds at most. */
#define READY_ROWS_MAX 10
/** @brief A row's height and pitch, px. */
#define READY_ROW_H 30
/** @brief The card's title row, y in the body. */
#define READY_TITLE_Y 70
/** @brief The first row's top, y in the body. */
#define READY_ROW_Y 96
/** @brief The rail line's top, y in the body. */
#define READY_RAIL_Y 404
/** @brief The label's column, px: 13 cells of 10px. */
#define READY_LABEL_W 130
/** @brief The state's column, px: 20 cells of 10px. */
#define READY_VALUE_W 204

/** @brief A row's state, as its dot and its value's colour show it. */
typedef enum {
    RDY_OK = 0, /**< met: green                                     */
    RDY_LIVE,   /**< met, and an output live through it: red dot    */
    RDY_SEQ,    /**< the hold does it: amber                        */
    RDY_BAD,    /**< refuses the hold: red                          */
    RDY_IDLE,   /**< waits on a row above it: grey                  */
    RDY_WARN,   /**< refuses nothing, the operator's to look at:
                     amber dot and value (no pin, no reading)       */
} rdy_state_t;

/** @brief What a row's text names when a step of the sequence fails on
 *         it: the supply, the PD mini, PORT PWM, ARM, or the step under
 *         way. */
typedef enum {
    RDY_K_OTHER = 0, /**< the step under way                        */
    RDY_K_SUPPLY,    /**< PROG 5V or the PD mini, whichever the run needs */
    RDY_K_PD_MINI,   /**< the PD mini, whatever the hold switches   */
    RDY_K_PWM,       /**< PORT PWM                                  */
    RDY_K_ARM,       /**< ARM                                       */
} rdy_kind_t;

/** @brief One row as the card draws it. */
typedef struct {
    uint8_t     st;        /**< rdy_state_t                                */
    const char *label;     /**< the label, in the language showing         */
    const char *why;       /**< the gate's whole text for SEQ and BAD; NULL */
    char        value[64]; /**< the state on the right                     */
    bool        fix;       /**< a tap takes a fix                          */
} rdy_row_t;

/** @brief The card's and the sequence's state, the member `rdy` of the
 *         screen's state. */
typedef struct {
    servo_seq_t seq;        /**< ARM + START's steps                     */
    servo_seq_need_t need;  /**< what the sequence under way needs        */
    int         pressed;    /**< the row a tap is on, -1 for none        */
    uint8_t     press_id;   /**< that contact's track id                 */
    uint32_t    drawn[2];   /**< ready_sig() as each buffer drew it      */
    const char *fail;       /**< the last failure's line, or NULL        */
    char        fail_buf[96]; /**< its text                              */
    uint32_t    fail_alert; /**< its band's number (ui_router_alert_number()),
                                 0 for none: a leave takes it back        */
    bool        clear_owed; /**< FAILSAFE CLEAR asked and not yet taken
                                 (servo_screen_take_clear())              */
} svs_ready_t;

/* --- servo_ready.c: the card ---------------------------------------------- */

/** @brief The card and the sequence at the screen's start. */
void ready_init(void);
/** @brief Whether the function shown has a READY card in the output
 *         card's place.  @return true on RUDDER AUTO, TEST and LIMIT RUN */
bool ready_shown(void);
/** @brief The card's rows now.  @return the count, at most
 *         READY_ROWS_MAX */
int ready_count(void);
/** @brief Row @p i as drawn now.  @param i the row @param r receives it
 *  @return false past the last */
bool ready_row(int i, rdy_row_t *r);
/** @brief The first row, in the gate's order, that refuses the hold, and
 *         its text for the alert band.
 *  @param buf takes `LABEL: why` @param n its size
 *  @return @p buf; NULL when no row refuses */
const char *ready_refusal(char *buf, size_t n);
/** @brief Row @p i's place.  @param i the row @return its rectangle;
 *         empty past READY_ROWS_MAX */
gfx_rect_t ready_row_rect(int i);
/** @brief The card's place: the output card's.  @return its rectangle */
gfx_rect_t ready_card_rect(void);
/** @brief The rail line's place.  @return its rectangle */
gfx_rect_t ready_rail_rect(void);
/** @brief The card drawn whole into @p c.  @param c the canvas */
void ready_render(gfx_canvas_t *c);
/** @brief The rail line alone, as the meter's readings move.
 *  @param c the canvas */
void ready_render_rail(gfx_canvas_t *c);
/** @brief One number for what the card shows but the rail line.
 *  @return the number */
uint32_t ready_sig(void);
/** @brief A DOWN on the card: a row's tap on its release.  Every DOWN on
 *         the card is the card's.  @param evt the DOWN
 *  @return whether it landed on the card */
bool ready_down(const touch_event_t *evt);
/** @brief The rest of the press ready_down() took.  @param evt the event
 *  @return whether it was that press's */
bool ready_rest(const touch_event_t *evt);
/** @brief A press under way dropped. */
void ready_cancel(void);
/** @brief The function's one hold, the footer's middle slot.
 *  @return its rectangle */
gfx_rect_t ready_hold_rect(void);
/** @brief Whether a press on the hold stops something now: the sequence
 *         under way, or the function's run.  @return true then */
bool ready_hold_stops(void);
/** @brief A DOWN on the hold, the function's: it stops the sequence or the
 *         run, or begins the hold.  @param evt the DOWN
 *  @return whether it landed on the hold */
bool ready_hold_down(const touch_event_t *evt);
/** @brief The hold drawn: ARM + START, the step under way, or STOP RUN.
 *  @param c the canvas */
void ready_draw_hold(gfx_canvas_t *c);

/* --- servo_ready.c: the sequence -------------------------------------------- */

/** @brief What a run started now needs switched on: PORT PWM on a PROG
 *         PORT, the carrier's PROG 5V on its port, the PD mini's output
 *         where it feeds the servo.  @return the needs */
servo_seq_need_t svs_seq_need(void);
/** @brief The hold fired: the sequence begins.  @return false while one is
 *         under way */
bool svs_seq_begin(void);
/**
 * @brief The sequence's frame: what the bench reads in, the steps' asks
 *        done.  A failure switches every output off and names the step in
 *        the alert band.
 * @param refused the caller's first check that refuses the run now, NULL
 *        for none: a step fails on it
 * @return true on the frame the caller starts its run; it answers with
 *         svs_seq_started()
 */
bool svs_seq_frame(const char *refused);
/** @brief The caller's answer to svs_seq_frame()'s start.
 *  @param ok whether the run took it
 *  @param why the run's refusal, for the failure's line; NULL with @p ok */
void svs_seq_started(bool ok, const char *why);
/** @brief End a sequence under way: the bench's reason, as test_end_now()
 *         gets it.  A leave ends it as the leave switches the outputs.
 *  @param why the reason */
void svs_seq_end(servo_test_abort_t why);
/** @brief Whether a sequence is under way.  @return true while it is */
bool svs_seq_busy(void);
/** @brief The step under way as the hold and the state line name it:
 *         `START, STEP 3 OF 5: READ-BACK`.
 *  @param buf takes it @param n its size  @return @p buf; "" when none */
const char *svs_seq_line(char *buf, size_t n);
/** @brief The step under way, short, for the hold: `READ-BACK ...`.
 *  @param buf takes it @param n its size  @return @p buf; "" when none */
const char *svs_seq_label(char *buf, size_t n);
/** @brief The last failure's line, `PROG 5V FAILED: NO READ-BACK IN
 *         1000ms`, until a hold begins a new one.  @return it, or NULL */
const char *svs_seq_failure(void);

/* --- servo_rudder_auto_ready.c: RUDDER AUTO's rows ------------------------- */

/** @brief RUDDER AUTO's rows, in the card's order. */
typedef enum {
    RAR_PORT = 0,  /**< a PROG PORT the tester drives                  */
    RAR_SERVO,     /**< the family the run programs                    */
    RAR_METER,     /**< the INA3221 channel the run reads              */
    RAR_ANGLE,     /**< the angle source                               */
    RAR_ZERO,      /**< ZERO taken                                     */
    RAR_RANGE,     /**< PULSE MIN to MAX hold 1000 to 2000us           */
    RAR_PROG,      /**< the programmer free                            */
    RAR_SUPPLY,    /**< PROG 5V, the PD mini or the rail               */
    RAR_PWM,       /**< the port's pulse                               */
    RAR_ARM,       /**< ARMED, or a run or a report in the way         */
    RAR_COUNT      /**< the count                                      */
} rauto_row_t;

/** @brief RUDDER AUTO's row @p i.  @param i the row @param r receives it
 *  @return false past the last */
bool rauto_ready_row(int i, rdy_row_t *r);
/** @brief The row of RUDDER AUTO's card a gate text belongs to.
 *  @param why a text rauto_blocked() returns
 *  @return the row; RAR_COUNT for a text of none */
rauto_row_t rauto_row_of(const char *why);
/** @brief Whether the hold may fix @p why itself: a SAFE bench and a part
 *         it switches.  @param why a gate text @return true for amber */
bool rauto_seq_fixes(const char *why);
/** @brief The first gate check that refuses the hold, as rauto_blocked()
 *         orders them, less those the hold does itself.
 *  @param row receives its row; may be NULL
 *  @return the text; NULL when the hold may begin */
const char *rauto_hold_blocked(rauto_row_t *row);
/** @brief A tap on RUDDER AUTO's row @p i: its fix, or its reason in the
 *         alert band.  @param i the row */
void rauto_ready_tap(int i);

/* --- servo_test_ready.c: TEST's rows and its hold ------------------------ */

/** @brief TEST's rows, in the card's order. */
typedef enum {
    TRR_PORT = 0,  /**< OUTPUTS, or a PROG PORT off the carrier, its pulse */
    TRR_METER,     /**< the meter the run reads                           */
    TRR_PD,        /**< the PD mini the steps set                         */
    TRR_STEPS,     /**< the steps chosen and inside the caps              */
    TRR_RANGE,     /**< the ends either side of PULSE CENTER              */
    TRR_ARM,       /**< ARMED, or a run or a report in the way            */
    TRR_COUNT      /**< the count                                         */
} test_row_t;

/** @brief TEST's row @p i.  @param i the row @param r receives it
 *  @return false past the last */
bool test_ready_row(int i, rdy_row_t *r);
/** @brief The row of TEST's card a gate text belongs to.
 *  @param why a text test_why() returns
 *  @return the row; TRR_COUNT for a text of none */
test_row_t test_row_of(const char *why);
/** @brief The first gate check that refuses ARM + START, less those the
 *         steps do themselves.  @param row receives its row; may be NULL
 *  @return the text; NULL when the hold may begin */
const char *test_hold_blocked(test_row_t *row);
/** @brief A tap on TEST's row @p i: its fix, or its reason in the alert
 *         band.  @param i the row */
void test_ready_tap(int i);
/** @brief A DOWN on TEST's ARM + START.  @param evt the DOWN
 *  @return whether it was on the hold */
bool test_hold_down(const touch_event_t *evt);
/** @brief ARM + START's 2s completed, on the footer or on the HV warning:
 *         the settings read, the steps begun. */
void test_hold_fired(void);
/** @brief TEST's frame: the hold, the steps, the run.
 *  @param dt_s the frame's time, s */
void test_frame(float dt_s);
/** @brief TEST's ARM + START in the footer.  @param c the canvas */
void test_draw_hold(gfx_canvas_t *c);
/** @brief The rail's plot under TEST's caps button.  @param c the canvas */
void test_draw_plot(gfx_canvas_t *c);

/* --- servo_limit_ready.c: LIMIT RUN's gate and rows ------------------------ */

/** @brief LIMIT RUN's rows, in the card's order. */
typedef enum {
    LRR_PORT = 0,  /**< an output the tester drives                    */
    LRR_SERVO,     /**< the family a run on a PROG PORT reads          */
    LRR_METER,     /**< the INA3221 channel the run reads              */
    LRR_RAIL,      /**< the servo rail, 3.0V or more                   */
    LRR_RANGE,     /**< PULSE MIN, CENTER and MAX                      */
    LRR_PROG,      /**< the programmer free                            */
    LRR_SUPPLY,    /**< PROG 5V, the PD mini or the rail               */
    LRR_PWM,       /**< the port's pulse                               */
    LRR_ARM,       /**< ARMED, or a run or a report in the way         */
    LRR_COUNT      /**< the count                                      */
} limrun_row_t;

/** @brief LIMIT RUN's row @p i.  @param i the row @param r receives it
 *  @return false past the last */
bool lim_ready_row(int i, rdy_row_t *r);
/** @brief The row of LIMIT RUN's card a gate text belongs to.
 *  @param why a text lim_blocked() returns
 *  @return the row; LRR_COUNT for a text of none */
limrun_row_t lim_row_of(const char *why);
/** @brief What a failed step names for @p why.  @param why a gate text
 *  @return its kind */
rdy_kind_t lim_kind_of(const char *why);
/** @brief Whether the hold may fix @p why itself: RUDDER AUTO's parts, and
 *         a dead rail the supply the hold switches feeds.
 *  @param why a gate text @return true for amber */
bool lim_seq_fixes(const char *why);
/** @brief The first gate check that refuses the hold, as lim_blocked()
 *         orders them, less those the hold does itself.
 *  @param row receives its row; may be NULL
 *  @return the text; NULL when the hold may begin */
const char *lim_hold_blocked(limrun_row_t *row);
/** @brief A tap on LIMIT RUN's row @p i: its fix, or its reason in the
 *         alert band.  @param i the row */
void lim_ready_tap(int i);
/** @brief lim_write_why() less the PROG 5V the hold switches on: why a run
 *         started by the hold writes nothing.  @return the line, or NULL */
const char *lim_write_why_held(void);

/* --- servo_drive_ready.c: LIVE's, SWEEP's and RUDDER's rows -------------- */

/** @brief The rows of LIVE, SWEEP and RUDDER; LIVE and SWEEP show PORT,
 *         SUPPLY and ARM. */
typedef enum {
    DRR_PORT = 0, /**< what the tester drives                         */
    DRR_ANGLE,    /**< the angle source: RUDDER only                  */
    DRR_ZERO,     /**< ZERO taken: RUDDER only                        */
    DRR_SUPPLY,   /**< PROG 5V, the PD mini or a supply off the bench */
    DRR_ARM,      /**< ARMED, or what keeps ARM from the bench        */
    DRR_COUNT     /**< the count                                      */
} drive_row_t;

/** @brief Whether the function shown is LIVE, SWEEP or RUDDER: the READY
 *         rows over its output card and ARM that switches the supply.
 *  @return true then */
bool drive_shown(void);
/** @brief The rows shown.  @return 3 on LIVE and SWEEP, 5 on RUDDER, 0
 *         elsewhere */
int drive_ready_count(void);
/** @brief Card row @p i as drawn now.  @param i the row
 *  @param r receives it  @return false past the last */
bool drive_ready_row(int i, rdy_row_t *r);
/** @brief The first row that refuses ARM, and its text.
 *  @param row receives its card row; may be NULL
 *  @return the text; NULL when ARM may be held */
const char *drive_hold_blocked(int *row);
/** @brief The step a refusal names, for the sequence's failure.
 *  @param why a text drive_hold_blocked() returned
 *  @return servo_seq_part_t: PROG 5V, PD MINI or ARM; NONE otherwise */
servo_seq_part_t drive_part_of(const char *why);
/** @brief What ARM switches on: PORT PWM on a PROG PORT, PROG 5V on the
 *         carrier's port, the PD mini where it feeds the servo and the
 *         rail does not read fed from elsewhere.  @return the needs */
servo_seq_need_t drive_seq_need(void);
/** @brief A tap on card row @p i: its fix, or its reason in the alert
 *         band.  @param i the row */
void drive_ready_tap(int i);
/** @brief The output card's controls laid out for the function shown:
 *         SPEED, CENTER, SWEEP and RELEASE where it has them. */
void drive_layout(void);
/** @brief A DOWN on ARM or DISARM on LIVE, SWEEP or RUDDER: DISARM and
 *         every output off, a stop of the steps, a refusal named, or the
 *         hold begun.  @param evt the DOWN  @return false on any other
 *         screen */
bool drive_arm_press(const touch_event_t *evt);
/** @brief ARM's hold fired on LIVE, SWEEP or RUDDER: the steps where a
 *         part reads off, else ARM.  @return false on any other screen */
bool drive_arm_fired(void);
/** @brief The frame: a hold dropped as a row turns red, the steps.
 *  @param dt_s the frame */
void drive_frame(float dt_s);
/** @brief Whether ARM's hold switches a part on first.  @return true
 *         while a part ARM needs reads off */
bool drive_switches(void);
/** @brief The output card on LIVE, SWEEP and RUDDER: the READY rows,
 *         SPEED, the type pill, the buttons and the rail line.
 *  @param c the canvas */
void drive_render_card(gfx_canvas_t *c);

/** @brief ARM, DISARM or the step under way in the footer's middle slot
 *         on LIVE, SWEEP and RUDDER.  @param c the canvas */
void drive_draw_arm(gfx_canvas_t *c);
/** @brief SPEED's track on LIVE and SWEEP.  @return it; empty elsewhere */
gfx_rect_t drive_speed_rect(void);
/** @brief The type pill under the rows.  @return its rectangle */
gfx_rect_t drive_tag_rect(void);
/** @brief The profile in force, its frame rate and pulse range, in the
 *         danger colour for one that can destroy a servo not made for it
 *         (servo_screen_draw.c).  @param c the canvas @param r the pill */
void svs_draw_tag(gfx_canvas_t *c, gfx_rect_t r);

/** @brief Every output the bench drives off and the bench to SAFE:
 *         DISARM, the carrier's PROG 5V, the PD mini's output. */
void svs_all_off(void);
/** @brief svs_all_off() without the DISARM: the carrier's PROG 5V and the
 *         PD mini's output off, for a run's failed end that leaves the
 *         DISARM to its own step (lim_safe_follow()). */
void svs_supplies_off(void);

/* --- servo_prog_ready.c: KST's and HITEC's rows and WRITE ------------------ */

/** @brief KST's and HITEC's rows, in the card's order. */
typedef enum {
    PRR_PORT = 0, /**< a PROG PORT the tester drives                  */
    PRR_SUPPLY,   /**< the supply the session's CONNECT switches      */
    PRR_SERVO,    /**< the servo read                                 */
    PRR_COUNT     /**< the count                                      */
} prog_ready_row_t;

/** @brief Whether the screen shown is a programmer's, KST or HITEC.
 *  @return true there */
bool prog_ready_shown(void);
/** @brief KST's or HITEC's row @p i.  @param i the row @param r receives
 *  it  @return false past the last or on another screen */
bool prog_ready_row(int i, rdy_row_t *r);
/** @brief A tap on row @p i: PORT's list, the supply off while its row
 *         reads ON, or the row's reason in the alert band.  @param i the
 *         row */
void prog_ready_tap(int i);
/** @brief Whether a press at (@p x, @p y) lands on the supply row while it
 *         reads ON: a tap there switches an output off (svs_safe_at()).
 *  @param x the body's x @param y its y  @return true then */
bool prog_ready_stops_at(int x, int y);
/** @brief The footer's WRITE n VALUES in the middle slot.
 *  @param c the canvas */
void prog_draw_write(gfx_canvas_t *c);
/** @brief One number for what the footer's WRITE shows.
 *  @return the number; 0 on another screen */
uint32_t prog_write_sig(void);
/** @brief Whether a contact is on the footer's WRITE.
 *  @return true then */
bool prog_write_pressed(void);
/** @brief A DOWN on the footer's middle slot on KST or HITEC.
 *  @param evt the DOWN  @return whether it landed there */
bool prog_write_down(const touch_event_t *evt);
/** @brief The rest of the press prog_write_down() took; its tap UP writes
 *         as the session's own WRITE n VALUES does.
 *  @param evt the event  @return whether it was that press's */
bool prog_write_rest(const touch_event_t *evt);
/** @brief A press on WRITE dropped. */
void prog_ready_reset(void);

#ifdef __cplusplus
}
#endif
