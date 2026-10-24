/*
 * heap_caps_malloc() and heap_caps_free() for the host: malloc() and
 * free(), every capability the same.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_STUB_ESP_HEAP_CAPS_H
#define RCBENCH_STUB_ESP_HEAP_CAPS_H

#include <stdint.h>
#include <stdlib.h>

#define MALLOC_CAP_SPIRAM   (1u << 10)
#define MALLOC_CAP_INTERNAL (1u << 11)

/** @brief malloc(), whatever @p caps asks. */
static inline void *heap_caps_malloc(size_t size, uint32_t caps)
{
    (void)caps;
    return malloc(size);
}

/** @brief free(). */
static inline void heap_caps_free(void *p)
{
    free(p);
}

#endif /* RCBENCH_STUB_ESP_HEAP_CAPS_H */
