/*
 * Screen router.
 *
 * The router owns the top 48 px of the panel, the status band with STOP, and
 * hands each screen a sub-canvas of the remaining 800x432 px through
 * gfx_canvas_sub, so a screen cannot draw over STOP.  The band is horizontal
 * because horizontal spans are the cheap direction on a panel whose frame
 * rate is bound by PSRAM (pseudo-static random-access memory) bandwidth.
 *
 * Pure C with no ESP-IDF (Espressif Internet-of-Things Development
 * Framework), so the whole navigation model renders on the host.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <math.h>
#include <stdbool.h>

#include "gfx.h"
#include "touch_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SCREEN_SPLASH = 0,
    SCREEN_OVERVIEW,
    SCREEN_MOTOR,
    SCREEN_SERVO,
    SCREEN_ANALYSER,
    SCREEN_LOGS,
    SCREEN_SETUP,
    SCREEN_BATTERY,
    SCREEN_BALANCE,
    SCREEN_PROGRAMMER,
    SCREEN_OUTPUTS,
    SCREEN_COUNT
} ui_screen_id_t;

typedef struct {
    const char *title;   /**< shown in the band                            */
    /**
     * Return the screen to its initial state.  ui_router_init() calls this on
     * every screen, because selection and scroll position are static state.
     */
    void (*reset)(void);
    void (*enter)(void);
    void (*leave)(void); /**< a bench screen disarms here                  */
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
 * What the band shows.  The application fills it in from the link and hands
 * it over; the router computes none of it.
 */
typedef struct {
    bool     link_up;
    bool     armed;
    uint16_t faults;        /**< a link_fault_t bitmap; 0 is quiet          */
    uint32_t run_seconds;
    const char *mode;       /**< output mode text, or NULL                  */
    /**
     * The numbers are modelled, not measured.
     *
     * Set when the bench numbers come from the panel's own simulator, which
     * runs only while no coprocessor answers.  A coprocessor that answers
     * reports what it can see and nothing else, so the flag it sends
     * (LINK_BN_SIMULATED) is never set by this firmware; the field it has no
     * source for is empty instead.  The router draws SIMULATION across the
     * whole screen while this is set, and no screen can opt out.
     */
    bool simulated;

    /*
     * A link_cap_t bitmap from the coprocessor's identity page.  Zero means
     * nothing is fitted or nothing answered; the menu treats both the same.
     */
    uint16_t capabilities;

    /**
     * Link errors since boot: the coprocessor's CRC (cyclic redundancy
     * check) failures and resyncs added together.  Zero is a clean bus.
     */
    uint32_t link_errors;

    /**
     * The panel's own die temperature in degrees Celsius, or NAN when it is
     * not read.  The panel's, not the coprocessor's: the two boards run at
     * different temperatures and this one is the display's.
     */
    float mcu_temp_c;
} ui_bench_status_t;

void ui_router_set_status(const ui_bench_status_t *status);

/** True while a STOP button is drawn; false on the splash, which has none. */
bool ui_router_stop_live(void);
const ui_bench_status_t *ui_router_status(void);

/**
 * True once after STOP was released, then clears when read.
 *
 * A latch rather than a callback: the application drains it in the loop that
 * drives the heartbeat, so the UI (user interface) never calls into hardware.
 */
bool ui_router_take_stop(void);

/* ------------------------------------------------------------------ alert */

/** Longest alert the router will show, including the terminator. */
#define UI_ALERT_MAX 48

/**
 * Show @p text in a 34 px band across the bottom of every screen, or NULL to
 * clear it.
 *
 * For faults the screen itself does not show, such as a touch controller that
 * has stopped answering.  At the bottom, it covers controls rather than
 * readings.
 */
void ui_router_set_alert(const char *text);
const char *ui_router_alert(void);

#ifdef __cplusplus
}
#endif
