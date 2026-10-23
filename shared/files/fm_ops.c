/**
 * @file fm_ops.c
 * @brief The file manager's jobs, one step at a time (fm_ops.h).
 *
 * SPDX-License-Identifier: MIT
 */

#include "fm_ops.h"

#include <string.h>

#include "log_name.h"

/**
 * @brief The steps of a job, in the order they run.  A job without data
 *        does all of its work in STEP_CHECK.
 */
enum {
    STEP_CHECK = 0,  /**< the card's answers on the names; the opens    */
    STEP_DATA,       /**< one chunk                                     */
    STEP_SYNC,       /**< the ".NEW" on the card, and closed            */
    STEP_UNLINK,     /**< a save's old file deleted                     */
    STEP_RENAME,     /**< the ".NEW" given its name                     */
};

/* ------------------------------------------------------------- starting */

/**
 * @brief Hold a refusal in the job, so fm_job_state() says it too.
 * @param job  the job
 * @param err  the refusal
 * @return @p err
 */
static fm_err_t refuse(fm_job_t *job, fm_err_t err)
{
    job->state = FM_JOB_FAILED;
    job->err = err;
    return err;
}

/**
 * @brief Clear @p job and name what it does.
 * @param job    the job
 * @param kind   what it does
 * @param mount  the mount its names are from
 */
static void begin(fm_job_t *job, fm_job_kind_t kind, unsigned mount)
{
    memset(job, 0, sizeof(*job));
    job->kind = kind;
    job->mount = mount;
    job->state = FM_JOB_FAILED;
    job->err = FM_ERR_ARG;
}

/**
 * @brief Let a checked job run.
 * @param job  the job
 * @return FM_OK
 */
static fm_err_t run(fm_job_t *job)
{
    job->state = FM_JOB_RUNNING;
    job->err = FM_OK;
    job->step = STEP_CHECK;
    return FM_OK;
}

/**
 * @brief The path of @p name in folder @p dir (fm_path_join()).
 * @param out     room for FM_PATH_SIZE bytes
 * @param dir     the folder
 * @param name    the entry's name
 * @param is_dir  the entry is a folder: it counts in the depth
 * @return FM_OK, or FM_ERR_NAME for a path too long or too deep
 */
static fm_err_t join(char *out, const char *dir, const char *name,
                     bool is_dir)
{
    return fm_path_err(fm_path_join(dir, name, is_dir, out, FM_PATH_SIZE));
}

/**
 * @brief Write the path of @p name's ".NEW" in @p dir into @p out.
 * @param out   room for FM_PATH_SIZE bytes
 * @param dir   the folder
 * @param name  the file's name
 * @return FM_OK, or FM_ERR_NAME when it does not fit
 */
static fm_err_t tmp_path(char *out, const char *dir, const char *name)
{
    char tmp[FM_NAME_BUF];
    if (!fm_name_new(name, tmp, sizeof(tmp))) {
        return FM_ERR_NAME;
    }
    return join(out, dir, tmp, false);
}

fm_err_t fm_job_copy(fm_job_t *job, const fm_bench_t *bench, unsigned mount,
                     const char *src_dir, const char *name,
                     const char *dst_dir)
{
    if (job == NULL) {
        return FM_ERR_ARG;
    }
    begin(job, FM_JOB_COPY, mount);
    if (src_dir == NULL || name == NULL || dst_dir == NULL) {
        return refuse(job, FM_ERR_ARG);
    }
    fm_err_t err = fm_allowed(bench, FM_OP_COPY, src_dir, name, false);
    if (err == FM_OK && fm_same_path(src_dir, dst_dir)) {
        err = FM_ERR_SAME;
    }
    if (err == FM_OK) {
        err = fm_target_check(bench, dst_dir, name, false);
    }
    if (err == FM_OK) {
        err = join(job->src, src_dir, name, false);
    }
    if (err == FM_OK) {
        err = join(job->dst, dst_dir, name, false);
    }
    if (err == FM_OK) {
        err = tmp_path(job->tmp, dst_dir, name);
    }
    return (err != FM_OK) ? refuse(job, err) : run(job);
}

/**
 * @brief The checks MOVE and RENAME share on the target: not a LOCKED or
 *        FAULT name, and a path that fits.
 * @param job    the job, which takes the target's path
 * @param bench  the rules; may be NULL
 * @param dir    the target folder
 * @param name   the target name
 * @param is_dir  the entry is a folder
 * @return FM_OK or the refusal
 */
static fm_err_t target_path(fm_job_t *job, const fm_bench_t *bench,
                            const char *dir, const char *name, bool is_dir)
{
    /* The name stays as the card holds it, any length it lists: a move
     * writes no ".NEW", so fm_target_check()'s length is not its rule. */
    if (name[0] == '\0' || strchr(name, '/') != NULL
        || strcmp(name, ".") == 0 || strcmp(name, "..") == 0
        || strlen(name) > FM_LIST_NAME_MAX) {
        return FM_ERR_NAME;
    }
    switch (fm_place_of(bench, dir, name, false)) {
    case FM_PLACE_LOCKED:
        return FM_ERR_LOCKED;
    case FM_PLACE_FAULT:
        return FM_ERR_FIXED;
    case FM_PLACE_FIXED:
    case FM_PLACE_FREE:
    default:
        break;
    }
    return join(job->dst, dir, name, is_dir);
}

fm_err_t fm_job_move(fm_job_t *job, const fm_bench_t *bench, unsigned mount,
                     const char *src_dir, const char *name, bool is_dir,
                     const char *dst_dir)
{
    if (job == NULL) {
        return FM_ERR_ARG;
    }
    begin(job, FM_JOB_MOVE, mount);
    if (src_dir == NULL || name == NULL || dst_dir == NULL) {
        return refuse(job, FM_ERR_ARG);
    }
    fm_err_t err = fm_allowed(bench, FM_OP_MOVE, src_dir, name, is_dir);
    if (err == FM_OK) {
        err = join(job->src, src_dir, name, is_dir);
    }
    /* Into its own folder, or a folder into itself or below it. */
    if (err == FM_OK && (fm_same_path(src_dir, dst_dir)
                         || (is_dir && fm_path_within(job->src, dst_dir)))) {
        err = FM_ERR_SAME;
    }
    if (err == FM_OK) {
        err = target_path(job, bench, dst_dir, name, is_dir);
    }
    job->is_dir = is_dir;
    return (err != FM_OK) ? refuse(job, err) : run(job);
}

fm_err_t fm_job_rename(fm_job_t *job, const fm_bench_t *bench,
                       unsigned mount, const char *dir, const char *name,
                       bool is_dir, const char *new_name)
{
    if (job == NULL) {
        return FM_ERR_ARG;
    }
    begin(job, FM_JOB_RENAME, mount);
    if (dir == NULL || name == NULL || new_name == NULL
        || strcmp(name, new_name) == 0) {
        return refuse(job, FM_ERR_ARG);
    }
    job->is_dir = is_dir;
    fm_err_t err = fm_allowed(bench, FM_OP_RENAME, dir, name, is_dir);
    if (err == FM_OK) {
        err = fm_target_check(bench, dir, new_name, true);
    }
    if (err == FM_OK) {
        err = join(job->src, dir, name, is_dir);
    }
    if (err == FM_OK) {
        err = join(job->dst, dir, new_name, is_dir);
    }
    return (err != FM_OK) ? refuse(job, err) : run(job);
}

fm_err_t fm_job_delete(fm_job_t *job, const fm_bench_t *bench,
                       unsigned mount, const char *dir, const char *name,
                       bool is_dir)
{
    if (job == NULL) {
        return FM_ERR_ARG;
    }
    begin(job, FM_JOB_DELETE, mount);
    if (dir == NULL || name == NULL) {
        return refuse(job, FM_ERR_ARG);
    }
    fm_err_t err = fm_allowed(bench, FM_OP_DELETE, dir, name, is_dir);
    if (err == FM_OK) {
        err = join(job->src, dir, name, is_dir);
    }
    if (err == FM_OK && !is_dir) {
        /* A run's report goes with its log, as LOGS deletes it. */
        char report[FM_NAME_BUF];
        log_name_report_of(report, sizeof(report), name);
        if (report[0] != '\0'
            && join(job->tmp, dir, report, false) != FM_OK) {
            job->tmp[0] = '\0';
        }
    }
    job->is_dir = is_dir;
    return (err != FM_OK) ? refuse(job, err) : run(job);
}

/**
 * @brief The checks NEW's two jobs share.
 * @param job    the job, which takes the new path
 * @param bench  the gate and the rules; may be NULL
 * @param dir    the folder to make it in
 * @param name   the new name
 * @return FM_OK or the refusal, which the job holds
 */
static fm_err_t start_new(fm_job_t *job, const fm_bench_t *bench,
                          const char *dir, const char *name)
{
    if (dir == NULL || name == NULL) {
        return refuse(job, FM_ERR_ARG);
    }
    fm_err_t err = fm_gate(FM_OP_NEW, bench);
    if (err == FM_OK) {
        err = fm_target_check(bench, dir, name, true);
    }
    if (err == FM_OK) {
        err = join(job->dst, dir, name, job->kind == FM_JOB_MKDIR);
    }
    return (err != FM_OK) ? refuse(job, err) : run(job);
}

fm_err_t fm_job_mkdir(fm_job_t *job, const fm_bench_t *bench, unsigned mount,
                      const char *dir, const char *name)
{
    if (job == NULL) {
        return FM_ERR_ARG;
    }
    begin(job, FM_JOB_MKDIR, mount);
    return start_new(job, bench, dir, name);
}

fm_err_t fm_job_create(fm_job_t *job, const fm_bench_t *bench,
                       unsigned mount, const char *dir, const char *name)
{
    if (job == NULL) {
        return FM_ERR_ARG;
    }
    begin(job, FM_JOB_CREATE, mount);
    return start_new(job, bench, dir, name);
}

fm_err_t fm_job_save(fm_job_t *job, const fm_bench_t *bench, unsigned mount,
                     const char *dir, const char *name, uint64_t len,
                     fm_fill_fn fill, void *fill_ctx)
{
    if (job == NULL) {
        return FM_ERR_ARG;
    }
    begin(job, FM_JOB_SAVE, mount);
    if (dir == NULL || name == NULL || fill == NULL) {
        return refuse(job, FM_ERR_ARG);
    }
    fm_err_t err = fm_allowed(bench, FM_OP_SAVE, dir, name, false);
    if (err == FM_OK) {
        err = fm_target_check(bench, dir, name, false);
    }
    if (err == FM_OK) {
        err = join(job->dst, dir, name, false);
    }
    if (err == FM_OK) {
        err = tmp_path(job->tmp, dir, name);
    }
    job->total = len;
    job->fill = fill;
    job->fill_ctx = fill_ctx;
    job->note_restart = fm_read_at_start(dir, name);
    return (err != FM_OK) ? refuse(job, err) : run(job);
}

fm_err_t fm_job_load(fm_job_t *job, const fm_bench_t *bench, unsigned mount,
                     const char *dir, const char *name, uint8_t *buf,
                     size_t cap)
{
    if (job == NULL) {
        return FM_ERR_ARG;
    }
    begin(job, FM_JOB_LOAD, mount);
    if (dir == NULL || name == NULL || buf == NULL) {
        return refuse(job, FM_ERR_ARG);
    }
    fm_err_t err = fm_allowed(bench, FM_OP_OPEN, dir, name, false);
    if (err == FM_OK) {
        err = join(job->src, dir, name, false);
    }
    job->buf = buf;
    job->cap = cap;
    return (err != FM_OK) ? refuse(job, err) : run(job);
}

/* -------------------------------------------------------------- running */

/**
 * @brief End the job: close what is open and, when @p drop, remove the
 *        ".NEW" it wrote.
 * @param job    the job
 * @param io     the card, held
 * @param state  FM_JOB_DONE, FM_JOB_STOPPED or FM_JOB_FAILED
 * @param err    why, FM_OK for DONE
 * @param drop   remove job->tmp
 * @return @p state
 */
static fm_job_state_t finish(fm_job_t *job, const fm_io_t *io,
                             fm_job_state_t state, fm_err_t err, bool drop)
{
    if (job->out != NULL) {
        (void)io->close(io->ctx, job->out);
        job->out = NULL;
    }
    if (job->in != NULL) {
        (void)io->close(io->ctx, job->in);
        job->in = NULL;
    }
    if (drop && job->tmp[0] != '\0') {
        /* Best effort: on a card gone the ".NEW" stays on that card. */
        (void)io->unlink(io->ctx, job->tmp);
    }
    job->state = state;
    job->err = err;
    return state;
}

/**
 * @brief Whether @p path is free for a new entry.
 * @param io    the card, held
 * @param path  the path
 * @return FM_OK for no such name; FM_ERR_EXISTS; or the card's failure
 */
static fm_err_t free_name(const fm_io_t *io, const char *path)
{
    fm_stat_t st;
    const fm_err_t err = io->stat(io->ctx, path, &st);
    if (err == FM_OK) {
        return FM_ERR_EXISTS;
    }
    return (err == FM_ERR_NOT_FOUND) ? FM_OK : err;
}

/**
 * @brief Whether @p path is there as the kind the job expects.
 * @param io      the card, held
 * @param path    the path
 * @param is_dir  a folder is expected
 * @param st      takes the answer
 * @return FM_OK; FM_ERR_ARG for the other kind; the card's answer
 */
static fm_err_t there(const fm_io_t *io, const char *path, bool is_dir,
                      fm_stat_t *st)
{
    const fm_err_t err = io->stat(io->ctx, path, st);
    if (err != FM_OK) {
        return err;
    }
    return (st->is_dir == is_dir) ? FM_OK : FM_ERR_ARG;
}

/**
 * @brief Count one entry of a folder that has to be empty.
 * @param e     the entry
 * @param vctx  an unsigned count
 */
static void count_entry(const fm_dirent_t *e, void *vctx)
{
    (void)e;
    ++*(unsigned *)vctx;
}

/**
 * @brief Whether folder @p path holds nothing, those names included that
 *        the walk cannot list.
 * @param io    the card, held
 * @param path  the folder
 * @return FM_OK; FM_ERR_NOT_EMPTY; the card's failure
 */
static fm_err_t empty_dir(const fm_io_t *io, const char *path)
{
    unsigned n = 0u;
    unsigned unlisted = 0u;
    const fm_err_t err = io->walk(io->ctx, path, count_entry, &n, &unlisted);
    if (err != FM_OK) {
        return err;
    }
    return (n + unlisted == 0u) ? FM_OK : FM_ERR_NOT_EMPTY;
}

/**
 * @brief The single calls of MOVE, RENAME, DELETE, MKDIR and CREATE, with
 *        their checks.
 * @param job  the job
 * @param io   the card, held
 * @return FM_OK or why not
 */
static fm_err_t single(fm_job_t *job, const fm_io_t *io)
{
    fm_stat_t st;
    fm_err_t err = FM_OK;
    const bool is_dir = job->is_dir;
    switch (job->kind) {
    case FM_JOB_MOVE:
    case FM_JOB_RENAME:
        err = io->stat(io->ctx, job->src, &st);
        if (err != FM_OK) {
            return err;
        }
        /* The kind the rules were asked about: a file listed that is a
         * folder now, a fixed one perhaps, is not renamed or moved. */
        if (st.is_dir != is_dir) {
            return FM_ERR_ARG;
        }
        /* A change of case is the entry itself, not another one. */
        if (!fm_same_path(job->src, job->dst)) {
            err = free_name(io, job->dst);
        }
        if (err == FM_OK && job->kind == FM_JOB_MOVE && st.is_dir) {
            err = empty_dir(io, job->src);
        }
        return (err == FM_OK) ? io->rename(io->ctx, job->src, job->dst)
                              : err;
    case FM_JOB_DELETE:
        err = there(io, job->src, is_dir, &st);
        if (err == FM_OK && is_dir) {
            err = empty_dir(io, job->src);
            return (err == FM_OK) ? io->rmdir(io->ctx, job->src) : err;
        }
        if (err == FM_OK) {
            err = io->unlink(io->ctx, job->src);
        }
        fm_err_t report = FM_ERR_NOT_FOUND;
        if (err == FM_OK && job->tmp[0] != '\0') {
            report = there(io, job->tmp, false, &st);
            /* No report, or a folder of its name: nothing to take along.
             * A card that does not answer is the answer. */
            if (report != FM_OK && report != FM_ERR_NOT_FOUND
                && report != FM_ERR_ARG) {
                return report;
            }
        }
        if (report == FM_OK) {
            /* Left behind, it would be the only file of its number.  A
             * report that stays is the answer, with its run gone: the
             * caller reads the folder again. */
            err = io->unlink(io->ctx, job->tmp);
        }
        return err;
    case FM_JOB_MKDIR:
        err = free_name(io, job->dst);
        return (err == FM_OK) ? io->mkdir(io->ctx, job->dst) : err;
    case FM_JOB_CREATE: {
        err = free_name(io, job->dst);
        void *f = NULL;
        if (err == FM_OK) {
            err = io->open(io->ctx, job->dst, FM_OPEN_NEW, &f);
        }
        if (err == FM_OK) {
            err = io->close(io->ctx, f);
            if (err != FM_OK) {
                (void)io->unlink(io->ctx, job->dst);
            }
        }
        return err;
    }
    case FM_JOB_NONE:
    case FM_JOB_COPY:
    case FM_JOB_SAVE:
    case FM_JOB_LOAD:
    default:
        return FM_ERR_ARG;
    }
}

/**
 * @brief The first step of a job with data: the names checked and the
 *        files opened.
 * @param job  the job
 * @param io   the card, held
 * @return FM_OK or why not
 */
static fm_err_t open_files(fm_job_t *job, const fm_io_t *io)
{
    fm_stat_t st;
    fm_err_t err = FM_OK;
    if (job->kind != FM_JOB_SAVE) {
        err = there(io, job->src, false, &st);
        if (err != FM_OK) {
            return err;
        }
        job->total = st.size;
        if (job->kind == FM_JOB_LOAD && st.size > (uint64_t)job->cap) {
            return FM_ERR_TOO_BIG;
        }
    }
    if (job->kind == FM_JOB_COPY) {
        err = free_name(io, job->dst);
    } else if (job->kind == FM_JOB_SAVE) {
        /* The file may be new, never a folder. */
        err = io->stat(io->ctx, job->dst, &st);
        job->dst_was = (err == FM_OK);
        if (err == FM_OK && st.is_dir) {
            err = FM_ERR_ARG;
        } else if (err == FM_ERR_NOT_FOUND) {
            err = FM_OK;
        }
    }
    /* A ".NEW" there already can be the only whole copy of a save cut
     * short: never written over. */
    if (err == FM_OK && job->kind != FM_JOB_LOAD) {
        err = free_name(io, job->tmp);
    }
    if (err == FM_OK && job->kind != FM_JOB_SAVE) {
        err = io->open(io->ctx, job->src, FM_OPEN_READ, &job->in);
    }
    if (err == FM_OK && job->kind != FM_JOB_LOAD) {
        err = io->open(io->ctx, job->tmp, FM_OPEN_NEW, &job->out);
        if (err != FM_OK) {
            job->out = NULL;
        }
    }
    return err;
}

/**
 * @brief One chunk of a copy, a save or a load.
 * @param job    the job
 * @param io     the card, held
 * @param chunk  the chunk's room
 * @param size   its bytes
 * @return FM_OK, with job->step moved on at the data's end; or why not
 */
static fm_err_t one_chunk(fm_job_t *job, const fm_io_t *io, uint8_t *chunk,
                          size_t size)
{
    size_t got = 0u;
    if (job->kind == FM_JOB_SAVE) {
        if (job->done >= job->total) {
            job->step = STEP_SYNC;
            return FM_OK;
        }
        const uint64_t left = job->total - job->done;
        const size_t want = (left < (uint64_t)size) ? (size_t)left : size;
        got = job->fill(job->fill_ctx, job->done, chunk, want);
        if (got == 0u || got > want) {
            return FM_ERR_ARG;          /* the text ended short */
        }
    } else {
        const fm_err_t err = io->read(io->ctx, job->in, chunk, size, &got);
        if (err != FM_OK) {
            return err;
        }
        if (got == 0u) {
            job->step = STEP_SYNC;
            return FM_OK;
        }
    }
    if (job->kind == FM_JOB_LOAD) {
        if (got > job->cap || job->done > (uint64_t)(job->cap - got)) {
            return FM_ERR_TOO_BIG;      /* the file grew since its stat */
        }
        memcpy(job->buf + job->done, chunk, got);
    } else {
        const fm_err_t err = io->write(io->ctx, job->out, chunk, got);
        if (err != FM_OK) {
            return err;
        }
    }
    job->done += got;
    return FM_OK;
}

/**
 * @brief The operation the rules are asked about for a job's kind.
 * @param kind  the job's kind
 * @return its fm_op_t
 */
static fm_op_t op_of(fm_job_kind_t kind)
{
    switch (kind) {
    case FM_JOB_COPY:
        return FM_OP_COPY;
    case FM_JOB_MOVE:
        return FM_OP_MOVE;
    case FM_JOB_RENAME:
        return FM_OP_RENAME;
    case FM_JOB_DELETE:
        return FM_OP_DELETE;
    case FM_JOB_MKDIR:
    case FM_JOB_CREATE:
        return FM_OP_NEW;
    case FM_JOB_SAVE:
        return FM_OP_SAVE;
    case FM_JOB_LOAD:
    case FM_JOB_NONE:
    default:
        return FM_OP_OPEN;
    }
}

/**
 * @brief Whether the rules let the job go on as the bench stands now:
 *        fm_allowed() of the source of a copy, a load, a move, a rename or
 *        a delete, of the file a save writes, or of the folder NEW makes
 *        something in.
 * @param job    the job
 * @param bench  the bench now
 * @return FM_OK or the refusal
 */
static fm_err_t still_allowed(const fm_job_t *job, const fm_bench_t *bench)
{
    const fm_op_t op = op_of(job->kind);
    const bool in_dst = (job->kind == FM_JOB_SAVE || op == FM_OP_NEW);
    const char *path = in_dst ? job->dst : job->src;
    char dir[FM_PATH_SIZE];
    const char *cut = strrchr(path, '/');
    const size_t k = (cut != NULL) ? (size_t)(cut - path) : 0u;
    memcpy(dir, path, k);
    dir[k] = '\0';
    if (op == FM_OP_NEW) {
        /* NEW asks about the folder it makes something in: the gate. */
        return fm_allowed(bench, op, dir, "", true);
    }
    return fm_allowed(bench, op, dir, (cut != NULL) ? cut + 1 : path,
                      job->is_dir);
}

/**
 * @brief One step of a copy, a save or a load.
 * @param job    the job
 * @param io     the card, held
 * @param bench  the gate as it stands now
 * @param chunk  the chunk's room
 * @param size   its bytes
 * @return the job's state after the step
 */
static fm_job_state_t data_step(fm_job_t *job, const fm_io_t *io,
                                const fm_bench_t *bench, uint8_t *chunk,
                                size_t size)
{
    const bool writes = (job->kind != FM_JOB_LOAD);
    fm_err_t err = FM_OK;
    switch (job->step) {
    case STEP_CHECK:
    case STEP_DATA: {
        if (job->cancel) {
            return finish(job, io, FM_JOB_STOPPED, FM_ERR_CANCELLED,
                          job->step == STEP_DATA && writes);
        }
        /* The gate, and what the source's place allows now: the fault log
         * is copied only while the link is up, and the link can drop
         * between two chunks. */
        const fm_err_t gate = still_allowed(job, bench);
        if (gate != FM_OK) {
            return finish(job, io, FM_JOB_STOPPED, gate,
                          job->step == STEP_DATA && writes);
        }
        if (job->step == STEP_CHECK) {
            err = open_files(job, io);
            if (err != FM_OK) {
                /* A ".NEW" this job did not open is not its own. */
                return finish(job, io, FM_JOB_FAILED, err,
                              job->out != NULL);
            }
            job->step = STEP_DATA;
            return FM_JOB_RUNNING;
        }
        if (chunk == NULL || size == 0u) {
            return finish(job, io, FM_JOB_FAILED, FM_ERR_ARG, writes);
        }
        err = one_chunk(job, io, chunk, size);
        if (err != FM_OK) {
            return finish(job, io, FM_JOB_FAILED, err, writes);
        }
        if (job->step == STEP_SYNC && !writes) {
            return finish(job, io, FM_JOB_DONE, FM_OK, false);
        }
        return FM_JOB_RUNNING;
    }
    case STEP_SYNC:
        err = io->sync(io->ctx, job->out);
        {
            const fm_err_t closed = io->close(io->ctx, job->out);
            job->out = NULL;
            if (err == FM_OK) {
                err = closed;
            }
        }
        if (err != FM_OK) {
            return finish(job, io, FM_JOB_FAILED, err, true);
        }
        if (job->in != NULL) {
            (void)io->close(io->ctx, job->in);
            job->in = NULL;
        }
        job->step = (job->kind == FM_JOB_SAVE) ? STEP_UNLINK : STEP_RENAME;
        return FM_JOB_RUNNING;
    case STEP_UNLINK:
        if (!job->dst_was) {
            /* Not there at the open: whatever stands there now is another
             * writer's, never deleted.  The ".NEW" holds the text and
             * stays listed for a RENAME. */
            err = free_name(io, job->dst);
            if (err != FM_OK) {
                return finish(job, io, FM_JOB_FAILED, err, false);
            }
            job->step = STEP_RENAME;
            return FM_JOB_RUNNING;
        }
        err = io->unlink(io->ctx, job->dst);
        if (err != FM_OK && err != FM_ERR_NOT_FOUND) {
            /* The file stands as it was; the ".NEW" is not needed. */
            return finish(job, io, FM_JOB_FAILED, err, true);
        }
        job->step = STEP_RENAME;
        return FM_JOB_RUNNING;
    case STEP_RENAME:
    default:
        err = io->rename(io->ctx, job->tmp, job->dst);
        if (err != FM_OK) {
            /* A save's ".NEW" is the only copy of the text now; a copy's
             * source is still there. */
            return finish(job, io, FM_JOB_FAILED, err,
                          job->kind == FM_JOB_COPY);
        }
        return finish(job, io, FM_JOB_DONE, FM_OK, false);
    }
}

fm_job_state_t fm_job_step(fm_job_t *job, const fm_io_t *io,
                           const fm_bench_t *bench, uint8_t *chunk,
                           size_t size)
{
    if (job == NULL) {
        return FM_JOB_IDLE;
    }
    if (job->state != FM_JOB_RUNNING) {
        return job->state;
    }
    if (io == NULL) {
        job->state = FM_JOB_FAILED;
        job->err = FM_ERR_ARG;
        return job->state;
    }
    if (!io->hold(io->ctx, job->mount)) {
        /* Another card or none: nothing of this job is written to it.
         * The handles are let go; a ".NEW" stays on the card it is on. */
        const fm_err_t why = io->mounted(io->ctx) ? FM_ERR_CARD_GONE
                                                  : FM_ERR_NO_CARD;
        return finish(job, io, FM_JOB_FAILED, why, false);
    }
    fm_job_state_t state;
    if (job->kind == FM_JOB_COPY || job->kind == FM_JOB_SAVE
        || job->kind == FM_JOB_LOAD) {
        state = data_step(job, io, bench, chunk, size);
    } else if (job->cancel) {
        state = finish(job, io, FM_JOB_STOPPED, FM_ERR_CANCELLED, false);
    } else {
        /* Queued while SAFE, run now: the gate, the place, the link and
         * the logger's files are asked again before the one call, and a
         * refusal changes nothing on the card. */
        const fm_err_t allowed = still_allowed(job, bench);
        const fm_err_t err = (allowed != FM_OK) ? allowed : single(job, io);
        state = finish(job, io, (err == FM_OK) ? FM_JOB_DONE : FM_JOB_FAILED,
                       err, false);
    }
    io->release(io->ctx);
    return state;
}

void fm_job_cancel(fm_job_t *job)
{
    if (job != NULL) {
        job->cancel = true;
    }
}

fm_job_state_t fm_job_state(const fm_job_t *job, uint64_t *done,
                            uint64_t *total)
{
    if (done != NULL) {
        *done = (job != NULL) ? job->done : 0u;
    }
    if (total != NULL) {
        *total = (job != NULL) ? job->total : 0u;
    }
    return (job != NULL) ? job->state : FM_JOB_IDLE;
}
