/**
 * @file
 * @brief How often the panel reads the IOmcu's STATUS page: every
 *        STATUS_POLL_MS (500ms), and at every bench poll (50ms) while the
 *        band shows HWSTOP, so the chip goes soon after the line closes.
 *
 * The band shows HWSTOP while SETUP > PANEL > Hardware stop is ON and the
 * bitmap it draws carries LINK_FAULT_HEARTBEAT (ui_band_hwstop()).  The
 * IOmcu clears that bit 80 to 100ms after the heartbeat line closes; read
 * every 500ms, the band would keep HWSTOP up to 555ms more.  Read at every
 * poll, 50 to 55ms apart, it keeps it up to 55ms more.  The first read
 * without the bit ends the fast reads: the next read is due STATUS_POLL_MS
 * after it.  With the setting OFF the period is STATUS_POLL_MS whatever
 * the bitmap.  The IOmcu and the link protocol are the same either way.
 *
 * A STATUS read of a 4.21 or later IOmcu is one request and 3 reply frames
 * of up to 4 registers: the fast reads add 18 reads, 72 frames a second,
 * while HWSTOP stands.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The STATUS read period while HWSTOP is not shown: 500ms. */
#define STATUS_POLL_MS 500u

/**
 * @brief Whether STATUS is read at every bench poll: @p hw_stop (SETUP >
 *        PANEL > Hardware stop) is ON and @p faults, the bitmap the band
 *        draws, carries LINK_FAULT_HEARTBEAT.
 *
 * The band's HWSTOP chip is shown by the same rule (ui_band_hwstop()).
 * @param hw_stop SET_HW_STOP.
 * @param faults  The STATUS fault bitmap as the band shows it, the STOP
 *                latch's grace applied (stop_latch_faults()).
 * @return true while HWSTOP stands.
 */
bool status_poll_fast(bool hw_stop, uint16_t faults);

/**
 * @brief Whether this bench poll reads STATUS, and when it does, the read's
 *        time into @p last_ms.
 *
 * Due at every call while @p fast, and otherwise STATUS_POLL_MS or more
 * after @p last_ms, wrap-safe across the 32-bit millisecond counter.
 * @param last_ms The last read's time, ms; updated when due.  NULL is
 *                never due.
 * @param now_ms  The poll's time, ms.
 * @param fast    status_poll_fast() of the last read.
 * @return true when STATUS is read now.
 */
bool status_poll_due(uint32_t *last_ms, uint32_t now_ms, bool fast);

#ifdef __cplusplus
}
#endif
