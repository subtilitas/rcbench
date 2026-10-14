/*
 * The coprocessor's half of the firmware update.  See link_update_dev.h and
 * the update section of link_pages.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "link_update_dev.h"

#include <stddef.h>
#include <string.h>

#include "link_crc.h"
#include "link_msg.h"

void link_update_dev_init(link_update_dev_t *u, const link_update_io_t *io,
                          const uint8_t *running, const uint8_t *target,
                          uint32_t slot_bytes, uint16_t slot)
{
    if (u == NULL) {
        return;
    }
    memset(u, 0, sizeof(*u));
    u->io = io;
    u->running = running;
    u->target = target;
    u->slot = slot;
    /* A slot this end cannot read or write is no slot. */
    const bool usable = (slot & LINK_UPD_SLOT_MASK) != LINK_UPD_SLOT_NONE
                        && running != NULL && target != NULL && io != NULL
                        && slot_bytes >= LINK_UPD_SECTOR_BYTES
                        && slot_bytes % LINK_UPD_SECTOR_BYTES == 0u
                        && slot_bytes / LINK_UPD_BLOCK_BYTES <= 0xFFFFu;
    if (usable) {
        u->slot_bytes = slot_bytes;
    } else {
        u->slot = (uint16_t)((slot & (uint16_t)~LINK_UPD_SLOT_MASK)
                             | LINK_UPD_SLOT_NONE);
    }
    u->state = LINK_UPD_IDLE;
}

static bool in_session(uint8_t state)
{
    return state == LINK_UPD_RECEIVING || state == LINK_UPD_RECEIVED
           || state == LINK_UPD_VERIFYING || state == LINK_UPD_VERIFIED;
}

static bool gate_open(const link_update_dev_t *u)
{
    return u->io->gate == NULL || u->io->gate(u->io->ctx);
}

/* A refusal: the reason kept for LINK_UPD_ERROR, the code for the NACK. */
static uint8_t refuse(link_update_dev_t *u, uint8_t err, uint8_t nack)
{
    u->error = err;
    return nack;
}

/* A failure that ends the session. */
static uint8_t fail(link_update_dev_t *u, uint8_t err)
{
    u->state = LINK_UPD_FAILED;
    u->error = err;
    u->restart_due = false;
    return LINK_NACK_BAD_VALUE;
}

static uint32_t arg32(const uint16_t *a)
{
    return (uint32_t)a[1] | ((uint32_t)a[2] << 16);
}

static uint8_t cmd_hash_running(link_update_dev_t *u, const uint16_t *a)
{
    if (u->slot_bytes == 0u) {
        return refuse(u, LINK_UPD_ERR_NO_SLOTS, LINK_NACK_BAD_VALUE);
    }
    if (in_session(u->state) || u->state == LINK_UPD_ACTIVATED) {
        return refuse(u, LINK_UPD_ERR_STATE, LINK_NACK_BAD_VALUE);
    }
    const uint32_t len = arg32(a);
    if (len == 0u || len > u->slot_bytes) {
        return refuse(u, LINK_UPD_ERR_SIZE, LINK_NACK_BAD_VALUE);
    }
    u->size = len;
    u->digest_of = LINK_UPD_DIGEST_NONE;
    link_image_hash_begin(&u->hash, len,
                          link_update_tbyb_byte(u->running, len), false);
    u->state = LINK_UPD_HASHING;
    u->error = LINK_UPD_ERR_NONE;
    return 0u;
}

static uint8_t cmd_begin(link_update_dev_t *u, const uint16_t *a)
{
    if (u->slot_bytes == 0u) {
        return refuse(u, LINK_UPD_ERR_NO_SLOTS, LINK_NACK_BAD_VALUE);
    }
    if ((u->slot & LINK_UPD_SLOT_TRIAL) != 0u) {
        /* The boot ROM keeps one image on trial at a time. */
        return refuse(u, LINK_UPD_ERR_TRIAL, LINK_NACK_BAD_VALUE);
    }
    if (u->state == LINK_UPD_ACTIVATED) {
        return refuse(u, LINK_UPD_ERR_STATE, LINK_NACK_BAD_VALUE);
    }
    const uint32_t size = arg32(a);
    if (size == 0u || size > u->slot_bytes) {
        return refuse(u, LINK_UPD_ERR_SIZE, LINK_NACK_BAD_VALUE);
    }
    if (!gate_open(u)) {
        return refuse(u, LINK_UPD_ERR_GATE, LINK_NACK_BUSY);
    }
    /* The first sector goes first, so from here the slot holds no image
     * the boot ROM would start until ACTIVATE writes it back. */
    if (!u->io->erase(u->io->ctx, 0u)) {
        return fail(u, LINK_UPD_ERR_FLASH);
    }
    u->size = size;
    u->blocks = (uint16_t)((size + LINK_UPD_BLOCK_BYTES - 1u)
                           / LINK_UPD_BLOCK_BYTES);
    u->next = 0u;
    u->digest_of = LINK_UPD_DIGEST_NONE;
    memset(u->first, 0xFF, sizeof(u->first));
    memset(u->sector, 0xFF, sizeof(u->sector));
    u->state = LINK_UPD_RECEIVING;
    u->error = LINK_UPD_ERR_NONE;
    return 0u;
}

/* The block staged in UPDATE_DATA as bytes, low byte of each register
 * first. */
static void staged_bytes(const link_update_dev_t *u, uint8_t *out)
{
    for (size_t i = 0; i < LINK_UPD_DATA_COUNT; ++i) {
        out[2u * i]      = (uint8_t)(u->data[i] & 0xFFu);
        out[2u * i + 1u] = (uint8_t)(u->data[i] >> 8);
    }
}

static uint8_t cmd_commit(link_update_dev_t *u, const uint16_t *a)
{
    const uint16_t block = a[1];
    const uint16_t crc   = a[2];

    /* The block before the next one, again: its acknowledgement was lost
     * on the way back, and it is in flash already. */
    if ((u->state == LINK_UPD_RECEIVING || u->state == LINK_UPD_RECEIVED)
        && u->next > 0u && block == (uint16_t)(u->next - 1u)) {
        return 0u;
    }
    if (u->state != LINK_UPD_RECEIVING) {
        return refuse(u, LINK_UPD_ERR_STATE, LINK_NACK_BAD_VALUE);
    }
    if (block != u->next) {
        return refuse(u, LINK_UPD_ERR_ORDER, LINK_NACK_BAD_VALUE);
    }

    const uint32_t off = (uint32_t)block * LINK_UPD_BLOCK_BYTES;
    uint32_t n = u->size - off;
    if (n > LINK_UPD_BLOCK_BYTES) {
        n = LINK_UPD_BLOCK_BYTES;
    }
    uint8_t bytes[LINK_UPD_BLOCK_BYTES];
    staged_bytes(u, bytes);
    if (link_crc(LINK_CRC_INIT, bytes, n) != crc) {
        return refuse(u, LINK_UPD_ERR_CRC, LINK_NACK_BAD_VALUE);
    }

    const bool last = (block + 1u == u->blocks);
    const uint32_t in_sector = off % LINK_UPD_SECTOR_BYTES;
    const uint32_t sector_off = off - in_sector;
    const bool fills = last || in_sector + n == LINK_UPD_SECTOR_BYTES;
    /* The first sector waits in RAM for ACTIVATE; any other is written the
     * moment it is whole, so only a write needs the gate. */
    const bool writes = fills && sector_off != 0u;
    if (writes && !gate_open(u)) {
        return refuse(u, LINK_UPD_ERR_GATE, LINK_NACK_BUSY);
    }

    uint8_t *buf = (sector_off == 0u) ? u->first : u->sector;
    memcpy(&buf[in_sector], bytes, n);
    if (writes) {
        if (!u->io->erase(u->io->ctx, sector_off)
            || !u->io->program(u->io->ctx, sector_off, u->sector)) {
            return fail(u, LINK_UPD_ERR_FLASH);
        }
        memset(u->sector, 0xFF, sizeof(u->sector));
    }
    u->next = (uint16_t)(block + 1u);
    if (last) {
        u->state = LINK_UPD_RECEIVED;
    }
    u->error = LINK_UPD_ERR_NONE;
    return 0u;
}

static uint8_t cmd_finish(link_update_dev_t *u)
{
    if (u->state != LINK_UPD_RECEIVED) {
        return refuse(u, LINK_UPD_ERR_STATE, LINK_NACK_BAD_VALUE);
    }
    for (size_t i = 0; i < LINK_UPD_EXPECT_REGS; ++i) {
        u->expect[2u * i]      = (uint8_t)(u->data[i] & 0xFFu);
        u->expect[2u * i + 1u] = (uint8_t)(u->data[i] >> 8);
    }
    /* The bytes as written: the TBYB flag as the panel sent it. */
    link_image_hash_begin(&u->hash, u->size, -1, false);
    u->digest_of = LINK_UPD_DIGEST_NONE;
    u->state = LINK_UPD_VERIFYING;
    u->error = LINK_UPD_ERR_NONE;
    return 0u;
}

static uint8_t cmd_activate(link_update_dev_t *u)
{
    if (u->state != LINK_UPD_VERIFIED) {
        return refuse(u, LINK_UPD_ERR_STATE, LINK_NACK_BAD_VALUE);
    }
    if (!gate_open(u)) {
        return refuse(u, LINK_UPD_ERR_GATE, LINK_NACK_BUSY);
    }
    /* Erased at BEGIN and not touched since. */
    if (!u->io->program(u->io->ctx, 0u, u->first)
        || memcmp(u->target, u->first, sizeof(u->first)) != 0) {
        return fail(u, LINK_UPD_ERR_FLASH);
    }
    u->state = LINK_UPD_ACTIVATED;
    u->error = LINK_UPD_ERR_NONE;
    u->restart_due = true;
    u->restart_timed = false;
    return 0u;
}

static uint8_t cmd_abort(link_update_dev_t *u)
{
    if (u->state == LINK_UPD_ACTIVATED) {
        return refuse(u, LINK_UPD_ERR_STATE, LINK_NACK_BAD_VALUE);
    }
    u->state = LINK_UPD_IDLE;
    u->error = LINK_UPD_ERR_NONE;
    return 0u;
}

static uint8_t cmd_buy(link_update_dev_t *u)
{
    if ((u->slot & LINK_UPD_SLOT_TRIAL) == 0u) {
        return 0u;   /* bought already, or never on trial */
    }
    if (u->io == NULL || u->io->buy == NULL || !u->io->buy(u->io->ctx)) {
        return refuse(u, LINK_UPD_ERR_BUY, LINK_NACK_BAD_VALUE);
    }
    u->slot = (uint16_t)(u->slot & (uint16_t)~LINK_UPD_SLOT_TRIAL);
    u->error = LINK_UPD_ERR_NONE;
    return 0u;
}

void link_update_dev_read_ctl(link_update_dev_t *u, uint8_t offset,
                              uint8_t count, uint16_t *out)
{
    if (u == NULL || out == NULL) {
        return;
    }
    uint16_t r[LINK_UPD_COUNT];
    memset(r, 0, sizeof(r));
    r[LINK_UPD_VERSION]      = LINK_UPD_PROTOCOL;
    r[LINK_UPD_STATE]        = u->state;
    r[LINK_UPD_ERROR]        = u->error;
    r[LINK_UPD_SLOT]         = u->slot;
    r[LINK_UPD_SLOT_SECTORS] = (uint16_t)(u->slot_bytes
                                          / LINK_UPD_SECTOR_BYTES);
    r[LINK_UPD_NEXT_BLOCK]   = u->next;
    r[LINK_UPD_SIZE_LO]      = (uint16_t)(u->size & 0xFFFFu);
    r[LINK_UPD_SIZE_HI]      = (uint16_t)(u->size >> 16);
    memcpy(&r[LINK_UPD_CMD], u->regs_cmd, sizeof(u->regs_cmd));
    r[LINK_UPD_DIGEST_OF]    = u->digest_of;
    if (u->digest_of != LINK_UPD_DIGEST_NONE) {
        for (size_t i = 0; i < LINK_SHA256_BYTES / 2u; ++i) {
            r[LINK_UPD_DIGEST + i] =
                (uint16_t)(u->digest[2u * i]
                           | ((uint16_t)u->digest[2u * i + 1u] << 8));
        }
    }
    for (uint32_t i = 0; i < count && offset + i < LINK_UPD_COUNT; ++i) {
        out[i] = r[offset + i];
    }
}

uint8_t link_update_dev_write_ctl(link_update_dev_t *u, uint8_t offset,
                                  uint8_t count, const uint16_t *in)
{
    if (u == NULL || in == NULL) {
        return LINK_NACK_BAD_VALUE;
    }
    /* One command, one frame: CMD first and its arguments after it. */
    if (offset != LINK_UPD_CMD || count == 0u || count > 4u) {
        return LINK_NACK_READ_ONLY;
    }
    uint16_t a[4] = { 0u, 0u, 0u, 0u };
    memcpy(a, in, (size_t)count * sizeof(in[0]));
    memcpy(u->regs_cmd, a, sizeof(a));

    switch (a[0]) {
    case LINK_UPD_CMD_HASH_RUNNING: return cmd_hash_running(u, a);
    case LINK_UPD_CMD_BEGIN:        return cmd_begin(u, a);
    case LINK_UPD_CMD_COMMIT:       return cmd_commit(u, a);
    case LINK_UPD_CMD_FINISH:       return cmd_finish(u);
    case LINK_UPD_CMD_ACTIVATE:     return cmd_activate(u);
    case LINK_UPD_CMD_ABORT:        return cmd_abort(u);
    case LINK_UPD_CMD_BUY:          return cmd_buy(u);
    default:
        return refuse(u, LINK_UPD_ERR_COMMAND, LINK_NACK_BAD_VALUE);
    }
}

void link_update_dev_read_data(link_update_dev_t *u, uint8_t offset,
                               uint8_t count, uint16_t *out)
{
    if (u == NULL || out == NULL) {
        return;
    }
    for (uint32_t i = 0; i < count && offset + i < LINK_UPD_DATA_COUNT; ++i) {
        out[i] = u->data[offset + i];
    }
}

uint8_t link_update_dev_write_data(link_update_dev_t *u, uint8_t offset,
                                   uint8_t count, const uint16_t *in)
{
    if (u == NULL || in == NULL
        || (uint32_t)offset + count > LINK_UPD_DATA_COUNT) {
        return LINK_NACK_BAD_RANGE;
    }
    memcpy(&u->data[offset], in, (size_t)count * sizeof(in[0]));
    return 0u;
}

/* Where the byte at @p pos of the image being checked is read from: the
 * first sector from RAM, which ACTIVATE has not written yet. */
static const uint8_t *verify_src(const link_update_dev_t *u, uint32_t pos)
{
    return (pos < LINK_UPD_SECTOR_BYTES) ? &u->first[pos] : &u->target[pos];
}

void link_update_dev_poll(link_update_dev_t *u, uint32_t now_ms,
                          uint32_t heard_ms)
{
    if (u == NULL) {
        return;
    }
    if (in_session(u->state)
        && (uint32_t)(now_ms - heard_ms) >= LINK_UPD_SILENCE_MS) {
        (void)fail(u, LINK_UPD_ERR_LINK);
        return;
    }
    if (u->state == LINK_UPD_HASHING || u->state == LINK_UPD_VERIFYING) {
        const uint32_t pos = u->hash.pos;
        /* A step never crosses a sector, so it reads from one place. */
        uint32_t n = LINK_UPD_SECTOR_BYTES - pos % LINK_UPD_SECTOR_BYTES;
        if (n > LINK_UPD_HASH_STEP) {
            n = LINK_UPD_HASH_STEP;
        }
        const uint8_t *src = (u->state == LINK_UPD_HASHING)
                             ? &u->running[pos] : verify_src(u, pos);
        if (link_image_hash_step(&u->hash, src, n)) {
            link_image_hash_end(&u->hash, u->digest);
            if (u->state == LINK_UPD_HASHING) {
                u->digest_of = LINK_UPD_DIGEST_RUNNING;
                u->state = LINK_UPD_IDLE;
            } else {
                u->digest_of = LINK_UPD_DIGEST_IMAGE;
                if (memcmp(u->digest, u->expect, sizeof(u->digest)) == 0) {
                    u->state = LINK_UPD_VERIFIED;
                } else {
                    (void)fail(u, LINK_UPD_ERR_DIGEST);
                }
            }
        }
    }
    if (u->restart_due) {
        if (!u->restart_timed) {
            u->restart_timed = true;
            u->restart_at_ms = now_ms;
        } else if ((uint32_t)(now_ms - u->restart_at_ms)
                   >= LINK_UPD_RESTART_MS) {
            u->restart_due = false;
            if (u->io != NULL && u->io->restart != NULL) {
                u->io->restart(u->io->ctx);
            }
        }
    }
}

bool link_update_dev_holds_arm(const link_update_dev_t *u)
{
    if (u == NULL) {
        return false;
    }
    return in_session(u->state) || u->state == LINK_UPD_ACTIVATED
           || (u->slot & LINK_UPD_SLOT_TRIAL) != 0u;
}

bool link_update_dev_refuses_arm(const link_update_dev_t *u, uint8_t off,
                                 uint8_t n, const uint16_t *in)
{
    if (u == NULL || in == NULL || !link_update_dev_holds_arm(u)) {
        return false;
    }
    /* ARM is the page's first register, so only a frame from register 0
     * carries it. */
    _Static_assert(LINK_CT_ARM == 0, "ARM is not CONTROL's first register");
    if (off != LINK_CT_ARM || n == 0u) {
        return false;   /* the frame does not carry ARM */
    }
    return in[0] != 0u;
}
