/**
 * @file fm_ops.h
 * @brief The file manager's jobs on the card: copy, move, rename, delete,
 *        make a folder, make an empty file, save a text and load a file,
 *        one step at a time.
 *
 * A job is started by one of the fm_job_*() calls, which check what can be
 * checked without the card: the arguments, the write gate and the places
 * (fm_rules.h), and the names a path and a ".NEW" need.  Nothing is read
 * or written until fm_job_step(), which a task of its own calls until the
 * job is no longer FM_JOB_RUNNING: one step is one call of the card, or a
 * few short ones, or one chunk of a copy, a save or a load.  The screen
 * reads fm_job_state() for the progress box.
 *
 * Every overwrite is refused: FAT (File Allocation Table) renames over no
 * name, and no job of the file manager replaces a file it did not write.
 * A target name that is taken is FM_ERR_EXISTS, and so is a ".NEW" of the
 * target that is there already: it can be the only whole copy of a save
 * that was cut short.
 *
 * COPY writes "<name>.NEW" in the target folder in chunks, puts it on the
 * card (sync) and gives it its name.  A copy stopped part way, by CANCEL,
 * by the gate or by a failure, closes both files and removes its ".NEW";
 * the source is never written.
 *
 * SAVE is the servo programmer's journal pattern: write "<name>.NEW", sync,
 * close, delete "<name>", rename "<name>.NEW" to "<name>".  A "<name>" that
 * was not there when the save opened its ".NEW" is never deleted: one that
 * is there by the rename was made by someone else, and the save fails with
 * FM_ERR_EXISTS, the ".NEW" kept and listed.  Between the
 * delete and the rename only "<name>.NEW" holds the text: a card pulled or
 * a power cut there leaves "<name>.NEW" whole and "<name>" gone, and the
 * next listing shows "<name>.NEW" for a RENAME.  A rename that fails there
 * keeps "<name>.NEW" for the same reason.  A failure before the delete
 * removes "<name>.NEW" and leaves "<name>" as it was.
 *
 * The rules (fm_allowed(): the write gate, the place, the IOmcu link and
 * the logger's files) are asked again as the job runs, with the bench as
 * fm_job_step() is handed it.  A MOVE, RENAME, DELETE or NEW asks them
 * once more right before its single call: queued while SAFE and run once
 * the bench is ARMED or a log is open, it fails (FM_JOB_FAILED) with the
 * refusal and changes nothing.  A copy, a save or a load asks before its
 * files are opened and before every chunk: a bench switched to ARMED, a
 * log run opened, or the IOmcu link gone while the fault log is copied
 * stops it there (FM_JOB_STOPPED: FM_ERR_ARMED, FM_ERR_LOGGING,
 * FM_ERR_BUSY), as CANCEL does (FM_ERR_CANCELLED).  A single call that has
 * started, and the end of a copy or a save (sync, close, rename), finish.  MOVE, RENAME and DELETE find the entry as the kind
 * it was listed as, or refuse with FM_ERR_ARG: a file listed that is a
 * folder now was never asked about as a folder.
 *
 * A job runs on the mount its names were listed from (fm_io.h hold()): on
 * another card it fails with FM_ERR_CARD_GONE and writes nothing.  A card
 * pulled part way fails the job with FM_ERR_CARD_GONE; a ".NEW" written so
 * far stays on that card.
 *
 * A SAVE can be told what it replaces (fm_job_match()).  FM_MATCH_SUM
 * reads the file there before its ".NEW" is opened, a chunk a step, and
 * sums its bytes: a file that is gone, or whose sum is not the one given,
 * fails the job with FM_ERR_CHANGED and nothing written.  FM_MATCH_ABSENT
 * fails it with FM_ERR_EXISTS when the name is taken.  The check and the
 * write run in one job, so between them only a writer outside the file
 * manager's job (fm_req.h) can change the file.  A LOAD and a SAVE can sum
 * the bytes they read or write as they pass (fm_job_sum()): FILES' editor
 * keeps the sum of the file it opened and of the text it saved last, and
 * hands it to the next SAVE as FM_MATCH_SUM, so a write of the file from
 * the Wi-Fi page in between fails that SAVE with FM_ERR_CHANGED.
 *
 * A SAVE can stop after its checks (fm_job_check_only()): the rules, the
 * target, the match and a ".NEW" left there are asked, nothing is opened
 * for writing, and the job ends FM_JOB_DONE.  The Wi-Fi page asks so before
 * it takes a body, and the SAVE of that body asks everything again.
 *
 * DELETE of a run's log takes its report with it, as LOGS' DELETE does:
 * ATEST042.CSV takes ATEST042.TXT (log_name_report_of()).  A report that
 * cannot be deleted fails the job with the card's answer, the log gone
 * and the report there; the caller reads the folder again.  A folder is
 * moved or deleted only when it is empty.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fm_io.h"
#include "fm_rules.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The chunk the panel copies, saves and loads in: 16KiB. */
#define FM_CHUNK 16384u

/**
 * @brief What a job does.
 */
typedef enum {
    FM_JOB_NONE = 0,
    FM_JOB_COPY,
    FM_JOB_MOVE,
    FM_JOB_RENAME,
    FM_JOB_DELETE,
    FM_JOB_MKDIR,
    FM_JOB_CREATE,
    FM_JOB_SAVE,
    FM_JOB_LOAD
} fm_job_kind_t;

/**
 * @brief Where a job stands.
 */
typedef enum {
    FM_JOB_IDLE = 0,   /**< no job started                              */
    FM_JOB_RUNNING,    /**< fm_job_step() has more to do                */
    FM_JOB_DONE,       /**< finished                                    */
    FM_JOB_STOPPED,    /**< stopped by CANCEL or the gate: err says which */
    FM_JOB_FAILED      /**< refused or failed: err says why             */
} fm_job_state_t;

/**
 * @brief What a SAVE checks of the file it replaces (fm_job_match()).
 */
typedef enum {
    FM_MATCH_ANY = 0,   /**< nothing: a file there is replaced          */
    FM_MATCH_SUM,       /**< the file is there and sums to a given value */
    FM_MATCH_ABSENT     /**< no entry has the name                      */
} fm_match_t;

/**
 * @brief A running sum over bytes, CRC-32 (cyclic redundancy check) on the
 *        panel: the sum of @p n more bytes after @p sum, 0 at the start.
 * @param sum   the sum so far; 0 before the first byte
 * @param data  the bytes
 * @param n     how many
 * @return the sum with them
 */
typedef uint32_t (*fm_sum_fn)(uint32_t sum, const void *data, size_t n);

/**
 * @brief The bytes a save writes, asked for one chunk at a time.
 * @param ctx  the caller's context
 * @param at   the offset of the first byte wanted
 * @param dst  takes the bytes
 * @param cap  room in @p dst
 * @return the bytes written into @p dst, 0 only past the text's end
 */
typedef size_t (*fm_fill_fn)(void *ctx, uint64_t at, uint8_t *dst,
                             size_t cap);

/**
 * @brief One job.  The fields are the job's; the screen reads them through
 *        fm_job_state().
 */
typedef struct {
    fm_job_kind_t kind;      /**< what it does                          */
    fm_job_state_t state;    /**< where it stands                       */
    fm_err_t err;            /**< why it stopped or failed              */
    int step;                /**< the step to run next (fm_ops.c)       */
    unsigned mount;          /**< the mount its names are from          */
    bool cancel;             /**< CANCEL was pressed                    */
    bool is_dir;             /**< MOVE, RENAME, DELETE: the entry is a
                                  folder                                 */
    bool dst_was;            /**< SAVE: the file was there at the open  */
    bool note_restart;       /**< a saved ESC profile: read at the next
                                  start (fm_read_at_start())             */
    char src[FM_PATH_SIZE];  /**< the source's path                     */
    char dst[FM_PATH_SIZE];  /**< the target's path                     */
    char tmp[FM_PATH_SIZE];  /**< the target's ".NEW", or the report a
                                  DELETE takes along                     */
    void *in;                /**< the open source, or NULL              */
    void *out;               /**< the open ".NEW", or NULL              */
    uint64_t done;           /**< bytes copied, saved or loaded         */
    uint64_t total;          /**< bytes to do; 0 for single calls       */
    fm_fill_fn fill;         /**< a save's bytes                        */
    void *fill_ctx;          /**< passed to @p fill                     */
    uint8_t *buf;            /**< a load's room                         */
    size_t cap;              /**< bytes @p buf takes                    */
    fm_match_t match;        /**< SAVE: what it checks of the old file  */
    fm_sum_fn sum;           /**< FM_MATCH_SUM: the sum                 */
    uint32_t want;           /**< FM_MATCH_SUM: the old file's sum      */
    uint32_t got;            /**< FM_MATCH_SUM: the sum read so far     */
    bool found;              /**< FM_MATCH_SUM: refused with got the sum
                                  of the whole file there                */
    bool check_only;         /**< fm_job_check_only(): ends after the
                                  checks, nothing written                */
    fm_sum_fn data_fn;       /**< fm_job_sum(): the sum, or NULL        */
    uint32_t data_sum;       /**< fm_job_sum(): the sum of the bytes
                                  loaded or saved so far                 */
} fm_job_t;

/**
 * @brief Copy file @p name of folder @p src_dir into folder @p dst_dir
 *        under the same name.
 * @param job      the job
 * @param bench    the gate and the rules; may be NULL
 * @param mount    the mount the names were listed from
 * @param src_dir  the source's folder
 * @param name     the file's name
 * @param dst_dir  the target folder
 * @return FM_OK when the job runs; else the refusal, which the job holds
 *         as FM_JOB_FAILED: FM_ERR_ARG, FM_ERR_ARMED, FM_ERR_LOGGING,
 *         FM_ERR_BUSY, FM_ERR_LOCKED, FM_ERR_FIXED, FM_ERR_SAME for one
 *         folder, FM_ERR_NAME
 */
fm_err_t fm_job_copy(fm_job_t *job, const fm_bench_t *bench, unsigned mount,
                     const char *src_dir, const char *name,
                     const char *dst_dir);

/**
 * @brief Move file or empty folder @p name of @p src_dir into @p dst_dir.
 * @param job      the job
 * @param bench    the gate and the rules; may be NULL
 * @param mount    the mount the names were listed from
 * @param src_dir  the source's folder
 * @param name     the entry's name
 * @param is_dir   the entry is a folder
 * @param dst_dir  the target folder
 * @return FM_OK, or the refusal as fm_job_copy(); FM_ERR_SAME also for a
 *         folder moved into itself
 */
fm_err_t fm_job_move(fm_job_t *job, const fm_bench_t *bench, unsigned mount,
                     const char *src_dir, const char *name, bool is_dir,
                     const char *dst_dir);

/**
 * @brief Give entry @p name of folder @p dir the name @p new_name.
 *
 * A change of case alone is a rename of one entry, not an overwrite.
 *
 * @param job       the job
 * @param bench     the gate and the rules; may be NULL
 * @param mount     the mount the names were listed from
 * @param dir       the folder
 * @param name      the entry's name now
 * @param is_dir    the entry is a folder
 * @param new_name  its new name
 * @return FM_OK, or the refusal; FM_ERR_ARG for the same name
 */
fm_err_t fm_job_rename(fm_job_t *job, const fm_bench_t *bench,
                       unsigned mount, const char *dir, const char *name,
                       bool is_dir, const char *new_name);

/**
 * @brief Delete file or empty folder @p name of folder @p dir.
 * @param job     the job
 * @param bench   the gate and the rules; may be NULL
 * @param mount   the mount the names were listed from
 * @param dir     the folder
 * @param name    the entry's name
 * @param is_dir  the entry is a folder
 * @return FM_OK, or the refusal
 */
fm_err_t fm_job_delete(fm_job_t *job, const fm_bench_t *bench,
                       unsigned mount, const char *dir, const char *name,
                       bool is_dir);

/**
 * @brief Make folder @p name in folder @p dir.
 * @param job    the job
 * @param bench  the gate and the rules; may be NULL
 * @param mount  the mount the folder was listed from
 * @param dir    the folder to make it in
 * @param name   the new folder's name
 * @return FM_OK, or the refusal
 */
fm_err_t fm_job_mkdir(fm_job_t *job, const fm_bench_t *bench, unsigned mount,
                      const char *dir, const char *name);

/**
 * @brief Make the empty file @p name in folder @p dir.
 * @param job    the job
 * @param bench  the gate and the rules; may be NULL
 * @param mount  the mount the folder was listed from
 * @param dir    the folder to make it in
 * @param name   the new file's name
 * @return FM_OK, or the refusal
 */
fm_err_t fm_job_create(fm_job_t *job, const fm_bench_t *bench,
                       unsigned mount, const char *dir, const char *name);

/**
 * @brief Write @p len bytes from @p fill as file @p name of folder @p dir,
 *        through "<name>.NEW" (the file's comment).
 * @param job       the job
 * @param bench     the gate and the rules; may be NULL
 * @param mount     the mount the file was read from
 * @param dir       the file's folder
 * @param name      the file's name; it need not be there yet
 * @param len       the text's bytes
 * @param fill      hands over the bytes
 * @param fill_ctx  passed to @p fill
 * @return FM_OK, or the refusal
 */
fm_err_t fm_job_save(fm_job_t *job, const fm_bench_t *bench, unsigned mount,
                     const char *dir, const char *name, uint64_t len,
                     fm_fill_fn fill, void *fill_ctx);

/**
 * @brief What the SAVE fm_job_save() started checks of the file it
 *        replaces, before it writes (the file's comment).
 * @param job    a SAVE that fm_job_save() started and no step has run
 * @param match  the check
 * @param sum    FM_MATCH_SUM: the sum; NULL otherwise
 * @param want   FM_MATCH_SUM: the sum the file has to have
 * @return FM_OK; FM_ERR_ARG for another job, one that runs already or
 *         was refused, or FM_MATCH_SUM without @p sum, the job unchanged
 */
fm_err_t fm_job_match(fm_job_t *job, fm_match_t match, fm_sum_fn sum,
                      uint32_t want);

/**
 * @brief End the SAVE fm_job_save() started after its checks: it answers as
 *        it would up to the first byte of its ".NEW", which it never opens.
 * @param job  a SAVE that fm_job_save() started and no step has run
 * @return FM_OK; FM_ERR_ARG for another job, one that runs already or was
 *         refused, the job unchanged
 */
fm_err_t fm_job_check_only(fm_job_t *job);

/**
 * @brief The sum of the file a SAVE refused with FM_ERR_CHANGED read there:
 *        what a SAVE that replaces that file as it stands now hands
 *        fm_job_match(), so one more change is refused in its turn.
 * @param job  the job
 * @param sum  takes the sum
 * @return true when the job read the whole file and its sum was not the
 *         one wanted; false for a file that is gone, and for any other job
 *         or answer, @p sum unchanged
 */
bool fm_job_found(const fm_job_t *job, uint32_t *sum);

/**
 * @brief Sum the bytes a LOAD reads or a SAVE writes as they pass, into
 *        the job's data_sum: once the job is FM_JOB_DONE it is the sum of
 *        the file as the card holds it, which a later SAVE of the same
 *        file hands fm_job_match().
 * @param job  a LOAD or a SAVE that its fm_job_*() call started and no
 *             step has run
 * @param sum  the sum
 * @return FM_OK; FM_ERR_ARG for another job, one that runs already or was
 *         refused, or no @p sum, the job unchanged
 */
fm_err_t fm_job_sum(fm_job_t *job, fm_sum_fn sum);

/**
 * @brief Read file @p name of folder @p dir into @p buf.  A read: the gate
 *        does not stop it.
 * @param job    the job
 * @param bench  the rules; may be NULL
 * @param mount  the mount the file was listed from
 * @param dir    the file's folder
 * @param name   the file's name
 * @param buf    takes the bytes
 * @param cap    room in @p buf; a larger file is FM_ERR_TOO_BIG
 * @return FM_OK, or the refusal
 */
fm_err_t fm_job_load(fm_job_t *job, const fm_bench_t *bench, unsigned mount,
                     const char *dir, const char *name, uint8_t *buf,
                     size_t cap);

/**
 * @brief Read file @p name of folder @p dir for its sum alone, a chunk a
 *        step: as fm_job_load() with no room for the bytes.  At
 *        FM_JOB_DONE the job's data_sum is the sum of the file.
 * @param job    the job
 * @param bench  the rules; may be NULL
 * @param mount  the mount the file was listed from
 * @param dir    the file's folder
 * @param name   the file's name
 * @param cap    bytes it reads at most; a larger file is FM_ERR_TOO_BIG
 * @param sum    the sum
 * @return FM_OK, or the refusal
 */
fm_err_t fm_job_sum_file(fm_job_t *job, const fm_bench_t *bench,
                         unsigned mount, const char *dir, const char *name,
                         size_t cap, fm_sum_fn sum);

/**
 * @brief Run the job's next step.
 * @param job    the job
 * @param io     the card
 * @param bench  the gate as it stands now; may be NULL
 * @param chunk  room for one chunk of a copy, a save or a load
 * @param size   bytes @p chunk takes; FM_CHUNK on the panel
 * @return the job's state after the step
 */
fm_job_state_t fm_job_step(fm_job_t *job, const fm_io_t *io,
                           const fm_bench_t *bench, uint8_t *chunk,
                           size_t size);

/**
 * @brief Ask the job to stop at its next chunk.  A single call that has
 *        started finishes.
 * @param job  the job
 */
void fm_job_cancel(fm_job_t *job);

/**
 * @brief Where the job stands.
 * @param job    the job; NULL is FM_JOB_IDLE
 * @param done   takes the bytes done; may be NULL
 * @param total  takes the bytes to do; may be NULL
 * @return its state
 */
fm_job_state_t fm_job_state(const fm_job_t *job, uint64_t *done,
                            uint64_t *total);

#ifdef __cplusplus
}
#endif
