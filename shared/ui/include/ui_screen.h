/*
 * Screen router.
 *
 * The predecessor gave every screen its whole 800x480 and shared nothing but
 * a home tag, on the argument that a log viewer, a settings list and a live
 * plot want very different things from their top edge.  That argument still
 * holds for the *body* of a screen and is why the bands below are as shallow
 * as they are.
 *
 * What overturned the rest of it: more than one screen can now arm something.
 * STOP has to be in the same place everywhere, and a status band is horizontal
 * -- the cheap direction on a panel whose frame rate is bandwidth-bound.
 *
 * So the router owns the top 48 px and hands each screen a sub-canvas of what
 * is left.  Not by convention: physically, via gfx_canvas_sub, so a screen
 * cannot draw over STOP even by mistake.
 *
 * Pure C, no ESP-IDF -- the whole navigation model renders on the host.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>

#include "gfx.h"
#include "touch_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SCREEN_SPLASH = 0,
    SCREEN_OVERVIEW,
    /* The five that hold the 44 small features. */
    SCREEN_MOTOR,
    SCREEN_SERVO,
    SCREEN_ANALYSER,
    SCREEN_LOGS,
    SCREEN_SETUP,
    /* Named, routed and honest about what is missing.  A stub that says
     * "coming soon" teaches nobody anything; one that says what is blocking it
     * is a to-do list somebody can answer. */
    SCREEN_BATTERY,
    SCREEN_BALANCE,
    SCREEN_PROGRAMMER,
    SCREEN_COUNT
} ui_screen_id_t;

typedef struct {
    const char *title;   /**< shown in the band                            */
    /**
     * Put the screen back to how it starts.  ui_router_init() calls this on
     * every screen, because a screen's selection and scroll position are
     * static state and "initialise the UI" has to mean it.
     */
    void (*reset)(void);
    void (*enter)(void);
    void (*leave)(void); /**< a bench disarms here; do the safe thing       */
    void (*tick)(float dt_s);
    /** Coordinates are relative to the screen's own area, not the panel. */
    void (*event)(const touch_event_t *evt);
    void (*render)(gfx_canvas_t *c, int buffer_index);
} ui_screen_t;

/** The band the router owns, in panel coordinates. */
#define UI_BAND_H 48

void ui_router_init(void);

/** Switch screens.  Calls leave() on the old and enter() on the new. */
void ui_router_goto(ui_screen_id_t id);

ui_screen_id_t ui_router_current(void);
const char *ui_router_title(ui_screen_id_t id);

void ui_router_tick(float dt_s);
void ui_router_event(const touch_event_t *evt);
void ui_router_render(gfx_canvas_t *c, int buffer_index);

/** Repaint every screen's cached chrome into every framebuffer. */
void ui_router_invalidate(void);

/* ------------------------------------------------------------ bench state */

/**
 * What the band shows.  The router does not compute any of it -- the
 * application fills this in from the link and hands it over, because the band
 * is a view of the *bench*, not of the panel.
 */
typedef struct {
    bool     link_up;
    bool     armed;
    uint16_t faults;        /**< a link_fault_t bitmap; 0 is quiet          */
    uint32_t run_seconds;
    const char *mode;       /**< "DSHOT600", "SBUS", ... or NULL            */
} ui_bench_status_t;

void ui_router_set_status(const ui_bench_status_t *status);
const ui_bench_status_t *ui_router_status(void);

/**
 * True on the frame where STOP was pressed, and clears when read.
 *
 * A latch rather than a callback: the application drains it in the same loop
 * that toggles the heartbeat, so a stop travels as a command *and* stops the
 * line, and neither depends on the UI calling into hardware.
 */
bool ui_router_take_stop(void);

/* ------------------------------------------------------------------ alert */

/** Longest alert the router will show, including the terminator. */
#define UI_ALERT_MAX 48

/**
 * A band across the bottom of every screen, or NULL to clear it.
 *
 * For faults the operator cannot discover from the screen itself -- the panel
 * has stopped answering, so nothing responds and the last frame still looks
 * perfectly healthy.  Bottom, not middle: it covers controls that are not
 * working anyway rather than the numbers, which may be the only thing still
 * worth reading.
 */
void ui_router_set_alert(const char *text);
const char *ui_router_alert(void);

#ifdef __cplusplus
}
#endif
