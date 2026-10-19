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
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

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

void fake_log(esp_log_level_t level, const char *tag, const char *fmt, ...)
{
    if (level > *level_of(tag) || g_log_n >= LOG_MAX) {
        return;                         /* not printed */
    }
    log_line_t *l = &g_log[g_log_n++];
    l->level = level;
    snprintf(l->tag, sizeof(l->tag), "%s", tag);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(l->text, sizeof(l->text), fmt, ap);
    va_end(ap);
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
    *total = 16ull * 1024u * 1024u * 1024u;
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

FRESULT f_opendir(FF_DIR *dp, const char *path)
{
    (void)dp;
    (void)path;
    return FR_NO_PATH;
}

FRESULT f_readdir(FF_DIR *dp, FILINFO *fno)
{
    (void)dp;
    fno->fname[0] = '\0';
    return FR_OK;
}

FRESULT f_closedir(FF_DIR *dp)
{
    (void)dp;
    return FR_OK;
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

int main(void)
{
    RUN(a_mount_tried_again_with_no_card_sets_the_bus_up_once);
    RUN(a_card_put_in_later_mounts_on_the_bus_already_up);
    RUN(a_bus_freed_or_failed_is_set_up_again);
    RUN(a_bus_another_driver_set_up_is_taken_as_up);
    RUN(a_card_that_will_not_mount_is_said_at_w_level);
    return test_summary("storage");
}
