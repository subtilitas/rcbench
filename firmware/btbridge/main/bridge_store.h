/**
 * @file bridge_store.h
 * @brief The bridge's settings in NVS (non-volatile storage): the record
 *        an HC-05 keeps in its flash (hc05emu_cfg_t), read at the start
 *        and written on every HC05EMU_ACT_SAVE.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>

#include "hc05emu.h"

/**
 * @brief Open NVS, erasing a partition of another layout, and read the
 *        record.
 * @param out Receives it.
 * @return true when a record of this layout was read; @p out is then for
 *         hc05emu_init() to check.  false: none, or NVS failed.
 */
bool bridge_store_load(hc05emu_cfg_t *out);

/**
 * @brief Write the record and commit it.
 * @param c The record.
 * @return true when it was committed.
 */
bool bridge_store_save(const hc05emu_cfg_t *c);
