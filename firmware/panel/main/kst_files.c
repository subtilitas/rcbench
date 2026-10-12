/*
 * The card as the servo programmer's files see it.  See kst_files.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "kst_files.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "storage.h"

/* The mount point, the directory, a name of the store and its separators. */
#define PATH_MAX_LEN (sizeof(STORAGE_MOUNT_POINT) + sizeof(KST_STORE_DIR) \
                      + STORAGE_NAME_MAX + 2u)

static void dir_path(char *out, size_t out_size)
{
    snprintf(out, out_size, "%s/%s", STORAGE_MOUNT_POINT, KST_STORE_DIR);
}

void kst_files_path(const char *name, char *out, size_t out_size)
{
    snprintf(out, out_size, "%s/%s/%s", STORAGE_MOUNT_POINT, KST_STORE_DIR,
             name);
}

/* 1: a directory of that name is there; 0: nothing has the name; -1: the
 * card does not say, or a file has the name. */
static int dir_state(void)
{
    char path[PATH_MAX_LEN];
    struct stat st;

    dir_path(path, sizeof(path));
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode) ? 1 : -1;
    }
    return errno == ENOENT ? 0 : -1;
}

bool kst_files_dir(void)
{
    char path[PATH_MAX_LEN];
    const int state = dir_state();

    if (state != 0) {
        return state > 0;
    }
    dir_path(path, sizeof(path));
    return mkdir(path, 0775) == 0 || dir_state() > 0;
}

/* Whether a failed call says that the card takes no file: it is write
 * protected, or access is refused. */
static bool denied(int err)
{
    return err == EROFS || err == EACCES || err == EPERM;
}

/* The answer to a create the directory was not there for: a file has its
 * name, or the card refuses to make it. */
static kst_store_io_result_t no_dir_result(void)
{
    char path[PATH_MAX_LEN];
    struct stat st;

    dir_path(path, sizeof(path));
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode) ? KST_STORE_IO_ERROR : KST_STORE_IO_DENIED;
    }
    if (errno != ENOENT) {
        return KST_STORE_IO_ERROR;
    }
    if (mkdir(path, 0775) == 0) {
        return KST_STORE_IO_ERROR;
    }
    return denied(errno) ? KST_STORE_IO_DENIED : KST_STORE_IO_ERROR;
}

static bool io_mounted(void *ctx)
{
    (void)ctx;
    return storage_mounted();
}

typedef struct {
    kst_store_visit_fn visit;
    void *ctx;
} walk_t;

static void walk_entry(const storage_entry_t *entry, void *ctx)
{
    const walk_t *w = ctx;

    w->visit(entry->name, entry->is_dir, w->ctx);
}

static bool io_walk(void *ctx, kst_store_visit_fn visit, void *visit_ctx)
{
    walk_t w = { .visit = visit, .ctx = visit_ctx };
    const int state = dir_state();

    (void)ctx;
    if (state <= 0) {
        /* A directory that does not exist has no entry. */
        return state == 0;
    }
    return storage_walk(KST_STORE_DIR, ".txt", walk_entry, &w, NULL) >= 0;
}

static int32_t io_size(void *ctx, const char *name)
{
    char path[PATH_MAX_LEN];
    struct stat st;

    (void)ctx;
    kst_files_path(name, path, sizeof(path));
    if (stat(path, &st) != 0 || S_ISDIR(st.st_mode)
        || st.st_size > INT32_MAX) {
        return -1;
    }
    return (int32_t)st.st_size;
}

static int32_t io_read(void *ctx, const char *name, char *buf, size_t cap)
{
    char path[PATH_MAX_LEN];
    FILE *f;
    size_t n;
    bool failed;

    (void)ctx;
    kst_files_path(name, path, sizeof(path));
    f = fopen(path, "rb");
    if (f == NULL) {
        return -1;
    }
    n = fread(buf, 1, cap, f);
    failed = ferror(f) != 0;
    fclose(f);
    return failed ? -1 : (int32_t)n;
}

static kst_store_io_result_t io_create(void *ctx, const char *name,
                                       const char *data, size_t len)
{
    char path[PATH_MAX_LEN];
    kst_store_io_result_t r = KST_STORE_IO_OK;
    size_t done = 0;
    int fd;

    (void)ctx;
    if (!kst_files_dir()) {
        return no_dir_result();
    }
    kst_files_path(name, path, sizeof(path));
    fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0664);
    if (fd < 0) {
        return errno == EEXIST   ? KST_STORE_IO_EXISTS
               : errno == ENOSPC ? KST_STORE_IO_FULL
               : denied(errno)   ? KST_STORE_IO_DENIED
                                 : KST_STORE_IO_ERROR;
    }
    while (done < len && r == KST_STORE_IO_OK) {
        const ssize_t n = write(fd, data + done, len - done);

        if (n > 0) {
            done += (size_t)n;
        } else {
            /* A write that takes nothing without an error is a full card. */
            r = (n == 0 || errno == ENOSPC) ? KST_STORE_IO_FULL
                                            : KST_STORE_IO_ERROR;
        }
    }
    if (r == KST_STORE_IO_OK && fsync(fd) != 0) {
        r = errno == ENOSPC ? KST_STORE_IO_FULL : KST_STORE_IO_ERROR;
    }
    if (close(fd) != 0 && r == KST_STORE_IO_OK) {
        r = errno == ENOSPC ? KST_STORE_IO_FULL : KST_STORE_IO_ERROR;
    }
    if (r != KST_STORE_IO_OK) {
        (void)remove(path);
    }
    return r;
}

const kst_store_io_t *kst_files_io(void)
{
    static const kst_store_io_t io = {
        .ctx = NULL,
        .mounted = io_mounted,
        .walk = io_walk,
        .size = io_size,
        .read = io_read,
        .create = io_create,
    };

    return &io;
}
