/**
 * @file
 * @brief The POWER page (0x43, protocol 4.41) of the remote servo node's
 *        IOmcu (board 3): the master rail switch, the 9 port switches, what
 *        cut them, and the BEC check.
 *
 * The registers are in link_pages_power.h.  The rules:
 *
 *   - RAIL 1 and a new PORT_WANT bit want a live heartbeat and a chip of
 *     the A4 stepping (LINK_NACK_NOT_ARMED); RAIL 1 wants every INA3221 of
 *     the board answering with its critical limits written
 *     (LINK_NACK_BUSY); a new PORT_WANT bit for a port whose output is
 *     bound as a throttle, and RAIL 1 with such a bit kept, are refused
 *     (LINK_NACK_BAD_VALUE): an ESC brings its own supply.  RAIL 0 and cleared bits are always taken; RAIL 0 keeps
 *     PORT_WANT.  A taken RAIL 1 clears TRIPPED.
 *   - PORT_ON: with the switches fitted, PORT_WANT while RAIL is 1; without
 *     them every bit follows RAIL, and PORT_WANT is kept and reported.
 *   - power_off(): RAIL, PORT_ON and PORT_WANT cleared, for a lost
 *     heartbeat, the link failsafe, a restart and a STOP.
 *   - An error trips: the INA3221s' critical alert line low, a critical
 *     alert flag (CF1 to CF3) in a Mask/Enable read that is new, a
 *     SENSE_LIMIT trip on channel 0 to 8 that is new, or a switch found in
 *     thermal shutdown.  A trip is power_off() with its TRIPPED bits set;
 *     the IOmcu makes the bench SAFE with it.
 *   - Thermal shutdown: a port on, read under half the rail's voltage on 2
 *     windows in a row, each closed wholly after the port came on.  Without
 *     the switches every port follows the rail, so the same check finds the
 *     master switch's shutdown.  A window without a reading of the port
 *     neither counts nor breaks the run.  Once a window wholly after the
 *     change has closed, every later one counts, however long the port
 *     stays on.  A new window series (the sensor schedule set up again,
 *     numbering from 0) counts from its first window and starts each run
 *     anew.
 *   - The BEC check: with RAIL and every port off, each window closed
 *     wholly after that sets port k's BEC bit at LINK_PW_BEC_MV or more and
 *     clears it under; a port without a reading keeps its bit.  With the
 *     rail or a port on the register keeps the last check.
 *
 * A window is the SENSE windows of 50ms (sense_sched.h): its mean bus
 * voltage per channel is one reading here.
 *
 * Pure logic, host-tested in test_power_page.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_POWER_PAGE_H
#define RCBENCH_POWER_PAGE_H

#include <stdbool.h>
#include <stdint.h>

#include "link_pages.h"
#include "out_bind.h"
#include "outputs.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The INA3221 parts behind the ports: 3 channels each. */
#define POWER_PARTS 3u

/** @brief Windows of a run below half the rail that trip: 2. */
#define POWER_THERMAL_N 2u

/** @brief The page's state. */
typedef struct {
    uint16_t fitted;     /**< FITTED: LINK_PW_PORT_ALL or 0               */
    bool     chip_ok;    /**< the chip is of the A4 stepping              */
    bool     rail;       /**< RAIL as driven                              */
    uint16_t want;       /**< PORT_WANT                                   */
    uint16_t tripped;    /**< TRIPPED                                     */
    uint16_t bec;        /**< BEC                                         */
    uint16_t on_prev;    /**< PORT_ON at the last tracking                */
    bool     have_win;   /**< last_win holds a window number              */
    uint16_t last_win;   /**< the newest window number seen               */
    uint16_t on_win[LINK_PW_PORTS]; /**< last_win when port k came on    */
    uint16_t settled;    /**< bit k: a window wholly after port k came on
                              has closed; every later one counts         */
    uint32_t series;     /**< the window series last_win belongs to       */
    uint8_t  low_n[LINK_PW_PORTS];  /**< windows in a row under half      */
    bool     quiet;      /**< RAIL and every port off                     */
    bool     quiet_any;  /**< quiet since the start, or a window wholly
                              after quiet began has closed: every window
                              counts                                     */
    uint16_t quiet_win;  /**< last_win when quiet began                   */
    bool     have_over;  /**< over_n holds the counts last seen           */
    uint16_t over_n[LINK_PW_PORTS]; /**< SENSE_LIMIT trips per channel    */
    bool     have_me[POWER_PARTS];  /**< me_n[a] holds a count            */
    uint16_t me_n[POWER_PARTS];     /**< Mask/Enable reads last seen      */
} power_page_t;

/** @brief What a write is judged against, besides the page. */
typedef struct {
    bool     beat_alive;   /**< the heartbeat is trusted, the link speaks  */
    bool     limits_held;  /**< every INA3221 answers with its critical
                                limits written                            */
    uint16_t throttle;     /**< bit k: port k's output is bound as a
                                throttle                                  */
} power_ctx_t;

/** @brief What one pass reads besides the page. */
typedef struct {
    bool     crit_n_low;   /**< the critical alert line reads low         */
    /** @brief Each part's Mask/Enable as last read; me_ok bit a: me[a]
     *         holds a read, me_n[a] counts the reads. */
    uint16_t me[POWER_PARTS];
    uint16_t me_n[POWER_PARTS];  /**< Mask/Enable reads, modulo 65536     */
    uint8_t  me_ok;              /**< bit a: part a has been read          */
    /** @brief Each channel's windows over its SENSE_LIMIT, modulo 65536. */
    uint16_t over_n[LINK_PW_PORTS];
} power_in_t;

/**
 * @brief The page at the start: everything off, nothing tripped.
 *
 * @param p        The state, overwritten; nothing is done for NULL.
 * @param switches The carrier has the port switches (SW_FITTED).
 * @param chip_ok  The chip is of the A4 stepping (chip_gate_ok()).
 */
void power_init(power_page_t *p, bool switches, bool chip_ok);

/**
 * @brief PORT_ON as driven.
 *
 * @param p The state.
 * @return  Bit k: port k on; 0 for NULL.
 */
uint16_t power_port_on(const power_page_t *p);

/**
 * @brief Whether the master rail switch is driven on.
 *
 * @param p The state.
 * @return  RAIL; false for NULL.
 */
bool power_rail(const power_page_t *p);

/**
 * @brief The panel's write.
 *
 * @param p    The state.
 * @param off  First register.
 * @param n    Registers.
 * @param in   The values.
 * @param ctx  The heartbeat, the limits and the throttle ports.
 * @return 0 when taken; LINK_NACK_BAD_RANGE for no register, a range past
 *         the page or a NULL argument; LINK_NACK_READ_ONLY for FITTED,
 *         PORT_ON, TRIPPED or BEC; LINK_NACK_BAD_VALUE for RAIL over 1, a
 *         PORT_WANT bit past port 9, a new bit on a throttle port, or
 *         RAIL 1 with a throttle port's bit kept;
 *         LINK_NACK_NOT_ARMED for RAIL 1 or a new PORT_WANT bit without a
 *         live heartbeat or on a chip before A4; LINK_NACK_BUSY for RAIL 1
 *         before the critical limits are held.  A refused write changes
 *         nothing.
 */
uint8_t power_write(power_page_t *p, uint8_t off, uint8_t n,
                    const uint16_t *in, const power_ctx_t *ctx);

/**
 * @brief The page's registers.
 *
 * @param p    The state.
 * @param off  First register.
 * @param n    Registers; past the page reads 0.
 * @param out  The values.
 */
void power_read(const power_page_t *p, uint8_t off, uint8_t n,
                uint16_t *out);

/**
 * @brief RAIL, PORT_ON and PORT_WANT off: a lost heartbeat, the link
 *        failsafe, a STOP.  TRIPPED stays.
 *
 * @param p The state; nothing for NULL.
 */
void power_off(power_page_t *p);

/**
 * @brief An error: power_off() and @p bits into TRIPPED.
 *
 * @param p    The state; nothing for NULL.
 * @param bits LINK_PW_TRIP_* bits.
 */
void power_trip(power_page_t *p, uint16_t bits);

/**
 * @brief One pass: the critical alert line, the Mask/Enable reads and the
 *        SENSE_LIMIT counts judged; a trip for each error found.
 *
 * The first counts seen are taken as they are: only a change after them
 * trips.
 *
 * @param p   The state.
 * @param in  What the pass read.
 * @return    The LINK_PW_TRIP_* bits this pass tripped; 0 for none or NULL.
 */
uint16_t power_step(power_page_t *p, const power_in_t *in);

/**
 * @brief A window closed: the thermal-shutdown check and the BEC check.
 *
 * @param p        The state.
 * @param series   Which window series @p number is of: a new value is the
 *                 sensor schedule set up again (sense_snap_t.setup).
 * @param number   The window's number, modulo 65536; the same number again
 *                 in the same series is not a new window and does
 *                 nothing.
 * @param mv       LINK_PW_PORTS mean bus voltages, mV, port 1 first.
 * @param have     Bit k: mv[k] is a reading.
 * @param rail_mv  The rail's voltage as the PD mini reads it, mV; 0 when
 *                 not known, which leaves the thermal check out.
 * @return LINK_PW_TRIP_THERMAL when the check tripped; 0 otherwise.
 */
uint16_t power_window(power_page_t *p, uint32_t series, uint16_t number,
                      const int32_t *mv,
                      uint16_t have, uint16_t rail_mv);

/**
 * @brief The ports whose output is bound as a throttle: a slot with a
 *        driver on the port's pin renders a channel whose role is
 *        OUT_ROLE_THROTTLE.
 *
 * @param o  The bank.
 * @param r  The board's roles: port k is r->out[k].
 * @return   Bit k for port k; 0 for NULL.
 */
uint16_t power_throttle(const outputs_t *o, const outbind_roles_t *r);

/**
 * @brief The STATUS fault bits TRIPPED stands for: LINK_FAULT_OVERCURRENT
 *        for a critical alert or a SENSE_LIMIT trip, LINK_FAULT_OVERTEMP
 *        for a thermal shutdown.
 *
 * @param p The state.
 * @return  link_fault_t bits; 0 for NULL.
 */
uint16_t power_faults(const power_page_t *p);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_POWER_PAGE_H */
