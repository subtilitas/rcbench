/**
 * @file panel_sbk.c
 * @brief The settings backup on the panel: the frame's side of the driver
 *        (sbk_ctl.h), the request handed to the logger task, and the card
 *        functions of BENCH/SETUP (settings_backup.h) it runs them with.
 *
 * The render loop calls panel_sbk_frame() once a frame, after the settings
 * save's tick: it feeds the driver, hands a request over when none is out
 * and gives the driver the answer once the logger has one.  The logger task
 * calls panel_sbk_card_serve() in its pass, beside the servo programmer's
 * files, so no card call is made by the render loop or the control task.
 * The text travels in sbk_buffer(), which only the side that holds the
 * request touches.
 *
 * SPDX-License-Identifier: MIT
 */
#include "panel_sbk.h"

#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "card_tree.h"
#include "esp_log.h"
#include "panel.h"
#include "sbk_ctl.h"
#include "settings_backup.h"
#include "storage.h"

static const char *TAG = "rcbench";  /**< ESP-IDF log tag. */

/** @brief The request handed to the logger. */
static sb_req_t    s_req;
/** @brief Its answer, the logger's until it counts s_answered up. */
static sb_done_t   s_done;
/** @brief The mount the request was taken under (storage_mount_count()). */
static unsigned    s_mount;
/** @brief Requests the render loop handed over. */
static atomic_uint s_sent;
/** @brief Requests the logger answered. */
static atomic_uint s_answered;
/** @brief A request is out: the render loop waits for its answer. */
static bool        s_req_out;

/* --------------------------------------------------- the card's functions */

/** @brief Whether the logger holds the request's volume; set per request. */
static bool s_held;

/** @brief The full path of @p name in CARD_SETUP. */
static void path_of(const char *name, char *out, size_t n)
{
    (void)snprintf(out, n, "%s/%s/%s", STORAGE_MOUNT_POINT, CARD_SETUP, name);
}

/** @brief Room for a path of the folder: mount point, folder, name. */
#define SBK_PATH (sizeof(STORAGE_MOUNT_POINT) + sizeof(CARD_SETUP) \
                  + SB_NAME_MAX + 2u)

/** @brief sb_io_t.mounted: the request's volume is held. */
static bool io_mounted(void *ctx)
{
    (void)ctx;
    return s_held;
}

/** @brief sb_io_t.size: the file's bytes, -1 for no file. */
static int32_t io_size(void *ctx, const char *name)
{
    (void)ctx;
    char path[SBK_PATH];
    storage_stat_t st;
    path_of(name, path, sizeof(path));
    if (storage_stat(path, &st) != ESP_OK || st.is_dir
        || st.size > (uint64_t)INT32_MAX) {
        return -1;
    }
    return (int32_t)st.size;
}

/** @brief sb_io_t.read: up to @p cap bytes from the file's start. */
static int32_t io_read(void *ctx, const char *name, char *buf, size_t cap)
{
    (void)ctx;
    char path[SBK_PATH];
    path_of(name, path, sizeof(path));
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return -1;
    }
    const size_t n = fread(buf, 1, cap, f);
    const bool failed = ferror(f) != 0;
    fclose(f);
    return failed ? -1 : (int32_t)n;
}

/** @brief An errno of a write as sb_io_result_t. */
static sb_io_result_t write_err(int err)
{
    if (err == ENOSPC) {
        return SB_IO_FULL;
    }
    return (err == EROFS || err == EACCES || err == EPERM) ? SB_IO_DENIED
                                                           : SB_IO_ERROR;
}

/**
 * @brief sb_io_t.write: the folder made where it is missing, the file
 *        created or emptied, written, committed to the card and closed; on
 *        a failure the file goes.
 */
static sb_io_result_t io_write(void *ctx, const char *name, const char *data,
                               size_t len)
{
    (void)ctx;
    const card_dir_t made = card_make_dirs(CARD_SETUP, panel_card_make_dir,
                                           NULL);
    if (made != CARD_DIR_OK) {
        return (made == CARD_DIR_FILE || made == CARD_DIR_DENIED)
                   ? SB_IO_DENIED : SB_IO_ERROR;
    }
    char path[SBK_PATH];
    path_of(name, path, sizeof(path));
    const int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0664);
    if (fd < 0) {
        return write_err(errno);
    }
    sb_io_result_t r = SB_IO_OK;
    size_t done = 0u;
    while (done < len && r == SB_IO_OK) {
        const ssize_t n = write(fd, data + done, len - done);
        if (n > 0) {
            done += (size_t)n;
        } else {
            /* A write that takes nothing without an error is a full card. */
            r = (n == 0) ? SB_IO_FULL : write_err(errno);
        }
    }
    if (r == SB_IO_OK && fsync(fd) != 0) {
        r = write_err(errno);
    }
    if (close(fd) != 0 && r == SB_IO_OK) {
        r = write_err(errno);
    }
    if (r != SB_IO_OK) {
        (void)unlink(path);
    }
    return r;
}

/** @brief sb_io_t.rename: @p from takes the name @p to. */
static sb_io_result_t io_rename(void *ctx, const char *from, const char *to)
{
    (void)ctx;
    char a[SBK_PATH];
    char b[SBK_PATH];
    path_of(from, a, sizeof(a));
    path_of(to, b, sizeof(b));
    if (rename(a, b) == 0) {
        return SB_IO_OK;
    }
    return (errno == ENOENT) ? SB_IO_MISSING : write_err(errno);
}

/** @brief sb_io_t.remove: the file goes; SB_IO_MISSING for none. */
static sb_io_result_t io_remove(void *ctx, const char *name)
{
    (void)ctx;
    char path[SBK_PATH];
    path_of(name, path, sizeof(path));
    if (unlink(path) == 0) {
        return SB_IO_OK;
    }
    return (errno == ENOENT) ? SB_IO_MISSING : write_err(errno);
}

/** @brief The folder's functions. */
static const sb_io_t k_io = {
    .ctx = NULL,
    .mounted = io_mounted,
    .size = io_size,
    .read = io_read,
    .write = io_write,
    .rename = io_rename,
    .remove = io_remove,
};

/* ------------------------------------------------------- the two tasks */

void panel_sbk_card_serve(void)
{
    const unsigned sent = atomic_load(&s_sent);
    if (sent == atomic_load(&s_answered)) {
        return;
    }
    /* Held across the request's card calls, on the volume it was taken
     * under: a card put in since is not the one the request was for. */
    s_held = storage_hold_mount(s_mount);
    sb_run(&k_io, &s_req, sbk_buffer(), sbk_buffer_size(), &s_done);
    if (s_held) {
        storage_release();
    }
    s_held = false;
    if (s_done.result != SB_OK) {
        ESP_LOGW(TAG, "settings backup: request %u ended %u",
                 (unsigned)s_req.op, (unsigned)s_done.result);
    }
    atomic_store(&s_answered, sent);
}

void panel_sbk_frame(bool committed, bool armed)
{
    /* The answer with this frame's ARMED: a load that arrives in the first
     * frame that sees ARMED is refused (sbk_done()). */
    if (s_req_out && atomic_load(&s_answered) == atomic_load(&s_sent)) {
        s_req_out = false;
        sbk_done(&s_done, armed);
    }
    const sbk_feed_t feed = {
        .mounted = storage_mounted(),
        .armed = armed,
        .committed = committed,
        .wall_s = (int64_t)time(NULL),
        .mount = storage_mount_count(),
    };
    /* Every frame, so a save to the store is seen while a request is out;
     * the driver hands out none until it has the answer. */
    sb_req_t req;
    if (sbk_tick(&feed, &req) && !s_req_out) {
        s_req = req;
        /* Before the count, which hands the request and the buffer over. */
        s_mount = storage_mount_count();
        s_req_out = true;
        atomic_fetch_add(&s_sent, 1u);
    }
}

bool panel_sbk_busy(void)
{
    return atomic_load(&s_sent) != atomic_load(&s_answered);
}
