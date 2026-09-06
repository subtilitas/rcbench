/*
 * The board-photograph partition, as an art_flash_t. See art_flash_esp.h.
 *
 * Nothing here decides anything: art_store owns the format and the order of
 * a write, and this is the three operations it needs on real flash.
 *
 * SPDX-License-Identifier: MIT
 */

#include "art_flash_esp.h"

#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_partition.h"
#include "spi_flash_mmap.h"   /* SPI_FLASH_SEC_SIZE */

static const char *TAG = "artflash";

static const esp_partition_t *s_part;

/*
 * In pieces, with a yield between them, and this is the whole point of the
 * file.
 *
 * CONFIG_SPI_FLASH_AUTO_SUSPEND is off, so a flash operation disables the
 * cache and stalls *both* cores for its duration: code that is not in IRAM
 * does not run anywhere. control_task is not in IRAM, and it is the task
 * that beats the safety line and polls the bench.
 *
 * A quarter-megabyte erase in one call therefore stops the heartbeat for as
 * long as it takes, the coprocessor's 200 ms silence timer trips, the
 * panel's own 1 s host timeout trips, and the screen says NO LINK while the
 * cable is fine. Putting the work on another task does not help: the stall
 * is the cache, not the scheduler.
 *
 * So every operation is broken into one sector, and the task doing it gives
 * the scheduler a tick in between. One 4 kB sector erase is tens of
 * milliseconds, which fits inside the heartbeat's 150 ms ceiling with room;
 * a whole slot does not.
 */
#define ART_FLASH_CHUNK 4096u

/* Let whatever was waiting run, and let the cache come back on. */
static void breathe(void)
{
    vTaskDelay(pdMS_TO_TICKS(2));
}

static bool part_read(void *ctx, uint32_t off, void *dst, uint32_t len)
{
    (void)ctx;
    if (s_part == NULL) {
        return false;
    }
    uint8_t *at = (uint8_t *)dst;
    while (len > 0u) {
        const uint32_t n = (len > ART_FLASH_CHUNK) ? ART_FLASH_CHUNK : len;
        if (esp_partition_read(s_part, off, at, n) != ESP_OK) {
            return false;
        }
        off += n;
        at  += n;
        len -= n;
        if (len > 0u) {
            breathe();
        }
    }
    return true;
}

static bool part_erase(void *ctx, uint32_t off, uint32_t len)
{
    (void)ctx;
    if (s_part == NULL) {
        return false;
    }
    while (len > 0u) {
        const uint32_t n = (len > SPI_FLASH_SEC_SIZE) ? SPI_FLASH_SEC_SIZE
                                                      : len;
        if (esp_partition_erase_range(s_part, off, n) != ESP_OK) {
            return false;
        }
        off += n;
        len -= n;
        if (len > 0u) {
            breathe();
        }
    }
    return true;
}

static bool part_write(void *ctx, uint32_t off, const void *src, uint32_t len)
{
    (void)ctx;
    if (s_part == NULL) {
        return false;
    }
    const uint8_t *at = (const uint8_t *)src;
    while (len > 0u) {
        const uint32_t n = (len > ART_FLASH_CHUNK) ? ART_FLASH_CHUNK : len;
        if (esp_partition_write(s_part, off, at, n) != ESP_OK) {
            return false;
        }
        off += n;
        at  += n;
        len -= n;
        if (len > 0u) {
            breathe();
        }
    }
    return true;
}

const art_flash_t *art_flash_esp(void)
{
    static art_flash_t s_flash;
    static bool        s_looked;

    if (!s_looked) {
        s_looked = true;
        s_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                          (esp_partition_subtype_t)0x40,
                                          "boardart");
        if (s_part == NULL) {
            /* An older partition table.  Pictures are then fetched on every
             * link-up, which is slow and correct. */
            ESP_LOGW(TAG, "no boardart partition; photographs will not be kept");
        } else {
            s_flash.read   = part_read;
            s_flash.erase  = part_erase;
            s_flash.write  = part_write;
            s_flash.ctx    = NULL;
            s_flash.size   = s_part->size;
            s_flash.sector = SPI_FLASH_SEC_SIZE;
            ESP_LOGI(TAG, "%u kB for board photographs, %u slots",
                     (unsigned)(s_part->size / 1024u),
                     (unsigned)art_store_slots(&s_flash));
        }
    }
    return (s_part != NULL) ? &s_flash : NULL;
}
