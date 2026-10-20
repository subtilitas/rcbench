/*
 * The panel's SD card mount (firmware/panel/components/storage/storage.c)
 * over the ESP-IDF calls of storage_stub/esp_fake.h.
 *
 * Under test: a mount tried again with no card -- the splash, RESCAN, an
 * opening of LOGS -- initialises the SPI bus once and logs the missing card
 * once per try at I level, with no E line from storage.c, from the SPI
 * driver or from ESP-IDF's mount; the mount's own log tag is back at its
 * level after each try.  A card put in later mounts; a bus that failed to
 * come up is tried again; a bus freed by storage_deinit() is set up again;
 * a mount that fails for another reason is said at W level.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "greatest.h"

#include "esp_fake.h"
#include "storage.h"

/* --------------------------------------------------------------- fakes */

#define LOG_MAX 32
typedef struct {
    esp_log_level_t level;
    char tag[24];
    char text[96];
} log_line_t;

static log_line_t g_log[LOG_MAX];
static unsigned   g_log_n;
static esp_log_level_t g_mount_tag_level;   /* "vfs_fat_sdmmc" */
static esp_log_level_t g_default_level;
static unsigned   g_bus_inits;              /* spi_bus_initialize() calls */
static bool       g_bus_up;
static esp_err_t  g_bus_fail;               /* what the next init returns */
static bool       g_card;                   /* a card in the slot */
static esp_err_t  g_mount_fail;             /* a card that will not mount */
static sdmmc_card_t g_sd;
static uint64_t   g_total;                  /* the volume's bytes */
static BYTE       g_fs_type;                /* FS_FAT12 .. FS_EXFAT */
static FRESULT    g_getfree;                /* what f_getfree() answers */
static FRESULT    g_stat_fail;              /* f_stat() on a listed name */

/* One folder of the volume, "BIG", as FatFs reads it. */
#define VOL_DIR "0:/BIG"
typedef struct {
    const char *name;
    uint64_t size;
    bool dir;
} vol_entry_t;
static const vol_entry_t k_vol[] = {
    { "EDGE1.CSV", 0xFFFFFFFFull,  false },  /* FAT32's largest file */
    { "EDGE2.CSV", 0x100000000ull, false },  /* exFAT only */
    { "HUGE.CSV",  0x200000000ull, false },
    { "SUB",       0u,             true  },
};
#define VOL_N (sizeof(k_vol) / sizeof(k_vol[0]))
/* 2024-05-17 12:34:56 as FAT stamps it: years from 1980, seconds / 2. */
#define VOL_FDATE ((WORD)(((2024 - 1980) << 9) | (5 << 5) | 17))
#define VOL_FTIME ((WORD)((12 << 11) | (34 << 5) | (56 / 2)))

const char *esp_err_to_name(esp_err_t err)
{
    switch (err) {
    case ESP_OK:                return "ESP_OK";
    case ESP_ERR_NO_MEM:        return "ESP_ERR_NO_MEM";
    case ESP_ERR_INVALID_STATE: return "ESP_ERR_INVALID_STATE";
    case ESP_ERR_TIMEOUT:       return "ESP_ERR_TIMEOUT";
    case ESP_ERR_NOT_FOUND:     return "ESP_ERR_NOT_FOUND";
    default:                    return "ESP_FAIL";
    }
}

static esp_log_level_t *level_of(const char *tag)
{
    return (strcmp(tag, "vfs_fat_sdmmc") == 0) ? &g_mount_tag_level
                                               : &g_default_level;
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
    (void)on;
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

esp_err_t esp_vfs_fat_sdspi_mount(const char *base, const sdmmc_host_t *host,
                                  const sdspi_device_config_t *slot,
                                  const esp_vfs_fat_sdmmc_mount_config_t *cfg,
                                  sdmmc_card_t **out)
{
    (void)base;
    (void)host;
    (void)slot;
    (void)cfg;
    if (!g_bus_up) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!g_card) {
        ESP_LOGE("vfs_fat_sdmmc", "sdmmc_card_init failed (0x%x).",
                 ESP_ERR_TIMEOUT);
        return ESP_ERR_TIMEOUT;
    }
    if (g_mount_fail != ESP_OK) {
        ESP_LOGW("vfs_fat_sdmmc", "failed to mount card (13)");
        ESP_LOGE("vfs_fat_sdmmc", "mount_to_vfs failed (0x%x).",
                 g_mount_fail);
        return g_mount_fail;
    }
    snprintf(g_sd.cid.name, sizeof(g_sd.cid.name), "SC16G");
    *out = &g_sd;
    return ESP_OK;
}

esp_err_t esp_vfs_fat_sdcard_unmount(const char *base, sdmmc_card_t *card)
{
    (void)base;
    (void)card;
    return ESP_OK;
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
    snprintf(label, 34, "UNITEST");
    return FR_OK;
}

FRESULT f_getfree(const char *path, DWORD *nclst, FATFS **fatfs)
{
    static FATFS fs;
    (void)path;
    *nclst = 0u;
    fs.fs_type = g_fs_type;
    *fatfs = &fs;
    return g_getfree;
}

FRESULT f_opendir(FF_DIR *dp, const char *path)
{
    dp->pos = 0;
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

/* ------------------------------------------------------------- helpers */

static void slot_empty(void)
{
    storage_deinit();
    memset(g_log, 0, sizeof(g_log));
    g_log_n = 0u;
    g_mount_tag_level = ESP_LOG_INFO;
    g_default_level   = ESP_LOG_INFO;
    g_bus_inits  = 0u;
    g_bus_up     = false;
    g_bus_fail   = ESP_OK;
    g_card       = false;
    g_mount_fail = ESP_OK;
    g_total      = 16ull * 1024u * 1024u * 1024u;
    g_fs_type    = FS_FAT32;
    g_getfree    = FR_OK;
    g_stat_fail  = FR_OK;
}

static void card_in(BYTE fs_type, uint64_t total)
{
    slot_empty();
    g_card = true;
    g_fs_type = fs_type;
    g_total = total;
    CHECK_EQ(storage_init(), ESP_OK);
}

/* The last line logged, or an empty one with none. */
static const log_line_t *last_line(void)
{
    static const log_line_t none;
    return (g_log_n > 0u) ? &g_log[g_log_n - 1u] : &none;
}

/* What mktime() makes of VOL_FDATE and VOL_FTIME, in the same zone. */
static int64_t vol_mtime(void)
{
    struct tm tm = {
        .tm_year = 2024 - 1900, .tm_mon = 5 - 1, .tm_mday = 17,
        .tm_hour = 12, .tm_min = 34, .tm_sec = 56, .tm_isdst = -1,
    };
    return (int64_t)mktime(&tm);
}

typedef struct {
    storage_entry_t got[8];
    unsigned n;
} seen_t;

static void see(const storage_entry_t *e, void *ctx)
{
    seen_t *v = ctx;
    if (v->n < sizeof(v->got) / sizeof(v->got[0])) {
        v->got[v->n++] = *e;
    }
}

static unsigned lines_at(esp_log_level_t level)
{
    unsigned n = 0u;
    for (unsigned i = 0u; i < g_log_n; ++i) {
        n += (g_log[i].level == level) ? 1u : 0u;
    }
    return n;
}

/* --------------------------------------------------------------- cases */

TEST_CASE(a_mount_tried_again_with_no_card_sets_the_bus_up_once)
{
    slot_empty();
    for (unsigned k = 1u; k <= 3u; ++k) {
        CHECK_EQ(storage_init(), ESP_ERR_NOT_FOUND);
        CHECK(!storage_mounted());
        CHECK_STR_EQ(storage_status(), "no card");
        CHECK_EQ(g_bus_inits, 1u);
        /* One line a try, at I level, and no E line from anyone. */
        CHECK_EQ(lines_at(ESP_LOG_ERROR), 0u);
        CHECK_EQ(lines_at(ESP_LOG_INFO), k);
        CHECK_STR_EQ(g_log[k - 1u].tag, "storage");
        CHECK(strstr(g_log[k - 1u].text, "no card") != NULL);
        /* The mount's tag is back where it was. */
        CHECK_EQ(g_mount_tag_level, ESP_LOG_INFO);
    }
}

TEST_CASE(a_card_put_in_later_mounts_on_the_bus_already_up)
{
    slot_empty();
    CHECK_EQ(storage_init(), ESP_ERR_NOT_FOUND);
    g_card = true;
    CHECK_EQ(storage_init(), ESP_OK);
    CHECK(storage_mounted());
    CHECK_STR_EQ(storage_card_name(), "UNITEST");
    CHECK_EQ(g_bus_inits, 1u);
    CHECK_EQ(lines_at(ESP_LOG_ERROR), 0u);
    /* Mounted: another call does nothing. */
    CHECK_EQ(storage_init(), ESP_OK);
    CHECK_EQ(g_bus_inits, 1u);
}

TEST_CASE(a_bus_freed_or_failed_is_set_up_again)
{
    slot_empty();
    g_bus_fail = ESP_ERR_NO_MEM;
    CHECK_EQ(storage_init(), ESP_ERR_NO_MEM);
    CHECK_STR_EQ(storage_status(), "SPI ESP_ERR_NO_MEM");
    CHECK_EQ(storage_init(), ESP_ERR_NOT_FOUND);
    CHECK_EQ(g_bus_inits, 2u);
    /* Freed: the next try sets it up again, and only once. */
    storage_deinit();
    CHECK(!g_bus_up);
    CHECK_EQ(storage_init(), ESP_ERR_NOT_FOUND);
    CHECK_EQ(storage_init(), ESP_ERR_NOT_FOUND);
    CHECK_EQ(g_bus_inits, 3u);
    CHECK_EQ(lines_at(ESP_LOG_ERROR), 0u);
}

TEST_CASE(a_bus_another_driver_set_up_is_taken_as_up)
{
    slot_empty();
    g_bus_up = true;                    /* not by storage_init() */
    CHECK_EQ(storage_init(), ESP_ERR_NOT_FOUND);
    CHECK_EQ(g_bus_inits, 1u);
    CHECK_EQ(storage_init(), ESP_ERR_NOT_FOUND);
    CHECK_EQ(g_bus_inits, 1u);
}

TEST_CASE(a_card_that_will_not_mount_is_said_at_w_level)
{
    slot_empty();
    g_card = true;
    g_mount_fail = ESP_FAIL;
    CHECK_EQ(storage_init(), ESP_FAIL);
    CHECK(!storage_mounted());
    CHECK_EQ(lines_at(ESP_LOG_ERROR), 0u);
    CHECK_EQ(lines_at(ESP_LOG_WARN), 1u);
    CHECK_STR_EQ(g_log[0].tag, "storage");
    CHECK_EQ(g_mount_tag_level, ESP_LOG_INFO);
    /* A tag set quieter or louder before is left as it was found. */
    g_mount_tag_level = ESP_LOG_DEBUG;
    CHECK_EQ(storage_init(), ESP_FAIL);
    CHECK_EQ(g_mount_tag_level, ESP_LOG_DEBUG);
}

/* The card the station mounted: SDHC by its CSD, one MBR partition of
 * 124,964,864 sectors formatted exFAT, "SK64G mounted, 61002 MB". */
TEST_CASE(an_exfat_card_mounts_and_its_line_names_the_format)
{
    card_in(FS_EXFAT, 61002ull * 1024u * 1024u);
    CHECK(storage_mounted());
    CHECK_EQ(lines_at(ESP_LOG_ERROR), 0u);
    const log_line_t *l = last_line();
    CHECK_STR_EQ(l->tag, "storage");
    CHECK_STR_EQ(l->text, "UNITEST mounted, exFAT, 61002 MB");
    CHECK(storage_total_bytes() == 61002ull * 1024u * 1024u);
}

TEST_CASE(every_fat_format_is_named_and_an_unread_one_says_so)
{
    static const struct {
        BYTE type;
        const char *line;
    } k[] = {
        { FS_FAT12, "UNITEST mounted, FAT12, 16384 MB" },
        { FS_FAT16, "UNITEST mounted, FAT16, 16384 MB" },
        { FS_FAT32, "UNITEST mounted, FAT32, 16384 MB" },
        { FS_EXFAT, "UNITEST mounted, exFAT, 16384 MB" },
        { 0u,       "UNITEST mounted, format unread, 16384 MB" },
    };
    for (size_t i = 0u; i < sizeof(k) / sizeof(k[0]); ++i) {
        card_in(k[i].type, 16ull * 1024u * 1024u * 1024u);
        CHECK_STR_EQ(last_line()->text, k[i].line);
    }
    slot_empty();
    g_card = true;
    g_getfree = FR_DISK_ERR;
    CHECK_EQ(storage_init(), ESP_OK);
    CHECK_STR_EQ(last_line()->text, "UNITEST mounted, format unread, 16384 MB");
}

TEST_CASE(a_walk_hands_on_sizes_past_4_gib_whole)
{
    card_in(FS_EXFAT, 64ull * 1000u * 1000u * 1000u);
    seen_t v = { 0 };
    CHECK_EQ(storage_walk("BIG", NULL, see, &v, NULL), (int)VOL_N);
    CHECK_EQ(v.n, (unsigned)VOL_N);
    CHECK_STR_EQ(v.got[0].name, "EDGE1.CSV");
    CHECK(v.got[0].size == 0xFFFFFFFFull);          /* 2^32 - 1 */
    CHECK(v.got[1].size == 0x100000000ull);         /* 2^32 */
    CHECK(v.got[2].size == 0x200000000ull);         /* 2^33 */
    CHECK(!v.got[2].is_dir);
    CHECK(v.got[3].is_dir);
    CHECK(v.got[3].size == 0u);
    /* The suffix filter passes the large files as any other. */
    memset(&v, 0, sizeof(v));
    CHECK_EQ(storage_walk("BIG", ".csv", see, &v, NULL), (int)VOL_N);
    CHECK(v.got[2].size == 0x200000000ull);
    /* A folder the volume does not have. */
    CHECK_EQ(storage_walk("NONE", NULL, see, &v, NULL), STORAGE_WALK_NO_DIR);
}

TEST_CASE(a_stat_reads_sizes_past_4_gib_whole)
{
    card_in(FS_EXFAT, 64ull * 1000u * 1000u * 1000u);
    static const struct {
        const char *path;
        uint64_t size;
    } k[] = {
        { STORAGE_MOUNT_POINT "/BIG/EDGE1.CSV", 0xFFFFFFFFull },
        { STORAGE_MOUNT_POINT "/BIG/EDGE2.CSV", 0x100000000ull },
        { STORAGE_MOUNT_POINT "/BIG/HUGE.CSV",  0x200000000ull },
    };
    for (size_t i = 0u; i < sizeof(k) / sizeof(k[0]); ++i) {
        storage_stat_t st;
        memset(&st, 0xA5, sizeof(st));
        CHECK_EQ(storage_stat(k[i].path, &st), ESP_OK);
        CHECK(st.size == k[i].size);
        CHECK(!st.is_dir);
        CHECK(st.mtime == vol_mtime());
    }
    storage_stat_t st;
    CHECK_EQ(storage_stat(STORAGE_MOUNT_POINT "/BIG/SUB", &st), ESP_OK);
    CHECK(st.is_dir);
    CHECK(st.size == 0u);
    CHECK_EQ(storage_stat(STORAGE_MOUNT_POINT "/BIG", &st), ESP_OK);
    CHECK(st.is_dir);
}

TEST_CASE(a_stat_of_what_it_cannot_reach_says_which)
{
    storage_stat_t st;
    slot_empty();
    /* Nothing mounted. */
    CHECK_EQ(storage_stat(STORAGE_MOUNT_POINT "/BIG/HUGE.CSV", &st),
             ESP_ERR_INVALID_STATE);
    card_in(FS_EXFAT, 64ull * 1000u * 1000u * 1000u);
    /* Not under the mount point, or another name that starts like it. */
    CHECK_EQ(storage_stat("/spiffs/HUGE.CSV", &st), ESP_ERR_INVALID_STATE);
    CHECK_EQ(storage_stat(STORAGE_MOUNT_POINT "X/BIG/HUGE.CSV", &st),
             ESP_ERR_INVALID_STATE);
    CHECK_EQ(storage_stat(NULL, &st), ESP_ERR_INVALID_STATE);
    CHECK_EQ(storage_stat(STORAGE_MOUNT_POINT "/BIG/HUGE.CSV", NULL),
             ESP_ERR_INVALID_STATE);
    /* The root, which f_stat() does not take, is a folder. */
    CHECK_EQ(storage_stat(STORAGE_MOUNT_POINT, &st), ESP_OK);
    CHECK(st.is_dir);
    CHECK_EQ(storage_stat(STORAGE_MOUNT_POINT "/", &st), ESP_OK);
    CHECK(st.is_dir);
    /* No such file, no such folder. */
    CHECK_EQ(storage_stat(STORAGE_MOUNT_POINT "/BIG/NONE.CSV", &st),
             ESP_ERR_NOT_FOUND);
    CHECK_EQ(storage_stat(STORAGE_MOUNT_POINT "/NONE/HUGE.CSV", &st),
             ESP_ERR_NOT_FOUND);
    /* A card that does not read: said at W level, and no size. */
    const unsigned w = lines_at(ESP_LOG_WARN);
    g_stat_fail = FR_DISK_ERR;
    CHECK_EQ(storage_stat(STORAGE_MOUNT_POINT "/BIG/HUGE.CSV", &st), ESP_FAIL);
    CHECK_EQ(lines_at(ESP_LOG_WARN), w + 1u);
    CHECK(strstr(last_line()->text, "0:/BIG/HUGE.CSV") != NULL);
}

int main(void)
{
    RUN(a_mount_tried_again_with_no_card_sets_the_bus_up_once);
    RUN(a_card_put_in_later_mounts_on_the_bus_already_up);
    RUN(a_bus_freed_or_failed_is_set_up_again);
    RUN(a_bus_another_driver_set_up_is_taken_as_up);
    RUN(a_card_that_will_not_mount_is_said_at_w_level);
    RUN(an_exfat_card_mounts_and_its_line_names_the_format);
    RUN(every_fat_format_is_named_and_an_unread_one_says_so);
    RUN(a_walk_hands_on_sizes_past_4_gib_whole);
    RUN(a_stat_reads_sizes_past_4_gib_whole);
    RUN(a_stat_of_what_it_cannot_reach_says_which);
    return test_summary("storage");
}
