/**
 * @file start_flush.c
 * @brief The L1 data cache written back and emptied at the app's first
 *        instruction after the CPU set-up, before .bss is cleared.
 *
 * ESP32-P4 rev 3.2 under ESP-IDF v6.1 faults now and then at start with a
 * store or load access fault on a valid address in a freshly allocated
 * task stack or heap block (ROM memset in prvInitialiseNewTask, or
 * tlsf_malloc); the rate depends on the image's layout (espressif/esp-idf
 * issue 19369).  On the bench, release 0.23.9 faulted on 7 of 30 starts;
 * the same build with this flush patched into call_start_cpu0() at the same
 * point on 0 of 60, and with it around cache_hal_init() instead on 5 of 60.
 * The cause is not known.
 *
 * call_start_cpu0() calls esp_rom_get_reset_reason() right after init_cpu()
 * and before init_bss().  The link option -Wl,--wrap=esp_rom_get_reset_reason
 * (main/CMakeLists.txt) routes that call here; the first one does the flush,
 * later ones only read the reason.
 *
 * SPDX-License-Identifier: MIT
 */
#include "esp_attr.h"
#include "esp_rom_sys.h"
#include "esp32p4/rom/cache.h"

/** @brief The ROM function the wrap stands in front of. */
soc_reset_reason_t __real_esp_rom_get_reset_reason(int cpu_no);
/** @brief The wrap's own declaration, for the warning on a missing one. */
soc_reset_reason_t __wrap_esp_rom_get_reset_reason(int cpu_no);

/** @brief Whether the flush is still to come.  In .data, not .bss: the
 *         first call comes before init_bss() clears .bss. */
static DRAM_ATTR int s_flush_due = 1;

/**
 * @brief The reset reason of @p cpu_no, after the one flush of the L1 data
 *        cache on the first call.
 * @param cpu_no The core.
 * @return The ROM's reset reason.
 */
IRAM_ATTR soc_reset_reason_t __wrap_esp_rom_get_reset_reason(int cpu_no)
{
    if (s_flush_due) {
        s_flush_due = 0;
        (void)Cache_WriteBack_Invalidate_All(CACHE_MAP_L1_DCACHE);
    }
    return __real_esp_rom_get_reset_reason(cpu_no);
}
