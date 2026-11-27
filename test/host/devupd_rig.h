/**
 * @file devupd_rig.h
 * @brief The rig of the device update's suites: the panel's engine
 *        (devupd_t) against a simulated device, the device side of
 *        protocols/bleota over a modelled flash, a link with latency and
 *        injected faults, and a 5ms clock.  test_devupd.c and
 *        test_devupd_sig.c include it; each suite has its own copy.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef DEVUPD_RIG_H
#define DEVUPD_RIG_H

#include <string.h>

#include "greatest.h"
#include "bleota.h"
#include "bleota_head.h"
#include "devupd.h"
#include "fw_sha256.h"

/** @brief The tests' image, bytes: 34 credit windows and a part. */
#define IMG 70001u
/** @brief The simulated device's slot. */
#define SLOT 0x20000u
/** @brief The simulation's step, ms: the panel's `blec` poll. */
#define STEP 5u
/** @brief Chunks or notifications in flight at most. */
#define AIR_MAX 512u

/** @brief A chunk or a notification in flight. */
typedef struct {
    uint32_t at;                    /**< the step it arrives at */
    uint32_t off;                   /**< a chunk's offset */
    uint16_t n;                     /**< a chunk's bytes */
    uint8_t  b[BLEOTA_LEN_STATUS];  /**< a notification's bytes */
} flight_t;

/** @brief A queue of things in flight. */
typedef struct {
    flight_t q[AIR_MAX]; /**< the ring */
    unsigned head;       /**< the oldest */
    unsigned n;          /**< the count */
} air_t;

/** @brief The image the panel sends. */
static uint8_t s_img[IMG];
/** @brief What the device wrote. */
static uint8_t s_flash[SLOT];

/** @brief The panel's engine, the device, and the faults injected. */
typedef struct {
    devupd_t       e;          /**< the engine */
    devupd_image_t im;         /**< the image it sends */
    bleota_dev_t   d;          /**< the device */
    uint8_t        ring[BLEOTA_WINDOW]; /**< the device's buffer */
    uint32_t       flash_n;    /**< bytes written */
    fw_sha256_t    sha;        /**< over them */
    uint32_t       now;        /**< the clock */
    uint16_t       mtu;        /**< the link's MTU */
    bool           up;         /**< the link stands */
    bool           restarted;  /**< the device restarted */
    bool           press;      /**< the device's key is pressed at WAIT */
    uint32_t       press_at;   /**< when it went down */
    uint32_t       drop_off;   /**< the first chunk at or past this offset
                                    is lost; UINT32_MAX none */
    bool           drop_tail;  /**< the image's last chunk is lost once */
    bool           mute;       /**< the device's notifications are lost */
    uint32_t       mute_from;  /**< from this offset taken on */
    bool           cut;        /**< the link drops at cut_at */
    uint32_t       cut_at;     /**< bytes taken when it drops */
    uint32_t       first_off;  /**< the first chunk's offset after a start */
    bool           first_seen; /**< first_off is set */
    uint32_t       ctrl_fail;  /**< the n-th control write is refused, 0 none */
    uint32_t       ctrl_n;     /**< control writes seen */
    uint8_t        last_op;    /**< the last control write's op */
    uint32_t       lat;        /**< steps from a write or a notification to
                                    its arrival; 0 at once */
    unsigned       rate;       /**< chunks the air carries a step, 0 all */
    bool           slow;       /**< the device writes one 512-byte piece a
                                    step, 1,024 bytes in 10ms */
    uint32_t       step;       /**< steps run */
    uint64_t       sent;       /**< image bytes the engine gave out */
    air_t          air;        /**< chunks in flight */
    air_t          back;       /**< notifications in flight */
    bool           old;        /**< the device runs protocol 1: it refuses
                                    protocol 2 with BLEOTA_E_PROTO, answers
                                    HASH parts 0 and 1 after that with
                                    BLEOTA_E_STATE, and its decoder drops
                                    parts 2 to 5 */
    bool           old_both;   /**< it refuses protocol 1 as well */
    bool           old_refused; /**< it refused the last BEGIN */
    unsigned       begins;     /**< BEGINs written */
    bool           sig_ok;     /**< BLEOTA_DO_SIG's verdict */
} sim_t;

/** @brief Fill the image with a pattern no shift repeats. */
static inline void make_image(void)
{
    uint32_t x = 0x12345678u;
    for (uint32_t i = 0; i < IMG; ++i) {
        x = x * 1664525u + 1013904223u;
        s_img[i] = (uint8_t)(x >> 24);
    }
    test_head_write(s_img, TEST_CHIP_S3, BLEOTA_KIND_DIAL);
}

/**
 * @brief A new simulation: the device runs 0.24.0, the panel sends 0.25.0
 *        of the right kind with the right SHA-256, the key pressed.
 * @param s the simulation
 */
static inline void sim_init(sim_t *s)
{
    memset(s, 0, sizeof *s);
    make_image();
    devupd_init(&s->e);
    s->im.img = s_img;
    s->im.size = IMG;
    s->im.kind = BLEOTA_KIND_DIAL;
    s->im.version = (fw_version_t){ 0u, 25u, 0u };
    fw_sha256(s_img, IMG, s->im.sha);
    const bleota_dev_cfg_t cfg = { .kind = BLEOTA_KIND_DIAL,
                                   .confirm = BLEOTA_CONFIRM_PRESS,
                                   .slot = SLOT, .ver = { 0u, 24u, 0u },
                                   .chip = TEST_CHIP_S3 };
    CHECK(bleota_dev_init(&s->d, &cfg, s->ring, sizeof s->ring));
    s->mtu = BLEOTA_MTU;
    s->press = true;
    s->drop_off = UINT32_MAX;
    s->now = 1000u;
}

/**
 * @brief The link comes up: both sides told.
 * @param s the simulation
 */
static inline void link_up(sim_t *s)
{
    s->up = true;
    devupd_link(&s->e, true, s->mtu, s->now);
}

/**
 * @brief The link drops: both sides told.
 * @param s the simulation
 */
static inline void link_down(sim_t *s)
{
    s->up = false;
    s->air.n = 0u;
    s->back.n = 0u;
    bleota_dev_lost(&s->d);
    devupd_link(&s->e, false, 0u, s->now);
}

/**
 * @brief Tell the engine a FAIL with @p code, as a device notifies it.
 * @param s    the simulation
 * @param code BLEOTA_E_*
 */
static inline void old_fail(sim_t *s, uint8_t code)
{
    bleota_status_t st;
    uint8_t b[BLEOTA_LEN_STATUS];
    memset(&st, 0, sizeof st);
    st.ev = BLEOTA_EV_FAIL;
    st.code = code;
    (void)bleota_status_encode(&st, b);
    devupd_notify(&s->e, b, sizeof b, s->now);
}

/**
 * @brief A device of protocol 1 takes a control write.
 * @param s the simulation
 * @param o the write
 * @return true when it answered itself; false hands it to the device
 */
static inline bool old_device(sim_t *s, const devupd_out_t *o)
{
    if (o->ctrl[0] == BLEOTA_OP_BEGIN) {
        s->old_refused = o->ctrl[1] != BLEOTA_PROTO_1 || s->old_both;
        if (s->old_refused) {
            old_fail(s, BLEOTA_E_PROTO);
        }
        return s->old_refused;
    }
    if (o->ctrl[0] == BLEOTA_OP_HASH) {
        if (o->ctrl[1] > 1u) {
            return true;                /* its decoder drops the write */
        }
        if (s->old_refused) {
            old_fail(s, BLEOTA_E_STATE);
            return true;
        }
    }
    return false;
}

/**
 * @brief The engine's writes of one step, over the link to the device.
 * @param s the simulation
 */
static inline void panel_writes(sim_t *s)
{
    devupd_out_t o;
    for (int i = 0; i < 8 && devupd_step(&s->e, s->now, true, &o); ++i) {
        if (!s->up) {
            continue;
        }
        if (o.kind == DEVUPD_OUT_CTRL) {
            ++s->ctrl_n;
            s->last_op = o.ctrl[0];
            if (s->ctrl_n == s->ctrl_fail) {
                devupd_written(&s->e, 0x0Eu, s->now);
                continue;
            }
            if (o.ctrl[0] == BLEOTA_OP_BEGIN) {
                ++s->begins;
            }
            if (!s->old || !old_device(s, &o)) {
                bleota_dev_ctrl(&s->d, o.ctrl, o.len, s->now);
            }
            devupd_written(&s->e, 0u, s->now);
            continue;
        }
        CHECK(o.n >= 1u && o.n <= bleota_chunk_bytes(s->mtu));
        CHECK(o.off + o.n <= IMG);
        s->sent += o.n;
        if (!s->first_seen) {
            s->first_seen = true;
            s->first_off = o.off;
        }
        if (o.off >= s->drop_off) {
            s->drop_off = UINT32_MAX;
            continue;
        }
        if (s->drop_tail && o.off + o.n == IMG) {
            s->drop_tail = false;
            continue;
        }
        if (s->lat > 0u) {
            CHECK(s->air.n < AIR_MAX);
            flight_t *f = &s->air.q[(s->air.head + s->air.n++) % AIR_MAX];
            f->at = s->step + s->lat;
            f->off = o.off;
            f->n = o.n;
            continue;
        }
        uint8_t b[BLEOTA_CHUNK_HDR + BLEOTA_CHUNK_MAX];
        bleota_chunk_header(o.off, b);
        memcpy(b + BLEOTA_CHUNK_HDR, s_img + o.off, o.n);
        bleota_dev_data(&s->d, b, BLEOTA_CHUNK_HDR + o.n, s->now);
    }
}

/**
 * @brief The chunks in flight that arrive this step, at most rate of them.
 * @param s the simulation
 */
static inline void air_arrives(sim_t *s)
{
    for (unsigned k = 0; s->air.n > 0u
         && s->air.q[s->air.head].at <= s->step
         && (s->rate == 0u || k < s->rate); ++k) {
        const flight_t *f = &s->air.q[s->air.head];
        s->air.head = (s->air.head + 1u) % AIR_MAX;
        --s->air.n;
        uint8_t b[BLEOTA_CHUNK_HDR + BLEOTA_CHUNK_MAX];
        bleota_chunk_header(f->off, b);
        memcpy(b + BLEOTA_CHUNK_HDR, s_img + f->off, f->n);
        bleota_dev_data(&s->d, b, BLEOTA_CHUNK_HDR + f->n, s->now);
    }
}

/**
 * @brief The device's key: pressed for 100ms once the window opens.
 * @param s the simulation
 */
static inline void device_key(sim_t *s)
{
    if (!s->press || s->d.st != BLEOTA_DEV_CONFIRM) {
        bleota_dev_key(&s->d, false, s->now);
        return;
    }
    if (s->press_at == 0u) {
        s->press_at = s->now;
    }
    bleota_dev_key(&s->d, s->now - s->press_at < 100u, s->now);
}

/**
 * @brief The device's caller: open, write, verify, install, restart.
 * @param s the simulation
 */
static inline void device_work(sim_t *s)
{
    uint8_t buf[512];
    uint8_t digest[32];
    for (int i = 0; i < 16; ++i) {
        switch (bleota_dev_poll(&s->d, s->now)) {
        case BLEOTA_DO_OPEN:
            s->flash_n = 0u;
            fw_sha256_init(&s->sha);
            bleota_dev_opened(&s->d, true, s->now);
            break;
        case BLEOTA_DO_WRITE: {
            const uint32_t n = bleota_dev_take(&s->d, buf, sizeof buf);
            memcpy(s_flash + s->flash_n, buf, n);
            s->flash_n += n;
            fw_sha256_add(&s->sha, buf, n);
            bleota_dev_written(&s->d, true, s->now);
            if (s->slow) {
                return;
            }
            break;
        }
        case BLEOTA_DO_VERIFY:
            fw_sha256_done(&s->sha, digest);
            (void)bleota_dev_verify(&s->d, digest, s->now);
            break;
        case BLEOTA_DO_SIG:
            bleota_dev_sig(&s->d, s->sig_ok, s->d.gen, s->now);
            break;
        case BLEOTA_DO_INSTALL:
            bleota_dev_installed(&s->d, true, BLEOTA_OK, s->now);
            break;
        case BLEOTA_DO_CLOSE:
            bleota_dev_closed(&s->d);
            break;
        case BLEOTA_DO_RESTART:
            if (!s->restarted && s->d.out_n == 0u && s->back.n == 0u) {
                s->restarted = true;
                link_down(s);
            }
            return;
        case BLEOTA_DO_NONE:
        default:
            return;
        }
    }
}

/**
 * @brief The device's notifications to the engine, unless muted.
 * @param s the simulation
 */
static inline void device_notifies(sim_t *s)
{
    uint8_t b[BLEOTA_LEN_STATUS];
    while (bleota_dev_status(&s->d, b)) {
        if (s->mute && s->d.rx >= s->mute_from) {
            continue;
        }
        if (s->up && s->lat > 0u) {
            CHECK(s->back.n < AIR_MAX);
            flight_t *f = &s->back.q[(s->back.head + s->back.n++) % AIR_MAX];
            f->at = s->step + s->lat;
            memcpy(f->b, b, sizeof b);
        } else if (s->up) {
            devupd_notify(&s->e, b, sizeof b, s->now);
        }
    }
    while (s->back.n > 0u && s->back.q[s->back.head].at <= s->step) {
        devupd_notify(&s->e, s->back.q[s->back.head].b, BLEOTA_LEN_STATUS,
                      s->now);
        s->back.head = (s->back.head + 1u) % AIR_MAX;
        --s->back.n;
    }
}

/**
 * @brief One step of STEP ms.
 * @param s the simulation
 */
static inline void sim_step(sim_t *s)
{
    panel_writes(s);
    air_arrives(s);
    device_key(s);
    device_work(s);
    if (s->cut && s->up && s->d.rx >= s->cut_at) {
        s->cut = false;
        link_down(s);
    }
    device_notifies(s);
    s->now += STEP;
    ++s->step;
}

/**
 * @brief Step until the engine ends or @p ms run out.
 * @param s  the simulation
 * @param ms the most time
 * @return the phase it ended in
 */
static inline devupd_phase_t sim_run(sim_t *s, uint32_t ms)
{
    const uint32_t end = s->now + ms;
    while (devupd_busy(&s->e) && s->now < end) {
        sim_step(s);
    }
    return (devupd_phase_t)s->e.phase;
}

/**
 * @brief Start the engine and bring the link up.
 * @param s the simulation
 */
static inline void sim_start(sim_t *s)
{
    CHECK(devupd_start(&s->e, &s->im, s->now));
    CHECK_EQ(s->e.phase, DEVUPD_LINK);
    link_up(s);
    CHECK_EQ(s->e.phase, DEVUPD_BEGIN);
}

#endif /* DEVUPD_RIG_H */
