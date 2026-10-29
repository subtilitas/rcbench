/**
 * @file fw_boot.c
 * @brief The first start after an update, and the restart note: see
 *        fw_boot.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "fw_boot.h"

#include <stddef.h>
#include <string.h>

fw_boot_t fw_boot_decide(const fw_boot_ops_t *ops, bool ui_up, bool noted)
{
    if (ops->pending(ops->ctx)) {
        if (!ui_up) {
            /*
             * Back to the build before.  mark_invalid restarts when it
             * works; when it returns, a plain restart leaves the trial
             * unconfirmed, which the bootloader also takes back.  Either
             * way this build is never marked valid.
             */
            ops->mark_invalid(ops->ctx);
            ops->restart(ops->ctx);
            return FW_BOOT_UNCONFIRMED;
        }
        return ops->mark_valid(ops->ctx) ? FW_BOOT_UPDATED
                                         : FW_BOOT_UNCONFIRMED;
    }
    /*
     * A start after a written build that did not stay: the bootloader has
     * marked that slot invalid and started this one.  The slot stays marked
     * until the next update, so the note from before the restart is what
     * makes this report once.
     */
    if (noted && ops->last_invalid(ops->ctx)) {
        return FW_BOOT_ROLLED_BACK;
    }
    return FW_BOOT_NORMAL;
}

/* ------------------------------------------------------------- the note */

/**
 * @brief FNV-1a (Fowler-Noll-Vo) over the version field.
 * @param text FW_TEXT_MAX bytes.
 * @return The hash.
 */
static uint32_t note_hash(const uint8_t *text)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < FW_TEXT_MAX; ++i) {
        h = (h ^ text[i]) * 16777619u;
    }
    return h;
}

/**
 * @brief Store @p v little-endian.
 * @param p Four bytes.
 * @param v The word.
 */
static void put_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/**
 * @brief Read a little-endian word.
 * @param p Four bytes.
 * @return The word.
 */
static uint32_t get_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16)
           | ((uint32_t)p[3] << 24);
}

bool fw_note_write(const fw_note_ops_t *ops, const char *version)
{
    uint8_t e[FW_NOTE_BYTES];
    memset(e, 0, sizeof(e));
    const size_t n = strnlen(version, FW_TEXT_MAX - 1u);
    memcpy(e + 4u, version, n);
    put_le32(e, FW_NOTE_MAGIC);
    put_le32(e + 4u + FW_TEXT_MAX, note_hash(e + 4u));
    return ops->save(ops->ctx, e, (uint32_t)sizeof(e));
}

fw_note_state_t fw_note_read(const fw_note_ops_t *ops, fw_note_seen_t *seen)
{
    uint8_t e[FW_NOTE_BYTES];
    memset(seen, 0, sizeof(*seen));
    const uint32_t len = ops->load(ops->ctx, e, (uint32_t)sizeof(e));
    if (len == 0u) {
        seen->state = FW_NOTE_NONE;
    } else if (len != FW_NOTE_BYTES || get_le32(e) != FW_NOTE_MAGIC
               || get_le32(e + 4u + FW_TEXT_MAX) != note_hash(e + 4u)
               || memchr(e + 4u, '\0', FW_TEXT_MAX) == NULL) {
        seen->state = FW_NOTE_FOREIGN;
    } else {
        seen->state = FW_NOTE_FOUND;
        memcpy(seen->version, e + 4u, FW_TEXT_MAX);
    }
    return seen->state;
}

fw_boot_t fw_boot_settle(const fw_boot_ops_t *boot, const fw_note_ops_t *note,
                         bool ui_up, fw_note_seen_t *seen)
{
    const bool noted = (fw_note_read(note, seen) == FW_NOTE_FOUND);
    const fw_boot_t r = fw_boot_decide(boot, ui_up, noted);
    /*
     * A trial that goes back restarts inside fw_boot_decide() on the
     * panel; when it returns, the note still waits for the build before.
     */
    const bool going_back = (r == FW_BOOT_UNCONFIRMED && !ui_up);
    if (!going_back && seen->state != FW_NOTE_NONE) {
        (void)note->erase(note->ctx);
    }
    return r;
}
