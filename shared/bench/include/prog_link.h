/**
 * @file prog_link.h
 * @brief The panel's side of the programming ports' page (0x33).
 *
 * The SERVO screen's tester drives one of two things: the standard servo
 * outputs, or a programming port.  Which one is a register of the
 * IOmcu (LINK_PG_TARGET), and whether the port renders a pulse is
 * another (its bit in LINK_PG_PWM).  This module holds what the page last
 * read, what the screen wants, and names the one write that brings the page
 * to it.
 *
 * Rules:
 *
 *   - Nothing is written before the page has been read, and a write that is
 *     not acknowledged makes the page unknown until the next read.
 *   - The target changes only while the bench is quiet: the bank disarmed
 *     and no surface holding a position this panel wrote.  The pulse is
 *     switched on under the same condition and switched off at any time.
 *   - A port the page does not have is not a target: the standard outputs
 *     stay the target.
 *   - The pulse bit of a port is set only while that port is the target.
 *   - While the servo programmer has a port, nothing is written to the
 *     page and the tester commands no port.
 *
 * An IOmcu older than protocol 4.13 has no page: nothing is read or
 * written and the target is the standard outputs.
 *
 * From protocol 4.19 the SERVO page's frame rate is the target's, and a
 * TARGET change returns it to 0.  prog_link_rate() says what the reads and
 * writes since it was last asked mean for the rate the panel last wrote.
 *
 * A port's PIN and, from protocol 4.26, its FUNC are the IOmcu's, kept in
 * its flash with the outputs' binding: the panel writes one only when the
 * operator asks (prog_link_ask()), never from a stored setting.  An ask
 * waits for a bench at rest as a TARGET change does; a port that is the
 * TARGET is first let go (TARGET 0, its pulse off first), and the target
 * wanted comes back after the write.  An IOmcu older than 4.26 has 24
 * registers and no FUNC: every port reads SERVO PWM and a FUNC ask is
 * refused before anything is sent.
 *
 * Pure logic, no I/O.  Nothing here has run on hardware.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_PROG_LINK_H
#define RCBENCH_PROG_LINK_H

#include <stdbool.h>
#include <stdint.h>

#include "link_pages.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The page is read this often, in ms, and at once while it is unknown.
 */
#define PROG_LINK_POLL_MS 500u
/** @brief What prog_link_pin() returns for a port without a pin. */
#define PROG_LINK_NO_PIN 0xFFu
/**
 * @brief prog_link_drive()'s target while the page is there and not known:
 *        where  the commands would go is not known, and the tester sends none.
 */
#define PROG_LINK_TARGET_UNKNOWN 0xFEu
/** @brief prog_link_drive()'s target with no IOmcu answering. */
#define PROG_LINK_TARGET_NO_LINK 0xFFu
/**
 * @brief The registers prog_link_cfg() fills: SLEW, MIN_US, MAX_US of a port.
 */
#define PROG_LINK_CFG_REGS 3u

/**
 * @brief What became of the SERVO rate the panel last wrote (prog_link_rate()).
 */
typedef enum {
    PROG_LINK_RATE_KEPT = 0,   /**< no TARGET change seen                 */
    PROG_LINK_RATE_RESET,      /**< the TARGET changed: the rate is 0     */
    PROG_LINK_RATE_UNKNOWN,    /**< a TARGET write went unacknowledged    */
} prog_link_rate_t;

/** @brief prog_link_t's seen while no TARGET is known. */
#define PROG_LINK_SEEN_NONE 0xFFu

/** @brief What a prog_link_ask() asks: a port's PIN or its FUNC. */
typedef enum {
    PROG_LINK_ASK_PIN = 0,
    PROG_LINK_ASK_FUNC,
} prog_link_ask_kind_t;

/** @brief Where the last prog_link_ask() stands. */
typedef enum {
    PROG_LINK_ASK_IDLE = 0,    /**< none asked since the link came up    */
    PROG_LINK_ASK_WAITING,     /**< asked, not written yet: the page not
                                    known, the programmer holds a port,
                                    or the bench is not at rest          */
    PROG_LINK_ASK_DONE,        /**< written and acknowledged            */
    PROG_LINK_ASK_REFUSED,     /**< written, not acknowledged: refused
                                    or unanswered                        */
    PROG_LINK_ASK_LOST,        /**< the link went down before the write  */
} prog_link_ask_state_t;

/** @brief What an ask waiting on the bench waits on. */
typedef enum {
    PROG_LINK_WAIT_NONE = 0, /**< nothing: written at the next pass          */
    PROG_LINK_WAIT_READ,     /**< the page is not read yet                   */
    PROG_LINK_WAIT_KST,      /**< the KST or Chaservo programmer has a port  */
    PROG_LINK_WAIT_HITEC,    /**< the Hitec programmer has a port            */
    PROG_LINK_WAIT_ESC,      /**< the ESC programmer has a pin               */
    PROG_LINK_WAIT_SAFE,     /**< the bench is ARMED                         */
    PROG_LINK_WAIT_TEST,     /**< a position, a sweep or a hold is out       */
} prog_link_wait_t;

/**
 * @brief The panel's side of the programming ports' page.
 */
typedef struct {
    bool     up;                   /**< an IOmcu answers              */
    bool     page;                 /**< the IOmcu has the page       */
    bool     target_rate;          /**< from 4.19: a TARGET change sets the
                                        SERVO rate to 0                    */
    uint8_t  seen;                 /**< the TARGET last known, or
                                        PROG_LINK_SEEN_NONE                */
    uint8_t  rate;                 /**< a prog_link_rate_t not yet asked   */
    bool     known;                /**< regs holds a read of it            */
    bool     read_once;            /**< read_ms is a time                  */
    uint32_t read_ms;                   /**< when the page was last read, ms */
    uint16_t regs[LINK_PG_REGS];        /**< the page's registers as last read */
    uint8_t  want_target;          /**< 0, or 1 + n for port n             */
    bool     want_pwm;                  /**< whether the wanted target is a PWM port */
    bool     held;                 /**< a programmer has a port            */
    uint8_t  holder;               /**< which: a prog_link_wait_t          */
    uint8_t  rest_wait;            /**< why the bench is not at rest: a
                                        prog_link_wait_t                   */
    bool     func;                 /**< from 4.26: FUNC and 28 registers  */
    bool     dshot;                /**< from 4.28: FUNC takes DShot       */
    uint8_t  ask_state;            /**< a prog_link_ask_state_t            */
    uint8_t  ask_port;                  /**< port the ask is for */
    uint8_t  ask_kind;             /**< a prog_link_ask_kind_t             */
    uint16_t ask_value;            /**< the register value to write        */
    bool     reread;               /**< an ask was written: nothing more
                                        until the page is read again      */
    bool     hitec;                /**< from 4.23: the HITEC page         */
    uint8_t  hitec_held;           /**< bit n: the HITEC page holds port n */
} prog_link_t;

/** @brief One register to write. */
typedef struct {
    uint8_t  off;
    uint16_t value;
} prog_link_write_t;

/**
 * @brief No link, no page, nothing wanted.
 *
 * @param p The link state to set up.
 */
void prog_link_init(prog_link_t *p);

/**
 * @brief An IOmcu answered with protocol minor @p minor.
 *
 * What is wanted
 *  stays; the page is unknown.
 *
 * @param p     The link state.
 * @param minor The IOmcu's protocol minor version.
 */
void prog_link_came_up(prog_link_t *p, uint16_t minor);

/**
 * @brief The link is down: no page, no link.
 *
 * @param p The link state.
 */
void prog_link_lost(prog_link_t *p);

/**
 * @brief What the screen wants: @p target 0 for the standard outputs or 1 + n
 *        for  port n, and whether that port renders its pulse.
 *
 * A target above
 *  LINK_PG_PORTS_MAX is taken as 0.
 *
 * @param p      The link state.
 * @param target 0 for the standard outputs, or 1 + n for port n.
 * @param pwm    true when that port renders its pulse.
 */
void prog_link_want(prog_link_t *p, uint8_t target, bool pwm);

/**
 * @brief Whether the servo programmer has a port (kst_prog_port_taken()).
 *
 * While it has, the page is read and not written: prog_link_want() changes
 * nothing, prog_link_next() names no write -- not a pulse switched off either
 * -- prog_link_pending() is false and prog_link_command() takes no command for
 * a port.  What is wanted afterwards is what prog_link_want() says after the
 * hold.
 *
 * @param p    The link state.
 * @param held true while the servo programmer has a port.
 */
void prog_link_hold(prog_link_t *p, bool held);
/**
 * @brief prog_link_hold() that says who holds: an ask waiting names it.
 *
 * @param p      The link state.
 * @param holder PROG_LINK_WAIT_KST, _HITEC or _ESC, or PROG_LINK_WAIT_NONE for
 *               no hold.
 */
void prog_link_hold_by(prog_link_t *p, prog_link_wait_t holder);
/**
 * @brief Why the bench is not at rest for a write of this page, as the caller
 *        of prog_link_next() judged @p quiet.
 *
 * @param p   The link state.
 * @param why PROG_LINK_WAIT_SAFE, _TEST, or PROG_LINK_WAIT_NONE at rest.
 */
void prog_link_rest_note(prog_link_t *p, prog_link_wait_t why);
/**
 * @brief Whether an ask waits and nothing here stops it being written: the page
 *        is known, read since the last ask and not held.  The bench's own rest
 *        is the caller's to reach, a sweep's state not known included.
 *
 * @param p The link state.
 * @return true then.
 */
bool prog_link_ask_open(const prog_link_t *p);
/**
 * @brief What an ask waits on.  PROG_LINK_WAIT_NONE when no ask waits, or when
 *        one is written at the next pass.
 *
 * @param p The link state.
 * @return A prog_link_wait_t.
 */
prog_link_wait_t prog_link_ask_wait(const prog_link_t *p);
/**
 * @brief Whether the servo programmer holds a port.
 *
 * @param p The link state.
 * @return true while the programmer holds a port.
 */
bool prog_link_held(const prog_link_t *p);

/**
 * @brief The registers a read of the page asks for: LINK_PG_REGS from 4.26,
 *        LINK_PG_REGS_V424 before.
 *
 * @param p The link state.
 * @return The number of registers a read of the page asks for.
 */
uint8_t prog_link_regs(const prog_link_t *p);

/**
 * @brief Whether the IOmcu takes a FUNC: protocol 4.26 or later.
 *
 * @param p The link state.
 * @return true when the IOmcu takes a FUNC.
 */
bool prog_link_has_func(const prog_link_t *p);

/**
 * @brief Whether the IOmcu takes FUNC DSHOT150 to DSHOT600: protocol 4.28 or
 *        later.
 *
 * @param p The link state.
 * @return true when the IOmcu takes FUNC DSHOT150 to DSHOT600.
 */
bool prog_link_has_dshot(const prog_link_t *p);

/**
 * @brief Ask for port @p port's PIN (@p value a GPIO, or LINK_PG_PIN_NONE) or
 *        FUNC (a link_pg_func_t) to be written.
 *
 * True when taken: the page is there, the port is one it has, and for FUNC the
 * IOmcu has the register and @p value is SERVO PWM or NONE, or from 4.28
 * DSHOT150, DSHOT300 or DSHOT600.  A later ask replaces one still waiting.
 * prog_link_next() writes it on a bench at rest.
 *
 * @param p     The link state.
 * @param port  The port.
 * @param kind  PIN or FUNC.
 * @param value The GPIO, or the link_pg_func_t.
 * @return true when the ask is taken.
 */
bool prog_link_ask(prog_link_t *p, uint8_t port, prog_link_ask_kind_t kind,
                   uint16_t value);

/**
 * @brief Where the last ask stands, and what it was (@p port, @p kind, @p value
 *        may be NULL).
 *
 * @param p     The link state.
 * @param port  Receives the port, or NULL.
 * @param kind  Receives the kind, or NULL.
 * @param value Receives the value, or NULL.
 * @return Where the last ask stands.
 */
prog_link_ask_state_t prog_link_ask_state(const prog_link_t *p,
                                          uint8_t *port, uint8_t *kind,
                                          uint16_t *value);

/**
 * @brief Whether the page is to be read now: it is unknown, or
 *        PROG_LINK_POLL_MS have passed.
 *
 * @param p      The link state.
 * @param now_ms The time, in ms.
 * @return true when the page is to be read now.
 */
bool prog_link_read_due(const prog_link_t *p, uint32_t now_ms);

/**
 * @brief The HITEC page beside the PROG page (protocol 4.23): a session the
 *        bench console opens holds a port the panel's rows must leave alone.
 *
 * Whether its STATE register is read with the PROG page: an IOmcu answers that
 * has the page.
 *
 * @param p The link state.
 * @return true when the HITEC STATE register is read with the PROG page.
 */
bool prog_link_hitec_due(const prog_link_t *p);

/**
 * @brief The HITEC STATE register as read, @p state NULL for a read nobody
 *        answered: the port it names is held from STOPPING on, free at FREE.
 *
 * A read not answered keeps what was known.
 *
 * @param p     The link state.
 * @param state The STATE register as read, or NULL for a read nobody answered.
 */
void prog_link_hitec_read(prog_link_t *p, const uint16_t *state);

/**
 * @brief Bit n: the HITEC page holds port n, as last read; 0 with no link, no
 *        page or nothing read since the link came up.
 *
 * @param p The link state.
 * @return Bit n set while the HITEC page holds port n.
 */
uint8_t prog_link_hitec_held(const prog_link_t *p);

/**
 * @brief The page as read, prog_link_regs() registers, or NULL for a read that
 *        was not answered: the page is unknown then.
 *
 * Below 4.26 every port's
 *  FUNC is taken as SERVO PWM.
 *
 * @param p      The link state.
 * @param regs   The page as read, or NULL for a read not answered.
 * @param now_ms The time, in ms.
 */
void prog_link_read(prog_link_t *p, const uint16_t *regs, uint32_t now_ms);

/**
 * @brief The page is unknown until it is read again.
 *
 * @param p The link state.
 */
void prog_link_stale(prog_link_t *p);

/**
 * @brief The write that is due, into @p w.
 *
 * @p quiet says the bank is disarmed and
 * no surface holds a position this panel wrote.  False for none: no page,
 * the page unknown, nothing differs, or what differs waits for @p quiet.
 * The pulse is switched off before the target changes and on after.  An
 * ask (prog_link_ask()) is written on a bench at rest once its port is
 * neither the target nor pulsing, before the pulse goes back on.
 *
 * @param p     The link state.
 * @param quiet true when the bank is disarmed and no surface holds a position
 *              this panel wrote.
 * @param w     Receives the write that is due.
 * @return true when a write is due.
 */
bool prog_link_next(const prog_link_t *p, bool quiet, prog_link_write_t *w);

/**
 * @brief The port @p w switches its pulse on for, as 1 + n, or 0 for a write
 *        that switches none on.
 *
 * An IOmcu from protocol 4.16 refuses that write
 * while the port's range is longer than the frame it runs at (1784us at
 * 560Hz), so the panel states the range the screen names to the port
 * first: a range kept from an earlier profile would keep it refused.
 *
 * @param p The link state.
 * @param w The write.
 * @return The port the write switches its pulse on for, as 1 + n, or 0 for
 *         none.
 */
uint8_t prog_link_pulse_on(const prog_link_t *p, const prog_link_write_t *w);

/**
 * @brief What became of @p w: acknowledged, or not (refused or unanswered),
 *        which  makes the page unknown.
 *
 * @param p   The link state.
 * @param w   The write.
 * @param ack true when the write was acknowledged.
 */
void prog_link_written(prog_link_t *p, const prog_link_write_t *w, bool ack);

/**
 * @brief The target the screen can have: what is wanted, or 0 when the page has
 *        no such port, the port has no pin bound to the PWM generator -- the
 *        IOmcu refuses such a target -- or an ask for that port waits.
 *
 * 0
 *  while the page is unknown or absent.
 *
 * @param p The link state.
 * @return The target the screen can have.
 */
uint8_t prog_link_wanted(const prog_link_t *p);

/**
 * @brief Whether prog_link_target() is the IOmcu's: there is no page, or  the
 *        page is read.
 *
 * A release goes to the target that holds the pulse,
 *  so it waits for this and not for prog_link_settled().
 *
 * @param p The link state.
 * @return true when prog_link_target() is the IOmcu's.
 */
bool prog_link_ready(const prog_link_t *p);

/**
 * @brief The target as the IOmcu holds it; 0 without a page or a read.
 *
 * @param p The link state.
 * @return The target as the IOmcu holds it.
 */
uint8_t prog_link_target(const prog_link_t *p);

/**
 * @brief Whether a target change waits: the page is known and its target is not
 *        the one prog_link_wanted() names.
 *
 * @param p The link state.
 * @return true while a target change waits.
 */
bool prog_link_pending(const prog_link_t *p);

/**
 * @brief Whether the tester may command: there is no page, or the page is known
 *        and its target is the one wanted.
 *
 * The pulse bit is not part of it.
 *
 * @param p The link state.
 * @return true when the tester may command.
 */
bool prog_link_settled(const prog_link_t *p);

/**
 * @brief Whether the tester sends a command now, a release for @p release.
 *
 * Not before the page is read (prog_link_ready()); not to a port while the
 * programmer has one, a release neither -- it stays owed; and a command that is
 * not a release only once the target is the one wanted (prog_link_settled())
 * and the screen wants one that can be driven (prog_link_no_drive()).
 *
 * @param p       The link state.
 * @param release true for a release command.
 * @return true when the tester sends the command now.
 */
bool prog_link_command(const prog_link_t *p, bool release);

/**
 * @brief What the reads and writes since the last call did to the SERVO rate,
 *        and forgets it.
 *
 * From 4.19: PROG_LINK_RATE_RESET once a read or an
 * acknowledged write shows a TARGET other than the one known before, and
 * PROG_LINK_RATE_UNKNOWN once a TARGET write went unacknowledged, which
 * may or may not have changed it; the stronger of the two when both
 * happened.  Always PROG_LINK_RATE_KEPT below 4.19, where the rate does
 * not follow the target, and for the first TARGET known after a link-up.
 *
 * @param p The link state.
 * @return What the reads and writes did to the SERVO rate.
 */
prog_link_rate_t prog_link_rate(prog_link_t *p);

/**
 * @brief Whether the target port renders its pulse, as the page holds it.
 *
 * False
 *  with the standard outputs as target.
 *
 * @param p The link state.
 * @return true when the target port renders its pulse.
 */
bool prog_link_pwm(const prog_link_t *p);

/** @brief prog_link_count() while the ports are not known. */
#define PROG_LINK_COUNT_UNKNOWN 0xFFu

/**
 * @brief The ports the IOmcu's page reports (register COUNT), at most
 *        LINK_PG_PORTS_MAX: 0 for an IOmcu without the page (protocol older
 *        than  4.13), PROG_LINK_COUNT_UNKNOWN with no link or before the page's
 *        first  read since the link came up, a write not acknowledged, or a
 *        read not  answered (prog_link_stale()).
 *
 * @param p The link state.
 * @return The ports the page reports, or PROG_LINK_COUNT_UNKNOWN.
 */
uint8_t prog_link_count(const prog_link_t *p);

/**
 * @brief The GPIO port @p port is bound to, or PROG_LINK_NO_PIN: no page, no
 *        read, no such port, no pin, or a pin the IOmcu refused.
 *
 * @param p    The link state.
 * @param port The port.
 * @return The GPIO the port is bound to, or PROG_LINK_NO_PIN.
 */
uint8_t prog_link_pin(const prog_link_t *p, uint8_t port);

/** @brief A port as the page holds it, for the OUTPUTS entry and the board view. */
typedef struct {
    uint8_t pin;        /**< the PIN register, PROG_LINK_NO_PIN for none  */
    uint8_t func;       /**< a link_pg_func_t                              */
    bool    refused;    /**< the IOmcu does not hold the pin (FLAGS)      */
} prog_link_port_t;

/**
 * @brief Port @p port as the page holds it into @p out.
 *
 * False, and @p out
 *  untouched, with no page, no read or no such port.
 *
 * @param p    The link state.
 * @param port The port.
 * @param out  Receives the port as the page holds it.
 * @return true when out holds the port.
 */
bool prog_link_port(const prog_link_t *p, uint8_t port, prog_link_port_t *out);

/**
 * @brief What the tester drives: the target, and with a port its pin and pulse.
 */
typedef struct {
    uint8_t target;     /**< prog_link_target(): 0, or 1 + n for port n;
                             PROG_LINK_TARGET_UNKNOWN or
                             PROG_LINK_TARGET_NO_LINK                    */
    uint8_t pin;        /**< that port's prog_link_pin(), else
                             PROG_LINK_NO_PIN                            */
    bool    pwm;        /**< that port renders its pulse (prog_link_pwm()) */
} prog_link_drive_t;

/**
 * @brief Whether the screen wants a port the page cannot make the target: one
 *        the IOmcu does not have, one without a pin the PWM generator holds
 *        (none, refused, or FUNCTION NONE), or one an ask waits on.
 *
 * prog_link_wanted() is then 0, so the IOmcu's target is the standard outputs;
 * the tester drives nothing in their place (prog_link_command() takes no
 * command but a release).  False before the page is read and with an IOmcu
 * without the page, whose target is the standard outputs.
 *
 * @param p The link state.
 * @return true when the screen wants a port the page cannot make the target.
 */
bool prog_link_no_drive(const prog_link_t *p);

/**
 * @brief Where the tester's commands go now, for the SERVO screen's TARGET row.
 *
 * PROG_LINK_TARGET_NO_LINK with no IOmcu answering (before the first
 * prog_link_came_up() and after prog_link_lost()), PROG_LINK_TARGET_UNKNOWN
 * while the page is there and not known (not read yet, a read not
 * answered, a write not acknowledged).  The tester sends no command in
 * either: nothing crosses a link that is down, and prog_link_command() is
 * false while the page is not known.  An IOmcu without the page has
 * target 0.
 *
 * While the servo programmer holds the port (prog_link_hold(true)) the
 * target is unchanged: the line keeps naming the target the IOmcu
 * holds, a port or the standard outputs.  The programmer drives the port
 * and the tester sends nothing.
 *
 * Otherwise the target is prog_link_target(), the one the commands use; the
 * pin is the target port's GPIO, or PROG_LINK_NO_PIN with the standard
 * outputs as target or a port without a usable pin (none, refused, or a
 * port the page does not have); pwm is prog_link_pwm(), false with target
 * 0.  A port target without a pin stays a port target: the commands still
 * go to the port and do not reach the SERVO PWM outputs, which an armed
 * bank holds at their rest, the centre between their endpoints.  With
 * prog_link_no_drive() and target 0 the target is the port the screen
 * wants, with no pin and pwm as wanted: the tester sends no command.
 *
 * @param p The link state.
 * @return Where the tester's commands go now.
 */
prog_link_drive_t prog_link_drive(const prog_link_t *p);

/**
 * @brief A channel's configuration as the CHAN_CFG page takes it
 *        (LINK_CC_STRIDE registers) as a port takes it from LINK_PG_SLEW on:
 *        SLEW, MIN_US, MAX_US.
 *
 * @param cc  The channel's CHAN_CFG registers, LINK_CC_STRIDE of them.
 * @param out Receives SLEW, MIN_US and MAX_US.
 */
void prog_link_cfg(const uint16_t *cc, uint16_t *out);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_PROG_LINK_H */
