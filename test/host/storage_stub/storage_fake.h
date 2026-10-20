/*
 * The card slot the storage suites run firmware/panel/components/storage
 * against (storage_fake.c): the ESP-IDF calls of esp_fake.h answered as a
 * card in the slot would, the lines each call would log, and the state a
 * case sets up or reads back.  test_storage and test_storage_swap share it.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_fake.h"
#include "storage.h"

#define LOG_MAX 32
typedef struct {
    esp_log_level_t level;
    char tag[24];
    char text[96];
} log_line_t;

extern log_line_t g_log[LOG_MAX];
extern unsigned   g_log_n;
extern esp_log_level_t g_mount_tag_level;   /* "vfs_fat_sdmmc" */
extern esp_log_level_t g_disk_tag_level;    /* "diskio_sdmmc" */
extern esp_log_level_t g_cmd_tag_level;     /* "sdmmc_cmd" */
extern esp_log_level_t g_default_level;
extern unsigned   g_bus_inits;              /* spi_bus_initialize() calls */
extern bool       g_bus_up;
extern esp_err_t  g_bus_fail;               /* what the next init returns */
extern esp_err_t  g_cs_fail;                /* what the next CS on returns */
extern bool       g_cs;                     /* chip select held */
extern bool       g_card;                   /* a card in the slot */
extern esp_err_t  g_card_start;             /* how the card's start-up ends */
extern FRESULT    g_card_fs;                /* what f_mount() finds on it */
extern int        g_probe_fs;               /* the second look, -1: as above */
extern bool       g_probe_card_gone;        /* pulled before the second look */
extern esp_err_t  g_dev_fail;               /* the next device add */
extern bool       g_drives_full;            /* ff_diskio_get_drive() */
extern unsigned   g_dev_adds;               /* sdspi_host_init_device() */
extern int        g_dev_live;               /* devices on the bus */
extern int        g_drive_live;             /* drives registered */
extern int        g_fs_live;                /* volumes f_mount()ed */
extern bool       g_cs_in_probe;            /* CS held at the second look */
extern uint64_t   g_total;                  /* the volume's bytes */
extern BYTE       g_fs_type;                /* FS_FAT12 .. FS_EXFAT */
extern FRESULT    g_getfree;                /* what f_getfree() answers */
extern FRESULT    g_stat_fail;              /* f_stat() on a listed name */
/* The card the volume was mounted from still answers: false once it is
 * taken out, or swapped for another that nothing has started. */
extern bool       g_live;
extern FRESULT    g_read_fail;              /* every read of a live card */
extern const char *g_label;                 /* the volume label */
extern unsigned   g_mounts;                 /* mounts that worked */
extern unsigned   g_unmounts;               /* esp_vfs_fat_sdcard_unmount() */
extern unsigned   g_status_asks;            /* CMD13s FatFs sent */
extern bool       g_status_check;           /* the disk status check on */
extern unsigned   g_lock_misuse;            /* a second take, a lone give */
extern bool       g_lock_taken;
extern FRESULT    g_probe_answer;           /* a live card's CMD13 look */
/* Entries a walk reads before the mounted card stops answering, -1:
 * none.  What the slot then holds is g_card's. */
extern int        g_pull_after;

/* One folder of the volume, "BIG", as FatFs reads it. */
#define VOL_DIR "0:/BIG"
typedef struct {
    const char *name;
    uint64_t size;
    bool dir;
} vol_entry_t;
extern const vol_entry_t k_vol[4];
#define VOL_N (sizeof(k_vol) / sizeof(k_vol[0]))
/* 2024-05-17 12:34:56 as FAT stamps it: years from 1980, seconds / 2. */
#define VOL_FDATE ((WORD)(((2024 - 1980) << 9) | (5 << 5) | 17))
#define VOL_FTIME ((WORD)((12 << 11) | (34 << 5) | (56 / 2)))

/* A name longer than storage.c's status field, as no ESP-IDF code has
 * today: the bound is the field's, not the names'. */
#define ESP_ERR_LONG_NAME 0x7777

/* Nothing mounted, nothing in the slot, every fake as at power-up. */
void slot_empty(void);
/* The second look let go of all it took, and chip select with it. */
bool probe_left_nothing(void);
/* An empty slot, then a card of @p fs_type and @p total bytes mounted. */
void card_in(BYTE fs_type, uint64_t total);
/* The last line logged, or an empty one with none. */
const log_line_t *last_line(void);
/* What mktime() makes of VOL_FDATE and VOL_FTIME, in the same zone. */
int64_t vol_mtime(void);
unsigned lines_at(esp_log_level_t level);
/* storage_card_name() as the panel copies it. */
const char *name_now(void);

typedef struct {
    storage_entry_t got[8];
    unsigned n;
} seen_t;
void see(const storage_entry_t *e, void *ctx);
