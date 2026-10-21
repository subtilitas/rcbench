/**
 * @file storage.c
 * @brief The SD card on SDMMC slot 0: mount, unmount, holds, and the look
 *        at a card that does not mount.
 *
 * The SDMMC host is shared with the ESP32-C6's SDIO link on slot 1
 * (wifi_radio.c).  Each slot's start and card start set the host's clock
 * divider, which both slots share, and send the clock update commands
 * outside ESP-IDF's request mutex (sdmmc_host_set_card_clk(), sdmmc_host.c
 * 301-351).  So a mount here can make a C6 transfer under way fail, and
 * esp_hosted's start of slot 1 (at the first Wi-Fi start, and at each C6
 * reset it makes) can make a card read or write under way fail.  Neither
 * is measured.  test_storage holds the slot count against
 * test/host/storage_stub/storage_fake.c, which counts slots as ESP-IDF
 * does; the shared clock divider is not modelled there.
 *
 * SPDX-License-Identifier: MIT
 */

#include "storage.h"
#include "storage_priv.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board_pins.h"
/* ff.h first: diskio_sdmmc.h uses its BYTE without including it. */
#include "ff.h"
#include "diskio_impl.h"
#include "diskio_sdmmc.h"
#include "driver/sdmmc_host.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "sdmmc_cmd.h"

static const char *TAG = "storage";
/* The tag of ESP-IDF's FAT-on-SD mount (components/fatfs/vfs/
 * vfs_fat_sdmmc.c). */
#define STORAGE_IDF_MOUNT_TAG "vfs_fat_sdmmc"
/* The tags of ESP-IDF's FAT-on-SD disk I/O (components/fatfs/diskio/
 * diskio_sdmmc.c) and SD commands (components/sdmmc/sdmmc_cmd.c): a card
 * looked at and found gone logs at E level under both. */
#define STORAGE_IDF_DISKIO_TAG "diskio_sdmmc"
#define STORAGE_IDF_CMD_TAG    "sdmmc_cmd"

/*
 * Who touches the card, and when it may be unmounted.
 *
 * Three tasks reach the card: app_main, which renders (LOGS, the servo
 * programmer's record, the start-up), the runlog task (every write) and
 * the Wi-Fi access's HTTP server (downloads).  Each of them may mount or
 * unmount it; s.lock makes those one at a time and keeps them apart from
 * storage_hold().
 *
 * A volume is unmounted only while nothing holds it.  A hold is taken for
 * every open file and folder and around every path call (mkdir, stat,
 * unlink, rename, a walk): an unmount frees the FatFs work area and the
 * VFS (virtual file system) slot under a call in flight, and a file left
 * open across a remount keeps a descriptor number the next volume hands
 * out again.  A card found gone while held is marked lost: no new hold is
 * given, its holders' calls fail, and the last release unmounts it.
 *
 * mounted and lost are s.lock's; usable and state are read without it,
 * so that the control task and every frame can ask without waiting.
 */
static struct {
    sdmmc_card_t *card;
    sd_pwr_ctrl_handle_t pwr;   /**< the card's supply, on-chip LDO channel
                                     4: made once, kept until
                                     storage_deinit() */
    bool mounted;
    /* Found gone while held: unmounted at the last release. */
    bool lost;
    unsigned holds;
    SemaphoreHandle_t lock;
    atomic_bool usable;                 /* mounted && !lost */
    atomic_int state_now;               /* card_state_t while not usable */
    atomic_uint mounts;                 /* mounts that worked */
    /* The volume label, or the CID's product name without one: 11 FAT
     * label characters take up to 33 bytes in UTF-8. */
    char name[40];
    /* The FatFs drive of the mounted card, "0:", for f_readdir(). */
    char drive[8];
    char status[48];
} s;

/* The lock, made at the first storage_init() or storage_check(): the
 * splash's, on app_main, before any task that reaches the card exists. */
static bool lock_ready(void)
{
    if (s.lock == NULL) {
        s.lock = xSemaphoreCreateMutex();
    }
    return s.lock != NULL;
}

static void lock(void)
{
    (void)xSemaphoreTake(s.lock, portMAX_DELAY);
}

static void unlock(void)
{
    (void)xSemaphoreGive(s.lock);
}

static void set_state(card_state_t state)
{
    atomic_store(&s.state_now, (int)state);
}

/* What the readers without the lock see: usable last, after the name. */
static void publish(void)
{
    atomic_store(&s.usable, s.mounted && !s.lost);
}

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
/* The name's writes and copies, a few bytes each: the render loop copies
 * it every frame while another task's mount can write it. */
static portMUX_TYPE s_name_mux = portMUX_INITIALIZER_UNLOCKED;

static void name_set(const char *name)
{
    taskENTER_CRITICAL(&s_name_mux);
    snprintf(s.name, sizeof(s.name), "%s", name);
    taskEXIT_CRITICAL(&s_name_mux);
}

static void read_name(void)
{
    name_set(s.card->cid.name);
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
        name_set(label);
    }
}

/*
 * The mounted volume's format, for the mount's log line: FAT12, FAT16,
 * FAT32 or exFAT.  After the mount's fat_usage(), whose f_getfree() counts a
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

/**
 * @brief What FatFs says of the card esp_vfs_fat_sdmmc_mount() just gave
 *        up on.
 *
 * That mount answers ESP_FAIL for every FatFs refusal alike, and only after
 * the card has started (its own start-up errors come back as they are): no
 * FAT volume on the card, a read error and an internal error are one code
 * to its caller.  So the card is started again here, the way the mount
 * starts it, and handed to FatFs alone, without the VFS (virtual file
 * system) path: f_mount()'s own answer, e.g. FR_NO_FILESYSTEM (13) for a
 * card that holds NTFS, a GPT or nothing.  The volume is let go before
 * returning; a mount is still storage_init()'s alone.
 *
 * Only on that failure, so a card that mounts costs nothing more.  The
 * work area is on the heap for the call: FATFS holds a sector, 4096 bytes
 * with the panel's CONFIG_FATFS_SECTOR_4096.
 *
 * @param host The host as the mount had it, SDMMC slot 0.
 * @param slot The slot's pins and width as the mount had them.
 * @param why  Set to the error that kept FatFs from being asked, or ESP_OK.
 * @return f_mount()'s FRESULT, or -1 with @p why set when FatFs could not
 *         be asked: no device, a card that no longer starts, no memory.
 */
static int probe_fatfs(const sdmmc_host_t *host,
                       const sdmmc_slot_config_t *slot, esp_err_t *why)
{
    /* No slot deinit after a slot that did not start: the SDMMC host is
     * shared with the C6's SDIO link on slot 1, ESP-IDF counts a slot only
     * once it has started, and a deinit here would count slot 1 down and
     * stop the whole host. */
    esp_err_t err = (*host->init)();
    if (err == ESP_OK) {
        err = sdmmc_host_init_slot(host->slot, slot);
    }
    if (err != ESP_OK) {
        *why = err;
        return -1;
    }
    int fr = -1;
    BYTE pdrv = 0xffu;
    sdmmc_card_t *card = calloc(1, sizeof(*card));
    FATFS *fs = calloc(1, sizeof(*fs));
    if (card == NULL || fs == NULL) {
        err = ESP_ERR_NO_MEM;
    } else if ((err = sdmmc_card_init(host, card)) == ESP_OK
               && (err = ff_diskio_get_drive(&pdrv)) == ESP_OK) {
        ff_diskio_register_sdmmc(pdrv, card);
        const char drv[3] = { (char)('0' + pdrv), ':', '\0' };
        fr = (int)f_mount(fs, drv, 1);
        (void)f_mount(NULL, drv, 0);
        ff_diskio_unregister(pdrv);
    }
    (void)(*host->deinit_p)(host->slot);
    free(fs);
    free(card);
    *why = err;
    return fr;
}

/**
 * @brief A mount that failed after the card started: which kind of card it
 *        is, said once in the log and kept for the screens.
 * @param err The mount's error.
 * @param host The host as the mount had it.
 * @param slot The slot as the mount had it.
 */
static void mount_failed(esp_err_t err, const sdmmc_host_t *host,
                         const sdmmc_slot_config_t *slot)
{
    set_state(CARD_STATE_UNREADABLE);
    if (err != ESP_FAIL) {
        /* The card's start-up or the mount's memory, by ESP-IDF's name. */
        snprintf(s.status, sizeof(s.status), "%s", esp_err_to_name(err));
        ESP_LOGW(TAG, "mount failed: %s", esp_err_to_name(err));
        return;
    }
    esp_err_t why = ESP_OK;
    const int fr = probe_fatfs(host, slot, &why);
    if (fr == (int)FR_NO_FILESYSTEM) {
        set_state(CARD_STATE_NO_FAT);
        snprintf(s.status, sizeof(s.status), "no FAT volume");
        ESP_LOGW(TAG, "card holds no FAT volume (FatFs %d): "
                      "format it FAT32 or exFAT", fr);
    } else if (fr >= 0) {
        snprintf(s.status, sizeof(s.status), "FatFs %d", fr);
        ESP_LOGW(TAG, "mount failed: FatFs %d", fr);
    } else {
        snprintf(s.status, sizeof(s.status), "%s", esp_err_to_name(err));
        ESP_LOGW(TAG, "mount failed: %s, card check: %s",
                 esp_err_to_name(err), esp_err_to_name(why));
    }
}

static bool fat_usage(uint64_t *total, uint64_t *freebytes)
{
    uint64_t t = 0;
    uint64_t f = 0;
    if (esp_vfs_fat_info(STORAGE_MOUNT_POINT, &t, &f) != ESP_OK) {
        return false;
    }
    *total = t;
    *freebytes = f;
    return true;
}

/**
 * @brief Mount whatever the slot holds, under s.lock, with nothing mounted.
 * @return As storage_init().
 */
static esp_err_t mount_locked(void)
{
    s.status[0] = '\0';

    /* The card's supply once: a retry after "no card" finds it on. */
    esp_err_t err = ESP_OK;
    if (s.pwr == NULL) {
        const sd_pwr_ctrl_ldo_config_t ldo = {
            .ldo_chan_id = BOARD_SD_LDO_CHAN,
        };
        err = sd_pwr_ctrl_new_on_chip_ldo(&ldo, &s.pwr);
        if (err != ESP_OK) {
            s.pwr = NULL;
            set_state(CARD_STATE_UNREADABLE);
            snprintf(s.status, sizeof(s.status), "LDO %s",
                     esp_err_to_name(err));
            ESP_LOGW(TAG, "card supply failed: %s", s.status);
            return err;
        }
    }

    /* SDMMC slot 0, 4-bit, on its IO MUX pins (board_pins.h). */
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = BOARD_SD_SLOT;
    host.max_freq_khz = BOARD_SD_FREQ_KHZ;
    host.pwr_ctrl_handle = s.pwr;

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = BOARD_SD_WIDTH;
    slot.clk = BOARD_SD_PIN_CLK;
    slot.cmd = BOARD_SD_PIN_CMD;
    slot.d0 = BOARD_SD_PIN_D0;
    slot.d1 = BOARD_SD_PIN_D1;
    slot.d2 = BOARD_SD_PIN_D2;
    slot.d3 = BOARD_SD_PIN_D3;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_vfs_fat_sdmmc_mount_config_t mount = {
        /* Never format on the bench: a card that will not mount is a card to
         * look at on a computer, not one to wipe. */
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 16 * 1024,
    };

    /*
     * The SDMMC host is shared with the C6's SDIO link on slot 1, which
     * esp_hosted starts before app_main().  ESP-IDF v5.5.5 counts a slot
     * only once it has started (sdmmc_host_init_slot(), sdmmc_host.c
     * 837-840), and the mount deinits slot 0 on every failure after the
     * host's init (vfs_fat_sdmmc.c 310-312): after a slot 0 that did not
     * start, that deinit would count slot 1 down to 0 and stop the host
     * under esp_hosted.  So slot 0 is started here first, and a slot that
     * does not start ends the attempt with no deinit, as in probe_fatfs().
     * With both slots started the count is at its ceiling of 2, so the
     * mount's own start of slot 0 does not count again, and its deinit
     * counts back to 1.
     */
    err = (*host.init)();
    if (err == ESP_OK) {
        err = sdmmc_host_init_slot(host.slot, &slot);
    }
    if (err != ESP_OK) {
        set_state(CARD_STATE_UNREADABLE);
        snprintf(s.status, sizeof(s.status), "%s", esp_err_to_name(err));
        ESP_LOGW(TAG, "SD slot did not start: %s", esp_err_to_name(err));
        return err;
    }

    /*
     * ESP-IDF's mount logs every failure of its own at E level, "no card"
     * among them ("sdmmc_card_init failed (0x107)").  An empty slot is not
     * a fault: its tag is quiet for the call, and the outcome is logged
     * here, once per attempt.
     */
    const esp_log_level_t was = esp_log_level_get(STORAGE_IDF_MOUNT_TAG);
    esp_log_level_set(STORAGE_IDF_MOUNT_TAG, ESP_LOG_NONE);
    err = esp_vfs_fat_sdmmc_mount(STORAGE_MOUNT_POINT, &host, &slot, &mount,
                                  &s.card);
    esp_log_level_set(STORAGE_IDF_MOUNT_TAG, was);
    if (err != ESP_OK) {
        if (err == ESP_ERR_TIMEOUT || err == ESP_ERR_NOT_FOUND ||
            err == ESP_ERR_INVALID_RESPONSE) {
            set_state(CARD_STATE_NONE);
            snprintf(s.status, sizeof(s.status), "no card");
            ESP_LOGI(TAG, "no card (%s)", esp_err_to_name(err));
            return ESP_ERR_NOT_FOUND;
        }
        /* The check starts the card again on its own. */
        mount_failed(err, &host, &slot);
        return err;
    }

    s.mounted = true;
    s.lost = false;
    set_state(CARD_STATE_MOUNTED);
    s.drive[0] = '\0';
    const BYTE pdrv = ff_diskio_get_pdrv_card(s.card);
    if (pdrv != 0xffu) {
        snprintf(s.drive, sizeof(s.drive), "%u:", (unsigned)pdrv);
    }
    read_name();
    uint64_t total = 0;
    uint64_t freebytes = 0;
    (void)fat_usage(&total, &freebytes);
    const unsigned long long mb = total / (1024ULL * 1024ULL);
    ESP_LOGI(TAG, "%s mounted, %s, %llu MB", s.name, volume_format(), mb);
    /* The name is written: now the readers without the lock may see it. */
    atomic_fetch_add(&s.mounts, 1u);
    publish();
    return ESP_OK;
}

/**
 * @brief Let the volume go, under s.lock, with nothing holding it.
 *
 * No card traffic: f_mount(NULL) forgets the volume without writing, and
 * the slot is let go.  The SDMMC host stays up: the radio's slot 1 shares
 * it.
 */
static void unmount_locked(void)
{
    if (!s.mounted) {
        return;
    }
    /* The state first, then the flag that lets the readers without the
     * lock read it: never "not usable" beside "mounted". */
    if (atomic_load(&s.state_now) == (int)CARD_STATE_MOUNTED) {
        set_state(CARD_STATE_NONE);
    }
    atomic_store(&s.usable, false);
    (void)esp_vfs_fat_sdcard_unmount(STORAGE_MOUNT_POINT, s.card);
    s.card = NULL;
    s.mounted = false;
    s.lost = false;
    s.drive[0] = '\0';
}

/*
 * The mounted volume is not to be used again: its card is gone
 * (CARD_STATE_NONE) or fails its reads (CARD_STATE_UNREADABLE).  Under
 * s.lock.  The state is stored before the flag the readers without the
 * lock go by.  Unmounted at once when nothing holds it, at the last
 * release otherwise.
 */
static void drop_locked(card_state_t state)
{
    set_state(state);
    s.lost = true;
    publish();
    if (s.holds == 0u) {
        unmount_locked();
    }
}

/*
 * The card the volume was mounted from does not answer: taken out or
 * swapped.  @p fr is what FatFs said; @p how the call that found it.
 */
static void lose_locked(int fr, const char *how)
{
    if (!s.mounted) {
        return;
    }
    if (!s.lost) {
        ESP_LOGI(TAG, "card gone (%s: FatFs %d)", how, fr);
    }
    snprintf(s.status, sizeof(s.status), "no card");
    drop_locked(CARD_STATE_NONE);
}

/*
 * Whether the mounted card still answers: one SEND_STATUS (CMD13) through
 * FatFs, under its volume lock, so it does not cut into a read or a write
 * another task has under way.
 *
 * FatFs asks the disk's status at the start of every call, and ESP-IDF's
 * SD status only sends CMD13 while its check is on; it is on for this call
 * alone.  f_getfree() answers from the count FatFs keeps once the mount
 * counted it, so a card that answers costs that one command.  A card that
 * does not answer makes FatFs drop the volume (FR_NOT_READY), and every
 * file and folder still open on it fails from then on without card
 * traffic.  ESP-IDF says that at E level, "Check status failed (0x107)";
 * a card looked for and gone is not a fault, so those tags are quiet for
 * the call.
 */
static int probe_locked(void)
{
    if (s.drive[0] < '0' || s.drive[0] > '9') {
        return (int)FR_OK;          /* no drive number: nothing to ask */
    }
    const BYTE pdrv = (BYTE)(s.drive[0] - '0');
    const esp_log_level_t was_disk = esp_log_level_get(STORAGE_IDF_DISKIO_TAG);
    const esp_log_level_t was_cmd = esp_log_level_get(STORAGE_IDF_CMD_TAG);
    esp_log_level_set(STORAGE_IDF_DISKIO_TAG, ESP_LOG_NONE);
    esp_log_level_set(STORAGE_IDF_CMD_TAG, ESP_LOG_NONE);
    ff_sdmmc_set_disk_status_check(pdrv, true);
    DWORD free_clusters = 0u;
    FATFS *fs = NULL;
    const FRESULT fr = f_getfree(s.drive, &free_clusters, &fs);
    ff_sdmmc_set_disk_status_check(pdrv, false);
    esp_log_level_set(STORAGE_IDF_CMD_TAG, was_cmd);
    esp_log_level_set(STORAGE_IDF_DISKIO_TAG, was_disk);
    return (int)fr;
}

/* FatFs's answers that say the card stopped answering rather than that a
 * name or the volume's contents are wrong. */
static bool removal_shaped(int fr)
{
    return fr == (int)FR_DISK_ERR || fr == (int)FR_NOT_READY
           || fr == (int)FR_INVALID_OBJECT;
}

esp_err_t storage_init(void)
{
    if (!lock_ready()) {
        return ESP_ERR_NO_MEM;
    }
    lock();
    if (s.mounted && s.lost && s.holds == 0u) {
        unmount_locked();
    }
    esp_err_t err = ESP_OK;
    if (s.mounted) {
        err = s.lost ? ESP_ERR_INVALID_STATE : ESP_OK;
    } else {
        err = mount_locked();
    }
    unlock();
    return err;
}

card_state_t storage_check(void)
{
    if (!lock_ready()) {
        return CARD_STATE_NONE;
    }
    lock();
    if (s.mounted && !s.lost) {
        /* Only an answer that says the card stopped answering: a volume
         * lock another task holds past FatFs's wait (FR_TIMEOUT) is not a
         * card taken out. */
        const int fr = probe_locked();
        if (removal_shaped(fr)) {
            lose_locked(fr, "check");
        }
    }
    if (s.mounted && s.lost && s.holds == 0u) {
        unmount_locked();
    }
    if (!s.mounted) {
        (void)mount_locked();
    }
    unlock();
    return storage_card_state();
}

bool storage_hold(void)
{
    if (s.lock == NULL) {
        return false;
    }
    lock();
    const bool ok = s.mounted && !s.lost;
    if (ok) {
        ++s.holds;
    }
    unlock();
    return ok;
}

bool storage_hold_mount(unsigned mount)
{
    if (s.lock == NULL) {
        return false;
    }
    lock();
    const bool ok = s.mounted && !s.lost
                    && atomic_load(&s.mounts) == mount;
    if (ok) {
        ++s.holds;
    }
    unlock();
    return ok;
}

void storage_release(void)
{
    if (s.lock == NULL) {
        return;
    }
    lock();
    if (s.holds > 0u) {
        --s.holds;
    }
    if (s.holds == 0u && s.mounted && s.lost) {
        unmount_locked();
    }
    unlock();
}

FILE *storage_fopen(const char *path, const char *mode)
{
    if (path == NULL || mode == NULL || !storage_hold()) {
        return NULL;
    }
    FILE *f = fopen(path, mode);
    if (f == NULL) {
        storage_release();
    }
    return f;
}

int storage_fclose(FILE *f)
{
    if (f == NULL) {
        return EOF;
    }
    const int r = fclose(f);
    storage_release();
    return r;
}

void storage_deinit(void)
{
    if (s.lock != NULL) {
        lock();
    }
    /* The end: nothing may still be open, so no hold is waited for. */
    s.holds = 0u;
    unmount_locked();
    if (s.pwr != NULL) {
        (void)sd_pwr_ctrl_del_on_chip_ldo(s.pwr);
        s.pwr = NULL;
    }
    /* Nothing is known of the slot until the next storage_init(). */
    set_state(CARD_STATE_NONE);
    s.status[0] = '\0';
    if (s.lock != NULL) {
        unlock();
    }
}

unsigned storage_mount_count(void)
{
    return atomic_load(&s.mounts);
}

bool storage_mounted(void)
{
    return atomic_load(&s.usable);
}

void storage_card_name(char *out, size_t n)
{
    if (out == NULL || n == 0u) {
        return;
    }
    taskENTER_CRITICAL(&s_name_mux);
    snprintf(out, n, "%s", atomic_load(&s.usable) ? s.name : "");
    taskEXIT_CRITICAL(&s_name_mux);
}

const char *storage_status(void)
{
    return s.status;
}

card_state_t storage_card_state(void)
{
    return atomic_load(&s.usable) ? CARD_STATE_MOUNTED
                                  : (card_state_t)atomic_load(&s.state_now);
}

uint64_t storage_total_bytes(void)
{
    uint64_t t = 0;
    uint64_t f = 0;
    if (!storage_hold()) {
        return 0;
    }
    const bool ok = fat_usage(&t, &f);
    storage_release();
    return ok ? t : 0;
}

uint64_t storage_free_bytes(void)
{
    uint64_t t = 0;
    uint64_t f = 0;
    if (!storage_hold()) {
        return 0;
    }
    const bool ok = fat_usage(&t, &f);
    storage_release();
    return ok ? f : 0;
}

/*
 * A read of the mounted card failed with @p fr.  Under s.lock, with the
 * caller's hold given back.  A failure that does not say the card stopped
 * answering leaves the volume as it is.  One that does is asked about
 * (probe_locked()): a card that does not answer is taken out or swapped
 * (lose_locked()).  One that answers fails its reads on its own: it is
 * unreadable and let go, and not mounted again by this call, so a card
 * that fails every read costs one mount per look and not a loop.  An
 * answer that says neither, such as FatFs's volume lock not taken in
 * time, leaves the volume as it is.
 *
 * @return whether the volume was let go for a card gone and may be
 *         mounted again now.
 */
static bool read_failed_locked(int fr, const char *how)
{
    if (!s.mounted || s.lost || !removal_shaped(fr)) {
        return false;
    }
    const int asked = probe_locked();
    if (removal_shaped(asked)) {
        lose_locked(asked, how);
        return !s.mounted;
    }
    if (asked != (int)FR_OK) {
        return false;
    }
    ESP_LOGW(TAG, "card answers and does not read (%s: FatFs %d)", how, fr);
    snprintf(s.status, sizeof(s.status), "FatFs %d", fr);
    drop_locked(CARD_STATE_UNREADABLE);
    return false;
}

const char *storage_drive(void)
{
    return s.drive;
}

bool storage_read_end(int fr, const char *how, bool may_mount)
{
    lock();
    if (s.holds > 0u) {
        --s.holds;
    }
    bool mounted_again = false;
    if (read_failed_locked(fr, how) && may_mount) {
        mounted_again = (mount_locked() == ESP_OK);
    }
    /* The last hold of a volume another task found gone, as
     * storage_release() lets it go. */
    if (s.holds == 0u && s.mounted && s.lost) {
        unmount_locked();
    }
    unlock();
    return mounted_again;
}
