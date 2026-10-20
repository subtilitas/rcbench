/*
 * See storage_fake.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "storage_fake.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

log_line_t g_log[LOG_MAX];
unsigned   g_log_n;
esp_log_level_t g_mount_tag_level;
esp_log_level_t g_disk_tag_level;
esp_log_level_t g_cmd_tag_level;
esp_log_level_t g_default_level;
unsigned   g_bus_inits;
bool       g_bus_up;
esp_err_t  g_bus_fail;
esp_err_t  g_cs_fail;
bool       g_cs;
bool       g_card;
esp_err_t  g_card_start;
FRESULT    g_card_fs;
int        g_probe_fs;
bool       g_probe_card_gone;
esp_err_t  g_dev_fail;
bool       g_drives_full;
unsigned   g_dev_adds;
int        g_dev_live;
int        g_drive_live;
int        g_fs_live;
bool       g_cs_in_probe;
static sdmmc_card_t g_sd;
uint64_t   g_total;
BYTE       g_fs_type;
FRESULT    g_getfree;
FRESULT    g_stat_fail;
bool       g_live;
FRESULT    g_read_fail;
const char *g_label;
unsigned   g_mounts;
unsigned   g_unmounts;
unsigned   g_status_asks;
bool       g_status_check;
unsigned   g_lock_misuse;
bool       g_lock_taken;

const vol_entry_t k_vol[4] = {
    { "EDGE1.CSV", 0xFFFFFFFFull,  false },  /* FAT32's largest file */
    { "EDGE2.CSV", 0x100000000ull, false },  /* exFAT only */
    { "HUGE.CSV",  0x200000000ull, false },
    { "SUB",       0u,             true  },
};

const char *esp_err_to_name(esp_err_t err)
{
    switch (err) {
    case ESP_OK:                return "ESP_OK";
    case ESP_ERR_NO_MEM:        return "ESP_ERR_NO_MEM";
    case ESP_ERR_INVALID_STATE: return "ESP_ERR_INVALID_STATE";
    case ESP_ERR_TIMEOUT:       return "ESP_ERR_TIMEOUT";
    case ESP_ERR_NOT_FOUND:     return "ESP_ERR_NOT_FOUND";
    case ESP_ERR_INVALID_CRC:   return "ESP_ERR_INVALID_CRC";
    case ESP_ERR_LONG_NAME:
        return "ESP_ERR_A_NAME_FAR_LONGER_THAN_ANY_FIELD_IN_STORAGE_C_HOLDS";
    default:                    return "ESP_FAIL";
    }
}

static esp_log_level_t *level_of(const char *tag)
{
    if (strcmp(tag, "vfs_fat_sdmmc") == 0) {
        return &g_mount_tag_level;
    }
    if (strcmp(tag, "diskio_sdmmc") == 0) {
        return &g_disk_tag_level;
    }
    if (strcmp(tag, "sdmmc_cmd") == 0) {
        return &g_cmd_tag_level;
    }
    return &g_default_level;
}

void esp_log_level_set(const char *tag, esp_log_level_t level)
{
    *level_of(tag) = level;
}

esp_log_level_t esp_log_level_get(const char *tag)
{
    return *level_of(tag);
}

void fake_log(esp_log_level_t level, const char *tag, const char *text)
{
    if (level > *level_of(tag) || g_log_n >= LOG_MAX) {
        return;                         /* not printed */
    }
    log_line_t *l = &g_log[g_log_n++];
    l->level = level;
    snprintf(l->tag, sizeof(l->tag), "%s", tag);
    snprintf(l->text, sizeof(l->text), "%s", text);
}

esp_err_t board_sd_cs(bool on)
{
    if (on && g_cs_fail != ESP_OK) {
        const esp_err_t e = g_cs_fail;
        g_cs_fail = ESP_OK;
        return e;
    }
    g_cs = on;
    return ESP_OK;
}

esp_err_t spi_bus_initialize(int host, const spi_bus_config_t *cfg, int dma)
{
    (void)host;
    (void)cfg;
    (void)dma;
    ++g_bus_inits;
    if (g_bus_fail != ESP_OK) {
        const esp_err_t e = g_bus_fail;
        g_bus_fail = ESP_OK;
        return e;
    }
    if (g_bus_up) {
        ESP_LOGE("spi", "spi_bus_initialize(802): SPI bus already initialized.");
        return ESP_ERR_INVALID_STATE;
    }
    g_bus_up = true;
    return ESP_OK;
}

esp_err_t spi_bus_free(int host)
{
    (void)host;
    g_bus_up = false;
    return ESP_OK;
}

/* The card's start-up, as both the mount and the second look run it: no
 * answer without a card or without chip select. */
static esp_err_t card_start(void)
{
    if (!g_bus_up) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!g_card || !g_cs) {
        return ESP_ERR_TIMEOUT;
    }
    return g_card_start;
}

esp_err_t esp_vfs_fat_sdspi_mount(const char *base, const sdmmc_host_t *host,
                                  const sdspi_device_config_t *slot,
                                  const esp_vfs_fat_sdmmc_mount_config_t *cfg,
                                  sdmmc_card_t **out)
{
    (void)base;
    (void)host;
    (void)slot;
    (void)cfg;
    const esp_err_t start = card_start();
    if (start != ESP_OK) {
        ESP_LOGE("vfs_fat_sdmmc", "sdmmc_card_init failed (0x%x).", start);
        return start;
    }
    if (g_card_fs != FR_OK) {
        ESP_LOGW("vfs_fat_sdmmc", "failed to mount card (%d)", (int)g_card_fs);
        ESP_LOGE("vfs_fat_sdmmc", "mount_to_vfs failed (0x%x).", ESP_FAIL);
        return ESP_FAIL;
    }
    snprintf(g_sd.cid.name, sizeof(g_sd.cid.name), "SC16G");
    *out = &g_sd;
    g_live = true;
    ++g_mounts;
    return ESP_OK;
}

esp_err_t sdspi_host_init_device(const sdspi_device_config_t *cfg,
                                 sdspi_dev_handle_t *out)
{
    (void)cfg;
    ++g_dev_adds;
    if (g_dev_fail != ESP_OK) {
        const esp_err_t e = g_dev_fail;
        g_dev_fail = ESP_OK;
        return e;
    }
    ++g_dev_live;
    *out = 0;
    return ESP_OK;
}

esp_err_t sdspi_host_remove_device(sdspi_dev_handle_t handle)
{
    (void)handle;
    --g_dev_live;
    return ESP_OK;
}

esp_err_t sdmmc_card_init(const sdmmc_host_t *host, sdmmc_card_t *card)
{
    (void)host;
    (void)card;
    g_cs_in_probe = g_cs;
    if (g_probe_card_gone) {
        g_card = false;
    }
    return card_start();
}

esp_err_t ff_diskio_get_drive(BYTE *out_pdrv)
{
    if (g_drives_full) {
        return ESP_ERR_NOT_FOUND;
    }
    *out_pdrv = 0u;
    return ESP_OK;
}

void ff_diskio_register_sdmmc(unsigned char pdrv, sdmmc_card_t *card)
{
    (void)pdrv;
    (void)card;
    ++g_drive_live;
}

void fake_diskio_unregister(BYTE pdrv)
{
    (void)pdrv;
    --g_drive_live;
}

FRESULT f_mount(FATFS *fs, const char *path, BYTE opt)
{
    (void)opt;
    if (path == NULL || strcmp(path, "0:") != 0) {
        return FR_INT_ERR;
    }
    if (fs == NULL) {
        g_fs_live = 0;
        return FR_OK;
    }
    if (g_drive_live <= 0) {
        return FR_NOT_READY;            /* no drive behind "0:" */
    }
    fs->win[0] = 0u;                    /* the work area takes a sector */
    g_fs_live = 1;
    return (g_probe_fs >= 0) ? (FRESULT)g_probe_fs : g_card_fs;
}

esp_err_t esp_vfs_fat_sdcard_unmount(const char *base, sdmmc_card_t *card)
{
    (void)base;
    (void)card;
    ++g_unmounts;
    g_live = false;
    return ESP_OK;
}

void ff_sdmmc_set_disk_status_check(BYTE pdrv, bool enable)
{
    (void)pdrv;
    g_status_check = enable;
}

/* A read of the mounted card: FR_DISK_ERR and ESP-IDF's E line when the
 * card it was mounted from is gone, the injected failure otherwise. */
static FRESULT card_read(void)
{
    if (!g_live) {
        ESP_LOGE("sdmmc_cmd", "sdmmc_read_sectors_dma: sdmmc_send_cmd "
                              "returned 0x107");
        ESP_LOGE("diskio_sdmmc", "sdmmc_read_blocks failed (0x107)");
        return FR_DISK_ERR;
    }
    return g_read_fail;
}

SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    static StaticSemaphore_t sem;
    sem.taken = 0;
    return &sem;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t wait)
{
    (void)wait;
    if (sem->taken != 0) {
        ++g_lock_misuse;            /* a deadlock on the panel */
    }
    sem->taken = 1;
    g_lock_taken = true;
    return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t sem)
{
    if (sem->taken == 0) {
        ++g_lock_misuse;
    }
    sem->taken = 0;
    g_lock_taken = false;
    return pdTRUE;
}

esp_err_t esp_vfs_fat_info(const char *base, uint64_t *total, uint64_t *free)
{
    (void)base;
    *total = g_total;
    *free  = *total / 2u;
    return ESP_OK;
}

BYTE ff_diskio_get_pdrv_card(const sdmmc_card_t *card)
{
    (void)card;
    return 0u;
}

FRESULT f_getlabel(const char *path, char *label, uint32_t *vsn)
{
    (void)path;
    (void)vsn;
    snprintf(label, 34, "%s", (g_label != NULL) ? g_label : "UNITEST");
    return FR_OK;
}

FRESULT f_getfree(const char *path, DWORD *nclst, FATFS **fatfs)
{
    static FATFS fs;
    (void)path;
    *nclst = 0u;
    fs.fs_type = g_fs_type;
    *fatfs = &fs;
    if (g_status_check) {
        ++g_status_asks;
        if (!g_live) {
            /* FatFs asks again at the volume's start-up, and both fail. */
            ESP_LOGE("diskio_sdmmc", "Check status failed (0x107)");
            ESP_LOGE("diskio_sdmmc", "Check status failed (0x107)");
            return FR_NOT_READY;
        }
    }
    return g_getfree;
}

FRESULT f_opendir(FF_DIR *dp, const char *path)
{
    dp->pos = 0;
    const FRESULT fr = card_read();
    if (fr != FR_OK) {
        return fr;
    }
    return (strcmp(path, VOL_DIR) == 0) ? FR_OK : FR_NO_PATH;
}

static void vol_fill(FILINFO *fno, const vol_entry_t *e)
{
    memset(fno, 0, sizeof(*fno));
    snprintf(fno->fname, sizeof(fno->fname), "%s", e->name);
    fno->fattrib = e->dir ? AM_DIR : 0u;
    fno->fsize = e->dir ? 0u : e->size;
    fno->fdate = VOL_FDATE;
    fno->ftime = VOL_FTIME;
}

FRESULT f_readdir(FF_DIR *dp, FILINFO *fno)
{
    const FRESULT fr = card_read();
    if (fr != FR_OK) {
        return fr;
    }
    if ((size_t)dp->pos >= VOL_N) {
        fno->fname[0] = '\0';
        return FR_OK;
    }
    vol_fill(fno, &k_vol[dp->pos++]);
    return FR_OK;
}

FRESULT f_closedir(FF_DIR *dp)
{
    (void)dp;
    return FR_OK;
}

FRESULT f_stat(const char *path, FILINFO *fno)
{
    const FRESULT fr = card_read();
    if (fr != FR_OK) {
        return fr;
    }
    if (strcmp(path, VOL_DIR) == 0) {
        static const vol_entry_t dir = { "BIG", 0u, true };
        vol_fill(fno, &dir);
        return FR_OK;
    }
    const size_t n = strlen(VOL_DIR);
    if (strncmp(path, VOL_DIR "/", n + 1u) != 0) {
        return FR_NO_PATH;
    }
    for (size_t i = 0u; i < VOL_N; ++i) {
        if (strcmp(path + n + 1u, k_vol[i].name) == 0) {
            if (g_stat_fail != FR_OK) {
                return g_stat_fail;
            }
            vol_fill(fno, &k_vol[i]);
            return FR_OK;
        }
    }
    return FR_NO_FILE;
}


void slot_empty(void)
{
    storage_deinit();
    memset(g_log, 0, sizeof(g_log));
    g_log_n = 0u;
    g_mount_tag_level = ESP_LOG_INFO;
    g_disk_tag_level  = ESP_LOG_INFO;
    g_cmd_tag_level   = ESP_LOG_INFO;
    g_default_level   = ESP_LOG_INFO;
    g_bus_inits  = 0u;
    g_bus_up     = false;
    g_bus_fail   = ESP_OK;
    g_cs_fail    = ESP_OK;
    g_cs         = false;
    g_card       = false;
    g_card_start = ESP_OK;
    g_card_fs    = FR_OK;
    g_probe_fs   = -1;
    g_probe_card_gone = false;
    g_dev_fail   = ESP_OK;
    g_drives_full = false;
    g_dev_adds   = 0u;
    g_dev_live   = 0;
    g_drive_live = 0;
    g_fs_live    = 0;
    g_cs_in_probe = false;
    g_total      = 16ull * 1024u * 1024u * 1024u;
    g_fs_type    = FS_FAT32;
    g_getfree    = FR_OK;
    g_stat_fail  = FR_OK;
    g_live       = false;
    g_read_fail  = FR_OK;
    g_label      = "UNITEST";
    g_mounts     = 0u;
    g_unmounts   = 0u;
    g_status_asks = 0u;
    g_status_check = false;
    g_lock_misuse = 0u;
}

/* The second look let go of all it took, and chip select with it. */
bool probe_left_nothing(void)
{
    return g_dev_live == 0 && g_drive_live == 0 && g_fs_live == 0 && !g_cs;
}

void card_in(BYTE fs_type, uint64_t total)
{
    slot_empty();
    g_card = true;
    g_fs_type = fs_type;
    g_total = total;
    if (storage_init() != ESP_OK) {
        fprintf(stderr, "card_in: the card did not mount\n");
        abort();
    }
}

/* The last line logged, or an empty one with none. */
const log_line_t *last_line(void)
{
    static const log_line_t none;
    return (g_log_n > 0u) ? &g_log[g_log_n - 1u] : &none;
}

/* What mktime() makes of VOL_FDATE and VOL_FTIME, in the same zone. */
int64_t vol_mtime(void)
{
    struct tm tm = {
        .tm_year = 2024 - 1900, .tm_mon = 5 - 1, .tm_mday = 17,
        .tm_hour = 12, .tm_min = 34, .tm_sec = 56, .tm_isdst = -1,
    };
    return (int64_t)mktime(&tm);
}

void see(const storage_entry_t *e, void *ctx)
{
    seen_t *v = ctx;
    if (v->n < sizeof(v->got) / sizeof(v->got[0])) {
        v->got[v->n++] = *e;
    }
}

unsigned lines_at(esp_log_level_t level)
{
    unsigned n = 0u;
    for (unsigned i = 0u; i < g_log_n; ++i) {
        n += (g_log[i].level == level) ? 1u : 0u;
    }
    return n;
}
