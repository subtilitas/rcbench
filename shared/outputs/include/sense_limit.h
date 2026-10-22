/**
 * @file
 * @brief The SENSE_LIMIT link page at the IOmcu (protocol 4.27): a current
 *        limit for each of SENSE_MAP's 12 channels, and the channels whose
 *        window passed its limit.
 *
 * The registers and the rule are in link_pages.h (LINK_SL_*); the state is the
 * SENSE page's (sense_page_t).  An alert only: nothing here stops or drives an
 * output.
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

/**
 * @brief A SENSE_LIMIT read from the link.
 *
 * A read that covers OVER clears it once @p out holds it.
 *
 * Nothing is done when @p p or @p out is NULL, or when off and n reach past
 * the page.
 *
 * @param p Page state, read and cleared in place.
 * @param off First register, 0 to LINK_SL_COUNT - 1.
 * @param n Number of registers to read.
 * @param out Receives the @p n register values.
 */
void sense_limit_read(sense_page_t *p, uint8_t off, uint8_t n,
                      uint16_t *out);

/**
 * @brief The registers as they stand, OVER kept: for a report, not the link.
 *
 * Nothing is done when @p p or @p out is NULL, or when off and n reach past
 * the page.
 *
 * @param p Page state, not changed.
 * @param off First register, 0 to LINK_SL_COUNT - 1.
 * @param n Number of registers to copy.
 * @param out Receives the @p n register values.
 */
void sense_limit_peek(const sense_page_t *p, uint8_t off, uint8_t n,
                      uint16_t *out);

/**
 * @brief A SENSE_LIMIT write, judged whole.
 *
 * Refused: off the page (BAD_RANGE), reaching register 12 (READ_ONLY), a limit
 * above 32767 (BAD_VALUE).
 *
 * Taken in any bank state.
 *
 * @param p Page state, written only when the whole write is taken.
 * @param off First register, 0 to LINK_SL_COUNT - 1.
 * @param n Number of registers to write.
 * @param in The @p n new limit values, each at most LINK_SL_MA_MAX (32767).
 * @return 0 when taken. LINK_NACK_BAD_RANGE for a NULL pointer or a range off
 *         the page. LINK_NACK_READ_ONLY when the range reaches register 12.
 *         LINK_NACK_BAD_VALUE when a value is above LINK_SL_MA_MAX.
 */
uint8_t sense_limit_write(sense_page_t *p, uint8_t off, uint8_t n,
                          const uint16_t *in);

/**
 * @brief What is kept: the 12 limits into @p kept.
 *
 * @param p Page state, not changed.
 * @param kept Receives LINK_SL_KEPT (12) limit values. Not checked for NULL.
 */
void sense_limit_kept(const sense_page_t *p, uint16_t *kept);

/**
 * @brief Kept limits back at boot, through sense_limit_write()'s checks: 0, or
 *        the refusal, which leaves every limit 0.
 *
 * @param p Page state, written through sense_limit_write().
 * @param kept The LINK_SL_KEPT (12) kept limit values.
 * @return 0 when all 12 limits are taken. Otherwise the refusal from
 *         sense_limit_write(): LINK_NACK_BAD_RANGE for a NULL pointer,
 *         LINK_NACK_BAD_VALUE for a limit above LINK_SL_MA_MAX.
 */
uint8_t sense_limit_restore(sense_page_t *p, const uint16_t *kept);

#ifdef __cplusplus
}
#endif
