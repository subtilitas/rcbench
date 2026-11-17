/**
 * @file fw_run.c
 * @brief The run note: see fw_run.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "fw_run.h"

#include <stddef.h>
#include <string.h>

/**
 * @brief The length of a version text the note can hold.
 * @param text The text; NULL reads as empty.
 * @return 1 to FW_RUN_TEXT_MAX; 0 when it is empty, too long or holds a
 *         byte outside 0x21 to 0x7E.
 */
static size_t text_len(const char *text)
{
    if (text == NULL) {
        return 0u;
    }
    size_t n = 0u;
    while (text[n] != '\0') {
        const unsigned char c = (unsigned char)text[n];
        if (n == FW_RUN_TEXT_MAX || c < 0x21u || c > 0x7Eu) {
            return 0u;
        }
        ++n;
    }
    return n;
}

/**
 * @brief Decode an entry.
 * @param e      The entry's bytes.
 * @param len    Its length, as the store gave it.
 * @param expect Where the version goes, terminated; FW_RUN_TEXT_MAX + 1
 *               bytes.
 * @param opens  Where the openings left go.
 * @return False when the entry is not a note in this layout.
 */
static bool decode(const uint8_t *e, uint32_t len, char *expect,
                   uint8_t *opens)
{
    if (len != FW_RUN_BYTES || e[0] != FW_RUN_LAYOUT) {
        return false;
    }
    const uint8_t n = e[1u + FW_RUN_TEXT_MAX];
    if (n < 1u || n > FW_RUN_OPENS) {
        return false;
    }
    memcpy(expect, e + 1u, FW_RUN_TEXT_MAX);
    expect[FW_RUN_TEXT_MAX] = '\0';
    const size_t t = text_len(expect);
    if (t == 0u) {
        return false;
    }
    for (size_t i = t; i < FW_RUN_TEXT_MAX; ++i) {
        if (e[1u + i] != 0u) {
            return false;   /* a byte behind the padding's start */
        }
    }
    *opens = n;
    return true;
}

/**
 * @brief Encode an entry.
 * @param e      FW_RUN_BYTES bytes.
 * @param expect The version; text_len() already checked it.
 * @param n      Its length.
 * @param opens  The openings left.
 */
static void encode(uint8_t *e, const char *expect, size_t n, uint8_t opens)
{
    memset(e, 0, FW_RUN_BYTES);
    e[0] = FW_RUN_LAYOUT;
    memcpy(e + 1u, expect, n);
    e[1u + FW_RUN_TEXT_MAX] = opens;
}

bool fw_run_begin(const fw_run_ops_t *ops, fw_run_t *run, const char *expect)
{
    const size_t n = text_len(expect);
    if (n == 0u) {
        /* Refused: a note that stands must not outlive this begin. */
        if (!run->clear) {
            run->clear = ops->erase(ops->ctx);
        }
        return false;
    }
    uint8_t e[FW_RUN_BYTES];
    encode(e, expect, n, (uint8_t)FW_RUN_OPENS);
    if (!ops->save(ops->ctx, e, (uint32_t)sizeof(e))) {
        run->clear = false;   /* the old entry, if any, may stand */
        return false;
    }
    run->clear = false;
    return true;
}

bool fw_run_after_install(const fw_run_ops_t *ops, fw_run_t *run,
                          fw_install_status_t st, const char *version)
{
    return st == FW_INSTALL_DONE && fw_run_begin(ops, run, version);
}

fw_run_seen_t fw_run_settle(const fw_run_ops_t *ops, fw_run_t *run,
                            const char *running)
{
    if (run->settled) {
        return run->seen;
    }
    run->settled = true;
    run->active = false;
    run->opens_left = 0u;
    run->expect[0] = '\0';

    uint8_t e[FW_RUN_BYTES];
    uint8_t opens = 0u;
    const uint32_t len = ops->load(ops->ctx, e, (uint32_t)sizeof(e));
    if (len == 0u) {
        run->clear = true;
        run->seen = FW_RUN_NONE;
        return run->seen;
    }
    if (!decode(e, len, run->expect, &opens)) {
        run->expect[0] = '\0';
        run->clear = ops->erase(ops->ctx);
        run->seen = FW_RUN_FOREIGN;
        return run->seen;
    }
    if (running == NULL || strcmp(run->expect, running) != 0) {
        /* A rollback, or a build put on by USB: the run is over. */
        run->clear = ops->erase(ops->ctx);
        run->seen = FW_RUN_MISMATCH;
        return run->seen;
    }
    /*
     * An opening is spent before it is used: one the store will not take
     * is not used, so a store that cannot be written opens nothing at
     * every start rather than something at every start.
     */
    const uint8_t left = (uint8_t)(opens - 1u);
    bool spent;
    if (left == 0u) {
        spent = ops->erase(ops->ctx);
        run->clear = spent;
    } else {
        encode(e, run->expect, strlen(run->expect), left);
        spent = ops->save(ops->ctx, e, (uint32_t)sizeof(e));
        run->clear = false;
    }
    run->active = spent;
    run->opens_left = spent ? left : 0u;
    run->seen = spent ? FW_RUN_STANDS : FW_RUN_UNSPENT;
    return run->seen;
}

bool fw_run_end(const fw_run_ops_t *ops, fw_run_t *run)
{
    run->active = false;
    run->opens_left = 0u;
    if (!run->clear) {
        run->clear = ops->erase(ops->ctx);
    }
    return run->clear;
}
