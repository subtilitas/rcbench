/*
 * The coprocessor firmware update, both ends over the real link layer.
 *
 * The panel's engine (link_update_host) talks through a link_port_t to a
 * simulated coprocessor: the real link_dev dispatch and link_update_dev
 * behind CAN (Controller Area Network) frames encoded and decoded by
 * link_can, a NOR flash of two slots, and a model of the boot ROM's A/B
 * choice with its TBYB (try before you buy) watchdog.  Frames can be lost,
 * corrupted or met by a coprocessor that restarts, and the flash can fail.
 *
 * What is under test is what a bench sees afterwards: which image runs,
 * whether it is bought, and that no failure leaves a slot the boot ROM
 * would start half-written.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdint.h>
#include <string.h>

#include "greatest.h"

#include "link_can.h"
#include "link_crc.h"
#include "link_dev.h"
#include "link_host.h"
#include "link_pages.h"
#include "link_port.h"
#include "link_sha256.h"
#include "link_update_dev.h"
#include "link_update_host.h"
#include "link_update_image.h"

/* ------------------------------------------------------------- the images */

#define SECTOR        LINK_UPD_SECTOR_BYTES
/* 16 sectors: big enough for every boundary, small enough to run fast. */
#define SLOT_SMALL    (16u * SECTOR)
/* The slot of firmware/iomcu/partitions.json: 2036 KiB. */
#define SLOT_FULL     (2036u * 1024u)
/* The v0.18.0 coprocessor image, in bytes. */
#define IMAGE_V018    184872u

#define MARKER_START  0xffffded3u
#define MARKER_END    0xab123579u
/* A picobin block of one IMAGE_TYPE item: start, item, LAST, link, end. */
#define BLOCK_BYTES   20u
/* Where make_image() keeps the image's version: 3 words of 16 bits. */
#define VERSION_AT    BLOCK_BYTES

static void put32(uint8_t *p, uint32_t at, uint32_t v)
{
    p[at]      = (uint8_t)v;
    p[at + 1u] = (uint8_t)(v >> 8);
    p[at + 2u] = (uint8_t)(v >> 16);
    p[at + 3u] = (uint8_t)(v >> 24);
}

/* A block at @p at holding an IMAGE_DEF (an IMAGE_TYPE item first) whose
 * flags are @p flags, linking to the block @p link bytes away. */
static void put_block(uint8_t *p, uint32_t at, uint16_t flags, int32_t link)
{
    put32(p, at, MARKER_START);
    p[at + 4u] = 0x42u;                 /* IMAGE_TYPE, 1 word */
    p[at + 5u] = 0x01u;
    p[at + 6u] = (uint8_t)flags;
    p[at + 7u] = (uint8_t)(flags >> 8);
    put32(p, at + 8u, 0x000001ffu);     /* LAST: 1 word before it */
    put32(p, at + 12u, (uint32_t)link);
    put32(p, at + 16u, MARKER_END);
}

/* The flags of an RP2350 Arm secure executable, TBYB clear. */
#define FLAGS_EXE 0x1021u

/*
 * An image of @p len bytes with bytes from @p seed, a block loop and the
 * version @p maj.@p min.@p patch.  With @p len of 64 bytes or more the loop
 * has two blocks, the second at the end of the image as the build's seal
 * puts it, and that second one is the IMAGE_DEF the boot ROM uses.
 */
static void make_image(uint8_t *img, uint32_t len, uint32_t seed,
                       uint16_t maj, uint16_t min, uint16_t patch)
{
    uint32_t x = seed * 2654435761u + 1u;
    for (uint32_t i = 0; i < len; ++i) {
        x = x * 1103515245u + 12345u;
        img[i] = (uint8_t)(x >> 16);
    }
    const uint32_t second = (len - BLOCK_BYTES) & ~3u;
    if (len >= 64u) {
        put_block(img, 0u, FLAGS_EXE, (int32_t)second);
        put_block(img, second, FLAGS_EXE, -(int32_t)second);
    } else {
        put_block(img, 0u, FLAGS_EXE, 0);
    }
    const uint16_t v[3] = { maj, min, patch };
    for (unsigned i = 0; i < 3u; ++i) {
        img[VERSION_AT + 2u * i]      = (uint8_t)v[i];
        img[VERSION_AT + 2u * i + 1u] = (uint8_t)(v[i] >> 8);
    }
}

static uint16_t version_word(const uint8_t *img, unsigned i)
{
    return (uint16_t)(img[VERSION_AT + 2u * i]
                      | (img[VERSION_AT + 2u * i + 1u] << 8));
}

/* ----------------------------------------------------- the clock and flash */

static uint64_t g_us;

static uint32_t now_ms(void)
{
    return (uint32_t)(g_us / 1000u);
}

static uint8_t g_flash[2][SLOT_FULL];

/* -------------------------------------------------------- the coprocessor */

/* RP2350 boot ROM: the TBYB watchdog, 16.7 s (datasheet 5.1.17). */
#define TBYB_MS       16700u
/* From a restart to the first answer.  Not measured on the bench. */
#define BOOT_MS       300u
/* One erase and program of a 4 KiB sector, as measured: 19.2 ms. */
#define SECTOR_US     19180u
/* One CAN frame at 1 Mbit/s, at most. */
#define FRAME_US      160u

typedef enum { BOOT_NORMAL, BOOT_FLASH_UPDATE } boot_kind_t;

static struct {
    /* What the boot ROM knows. */
    uint32_t slot_bytes;
    unsigned running;       /* the slot that runs                        */
    unsigned bought;        /* the slot a normal boot starts             */
    bool     trial;         /* running on trial: no BUY yet              */
    uint32_t trial_ms;
    bool     up;            /* answering                                 */
    uint64_t boot_at_us;
    boot_kind_t boot_kind;
    unsigned boot_slot;
    unsigned boots;
    bool     no_table;      /* booted without the partition table        */

    /* What the firmware is. */
    bool     knows_update;  /* serves UPDATE_CTL and UPDATE_DATA         */
    uint16_t proto_major, proto_minor;
    bool     new_image_dead; /* the image sent starts and never answers  */

    /* The firmware's state. */
    link_dev_t        dev;
    link_update_dev_t upd;
    link_update_io_t  io;
    uint16_t control[LINK_CT_COUNT];
    unsigned arm_refused;

    /* Faults. */
    bool     gate_shut;
    uint64_t gate_shut_until_us;
    int      erase_fails_at;     /* sector offset; -1 none               */
    int      program_flips_at;   /* sector offset whose program loses a bit */
    bool     buy_fails;
    uint64_t no_poll_until_us;   /* the main loop stalls: no hash slices  */
    unsigned programs, erases;
    uint32_t sector0_programs;   /* programs of the target's first sector */
} cp;

/* The bus. */
static struct {
    link_host_t host;
    unsigned exchanges;
    unsigned request_frames;
    unsigned data_writes;        /* UPDATE_DATA write frames              */
    unsigned commits;
    long     drop_request;       /* the n-th request frame from now: lost */
    long     drop_reply;         /* the n-th reply frame from now: lost   */
    unsigned corrupt_block_left; /* corrupt this many sends of a block    */
    uint16_t corrupt_block;
    bool     dead;               /* nothing gets through                  */
    uint64_t dead_until_us;
} bus;

static void cp_boot_now(void);

/* One pass of the coprocessor's main loop. */
static void cp_pass(void)
{
    if (!cp.up) {
        if (g_us >= cp.boot_at_us) {
            cp_boot_now();
        }
        return;
    }
    if (g_us >= cp.no_poll_until_us) {
        link_update_dev_poll(&cp.upd, now_ms(), cp.dev.last_request_ms);
    }
    if (!cp.up) {
        return;   /* the poll restarted it */
    }
    (void)link_dev_tick(&cp.dev, now_ms());
    if (cp.trial && (uint32_t)(now_ms() - cp.trial_ms) >= TBYB_MS) {
        /* The boot ROM's watchdog: a normal boot of the image bought. */
        cp.up = false;
        cp.boot_kind = BOOT_NORMAL;
        cp.boot_at_us = g_us + (uint64_t)BOOT_MS * 1000u;
    }
}

static void advance_us(uint64_t us)
{
    while (us > 0u) {
        const uint64_t d = (us > 1000u) ? 1000u : us;
        g_us += d;
        us -= d;
        cp_pass();
    }
}

/* ---- flash and boot ROM, as the firmware's callbacks see them */

static uint8_t *target_slot(void)
{
    return g_flash[1u - cp.running];
}

static bool io_erase(void *ctx, uint32_t off)
{
    (void)ctx;
    ++cp.erases;
    if (cp.erase_fails_at >= 0 && off == (uint32_t)cp.erase_fails_at) {
        return false;
    }
    memset(&target_slot()[off], 0xFF, SECTOR);
    g_us += SECTOR_US;   /* the loop is parked for the write */
    return true;
}

static bool io_program(void *ctx, uint32_t off, const uint8_t *data)
{
    (void)ctx;
    ++cp.programs;
    if (off == 0u) {
        ++cp.sector0_programs;
    }
    uint8_t *dst = &target_slot()[off];
    for (uint32_t i = 0; i < SECTOR; ++i) {
        dst[i] &= data[i];   /* NOR: a program only clears bits */
    }
    if (cp.program_flips_at >= 0 && off == (uint32_t)cp.program_flips_at) {
        dst[100] ^= 0x01u;   /* a bit lost and not reported */
    }
    return true;
}

static bool io_gate(void *ctx)
{
    (void)ctx;
    return !cp.gate_shut && g_us >= cp.gate_shut_until_us;
}

static bool image_valid(const uint8_t *slot)
{
    return link_update_tbyb_byte(slot, cp.slot_bytes) >= 0;
}

static bool io_buy(void *ctx)
{
    (void)ctx;
    if (cp.buy_fails) {
        return false;
    }
    /* The boot ROM clears the flag in flash -- a program, NOR clears it --
     * and the other image is erased so a normal boot finds this one. */
    uint8_t *slot = g_flash[cp.running];
    const int32_t at = link_update_tbyb_byte(slot, cp.slot_bytes);
    if (at >= 0) {
        slot[at] &= (uint8_t)~LINK_IMAGE_TBYB_BIT;
    }
    memset(g_flash[1u - cp.running], 0xFF, SECTOR);
    cp.bought = cp.running;
    cp.trial = false;
    return true;
}

static void io_restart(void *ctx)
{
    (void)ctx;
    cp.up = false;
    cp.boot_kind = BOOT_FLASH_UPDATE;
    cp.boot_slot = 1u - cp.running;
    cp.boot_at_us = g_us + (uint64_t)BOOT_MS * 1000u;
}

/* ---- the pages */

static void rd_identity(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    const uint8_t *img = g_flash[cp.running];
    const uint16_t r[LINK_ID_COUNT] = {
        cp.proto_major, cp.proto_minor,
        version_word(img, 0u), version_word(img, 1u), version_word(img, 2u),
        1u, 0u,
    };
    for (uint8_t i = 0; i < n; ++i) {
        out[i] = r[off + i];
    }
}

static void rd_ctl(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    link_update_dev_read_ctl(&cp.upd, off, n, out);
}

static uint8_t wr_ctl(void *ctx, uint8_t off, uint8_t n, const uint16_t *in)
{
    (void)ctx;
    return link_update_dev_write_ctl(&cp.upd, off, n, in);
}

static void rd_data(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    link_update_dev_read_data(&cp.upd, off, n, out);
}

static uint8_t wr_data(void *ctx, uint8_t off, uint8_t n, const uint16_t *in)
{
    (void)ctx;
    return link_update_dev_write_data(&cp.upd, off, n, in);
}

static void rd_control(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    memcpy(out, &cp.control[off], (size_t)n * sizeof(out[0]));
}

/* The control page as the firmware has it: the update's refusal first. */
static uint8_t wr_control(void *ctx, uint8_t off, uint8_t n,
                          const uint16_t *in)
{
    (void)ctx;
    if (link_update_dev_refuses_arm(&cp.upd, off, n, in)) {
        ++cp.arm_refused;
        return LINK_NACK_NOT_ARMED;
    }
    memcpy(&cp.control[off], in, (size_t)n * sizeof(in[0]));
    return 0u;
}

static const link_page_t k_pages_new[] = {
    { LINK_PAGE_IDENTITY, LINK_ID_COUNT, rd_identity, NULL },
    { LINK_PAGE_CONTROL, LINK_CT_COUNT, rd_control, wr_control },
    { LINK_PAGE_UPDATE_CTL, LINK_UPD_COUNT, rd_ctl, wr_ctl },
    { LINK_PAGE_UPDATE_DATA, LINK_UPD_DATA_COUNT, rd_data, wr_data },
};
/* A coprocessor of 4.21 or older: no update pages. */
static const link_page_t k_pages_old[] = {
    { LINK_PAGE_IDENTITY, LINK_ID_COUNT, rd_identity, NULL },
    { LINK_PAGE_CONTROL, LINK_CT_COUNT, rd_control, wr_control },
};

/* The boot ROM chooses, the image starts. */
static void cp_boot_now(void)
{
    ++cp.boots;
    cp.trial = false;
    if (cp.boot_kind == BOOT_FLASH_UPDATE
        && image_valid(g_flash[cp.boot_slot])) {
        cp.running = cp.boot_slot;
        const uint8_t *img = g_flash[cp.running];
        const int32_t at = link_update_tbyb_byte(img, cp.slot_bytes);
        if ((img[at] & LINK_IMAGE_TBYB_BIT) != 0u) {
            cp.trial = true;
            cp.trial_ms = now_ms();
        } else {
            /* Not on trial: bought as it starts, the other one erased. */
            cp.bought = cp.running;
            memset(g_flash[1u - cp.running], 0xFF, SECTOR);
        }
    } else if (image_valid(g_flash[cp.bought])) {
        cp.running = cp.bought;
    } else if (image_valid(g_flash[1u - cp.bought])) {
        cp.running = 1u - cp.bought;
        cp.bought = cp.running;
    } else {
        cp.up = false;
        cp.boot_at_us = UINT64_MAX;   /* nothing to start: BOOTSEL only */
        return;
    }
    if (cp.trial && cp.new_image_dead) {
        cp.up = false;                /* started and never answering */
        cp.boot_at_us = UINT64_MAX;
        cp.trial_ms = now_ms();
        /* The watchdog still runs in the boot ROM's name. */
        return;
    }
    cp.up = true;
    cp.io = (link_update_io_t){ io_erase, io_program, io_gate, io_buy,
                                io_restart, NULL };
    uint16_t slot = cp.no_table ? LINK_UPD_SLOT_NONE
                                : (uint16_t)cp.running;
    if (cp.trial) {
        slot |= LINK_UPD_SLOT_TRIAL;
    }
    link_update_dev_init(&cp.upd, &cp.io, g_flash[cp.running],
                         g_flash[1u - cp.running], cp.slot_bytes, slot);
    if (cp.knows_update) {
        link_dev_init(&cp.dev, k_pages_new,
                      (uint8_t)(sizeof(k_pages_new) / sizeof(k_pages_new[0])),
                      NULL, now_ms());
    } else {
        link_dev_init(&cp.dev, k_pages_old,
                      (uint8_t)(sizeof(k_pages_old) / sizeof(k_pages_old[0])),
                      NULL, now_ms());
    }
    memset(cp.control, 0, sizeof(cp.control));
}

/* A dead coprocessor's watchdog: the boot ROM restarts it. */
static void dead_watchdog(void)
{
    if (!cp.up && cp.boot_at_us == UINT64_MAX && cp.trial
        && (uint32_t)(now_ms() - cp.trial_ms) >= TBYB_MS) {
        cp.boot_kind = BOOT_NORMAL;
        cp.boot_at_us = g_us;
    }
}

/* ---------------------------------------------------------------- the wire */

/*
 * One exchange of a kind, in one phase of the panel's engine, made to go
 * wrong: the request lost, the reply lost, a refusal in the coprocessor's
 * place, a register of the reply changed, the gate shut or the main loop
 * stalled from it on.
 */
enum { F_NONE = 0, F_LOSE, F_DROP_REPLY, F_NACK, F_EDIT, F_GATE, F_STALL };

typedef struct {
    int      action;
    uint8_t  phase;          /* the engine's phase the exchange is made in */
    uint8_t  page;
    uint8_t  op;             /* LINK_OP_READ or LINK_OP_WRITE              */
    int      cmd;            /* an UPDATE_CTL command; -1 any write        */
    unsigned skip;           /* matching exchanges let through first       */
    unsigned times;          /* then this many go wrong; 0 is 1            */
    uint16_t nack;           /* F_NACK                                     */
    bool     when;           /* F_EDIT: only a reply in state `state`      */
    uint16_t state;
    uint8_t  reg;            /* F_EDIT: the register and its new value     */
    uint16_t val;
    bool     two;            /* and a second one                           */
    uint8_t  reg2;
    uint16_t val2;
    uint32_t ms;             /* F_GATE, F_STALL, F_DROP_REPLY: how long
                              * the gate stays shut or the loop stalls     */
} fault_t;

static fault_t        g_fault;
static const link_uph_t *g_u;   /* the engine run() steps */

static bool fault_matches(const link_msg_t *req)
{
    if (g_fault.action == F_NONE || g_fault.times == 0u || g_u == NULL
        || g_u->phase != g_fault.phase || req->page != g_fault.page
        || req->op != g_fault.op) {
        return false;
    }
    if (g_fault.cmd >= 0
        && (req->page != LINK_PAGE_UPDATE_CTL || req->offset != LINK_UPD_CMD
            || req->regs[0] != (uint16_t)g_fault.cmd)) {
        return false;
    }
    if (g_fault.skip > 0u) {
        --g_fault.skip;
        return false;
    }
    return true;
}

static bool deliver_reply(const link_msg_t *ans, link_msg_t *reply)
{
    link_can_frame_t back[LINK_CAN_MAX_FRAMES];
    const size_t m = link_can_encode(ans, back, LINK_CAN_MAX_FRAMES);
    bool done = false;
    for (size_t k = 0; k < m; ++k) {
        g_us += FRAME_US;
        if (bus.drop_reply >= 0 && bus.drop_reply-- == 0) {
            continue;
        }
        link_msg_t part;
        if (link_can_decode(&back[k], &part)
            && link_host_accept(&bus.host, &part, now_ms(), reply)) {
            done = true;
        }
    }
    return done;
}

static bool port_exchange(void *ctx, link_host_t *h, const link_msg_t *req,
                          link_msg_t *reply)
{
    (void)ctx;
    link_can_frame_t out[LINK_CAN_MAX_FRAMES];
    const size_t n = link_can_encode(req, out, LINK_CAN_MAX_FRAMES);
    ++bus.exchanges;
    int action = F_NONE;
    if (fault_matches(req)) {
        action = g_fault.action;
        --g_fault.times;
        if (action == F_NACK) {
            link_msg_t ans;
            memset(&ans, 0, sizeof(ans));
            ans.op = LINK_OP_NACK;
            ans.page = req->page;
            ans.offset = req->offset;
            ans.count = 1u;
            ans.regs[0] = g_fault.nack;
            g_us += FRAME_US;
            if (deliver_reply(&ans, reply)) {
                return true;
            }
        } else if (action == F_GATE) {
            cp.gate_shut_until_us = g_us + (uint64_t)g_fault.ms * 1000u;
        } else if (action == F_STALL
                   || (action == F_DROP_REPLY && g_fault.ms > 0u)) {
            cp.no_poll_until_us = g_us + (uint64_t)g_fault.ms * 1000u;
        }
    }
    if (req->page == LINK_PAGE_UPDATE_DATA && req->op == LINK_OP_WRITE) {
        ++bus.data_writes;
    }
    if (req->page == LINK_PAGE_UPDATE_CTL && req->op == LINK_OP_WRITE
        && req->regs[0] == LINK_UPD_CMD_COMMIT) {
        ++bus.commits;
    }
    if (bus.dead && g_us >= bus.dead_until_us) {
        bus.dead = false;
    }
    bool answered = false;
    for (size_t i = 0; i < n; ++i) {
        ++bus.request_frames;
        g_us += FRAME_US;
        if (bus.dead || !cp.up || action == F_LOSE
            || (bus.drop_request >= 0 && bus.drop_request-- == 0)) {
            continue;
        }
        link_can_frame_t f = out[i];
        /* A bit flipped on the wire that the CAN CRC let through: the case
         * the block's own CRC-16 is there for. */
        if (bus.corrupt_block_left > 0u
            && req->page == LINK_PAGE_UPDATE_DATA && req->offset == 0u
            && req->op == LINK_OP_WRITE) {
            if (cp.upd.state == LINK_UPD_RECEIVING
                && cp.upd.next == bus.corrupt_block) {
                f.data[0] ^= 0x10u;
                --bus.corrupt_block_left;
            }
        }
        link_msg_t m, ans;
        if (!link_can_decode(&f, &m)) {
            continue;
        }
        if (link_dev_dispatch(&cp.dev, &m, &ans, now_ms())
            && action != F_DROP_REPLY && deliver_reply(&ans, reply)) {
            answered = true;
        }
        cp_pass();
    }
    if (answered && action == F_EDIT && reply->op == LINK_OP_DATA) {
        if (!g_fault.when
            || reply->regs[LINK_UPD_STATE] == g_fault.state) {
            reply->regs[g_fault.reg] = g_fault.val;
            if (g_fault.two) {
                reply->regs[g_fault.reg2] = g_fault.val2;
            }
        } else {
            ++g_fault.times;   /* not this reply: the next one */
        }
    }
    if (!answered) {
        /* The panel waits out its timeout, the coprocessor runs on. */
        link_host_abandon(h);
        advance_us((uint64_t)LINK_HOST_TIMEOUT_MS * 1000u);
        dead_watchdog();
        return false;
    }
    return true;
}

static uint32_t port_now(void *ctx)
{
    (void)ctx;
    return now_ms();
}

static const link_port_t k_port = { port_exchange, port_now, NULL, NULL };

/* ------------------------------------------------------------- the bench */

static uint8_t g_old[SLOT_FULL];   /* what the coprocessor runs */
static uint8_t g_new[SLOT_FULL];   /* what the panel carries     */

/* A coprocessor of @p slot_bytes slots, running @p old_len bytes of g_old
 * from slot A, bought; the clock at @p start_ms. */
static void bench(uint32_t slot_bytes, uint32_t old_len, uint32_t start_ms)
{
    memset(&cp, 0, sizeof(cp));
    memset(&bus, 0, sizeof(bus));
    g_us = (uint64_t)start_ms * 1000u;
    memset(g_flash, 0xFF, sizeof(g_flash));
    make_image(g_old, old_len, 1u, 18u, 0u, 0u);
    memcpy(g_flash[0], g_old, old_len);
    cp.slot_bytes = slot_bytes;
    cp.knows_update = true;
    cp.proto_major = LINK_PROTOCOL_MAJOR;
    cp.proto_minor = LINK_PROTOCOL_MINOR;
    cp.erase_fails_at = -1;
    cp.program_flips_at = -1;
    cp.running = 0u;
    cp.bought = 0u;
    cp.boot_kind = BOOT_NORMAL;
    cp_boot_now();
    bus.drop_request = -1;
    bus.drop_reply = -1;
    link_host_init(&bus.host, now_ms());
    memset(&g_fault, 0, sizeof(g_fault));
    g_u = NULL;
}

typedef void (*hook_t)(const link_uph_t *u);

/* Step until done; @p hook sees the engine after every step. */
static uint8_t run(link_uph_t *u, hook_t hook)
{
    g_u = u;
    for (unsigned i = 0; i < 400000u; ++i) {
        if (link_uph_step(u, &bus.host, &k_port) == LINK_UPH_DONE) {
            g_u = NULL;
            return u->result;
        }
        advance_us(200u);   /* the panel's own loop between two steps */
        dead_watchdog();
        if (hook != NULL) {
            hook(u);
        }
    }
    g_u = NULL;
    return 0xFFu;
}

static uint8_t update(uint32_t len, hook_t hook)
{
    link_uph_t u;
    link_uph_update(&u, g_new, len, now_ms());
    return run(&u, hook);
}

static uint8_t check(uint32_t len)
{
    link_uph_t u;
    link_uph_check(&u, g_new, len, now_ms());
    return run(&u, NULL);
}

/* The new image runs from slot B, bought, flag clear, the old slot's first
 * sector erased: the state after a good update. */
static void check_runs_new(uint32_t len)
{
    CHECK(cp.up);
    CHECK_EQ(cp.running, 1u);
    CHECK_EQ(cp.bought, 1u);
    CHECK(!cp.trial);
    uint8_t want[LINK_SHA256_BYTES], got[LINK_SHA256_BYTES];
    link_update_build_hash(g_new, len, want);
    link_update_build_hash(g_flash[1], len, got);
    CHECK(memcmp(want, got, sizeof(want)) == 0);
    const int32_t at = link_update_tbyb_byte(g_flash[1], len);
    CHECK(at >= 0);
    CHECK_EQ(g_flash[1][at] & LINK_IMAGE_TBYB_BIT, 0u);
    CHECK(!image_valid(g_flash[0]));
    CHECK(!link_update_dev_holds_arm(&cp.upd));
}

/* The old image runs from slot A, as before. */
static void check_runs_old(void)
{
    CHECK(cp.up);
    CHECK_EQ(cp.running, 0u);
    CHECK_EQ(cp.bought, 0u);
    CHECK(!cp.trial);
    CHECK(memcmp(g_flash[0], g_old, SECTOR) == 0);
}

/* ------------------------------------------------------- the image format */

TEST_CASE(the_flag_is_found_in_the_last_image_def_of_the_loop)
{
    static uint8_t img[8192];
    make_image(img, sizeof(img), 7u, 1u, 2u, 3u);
    const uint32_t second = (sizeof(img) - BLOCK_BYTES) & ~3u;
    CHECK_EQ(link_update_tbyb_byte(img, sizeof(img)), (int32_t)(second + 7u));
    /* A loop of one block: its own flag. */
    make_image(img, 40u, 7u, 1u, 2u, 3u);
    CHECK_EQ(link_update_tbyb_byte(img, 40u), 7);
    /* The first block may sit anywhere in the first 4 KiB. */
    memset(img, 0, sizeof(img));
    put_block(img, 4092u - BLOCK_BYTES, FLAGS_EXE, 0);
    CHECK_EQ(link_update_tbyb_byte(img, sizeof(img)),
             (int32_t)(4092u - BLOCK_BYTES + 7u));
}

TEST_CASE(no_flag_without_a_closed_loop_holding_an_image_def)
{
    static uint8_t img[8192];
    /* The first block past the first 4 KiB is not looked for. */
    memset(img, 0, sizeof(img));
    put_block(img, 4096u, FLAGS_EXE, 0);
    CHECK_EQ(link_update_tbyb_byte(img, sizeof(img)), -1);
    /* A loop that does not close. */
    memset(img, 0, sizeof(img));
    put_block(img, 0u, FLAGS_EXE, 1000);
    CHECK_EQ(link_update_tbyb_byte(img, sizeof(img)), -1);
    /* A block cut off by the image's end. */
    put_block(img, 0u, FLAGS_EXE, 0);
    CHECK_EQ(link_update_tbyb_byte(img, BLOCK_BYTES - 1u), -1);
    CHECK_EQ(link_update_tbyb_byte(img, BLOCK_BYTES), 7);
    /* A block whose first item is not IMAGE_TYPE is no IMAGE_DEF. */
    img[4] = 0x44u;
    CHECK_EQ(link_update_tbyb_byte(img, sizeof(img)), -1);
    /* A LAST item that miscounts its words. */
    put_block(img, 0u, FLAGS_EXE, 0);
    img[9] = 2u;
    CHECK_EQ(link_update_tbyb_byte(img, sizeof(img)), -1);
    /* A missing end marker. */
    put_block(img, 0u, FLAGS_EXE, 0);
    img[16] ^= 1u;
    CHECK_EQ(link_update_tbyb_byte(img, sizeof(img)), -1);
    /* An erased slot. */
    memset(img, 0xFF, sizeof(img));
    CHECK_EQ(link_update_tbyb_byte(img, sizeof(img)), -1);
    CHECK_EQ(link_update_tbyb_byte(NULL, 100u), -1);
}

/* The build hash names a build whatever its flag says: the release image,
 * the copy on trial and the copy bought hash the same. */
TEST_CASE(the_build_hash_ignores_the_flag_and_nothing_else)
{
    static uint8_t img[10000];
    make_image(img, sizeof(img), 3u, 1u, 0u, 0u);
    const int32_t at = link_update_tbyb_byte(img, sizeof(img));
    CHECK(at >= 0);
    uint8_t clear[LINK_SHA256_BYTES], set[LINK_SHA256_BYTES],
            other[LINK_SHA256_BYTES], plain[LINK_SHA256_BYTES];
    link_update_build_hash(img, sizeof(img), clear);
    link_sha256(img, sizeof(img), plain);
    CHECK(memcmp(clear, plain, sizeof(plain)) == 0);   /* flag clear */
    img[at] |= LINK_IMAGE_TBYB_BIT;
    link_update_build_hash(img, sizeof(img), set);
    CHECK(memcmp(clear, set, sizeof(set)) == 0);
    img[at] ^= 0x01u;    /* another bit of the same byte */
    link_update_build_hash(img, sizeof(img), other);
    CHECK(memcmp(clear, other, sizeof(other)) != 0);
    img[at] ^= 0x01u;

    /* Fed in pieces that split around the flag's byte. */
    for (uint32_t step = 1u; step < 40u; step += 7u) {
        link_image_hash_t h;
        uint8_t d[LINK_SHA256_BYTES];
        link_image_hash_begin(&h, sizeof(img), at, false);
        while (!link_image_hash_step(&h, &img[h.pos], step)) {
        }
        link_image_hash_end(&h, d);
        CHECK(memcmp(d, clear, sizeof(d)) == 0);
    }
    CHECK_EQ(link_image_byte(img, (uint32_t)at, at, false),
             img[at] & (uint8_t)~LINK_IMAGE_TBYB_BIT);
    CHECK_EQ(link_image_byte(img, (uint32_t)at, at, true),
             img[at] | LINK_IMAGE_TBYB_BIT);
    CHECK_EQ(link_image_byte(img, 5u, at, true), img[5]);
    CHECK_EQ(link_image_byte(img, 5u, -1, true), img[5]);
}

/* ------------------------------------------------------- the device pages */

static uint8_t cmd(uint16_t c, uint16_t a0, uint16_t a1)
{
    const uint16_t r[3] = { c, a0, a1 };
    return link_update_dev_write_ctl(&cp.upd, LINK_UPD_CMD, 3u, r);
}

static uint16_t ctl(uint8_t reg)
{
    uint16_t v = 0xBEEFu;
    link_update_dev_read_ctl(&cp.upd, reg, 1u, &v);
    return v;
}

/* Stage block @p b of g_new and commit it. */
static uint8_t commit(uint16_t b, uint32_t len)
{
    uint8_t bytes[64];
    memset(bytes, 0xFF, sizeof(bytes));
    const uint32_t off = (uint32_t)b * 64u;
    const uint32_t n = (len - off < 64u) ? len - off : 64u;
    memcpy(bytes, &g_new[off], n);
    uint16_t regs[32];
    for (unsigned i = 0; i < 32u; ++i) {
        regs[i] = (uint16_t)(bytes[2u * i] | (bytes[2u * i + 1u] << 8));
    }
    CHECK_EQ(link_update_dev_write_data(&cp.upd, 0u, 32u, regs), 0u);
    return cmd(LINK_UPD_CMD_COMMIT, b, link_crc(LINK_CRC_INIT, bytes, n));
}

TEST_CASE(the_control_page_takes_commands_and_nothing_else)
{
    bench(SLOT_SMALL, 30000u, 0u);
    CHECK_EQ(ctl(LINK_UPD_VERSION), LINK_UPD_PROTOCOL);
    CHECK_EQ(ctl(LINK_UPD_STATE), LINK_UPD_IDLE);
    CHECK_EQ(ctl(LINK_UPD_SLOT), LINK_UPD_SLOT_A);
    CHECK_EQ(ctl(LINK_UPD_SLOT_SECTORS), 16u);
    CHECK_EQ(ctl(13u), 0u);
    const uint16_t v[5] = { LINK_UPD_CMD_ABORT, 0u, 0u, 0u, 0u };
    CHECK_EQ(link_update_dev_write_ctl(&cp.upd, LINK_UPD_STATE, 1u, v),
             LINK_NACK_READ_ONLY);
    CHECK_EQ(link_update_dev_write_ctl(&cp.upd, LINK_UPD_CMD, 5u, v),
             LINK_NACK_READ_ONLY);
    CHECK_EQ(link_update_dev_write_ctl(&cp.upd, LINK_UPD_CMD, 0u, v),
             LINK_NACK_READ_ONLY);
    CHECK_EQ(link_update_dev_write_ctl(&cp.upd, LINK_UPD_CMD, 1u, v), 0u);
    CHECK_EQ(cmd(99u, 0u, 0u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(ctl(LINK_UPD_ERROR), LINK_UPD_ERR_COMMAND);
    CHECK_EQ(link_update_dev_write_data(&cp.upd, 30u, 3u, v),
             LINK_NACK_BAD_RANGE);
    /* BUY with nothing on trial: nothing to do. */
    CHECK_EQ(cmd(LINK_UPD_CMD_BUY, 0u, 0u), 0u);
    /* The update registers' size, as the frame layer carries them. */
    CHECK(LINK_UPD_COUNT <= LINK_MAX_REGS);
    CHECK(LINK_UPD_DATA_COUNT * 2u == LINK_UPD_BLOCK_BYTES);
}

TEST_CASE(begin_takes_one_byte_to_exactly_one_slot)
{
    bench(SLOT_SMALL, 30000u, 0u);
    CHECK_EQ(cmd(LINK_UPD_CMD_BEGIN, 0u, 0u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(ctl(LINK_UPD_ERROR), LINK_UPD_ERR_SIZE);
    const uint32_t over = SLOT_SMALL + 1u;
    CHECK_EQ(cmd(LINK_UPD_CMD_BEGIN, (uint16_t)over, (uint16_t)(over >> 16)),
             LINK_NACK_BAD_VALUE);
    CHECK_EQ(ctl(LINK_UPD_ERROR), LINK_UPD_ERR_SIZE);
    CHECK_EQ(cp.erases, 0u);
    CHECK_EQ(cmd(LINK_UPD_CMD_BEGIN, (uint16_t)SLOT_SMALL,
                 (uint16_t)(SLOT_SMALL >> 16)), 0u);
    CHECK_EQ(ctl(LINK_UPD_STATE), LINK_UPD_RECEIVING);
    CHECK_EQ(cp.erases, 1u);   /* the first sector, at once */
    CHECK_EQ(cmd(LINK_UPD_CMD_BEGIN, 1u, 0u), 0u);   /* starts over */
    CHECK_EQ(ctl(LINK_UPD_NEXT_BLOCK), 0u);
}

TEST_CASE(blocks_go_in_strictly_in_order_and_a_repeat_is_a_no_op)
{
    bench(SLOT_SMALL, 30000u, 0u);
    const uint32_t len = 2u * SECTOR + 10u;
    make_image(g_new, len, 2u, 19u, 0u, 0u);
    CHECK_EQ(cmd(LINK_UPD_CMD_COMMIT, 0u, 0u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(ctl(LINK_UPD_ERROR), LINK_UPD_ERR_STATE);
    CHECK_EQ(cmd(LINK_UPD_CMD_BEGIN, (uint16_t)len, 0u), 0u);
    CHECK_EQ(commit(1u, len), LINK_NACK_BAD_VALUE);
    CHECK_EQ(ctl(LINK_UPD_ERROR), LINK_UPD_ERR_ORDER);
    CHECK_EQ(commit(0u, len), 0u);
    CHECK_EQ(commit(0u, len), 0u);           /* again: acknowledged */
    CHECK_EQ(ctl(LINK_UPD_NEXT_BLOCK), 1u);
    CHECK_EQ(commit(2u, len), LINK_NACK_BAD_VALUE);
    CHECK_EQ(ctl(LINK_UPD_ERROR), LINK_UPD_ERR_ORDER);
    CHECK_EQ(ctl(LINK_UPD_STATE), LINK_UPD_RECEIVING);
    /* A block that fails its CRC is not taken and costs nothing. */
    CHECK_EQ(cmd(LINK_UPD_CMD_COMMIT, 1u, 0x1234u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(ctl(LINK_UPD_ERROR), LINK_UPD_ERR_CRC);
    CHECK_EQ(ctl(LINK_UPD_NEXT_BLOCK), 1u);
    for (uint16_t b = 1u; b < 128u; ++b) {
        CHECK_EQ(commit(b, len), 0u);
    }
    /* Sector 1 is whole and written; sector 0 waits in RAM. */
    CHECK_EQ(cp.programs, 1u);
    CHECK(memcmp(&g_flash[1][SECTOR], &g_new[SECTOR], SECTOR) == 0);
    for (uint32_t i = 0; i < SECTOR; ++i) {
        if (g_flash[1][i] != 0xFFu) {
            T_FAIL("first sector written at byte %u", i);
        }
    }
    /* The last block: 10 bytes, alone in its sector, written at once. */
    CHECK_EQ(commit(128u, len), 0u);
    CHECK_EQ(ctl(LINK_UPD_STATE), LINK_UPD_RECEIVED);
    CHECK_EQ(cp.programs, 2u);
    CHECK(memcmp(&g_flash[1][2u * SECTOR], &g_new[2u * SECTOR], 10u) == 0);
    CHECK_EQ(g_flash[1][2u * SECTOR + 10u], 0xFFu);
    CHECK_EQ(commit(128u, len), 0u);         /* the last again, too */
    CHECK_EQ(commit(129u, len), LINK_NACK_BAD_VALUE);
    CHECK_EQ(ctl(LINK_UPD_ERROR), LINK_UPD_ERR_STATE);
}

TEST_CASE(a_shut_gate_refuses_every_flash_write_with_busy)
{
    bench(SLOT_SMALL, 30000u, 0u);
    const uint32_t len = SECTOR + 64u;
    make_image(g_new, len, 2u, 19u, 0u, 0u);
    cp.gate_shut = true;
    CHECK_EQ(cmd(LINK_UPD_CMD_BEGIN, (uint16_t)len, 0u), LINK_NACK_BUSY);
    CHECK_EQ(ctl(LINK_UPD_ERROR), LINK_UPD_ERR_GATE);
    CHECK_EQ(cp.erases, 0u);
    cp.gate_shut = false;
    CHECK_EQ(cmd(LINK_UPD_CMD_BEGIN, (uint16_t)len, 0u), 0u);
    cp.gate_shut = true;
    /* Blocks of the first sector need no flash and go in. */
    for (uint16_t b = 0u; b < 64u; ++b) {
        CHECK_EQ(commit(b, len), 0u);
    }
    /* The last block writes sector 1: refused, and not taken. */
    CHECK_EQ(commit(64u, len), LINK_NACK_BUSY);
    CHECK_EQ(ctl(LINK_UPD_NEXT_BLOCK), 64u);
    cp.gate_shut = false;
    CHECK_EQ(commit(64u, len), 0u);
    uint16_t regs[16];
    uint8_t d[LINK_SHA256_BYTES];
    link_sha256(g_new, len, d);
    for (unsigned i = 0; i < 16u; ++i) {
        regs[i] = (uint16_t)(d[2u * i] | (d[2u * i + 1u] << 8));
    }
    CHECK_EQ(link_update_dev_write_data(&cp.upd, 0u, 16u, regs), 0u);
    CHECK_EQ(cmd(LINK_UPD_CMD_FINISH, 0u, 0u), 0u);
    CHECK_EQ(cmd(LINK_UPD_CMD_ACTIVATE, 0u, 0u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(ctl(LINK_UPD_ERROR), LINK_UPD_ERR_STATE);   /* still hashing */
    for (int i = 0; i < 10; ++i) {
        link_update_dev_poll(&cp.upd, now_ms(), now_ms());
    }
    CHECK_EQ(ctl(LINK_UPD_STATE), LINK_UPD_VERIFIED);
    cp.gate_shut = true;
    CHECK_EQ(cmd(LINK_UPD_CMD_ACTIVATE, 0u, 0u), LINK_NACK_BUSY);
    CHECK_EQ(cp.sector0_programs, 0u);
    cp.gate_shut = false;
    CHECK_EQ(cmd(LINK_UPD_CMD_ACTIVATE, 0u, 0u), 0u);
    CHECK_EQ(ctl(LINK_UPD_STATE), LINK_UPD_ACTIVATED);
    CHECK_EQ(cmd(LINK_UPD_CMD_ABORT, 0u, 0u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(cmd(LINK_UPD_CMD_BEGIN, 1u, 0u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(cmd(LINK_UPD_CMD_HASH_RUNNING, 1u, 0u), LINK_NACK_BAD_VALUE);
}

TEST_CASE(arm_is_refused_for_the_whole_session_and_disarm_never)
{
    bench(SLOT_SMALL, 30000u, 0u);
    const uint16_t arm[3] = { 1u, 0u, 14u };
    const uint16_t disarm[1] = { 0u };
    CHECK(!link_update_dev_refuses_arm(&cp.upd, LINK_CT_ARM, 3u, arm));
    CHECK_EQ(cmd(LINK_UPD_CMD_BEGIN, 100u, 0u), 0u);
    CHECK(link_update_dev_holds_arm(&cp.upd));
    CHECK(link_update_dev_refuses_arm(&cp.upd, LINK_CT_ARM, 3u, arm));
    CHECK(!link_update_dev_refuses_arm(&cp.upd, LINK_CT_ARM, 1u, disarm));
    /* A frame that does not carry ARM is not the update's business. */
    CHECK(!link_update_dev_refuses_arm(&cp.upd, LINK_CT_THROTTLE, 1u, arm));
    CHECK(!link_update_dev_refuses_arm(&cp.upd, LINK_CT_ARM, 0u, arm));
    CHECK(!link_update_dev_refuses_arm(&cp.upd, LINK_CT_ARM, 3u, NULL));
    /* Through the page, as the panel's ARM goes. */
    CHECK_EQ(wr_control(NULL, LINK_CT_ARM, 3u, arm), LINK_NACK_NOT_ARMED);
    CHECK_EQ(cp.control[LINK_CT_ARM], 0u);
    CHECK_EQ(cmd(LINK_UPD_CMD_ABORT, 0u, 0u), 0u);
    CHECK(!link_update_dev_holds_arm(&cp.upd));
    CHECK_EQ(wr_control(NULL, LINK_CT_ARM, 3u, arm), 0u);
    /* A failed session holds nothing either. */
    CHECK_EQ(cmd(LINK_UPD_CMD_BEGIN, 100u, 0u), 0u);
    link_update_dev_poll(&cp.upd, now_ms() + LINK_UPD_SILENCE_MS,
                         now_ms());
    CHECK_EQ(ctl(LINK_UPD_STATE), LINK_UPD_FAILED);
    CHECK_EQ(ctl(LINK_UPD_ERROR), LINK_UPD_ERR_LINK);
    CHECK(!link_update_dev_holds_arm(&cp.upd));
}

TEST_CASE(link_silence_ends_a_session_and_nothing_shorter_does)
{
    bench(SLOT_SMALL, 30000u, 0u);
    CHECK_EQ(cmd(LINK_UPD_CMD_BEGIN, 100u, 0u), 0u);
    const uint32_t t = now_ms();
    link_update_dev_poll(&cp.upd, t + LINK_UPD_SILENCE_MS - 1u, t);
    CHECK_EQ(ctl(LINK_UPD_STATE), LINK_UPD_RECEIVING);
    link_update_dev_poll(&cp.upd, t + LINK_UPD_SILENCE_MS, t);
    CHECK_EQ(ctl(LINK_UPD_STATE), LINK_UPD_FAILED);
    /* Across the wrap of the millisecond clock. */
    CHECK_EQ(cmd(LINK_UPD_CMD_BEGIN, 100u, 0u), 0u);
    link_update_dev_poll(&cp.upd, 100u, 0xFFFFFF00u);
    CHECK_EQ(ctl(LINK_UPD_STATE), LINK_UPD_RECEIVING);
    link_update_dev_poll(&cp.upd, LINK_UPD_SILENCE_MS, 0xFFFFFF00u);
    CHECK_EQ(ctl(LINK_UPD_STATE), LINK_UPD_FAILED);
    /* Idle silence is nothing. */
    CHECK_EQ(cmd(LINK_UPD_CMD_ABORT, 0u, 0u), 0u);
    link_update_dev_poll(&cp.upd, 1000000u, 0u);
    CHECK_EQ(ctl(LINK_UPD_STATE), LINK_UPD_IDLE);
}

TEST_CASE(no_partition_table_means_no_slots)
{
    bench(SLOT_SMALL, 30000u, 0u);
    link_update_dev_init(&cp.upd, &cp.io, g_flash[0], g_flash[1], SLOT_SMALL,
                         LINK_UPD_SLOT_NONE);
    CHECK_EQ(ctl(LINK_UPD_SLOT), LINK_UPD_SLOT_NONE);
    CHECK_EQ(ctl(LINK_UPD_SLOT_SECTORS), 0u);
    CHECK_EQ(cmd(LINK_UPD_CMD_BEGIN, 100u, 0u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(ctl(LINK_UPD_ERROR), LINK_UPD_ERR_NO_SLOTS);
    CHECK_EQ(cmd(LINK_UPD_CMD_HASH_RUNNING, 100u, 0u), LINK_NACK_BAD_VALUE);
    /* A slot size the pages cannot count in blocks, or not whole sectors. */
    link_update_dev_init(&cp.upd, &cp.io, g_flash[0], g_flash[1],
                         SECTOR + 1u, LINK_UPD_SLOT_A);
    CHECK_EQ(ctl(LINK_UPD_SLOT), LINK_UPD_SLOT_NONE);
    link_update_dev_init(&cp.upd, &cp.io, g_flash[0], g_flash[1],
                         0x10000u * 64u, LINK_UPD_SLOT_A);
    CHECK_EQ(ctl(LINK_UPD_SLOT), LINK_UPD_SLOT_NONE);
    link_update_dev_init(&cp.upd, &cp.io, g_flash[0], NULL, SLOT_SMALL,
                         LINK_UPD_SLOT_A);
    CHECK_EQ(ctl(LINK_UPD_SLOT), LINK_UPD_SLOT_NONE);
    /* The trial flag survives: a BUY is still owed. */
    link_update_dev_init(&cp.upd, NULL, NULL, NULL, 0u,
                         LINK_UPD_SLOT_B | LINK_UPD_SLOT_TRIAL);
    CHECK_EQ(ctl(LINK_UPD_SLOT), LINK_UPD_SLOT_NONE | LINK_UPD_SLOT_TRIAL);
    CHECK_EQ(cmd(LINK_UPD_CMD_BUY, 0u, 0u), LINK_NACK_BAD_VALUE);
    CHECK_EQ(ctl(LINK_UPD_ERROR), LINK_UPD_ERR_BUY);
}

/* ------------------------------------------------------------ the engine */

TEST_CASE(an_update_moves_the_coprocessor_to_the_image_and_buys_it)
{
    bench(SLOT_SMALL, 30000u, 0u);
    const uint32_t len = 40000u;
    make_image(g_new, len, 2u, 19u, 0u, 0u);
    CHECK_EQ(check(len), LINK_UPH_DIFFERENT);
    link_uph_t u;
    link_uph_update(&u, g_new, len, now_ms());
    CHECK_EQ(run(&u, NULL), LINK_UPH_UPDATED);
    CHECK_EQ(link_uph_permille(&u), 1000u);
    check_runs_new(len);
    /* What the panel read from the new image. */
    CHECK(u.have_ident);
    CHECK_EQ(u.ident[LINK_ID_FIRMWARE_MAJOR], 19u);
    CHECK_EQ(u.ident[LINK_ID_PROTOCOL_MAJOR], LINK_PROTOCOL_MAJOR);
    CHECK_EQ(cp.boots, 2u);   /* the bench's start, the update's */
    CHECK_EQ(check(len), LINK_UPH_SAME);
    /* And back: the old image, now on slot A. */
    memcpy(g_new, g_old, 30000u);
    CHECK_EQ(check(30000u), LINK_UPH_DIFFERENT);
    CHECK_EQ(update(30000u, NULL), LINK_UPH_UPDATED);
    CHECK_EQ(cp.running, 0u);
    CHECK_EQ(check(30000u), LINK_UPH_SAME);
}

/* The sizes at the edges: one byte, the last block part full, a block
 * alone in the last sector, and exactly one slot. */
TEST_CASE(image_sizes_at_the_limits)
{
    static const uint32_t lens[] = {
        40u, 64u, 65u, SECTOR - 1u, SECTOR, SECTOR + 1u, 3u * SECTOR + 63u,
        SLOT_SMALL - 1u, SLOT_SMALL,
    };
    for (unsigned i = 0; i < sizeof(lens) / sizeof(lens[0]); ++i) {
        bench(SLOT_SMALL, 30000u, 0u);
        make_image(g_new, lens[i], 10u + i, 19u, 0u, (uint16_t)i);
        if (update(lens[i], NULL) != LINK_UPH_UPDATED) {
            T_FAIL("an image of %u bytes", lens[i]);
        }
        check_runs_new(lens[i]);
        CHECK_EQ(cp.sector0_programs, 1u);
        CHECK_EQ(bus.commits, (lens[i] + 63u) / 64u);
    }
}

TEST_CASE(an_image_one_byte_over_a_slot_needs_usb)
{
    bench(SLOT_SMALL, 30000u, 0u);
    const uint32_t len = SLOT_SMALL + 1u;
    make_image(g_new, len, 2u, 19u, 0u, 0u);
    CHECK_EQ(check(len), LINK_UPH_NEEDS_USB);
    link_uph_t u;
    link_uph_update(&u, g_new, len, now_ms());
    CHECK_EQ(run(&u, NULL), LINK_UPH_REFUSED);
    CHECK_EQ(u.error, LINK_UPD_ERR_SIZE);
    CHECK_EQ(cp.erases, 0u);
    check_runs_old();
}

/* The slot's first sector is written last: until ACTIVATE the slot holds
 * nothing the boot ROM would start. */
static void first_sector_blank_until_activate(const link_uph_t *u)
{
    if (u->phase < LINK_UPH_RESTARTING && cp.running == 0u
        && image_valid(g_flash[1])) {
        T_FAIL("slot B startable in phase %u", (unsigned)u->phase);
    }
}

TEST_CASE(the_slot_is_not_startable_before_activate)
{
    bench(SLOT_SMALL, 30000u, 0u);
    const uint32_t len = 5u * SECTOR + 7u;
    make_image(g_new, len, 2u, 19u, 0u, 0u);
    CHECK_EQ(update(len, first_sector_blank_until_activate),
             LINK_UPH_UPDATED);
    check_runs_new(len);
}

TEST_CASE(a_corrupted_block_is_sent_again)
{
    bench(SLOT_SMALL, 30000u, 0u);
    const uint32_t len = 20000u;
    make_image(g_new, len, 2u, 19u, 0u, 0u);
    bus.corrupt_block = 70u;     /* the first block of sector 1 */
    bus.corrupt_block_left = 2u;
    CHECK_EQ(update(len, NULL), LINK_UPH_UPDATED);
    CHECK_EQ(bus.corrupt_block_left, 0u);
    CHECK_EQ(bus.commits, (len + 63u) / 64u + 2u);
    check_runs_new(len);
}

TEST_CASE(a_block_that_never_passes_its_crc_ends_the_update)
{
    bench(SLOT_SMALL, 30000u, 0u);
    const uint32_t len = 20000u;
    make_image(g_new, len, 2u, 19u, 0u, 0u);
    bus.corrupt_block = 3u;
    bus.corrupt_block_left = 1000u;
    link_uph_t u;
    link_uph_update(&u, g_new, len, now_ms());
    CHECK_EQ(run(&u, NULL), LINK_UPH_REFUSED);
    CHECK_EQ(u.error, LINK_UPD_ERR_CRC);
    CHECK_EQ(bus.commits, 3u + 1u + LINK_UPH_BLOCK_TRIES);
    /* The session stays open; ARM stays refused until it ends. */
    CHECK(link_update_dev_holds_arm(&cp.upd));
    advance_us((uint64_t)LINK_UPD_SILENCE_MS * 1000u);
    CHECK(!link_update_dev_holds_arm(&cp.upd));
    check_runs_old();
}

/* The acknowledgement of a COMMIT lost: the panel asks where it stands and
 * goes on from the block the coprocessor expects. */
TEST_CASE(a_lost_acknowledgement_resumes_without_a_restart)
{
    for (long drop = 3; drop < 40; drop += 9) {
        bench(SLOT_SMALL, 30000u, 0u);
        const uint32_t len = 20000u;
        make_image(g_new, len, 2u, 19u, 0u, 0u);
        bus.drop_reply = 200 + drop;
        link_uph_t u;
        link_uph_update(&u, g_new, len, now_ms());
        CHECK_EQ(run(&u, NULL), LINK_UPH_UPDATED);
        CHECK_EQ(u.restarts, 0u);
        check_runs_new(len);
    }
    /* A request frame lost on the way out, too. */
    bench(SLOT_SMALL, 30000u, 0u);
    make_image(g_new, 20000u, 2u, 19u, 0u, 0u);
    bus.drop_request = 400;
    link_uph_t u;
    link_uph_update(&u, g_new, 20000u, now_ms());
    CHECK_EQ(run(&u, NULL), LINK_UPH_UPDATED);
    CHECK_EQ(u.restarts, 0u);
}

static uint16_t g_reset_at;
static unsigned g_resets_left;
static uint32_t g_drop_ms;

static void reset_at_block(const link_uph_t *u)
{
    if (g_resets_left > 0u && u->phase == LINK_UPH_SEND
        && u->block == g_reset_at && cp.up) {
        --g_resets_left;
        cp.up = false;
        cp.boot_kind = BOOT_NORMAL;
        cp.boot_at_us = g_us + (uint64_t)BOOT_MS * 1000u;
    }
}

TEST_CASE(a_coprocessor_restart_mid_transfer_starts_again_from_block_0)
{
    bench(SLOT_SMALL, 30000u, 0u);
    const uint32_t len = 30000u;
    make_image(g_new, len, 2u, 19u, 0u, 0u);
    g_reset_at = 200u;
    g_resets_left = 1u;
    link_uph_t u;
    link_uph_update(&u, g_new, len, now_ms());
    CHECK_EQ(run(&u, reset_at_block), LINK_UPH_UPDATED);
    CHECK_EQ(u.restarts, 1u);
    CHECK_EQ(cp.boots, 3u);
    CHECK(bus.commits > (len + 63u) / 64u + 199u);
    check_runs_new(len);
}

TEST_CASE(restarts_without_end_give_up)
{
    bench(SLOT_SMALL, 30000u, 0u);
    const uint32_t len = 30000u;
    make_image(g_new, len, 2u, 19u, 0u, 0u);
    g_reset_at = 50u;
    g_resets_left = 100u;
    link_uph_t u;
    link_uph_update(&u, g_new, len, now_ms());
    CHECK_EQ(run(&u, reset_at_block), LINK_UPH_RESTARTED_OUT);
    CHECK_EQ(u.restarts, LINK_UPH_RESTARTS + 1u);
    advance_us(1000000u);
    check_runs_old();
}

static void drop_link_at_block(const link_uph_t *u)
{
    if (g_resets_left > 0u && u->phase == LINK_UPH_SEND
        && u->block == g_reset_at) {
        --g_resets_left;
        bus.dead = true;
        bus.dead_until_us = g_us + (uint64_t)g_drop_ms * 1000u;
    }
}

TEST_CASE(a_link_drop_mid_transfer)
{
    /* Shorter than the coprocessor's 5 s: the session goes on. */
    bench(SLOT_SMALL, 30000u, 0u);
    const uint32_t len = 30000u;
    make_image(g_new, len, 2u, 19u, 0u, 0u);
    g_reset_at = 100u;
    g_resets_left = 1u;
    g_drop_ms = 2500u;
    link_uph_t u;
    link_uph_update(&u, g_new, len, now_ms());
    CHECK_EQ(run(&u, drop_link_at_block), LINK_UPH_UPDATED);
    CHECK_EQ(u.restarts, 0u);
    check_runs_new(len);

    /* Longer than the panel's 4 s: the panel gives up, the coprocessor's
     * session ends at 5 s, and the next update starts from block 0. */
    bench(SLOT_SMALL, 30000u, 0u);
    make_image(g_new, len, 2u, 19u, 0u, 0u);
    g_resets_left = 1u;
    g_drop_ms = 8000u;
    link_uph_update(&u, g_new, len, now_ms());
    CHECK_EQ(run(&u, drop_link_at_block), LINK_UPH_NO_ANSWER);
    advance_us((uint64_t)LINK_UPD_SILENCE_MS * 1000u);
    CHECK_EQ(cp.upd.state, LINK_UPD_FAILED);
    CHECK_EQ(cp.upd.error, LINK_UPD_ERR_LINK);
    CHECK(!link_update_dev_holds_arm(&cp.upd));
    CHECK_EQ(update(len, NULL), LINK_UPH_UPDATED);
    check_runs_new(len);
}

static unsigned g_arm_tries;

static void arm_during(const link_uph_t *u)
{
    (void)u;
    if (cp.up) {
        const uint16_t arm[3] = { 1u, 0u, 14u };
        link_msg_t r;
        if (link_write_acked(&bus.host, &k_port, LINK_PAGE_CONTROL,
                             LINK_CT_ARM, 3u, arm, &r)) {
            ++g_arm_tries;
            if (r.op == LINK_OP_ACK && link_update_dev_holds_arm(&cp.upd)) {
                T_FAIL("ARM taken in phase %u", (unsigned)u->phase);
            }
            if (r.op == LINK_OP_ACK) {
                /* Disarm again: the next try asks anew. */
                const uint16_t off[1] = { 0u };
                (void)link_write_acked(&bus.host, &k_port, LINK_PAGE_CONTROL,
                                       LINK_CT_ARM, 1u, off, &r);
            }
        }
    }
}

TEST_CASE(arm_attempted_through_an_update_is_refused)
{
    bench(SLOT_SMALL, 30000u, 0u);
    const uint32_t len = 9000u;
    make_image(g_new, len, 2u, 19u, 0u, 0u);
    g_arm_tries = 0u;
    cp.arm_refused = 0u;
    CHECK_EQ(update(len, arm_during), LINK_UPH_UPDATED);
    CHECK(g_arm_tries > 100u);
    CHECK(cp.arm_refused > 100u);
    check_runs_new(len);
    /* Bought: ARM goes through again. */
    const uint16_t arm[3] = { 1u, 0u, 14u };
    link_msg_t r;
    CHECK(link_write_acked(&bus.host, &k_port, LINK_PAGE_CONTROL, LINK_CT_ARM,
                           3u, arm, &r));
    CHECK_EQ(r.op, LINK_OP_ACK);
}

TEST_CASE(a_shut_gate_is_waited_for_and_then_given_up_on)
{
    bench(SLOT_SMALL, 30000u, 0u);
    const uint32_t len = 9000u;
    make_image(g_new, len, 2u, 19u, 0u, 0u);
    cp.gate_shut_until_us = g_us + 3000000u;   /* 3 s, a store saving */
    CHECK_EQ(update(len, NULL), LINK_UPH_UPDATED);
    check_runs_new(len);

    bench(SLOT_SMALL, 30000u, 0u);
    make_image(g_new, len, 2u, 19u, 0u, 0u);
    cp.gate_shut = true;                       /* the bank is driving */
    link_uph_t u;
    link_uph_update(&u, g_new, len, now_ms());
    CHECK_EQ(run(&u, NULL), LINK_UPH_BUSY);
    CHECK_EQ(cp.erases, 0u);
    check_runs_old();
}

TEST_CASE(a_flash_fault_is_caught_by_the_digest)
{
    bench(SLOT_SMALL, 30000u, 0u);
    const uint32_t len = 20000u;
    make_image(g_new, len, 2u, 19u, 0u, 0u);
    cp.program_flips_at = (int)(2u * SECTOR);
    link_uph_t u;
    link_uph_update(&u, g_new, len, now_ms());
    CHECK_EQ(run(&u, NULL), LINK_UPH_BAD_DIGEST);
    CHECK_EQ(cp.upd.error, LINK_UPD_ERR_DIGEST);
    CHECK_EQ(cp.sector0_programs, 0u);
    CHECK(!image_valid(g_flash[1]));
    CHECK(memcmp(u.digest, u.expect, sizeof(u.digest)) != 0);
    advance_us(1000000u);
    check_runs_old();
    CHECK(!link_update_dev_holds_arm(&cp.upd));
}

TEST_CASE(a_first_sector_that_reads_back_wrong_is_not_started)
{
    bench(SLOT_SMALL, 30000u, 0u);
    const uint32_t len = 20000u;
    make_image(g_new, len, 2u, 19u, 0u, 0u);
    cp.program_flips_at = 0;
    link_uph_t u;
    link_uph_update(&u, g_new, len, now_ms());
    CHECK_EQ(run(&u, NULL), LINK_UPH_REFUSED);
    CHECK_EQ(u.error, LINK_UPD_ERR_FLASH);
    CHECK_EQ(cp.boots, 1u);
    check_runs_old();
}

TEST_CASE(an_erase_that_fails_ends_or_restarts_the_session)
{
    bench(SLOT_SMALL, 30000u, 0u);
    const uint32_t len = 20000u;
    make_image(g_new, len, 2u, 19u, 0u, 0u);
    cp.erase_fails_at = 0;
    link_uph_t u;
    link_uph_update(&u, g_new, len, now_ms());
    CHECK_EQ(run(&u, NULL), LINK_UPH_REFUSED);
    CHECK_EQ(u.error, LINK_UPD_ERR_FLASH);
    /* A later sector: the session fails, the panel starts over, and gives
     * up when it keeps failing. */
    bench(SLOT_SMALL, 30000u, 0u);
    make_image(g_new, len, 2u, 19u, 0u, 0u);
    cp.erase_fails_at = (int)(3u * SECTOR);
    link_uph_update(&u, g_new, len, now_ms());
    CHECK_EQ(run(&u, NULL), LINK_UPH_RESTARTED_OUT);
    check_runs_old();
}

TEST_CASE(an_image_never_bought_goes_back_to_the_old_one)
{
    bench(SLOT_SMALL, 30000u, 0u);
    const uint32_t len = 20000u;
    make_image(g_new, len, 2u, 19u, 0u, 0u);
    /* The panel stops after the restart: no BUY comes. */
    link_uph_t u;
    link_uph_update(&u, g_new, len, now_ms());
    while (u.phase != LINK_UPH_RESTARTING) {
        CHECK(link_uph_step(&u, &bus.host, &k_port) != LINK_UPH_DONE);
    }
    advance_us(1000000u);
    CHECK(cp.up);
    CHECK(cp.trial);
    CHECK_EQ(cp.running, 1u);
    CHECK(link_update_dev_holds_arm(&cp.upd));
    CHECK_EQ(check(len), LINK_UPH_TRIAL);
    advance_us((uint64_t)TBYB_MS * 1000u);
    check_runs_old();
    CHECK_EQ(cp.boots, 3u);
    CHECK(!link_update_dev_holds_arm(&cp.upd));
    CHECK_EQ(check(len), LINK_UPH_DIFFERENT);
    /* The slot is written and startable; the next update writes it again. */
    CHECK_EQ(update(len, NULL), LINK_UPH_UPDATED);
    check_runs_new(len);
}

TEST_CASE(an_image_found_on_trial_is_bought_at_link_up)
{
    bench(SLOT_SMALL, 30000u, 0u);
    const uint32_t len = 20000u;
    make_image(g_new, len, 2u, 19u, 0u, 0u);
    link_uph_t u;
    link_uph_update(&u, g_new, len, now_ms());
    while (u.phase != LINK_UPH_RESTARTING) {
        (void)link_uph_step(&u, &bus.host, &k_port);
    }
    advance_us(2000000u);   /* the panel restarted meanwhile */
    CHECK_EQ(check(len), LINK_UPH_TRIAL);
    link_uph_confirm(&u, now_ms());
    CHECK_EQ(run(&u, NULL), LINK_UPH_UPDATED);
    check_runs_new(len);
    advance_us((uint64_t)TBYB_MS * 1000u);
    check_runs_new(len);
}

TEST_CASE(a_new_image_that_never_answers_is_rolled_back)
{
    bench(SLOT_SMALL, 30000u, 0u);
    const uint32_t len = 20000u;
    make_image(g_new, len, 2u, 19u, 0u, 0u);
    cp.new_image_dead = true;
    link_uph_t u;
    link_uph_update(&u, g_new, len, now_ms());
    CHECK_EQ(run(&u, NULL), LINK_UPH_NOT_STARTED);
    CHECK(!u.have_ident);
    advance_us((uint64_t)TBYB_MS * 1000u);
    dead_watchdog();
    advance_us(1000000u);
    check_runs_old();
}

TEST_CASE(a_buy_the_boot_rom_refuses_is_reported)
{
    bench(SLOT_SMALL, 30000u, 0u);
    const uint32_t len = 20000u;
    make_image(g_new, len, 2u, 19u, 0u, 0u);
    cp.buy_fails = true;
    link_uph_t u;
    link_uph_update(&u, g_new, len, now_ms());
    CHECK_EQ(run(&u, NULL), LINK_UPH_REFUSED);
    CHECK_EQ(u.error, LINK_UPD_ERR_BUY);
    advance_us((uint64_t)(TBYB_MS + 1000u) * 1000u);
    check_runs_old();
}

/* A coprocessor of 4.21 or older answers both pages with BAD_PAGE. */
TEST_CASE(an_old_coprocessor_needs_usb)
{
    bench(SLOT_SMALL, 30000u, 0u);
    cp.knows_update = false;
    cp.proto_minor = LINK_MINOR_UPDATE - 1u;
    cp_boot_now();
    make_image(g_new, 20000u, 2u, 19u, 0u, 0u);
    CHECK_EQ(check(20000u), LINK_UPH_NEEDS_USB);
    CHECK_EQ(update(20000u, NULL), LINK_UPH_NEEDS_USB);
    CHECK_EQ(cp.erases, 0u);
    /* One that knows the pages but booted without the partition table. */
    bench(SLOT_SMALL, 30000u, 0u);
    cp.no_table = true;
    cp_boot_now();
    CHECK_EQ(check(20000u), LINK_UPH_NEEDS_USB);
    CHECK_EQ(update(20000u, NULL), LINK_UPH_NEEDS_USB);
    /* No coprocessor at all. */
    bench(SLOT_SMALL, 30000u, 0u);
    bus.dead = true;
    bus.dead_until_us = UINT64_MAX;
    CHECK_EQ(check(20000u), LINK_UPH_NO_ANSWER);
}

/* A newer minor and another major answer the frozen pages the same way. */
TEST_CASE(another_minor_or_major_still_updates)
{
    static const uint16_t vers[][2] = {
        { LINK_PROTOCOL_MAJOR, LINK_PROTOCOL_MINOR + 3u },
        { LINK_PROTOCOL_MAJOR + 1u, 0u },
        { LINK_PROTOCOL_MAJOR - 1u, 40u },
    };
    for (unsigned i = 0; i < 3u; ++i) {
        bench(SLOT_SMALL, 30000u, 0u);
        cp.proto_major = vers[i][0];
        cp.proto_minor = vers[i][1];
        make_image(g_new, 20000u, 2u, 17u, 0u, 0u);   /* an older build */
        CHECK_EQ(check(20000u), LINK_UPH_DIFFERENT);
        link_uph_t u;
        link_uph_update(&u, g_new, 20000u, now_ms());
        CHECK_EQ(run(&u, NULL), LINK_UPH_UPDATED);
        CHECK_EQ(u.ident[LINK_ID_PROTOCOL_MAJOR], vers[i][0]);
        CHECK_EQ(u.ident[LINK_ID_FIRMWARE_MAJOR], 17u);
        check_runs_new(20000u);
    }
}

TEST_CASE(the_clock_may_wrap_during_an_update)
{
    bench(SLOT_SMALL, 30000u, 0xFFFFFFFFu - 1500u);
    const uint32_t len = 30000u;
    make_image(g_new, len, 2u, 19u, 0u, 0u);
    cp.gate_shut_until_us = g_us + 2000000u;
    CHECK_EQ(update(len, NULL), LINK_UPH_UPDATED);
    CHECK(now_ms() < 100000u);
    check_runs_new(len);
}

TEST_CASE(the_engine_refuses_nothing_to_send)
{
    link_uph_t u;
    link_uph_update(&u, NULL, 100u, 0u);
    CHECK_EQ(u.phase, LINK_UPH_DONE);
    CHECK_EQ(u.result, LINK_UPH_REFUSED);
    link_uph_update(&u, g_new, 0u, 0u);
    CHECK_EQ(u.result, LINK_UPH_REFUSED);
    link_uph_update(&u, g_new, 0x10000u * 64u + 1u, 0u);
    CHECK_EQ(u.result, LINK_UPH_REFUSED);
    link_uph_check(&u, NULL, 1u, 0u);
    CHECK_EQ(u.result, LINK_UPH_REFUSED);
    CHECK_EQ(link_uph_step(NULL, &bus.host, &k_port), LINK_UPH_IDLE);
    link_uph_update(&u, g_new, 100u, 0u);
    CHECK_EQ(link_uph_step(&u, NULL, &k_port), LINK_UPH_PREPARE);
    CHECK_EQ(link_uph_permille(NULL), 0u);
}

/*
 * The v0.18.0 image's size into a slot of the partition table: the
 * exchanges the transfer costs, as docs/Link.md gives them, and the bus time
 * of its frames.
 */
/*
 * Every exchange of both runs, made to go wrong one at a time: what the
 * engine ends with.  A lost request or reply is taken up again; a refusal
 * ends the run with its reason; a word the engine cannot reconcile ends it
 * rather than guessing.
 */
enum { RUN_CHECK, RUN_UPDATE };

/* One request frame lost at block 10: the engine reads where the
 * coprocessor stands (RESYNC), which the RESYNC cases make go wrong. */
static bool g_lost_one;

static void lose_one_request(const link_uph_t *u)
{
    if (!g_lost_one && u->phase == LINK_UPH_SEND && u->block == 10u) {
        bus.drop_request = 0;
        g_lost_one = true;
    }
}

#define RD  LINK_OP_READ
#define WR  LINK_OP_WRITE
#define CTL LINK_PAGE_UPDATE_CTL
#define DAT LINK_PAGE_UPDATE_DATA
#define IDN LINK_PAGE_IDENTITY

typedef struct {
    const char *name;
    int         mode;
    fault_t     f;
    uint8_t     want;
    bool        kept;   /* the new image runs and is bought all the same */
} scenario_t;

#define F(act, ph, pg, o, c) .action = (act), .phase = (ph), .page = (pg), \
                             .op = (o), .cmd = (c)

static const scenario_t k_scenarios[] = {
    /* The check. */
    { .name = "check: the slot read lost",
      .mode = RUN_CHECK,
      .f = { F(F_LOSE, LINK_UPH_CHECK_ASK, CTL, RD, -1) },
      .want = LINK_UPH_DIFFERENT },
    { .name = "check: the slot read refused",
      .mode = RUN_CHECK,
      .f = { F(F_NACK, LINK_UPH_CHECK_ASK, CTL, RD, -1),
        .nack = LINK_NACK_BAD_RANGE },
      .want = LINK_UPH_REFUSED },
    { .name = "check: HASH_RUNNING lost",
      .mode = RUN_CHECK,
      .f = { F(F_LOSE, LINK_UPH_CHECK_ASK, CTL, WR,
               LINK_UPD_CMD_HASH_RUNNING) },
      .want = LINK_UPH_DIFFERENT },
    { .name = "check: HASH_RUNNING's acknowledgement lost, "
              "the hash still running",
      .mode = RUN_CHECK,
      .f = { F(F_DROP_REPLY, LINK_UPH_CHECK_ASK, CTL, WR,
               LINK_UPD_CMD_HASH_RUNNING),
             .ms = LINK_HOST_TIMEOUT_MS + 500u },
      .want = LINK_UPH_DIFFERENT },
    { .name = "check: HASH_RUNNING refused",
      .mode = RUN_CHECK,
      .f = { F(F_NACK, LINK_UPH_CHECK_ASK, CTL, WR, LINK_UPD_CMD_HASH_RUNNING),
        .nack = LINK_NACK_BAD_VALUE },
      .want = LINK_UPH_REFUSED },
    { .name = "check: the hash never ends",
      .mode = RUN_CHECK,
      .f = { F(F_STALL, LINK_UPH_CHECK_ASK, CTL, WR,
               LINK_UPD_CMD_HASH_RUNNING),
        .ms = LINK_UPH_HASH_MS + 2000u },
      .want = LINK_UPH_NO_ANSWER },
    { .name = "check: the answer read lost",
      .mode = RUN_CHECK,
      .f = { F(F_LOSE, LINK_UPH_CHECK_WAIT, CTL, RD, -1) },
      .want = LINK_UPH_DIFFERENT },
    { .name = "check: the answer read refused",
      .mode = RUN_CHECK,
      .f = { F(F_NACK, LINK_UPH_CHECK_WAIT, CTL, RD, -1),
        .nack = LINK_NACK_BAD_RANGE },
      .want = LINK_UPH_REFUSED },
    { .name = "check: a digest of something else",
      .mode = RUN_CHECK,
      .f = { F(F_EDIT, LINK_UPH_CHECK_WAIT, CTL, RD, -1),
        .when = true, .state = LINK_UPD_IDLE, .reg = LINK_UPD_DIGEST_OF,
        .val = LINK_UPD_DIGEST_IMAGE },
      .want = LINK_UPH_REFUSED },

    /* Before the first block. */
    { .name = "update: the slot read lost",
      .mode = RUN_UPDATE,
      .f = { F(F_LOSE, LINK_UPH_PREPARE, CTL, RD, -1) },
      .want = LINK_UPH_UPDATED },
    { .name = "update: the slot read refused",
      .mode = RUN_UPDATE,
      .f = { F(F_NACK, LINK_UPH_PREPARE, CTL, RD, -1),
        .nack = LINK_NACK_BAD_RANGE },
      .want = LINK_UPH_REFUSED },
    { .name = "update: BEGIN lost",
      .mode = RUN_UPDATE,
      .f = { F(F_LOSE, LINK_UPH_BEGIN, CTL, WR, LINK_UPD_CMD_BEGIN) },
      .want = LINK_UPH_UPDATED },

    /* The blocks. */
    { .name = "update: a block's data refused",
      .mode = RUN_UPDATE,
      .f = { F(F_NACK, LINK_UPH_SEND, DAT, WR, -1),
        .nack = LINK_NACK_BAD_RANGE, .skip = 40u },
      .want = LINK_UPH_REFUSED },
    { .name = "update: a COMMIT lost",
      .mode = RUN_UPDATE,
      .f = { F(F_LOSE, LINK_UPH_SEND, CTL, WR, LINK_UPD_CMD_COMMIT),
        .skip = 10u },
      .want = LINK_UPH_UPDATED },
    { .name = "update: a COMMIT's acknowledgement lost",
      .mode = RUN_UPDATE,
      .f = { F(F_DROP_REPLY, LINK_UPH_SEND, CTL, WR, LINK_UPD_CMD_COMMIT),
        .skip = 10u },
      .want = LINK_UPH_UPDATED },
    { .name = "update: the gate shut at the COMMIT that writes a "
              "sector",
      .mode = RUN_UPDATE,
      .f = { F(F_GATE, LINK_UPH_SEND, CTL, WR, LINK_UPD_CMD_COMMIT),
        .skip = 63u, .ms = 1500u },
      .want = LINK_UPH_UPDATED },
    { .name = "update: COMMIT answered BAD_PAGE",
      .mode = RUN_UPDATE,
      .f = { F(F_NACK, LINK_UPH_SEND, CTL, WR, LINK_UPD_CMD_COMMIT),
        .nack = LINK_NACK_BAD_PAGE, .skip = 5u },
      .want = LINK_UPH_NEEDS_USB },
    { .name = "update: the read after a lost request refused",
      .mode = RUN_UPDATE,
      .f = { F(F_NACK, LINK_UPH_RESYNC, CTL, RD, -1),
        .nack = LINK_NACK_BAD_RANGE },
      .want = LINK_UPH_REFUSED },
    { .name = "update: the read after a lost request says the "
              "digest failed",
      .mode = RUN_UPDATE,
      .f = { F(F_EDIT, LINK_UPH_RESYNC, CTL, RD, -1),
        .reg = LINK_UPD_STATE, .val = LINK_UPD_FAILED,
        .two = true, .reg2 = LINK_UPD_ERROR, .val2 = LINK_UPD_ERR_DIGEST },
      .want = LINK_UPH_BAD_DIGEST },
    { .name = "update: the read after a lost request says ACTIVATED",
      .mode = RUN_UPDATE,
      .f = { F(F_EDIT, LINK_UPH_RESYNC, CTL, RD, -1),
        .reg = LINK_UPD_STATE, .val = LINK_UPD_ACTIVATED },
      .want = LINK_UPH_REFUSED },

    /* FINISH. */
    { .name = "update: the digest's data lost",
      .mode = RUN_UPDATE,
      .f = { F(F_LOSE, LINK_UPH_FINISH, DAT, WR, -1) },
      .want = LINK_UPH_UPDATED },
    { .name = "update: the digest's data refused",
      .mode = RUN_UPDATE,
      .f = { F(F_NACK, LINK_UPH_FINISH, DAT, WR, -1),
        .nack = LINK_NACK_BAD_RANGE },
      .want = LINK_UPH_REFUSED },
    { .name = "update: FINISH lost",
      .mode = RUN_UPDATE,
      .f = { F(F_LOSE, LINK_UPH_FINISH, CTL, WR, LINK_UPD_CMD_FINISH) },
      .want = LINK_UPH_UPDATED },
    { .name = "update: FINISH's acknowledgement lost",
      .mode = RUN_UPDATE,
      .f = { F(F_DROP_REPLY, LINK_UPH_FINISH, CTL, WR, LINK_UPD_CMD_FINISH) },
      .want = LINK_UPH_UPDATED },
    { .name = "update: FINISH refused once",
      .mode = RUN_UPDATE,
      .f = { F(F_NACK, LINK_UPH_FINISH, CTL, WR, LINK_UPD_CMD_FINISH),
        .nack = LINK_NACK_BAD_VALUE },
      .want = LINK_UPH_UPDATED },
    { .name = "update: FINISH refused every time",
      .mode = RUN_UPDATE,
      .f = { F(F_NACK, LINK_UPH_FINISH, CTL, WR, LINK_UPD_CMD_FINISH),
        .nack = LINK_NACK_BAD_VALUE, .times = 100u },
      .want = LINK_UPH_REFUSED },

    /* The coprocessor's hash. */
    { .name = "update: the verify read lost",
      .mode = RUN_UPDATE,
      .f = { F(F_LOSE, LINK_UPH_VERIFY, CTL, RD, -1) },
      .want = LINK_UPH_UPDATED },
    { .name = "update: the verify read refused",
      .mode = RUN_UPDATE,
      .f = { F(F_NACK, LINK_UPH_VERIFY, CTL, RD, -1),
        .nack = LINK_NACK_BAD_RANGE },
      .want = LINK_UPH_REFUSED },
    { .name = "update: the coprocessor's hash never ends",
      .mode = RUN_UPDATE,
      .f = { F(F_STALL, LINK_UPH_FINISH, CTL, WR, LINK_UPD_CMD_FINISH),
        .ms = LINK_UPH_HASH_MS + 2000u },
      .want = LINK_UPH_NO_ANSWER },
    { .name = "update: VERIFIED with the digest of the running slot",
      .mode = RUN_UPDATE,
      .f = { F(F_EDIT, LINK_UPH_VERIFY, CTL, RD, -1),
        .when = true, .state = LINK_UPD_VERIFIED, .reg = LINK_UPD_DIGEST_OF,
        .val = LINK_UPD_DIGEST_RUNNING },
      .want = LINK_UPH_BAD_DIGEST },
    { .name = "update: the verify read says RECEIVING",
      .mode = RUN_UPDATE,
      .f = { F(F_EDIT, LINK_UPH_VERIFY, CTL, RD, -1),
        .reg = LINK_UPD_STATE, .val = LINK_UPD_RECEIVING },
      .want = LINK_UPH_REFUSED },

    /* ACTIVATE, the restart and the BUY. */
    { .name = "update: the gate shut at ACTIVATE",
      .mode = RUN_UPDATE,
      .f = { F(F_GATE, LINK_UPH_ACTIVATE, CTL, WR, LINK_UPD_CMD_ACTIVATE),
        .ms = 1500u },
      .want = LINK_UPH_UPDATED },
    { .name = "update: ACTIVATE lost: the old slot answers until "
              "the clock ends it",
      .mode = RUN_UPDATE,
      .f = { F(F_LOSE, LINK_UPH_ACTIVATE, CTL, WR, LINK_UPD_CMD_ACTIVATE) },
      .want = LINK_UPH_NOT_STARTED },
    { .name = "update: the slot read after the restart lost",
      .mode = RUN_UPDATE,
      .f = { F(F_LOSE, LINK_UPH_RESTARTING, CTL, RD, -1) },
      .want = LINK_UPH_UPDATED },
    { .name = "update: BUY lost",
      .mode = RUN_UPDATE,
      .f = { F(F_LOSE, LINK_UPH_BUY, CTL, WR, LINK_UPD_CMD_BUY) },
      .want = LINK_UPH_UPDATED },
    { .name = "update: the read after BUY lost",
      .mode = RUN_UPDATE,
      .f = { F(F_LOSE, LINK_UPH_BUY, CTL, RD, -1) },
      .want = LINK_UPH_UPDATED },
    { .name = "update: BUY refused",
      .mode = RUN_UPDATE,
      .f = { F(F_NACK, LINK_UPH_BUY, CTL, WR, LINK_UPD_CMD_BUY),
        .nack = LINK_NACK_BAD_VALUE },
      .want = LINK_UPH_REFUSED },
    { .name = "update: still on trial after BUY",
      .mode = RUN_UPDATE,
      .f = { F(F_EDIT, LINK_UPH_BUY, CTL, RD, -1),
        .reg = LINK_UPD_SLOT, .val = 1u | LINK_UPD_SLOT_TRIAL },
      .want = LINK_UPH_REFUSED, .kept = true },
};

TEST_CASE(every_exchange_gone_wrong_ends_as_it_should)
{
    const uint32_t len = 30000u;
    for (size_t i = 0; i < sizeof(k_scenarios) / sizeof(k_scenarios[0]);
         ++i) {
        const scenario_t *sc = &k_scenarios[i];
        bench(SLOT_SMALL, 30000u, 0u);
        make_image(g_new, len, 2u, 19u, 0u, 0u);
        g_fault = sc->f;
        if (g_fault.times == 0u) {
            g_fault.times = 1u;
        }
        const unsigned times = g_fault.times;
        g_lost_one = false;
        link_uph_t u;
        uint8_t got;
        if (sc->mode == RUN_CHECK) {
            link_uph_check(&u, g_new, len, now_ms());
            got = run(&u, NULL);
        } else if (sc->f.phase == LINK_UPH_RESYNC) {
            link_uph_update(&u, g_new, len, now_ms());
            got = run(&u, lose_one_request);
        } else {
            link_uph_update(&u, g_new, len, now_ms());
            got = run(&u, NULL);
        }
        if (got != sc->want) {
            T_FAIL("%s: ended %u, not %u (phase %u, error %u)", sc->name,
                   got, sc->want, (unsigned)u.phase, u.error);
        }
        if (g_fault.times == times) {
            T_FAIL("%s: the fault never fired", sc->name);
        }
        /* An update that ended any other way leaves the old image. */
        if (sc->mode == RUN_UPDATE && got != LINK_UPH_UPDATED
            && got != LINK_UPH_NO_ANSWER && !sc->kept) {
            advance_us((uint64_t)(TBYB_MS + 1000u) * 1000u);
            dead_watchdog();
            advance_us((uint64_t)(BOOT_MS + 100u) * 1000u);
            if (!cp.up || cp.running != 0u || cp.trial) {
                T_FAIL("%s: the old image does not run", sc->name);
            }
            check_runs_old();
        }
        if (sc->mode == RUN_UPDATE && (got == LINK_UPH_UPDATED || sc->kept)) {
            check_runs_new(len);
        }
    }
}

TEST_CASE(the_engine_takes_no_null_and_an_idle_step_does_nothing)
{
    link_uph_check(NULL, g_new, 100u, 0u);
    link_uph_update(NULL, g_new, 100u, 0u);
    link_uph_confirm(NULL, 0u);
    CHECK_EQ(link_uph_permille(NULL), 0u);
    CHECK_EQ(link_uph_step(NULL, &bus.host, &k_port), LINK_UPH_IDLE);

    bench(SLOT_SMALL, 30000u, 0u);
    link_uph_t u;
    memset(&u, 0, sizeof(u));
    const unsigned before = bus.exchanges;
    CHECK_EQ(link_uph_step(&u, &bus.host, &k_port), LINK_UPH_IDLE);
    CHECK_EQ(link_uph_step(&u, NULL, &k_port), LINK_UPH_IDLE);
    CHECK_EQ(link_uph_step(&u, &bus.host, NULL), LINK_UPH_IDLE);
    CHECK_EQ(bus.exchanges, before);
}

TEST_CASE(a_full_size_update_costs_what_the_docs_say)
{
    bench(SLOT_FULL, IMAGE_V018, 0u);
    make_image(g_new, IMAGE_V018, 2u, 19u, 0u, 0u);
    link_uph_t u;
    link_uph_update(&u, g_new, IMAGE_V018, now_ms());
    unsigned before = 0u;
    uint16_t last = 0u;
    while (u.phase != LINK_UPH_DONE) {
        (void)link_uph_step(&u, &bus.host, &k_port);
        advance_us(200u);
        if (u.phase == LINK_UPH_SEND && before == 0u) {
            before = bus.exchanges;
        }
        if (link_uph_permille(&u) < last) {
            T_FAIL("progress went back from %u", last);
        }
        last = link_uph_permille(&u);
    }
    CHECK_EQ(u.result, LINK_UPH_UPDATED);
    check_runs_new(IMAGE_V018);
    const unsigned blocks = (IMAGE_V018 + 63u) / 64u;
    CHECK_EQ(blocks, 2889u);
    CHECK_EQ(bus.commits, blocks);
    CHECK_EQ(bus.data_writes, blocks * 8u + 4u);   /* and the digest */
    /* 46 sectors: 45 written as they fill, the first at ACTIVATE. */
    CHECK_EQ(cp.programs, 46u);
    CHECK_EQ(cp.sector0_programs, 1u);
    printf("      %u exchanges, %u request frames, %.1f s simulated\n",
           bus.exchanges, bus.request_frames, (double)g_us / 1e6);
    CHECK(bus.exchanges >= blocks * 9u);
    CHECK(bus.exchanges < blocks * 9u + 400u);
}

int main(void)
{
    RUN(the_flag_is_found_in_the_last_image_def_of_the_loop);
    RUN(no_flag_without_a_closed_loop_holding_an_image_def);
    RUN(the_build_hash_ignores_the_flag_and_nothing_else);
    RUN(the_control_page_takes_commands_and_nothing_else);
    RUN(begin_takes_one_byte_to_exactly_one_slot);
    RUN(blocks_go_in_strictly_in_order_and_a_repeat_is_a_no_op);
    RUN(a_shut_gate_refuses_every_flash_write_with_busy);
    RUN(arm_is_refused_for_the_whole_session_and_disarm_never);
    RUN(link_silence_ends_a_session_and_nothing_shorter_does);
    RUN(no_partition_table_means_no_slots);
    RUN(an_update_moves_the_coprocessor_to_the_image_and_buys_it);
    RUN(image_sizes_at_the_limits);
    RUN(an_image_one_byte_over_a_slot_needs_usb);
    RUN(the_slot_is_not_startable_before_activate);
    RUN(a_corrupted_block_is_sent_again);
    RUN(a_block_that_never_passes_its_crc_ends_the_update);
    RUN(a_lost_acknowledgement_resumes_without_a_restart);
    RUN(a_coprocessor_restart_mid_transfer_starts_again_from_block_0);
    RUN(restarts_without_end_give_up);
    RUN(a_link_drop_mid_transfer);
    RUN(arm_attempted_through_an_update_is_refused);
    RUN(a_shut_gate_is_waited_for_and_then_given_up_on);
    RUN(a_flash_fault_is_caught_by_the_digest);
    RUN(a_first_sector_that_reads_back_wrong_is_not_started);
    RUN(an_erase_that_fails_ends_or_restarts_the_session);
    RUN(an_image_never_bought_goes_back_to_the_old_one);
    RUN(an_image_found_on_trial_is_bought_at_link_up);
    RUN(a_new_image_that_never_answers_is_rolled_back);
    RUN(a_buy_the_boot_rom_refuses_is_reported);
    RUN(an_old_coprocessor_needs_usb);
    RUN(another_minor_or_major_still_updates);
    RUN(the_clock_may_wrap_during_an_update);
    RUN(the_engine_refuses_nothing_to_send);
    RUN(every_exchange_gone_wrong_ends_as_it_should);
    RUN(the_engine_takes_no_null_and_an_idle_step_does_nothing);
    RUN(a_full_size_update_costs_what_the_docs_say);
    return test_summary("link_update");
}
