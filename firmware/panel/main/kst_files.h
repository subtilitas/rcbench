/*
 * The card as the servo programmer's files see it: the 5 functions of
 * kst_store_io_t over the directory KST of the mounted card.
 *
 * Every call blocks for as long as the card takes.  They run on the task
 * that owns the card, never on the render loop.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "kst_store.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The card for kst_card_run(). */
const kst_store_io_t *kst_files_io(void);

/** Create the directory when it is missing.  False when it is not there
 *  afterwards. */
bool kst_files_dir(void);

/** The full path of @p name inside the directory, ready for fopen(). */
void kst_files_path(const char *name, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif
