/**
 * @file
 * @brief The PROG 5V watch: when the programming ports' own 5V, switched on
 *        from the panel, collapses under it.
 *
 * On the rbmod carrier the programming ports run from the buck module M7,
 * enabled by GPIO 21 through the PROG_SUPPLY page (prog_supply_link.h).
 * Its controller, most likely a JoulWatt JW5068A, latches its output off on
 * an over-voltage (over 6.0V for 20us) or an under-voltage (under 3.25V for
 * 200us), and starts again only when EN is toggled.  A servo on a
 * programming port that moves fast can latch it.  The page then still reads
 * the pin on; the INA3221 channel that watches the ports' 5V reads the
 * collapse.
 *
 * The watch counts the channel's 50ms windows, each once, by the number the
 * SENSE_WIN page carries.  Two new windows in a row with a mean bus voltage
 * under PROG5V_LOST_MV, both read PROG5V_LOST_SETTLE_MS or more after the
 * ON, are a trip:
 *
 * - Under 3.25V for 200us the converter has latched by its datasheet, and a
 *   latched output falls to near 0V.  3000mV is under that threshold, so a
 *   window under it is a supply that is not regulating.
 * - Two windows: one window of a heavy load's sag does not trip.
 * - PROG5V_LOST_SETTLE_MS after the ON: the converter's start and a read of
 *   the page that is up to PROG_SUPPLY_LINK_READ_MS (200ms) old are past.
 *
 * One trip an ON: none again until the caller's ON has gone false and come
 * back.  An OFF asked, by an error or by the operator, moves the
 * PROG_SUPPLY cuts (prog_supply_link_cuts()); the page reads the pin on
 * until its next read, so a change of the cuts starts the settle time again
 * and drops the windows counted.  An invalid reading drops them as well.
 *
 * What the trip does is the caller's: the panel's error path
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

/** @brief A window's mean bus voltage under this is low, mV. */
#define PROG5V_LOST_MV        3000u
/** @brief Low windows in a row that make a trip. */
#define PROG5V_LOST_WINDOWS   2u
/** @brief A window counts only when read this long after the ON or after
 *         the latest change of the cuts, ms. */
#define PROG5V_LOST_SETTLE_MS 500u

/** @brief The channel's last window, as the SENSE_WIN page was read. */
typedef struct {
    bool     ok;      /**< a closed window with current and voltage
                           samples of the channel                     */
    uint16_t mv;      /**< its mean bus voltage, mV                   */
    uint16_t window;  /**< its number, the page's WINDOW register     */
} prog5v_window_t;

/** @brief The watch's state; a zeroed one is the start. */
typedef struct {
    bool     on;       /**< the ON as last stepped                    */
    bool     settled;  /**< PROG5V_LOST_SETTLE_MS passed since on_ms  */
    bool     tripped;  /**< this ON has tripped                       */
    bool     seen;     /**< window holds a window number read         */
    uint8_t  low;      /**< low windows in a row                      */
    uint16_t window;   /**< the last window number read               */
    uint16_t mv[PROG5V_LOST_WINDOWS]; /**< the low windows' voltages, mV */
    uint32_t cuts;     /**< the cuts at on_ms                         */
    uint32_t on_ms;    /**< the ON, or the latest change of the cuts  */
    uint32_t trip_ms;  /**< the trip's time after on_ms, ms           */
} prog5v_lost_t;

/**
 * @brief Start over: no ON, no window read.
 * @param d the state; nothing for NULL
 */
void prog5v_lost_init(prog5v_lost_t *d);

/**
 * @brief One pass of the control task.
 * @param d      the state
 * @param on     the panel's ON of the ports' 5V stands: the page reads it on
 *               or acknowledged it, no programming port holds it, and the
 *               heartbeat edges
 * @param cuts   prog_supply_link_cuts()
 * @param now_ms the time, ms
 * @param w      the channel's last window; NULL or !ok for none
 * @return true once, at the trip
 */
bool prog5v_lost_step(prog5v_lost_t *d, bool on, uint32_t cuts,
                      uint32_t now_ms, const prog5v_window_t *w);

/**
 * @brief The voltage of the window that tripped, mV.
 * @param d the state
 * @return the second low window's mean bus voltage while the ON that
 *         tripped stands; 0 for NULL and otherwise
 */
uint16_t prog5v_lost_mv(const prog5v_lost_t *d);

/**
 * @brief The console line of a trip, English:
 *        `PROG 5V lost: INA3221 0x40 CH3 read 0.12V, 0.08V in 2 windows,
 *        640ms after ON. All outputs off.`
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
