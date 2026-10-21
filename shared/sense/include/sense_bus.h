/**
 * @file
 * @brief The sensor bus: one I2C (Inter-Integrated Circuit) bus with current
 * monitors on it, as the drivers see it.
 *
 * The drivers (ina228.h, ina3221.h) reach the wire through two callbacks, a
 * register read and a register write, each answering with an error code. The
 * IOmcu supplies them from its I2C block; the host suite supplies a fake that
 * answers as each part does.  Both parts send the most significant byte first
 * (INA228 SLYS021A §7.5.1, INA3221 SBOS576C §8.2) and keep the register pointer
 * between transactions, so a read is the pointer write and n bytes back, and a
 * write is the pointer and 2 bytes.
 *
 * What this module decides:
 *
 * - Bus stuck.  A transaction that finds SDA or SCL held low (SENSE_BUS_LOW),
 * or SENSE_STUCK_TIMEOUTS transactions in a row that do not finish
 * (SENSE_TIMEOUT), mark the bus stuck.  A NACK (not acknowledged) does not: the
 * address byte went out and the lines came back, so the bus works and a part is
 * missing.  While the bus is stuck nothing is sent; a recovery is due at once
 * and every SENSE_RECOVER_MS after that.  The caller runs it -- 9 clocks on SCL
 * as GPIO (general-purpose input/output), a STOP, the I2C block re-initialised
 * -- and reports it with sense_bus_recovered().  The next transaction then goes
 * out as the test: one that ends with OK or a NACK clears the stuck flag.  The
 * INA3221 also frees the bus by itself after SCL is held low for 28 ms to 35 ms
 * (SBOS576C §6.5, SMBus timeout); the INA228's datasheet states no such
 * timeout.
 *
 * - A part online or offline.  A part is probed (identity read, set-up written
 * and read back) before anything else is said to it. SENSE_FAILS failed
 * transactions in a row take an online part offline: 6 ms of readings at 500
 * Hz.  So does a set-up register that reads back another value than the one
 * written, twice in a row: the part reset itself and runs on its power-on
 * set-up (sense_part_lost()).  A part that is not online is probed again every
 * SENSE_RETRY_MS.  Nothing is acted on here: an offline part is reported, its
 * readings stop, and the bench is not disarmed.
 *
 * Pure C, no SDK (software development kit): host-tested against a fake bus in
 * test_ina228 and test_ina3221.
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

/**
 * @brief What a transaction came to.
 *
 * The first four come from the wire; the rest are answers given without sending
 * anything.
 */
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

/**
 * @brief The wire.
 *
 * @p n is the register width in bytes: 2, 3 or 5 for a read (INA228 SLYS021A
 * Table 7-3, INA3221 SBOS576C Table 8-1), 2 for a write.
 */
typedef struct {
    sense_err_t (*read)(void *ctx, uint8_t addr, uint8_t reg,
                        uint8_t *buf, size_t n);
    sense_err_t (*write)(void *ctx, uint8_t addr, uint8_t reg,
                         const uint8_t *buf, size_t n);
    void *ctx;
} sense_i2c_t;

/**
 * @brief Bus timing constants.
 */
#define SENSE_STUCK_TIMEOUTS  2u   /**< timeouts in a row: the bus is stuck  */
#define SENSE_RECOVER_MS    100u   /**< recoveries this far apart while stuck;
                                        chosen, not measured               */
#define SENSE_FAILS           3u   /**< failed transactions in a row: offline */
#define SENSE_RETRY_MS     1000u   /**< a part not online, probed again     */

/**
 * @brief The state of one bus: its error counts, stuck flag and recoveries.
 */
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

/**
 * @brief Set up a bus over a wire.
 *
 * @param b The bus state to set up.
 * @param io The wire callbacks; kept, not copied.
 */
void sense_bus_init(sense_bus_t *b, const sense_i2c_t *io);

/**
 * @brief A register read: @p n bytes, most significant first, into @p buf.
 *
 * @param b The bus.
 * @param addr I2C address of the part.
 * @param reg Register address.
 * @param buf Receives n bytes.
 * @param n Register width in bytes.
 * @return SENSE_OK, a wire error, or SENSE_STUCK while the bus is stuck.
 */
sense_err_t sense_bus_read(sense_bus_t *b, uint8_t addr, uint8_t reg,
                           uint8_t *buf, size_t n);

/**
 * @brief A 16-bit register write.
 *
 * @param b The bus.
 * @param addr I2C address of the part.
 * @param reg Register address.
 * @param value The 16-bit value, most significant byte sent first.
 * @return SENSE_OK, a wire error, or SENSE_STUCK while the bus is stuck.
 */
sense_err_t sense_bus_write16(sense_bus_t *b, uint8_t addr, uint8_t reg,
                              uint16_t value);

/**
 * @brief A transaction made on the wire without this module -- an address scan
 * -- and what it came to, for the stuck decision: a held line or a timeout
 * counts as one of this module's would, and marks the bus stuck by the same
 * rule.
 *
 * An OK or a NACK shows the lines working and ends a run of timeouts; a NACK is
 * a missing address and is not counted as an error.  Any other code reached
 * nothing and changes nothing.
 *
 * @param b The bus.
 * @param e The result of the transaction.
 */
void sense_bus_note(sense_bus_t *b, sense_err_t e);

/**
 * @brief Whether a recovery is to be run now: the bus is stuck, and none has
 * run since, or the last ran SENSE_RECOVER_MS or more ago without a transaction
 * proving the bus free.
 *
 * @param b The bus.
 * @param now_ms The current time, in ms.
 * @return true when the caller should run a recovery.
 */
bool sense_bus_recovery_due(const sense_bus_t *b, uint32_t now_ms);

/**
 * @brief The caller has run a recovery.
 *
 * The next transaction goes out as the test; on a bus that is not stuck this
 * only counts it.
 *
 * @param b The bus.
 * @param now_ms The current time, in ms.
 */
void sense_bus_recovered(sense_bus_t *b, uint32_t now_ms);

/** @brief one part */

/**
 * @brief A part on the bus, as far as it is known.
 */
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

/**
 * @brief A part on the bus: its address, state and last probe.
 */
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

/**
 * @brief Set up a part record on a bus.
 *
 * @param p The part record.
 * @param bus The bus the part sits on.
 * @param addr I2C address of the part.
 */
void sense_part_init(sense_part_t *p, sense_bus_t *bus, uint8_t addr);

/**
 * @brief Every tick, with the time: whether the part is to be probed now.
 *
 * @param p The part record.
 * @param now_ms The current time, in ms.
 * @return true when the part is due a probe.
 */
bool sense_part_probe_due(sense_part_t *p, uint32_t now_ms);

/**
 * @brief The probe's result; a part left anything but online is probed again
 * SENSE_RETRY_MS on.
 *
 * @param p The part record.
 * @param state The state the probe reached.
 */
void sense_part_probed(sense_part_t *p, sense_state_t state);

/**
 * @brief A read or write of an online part, counted towards taking it offline.
 *
 * A part that is not online is not addressed: SENSE_OFFLINE.
 *
 * @param p The part record.
 * @param reg Register address.
 * @param buf Receives n bytes.
 * @param n Register width in bytes.
 * @return SENSE_OK, SENSE_OFFLINE, or the bus error.
 */
sense_err_t sense_part_read(sense_part_t *p, uint8_t reg,
                            uint8_t *buf, size_t n);
/**
 * @brief Write a 16-bit register of a part, counted towards taking it offline.
 *
 * @param p The part record.
 * @param reg Register address.
 * @param value The 16-bit value.
 * @return SENSE_OK, SENSE_OFFLINE, or the bus error.
 */
sense_err_t sense_part_write16(sense_part_t *p, uint8_t reg, uint16_t value);

/**
 * @brief After a sample of two reads whose second failed: the failure run is @p
 * before plus one, as if the first read had not reset it.
 *
 * @p before is the part's fails count taken ahead of the sample.  A part that
 * the failed read took offline is left as it is.
 *
 * @param p The part record.
 * @param before The part's fails count taken ahead of the sample.
 */
void sense_part_sample_failed(sense_part_t *p, uint8_t before);

/**
 * @brief An online part that answers and no longer holds the set-up written to
 * it: offline, and probed again SENSE_RETRY_MS on.
 *
 * Nothing for a part that is not online.
 *
 * @param p The part record.
 */
void sense_part_lost(sense_part_t *p);

/** @brief codec */

/**
 * @brief Big-endian register contents, most significant byte first.
 *
 * @param b Two bytes, most significant first.
 * @return The value.
 */
uint16_t sense_be16(const uint8_t *b);
/**
 * @brief Read a big-endian 24-bit value.
 *
 * @param b Three bytes, most significant first.
 * @return The value.
 */
uint32_t sense_be24(const uint8_t *b);
/**
 * @brief Read a big-endian 40-bit value.
 *
 * @param b Five bytes, most significant first.
 * @return The value.
 */
uint64_t sense_be40(const uint8_t *b);

/**
 * @brief The low @p bits of @p v as a two's complement number (1 to 32 bits).
 *
 * @param v The value.
 * @param bits Width of the field, 1 to 32.
 * @return The signed value.
 */
int32_t sense_sext32(uint32_t v, unsigned bits);
/**
 * @brief The same for up to 64 bits.
 *
 * @param v The value.
 * @param bits Width of the field, 1 to 64.
 * @return The signed value.
 */
int64_t sense_sext64(uint64_t v, unsigned bits);

/**
 * @brief @p a × @p mul / @p div without the product overflowing, truncated
 * towards zero.
 *
 * Exact while |a / div × mul| and div × mul fit 63 bits.
 *
 * @param a The multiplicand.
 * @param mul The multiplier.
 * @param div The divisor, not zero.
 * @return a × mul / div, truncated towards zero.
 */
int64_t sense_muldiv(int64_t a, uint32_t mul, uint64_t div);

/**
 * @brief A reading that may be at the end of its register's range.
 */
typedef enum {
    SENSE_CLIP_LOW  = -1,  /**< at the bottom of the range                */
    SENSE_CLIP_NONE =  0,
    SENSE_CLIP_HIGH =  1,  /**< at the top of the range                   */
} sense_clip_t;

/**
 * @brief A measurement, or a clip.
 *
 * A clipped reading carries no value: the quantity is at or past the end of the
 * range, by how much is not known, and value is 0.
 */
typedef struct {
    int32_t      value;
    sense_clip_t clip;
} sense_value_t;

#ifdef __cplusplus
}
#endif
