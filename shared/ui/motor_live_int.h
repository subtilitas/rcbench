/**
 * @file motor_live_int.h
 * @brief The private seam between motor_screen.c, the MOTOR & ESC screen's
 *        state, commands and interface, motor_events.c, its touch events and
 *        frames, and motor_draw.c, its drawing: LIVE's layout inside
 *        motor_screen_int.h's, the channels, the state and the functions one
 *        calls in another.  The other files of the screen (motor_workbench.c,
 *        motor_program_page.c, motor_settings_page.c) keep their own state and
 *        do not include this header.  Nothing outside shared/ui includes it.
 *
 * Included last: it names the state s, which no header included after it
 * may meet.  What one file calls or reads in another carries the prefix
 * mo_.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gfx.h"
#include "motor_screen.h"
#include "touch_types.h"
#include "ui_plot.h"
#include "ui_slider.h"
#include "ui_tabs.h"
#include "ui_text.h"
#include "ui_value_gate.h"
#include "ui_widgets.h"

#include "motor_screen_int.h"

/*
 * Inside the telemetry card: the title line, PLOT | TABLE with the poll
 * rate beside them, the legend, then the plot.  The table takes the
 * legend's and the plot's place.
 */
#define SUB_Y     (UP_Y + TITLE_H + 6)    /**< PLOT | TABLE, px. */
#define SUB_H     40                      /**< Their height, px. */
#define SUB_W     200                     /**< Their row: two of 98px. */
#define LEG_Y     (SUB_Y + SUB_H + 4)     /**< The legend, px. */
#define LEG_H     20                      /**< Its height, px. */
#define PLOT_X    (LEFT_X + INNER)        /**< The plot's left edge, px. */
#define PLOT_W    (LEFT_W - 2 * INNER)    /**< Its width: 600px. */
#define PLOT_Y    (LEG_Y + LEG_H + 4)     /**< Its top, px. */
#define PLOT_H    (UP_Y + UP_H - INNER - PLOT_Y) /**< Its height: 184px. */
/** @brief The poll rate and the link's errors, right of PLOT | TABLE. */
#define RATE_X    (PLOT_X + SUB_W + 14)

/*
 * Inside the THROTTLE card: the readout on the title line's right, then
 * the track with a step key at each end, then the hint, which ends above
 * the alert band.
 */
#define ROW_Y     (LO_Y + 4)              /**< The readout's top, px. */
#define READOUT_W 260                     /**< Its box, at the right, px. */
#define READOUT_H 46                      /**< Its height, px. */
#define READOUT_X (LEFT_X + LEFT_W - INNER - READOUT_W) /**< Its left. */
#define NUDGE_W   72                      /**< A step key's width, px. */
#define NUDGE_H   56                      /**< Its height, px. */
#define CTRL_Y    (ROW_Y + READOUT_H + 4) /**< The step keys' top, px. */
#define TRACK_H   48                      /**< The track's height, px. */
#define TRACK_Y   (CTRL_Y + (NUDGE_H - TRACK_H) / 2) /**< Its top, px. */
#define TRACK_X   (LEFT_X + INNER + NUDGE_W + 8) /**< Its left end, px. */
/**
 * @brief The right end of the step keys' row: UI_TAP_MIN left of the rail's
 *        column, so a finger that misses DISARM to its left lands on no
 *        throttle control.  +1 is x 528 to 599, DISARM from x 648.
 */
#define THR_R     (RC_X - UI_TAP_MIN)
/** @brief The track's length: 422px, between the two step keys. */
#define TRACK_W   (THR_R - NUDGE_W - 8 - TRACK_X)
#define HINT_Y    (CTRL_Y + NUDGE_H + 8)  /**< The hint's line, px. */
/** @brief The hint's left edge, at the card's padding.  The hint draws in
 *         MO_FONT_FINE: the German one is 61 cells, 488px. */
#define HINT_X    (LEFT_X + INNER)
_Static_assert(TRACK_X + TRACK_W + 8 + NUDGE_W + UI_TAP_MIN <= RC_X,
               "+1 ends a finger's width left of DISARM");
_Static_assert(HINT_Y + 20 <= STOP_FLOOR,
               "the throttle's hint ends above the alert band");
_Static_assert(ROW_Y + READOUT_H <= CTRL_Y,
               "the readout ends above the step keys");

/**
 * @brief ARM is a hold, not a press: two seconds from the OK green to the danger
 * red, and the command goes when the fade completes.  A release before then
 * arms nothing, and neither does a finger that leaves the button: the
 * gesture is contact with the control, not with the panel.  A bench that
 * spins a propeller should not do it on a touch that could have been an
 * elbow.
 *
 * The timing, the fade and the flash are ui_widgets' (UI_HOLD_S,
 * ui_hold_fill, ui_hold_flash), so the other gesture that is held rather
 * than pressed cannot come to look different from this one.
 */
#define ARM_HOLD_S    UI_HOLD_S

enum { S_VOLT = 0, S_CURR, S_POWER, S_RPM, S_COUNT };

/*
 * The panel polls at 20Hz, one sample per plot column, so the time base is
 * the plot width in samples: PLOT_W / 20s.
 */
/**
 * @brief The panel's polling rate, 20Hz: one sample per plot column, so the
 *        time base is PLOT_W / 20s.
 */
#define SAMPLE_HZ 20.0f
/**
 * @brief MOTOR & ESC's LIVE state: the plot, the throttle slider and the
 *        bench's report, the ARM gate, the knob's pending command, the
 *        revisions each frame buffer last drew and the touches tracked.
 */
typedef struct {
    ui_plot_t     plot;                 /**< plot of volts, amps, power and RPM */
    ui_slider_t   slider;               /**< throttle slider */
    ui_tabs_t     tabs;                 /**< PLOT | TABLE tabs */
    bench_state_t bench;                /**< the bench's report */
    /* Whether the throttle follows its controls (ui_value_gate.h): the
     * bench's report, an ARM the hold has asked for and a DISARM or STOP not
     * yet answered.  gate.armed is what the ARM button reads. */
    /**
     * @brief Whether the throttle follows its controls; gate.armed is what the
     *        ARM button reads
     */
    ui_value_gate_t gate;
    motor_cmd_t   pending;              /**< the command waiting for the application */
    /* The knob's throttle command waiting in `pending`, and what to put back
     * if it is withdrawn: the slider's value before the knob moved it, and
     * whether a throttle was pending before it. */
    /** @brief Whether the knob's throttle command waits in pending */
    bool          knob_pending;
    /** @brief Whether a throttle was pending before the knob moved */
    bool          knob_had_cmd;
    float         knob_prev;            /**< slider value before the knob moved it */
    /* A finger owned the slider at some point since knob_frame(). */
    /** @brief Whether a finger owned the slider since knob_frame() */
    bool          knob_finger;
    /** @brief Bit per frame buffer: its title and strip were drawn */
    unsigned      drawn_mask;
    /* The arm state of the bench itself, which is what bounds a run.  Kept
     * apart from the gate: leave() reads the bench as disarmed for a disarm
     * it has only asked for. */
    bool          bench_armed;          /**< arm state of the bench itself, which bounds a run */
    /* The push count last painted into each framebuffer; UINT32_MAX
     * means "no paint recorded", which is not a count push can reach. */
    /**
     * @brief Push count last painted into each frame buffer; UINT32_MAX for
     *        none
     */
    uint32_t      drawn_push[2];
    /* And the plot revision, which moves for fewer reasons: a stopped plot
     * takes no samples, so a disarmed bench repaints its readouts and leaves
     * the trace alone. */
    uint32_t      drawn_plot[2];        /**< plot revision last painted into each frame buffer */
    /* The telemetry tag last painted, 0xFF for none.  It changes on an arm
     * edge rather than with the numbers, so it cannot ride drawn_push. */
    /**
     * @brief Telemetry tag last painted into each frame buffer; 0xFF for none
     */
    uint8_t       drawn_title[2];
    /* Bumped by anything that changes a button's or the tab row's
     * appearance.  The throttle carries its own, because a drag moves it on
     * every frame and repainting the buttons with it costs the row's full
     * 800 x 90px rather than the readout's 396 x 30. */
    uint32_t      ctrl_rev;             /**< revision of the buttons and the tab row */
    uint32_t      drawn_ctrl[2];        /**< ctrl_rev last painted into each frame buffer */
    uint32_t      thr_rev;              /**< revision of the throttle's readout and track */
    uint32_t      drawn_thr[2];         /**< thr_rev last painted into each frame buffer */
    /* ARM animates, so it gets a third counter and repaints its own rect. */
    uint32_t      arm_rev;              /**< revision of the ARM button, which animates */
    uint32_t      drawn_arm[2];         /**< arm_rev last painted into each frame buffer */
    int           esc_kv;        /**< as reported by the ESC, 0 if it does not */
    /* The GPIOs bound as throttle (bit n is GPIO n) and whether a binding
     * was read: what the throttle panel names as its target. */
    uint64_t      thr_pins;             /**< GPIOs bound as throttle, bit n for GPIO n */
    bool          thr_pins_known;       /**< whether a binding was read */
    /* The protocol they are bound as: an outbind_protos() index, 0 for
     * none known, MOTOR_PROTO_MIXED for several. */
    /**
     * @brief Protocol they are bound as: an outbind_protos() index, 0 for none
     *        known, MOTOR_PROTO_MIXED for several
     */
    uint8_t       thr_proto;
    /* port_view_revision() when the TARGET row last named the DShot
     * programming ports. */
    /**
     * @brief Port_view_revision() when the TARGET row last named the DShot
     *        programming ports
     */
    uint32_t      thr_ports_rev;
    /* The strip's and SETTINGS' page's revisions last painted into each
     * framebuffer, and whether the TARGET row was drawn locked. */
    uint32_t      drawn_strip[2];       /**< strip revision last painted into each frame buffer */
    /** @brief SETTINGS page revision last painted into each frame buffer */
    uint32_t      drawn_page[2];
    bool          target_locked;        /**< whether the TARGET row was drawn locked */
    /* PROGRAM's session as each framebuffer last had it: whether it was
     * shown, its revision, and the output strip's text (a signature). */
    /** @brief Whether PROGRAM's session was shown in each frame buffer */
    bool          drawn_sess_shown[2];
    uint32_t      drawn_sess[2];        /**< the session's revision in each frame buffer */
    uint32_t      drawn_sess_hold[2];   /**< the session's hold revision in each */
    /** @brief The output strip's text signature in each frame buffer */
    uint32_t      drawn_ostrip[2];
    /* Whether each channel's peak is a measurement: a valid live reading
     * has arrived since the run started.  Kept apart from the live flags,
     * which a source that stops answering mid-run clears while the run's
     * peaks it measured stand. */
    bool          pk_ok[S_COUNT];       /**< per channel: whether its peak is a measurement */
    ui_hold_t     arm;           /**< the ARM gesture: ui_widgets owns it  */
    bool          kst_held;      /**< the servo programmer holds a port    */
    bool          no_pin;        /**< the binding is read, with no motor pin */
    gfx_rect_t    arm_rect;             /**< ARM and DISARM */
    gfx_rect_t    reset_rect;           /**< RESET PEAKS */
    gfx_rect_t    down_rect;    /**< one percentage point down */
    gfx_rect_t    up_rect;      /**< and one up                */
    int           pressed;      /**< 0 none, 1 arm, 2 reset, 3 -1, 4 +1 */
    uint8_t       press_id;             /**< touch id of the press */
    bool          have_press;           /**< whether a press is on the glass */
} mo_state_t;

/* Defined in motor_screen.c. */
/** @brief MOTOR & ESC's LIVE state, in motor_screen.c. */
extern mo_state_t mo_s;
/** @brief Short name for mo_s inside the MOTOR & ESC files. */
#define s mo_s

/* --- motor_screen.c: the channels, the state's rules and the commands --- */

/** @brief The four channels' plot series: volts, amps, power and RPM. */
extern const ui_plot_series_t mo_series[S_COUNT];
/** @brief Per channel, the bench report flags that make its reading valid. */
extern const uint16_t mo_ok[S_COUNT];
/** @brief Per channel, the text id of its name. */
extern const ui_text_id_t mo_names[S_COUNT];

/** @brief Sets the plot's and the slider's colours from the active theme. */
void mo_bind_colours(void);
/**
 * @brief Returns whether the throttle follows its controls: only in
 *        UI_VALUE_LIVE, not on a disarmed bench, not between the ARM hold and
 *        the bench's answer, and not after a DISARM or STOP until the bench
 *        reports disarmed.
 * @return true when the throttle is live
 */
bool mo_value_live(void);
/** @brief Ends a drag on the track and keeps the knob out of the frame. */
void mo_end_drag(void);
/**
 * @brief Posts a command in the one slot, coalescing: a new command overwrites
 *        the pending one, except that a pending DISARM survives everything.
 * @param kind the command
 * @param value the throttle for MOTOR_CMD_THROTTLE, percent
 */
void mo_post(motor_cmd_kind_t kind, float value);
bool mo_arm_locked(void);
bool mo_no_motor_pin(void);
/**
 * @brief Puts the slider, the readout and the step keys at 0% across an arm or
 *        disarm edge, drops a pending throttle and releases a drag.
 */
void mo_throttle_from_zero(void);
/**
 * @brief Returns whether the bench is SAFE as this screen sees it: disarmed,
 *        with no ARM or DISARM asked and unanswered.
 * @return true when SAFE
 */
bool mo_bench_safe(void);
/**
 * @brief Returns whether the Idle pulse and Full pulse may change: SAFE and
 *        ARM not held.
 * @return true when the endpoints are free
 */
bool mo_endpoints_free(void);
/**
 * @brief Returns whether the TARGET row takes no press: the bench is not SAFE,
 *        ARM is held or the servo programmer holds its port.
 * @return true when the row is locked
 */
bool mo_target_locked(void);

/* --- motor_events.c ----------------------------------------------------- */

/**
 * @brief Returns whether a DOWN at (@p x, @p y) is on a control whose press
 *        stops what the bench drives: a session's ABORT, or DISARM while the
 *        button reads DISARM.
 * @param x column, px
 * @param y row, px
 * @return true when the point is on such a control
 */
bool mo_safe_at(int x, int y);
/**
 * @brief Takes a touch event.
 * @param evt the event
 */
void mo_event(const touch_event_t *evt);
/**
 * @brief Advances the screen by one frame: the ARM hold and flash animations,
 *        the knob and the polled state.
 * @param dt_s time since the last tick, s
 */
void mo_tick(float dt_s);

/* --- motor_draw.c ------------------------------------------------------- */

/**
 * @brief Draws the screen into frame buffer @p buffer_index.
 * @param c canvas drawn on
 * @param buffer_index the frame buffer
 */
void mo_render(gfx_canvas_t *c, int buffer_index);
