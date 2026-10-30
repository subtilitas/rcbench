/**
 * @file
 * @brief The panel's end of the PROG_SUPPLY page (link 4.32): the
 *        programming ports' own supply on a board that switches one, the
 *        rbmod carrier's GPIO 21.
 *
 * The KST and Hitec programmers switch this supply for a session, on the
 * IOmcu.  The panel switches it too, from the PROG 5V box (prog5v_box.h):
 * an ON the operator held for 2s, an OFF tapped.  The panel reads the page,
 * to show the supply's state, and writes its OFF on every error
 * (error_sites.h), whatever it reads of the output; the IOmcu holds that
 * OFF for a port's write, SUPPLY_PAGE_WRITE_WAIT_MS at most.  An OFF that
 * gets no answer is written again at the next pass; one the IOmcu answers,
 * ACK or NACK, is done.  An ON is written once, after any OFF owed: an
 * error or an OFF drops one not yet written, and one the IOmcu does not
 * answer is not written again.  The panel writes PANEL 1 whenever a read finds it
 * 0 on a board with the switch: the IOmcu lets a programming port switch
 * the supply on only after that write, and clears it when the heartbeat
 * goes.
 *
 * Pure C, no ESP-IDF.  Control task only.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_PROG_SUPPLY_LINK_H
#define RCBENCH_PROG_SUPPLY_LINK_H

#include <stdbool.h>
#include <stdint.h>

#include "link_pages.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief How often the page is read, ms. */
#define PROG_SUPPLY_LINK_READ_MS 200u

/** @brief A write the IOmcu did not answer. */
#define PROG_SUPPLY_LINK_NO_ANSWER (-1)
/** @brief A write the IOmcu acknowledged. */
#define PROG_SUPPLY_LINK_ACK       0
/** @brief No ON written since the start, or since its outcome was taken. */
#define PROG_SUPPLY_LINK_NONE      (-2)

/** @brief The supply as SUPPLY shows it. */
typedef enum {
    PROG_SUPPLY_NONE = 0, /**< no page, or a board without the switch */
    PROG_SUPPLY_UNKNOWN,  /**< fitted, the last read went unanswered  */
    PROG_SUPPLY_OFF,      /**< fitted and off                         */
    PROG_SUPPLY_ON,       /**< fitted and on                          */
} prog_supply_state_t;

/** @brief The panel's end of the page. */
typedef struct {
    bool     page;     /**< the IOmcu serves the page (minor 4.32 on)   */
    bool     fitted;   /**< a read said the board has the switch        */
    bool     answered; /**< the last read was answered                  */
    bool     on;       /**< the last answered read: the pin drives on   */
    uint8_t  pin;      /**< the switching GPIO, as read                 */
    bool     owed;     /**< an OFF waits to be written                  */
    bool     on_owed;  /**< the panel's ON waits to be written          */
    int      on_result;/**< how the last ON went: PROG_SUPPLY_LINK_ACK,
                            a NACK reason, PROG_SUPPLY_LINK_NO_ANSWER;
                            PROG_SUPPLY_LINK_NONE before one            */
    bool     port;     /**< the last answered read: a port holds it     */
    bool     declared; /**< PANEL reads 1, or its write was answered
                            since the last read                         */
    bool     read_once;/**< a read was made since the start            */
    uint32_t read_ms;  /**< when the last read was made                 */
} prog_supply_link_t;

/**
 * @brief Start over for an IOmcu that serves the page or not; nothing owed.
 * @param l    the state; nothing for NULL
 * @param page the IOmcu's minor is LINK_MINOR_PROG_SUPPLY or later
 */
void prog_supply_link_init(prog_supply_link_t *l, bool page);

/**
 * @brief An error: owe the page's OFF, whatever the panel reads.
 * @param l the state; nothing for NULL or without the page
 */
void prog_supply_link_cut(prog_supply_link_t *l);

/**
 * @brief The operator's ON: owe OUTPUT 1, written after any OFF owed.
 *
 * Refused here, nothing owed: no page, no switch as last read, the last
 * read unanswered, a port holding the supply as last read, or an OFF owed.
 * The IOmcu judges it again: NOT_ARMED without a live heartbeat,
 * BAD_VALUE while a port holds the supply.
 *
 * @param l the state
 * @return true when the ON is owed
 */
bool prog_supply_link_ask_on(prog_supply_link_t *l);

/**
 * @brief The operator's OFF: owe OUTPUT 0 and drop an ON not yet written,
 *        as an error does (prog_supply_link_cut()).
 * @param l the state; nothing for NULL or without the page
 */
void prog_supply_link_ask_off(prog_supply_link_t *l);

/**
 * @brief How the last ON went, once: PROG_SUPPLY_LINK_ACK, a NACK reason or
 *        PROG_SUPPLY_LINK_NO_ANSWER; PROG_SUPPLY_LINK_NONE when there is
 *        none to tell.  The outcome is forgotten once taken.
 * @param l the state
 * @return the outcome; PROG_SUPPLY_LINK_NONE for NULL
 */
int prog_supply_link_take_on_result(prog_supply_link_t *l);

/**
 * @brief The write owed, if any: OUTPUT 0 first, then the operator's
 *        OUTPUT 1, then PANEL 1 while a read found PANEL 0 on a board with
 *        the switch.
 * @param l   the state
 * @param off receives the register, LINK_PS_OUTPUT or LINK_PS_PANEL
 * @param v   receives the value, 0 or 1
 * @return true when a write is owed
 */
bool prog_supply_link_next(const prog_supply_link_t *l, uint8_t *off,
                           uint16_t *v);

/**
 * @brief How that write went.  For an OFF or PANEL: PROG_SUPPLY_LINK_ACK or
 *        a NACK reason ends it until the next read,
 *        PROG_SUPPLY_LINK_NO_ANSWER keeps it owed.  For an ON every outcome
 *        ends it and is kept for prog_supply_link_take_on_result(); an ACK
 *        reads the output on until the next read says otherwise.
 * @param l      the state
 * @param result the outcome
 */
void prog_supply_link_written(prog_supply_link_t *l, int result);

/**
 * @brief Whether a read is due: the page served, and none yet or
 *        PROG_SUPPLY_LINK_READ_MS since the last, across the clock's wrap.
 * @param l      the state
 * @param now_ms the time, ms
 * @return true when due
 */
bool prog_supply_link_read_due(const prog_supply_link_t *l, uint32_t now_ms);

/**
 * @brief A read of registers 0 to LINK_PS_COUNT - 1, or NULL for none
 *        answered.
 * @param l      the state
 * @param regs   the registers, or NULL
 * @param now_ms the time, ms
 */
void prog_supply_link_read(prog_supply_link_t *l, const uint16_t *regs,
                           uint32_t now_ms);

/**
 * @brief The supply as SUPPLY shows it.
 * @param l the state
 * @return PROG_SUPPLY_NONE for NULL, no page or no switch
 */
prog_supply_state_t prog_supply_link_state(const prog_supply_link_t *l);

/**
 * @brief Whether a programming port holds the supply, as last read
 *        (FLAGS bit 2).
 * @param l the state
 * @return false for NULL, without the page or the switch, or after an
 *         unanswered read
 */
bool prog_supply_link_port(const prog_supply_link_t *l);

/**
 * @brief Whether the ports' own supply is the programmers' supply: the
 *        page served and the switch fitted, as read or, before the first
 *        read, as the board says.  The PD mini is then not the ports'
 *        supply, and the programmers' feed carries no reading of it.
 * @param l         the state
 * @param board_has the board the IOmcu reports switches the ports' supply
 * @return false for NULL or without the page
 */
bool prog_supply_link_owns(const prog_supply_link_t *l, bool board_has);

/**
 * @brief Whether the ports' own supply counts as live for the gates that
 *        wait for a bench with every supply off (a firmware install, a
 *        flash write): owned, and on as last read, not read since the
 *        start or since the last unanswered read, or a programmer holding
 *        a port, which switches it on.
 * @param l          the state
 * @param board_has  as for prog_supply_link_owns()
 * @param port_taken a programmer session holds a port
 * @return false for NULL, without the page or on a board without the switch
 */
bool prog_supply_link_live(const prog_supply_link_t *l, bool board_has,
                           bool port_taken);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_PROG_SUPPLY_LINK_H */
