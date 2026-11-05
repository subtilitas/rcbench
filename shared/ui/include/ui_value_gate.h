/**
 * @file
 * @brief Whether a screen's output value follows its controls.
 *
 * MOTOR & ESC's throttle and SERVO's position are commanded only while the
 * bench drives them.  One definition of that, shared by both screens, from
 * what a screen knows: the bench's last report of its armed state, and what
 * the screen itself has asked of the bench and not yet been answered on.
 *
 *   OFF      the bench reports disarmed and nothing is asked
 *   ARMING   an ARM is posted; the bench has not reported armed
 *   LIVE     the bench reports armed and nothing asks it to stop
 *   LEAVING  a DISARM is posted or a STOP latched; the bench still reports
 *            armed, for the frames its answer takes
 *
 * Only LIVE takes input.  Everything a screen decides about the value reads
 * ui_value_state(): the input gate, the dimming, the owners of the value
 * (a drag, the knob, a sweep, a run), which end on the edge out of LIVE,
 * and the callbacks that arrive late.
 *
 * An ARM is answered by an armed report or by a refusal.  Each ARM carries
 * a stamp (ui_value_new_ask()); the panel's control task names the stamp of
 * the newest ARM it refused, and ui_value_refused() takes the gate from
 * ARMING to OFF only for the ask that still waits under that stamp.
 *
 * A link that goes quiet under an armed bench is not an input here: the
 * panel latches a stop for it, which is one.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief What a screen's value does; see the file's head. */
typedef enum {
    UI_VALUE_OFF = 0,
    UI_VALUE_ARMING,
    UI_VALUE_LIVE,
    UI_VALUE_LEAVING,
} ui_value_state_t;

/** @brief What a screen knows of the bench and of its own asks. */
typedef struct {
    bool armed;        /**< the bench as last reported, or as the screen
                            reads it after ui_value_hide()               */
    bool arm_asked;    /**< an ARM posted and not answered               */
    bool leave_asked;  /**< a DISARM posted or a STOP latched, and the
                            bench has not reported disarmed since        */
    uint32_t ask;      /**< the stamp of the newest ARM posted
                            (ui_value_new_ask()), 0 before the first     */
} ui_value_gate_t;

/** @brief What a change did to LIVE. */
typedef enum {
    UI_VALUE_EDGE_NONE = 0,
    UI_VALUE_EDGE_LIVE,     /**< into LIVE, from OFF or ARMING           */
    UI_VALUE_EDGE_LEFT,     /**< out of LIVE, to LEAVING or OFF          */
} ui_value_edge_t;

/**
 * @brief The gate's state.
 * @param g The screen's gate.
 * @return OFF, ARMING, LIVE or LEAVING.
 */
static inline ui_value_state_t ui_value_state(const ui_value_gate_t *g)
{
    if (g->armed) {
        return g->leave_asked ? UI_VALUE_LEAVING : UI_VALUE_LIVE;
    }
    return g->arm_asked ? UI_VALUE_ARMING : UI_VALUE_OFF;
}

/**
 * @brief Whether the value takes input.
 * @param g The screen's gate.
 * @return true in LIVE.
 */
static inline bool ui_value_live(const ui_value_gate_t *g)
{
    return ui_value_state(g) == UI_VALUE_LIVE;
}

/**
 * @brief The edge between two states.
 * @param before The state before a change.
 * @param after  The state after it.
 * @return Into LIVE, out of LIVE, or neither.
 */
static inline ui_value_edge_t ui_value_edge(ui_value_state_t before,
                                            ui_value_state_t after)
{
    if (before != UI_VALUE_LIVE && after == UI_VALUE_LIVE) {
        return UI_VALUE_EDGE_LIVE;
    }
    if (before == UI_VALUE_LIVE && after != UI_VALUE_LIVE) {
        return UI_VALUE_EDGE_LEFT;
    }
    return UI_VALUE_EDGE_NONE;
}

/**
 * @brief The stamp the next ARM a screen posts carries: one count for every
 *        screen, so no two asks of any screens carry the same one.  Never 0;
 *        after 0xFFFFFFFF it is 1.  Render side only.
 * @return The new stamp.
 */
uint32_t ui_value_new_ask(void);

/**
 * @brief For tests: the stamp the last ARM posted carried, so the next
 *        ui_value_new_ask() returns the one after it.
 * @param last The stamp taken as the last one given.
 */
void ui_value_ask_seed(uint32_t last);

/**
 * @brief An ARM has been posted.
 * @param g   The screen's gate.
 * @param ask Its stamp, from ui_value_new_ask().
 */
static inline void ui_value_ask_arm(ui_value_gate_t *g, uint32_t ask)
{
    g->arm_asked = true;
    g->ask       = ask;
}

/**
 * @brief The bench refused the ARM stamped @p ask: an ARM this gate still
 *        waits on under that stamp is dropped, and the gate reads OFF.
 *
 * Anything else is left as it is: an ask already answered, one dropped by a
 * DISARM, a STOP, a touch loss or leaving the screen, one followed by a
 * newer ARM, and an ARM another screen asked for.  The same stamp again
 * changes nothing either, so the caller may pass the newest refusal every
 * frame.
 *
 * @param g   The screen's gate.
 * @param ask The stamp of the ARM refused.
 * @return true when this gate's waiting ARM was the one refused.
 */
static inline bool ui_value_refused(ui_value_gate_t *g, uint32_t ask)
{
    if (!g->arm_asked || g->ask != ask) {
        return false;
    }
    g->arm_asked = false;
    return true;
}

/**
 * @brief The ARM asked for is abandoned: a stop, a touch loss, leaving.
 * @param g The screen's gate.
 */
static inline void ui_value_drop_arm(ui_value_gate_t *g)
{
    g->arm_asked = false;
}

/**
 * @brief A DISARM has been posted, or a STOP latched.
 *
 * On a bench that reports armed it stands until the bench reports disarmed,
 * whether or not the command has been collected.  On one that does not,
 * nothing is left to wait for: an ARM asked for is dropped, and the next
 * armed report is a new arm.
 *
 * @param g The screen's gate.
 * @return The edge it made.
 */
static inline ui_value_edge_t ui_value_ask_leave(ui_value_gate_t *g)
{
    const ui_value_state_t before = ui_value_state(g);
    if (g->armed) {
        g->leave_asked = true;
    }
    g->arm_asked = false;
    return ui_value_edge(before, ui_value_state(g));
}

/**
 * @brief The screen is left: it reads the bench as disarmed until the next
 *        report.  A DISARM asked for stands.
 * @param g The screen's gate.
 * @return The edge it made.
 */
static inline ui_value_edge_t ui_value_hide(ui_value_gate_t *g)
{
    const ui_value_state_t before = ui_value_state(g);
    g->armed     = false;
    g->arm_asked = false;
    return ui_value_edge(before, ui_value_state(g));
}

/**
 * @brief The bench's report of its armed state, every frame.  Disarmed
 *        answers whatever asked it to stop; armed answers an ARM.
 * @param g     The screen's gate.
 * @param armed The bench's report.
 * @return The edge it made.
 */
static inline ui_value_edge_t ui_value_report(ui_value_gate_t *g, bool armed)
{
    const ui_value_state_t before = ui_value_state(g);
    g->armed = armed;
    if (armed) {
        g->arm_asked = false;
    } else {
        g->leave_asked = false;
    }
    return ui_value_edge(before, ui_value_state(g));
}

#ifdef __cplusplus
}
#endif
