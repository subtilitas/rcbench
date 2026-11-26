/**
 * @file node_scan.h
 * @brief The node SETUP's I2C (Inter-Integrated Circuit) SCAN: every 7-bit
 *        address of 0x08 to 0x77 asked once, the INA228s among them marked.
 *        Pure C over two bus calls, under the host suite (test_node_scan).
 *
 * Each address of 0x08 to 0x77 (112) gets one probe: its address byte and
 * a STOP, with no data byte, as ESP-IDF's i2c_master_probe() sends it.  An
 * address that acknowledges answers.  No part is written to: the probe
 * carries no data, and the only other transaction is the INA228 check
 * (ina228_identity_ok(), #985's), two 16-bit register reads of
 * MANUFACTURER_ID (3Eh) and DEVICE_ID (3Fh), at the answering addresses of
 * 0x40 to 0x4F only.  A register read sends the register's number before
 * its repeated START, as every read of the power role does.  0x00 to 0x07
 * and 0x78 to 0x7F are I2C's reserved addresses and are not asked.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The addresses asked. @{ */
#define NSCAN_FIRST 0x08u  /**< the first */
#define NSCAN_LAST  0x77u  /**< the last */
/** @} */

/** @brief The bus calls the scan makes. */
typedef struct {
    /** @brief The address byte and a STOP, no data.  @param ctx ctx.
     *  @param addr The address.  @return true when it acknowledged. */
    bool (*probe)(void *ctx, uint8_t addr);
    /** @brief A 16-bit register, big-endian.  @param ctx ctx.  @param addr
     *  The address.  @param reg The register.  @param v Receives it.
     *  @return true when it read. */
    bool (*read16)(void *ctx, uint8_t addr, uint8_t reg, uint16_t *v);
    void *ctx;  /**< passed unchanged */
} nscan_io_t;

/** @brief A scan's result: a bit per address, bit (a & 7) of byte a / 8. */
typedef struct {
    uint8_t answered[16];  /**< the addresses that acknowledged */
    uint8_t ina228[16];    /**< those that hold an INA228 */
    uint8_t n;             /**< the addresses that acknowledged */
    uint8_t n_ina228;      /**< the INA228s among them */
    bool    done;          /**< a scan ran */
} nscan_t;

/**
 * @brief Run the scan: 0x08 to 0x77 probed, the INA228 check at the
 *        answering addresses of 0x40 to 0x4F.
 * @param io  The bus.
 * @param out Receives the result; cleared first.
 */
void nscan_run(const nscan_io_t *io, nscan_t *out);

/**
 * @brief Whether an address answered.
 * @param r The result.  @param addr The address.
 * @return true when it did.
 */
bool nscan_answered(const nscan_t *r, uint8_t addr);

/**
 * @brief Whether an address holds an INA228.
 * @param r The result.  @param addr The address.
 * @return true when it does.
 */
bool nscan_is_ina228(const nscan_t *r, uint8_t addr);

/**
 * @brief The @p i-th answering address, lowest first.
 * @param r The result.
 * @param i The index; past the count gives 0.
 * @param only_ina228 Count the INA228s only.
 * @return The address, 0 past the last.
 */
uint8_t nscan_at(const nscan_t *r, unsigned i, bool only_ina228);

#ifdef __cplusplus
}
#endif
