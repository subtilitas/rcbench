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
#include <stdint.h>

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
    SCREEN_OUTPUTS,
    SCREEN_PICKER,
    SCREEN_SUPPLY,
    /* Start-up only, and not reachable from the menu: the bus self-test
     * failed and the panel says so before anything else. */
    SCREEN_BUSFAULT,
    /* WI-FI, from LOGS and from SETUP: the panel's Wi-Fi for card
     * downloads and firmware uploads. */
    SCREEN_WIFI,
    /* While the panel writes its own firmware, and not reachable from the
     * menu: nothing on the bench runs, so it carries no band either. */
    SCREEN_UPDATE,
    /* Not reachable from the menu either: the panel opens it to offer the
     * IOmcu an update, and shows the update's progress on it. */
    SCREEN_COPROC_UPDATE,
    /* UPDATES, from SETUP: the check for a newer release on GitHub, the
     * reminder's switch, and the download. */
    SCREEN_UPDATES,
    /* Not reachable from the menu: the panel opens it on OVERVIEW when the
     * start-up check found a newer release. */
    SCREEN_UPD_REMIND,
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
    /**
     * The touch stream broke: at least one event between the last frame and
     * this one never reached this screen.  Drop any gesture in progress.
     *
     * A screen whose controls all act on a release can leave this NULL --
     * losing an event there costs a highlight.  A screen with a gesture that
     * completes on a timer cannot: a press whose release went missing goes
     * on being held, and the timer finishes it with nothing on the glass.
     */
    void (*cancel)(void);
    void (*render)(gfx_canvas_t *c, int buffer_index);
    /**
     * The home tag was tapped.  Return true when the screen went up a level
     * of its own, such as SETUP from a settings list to its tiles, or closed
     * a question or a pop-up that covers it; the router has released every
     * contact the screen owned, as at a change of screen.  False, or NULL,
     * leaves the screen for ui_router_parent().  A level is what an
     * in-screen BACK climbs out of, and the home tag climbs one.
     */
    bool (*back)(void);
    /**
     * The home tag leaves the screen, for @p to: back() returned false or
     * is NULL.  Return true to hold the navigation; the screen stays on top
     * and later resumes it with ui_router_goto(@p to) or drops it.  False,
     * or NULL, leaves at once.  Only the home tag asks: ui_router_goto()
     * never does, so STOP, a safety route and every route the application
     * takes leave at once.  SETUP holds it while settings are unsaved.
     */
    bool (*hold)(ui_screen_id_t to);
    /**
     * Whether a press coming down at (@p x, @p y), in the screen's own
     * coordinates, lands on a control that stops what the bench drives and
     * acts on it now: DISARM on an armed bench, a run's ABORT or STOP.  The
     * answer follows what the control shows, so ARM on a disarmed bench is
     * none.  The router hands such a press to the screen even where the
     * alert band or the band's help bubble covers it, and closes what
     * covered it; every other press there stays the router's.  NULL: the
     * screen has no such control.
     */
    bool (*safe_at)(int x, int y);
} ui_screen_t;

/** The band the router owns, in panel coordinates. */
#define UI_BAND_H 48

/**
 * The alert band the router draws across the bottom of the 480 px panel
 * while an alert shows: its height, and its top in panel coordinates.  It
 * takes the touches under it, so a screen puts nothing that acts there --
 * except a control that stops what the bench drives, whose press the router
 * hands through (ui_screen_t's safe_at).
 */
#define UI_ALERT_H 34
#define UI_ALERT_Y (480 - UI_ALERT_H)

/**
 * The rows the alert band gives @p text: 1 for a text of up to 94 cells,
 * 2 when it breaks at a space onto a second row, more for a text the band
 * cannot hold.  The band draws two rows of the 8x16 face, 16 px apart.
 */
int ui_router_alert_rows(const char *text);

void ui_router_init(void);

/** Switch screens.  Calls leave() on the old and enter() on the new. */
void ui_router_goto(ui_screen_id_t id);

ui_screen_id_t ui_router_current(void);

/**
 * Where the home tag of screen @p id leads when it leaves the screen.  A
 * screen with several openers returns to the one that last opened it:
 * WI-FI to LOGS or SETUP, OUTPUTS to MOTOR & ESC, SERVO or SETUP.  A screen
 * a tile of SETUP opens returns to SETUP, and every other to the menu.
 * WI-FI or OUTPUTS not yet opened from an opener returns to SETUP.
 */
ui_screen_id_t ui_router_parent(ui_screen_id_t id);

/**
 * Count of navigations since ui_router_init(): every switch to a different
 * screen adds one.  Two reads that differ tell that the router moved between
 * them, including away and back to the same screen, which comparing
 * ui_router_current() cannot.
 */
uint32_t ui_router_navigations(void);

/**
 * Count of touch events handed to a screen's event() since ui_router_init(),
 * a release the router makes up for a contact that left the body included.
 * An event the router keeps -- the band's, the alert strip's, one of a
 * contact no screen owns -- does not count.
 */
uint32_t ui_router_dispatched(void);
const char *ui_router_title(ui_screen_id_t id);

void ui_router_tick(float dt_s);

/**
 * One touch event, in panel coordinates, in the order the tracker emits
 * them.
 *
 * A contact has one owner from its DOWN to its UP.  The screen on top owns a
 * contact whose DOWN it was handed, and is handed a MOVE or an UP of no
 * other: a contact that came down on the band or on the alert strip, or
 * that was down when the screen changed or gestures were cancelled, reaches
 * no screen.  One exception: a contact that comes down on a control the
 * screen's safe_at names is the screen's, under the alert strip or with
 * the help bubble open, and the strip or the bubble closes.  A contact the screen owns that reaches the band (y below
 * UI_BAND_H) is released for the screen with an UP at the last point the
 * screen was handed, flagged TOUCH_FLAG_NO_TAP, and is the router's from
 * then on.  A flagged UP ends a press or a drag and activates no control.
 * STOP and the home tag answer to a DOWN on them only.
 */
void ui_router_event(const touch_event_t *evt);

/**
 * Tell every screen that touch events were lost, so a gesture that
 * completes on a timer cannot finish on a contact that is no longer there.
 * Called once per frame in which the panel could not deliver every event.
 * The contacts on the glass are no screen's until they lift and come down
 * again.
 */
void ui_router_cancel_gestures(void);
void ui_router_render(gfx_canvas_t *c, int buffer_index);

/** Repaint every screen's cached chrome into every framebuffer. */
void ui_router_invalidate(void);

/* ------------------------------------------------------------ bench state */

/**
 * What the band shows.  The application fills it in from the link and hands
 * it over; the router computes none of it.
 */
/** What an arm covers, for the band's chip: the motor's outputs, the
 *  servos' and the programming ports'. */
#define UI_ARMED_MOTOR 0x01u
#define UI_ARMED_SERVO 0x02u

typedef struct {
    bool     link_up;
    bool     armed;
    /**
     * UI_ARMED_* bits while armed.  One of them names the role on the chip,
     * ARMED MOTOR or ARMED SERVO; both, or none, read ARMED: every output.
     */
    uint8_t  armed_roles;
    uint16_t faults;        /**< a link_fault_t bitmap; 0 is quiet          */
    uint32_t run_seconds;
    const char *mode;       /**< ui_band_mode(): BENCH or SIM, or NULL      */
    /**
     * The numbers are modelled, not measured.
     *
     * Set when the bench numbers come from the panel's own simulator, which
     * runs only while no IOmcu answers.  An IOmcu that answers
     * reports what it can see and nothing else, so the flag it sends
     * (LINK_BN_SIMULATED) is never set by this firmware; the field it has no
     * source for is empty instead.  The router draws SIMULATION across the
     * whole screen while this is set, and no screen can opt out.
     */
    bool simulated;

    /*
     * A link_cap_t bitmap from the IOmcu's identity page.  Zero means
     * nothing is fitted or nothing answered; the menu treats both the same.
     */
    uint16_t capabilities;

    /**
     * Link errors since boot: the IOmcu's CRC (cyclic redundancy
     * check) failures and resyncs added together.  Zero is a clean bus.
     */
    uint32_t link_errors;

    /**
     * CAN frames the IOmcu refused to decode since its power-up,
     * modulo 2^32: STATUS registers 8 and 9, protocol 4.21.  0 for an
     * IOmcu older than 4.21, which does not count them, and until the
     * first status read after a link-up.
     */
    uint32_t link_refused;

    /**
     * The panel's own die temperature in degrees Celsius, or NAN when it is
     * not read.  The panel's, not the IOmcu's: the two boards run at
     * different temperatures and this one is the display's.
     */
    float mcu_temp_c;
} ui_bench_status_t;

void ui_router_set_status(const ui_bench_status_t *status);

/**
 * The band's chips, each of which opens a help bubble when tapped.  NONE is
 * no chip.
 */
typedef enum {
    UI_BAND_CHIP_NONE = 0,
    UI_BAND_CHIP_LINK,    /**< LINK or NO LINK                             */
    UI_BAND_CHIP_MODE,    /**< BENCH or SIM, ui_band_mode()                */
    UI_BAND_CHIP_FAULT,   /**< FAULT and the link_fault_t bitmap           */
    UI_BAND_CHIP_ARMED,   /**< SAFE, ARMED, ARMED MOTOR or ARMED SERVO     */
    UI_BAND_CHIP_CLOCK,   /**< the run clock                               */
    UI_BAND_CHIP_COUNT
} ui_band_chip_t;

/**
 * The chip whose help bubble is open, or UI_BAND_CHIP_NONE.
 *
 * A tap on a chip of the band opens its bubble under the band.  The bubble
 * follows the status as it changes and closes when its chip is no longer
 * drawn, when the screen changes, or on the next DOWN anywhere; that DOWN
 * reaches no screen, except that a DOWN on STOP presses STOP as well.
 */
ui_band_chip_t ui_router_help(void);

/** Where the open bubble is drawn, in panel coordinates; false when none
 *  is. */
bool ui_router_help_rect(gfx_rect_t *out);

/** True while a STOP button is drawn; false on the splash, which has none. */
bool ui_router_stop_live(void);
const ui_bench_status_t *ui_router_status(void);

/**
 * @brief Whether a press came down on STOP since the last call; clears it.
 *
 * The press acts at once; its release does nothing.  A latch rather than a
 * callback: the application drains it in the loop that drives the
 * heartbeat, so the UI (user interface) never calls into hardware.
 *
 * @return true once after each press on STOP
 */
bool ui_router_take_stop(void);

/* ------------------------------------------------------------------ alert */

/** Longest alert the router will show, including the terminator. */
#define UI_ALERT_MAX 128

/** How long an alert shows before it clears itself, in seconds. */
#define UI_ALERT_SHOW_S 30.0f

/**
 * Show @p text in a 34 px band across the bottom of every screen, or NULL to
 * clear it.
 *
 * For faults the screen itself does not show, such as a touch controller that
 * has stopped answering.  At the bottom, it covers controls rather than
 * readings.  The band clears after UI_ALERT_SHOW_S of ui_router_tick(), or
 * when a tap lands on it and lifts there; a tap on the band reaches no
 * screen.  A press on a control under it that stops what the bench drives
 * -- DISARM, ABORT, STOP TEST -- reaches the screen as if the band were not
 * there, and clears the band (ui_screen_t's safe_at).  Setting an alert again, even the same text, restarts its time.
 */
void ui_router_set_alert(const char *text);

/**
 * The number of the alert ui_router_set_alert() shows now, 0 while none
 * does.  Every call that sets one gives it the next number, so a caller
 * that noted the number can tell whether its alert still shows: not once
 * time or a tap cleared it or another replaced it.
 */
uint32_t ui_router_alert_number(void);

/**
 * Clear the alert ui_router_set_alert() shows if it is still number
 * @p number (ui_router_alert_number()): for an alert whose cause is over
 * before its time is.  Another alert, a held one and a number of 0 are
 * left as they are.  True when it cleared one.
 */
bool ui_router_withdraw_alert(uint32_t number);

/**
 * An alert that neither time nor a tap clears, or NULL to clear it: for a
 * fault that lasts until restart and leaves no touch to tap with, such as a
 * touch controller that did not answer at start.  It is kept apart from the
 * alert ui_router_set_alert() shows: that one shows over it, without an x
 * for the held one, and once cleared the held one shows again.
 */
void ui_router_hold_alert(const char *text);

/** The alert showing: the passing one, else the held one, else NULL. */
const char *ui_router_alert(void);

#ifdef __cplusplus
}
#endif
