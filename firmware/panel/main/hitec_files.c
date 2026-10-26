/**
 * @file
 * @brief The Hitec programmer's servo files on the card: the 5 functions of
 *        servo_store_io_t over the folder BENCH/HITEC/CONF of the mounted
 *        card (CARD_HITEC_CONF), and the logger task's service of the
 *        model's requests (hitec_card_run.h).
 *
 * Hitec is a name or trademark of its owner; it is used here only to say
 * which servos this module works with.
 *
 * The render loop takes a request from the model into s_hitec_card_req and
 * counts s_hitec_card_sent up; the logger task runs it on the card
 * (hitec_card_serve()), leaves the answer in s_hitec_card_done and counts
 * s_hitec_card_answered up.  One request at a time: the render loop takes
 * the next only while the two counts are equal, and reads the answer only
 * after they are equal again.
 *
 * Every call blocks for as long as the card takes.  They run on the logger
 * task, never on the render loop.
 *
 * SPDX-License-Identifier: MIT
 */
#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "card_tree.h"
#include "hitec_card_run.h"
#include "panel.h"
#include "servo_store.h"
#include "storage.h"

/** @brief The mount point, the folder, a name of the store and its
 *         separators. */
#define PATH_MAX_LEN (sizeof(STORAGE_MOUNT_POINT) + sizeof(CARD_HITEC_CONF) \
                      + STORAGE_NAME_MAX + 2u)

/** @brief The request (panel.h). */
hitec_card_req_t  s_hitec_card_req;
/** @brief The mount it was taken under (panel.h). */
unsigned          s_hitec_card_mount;
/** @brief The answer (panel.h). */
hitec_card_done_t s_hitec_card_done;
/** @brief Requests handed over (panel.h). */
atomic_uint       s_hitec_card_sent;
/** @brief Requests answered (panel.h). */
atomic_uint       s_hitec_card_answered;

/**
 * @brief The full path of @p name inside the folder.
 *
 * @param name     The name; "" for the folder itself.
 * @param out      Receives the path.
 * @param out_size Bytes at @p out.
 */
static void path_of(const char *name, char *out, size_t out_size)
{
    if (name[0] == '\0') {
        snprintf(out, out_size, "%s/%s", STORAGE_MOUNT_POINT,
                 CARD_HITEC_CONF);
    } else {
        snprintf(out, out_size, "%s/%s/%s", STORAGE_MOUNT_POINT,
                 CARD_HITEC_CONF, name);
    }
}

/**
 * @brief Whether the folder is there.
 *
 * @return 1: a folder has its name; 0: nothing has it; -1: the card does
 *         not say, or a file has the name.
 */
static int dir_state(void)
{
    char path[PATH_MAX_LEN];
    struct stat st;

    path_of("", path, sizeof(path));
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode) ? 1 : -1;
    }
    return errno == ENOENT ? 0 : -1;
}

/**
 * @brief Whether a failed call says that the card takes no file.
 *
 * @param err The errno.
 * @return true for a write-protected card or a refused access.
 */
static bool denied(int err)
{
    return err == EROFS || err == EACCES || err == EPERM;
}

/** @brief Whether the logger holds the volume for the request. */
static bool s_held;

/**
 * @brief servo_store_io_t.mounted: the volume held for the request.
 *
 * @param ctx Unused.
 * @return true when held.
 */
static bool io_mounted(void *ctx)
{
    (void)ctx;
    return s_held;
}

/** @brief A walk handed to storage_walk(). */
typedef struct {
    servo_store_visit_fn visit;  /**< the store's visit */
    void *ctx;                   /**< its context       */
} files_walk_t;

/**
 * @brief One entry of the walk to the store's visit.
 *
 * @param entry The entry.
 * @param ctx   The files_walk_t.
 */
static void walk_entry(const storage_entry_t *entry, void *ctx)
{
    const files_walk_t *w = ctx;

    w->visit(entry->name, entry->is_dir, w->ctx);
}

/**
 * @brief servo_store_io_t.walk over the folder's ".txt" files.
 *
 * @param ctx       Unused.
 * @param visit     The store's visit.
 * @param visit_ctx Its context.
 * @return false when the folder does not read to its end.
 */
static bool io_walk(void *ctx, servo_store_visit_fn visit, void *visit_ctx)
{
    files_walk_t w = { .visit = visit, .ctx = visit_ctx };
    const int state = dir_state();

    (void)ctx;
    if (state <= 0) {
        /* A folder that does not exist has no entry. */
        return state == 0;
    }
    return storage_walk(CARD_HITEC_CONF, ".txt", walk_entry, &w, NULL) >= 0;
}

/**
 * @brief servo_store_io_t.size, by storage_stat(): stat()'s 32-bit st_size
 *        reads a file of 4GiB and more as a small one.
 *
 * @param ctx  Unused.
 * @param name The file.
 * @return Its size; -1 for none.
 */
static int32_t io_size(void *ctx, const char *name)
{
    char path[PATH_MAX_LEN];
    storage_stat_t st;

    (void)ctx;
    path_of(name, path, sizeof(path));
    if (storage_stat(path, &st) != ESP_OK || st.is_dir
        || st.size > (uint64_t)INT32_MAX) {
        return -1;
    }
    return (int32_t)st.size;
}

/**
 * @brief servo_store_io_t.read.
 *
 * @param ctx  Unused.
 * @param name The file.
 * @param buf  Receives up to @p cap bytes.
 * @param cap  Bytes at @p buf.
 * @return The count; -1 on a failure.
 */
static int32_t io_read(void *ctx, const char *name, char *buf, size_t cap)
{
    char path[PATH_MAX_LEN];
    FILE *f;
    size_t n;
    bool failed;

    (void)ctx;
    path_of(name, path, sizeof(path));
    f = fopen(path, "rb");
    if (f == NULL) {
        return -1;
    }
    n = fread(buf, 1, cap, f);
    failed = ferror(f) != 0;
    fclose(f);
    return failed ? -1 : (int32_t)n;
}

/**
 * @brief servo_store_io_t.create: the folder made where it is missing, the
 *        file created new, written, committed and closed.
 *
 * @param ctx  Unused.
 * @param name The file.
 * @param data Its bytes.
 * @param len  Their count.
 * @return servo_store_io_result_t.
 */
static servo_store_io_result_t io_create(void *ctx, const char *name,
                                         const char *data, size_t len)
{
    char path[PATH_MAX_LEN];
    servo_store_io_result_t r = SERVO_STORE_IO_OK;
    size_t done = 0;
    card_dir_t made;
    int fd;

    (void)ctx;
    made = dir_state() > 0 ? CARD_DIR_OK
                           : card_make_dirs(CARD_HITEC_CONF,
                                            panel_card_make_dir, NULL);
    if (made != CARD_DIR_OK) {
        /* A file holding a level's name, or a card that takes no folder,
         * is a card that takes no file until it is seen to. */
        return (made == CARD_DIR_FILE || made == CARD_DIR_DENIED)
                   ? SERVO_STORE_IO_DENIED
                   : SERVO_STORE_IO_ERROR;
    }
    path_of(name, path, sizeof(path));
    fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0664);
    if (fd < 0) {
        return errno == EEXIST   ? SERVO_STORE_IO_EXISTS
               : errno == ENOSPC ? SERVO_STORE_IO_FULL
               : denied(errno)   ? SERVO_STORE_IO_DENIED
                                 : SERVO_STORE_IO_ERROR;
    }
    while (done < len && r == SERVO_STORE_IO_OK) {
        const ssize_t n = write(fd, data + done, len - done);

        if (n > 0) {
            done += (size_t)n;
        } else {
            /* A write that takes nothing without an error is a full card. */
            r = (n == 0 || errno == ENOSPC) ? SERVO_STORE_IO_FULL
                                            : SERVO_STORE_IO_ERROR;
        }
    }
    if (r == SERVO_STORE_IO_OK && fsync(fd) != 0) {
        r = errno == ENOSPC ? SERVO_STORE_IO_FULL : SERVO_STORE_IO_ERROR;
    }
    if (close(fd) != 0 && r == SERVO_STORE_IO_OK) {
        r = errno == ENOSPC ? SERVO_STORE_IO_FULL : SERVO_STORE_IO_ERROR;
    }
    if (r != SERVO_STORE_IO_OK) {
        (void)remove(path);
    }
    return r;
}

/** @brief The card for hitec_card_run(). */
static const servo_store_io_t k_io = {
    .ctx = NULL,
    .stem = HITEC_CARD_STEM,
    .mounted = io_mounted,
    .walk = io_walk,
    .size = io_size,
    .read = io_read,
    .create = io_create,
};

/**
 * @brief Run the request the render loop handed over (panel.h).
 */
void hitec_card_serve(void)
{
    const unsigned sent = atomic_load(&s_hitec_card_sent);

    if (sent == atomic_load(&s_hitec_card_answered)) {
        return;
    }
    /* Held across the request's card calls, on the volume it was taken
     * under: a card mounted in its place since holds other files under
     * the same names, and the request finds no card. */
    s_held = storage_hold_mount(s_hitec_card_mount);
    hitec_card_run(&k_io, &s_hitec_card_req, &s_hitec_card_done);
    if (s_held) {
        storage_release();
        s_held = false;
    }
    atomic_store(&s_hitec_card_answered, sent);
}
