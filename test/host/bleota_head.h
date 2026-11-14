/**
 * @file bleota_head.h
 * @brief The tests' image head: the first BLEOTA_HEAD_LEN bytes of an
 *        ESP-IDF application as bleota_head_check() reads them, written
 *        over a test image so that a device takes it.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_TEST_BLEOTA_HEAD_H
#define RCBENCH_TEST_BLEOTA_HEAD_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "bleota.h"

/** @brief The ESP32-C6's chip ID, the nodes' (ESP_CHIP_ID_ESP32C6). */
#define TEST_CHIP_C6 0x000Du
/** @brief The ESP32-S3's chip ID, the Dial's (ESP_CHIP_ID_ESP32S3). */
#define TEST_CHIP_S3 0x0009u

/**
 * @brief Write a 32-bit value least significant byte first.
 * @param p where
 * @param v the value
 */
static inline void test_head_put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/**
 * @brief Write an application head for @p chip and the role @p kind over
 *        the first BLEOTA_HEAD_LEN bytes of @p img.
 * @param img  the image, at least BLEOTA_HEAD_LEN bytes
 * @param chip the ESP chip ID
 * @param kind BLEOTA_KIND_*
 */
static inline void test_head_write(uint8_t *img, uint16_t chip, uint8_t kind)
{
    memset(img, 0, BLEOTA_HEAD_LEN);
    img[0] = BLEOTA_IMG_MAGIC;
    img[1] = 4u;                                /* segments */
    img[BLEOTA_HEAD_CHIP] = (uint8_t)chip;
    img[BLEOTA_HEAD_CHIP + 1u] = (uint8_t)(chip >> 8);
    test_head_put32(img + BLEOTA_HEAD_DESC, BLEOTA_DESC_MAGIC);
    test_head_put32(img + BLEOTA_HEAD_ROLE, BLEOTA_ROLE_MAGIC);
    img[BLEOTA_HEAD_ROLE + 4u] = kind;
    img[BLEOTA_HEAD_ROLE + 5u] = BLEOTA_PROTO;
}

#endif
