/**
 * @file
 * @brief The card as the servo programmer's files see it: the 5 functions of
 *        servo_store_io_t over the folder BENCH/KST/CONF of the mounted card
 *        (CARD_KST_CONF).
 *
 * Every call blocks for as long as the card takes.  They run on the task
 * that owns the card, never on the render loop.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "servo_store.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The card for kst_card_run(), with the stem KST_CARD_STEM.
 *
 * With @p held, the caller holds the volume (storage_hold()) for the whole
 * request; without, the request finds no card, even one another task mounts
 * meanwhile.
 *
 * @param held Whether the caller holds the volume.
 * @return The store's card functions.
 */
const servo_store_io_t *kst_files_io(bool held);

/** Make the folder, each missing level of it.  False when it is not
 *  there afterwards. */
bool kst_files_dir(void);

/** The full path of @p name inside the directory, ready for fopen(). */
void kst_files_path(const char *name, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif
