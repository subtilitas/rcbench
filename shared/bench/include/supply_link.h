/**
 * @file
 * @brief The panel's half of the SUPPLY link page (0x2A, protocol 4.3 to 4.13):
 *        what is written to it, in what order, and what a read of it means for
 *        the supply the screen shows.
 *
 * The IOmcu drives the PD mini; the panel asks.  Two things are
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
 * page holding it: a 4.5 IOmcu holds a change while a module has
 * answered until a state read of it shows the output off (FLAGS bit 8),
 * and refuses it when that read shows it on or fails (bit 9), which is
 * taken as a refusal and said as one (SUPPLY_LINK_EV_WIRING_LIVE).
 * Nothing else is written while it waits.  A read that shows the page
 * holding other than what was written -- an IOmcu that restarted
 * between two polls, or one that switched the output off when the
 * heartbeat stopped -- makes everything owed again, and an ON the page no
 * longer holds is lost.
 *
 * A programming port commands the supply too (protocol 4.13, FLAGS bit 11,
 * LINK_SP_PORT).  A read with the bit set, and the first read after it,
 * that shows OUTPUT, SET_MV or SET_MA other than written is the port's
 * doing: the page's values become what is asked, nothing is written back
 * and no ON is reported lost.  supply_link_adopted() hands them to the
 * caller, whose own copy of the command has to follow.  What the caller
 * asks next with supply_link_command() is written as ever; the page
 * refuses it while the port holds the supply unless it is an OFF.  A
 * refusal of set points for an output that is on is judged by the next
 * read: with the bit set the page's values are adopted as above and the
 * output stays on, without it the ON is refused as ever.  Nothing is
 * written between the refusal and that read except an OFF asked for.
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

/**
 * @brief The PD mini's own range, as the screen's caps: 1 to 20V and 0.05 to
 *        3A from the vendor's page, in 10mV and 10mA steps.
 */
#define SUPPLY_CAPS_PDMINI { 1.0f, 20.0f, 0.01f, 0.05f, 3.0f, 0.01f }

/**
 * @brief A reading older than this is not the supply answering: longer than one
 *        exchange's 1000ms timeout, so one lost frame is not a supply gone,
 *        and  about the 3 failed transactions the IOmcu's driver takes.
 */
#define SUPPLY_LINK_STALE_MS 1500u
/**
 * @brief A reading count that has stood this long is a supply no trip can
 *        watch: 2 x SUPPLY_LINK_STALE_MS, so the readings have been marked not
 *        valid (ok 0) for a further 1500ms (supply_link_blind()).
 */
#define SUPPLY_LINK_BLIND_MS (2u * SUPPLY_LINK_STALE_MS)
/** @brief How often the page is read while the supply is in use. */
#define SUPPLY_LINK_READ_MS  100u

/** @brief The baud setting that asks the IOmcu to find the rate (4.4). */
#define SUPPLY_LINK_BAUD_AUTO 7u

/** @brief Where the PD mini is wired, as SETUP INTERFACES has it. */
typedef struct {
    bool    en;
    int8_t  tx, rx;   /**< IOmcu GPIO; -1 not set          */
    uint8_t baud;     /**< the module's UART Baudrate, 0 to 6, 7 AUTO */
} supply_wiring_t;

/**
 * @brief What came of a write or a read: ACK or DATA, a NACK's reason, or none.
 */
#define SUPPLY_LINK_NO_ANSWER (-1)
#define SUPPLY_LINK_ACK       0

/** @brief Things the caller tells the operator about, each once. */
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

/**
 * @brief The writes the supply link asks for next.
 */
typedef enum {
    SUPPLY_LINK_W_NONE = 0,
    SUPPLY_LINK_W_OFF,
    SUPPLY_LINK_W_WIRING,
    SUPPLY_LINK_W_COMMAND,
    SUPPLY_LINK_W_RESET,
} supply_link_write_t;

/**
 * @brief The panel's state of the SUPPLY link page.
 */
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
     * new reading -- a display read on the IOmcu that was slow or
     * failed -- however fresh the page itself is. */
    uint16_t samples_seen;
    uint32_t sample_ms;

    uint16_t events;
    uint8_t  found;          /* the rate AUTO last reported, 7 none    */
    /* The input and the set point, in mV, on the read that first showed
     * the output switched off for a sagging input (FLAGS bit 10). */
    uint16_t sag_vin_mv, sag_set_mv;
    bool     reset_owed;     /* a module restart asked, not yet written */
    bool     port_held;      /* the last read had LINK_SP_PORT set      */
    bool     judge;          /* set points refused on an output that is
                                on: the next read says by whom           */
    bool     off_owed;       /**< an OFF asked by supply_link_cut(), not
                                  yet answered */
    bool     adopted;        /* on, mv, ma were taken from the page and
                                not yet handed out                      */
    bool     kept;           /**< the page's command left to the port
                                  that holds it (supply_link_keep()),
                                  until a read shows the port gone      */
} supply_link_t;

/**
 * @brief Sets up the supply link state.
 *
 * @param s The link state to set up.
 */
void supply_link_init(supply_link_t *s);

/**
 * @brief The link went or came back: nothing on the page is known, and the
 *        panel's ON, if it had one, is over.
 *
 * @param s The link state.
 */
void supply_link_lost(supply_link_t *s);

/**
 * @brief The wiring the settings name now.
 *
 * @param s The link state.
 * @param w The wiring the settings name.
 */
void supply_link_wire(supply_link_t *s, const supply_wiring_t *w);

/**
 * @brief What the output is to be; set points clamped to the page's 1000 to
 *        20000mV and 50 to 3000mA.
 *
 * @param s  The link state.
 * @param on true to switch the output on.
 * @param mv The set point voltage, in mV.
 * @param ma The set point current, in mA.
 */
void supply_link_command(supply_link_t *s, bool on, uint16_t mv, uint16_t ma);

/**
 * @brief An OFF written to the page whatever this end thinks it holds: the
 *        output asked off, and OUTPUT 0 written alone before anything else,
 *        once, even with the page read off.
 *
 * For an error while a programming port holds the supply (error_stop.h): a
 * port in a power cycle has the output off and is about to switch it on,
 * and an IOmcu of link 4.31 refuses that ON after an OFF of the panel's.
 * Written again while it goes unanswered; dropped with the link.
 *
 * @param s The link state; nothing is done for NULL.
 */
void supply_link_cut(supply_link_t *s);

/**
 * @brief An error that leaves the supply to the programming port that holds
 *        it (error_sites_page_keeps()): this end asks what the page held at
 *        the last read, so no OFF of the panel's is written over the
 *        port's command, and an ON the settings' refused wiring would
 *        refuse is not refused again while the port holds the page.
 *
 * Handed out as the port's command (supply_link_adopted()).  For an IOmcu
 * older than link 4.31, which cuts a write to the servo's memory at the
 * panel's OFF.
 *
 * @param s The link state; nothing is done for NULL.
 * @return true when the last read showed a port holding the page and the
 *         page's command was known, with no OFF owed by supply_link_cut();
 *         false otherwise, and nothing changed.
 */
bool supply_link_keep(supply_link_t *s);

/**
 * @brief Restart the PD mini (protocol 4.4): written once the page's output is
 *        off and its wiring written, after any OFF owed.
 *
 * @param s The link state.
 */
void supply_link_reset(supply_link_t *s);

/**
 * @brief The next write owed, or SUPPLY_LINK_W_NONE.
 *
 * @p off and @p n say where
 * on the page and how many registers, @p regs what; the caller writes them
 * and reports with supply_link_written().
 *
 * @param s    The link state.
 * @param off  Receives the page offset of the write.
 * @param n    Receives the number of registers.
 * @param regs Receives the register values.
 * @return The write owed, or SUPPLY_LINK_W_NONE.
 */
supply_link_write_t supply_link_next(supply_link_t *s, uint8_t *off,
                                     uint8_t *n, uint16_t *regs);

/**
 * @brief How the write supply_link_next() gave went: SUPPLY_LINK_ACK, a NACK
 *        reason, or SUPPLY_LINK_NO_ANSWER.
 *
 * @param s      The link state.
 * @param result The result: an acknowledgement, a NACK's reason or
 *               SUPPLY_LINK_NO_ANSWER.
 */
void supply_link_written(supply_link_t *s, int result);

/**
 * @brief Whether nothing is owed: the page holds the command asked and the
 *        wiring, or the wiring was refused.
 *
 * @param s The link state.
 * @return true when nothing is owed.
 */
bool supply_link_settled(const supply_link_t *s);

/**
 * @brief Whether what @p ev, one SUPPLY_LINK_EV_* bit, says still holds now.
 *
 * A refused wiring -- SUPPLY_LINK_EV_WIRING_REFUSED or _WIRING_LIVE -- until a
 * read shows the page holding the wiring the settings ask: pins stepped back to
 * ones the page takes end it with the first read that shows them held.  With no
 * IOmcu answering it stands.  True for every other event.  The caller takes
 * back an alert it showed for a refused wiring once this is false.
 *
 * @param s  The link state.
 * @param ev One SUPPLY_LINK_EV_* bit.
 * @return true while the cause of the event holds.
 */
bool supply_link_event_stands(const supply_link_t *s, uint16_t ev);

/**
 * @brief Whether a read of the page is due at @p now_ms.
 *
 * @param s      The link state.
 * @param now_ms The time, in ms.
 * @return true when a read of the page is due.
 */
bool supply_link_read_due(const supply_link_t *s, uint32_t now_ms);

/**
 * @brief The whole page as read at @p now_ms, or @p regs NULL for a read that
 *        was not answered.
 *
 * @param s      The link state.
 * @param regs   The page as read, or NULL for a read not answered.
 * @param now_ms The time, in ms.
 */
void supply_link_read(supply_link_t *s, const uint16_t *regs,
                      uint32_t now_ms);

/**
 * @brief The supply as the page last said, into @p st: readings, set points
 *        read back from the module, the mode -- OFF while the module reads its
 *        output off -- the module's reading count (SAMPLES, the IOmcu's count
 *        of output readings the module answered) and the time of the page read
 *        that first showed that count, and online only for a page read younger
 *        than SUPPLY_LINK_STALE_MS from a module the page drives.
 *
 * The readings are marked valid (ok) only while the count moved within the last
 * SUPPLY_LINK_STALE_MS; behind a count that stands still they stay as the last
 * ones with ok 0.  Left as they are: output, which is what the panel asked, and
 * the run's extremes, totals and trip.
 *
 * @param s      The link state.
 * @param now_ms The time, in ms.
 * @param st     Receives the supply state.
 */
void supply_link_state(const supply_link_t *s, uint32_t now_ms,
                       supply_state_t *st);

/**
 * @brief Whether the module's readings have stopped at @p now_ms: a page has
 *        been read and its reading count (SAMPLES) has not moved for
 *        SUPPLY_LINK_BLIND_MS or more, counted on the 32-bit millisecond clock
 *        across its wrap.
 *
 * The page may still answer and the module read ONLINE:
 * a display read that fails while the other reads answer.  No operator
 * trip acts on such readings, so the caller switches an output that is on
 * off and says why.  False before the first page read.
 *
 * @param s      The link state.
 * @param now_ms The time, in ms.
 * @return true when the page has been read and the last read is at least
 *         SUPPLY_LINK_BLIND_MS old.
 */
bool supply_link_blind(const supply_link_t *s, uint32_t now_ms);

/**
 * @brief The command a programming port left on the page, once: true with @p
 *        on, @p mv and @p ma filled when the page's command was taken as the
 *        one asked since the last call.
 *
 * The caller's own copy of the command follows it; a copy left as it was is
 * written again at the caller's next supply_link_command().
 *
 * @param s  The link state.
 * @param on Receives the output state the page holds.
 * @param mv Receives the set point voltage, in mV.
 * @param ma Receives the set point current, in mA.
 * @return true when the page took the command asked since the last call.
 */
bool supply_link_adopted(supply_link_t *s, bool *on, uint16_t *mv,
                         uint16_t *ma);

/**
 * @brief Whether the last read shows the supply free of every programming
 *        port and its output off: LINK_SP_PORT clear, OUTPUT 0, and the
 *        module neither read on nor possibly on (LINK_SP_ON, LINK_SP_LIVE).
 *
 * The moment a port's set points can give way to the caller's own without
 * reaching an output that is on (supply_screen_port_done()).
 *
 * @param s The link state.
 * @return true when it does; false for NULL and before the first read.
 */
bool supply_link_port_free_off(const supply_link_t *s);

/**
 * @brief The events since the last call, SUPPLY_LINK_EV_*, and cleared.
 *
 * @param s The link state.
 * @return The events since the last call, as SUPPLY_LINK_EV_* bits.
 */
uint16_t supply_link_events(supply_link_t *s);

/** @brief The events that are errors of the supply (error_stop.h): every one
 *         but a refused wiring, which answers the operator's edit, and the
 *         rate AUTO found. */
#define SUPPLY_LINK_EV_ERRORS                                              \
    (SUPPLY_LINK_EV_ON_REFUSED | SUPPLY_LINK_EV_ON_LOST                    \
     | SUPPLY_LINK_EV_STUCK | SUPPLY_LINK_EV_SET_STUCK                     \
     | SUPPLY_LINK_EV_TRIPPED | SUPPLY_LINK_EV_SAGGED)

/**
 * @brief Whether @p ev holds an error of the supply: an ON refused or let go,
 *        an output that would not switch, set points that would not take,
 *        the module's own protection, a sagging input.
 *
 * The panel switches the output off and the bench to SAFE for one
 * (error_stop.h).
 *
 * @param ev SUPPLY_LINK_EV_* bits, as supply_link_events() returned them.
 * @return true when one of SUPPLY_LINK_EV_ERRORS is set.
 */
bool supply_link_ev_error(uint16_t ev);

/** @brief What the operator is told of an ON that ended or never began. */
typedef enum {
    SUPPLY_LINK_SAY_NONE = 0,  /**< nothing of that kind happened         */
    SUPPLY_LINK_SAY_TRIPPED,   /**< the module switched its output off    */
    SUPPLY_LINK_SAY_LOST,      /**< the IOmcu let an ON go                */
    SUPPLY_LINK_SAY_REFUSED,   /**< an ON refused                         */
    /** @brief An ON refused while the IOmcu reports its failsafe:
     *         LINK_FAULT_LINK_SILENT or LINK_FAULT_HEARTBEAT.  It takes no
     *         ON until a CLEAR, which only an ARM writes. */
    SUPPLY_LINK_SAY_FAILSAFE,
} supply_link_say_t;

/**
 * @brief Which of the ON's endings @p ev holds, the module's own trip first,
 *        then an ON let go, then an ON refused, named for the failsafe when
 *        @p faults holds it.
 *
 * The IOmcu takes an ON only with the heartbeat trusted and the link out
 * of failsafe (safety_gate_supply_ok()); the STATUS page reports those two
 * as LINK_FAULT_HEARTBEAT and LINK_FAULT_LINK_SILENT.
 *
 * @param ev     SUPPLY_LINK_EV_* bits, as supply_link_events() returned
 *               them.
 * @param faults The STATUS page's fault bitmap at its last read.
 * @return       SUPPLY_LINK_SAY_NONE when @p ev holds none of
 *               SUPPLY_LINK_EV_TRIPPED, _ON_LOST and _ON_REFUSED.
 */
supply_link_say_t supply_link_on_ended(uint16_t ev, uint16_t faults);

/**
 * @brief The input and the set point in mV that SUPPLY_LINK_EV_SAGGED was read
 *        with: the page's VIN_MV and SET_MV_RB on the read that first showed
 *        FLAGS bit 10.
 *
 * An input reading taken between the IOmcu's cut and
 *  that read replaces the one that cut.
 *
 * @param s      The link state.
 * @param vin_mv Receives the input voltage, in mV.
 * @param set_mv Receives the set point, in mV.
 */
void supply_link_sag(const supply_link_t *s, uint16_t *vin_mv,
                     uint16_t *set_mv);

/**
 * @brief Where the PD mini's wiring stood when last followed: the edit count
 *        and the wiring word the settings publish.
 *
 * supply_link_wiring_moved() says whether either has moved since, and takes the
 * new ones.  The count is read before the word: an edit that has counted and
 * not yet stored its word, or one undone, is a move too.
 */
typedef struct {
    bool     seen;
    unsigned edits, word;
} supply_link_follow_t;

/**
 * @brief Whether the PD mini's wiring has moved since it was last followed.
 *
 * @param f     The followed wiring state, updated.
 * @param edits The edit count now.
 * @param word  The wiring word now.
 * @return true when the edit count or the wiring word has moved since the last
 *         call.
 */
bool supply_link_wiring_moved(supply_link_follow_t *f, unsigned edits,
                              unsigned word);

/**
 * @brief The PD mini's pins the page holds by its last read, as the IOmcu
 *        reserves them (supply_page_pins()): TX and RX while ENABLE is on, -1
 *        and -1 while it is off.
 *
 * False, both -1, before the first read since
 * the link came up; a refused wiring leaves the page's old one, and that
 * is what is read.  @p tx and @p rx may be NULL.
 *
 * @param s  The link state.
 * @param tx Receives the TX pin, or -1; may be NULL.
 * @param rx Receives the RX pin, or -1; may be NULL.
 * @return false before the first read since the link came up.
 */
bool supply_link_held_pins(const supply_link_t *s, int *tx, int *rx);

/**
 * @brief Whether the last read of the page had LINK_SP_PORT set: a
 *        programming port holds the supply, and the page takes an OFF of
 *        the panel's and no other change of OUTPUT, SET_MV or SET_MA.
 *
 * The KST page holds it for its whole session; from protocol 4.29 the
 * HITEC page holds it from its OPEN to its CLOSE.  False before the first
 * read since the link came up.
 *
 * @param s The link state.
 * @return true while held.
 */
bool supply_link_port_held(const supply_link_t *s);

/**
 * @brief Whether a programming port holds the PD mini's output for the
 *        panel's error rule (error_bench_t.port_holds, error_sites.h): the
 *        PD mini in use, the last read of the page with LINK_SP_PORT set,
 *        and a programmer's session with a port.
 *
 * Both words count, and the hold ends at the first that says it ended.  A
 * session with a port whose supply the page does not hold -- the rbmod
 * carrier's ports, which run from PROG_SUPPLY, or a Hitec session between
 * two POWERs on an IOmcu older than 4.29 -- does not count.  Nor does the
 * page's flag once the session has ended: a port that lets go before the
 * next read of the page.  The page's flag is as last read: a port that
 * takes the supply counts from the next read, SUPPLY_LINK_READ_MS (100ms)
 * apart while the PD mini is in use.  An unanswered read keeps the last
 * answer; a lost link clears it.
 *
 * @param s       The link state.
 * @param pdmini  The PD mini is the supply, not the panel's model.
 * @param session A KST or Hitec programmer has a port, as the panel last
 *                read its page.
 * @return true while held; false for NULL, for the model, without a
 *         session, and before the first read since the link came up.
 */
bool supply_link_port_holds(const supply_link_t *s, bool pdmini,
                            bool session);

/**
 * @brief Whether a programmer's session keeps the panel's own OUTPUT ON
 *        and set points off the PD mini: a session with a port, on a board
 *        whose programming ports run from the PD mini.
 *
 * On a board that switches the ports' own supply (PROG_SUPPLY, the rbmod
 * carrier) the ports never take the PD mini, the IOmcu never holds the
 * SUPPLY page for them, and the operator keeps the PD mini.
 *
 * @param session   A KST or Hitec programmer has a port.
 * @param ports_own The ports' own supply is the programmers' supply
 *                  (prog_supply_link_owns()).
 * @return true when the panel takes no ON and writes no set point.
 */
bool supply_link_session_blocks(bool session, bool ports_own);

#ifdef __cplusplus
}
#endif
