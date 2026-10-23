/**
 * @file
 * @brief Why the IOmcu last started: see reset_cause.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "reset_cause.h"

#include <stdio.h>

/** @brief The words, indexed by reset_cause_t. */
static const char *const k_words[RESET_CAUSE_COUNT] = {
    [RESET_CAUSE_UNKNOWN]        = "UNKNOWN",
    [RESET_CAUSE_POR]            = "POR",
    [RESET_CAUSE_BOR]            = "BOR",
    [RESET_CAUSE_RUN_PIN]        = "RUN_PIN",
    [RESET_CAUSE_GLITCH]         = "GLITCH",
    [RESET_CAUSE_DEBUGGER]       = "DEBUGGER",
    [RESET_CAUSE_POWER_DOWN]     = "POWER_DOWN",
    [RESET_CAUSE_FLASH_UPDATE]   = "FLASH_UPDATE",
    [RESET_CAUSE_RAM_IMAGE]      = "RAM_IMAGE",
    [RESET_CAUSE_BOOTSEL_REBOOT] = "BOOTSEL_REBOOT",
    [RESET_CAUSE_SW_RESET]       = "SW_RESET",
    [RESET_CAUSE_WATCHDOG]       = "WATCHDOG",
};

int reset_cause_boot_type(const reset_regs_t *r)
{
    if (r == NULL || !r->boot_ok) {
        return -1;
    }
    return (int)((r->boot_word >> 8) & 0xFFu & ~RESET_BOOT_TYPE_CHAINED_FLAG);
}

reset_cause_t reset_cause_decode(const reset_regs_t *r)
{
    if (r == NULL) {
        return RESET_CAUSE_UNKNOWN;
    }
    /* A boot type other than NORMAL follows a reboot() vector only. */
    const int type = reset_cause_boot_type(r);
    if (type == (int)RESET_BOOT_TYPE_FLASH_UPDATE) {
        return RESET_CAUSE_FLASH_UPDATE;
    }
    if (type == (int)RESET_BOOT_TYPE_RAM_IMAGE) {
        return RESET_CAUSE_RAM_IMAGE;
    }
    if (type == (int)RESET_BOOT_TYPE_BOOTSEL) {
        return RESET_CAUSE_BOOTSEL_REBOOT;
    }
    /*
     * A watchdog system reset: it leaves CHIP_RESET as the last chip-level
     * reset set it, so CHIP_RESET is not read here (datasheet 7.3.3, 7.4).
     */
    const uint32_t why = r->wd_reason
                         & (RESET_WD_REASON_TIMER | RESET_WD_REASON_FORCE);
    if (why != 0u) {
        if (type == (int)RESET_BOOT_TYPE_NORMAL
            && r->wd_scratch2 == RESET_PICOTOOL_P0
            && r->wd_scratch3 == RESET_PICOTOOL_P1) {
            return RESET_CAUSE_BOOTSEL_REBOOT;
        }
        return (why & RESET_WD_REASON_FORCE) != 0u ? RESET_CAUSE_SW_RESET
                                                   : RESET_CAUSE_WATCHDOG;
    }
    /* REASON 0: a chip-level reset reset the watchdog, and CHIP_RESET
     * names it. */
    const uint32_t c = r->chip_reset;
    if ((c & RESET_CHIP_HAD_POR) != 0u) {
        return RESET_CAUSE_POR;
    }
    if ((c & RESET_CHIP_HAD_BOR) != 0u) {
        return RESET_CAUSE_BOR;
    }
    if ((c & RESET_CHIP_HAD_RUN_LOW) != 0u) {
        return RESET_CAUSE_RUN_PIN;
    }
    if ((c & RESET_CHIP_HAD_GLITCH_DETECT) != 0u) {
        return RESET_CAUSE_GLITCH;
    }
    if ((c & (RESET_CHIP_HAD_DP_RESET_REQ | RESET_CHIP_HAD_RESCUE
              | RESET_CHIP_HAD_HZD_SYS_RESET_REQ)) != 0u) {
        return RESET_CAUSE_DEBUGGER;
    }
    if ((c & RESET_CHIP_HAD_SWCORE_PD) != 0u) {
        return RESET_CAUSE_POWER_DOWN;
    }
    if ((c & RESET_CHIP_HAD_WATCHDOG_ANY) != 0u) {
        return RESET_CAUSE_WATCHDOG;
    }
    return RESET_CAUSE_UNKNOWN;
}

const char *reset_cause_word(reset_cause_t c)
{
    if ((unsigned)c >= (unsigned)RESET_CAUSE_COUNT) {
        return k_words[RESET_CAUSE_UNKNOWN];
    }
    return k_words[c];
}

/**
 * @brief snprintf()'s count as a size_t; a format error reads as 0.
 *
 * @param len  What snprintf() returned.
 * @return @p len, or 0 when it is negative.
 */
static size_t as_len(int len)
{
    return len < 0 ? 0u : (size_t)len;
}

size_t reset_cause_full(const reset_regs_t *r, char *out, size_t n)
{
    static const reset_regs_t k_none = { 0 };
    const reset_regs_t *v = r != NULL ? r : &k_none;
    const char *word = reset_cause_word(reset_cause_decode(r));
    if (!v->boot_ok) {
        return as_len(snprintf(out, n,
                               "%s (chip_reset 0x%08lX reason 0x%lX boot none "
                               "scratch 0x%08lX 0x%08lX)",
                               word, (unsigned long)v->chip_reset,
                               (unsigned long)v->wd_reason,
                               (unsigned long)v->wd_scratch2,
                               (unsigned long)v->wd_scratch3));
    }
    return as_len(snprintf(out, n,
                           "%s (chip_reset 0x%08lX reason 0x%lX boot 0x%08lX "
                           "params 0x%08lX 0x%08lX scratch 0x%08lX 0x%08lX)",
                           word, (unsigned long)v->chip_reset,
                           (unsigned long)v->wd_reason,
                           (unsigned long)v->boot_word,
                           (unsigned long)v->reboot_params[0],
                           (unsigned long)v->reboot_params[1],
                           (unsigned long)v->wd_scratch2,
                           (unsigned long)v->wd_scratch3));
}
