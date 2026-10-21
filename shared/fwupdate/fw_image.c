/*
 * The panel's application image. See fw_image.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "fw_image.h"

#include <string.h>

/* Offsets in the image, from esp_app_format.h and esp_app_desc.h. */
#define OFF_SEGMENTS    1u
#define OFF_CHIP        12u
#define OFF_HASHED      23u
#define HEADER_BYTES    24u
#define SEGHEAD_BYTES   8u
#define OFF_DESC        (HEADER_BYTES + SEGHEAD_BYTES)
#define DESC_BYTES      256u
#define OFF_VERSION     (OFF_DESC + 16u)
#define OFF_PROJECT     (OFF_DESC + 48u)
#define OFF_ELF_SHA     (OFF_DESC + 144u)

_Static_assert(OFF_DESC + DESC_BYTES == FW_HEAD_BYTES,
               "the head is the header, one segment header and the descriptor");

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16)
           | ((uint32_t)p[3] << 24);
}

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

const char *fw_err_str(fw_err_t e)
{
    switch (e) {
    case FW_OK:            return "ok";
    case FW_ERR_EMPTY:     return "empty";
    case FW_ERR_SHORT:     return "cut short";
    case FW_ERR_TOO_BIG:   return "larger than the slot";
    case FW_ERR_NOT_IMAGE: return "not an application image";
    case FW_ERR_CHIP:      return "built for another chip";
    case FW_ERR_NO_DESC:   return "no application descriptor";
    case FW_ERR_PROJECT:   return "another project";
    case FW_ERR_VERSION:   return "no MAJOR.MINOR.PATCH version";
    case FW_ERR_NO_HASH:   return "no appended SHA-256";
    case FW_ERR_LENGTH:    return "segments do not add up";
    case FW_ERR_HASH:      return "SHA-256 mismatch";
    case FW_ERR_READ:      return "transfer broke off";
    case FW_ERR_RUNNING:   return "the running build";
    case FW_ERR_MEMORY:    return "no memory";
    case FW_ERR_FLASH:     return "flash failed";
    case FW_ERR_BUSY:      return "another image is staged";
    case FW_ERR_COUNT:
    default:               return "?";
    }
}

bool fw_version_parse(const char *text, size_t n, fw_version_t *out)
{
    if (text == NULL) {
        return false;
    }
    uint32_t part[3] = { 0u, 0u, 0u };
    unsigned which = 0u;
    unsigned digits = 0u;
    size_t i = 0u;
    for (; i < n && text[i] != '\0'; ++i) {
        const char ch = text[i];
        if (ch >= '0' && ch <= '9') {
            part[which] = part[which] * 10u + (uint32_t)(ch - '0');
            if (part[which] > 0xFFFFu) {
                return false;
            }
            ++digits;
        } else if (ch == '.' && digits > 0u && which < 2u) {
            ++which;
            digits = 0u;
        } else {
            return false;
        }
    }
    if (which != 2u || digits == 0u) {
        return false;
    }
    if (out != NULL) {
        out->major = (uint16_t)part[0];
        out->minor = (uint16_t)part[1];
        out->patch = (uint16_t)part[2];
    }
    return true;
}

int fw_version_cmp(fw_version_t a, fw_version_t b)
{
    if (a.major != b.major) {
        return (a.major < b.major) ? -1 : 1;
    }
    if (a.minor != b.minor) {
        return (a.minor < b.minor) ? -1 : 1;
    }
    if (a.patch != b.patch) {
        return (a.patch < b.patch) ? -1 : 1;
    }
    return 0;
}

fw_err_t fw_image_head(const uint8_t *head, uint32_t have, uint32_t bytes,
                       uint32_t slot, fw_image_info_t *out)
{
    if (bytes == 0u) {
        return FW_ERR_EMPTY;
    }
    if (bytes > slot) {
        return FW_ERR_TOO_BIG;
    }
    if (bytes < FW_HEAD_BYTES) {
        return FW_ERR_SHORT;
    }
    if (head == NULL || have < FW_HEAD_BYTES) {
        return FW_ERR_READ;
    }
    if (head[0] != FW_IMAGE_MAGIC || head[OFF_SEGMENTS] == 0u
        || head[OFF_SEGMENTS] > FW_MAX_SEGMENTS) {
        return FW_ERR_NOT_IMAGE;
    }
    if (le16(head + OFF_CHIP) != FW_CHIP_ESP32P4) {
        return FW_ERR_CHIP;
    }
    if (le32(head + HEADER_BYTES + 4u) < DESC_BYTES
        || le32(head + OFF_DESC) != FW_DESC_MAGIC) {
        return FW_ERR_NO_DESC;
    }
    /* The whole field, terminator included: "rcbench-panel-x" is another
     * project, and so is a name that fills the field without one. */
    const char *project = (const char *)(head + OFF_PROJECT);
    if (memchr(project, '\0', FW_TEXT_MAX) == NULL
        || strcmp(project, FW_PROJECT) != 0) {
        return FW_ERR_PROJECT;
    }
    fw_version_t v;
    const char *version = (const char *)(head + OFF_VERSION);
    if (memchr(version, '\0', FW_TEXT_MAX) == NULL
        || !fw_version_parse(version, FW_TEXT_MAX, &v)) {
        return FW_ERR_VERSION;
    }
    if (head[OFF_HASHED] != 1u) {
        return FW_ERR_NO_HASH;
    }
    if (out != NULL) {
        memset(out, 0, sizeof(*out));
        out->bytes = bytes;
        out->version = v;
        memcpy(out->version_text, version, FW_TEXT_MAX);
        memcpy(out->elf_sha, head + OFF_ELF_SHA, FW_SHA256_BYTES);
    }
    return FW_OK;
}

/*
 * The length the segment table gives: the header, each segment's header and
 * data, the checksum byte padded to 16, and the hash.  0 when a segment runs
 * past @p bytes, which is a file cut short.
 */
static uint32_t image_length(const uint8_t *img, uint32_t bytes)
{
    uint64_t at = HEADER_BYTES;
    const unsigned n = img[OFF_SEGMENTS];
    for (unsigned i = 0; i < n; ++i) {
        if (at + SEGHEAD_BYTES > bytes) {
            return 0u;
        }
        at += SEGHEAD_BYTES + (uint64_t)le32(img + at + 4u);
        if (at > bytes) {
            return 0u;
        }
    }
    at = ((at + 1u) + 15u) & ~(uint64_t)15u;
    at += FW_SHA256_BYTES;
    return (at > bytes) ? 0u : (uint32_t)at;
}

fw_err_t fw_image_check(const uint8_t *img, uint32_t bytes, uint32_t slot,
                        fw_image_info_t *out)
{
    fw_image_info_t info;
    const fw_err_t e = fw_image_head(img, bytes, bytes, slot, &info);
    if (e != FW_OK) {
        return e;
    }
    const uint32_t len = image_length(img, bytes);
    if (len == 0u) {
        return FW_ERR_SHORT;
    }
    if (len != bytes) {
        return FW_ERR_LENGTH;
    }
    uint8_t sha[FW_SHA256_BYTES];
    fw_sha256(img, bytes - FW_SHA256_BYTES, sha);
    if (memcmp(sha, img + bytes - FW_SHA256_BYTES, FW_SHA256_BYTES) != 0) {
        return FW_ERR_HASH;
    }
    if (out != NULL) {
        *out = info;
    }
    return FW_OK;
}

fw_offer_t fw_offer(const fw_image_info_t *running,
                    const fw_image_info_t *image)
{
    const int cmp = fw_version_cmp(image->version, running->version);
    if (cmp > 0) {
        return FW_OFFER_NEWER;
    }
    if (cmp < 0) {
        return FW_OFFER_OLDER;
    }
    return (memcmp(image->elf_sha, running->elf_sha, FW_SHA256_BYTES) == 0)
               ? FW_OFFER_INSTALLED
               : FW_OFFER_REBUILD;
}

bool fw_offer_installable(fw_offer_t o)
{
    return o == FW_OFFER_NEWER || o == FW_OFFER_OLDER
           || o == FW_OFFER_REBUILD;
}
