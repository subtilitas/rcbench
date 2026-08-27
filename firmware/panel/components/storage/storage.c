/*
 * SPDX-License-Identifier: MIT
 */

#include "storage.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "board.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

static const char *TAG = "storage";

static struct {
    sdmmc_card_t *card;
    bool bus_up;
    bool mounted;
    char name[24];
    char status[48];
} s;

esp_err_t storage_init(void)
{
    if (s.mounted) {
        return ESP_OK;
    }
    s.status[0] = '\0';

    spi_bus_config_t bus = {
        .mosi_io_num = BOARD_SD_PIN_MOSI,
        .miso_io_num = BOARD_SD_PIN_MISO,
        .sclk_io_num = BOARD_SD_PIN_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };
    esp_err_t err = spi_bus_initialize(BOARD_SD_SPI_HOST, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        snprintf(s.status, sizeof(s.status), "SPI %s", esp_err_to_name(err));
        return err;
    }
    s.bus_up = true;

    /* Chip select is on the expander, and this is a read-modify-write: the
     * vendor demo's raw 0x0A to address 0x38 would also clear EXIO2 and take
     * the backlight down with it. */
    err = board_sd_cs(true);
    if (err != ESP_OK) {
        snprintf(s.status, sizeof(s.status), "CS %s", esp_err_to_name(err));
        return err;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = BOARD_SD_SPI_HOST;
    host.max_freq_khz = BOARD_SD_FREQ_KHZ;

    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.host_id = BOARD_SD_SPI_HOST;
    slot.gpio_cs = GPIO_NUM_NC; /* held by the expander, see board_pins.h */

    esp_vfs_fat_sdmmc_mount_config_t mount = {
        /* Never format on the bench: a card that will not mount is a card to
         * look at on a computer, not one to wipe. */
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 16 * 1024,
    };

    err = esp_vfs_fat_sdspi_mount(STORAGE_MOUNT_POINT, &host, &slot, &mount,
                                  &s.card);
    if (err != ESP_OK) {
        board_sd_cs(false);
        if (err == ESP_ERR_TIMEOUT || err == ESP_ERR_NOT_FOUND ||
            err == ESP_ERR_INVALID_RESPONSE) {
            snprintf(s.status, sizeof(s.status), "no card");
            return ESP_ERR_NOT_FOUND;
        }
        snprintf(s.status, sizeof(s.status), "%s", esp_err_to_name(err));
        ESP_LOGW(TAG, "mount failed: %s", esp_err_to_name(err));
        return err;
    }

    s.mounted = true;
    snprintf(s.name, sizeof(s.name), "%s", s.card->cid.name);
    ESP_LOGI(TAG, "%s mounted, %llu MB", s.name,
             (unsigned long long)(storage_total_bytes() / (1024ULL * 1024ULL)));
    return ESP_OK;
}

void storage_deinit(void)
{
    if (s.mounted) {
        esp_vfs_fat_sdcard_unmount(STORAGE_MOUNT_POINT, s.card);
        s.card = NULL;
        s.mounted = false;
        board_sd_cs(false);
    }
    if (s.bus_up) {
        spi_bus_free(BOARD_SD_SPI_HOST);
        s.bus_up = false;
    }
}

bool storage_mounted(void)
{
    return s.mounted;
}

const char *storage_card_name(void)
{
    return s.mounted ? s.name : "";
}

const char *storage_status(void)
{
    return s.status;
}

static bool fat_usage(uint64_t *total, uint64_t *freebytes)
{
    if (!s.mounted) {
        return false;
    }
    uint64_t t = 0;
    uint64_t f = 0;
    if (esp_vfs_fat_info(STORAGE_MOUNT_POINT, &t, &f) != ESP_OK) {
        return false;
    }
    *total = t;
    *freebytes = f;
    return true;
}

uint64_t storage_total_bytes(void)
{
    uint64_t t = 0;
    uint64_t f = 0;
    return fat_usage(&t, &f) ? t : 0;
}

uint64_t storage_free_bytes(void)
{
    uint64_t t = 0;
    uint64_t f = 0;
    return fat_usage(&t, &f) ? f : 0;
}

void storage_path(const char *dir, const char *name, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0u) {
        return;
    }
    if (dir == NULL || dir[0] == '\0' || strcmp(dir, "/") == 0) {
        snprintf(out, out_size, "%s/%s", STORAGE_MOUNT_POINT, name);
    } else {
        snprintf(out, out_size, "%s/%s/%s", STORAGE_MOUNT_POINT, dir, name);
    }
}

/* Case-insensitive suffix test against a space-separated list. */
static bool suffix_matches(const char *name, const char *suffixes)
{
    if (suffixes == NULL || suffixes[0] == '\0') {
        return true;
    }
    size_t n = strlen(name);
    const char *p = suffixes;
    while (*p != '\0') {
        while (*p == ' ') {
            ++p;
        }
        const char *end = p;
        while (*end != '\0' && *end != ' ') {
            ++end;
        }
        size_t len = (size_t)(end - p);
        if (len > 0u && len <= n) {
            const char *tail = name + (n - len);
            size_t i = 0;
            while (i < len && tolower((unsigned char)tail[i]) == p[i]) {
                ++i;
            }
            if (i == len) {
                return true;
            }
        }
        p = end;
    }
    return false;
}

static int compare_entries(const void *a, const void *b)
{
    const storage_entry_t *x = (const storage_entry_t *)a;
    const storage_entry_t *y = (const storage_entry_t *)b;
    if (x->is_dir != y->is_dir) {
        return x->is_dir ? -1 : 1;
    }
    return strcasecmp(x->name, y->name);
}

int storage_list(const char *dir, const char *suffixes, storage_entry_t *out,
                 int max_entries)
{
    if (!s.mounted || out == NULL || max_entries <= 0) {
        return -1;
    }

    char path[STORAGE_NAME_MAX * 2];
    if (dir == NULL || dir[0] == '\0' || strcmp(dir, "/") == 0) {
        snprintf(path, sizeof(path), "%s", STORAGE_MOUNT_POINT);
    } else {
        snprintf(path, sizeof(path), "%s/%s", STORAGE_MOUNT_POINT, dir);
    }

    DIR *d = opendir(path);
    if (d == NULL) {
        return -1;
    }

    int n = 0;
    struct dirent *e;
    while (n < max_entries && (e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') {
            continue; /* ".", ".." and the metadata files a Mac leaves behind */
        }
        bool is_dir = (e->d_type == DT_DIR);
        if (!is_dir && !suffix_matches(e->d_name, suffixes)) {
            continue;
        }
        size_t len = strlen(e->d_name);
        if (len >= STORAGE_NAME_MAX) {
            /* A name that will not fit is a name that cannot be reopened, so
             * showing a truncated version of it would be a lie. */
            ESP_LOGW(TAG, "skipping over-long name (%u chars)", (unsigned)len);
            continue;
        }

        memcpy(out[n].name, e->d_name, len + 1u);
        out[n].is_dir = is_dir;
        out[n].size = 0;

        if (!is_dir) {
            char full[STORAGE_NAME_MAX * 3];
            int w = snprintf(full, sizeof(full), "%s/%s", path, e->d_name);
            struct stat st;
            if (w > 0 && (size_t)w < sizeof(full) && stat(full, &st) == 0) {
                out[n].size = (uint32_t)st.st_size;
            }
        }
        ++n;
    }
    closedir(d);

    qsort(out, (size_t)n, sizeof(out[0]), compare_entries);
    return n;
}
