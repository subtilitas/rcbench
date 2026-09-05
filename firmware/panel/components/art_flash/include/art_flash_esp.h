/*
 * The board-photograph partition, as an art_flash_t.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_ART_FLASH_ESP_H
#define RCBENCH_ART_FLASH_ESP_H

#include "art_store.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The store's view of the `boardart` partition, or NULL when there is none.
 *
 * A build flashed with an older partition table has no such partition, and
 * that is not a fault: pictures are then fetched every time the link comes
 * up, which is slow and correct rather than fast and wrong.
 */
const art_flash_t *art_flash_esp(void);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_ART_FLASH_ESP_H */
