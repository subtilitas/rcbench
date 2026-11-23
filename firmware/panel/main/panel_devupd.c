/**
 * @file panel_devupd.c
 * @brief The wireless devices' update on the panel (panel_devupd.h): the
 *        images of the release the panel runs, fetched by a row's hold
 *        (panel_devimg.h), the `blec` task's update client, and the
 *        frame's rows of UPDATES' devices' page.
 *
 * Three rows, four images: the power node's row sends the image of the
 * board its INFO names, the ESP32-C6-Zero's (BLEOTA_KIND_NODE_POWER) or
 * the ES3C28P display node's (BLEOTA_KIND_NODE_POWER_S3), chosen by
 * devimg_pnode_image() and held to that board by devimg_fits() before
 * the first byte goes out.
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
#include "panel_devimg.h"
#include "panel_dial.h"
#include "panel_pnode.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "upd_dev.h"
#include "upd_screen.h"
#include "upd_wizard.h"
#include "wifi_access.h"

/** @brief The console lines' tag. */
static const char *TAG = "devupd";

/** @brief From the hold to the device's own link down, ms at most. */
#define HOLD_MS 5000u
/** @brief The console's progress line, every this many percent. */
#define LOG_STEP_PCT 10u

_Static_assert(PANEL_BLEC_UPD == 4u, "the update client is the hub's owner 4");
_Static_assert(PANEL_BLEC_UPD < BLEU_OWNER_MAX,
               "the update client's owner fits a token");
/** @brief The rows, BLEOTA_KIND_DIAL to BLEOTA_KIND_NODE_BAL less one:
 *         the display node is the power node's row with another image. */
#define DEV_N BLEOTA_KIND_NODE_BAL

_Static_assert(UPD_DEV_COUNT == BLEOTA_KIND_NODE_BAL
                   && UPD_DEV_DIAL == BLEOTA_KIND_DIAL - 1
                   && UPD_DEV_PNODE == BLEOTA_KIND_NODE_POWER - 1
                   && UPD_DEV_BAL == BLEOTA_KIND_NODE_BAL - 1,
               "a row, its image and its device share one index");

/** @brief One device's image, as `blec` sends it: taken from
 *         panel_devimg.c once it arrived and passed its checks there. */
typedef struct {
    const char     *word;    /**< its device's name on the console */
    bool            ready;   /**< taken: the fields below are set */
    const uint8_t  *p;       /**< its first byte */
    uint32_t        n;       /**< its length */
    fw_image_info_t info;    /**< what it says */
    uint8_t         sha[32]; /**< the SHA-256 of every byte */
} carried_t;

/** @brief The images, by devimg_id_t, which is their BLEOTA_KIND_*
 *         (devimg.c); the C6 radio's is panel_c6.c's.  `blec`'s own. */
static carried_t s_img[DEVIMG_COUNT] = {
    [DEVIMG_DIAL] = { .word = "Dial" },
    [DEVIMG_PNODE] = { .word = "power node" },
    [DEVIMG_BAL] = { .word = "balance node" },
    [DEVIMG_PNODE_S3] = { .word = "display node" },
};

/**
 * @brief The image row @p dev sends: the power node's row the one of the
 *        board its INFO named (devimg_pnode_image()).
 * @param dev   the row, by BLEOTA_KIND_* - 1
 * @param board the power node's RCNODE_BOARD_*, 0 before INFO
 * @return the image; DEVIMG_COUNT for a board no release image is for
 */
static devimg_id_t row_img(unsigned dev, uint8_t board)
{
    switch (dev) {
    case UPD_DEV_DIAL:
        return DEVIMG_DIAL;
    case UPD_DEV_PNODE:
        return devimg_pnode_image(board);
    case UPD_DEV_BAL:
        return DEVIMG_BAL;
    default:
        return DEVIMG_COUNT;
    }
}

/** @brief A device's own client, as the update uses it. */
typedef struct {
    ui_text_id_t name; /**< its name in the band's lines */
    ui_text_id_t ask;  /**< the band's ask during the confirmation */
    /** @brief The hold on its own link (panel_dial_blec_hold()). */
    void (*hold)(bool on, uint32_t now);
    /** @brief What its link says (panel_dial_blec_target()). */
    bool (*target)(bleu_addr_t *addr, bool *linked, uint8_t version[3],
                   bool *down);
    /** @brief The board its INFO named (panel_pnode_blec_board()), which
     *         picks its image; NULL: one image for every board. */
    uint8_t (*board)(void);
    /** @brief The device's switch on WIRELESS is OFF
     *         (panel_dial_blec_off()). */
    bool (*off)(void);
} dev_hooks_t;

/** @brief The devices, by BLEOTA_KIND_* - 1, as s_img. */
static const dev_hooks_t k_dev[DEV_N] = {
    { TX_DEV_NAME_DIAL, TX_ALERT_DIAL_ASK, panel_dial_blec_hold,
      panel_dial_blec_target, NULL, panel_dial_blec_off },
    { TX_DEV_NAME_PNODE, TX_ALERT_NODE_ASK, panel_pnode_blec_hold,
      panel_pnode_blec_target, panel_pnode_blec_board,
      panel_pnode_blec_off },
    { TX_DEV_NAME_BAL, TX_ALERT_NODE_ASK, panel_bal_blec_hold,
      panel_bal_blec_target, NULL, panel_bal_blec_off },
};

/** @brief Where the `blec` task's update stands. */
typedef enum {
    STAGE_IDLE = 0, /**< none */
    STAGE_HOLD,     /**< the device's own link ends */
    STAGE_RUN,      /**< the client runs the transfer */
} stage_t;

/** @brief What a device's link says, for its row. */
typedef struct {
    bool         other;     /**< it runs a board the release has no image
                                 for to send over Bluetooth LE */
    uint8_t      board;     /**< the board its INFO named, 0 for none */
    bool         bound;     /**< a device of the kind is bound */
    bool         off;       /**< bound, its switch on WIRELESS OFF */
    bool         linked;    /**< the device is connected, its version read */
    fw_version_t running;   /**< that version */
} dev_pub_t;

/** @brief One update's end, for the band. */
typedef struct {
    uint8_t      dev;       /**< its device's row */
    uint8_t      img;       /**< the image sent, devimg_id_t */
    bool         ok;        /**< it installed */
    bool         unknown;   /**< its install's result is not known */
    uint8_t      err;       /**< otherwise devupd_err_t */
    uint8_t      code;      /**< and the device's BLEOTA_E_* */
    fw_version_t ver;       /**< the image's version */
} end_t;

/** @brief Ends the publication keeps: a settle reported at a new start and
 *         that start's refusal can fall in one poll. */
#define END_RING 4u

/** @brief What the `blec` task publishes for the frame. */
typedef struct {
    dev_pub_t    d[DEV_N]; /**< each device */
    uint8_t      dev;       /**< the device of the running or last update */
    uint8_t      phase;     /**< devupd_phase_t of the transfer */
    unsigned     percent;   /**< its share taken */
    unsigned     secs;      /**< DEVUPD_CONFIRM: seconds given */
    uint32_t     end_seq;   /**< updates ended, counted */
    end_t        ends[END_RING]; /**< the last ends, by end_seq % END_RING */
} pub_t;

/** @brief The update client, the `blec` task's own. */
static devupd_client_t s_cli;
/** @brief The `blec` task's stage. */
static stage_t         s_stage;
/** @brief When the stage began. */
static uint32_t        s_stage_t;
/** @brief The image being sent. */
static devupd_image_t  s_im;
/** @brief The row of the running or last update. */
static unsigned        s_cur;
/** @brief The image of the running or last update, devimg_id_t. */
static unsigned        s_cur_img = DEVIMG_DIAL;
/** @brief The image of the install the settler waits on, devimg_id_t. */
static unsigned        s_settle_img = DEVIMG_DIAL;
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
/** @brief An install whose result is not known waits for its own
 *         device's version, the `blec` task's own. */
static devupd_settler_t s_settler;
/** @brief The frame asked to cancel the running update. */
static atomic_bool     s_cancel;
/** @brief The frame task's last end_seq: the ends it reported. */
static uint32_t        s_seen_end;
/** @brief The rows were published once, or the client was not set up and
 *         publishes none: panel_devupd_known(). */
static atomic_bool     s_known;
/** @brief The device the frame task last asked to update. */
static unsigned        s_ask_dev;
/** @brief Each row's image at the frame task's last look, devimg_id_t. */
static devimg_id_t     s_row_img[DEV_N] = {
    DEVIMG_DIAL, DEVIMG_PNODE, DEVIMG_BAL,
};
/** @brief The frame task's last phase, for the band's ask. */
static uint8_t         s_seen_phase;

bool panel_devupd_busy(void)
{
    return devupd_gate_busy(&s_gate);
}

fw_upd_t panel_devupd_running(void)
{
    /* The device is stored before the request (devupd_gate_ask()), and
     * the request read before the run: a gate that reads busy names the
     * device its ask named. */
    if (!devupd_gate_busy(&s_gate)) {
        return FW_UPD_NONE;
    }
    static const fw_upd_t k_upd[DEV_N] = {
        FW_UPD_DIAL, FW_UPD_PNODE, FW_UPD_BAL,
    };
    const unsigned d = atomic_load(&s_gate.dev);
    return d < DEV_N ? k_upd[d] : FW_UPD_DIAL;
}

bool panel_devupd_known(void)
{
    return atomic_load(&s_known);
}

void panel_devupd_start(void)
{
    s_started = devupd_client_init(&s_cli, PANEL_BLEC_UPD);
    if (!s_started) {
        ESP_LOGE(TAG, "update client not set up; no device updates");
        atomic_store(&s_known, true);
    }
}

/**
 * @brief Take image @p id from panel_devimg.c once it is there, checked:
 *        `blec`'s, at the poll's start and at an update's start.
 * @param id the image, devimg_id_t; the C6's and one out of range are
 *           never taken
 * @return true when the image is taken
 */
static bool adopt(unsigned id)
{
    if (id == DEVIMG_C6 || id >= DEVIMG_COUNT) {
        return false;
    }
    carried_t *c = &s_img[id];
    if (!c->ready
        && panel_devimg_get((devimg_id_t)id, &c->p, &c->n, &c->info,
                            c->sha)) {
        c->ready = true;
        ESP_LOGI(TAG, "the %s image %s, %lu bytes, %lu%% of its slot, "
                      "SHA-256 %02x%02x%02x%02x", c->word,
                 c->info.version_text, (unsigned long)c->n,
                 (unsigned long)((uint64_t)c->n * 100u
                                 / devimg_slot((devimg_id_t)id)),
                 c->sha[0], c->sha[1], c->sha[2], c->sha[3]);
    }
    return c->ready;
}

/** @brief What each device's link says, at this poll. */
typedef struct {
    bleu_addr_t addr;   /**< its bound address */
    bool        bound;  /**< one is bound */
    bool        linked; /**< connected, its version read */
    bool        down;   /**< no link stands or is under way */
    bool        other;  /**< it runs a board the release sends no image to */
    uint8_t     board;  /**< the board its INFO named, 0 for none */
    unsigned    img;    /**< the image its row sends, devimg_id_t */
    uint8_t     ver[3]; /**< its version */
} seen_t;

/**
 * @brief Publish one update's end for the band, with the version of the
 *        image being sent.
 * @param dev     its device's row
 * @param img     the image it was sent, devimg_id_t
 * @param ok      it installed
 * @param unknown its install's result is not known
 * @param err     devupd_err_t otherwise
 * @param code    the device's BLEOTA_E_*
 */
static void push_end(unsigned dev, unsigned img, bool ok, bool unknown,
                     uint8_t err, uint8_t code)
{
    end_t *e = &s_next.ends[s_next.end_seq % END_RING];
    e->dev = (uint8_t)dev;
    e->img = (uint8_t)img;
    e->ok = ok;
    e->unknown = unknown;
    e->err = err;
    e->code = code;
    e->ver = s_im.version;
    ++s_next.end_seq;
}

/**
 * @brief The running update's device as the console names it.
 * @return its name, from the images' table
 */
static const char *cur_word(void)
{
    return s_cur_img < DEVIMG_COUNT && s_img[s_cur_img].word != NULL
               ? s_img[s_cur_img].word
               : "device";
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
        devupd_settler_arm(&s_settler, s_cur, s_im.version, now);
        s_settle_img = s_cur_img;
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
    push_end(s_cur, s_cur_img, ok, false, err, code);
}

/**
 * @brief Report the pending settle when its own device has reported its
 *        version, its time ran out, or a new update is to start
 *        (devupd_settler_step()).
 * @param t         what each device's link says, by s_img's index
 * @param now       the clock
 * @param now_start a new update is to start
 */
static void settle(const seen_t t[DEV_N], uint32_t now,
                   bool now_start)
{
    devupd_seen_t seen[DEV_N];
    for (unsigned i = 0; i < DEV_N; ++i) {
        seen[i].known = t[i].bound && t[i].linked;
        seen[i].running = (fw_version_t){ t[i].ver[0], t[i].ver[1],
                                          t[i].ver[2] };
    }
    const fw_version_t want = s_settler.want;
    devupd_settled_t r;
    if (!devupd_settler_step(&s_settler, seen, DEV_N, now,
                             now_start, &r)) {
        return;
    }
    ESP_LOGI(TAG, "%s update settled%s: %s, it runs %u.%u.%u",
             s_img[s_settle_img].word,
             now_start ? " before a new start" : "",
             r.r == DEVUPD_SETTLE_DONE ? "installed"
             : r.r == DEVUPD_SETTLE_FAILED ? "not installed" : "not known",
             (unsigned)r.running.major, (unsigned)r.running.minor,
             (unsigned)r.running.patch);
    push_end(r.dev, s_settle_img, r.r == DEVUPD_SETTLE_DONE,
             r.r == DEVUPD_SETTLE_UNKNOWN, DEVUPD_E_LOST, 0u);
    s_next.ends[(s_next.end_seq - 1u) % END_RING].ver = want;
}

/**
 * @brief The row's hold: hold the device's own link off, if the device is
 *        there and its image ready and fit for it.  The power node's image
 *        is the one of the board its INFO named on the link that reported
 *        its version, and devimg_fits() holds the image's head to that
 *        board's chip and kind: a mismatch ends here, before the link is
 *        touched or a byte is sent.
 * @param dev the device's row
 * @param t   what its link says
 * @param now the clock
 */
static void begin(unsigned dev, const seen_t *t, uint32_t now)
{
    s_cur = dev;
    s_cur_img = t->img;
    const bool linked = t->linked && !t->other;
    const bool ready = adopt(t->img);
    if (!s_started || !t->bound || !linked || !ready
        || s_stage != STAGE_IDLE) {
        ESP_LOGW(TAG, "%s update refused: bound %d, linked %d, image %d",
                 cur_word(), t->bound, linked, ready);
        s_t0 = now;
        finish(false, DEVUPD_E_LINK, 0u, now);
        return;
    }
    const carried_t *c = &s_img[t->img];
    if (dev == UPD_DEV_PNODE) {
        const devimg_err_t fit = devimg_fits((devimg_id_t)t->img, t->board,
                                             c->p, c->n);
        if (fit != DEVIMG_OK) {
            ESP_LOGW(TAG, "%s update refused: the image does not fit the "
                          "node's board %u (check %u)", cur_word(),
                     (unsigned)t->board, (unsigned)fit);
            s_t0 = now;
            finish(false, DEVUPD_E_IMAGE, 0u, now);
            return;
        }
    }
    memset(&s_im, 0, sizeof(s_im));
    s_im.img = c->p;
    s_im.size = c->n;
    s_im.kind = devimg_kind((devimg_id_t)t->img);
    s_im.version = c->info.version;
    memcpy(s_im.sha, c->sha, sizeof(s_im.sha));
    const fw_version_t run = { t->ver[0], t->ver[1], t->ver[2] };
    s_im.reinstall = fw_version_cmp(run, s_im.version) == 0;
    ESP_LOGI(TAG, "%s update%s: %s, %lu bytes; it runs %u.%u.%u; its link "
                  "is held off", cur_word(),
             s_im.reinstall ? " (reinstall)" : "", c->info.version_text,
             (unsigned long)s_im.size, (unsigned)t->ver[0],
             (unsigned)t->ver[1], (unsigned)t->ver[2]);
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

void panel_devupd_blec_poll(uint32_t now)
{
    if (!s_started) {
        return;
    }
    for (unsigned i = DEVIMG_DIAL; i < DEVIMG_COUNT; ++i) {
        (void)adopt(i);
    }
    seen_t t[DEV_N];
    memset(t, 0, sizeof(t));
    for (unsigned i = 0; i < DEV_N; ++i) {
        t[i].bound = k_dev[i].target(&t[i].addr, &t[i].linked, t[i].ver,
                                     &t[i].down);
        t[i].board = k_dev[i].board != NULL ? k_dev[i].board() : 0u;
        t[i].img = row_img(i, t[i].board);
        /* A board no image of the release is for: its row reads NO
         * FIRMWARE, and a hold sends nothing. */
        t[i].other = t[i].bound && t[i].img >= DEVIMG_COUNT;
    }
    /* The control task's live flag, which it hands the Dial: ARMED or an
     * arm waiting, for every device. */
    const bool live = panel_dial_live();
    unsigned dev = 0u;
    const devupd_take_t take = devupd_gate_take(&s_gate, live, &dev);
    if (dev >= DEV_N) {
        dev = 0u;
    }
    if (take == DEVUPD_TAKE_START) {
        /* A pending unknown install is reported first, from its own
         * device: the new update never reads as its result. */
        settle(t, now, true);
        begin(dev, &t[dev], now);
    } else if (take == DEVUPD_TAKE_ARMED) {
        s_cur = dev;
        s_cur_img = t[dev].img;
        ESP_LOGW(TAG, "%s update refused: the bench is ARMED", cur_word());
        s_t0 = now;
        s_stage = STAGE_HOLD;
        finish(false, DEVUPD_E_ARMED, 0u, now);
    }
    const seen_t *c = &t[s_cur];
    if (atomic_exchange(&s_cancel, false) && s_stage != STAGE_IDLE) {
        ESP_LOGW(TAG, "%s update: cancelled on UPDATES", cur_word());
        if (s_stage == STAGE_HOLD) {
            finish(false, DEVUPD_E_ABORTED, 0u, now);
        } else {
            /* From END on the device installs whatever comes: the abort
             * does nothing and the result follows. */
            devupd_client_abort(&s_cli, now);
        }
    }
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
    if (s_stage == STAGE_IDLE) {
        settle(t, now, false);
    }
    for (unsigned i = 0; i < DEV_N; ++i) {
        dev_pub_t *d = &s_next.d[i];
        /* A board no image of the release is for: none is sent to it. */
        d->other = t[i].other;
        d->board = t[i].board;
        d->bound = t[i].bound;
        d->off = t[i].bound && k_dev[i].off();
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
    atomic_store(&s_known, true);
}

/**
 * @brief The band's reason for a failed update.
 * @param p   the end
 * @param buf room for an "error %u" text
 * @param n   its bytes
 * @return the text
 */
static const char *fail_text(const end_t *p, char *buf, size_t n)
{
    if (p->err != DEVUPD_E_DEVICE) {
        switch (p->err) {
        case DEVUPD_E_LINK:    return TR(DEV_FAIL_LINK);
        case DEVUPD_E_LOST:    return TR(DEV_FAIL_LOST);
        case DEVUPD_E_TIMEOUT: return TR(DEV_FAIL_TIMEOUT);
        case DEVUPD_E_WRITE:   return TR(DEV_FAIL_WRITE);
        case DEVUPD_E_ARMED:   return TR(DEV_FAIL_ARMED);
        case DEVUPD_E_ABORTED: return TR(DEV_FAIL_CANCELLED);
        case DEVUPD_E_STALLED: return TR(DEV_FAIL_STALLED);
        case DEVUPD_E_NO_SERVICE: return TR(DEV_FAIL_NO_SERVICE);
        case DEVUPD_E_IMAGE:   return TR(DEV_FAIL_BOARD);
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
    /* Bluetooth LE that cannot link a device: no device step waits for it
     * in an update run.  Its start (STARTING) and a start not yet asked
     * for (OFF) come to READY by themselves. */
    const panel_ble_state_t ble = panel_ble_state();
    const bool no_ble = ble == PANEL_BLE_NO_LINK || ble == PANEL_BLE_NO_SYNC;
    for (unsigned i = 0; i < DEV_N; ++i) {
        const dev_pub_t *d = &p.d[i];
        /* The frame asks alone, so the device it asked for is the one the
         * gate holds while it reads busy. */
        const bool mine = busy && s_ask_dev == i;
        /* The image on its way reads as the update running; another
         * image on its way holds the row as another update does.  The
         * power node's row takes the image of its node's board
         * (pnode_ctl_board(), kept over a reconnect): when a new INFO or
         * another address changes it, an update owed to the other image
         * no longer starts. */
        const devimg_id_t id = row_img(i, d->board);
        panel_devimg_forget(devimg_follow(&s_row_img[i], id));
        const bool fetching = panel_devimg_running(id);
        const fw_version_t rel = panel_devimg_want(id);
        const devupd_row_in_t in = {
            .have_image = !d->other, .image_due = false,
            .image_ok = !d->other,
            .linked = d->linked, .known = d->linked, .running = d->running,
            .carried = rel, .armed = armed,
            .other = other || (busy && !mine)
                     || (panel_devimg_busy() && !fetching),
            .busy = mine || fetching,
        };
        /* The row shows the update under way while its start is still
         * owed, so no SKIP or other hold appears under the radio's wait;
         * the gate above stays the one the owed start opens on. */
        devupd_row_in_t shown = in;
        shown.busy = in.busy || panel_devimg_pending(id);
        shown.other = in.other || panel_devimg_pending_other(id);
        const devupd_row_t gate_row = devupd_row(&in);
        upd_dev_view_t v = {
            .row = devupd_row(&shown), .bound = d->bound, .off = d->off,
            .no_ble = no_ble, .known = d->linked,
            .running = d->running, .have_image = !d->other,
            .display = id == DEVIMG_PNODE_S3,
            .carried = rel, .phase = mine ? p.phase : 0u,
            .percent = mine ? p.percent : 0u, .secs = mine ? p.secs : 0u,
        };
        panel_devimg_view(id, &v.fetch);
        upd_dev_set((upd_dev_id_t)i, &v);
        /* The gate's own reasons, for the band. */
        upd_dev_view_t gv = v;
        gv.row = gate_row;
        const bool open = gate_row == DEVUPD_ROW_READY
                          || gate_row == DEVUPD_ROW_REINSTALL;
        bool ask = false;
        if (upd_dev_take((upd_dev_id_t)i)) {
            if (!open || panel_devimg_pending(id)) {
                /* An owed start is this hold's already: it reads as the
                 * update running. */
                ui_router_set_notice(upd_dev_reason(open ? &v : &gv));
            } else if (panel_devimg_get(id, NULL, NULL, NULL, NULL)) {
                ask = true;
            } else {
                /* Not in memory: the download or the card read first;
                 * the update starts once the image passed its checks. */
                (void)panel_devimg_request(id, armed);
            }
        }
        /* After a download the radio goes off, and its time request can
         * hold it up to 30s longer: the transfer runs on the C6's
         * Bluetooth LE controller without Wi-Fi beside it. */
        const bool radio_free = !s_wifi_ok || wifi_access_down();
        switch (panel_devimg_owed(id, open && radio_free,
                                  open && !radio_free)) {
        case DEVIMG_OWED_FIRE:
            ask = true;
            break;
        case DEVIMG_OWED_WAIT:
            if (open && !radio_free) {
                wifi_access_end_linger();
            }
            break;
        case DEVIMG_OWED_DROP:
            ESP_LOGW(TAG, "%s: the image arrived; the update does not "
                          "start: row %u", s_img[id].word, (unsigned)v.row);
            if (upd_dev_reason(&gv) != NULL) {
                ui_router_set_notice(upd_dev_reason(&gv));
            }
            break;
        case DEVIMG_OWED_NONE:
        default:
            break;
        }
        if (ask) {
            s_ask_dev = i;
            devupd_gate_ask(&s_gate, i);
        }
    }
    const unsigned dev = p.dev < DEV_N ? p.dev : 0u;
    if (busy && p.phase == DEVUPD_CONFIRM && s_seen_phase != DEVUPD_CONFIRM) {
        ui_router_set_notice(ui_tr(k_dev[dev].ask));
    }
    s_seen_phase = p.phase;
    if (upd_dev_take_cancel()) {
        /* CANCEL UPDATE while the image is on its way, or while its
         * update is owed, drops the owed start and ends a download
         * (upd_service()); otherwise it ends the update that runs. */
        if (!panel_devimg_cancel()) {
            atomic_store(&s_cancel, true);
        }
    }
    /* Each end since the last frame, in order; more than the ring holds
     * only the last END_RING. */
    if (p.end_seq - s_seen_end > END_RING) {
        s_seen_end = p.end_seq - END_RING;
    }
    while (s_seen_end != p.end_seq) {
        const end_t *e = &p.ends[s_seen_end % END_RING];
        ++s_seen_end;
        char line[128], why[40];
        char ver[24];
        snprintf(ver, sizeof(ver), "%u.%u.%u", (unsigned)e->ver.major,
                 (unsigned)e->ver.minor, (unsigned)e->ver.patch);
        const unsigned d = e->dev < DEV_N ? e->dev : 0u;
        const bool dnode = d == UPD_DEV_PNODE && e->img == DEVIMG_PNODE_S3;
        const char *name = ui_tr(dnode ? TX_DEV_NAME_DNODE : k_dev[d].name);
        if (e->ok) {
            snprintf(line, sizeof(line), TR(ALERT_DEV_DONE), name, ver);
        } else if (e->unknown) {
            snprintf(line, sizeof(line), TR(ALERT_DEV_UNKNOWN), name);
        } else {
            snprintf(line, sizeof(line), TR(ALERT_DEV_FAILED), name,
                     fail_text(e, why, sizeof(why)));
        }
        control_alert(line);
        /* UPDATES' step of that device shows a failure on its card, the
         * hold the retry; a success clears it. */
        upd_screen_set_result(UPD_WIZ_DIAL + d, e->ok ? NULL : line);
    }
}
