/*
 * The ESP-IDF calls firmware/panel/components/storage/storage.c makes,
 * for test_storage: an SPI bus that remembers it is up, a mount that finds
 * a card or none, and a log that records each line it would print with its
 * level and tag.  Each stands in as ESP-IDF v5.4 behaves where the suite
 * looks: spi_bus_initialize() on a bus already up logs an E line and
 * returns ESP_ERR_INVALID_STATE (spi_common.c:802), and
 * esp_vfs_fat_sdspi_mount() with no card logs "sdmmc_card_init failed
 * (0x107)" at E level under the tag "vfs_fat_sdmmc" and returns
 * ESP_ERR_TIMEOUT (vfs_fat_sdmmc.c:378), and with a card FatFs does not
 * mount logs "failed to mount card (<FRESULT>)" at W level and answers
 * ESP_FAIL (vfs_fat_sdmmc.c:126).  The second look storage.c takes at such
 * a card -- a device on the bus, the card started, FatFs's f_mount() on
 * a drive of diskio_impl.h -- answers as the card in the slot would.
 * Every header of this directory includes this one.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <stdio.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef int esp_err_t;
#define ESP_OK                    0
#define ESP_FAIL                 (-1)
#define ESP_ERR_NO_MEM           0x101
#define ESP_ERR_INVALID_STATE    0x103
#define ESP_ERR_INVALID_RESPONSE 0x108
#define ESP_ERR_TIMEOUT          0x107
#define ESP_ERR_NOT_FOUND        0x105
#define ESP_ERR_INVALID_CRC      0x109
const char *esp_err_to_name(esp_err_t err);

typedef enum {
    ESP_LOG_NONE = 0,
    ESP_LOG_ERROR,
    ESP_LOG_WARN,
    ESP_LOG_INFO,
    ESP_LOG_DEBUG,
    ESP_LOG_VERBOSE,
} esp_log_level_t;

void esp_log_level_set(const char *tag, esp_log_level_t level);
esp_log_level_t esp_log_level_get(const char *tag);
/* The line is formatted where it is logged: a variadic fake trips the
 * clang analyzer's va_list check (valist.Uninitialized) in clang-tidy 18. */
void fake_log(esp_log_level_t level, const char *tag, const char *text);
#define FAKE_LOG(level, tag, ...)                                     \
    do {                                                              \
        char fake_text_[256];                                         \
        (void)snprintf(fake_text_, sizeof(fake_text_), __VA_ARGS__);  \
        fake_log((level), (tag), fake_text_);                         \
    } while (0)
#define ESP_LOGE(tag, ...) FAKE_LOG(ESP_LOG_ERROR, tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...) FAKE_LOG(ESP_LOG_WARN, tag, __VA_ARGS__)
#define ESP_LOGI(tag, ...) FAKE_LOG(ESP_LOG_INFO, tag, __VA_ARGS__)
#define ESP_LOGD(tag, ...) FAKE_LOG(ESP_LOG_DEBUG, tag, __VA_ARGS__)

/* board.h */
#define BOARD_SD_PIN_MOSI  11
#define BOARD_SD_PIN_MISO  13
#define BOARD_SD_PIN_CLK   12
#define BOARD_SD_SPI_HOST  1
#define BOARD_SD_FREQ_KHZ  20000
esp_err_t board_sd_cs(bool on);

/* driver/spi_common.h */
typedef struct {
    int mosi_io_num, miso_io_num, sclk_io_num, quadwp_io_num, quadhd_io_num;
    int max_transfer_sz;
} spi_bus_config_t;
#define SPI_DMA_CH_AUTO 3
esp_err_t spi_bus_initialize(int host, const spi_bus_config_t *cfg, int dma);
esp_err_t spi_bus_free(int host);

/* sdmmc_cmd.h, driver/sdspi_host.h */
typedef struct { char name[8]; } sdmmc_cid_t;
typedef struct { sdmmc_cid_t cid; } sdmmc_card_t;
typedef struct { int slot; int max_freq_khz; } sdmmc_host_t;
#define SDSPI_HOST_DEFAULT() ((sdmmc_host_t){ .slot = 1, .max_freq_khz = 20000 })
#define GPIO_NUM_NC (-1)
typedef struct { int host_id; int gpio_cs; } sdspi_device_config_t;
typedef int sdspi_dev_handle_t;
esp_err_t sdspi_host_init_device(const sdspi_device_config_t *cfg,
                                 sdspi_dev_handle_t *out);
esp_err_t sdspi_host_remove_device(sdspi_dev_handle_t handle);
esp_err_t sdmmc_card_init(const sdmmc_host_t *host, sdmmc_card_t *card);
#define SDSPI_DEVICE_CONFIG_DEFAULT() \
    ((sdspi_device_config_t){ .host_id = 1, .gpio_cs = 34 })

/* ff.h, diskio_sdmmc.h, as the panel builds FatFs: exFAT on
 * (firmware/panel/fatfs_exfat.cmake), so FSIZE_t is 64-bit. */
#define FF_FS_EXFAT 1
typedef unsigned char BYTE;
typedef uint16_t WORD;
typedef uint32_t DWORD;
typedef uint64_t FSIZE_t;
typedef enum {
    FR_OK = 0, FR_DISK_ERR, FR_INT_ERR, FR_NOT_READY, FR_NO_FILE, FR_NO_PATH,
    FR_INVALID_NAME, FR_NO_FILESYSTEM = 13,
} FRESULT;
typedef struct { int pos; } FF_DIR;
typedef struct {
    FSIZE_t fsize;
    WORD fdate;
    WORD ftime;
    BYTE fattrib;
    char fname[256];
} FILINFO;
typedef struct { BYTE fs_type; BYTE win[4096]; } FATFS;
#define FS_FAT12 1
#define FS_FAT16 2
#define FS_FAT32 3
#define FS_EXFAT 4
FRESULT f_mount(FATFS *fs, const char *path, BYTE opt);
#define AM_DIR 0x10
FRESULT f_opendir(FF_DIR *dp, const char *path);
FRESULT f_readdir(FF_DIR *dp, FILINFO *fno);
FRESULT f_closedir(FF_DIR *dp);
FRESULT f_stat(const char *path, FILINFO *fno);
FRESULT f_getfree(const char *path, DWORD *nclst, FATFS **fatfs);
FRESULT f_getlabel(const char *path, char *label, uint32_t *vsn);
BYTE ff_diskio_get_pdrv_card(const sdmmc_card_t *card);
void ff_diskio_register_sdmmc(unsigned char pdrv, sdmmc_card_t *card);
/* diskio_impl.h */
esp_err_t ff_diskio_get_drive(BYTE *out_pdrv);
void fake_diskio_unregister(BYTE pdrv);
#define ff_diskio_unregister(pdrv_) fake_diskio_unregister(pdrv_)

/* esp_vfs_fat.h */
typedef struct {
    bool format_if_mount_failed;
    int max_files;
    size_t allocation_unit_size;
} esp_vfs_fat_sdmmc_mount_config_t;
esp_err_t esp_vfs_fat_sdspi_mount(const char *base, const sdmmc_host_t *host,
                                  const sdspi_device_config_t *slot,
                                  const esp_vfs_fat_sdmmc_mount_config_t *cfg,
                                  sdmmc_card_t **out);
esp_err_t esp_vfs_fat_sdcard_unmount(const char *base, sdmmc_card_t *card);
esp_err_t esp_vfs_fat_info(const char *base, uint64_t *total, uint64_t *free);
