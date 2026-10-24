/*
 * Panel application images made in memory, laid out as esptool writes them:
 * the 24-byte image header, segments each behind an 8-byte header with the
 * application descriptor as the first segment's first 256 bytes, the
 * checksum byte at the end of a 16-byte block, and the SHA-256 of all of it.
 *
 * Every field the checks read can be set wrong on its own.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_FW_FIXTURE_H
#define RCBENCH_FW_FIXTURE_H

#include <stdint.h>
#include <string.h>

#include "fw_image.h"
#include "fw_sha256.h"

typedef struct {
    uint8_t     magic;        /* 0xE9                                   */
    uint8_t     segments;     /* 1 to 16                                */
    uint16_t    chip;         /* 0x0012, the ESP32-P4                  */
    uint32_t    desc_magic;
    const char *project;      /* copied into the 32-byte field          */
    const char *version;
    uint8_t     elf;          /* the ELF SHA-256's every byte           */
    uint8_t     hashed;       /* 1                                      */
    uint32_t    body;         /* bytes of segment data after the
                               * descriptor, spread over the segments   */
} fw_fix_t;

static inline fw_fix_t fw_fix_default(void)
{
    fw_fix_t f;
    f.magic      = FW_IMAGE_MAGIC;
    f.segments   = 3u;
    f.chip       = FW_CHIP_ESP32P4;
    f.desc_magic = FW_DESC_MAGIC;
    f.project    = FW_PROJECT;
    f.version    = "0.19.0";
    f.elf        = 0x5Au;
    f.hashed     = 1u;
    f.body       = 10000u;
    return f;
}

static inline void fw_put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/* The length fw_fix_make() writes for @p f. */
static inline uint32_t fw_fix_len(const fw_fix_t *f)
{
    const uint32_t n = (f->segments == 0u) ? 1u : f->segments;
    uint32_t at = 24u + n * 8u + 256u + f->body;
    at = (at + 1u + 15u) & ~15u;
    return at + (f->hashed ? 32u : 0u);
}

/* Write @p f into @p buf, which holds fw_fix_len() bytes; returns that. */
static inline uint32_t fw_fix_make(const fw_fix_t *f, uint8_t *buf)
{
    const uint32_t len = fw_fix_len(f);
    memset(buf, 0, len);
    buf[0] = f->magic;
    buf[1] = f->segments;
    buf[2] = 2u;                   /* DIO */
    buf[3] = 0x2Fu;                /* 8MB, 80MHz */
    fw_put32(buf + 4, 0x40375000u);
    buf[12] = (uint8_t)f->chip;
    buf[13] = (uint8_t)(f->chip >> 8);
    buf[23] = f->hashed;
    const uint32_t n = (f->segments == 0u) ? 1u : f->segments;
    uint32_t at = 24u;
    uint8_t sum = 0xEFu;
    for (uint32_t s = 0; s < n; ++s) {
        uint32_t data = f->body / n + ((s == 0u) ? f->body % n : 0u);
        if (s == 0u) {
            data += 256u;
        }
        fw_put32(buf + at, 0x3C000000u + s * 0x10000u);
        fw_put32(buf + at + 4u, data);
        at += 8u;
        uint8_t *d = buf + at;
        for (uint32_t i = 0; i < data; ++i) {
            d[i] = (uint8_t)(i * 7u + s);
        }
        if (s == 0u) {
            memset(d, 0, 256u);
            fw_put32(d, f->desc_magic);
            strncpy((char *)d + 16, f->version, 32u);
            strncpy((char *)d + 48, f->project, 32u);
            memset(d + 144, f->elf, 32u);
        }
        for (uint32_t i = 0; i < data; ++i) {
            sum ^= d[i];
        }
        at += data;
    }
    at = (at + 1u + 15u) & ~15u;
    buf[at - 1u] = sum;
    if (f->hashed) {
        fw_sha256(buf, at, buf + at);
        at += 32u;
    }
    return at;
}

/* What the panel says of itself when it runs @p version built as @p elf. */
static inline fw_image_info_t fw_fix_running(const char *version, uint8_t elf)
{
    fw_image_info_t r;
    memset(&r, 0, sizeof(r));
    (void)fw_version_parse(version, FW_TEXT_MAX, &r.version);
    strncpy(r.version_text, version, FW_TEXT_MAX - 1u);
    memset(r.elf_sha, elf, sizeof(r.elf_sha));
    return r;
}

#endif /* RCBENCH_FW_FIXTURE_H */
