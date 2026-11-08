/**
 * @file
 * @brief The PROG 5V watch: when the programming ports' own 5V does not
 *        come after an ON, or collapses under it.
 *
 * On the rbmod carrier the programming ports run from the buck module M7,
 * enabled by GPIO 21 through the PROG_SUPPLY page (prog_supply_link.h).
 * M7 makes 5V from the +15V net, the carrier's USB-C input through the PD
 * (Power Delivery) trigger M6.  The page reads the pin; the INA3221
 * channel that watches the ports' 5V reads what M7 makes.
 *
 * The watch counts the channel's 50ms windows, each once, by the number the
 * SENSE_WIN page carries, by their mean bus voltage.  It runs on every ON
 * the page reads, the panel's own and a programming port's, and finds two
 * things:
 *
 * - MISSING: no window of PROG5V_MIN_MV (4750mV) or more by
 *   PROG5V_REACH_MS after the ON.  The first new window read at or after
 *   that time decides.  An input supply of 5V on the +15V net leaves M7
 *   under 5V.
 * - LOST: once the 5V has read PROG5V_MIN_MV or more, two new windows in a
 *   row under PROG5V_LOST_MV (3000mV), both read PROG5V_LOST_SETTLE_MS or
 *   more after the ON or after the latest change of the cuts.  M7's controller, most likely a
 *   JoulWatt JW5068A, latches its output off on an over-voltage (over 6.0V
 *   for 20us) or an under-voltage (under 3.25V for 200us), and starts
 *   again only when EN is toggled; a latched output falls to near 0V.  The
 *   known cause on the carrier is the over-voltage: a servo on a
 *   programming port that brakes feeds energy back into the 5V, which M7
 *   cannot sink.  The page then still reads the pin on.
 *
 * Why the numbers:
 *
 * - PROG5V_MIN_MV, 4750mV: 5V less 5%, the lower bound of USB's 5V
 *   (VBUS) range.  KST and Chaservo servos are programmed at 5V.  Only an
 *   ON's start is held to it: under a servo's load the shunt and M7's load
 *   regulation pull the bus reading near 4750mV, and a run is not stopped
 *   for that.
 * - PROG5V_LOST_MV, 3000mV: under M7's under-voltage latch of 3.25V, so a
 *   window under it is a supply that is not regulating.
 * - PROG5V_REACH_MS, 200ms: PROG_SUPPLY_LINK_READ_MS, the page's read
 *   period.  A window is 50ms and the panel reads one every 40ms
 *   (SENSE_WIN_LINK_READ_MS), so at least two whole windows closed after
 *   the ON are read in it.
 * - Two windows for LOST: one window of a heavy load's sag does not trip.
 * - PROG5V_LOST_SETTLE_MS after the ON: the converter's start and a read of
 *   the page that is up to PROG_SUPPLY_LINK_READ_MS (200ms) old are past.
 *
 * A programming port's session switches the 5V off and on itself (a KST
 * rail cycle of 1000ms off, a classic Hitec entry of 500ms off), and the
 * panel sees the pin only at its page reads.  While a port holds the 5V a
 * finding waits for a page read made after it that still reads the pin on;
 * a read that shows the pin off ends the ON.  A window that is not low
 * (PROG5V_MIN_MV or more for MISSING, PROG5V_LOST_MV or more for LOST)
 * drops a finding that waits.  The panel's own OFF moves the PROG_SUPPLY
 * cuts (prog_supply_link_cuts()); the page reads the pin on until its next
 * read, so a change of the cuts starts the settle time again, drops the
 * windows counted and a finding that waits, and holds MISSING back until
 * the settle time has passed as well.  An invalid reading drops them too.
 *
 * One trip an ON: none again until the caller's ON has gone false and come
 * back.  What the trip does is the caller's: the panel's error path
 * (bench_error_stop()), which writes the page's OFF.  That OFF toggles EN
 * at the next ON and so clears M7's latch.
 *
 * Without a channel watching the ports' 5V, or on a board without the
 * PROG_SUPPLY switch, the caller passes no reading or no ON, and nothing
 * trips.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 * Control task only.  Host-tested in test_prog5v_lost.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_PROG5V_LOST_H
#define RCBENCH_PROG5V_LOST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The 5V an ON must reach, mV: 5V less 5%. */
#define PROG5V_MIN_MV         4750u
/** @brief Once reached, a window's mean bus voltage under this is low, mV:
 *         under M7's under-voltage latch of 3.25V. */
#define PROG5V_LOST_MV        3000u
/** @brief The time after the ON by which a window must read PROG5V_MIN_MV
 *         or more, ms: the PROG_SUPPLY page's read period. */
#define PROG5V_REACH_MS       200u
/** @brief Low windows in a row that make a LOST trip. */
#define PROG5V_LOST_WINDOWS   2u
/** @brief A window counts for LOST only when read this long after the ON
 *         or after the latest change of the cuts, ms. */
#define PROG5V_LOST_SETTLE_MS 500u

/** @brief The channel's last window, as the SENSE_WIN page was read. */
typedef struct {
    bool     ok;      /**< a closed window with current and voltage
                           samples of the channel                     */
    uint16_t mv;      /**< its mean bus voltage, mV                   */
    uint16_t window;  /**< its number, the page's WINDOW register     */
} prog5v_window_t;

/** @brief What a trip found. */
typedef enum {
    PROG5V_TRIP_NONE = 0, /**< none                                          */
    PROG5V_TRIP_MISSING,  /**< no window at PROG5V_MIN_MV by PROG5V_REACH_MS */
    PROG5V_TRIP_LOST,     /**< two low windows after the 5V had come         */
} prog5v_trip_t;

/** @brief The ON as the panel's control task reads it, once a pass. */
typedef struct {
    bool     on;     /**< the page reads the pin on or acknowledged the
                          panel's ON, and the heartbeat edges           */
    bool     port;   /**< a programming port holds the 5V              */
    uint32_t cuts;   /**< prog_supply_link_cuts()                       */
    uint32_t reads;  /**< prog_supply_link_on_reads(): answered page
                          reads that showed the pin on, counted         */
} prog5v_on_t;

/** @brief The watch's state; a zeroed one is the start. */
typedef struct {
    bool     on;       /**< the ON as last stepped                    */
    bool     settled;  /**< PROG5V_LOST_SETTLE_MS passed since on_ms  */
    bool     reached;  /**< a window of this ON read PROG5V_MIN_MV or
                            more                                    */
    bool     tripped;  /**< this ON has tripped                       */
    bool     cut;      /**< the cuts moved under this ON: MISSING waits
                            for the settle time too                 */
    bool     seen;    /**< window holds a window number read         */
    bool     waits;    /**< a finding waits for a page read (port)    */
    uint8_t  kind;     /**< prog5v_trip_t of the trip, or of the
                            finding that waits                      */
    uint8_t  low;      /**< low windows in a row                      */
    uint16_t window;   /**< the last window number read               */
    uint16_t mv[PROG5V_LOST_WINDOWS]; /**< the low windows' voltages, mV;
                            MISSING's window in mv[0]               */
    uint32_t cuts;     /**< the cuts at on_ms                         */
    uint32_t wait_reads; /**< the page's reads at the finding         */
    uint32_t on_ms;    /**< the ON, or the latest change of the cuts  */
    uint32_t start_ms; /**< the ON, for PROG5V_REACH_MS               */
    uint32_t trip_ms;  /**< the finding's time after on_ms (LOST) or
                            start_ms (MISSING), ms                  */
} prog5v_lost_t;

/**
 * @brief Start over: no ON, no window read.
 * @param d the state; nothing for NULL
 */
void prog5v_lost_init(prog5v_lost_t *d);

/**
 * @brief One pass of the control task.
 * @param d      the state
 * @param on     the ON as the page reads it; NULL for none
 * @param now_ms the time, ms
 * @param w      the channel's last window; NULL or !ok for none
 * @return true once, at the trip
 */
bool prog5v_lost_step(prog5v_lost_t *d, const prog5v_on_t *on,
                      uint32_t now_ms, const prog5v_window_t *w);

/**
 * @brief What the trip found.
 * @param d the state
 * @return PROG5V_TRIP_MISSING or PROG5V_TRIP_LOST while the ON that
 *         tripped stands; PROG5V_TRIP_NONE for NULL and otherwise
 */
prog5v_trip_t prog5v_lost_kind(const prog5v_lost_t *d);

/**
 * @brief The voltage of the window that decided the trip, mV.
 * @param d the state
 * @return its mean bus voltage while the ON that tripped stands; 0 for
 *         NULL and otherwise
 */
uint16_t prog5v_lost_mv(const prog5v_lost_t *d);

/**
 * @brief The console line of a trip, English:
 *        `PROG 5V lost: INA3221 0x40 CH3 read 0.12V, 0.08V in 2 windows,
 *        640ms after ON. All outputs off.` or `PROG 5V missing: INA3221
 *        0x40 CH3 read 3.10V 200ms after ON. All outputs off.`
 * @param d   the state
 * @param ch  the channel, 0 to 11
 * @param buf where it goes
 * @param n   the size of @p buf, bytes; 112 holds every line
 * @return @p buf; "" for NULL, no buffer or @p ch past 11
 */
const char *prog5v_lost_line(const prog5v_lost_t *d, unsigned ch, char *buf,
                             size_t n);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_PROG5V_LOST_H */
