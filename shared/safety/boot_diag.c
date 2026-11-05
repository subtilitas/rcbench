/**
 * @file
 * @brief The boot ROM's record of the last boot as text: see boot_diag.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "boot_diag.h"

#include <stdio.h>

/** @brief Room for a name formatted into a buffer: "0xFF" or "15" and the NUL. */
#define NAME_MAX_BYTES 8u

/**
 * @brief The name of a partition code of the boot word.
 *
 * @param code  The signed byte from the boot word.
 * @param buf   Receives the name when it is a number.
 * @return The name: a static string, or @p buf.
 */
static const char *partition_name(int code, char buf[NAME_MAX_BYTES])
{
    switch (code) {
    case BOOT_DIAG_PARTITION_NONE:   return "none";
    case BOOT_DIAG_PARTITION_SLOT0:  return "slot 0";
    case BOOT_DIAG_PARTITION_SLOT1:  return "slot 1";
    case BOOT_DIAG_PARTITION_WINDOW: return "window";
    default:
        break;
    }
    if (code >= 0 && code <= 15) {
        (void)snprintf(buf, NAME_MAX_BYTES, "%d", code);
    } else {
        (void)snprintf(buf, NAME_MAX_BYTES, "0x%02X",
                       (unsigned)(code & 0xFF));
    }
    return buf;
}

/**
 * @brief The name of a boot type without its chained flag.
 *
 * @param type  The boot type, 0 to 0x7F; only its low byte is printed.
 * @param buf   Receives the name when the type has none.
 * @return The name: a static string, or @p buf.
 */
static const char *type_name(unsigned type, char buf[NAME_MAX_BYTES])
{
    switch (type) {
    case RESET_BOOT_TYPE_NORMAL:       return "NORMAL";
    case RESET_BOOT_TYPE_BOOTSEL:      return "BOOTSEL";
    case RESET_BOOT_TYPE_RAM_IMAGE:    return "RAM_IMAGE";
    case RESET_BOOT_TYPE_FLASH_UPDATE: return "FLASH_UPDATE";
    case RESET_BOOT_TYPE_PC_SP:        return "PC_SP";
    default:
        break;
    }
    (void)snprintf(buf, NAME_MAX_BYTES, "0x%02X", type & 0xFFu);
    return buf;
}

size_t boot_diag_text(const reset_regs_t *r, char *out, size_t n)
{
    int len;
    if (r == NULL || !r->boot_ok) {
        len = snprintf(out, n, "boot info none");
    } else {
        char tbuf[NAME_MAX_BYTES];
        char pbuf[NAME_MAX_BYTES];
        char sbuf[NAME_MAX_BYTES];
        const unsigned byte1 = (unsigned)((r->boot_word >> 8) & 0xFFu);
        const int part = (int)(int8_t)(uint8_t)((r->boot_word >> 16) & 0xFFu);
        const int source = (int)(int8_t)(uint8_t)(r->boot_word & 0xFFu);
        len = snprintf(out, n,
                       "boot type %s%s partition %s source %s "
                       "diagnostic 0x%08lX",
                       type_name(byte1 & ~RESET_BOOT_TYPE_CHAINED_FLAG, tbuf),
                       (byte1 & RESET_BOOT_TYPE_CHAINED_FLAG) != 0u
                           ? "+CHAINED" : "",
                       partition_name(part, pbuf),
                       partition_name(source, sbuf),
                       (unsigned long)r->boot_diagnostic);
    }
    return len < 0 ? 0u : (size_t)len;
}
