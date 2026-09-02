/*
 * The SD (Secure Digital) card: mounting it, and listing what is on it.
 *
 * The card is alone on SPI2 (Serial Peripheral Interface), and its chip
 * select is on the I/O (input/output) expander rather than on a GPIO
 * (general-purpose input/output), so the SPI driver is configured with no
 * CS (chip select) pin and board_sd_cs() holds the line asserted while the
 * card is mounted.
 *
 * Everything above this file works in names and sizes, not paths and FILE
 * pointers, so a screen can be driven from a fake list on the host.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STORAGE_MOUNT_POINT "/sdcard"
#define STORAGE_NAME_MAX 64

typedef struct {
    char name[STORAGE_NAME_MAX];
    uint32_t size;
    bool is_dir;
} storage_entry_t;

/**
 * Bring up SPI2 and mount the card at /sdcard.
 *
 * @return ESP_ERR_NOT_FOUND when no card answers, which is a normal state:
 *         the bench runs without a card.
 */
esp_err_t storage_init(void);

/** Unmount and free the bus.  Safe to call when nothing was mounted. */
void storage_deinit(void);

bool storage_mounted(void);

/** Card size and free space in bytes; 0 when nothing is mounted. */
uint64_t storage_total_bytes(void);
uint64_t storage_free_bytes(void);

/** Card name from its CID, or "" when nothing is mounted. */
const char *storage_card_name(void);

/** What went wrong, for the splash line.  "" when all is well. */
const char *storage_status(void);

/**
 * List a directory.
 *
 * @param dir       relative to the mount point; "" or "/" for the root
 * @param suffixes  space-separated, lower case, e.g. ".csv .txt"; NULL takes
 *                  everything
 * @return the number of entries written, or -1 when the directory cannot be
 *         opened.  Directories sort first, then names, case-insensitively.
 */
int storage_list(const char *dir, const char *suffixes, storage_entry_t *out,
                 int max_entries);

/** Full path for a name inside @p dir, ready for fopen(). */
void storage_path(const char *dir, const char *name, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif
