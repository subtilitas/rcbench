/*
 * The PD mini driver's private functions, shared by pdmini.c and
 * pdmini_jobs.c.  Not part of the module's interface; another project
 * includes pdmini.h only.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef PDMINI_INT_H
#define PDMINI_INT_H

#include "pdmini.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Starts the transaction @p req of @p n bytes and its CRC8: attaches the
 * pins and enters PD_ATTACH.  @p write is true for a write. */
void pdmini_start(pdmini_t *d, uint32_t now, const uint8_t *req, size_t n,
                  bool write);

/* Starts the one-byte read @p cmd. */
void pdmini_read1(pdmini_t *d, uint32_t now, uint8_t cmd);

/* Ends the transaction under way, @p ok when its reply was taken (a write:
 * when its bytes left), and enters PD_GAP. */
void pdmini_finish(pdmini_t *d, uint32_t now, bool ok);

/* The voltage set point the module is given for the one asked. */
uint16_t pdmini_target_mv(const pdmini_t *d);

/* Whether the input last read lies under the set point read back and
 * PDMINI_HEADROOM_MV. */
bool pdmini_under_line(const pdmini_t *d);

/* The output switched off for the input, or its ON not written, and held
 * off until an OFF is asked. */
void pdmini_sag_hold(pdmini_t *d);

#endif /* PDMINI_INT_H */
