/**
 * @file panel_devimg.c
 * @brief The device images fetched on demand (panel_devimg.h): the
 *        download through wifi_access's DEVICE job, the SD card's read, the
 *        checks on the task `devimg`, and each image's place in PSRAM.
 *
 * One image is on its way at a time.  The frame starts it
 * (panel_devimg_request()), the download runs on wifi_access's fetch task
 * and ends with the file in a PSRAM buffer that this file takes over; the
 * card's read and every check run on `devimg`, a task made for the one job
 * and gone after it, so the frame never waits on the card or on a SHA-256
 * pass over 1.9MB.  An image that passed stays in PSRAM until the panel
 * restarts: the release the panel runs does not change under it.
 *
 * SPDX-License-Identifier: MIT
 */
#include "panel_devimg.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_hosted_host_fw_ver.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "fw_release.h"
#include "panel.h"
#include "rcbench_version.h"
#include "storage.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "upd_screen.h"
#include "upd_wizard.h"
#include "wifi_access.h"

/** @brief The console lines' tag. */
static const char *TAG = "devimg";

/** @brief `devimg`'s stack, bytes: the card's FatFs calls, the checks
 *         and the release key's verifier, whose Mbed TLS calls through a
 *         pointer the depth leaves out; docs/Performance.md holds it. */
#define DEVIMG_STACK 8192u
/** @brief `devimg`'s priority: under the UI loop's, so a check shares the
 *         core by time slice and never starves the frame. */
#define DEVIMG_PRIO  1u
/** @brief `devimg`'s core: 0, with the UI; core 1 runs the control task. */
#define DEVIMG_CORE  0

_Static_assert(DEVIMG_NAME_MAX <= WIFI_DEV_NAME_MAX,
               "the download takes every image's longest name");
_Static_assert(DEVIMG_NAME_MAX + sizeof(".sig") - 1u <= WA_DEV_SIG_NAME_MAX,
               "and the longest name's signature beside it");

/** @brief One job of `devimg`: what it reads and checks, and the answer. */
typedef struct {
    devimg_id_t     id;        /**< the image */
    bool            card;      /**< read it from the card first */
    fw_version_t    want;      /**< the version it must carry */
    char            name[DEVIMG_NAME_MAX]; /**< its file name */
    uint8_t        *buf;       /**< the bytes: the download's, or the
                                    card's once read */
    uint32_t        n;         /**< their count */
    devimg_card_t   card_err;  /**< the card's answer */
    devimg_err_t    err;       /**< the checks' answer */
    fw_sig_t        sig;       /**< the release key's verdict on its .sig */
    fw_image_info_t info;      /**< what the image says */
    uint8_t         sha[FW_SHA256_BYTES]; /**< of every byte */
} job_t;

/** @brief The images' states, the frame's own. */
static devimg_slot_t   s_sl[DEVIMG_COUNT];
/** @brief Each image's bytes once READY; unchanged from then. */
static uint8_t        *s_buf[DEVIMG_COUNT];
/** @brief Each READY image's length. */
static uint32_t        s_n[DEVIMG_COUNT];
/** @brief Each READY image's header information. */
static fw_image_info_t s_info[DEVIMG_COUNT];
/** @brief Each READY image's SHA-256 over every byte. */
static uint8_t         s_sha[DEVIMG_COUNT][FW_SHA256_BYTES];
/** @brief Each READY image's signature verdict (fw_sig.h). */
static fw_sig_t        s_sig[DEVIMG_COUNT];
/** @brief Set once an image's fields above are written: other tasks read
 *         them only after it. */
static atomic_bool     s_ready[DEVIMG_COUNT];
/** @brief An image is on its way: panel_devimg_busy(). */
static atomic_bool     s_busy;
/** @brief The image on its way, while one is. */
static devimg_id_t     s_cur;
/** @brief The DEVICE job's number (wa_fetch_t.gen) while it downloads. */
static uint32_t        s_gen;
/** @brief `devimg`'s job: the frame writes it before the task starts and
 *         reads it after s_done. */
static job_t           s_job;
/** @brief `devimg` finished its job. */
static atomic_bool     s_done;
/** @brief CANCEL UPDATE asked to end the download. */
static atomic_bool     s_cancel;
/** @brief The share of the download last shown, 0-100. */
static uint8_t         s_pct;
/** @brief The step on UPDATES of each image, by devimg_id_t. */
static const unsigned  k_step[DEVIMG_COUNT] = {
    UPD_WIZ_C6, UPD_WIZ_DIAL, UPD_WIZ_PNODE, UPD_WIZ_BAL,
    UPD_WIZ_PNODE, /* the display node: the power node's step */
};

fw_version_t panel_devimg_want(devimg_id_t id)
{
    if (id == DEVIMG_C6) {
        return (fw_version_t){ ESP_HOSTED_VERSION_MAJOR_1,
                               ESP_HOSTED_VERSION_MINOR_1,
                               ESP_HOSTED_VERSION_PATCH_1 };
    }
    return (fw_version_t){ RCBENCH_VERSION_MAJOR, RCBENCH_VERSION_MINOR,
                           RCBENCH_VERSION_PATCH };
}

/** @brief The release the panel runs: the tag every image is fetched of. */
static fw_version_t release(void)
{
    return panel_devimg_want(DEVIMG_DIAL);
}

bool panel_devimg_get(devimg_id_t id, const uint8_t **p, uint32_t *n,
                      fw_image_info_t *info, uint8_t sha[FW_SHA256_BYTES])
{
    if ((unsigned)id >= DEVIMG_COUNT || !atomic_load(&s_ready[id])) {
        return false;
    }
    if (p != NULL) {
        *p = s_buf[id];
    }
    if (n != NULL) {
        *n = s_n[id];
    }
    if (info != NULL) {
        *info = s_info[id];
    }
    if (sha != NULL) {
        memcpy(sha, s_sha[id], FW_SHA256_BYTES);
    }
    return true;
}

bool panel_devimg_busy(void)
{
    return atomic_load(&s_busy);
}

bool panel_devimg_running(devimg_id_t id)
{
    return (unsigned)id < DEVIMG_COUNT && devimg_busy(&s_sl[id]);
}

bool panel_devimg_pending(devimg_id_t id)
{
    return (unsigned)id < DEVIMG_COUNT && devimg_pending(&s_sl[id]);
}

bool panel_devimg_pending_other(devimg_id_t id)
{
    for (unsigned i = 0; i < DEVIMG_COUNT; ++i) {
        if (i != (unsigned)id && devimg_pending(&s_sl[i])) {
            return true;
        }
    }
    return false;
}

bool panel_devimg_cancel(void)
{
    bool took = false;
    for (unsigned i = 0; i < DEVIMG_COUNT; ++i) {
        const bool busy = devimg_busy(&s_sl[i]);
        took = devimg_cancel(&s_sl[i]) || busy || took;
        if (busy && s_sl[i].st == DEVIMG_FETCH) {
            atomic_store(&s_cancel, true);
        }
    }
    return took;
}

void panel_devimg_forget(devimg_id_t id)
{
    if ((unsigned)id < DEVIMG_COUNT) {
        (void)devimg_cancel(&s_sl[id]);
    }
}

bool panel_devimg_take_cancel(void)
{
    return atomic_exchange(&s_cancel, false);
}

devimg_owed_t panel_devimg_owed(devimg_id_t id, bool open, bool wait)
{
    if ((unsigned)id >= DEVIMG_COUNT) {
        return DEVIMG_OWED_NONE;
    }
    return devimg_owed_step(&s_sl[id], open, wait, now_ms());
}

void panel_devimg_view(devimg_id_t id, upd_fetch_t *out)
{
    memset(out, 0, sizeof(*out));
    if ((unsigned)id >= DEVIMG_COUNT || !s_wifi_ok) {
        out->src = DEVIMG_SRC_CARD;
    }
    if ((unsigned)id >= DEVIMG_COUNT) {
        return;
    }
    const devimg_slot_t *s = &s_sl[id];
    char ssid[WIFI_SSID_MAX + 1];
    const bool net = s_wifi_ok && wifi_access_home(ssid, sizeof(ssid));
    const devimg_src_t src = devimg_src(s, net);
    out->src = (uint8_t)src;
    const fw_version_t v = panel_devimg_want(id);
    if (src == DEVIMG_SRC_CARD) {
        (void)devimg_card_path(id, v, out->file, sizeof(out->file));
    } else {
        (void)devimg_name(id, v, out->file, sizeof(out->file));
    }
    switch (s->st) {
    case DEVIMG_FETCH:
        out->run = UPD_FETCH_NET;
        out->percent = s_pct;
        break;
    case DEVIMG_VERIFY:
        out->run = UPD_FETCH_CHECK;
        break;
    case DEVIMG_CARD:
        out->run = UPD_FETCH_CARD;
        break;
    case DEVIMG_READY:
        out->nosig = !fw_sig_signed(s_sig[id]);
        out->sigbad = fw_sig_bad(s_sig[id]);
        break;
    case DEVIMG_IDLE:
    default:
        break;
    }
}

/**
 * @brief The verdict on the card's "<file>.sig" beside the job's file
 *        (devimg_card_sig()): `devimg`'s, with the card held.
 * @param j    the job, its file read
 * @param path the file's path on the card
 * @return the verdict
 */
static fw_sig_t card_sig(const job_t *j, const char *path)
{
    char sp[sizeof(STORAGE_MOUNT_POINT) + DEVIMG_PATH_MAX + 8u];
    const int k = snprintf(sp, sizeof(sp), "%s.sig", path);
    if (k <= 0 || (size_t)k >= sizeof(sp)) {
        return FW_SIG_FORM;
    }
    storage_stat_t st;
    const esp_err_t e = storage_stat(sp, &st);
    fw_sig_t v = FW_SIG_NONE;
    if (e == ESP_ERR_NOT_FOUND || (e == ESP_OK && st.is_dir)) {
        (void)devimg_card_sig(false, NULL, 0u, &v);
        return v;
    }
    /* One byte past the longest, so that a longer file is told apart. */
    uint8_t sig[FW_SIG_MAX + 1u];
    size_t got = 0u;
    FILE *f = (e == ESP_OK) ? fopen(sp, "rb") : NULL;
    if (f != NULL) {
        got = fread(sig, 1u, sizeof(sig), f);
        (void)fclose(f);
    }
    /* The panel image's verifier and key, called by name so that the
     * stack check follows it into `devimg`'s depth. */
    return devimg_card_sig(true, sig, (uint32_t)got, &v)
               ? wifi_access_sig_verify(j->buf, j->n, sig, (uint32_t)got)
               : v;
}

/**
 * @brief Read the job's file from the card into PSRAM, with the card's
 *        SHA256SUMS line when the folder holds one: `devimg`'s.
 * @param j        the job
 * @param sha      takes the SHA256SUMS line's hash
 * @param have_sha set when the card's SHA256SUMS names the file
 * @return DEVIMG_CARD_OK with j->buf and j->n set, or why not
 */
static devimg_card_t read_card(job_t *j, uint8_t sha[FW_SHA256_BYTES],
                               bool *have_sha)
{
    *have_sha = false;
    if (!storage_hold()) {
        return DEVIMG_CARD_NONE;
    }
    char path[sizeof(STORAGE_MOUNT_POINT) + DEVIMG_PATH_MAX + 1u];
    storage_path(DEVIMG_DIR, j->name, path, sizeof(path));
    storage_stat_t st;
    devimg_card_t r = DEVIMG_CARD_OK;
    const esp_err_t e = storage_stat(path, &st);
    if (e == ESP_ERR_NOT_FOUND || (e == ESP_OK && st.is_dir)) {
        r = DEVIMG_CARD_NO_FILE;
    } else if (e != ESP_OK) {
        r = DEVIMG_CARD_READ;
    } else if (st.size == 0u || st.size > devimg_slot(j->id)) {
        r = DEVIMG_CARD_SIZE;
    }
    FILE *f = NULL;
    if (r == DEVIMG_CARD_OK) {
        j->buf = heap_caps_malloc((size_t)st.size, MALLOC_CAP_SPIRAM);
        r = (j->buf == NULL) ? DEVIMG_CARD_MEMORY : DEVIMG_CARD_OK;
    }
    if (r == DEVIMG_CARD_OK) {
        f = fopen(path, "rb");
        r = (f == NULL) ? DEVIMG_CARD_READ : DEVIMG_CARD_OK;
    }
    if (r == DEVIMG_CARD_OK) {
        const size_t got = fread(j->buf, 1u, (size_t)st.size, f);
        j->n = (uint32_t)got;
        r = (got != (size_t)st.size) ? DEVIMG_CARD_READ : DEVIMG_CARD_OK;
    }
    if (f != NULL) {
        (void)fclose(f);
    }
    if (r == DEVIMG_CARD_OK) {
        j->sig = card_sig(j, path);
    }
    /* The card's SHA256SUMS, when the folder holds one: a file it names
     * is held to its line; a file it does not name is checked by the
     * image's own appended SHA-256 alone. */
    storage_path(DEVIMG_DIR, DEVIMG_SUMS, path, sizeof(path));
    char *sums = NULL;
    if (r == DEVIMG_CARD_OK && storage_stat(path, &st) == ESP_OK
        && !st.is_dir) {
        sums = (st.size <= FW_REL_SUMS_MAX)
                   ? heap_caps_malloc(FW_REL_SUMS_MAX, MALLOC_CAP_SPIRAM)
                   : NULL;
        FILE *g = (sums != NULL) ? fopen(path, "rb") : NULL;
        const size_t got = (g != NULL)
                               ? fread(sums, 1u, (size_t)st.size, g) : 0u;
        if (g != NULL) {
            (void)fclose(g);
        }
        const fw_rel_err_t se = (got == (size_t)st.size && sums != NULL)
                                    ? fw_release_parse_sums(sums, got,
                                                            j->name, sha)
                                    : FW_REL_SUMS_BAD;
        if (se == FW_REL_OK) {
            *have_sha = true;
        } else if (se != FW_REL_SUMS_NONE) {
            r = DEVIMG_CARD_SUMS;
        }
    }
    heap_caps_free(sums);
    storage_release();
    if (r != DEVIMG_CARD_OK) {
        heap_caps_free(j->buf);
        j->buf = NULL;
        j->n = 0u;
    }
    return r;
}

/**
 * @brief `devimg`: the card's read when the job asks for it, then every
 *        check, then gone.
 * @param arg unused
 */
static void devimg_task(void *arg)
{
    (void)arg;
    job_t *j = &s_job;
    uint8_t want_sha[FW_SHA256_BYTES];
    bool have_sha = false;
    j->card_err = DEVIMG_CARD_OK;
    if (j->card) {
        j->card_err = read_card(j, want_sha, &have_sha);
    }
    if (j->card_err == DEVIMG_CARD_OK) {
        j->err = devimg_verify(j->id, j->buf, j->n, j->want,
                               have_sha ? want_sha : NULL, &j->info, j->sha,
                               NULL);
    }
    if (j->card_err == DEVIMG_CARD_OK && j->err == DEVIMG_OK) {
        /* The panel image's policy: warn while RCBENCH_SIGN_ENFORCE is 0,
         * refuse while it is 1. */
        j->err = devimg_sig_policy(j->sig, fw_sig_enforced());
    }
    atomic_store(&s_done, true);
    vTaskDelete(NULL);
}

/**
 * @brief Start `devimg` on s_job.
 * @return true when it runs
 */
static bool task_start(void)
{
    atomic_store(&s_done, false);
    return xTaskCreatePinnedToCore(devimg_task, "devimg", DEVIMG_STACK, NULL,
                                   DEVIMG_PRIO, NULL, DEVIMG_CORE) == pdPASS;
}

/**
 * @brief The attempt on s_cur failed: the step's result line with its
 *        code, the console line, the image's state.
 * @param kind   which part failed
 * @param n      its reason's number for the code
 * @param reason its reason's text
 */
static void fail(upd_fetch_fail_t kind, unsigned n, const char *reason)
{
    static const char k_letter[] = { 'D', 'S', 'V' };
    devimg_slot_t *s = &s_sl[s_cur];
    devimg_failed(s, k_letter[kind], n);
    char line[UI_ALERT_MAX];
    upd_fetch_fail_line(kind, s->code, reason, line, sizeof(line));
    ESP_LOGW(TAG, "image %u: %s", (unsigned)s_cur, line);
    upd_screen_set_result(k_step[s_cur], line);
    atomic_store(&s_busy, false);
}

/**
 * @brief The download ended without the file: why, in the words of
 *        UPDATES' own download (upd_result_text()).
 * @param f the job as it ended
 */
static void fail_download(const wa_fetch_t *f)
{
    static upd_view_t v;   /* off the stack: the frame's is the display's */
    memset(&v, 0, sizeof(v));
    v.job = UPD_JOB_DOWNLOAD;
    v.phase = UPD_PH_ENDED;
    v.got = (upd_got_t)f->got;
    v.why = (upd_why_t)f->why;
    v.join = (wifi_join_t)f->join;
    v.rel = (upd_rel_t)f->rel;
    v.status = f->status;
    v.budget_s = f->budget_ms / 1000u;
    v.have_net = true;
    char what[UI_ALERT_MAX], second[96];
    upd_result_text(&v, what, sizeof(what), second, sizeof(second));
    fail(UPD_FETCH_FAIL_NET, (unsigned)f->got, what);
}

/**
 * @brief A refused start's reason in the alert band, in the words of
 *        UPDATES' own check (upd_result_text()).
 * @param got how the start ended: BLOCKED or NO_NET
 * @param why BLOCKED's reason
 */
static void refused(wa_got_t got, wa_why_t why)
{
    static upd_view_t v;   /* off the stack, as fail_download()'s */
    memset(&v, 0, sizeof(v));
    v.phase = UPD_PH_ENDED;
    v.got = (upd_got_t)got;
    v.why = (upd_why_t)why;
    char what[UI_ALERT_MAX], second[96];
    upd_result_text(&v, what, sizeof(what), second, sizeof(second));
    ui_router_set_notice(what);
}

bool panel_devimg_request(devimg_id_t id, bool armed)
{
    if ((unsigned)id >= DEVIMG_COUNT) {
        return false;
    }
    devimg_slot_t *s = &s_sl[id];
    if (atomic_load(&s_busy)) {
        refused(WA_GOT_BLOCKED, WA_WHY_BUSY);
        return false;
    }
    char ssid[WIFI_SSID_MAX + 1];
    const bool net = s_wifi_ok && wifi_access_home(ssid, sizeof(ssid));
    const devimg_src_t src = devimg_src(s, net);
    if (src == DEVIMG_SRC_RAM) {
        return false;
    }
    memset(&s_job, 0, sizeof(s_job));
    s_job.id = id;
    s_job.want = panel_devimg_want(id);
    if (!devimg_name(id, s_job.want, s_job.name, sizeof(s_job.name))) {
        return false;
    }
    s_cur = id;
    s_pct = 0u;
    if (src == DEVIMG_SRC_CARD) {
        /* A card read changes no output and needs no radio; ARMED and the
         * rest of the bench's gate hold it as they hold the update. */
        const wa_why_t g = upd_dev_gate(armed);
        if (g != WA_WHY_NONE) {
            refused(WA_GOT_BLOCKED, g);
            return false;
        }
        s_job.card = true;
        (void)devimg_begin(s, DEVIMG_SRC_CARD);
        atomic_store(&s_busy, true);
        if (!task_start()) {
            fail(UPD_FETCH_FAIL_CARD, (unsigned)DEVIMG_CARD_MEMORY,
                 TR(UPD_FETCH_MEMORY));
            return false;
        }
        ESP_LOGI(TAG, "%s: reading %s/%s", s_job.name, DEVIMG_DIR,
                 s_job.name);
        return true;
    }
    const wa_got_t got = wifi_access_fetch_dev((uint8_t)id, release(),
                                               s_job.name,
                                               devimg_slot(id),
                                               upd_dev_gate(armed));
    if (got != WA_GOT_NONE) {
        wa_fetch_t f;
        wifi_access_fetch_get(&f);
        refused(got, wa_fetch_busy(&f) ? WA_WHY_BUSY : f.why);
        return false;
    }
    wa_fetch_t f;
    wifi_access_fetch_get(&f);
    s_gen = f.gen;
    (void)devimg_begin(s, DEVIMG_SRC_NET);
    atomic_store(&s_busy, true);
    ESP_LOGI(TAG, "%s: download of v%u.%u.%u's file, job %lu", s_job.name,
             (unsigned)release().major, (unsigned)release().minor,
             (unsigned)release().patch, (unsigned long)s_gen);
    return true;
}

/**
 * @brief The download under way, once a frame: its share, and its end.
 */
static void fetch_service(void)
{
    wa_fetch_t f;
    wifi_access_fetch_get(&f);
    if (f.gen != s_gen || f.job != WA_JOB_DEVICE) {
        /* Another job replaced it: none starts while it runs, so it
         * ended unseen. */
        f.got = WA_GOT_ABORTED;
        f.why = WA_WHY_BUSY;
        fail_download(&f);
        return;
    }
    if (f.total > 0u) {
        s_pct = (uint8_t)((uint64_t)f.done * 100u / f.total);
    }
    if (wa_fetch_busy(&f)) {
        return;
    }
    uint8_t *buf = NULL;
    uint32_t n = 0u;
    fw_sig_t sig = FW_SIG_NONE;
    if (f.got != WA_GOT_STAGED
        || !wifi_access_dev_take(s_gen, &buf, &n, &sig)) {
        fail_download(&f);
        return;
    }
    devimg_fetched(&s_sl[s_cur]);
    s_job.buf = buf;
    s_job.n = n;
    s_job.sig = sig;
    s_job.card = false;
    ESP_LOGI(TAG, "%s: %lu bytes, SHA-256 as SHA256SUMS; checking",
             s_job.name, (unsigned long)n);
    if (!task_start()) {
        heap_caps_free(buf);
        s_job.buf = NULL;
        fail(UPD_FETCH_FAIL_CHECK, (unsigned)DEVIMG_E_SIZE,
             TR(UPD_FETCH_MEMORY));
    }
}

/**
 * @brief `devimg`'s job ended: the image kept, or the failure said.
 */
static void job_service(void)
{
    if (!atomic_load(&s_done)) {
        return;
    }
    atomic_store(&s_done, false);
    job_t *j = &s_job;
    if (j->card && j->card_err != DEVIMG_CARD_OK) {
        char path[DEVIMG_PATH_MAX], why[UI_ALERT_MAX];
        (void)devimg_card_path(j->id, j->want, path, sizeof(path));
        upd_fetch_card_text(j->card_err, path, why, sizeof(why));
        fail(UPD_FETCH_FAIL_CARD, (unsigned)j->card_err, why);
        return;
    }
    if (j->err != DEVIMG_OK) {
        char have[FW_REL_VER_TEXT], want[FW_REL_VER_TEXT], why[UI_ALERT_MAX];
        fw_release_version_text(j->info.version, have);
        fw_release_version_text(j->want, want);
        upd_fetch_check_text(j->err, have, want, why, sizeof(why));
        heap_caps_free(j->buf);
        j->buf = NULL;
        fail(UPD_FETCH_FAIL_CHECK, (unsigned)j->err, why);
        return;
    }
    const devimg_id_t id = j->id;
    s_buf[id] = j->buf;
    s_n[id] = j->n;
    s_info[id] = j->info;
    memcpy(s_sha[id], j->sha, FW_SHA256_BYTES);
    s_sig[id] = j->sig;
    atomic_store(&s_ready[id], true);
    j->buf = NULL;
    devimg_ready(&s_sl[id], now_ms());
    upd_screen_set_result(k_step[id], NULL);
    atomic_store(&s_busy, false);
    ESP_LOGI(TAG, "%s: %s %lu bytes, SHA-256 %02x%02x%02x%02x, from %s",
             j->name, j->info.version_text, (unsigned long)j->n, j->sha[0],
             j->sha[1], j->sha[2], j->sha[3],
             j->card ? "the SD card" : "GitHub");
    if (!fw_sig_signed(j->sig)) {
        /* The build warns (devimg_sig_policy()): the band says so once,
         * and the step's card keeps the line (upd_fetch_line()). */
        char line[UI_ALERT_MAX];
        upd_fetch_nosig_note(j->name, line, sizeof(line));
        ui_router_set_notice(line);
        ESP_LOGW(TAG, "%s: %s; installed only with a warning", j->name,
                 fw_sig_str(j->sig));
    }
}

void devimg_service(bool armed)
{
    (void)armed;
    if (!atomic_load(&s_busy)) {
        (void)panel_devimg_take_cancel();
        return;
    }
    if (s_sl[s_cur].st == DEVIMG_FETCH) {
        fetch_service();
    } else {
        job_service();
    }
}
