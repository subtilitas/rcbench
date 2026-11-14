/**
 * @file test_devupd_client.c
 * @brief The panel's client of a device update (devupd_client_t) over a
 *        scripted host stack with the device side of protocols/bleota
 *        behind it: the connect to the bound address, the discovery that
 *        finds CTRL as STREAM and CMD and DATA's handle, the subscribe and
 *        the MTU, BEGIN through the machine's command write, the chunks to
 *        DATA, the install, and the machine switched off at the end; a
 *        device without DATA never starts the transfer; an owner out of
 *        range is refused.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"
#include "bleota.h"
#include "bleota_head.h"
#include "bleuart.h"
#include "devupd_client.h"
#include "fw_sha256.h"

/** @brief The device's handles. @{ */
#define H_START 0x0020u
#define H_CTRL_DEF 0x0021u
#define H_CTRL 0x0022u
#define H_CCCD 0x0023u
#define H_DATA_DEF 0x0024u
#define H_DATA 0x0025u
#define H_END 0x0025u
/** @} */
/** @brief The connection's handle. */
#define CONN 3u
/** @brief The image, bytes. */
#define IMG 9000u
/** @brief The device's slot. */
#define SLOT 0x10000u

/** @brief The image. */
static uint8_t s_img[IMG];

/** @brief The host stack, the device and what went over the air. */
typedef struct {
    devupd_client_t c;         /**< the client */
    bleota_dev_t    d;         /**< the device */
    uint8_t         ring[BLEOTA_WINDOW]; /**< its buffer */
    fw_sha256_t     sha;       /**< over what it wrote */
    uint32_t        flash_n;   /**< bytes written */
    bleu_evt_t      q[64];     /**< events due to the client */
    unsigned        qn;        /**< their count */
    uint32_t        now;       /**< the clock */
    bool            no_data;   /**< the device lacks DATA */
    bool            linked;    /**< a connection stands */
    unsigned        connects;  /**< CONNECTs seen */
    unsigned        disconnects; /**< DISCONNECTs asked */
    unsigned        ctrl_writes; /**< writes to CTRL */
    unsigned        chunks;    /**< writes to DATA */
    bool            restarted; /**< the device restarted */
    bleu_addr_t     addr;      /**< the address connected */
    unsigned        owner;     /**< the owner the CONNECT's token names */
} host_t;

/**
 * @brief A new event for the client.
 * @param h    the host
 * @param kind bleu_ev_kind_t
 * @param tok  the token it answers
 * @return the event
 */
static bleu_evt_t *ev(host_t *h, uint8_t kind, uint16_t tok)
{
    bleu_evt_t *e = &h->q[h->qn++];
    memset(e, 0, sizeof *e);
    e->kind = kind;
    e->token = tok;
    e->conn = CONN;
    return e;
}

/**
 * @brief A characteristic of the device.
 * @param h     the host
 * @param tok   the discovery's token
 * @param def   its declaration
 * @param val   its value handle
 * @param props its properties
 * @param uuid  its UUID
 */
static void chr(host_t *h, uint16_t tok, uint16_t def, uint16_t val,
                uint8_t props, const uint8_t *uuid)
{
    bleu_evt_t *e = ev(h, BLEU_EV_CHR, tok);
    e->h1 = def;
    e->h2 = val;
    e->props = props;
    e->len = 16u;
    memcpy(e->data, uuid, 16u);
}

/**
 * @brief Perform one action of the client, as NimBLE and the device would.
 * @param h the host
 * @param a the action
 */
static void act(host_t *h, const bleu_act_t *a)
{
    bleu_evt_t *e;
    switch (a->kind) {
    case BLEU_ACT_CONNECT:
        ++h->connects;
        h->addr = a->addr;
        h->owner = bleu_token_owner(a->token);
        h->linked = true;
        e = ev(h, BLEU_EV_CONNECT, a->token);
        e->h1 = a->cp.itvl_max;
        e->h3 = a->cp.supervision;
        break;
    case BLEU_ACT_DISC_SVC:
        CHECK(memcmp(a->data, bleota_uuid_svc, 16u) == 0);
        e = ev(h, BLEU_EV_SVC, a->token);
        e->h1 = H_START;
        e->h2 = H_END;
        (void)ev(h, BLEU_EV_SVC_DONE, a->token);
        break;
    case BLEU_ACT_DISC_CHRS:
        chr(h, a->token, H_CTRL_DEF, H_CTRL,
            BLEU_PROP_WRITE | BLEU_PROP_NOTIFY, bleota_uuid_ctrl);
        if (!h->no_data) {
            chr(h, a->token, H_DATA_DEF, H_DATA, BLEU_PROP_WRITE_NR,
                bleota_uuid_data);
        }
        (void)ev(h, BLEU_EV_CHR_DONE, a->token);
        break;
    case BLEU_ACT_DISC_DSCS:
        CHECK_EQ(a->h1, H_CTRL);
        e = ev(h, BLEU_EV_DSC, a->token);
        e->h1 = H_CCCD;
        e->len = 2u;
        e->data[0] = 0x02;
        e->data[1] = 0x29;
        (void)ev(h, BLEU_EV_DSC_DONE, a->token);
        break;
    case BLEU_ACT_MTU:
        e = ev(h, BLEU_EV_MTU, a->token);
        e->h1 = BLEOTA_MTU;
        break;
    case BLEU_ACT_SUBSCRIBE:
        CHECK_EQ(a->h1, H_CCCD);
        (void)ev(h, BLEU_EV_WRITE_DONE, a->token);
        break;
    case BLEU_ACT_WRITE:
        CHECK_EQ(a->h1, H_CTRL);
        ++h->ctrl_writes;
        bleota_dev_ctrl(&h->d, a->data, a->len, h->now);
        (void)ev(h, BLEU_EV_WRITE_DONE, a->token);
        break;
    case BLEU_ACT_DISCONNECT:
        ++h->disconnects;
        if (h->linked) {
            h->linked = false;
            e = ev(h, BLEU_EV_DISCONNECT, a->token);
            e->status = 0x16u;
        }
        bleota_dev_lost(&h->d);
        break;
    default:
        break;
    }
}

/**
 * @brief The device's caller: open, write, verify, install, restart.
 * @param h the host
 */
static void device(host_t *h)
{
    uint8_t buf[512];
    uint8_t dig[32];
    bool more = true;
    for (int i = 0; i < 8 && more; ++i) {
        switch (bleota_dev_poll(&h->d, h->now)) {
        case BLEOTA_DO_OPEN:
            fw_sha256_init(&h->sha);
            bleota_dev_opened(&h->d, true, h->now);
            break;
        case BLEOTA_DO_WRITE: {
            const uint32_t n = bleota_dev_take(&h->d, buf, sizeof buf);
            fw_sha256_add(&h->sha, buf, n);
            h->flash_n += n;
            bleota_dev_written(&h->d, true, h->now);
            break;
        }
        case BLEOTA_DO_VERIFY:
            fw_sha256_done(&h->sha, dig);
            (void)bleota_dev_verify(&h->d, dig, h->now);
            break;
        case BLEOTA_DO_INSTALL:
            bleota_dev_installed(&h->d, true, BLEOTA_OK, h->now);
            break;
        case BLEOTA_DO_CLOSE:
            bleota_dev_closed(&h->d);
            break;
        case BLEOTA_DO_RESTART:
            if (!h->restarted && h->d.out_n == 0u && h->linked) {
                h->restarted = true;
                h->linked = false;
                bleu_evt_t *e = ev(h, BLEU_EV_DISCONNECT, 0u);
                e->status = 0x13u;
            }
            more = false;
            break;
        default:
            more = false;
            break;
        }
    }
    uint8_t b[BLEOTA_LEN_STATUS];
    while (h->linked && bleota_dev_status(&h->d, b)) {
        bleu_evt_t *e = ev(h, BLEU_EV_NOTIFY, 0u);
        e->h1 = H_CTRL;
        e->len = BLEOTA_LEN_STATUS;
        memcpy(e->data, b, sizeof b);
    }
}

/**
 * @brief One poll of 5ms: events in, actions out, chunks out, the device.
 * @param h the host
 */
static void poll(host_t *h)
{
    for (unsigned i = 0; i < h->qn; ++i) {
        devupd_client_event(&h->c, &h->q[i], h->now);
    }
    h->qn = 0u;
    bleu_act_t a;
    for (int i = 0; i < 8 && devupd_client_step(&h->c, h->now, &a); ++i) {
        act(h, &a);
    }
    devupd_out_t o;
    uint16_t conn = 0, handle = 0;
    for (int i = 0; i < 8
         && devupd_client_chunk(&h->c, h->now, &o, &conn, &handle); ++i) {
        CHECK_EQ(conn, CONN);
        CHECK_EQ(handle, H_DATA);
        ++h->chunks;
        uint8_t b[BLEOTA_CHUNK_HDR + BLEOTA_CHUNK_MAX];
        bleota_chunk_header(o.off, b);
        memcpy(b + BLEOTA_CHUNK_HDR, s_img + o.off, o.n);
        bleota_dev_data(&h->d, b, BLEOTA_CHUNK_HDR + o.n, h->now);
    }
    if (h->d.st == BLEOTA_DEV_CONFIRM) {
        bleota_dev_key(&h->d, true, h->now);
        bleota_dev_key(&h->d, false, h->now + 1u);
    }
    device(h);
    h->now += 5u;
}

/**
 * @brief Set up the host, the device and the image, and start.
 * @param h       the host
 * @param no_data the device lacks DATA
 */
static void start(host_t *h, bool no_data)
{
    memset(h, 0, sizeof *h);
    for (uint32_t i = 0; i < IMG; ++i) {
        s_img[i] = (uint8_t)(i * 7u + 3u);
    }
    test_head_write(s_img, TEST_CHIP_S3, BLEOTA_KIND_DIAL);
    h->no_data = no_data;
    h->now = 100u;
    const bleota_dev_cfg_t cfg = { .kind = BLEOTA_KIND_DIAL,
                                   .confirm = BLEOTA_CONFIRM_PRESS,
                                   .slot = SLOT, .ver = { 0u, 24u, 0u },
                                   .chip = TEST_CHIP_S3 };
    CHECK(bleota_dev_init(&h->d, &cfg, h->ring, sizeof h->ring));
    CHECK(devupd_client_init(&h->c, 4u));
    devupd_image_t im = { .img = s_img, .size = IMG,
                          .kind = BLEOTA_KIND_DIAL,
                          .version = { 0u, 25u, 0u } };
    fw_sha256(s_img, IMG, im.sha);
    bleu_addr_t a = { .val = { 1, 2, 3, 4, 5, 6 }, .type = BLEU_ADDR_PUBLIC };
    CHECK(devupd_client_start(&h->c, &a, &im, h->now));
    CHECK(!devupd_client_start(&h->c, &a, &im, h->now));
}

TEST_CASE(an_update_connects_finds_data_sends_and_switches_off)
{
    static host_t h;
    start(&h, false);
    for (int i = 0; i < 20000 && devupd_busy(&h.c.e); ++i) {
        poll(&h);
    }
    CHECK_EQ(h.c.e.err, DEVUPD_E_NONE);
    CHECK_EQ(h.c.e.dev_code, BLEOTA_OK);
    CHECK_EQ(h.c.e.phase, DEVUPD_DONE);
    CHECK_EQ(h.connects, 1u);
    CHECK_EQ(h.addr.val[0], 1);
    CHECK_EQ(h.flash_n, IMG);
    CHECK(h.restarted);
    CHECK_EQ(h.ctrl_writes, 4u);   /* BEGIN, two HASH halves, END */
    CHECK(h.chunks >= IMG / BLEOTA_CHUNK_MAX);
    CHECK_EQ(h.owner, 4u);
    /* Switched off: no reconnect after the device's restart. */
    for (int i = 0; i < 2000; ++i) {
        poll(&h);
    }
    CHECK(!h.c.on);
    CHECK_EQ(h.connects, 1u);
    CHECK_EQ(h.c.b.st, BLEU_ST_OFF);
}

TEST_CASE(a_device_without_data_never_starts_the_transfer)
{
    static host_t h;
    start(&h, true);
    for (int i = 0; i < 1000; ++i) {
        poll(&h);
    }
    CHECK_EQ(h.ctrl_writes, 0u);
    CHECK_EQ(h.c.e.phase, DEVUPD_LINK);
    for (int i = 0; i < (int)(DEVUPD_LINK_MS / 5u); ++i) {
        poll(&h);
    }
    CHECK_EQ(h.c.e.phase, DEVUPD_FAILED);
    CHECK_EQ(h.c.e.err, DEVUPD_E_LINK);
    CHECK(!h.c.on);
}

TEST_CASE(an_abort_ends_the_link_and_owners_out_of_range_are_refused)
{
    static host_t h;
    start(&h, false);
    for (int i = 0; i < 400 && h.c.e.phase != DEVUPD_SEND; ++i) {
        poll(&h);
    }
    CHECK_EQ(h.c.e.phase, DEVUPD_SEND);
    devupd_client_abort(&h.c, h.now);
    for (int i = 0; i < 400; ++i) {
        poll(&h);
    }
    CHECK_EQ(h.c.e.err, DEVUPD_E_ABORTED);
    CHECK(h.disconnects >= 1u);
    CHECK(!h.c.on);
    CHECK(!h.restarted);
    devupd_client_t c;
    CHECK(!devupd_client_init(&c, BLEU_OWNER_MAX));
    CHECK(!devupd_client_init(NULL, 0u));
}

int main(void)
{
    RUN(an_update_connects_finds_data_sends_and_switches_off);
    RUN(a_device_without_data_never_starts_the_transfer);
    RUN(an_abort_ends_the_link_and_owners_out_of_range_are_refused);
    return test_summary("devupd_client");
}
