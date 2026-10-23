/**
 * @file panel_files.c
 * @brief FILES' card task: the file manager's card (fm_io.h) over the
 *        storage component, the task cardjob that runs the screen's
 *        requests (fm_req.h), and the screen's way to it (files_io_t).
 *
 * The FILES screen makes no card call: it hands a request over through
 * k_files_io, and cardjob makes every call of it, listings and INFO
 * included, one request at a time.  A long copy then holds up neither a
 * frame nor STOP.  cardjob runs at priority 1 on core 0, the render
 * loop's, and is created at the first request, not at start-up: its
 * stack (FILES_STACK) and the copy's chunk (FM_CHUNK, internal and
 * DMA-capable, since the card's driver reads a PSRAM target one sector at
 * a time) cost internal RAM only once FILES is used.  Both are kept from
 * then on.
 *
 * Every call runs under the hold of the mount the names were listed from
 * (storage_hold_mount()), which fm_tree.c and fm_ops.c take: a card swapped
 * since is never written under a stale name.  A file stays held from
 * open() to close() as well (storage_hold()).  A call that fails with no
 * other reason looks at the slot once (storage_check()); a card found gone
 * answers FM_ERR_CARD_GONE.
 *
 * The bench the write gate reads is the control task's snapshot: ARMED
 * and an arm waiting count as ARMED, the IOmcu link as it stands, and the
 * logger's open files (log_open_marks()).  A job reads it before each
 * chunk, so an ARM stops a copy at its next 16 KiB.
 *
 * Each copy logs its bytes, its time and its rate, and the stack cardjob
 * left unused: the card's rate on this board is not measured.
 *
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "files_screen.h"
#include "fm_io.h"
#include "fm_ops.h"
#include "fm_req.h"
#include "log_take.h"
#include "panel.h"
#include "storage.h"
#include "ui_screen.h"

static const char *TAG = "rcbench";

/** @brief cardjob's stack, bytes: the logger's figure for the deepest card
 *         chain, a walk with the card mounted again under it (main.c). */
#define FILES_STACK 6656
/** @brief cardjob's priority: the render loop's. */
#define FILES_PRIO  1
/** @brief cardjob's core: the render loop's. */
#define FILES_CORE  0

/** @brief Room for "/sdcard/" and a path of the file manager. */
#define FILES_PATH (sizeof(STORAGE_MOUNT_POINT) + FM_PATH_SIZE)

/* ------------------------------------------------------------ the card */

/**
 * @brief The full path of @p path, a path from the card's root.
 * @param path  the path; "" for the root
 * @param out   takes "/sdcard/<path>", or "/sdcard" for the root
 * @param n     bytes at @p out
 * @return false for a path that does not fit
 */
static bool full_path(const char *path, char *out, size_t n)
{
    const int w = (path[0] == '\0')
                      ? snprintf(out, n, "%s", STORAGE_MOUNT_POINT)
                      : snprintf(out, n, "%s/%s", STORAGE_MOUNT_POINT, path);
    return w > 0 && (size_t)w < n;
}

/**
 * @brief The core's answer for a failed call's errno.  A failure with no
 *        other reason looks at the slot: a card taken out answers so.
 * @param e  the errno
 * @return the answer
 */
static fm_err_t err_of(int e)
{
    switch (e) {
    case ENOENT:
    case ENOTDIR:
        return FM_ERR_NOT_FOUND;
    case EEXIST:
        return FM_ERR_EXISTS;
    case ENOSPC:
        return FM_ERR_FULL;
    case EACCES:
    case EPERM:
    case EROFS:
        return FM_ERR_DENIED;
    case ENOTEMPTY:
        return FM_ERR_NOT_EMPTY;
    case ENAMETOOLONG:
    case EINVAL:
        return FM_ERR_NAME;
    default:
        break;
    }
    (void)storage_check();
    return storage_mounted() ? FM_ERR_IO : FM_ERR_CARD_GONE;
}

/**
 * @brief The answer of a call that found nothing it can name.
 * @return FM_ERR_IO with a volume mounted, FM_ERR_CARD_GONE without
 */
static fm_err_t gone_or_io(void)
{
    return storage_mounted() ? FM_ERR_IO : FM_ERR_CARD_GONE;
}

/**
 * @brief fm_io_t mounted().
 * @param ctx  unused
 * @return storage_mounted()
 */
static bool io_mounted(void *ctx)
{
    (void)ctx;
    return storage_mounted();
}

/**
 * @brief fm_io_t mounts().
 * @param ctx  unused
 * @return storage_mount_count()
 */
static unsigned io_mounts(void *ctx)
{
    (void)ctx;
    return storage_mount_count();
}

/**
 * @brief fm_io_t hold().
 * @param ctx    unused
 * @param mount  the mount the names were read from
 * @return storage_hold_mount()
 */
static bool io_hold(void *ctx, unsigned mount)
{
    (void)ctx;
    return storage_hold_mount(mount);
}

/**
 * @brief fm_io_t release().
 * @param ctx  unused
 */
static void io_release(void *ctx)
{
    (void)ctx;
    storage_release();
}

/** @brief A walk's visit and its context, for walk_take(). */
typedef struct {
    fm_visit_fn visit;   /**< the core's visit      */
    void *vctx;          /**< its context           */
} walk_ctx_t;

/**
 * @brief One entry of storage_walk_counted() handed to the core.
 * @param entry  the entry, borrowed for the call
 * @param ctx    the walk_ctx_t
 */
static void walk_take(const storage_entry_t *entry, void *ctx)
{
    const walk_ctx_t *w = (const walk_ctx_t *)ctx;
    const fm_dirent_t e = {
        .name = entry->name, .size = entry->size, .is_dir = entry->is_dir,
    };
    w->visit(&e, w->vctx);
}

/**
 * @brief fm_io_t walk(): storage_walk_counted() with no suffix filter, so
 *        every entry is visited or counted: a name starting with "." and a
 *        name of 64 characters or more are counted as unlisted.
 * @param ctx       unused
 * @param dir       the folder
 * @param visit     called per entry
 * @param vctx      passed to @p visit
 * @param unlisted  takes the count left out; may be NULL
 * @return FM_OK, FM_ERR_NOT_FOUND, FM_ERR_CARD_GONE or FM_ERR_IO
 */
static fm_err_t io_walk(void *ctx, const char *dir, fm_visit_fn visit,
                        void *vctx, unsigned *unlisted)
{
    (void)ctx;
    walk_ctx_t w = { .visit = visit, .vctx = vctx };
    unsigned left = 0u;
    const int n = storage_walk_counted(dir, NULL, walk_take, &w, NULL, &left);
    if (unlisted != NULL) {
        *unlisted = left;
    }
    if (n == STORAGE_WALK_NO_DIR) {
        return FM_ERR_NOT_FOUND;
    }
    if (n == STORAGE_WALK_REMOUNTED) {
        return FM_ERR_CARD_GONE;
    }
    return (n < 0) ? gone_or_io() : FM_OK;
}

/**
 * @brief fm_io_t stat(), FatFs's own f_stat() through storage_stat() for
 *        a 64-bit size.
 * @param ctx   unused
 * @param path  the name's path
 * @param out   takes the answer
 * @return FM_OK, FM_ERR_NOT_FOUND, FM_ERR_CARD_GONE or FM_ERR_IO
 */
static fm_err_t io_stat(void *ctx, const char *path, fm_stat_t *out)
{
    (void)ctx;
    char full[FILES_PATH];
    if (!full_path(path, full, sizeof(full))) {
        return FM_ERR_NAME;
    }
    storage_stat_t st;
    const esp_err_t r = storage_stat(full, &st);
    if (r == ESP_ERR_NOT_FOUND) {
        return FM_ERR_NOT_FOUND;
    }
    if (r != ESP_OK) {
        return gone_or_io();
    }
    out->size = st.size;
    out->is_dir = st.is_dir;
    out->mtime = (st.mtime >= 0) ? st.mtime : -1;
    return FM_OK;
}

/**
 * @brief A path call on the full path: mkdir(), unlink() or rmdir().
 * @param path  the path from the root
 * @param call  0 mkdir, 1 unlink, 2 rmdir
 * @return FM_OK or err_of()
 */
static fm_err_t path_call(const char *path, int call)
{
    char full[FILES_PATH];
    if (!full_path(path, full, sizeof(full))) {
        return FM_ERR_NAME;
    }
    const int r = (call == 0) ? mkdir(full, 0777)
                  : (call == 1) ? unlink(full)
                                : rmdir(full);
    return (r == 0) ? FM_OK : err_of(errno);
}

/**
 * @brief fm_io_t mkdir().
 * @param ctx   unused
 * @param path  the new folder
 * @return path_call()
 */
static fm_err_t io_mkdir(void *ctx, const char *path)
{
    (void)ctx;
    return path_call(path, 0);
}

/**
 * @brief fm_io_t unlink().
 * @param ctx   unused
 * @param path  the file
 * @return path_call()
 */
static fm_err_t io_unlink(void *ctx, const char *path)
{
    (void)ctx;
    return path_call(path, 1);
}

/**
 * @brief fm_io_t rmdir().  FatFs refuses a folder with entries with
 *        FR_DENIED, which is EACCES and FM_ERR_DENIED.
 * @param ctx   unused
 * @param path  the folder
 * @return path_call()
 */
static fm_err_t io_rmdir(void *ctx, const char *path)
{
    (void)ctx;
    return path_call(path, 2);
}

/**
 * @brief fm_io_t rename(): FatFs's f_rename(), which renames over no
 *        name (FR_EXIST, EEXIST) but the same entry's in another case.
 * @param ctx   unused
 * @param from  the entry's path
 * @param to    its new path
 * @return FM_OK or err_of()
 */
static fm_err_t io_rename(void *ctx, const char *from, const char *to)
{
    (void)ctx;
    char a[FILES_PATH];
    char b[FILES_PATH];
    if (!full_path(from, a, sizeof(a)) || !full_path(to, b, sizeof(b))) {
        return FM_ERR_NAME;
    }
    return (rename(a, b) == 0) ? FM_OK : err_of(errno);
}

/**
 * @brief The descriptor of a handle: fd + 1, so no handle is NULL.
 * @param file  the handle
 * @return the descriptor
 */
static int fd_of(void *file)
{
    return (int)(intptr_t)file - 1;
}

/**
 * @brief fm_io_t open(): a descriptor, the volume held until close().  A
 *        new file is O_EXCL: a taken name is FM_ERR_EXISTS.
 * @param ctx   unused
 * @param path  the file
 * @param mode  read, or a new file
 * @param file  takes the handle
 * @return FM_OK or why not
 */
static fm_err_t io_open(void *ctx, const char *path, fm_mode_t mode,
                        void **file)
{
    (void)ctx;
    char full[FILES_PATH];
    if (!full_path(path, full, sizeof(full))) {
        return FM_ERR_NAME;
    }
    if (!storage_hold()) {
        return gone_or_io();
    }
    const int fd = (mode == FM_OPEN_NEW)
                       ? open(full, O_WRONLY | O_CREAT | O_EXCL, 0666)
                       : open(full, O_RDONLY);
    if (fd < 0) {
        const int e = errno;
        storage_release();
        return err_of(e);
    }
    *file = (void *)(intptr_t)(fd + 1);
    return FM_OK;
}

/**
 * @brief fm_io_t read().
 * @param ctx   unused
 * @param file  the handle
 * @param buf   takes the bytes
 * @param cap   room in @p buf
 * @param got   takes the count, 0 at the end
 * @return FM_OK or err_of()
 */
static fm_err_t io_read(void *ctx, void *file, uint8_t *buf, size_t cap,
                        size_t *got)
{
    (void)ctx;
    const ssize_t r = read(fd_of(file), buf, cap);
    if (r < 0) {
        *got = 0u;
        return err_of(errno);
    }
    *got = (size_t)r;
    return FM_OK;
}

/**
 * @brief fm_io_t write(): all of it, or FM_ERR_FULL for a short write.
 * @param ctx   unused
 * @param file  the handle
 * @param buf   the bytes
 * @param len   how many
 * @return FM_OK, FM_ERR_FULL or err_of()
 */
static fm_err_t io_write(void *ctx, void *file, const uint8_t *buf,
                         size_t len)
{
    (void)ctx;
    size_t at = 0u;
    while (at < len) {
        const ssize_t w = write(fd_of(file), buf + at, len - at);
        if (w < 0) {
            return err_of(errno);
        }
        if (w == 0) {
            return FM_ERR_FULL;    /* FatFs writes 0 bytes to a full card */
        }
        at += (size_t)w;
    }
    return FM_OK;
}

/**
 * @brief fm_io_t sync(): fsync(), the file on the card.
 * @param ctx   unused
 * @param file  the handle
 * @return FM_OK or err_of()
 */
static fm_err_t io_sync(void *ctx, void *file)
{
    (void)ctx;
    return (fsync(fd_of(file)) == 0) ? FM_OK : err_of(errno);
}

/**
 * @brief fm_io_t close(), and the open's hold given back.
 * @param ctx   unused
 * @param file  the handle
 * @return FM_OK or err_of()
 */
static fm_err_t io_close(void *ctx, void *file)
{
    (void)ctx;
    const int r = close(fd_of(file));
    const int e = errno;
    storage_release();
    return (r == 0) ? FM_OK : err_of(e);
}

/** @brief The card as the core sees it. */
static const fm_io_t k_io = {
    .mounted = io_mounted, .mounts = io_mounts, .hold = io_hold,
    .release = io_release, .walk = io_walk, .stat = io_stat,
    .mkdir = io_mkdir, .unlink = io_unlink, .rmdir = io_rmdir,
    .rename = io_rename, .open = io_open, .read = io_read,
    .write = io_write, .sync = io_sync, .close = io_close, .ctx = NULL,
};

/* ------------------------------------------------------------- cardjob */

/* The request handed over, cardjob's from the count up to its answer. */
static fm_req_t *s_req;
/* Requests handed over and answered: equal while none is under way. */
static atomic_uint s_sent;
static atomic_uint s_done;
/* CANCEL, set by the render task, read by cardjob before each step. */
static atomic_bool s_cancel;
/* The job's bytes done and to do, 64-bit: under s_prog_mux. */
static portMUX_TYPE s_prog_mux = portMUX_INITIALIZER_UNLOCKED;
static uint64_t s_prog_done;
static uint64_t s_prog_total;
static TaskHandle_t s_task;
static uint8_t *s_chunk;
/* The logger's open files as cardjob last read them, and their folders. */
static log_marks_t s_job_marks;
static char s_job_run_dir[CARD_DIR_MAX];
static char s_job_test_dir[CARD_DIR_MAX];

/**
 * @brief The runner's bench(): the control task's snapshot and the
 *        logger's open files, now.
 * @param out  takes the bench
 * @param ctx  unused
 */
static void job_bench(fm_bench_t *out, void *ctx)
{
    (void)ctx;
    snap_lock();
    out->armed = s_snap.armed || s_snap.arm_wanted;
    out->link_up = s_snap.link_up;
    snap_unlock();
    log_open_marks(&s_job_marks, s_job_run_dir, s_job_test_dir);
    out->marks = &s_job_marks;
}

/**
 * @brief The runner's cancelled().
 * @param ctx  unused
 * @return CANCEL was pressed
 */
static bool job_cancelled(void *ctx)
{
    (void)ctx;
    return atomic_load(&s_cancel);
}

/**
 * @brief The runner's progress().
 * @param done   bytes done
 * @param total  bytes to do
 * @param ctx    unused
 */
static void job_progress(uint64_t done, uint64_t total, void *ctx)
{
    (void)ctx;
    taskENTER_CRITICAL(&s_prog_mux);
    s_prog_done = done;
    s_prog_total = total;
    taskEXIT_CRITICAL(&s_prog_mux);
}

/**
 * @brief The runner's look(): a card put in, taken out or swapped is found.
 * @param ctx  unused
 */
static void job_look(void *ctx)
{
    (void)ctx;
    (void)storage_check();
}

/**
 * @brief A copy's bytes, time and rate, and the stack cardjob left: the
 *        card's figures on this board.
 * @param req  the answered request
 * @param us   its time, microseconds
 */
static void log_copy(const fm_req_t *req, int64_t us)
{
    if (req->kind != FM_REQ_JOB || req->job.kind != FM_JOB_COPY) {
        return;
    }
    const uint64_t ms = (us > 0) ? (uint64_t)us / 1000u : 0u;
    const uint64_t rate = (us > 0) ? req->job.done * 1000000u / (uint64_t)us
                                         / 1024u
                                   : 0u;
    ESP_LOGI(TAG, "files: copy of %lluB in %llums: %llukB/s, %s; cardjob "
             "stack %uB unused", (unsigned long long)req->job.done,
             (unsigned long long)ms, (unsigned long long)rate,
             (req->err == FM_OK) ? "done" : "not done",
             (unsigned)uxTaskGetStackHighWaterMark(NULL));
}

/**
 * @brief cardjob: each request handed over, run to its end.
 * @param arg  unused
 */
static void files_task(void *arg)
{
    (void)arg;
    const fm_runner_t run = {
        .io = &k_io, .bench = job_bench, .cancelled = job_cancelled,
        .progress = job_progress, .look = job_look, .chunk = s_chunk,
        .size = FM_CHUNK, .ctx = NULL,
    };
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        const unsigned sent = atomic_load(&s_sent);
        if (sent == atomic_load(&s_done)) {
            continue;
        }
        fm_req_t *req = s_req;
        const int64_t t0 = esp_timer_get_time();
        (void)fm_req_run(req, &run);
        log_copy(req, esp_timer_get_time() - t0);
        atomic_store(&s_done, sent);
    }
}

/**
 * @brief cardjob and its chunk, at the first request.
 * @return true when the task runs
 */
static bool start_task(void)
{
    if (s_task != NULL) {
        return true;
    }
    const size_t before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    if (s_chunk == NULL) {
        s_chunk = heap_caps_malloc(FM_CHUNK, MALLOC_CAP_INTERNAL
                                                 | MALLOC_CAP_DMA
                                                 | MALLOC_CAP_8BIT);
    }
    if (s_chunk == NULL
        || xTaskCreatePinnedToCore(files_task, "cardjob", FILES_STACK, NULL,
                                   FILES_PRIO, &s_task, FILES_CORE)
               != pdPASS) {
        s_task = NULL;
        ESP_LOGW(TAG, "files: no internal RAM for cardjob");
        return false;
    }
    ESP_LOGI(TAG, "files: cardjob started; internal RAM %uB free before, "
             "%uB after", (unsigned)before,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    return true;
}

/* ----------------------------------------------------------- the screen */

/**
 * @brief files_io_t submit().
 * @param req  the request
 * @param ctx  unused
 * @return false with one under way, or with no task
 */
static bool fio_submit(fm_req_t *req, void *ctx)
{
    (void)ctx;
    if (atomic_load(&s_sent) != atomic_load(&s_done) || !start_task()) {
        return false;
    }
    s_req = req;
    atomic_store(&s_cancel, false);
    job_progress(0u, 0u, NULL);
    atomic_fetch_add(&s_sent, 1u);
    xTaskNotifyGive(s_task);
    return true;
}

/**
 * @brief files_io_t idle().
 * @param ctx  unused
 * @return true with no request under way
 */
static bool fio_idle(void *ctx)
{
    (void)ctx;
    return atomic_load(&s_sent) == atomic_load(&s_done);
}

/**
 * @brief files_io_t progress().
 * @param done   takes the bytes done
 * @param total  takes the bytes to do
 * @param ctx    unused
 */
static void fio_progress(uint64_t *done, uint64_t *total, void *ctx)
{
    (void)ctx;
    taskENTER_CRITICAL(&s_prog_mux);
    *done = s_prog_done;
    *total = s_prog_total;
    taskEXIT_CRITICAL(&s_prog_mux);
}

/**
 * @brief files_io_t cancel().
 * @param ctx  unused
 */
static void fio_cancel(void *ctx)
{
    (void)ctx;
    atomic_store(&s_cancel, true);
}

/* The logger's open files as the screen last read them: the task that
 * renders' own. */
static log_marks_t s_ui_marks;
static char s_ui_run_dir[CARD_DIR_MAX];
static char s_ui_test_dir[CARD_DIR_MAX];

/**
 * @brief files_io_t bench(): the band's ARMED and link, and the logger's
 *        open files, for the footer's gate and the rows' marks.
 * @param out  takes the bench
 * @param ctx  unused
 */
static void fio_bench(fm_bench_t *out, void *ctx)
{
    (void)ctx;
    const ui_bench_status_t *st = ui_router_status();
    out->armed = st->armed;
    out->link_up = st->link_up;
    log_open_marks(&s_ui_marks, s_ui_run_dir, s_ui_test_dir);
    out->marks = &s_ui_marks;
}

/**
 * @brief files_io_t volume().
 * @param ctx  unused
 * @return the card's name, the render task's copy
 */
static const char *fio_volume(void *ctx)
{
    (void)ctx;
    static char name[STORAGE_NAME_MAX];
    storage_card_name(name, sizeof(name));
    return name;
}

/**
 * @brief files_io_t card().
 * @param ctx  unused
 * @return storage_card_state()
 */
static card_state_t fio_card(void *ctx)
{
    (void)ctx;
    return storage_card_state();
}

const files_io_t k_files_io = {
    .submit = fio_submit, .idle = fio_idle, .progress = fio_progress,
    .cancel = fio_cancel, .bench = fio_bench, .volume = fio_volume,
    .card = fio_card, .ctx = NULL,
};
