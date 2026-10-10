/*
 * Whether a screen's output value follows its controls.
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
 * A link that goes quiet under an armed bench is not an input here: the
 * panel latches a stop for it, which is one.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_VALUE_OFF = 0,
    UI_VALUE_ARMING,
    UI_VALUE_LIVE,
    UI_VALUE_LEAVING,
} ui_value_state_t;

typedef struct {
    bool armed;        /**< the bench as last reported, or as the screen
                            reads it after ui_value_hide()               */
    bool arm_asked;    /**< an ARM posted and not answered               */
    bool leave_asked;  /**< a DISARM posted or a STOP latched, and the
                            bench has not reported disarmed since        */
} ui_value_gate_t;

/** What a change did to LIVE. */
typedef enum {
    UI_VALUE_EDGE_NONE = 0,
    UI_VALUE_EDGE_LIVE,     /**< into LIVE, from OFF or ARMING           */
    UI_VALUE_EDGE_LEFT,     /**< out of LIVE, to LEAVING or OFF          */
} ui_value_edge_t;

static inline ui_value_state_t ui_value_state(const ui_value_gate_t *g)
{
    if (g->armed) {
        return g->leave_asked ? UI_VALUE_LEAVING : UI_VALUE_LIVE;
    }
    return g->arm_asked ? UI_VALUE_ARMING : UI_VALUE_OFF;
}

static inline bool ui_value_live(const ui_value_gate_t *g)
{
    return ui_value_state(g) == UI_VALUE_LIVE;
}

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

/** An ARM has been posted. */
static inline void ui_value_ask_arm(ui_value_gate_t *g)
{
    g->arm_asked = true;
}

/** The ARM asked for is abandoned: a stop, a touch loss, leaving. */
static inline void ui_value_drop_arm(ui_value_gate_t *g)
{
    g->arm_asked = false;
}

/**
 * A DISARM has been posted, or a STOP latched.  On a bench that reports
 * armed it stands until the bench reports disarmed, whether or not the
 * command has been collected.  On one that does not, nothing is left to
 * wait for: an ARM asked for is dropped, and the next armed report is a new
 * arm.
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

/** The screen is left: it reads the bench as disarmed until the next
 *  report.  A DISARM asked for stands. */
static inline ui_value_edge_t ui_value_hide(ui_value_gate_t *g)
{
    const ui_value_state_t before = ui_value_state(g);
    g->armed     = false;
    g->arm_asked = false;
    return ui_value_edge(before, ui_value_state(g));
}

/** The bench's report of its armed state, every frame.  Disarmed answers
 *  whatever asked it to stop; armed answers an ARM. */
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
