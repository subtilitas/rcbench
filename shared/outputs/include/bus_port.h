/**
 * @file bus_port.h
 * @brief A serial bus servo protocol on a programming port, and its link
 *        page (BUS, 0x34).
 *
 * The port takes the pin of one programming port (prog_port.h) from the
 * PWM (pulse-width modulation) generator, hands it to the bus driver and
 * runs a session of the protocol on it.  Protocol 0 is Feetech SCS and STS
 * (protocols/feetech); the page names the protocol in every command so
 * that another one plugs in as another number.  A port is in PWM or
 * carries the bus, never both, and the KST page and this page never hold
 * one pin at once.
 *
 * The way out of PWM, started by the operation OPEN:
 *
 *   OPENING  BUS_PORT_STOP_MS: the port renders no pulse, so a pulse that
 *            runs ends and no other starts.  Then the generator gives up
 *            the pin and the driver takes it at the baud index of the
 *            command: an input with its pull-up, the line high.
 *   OPEN     the session runs.
 *
 * The way back, started by CLOSE, by a stop or by an unsafe link:
 *
 *   CLOSING  the session's stop: a frame on the wire is finished, then
 *            torque off (register 40 := 0) by broadcast, and the lock flag
 *            1 to a servo an EEPROM write left at 0.  Then the driver gives
 *            the pin back and the port is CLOSED.
 *
 * OPEN is refused while the bank is armed (LINK_BUS_REF_ARMED), and the
 * caller arms nothing while bus_port_quiet_mask() is not 0: bus traffic
 * runs on a SAFE bench only, as KST programming does.  A stop is the
 * heartbeat lost, the link silent or the bank switched off; the port then
 * closes.  While the heartbeat is not trusted or the link is silent every
 * command is refused with LINK_BUS_REF_UNSAFE; the closing frames go out
 * all the same.  The operation STOP, which the panel sends for its STOP
 * button, ends the operation and the watch and sends torque off, and the
 * bus stays open.
 *
 * The servo's supply is not switched here: the user sets it on the SUPPLY
 * screen, or powers the servo from another source.
 *
 * An EEPROM write is refused unless the staged registers carry
 * LINK_BUS_WRITE_KEY.  A WRITE takes RAM addresses only and never the lock
 * flag; an EEPROM write takes the EEPROM area or the lock flag alone.  The
 * staged registers of a WRITE and an EEPROM write are taken only with the
 * CRC the command names.
 *
 * Commands: registers 0 to 3 of a write, one frame.  SEQ is LINK_BS_SEQ
 * plus 1; the frame just taken, sent again, is acknowledged and does
 * nothing; any other SEQ is refused with LINK_NACK_BAD_VALUE.  A command
 * that is taken is acknowledged, and what became of it is on the page:
 * LINK_BS_RESULT's high byte says why it started nothing, or 0.
 *
 * Nothing here is kept over a restart of the coprocessor: after one the
 * port is CLOSED, whatever the servos do.
 *
 * Pure logic: the pin and the driver are behind bus_port_hw_t.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_BUS_PORT_H
#define RCBENCH_BUS_PORT_H

#include <stdbool.h>
#include <stdint.h>

#include "feetech_bus.h"
#include "link_pages.h"
#include "outputs.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The port renders no pulse this long before the generator gives up the
 *  pin: 2 frames at 40 Hz, the lowest PWM rate.  As KST_PORT_STOP_MS. */
#define BUS_PORT_STOP_MS 50u

/** What the port needs from the hardware. */
typedef struct {
    void *ctx;
    /** Programming ports the build has, 1 to LINK_PG_PORTS_MAX. */
    uint8_t ports;
    /** The pin the PWM generator has for @p port, or a value above
     *  OUT_MAX_PIN while it has none. */
    uint8_t (*pin)(void *ctx, uint8_t port);
    /** Whether another user of programming ports (the KST page) holds
     *  @p port's pin or is out of PWM. */
    bool (*held)(void *ctx, uint8_t port);
    /** Take @p pin of @p port from the generator and open the bus driver
     *  on it at @p baud.  False when the driver gets no resources; give()
     *  returns the pin. */
    bool (*take)(void *ctx, uint8_t port, uint8_t pin, uint32_t baud);
    /** Close the bus driver and give the pin back to the generator. */
    void (*give)(void *ctx);
    /** The bus driver. */
    feetech_driver_t driver;
} bus_port_hw_t;

typedef struct {
    bus_port_hw_t hw;
    feetech_bus_t bus;
    uint16_t staged[LINK_BS_W_STAGED];
    uint16_t last_cmd[LINK_BS_W_CMD_FRAME];
    uint32_t t0_us;      /**< OPENING: when it began */
    uint16_t seq;
    uint8_t  state;      /**< link_bus_state_t */
    uint8_t  port;
    uint8_t  pin;
    uint8_t  proto;
    uint8_t  baud_idx;
    uint8_t  op;         /**< link_bus_op_t of the last operation started */
    uint8_t  result;     /**< feetech_res_t of that operation */
    uint8_t  refusal;    /**< link_bus_refusal_t of the last command */
    uint8_t  running;    /**< the operation is the session's and runs */
    uint8_t  has_pin;    /**< the pin is the bus driver's */
    uint8_t  safe;
    uint8_t  armed;
    uint8_t  at;         /**< the address of the last read or EEPROM write */
    uint8_t  timed;      /**< t0_us is set */
    uint8_t  baud_due;   /**< BAUD taken while a watch read was on the
                              wire: its index + 1, set when the read ends */
} bus_port_t;

/** Prepare a port, CLOSED.  False when @p hw names no port or lacks a
 *  function. */
bool bus_port_init(bus_port_t *p, const bus_port_hw_t *hw);

/**
 * A write of the page: 0, or the LINK_NACK_* reason with nothing changed.
 * A window that starts in registers 0 to 3 is a command and has to be
 * exactly those 4.  Registers 4 to 13 are taken in any windows; 14 and up
 * are LINK_NACK_READ_ONLY.
 */
uint8_t bus_port_write(bus_port_t *p, uint8_t off, uint8_t n,
                       const uint16_t *in);

/** A read of the page, LINK_BS_*. */
void bus_port_read(const bus_port_t *p, uint8_t off, uint8_t n,
                   uint16_t *out);
/** A read of the BUS_IDS page, LINK_BI_*. */
void bus_port_read_ids(const bus_port_t *p, uint8_t off, uint8_t n,
                       uint16_t *out);

/**
 * One pass.  @p safe: the heartbeat is trusted and the link is not silent.
 * @p stop: the bank was switched off since the last pass.  A pass every
 * 1 ms or faster keeps the session's timing.
 */
void bus_port_step(bus_port_t *p, bool safe, bool stop, uint32_t now_us);

/** Where the port is. */
link_bus_state_t bus_port_state(const bus_port_t *p);

/** Bit n: programming port n renders no PWM pulse.  The held port in every
 *  state but CLOSED. */
uint8_t bus_port_quiet_mask(const bus_port_t *p);

/** Whether a CONTROL write of @p n registers from @p off is refused: it
 *  carries an ARM other than 0 while the bus is not closed.  The caller
 *  answers LINK_NACK_NOT_ARMED and stores nothing of the frame, so no ARM
 *  waits in the page for the bus to close. */
bool bus_port_refuses_arm(const bus_port_t *p, uint8_t off, uint8_t n,
                          const uint16_t *in);

/** Bit n: programming port n's pin is the bus driver's. */
uint8_t bus_port_pin_mask(const bus_port_t *p);

/** Whether the bank is armed.  An OPEN is refused while it is. */
void bus_port_bench_armed(bus_port_t *p, bool armed);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_BUS_PORT_H */
