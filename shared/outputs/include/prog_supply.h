/**
 * @file
 * @brief The PROG_SUPPLY page: the programming ports' own supply on a board
 *        that switches one with a pin (the rbmod carrier's GPIO 21, the
 *        enable of its 5V buck M7).
 *
 * The page follows the SUPPLY page's rules for a programming port
 * (supply_page.h), for a supply that is a pin and not a module on a UART:
 * the panel's ON wants a live heartbeat and no port holding the supply; the
 * panel's OFF is always taken and, while a port writes to the servo's
 * memory, held back from the pin until the write ends,
 * SUPPLY_PAGE_WRITE_WAIT_MS at most; after that OFF the port's own ON is
 * refused until the port lets go.  A lost heartbeat or a silent link
 * switches it off at once, whoever holds it.  An OFF that finds a port's
 * frame on the wire waits for the frame's end, SUPPLY_PAGE_OFF_WAIT_MS at
 * most.  The supply gives a fixed voltage: a port's ON names none.  A
 * port gets the supply only after the panel has written PANEL since the
 * heartbeat last went: a panel before 4.32, which knows nothing of this
 * supply, never lets a port switch it on.
 *
 * Pure C: prog_supply_step() says what the pin is to drive, and the IOmcu
 * drives it.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_PROG_SUPPLY_H
#define RCBENCH_PROG_SUPPLY_H

#include <stdbool.h>
#include <stdint.h>

#include "link_pages.h"
#include "supply_page.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The voltage the rbmod carrier's programming-port buck gives, mV. */
#define PROG_SUPPLY_CARRIER_MV 5000u

/** @brief The page's state. */
typedef struct {
    uint8_t  pin;        /**< the switching GPIO, LINK_PS_NO_PIN for none  */
    uint16_t mv;         /**< the voltage it gives, mV                     */
    bool     output;     /**< OUTPUT as asked                              */
    bool     known;      /**< PANEL: the panel knows the page, written
                              since the heartbeat last went               */
    bool     drive;      /**< what the pin drives after the last step      */
    bool     was_on;     /**< asked on at the last step                    */
    bool     held;       /**< a programming port holds the supply          */
    bool     writing;    /**< a port writes to the servo's memory          */
    bool     off_held;   /**< the panel's OFF waits for the write          */
    bool     sticky;     /**< the panel's OFF stands: no port ON           */
    bool     frame_out;  /**< a port has a frame on the wire               */
    bool     off_wait;   /**< an OFF waits for that frame's end            */
    uint32_t now_ms;     /**< the clock of prog_supply_port_frame()        */
    uint32_t held_t0;    /**< when the held OFF was written                */
    uint32_t off_t0;     /**< when the waiting OFF was asked               */
} prog_supply_t;

/**
 * @brief The page at its start: off, on @p pin.
 * @param p   the state, overwritten; nothing is done when NULL
 * @param pin the switching GPIO, LINK_PS_NO_PIN on a board without one
 * @param mv  the voltage it gives, mV; ignored without a pin
 */
void prog_supply_init(prog_supply_t *p, uint8_t pin, uint16_t mv);

/**
 * @brief Whether the board has the switch.
 * @param p the state
 * @return true with a pin; false for NULL
 */
bool prog_supply_fitted(const prog_supply_t *p);

/**
 * @brief The panel's write.
 * @param p          the state
 * @param off        first register
 * @param n          registers
 * @param in         the values
 * @param beat_alive whether the panel's heartbeat is live
 * @return 0 when taken; LINK_NACK_BAD_RANGE past the page or for NULL,
 *         LINK_NACK_READ_ONLY for FLAGS to MV, LINK_NACK_BAD_VALUE for an
 *         OUTPUT over 1, an ON without the switch or under a port's hold,
 *         a PANEL other than 1, LINK_NACK_NOT_ARMED for an ON or a PANEL
 *         without a live heartbeat
 */
uint8_t prog_supply_write(prog_supply_t *p, uint8_t off, uint8_t n,
                          const uint16_t *in, bool beat_alive);

/**
 * @brief The page's registers.
 * @param p   the state
 * @param off first register
 * @param n   registers; past the page reads 0
 * @param out the values
 */
void prog_supply_read(const prog_supply_t *p, uint8_t off, uint8_t n,
                      uint16_t *out);

/**
 * @brief A programming port's request, in place of the panel's OUTPUT.
 *
 * OFF is always taken and ends a held OFF.  ON is refused without the
 * switch, without a live heartbeat, before the panel has written PANEL,
 * and from the panel's OFF of a held supply until the port lets go
 * (prog_supply_hold()).
 *
 * @param p          the state
 * @param on         true for ON
 * @param beat_alive whether the panel's heartbeat is live
 * @return true when taken
 */
bool prog_supply_port_set(prog_supply_t *p, bool on, bool beat_alive);

/**
 * @brief The supply as a programming port sees it, in the SUPPLY page's
 *        shape: online with the switch once the panel has written PANEL,
 *        on as driven, set_mv the fixed voltage; no input read.
 * @param p   the state
 * @param out zeroed, then filled; nothing for NULL
 */
void prog_supply_port_view(const prog_supply_t *p, supply_port_view_t *out);

/**
 * @brief Whether a programming port holds the supply.  Letting go ends the
 *        panel's standing OFF.
 * @param p    the state
 * @param held true while a port holds it
 */
void prog_supply_hold(prog_supply_t *p, bool held);

/**
 * @brief Whether a port writes to the servo's memory now.
 * @param p       the state
 * @param writing true while a write runs
 */
void prog_supply_port_writes(prog_supply_t *p, bool writing);

/**
 * @brief Whether a port has a frame on the wire, and the clock.
 * @param p         the state
 * @param frame_out true while a frame is out
 * @param now_ms    the time, ms
 */
void prog_supply_port_frame(prog_supply_t *p, bool frame_out,
                            uint32_t now_ms);

/**
 * @brief One pass: off and PANEL cleared without a heartbeat, the held OFF
 *        and the frame wait judged, and what the pin is to drive.
 * @param p          the state
 * @param beat_alive whether the panel's heartbeat is live and the link
 *                   speaks
 * @return true when the pin is to switch the supply on; false for NULL
 */
bool prog_supply_step(prog_supply_t *p, bool beat_alive);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_PROG_SUPPLY_H */
