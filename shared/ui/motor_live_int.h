/*
 * The private seam between motor_screen.c, the MOTOR & ESC screen's state,
 * commands and interface, motor_events.c, its touch events and frames, and
 * motor_draw.c, its drawing: LIVE's layout inside motor_screen_int.h's,
 * the channels, the state and the functions one calls in another.  The
 * other files of the screen (motor_workbench.c, motor_program_page.c,
 * motor_settings_page.c) keep their own state and do not include this
 * header.  Nothing outside shared/ui includes it.
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

/* Inside the upper row's readouts: four stacked on the right. */
#define HERO_H    ((UP_H - 3 * HERO_GAP) / 4)
#define HERO_GAP  4

/* Inside the telemetry panel: its title strip, the legend, then the plot. */
#define LEG_Y     (UP_Y + TITLE_H + 4)
#define LEG_H     30
#define PLOT_X    (LEFT_X + INNER)
#define PLOT_W    (LEFT_W - 2 * INNER)
#define PLOT_Y    (LEG_Y + LEG_H + 4)
#define PLOT_H    (UP_Y + UP_H - INNER - PLOT_Y)

/* Inside the throttle panel: the readout shares the title strip, then the
 * track with a fine adjustment at each end. */
#define ROW_Y     (LO_Y + TITLE_H + 2)
#define ROW_H     34
#define NUDGE_W   54
#define CTRL_Y    (ROW_Y + 42)
#define TRACK_H   40
#define TRACK_X   (LEFT_X + INNER + NUDGE_W + 6)
#define TRACK_W   (LEFT_W - 2 * INNER - 2 * (NUDGE_W + 6))
#define HINT_Y    (CTRL_Y + TRACK_H + 14)

/* The readout's box: from the panel's left edge to where the numerals end.
 * The hero numerals are 32 px tall with 3 px of slant on a 34 px row. */
#define READOUT_W (LEFT_W - 2 * INNER)
#define READOUT_H (CTRL_Y - ROW_Y - 4)

/*
 * ARM is a hold, not a press: two seconds from the OK green to the danger
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
#define ARM_FLASH_FRAMES UI_HOLD_FLASH_FRAMES

/* LIVE's views, PLOT and TABLE, at the left of the line under the strip. */
#define TAB_Y     HDR_Y
#define TAB_H     HDR_H
#define TAB_W     124

enum { S_VOLT = 0, S_CURR, S_POWER, S_RPM, S_COUNT };

/*
 * The panel polls at 20 Hz, one sample per plot column, so the time base is
 * the plot width in samples: PLOT_W / 20 s.
 */
#define SAMPLE_HZ 20.0f
typedef struct {
    ui_plot_t     plot;
    ui_slider_t   slider;
    ui_tabs_t     tabs;
    bench_state_t bench;
    /* Whether the throttle follows its controls (ui_value_gate.h): the
     * bench's report, an ARM the hold has asked for and a DISARM or STOP not
     * yet answered.  gate.armed is what the ARM button reads. */
    ui_value_gate_t gate;
    motor_cmd_t   pending;
    /* The knob's throttle command waiting in `pending`, and what to put back
     * if it is withdrawn: the slider's value before the knob moved it, and
     * whether a throttle was pending before it. */
    bool          knob_pending;
    bool          knob_had_cmd;
    float         knob_prev;
    /* A finger owned the slider at some point since knob_frame(). */
    bool          knob_finger;
    unsigned      drawn_mask;
    /* The arm state of the bench itself, which is what bounds a run.  Kept
     * apart from the gate: leave() reads the bench as disarmed for a disarm
     * it has only asked for. */
    bool          bench_armed;
    /* The push count last painted into each framebuffer; UINT32_MAX
     * means "no paint recorded", which is not a count push can reach. */
    uint32_t      drawn_push[2];
    /* And the plot revision, which moves for fewer reasons: a stopped plot
     * takes no samples, so a disarmed bench repaints its readouts and leaves
     * the trace alone. */
    uint32_t      drawn_plot[2];
    /* The telemetry tag last painted, 0xFF for none.  It changes on an arm
     * edge rather than with the numbers, so it cannot ride drawn_push. */
    uint8_t       drawn_title[2];
    /* Bumped by anything that changes a button's or the tab row's
     * appearance.  The throttle carries its own, because a drag moves it on
     * every frame and repainting the buttons with it costs the row's full
     * 800 x 90 px rather than the readout's 396 x 30. */
    uint32_t      ctrl_rev;
    uint32_t      drawn_ctrl[2];
    uint32_t      thr_rev;
    uint32_t      drawn_thr[2];
    /* ARM animates, so it gets a third counter and repaints its own rect. */
    uint32_t      arm_rev;
    uint32_t      drawn_arm[2];
    int           esc_kv;        /**< as reported by the ESC, 0 if it does not */
    /* The GPIOs bound as throttle (bit n is GPIO n) and whether a binding
     * was read: what the throttle panel names as its target. */
    uint64_t      thr_pins;
    bool          thr_pins_known;
    /* The protocol they are bound as: an outbind_protos() index, 0 for
     * none known, MOTOR_PROTO_MIXED for several. */
    uint8_t       thr_proto;
    /* port_view_revision() when the TARGET row last named the DShot
     * programming ports. */
    uint32_t      thr_ports_rev;
    /* The strip's and SETTINGS' page's revisions last painted into each
     * framebuffer, and whether the TARGET row was drawn locked. */
    uint32_t      drawn_strip[2];
    uint32_t      drawn_page[2];
    bool          target_locked;
    /* PROGRAM's session as each framebuffer last had it: whether it was
     * shown, its revision, and the output strip's text (a signature). */
    bool          drawn_sess_shown[2];
    uint32_t      drawn_sess[2];
    uint32_t      drawn_ostrip[2];
    /* Whether each channel's peak is a measurement: a valid live reading
     * has arrived since the run started.  Kept apart from the live flags,
     * which a source that stops answering mid-run clears while the run's
     * peaks it measured stand. */
    bool          pk_ok[S_COUNT];
    ui_hold_t     arm;           /**< the ARM gesture: ui_widgets owns it  */
    bool          kst_held;      /**< the servo programmer holds a port    */
    gfx_rect_t    arm_rect;
    gfx_rect_t    reset_rect;
    gfx_rect_t    down_rect;    /**< one percentage point down */
    gfx_rect_t    up_rect;      /**< and one up                */
    int           pressed;      /**< 0 none, 1 arm, 2 reset, 3 -1, 4 +1 */
    uint8_t       press_id;
    bool          have_press;
} mo_state_t;

/* Defined in motor_screen.c. */
extern mo_state_t mo_s;
#define s mo_s

/* --- motor_screen.c: the channels, the state's rules and the commands --- */

extern const ui_plot_series_t mo_series[S_COUNT];
extern const uint16_t mo_ok[S_COUNT];
extern const ui_text_id_t mo_names[S_COUNT];

void mo_bind_colours(void);
bool mo_value_live(void);
void mo_end_drag(void);
void mo_post(motor_cmd_kind_t kind, float value);
bool mo_arm_locked(void);
void mo_throttle_from_zero(void);
bool mo_bench_safe(void);
bool mo_endpoints_free(void);
bool mo_target_locked(void);

/* --- motor_events.c ----------------------------------------------------- */

bool mo_safe_at(int x, int y);
void mo_event(const touch_event_t *evt);
void mo_tick(float dt_s);

/* --- motor_draw.c ------------------------------------------------------- */

void mo_render(gfx_canvas_t *c, int buffer_index);
