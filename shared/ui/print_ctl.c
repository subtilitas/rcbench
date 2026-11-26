/**
 * @file print_ctl.c
 * @brief The label printer's controller (print_ctl.h), the `blec`
 *        task's.  The frame task's requests and view are print_req.c's.
 *
 * SPDX-License-Identifier: MIT
 */
#include "print_ctl.h"

#include <stdio.h>
#include <string.h>

_Static_assert(PRINT_BLE_OWNER < BLEU_OWNER_MAX, "the owner fits a token");
_Static_assert(SV_CMD_LEN <= BLEU_WRITE_MAX, "a command is one write");
_Static_assert(SV_FRAGMENT <= BLEU_WRITE_EXT_MAX,
               "a data fragment is one long write");
_Static_assert(PRINT_MTU_MIN == SV_FRAGMENT + 3u,
               "the MTU carries a fragment and the ATT header");
_Static_assert(PRINT_NAME_SIZE >= BLEU_ADDR_TEXT_MAX,
               "the view's printer holds an address text");

/** @brief The vendor service 0000e0ff-3c17-d293-8e48-14fe2e4da212, least
 *         significant byte first. */
static const uint8_t k_svc[16] = { 0x12, 0xA2, 0x4D, 0x2E, 0xFE, 0x14,
                                   0x48, 0x8E, 0x93, 0xD2, 0x17, 0x3C,
                                   0xFF, 0xE0, 0x00, 0x00 };

/** @brief The Supvan address prefix A4:93:40, in reading order. */
static const uint8_t k_oui[3] = { 0xA4, 0x93, 0x40 };

const bleu_profile_t print_profile = {
    .svc = k_svc,
    .chr = {
        { .uuid = NULL, .role = BLEU_ROLE_STREAM, .flags = 0u,
          .uuid16 = 0xFFE1u },
        { .uuid = NULL, .role = BLEU_ROLE_CMD, .flags = 0u,
          .uuid16 = 0xFFE9u },
        { .uuid = NULL, .role = BLEU_ROLE_NONE, .flags = 0u,
          .uuid16 = 0xFFEAu },
    },
    .chr_n = 3u,
    /* 20 to 40ms, latency 0, supervision 4s: the central sets them over
     * the printer's own wish of latency 5 and 3s. */
    .conn = { .itvl_min = 16u, .itvl_max = 32u, .latency = 0u,
              .supervision = 400u, .scan_itvl = BLEU_CONN_SCAN_ITVL,
              .scan_window = BLEU_CONN_SCAN_WINDOW },
    .adv_svc = NULL,
    .adv_uuid16 = 0u,
    .adv_oui = k_oui,
    .cccd_next = true,
};

/** @brief The heap block's start: the encoder and the layout's result;
 *         the bitmap, one print buffer and the stream follow. */
typedef struct {
    lzlit_t      z;    /**< the encoder */
    label_info_t info; /**< the layout's result */
} print_head_t;

/**
 * @brief Round up to 8 bytes.
 * @param n Bytes.
 * @return n rounded up.
 */
static size_t align8(size_t n)
{
    return (n + 7u) & ~(size_t)7u;
}

/**
 * @brief Copy a text into a field, cut to fit.
 * @param dst The field.
 * @param n   Its size.
 * @param src The text; NULL is "".
 */
static void copy_text(char *dst, size_t n, const char *src)
{
    if (src == NULL) {
        src = "";
    }
    size_t k = strlen(src);
    if (k >= n) {
        k = n - 1u;
    }
    memcpy(dst, src, k);
    dst[k] = '\0';
}

/**
 * @brief Whether a phase is a request in work.
 * @param phase print_phase_t.
 * @return true from CONNECTING to CHECKING.
 */
static bool in_work(uint8_t phase)
{
    return phase >= PRINT_CONNECTING && phase <= PRINT_CHECKING;
}

/**
 * @brief Ask the central for the link, hurried; it connects once the
 *        radio is synced.  A central that waits out a retry gap (a printer
 *        that was off during the linger: up to BLEU_BACKOFF_MAX_MS) is
 *        bound again, which drops the gap and the backoff: the request's
 *        connect goes at once.  A connect under way is left to run.
 * @param c      The controller.
 * @param now_ms The clock.
 */
static void link_on(print_ctl_t *c, uint32_t now_ms)
{
    bleu_addr_t a;
    if (!bleu_addr_parse(c->addr, &a)) {
        return;
    }
    bleu_bind(&c->b, &a, now_ms);
    bleu_hurry(&c->b);
    c->on = true;
    bleu_enable(&c->b, c->radio == IMU_RADIO_READY, now_ms);
}

/**
 * @brief End the link and the connects.
 * @param c      The controller.
 * @param now_ms The clock.
 */
static void link_off(print_ctl_t *c, uint32_t now_ms)
{
    if (c->on) {
        bleu_enable(&c->b, false, now_ms);
        c->on = false;
    }
}

/**
 * @brief Hand the block back and forget its parts.
 * @param c The controller.
 */
static void drop_block(print_ctl_t *c)
{
    if (c->blk != NULL) {
        c->blk_free = c->blk;
    }
    c->blk = NULL;
    c->blk_want = 0u;
    c->z = NULL;
    c->info = NULL;
    c->buf = NULL;
    c->stream = NULL;
    memset(&c->bmp, 0, sizeof(c->bmp));
}

/**
 * @brief End the request.
 * @param c      The controller.
 * @param err    print_err_t; PRINT_OK is DONE.
 * @param now_ms The clock.
 */
static void finish(print_ctl_t *c, uint8_t err, uint32_t now_ms)
{
    const bool linking = c->phase == PRINT_CONNECTING;
    c->phase = (err == PRINT_OK) ? PRINT_DONE : PRINT_FAILED;
    c->err = err;
    c->job_on = false;
    c->t_quiet = now_ms;
    drop_block(c);
    /* A link that never took a job is not kept: an abort while it was
     * being made ends the connect too. */
    if (err == PRINT_ERR_NOT_FOUND || err == PRINT_ERR_RADIO
        || err == PRINT_ERR_NOT_PRINTER || err == PRINT_ERR_MTU
        || err == PRINT_ERR_UPDATE
        || (err == PRINT_ERR_ABORTED && linking)) {
        link_off(c, now_ms);
    }
    ++c->rev;
}

/**
 * @brief Set the phase.
 * @param c     The controller.
 * @param phase print_phase_t.
 */
static void to_phase(print_ctl_t *c, uint8_t phase)
{
    if (c->phase != phase) {
        c->phase = phase;
        ++c->rev;
    }
}

/**
 * @brief The request's error for a job's.
 * @param e        sv_err_t.
 * @param counted  The counter was read three times after a link drop
 *                 and stayed: not printed.  Without it a link error
 *                 gives no verdict.
 * @return print_err_t.
 */
static uint8_t err_of_job(uint8_t e, bool counted)
{
    switch (e) {
    case SV_OK:              return PRINT_OK;
    case SV_ERR_COVER:       return PRINT_ERR_COVER;
    case SV_ERR_NO_LABEL:    return PRINT_ERR_NO_LABEL;
    case SV_ERR_LABEL:       return PRINT_ERR_LABEL;
    case SV_ERR_BATTERY:     return PRINT_ERR_BATTERY;
    case SV_ERR_HOT:         return PRINT_ERR_HOT;
    case SV_ERR_BUSY:        return PRINT_ERR_BUSY;
    case SV_ERR_WRITE:       return PRINT_ERR_WRITE;
    case SV_ERR_LINK:
        return counted ? PRINT_ERR_LINK_NOT_PRINTED : PRINT_ERR_LINK;
    case SV_ERR_NOT_PRINTED: return PRINT_ERR_NOT_PRINTED;
    case SV_ERR_ROLL:        return PRINT_ERR_ROLL;
    case SV_ERR_SMALL:       return PRINT_ERR_SMALL;
    case SV_ERR_LARGE:       return PRINT_ERR_LARGE;
    case SV_ERR_ABORTED:     return PRINT_ERR_ABORTED;
    case SV_ERR_SILENT:
    default:                 return PRINT_ERR_SILENT;
    }
}

/**
 * @brief The request's error for a layout's.
 * @param e label_err_t.
 * @return print_err_t.
 */
static uint8_t err_of_label(label_err_t e)
{
    switch (e) {
    case LABEL_NO_LABEL:  return PRINT_ERR_NO_LABEL;
    case LABEL_TOO_LARGE: return PRINT_ERR_LARGE;
    case LABEL_MEMORY:    return PRINT_ERR_MEMORY;
    case LABEL_TOO_SMALL:
    case LABEL_QR:
    default:              return PRINT_ERR_SMALL;
    }
}

void print_ctl_init(print_ctl_t *c, const char *version)
{
    if (c == NULL) {
        return;
    }
    memset(c, 0, sizeof(*c));
    bleu_init(&c->b);
    (void)bleu_setup(&c->b, &print_profile, PRINT_BLE_OWNER);
    copy_text(c->version, sizeof(c->version), version);
    c->left = -1;
}

void print_ctl_bind(print_ctl_t *c, const char *addr, const char *name,
                    uint32_t now_ms)
{
    bleu_addr_t a;
    /* A printer's address is public: a random one binds nothing.  The
     * address is kept as bleu_addr_format() writes it, so a bind of the
     * bound printer in other letters is the same address. */
    const bool ok = addr != NULL && bleu_addr_parse(addr, &a)
                    && a.type == BLEU_ADDR_PUBLIC;
    char text[BLEU_ADDR_TEXT_MAX] = "";
    if (ok) {
        (void)bleu_addr_format(&a, true, text, sizeof(text));
    }
    if (strcmp(text, c->addr) == 0 && strcmp(name != NULL ? name : "",
                                             c->name) == 0) {
        return;
    }
    const bool moved = strcmp(text, c->addr) != 0;
    copy_text(c->addr, sizeof(c->addr), text);
    copy_text(c->name, sizeof(c->name), name);
    c->bound = ok;
    ++c->rev;
    if (!moved) {
        return;
    }
    /* Another printer: its roll is not known, also while a job for the
     * old one runs; that job's end leaves them so (job_end()). */
    c->roll_known = false;
    c->left = -1;
    if (!in_work(c->phase)) {
        link_off(c, now_ms);
        bleu_bind(&c->b, NULL, now_ms);
        /* The last request was the old printer's: State reads IDLE. */
        c->phase = PRINT_IDLE;
        c->err = PRINT_OK;
        c->req.kind = PRINT_REQ_NONE;
    }
}

void print_ctl_radio(print_ctl_t *c, imu_radio_t radio, uint32_t now_ms)
{
    if (c->radio == radio) {
        return;
    }
    c->radio = radio;
    if (c->on) {
        bleu_enable(&c->b, radio == IMU_RADIO_READY, now_ms);
    }
}

/**
 * @brief End the scan: its list stays in the central.  A central the scan
 *        switched on with no link up goes off.
 * @param c      The controller.
 * @param cut    The bench ARMED stopped it.
 * @param now_ms The clock.
 */
static void scan_end(print_ctl_t *c, bool cut, uint32_t now_ms)
{
    if (c->scan_on && c->b.scanning) {
        bleu_scan_stop(&c->b, now_ms);
    }
    c->scan_due = false;
    c->scan_on = false;
    c->scan_done = true;
    c->scan_cut = cut;
    if (!in_work(c->phase) && !bleu_link_up(&c->b)) {
        link_off(c, now_ms);
    }
    ++c->rev;
}

/**
 * @brief Start the scan that waits: the central on and unbound, so no
 *        connect follows the scan.  A lingering link ends first: a linked
 *        printer does not advertise, and the scan would miss the bound
 *        one.  The central holds the scan until the disconnect settles
 *        (BLEU_SETTLE_MS at most); its BLEU_SCAN_MS run from then.
 * @param c      The controller.
 * @param now_ms The clock.
 */
static void scan_go(print_ctl_t *c, uint32_t now_ms)
{
    c->scan_due = false;
    link_off(c, now_ms);
    bleu_bind(&c->b, NULL, now_ms);
    c->on = true;
    bleu_enable(&c->b, true, now_ms);
    c->scan_on = bleu_scan(&c->b, now_ms);
    if (!c->scan_on) {
        ++c->scan_refused;
        scan_end(c, false, now_ms);
        c->scan_done = false;
    }
    ++c->rev;
}

/**
 * @brief A scan request: refused while a job or a scan runs, while ARMED
 *        and while an update runs; else it starts, or waits for Bluetooth.
 * @param c      The controller.
 * @param now_ms The clock.
 * @return false when refused.
 */
static bool scan_ask(print_ctl_t *c, uint32_t now_ms)
{
    if (in_work(c->phase) || c->scan_due || c->scan_on || c->armed
        || c->update || c->radio == IMU_RADIO_NO_LINK) {
        ++c->scan_refused;
        ++c->rev;
        return false;
    }
    c->scan_done = false;
    c->scan_cut = false;
    c->scan_due = true;
    c->t_scan = now_ms;
    ++c->rev;
    if (c->radio == IMU_RADIO_READY) {
        scan_go(c, now_ms);
    }
    return true;
}

/**
 * @brief The scan's course each poll: its start once Bluetooth is ready,
 *        its give-up PRINT_FIND_MS after the ask, its end.
 * @param c      The controller.
 * @param now_ms The clock.
 */
static void scan_follow(print_ctl_t *c, uint32_t now_ms)
{
    if (c->scan_due) {
        if (c->radio == IMU_RADIO_READY) {
            scan_go(c, now_ms);
        } else if (now_ms - c->t_scan >= PRINT_FIND_MS
                   || c->radio == IMU_RADIO_NO_LINK) {
            ++c->scan_refused;
            scan_end(c, false, now_ms);
            c->scan_done = false;
        }
    } else if (c->scan_on && !c->b.scanning) {
        scan_end(c, false, now_ms);
    }
}

void print_ctl_gate(print_ctl_t *c, bool armed, bool update,
                    uint32_t now_ms)
{
    if (c->update != update) {
        ++c->rev;
    }
    c->armed = armed;
    c->update = update;
    if (armed && (c->scan_due || c->scan_on)) {
        scan_end(c, true, now_ms);
    }
    if (update && !in_work(c->phase) && !c->scan_on) {
        link_off(c, now_ms);
    }
}

/**
 * @brief Start a QUERY job on the link that is up, after the MTU check.
 * @param c      The controller.
 * @param now_ms The clock.
 */
static void start_query(print_ctl_t *c, uint32_t now_ms)
{
    const uint16_t mtu = c->b.mtu >= 23u ? c->b.mtu : 23u;
    if (mtu < PRINT_MTU_MIN) {
        finish(c, PRINT_ERR_MTU, now_ms);
        return;
    }
    (void)sv_job_start(&c->job, SV_JOB_QUERY, NULL, now_ms);
    c->job_on = true;
    c->wr_out = false;
    c->t_quiet = now_ms;
    to_phase(c, PRINT_READING);
}

bool print_ctl_request(print_ctl_t *c, const print_req_t *r,
                       uint32_t now_ms)
{
    if (c == NULL || r == NULL) {
        return false;
    }
    if (r->kind == PRINT_REQ_SCAN) {
        return scan_ask(c, now_ms);
    }
    if (r->kind == PRINT_REQ_ABORT) {
        if (!in_work(c->phase)) {
            return false;
        }
        if (c->job_on) {
            sv_job_abort(&c->job, now_ms);
        } else {
            finish(c, PRINT_ERR_ABORTED, now_ms);
        }
        return true;
    }
    if (in_work(c->phase) || r->kind < PRINT_REQ_LABEL
        || r->kind > PRINT_REQ_QUERY) {
        return false;
    }
    servo_id_t id;
    if (r->kind == PRINT_REQ_LABEL && !servo_id_parse(r->id, &id)) {
        return false;
    }
    if (c->scan_due || c->scan_on) {
        /* The label wins: the scan ends with the list it has. */
        scan_end(c, false, now_ms);
    }
    c->req = *r;
    c->req.id[sizeof(c->req.id) - 1u] = '\0';
    c->req.dut[sizeof(c->req.dut) - 1u] = '\0';
    c->t_req = now_ms;
    ++c->jobs;
    c->relinked = false;
    c->seen_frame = 0u;
    bleu_addr_t a;
    if (c->on && (!bleu_addr_parse(c->addr, &a)
                  || !bleu_addr_eq(&a, &c->b.addr))) {
        /* Bound anew during the last job: the old link ends first. */
        link_off(c, now_ms);
    }
    c->phase = PRINT_CONNECTING;
    ++c->rev;
    if (!c->bound) {
        finish(c, PRINT_ERR_NO_PRINTER, now_ms);
    } else if (c->update) {
        finish(c, PRINT_ERR_UPDATE, now_ms);
    } else if (c->armed && r->kind != PRINT_REQ_QUERY) {
        finish(c, PRINT_ERR_ARMED, now_ms);
    } else if (c->radio == IMU_RADIO_NO_LINK
               || c->radio == IMU_RADIO_NO_SYNC) {
        finish(c, PRINT_ERR_RADIO, now_ms);
    } else if (bleu_link_up(&c->b)) {
        start_query(c, now_ms);
    } else {
        link_on(c, now_ms);
    }
    return true;
}

/**
 * @brief The link's state and the job's write, after the central moved:
 *        a drop goes to the job before the write's end, so the job waits
 *        for the link instead of ending on a refused write.
 * @param c      The controller.
 * @param now_ms The clock.
 */
static void follow(print_ctl_t *c, uint32_t now_ms)
{
    const bool up = bleu_link_up(&c->b);
    if (up != c->was_up) {
        c->was_up = up;
        ++c->rev;
        if (c->job_on) {
            c->wr_out = false;
            sv_job_link(&c->job, up, now_ms);
            if (!up) {
                /* The reconnect of section 4.3: once, hurried. */
                bleu_hurry(&c->b);
            }
        }
    }
    if (c->wr_out && c->b.wr != BLEU_WR_BUSY) {
        c->wr_out = false;
        if (c->job_on) {
            sv_job_written(&c->job, c->b.wr == BLEU_WR_OK, now_ms);
        }
    }
}

void print_ctl_event(print_ctl_t *c, const bleu_evt_t *e, uint32_t now_ms)
{
    if (c == NULL || e == NULL) {
        return;
    }
    const bool stream = bleu_event(&c->b, e, now_ms);
    follow(c, now_ms);
    if (stream) {
        c->t_quiet = now_ms;
        if (c->job_on) {
            sv_job_rx(&c->job, e->data, e->len, now_ms);
        }
    }
}

bool print_ctl_step(print_ctl_t *c, uint32_t now_ms, bleu_act_t *out)
{
    if (c == NULL || out == NULL) {
        return false;
    }
    if (c->job_on && !c->wr_out && bleu_can_send(&c->b)) {
        const uint8_t *d = NULL;
        uint16_t n = 0u;
        if (sv_job_tx(&c->job, now_ms, &d, &n)) {
            bool sent;
            if (n == SV_FRAGMENT) {
                memcpy(c->frag, d, SV_FRAGMENT);
                sent = bleu_send_ext(&c->b, n, now_ms);
            } else {
                sent = bleu_send(&c->b, d, n, now_ms);
            }
            if (sent) {
                c->wr_out = true;
            } else {
                sv_job_written(&c->job, false, now_ms);
            }
        }
    }
    const bool any = bleu_step(&c->b, now_ms, out);
    follow(c, now_ms);
    return any;
}

const uint8_t *print_ctl_ext(const print_ctl_t *c, const bleu_act_t *a)
{
    if (c == NULL || a == NULL || a->h2 != SV_FRAGMENT) {
        return NULL;
    }
    return c->frag;
}

/**
 * @brief The roll as the layout takes it.
 * @param c The controller.
 * @return The roll.
 */
static label_roll_t roll_of(const print_ctl_t *c)
{
    const label_roll_t r = { .w_mm = c->roll.w_mm, .h_mm = c->roll.h_mm };
    return r;
}

/**
 * @brief After the QUERY: the roll's check, then the block asked for.
 * @param c      The controller.
 * @param now_ms The clock.
 */
static void begin_build(print_ctl_t *c, uint32_t now_ms)
{
    const sv_err_t r = sv_roll_check(&c->roll);
    if (r != SV_OK) {
        finish(c, err_of_job((uint8_t)r, false), now_ms);
        return;
    }
    const label_roll_t roll = roll_of(c);
    const size_t bmp = label_bmp_bytes(&roll);
    if (bmp == 0u) {
        finish(c, PRINT_ERR_SMALL, now_ms);
        return;
    }
    c->bufs = sv_buf_count((uint16_t)(bmp / LABEL_COL_BYTES));
    c->cap = LZLIT_BOUND((size_t)c->bufs * SV_BUF_SIZE);
    c->blk_want = align8(sizeof(print_head_t)) + align8(bmp) + SV_BUF_SIZE
                  + c->cap;
    c->next = (uint16_t)(c->bufs + 1u);
    c->t_build = now_ms;
    to_phase(c, PRINT_BUILDING);
}

size_t print_ctl_block_want(const print_ctl_t *c)
{
    return (c != NULL && c->blk == NULL && c->phase == PRINT_BUILDING)
               ? c->blk_want : 0u;
}

void print_ctl_block_give(print_ctl_t *c, void *blk, uint32_t now_ms)
{
    if (c == NULL || c->phase != PRINT_BUILDING || c->blk != NULL) {
        if (c != NULL && blk != NULL) {
            c->blk_free = blk;
        }
        return;
    }
    if (blk == NULL) {
        finish(c, PRINT_ERR_MEMORY, now_ms);
        return;
    }
    const label_roll_t roll = roll_of(c);
    const size_t bmp = label_bmp_bytes(&roll);
    print_head_t *head = blk;
    c->blk = blk;
    c->z = &head->z;
    c->info = &head->info;
    uint8_t *p = c->blk + align8(sizeof(print_head_t));
    c->bmp.bits = p;
    c->bmp.cap = bmp;
    p += align8(bmp);
    c->buf = p;
    p += SV_BUF_SIZE;
    c->stream = p;
}

void *print_ctl_block_back(print_ctl_t *c)
{
    if (c == NULL) {
        return NULL;
    }
    void *p = c->blk_free;
    c->blk_free = NULL;
    return p;
}

/**
 * @brief One step of the build: the layout, then one buffer filled and
 *        encoded, then the PRINT job.
 * @param c      The controller.
 * @param now_ms The clock.
 */
static void build_step(print_ctl_t *c, uint32_t now_ms)
{
    if (c->blk == NULL) {
        return;
    }
    const label_roll_t roll = roll_of(c);
    if (c->next > c->bufs) {
        label_err_t e;
        if (c->req.kind == PRINT_REQ_TEST) {
            e = label_layout_test(&roll, c->version, &c->bmp, c->info);
        } else {
            const label_text_t t = { .id = c->req.id, .dut = c->req.dut };
            e = label_layout(&roll, &t, &c->bmp, c->info);
        }
        if (e != LABEL_OK) {
            finish(c, err_of_label(e), now_ms);
            return;
        }
        lzlit_begin(c->z, c->stream, c->cap,
                    (uint32_t)c->bufs * SV_BUF_SIZE);
        c->next = 0u;
        return;
    }
    if (c->next < c->bufs) {
        (void)sv_buf_fill(c->buf, c->bmp.bits, c->bmp.cols, c->next);
        lzlit_put(c->z, c->buf, SV_BUF_SIZE);
        ++c->next;
        return;
    }
    c->zlen = (uint32_t)lzlit_end(c->z);
    c->build_ms = now_ms - c->t_build;
    ++c->builds;
    if (!bleu_link_up(&c->b)) {
        finish(c, PRINT_ERR_LINK, now_ms);
        return;
    }
    const sv_stream_t z = {
        .data = c->stream, .len = c->zlen, .buffers = c->bufs,
        .w_mm = c->roll.w_mm, .h_mm = c->roll.h_mm,
    };
    if (c->zlen == 0u || !sv_job_start(&c->job, SV_JOB_PRINT, &z, now_ms)) {
        finish(c, PRINT_ERR_MEMORY, now_ms);
        return;
    }
    c->job_on = true;
    c->wr_out = false;
    to_phase(c, PRINT_SENDING);
}

/**
 * @brief The phase a running PRINT job shows.
 * @param c The controller.
 */
static void job_phase(print_ctl_t *c)
{
    if (c->job.kind != SV_JOB_PRINT) {
        return;
    }
    switch (c->job.step) {
    case SV_STEP_DONE:    to_phase(c, PRINT_PRINTING); break;
    case SV_STEP_COUNTER: to_phase(c, PRINT_CHECKING); break;
    default:              break;
    }
}

/**
 * @brief A job that ended: the QUERY's roll, the PRINT's verdict.
 * @param c      The controller.
 * @param now_ms The clock.
 */
static void job_end(print_ctl_t *c, uint32_t now_ms)
{
    c->job_on = false;
    c->wr_out = false;
    const sv_job_t *j = &c->job;
    /* The view shows the roll of the bound printer only: a bind of
     * another address during the job leaves it unknown. */
    bleu_addr_t a;
    const bool ours = c->b.bound && bleu_addr_parse(c->addr, &a)
                      && bleu_addr_eq(&a, &c->b.addr);
    /* A record read: a zeroed one (no label) leaves the roll unknown. */
    if (j->last.w_mm != 0u || j->last.h_mm != 0u || j->last.left != 0u) {
        c->roll = j->last;
        if (ours) {
            c->roll_known = true;
            c->left = (int32_t)j->last.left;
        }
    } else if (j->err == SV_ERR_NO_LABEL && ours) {
        c->roll_known = false;
    }
    c->relinked = j->relinked;
    const uint8_t err = err_of_job(j->err, j->counted);
    if (err != PRINT_OK || j->kind == SV_JOB_PRINT
        || c->req.kind == PRINT_REQ_QUERY) {
        finish(c, err, now_ms);
        return;
    }
    begin_build(c, now_ms);
}

void print_ctl_poll(print_ctl_t *c, uint32_t now_ms)
{
    if (c == NULL) {
        return;
    }
    c->t_now = now_ms;
    follow(c, now_ms);
    scan_follow(c, now_ms);
    if (c->phase == PRINT_CONNECTING) {
        if (c->b.st == BLEU_ST_NOT_PEER) {
            finish(c, PRINT_ERR_NOT_PRINTER, now_ms);
        } else if (bleu_link_up(&c->b)) {
            start_query(c, now_ms);
        } else if (now_ms - c->t_req >= PRINT_FIND_MS) {
            finish(c, c->radio == IMU_RADIO_READY ? PRINT_ERR_NOT_FOUND
                                                   : PRINT_ERR_RADIO,
                   now_ms);
        } else if (c->radio == IMU_RADIO_NO_LINK
                   || c->radio == IMU_RADIO_NO_SYNC) {
            finish(c, PRINT_ERR_RADIO, now_ms);
        }
    }
    if (c->job_on) {
        sv_job_tick(&c->job, now_ms);
        if (c->job.frame != c->seen_frame) {
            c->seen_frame = c->job.frame;
            ++c->rev;
        }
        job_phase(c);
        if (sv_job_ended(&c->job)) {
            job_end(c, now_ms);
        }
    } else if (c->phase == PRINT_BUILDING) {
        build_step(c, now_ms);
    }
    /* The linger's end, or a device update that wants the spare link. */
    if (!in_work(c->phase) && c->on && !c->scan_on
        && (c->update || now_ms - c->t_quiet >= PRINT_LINGER_MS)) {
        link_off(c, now_ms);
    }
}

bool print_ctl_busy(const print_ctl_t *c)
{
    return c != NULL && (in_work(c->phase) || c->scan_due || c->scan_on);
}

void print_ctl_view_of(const print_ctl_t *c, print_view_t *out)
{
    memset(out, 0, sizeof(*out));
    out->phase = c->phase;
    out->err = c->err;
    out->kind = c->req.kind;
    out->frame = (uint8_t)c->job.frame;
    out->frames = (uint8_t)c->job.frames;
    out->bound = c->bound;
    out->linked = bleu_link_up(&c->b);
    out->relinked = c->relinked;
    copy_text(out->printer, sizeof(out->printer),
              c->name[0] != '\0' ? c->name : c->addr);
    if (c->roll_known) {
        out->w_mm = c->roll.w_mm;
        out->h_mm = c->roll.h_mm;
        out->gap_mm = c->roll.gap_mm;
    }
    out->left = c->left;
    out->mtu = out->linked ? c->b.mtu : 0u;
    copy_text(out->id, sizeof(out->id),
              c->req.kind == PRINT_REQ_LABEL ? c->req.id : "");
    copy_text(out->addr, sizeof(out->addr), c->addr);
    copy_text(out->name, sizeof(out->name), c->name);
    out->update = c->update;
    out->scanning = c->scan_due || c->scan_on;
    out->scan_done = c->scan_done;
    out->scan_cut = c->scan_cut;
    out->scan_left_s =
        c->scan_due ? (uint8_t)(BLEU_SCAN_MS / 1000u)
                    : (uint8_t)((bleu_scan_left_ms(&c->b, c->t_now) + 999u)
                                / 1000u);
    const uint8_t n = c->b.list.found_n < PRINT_FOUND_MAX
                          ? c->b.list.found_n : (uint8_t)PRINT_FOUND_MAX;
    out->found_n = n;
    for (uint8_t i = 0u; i < n; ++i) {
        const bleu_dev_t *d = &c->b.list.found[i];
        (void)bleu_addr_format(&d->addr, true, out->found[i].addr,
                               sizeof(out->found[i].addr));
        copy_text(out->found[i].name, sizeof(out->found[i].name), d->name);
        out->found[i].rssi = d->rssi;
    }
    out->scan_refused = c->scan_refused;
    out->jobs = c->jobs;
    out->rev = c->rev;
}
