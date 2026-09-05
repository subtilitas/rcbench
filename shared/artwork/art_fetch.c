/*
 * Fetching a board's photograph. See art_fetch.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "art_fetch.h"

#include <stddef.h>
#include <string.h>

#include "link_pages.h"

static art_fetch_state_t fail(art_fetch_t *f, const char *why)
{
    f->state = ART_FETCH_FAILED;
    f->why   = why;
    return f->state;
}

bool art_fetch_meta(const art_transport_t *t, uint16_t *meta_regs,
                    uint32_t *bytes, const char **why)
{
    const char *reason = "";
    bool ok = false;

    if (t == NULL || t->read_page == NULL || meta_regs == NULL
        || bytes == NULL) {
        reason = "there is no link to ask on";
    } else if (!t->read_page(t->ctx, (uint8_t)LINK_PAGE_ARTWORK,
                             (uint8_t)LINK_AW_COUNT, meta_regs)) {
        /* No such page, or nothing answered. A coprocessor built before the
         * page refuses it by design, every time the link comes up. */
        reason = "it does not answer the artwork page";
    } else if (meta_regs[LINK_AW_BLOCKS] == 0u) {
        reason = "it carries no photograph of itself";
    } else {
        link_art_meta_t m;
        if (!link_artxfer_meta_ok(meta_regs, &m)) {
            /* Worth saying out loud: this one is a coprocessor disagreeing
             * with itself, not a board that simply has no picture. */
            reason = "it describes a photograph that cannot be one";
        } else {
            *bytes = m.bytes;
            ok = true;
        }
    }

    if (!ok && bytes != NULL) {
        *bytes = 0u;       /* nothing to size an allocation from */
    }
    if (why != NULL) {
        *why = reason;
    }
    return ok;
}

bool art_fetch_begin(art_fetch_t *f, const uint16_t *meta_regs, uint8_t *buf,
                     uint32_t cap)
{
    if (f == NULL) {
        return false;
    }
    memset(f, 0, sizeof(*f));
    f->why = "";
    if (!link_artxfer_begin(&f->x, meta_regs, buf, cap)) {
        (void)fail(f, "it describes a picture that cannot be one");
        return false;
    }
    f->state = ART_FETCH_RUNNING;
    return true;
}

art_fetch_state_t art_fetch_step(art_fetch_t *f, const art_transport_t *t,
                                 unsigned blocks)
{
    if (f == NULL) {
        return ART_FETCH_FAILED;
    }
    if (t == NULL || t->read_page == NULL || t->write_reg == NULL) {
        return fail(f, "there is no link to ask on");
    }
    if (f->state != ART_FETCH_RUNNING) {
        return f->state;
    }

    for (unsigned i = 0; i < blocks && f->state == ART_FETCH_RUNNING; ++i) {
        const uint16_t want = link_artxfer_next(&f->x);
        /*
         * Say which block, then read it.  The far end does not advance on
         * its own, so a reply that goes missing is asked for again rather
         * than skipped -- which is the whole reason this is two
         * transactions and not one.
         */
        if (!t->write_reg(t->ctx, (uint8_t)LINK_PAGE_ART_DATA,
                          (uint8_t)LINK_AD_BLOCK, want)) {
            return fail(f, "the far end would not take a block number");
        }
        uint16_t regs[LINK_AD_COUNT];
        if (!t->read_page(t->ctx, (uint8_t)LINK_PAGE_ART_DATA,
                          (uint8_t)LINK_AD_COUNT, regs)) {
            return fail(f, "a block did not arrive");
        }
        if (!link_artxfer_take(&f->x, regs)) {
            return fail(f, "a block arrived that was not the one asked for");
        }
        if (link_artxfer_complete(&f->x)) {
            if (!link_artxfer_verify(&f->x)) {
                /* Every block arrived and the whole is not the picture.
                 * Keeping it would cache the corruption. */
                return fail(f, "it did not match its own checksum");
            }
            f->state = ART_FETCH_DONE;
        }
    }
    return f->state;
}

uint16_t art_fetch_width(const art_fetch_t *f)
{
    return (f != NULL) ? f->x.meta.width : 0u;
}

uint16_t art_fetch_height(const art_fetch_t *f)
{
    return (f != NULL) ? f->x.meta.height : 0u;
}

uint16_t art_fetch_crc(const art_fetch_t *f)
{
    return (f != NULL) ? f->x.meta.crc : 0u;
}

uint32_t art_fetch_bytes(const art_fetch_t *f)
{
    return (f != NULL) ? f->x.meta.bytes : 0u;
}

const char *art_fetch_why(const art_fetch_t *f)
{
    return (f != NULL && f->why != NULL) ? f->why : "";
}
