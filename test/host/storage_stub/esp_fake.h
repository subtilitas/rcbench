/*
 * The ESP-IDF calls firmware/panel/components/storage/storage.c makes,
 * for test_storage: a card supply on an on-chip LDO (low-dropout
 * regulator) that remembers it is on, a mount that finds a card or none,
 * and a log that records each line it would print with its level and tag.
 * Each stands in as ESP-IDF v5.5 behaves where the suite looks:
 * esp_vfs_fat_sdmmc_mount() with no card logs "sdmmc_card_init failed
 * (0x107)" at E level under the tag "vfs_fat_sdmmc" and returns
 * ESP_ERR_TIMEOUT, and with a card FatFs does not mount logs "failed to
 * mount card (<FRESULT>)" at W level and answers ESP_FAIL.  A card whose
 * host carries no supply handle does not start.  The second look
 * storage.c takes at such a card -- the SDMMC host and slot set up, the
 * card started, FatFs's f_mount() on a drive of diskio_impl.h, the slot
 * let go -- answers as the card in the slot would.  A
 * mounted card taken out or swapped fails every read with FR_DISK_ERR and
 * an E line under "diskio_sdmmc" (diskio_sdmmc.c:58), and f_getfree() with
 * the disk status check on answers FR_NOT_READY after an E line "Check
 * status failed (0x107)" under the same tag (diskio_sdmmc.c:26).  The
 * FreeRTOS mutex of freertos/semphr.h is a flag the suite reads.
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

/* board_pins.h */
#define BOARD_SD_SLOT      0
#define BOARD_SD_WIDTH     4
#define BOARD_SD_PIN_CLK   43
#define BOARD_SD_PIN_CMD   44
#define BOARD_SD_PIN_D0    39
#define BOARD_SD_PIN_D1    40
#define BOARD_SD_PIN_D2    41
#define BOARD_SD_PIN_D3    42
#define BOARD_SD_LDO_CHAN  4
#define BOARD_SD_FREQ_KHZ  20000

/* sd_pwr_ctrl_by_on_chip_ldo.h */
typedef struct fake_pwr *sd_pwr_ctrl_handle_t;
typedef struct { int ldo_chan_id; } sd_pwr_ctrl_ldo_config_t;
esp_err_t sd_pwr_ctrl_new_on_chip_ldo(const sd_pwr_ctrl_ldo_config_t *cfg,
                                      sd_pwr_ctrl_handle_t *out);
esp_err_t sd_pwr_ctrl_del_on_chip_ldo(sd_pwr_ctrl_handle_t handle);

/* sdmmc_cmd.h, driver/sdmmc_host.h */
typedef struct { char name[8]; } sdmmc_cid_t;
typedef struct { sdmmc_cid_t cid; } sdmmc_card_t;
typedef struct {
    int slot;
    int max_freq_khz;
    sd_pwr_ctrl_handle_t pwr_ctrl_handle;
    esp_err_t (*init)(void);
    esp_err_t (*deinit_p)(int slot);
} sdmmc_host_t;
typedef struct {
    int clk, cmd, d0, d1, d2, d3;
    int width;
    unsigned flags;
} sdmmc_slot_config_t;
#define SDMMC_SLOT_FLAG_INTERNAL_PULLUP 1u
esp_err_t sdmmc_host_init(void);
esp_err_t sdmmc_host_init_slot(int slot, const sdmmc_slot_config_t *cfg);
esp_err_t sdmmc_host_deinit_slot(int slot);
#define SDMMC_HOST_DEFAULT() { .slot = 1, .max_freq_khz = 20000, \
    .pwr_ctrl_handle = NULL, .init = sdmmc_host_init,               \
    .deinit_p = sdmmc_host_deinit_slot }
#define SDMMC_SLOT_CONFIG_DEFAULT() { .clk = 43, .cmd = 44, .d0 = 39, \
    .d1 = 40, .d2 = 41, .d3 = 42, .width = 0, .flags = 0u }
esp_err_t sdmmc_card_init(const sdmmc_host_t *host, sdmmc_card_t *card);

/* ff.h, diskio_sdmmc.h, as the panel builds FatFs: exFAT on
 * (firmware/panel/fatfs_exfat.cmake), so FSIZE_t is 64-bit. */
#define FF_FS_EXFAT 1
typedef unsigned char BYTE;
typedef uint16_t WORD;
typedef uint32_t DWORD;
typedef uint64_t FSIZE_t;
typedef enum {
    FR_OK = 0, FR_DISK_ERR, FR_INT_ERR, FR_NOT_READY, FR_NO_FILE, FR_NO_PATH,
    FR_INVALID_NAME, FR_DENIED, FR_EXIST, FR_INVALID_OBJECT,
    FR_WRITE_PROTECTED, FR_INVALID_DRIVE, FR_NOT_ENABLED, FR_NO_FILESYSTEM,
    FR_MKFS_ABORTED, FR_TIMEOUT,
} FRESULT;
typedef struct { int pos; int folder; } FF_DIR;
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
void ff_sdmmc_set_disk_status_check(BYTE pdrv, bool enable);
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
esp_err_t esp_vfs_fat_sdmmc_mount(const char *base, const sdmmc_host_t *host,
                                  const void *slot,
                                  const esp_vfs_fat_sdmmc_mount_config_t *cfg,
                                  sdmmc_card_t **out);
esp_err_t esp_vfs_fat_sdcard_unmount(const char *base, sdmmc_card_t *card);
esp_err_t esp_vfs_fat_info(const char *base, uint64_t *total, uint64_t *free);

/* freertos/FreeRTOS.h, freertos/semphr.h: a mutex that records a second
 * take by its holder (a deadlock on the panel) and a give without a take. */
typedef uint32_t TickType_t;
typedef int BaseType_t;
#define portMAX_DELAY ((TickType_t)0xffffffffu)
#define pdTRUE 1
#define pdFALSE 0
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define taskENTER_CRITICAL(mux) ((void)(mux))
#define taskEXIT_CRITICAL(mux) ((void)(mux))
typedef struct { int taken; } StaticSemaphore_t;
typedef StaticSemaphore_t *SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateMutex(void);
BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t wait);
BaseType_t xSemaphoreGive(SemaphoreHandle_t sem);
