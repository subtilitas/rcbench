/**
 * @file
 * @brief The screen a bad CAN (Controller Area Network) bus gets at start-up,
 *        and a link that stops.
 *
 * The panel runs the echo self-test once, before the bench is usable, and
 * shows this instead of the menu when the test does not pass.  It exists
 * because the fault it reports is not visible from any other screen: a bus
 * that does not carry frames looks exactly like an IOmcu that is not
 * there, and both look like a bench that simply shows no numbers.
 *
 * What it puts on the panel is the verdict, the counts behind it, and the
 * things to check in the order that costs least to check.  An operator with
 * no console and no meter can act on it.
 *
 * Leaving it takes a two-second hold, the ARM gesture: this is the one screen
 * that says the bench cannot be trusted, and an acknowledgement given by
 * brushing the panel is not one.  While the link is silent the hold is the
 * acknowledgement, and the bench runs in simulation.  Once the link has
 * answered for BUSFAULT_BACK_MS without a break the hold is RECONNECT: the
 * menu, on the live bench, SAFE.  A drop takes RECONNECT back.
 *
 * One start-up verdict leaves with a tap instead: a silent bus on which no
 * IOmcu answered a status request either (busfault_no_iomcu()).  That is
 * the panel powered on its own, the IOmcu not yet fitted or not yet wired,
 * and the screen says so in its own words: no IOmcu answered, the panel
 * runs its simulation.  Frames that came back altered or went missing are a
 * wiring fault, and keep the hold.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_BUSFAULT_SCREEN_H
#define RCBENCH_BUSFAULT_SCREEN_H

#include <stdbool.h>
#include <stdint.h>

#include "can_selftest.h"
#include "ui_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Which of the two faults brought the screen up. */
typedef enum {
    /** The start-up echo test did not pass. */
    BUSFAULT_SELFTEST = 0,
    /**
     * The link was up and stopped.  A different question from the start-up
     * one: the wire carried frames a moment ago, so what is worth showing is
     * what this end's controller is doing now, not what to check on the
     * bench.
     */
    BUSFAULT_LINK_LOST,
} busfault_kind_t;

/**
 * The panel's own CAN controller, as can_twai_recover() found it.
 *
 * Mirrors can_twai_health_t, which lives in a firmware component: the screen
 * is built for the host suite and cannot see it.  The application maps one to
 * the other in one place.
 */
typedef enum {
    BUSFAULT_BUS_UNKNOWN = 0, /**< not started, or the status would not read */
    BUSFAULT_BUS_RUNNING,     /**< on the bus                               */
    BUSFAULT_BUS_RECOVERING,  /**< counting bus-free signals before stopping */
    BUSFAULT_BUS_STOPPED,     /**< idle, and would not start                */
    BUSFAULT_BUS_OFF,         /**< off the bus, and recovery would not begin */
} busfault_bus_t;

/**
 * What the start-up test found, as the screen needs it.
 *
 * Both ends, because the pair is what separates the faults: frames the far
 * end answered and this end never heard are a return-path fault, and frames
 * it never answered are an outbound one.
 */
typedef struct {
    busfault_kind_t kind;  /**< why the screen is shown: the start-up test failed or the link was lost */
    can_selftest_verdict_t verdict;  /**< the self-test's verdict */

    uint32_t sent;  /**< the frames sent */
    uint32_t echoed;  /**< the frames the far end echoed */
    uint32_t corrupt;  /**< the frames that came back corrupt */
    uint32_t lost;  /**< the frames that did not come back */

    uint32_t tx_errors;   /**< the panel's controller */
    uint32_t rx_errors;  /**< the receive errors of the panel's controller */
    uint32_t bus_errors;  /**< the bus errors of the panel's controller */
    bool     bus_off;  /**< the controller is off the bus */

    /* --- BUSFAULT_LINK_LOST only ------------------------------------- */
    busfault_bus_t bus;        /**< what this panel's controller is doing  */
    uint32_t       down_s;     /**< seconds since the link last answered   */
    uint32_t       recoveries; /**< times the bus has been asked back      */
    uint32_t       polls;      /**< requests this panel has sent           */
    uint32_t       timeouts;   /**< and how many got no answer             */

    bool     have_remote; /**< the IOmcu answered a status request */
    /** @brief The IOmcu answered either status request, the one before
     *         the echo run or the one after it: an IOmcu that answered
     *         and then went quiet is a fault, not a missing IOmcu. */
    bool     remote_seen;
    bool     remote_up;   /**< and said its own controller came up       */
    uint8_t  remote_tx_errors; /**< the IOmcu's transmit error counter */
    uint8_t  remote_rx_errors; /**< its receive error counter          */
    uint8_t  remote_flags;     /**< its controller's flags             */
    uint16_t remote_overflows; /**< frames it dropped for want of a buffer */
} busfault_report_t;

/** @brief The time the link answers without a break before RECONNECT is
 *         offered: 1000ms of the band's LINK. */
#define BUSFAULT_BACK_MS 1000u

/** @brief Which hold or tap left the screen. */
typedef enum {
    BUSFAULT_EXIT_NONE = 0,    /**< none has completed since the last read */
    BUSFAULT_EXIT_SIMULATION,  /**< the acknowledgement, the link silent   */
    BUSFAULT_EXIT_RECONNECT,   /**< RECONNECT, the link answering          */
} busfault_exit_t;

/**
 * @brief Whether @p r is a start-up test on which no IOmcu answered: the
 *        SILENT verdict, and no answer to either status request, the one
 *        before the echo run (remote_seen) or the one after (have_remote).
 *
 * The screen then names no fault but the missing IOmcu, and a tap on its
 * button leaves it; RECONNECT, once the link answers, stays a hold.  Every
 * other verdict, and a silent echo run from an IOmcu that answered either
 * status request, is a wiring or firmware fault and keeps the hold.
 * @param r The report; NULL is false.
 * @return true for a bus on which nothing answered at all.
 */
bool busfault_no_iomcu(const busfault_report_t *r);

/**
 * @brief The report's heading in the language showing: "No IOmcu
 *        answered." for busfault_no_iomcu(), otherwise the verdict
 *        (busfault_verdict_text()).  For the splash's CAN line as well.
 * @param r The start-up test's report; NULL reads as running.
 * @return A static string.
 */
const char *busfault_headline(const busfault_report_t *r);

/**
 * @brief Hand the screen what to show.  The hold, the hint and the link's
 *        record start again: a new report is a new fault.
 * @param r The report, or NULL to clear it.
 */
void busfault_screen_set(const busfault_report_t *r);

/**
 * @brief Newer numbers for the report showing, every frame: the counters
 *        move on the glass instead of freezing.  The hold in progress, the
 *        hint and the link's record stay.
 *
 * A report of another kind than the one showing, or none showing, is
 * ignored: the start-up test's numbers are a verdict and do not move.
 * @param r The numbers, as link_lost_report() reads them.
 */
void busfault_screen_refresh(const busfault_report_t *r);

/**
 * @brief Whether the link answers, every frame the screen shows.
 *
 * RECONNECT is offered once @p up has held for BUSFAULT_BACK_MS, measured
 * on @p now_ms with a wrap-safe 32-bit difference.  A frame with @p up
 * false withdraws it at once and starts the count again.  The offer
 * arriving or going abandons a hold in progress: the control changed under
 * the finger, and the finger lifts before a new hold begins.
 * @param up     The band's LINK: the IOmcu answered the last poll.
 * @param now_ms The panel's clock, ms.
 */
void busfault_screen_link(bool up, uint32_t now_ms);

/** @brief Whether RECONNECT is offered.
 *  @return true once the link has answered for BUSFAULT_BACK_MS. */
bool busfault_screen_link_back(void);

/**
 * @brief The hold that completed, and clears when read.
 *
 * Neither exit changes the bench: no latch, STOP or fault code is cleared
 * here or by the application on the way out.  Where it leads is the
 * application's business; both lead to the menu.
 * @return The exit, BUSFAULT_EXIT_NONE when none completed.
 */
busfault_exit_t busfault_screen_take_exit(void);

/**
 * @brief What was handed in, for the application deciding whether to show it.
 *
 * @return the report
 */
const busfault_report_t *busfault_screen_report(void);

/**
 * @brief True once either hold has completed, and clears when read: the
 *        same latch busfault_screen_take_exit() reads.
 *
 * A latch rather than a jump: which screen an acknowledged fault leads to is
 * the application's business, not the screen's.
 * @return Whether a hold completed since the last read.
 */
bool busfault_screen_take_ack(void);

/** @brief Paint the whole screen again in both framebuffers at the next
 *         frames. */
void busfault_screen_invalidate(void);

/**
 * @brief The self-test's verdict as the screen heads it, in the language showing.
 * can_selftest_text() is the console's English.
 *
 * @param v the verdict
 * @return the text
 */
const char *busfault_verdict_text(can_selftest_verdict_t v);

/** @brief The screen, for the router.
 *  @return Its table of handlers. */
const ui_screen_t *busfault_screen(void);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_BUSFAULT_SCREEN_H */
