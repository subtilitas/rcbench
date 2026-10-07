/*
 * The panel's half of the SUPPLY link page (0x2A, protocol 4.3 to 4.5): what is
 * written to it, in what order, and what a read of it means for the supply
 * the screen shows.
 *
 * The coprocessor drives the PD mini; the panel asks.  Two things are
 * written: the wiring (ENABLE to BAUD, from SETUP INTERFACES) and the
 * command (OUTPUT to SET_MA, from the SUPPLY screen).  The order is fixed:
 *
 *   1. an OFF, alone in OUTPUT, before anything else owed, and first of
 *      all while what the page holds is not known;
 *   2. the wiring, only with OUTPUT 0 on the page and FLAGS bit 6 clear,
 *      as the page refuses it otherwise;
 *   3. the command: ON and the set points in one frame.
 *
 * A refused wiring is not written again until the settings change.  A
 * wiring acknowledged is taken as the page's only once a read shows the
 * page holding it: a 4.5 coprocessor holds a change while a module has
 * answered until a state read of it shows the output off (FLAGS bit 8),
 * and refuses it when that read shows it on or fails (bit 9), which is
 * taken as a refusal and said as one (SUPPLY_LINK_EV_WIRING_LIVE).
 * Nothing else is written while it waits.  A read that shows the page
 * holding other than what was written -- a coprocessor that restarted
 * between two polls, or one that switched the output off when the
 * heartbeat stopped -- makes everything owed again, and an ON the page no
 * longer holds is lost.
 *
 * Pure C, no link of its own: the caller makes each exchange and reports
 * how it went.  Control task only.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "link_pages.h"
#include "supply.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The PD mini's own range, as the screen's caps: 1 to 20 V and 0.05 to
 *  3 A from the vendor's page, in 10 mV and 10 mA steps. */
#define SUPPLY_CAPS_PDMINI { 1.0f, 20.0f, 0.01f, 0.05f, 3.0f, 0.01f }

/** A reading older than this is not the supply answering: longer than one
 *  exchange's 1000 ms timeout, so one lost frame is not a supply gone, and
 *  about the 3 failed transactions the coprocessor's driver takes. */
#define SUPPLY_LINK_STALE_MS 1500u
/** How often the page is read while the supply is in use. */
#define SUPPLY_LINK_READ_MS  100u

/** The baud setting that asks the coprocessor to find the rate (4.4). */
#define SUPPLY_LINK_BAUD_AUTO 7u

/** Where the PD mini is wired, as SETUP INTERFACES has it. */
typedef struct {
    bool    en;
    int8_t  tx, rx;   /**< coprocessor GPIO; -1 not set          */
    uint8_t baud;     /**< the module's UART Baudrate, 0 to 6, 7 AUTO */
} supply_wiring_t;

/** What came of a write or a read: ACK or DATA, a NACK's reason, or none. */
#define SUPPLY_LINK_NO_ANSWER (-1)
#define SUPPLY_LINK_ACK       0

/** Things the caller tells the operator about, each once. */
enum {
    SUPPLY_LINK_EV_WIRING_REFUSED = 0x01,  /**< the pins or baud refused  */
    SUPPLY_LINK_EV_ON_REFUSED     = 0x02,  /**< an ON refused: no output  */
    SUPPLY_LINK_EV_ON_LOST        = 0x04,  /**< the page let an ON go     */
    SUPPLY_LINK_EV_STUCK          = 0x08,  /**< would not switch          */
    SUPPLY_LINK_EV_SET_STUCK      = 0x10,  /**< set points would not take */
    SUPPLY_LINK_EV_TRIPPED        = 0x20,  /**< the module switched it off */
    SUPPLY_LINK_EV_BAUD_FOUND     = 0x40,  /**< AUTO found the module's rate */
    SUPPLY_LINK_EV_SAGGED         = 0x80,  /**< switched off: input sagged */
    SUPPLY_LINK_EV_WIRING_LIVE    = 0x100, /**< wiring refused: output may
                                                be on (4.5)               */
};

typedef enum {
    SUPPLY_LINK_W_NONE = 0,
    SUPPLY_LINK_W_OFF,
    SUPPLY_LINK_W_WIRING,
    SUPPLY_LINK_W_COMMAND,
    SUPPLY_LINK_W_RESET,
} supply_link_write_t;

typedef struct {
    /* What is asked. */
    supply_wiring_t want;
    bool     on;
    uint16_t mv, ma;

    /* What the page holds, as written and acknowledged since the last
     * time it was known. */
    bool     wired;          /* page.wiring holds `want` as it was then */
    bool     wire_wait;      /* page.wiring acknowledged, not yet read held */
    supply_wiring_t page;
    bool     refused;        /* `want` was refused; not written again   */
    bool     commanded;      /* the page holds page_on, page_mv, page_ma */
    bool     page_on;
    uint16_t page_mv, page_ma;

    /* The write in flight. */
    supply_link_write_t pending;
    uint16_t out[LINK_SP_FLAGS];

    /* The last read. */
    uint16_t regs[LINK_SP_COUNT];
    bool     read_any;
    bool     asked;          /* a read was asked for since it was known */
    uint32_t read_ms;
    uint32_t asked_ms;       /* when the last read was asked for        */
    /* The module's readings: SAMPLES as last read, and the page read that
     * first showed it.  A page read whose SAMPLES did not move brings no
     * new reading -- a display read on the coprocessor that was slow or
     * failed -- however fresh the page itself is. */
    uint16_t samples_seen;
    uint32_t sample_ms;

    uint16_t events;
    uint8_t  found;          /* the rate AUTO last reported, 7 none    */
    /* The input and the set point, in mV, on the read that first showed
     * the output switched off for a sagging input (FLAGS bit 10). */
    uint16_t sag_vin_mv, sag_set_mv;
    bool     reset_owed;     /* a module restart asked, not yet written */
} supply_link_t;

void supply_link_init(supply_link_t *s);

/** The link went or came back: nothing on the page is known, and the
 *  panel's ON, if it had one, is over. */
void supply_link_lost(supply_link_t *s);

/** The wiring the settings name now. */
void supply_link_wire(supply_link_t *s, const supply_wiring_t *w);

/** What the output is to be; set points clamped to the page's 1000 to
 *  20000 mV and 50 to 3000 mA. */
void supply_link_command(supply_link_t *s, bool on, uint16_t mv, uint16_t ma);

/** Restart the PD mini (protocol 4.4): written once the page's output is
 *  off and its wiring written, after any OFF owed. */
void supply_link_reset(supply_link_t *s);

/**
 * The next write owed, or SUPPLY_LINK_W_NONE.  @p off and @p n say where
 * on the page and how many registers, @p regs what; the caller writes them
 * and reports with supply_link_written().
 */
supply_link_write_t supply_link_next(supply_link_t *s, uint8_t *off,
                                     uint8_t *n, uint16_t *regs);

/** How the write supply_link_next() gave went: SUPPLY_LINK_ACK, a NACK
 *  reason, or SUPPLY_LINK_NO_ANSWER. */
void supply_link_written(supply_link_t *s, int result);

/** Whether nothing is owed: the page holds the command asked and the
 *  wiring, or the wiring was refused. */
bool supply_link_settled(const supply_link_t *s);

/** Whether a read of the page is due at @p now_ms. */
bool supply_link_read_due(const supply_link_t *s, uint32_t now_ms);

/** The whole page as read at @p now_ms, or @p regs NULL for a read that
 *  was not answered. */
void supply_link_read(supply_link_t *s, const uint16_t *regs,
                      uint32_t now_ms);

/**
 * The supply as the page last said, into @p st: readings, set points read
 * back from the module, the mode -- OFF while the module reads its output
 * off -- the module's reading count (SAMPLES, the coprocessor's count of
 * output readings the module answered) and the time of the page read that
 * first showed that count, and online only for a page read younger than
 * SUPPLY_LINK_STALE_MS from a module the page drives.  Left as they are: output, which is what
 * the panel asked, and the run's extremes, totals and trip.
 */
void supply_link_state(const supply_link_t *s, uint32_t now_ms,
                       supply_state_t *st);

/** The events since the last call, SUPPLY_LINK_EV_*, and cleared. */
uint16_t supply_link_events(supply_link_t *s);

/** The input and the set point in mV that SUPPLY_LINK_EV_SAGGED was read
 *  with: the page's VIN_MV and SET_MV_RB on the read that first showed
 *  FLAGS bit 10.  An input reading taken between the coprocessor's cut and
 *  that read replaces the one that cut; not seen on the bench. */
void supply_link_sag(const supply_link_t *s, uint16_t *vin_mv,
                     uint16_t *set_mv);

/**
 * Where the PD mini's wiring stood when last followed: the edit count and
 * the wiring word the settings publish.  supply_link_wiring_moved() says
 * whether either has moved since, and takes the new ones.  The count is
 * read before the word: an edit that has counted and not yet stored its
 * word, or one undone, is a move too.
 */
typedef struct {
    bool     seen;
    unsigned edits, word;
} supply_link_follow_t;

bool supply_link_wiring_moved(supply_link_follow_t *f, unsigned edits,
                              unsigned word);

#ifdef __cplusplus
}
#endif
