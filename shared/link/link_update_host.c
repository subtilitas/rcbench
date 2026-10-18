/*
 * The panel's half of the IOmcu update.  See link_update_host.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "link_update_host.h"

#include <stddef.h>
#include <string.h>

#include "link_crc.h"
#include "link_msg.h"

/* How an exchange ended. */
typedef enum {
    X_NONE = 0,   /* unanswered                       */
    X_OK,         /* DATA for a read, ACK for a write  */
    X_NACK,       /* refused: nack holds the reason    */
} x_t;

static void start(link_uph_t *u, const uint8_t *img, uint32_t len,
                  link_uph_phase_t phase, uint32_t now_ms)
{
    memset(u, 0, sizeof(*u));
    u->img = img;
    u->len = len;
    u->flag_at = link_update_tbyb_byte(img, len);
    u->phase = phase;
    u->since_ms = now_ms;
}

static void done(link_uph_t *u, uint8_t result)
{
    u->phase = LINK_UPH_DONE;
    u->result = result;
}

void link_uph_check(link_uph_t *u, const uint8_t *img, uint32_t len,
                    uint32_t now_ms)
{
    if (u == NULL) {
        return;
    }
    start(u, img, len, LINK_UPH_CHECK_HASH, now_ms);
    if (img == NULL || len == 0u) {
        done(u, LINK_UPH_REFUSED);
        return;
    }
    /* The build's name: the flag read as clear (link_update_image.h). */
    link_image_hash_begin(&u->hash, len, u->flag_at, false);
}

void link_uph_update(link_uph_t *u, const uint8_t *img, uint32_t len,
                     uint32_t now_ms)
{
    if (u == NULL) {
        return;
    }
    start(u, img, len, LINK_UPH_PREPARE, now_ms);
    if (img == NULL || len == 0u
        || (len + LINK_UPD_BLOCK_BYTES - 1u) / LINK_UPD_BLOCK_BYTES
               > 0xFFFFu) {
        done(u, LINK_UPH_REFUSED);
        return;
    }
    u->blocks = (uint16_t)((len + LINK_UPD_BLOCK_BYTES - 1u)
                           / LINK_UPD_BLOCK_BYTES);
    /* The image as it goes over the wire: the flag set, so the boot ROM
     * starts it on trial. */
    link_image_hash_begin(&u->hash, len, u->flag_at, true);
}

void link_uph_confirm(link_uph_t *u, uint32_t now_ms)
{
    if (u == NULL) {
        return;
    }
    start(u, NULL, 0u, LINK_UPH_BUY, now_ms);
}

uint16_t link_uph_permille(const link_uph_t *u)
{
    if (u == NULL || u->blocks == 0u) {
        return 0u;
    }
    return (uint16_t)((uint32_t)u->block * 1000u / u->blocks);
}

/* ---------------------------------------------------------------- the wire */

static x_t read_regs(link_host_t *h, const link_port_t *port, uint8_t page,
                     uint8_t offset, uint8_t count, link_msg_t *reply)
{
    if (!link_read_window(h, port, page, offset, count, reply)) {
        return X_NONE;
    }
    return (reply->op == LINK_OP_DATA) ? X_OK : X_NACK;
}

static x_t command(link_host_t *h, const link_port_t *port, uint16_t cmd,
                   uint16_t a0, uint16_t a1, link_msg_t *reply)
{
    const uint16_t regs[3] = { cmd, a0, a1 };
    if (!link_write_acked(h, port, LINK_PAGE_UPDATE_CTL, LINK_UPD_CMD, 3u,
                          regs, reply)) {
        return X_NONE;
    }
    return (reply->op == LINK_OP_ACK) ? X_OK : X_NACK;
}

/* An exchange went unanswered; true when the link has been silent for
 * LINK_UPH_LOST_MS and the update ends. */
static bool missed(link_uph_t *u, const link_port_t *port)
{
    /* Read after the exchange: an unanswered one took its timeout. */
    const uint32_t now_ms = port->now_ms(port->ctx);
    if (!u->missing) {
        u->missing = true;
        u->missing_ms = now_ms;
    }
    if ((uint32_t)(now_ms - u->missing_ms) >= LINK_UPH_LOST_MS) {
        done(u, LINK_UPH_NO_ANSWER);
        return true;
    }
    return false;
}

/* An exchange was answered. */
static void heard(link_uph_t *u)
{
    u->missing = false;
}

/* The refusal's reason, read back; the result REFUSED. */
static void refused(link_uph_t *u, link_host_t *h, const link_port_t *port,
                    const link_msg_t *nack)
{
    if (nack->regs[0] == LINK_NACK_BAD_PAGE) {
        done(u, LINK_UPH_NEEDS_USB);
        return;
    }
    link_msg_t r;
    if (read_regs(h, port, LINK_PAGE_UPDATE_CTL, 0u, LINK_UPD_SLOT + 1u, &r)
        == X_OK) {
        u->error = (uint8_t)r.regs[LINK_UPD_ERROR];
    }
    done(u, LINK_UPH_REFUSED);
}

/*
 * A command the store's gate refused: wait for it, up to LINK_UPH_BUSY_MS
 * from the first refusal.  True while still waiting.
 */
static bool wait_gate(link_uph_t *u, uint32_t now_ms)
{
    if (!u->busy) {
        u->busy = true;
        u->busy_ms = now_ms;
    }
    if ((uint32_t)(now_ms - u->busy_ms) >= LINK_UPH_BUSY_MS) {
        done(u, LINK_UPH_BUSY);
        return false;
    }
    return true;
}

/* The IOmcu lost the session: from block 0 again, or give up. */
static void start_over(link_uph_t *u, uint32_t now_ms)
{
    if (++u->restarts > LINK_UPH_RESTARTS) {
        done(u, LINK_UPH_RESTARTED_OUT);
        return;
    }
    u->block = 0u;
    u->block_tries = 0u;
    u->busy = false;
    u->phase = LINK_UPH_BEGIN;
    u->since_ms = now_ms;
}

static void resync(link_uph_t *u, uint8_t from)
{
    u->after_resync = from;
    u->phase = LINK_UPH_RESYNC;
}

/* -------------------------------------------------------------- the check */

static void step_check_hash(link_uph_t *u)
{
    if (link_image_hash_step(&u->hash, &u->img[u->hash.pos],
                             LINK_UPH_HASH_STEP)) {
        link_image_hash_end(&u->hash, u->build);
        u->phase = LINK_UPH_CHECK_ASK;
    }
}

static void step_check_ask(link_uph_t *u, link_host_t *h,
                           const link_port_t *port, uint32_t now_ms)
{
    link_msg_t r;
    const x_t x = read_regs(h, port, LINK_PAGE_UPDATE_CTL, 0u,
                            LINK_UPD_SLOT_SECTORS + 1u, &r);
    if (x == X_NONE) {
        (void)missed(u, port);
        return;
    }
    heard(u);
    if (x == X_NACK) {
        refused(u, h, port, &r);
        return;
    }
    u->slot = r.regs[LINK_UPD_SLOT];
    if ((u->slot & LINK_UPD_SLOT_TRIAL) != 0u) {
        done(u, LINK_UPH_TRIAL);
        return;
    }
    if ((u->slot & LINK_UPD_SLOT_MASK) == LINK_UPD_SLOT_NONE
        || r.regs[LINK_UPD_SLOT_SECTORS] == 0u) {
        done(u, LINK_UPH_NEEDS_USB);
        return;
    }
    if (u->len > (uint32_t)r.regs[LINK_UPD_SLOT_SECTORS]
                 * LINK_UPD_SECTOR_BYTES) {
        /* Its slot is smaller than this image: it runs another build, and
         * this one cannot go there over the link. */
        done(u, LINK_UPH_NEEDS_USB);
        return;
    }
    const uint16_t state = r.regs[LINK_UPD_STATE];
    if (state == LINK_UPD_HASHING) {
        /* A HASH_RUNNING whose acknowledgement was lost: its hash runs. */
        u->phase = LINK_UPH_CHECK_WAIT;
        u->since_ms = now_ms;
        return;
    }
    if (state == LINK_UPD_ACTIVATED) {
        /* A restart is due in LINK_UPD_RESTART_MS: wait for it. */
        if ((uint32_t)(now_ms - u->since_ms) >= LINK_UPH_HASH_MS) {
            u->error = LINK_UPD_ERR_STATE;
            done(u, LINK_UPH_REFUSED);
        }
        return;
    }
    if (state == LINK_UPD_RECEIVING || state == LINK_UPD_RECEIVED
        || state == LINK_UPD_VERIFYING || state == LINK_UPD_VERIFIED) {
        /* A session nobody sends to: ended, then read again.  Once; a
         * session that is still there after its ABORT is not this
         * panel's to end. */
        if (u->aborted) {
            u->error = LINK_UPD_ERR_STATE;
            done(u, LINK_UPH_REFUSED);
            return;
        }
        const x_t a = command(h, port, LINK_UPD_CMD_ABORT, 0u, 0u, &r);
        if (a == X_NONE) {
            (void)missed(u, port);
            return;
        }
        if (a == X_NACK) {
            refused(u, h, port, &r);
            return;
        }
        u->aborted = true;
        return;
    }
    const x_t c = command(h, port, LINK_UPD_CMD_HASH_RUNNING,
                          (uint16_t)(u->len & 0xFFFFu),
                          (uint16_t)(u->len >> 16), &r);
    if (c == X_NONE) {
        (void)missed(u, port);
        return;
    }
    if (c == X_NACK) {
        refused(u, h, port, &r);
        return;
    }
    u->phase = LINK_UPH_CHECK_WAIT;
    u->since_ms = now_ms;
}

static void step_check_wait(link_uph_t *u, link_host_t *h,
                            const link_port_t *port, uint32_t now_ms)
{
    link_msg_t r;
    const x_t x = read_regs(h, port, LINK_PAGE_UPDATE_CTL, 0u,
                            LINK_UPD_COUNT, &r);
    if (x == X_NONE) {
        (void)missed(u, port);
        return;
    }
    heard(u);
    if (x == X_NACK) {
        refused(u, h, port, &r);
        return;
    }
    const uint32_t size = (uint32_t)r.regs[LINK_UPD_SIZE_LO]
                          | ((uint32_t)r.regs[LINK_UPD_SIZE_HI] << 16);
    if (r.regs[LINK_UPD_STATE] == LINK_UPD_HASHING) {
        if ((uint32_t)(now_ms - u->since_ms) >= LINK_UPH_HASH_MS) {
            done(u, LINK_UPH_NO_ANSWER);
        }
        return;
    }
    if (r.regs[LINK_UPD_DIGEST_OF] != LINK_UPD_DIGEST_RUNNING
        || size != u->len) {
        /* Something else asked it for another hash meanwhile. */
        u->error = (uint8_t)r.regs[LINK_UPD_ERROR];
        done(u, LINK_UPH_REFUSED);
        return;
    }
    for (size_t i = 0; i < LINK_SHA256_BYTES / 2u; ++i) {
        const uint16_t v = r.regs[LINK_UPD_DIGEST + i];
        u->digest[2u * i]      = (uint8_t)(v & 0xFFu);
        u->digest[2u * i + 1u] = (uint8_t)(v >> 8);
    }
    done(u, memcmp(u->digest, u->build, sizeof(u->build)) == 0
                ? LINK_UPH_SAME : LINK_UPH_DIFFERENT);
}

/* ------------------------------------------------------------- the update */

static void step_prepare(link_uph_t *u, link_host_t *h,
                         const link_port_t *port, uint32_t now_ms)
{
    if (u->hash.pos < u->len) {
        if (!link_image_hash_step(&u->hash, &u->img[u->hash.pos],
                                  LINK_UPH_HASH_STEP)) {
            return;
        }
        link_image_hash_end(&u->hash, u->expect);
    }
    /* Which slot runs now, so the restart can be told from no restart. */
    link_msg_t r;
    const x_t x = read_regs(h, port, LINK_PAGE_UPDATE_CTL, 0u,
                            LINK_UPD_SLOT_SECTORS + 1u, &r);
    if (x == X_NONE) {
        (void)missed(u, port);
        return;
    }
    heard(u);
    if (x == X_NACK) {
        refused(u, h, port, &r);
        return;
    }
    u->slot = r.regs[LINK_UPD_SLOT];
    if ((u->slot & LINK_UPD_SLOT_MASK) == LINK_UPD_SLOT_NONE) {
        done(u, LINK_UPH_NEEDS_USB);
        return;
    }
    u->phase = LINK_UPH_BEGIN;
    u->since_ms = now_ms;
}

static void step_begin(link_uph_t *u, link_host_t *h, const link_port_t *port,
                       uint32_t now_ms)
{
    link_msg_t r;
    const x_t x = command(h, port, LINK_UPD_CMD_BEGIN,
                          (uint16_t)(u->len & 0xFFFFu),
                          (uint16_t)(u->len >> 16), &r);
    if (x == X_NONE) {
        (void)missed(u, port);
        return;
    }
    heard(u);
    if (x == X_NACK) {
        if (r.regs[0] == LINK_NACK_BUSY) {
            (void)wait_gate(u, now_ms);
            return;
        }
        refused(u, h, port, &r);
        return;
    }
    u->busy = false;
    u->block = 0u;
    u->phase = LINK_UPH_SEND;
}

static void block_regs(const link_uph_t *u, uint16_t block, uint16_t *regs,
                       uint16_t *crc)
{
    const uint32_t off = (uint32_t)block * LINK_UPD_BLOCK_BYTES;
    uint32_t n = u->len - off;
    if (n > LINK_UPD_BLOCK_BYTES) {
        n = LINK_UPD_BLOCK_BYTES;
    }
    uint8_t bytes[LINK_UPD_BLOCK_BYTES];
    memset(bytes, 0xFF, sizeof(bytes));
    for (uint32_t i = 0; i < n; ++i) {
        bytes[i] = link_image_byte(u->img, off + i, u->flag_at, true);
    }
    *crc = link_crc(LINK_CRC_INIT, bytes, n);
    for (size_t i = 0; i < LINK_UPD_DATA_COUNT; ++i) {
        regs[i] = (uint16_t)(bytes[2u * i]
                             | ((uint16_t)bytes[2u * i + 1u] << 8));
    }
}

static void step_send(link_uph_t *u, link_host_t *h, const link_port_t *port,
                      uint32_t now_ms)
{
    uint16_t regs[LINK_UPD_DATA_COUNT];
    uint16_t crc = 0u;
    block_regs(u, u->block, regs, &crc);

    link_msg_t r;
    if (!link_write_acked(h, port, LINK_PAGE_UPDATE_DATA, 0u,
                          LINK_UPD_DATA_COUNT, regs, &r)) {
        if (!missed(u, port)) {
            resync(u, LINK_UPH_SEND);
        }
        return;
    }
    heard(u);
    if (r.op != LINK_OP_ACK) {
        refused(u, h, port, &r);
        return;
    }
    const x_t x = command(h, port, LINK_UPD_CMD_COMMIT, u->block, crc, &r);
    if (x == X_NONE) {
        if (!missed(u, port)) {
            resync(u, LINK_UPH_SEND);
        }
        return;
    }
    heard(u);
    if (x == X_NACK) {
        if (r.regs[0] == LINK_NACK_BUSY) {
            (void)wait_gate(u, now_ms);
            return;
        }
        if (r.regs[0] == LINK_NACK_BAD_PAGE) {
            done(u, LINK_UPH_NEEDS_USB);
            return;
        }
        resync(u, LINK_UPH_SEND);
        return;
    }
    u->busy = false;
    ++u->block;
    if (u->block >= u->blocks) {
        u->phase = LINK_UPH_FINISH;
    }
}

/*
 * Where the IOmcu stands, after an exchange went unanswered or a
 * command was refused: carry on from the block it expects, check what it
 * has, or start over from block 0 when it lost the session.
 */
static void step_resync(link_uph_t *u, link_host_t *h, const link_port_t *port,
                        uint32_t now_ms)
{
    link_msg_t r;
    const x_t x = read_regs(h, port, LINK_PAGE_UPDATE_CTL, 0u,
                            LINK_UPD_SLOT_SECTORS + 2u, &r);
    if (x == X_NONE) {
        (void)missed(u, port);
        return;
    }
    heard(u);
    if (x == X_NACK) {
        refused(u, h, port, &r);
        return;
    }
    const uint16_t state = r.regs[LINK_UPD_STATE];
    const uint16_t next  = r.regs[LINK_UPD_NEXT_BLOCK];
    u->error = (uint8_t)r.regs[LINK_UPD_ERROR];

    if ((state == LINK_UPD_RECEIVING || state == LINK_UPD_RECEIVED)
        && u->after_resync == LINK_UPH_SEND && next <= u->blocks) {
        /* The same block refused again and again is a block that will not
         * go: a CRC that keeps failing is not noise. */
        if (next == u->block_tried) {
            if (++u->block_tries > LINK_UPH_BLOCK_TRIES) {
                done(u, LINK_UPH_REFUSED);
                return;
            }
        } else {
            u->block_tried = next;
            u->block_tries = 1u;
        }
        u->block = next;
        u->phase = (next >= u->blocks) ? LINK_UPH_FINISH : LINK_UPH_SEND;
        return;
    }
    if (u->after_resync == LINK_UPH_FINISH) {
        if (state == LINK_UPD_VERIFYING || state == LINK_UPD_VERIFIED) {
            u->phase = LINK_UPH_VERIFY;   /* the FINISH went in */
            u->since_ms = now_ms;
            return;
        }
        if (state == LINK_UPD_RECEIVED) {
            if (++u->block_tries > LINK_UPH_BLOCK_TRIES) {
                done(u, LINK_UPH_REFUSED);
                return;
            }
            u->phase = LINK_UPH_FINISH;
            return;
        }
    }
    if (state == LINK_UPD_IDLE || state == LINK_UPD_FAILED
        || state == LINK_UPD_HASHING) {
        if (state == LINK_UPD_FAILED && u->error == LINK_UPD_ERR_DIGEST) {
            done(u, LINK_UPH_BAD_DIGEST);
            return;
        }
        start_over(u, now_ms);
        return;
    }
    done(u, LINK_UPH_REFUSED);
}

static void step_finish(link_uph_t *u, link_host_t *h, const link_port_t *port,
                        uint32_t now_ms)
{
    uint16_t regs[LINK_UPD_EXPECT_REGS];
    for (size_t i = 0; i < LINK_UPD_EXPECT_REGS; ++i) {
        regs[i] = (uint16_t)(u->expect[2u * i]
                             | ((uint16_t)u->expect[2u * i + 1u] << 8));
    }
    link_msg_t r;
    if (!link_write_acked(h, port, LINK_PAGE_UPDATE_DATA, 0u,
                          LINK_UPD_EXPECT_REGS, regs, &r)) {
        if (!missed(u, port)) {
            resync(u, LINK_UPH_FINISH);
        }
        return;
    }
    heard(u);
    if (r.op != LINK_OP_ACK) {
        refused(u, h, port, &r);
        return;
    }
    const x_t x = command(h, port, LINK_UPD_CMD_FINISH, 0u, 0u, &r);
    if (x == X_NONE) {
        if (!missed(u, port)) {
            resync(u, LINK_UPH_FINISH);
        }
        return;
    }
    heard(u);
    if (x == X_NACK) {
        resync(u, LINK_UPH_FINISH);
        return;
    }
    u->phase = LINK_UPH_VERIFY;
    u->since_ms = now_ms;
}

static void step_verify(link_uph_t *u, link_host_t *h, const link_port_t *port,
                        uint32_t now_ms)
{
    link_msg_t r;
    const x_t x = read_regs(h, port, LINK_PAGE_UPDATE_CTL, 0u,
                            LINK_UPD_COUNT, &r);
    if (x == X_NONE) {
        (void)missed(u, port);
        return;
    }
    heard(u);
    if (x == X_NACK) {
        refused(u, h, port, &r);
        return;
    }
    const uint16_t state = r.regs[LINK_UPD_STATE];
    if (state == LINK_UPD_VERIFYING) {
        if ((uint32_t)(now_ms - u->since_ms) >= LINK_UPH_HASH_MS) {
            done(u, LINK_UPH_NO_ANSWER);
        }
        return;
    }
    for (size_t i = 0; i < LINK_SHA256_BYTES / 2u; ++i) {
        const uint16_t v = r.regs[LINK_UPD_DIGEST + i];
        u->digest[2u * i]      = (uint8_t)(v & 0xFFu);
        u->digest[2u * i + 1u] = (uint8_t)(v >> 8);
    }
    if (state == LINK_UPD_VERIFIED) {
        /* Its word and this end's own comparison, both. */
        if (r.regs[LINK_UPD_DIGEST_OF] != LINK_UPD_DIGEST_IMAGE
            || memcmp(u->digest, u->expect, sizeof(u->expect)) != 0) {
            done(u, LINK_UPH_BAD_DIGEST);
            return;
        }
        u->phase = LINK_UPH_ACTIVATE;
        u->since_ms = now_ms;
        return;
    }
    u->error = (uint8_t)r.regs[LINK_UPD_ERROR];
    if (state == LINK_UPD_FAILED && u->error == LINK_UPD_ERR_DIGEST) {
        done(u, LINK_UPH_BAD_DIGEST);
        return;
    }
    if (state == LINK_UPD_IDLE || state == LINK_UPD_FAILED) {
        start_over(u, now_ms);
        return;
    }
    done(u, LINK_UPH_REFUSED);
}

static void step_activate(link_uph_t *u, link_host_t *h,
                          const link_port_t *port, uint32_t now_ms)
{
    link_msg_t r;
    const x_t x = command(h, port, LINK_UPD_CMD_ACTIVATE, 0u, 0u, &r);
    if (x == X_NACK) {
        if (r.regs[0] == LINK_NACK_BUSY) {
            (void)wait_gate(u, now_ms);
            return;
        }
        refused(u, h, port, &r);
        return;
    }
    /* Taken, or unanswered: an ACTIVATE that went in restarts the
     * IOmcu 20 ms after it, and its acknowledgement may be what was
     * lost.  The restart says which. */
    heard(u);
    u->busy = false;
    u->block = u->blocks;
    u->phase = LINK_UPH_RESTARTING;
    u->since_ms = now_ms;
}

static void step_restarting(link_uph_t *u, link_host_t *h,
                            const link_port_t *port, uint32_t now_ms)
{
    const uint32_t waited = (uint32_t)(now_ms - u->since_ms);
    if (waited < LINK_UPH_SETTLE_MS) {
        return;
    }
    if (waited >= LINK_UPH_CONFIRM_MS) {
        done(u, LINK_UPH_NOT_STARTED);
        return;
    }
    /* Who answers: the identity first, as for any IOmcu. */
    link_msg_t r;
    if (read_regs(h, port, LINK_PAGE_IDENTITY, 0u, LINK_ID_COUNT, &r)
        != X_OK) {
        return;   /* restarting; asked again next step */
    }
    memcpy(u->ident, r.regs, sizeof(u->ident));
    u->have_ident = true;
    if (read_regs(h, port, LINK_PAGE_UPDATE_CTL, 0u, LINK_UPD_SLOT + 1u, &r)
        != X_OK) {
        return;
    }
    const uint16_t slot = r.regs[LINK_UPD_SLOT];
    const bool moved = (slot & LINK_UPD_SLOT_MASK)
                       != (u->slot & LINK_UPD_SLOT_MASK);
    if (!moved) {
        /* The old slot, still or again: not restarted yet, or the boot
         * ROM would not start the new image.  The clock decides. */
        return;
    }
    if ((slot & LINK_UPD_SLOT_TRIAL) != 0u) {
        u->phase = LINK_UPH_BUY;
        return;
    }
    done(u, LINK_UPH_UPDATED);   /* bought already */
}

static void step_buy(link_uph_t *u, link_host_t *h, const link_port_t *port)
{
    link_msg_t r;
    const x_t x = command(h, port, LINK_UPD_CMD_BUY, 0u, 0u, &r);
    if (x == X_NONE) {
        (void)missed(u, port);
        return;
    }
    heard(u);
    if (x == X_NACK) {
        refused(u, h, port, &r);
        return;
    }
    const x_t y = read_regs(h, port, LINK_PAGE_UPDATE_CTL, 0u,
                            LINK_UPD_SLOT + 1u, &r);
    if (y == X_NONE) {
        (void)missed(u, port);
        return;
    }
    if (y == X_OK && (r.regs[LINK_UPD_SLOT] & LINK_UPD_SLOT_TRIAL) == 0u) {
        done(u, LINK_UPH_UPDATED);
        return;
    }
    u->error = (y == X_OK) ? (uint8_t)r.regs[LINK_UPD_ERROR] : 0u;
    done(u, LINK_UPH_REFUSED);
}

link_uph_phase_t link_uph_step(link_uph_t *u, link_host_t *h,
                               const link_port_t *port)
{
    if (u == NULL) {
        return LINK_UPH_IDLE;
    }
    if (h == NULL || port == NULL || port->now_ms == NULL) {
        return u->phase;
    }
    const uint32_t now_ms = port->now_ms(port->ctx);
    switch (u->phase) {
    case LINK_UPH_CHECK_HASH:  step_check_hash(u);                  break;
    case LINK_UPH_CHECK_ASK:   step_check_ask(u, h, port, now_ms);  break;
    case LINK_UPH_CHECK_WAIT:  step_check_wait(u, h, port, now_ms); break;
    case LINK_UPH_PREPARE:     step_prepare(u, h, port, now_ms);    break;
    case LINK_UPH_BEGIN:       step_begin(u, h, port, now_ms);      break;
    case LINK_UPH_SEND:        step_send(u, h, port, now_ms);       break;
    case LINK_UPH_RESYNC:      step_resync(u, h, port, now_ms);     break;
    case LINK_UPH_FINISH:      step_finish(u, h, port, now_ms);     break;
    case LINK_UPH_VERIFY:      step_verify(u, h, port, now_ms);     break;
    case LINK_UPH_ACTIVATE:    step_activate(u, h, port, now_ms);   break;
    case LINK_UPH_RESTARTING:  step_restarting(u, h, port, now_ms); break;
    case LINK_UPH_BUY:         step_buy(u, h, port);                break;
    case LINK_UPH_IDLE:
    case LINK_UPH_DONE:
    default:
        break;
    }
    return u->phase;
}
