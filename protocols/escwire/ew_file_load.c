/**
 * @file ew_file_load.c
 * @brief The settings of an ESC as a text file: a file held against an ESC, for
 *        a load or a restore; see ew_file.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "ew_file_int.h"

#include <string.h>

/* --- a file against an ESC ------------------------------------------------ */

/**
 * @brief Whether a line of an edited file reads as the ESC's value already
 *        does: several bytes of a percentage read as one text, and the line
 *        gave the lowest of them.  A line of `raw <n>` names its byte.
 * @param f    the file
 * @param fd   the field of the byte, may be NULL
 * @param off  the byte
 * @param have the ESC's byte
 * @return true when the load leaves the byte
 */
static bool edited_same(const ew_file_t *f, const ew_field_t *fd, size_t off,
                        uint8_t have)
{
    char a[VALUE_MAX];
    char b[VALUE_MAX];

    if (f->kind != (uint8_t)EW_FILE_EDITED || fd == NULL
        || (f->raw[off / 8u] & (1u << (off % 8u))) != 0u) {
        return false;
    }
    (void)ew_field_text(fd, have, a, sizeof(a));
    (void)ew_field_text(fd, f->page[off], b, sizeof(b));
    return strcmp(a, b) == 0;
}

/**
 * @brief ESCape32's keys into the target.  `music` is not among them.
 * @param f   the file
 * @param esc the keys as read
 * @param out takes the result
 */
static void load_keys(const ew_file_t *f, const uint8_t *esc,
                      ew_file_load_t *out)
{
    out->fields = EW_E32_KEYS;
    for (size_t i = 0; i < EW_E32_KEYS; ++i) {
        const int16_t v = ew_e32_get(f->page, i);
        const bool gives = f->kind == (uint8_t)EW_FILE_VERIFIED
                               ? v != EW_KEY_NONE
                               : (f->has[i / 8u] & (1u << (i % 8u))) != 0u;

        if (!gives) {
            continue;
        }
        out->named++;
        if (v != ew_e32_get(esc, i)) {
            ew_e32_put(out->target, i, v);
            out->changed++;
        }
    }
}

ew_load_t ew_file_load(const ew_file_t *f, const ew_ident_t *id,
                       const uint8_t *esc, ew_file_load_t *out)
{
    if (f == NULL || id == NULL || esc == NULL || out == NULL
        || id->len > EW_PAGE_MAX) {
        return EW_LOAD_ARG;
    }
    memset(out, 0, sizeof(*out));
    out->len = id->len;
    memcpy(out->target, esc, id->len);
    if (f->fw != id->fw) {
        return EW_LOAD_FIRMWARE;
    }
    if (id->erased) {
        return EW_LOAD_ERASED;
    }
    if (f->layout != id->layout) {
        return EW_LOAD_LAYOUT;
    }
    if (!id->writable) {
        return EW_LOAD_READ_ONLY;
    }
    for (size_t i = 0; f->fw != (uint8_t)EW_FW_E32 && i < id->len; ++i) {
        const ew_field_t *fd = ew_map_find(f->fw, f->layout, (uint8_t)i);
        const bool editable = fd != NULL && fd->access == (uint8_t)EW_ACC_EDIT;
        const bool gives = f->kind == (uint8_t)EW_FILE_VERIFIED
                           || (f->has[i / 8u] & (1u << (i % 8u))) != 0u;

        if (editable) {
            out->fields++;
        }
        if (!gives) {
            continue;
        }
        if (editable) {
            out->named++;
        }
        if (f->page[i] == esc[i] || edited_same(f, fd, i, esc[i])) {
            continue;
        }
        if (editable) {
            out->target[i] = f->page[i];
            out->changed++;
        } else {
            out->skipped++;
        }
    }
    if (f->fw == (uint8_t)EW_FW_E32 && id->len == EW_MAP_LEN_E32) {
        load_keys(f, esc, out);
    }
    uint16_t off = 0u;

    if (ew_image_check(f->fw, f->layout, esc, out->target, id->len, &off)
        != EW_IMG_OK) {
        out->bad_off = off;
        memcpy(out->target, esc, id->len);
        return EW_LOAD_VALUE;
    }
    return EW_LOAD_OK;
}

ew_restore_t ew_file_restore_check(const ew_file_t *f, const ew_ident_t *id,
                                   const ew_backup_id_t *here,
                                   const uint8_t *esc, ew_backup_id_t *from)
{
    if (f == NULL || id == NULL || here == NULL || esc == NULL
        || from == NULL) {
        return EW_RESTORE_ARG;
    }
    if (f->kind != (uint8_t)EW_FILE_VERIFIED) {
        return EW_RESTORE_NOT_VERIFIED;
    }
    if (f->fw != (uint8_t)EW_FW_AM || id->fw != (uint8_t)EW_FW_AM) {
        return EW_RESTORE_FIRMWARE;
    }
    if (!id->erased) {
        return EW_RESTORE_NOT_ERASED;
    }
    for (size_t i = 0; i < EW_MAP_LEN_AM; ++i) {
        if (esc[i] != 0xFFu) {
            return EW_RESTORE_NOT_ERASED;
        }
    }
    if (id->can_build) {
        return EW_RESTORE_CAN_BUILD;
    }
    if (f->page[0] != 0x01u || !ew_map_writable(EW_FW_AM, f->page[1])) {
        return EW_RESTORE_BAD_PAGE;
    }
    if (f->pin_code != here->pin_code) {
        return EW_RESTORE_PIN_CODE;
    }
    if (f->flash_code != here->flash_code) {
        return EW_RESTORE_FLASH_CODE;
    }
    if (f->protocol != here->protocol) {
        return EW_RESTORE_PROTOCOL;
    }
    if (f->name[0] == '\0') {
        return EW_RESTORE_NO_NAME;
    }
    if (strncmp(f->name, here->name, sizeof(f->name)) != 0) {
        return EW_RESTORE_NAME;
    }
    memset(from, 0, sizeof(*from));
    from->pin_code = f->pin_code;
    from->flash_code = f->flash_code;
    from->protocol = f->protocol;
    memcpy(from->name, f->name, sizeof(from->name));
    return EW_RESTORE_OK;
}
