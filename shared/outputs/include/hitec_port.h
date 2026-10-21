/**
 * @file hitec_port.h
 * @brief Raw transactions of the Hitec servo wire on a programming port, and
 *        their link page (0x34).
 *
 * Implements the signal-wire programming protocols of Hitec servos.  Hitec
 * is a name or trademark of its owner; it is used here only to say which
 * servos this module works with (README.md, "Legal notice and disclaimer").
 *
 * The port holds no session.  It takes the pin of one programming port
 * (prog_port.h) from the PWM (pulse-width modulation) generator, opens the
 * wire driver of protocols/hitec on it, and runs one transaction at a time
 * as the page's writer stages it: SEND, LISTEN, LEVEL, or POWER, a supply
 * cycle with a listen from the ON.  The capture and its decoding are on
 * the page until the next transaction.  Which bytes a servo takes, and in
 * which order, is the writer's business.
 *
 * A port is free, or held by this page, or held by the KST page, never two
 * of them.  OPEN is refused with LINK_HITEC_REF_HELD while the KST page
 * holds the port (hitec_port_other_mask()), and the KST page refuses ENTER
 * and a REBOOT from PWM with LINK_KST_REF_BUSY while this one holds it.
 *
 * The way in, started by OPEN:
 *
 *   STOPPING  HITEC_PORT_STOP_MS: the port renders no pulse, so a pulse
 *             that runs ends and no other starts.
 *   OPEN      the generator gives up the pin and the driver opens the line
 *             on it, released: classic with its pull-down, the D series
 *             with its pull-up.  A driver that gets no state machine,
 *             program space or DMA (direct memory access) channel leaves
 *             the port free with LINK_HITEC_RESULT_NO_LINE.
 *
 * In OPEN a command starts one transaction (BUSY) and the port is in OPEN
 * again when the driver hands back its capture.  The driver ends every
 * transaction itself (protocols/hitec/rp2350/hitec_line.h); one that runs
 * HITEC_PORT_BUSY_MS is aborted here as well.
 *
 * POWER needs a PD mini that answers.  It asks the supply off, waits until
 * it reads off (HITEC_PORT_SUPPLY_MS at most, else
 * LINK_HITEC_RESULT_SUPPLY), keeps it off for the staged off time, asks it
 * on at the staged LINK_HITEC_SUPPLY_MIN_MV to LINK_HITEC_SUPPLY_MAX_MV
 * (4800 to 6000 mV), and starts the staged listen in the same pass.  A
 * supply that refuses the ON gives LINK_HITEC_RESULT_SUPPLY; a listen the
 * driver refuses gives LINK_HITEC_RESULT_START with the supply on.  From
 * the OFF to the end of the listen the SUPPLY page is held
 * (hitec_port_supply_hold()): the panel can switch the supply off and
 * cannot switch it on or move a set point.
 *
 * The way out is CLOSE, from any state but free: a running transaction is
 * cut, the line closed and the pin the generator's.  The supply is left as
 * it is.  ABORT cuts what runs and keeps the port.
 *
 * A stop is the heartbeat lost, the link silent or the bank disarmed.  On a
 * stop the running transaction is aborted (the pad released at once); a
 * POWER in its off time ends there with the supply off, and one whose
 * listen runs is cut as a transaction, the supply as the SUPPLY page has
 * it.  The port stays held with
 * the pin an input; a port still in STOPPING is free again.  The flag
 * LINK_HT_F_STOPPED is set, and SEND, LISTEN, LEVEL and POWER are refused
 * with LINK_HITEC_REF_STOPPED until CLEAR, which is taken while the
 * heartbeat is trusted and the link is not silent, or until CLOSE and
 * OPEN.  While the heartbeat is not trusted or the link is silent every
 * command but ABORT and CLOSE is refused with LINK_HITEC_REF_UNSAFE, and
 * every pass counts as a stop.
 *
 * Every SEND, and a LISTEN or a POWER that answers the first byte, needs
 * LINK_HITEC_WRITE_KEY in the command.  OPEN is refused while the bank is
 * armed; the caller arms no output while hitec_port_quiet_mask() is not 0.
 *
 * Commands: registers 0 to 3 of a write, one frame.  SEQ is LINK_HT_SEQ
 * plus 1; the frame just taken, sent again, is acknowledged and does
 * nothing; any other SEQ is refused with LINK_NACK_BAD_VALUE.  A command
 * that is taken is acknowledged, and what became of it is on the page:
 * LINK_HT_RESULT's high byte says why it started nothing, or 0.
 *
 * Receive mode (4.24).  Write register 23 (LINK_HT_W_RX_MODE) says how a
 * D series line receives from the next OPEN on: PULL-UP, released for the
 * answer with a pull-up on the board, or PRECHARGE, the IOmcu's sampler
 * (protocols/hitec/include/hitec_pre.h).  The OPEN takes the register's
 * value with the family; a classic OPEN ignores it.  On a PRECHARGE line a
 * LISTEN or POWER that answers and a receive window above
 * HITEC_PRE_WINDOW_MAX_US are refused with LINK_HITEC_REF_RANGE.  A
 * PRECHARGE transaction started with the supply asked on is cut when the
 * supply is no longer asked on, the PD mini stops answering, or the output
 * reads off after it read on.  A SEND, LISTEN or LEVEL counts as read on
 * from its start.  POWER's listen starts in the pass of the ON and counts
 * from the first pass the output does not read off; one whose output never
 * leaves off runs to the end of its window (HITEC_PRE_WINDOW_MAX_US at
 * most) and ends LINK_HITEC_RESULT_DONE with what it heard.
 *
 * Nothing here is kept over a restart of the IOmcu: after one the port is
 * free and the register reads PULL-UP.
 *
 * Pure logic: the pin, the driver and the supply are behind
 * hitec_port_hw_t.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_HITEC_PORT_H
#define RCBENCH_HITEC_PORT_H

#include <stdbool.h>
#include <stdint.h>

#include "hitec_wire.h"
#include "link_pages.h"
#include "outputs.h"
#include "supply_page.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The port renders no pulse this long before the generator gives up the
 *  pin: 2 frames at 40 Hz, the lowest PWM rate. */
#define HITEC_PORT_STOP_MS 50u
/** @brief A transaction still running this long is aborted: longer than the
 *        longest one the driver takes, a 4 s lead and a 4 s window with 16
 *  bytes. */
#define HITEC_PORT_BUSY_MS 10000u
/** @brief The PD mini has this long to read off after a POWER asked it off. */
#define HITEC_PORT_SUPPLY_MS LINK_HITEC_SUPPLY_MS

/** @brief What the driver says about the line and the running transaction. */
typedef struct {
    uint32_t lead_ns;   /**< the lead's first edge to the release: the
                             lead and every bit time sent */
    uint32_t span_us;   /**< the longest the run can take */
    uint32_t bit_ns;    /**< the bit time of the open line */
} hitec_port_run_t;

/** @brief What the port needs from the hardware. */
typedef struct {
    /** The caller's context, passed as the first argument of each hook. */
    void *ctx;
    /** Programming ports the build has, 1 to LINK_PG_PORTS_MAX. */
    uint8_t ports;
    /** The pin the PWM generator has for @p port, or a value above
     *  OUT_MAX_PIN while it has none. */
    uint8_t (*pin)(void *ctx, uint8_t port);
    /** Take @p pin of @p port from the generator and open the wire driver
     *  on it for @p family, @p drive and the receive mode @p rx
     *  (hitec_rx_mode_t; PULL-UP for classic), released.  False when the
     *  driver gets no resources; give() returns the pin. */
    bool (*take)(void *ctx, uint8_t port, uint8_t pin, uint8_t family,
                 uint8_t drive, uint8_t rx);
    /** Close the wire driver and give the pin back to the generator. */
    void (*give)(void *ctx);
    /** Cut the running transaction: the next poll() hands back what came,
     *  marked aborted. */
    void (*abort)(void *ctx);
    /** What the driver says about the line; NULL: nothing. */
    void (*run)(void *ctx, hitec_port_run_t *out);
    /** What the supply says now (supply_page_port_view()).  NULL, with
     *  supply_set: the build has no supply to switch. */
    void (*supply)(void *ctx, supply_port_view_t *out);
    /** Ask the supply on at @p mv and @p ma, or off.  False when it does
     *  not take the request. */
    bool (*supply_set)(void *ctx, bool on, uint16_t mv, uint16_t ma);
    /** The wire driver.  now_us() works while the driver is closed. */
    hitec_driver_t driver;
} hitec_port_hw_t;

/**
 * @brief The state of one Hitec port: the hardware, the staged registers,
 *        the running or last transaction and its capture.
 */
typedef struct {
    hitec_port_hw_t hw;
    uint16_t staged[LINK_HT_W_STAGED];
    uint16_t last_cmd[LINK_HT_W_CMD_FRAME];
    hitec_xfer_t xfer;      /**< the running transaction, or the last */
    hitec_capture_t cap;    /**< the last capture */
    hitec_reply_t reply;    /**< its decoding */
    hitec_port_run_t info;  /**< the driver's word at the last start */
    uint32_t t0_ms;
    uint32_t t1_ms;
    uint32_t start_us;
    uint32_t cap_us;        /**< from the start to the capture */
    uint16_t seq;
    uint16_t count;         /**< transactions since the port was taken */
    uint16_t mv;            /**< the running POWER's set points */
    uint16_t ma;
    uint16_t off_ms;
    uint8_t  state;         /**< link_hitec_state_t */
    uint8_t  port;
    uint8_t  pin;
    uint8_t  family;
    uint8_t  drive;
    uint8_t  op;            /**< link_hitec_op_t of the last operation */
    uint8_t  result;        /**< link_hitec_result_t of that operation */
    uint8_t  refusal;       /**< link_hitec_refusal_t of the last command */
    uint8_t  rx;            /**< hitec_rx_t of the last decoding */
    uint8_t  dec;           /**< link_hitec_dec_t of the last transaction */
    uint8_t  dec_arg;
    uint8_t  view;
    uint8_t  safe;
    uint8_t  armed;
    uint8_t  other;         /**< ports the KST page holds */
    uint8_t  has_pin;
    uint8_t  has_cap;
    uint8_t  stopped;
    uint8_t  keyed;         /**< the last transaction puts a byte out */
    uint8_t  cutting;       /**< the driver was told to abort */
    uint8_t  off_seen;      /**< POWER: the supply read off */
    uint8_t  rx_mode;       /**< LINK_HT_W_RX_MODE as written */
    uint8_t  rx_line;       /**< the receive mode of the line, from its
                                 OPEN */
    uint8_t  supply_on;     /**< the supply was asked on at the start */
    uint8_t  read_on;       /**< the output read on (not off) since the
                                 start; set at the start but for POWER */
} hitec_port_t;

/** @brief Prepare a free port.
 *
 * False when @p hw names no port or lacks a function other than run and the
 * supply's two, or has one of those
 *  two.
 *
 * @param p  The port to prepare; zeroed first.
 * @param hw The hardware hooks, copied into the port.
 * @return true when the port is prepared; false for a NULL p, or for a hw that
 *         fails the checks above.
 */
bool hitec_port_init(hitec_port_t *p, const hitec_port_hw_t *hw);

/**
 * @brief A write of the page: 0, or the LINK_NACK_* reason with nothing
 *        changed.
 *
 * A window that starts in registers 0 to 3 is a command and has to be exactly
 * those 4.  Registers 4 to 23 are taken in any windows; 24 and up are
 * LINK_NACK_READ_ONLY.  A VIEW or an RX_MODE above the last is
 * LINK_NACK_BAD_VALUE.
 *
 * @param p      The port.
 * @param off    First register of the window.
 * @param n      Number of registers in the window; 0 is refused.
 * @param in     The n register values to write.
 * @param now_ms Current time in milliseconds.
 * @return 0 when the write is applied or the command is acknowledged; otherwise
 *         LINK_NACK_BAD_RANGE, LINK_NACK_READ_ONLY or LINK_NACK_BAD_VALUE.
 */
uint8_t hitec_port_write(hitec_port_t *p, uint8_t off, uint8_t n,
                         const uint16_t *in, uint32_t now_ms);

/**
 * @brief A read of the page, LINK_HT_*.
 *
 * @param p   The port.
 * @param off First register of the window.
 * @param n   Number of registers to read.
 * @param out Receives n register values; left unchanged for a NULL p or out, or
 *            a window past LINK_HT_COUNT.
 */
void hitec_port_read(const hitec_port_t *p, uint8_t off, uint8_t n,
                     uint16_t *out);

/**
 * @brief One pass.
 *
 * @p safe: the heartbeat is trusted and the link is not silent.
 * @p stop: the bank was switched off or disarmed since the last pass.
 *
 * @param p      The port.
 * @param safe   true when the heartbeat is trusted and the link is not silent.
 * @param stop   true when the bank was switched off or disarmed since the last
 *               pass.
 * @param now_ms Current time in milliseconds.
 */
void hitec_port_step(hitec_port_t *p, bool safe, bool stop, uint32_t now_ms);

/**
 * @brief Where the port is.
 *
 * @param p The port, or NULL.
 * @return The state of the port; LINK_HITEC_FREE for a NULL p.
 */
link_hitec_state_t hitec_port_state(const hitec_port_t *p);

/** @brief Bit n: programming port n renders no PWM pulse.
 *
 * The held port in every
 *  state but LINK_HITEC_FREE.
 *
 * @param p The port, or NULL.
 * @return Bit n set for port n while the port is held and not free; 0 for a
 *         NULL p.
 */
uint8_t hitec_port_quiet_mask(const hitec_port_t *p);

/** @brief Bit n: programming port n's pin is the wire driver's.
 *
 * From the take
 *  until CLOSE.
 *
 * @param p The port, or NULL.
 * @return Bit n set for port n while the wire driver holds its pin; 0 for a
 *         NULL p.
 */
uint8_t hitec_port_pin_mask(const hitec_port_t *p);

/**
 * @brief Whether the bank is armed: OPEN is refused while it is.
 *
 * @param p     The port, or NULL; a NULL p is ignored.
 * @param armed true when the bank is armed.
 */
void hitec_port_bench_armed(hitec_port_t *p, bool armed);

/** @brief Bit n: the KST page holds programming port n (kst_port_quiet_mask()).
 *  OPEN on it is refused with LINK_HITEC_REF_HELD.
 *
 * @param p    The port, or NULL; a NULL p is ignored.
 * @param mask Bit n: the KST page holds programming port n.
 */
void hitec_port_other_mask(hitec_port_t *p, uint8_t mask);

/**
 * @brief A POWER runs: the SUPPLY page is held (supply_page_hold()).
 *
 * @param p The port, or NULL.
 * @return true while a POWER runs, from the OFF to the end of its listen; false
 *         for a NULL p.
 */
bool hitec_port_supply_hold(const hitec_port_t *p);

/** @brief A byte may be on the wire: a SEND from its start until the lead and
 *        the bytes have had their time, counted in whole ms; a LISTEN or a
 *        POWER
 *  that answers, while it runs.
 *
 * @param p      The port, or NULL.
 * @param now_ms Current time in milliseconds.
 * @return true while a byte may be on the wire; false for a NULL p or a port
 *         that is not BUSY.
 */
bool hitec_port_frame_out(const hitec_port_t *p, uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_HITEC_PORT_H */
