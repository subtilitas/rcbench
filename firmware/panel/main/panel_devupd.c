/**
 * @file panel_devupd.c
 * @brief The wireless devices' update on the panel (panel_devupd.h): the
 *        carried images, the `blec` task's update client, and the frame's
 *        rows of UPDATES' devices' page.
 *
 * SPDX-License-Identifier: MIT
 */
#include "panel_devupd.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "devupd.h"
#include "devupd_client.h"
#include "fw_image.h"
#include "fw_sha256.h"
#include "panel.h"
#include "panel_ble.h"
#include "panel_blec.h"
#include "panel_bal.h"
#include "panel_dial.h"
#include "panel_pnode.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "upd_dev.h"

/** @brief The console lines' tag. */
static const char *TAG = "devupd";

/** @brief From the hold to the device's own link down, ms at most. */
#define HOLD_MS 5000u
/** @brief The console's progress line, every this many percent. */
#define LOG_STEP_PCT 10u

_Static_assert(PANEL_BLEC_UPD == 4u, "the update client is the hub's owner 4");
_Static_assert(PANEL_BLEC_UPD < BLEU_OWNER_MAX,
               "the update client's owner fits a token");
_Static_assert(UPD_DEV_COUNT == BLEOTA_KIND_MAX
                   && UPD_DEV_DIAL == BLEOTA_KIND_DIAL - 1
                   && UPD_DEV_PNODE == BLEOTA_KIND_NODE_POWER - 1
                   && UPD_DEV_BAL == BLEOTA_KIND_NODE_BAL - 1,
               "a row, its image and its device share one index");

#ifdef RCBENCH_DIAL_IMAGE
/** @brief The carried Dial image (main/CMakeLists.txt). @{ */
extern const uint8_t _binary_rcbench_dial_bin_start[];
extern const uint8_t _binary_rcbench_dial_bin_end[];
/** @} */
#endif
#ifdef RCBENCH_NODE_PWR_IMAGE
/** @brief The carried power node image. @{ */
extern const uint8_t _binary_rcbench_node_pwr_bin_start[];
extern const uint8_t _binary_rcbench_node_pwr_bin_end[];
/** @} */
#endif
#ifdef RCBENCH_NODE_BAL_IMAGE
/** @brief The carried balance node image. @{ */
extern const uint8_t _binary_rcbench_node_bal_bin_start[];
extern const uint8_t _binary_rcbench_node_bal_bin_end[];
/** @} */
#endif

/** @brief A carried image's check. */
typedef enum {
    IMG_NONE = 0, /**< not carried */
    IMG_DUE,      /**< carried, not checked yet */
    IMG_READY,    /**< checked: version and SHA-256 known */
    IMG_BAD,      /**< failed its check */
} img_state_t;

/** @brief One carried image. */
typedef struct {
    const char     *word;    /**< its name on the console */
    const uint8_t  *p;       /**< its first byte */
    uint32_t        n;       /**< its length */
    uint8_t         kind;    /**< BLEOTA_KIND_* */
    uint16_t        chip;    /**< FW_CHIP_* it must name */
    const char     *project; /**< the project it must name */
    uint32_t        slot;    /**< the device's slot */
    uint8_t         st;      /**< img_state_t, written by `blec` */
    fw_err_t        err;     /**< IMG_BAD: why */
    fw_image_info_t info;    /**< IMG_READY: what it says */
    uint8_t         sha[32]; /**< IMG_READY: the SHA-256 of every byte */
} carried_t;

/** @brief The images, by BLEOTA_KIND_* - 1. */
static carried_t s_img[BLEOTA_KIND_MAX] = {
    { .word = "Dial", .kind = BLEOTA_KIND_DIAL, .chip = DEVUPD_CHIP_ESP32S3,
      .project = DEVUPD_PROJECT_DIAL, .slot = DEVUPD_SLOT_DIAL },
    { .word = "power node", .kind = BLEOTA_KIND_NODE_POWER,
      .chip = FW_CHIP_ESP32C6, .project = DEVUPD_PROJECT_NODE,
      .slot = DEVUPD_SLOT_NODE },
    { .word = "balance node", .kind = BLEOTA_KIND_NODE_BAL,
      .chip = FW_CHIP_ESP32C6, .project = DEVUPD_PROJECT_NODE,
      .slot = DEVUPD_SLOT_NODE },
};

/** @brief A device's own client, as the update uses it. */
typedef struct {
    ui_text_id_t name; /**< its name in the band's lines */
    ui_text_id_t ask;  /**< the band's ask during the confirmation */
    /** @brief The hold on its own link (panel_dial_blec_hold()). */
    void (*hold)(bool on, uint32_t now);
    /** @brief What its link says (panel_dial_blec_target()). */
    bool (*target)(bleu_addr_t *addr, bool *linked, uint8_t version[3],
                   bool *down);
} dev_hooks_t;

/** @brief The devices, by BLEOTA_KIND_* - 1, as s_img. */
static const dev_hooks_t k_dev[BLEOTA_KIND_MAX] = {
    { TX_DEV_NAME_DIAL, TX_ALERT_DIAL_ASK, panel_dial_blec_hold,
      panel_dial_blec_target },
    { TX_DEV_NAME_PNODE, TX_ALERT_NODE_ASK, panel_pnode_blec_hold,
      panel_pnode_blec_target },
    { TX_DEV_NAME_BAL, TX_ALERT_NODE_ASK, panel_bal_blec_hold,
      panel_bal_blec_target },
};

/** @brief Where the `blec` task's update stands. */
typedef enum {
    STAGE_IDLE = 0, /**< none */
    STAGE_HOLD,     /**< the device's own link ends */
    STAGE_RUN,      /**< the client runs the transfer */
} stage_t;

/** @brief What a device's link and image say, for its row. */
typedef struct {
    bool         img_due;   /**< its image is not checked yet */
    bool         have_img;  /**< it is carried */
    bool         img_ok;    /**< it passed */
    fw_version_t carried;   /**< its version */
    bool         linked;    /**< the device is connected, its version read */
    fw_version_t running;   /**< that version */
} dev_pub_t;

/** @brief What the `blec` task publishes for the frame. */
typedef struct {
    dev_pub_t    d[BLEOTA_KIND_MAX]; /**< each device */
    uint8_t      dev;       /**< the device of the running or last update */
    uint8_t      phase;     /**< devupd_phase_t of the transfer */
    unsigned     percent;   /**< its share taken */
    unsigned     secs;      /**< DEVUPD_CONFIRM: seconds given */
    uint32_t     end_seq;   /**< updates ended, counted */
    bool         ok;        /**< the last one installed */
    bool         unknown;   /**< its install's result is not known */
    uint8_t      err;       /**< otherwise devupd_err_t */
    uint8_t      code;      /**< and the device's BLEOTA_E_* */
} pub_t;

/** @brief The update client, the `blec` task's own. */
static devupd_client_t s_cli;
/** @brief The `blec` task's stage. */
static stage_t         s_stage;
/** @brief When the stage began. */
static uint32_t        s_stage_t;
/** @brief The image being sent. */
static devupd_image_t  s_im;
/** @brief The device of the running or last update, by s_img's index. */
static unsigned        s_cur;
/** @brief The transfer's start, for its rate. */
static uint32_t        s_t0;
/** @brief The last progress line's share. */
static unsigned        s_logged_pct;
/** @brief The last phase logged. */
static uint8_t         s_logged_phase;
/** @brief The client was set up. */
static bool            s_started;
/** @brief The `blec` task's copy before it is published. */
static pub_t           s_next;
/** @brief Guards s_pub. */
static portMUX_TYPE    s_mux = portMUX_INITIALIZER_UNLOCKED;
/** @brief Published by `blec`, read by the frame. */
static pub_t           s_pub;
/** @brief The start between the frame and `blec`, and panel_devupd_busy(). */
static devupd_gate_t   s_gate;
/** @brief An install whose result is not known waits for the device's
 *         version, the `blec` task's own. @{ */
static bool            s_settle;
static uint32_t        s_settle_t;  /**< when the transfer ended */
/** @} */
/** @brief The frame task's last end_seq. */
static uint32_t        s_seen_end;
/** @brief The device the frame task last asked to update. */
static unsigned        s_ask_dev;
/** @brief The frame task's last phase, for the band's ask. */
static uint8_t         s_seen_phase;

bool panel_devupd_busy(void)
{
    return devupd_gate_busy(&s_gate);
}

void panel_devupd_start(void)
{
#ifdef RCBENCH_DIAL_IMAGE
    s_img[0].p = _binary_rcbench_dial_bin_start;
    /* cppcheck takes the two linker symbols for two arrays. */
    // cppcheck-suppress subtractPointers
    s_img[0].n = (uint32_t)(_binary_rcbench_dial_bin_end - _binary_rcbench_dial_bin_start);
#endif
#ifdef RCBENCH_NODE_PWR_IMAGE
    s_img[1].p = _binary_rcbench_node_pwr_bin_start;
    /* cppcheck takes the two linker symbols for two arrays. */
    // cppcheck-suppress subtractPointers
    s_img[1].n = (uint32_t)(_binary_rcbench_node_pwr_bin_end - _binary_rcbench_node_pwr_bin_start);
#endif
#ifdef RCBENCH_NODE_BAL_IMAGE
    s_img[2].p = _binary_rcbench_node_bal_bin_start;
    /* cppcheck takes the two linker symbols for two arrays. */
    // cppcheck-suppress subtractPointers
    s_img[2].n = (uint32_t)(_binary_rcbench_node_bal_bin_end - _binary_rcbench_node_bal_bin_start);
#endif
    for (unsigned i = 0; i < BLEOTA_KIND_MAX; ++i) {
        s_img[i].st = s_img[i].p != NULL ? IMG_DUE : IMG_NONE;
    }
    s_started = devupd_client_init(&s_cli, PANEL_BLEC_UPD);
    if (!s_started) {
        ESP_LOGE(TAG, "update client not set up; no device updates");
    }
}

/**
 * @brief Check the next image that waits for its check, one a poll: a poll
 *        that checks runs no longer than one image's two passes.
 */
static void check_next(void)
{
    for (unsigned i = 0; i < BLEOTA_KIND_MAX; ++i) {
        carried_t *c = &s_img[i];
        if (c->st != IMG_DUE) {
            continue;
        }
        c->err = fw_image_check_as(c->p, c->n, c->slot, c->chip, c->project,
                                   &c->info);
        if (c->err == FW_OK
            && bleota_head_check(c->p, c->n, c->chip, c->kind) != BLEOTA_OK) {
            /* The role in its .rodata_custom_desc names another device:
             * the two node roles share chip and project. */
            c->err = FW_ERR_PROJECT;
        }
        if (c->err != FW_OK) {
            c->st = IMG_BAD;
            ESP_LOGE(TAG, "the carried %s image fails its check: %s",
                     c->word, fw_err_str(c->err));
            return;
        }
        fw_sha256(c->p, c->n, c->sha);
        c->st = IMG_READY;
        ESP_LOGI(TAG, "carries the %s image %s, %lu bytes, %lu%% of its "
                      "slot, SHA-256 %02x%02x%02x%02x", c->word,
                 c->info.version_text, (unsigned long)c->n,
                 (unsigned long)((uint64_t)c->n * 100u / c->slot),
                 c->sha[0], c->sha[1], c->sha[2], c->sha[3]);
        return;
    }
}

/**
 * @brief The running update's device as the console names it.
 * @return its name, from the carried image's table
 */
static const char *cur_word(void)
{
    return s_img[s_cur].word;
}

/**
 * @brief End the update: the console's line, the device's link back, the
 *        frame told.
 * @param ok   installed
 * @param err  devupd_err_t otherwise
 * @param code the device's BLEOTA_E_*
 * @param now  the clock
 */
static void finish(bool ok, uint8_t err, uint8_t code, uint32_t now)
{
    const uint32_t ms = now - s_t0;
    s_stage = STAGE_IDLE;
    k_dev[s_cur].hold(false, now);
    devupd_gate_end(&s_gate);
    if (!ok && err == DEVUPD_E_UNSURE) {
        ESP_LOGW(TAG, "%s update: the link ended after END, or DONE did "
                      "not come; its version after its restart says "
                      "whether %u.%u.%u is installed", cur_word(),
                 (unsigned)s_im.version.major, (unsigned)s_im.version.minor,
                 (unsigned)s_im.version.patch);
        s_settle = true;
        s_settle_t = now;
        return;
    }
    if (ok) {
        ESP_LOGI(TAG, "%s update done: %u.%u.%u installed, %lu bytes in "
                      "%lums, %lu bytes/s, %lu rewinds, %lu polls; it "
                      "restarts", cur_word(), (unsigned)s_im.version.major,
                 (unsigned)s_im.version.minor, (unsigned)s_im.version.patch,
                 (unsigned long)s_im.size, (unsigned long)ms,
                 (unsigned long)(ms > 0u ? (uint64_t)s_im.size * 1000u / ms
                                         : 0u),
                 (unsigned long)s_cli.e.rewinds,
                 (unsigned long)s_cli.e.polls_sent);
    } else {
        ESP_LOGW(TAG, "%s update failed: error %u, device code %u, at %lu "
                      "of %lu bytes after %lums; it keeps its firmware",
                 cur_word(), (unsigned)err, (unsigned)code,
                 (unsigned long)s_cli.e.next, (unsigned long)s_im.size,
                 (unsigned long)ms);
    }
    s_next.ok = ok;
    s_next.unknown = false;
    s_next.err = err;
    s_next.code = code;
    ++s_next.end_seq;
}

/**
 * @brief Settle an install whose result is not known by the version the
 *        device reports once it is back (devupd_settle()).
 * @param linked the device is connected and reported its version
 * @param ver    that version
 * @param now    the clock
 */
static void settle(bool linked, const uint8_t ver[3], uint32_t now)
{
    const fw_version_t run = { ver[0], ver[1], ver[2] };
    const devupd_settle_t r = devupd_settle(linked, run, s_im.version,
                                            now - s_settle_t);
    if (r == DEVUPD_SETTLE_WAIT) {
        return;
    }
    s_settle = false;
    ESP_LOGI(TAG, "%s update settled: %s, it runs %u.%u.%u", cur_word(),
             r == DEVUPD_SETTLE_DONE ? "installed"
             : r == DEVUPD_SETTLE_FAILED ? "not installed" : "not known",
             (unsigned)ver[0], (unsigned)ver[1], (unsigned)ver[2]);
    s_next.ok = r == DEVUPD_SETTLE_DONE;
    s_next.unknown = r == DEVUPD_SETTLE_UNKNOWN;
    s_next.err = DEVUPD_E_LOST;
    s_next.code = 0u;
    ++s_next.end_seq;
}

/**
 * @brief The row's hold: hold the device's own link off, if the device is
 *        there and its image ready.
 * @param dev    the device, by s_img's index
 * @param bound  it is bound
 * @param linked it is connected and reported its version
 * @param ver    that version
 * @param now    the clock
 */
static void begin(unsigned dev, bool bound, bool linked, const uint8_t ver[3],
                  uint32_t now)
{
    s_cur = dev;
    const carried_t *c = &s_img[dev];
    if (!s_started || !bound || !linked || c->st != IMG_READY
        || s_stage != STAGE_IDLE) {
        ESP_LOGW(TAG, "%s update refused: bound %d, linked %d, image %u",
                 cur_word(), bound, linked, (unsigned)c->st);
        s_t0 = now;
        finish(false, DEVUPD_E_LINK, 0u, now);
        return;
    }
    memset(&s_im, 0, sizeof(s_im));
    s_im.img = c->p;
    s_im.size = c->n;
    s_im.kind = c->kind;
    s_im.version = c->info.version;
    memcpy(s_im.sha, c->sha, sizeof(s_im.sha));
    const fw_version_t run = { ver[0], ver[1], ver[2] };
    s_im.reinstall = fw_version_cmp(run, s_im.version) == 0;
    ESP_LOGI(TAG, "%s update%s: %s, %lu bytes; it runs %u.%u.%u; its link "
                  "is held off", cur_word(),
             s_im.reinstall ? " (reinstall)" : "", c->info.version_text,
             (unsigned long)s_im.size, (unsigned)ver[0], (unsigned)ver[1],
             (unsigned)ver[2]);
    s_t0 = now;
    s_logged_pct = 0u;
    s_logged_phase = DEVUPD_IDLE;
    s_stage = STAGE_HOLD;
    s_stage_t = now;
    k_dev[dev].hold(true, now);
}

/**
 * @brief The console's lines of the transfer's phases and progress.
 */
static void log_progress(void)
{
    const devupd_t *e = &s_cli.e;
    if (e->phase != s_logged_phase) {
        s_logged_phase = e->phase;
        if (e->phase == DEVUPD_BEGIN) {
            ESP_LOGI(TAG, "%s update: link up, MTU %u, %u bytes a chunk",
                     cur_word(), (unsigned)s_cli.b.mtu, (unsigned)e->chunk);
        } else if (e->phase == DEVUPD_CONFIRM) {
            ESP_LOGI(TAG, "%s update: it asks for its confirmation, %us",
                     cur_word(), (unsigned)e->secs);
        } else if (e->phase == DEVUPD_END) {
            ESP_LOGI(TAG, "%s update: every byte taken, END sent",
                     cur_word());
        }
    }
    const unsigned pct = devupd_percent(e);
    if (e->phase == DEVUPD_SEND && pct >= s_logged_pct + LOG_STEP_PCT) {
        s_logged_pct = pct - pct % LOG_STEP_PCT;
        ESP_LOGI(TAG, "%s update %u%%: %lu of %lu bytes, %lu rewinds, %lu "
                      "polls", cur_word(), pct, (unsigned long)e->next,
                 (unsigned long)e->im.size, (unsigned long)e->rewinds,
                 (unsigned long)e->polls_sent);
    }
}

/** @brief What each device's link says, at this poll. */
typedef struct {
    bleu_addr_t addr;   /**< its bound address */
    bool        bound;  /**< one is bound */
    bool        linked; /**< connected, its version read */
    bool        down;   /**< no link stands or is under way */
    uint8_t     ver[3]; /**< its version */
} seen_t;

void panel_devupd_blec_poll(uint32_t now)
{
    if (!s_started) {
        return;
    }
    check_next();
    seen_t t[BLEOTA_KIND_MAX];
    memset(t, 0, sizeof(t));
    for (unsigned i = 0; i < BLEOTA_KIND_MAX; ++i) {
        t[i].bound = k_dev[i].target(&t[i].addr, &t[i].linked, t[i].ver,
                                     &t[i].down);
    }
    /* The control task's live flag, which it hands the Dial: ARMED or an
     * arm waiting, for every device. */
    const bool live = panel_dial_live();
    unsigned dev = 0u;
    const devupd_take_t take = devupd_gate_take(&s_gate, live, &dev);
    if (dev >= BLEOTA_KIND_MAX) {
        dev = 0u;
    }
    if (take == DEVUPD_TAKE_START) {
        s_settle = false;
        begin(dev, t[dev].bound, t[dev].linked, t[dev].ver, now);
    } else if (take == DEVUPD_TAKE_ARMED) {
        s_cur = dev;
        ESP_LOGW(TAG, "%s update refused: the bench is ARMED", cur_word());
        s_t0 = now;
        s_stage = STAGE_HOLD;
        finish(false, DEVUPD_E_ARMED, 0u, now);
    }
    const seen_t *c = &t[s_cur];
    if (s_stage == STAGE_HOLD && live) {
        ESP_LOGW(TAG, "%s update ended before its start: ARMED", cur_word());
        finish(false, DEVUPD_E_ARMED, 0u, now);
    }
    if (s_stage == STAGE_HOLD) {
        if (c->bound && c->down) {
            (void)devupd_client_start(&s_cli, &c->addr, &s_im, now);
            s_stage = STAGE_RUN;
        } else if (now - s_stage_t >= HOLD_MS) {
            ESP_LOGW(TAG, "%s update: its own link did not end in %ums",
                     cur_word(), (unsigned)HOLD_MS);
            finish(false, DEVUPD_E_LINK, 0u, now);
        }
    }
    if (s_stage == STAGE_RUN) {
        log_progress();
        if (!devupd_busy(&s_cli.e) && !s_cli.on) {
            finish(s_cli.e.phase == DEVUPD_DONE, s_cli.e.err,
                   s_cli.e.dev_code, now);
        }
    }
    if (s_settle && s_stage == STAGE_IDLE) {
        settle(c->bound && c->linked, c->ver, now);
    }
    for (unsigned i = 0; i < BLEOTA_KIND_MAX; ++i) {
        const carried_t *g = &s_img[i];
        dev_pub_t *d = &s_next.d[i];
        d->img_due = g->st == IMG_DUE;
        d->have_img = g->st != IMG_NONE;
        d->img_ok = g->st == IMG_READY;
        d->carried = g->info.version;
        d->linked = t[i].bound && t[i].linked;
        d->running = (fw_version_t){ t[i].ver[0], t[i].ver[1], t[i].ver[2] };
    }
}

void panel_devupd_blec_event(const bleu_evt_t *e, uint32_t now)
{
    if (s_started) {
        devupd_client_event(&s_cli, e, now);
    }
}

bool panel_devupd_blec_step(uint32_t now, bleu_act_t *out)
{
    return s_started && devupd_client_step(&s_cli, now, out);
}

void panel_devupd_blec_chunks(uint32_t now)
{
    if (!s_started || s_stage != STAGE_RUN) {
        return;
    }
    devupd_out_t o;
    uint16_t conn = 0u, handle = 0u;
    for (int i = 0; i < PANEL_DEVUPD_CHUNKS
         && devupd_client_chunk(&s_cli, now, &o, &conn, &handle); ++i) {
        uint8_t hdr[BLEOTA_CHUNK_HDR];
        bleota_chunk_header(o.off, hdr);
        const int rc = panel_ble_write_chunk(conn, handle, hdr, sizeof(hdr),
                                             s_im.img + o.off, o.n);
        if (rc != 0) {
            devupd_client_unsend(&s_cli, &o);
            break;
        }
    }
}

void panel_devupd_blec_publish(uint32_t now)
{
    (void)now;
    if (!s_started) {
        return;
    }
    s_next.dev = (uint8_t)s_cur;
    s_next.phase = s_stage == STAGE_HOLD ? (uint8_t)DEVUPD_LINK
                                         : s_cli.e.phase;
    s_next.percent = devupd_percent(&s_cli.e);
    s_next.secs = s_cli.e.secs;
    taskENTER_CRITICAL(&s_mux);
    s_pub = s_next;
    taskEXIT_CRITICAL(&s_mux);
}

/**
 * @brief The band's reason for a failed update.
 * @param p   what was published
 * @param buf room for an "error %u" text
 * @param n   its bytes
 * @return the text
 */
static const char *fail_text(const pub_t *p, char *buf, size_t n)
{
    if (p->err != DEVUPD_E_DEVICE) {
        switch (p->err) {
        case DEVUPD_E_LINK:    return TR(DEV_FAIL_LINK);
        case DEVUPD_E_LOST:    return TR(DEV_FAIL_LOST);
        case DEVUPD_E_TIMEOUT: return TR(DEV_FAIL_TIMEOUT);
        case DEVUPD_E_WRITE:   return TR(DEV_FAIL_WRITE);
        case DEVUPD_E_ARMED:   return TR(DEV_FAIL_ARMED);
        default:
            snprintf(buf, n, TR(DEV_FAIL_OTHER), (unsigned)p->err);
            return buf;
        }
    }
    switch (p->code) {
    case BLEOTA_E_KIND:       return TR(DEV_FAIL_KIND);
    case BLEOTA_E_SIZE:       return TR(DEV_FAIL_SIZE);
    case BLEOTA_E_SAME:       return TR(DEV_FAIL_SAME);
    case BLEOTA_E_NO_CONFIRM: return TR(DEV_FAIL_CONFIRM);
    case BLEOTA_E_REFUSED:    return TR(DEV_FAIL_REFUSED);
    case BLEOTA_E_SHA:        return TR(DEV_FAIL_SHA);
    case BLEOTA_E_FLASH:      return TR(DEV_FAIL_FLASH);
    case BLEOTA_E_IMAGE:      return TR(DEV_FAIL_IMAGE);
    case BLEOTA_E_TIMEOUT:    return TR(DEV_FAIL_TIMEOUT);
    default:
        snprintf(buf, n, TR(DEV_FAIL_OTHER), (unsigned)p->code);
        return buf;
    }
}

void devupd_service(bool armed)
{
    pub_t p;
    taskENTER_CRITICAL(&s_mux);
    p = s_pub;
    taskEXIT_CRITICAL(&s_mux);
    const bool busy = devupd_gate_busy(&s_gate);
    const bool other = panel_c6_busy() || panel_cu_busy() || panel_fw_busy();
    for (unsigned i = 0; i < BLEOTA_KIND_MAX; ++i) {
        const dev_pub_t *d = &p.d[i];
        /* The frame asks alone, so the device it asked for is the one the
         * gate holds while it reads busy. */
        const bool mine = busy && s_ask_dev == i;
        const devupd_row_in_t in = {
            .have_image = d->have_img, .image_due = d->img_due,
            .image_ok = d->img_ok,
            .linked = d->linked, .known = d->linked, .running = d->running,
            .carried = d->carried, .armed = armed,
            .other = other || (busy && !mine), .busy = mine,
        };
        const upd_dev_view_t v = {
            .row = devupd_row(&in), .known = d->linked,
            .running = d->running, .have_image = d->img_ok,
            .carried = d->carried, .phase = mine ? p.phase : 0u,
            .percent = mine ? p.percent : 0u, .secs = mine ? p.secs : 0u,
        };
        upd_dev_set((upd_dev_id_t)i, &v);
        if (upd_dev_take((upd_dev_id_t)i)) {
            if (v.row == DEVUPD_ROW_READY || v.row == DEVUPD_ROW_REINSTALL) {
                s_ask_dev = i;
                devupd_gate_ask(&s_gate, i);
            } else {
                ui_router_set_notice(upd_dev_reason(&v));
            }
        }
    }
    const unsigned dev = p.dev < BLEOTA_KIND_MAX ? p.dev : 0u;
    if (busy && p.phase == DEVUPD_CONFIRM && s_seen_phase != DEVUPD_CONFIRM) {
        ui_router_set_notice(ui_tr(k_dev[dev].ask));
    }
    s_seen_phase = p.phase;
    if (p.end_seq != s_seen_end) {
        s_seen_end = p.end_seq;
        char line[128], why[40];
        char ver[24];
        const fw_version_t car = p.d[dev].carried;
        snprintf(ver, sizeof(ver), "%u.%u.%u", (unsigned)car.major,
                 (unsigned)car.minor, (unsigned)car.patch);
        const char *name = ui_tr(k_dev[dev].name);
        if (p.ok) {
            snprintf(line, sizeof(line), TR(ALERT_DEV_DONE), name, ver);
        } else if (p.unknown) {
            snprintf(line, sizeof(line), TR(ALERT_DEV_UNKNOWN), name);
        } else {
            snprintf(line, sizeof(line), TR(ALERT_DEV_FAILED), name,
                     fail_text(&p, why, sizeof(why)));
        }
        control_alert(line);
    }
}
