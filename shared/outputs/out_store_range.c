/**
 * @file
 * @brief Which flash ranges the IOmcu's firmware update may write.
 *
 * See out_store_range.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "out_store_range.h"

/**
 * @brief Whether [@p off, @p off + @p len) lies between the floor and
 * @p ceiling, without a wrap past 4GiB.
 *
 * @param off      First byte.
 * @param len      Bytes in the range.
 * @param ceiling  First byte past the allowed area.
 * @return True when the whole range is allowed.
 */
static bool inside(uint32_t off, uint32_t len, uint32_t ceiling)
{
    return off >= OUT_STORE_RANGE_FLOOR && off <= ceiling
           && len <= ceiling - off;
}

bool out_store_range_erase_ok(uint32_t off, uint32_t ceiling)
{
    return off % OUT_STORE_RANGE_SECTOR == 0u
           && inside(off, OUT_STORE_RANGE_SECTOR, ceiling);
}

bool out_store_range_program_ok(uint32_t off, uint32_t len, uint32_t ceiling)
{
    return off % OUT_STORE_RANGE_SECTOR == 0u
           && len % OUT_STORE_RANGE_PAGE == 0u
           && len <= OUT_STORE_RANGE_SECTOR && inside(off, len, ceiling);
}
