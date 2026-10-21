/*
 * The SENSE_LIMIT link page at the IOmcu (protocol 4.27): a current limit
 * for each of SENSE_MAP's 12 channels, and the channels whose window
 * passed its limit.  The registers and the rule are in link_pages.h
 * (LINK_SL_*); the state is the SENSE page's (sense_page_t).  An alert
 * only: nothing here stops or drives an output.
 *
 * Host-tested in test_sense_limit.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>

#include "sense_page.h"

#ifdef __cplusplus
extern "C" {
#endif

/** A SENSE_LIMIT read from the link.  A read that covers OVER clears it
 *  once @p out holds it. */
void sense_limit_read(sense_page_t *p, uint8_t off, uint8_t n,
                      uint16_t *out);

/** The registers as they stand, OVER kept: for a report, not the link. */
void sense_limit_peek(const sense_page_t *p, uint8_t off, uint8_t n,
                      uint16_t *out);

/** A SENSE_LIMIT write, judged whole.  Refused: off the page (BAD_RANGE),
 *  reaching register 12 (READ_ONLY), a limit above 32767 (BAD_VALUE).
 *  Taken in any bank state. */
uint8_t sense_limit_write(sense_page_t *p, uint8_t off, uint8_t n,
                          const uint16_t *in);

/** What is kept: the 12 limits into @p kept. */
void sense_limit_kept(const sense_page_t *p, uint16_t *kept);

/** Kept limits back at boot, through sense_limit_write()'s checks: 0, or
 *  the refusal, which leaves every limit 0. */
uint8_t sense_limit_restore(sense_page_t *p, const uint16_t *kept);

#ifdef __cplusplus
}
#endif
