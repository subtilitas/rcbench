/**
 * @file ew_id.c
 * @brief Identification; see ew_id.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "ew_id.h"

#include <string.h>

/** @brief The offset of the firmware name tag on an EFM8. */
#define TAG_OFF 0x60u
/** @brief The length of the firmware name tag. */
#define TAG_LEN 16u
/** @brief The settings marker on an EFM8. */
#define MARK_OFF 0x0Du

/**
 * @brief Whether every byte of a block is @p v.
 * @param p the bytes
 * @param n their count
 * @param v the value
 * @return true when all are
 */
static bool all_are(const uint8_t *p, size_t n, uint8_t v)
{
    for (size_t i = 0; i < n; ++i) {
        if (p[i] != v) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Whether a pin code is one AM32 lists.
 * @param code byte 3 of the answer
 * @return true when it is listed
 */
static bool pin_code_listed(uint8_t code)
{
    static const uint8_t k_codes[] = { 0x02, 0x14, 0x06, 0x0F, 0x00, 0x12,
                                       0x26 };

    for (size_t i = 0; i < sizeof(k_codes); ++i) {
        if (k_codes[i] == code) {
            return true;
        }
    }
    return false;
}

void ew_id_contact(const uint8_t *ans, ew_contact_t *out)
{
    memset(out, 0, sizeof(*out));
    out->signature = (uint16_t)(((uint16_t)ans[4] << 8) | ans[5]);
    out->pin_code = ans[3];
    out->flash_code = ans[4];
    out->protocol = ans[7];

    if (ans[4] == 0xE8u && (ans[5] == 0xB1u || ans[5] == 0xB2u)) {
        out->cpu = EW_CPU_EFM8;
        out->base = EW_EFM8_BASE_BB2;
        out->page = EW_EFM8_PAGE_BB2;
        out->boot = EW_EFM8_BOOT_BB2;
        return;
    }
    if (ans[4] == 0xE8u && ans[5] == 0xB5u) {
        out->cpu = EW_CPU_EFM8;
        out->base = EW_EFM8_BASE_BB5;
        out->page = EW_EFM8_PAGE_BB5;
        out->boot = EW_EFM8_BOOT_BB5;
        return;
    }
    if (ans[5] != 0x06u || ans[4] < 0x01u || ans[4] > 0x8Fu) {
        out->why = EW_WHY_SIGNATURE;
        return;
    }
    out->cpu = EW_CPU_ARM;
    out->base = EW_AM_ADDR_SETTINGS;
    if (ans[7] != 2u && ans[7] != 3u) {
        out->why = EW_WHY_PROTOCOL;
    } else if (!pin_code_listed(ans[3])) {
        out->why = EW_WHY_PIN_CODE;
    }
}

/**
 * @brief Whether the tag of a page begins with the word for Bluejay.
 * @param page the page
 * @return true when it does
 */
static bool tagged_bj(const uint8_t *page)
{
    static const char k_tag[] = "Bluejay";

    return memcmp(page + TAG_OFF, k_tag, sizeof(k_tag) - 1u) == 0;
}

/**
 * @brief Fill a result for a known firmware's page: writable when its layout
 *        is written and its marker fits.
 * @param out    takes the result
 * @param fw     the firmware
 * @param page   the page
 * @param shown  the layout is shown only
 */
static void efm8_known(ew_ident_t *out, ew_fw_t fw, const uint8_t *page,
                       bool shown)
{
    out->fw = (uint8_t)fw;
    out->layout = page[2];
    out->len = ew_map_len((uint8_t)fw);
    if (shown) {
        out->why = EW_WHY_OLD_LAYOUT;
    } else if (page[MARK_OFF] != 0x55u || page[MARK_OFF + 1u] != 0xAAu) {
        out->why = EW_WHY_MARKER;
    } else {
        out->writable = true;
    }
}

/**
 * @brief Whether a Bluejay layout is one of the released ones.
 * @param layout the byte at 0x02
 * @return true for 33 and 200 to 208 but 202 and 205
 */
static bool bj_layout_known(uint8_t layout)
{
    return layout == 33u
           || (layout >= 200u && layout <= 208u && layout != 202u
               && layout != 205u);
}

/**
 * @brief Set a result refused.
 * @param out takes it
 * @param why the reason
 * @param len bytes a read of the raw page may ask for
 */
static void refused(ew_ident_t *out, ew_why_t why, uint16_t len)
{
    out->fw = EW_FW_NONE;
    out->why = (uint8_t)why;
    out->len = len;
}

void ew_id_efm8(const ew_contact_t *c, const uint8_t *page, ew_ident_t *out)
{
    const bool tag_ff = all_are(page + TAG_OFF, TAG_LEN, 0xFFu);
    const bool tag_blank = all_are(page + TAG_OFF, TAG_LEN, 0x20u);

    memset(out, 0, sizeof(*out));
    if (tagged_bj(page)) {
        if (c->signature == 0xE8B1u) {
            refused(out, EW_WHY_BJ_ON_BB1, EW_MAP_LEN_S);
        } else if (!bj_layout_known(page[2])) {
            refused(out, EW_WHY_LAYOUT, EW_MAP_LEN_S);
        } else {
            efm8_known(out, EW_FW_BJ, page, page[2] != 208u);
        }
        return;
    }
    if (tag_ff && page[0] == 16u && page[2] == 33u) {
        efm8_known(out, EW_FW_S, page, false);
        out->writable = false;
        out->why = EW_WHY_RESET_PAGE;
        return;
    }
    if (tag_ff && page[0] == 0u && page[2] == 208u) {
        if (c->signature == 0xE8B1u) {
            refused(out, EW_WHY_BJ_ON_BB1, EW_MAP_LEN_S);
            return;
        }
        efm8_known(out, EW_FW_BJ, page, false);
        out->writable = false;
        out->why = EW_WHY_RESET_PAGE;
        return;
    }
    if (tag_blank && page[0] == 16u && (page[1] == 8u || page[1] == 9u)) {
        refused(out, EW_WHY_VARIANT, EW_MAP_LEN_S);
        return;
    }
    if (tag_blank && page[0] == 16u && (page[2] == 32u || page[2] == 33u)) {
        efm8_known(out, EW_FW_S, page, page[2] == 32u);
        return;
    }
    refused(out, EW_WHY_NOT_KNOWN, EW_MAP_LEN_S);
}

/**
 * @brief The name out of its region: up to the first zero byte; an erased
 *        byte, and any other byte that is no printable character, ends it too.
 * @param region the name region
 * @param out    takes EW_ID_NAME_MAX characters at most and a NUL
 */
static void name_from_region(const uint8_t *region, char *out)
{
    size_t i = 0u;

    while (i < EW_ID_NAME_MAX && region[i] >= 0x20u && region[i] <= 0x7Eu) {
        out[i] = (char)region[i];
        i++;
    }
    out[i] = '\0';
}

void ew_id_arm(const ew_contact_t *c, const uint8_t *page,
               const uint8_t *region, const uint8_t *record, ew_ident_t *out)
{
    static const uint8_t k_magic[8] = { 0xDA, 0xE3, 0x25, 0x59,
                                        0xD9, 0x63, 0xB8, 0x4E };

    memset(out, 0, sizeof(*out));
    out->len = EW_MAP_LEN_AM;
    name_from_region(region, out->name);
    if (page[0] == 0xFFu) {
        out->fw = EW_FW_AM;
        out->erased = true;
        out->why = EW_WHY_ERASED;
        return;
    }
    if (page[0] != 0x01u) {
        refused(out, EW_WHY_START_MARKER, EW_MAP_LEN_AM);
        return;
    }
    out->fw = EW_FW_AM;
    out->layout = page[1];
    if (page[1] < 3u) {
        out->why = EW_WHY_OLD_REVISION;
    } else if (page[1] > 4u) {
        out->why = EW_WHY_NEW_REVISION;
    } else if (record != NULL && memcmp(record, k_magic, sizeof(k_magic)) == 0
               && c->protocol == 3u
               && (((uint16_t)record[20] << 8) | record[19])
                      == EW_ID_CAN_START) {
        out->why = EW_WHY_CAN_BUILD;
    } else {
        out->writable = true;
    }
}
