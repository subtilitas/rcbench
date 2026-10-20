/*
 * SPDX-License-Identifier: MIT
 */

#include "storage.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "board.h"
/* ff.h first: diskio_sdmmc.h uses its BYTE without including it. */
#include "ff.h"
#include "diskio_sdmmc.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

static const char *TAG = "storage";
/* The tag of ESP-IDF's FAT-on-SD mount (components/fatfs/vfs/
 * vfs_fat_sdmmc.c). */
#define STORAGE_IDF_MOUNT_TAG "vfs_fat_sdmmc"

static struct {
    sdmmc_card_t *card;
    bool bus_up;
    bool mounted;
    /* The volume label, or the CID's product name without one: 11 FAT
     * label characters take up to 33 bytes in UTF-8. */
    char name[40];
    /* The FatFs drive of the mounted card, "0:", for f_readdir(). */
    char drive[8];
    char status[48];
} s;

/*
 * The card's name: its volume label, e.g. "UNITEST", or the product name in
 * its CID (card identification register), e.g. "SC16G", when the volume has
 * no label or it does not read.
 *
 * Read once per mount, here, and kept: f_getlabel() reads the root
 * directory from its first sector until it meets the label's entry.  A
 * formatter writes that entry first, so a labelled card costs one 512-byte
 * sector read; a card without a label is read to the directory's end, 16
 * entries per sector, about 125 sectors for 999 runs and their reports.
 */
static void read_name(void)
{
    snprintf(s.name, sizeof(s.name), "%s", s.card->cid.name);
    if (s.drive[0] == '\0') {
        return;
    }
    /* FatFs asks for 34 bytes for a label in UTF-8 (ff.c, f_getlabel). */
    char label[34];
    label[0] = '\0';
    const FRESULT fr = f_getlabel(s.drive, label, NULL);
    if (fr != FR_OK) {
        ESP_LOGW(TAG, "volume label did not read: %d", (int)fr);
        return;
    }
    label[sizeof(label) - 1u] = '\0';
    if (label[0] != '\0') {
        snprintf(s.name, sizeof(s.name), "%s", label);
    }
}

/*
 * The mounted volume's format, for the mount's log line: FAT12, FAT16,
 * FAT32 or exFAT.  After storage_total_bytes(), whose f_getfree() counts a
 * FAT32 volume's free clusters once per mount when the volume does not say;
 * this call then reads the count FatFs keeps.
 */
static const char *volume_format(void)
{
    DWORD free_clusters = 0u;
    FATFS *fs = NULL;
    if (s.drive[0] == '\0'
        || f_getfree(s.drive, &free_clusters, &fs) != FR_OK || fs == NULL) {
        return "format unread";
    }
    switch (fs->fs_type) {
    case FS_FAT12: return "FAT12";
    case FS_FAT16: return "FAT16";
    case FS_FAT32: return "FAT32";
    case FS_EXFAT: return "exFAT";
    default:       return "format unread";
    }
}

esp_err_t storage_init(void)
{
    if (s.mounted) {
        return ESP_OK;
    }
    s.status[0] = '\0';

    /* The bus once: a retry after "no card" finds it up, and ESP-IDF logs
     * a second initialisation as an error before it returns
     * ESP_ERR_INVALID_STATE. */
    esp_err_t err = ESP_OK;
    if (!s.bus_up) {
        spi_bus_config_t bus = {
            .mosi_io_num = BOARD_SD_PIN_MOSI,
            .miso_io_num = BOARD_SD_PIN_MISO,
            .sclk_io_num = BOARD_SD_PIN_CLK,
            .quadwp_io_num = -1,
            .quadhd_io_num = -1,
            .max_transfer_sz = 4096,
        };
        err = spi_bus_initialize(BOARD_SD_SPI_HOST, &bus, SPI_DMA_CH_AUTO);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            snprintf(s.status, sizeof(s.status), "SPI %s",
                     esp_err_to_name(err));
            return err;
        }
        s.bus_up = true;
    }

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

    /*
     * ESP-IDF's mount logs every failure of its own at E level, "no card"
     * among them ("sdmmc_card_init failed (0x107)").  An empty slot is not
     * a fault: its tag is quiet for the call, and the outcome is logged
     * here, once per attempt.
     */
    const esp_log_level_t was = esp_log_level_get(STORAGE_IDF_MOUNT_TAG);
    esp_log_level_set(STORAGE_IDF_MOUNT_TAG, ESP_LOG_NONE);
    err = esp_vfs_fat_sdspi_mount(STORAGE_MOUNT_POINT, &host, &slot, &mount,
                                  &s.card);
    esp_log_level_set(STORAGE_IDF_MOUNT_TAG, was);
    if (err != ESP_OK) {
        board_sd_cs(false);
        if (err == ESP_ERR_TIMEOUT || err == ESP_ERR_NOT_FOUND ||
            err == ESP_ERR_INVALID_RESPONSE) {
            snprintf(s.status, sizeof(s.status), "no card");
            ESP_LOGI(TAG, "no card (%s)", esp_err_to_name(err));
            return ESP_ERR_NOT_FOUND;
        }
        snprintf(s.status, sizeof(s.status), "%s", esp_err_to_name(err));
        ESP_LOGW(TAG, "mount failed: %s", esp_err_to_name(err));
        return err;
    }

    s.mounted = true;
    s.drive[0] = '\0';
    const BYTE pdrv = ff_diskio_get_pdrv_card(s.card);
    if (pdrv != 0xffu) {
        snprintf(s.drive, sizeof(s.drive), "%u:", (unsigned)pdrv);
    }
    read_name();
    const unsigned long long mb = storage_total_bytes() / (1024ULL * 1024ULL);
    ESP_LOGI(TAG, "%s mounted, %s, %llu MB", s.name, volume_format(), mb);
    return ESP_OK;
}

void storage_deinit(void)
{
    if (s.mounted) {
        esp_vfs_fat_sdcard_unmount(STORAGE_MOUNT_POINT, s.card);
        s.card = NULL;
        s.mounted = false;
        s.drive[0] = '\0';
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

/* What one walk reads into: on the heap, because the walk runs on the task
 * that renders as well as on the run logger's, and a FILINFO with its long
 * name buffer is 280 bytes or more. */
typedef struct {
    FF_DIR dir;
    FILINFO info;
} walk_buf_t;

/* exFAT is on (firmware/panel/fatfs_exfat.cmake): a build that lost it
 * stops here rather than run without it.  A file's size then is FatFs's
 * 64-bit FSIZE_t, and goes into storage_entry_t and storage_stat_t as it
 * is. */
#if !defined(FF_FS_EXFAT) || FF_FS_EXFAT != 1
#error "FatFs without exFAT: see firmware/panel/fatfs_exfat.cmake"
#endif
_Static_assert(sizeof(FSIZE_t) <= sizeof(((storage_entry_t *)0)->size),
               "storage_entry_t.size holds every FatFs size");
_Static_assert(sizeof(FSIZE_t) <= sizeof(((storage_stat_t *)0)->size),
               "storage_stat_t.size holds every FatFs size");

int storage_walk(const char *dir, const char *suffixes,
                 storage_visit_fn visit, void *ctx, storage_tick_fn tick)
{
    if (!s.mounted || s.drive[0] == '\0') {
        return -1;
    }

    /*
     * FatFs's own directory read, by drive number, rather than opendir() and
     * readdir() through the VFS (virtual file system): f_readdir() fills the
     * entry's size and attributes in the same pass, and readdir() drops
     * them.  A size taken afterwards is an f_stat(), which looks the name up
     * from the directory's first entry, one 512-byte sector per 16 entries:
     * 48 sizes on a root of 999 runs were about 2900 sector reads.
     */
    char path[STORAGE_NAME_MAX * 2];
    if (dir == NULL || dir[0] == '\0' || strcmp(dir, "/") == 0) {
        snprintf(path, sizeof(path), "%s/", s.drive);
    } else {
        snprintf(path, sizeof(path), "%s/%s", s.drive, dir);
    }

    walk_buf_t *w = malloc(sizeof(*w));
    if (w == NULL) {
        ESP_LOGW(TAG, "no memory to list %s", path);
        return -1;
    }
    FRESULT fr = f_opendir(&w->dir, path);
    if (fr != FR_OK) {
        free(w);
        /* A folder not made yet is an empty list to the caller, not a
         * volume that is gone. */
        return (fr == FR_NO_PATH || fr == FR_NO_FILE) ? STORAGE_WALK_NO_DIR
                                                      : -1;
    }

    /*
     * The end of the directory and a read that failed are two answers: a
     * card pulled part way through a listing is a failure, and reporting it
     * as the end would tell the operator their logs had vanished rather than
     * that the card had.
     */
    int matched = 0;
    for (;;) {
        fr = f_readdir(&w->dir, &w->info);
        if (fr != FR_OK || w->info.fname[0] == '\0') {
            break;
        }
        const FILINFO *e = &w->info;
        /* Per entry read, before any filter.  What the filters reject never
         * reaches the visitor, and a root of unrelated files is the case that
         * takes longest with nothing else running. */
        if (tick != NULL) {
            tick();
        }
        if (e->fname[0] == '.') {
            continue; /* the metadata files a Mac leaves behind */
        }
        const bool is_dir = (e->fattrib & AM_DIR) != 0;
        if (!is_dir && !suffix_matches(e->fname, suffixes)) {
            continue;
        }
        size_t len = strlen(e->fname);
        if (len >= STORAGE_NAME_MAX) {
            /* A name that does not fit cannot be reopened, so a truncated
             * version is not shown. */
            ESP_LOGW(TAG, "skipping over-long name (%u chars)", (unsigned)len);
            continue;
        }

        storage_entry_t cur;
        memcpy(cur.name, e->fname, len + 1u);
        cur.is_dir = is_dir;
        cur.size = is_dir ? 0u : (uint64_t)e->fsize;
        ++matched;
        if (visit != NULL) {
            visit(&cur, ctx);
        }
    }
    (void)f_closedir(&w->dir);
    free(w);
    if (fr != FR_OK) {
        ESP_LOGW(TAG, "listing %s stopped: FatFs %d", path, (int)fr);
        return -1;              /* no volume, not an empty one */
    }

    return matched;
}

/* FAT's date and time stamp as local time, the way ESP-IDF's stat() reads
 * it (components/fatfs/vfs/vfs_fat.c, update_stat_struct()): the date's
 * bits 15..9 are years from 1980, 8..5 the month, 4..0 the day; the time's
 * 15..11 the hour, 10..5 the minute, 4..0 seconds in steps of 2. */
static int64_t fat_mtime(WORD fdate, WORD ftime)
{
    struct tm tm = {
        .tm_mday = (int)(fdate & 0x1fu),
        .tm_mon = (int)((fdate >> 5) & 0x0fu) - 1,
        .tm_year = (int)(fdate >> 9) + 80,
        .tm_sec = (int)(ftime & 0x1fu) * 2,
        .tm_min = (int)((ftime >> 5) & 0x3fu),
        .tm_hour = (int)((ftime >> 11) & 0x1fu),
        .tm_isdst = -1,
    };
    return (int64_t)mktime(&tm);
}

esp_err_t storage_stat(const char *path, storage_stat_t *out)
{
    if (path == NULL || out == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    const size_t mp = sizeof(STORAGE_MOUNT_POINT) - 1u;
    if (!s.mounted || s.drive[0] == '\0'
        || strncmp(path, STORAGE_MOUNT_POINT, mp) != 0
        || (path[mp] != '\0' && path[mp] != '/')) {
        return ESP_ERR_INVALID_STATE;
    }
    const char *rel = path + mp;            /* "" or "/..." */
    if (rel[0] == '\0' || strcmp(rel, "/") == 0) {
        /* f_stat() does not take a volume's root; it is a folder. */
        memset(out, 0, sizeof(*out));
        out->is_dir = true;
        return ESP_OK;
    }

    /* On the heap, as the walk's: a FILINFO with its long name buffer is
     * 280 bytes or more, and the run logger's task calls this. */
    const size_t need = strlen(s.drive) + strlen(rel) + 1u;
    typedef struct {
        FILINFO info;
        char path[];
    } stat_buf_t;
    stat_buf_t *b = malloc(sizeof(*b) + need);
    if (b == NULL) {
        return ESP_ERR_NO_MEM;
    }
    snprintf(b->path, need, "%s%s", s.drive, rel);
    const FRESULT fr = f_stat(b->path, &b->info);
    esp_err_t err = ESP_OK;
    if (fr == FR_OK) {
        out->is_dir = (b->info.fattrib & AM_DIR) != 0;
        out->size = out->is_dir ? 0u : (uint64_t)b->info.fsize;
        out->mtime = fat_mtime(b->info.fdate, b->info.ftime);
    } else if (fr == FR_NO_FILE || fr == FR_NO_PATH) {
        err = ESP_ERR_NOT_FOUND;
    } else {
        ESP_LOGW(TAG, "stat %s: FatFs %d", b->path, (int)fr);
        err = ESP_FAIL;
    }
    free(b);
    return err;
}
