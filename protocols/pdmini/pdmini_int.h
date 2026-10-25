/**
 * @file
 * @brief The PD mini driver's private functions, shared by pdmini.c and
 *        pdmini_jobs.c.
 *
 * Not part of the module's interface; another project includes pdmini.h only.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef PDMINI_INT_H
#define PDMINI_INT_H

#include "pdmini.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Starts the transaction @p req of @p n bytes and its CRC8: attaches the
 *        pins and enters PD_ATTACH.
 *
 * @p write is true for a write.
 *
 * @param d      Driver state.
 * @param now    Current time, in milliseconds.
 * @param req    Request bytes, without the CRC8.
 * @param n      Number of bytes in @p req.
 * @param write  true for a write, which has no reply.
 */
void pdmini_start(pdmini_t *d, uint32_t now, const uint8_t *req, size_t n,
                  bool write);

/**
 * @brief Starts the one-byte read @p cmd.
 *
 * @param d    Driver state.
 * @param now  Current time, in milliseconds.
 * @param cmd  Command byte to read.
 */
void pdmini_read1(pdmini_t *d, uint32_t now, uint8_t cmd);

/**
 * @brief Ends the transaction under way, @p ok when its reply was taken (a
 *        write: when its bytes left), and enters PD_GAP.
 *
 * @param d    Driver state.
 * @param now  Current time, in milliseconds.
 * @param ok   true when its reply was taken (a write: when its bytes left).
 */
void pdmini_finish(pdmini_t *d, uint32_t now, bool ok);

/**
 * @brief The voltage set point the module is given for the one asked.
 *
 * @param d  Driver state.
 * @return   Voltage in mV.
 */
uint16_t pdmini_target_mv(const pdmini_t *d);

/**
 * @brief Whether the input last read lies under the set point read back and
 *        PDMINI_HEADROOM_MV.
 *
 * @param d  Driver state.
 * @return   true when the input is under the line.
 */
bool pdmini_under_line(const pdmini_t *d);

/**
 * @brief The output switched off for the input, or its ON not written, and held
 *        off until an OFF is asked.
 *
 * @param d  Driver state.
 */
void pdmini_sag_hold(pdmini_t *d);

#endif /* PDMINI_INT_H */
