/*
 * The SENSE_MAP, SENSE_WIN_A and SENSE_WIN_B link pages at the IOmcu
 * (protocol 4.25): up to four INA3221 at 0x40 to 0x43, a shunt and a watch
 * target for each of their 12 channels, the FAST channel, and every
 * channel's last 50 ms window.  The registers are in link_pages.h
 * (LINK_SM_*, LINK_SX_*); the state is the SENSE page's (sense_page_t), so
 * one set-up generation covers both.
 *
 * Until it is first written the map is SENSE's one part (derived): ENABLE
 * bit 1 fits the part at I3221_ADDR, each channel in I3221_CHANNELS has
 * SENSE's shunt, FAST is that part's CH1 when it is read.  A write puts the
 * map in force for good (map_set, kept in flash): SENSE's ENABLE bit 1 and
 * registers 8 to 10 then mirror it, and a SENSE write that would change
 * them is refused (sense_page_write()).  The mirror part is FAST's, or the
 * lowest fitted part while FAST is 0xFF.
 *
 * Host-tested in test_sense_map.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "outputs.h"
#include "sense_page.h"

#ifdef __cplusplus
extern "C" {
#endif

/** A SENSE_MAP read.  Registers 0 to 25 are the map in force, or the one
 *  derived from SENSE; 26 to 30 what core 1 last read; 31 reads 0. */
void sense_map_read(const sense_page_t *p, uint8_t off, uint8_t n,
                    uint16_t *out);

/**
 * A SENSE_MAP write, merged into the map in force (or the derived one) and
 * the whole map judged before any of it is stored.  Refused: off the page
 * (BAD_RANGE); a register from 26 on (READ_ONLY); ENABLE above 15, FAST not
 * 0xFF and past 11, on a part not fitted or on a channel whose shunt is 0,
 * a shunt not 0 outside 50 to 10000, a WATCH value outside the list, a
 * fitted part with no channel, a fitted part on the INA228's address while
 * SENSE enables it, the mirrored SENSE set-up refused as a SENSE write
 * would be (its pins, while the map opens the bus), and any change while
 * @p o drives (BAD_VALUE).  A map taken is in force from then on; a change
 * of ENABLE, FAST or a shunt is a new set-up (the generation moves, the
 * readings go), a change of WATCH alone is not.
 */
uint8_t sense_map_write(sense_page_t *p, uint8_t off, uint8_t n,
                        const uint16_t *in, const outputs_t *o,
                        uint64_t taken);

/** A SENSE_WIN_A (@p page 0) or SENSE_WIN_B (1) read; no write is taken. */
void sense_xwin_read(const sense_page_t *p, unsigned page, uint8_t off,
                     uint8_t n, uint16_t *out);

/** What is kept: registers 0 to 25 (LINK_SM_KEPT) into @p kept, and 1 into
 *  @p set while the map is in force, else 0 (the registers then the
 *  derived ones). */
void sense_map_kept(const sense_page_t *p, uint16_t *kept, uint16_t *set);

/** A kept map back in force at boot, through sense_map_write()'s checks,
 *  after SENSE's restore: nothing when @p set is 0.  0, or the refusal,
 *  which leaves the map derived. */
uint8_t sense_map_restore(sense_page_t *p, const uint16_t *kept,
                          uint16_t set, const outputs_t *o, uint64_t taken);

#ifdef __cplusplus
}
#endif
