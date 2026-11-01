/**
 * @file
 * @brief The SUPPLY link page at the IOmcu: a PD mini on a PIO UART, its
 *        wiring, what it is to do and what it last said.
 *
 * The registers are in link_pages.h (LINK_SP_*).
 *
 * The wiring is refused while the output is on, and on pins an output or
 * the board already holds; the pins it takes are reserved from the outputs
 * for as long as it holds them (supply_page_pins()).  A change while a
 * module has answered waits for a state read sent after it
 * (pdmini_check_off()): taken if that read shows the output off, refused
 * if it shows it on or fails.  FLAGS bit 8 says it waits, bit 9 that the
 * last one was refused.  The output switches
 * on only with a live heartbeat from the panel and goes off when that
 * stops, so a panel that dies leaves no supply running.
 *
 * A programming port that holds the page (supply_page_hold()) and writes
 * to the servo's memory (supply_page_port_writes()) keeps its supply
 * through an OFF the panel writes: OUTPUT reads 0 at once, the module and
 * the port's view stay on until the write ends, SUPPLY_PAGE_WRITE_WAIT_MS
 * after the OFF at the latest.  A heartbeat lost or a silent link is not
 * held.  From the panel's OFF until the port lets go of the page, the
 * port's own ON is refused (protocol 4.31, LINK_MINOR_PORT_CUT).
 *
 * Host-tested; the IOmcu is wiring: it runs the PIO UART, feeds the
 * driver and calls supply_page_step() every pass.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "link_pages.h"
#include "outputs.h"
#include "pdmini.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The module's UART Baudrate settings, 0 to 6, and 7 to find it. */
#define SUPPLY_BAUD_COUNT 7u
#define SUPPLY_BAUD_AUTO  7u

/**
 * @brief The baud rate of a UART setting.
 *
 * @param setting UART setting, 0 to SUPPLY_BAUD_COUNT - 1.
 * @return 9600, 19200, 38400, 57600, 115200, 230400 or 460800 for settings 0
 *         to 6; 0 for a setting of 7 or more, including SUPPLY_BAUD_AUTO.
 */
uint32_t supply_page_baud(uint16_t setting);

/** @brief The longest an OFF waits for a programming port's frame to end, in
 *        ms.
 *
 * A frame of the port and its answer are shorter; how long a servo needs
 *  after a frame to store a register is not measured. */
#define SUPPLY_PAGE_OFF_WAIT_MS 12u

/**
 * @brief The longest the panel's OFF of a held supply waits for a programming
 *        port's write to the servo's memory to end, counted from the OFF,
 *        in ms.
 *
 * The longest writes the code allows, without a fault: a KST write of
 * KST_PLAN_MAX_STEPS (33) steps, 258 read frames of 5.9ms and 33 write
 * frames of 12.4ms, 1.93s on the wire after its planning; a classic Hitec
 * write of HITEC_PLAN_MAX (56) steps, 2.9s with its read-back at 32ms a
 * step on the panel's host loopback; a D series write of every register
 * of the full read with its save and its 1000ms reboot wait, 3.5s.  None
 * is measured on hardware.  A write that takes longer is cut at the bound.
 */
#define SUPPLY_PAGE_WRITE_WAIT_MS 10000u

/** @brief Where a wiring change that waits for a state read is. */
typedef enum {
    SUPPLY_WIRE_IDLE = 0,  /**< none waits                              */
    SUPPLY_WIRE_WAIT,      /**< waits for the driver's state read        */
    SUPPLY_WIRE_READY,     /**< the read showed off: to be written now   */
} supply_wire_t;

/** @brief The SUPPLY page state: registers, and the wiring change in hand. */
typedef struct {
    uint16_t regs[LINK_SP_COUNT];  /**< the page's registers, LINK_SP_* */
    bool     commanded;   /**< set points written since init          */
    bool     answered;    /**< the driver's module has answered once  */
    uint8_t  wire;        /**< supply_wire_t                          */
    bool     wire_ask;    /**< the state read not yet asked of the driver */
    bool     wire_refused; /**< the last change waited and was refused */
    uint16_t wire_next[4]; /**< the change waiting, ENABLE to BAUD    */
    bool     reset_owed;  /**< RESET written, not yet passed on       */
    uint8_t  scan;        /**< the rate being tried under AUTO, 0..6   */
    uint16_t scan_seen;   /**< the driver's who_failed when last looked */
    bool     held;        /**< a programming port holds the command   */
    bool     frame_out;   /**< the port has a frame on the wire        */
    bool     was_on;      /**< OUTPUT was 1 at the last step           */
    bool     off_wait;    /**< an OFF waits for the frame's end        */
    uint32_t now_ms;      /**< the clock of supply_page_port_frame()   */
    uint32_t off_t0;      /**< when the waiting OFF was asked          */
    bool     writing;     /**< the port writes to the servo's memory   */
    bool     off_held;    /**< the panel's OFF waits for the write     */
    bool     sticky;      /**< the panel's OFF stands: no port ON      */
    bool     wire_frozen; /**< supply_page_wire_freeze()               */
    uint32_t held_t0;     /**< when the held OFF was written           */
} supply_page_t;

/** @brief The supply as a programming port sees it (kst_port.h). */
typedef struct {
    bool     online;    /**< wired, a PD mini answers, no wiring change
                             waits */
    bool     asked_on;  /**< OUTPUT is 1, or the panel's OFF is held for
                             the port's write */
    bool     on;        /**< asked on and read on, at a voltage no higher
                             than asked, with nothing stuck */
    bool     off;       /**< read off, and not maybe on */
    uint16_t set_mv;    /**< SET_MV and SET_MA as asked */
    uint16_t set_ma;                    /**< the current limit asked for, mA */
    uint16_t vin_mv;    /**< the PD mini's input, 0 while not read */
} supply_port_view_t;

/**
 * @brief Clears the page to its start: BAUD 1 (19200), BAUD_FOUND AUTO, the
 *        scan at 1.
 *
 * Nothing is done when @p p is NULL.
 *
 * @param p The page state, overwritten.
 */
void supply_page_init(supply_page_t *p);

/**
 * @brief The rate the UART is to run at now, 0 to 6.
 *
 * The setting itself, or under AUTO the one being tried: the next after every
 * WHO_AM_I @p drv reports without a valid answer, until a module has answered
 * once, from when the rate holds.  Publishes it in BAUD_FOUND -- 7 while AUTO
 * has found none.
 * @p drv NULL leaves the scan where it is.
 *
 * @param p The page state. BAUD_FOUND is updated.
 * @param drv The PD mini driver, or NULL.
 * @return The setting, 0 to 6. 1 for a NULL @p p.
 */
uint8_t supply_page_rate(supply_page_t *p, const pdmini_t *drv);

/**
 * @brief The baud the UART is to be opened at now.
 *
 * @param p The page state.
 * @return supply_page_baud() of the rate from supply_page_rate()'s rules, or 0
 *         for NULL.
 */
uint32_t supply_page_uart_baud(const supply_page_t *p);

/**
 * @brief A write, validated whole before any of it is stored.
 *
 * Refused: off the page (BAD_RANGE); a read-only register (READ_ONLY); a wiring
 * change while the output is asked on or may be on, a pin past the bank,
 * reserved, bound to an output or the other pin, a baud setting past 7 (AUTO),
 * an ON without the supply enabled or without its set points in the same frame,
 * a set point above the module's range, an ON while a wiring change waits, a
 * wiring change that waits in the same frame as OUTPUT to SET_MA (BAD_VALUE);
 * an ON without a live heartbeat (NOT_ARMED).
 *
 * A wiring change that passes, while the driver's module has answered
 * (supply_page_follow()), is acknowledged and not yet taken: it waits
 * (SUPPLY_WIRE_WAIT, FLAGS bit 8) for a state read the next
 * supply_page_step() asks of the driver.  Another change replaces it and
 * is read for again; a write of the wiring in force drops it.
 *
 * @param p The page state, changed only when the write is taken.
 * @param off First register, 0 to LINK_SP_COUNT - 1.
 * @param n Number of registers to write. 0 is taken with no change.
 * @param in The @p n new register values.
 * @param o The outputs bank, for the pins a wiring change takes.
 * @param beat_alive Whether the panel's heartbeat is live.
 * @return 0 when taken, or when the write waits for a state read. The NACK
 *         reason otherwise: LINK_NACK_BAD_RANGE, LINK_NACK_READ_ONLY,
 *         LINK_NACK_BAD_VALUE or LINK_NACK_NOT_ARMED, as listed above.
 */
uint8_t supply_page_write(supply_page_t *p, uint8_t off, uint8_t n,
                          const uint16_t *in, const outputs_t *o,
                          bool beat_alive);

/**
 * @brief What the driver says now, straight before a write is judged: FLAGS bit
 *        6 (the output on or maybe on) and whether its module has answered.
 *
 * @p drv NULL for no driver: nothing answered, nothing on.  supply_page_step()
 * does the same every pass.
 *
 * @param p The page state, updated.
 * @param drv The PD mini driver, or NULL.
 */
void supply_page_follow(supply_page_t *p, const pdmini_t *drv);

/**
 * @brief The wiring change whose state read showed the output off, into @p regs
 *        (ENABLE to BAUD): true while one is SUPPLY_WIRE_READY.
 *
 * The caller writes it with supply_page_wire_write() in the same pass.
 *
 * @param p The page state.
 * @param regs Receives the four wiring registers, ENABLE to BAUD. Not written
 *        when false is returned.
 * @return true when a change is ready and @p regs is filled, false otherwise
 *         or for NULL.
 */
bool supply_page_wire_ready(const supply_page_t *p, uint16_t *regs);

/**
 * @brief The ready wiring written: judged whole as a link write is, against the
 *        driver's state as supply_page_follow() last took it, without waiting
 *        again.  0, or the NACK reason, which refuses it (FLAGS bit 9).
 *
 * @param p The page state.
 * @param o The outputs bank, for the pins.
 * @return 0 when written. LINK_NACK_BAD_VALUE when no change is ready, or the
 *         refusal of the page write otherwise.
 */
uint8_t supply_page_wire_write(supply_page_t *p, const outputs_t *o);

/**
 * @brief The ready wiring could not be put in force after it was written (no
 *        UART for its pins): refused, FLAGS bit 9, as a refused read is.
 *
 * @param p The page state. Nothing is done when NULL.
 */
void supply_page_wire_refuse(supply_page_t *p);

/**
 * @brief LINK_NACK_BAD_VALUE for an OUTPUTS page (@p slots, LINK_OS_COUNT
 *        registers) that binds a slot to a pin the supply holds; 0 otherwise.
 *
 * Checked before the page is stored, so no binding is kept that the bank would
 * leave unbound -- and that a restart, with the supply not yet
 *  wired, would drive on the module's pin.
 *
 * @param p The page state.
 * @param slots The OUTPUTS page, LINK_OS_COUNT registers.
 * @return LINK_NACK_BAD_VALUE for a NULL @p slots or a slot with a driver on a
 *         pin in supply_page_pins(), 0 otherwise.
 */
uint8_t supply_page_slots_check(const supply_page_t *p, const uint16_t *slots);

/**
 * @brief Copies registers of the SUPPLY page.
 *
 * Nothing is done when @p p or @p out is NULL, or when off and n reach past
 * LINK_SP_COUNT.
 *
 * @param p The page state, not changed.
 * @param off First register, 0 to LINK_SP_COUNT - 1.
 * @param n Number of registers to copy.
 * @param out Receives the @p n register values.
 */
void supply_page_read(const supply_page_t *p, uint8_t off, uint8_t n,
                      uint16_t *out);

/**
 * @brief Whether the page drives a supply, and on which pins.
 *
 * @param p The page state.
 * @return true when ENABLE is not 0, false otherwise or for NULL.
 */
bool supply_page_enabled(const supply_page_t *p);

/**
 * @brief The TX pin register, as a GPIO number.
 *
 * @param p The page state.
 * @return The pin, 0 for NULL.
 */
uint8_t supply_page_tx(const supply_page_t *p);

/**
 * @brief The RX pin register, as a GPIO number.
 *
 * @param p The page state.
 * @return The pin, 0 for NULL.
 */
uint8_t supply_page_rx(const supply_page_t *p);

/**
 * @brief The pins it holds, as a reservation mask; 0 while not enabled.
 *
 * @param p The page state.
 * @return Bits for the TX and RX pins, 0 when not enabled or for NULL.
 */
uint64_t supply_page_pins(const supply_page_t *p);

/**
 * @brief The longest the IOmcu's start-up waits for the PD mini to read off
 *        before anything else starts, in ms.
 *
 * Seven rates under AUTO, each WHO_AM_I that nothing answers 415ms with
 * pdmini_hurry(), are 2.9s; the module found at the last of them is read
 * and switched off, and its OFF read back, about 0.35s later.  3500ms
 * leaves 0.25s more.  A module that does not answer delays the link by this
 * much at every start.
 */
#define SUPPLY_PAGE_BOOT_OFF_MS 3500u

/**
 * @brief Whether the start-up still waits for the PD mini: a module is wired,
 *        its output may be on (pdmini_may_be_on()), and @p elapsed_ms is
 *        under SUPPLY_PAGE_BOOT_OFF_MS.
 *
 * @param p          The page state.
 * @param drv        The PD mini driver, or NULL while none is attached.
 * @param elapsed_ms Milliseconds since the wait began.
 * @return true while the start-up waits; false for a NULL @p p or @p drv,
 *         a page not enabled, an output read off and the bound reached.
 */
bool supply_page_boot_owed(const supply_page_t *p, const pdmini_t *drv,
                           uint32_t elapsed_ms);

/**
 * @brief One pass: the output off without a heartbeat, what the driver is to
 *        do, and what it last said into the read-only registers.
 *
 * @p drv may be NULL while no supply is wired.  A wiring change waiting asks
 * the driver for its state read and follows the answer: off makes it
 * SUPPLY_WIRE_READY, anything else refuses it (FLAGS bit 9).  With no driver it
 * is ready: nothing is attached to the module to switch it off.
 *
 * @param p The page state. Nothing is done when NULL.
 * @param beat_alive Whether the panel's heartbeat is live.
 * @param drv The PD mini driver, or NULL.
 */
void supply_page_step(supply_page_t *p, bool beat_alive, pdmini_t *drv);

/**
 * @brief The page as of the last supply_page_step(), for a programming port.
 *
 * @param p The page state.
 * @param out Receives the view. Zeroed first; nothing else is done when @p p
 *        is NULL. Nothing is done when @p out is NULL.
 */
void supply_page_port_view(const supply_page_t *p, supply_port_view_t *out);

/**
 * @brief A programming port's request, in place of a link write of OUTPUT to
 *        SET_MA.
 *
 * Off is always taken and leaves the set points; it ends a held OFF of the
 * panel's, which reaches the module with it.  On is refused (false, nothing
 * changed) without a live heartbeat, while the supply is not wired or a wiring
 * change waits, for set points outside the module's range, for a voltage
 * above SET_MV while OUTPUT is 1 (the voltage of an output that is on is not
 * raised here), and from the panel's OFF of a held page until the port lets
 * go of it (supply_page_hold()).
 *
 * @param p The page state.
 * @param on true for ON, false for OFF.
 * @param mv Set voltage in millivolts, for ON.
 * @param ma Set current in milliamps, for ON.
 * @param beat_alive Whether the panel's heartbeat is live.
 * @return true when taken. false for NULL, an ON that is refused (see above),
 *         otherwise true for an OFF.
 */
bool supply_page_port_set(supply_page_t *p, bool on, uint16_t mv,
                          uint16_t ma, bool beat_alive);

/**
 * @brief Refuse a write of ENABLE to BAUD that changes them with LINK_NACK_BUSY:
 *        the ESC_WIRE page holds a pin, and an attach could take the PIO room its
 *        output needs back.  The wiring in force is taken, with none waiting: a
 *        panel that restarts in a hold writes it, and takes a refusal as its pins'.
 *
 * @param p      The page state, or NULL.
 * @param frozen True while the page holds a pin.
 */
void supply_page_wire_freeze(supply_page_t *p, bool frozen);

/**
 * @brief Whether a programming port holds the command registers (FLAGS bit 11,
 *        LINK_SP_PORT).
 *
 * While it does, supply_page_write() takes an OFF, which leaves SET_MV and
 * SET_MA as they are whatever the write names, and refuses every other write of
 * OUTPUT to SET_MA that changes one of the three with LINK_NACK_BAD_VALUE: the
 * panel can switch the supply off, and cannot switch it on or move a set point
 * under the servo.  That OFF stands until the port lets go: the port's ON is
 * refused (supply_page_port_set()), and while the port writes the OFF waits
 * for the write (supply_page_port_writes()).  Letting go ends both.
 *
 * @param p The page state. Nothing is done when NULL.
 * @param held true while the programming port holds the registers.
 */
void supply_page_hold(supply_page_t *p, bool held);

/**
 * @brief Whether a programming port has a frame on the wire, and the clock,
 *        once a pass before supply_page_step().
 *
 * An OFF taken while one is -- written by the panel or forced by a lost
 * heartbeat -- reaches the driver when the frame has ended,
 * SUPPLY_PAGE_OFF_WAIT_MS after it was asked at the latest.  OUTPUT reads 0
 * from the moment it is asked.
 *
 * @param p The page state. Nothing is done when NULL.
 * @param frame_out true while a frame is on the wire.
 * @param now_ms Current time in milliseconds.
 */
void supply_page_port_frame(supply_page_t *p, bool frame_out,
                            uint32_t now_ms);

/**
 * @brief Whether a programming port writes to the servo's memory now, once a
 *        pass before supply_page_step() and after the ports' own steps, and
 *        after each command of a port's page.
 *
 * Told after a command too, so a SUPPLY OFF taken in the same batch of
 * frames as the command that starts a write is judged against that write.
 *
 * An OFF the panel writes while a port holds the page and this says true is
 * held back from the module: OUTPUT reads 0, the driver and the port's view
 * (supply_page_port_view()) stay on.  The held OFF reaches the module at the
 * first step that finds no write running, no port holding the page, or
 * SUPPLY_PAGE_WRITE_WAIT_MS gone since the OFF on the clock of
 * supply_page_port_frame(), across its wrap at 2^32ms; and at once with a
 * heartbeat lost or a silent link, or the port's own OFF.  The frame wait
 * (SUPPLY_PAGE_OFF_WAIT_MS) follows it as for any OFF.
 *
 * @param p       The page state. Nothing is done when NULL.
 * @param writing true while a port's write to the servo's memory runs
 *                (kst_port_writing(), hitec_port_writing()).
 */
void supply_page_port_writes(supply_page_t *p, bool writing);

/**
 * @brief Whether the panel's OFF waits for a port's write now.
 *
 * @param p The page state.
 * @return true while a held OFF has not reached the module; false for NULL.
 */
bool supply_page_off_held(const supply_page_t *p);

#ifdef __cplusplus
}
#endif
