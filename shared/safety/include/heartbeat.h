/*
 * The safety heartbeat: the panel's signal that it is running, and the
 * coprocessor's judgement of whether to believe it.
 *
 * A level cannot carry this.  A GPIO (general-purpose input/output) held
 * high is also what a crashed processor, a shorted pin and an unpowered
 * board produce.  A running panel is one whose output changes, so the safety
 * line carries edges and the coprocessor's outputs sit behind a
 * retriggerable monostable that stays energised only while edges keep
 * arriving.
 *
 * The monostable is a backstop, not the mechanism.  It cannot tell a
 * heartbeat from noise: anything that edges fast enough retriggers it, and a
 * shorted or ringing line edges fast.  So the coprocessor checks the period
 * in firmware too.  Both ends live in one file because they share the
 * numbers.
 *
 * The monitor is asymmetric:
 *
 *     slow to trust    -- several consecutive well-spaced edges before the
 *                         line is called alive, so a burst of noise or a
 *                         single edge at power-on cannot enable an output
 *     instant to doubt -- one bad interval, or one silent window, and it is
 *                         dead again immediately
 *
 * The inverse, a line that enables on a glitch and hesitates to disable, is
 * not a safety interlock.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/**
 * How often the panel asks for an edge.
 *
 * The panel toggles from its render loop, which runs at 39 Hz when the screen
 * is idle and 19.5 Hz on the frames a telemetry sample lands, so the interval
 * actually delivered lands between about 26 ms and 52 ms. Asking for 20 ms
 * means "every frame" without the generator having to know the frame rate.
 */
#define HEARTBEAT_PERIOD_MS   20u

/**
 * The interval the monitor will accept between two edges.
 *
 * The floor rejects noise: a line that is ringing, shorted to a clock, or
 * being driven by anything other than a 39 Hz render loop edges far faster
 * than a panel can, and the monostable alone would happily be retriggered by
 * it. The ceiling is the render loop's worst frame with room to spare -- a
 * panel that has not drawn for 150 ms has stopped drawing.
 */
#define HEARTBEAT_MIN_GAP_MS  4u
#define HEARTBEAT_MAX_GAP_MS  150u

/**
 * Consecutive good intervals before the line is called alive.
 *
 * Four, which is two full toggles: enough that a glitch pair cannot do it,
 * few enough that arming does not wait a perceptible time. At the panel's
 * rate this is a little over a tenth of a second.
 */
#define HEARTBEAT_GOOD_RUN    4u

/* ------------------------------------------------------- the panel's end */

typedef struct {
    uint32_t last_ms;   /**< when the last edge was emitted */
    bool     level;     /**< the level the line is being held at */
    bool     started;   /**< false until the first step, so t=0 is not an edge */
} heartbeat_gen_t;

/** Start held low: nothing downstream may run until this loop asks for it. */
void heartbeat_gen_init(heartbeat_gen_t *g);

/**
 * Advance the generator; returns the level the pin should be driven to.
 *
 * @p alive is the caller's own judgement of whether it is fit to keep the
 * bench running -- touch answering, link up, no latched stop. When it is
 * false the line goes low *immediately* rather than merely stopping: waiting
 * for the monostable to time out would spend its whole period still enabled,
 * and the fastest correct thing to do with a stop is all of it at once.
 *
 * Call it as often as you like; it edges no faster than HEARTBEAT_PERIOD_MS.
 */
bool heartbeat_gen_step(heartbeat_gen_t *g, uint32_t now_ms, bool alive);

/* ------------------------------------------------ the coprocessor's end */

typedef struct {
    uint32_t last_edge_ms;
    uint32_t good_run;      /**< consecutive in-window intervals */
    bool     have_edge;     /**< false until the first edge is seen */
    bool     alive;
    /* Kept for the status page: a line that is rejected is worth being able
     * to ask about, and "how" narrows a fault far faster than "not alive". */
    uint32_t rejected_fast; /**< intervals under the floor  -- noise */
    uint32_t rejected_slow; /**< intervals over the ceiling -- a stall */
} heartbeat_mon_t;

/** Start not alive, with no edge seen. */
void heartbeat_mon_init(heartbeat_mon_t *m);

/**
 * Record an edge on the safety line.
 *
 * Called from wherever edges are noticed -- an interrupt, or a poll that
 * samples faster than HEARTBEAT_MIN_GAP_MS. It only ever costs a subtraction
 * and a compare, so an ISR is a fine place for it.
 */
void heartbeat_mon_edge(heartbeat_mon_t *m, uint32_t now_ms);

/**
 * Whether the line may be believed *now*.
 *
 * Must be called even when no edges are arriving: silence is the failure this
 * exists to catch, and silence generates no events. Calling it is what
 * notices that nothing has happened.
 */
bool heartbeat_mon_alive(heartbeat_mon_t *m, uint32_t now_ms);
