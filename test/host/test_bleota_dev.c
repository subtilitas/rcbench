/**
 * @file test_bleota_dev.c
 * @brief The device's side of a bleota transfer (bleota_dev_t): BEGIN's
 *        refusals before anything opens (kind, size against the slot,
 *        the running version, protocol and flags, a BEGIN while busy), the
 *        confirmation of both kinds at 999ms and 1,000ms with a stuck key
 *        and the 30s window, the buffer's order with a skipped chunk
 *        answered once, a repeat, an overlap and a chunk past the limit,
 *        the credit steps and their gap mark, the image head checked
 *        before the first write, END early, the SHA-256 compare, the
 *        install and the restart (also after an ABORT or a lost link
 *        while it runs), the idle timeout, a failed open or write and a
 *        lost link.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"
#include "bleota.h"
#include "bleota_head.h"

/** @brief The slot of the tests' device, bytes. */
#define SLOT 4096u

/** @brief A device, its buffer and what its caller did. */
typedef struct {
    bleota_dev_t d;                 /**< the device */
    uint8_t      ring[BLEOTA_WINDOW]; /**< its buffer */
    uint8_t      flash[SLOT];       /**< what was written */
    uint32_t     flash_n;           /**< how many bytes */
} devsim_t;

/** @brief The tests' version. */
static const uint16_t k_ver[3] = { 0u, 24u, 0u };

/** @brief The head of the tests' images: a power node's on the ESP32-C6. */
static uint8_t s_head[BLEOTA_HEAD_LEN];

/**
 * @brief A byte of the tests' image of pattern @p v: the head, then the
 *        pattern.
 * @param v   the pattern's start
 * @param off the offset
 * @return the byte
 */
static uint8_t img_at(uint8_t v, uint32_t off)
{
    if (s_head[0] != BLEOTA_IMG_MAGIC) {
        test_head_write(s_head, TEST_CHIP_C6, BLEOTA_KIND_NODE_POWER);
    }
    return off < BLEOTA_HEAD_LEN ? s_head[off] : (uint8_t)(v + off);
}

/**
 * @brief Set up a power node of SLOT bytes running k_ver.
 * @param t       the device
 * @param confirm BLEOTA_CONFIRM_*
 */
static void dev_init(devsim_t *t, uint8_t confirm)
{
    memset(t, 0, sizeof *t);
    const bleota_dev_cfg_t cfg = { .kind = BLEOTA_KIND_NODE_POWER,
                                   .confirm = confirm, .slot = SLOT,
                                   .ver = { 0u, 24u, 0u },
                                   .chip = TEST_CHIP_C6 };
    CHECK(bleota_dev_init(&t->d, &cfg, t->ring, sizeof t->ring));
}

/**
 * @brief Send BEGIN.
 * @param t     the device
 * @param kind  its kind
 * @param size  the image's size
 * @param ver   its version
 * @param flags BLEOTA_F_*
 * @param now   the clock
 */
static void send_begin(devsim_t *t, uint8_t kind, uint32_t size,
                       const uint16_t ver[3], uint8_t flags, uint32_t now)
{
    uint8_t b[BLEOTA_LEN_BEGIN];
    (void)bleota_begin_encode(kind, size, ver, flags, b);
    bleota_dev_ctrl(&t->d, b, sizeof b, now);
}

/**
 * @brief Send both HASH halves of @p sha.
 * @param t   the device
 * @param sha the SHA-256
 * @param now the clock
 */
static void send_hash(devsim_t *t, const uint8_t sha[32], uint32_t now)
{
    uint8_t b[BLEOTA_LEN_HASH];
    for (uint8_t p = 0; p < 2u; ++p) {
        (void)bleota_hash_encode(sha, p, b);
        bleota_dev_ctrl(&t->d, b, sizeof b, now);
    }
}

/**
 * @brief Send a one-byte op.
 * @param t   the device
 * @param op  BLEOTA_OP_*
 * @param now the clock
 */
static void send_op(devsim_t *t, uint8_t op, uint32_t now)
{
    uint8_t b[1];
    (void)bleota_op_encode(op, b);
    bleota_dev_ctrl(&t->d, b, 1u, now);
}

/**
 * @brief The next notification.
 * @param t the device
 * @param s it, decoded
 * @return false when none waits
 */
static bool next_status(devsim_t *t, bleota_status_t *s)
{
    uint8_t b[BLEOTA_LEN_STATUS];
    return bleota_dev_status(&t->d, b) && bleota_status_decode(b, sizeof b, s);
}

/**
 * @brief Send a chunk of @p n bytes of value @p v at @p off.
 * @param t   the device
 * @param off its offset
 * @param n   its image bytes
 * @param v   their value, plus the index
 * @param now the clock
 */
static void send_chunk(devsim_t *t, uint32_t off, uint16_t n, uint8_t v,
                       uint32_t now)
{
    uint8_t b[BLEOTA_CHUNK_HDR + BLEOTA_CHUNK_MAX];
    bleota_chunk_header(off, b);
    for (uint16_t i = 0; i < n; ++i) {
        b[BLEOTA_CHUNK_HDR + i] = img_at(v, off + i);
    }
    bleota_dev_data(&t->d, b, BLEOTA_CHUNK_HDR + n, now);
}

/**
 * @brief Run the caller's writes until none is due.
 * @param t   the device
 * @param now the clock
 */
static void drain(devsim_t *t, uint32_t now)
{
    uint8_t buf[300];
    while (bleota_dev_poll(&t->d, now) == BLEOTA_DO_WRITE) {
        const uint32_t n = bleota_dev_take(&t->d, buf, sizeof buf);
        memcpy(t->flash + t->flash_n, buf, n);
        t->flash_n += n;
        bleota_dev_written(&t->d, true, now);
    }
}

/**
 * @brief A device taken to RECV with a confirmed BEGIN of @p size bytes.
 * @param t    the device
 * @param size the image's size
 * @param sha  its SHA-256
 */
static void to_recv(devsim_t *t, uint32_t size, const uint8_t sha[32])
{
    const uint16_t v[3] = { 0u, 25u, 0u };
    bleota_status_t s = { 0 };
    dev_init(t, BLEOTA_CONFIRM_HOLD);
    send_begin(t, BLEOTA_KIND_NODE_POWER, size, v, 0u, 0u);
    send_hash(t, sha, 0u);
    CHECK(next_status(t, &s));
    CHECK_EQ(s.ev, BLEOTA_EV_WAIT);
    bleota_dev_key(&t->d, true, 10u);
    bleota_dev_key(&t->d, true, 1010u);
    CHECK_EQ(bleota_dev_poll(&t->d, 1010u), BLEOTA_DO_OPEN);
    bleota_dev_opened(&t->d, true, 1010u);
    CHECK(next_status(t, &s));
    CHECK_EQ(s.ev, BLEOTA_EV_CREDIT);
    CHECK_EQ(s.next, 0u);
    CHECK_EQ(s.limit, size < BLEOTA_WINDOW ? size : BLEOTA_WINDOW);
}

TEST_CASE(init_refuses_a_small_or_uneven_buffer_and_a_kind_out_of_range)
{
    bleota_dev_t d;
    uint8_t ring[2u * BLEOTA_WINDOW];
    bleota_dev_cfg_t cfg = { .kind = BLEOTA_KIND_DIAL, .slot = 1u };
    CHECK(bleota_dev_init(&d, &cfg, ring, BLEOTA_WINDOW));
    CHECK(bleota_dev_init(&d, &cfg, ring, 2u * BLEOTA_WINDOW));
    CHECK(!bleota_dev_init(&d, &cfg, ring, BLEOTA_WINDOW / 2u));
    CHECK(!bleota_dev_init(&d, &cfg, ring, BLEOTA_WINDOW + 4u));
    CHECK(!bleota_dev_init(&d, &cfg, NULL, BLEOTA_WINDOW));
    cfg.kind = 0u;
    CHECK(!bleota_dev_init(&d, &cfg, ring, BLEOTA_WINDOW));
    cfg.kind = BLEOTA_KIND_MAX + 1u;
    CHECK(!bleota_dev_init(&d, &cfg, ring, BLEOTA_WINDOW));
}

TEST_CASE(begin_is_refused_for_kind_size_version_protocol_and_flags)
{
    devsim_t t;
    bleota_status_t s = { 0 };
    const uint16_t newer[3] = { 0u, 25u, 0u };
    struct {
        uint8_t kind; uint32_t size; const uint16_t *ver; uint8_t flags;
        uint8_t code;
    } k[] = {
        { BLEOTA_KIND_DIAL, 100u, newer, 0u, BLEOTA_E_KIND },
        { BLEOTA_KIND_NODE_BAL, 100u, newer, 0u, BLEOTA_E_KIND },
        { BLEOTA_KIND_NODE_POWER, 0u, newer, 0u, BLEOTA_E_SIZE },
        { BLEOTA_KIND_NODE_POWER, SLOT + 1u, newer, 0u, BLEOTA_E_SIZE },
        { BLEOTA_KIND_NODE_POWER, 100u, k_ver, 0u, BLEOTA_E_SAME },
        { BLEOTA_KIND_NODE_POWER, 100u, newer, 0x02u, BLEOTA_E_PROTO },
    };
    for (unsigned i = 0; i < sizeof k / sizeof k[0]; ++i) {
        dev_init(&t, BLEOTA_CONFIRM_PRESS);
        send_begin(&t, k[i].kind, k[i].size, k[i].ver, k[i].flags, 0u);
        CHECK(next_status(&t, &s));
        CHECK_EQ(s.ev, BLEOTA_EV_FAIL);
        CHECK_EQ(s.code, k[i].code);
        CHECK_EQ(t.d.st, BLEOTA_DEV_IDLE);
        CHECK_EQ(bleota_dev_poll(&t.d, 1u), BLEOTA_DO_NONE);
    }
    /* Another protocol version. */
    dev_init(&t, BLEOTA_CONFIRM_PRESS);
    uint8_t b[BLEOTA_LEN_BEGIN];
    (void)bleota_begin_encode(BLEOTA_KIND_NODE_POWER, 100u, newer, 0u, b);
    b[1] = BLEOTA_PROTO + 1u;
    bleota_dev_ctrl(&t.d, b, sizeof b, 0u);
    CHECK(next_status(&t, &s));
    CHECK_EQ(s.code, BLEOTA_E_PROTO);
    /* The slot exactly, and the running version with the flag. */
    dev_init(&t, BLEOTA_CONFIRM_PRESS);
    send_begin(&t, BLEOTA_KIND_NODE_POWER, SLOT, newer, 0u, 0u);
    CHECK(!next_status(&t, &s));
    CHECK_EQ(t.d.st, BLEOTA_DEV_HASH);
    dev_init(&t, BLEOTA_CONFIRM_PRESS);
    send_begin(&t, BLEOTA_KIND_NODE_POWER, 100u, k_ver, BLEOTA_F_REINSTALL,
               0u);
    CHECK_EQ(t.d.st, BLEOTA_DEV_HASH);
    /* A second BEGIN is answered busy and changes nothing. */
    send_begin(&t, BLEOTA_KIND_NODE_POWER, 200u, newer, 0u, 1u);
    CHECK(next_status(&t, &s));
    CHECK_EQ(s.code, BLEOTA_E_BUSY);
    CHECK_EQ(t.d.st, BLEOTA_DEV_HASH);
    CHECK_EQ(t.d.size, 100u);
}

TEST_CASE(the_hash_halves_open_the_window_and_writes_out_of_order_fail)
{
    devsim_t t;
    bleota_status_t s = { 0 };
    uint8_t sha[32] = { 1 };
    dev_init(&t, BLEOTA_CONFIRM_PRESS);
    send_hash(&t, sha, 0u);
    CHECK(next_status(&t, &s));
    CHECK_EQ(s.code, BLEOTA_E_STATE);
    CHECK(next_status(&t, &s));
    CHECK_EQ(s.code, BLEOTA_E_STATE);
    CHECK(!next_status(&t, &s));
    const uint16_t v[3] = { 1u, 0u, 0u };
    send_begin(&t, BLEOTA_KIND_NODE_POWER, 100u, v, 0u, 0u);
    uint8_t b[BLEOTA_LEN_HASH];
    (void)bleota_hash_encode(sha, 1u, b);
    bleota_dev_ctrl(&t.d, b, sizeof b, 1u);
    bleota_dev_ctrl(&t.d, b, sizeof b, 2u);
    CHECK_EQ(t.d.st, BLEOTA_DEV_HASH);
    (void)bleota_hash_encode(sha, 0u, b);
    bleota_dev_ctrl(&t.d, b, sizeof b, 3u);
    CHECK_EQ(t.d.st, BLEOTA_DEV_CONFIRM);
    CHECK(next_status(&t, &s));
    CHECK_EQ(s.ev, BLEOTA_EV_WAIT);
    CHECK_EQ(s.secs, 30);
    CHECK_EQ(bleota_dev_secs(&t.d, 3u), 30);
    CHECK_EQ(bleota_dev_secs(&t.d, 3u + 29001u), 1);
    CHECK(memcmp(t.d.sha, sha, 32) == 0);
    /* END in CONFIRM. */
    send_op(&t, BLEOTA_OP_END, 4u);
    CHECK(next_status(&t, &s));
    CHECK_EQ(s.code, BLEOTA_E_STATE);
    /* The halves are due within 5,000ms of BEGIN. */
    dev_init(&t, BLEOTA_CONFIRM_PRESS);
    send_begin(&t, BLEOTA_KIND_NODE_POWER, 100u, v, 0u, 100u);
    (void)bleota_dev_poll(&t.d, 100u + BLEOTA_HASH_MS - 1u);
    CHECK_EQ(t.d.st, BLEOTA_DEV_HASH);
    (void)bleota_dev_poll(&t.d, 100u + BLEOTA_HASH_MS);
    CHECK(next_status(&t, &s));
    CHECK_EQ(s.code, BLEOTA_E_TIMEOUT);
}

TEST_CASE(a_press_accepts_under_1000ms_and_a_hold_refuses)
{
    devsim_t t;
    bleota_status_t s = { 0 };
    const uint16_t v[3] = { 1u, 0u, 0u };
    const uint8_t sha[32] = { 0 };
    /* Down 999ms, then up: accepted. */
    dev_init(&t, BLEOTA_CONFIRM_PRESS);
    send_begin(&t, BLEOTA_KIND_NODE_POWER, 100u, v, 0u, 0u);
    send_hash(&t, sha, 0u);
    (void)next_status(&t, &s);
    bleota_dev_key(&t.d, true, 100u);
    bleota_dev_key(&t.d, true, 1098u);
    bleota_dev_key(&t.d, false, 1099u);
    CHECK_EQ(t.d.st, BLEOTA_DEV_OPEN);
    /* Held to 1,000ms: refused while still down. */
    dev_init(&t, BLEOTA_CONFIRM_PRESS);
    send_begin(&t, BLEOTA_KIND_NODE_POWER, 100u, v, 0u, 0u);
    send_hash(&t, sha, 0u);
    (void)next_status(&t, &s);
    bleota_dev_key(&t.d, true, 100u);
    bleota_dev_key(&t.d, true, 1099u);
    CHECK_EQ(t.d.st, BLEOTA_DEV_CONFIRM);
    bleota_dev_key(&t.d, true, 1100u);
    CHECK_EQ(t.d.st, BLEOTA_DEV_IDLE);
    CHECK(next_status(&t, &s));
    CHECK_EQ(s.code, BLEOTA_E_REFUSED);
    bleota_dev_key(&t.d, false, 1200u);
    CHECK_EQ(t.d.st, BLEOTA_DEV_IDLE);
    CHECK_EQ(bleota_dev_poll(&t.d, 1200u), BLEOTA_DO_NONE);
}

TEST_CASE(a_hold_of_1000ms_accepts_a_node_and_a_stuck_key_does_not)
{
    devsim_t t;
    bleota_status_t s = { 0 };
    const uint16_t v[3] = { 1u, 0u, 0u };
    const uint8_t sha[32] = { 0 };
    dev_init(&t, BLEOTA_CONFIRM_HOLD);
    send_begin(&t, BLEOTA_KIND_NODE_POWER, 100u, v, 0u, 0u);
    send_hash(&t, sha, 0u);
    (void)next_status(&t, &s);
    bleota_dev_key(&t.d, true, 50u);
    bleota_dev_key(&t.d, true, 1049u);
    CHECK_EQ(t.d.st, BLEOTA_DEV_CONFIRM);
    bleota_dev_key(&t.d, false, 1049u);  /* a press is no hold */
    CHECK_EQ(t.d.st, BLEOTA_DEV_CONFIRM);
    bleota_dev_key(&t.d, true, 2000u);
    bleota_dev_key(&t.d, true, 3000u);
    CHECK_EQ(t.d.st, BLEOTA_DEV_OPEN);
    /* A key down before the window opens confirms nothing until it was
     * let go. */
    dev_init(&t, BLEOTA_CONFIRM_HOLD);
    bleota_dev_key(&t.d, true, 0u);
    send_begin(&t, BLEOTA_KIND_NODE_POWER, 100u, v, 0u, 0u);
    send_hash(&t, sha, 0u);
    bleota_dev_key(&t.d, true, 5000u);
    CHECK_EQ(t.d.st, BLEOTA_DEV_CONFIRM);
    bleota_dev_key(&t.d, false, 5005u);
    bleota_dev_key(&t.d, true, 5010u);
    bleota_dev_key(&t.d, true, 6010u);
    CHECK_EQ(t.d.st, BLEOTA_DEV_OPEN);
}

TEST_CASE(no_confirmation_in_30s_refuses_and_opens_nothing)
{
    devsim_t t;
    bleota_status_t s = { 0 };
    const uint16_t v[3] = { 1u, 0u, 0u };
    const uint8_t sha[32] = { 0 };
    dev_init(&t, BLEOTA_CONFIRM_PRESS);
    send_begin(&t, BLEOTA_KIND_NODE_POWER, 100u, v, 0u, 10u);
    send_hash(&t, sha, 10u);
    (void)next_status(&t, &s);
    CHECK_EQ(bleota_dev_poll(&t.d, 10u + BLEOTA_CONFIRM_MS - 1u),
             BLEOTA_DO_NONE);
    CHECK_EQ(t.d.st, BLEOTA_DEV_CONFIRM);
    CHECK_EQ(bleota_dev_poll(&t.d, 10u + BLEOTA_CONFIRM_MS), BLEOTA_DO_NONE);
    CHECK(next_status(&t, &s));
    CHECK_EQ(s.code, BLEOTA_E_NO_CONFIRM);
    CHECK(!t.d.open);
    CHECK_EQ(bleota_dev_secs(&t.d, 10u), 0);
}

TEST_CASE(chunks_go_in_order_and_a_skipped_one_is_answered_once)
{
    devsim_t t;
    bleota_status_t s = { 0 };
    uint8_t sha[32] = { 0 };
    to_recv(&t, 1000u, sha);
    send_chunk(&t, 0u, 200u, 7u, 1100u);
    CHECK_EQ(t.d.rx, 200u);
    /* 200..399 lost: 400 skips ahead, answered with 200 once. */
    send_chunk(&t, 400u, 200u, 7u, 1101u);
    send_chunk(&t, 600u, 200u, 7u, 1102u);
    CHECK_EQ(t.d.rx, 200u);
    CHECK_EQ(t.d.gaps, 2u);
    CHECK(next_status(&t, &s));
    CHECK_EQ(s.ev, BLEOTA_EV_CREDIT);
    CHECK_EQ(s.next, 200u);
    CHECK(!next_status(&t, &s));
    /* A repeat of a taken chunk, then an overlap that brings new bytes. */
    send_chunk(&t, 0u, 200u, 7u, 1103u);
    CHECK_EQ(t.d.dups, 1u);
    send_chunk(&t, 100u, 200u, 7u, 1104u);
    CHECK_EQ(t.d.rx, 300u);
    send_chunk(&t, 300u, 240u, 7u, 1105u);
    send_chunk(&t, 540u, 240u, 7u, 1106u);
    send_chunk(&t, 780u, 220u, 7u, 1107u);
    CHECK_EQ(t.d.rx, 1000u);
    drain(&t, 1108u);
    CHECK_EQ(t.flash_n, 1000u);
    for (uint32_t i = 0; i < 1000u; ++i) {
        CHECK_EQ(t.flash[i], img_at(7u, i));
    }
    CHECK_EQ(bleota_dev_percent(&t.d), 100u);
}

TEST_CASE(a_chunk_past_the_limit_is_dropped_and_credits_step_by_512)
{
    devsim_t t;
    bleota_status_t s = { 0 };
    uint8_t sha[32] = { 0 };
    to_recv(&t, SLOT, sha);
    /* Up to the window of 2,048 bytes without a write; one byte more is
     * past the limit. */
    uint32_t off = 0;
    while (off + 240u <= BLEOTA_WINDOW) {
        send_chunk(&t, off, 240u, 1u, 1100u);
        off += 240u;
    }
    send_chunk(&t, off, (uint16_t)(BLEOTA_WINDOW - off), 1u, 1100u);
    CHECK_EQ(t.d.rx, BLEOTA_WINDOW);
    send_chunk(&t, BLEOTA_WINDOW, 1u, 1u, 1100u);
    CHECK_EQ(t.d.over, 1u);
    CHECK_EQ(t.d.rx, BLEOTA_WINDOW);
    /* 511 bytes written move the limit too little; the 512th does. */
    uint8_t buf[512];
    CHECK_EQ(bleota_dev_poll(&t.d, 1101u), BLEOTA_DO_WRITE);
    CHECK_EQ(bleota_dev_take(&t.d, buf, 511u), 511u);
    bleota_dev_written(&t.d, true, 1101u);
    CHECK(!next_status(&t, &s));
    CHECK_EQ(bleota_dev_take(&t.d, buf, 1u), 1u);
    bleota_dev_written(&t.d, true, 1102u);
    CHECK(next_status(&t, &s));
    CHECK_EQ(s.next, BLEOTA_WINDOW);
    CHECK_EQ(s.limit, 512u + BLEOTA_WINDOW);
    /* POLL answers with the same. */
    send_op(&t, BLEOTA_OP_POLL, 1103u);
    CHECK(next_status(&t, &s));
    CHECK_EQ(s.limit, 512u + BLEOTA_WINDOW);
}

TEST_CASE(end_compares_the_sha_and_installs_only_on_a_match)
{
    devsim_t t;
    bleota_status_t s = { 0 };
    uint8_t sha[32];
    memset(sha, 0x11, sizeof sha);
    to_recv(&t, 300u, sha);
    send_chunk(&t, 0u, 240u, 0u, 1100u);
    /* END before the last byte. */
    send_op(&t, BLEOTA_OP_END, 1101u);
    CHECK(next_status(&t, &s));
    CHECK_EQ(s.code, BLEOTA_E_SHORT);
    CHECK_EQ(bleota_dev_poll(&t.d, 1101u), BLEOTA_DO_CLOSE);
    bleota_dev_closed(&t.d);
    CHECK(!t.d.open);

    /* Whole, END, then a digest that differs in its last byte. */
    to_recv(&t, 300u, sha);
    send_chunk(&t, 0u, 240u, 0u, 1100u);
    send_chunk(&t, 240u, 60u, 0u, 1100u);
    send_op(&t, BLEOTA_OP_END, 1101u);
    drain(&t, 1102u);
    while (next_status(&t, &s)) {
    }
    CHECK_EQ(bleota_dev_poll(&t.d, 1102u), BLEOTA_DO_VERIFY);
    uint8_t other[32];
    memcpy(other, sha, sizeof other);
    other[31] ^= 1u;
    CHECK(!bleota_dev_verify(&t.d, other, 1103u));
    CHECK(next_status(&t, &s));
    CHECK_EQ(s.code, BLEOTA_E_SHA);
    CHECK_EQ(bleota_dev_poll(&t.d, 1103u), BLEOTA_DO_CLOSE);
    bleota_dev_closed(&t.d);
    CHECK_EQ(bleota_dev_poll(&t.d, 1103u), BLEOTA_DO_NONE);

    /* The same with the right digest: install, DONE, restart. */
    to_recv(&t, 300u, sha);
    send_chunk(&t, 0u, 240u, 0u, 1100u);
    send_chunk(&t, 240u, 60u, 0u, 1100u);
    send_op(&t, BLEOTA_OP_END, 1101u);
    drain(&t, 1102u);
    while (next_status(&t, &s)) {
    }
    CHECK_EQ(bleota_dev_poll(&t.d, 1102u), BLEOTA_DO_VERIFY);
    CHECK(bleota_dev_verify(&t.d, sha, 1103u));
    CHECK_EQ(bleota_dev_poll(&t.d, 1103u), BLEOTA_DO_INSTALL);
    bleota_dev_installed(&t.d, true, BLEOTA_OK, 1104u);
    CHECK(next_status(&t, &s));
    CHECK_EQ(s.ev, BLEOTA_EV_DONE);
    CHECK_EQ(bleota_dev_poll(&t.d, 1104u), BLEOTA_DO_RESTART);
    bleota_dev_lost(&t.d);
    CHECK_EQ(bleota_dev_poll(&t.d, 1105u), BLEOTA_DO_RESTART);

    /* The device's own check refuses the image. */
    to_recv(&t, 300u, sha);
    send_chunk(&t, 0u, 240u, 0u, 1100u);
    send_chunk(&t, 240u, 60u, 0u, 1100u);
    send_op(&t, BLEOTA_OP_END, 1101u);
    drain(&t, 1102u);
    (void)bleota_dev_poll(&t.d, 1102u);
    CHECK(bleota_dev_verify(&t.d, sha, 1103u));
    bleota_dev_installed(&t.d, false, BLEOTA_E_IMAGE, 1104u);
    while (next_status(&t, &s) && s.ev != BLEOTA_EV_FAIL) {
    }
    CHECK_EQ(s.code, BLEOTA_E_IMAGE);
    CHECK_EQ(bleota_dev_poll(&t.d, 1104u), BLEOTA_DO_NONE);
}

TEST_CASE(a_silent_central_a_failed_write_and_a_lost_link_end_it)
{
    devsim_t t;
    bleota_status_t s = { 0 };
    uint8_t sha[32] = { 0 };
    to_recv(&t, 1000u, sha);
    send_chunk(&t, 0u, 100u, 0u, 2000u);
    drain(&t, 2000u);
    CHECK_EQ(bleota_dev_poll(&t.d, 2000u + BLEOTA_IDLE_MS - 1u),
             BLEOTA_DO_NONE);
    CHECK_EQ(bleota_dev_poll(&t.d, 2000u + BLEOTA_IDLE_MS), BLEOTA_DO_CLOSE);
    while (next_status(&t, &s) && s.ev != BLEOTA_EV_FAIL) {
    }
    CHECK_EQ(s.code, BLEOTA_E_TIMEOUT);

    to_recv(&t, 1000u, sha);
    send_chunk(&t, 0u, 240u, 0u, 1100u);
    send_chunk(&t, 240u, 60u, 0u, 1100u);
    uint8_t buf[100];
    CHECK_EQ(bleota_dev_poll(&t.d, 1100u), BLEOTA_DO_WRITE);
    CHECK_EQ(bleota_dev_take(&t.d, buf, sizeof buf), 100u);
    bleota_dev_written(&t.d, false, 1100u);
    while (next_status(&t, &s) && s.ev != BLEOTA_EV_FAIL) {
    }
    CHECK_EQ(s.code, BLEOTA_E_FLASH);
    CHECK_EQ(bleota_dev_poll(&t.d, 1100u), BLEOTA_DO_CLOSE);

    to_recv(&t, 1000u, sha);
    send_chunk(&t, 0u, 100u, 0u, 1100u);
    bleota_dev_lost(&t.d);
    CHECK_EQ(t.d.st, BLEOTA_DEV_IDLE);
    CHECK(!next_status(&t, &s));
    CHECK_EQ(bleota_dev_poll(&t.d, 1100u), BLEOTA_DO_CLOSE);
    bleota_dev_closed(&t.d);
    /* A new BEGIN runs from the first byte. */
    const uint16_t v[3] = { 0u, 25u, 0u };
    send_begin(&t, BLEOTA_KIND_NODE_POWER, 1000u, v, 0u, 1200u);
    CHECK_EQ(t.d.st, BLEOTA_DEV_HASH);
    CHECK_EQ(t.d.rx, 0u);

    /* The slot does not open. */
    dev_init(&t, BLEOTA_CONFIRM_HOLD);
    send_begin(&t, BLEOTA_KIND_NODE_POWER, 1000u, v, 0u, 0u);
    send_hash(&t, sha, 0u);
    bleota_dev_key(&t.d, true, 0u);
    bleota_dev_key(&t.d, true, 1000u);
    bleota_dev_opened(&t.d, false, 1000u);
    while (next_status(&t, &s) && s.ev != BLEOTA_EV_FAIL) {
    }
    CHECK_EQ(s.code, BLEOTA_E_FLASH);
    CHECK_EQ(bleota_dev_poll(&t.d, 1001u), BLEOTA_DO_NONE);

    /* ABORT while taking chunks. */
    to_recv(&t, 1000u, sha);
    send_op(&t, BLEOTA_OP_ABORT, 1100u);
    while (next_status(&t, &s) && s.ev != BLEOTA_EV_FAIL) {
    }
    CHECK_EQ(s.code, BLEOTA_E_ABORTED);
    CHECK_EQ(bleota_dev_poll(&t.d, 1100u), BLEOTA_DO_CLOSE);
}

TEST_CASE(the_buffer_wraps_over_a_whole_slot)
{
    devsim_t t;
    uint8_t sha[32] = { 0 };
    to_recv(&t, SLOT, sha);
    uint32_t off = 0;
    while (off < SLOT) {
        const uint16_t n = (uint16_t)(SLOT - off < 233u ? SLOT - off : 233u);
        send_chunk(&t, off, n, 3u, 1100u);
        drain(&t, 1100u);
        off += n;
    }
    CHECK_EQ(t.flash_n, SLOT);
    for (uint32_t i = 0; i < SLOT; ++i) {
        CHECK_EQ(t.flash[i], img_at(3u, i));
    }
}

TEST_CASE(a_full_notification_queue_drops_the_oldest_and_nulls_are_safe)
{
    devsim_t t;
    bleota_status_t s = { 0 };
    uint8_t sha[32] = { 0 };
    to_recv(&t, 1000u, sha);
    for (unsigned i = 0; i < BLEOTA_DEV_OUT + 2u; ++i) {
        send_op(&t, BLEOTA_OP_POLL, 1100u + i);
    }
    unsigned n = 0;
    while (next_status(&t, &s)) {
        ++n;
    }
    CHECK_EQ(n, BLEOTA_DEV_OUT);
    bleota_dev_ctrl(NULL, NULL, 0u, 0u);
    bleota_dev_data(NULL, NULL, 0u, 0u);
    bleota_dev_key(NULL, true, 0u);
    bleota_dev_lost(NULL);
    bleota_dev_closed(NULL);
    CHECK_EQ(bleota_dev_poll(NULL, 0u), BLEOTA_DO_NONE);
    CHECK_EQ(bleota_dev_take(NULL, NULL, 0u), 0u);
    CHECK(!bleota_dev_verify(NULL, sha, 0u));
    CHECK_EQ(bleota_dev_percent(NULL), 0u);
    CHECK_EQ(bleota_dev_secs(NULL, 0u), 0u);
}

/* The gap mark tells a lost chunk from a CREDIT that a chunk in flight
 * outruns: only the answer to a skipped chunk and a POLL that finds bytes
 * missing under the last limit carry it. */
TEST_CASE(only_a_skipped_chunk_and_a_poll_with_bytes_missing_mark_a_gap)
{
    devsim_t t;
    bleota_status_t s = { 0 };
    uint8_t sha[32] = { 0 };
    to_recv(&t, SLOT, sha);                  /* the first CREDIT: routine */
    for (uint32_t off = 0; off < 960u; off += 240u) {
        send_chunk(&t, off, 240u, 1u, 1100u);
    }
    drain(&t, 1100u);                        /* 960 written: 512 step */
    CHECK(next_status(&t, &s));
    CHECK_EQ(s.ev, BLEOTA_EV_CREDIT);
    CHECK_EQ(s.code, BLEOTA_CREDIT_ROUTINE);
    CHECK_EQ(s.next, 960u);
    /* POLL while bytes under the limit are missing: a gap. */
    send_op(&t, BLEOTA_OP_POLL, 1200u);
    CHECK(next_status(&t, &s));
    CHECK_EQ(s.code, BLEOTA_CREDIT_GAP);
    CHECK_EQ(s.next, 960u);
    /* A chunk past next: a gap, answered once. */
    send_chunk(&t, 1200u, 240u, 1u, 1201u);
    CHECK(next_status(&t, &s));
    CHECK_EQ(s.code, BLEOTA_CREDIT_GAP);
    CHECK(!next_status(&t, &s));
    /* Every byte up to the limit taken: a POLL finds none missing. */
    uint32_t off = 960u;
    while (off < t.d.told) {
        const uint16_t n = (uint16_t)(t.d.told - off < 240u ? t.d.told - off
                                                            : 240u);
        send_chunk(&t, off, n, 1u, 1300u);
        off += n;
    }
    while (next_status(&t, &s)) {
    }
    send_op(&t, BLEOTA_OP_POLL, 1301u);
    CHECK(next_status(&t, &s));
    CHECK_EQ(s.code, BLEOTA_CREDIT_ROUTINE);
    CHECK_EQ(s.next, t.d.told);
}

/* Nothing is written before the image's head names this device: the head
 * waits for BLEOTA_HEAD_LEN bytes, and another role, another chip or no
 * head at all is refused with nothing in flash. */
TEST_CASE(the_head_is_checked_before_the_first_write)
{
    devsim_t t;
    bleota_status_t s = { 0 };
    uint8_t sha[32] = { 0 };
    to_recv(&t, 1000u, sha);
    send_chunk(&t, 0u, 240u, 0u, 1100u);
    send_chunk(&t, 240u, (uint16_t)(BLEOTA_HEAD_LEN - 1u - 240u), 0u, 1100u);
    CHECK_EQ(bleota_dev_poll(&t.d, 1100u), BLEOTA_DO_NONE);
    uint8_t buf[64];
    CHECK_EQ(bleota_dev_take(&t.d, buf, sizeof buf), 0u);
    send_chunk(&t, BLEOTA_HEAD_LEN - 1u, 1u, 0u, 1101u);
    CHECK_EQ(bleota_dev_poll(&t.d, 1101u), BLEOTA_DO_WRITE);
    CHECK(t.d.head_ok);

    /* Another role, another chip, a missing role: refused, unwritten. */
    static const struct {
        uint16_t chip;
        uint8_t  kind;
        bool     role;
        uint8_t  code;
    } k_bad[] = {
        { TEST_CHIP_C6, BLEOTA_KIND_NODE_BAL, true, BLEOTA_E_KIND },
        { TEST_CHIP_S3, BLEOTA_KIND_NODE_POWER, true, BLEOTA_E_KIND },
        { TEST_CHIP_C6, BLEOTA_KIND_NODE_POWER, false, BLEOTA_E_IMAGE },
    };
    for (size_t k = 0; k < sizeof k_bad / sizeof k_bad[0]; ++k) {
        to_recv(&t, 1000u, sha);
        while (next_status(&t, &s)) {
        }
        uint8_t head[BLEOTA_HEAD_LEN];
        test_head_write(head, k_bad[k].chip, k_bad[k].kind);
        if (!k_bad[k].role) {
            memset(head + BLEOTA_HEAD_ROLE, 0, BLEOTA_ROLE_LEN);
        }
        uint8_t b[BLEOTA_CHUNK_HDR + BLEOTA_CHUNK_MAX];
        for (uint32_t off = 0; off < BLEOTA_HEAD_LEN; off += 148u) {
            bleota_chunk_header(off, b);
            memcpy(b + BLEOTA_CHUNK_HDR, head + off, 148u);
            bleota_dev_data(&t.d, b, BLEOTA_CHUNK_HDR + 148u, 1100u);
        }
        CHECK_EQ(bleota_dev_poll(&t.d, 1100u), BLEOTA_DO_CLOSE);
        CHECK(next_status(&t, &s));
        CHECK_EQ(s.ev, BLEOTA_EV_FAIL);
        CHECK_EQ(s.code, k_bad[k].code);
        CHECK_EQ(t.flash_n, 0u);
        CHECK_EQ(t.d.wr, 0u);
    }

    /* An image shorter than the head is no application. */
    to_recv(&t, 100u, sha);
    while (next_status(&t, &s)) {
    }
    send_chunk(&t, 0u, 100u, 0u, 1100u);
    CHECK_EQ(bleota_dev_poll(&t.d, 1100u), BLEOTA_DO_CLOSE);
    while (next_status(&t, &s) && s.ev != BLEOTA_EV_FAIL) {
    }
    CHECK_EQ(s.ev, BLEOTA_EV_FAIL);
    CHECK_EQ(s.code, BLEOTA_E_IMAGE);
    CHECK_EQ(t.flash_n, 0u);
}

/* The boot slot may be set while the install runs outside the device's
 * lock: an ABORT or a lost link then leaves its result to decide, and an
 * installed image still restarts. */
TEST_CASE(an_abort_or_a_lost_link_during_the_install_waits_for_its_result)
{
    for (int k = 0; k < 3; ++k) {
        devsim_t t;
        bleota_status_t s = { 0 };
        uint8_t sha[32];
        memset(sha, 0x22, sizeof sha);
        to_recv(&t, 300u, sha);
        send_chunk(&t, 0u, 240u, 0u, 1100u);
        send_chunk(&t, 240u, 60u, 0u, 1100u);
        send_op(&t, BLEOTA_OP_END, 1101u);
        drain(&t, 1102u);
        while (next_status(&t, &s)) {
        }
        CHECK_EQ(bleota_dev_poll(&t.d, 1102u), BLEOTA_DO_VERIFY);
        CHECK(bleota_dev_verify(&t.d, sha, 1103u));
        if (k == 0) {
            send_op(&t, BLEOTA_OP_ABORT, 1104u);
        } else {
            bleota_dev_lost(&t.d);
        }
        CHECK_EQ(t.d.st, BLEOTA_DEV_INSTALL);
        CHECK(!next_status(&t, &s));
        CHECK_EQ(bleota_dev_poll(&t.d, 1104u), BLEOTA_DO_INSTALL);
        if (k < 2) {
            bleota_dev_installed(&t.d, true, BLEOTA_OK, 1105u);
            CHECK_EQ(t.d.st, BLEOTA_DEV_DONE);
            CHECK_EQ(bleota_dev_poll(&t.d, 1105u), BLEOTA_DO_RESTART);
        } else {
            /* The device's own check refused it: the running image stays. */
            bleota_dev_installed(&t.d, false, BLEOTA_E_IMAGE, 1105u);
            CHECK_EQ(t.d.st, BLEOTA_DEV_IDLE);
            CHECK_EQ(t.d.code, BLEOTA_E_IMAGE);
            CHECK_EQ(bleota_dev_poll(&t.d, 1105u), BLEOTA_DO_NONE);
        }
    }
}

int main(void)
{
    RUN(init_refuses_a_small_or_uneven_buffer_and_a_kind_out_of_range);
    RUN(begin_is_refused_for_kind_size_version_protocol_and_flags);
    RUN(the_hash_halves_open_the_window_and_writes_out_of_order_fail);
    RUN(a_press_accepts_under_1000ms_and_a_hold_refuses);
    RUN(a_hold_of_1000ms_accepts_a_node_and_a_stuck_key_does_not);
    RUN(no_confirmation_in_30s_refuses_and_opens_nothing);
    RUN(chunks_go_in_order_and_a_skipped_one_is_answered_once);
    RUN(a_chunk_past_the_limit_is_dropped_and_credits_step_by_512);
    RUN(end_compares_the_sha_and_installs_only_on_a_match);
    RUN(a_silent_central_a_failed_write_and_a_lost_link_end_it);
    RUN(the_buffer_wraps_over_a_whole_slot);
    RUN(only_a_skipped_chunk_and_a_poll_with_bytes_missing_mark_a_gap);
    RUN(the_head_is_checked_before_the_first_write);
    RUN(an_abort_or_a_lost_link_during_the_install_waits_for_its_result);
    RUN(a_full_notification_queue_drops_the_oldest_and_nulls_are_safe);
    return test_summary("bleota_dev");
}
