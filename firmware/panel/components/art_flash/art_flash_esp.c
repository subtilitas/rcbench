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

#include "esp_log.h"
#include "esp_partition.h"
#include "spi_flash_mmap.h"   /* SPI_FLASH_SEC_SIZE */

static const char *TAG = "artflash";

static const esp_partition_t *s_part;

static bool part_read(void *ctx, uint32_t off, void *dst, uint32_t len)
{
    (void)ctx;
    return s_part != NULL
           && esp_partition_read(s_part, off, dst, len) == ESP_OK;
}

static bool part_erase(void *ctx, uint32_t off, uint32_t len)
{
    (void)ctx;
    return s_part != NULL
           && esp_partition_erase_range(s_part, off, len) == ESP_OK;
}

static bool part_write(void *ctx, uint32_t off, const void *src, uint32_t len)
{
    (void)ctx;
    return s_part != NULL
           && esp_partition_write(s_part, off, src, len) == ESP_OK;
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
