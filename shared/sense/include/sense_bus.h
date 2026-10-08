/*
 * The sensor bus: one I2C (Inter-Integrated Circuit) bus with current
 * monitors on it, as the drivers see it.
 *
 * The drivers (ina228.h, ina3221.h) reach the wire through two callbacks,
 * a register read and a register write, each answering with an error code.
 * The coprocessor supplies them from its I2C block; the host suite supplies
 * a fake that answers as each part does.  Both parts send the most
 * significant byte first (INA228 SLYS021A §7.5.1, INA3221 SBOS576C §8.2)
 * and keep the register pointer between transactions, so a read is the
 * pointer write and n bytes back, and a write is the pointer and 2 bytes.
 *
 * What this module decides:
 *
 * - Bus stuck.  A transaction that finds SDA or SCL held low
 *   (SENSE_BUS_LOW), or SENSE_STUCK_TIMEOUTS transactions in a row that do
 *   not finish (SENSE_TIMEOUT), mark the bus stuck.  A NACK (not
 *   acknowledged) does not: the address byte went out and the lines came
 *   back, so the bus works and a part is missing.  While the bus is stuck
 *   nothing is sent; a recovery is due at once and every SENSE_RECOVER_MS
 *   after that.  The caller runs it -- 9 clocks on SCL as GPIO
 *   (general-purpose input/output), a STOP, the I2C block re-initialised --
 *   and reports it with sense_bus_recovered().  The next transaction then
 *   goes out as the test: one that ends with OK or a NACK clears the stuck
 *   flag.  The INA3221 also frees the bus by itself after SCL is held low
 *   for 28 ms to 35 ms (SBOS576C §6.5, SMBus timeout); the INA228's
 *   datasheet states no such timeout.
 *
 * - A part online or offline.  A part is probed (identity read, set-up
 *   written and read back) before anything else is said to it.
 *   SENSE_FAILS failed transactions in a row take an online part offline:
 *   6 ms of readings at 500 Hz.  A part that is not online is probed again
 *   every SENSE_RETRY_MS.  Nothing is acted on here: an offline part is
 *   reported, its readings stop, and the bench is not disarmed.
 *
 * Pure C, no SDK (software development kit): host-tested against a fake
 * bus in test_ina228 and test_ina3221.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** What a transaction came to.  The first four come from the wire; the
 *  rest are answers given without sending anything. */
typedef enum {
    SENSE_OK = 0,
    SENSE_NACK,      /**< address or a byte not acknowledged               */
    SENSE_TIMEOUT,   /**< did not finish in its time: SCL held low          */
    SENSE_BUS_LOW,   /**< SDA or SCL low with the bus idle, or arbitration
                          lost                                             */
    SENSE_STUCK,     /**< not sent: the bus is stuck, a recovery is owed    */
    SENSE_OFFLINE,   /**< not sent: the part is not online                  */
    SENSE_BAD_ARG,   /**< not sent: a channel the part has not enabled      */
} sense_err_t;

/** The wire.  @p n is the register width in bytes: 2, 3 or 5 for a read
 *  (INA228 SLYS021A Table 7-3, INA3221 SBOS576C Table 8-1), 2 for a
 *  write. */
typedef struct {
    sense_err_t (*read)(void *ctx, uint8_t addr, uint8_t reg,
                        uint8_t *buf, size_t n);
    sense_err_t (*write)(void *ctx, uint8_t addr, uint8_t reg,
                         const uint8_t *buf, size_t n);
    void *ctx;
} sense_i2c_t;

#define SENSE_STUCK_TIMEOUTS  2u   /**< timeouts in a row: the bus is stuck  */
#define SENSE_RECOVER_MS    100u   /**< recoveries this far apart while stuck;
                                        chosen, not measured               */
#define SENSE_FAILS           3u   /**< failed transactions in a row: offline */
#define SENSE_RETRY_MS     1000u   /**< a part not online, probed again     */

typedef struct {
    sense_i2c_t io;
    uint16_t errors;      /**< transactions failed on the wire, mod 65536 */
    uint8_t  timeouts;    /**< SENSE_TIMEOUT in a row                     */
    bool     stuck;       /**< nothing is sent until a recovery           */
    bool     trial;       /**< a recovery ran; the next transaction tests  */
    bool     recovered;   /**< a recovery has run since the bus stuck     */
    uint32_t recover_at;  /**< when the last recovery ran                 */
    uint32_t recoveries;  /**< recoveries run, all told                   */
} sense_bus_t;

void sense_bus_init(sense_bus_t *b, const sense_i2c_t *io);

/** A register read: @p n bytes, most significant first, into @p buf. */
sense_err_t sense_bus_read(sense_bus_t *b, uint8_t addr, uint8_t reg,
                           uint8_t *buf, size_t n);

/** A 16-bit register write. */
sense_err_t sense_bus_write16(sense_bus_t *b, uint8_t addr, uint8_t reg,
                              uint16_t value);

/** A transaction made on the wire without this module -- an address scan
 *  -- and what it came to, for the stuck decision: a held line or a
 *  timeout counts as one of this module's would, and marks the bus stuck
 *  by the same rule.  An OK or a NACK shows the lines working and ends a
 *  run of timeouts; a NACK is a missing address and is not counted as an
 *  error.  Any other code reached nothing and changes nothing. */
void sense_bus_note(sense_bus_t *b, sense_err_t e);

/** Whether a recovery is to be run now: the bus is stuck, and none has run
 *  since, or the last ran SENSE_RECOVER_MS or more ago without a
 *  transaction proving the bus free. */
bool sense_bus_recovery_due(const sense_bus_t *b, uint32_t now_ms);

/** The caller has run a recovery.  The next transaction goes out as the
 *  test; on a bus that is not stuck this only counts it. */
void sense_bus_recovered(sense_bus_t *b, uint32_t now_ms);

/* ----------------------------------------------------------- one part */

/** A part on the bus, as far as it is known. */
typedef enum {
    SENSE_PART_UNSET = 0,  /**< no valid set-up given; nothing is sent     */
    SENSE_PART_UNPROBED,   /**< set up, not probed yet                     */
    SENSE_PART_ABSENT,     /**< the probe went unanswered                  */
    SENSE_PART_WRONG_ID,   /**< answered with another identity; not used   */
    SENSE_PART_REFUSED,    /**< identity good; the set-up did not read back
                                as written                                */
    SENSE_PART_ONLINE,
    SENSE_PART_OFFLINE,    /**< was online; SENSE_FAILS transactions in a
                                row failed                                */
} sense_state_t;

typedef struct {
    sense_bus_t  *bus;
    uint8_t       addr;
    sense_state_t state;
    uint8_t       fails;       /**< failed transactions in a row          */
    sense_err_t   last_err;    /**< the last failure                      */
    uint16_t      id_maker;    /**< manufacturer ID at the last probe     */
    uint16_t      id_device;   /**< device or die ID at the last probe    */
    uint32_t      now;         /**< the time sense_part_probe_due() saw   */
    uint32_t      probe_at;    /**< when a part not online is probed again */
} sense_part_t;

void sense_part_init(sense_part_t *p, sense_bus_t *bus, uint8_t addr);

/** Every tick, with the time: whether the part is to be probed now. */
bool sense_part_probe_due(sense_part_t *p, uint32_t now_ms);

/** The probe's result; a part left anything but online is probed again
 *  SENSE_RETRY_MS on. */
void sense_part_probed(sense_part_t *p, sense_state_t state);

/** A read or write of an online part, counted towards taking it offline.
 *  A part that is not online is not addressed: SENSE_OFFLINE. */
sense_err_t sense_part_read(sense_part_t *p, uint8_t reg,
                            uint8_t *buf, size_t n);
sense_err_t sense_part_write16(sense_part_t *p, uint8_t reg, uint16_t value);

/* ------------------------------------------------------------- codec */

/** Big-endian register contents, most significant byte first. */
uint16_t sense_be16(const uint8_t *b);
uint32_t sense_be24(const uint8_t *b);
uint64_t sense_be40(const uint8_t *b);

/** The low @p bits of @p v as a two's complement number (1 to 32 bits). */
int32_t sense_sext32(uint32_t v, unsigned bits);
/** The same for up to 64 bits. */
int64_t sense_sext64(uint64_t v, unsigned bits);

/** @p a × @p mul / @p div without the product overflowing, truncated
 *  towards zero.  Exact while |a / div × mul| and div × mul fit 63 bits. */
int64_t sense_muldiv(int64_t a, uint32_t mul, uint64_t div);

/** A reading that may be at the end of its register's range. */
typedef enum {
    SENSE_CLIP_LOW  = -1,  /**< at the bottom of the range                */
    SENSE_CLIP_NONE =  0,
    SENSE_CLIP_HIGH =  1,  /**< at the top of the range                   */
} sense_clip_t;

/** A measurement, or a clip.  A clipped reading carries no value: the
 *  quantity is at or past the end of the range, by how much is not
 *  known, and value is 0. */
typedef struct {
    int32_t      value;
    sense_clip_t clip;
} sense_value_t;

#ifdef __cplusplus
}
#endif
