/**
 * @file fm_rules.c
 * @brief The places the bench keeps, the write gate and the kinds of
 *        entries (fm_rules.h).
 *
 * SPDX-License-Identifier: MIT
 */

#include "fm_rules.h"

#include <ctype.h>
#include <string.h>

#include "card_tree.h"
#include "kst_store.h"
#include "log_name.h"

/** @brief The journal's name, KST.TXT, as the panel writes it. */
#define JOURNAL_TXT KST_STORE_RECORD ".TXT"
/** @brief The journal's name while it is written, KST.NEW. */
#define JOURNAL_NEW KST_STORE_RECORD FM_NEW_SUFFIX

/**
 * @brief The bench's deepest folders: each of them and every folder above
 *        it is FIXED (card_tree.h).
 */
static const char *const k_bench_dirs[] = {
    CARD_AUTO_LOGS, CARD_USER_LOGS, CARD_ESC_CONF, CARD_KST_CONF,
    CARD_HITEC_CONF,
};

/** @brief The fault log's three names (fault_log.h). */
static const char *const k_fault[] = {
    CARD_FAULT_LOG, CARD_FAULT_OLD, CARD_FAULT_TMP,
};

/**
 * @brief Whether two strings are one with case folded, ASCII only.
 * @param a  a string
 * @param b  a string
 * @return true when they are one
 */
static bool same_folded(const char *a, const char *b)
{
    return log_name_case_cmp(a, b) == 0;
}

/**
 * @brief Whether @p name ends in @p suffix, case folded, after at least one
 *        character.
 * @param name    a name
 * @param suffix  ".csv", ".txt" or ".json"
 * @return true for "RUN.CSV" and "run.csv"; false for ".csv" alone
 */
static bool ends_in(const char *name, const char *suffix)
{
    const size_t n = strlen(name);
    const size_t k = strlen(suffix);
    return n > k && same_folded(name + n - k, suffix);
}

bool fm_op_writes(fm_op_t op)
{
    switch (op) {
    case FM_OP_COPY:
    case FM_OP_MOVE:
    case FM_OP_RENAME:
    case FM_OP_DELETE:
    case FM_OP_NEW:
    case FM_OP_SAVE:
        return true;
    case FM_OP_OPEN:
    case FM_OP_INFO:
    case FM_OP_EDIT:
    case FM_OP_COUNT:
    default:
        return false;
    }
}

bool fm_logging(const log_marks_t *marks)
{
    return marks != NULL
           && (marks->run != 0u || marks->test != 0u || marks->txt != 0u);
}

fm_err_t fm_gate(fm_op_t op, const fm_bench_t *bench)
{
    if (!fm_op_writes(op) || bench == NULL) {
        return FM_OK;
    }
    if (bench->armed) {
        return FM_ERR_ARMED;
    }
    if (fm_logging(bench->marks)) {
        return FM_ERR_LOGGING;
    }
    return FM_OK;
}

fm_err_t fm_path_err(fm_path_t r)
{
    switch (r) {
    case FM_PATH_OK:
        return FM_OK;
    case FM_PATH_ARG:
        return FM_ERR_ARG;
    case FM_PATH_BAD:
    case FM_PATH_LONG:
    case FM_PATH_DEEP:
    default:
        return FM_ERR_NAME;
    }
}

bool fm_same_path(const char *a, const char *b)
{
    return a != NULL && b != NULL && same_folded(a, b);
}

bool fm_path_within(const char *outer, const char *inner)
{
    if (outer == NULL || inner == NULL) {
        return false;
    }
    const size_t n = strlen(outer);
    if (n == 0u) {
        return true;                 /* everything is in the root */
    }
    if (strlen(inner) < n) {
        return false;
    }
    for (size_t i = 0u; i < n; ++i) {
        if (tolower((unsigned char)outer[i])
            != tolower((unsigned char)inner[i])) {
            return false;
        }
    }
    return inner[n] == '\0' || inner[n] == '/';
}

/**
 * @brief Whether folder @p path is one of the bench's or a log folder, or
 *        stands above one.
 * @param bench  the log folders in force; may be NULL
 * @param path   the folder's path
 * @return true for a FIXED folder
 */
static bool fixed_dir(const fm_bench_t *bench, const char *path)
{
    if (path[0] == '\0') {
        return false;
    }
    for (size_t i = 0u; i < sizeof(k_bench_dirs) / sizeof(k_bench_dirs[0]);
         ++i) {
        if (fm_path_within(path, k_bench_dirs[i])) {
            return true;
        }
    }
    /* The folders the logger writes to, as it reads the settings: a stored
     * folder card_folder_check() refuses is written as the default. */
    char user[CARD_DIR_MAX];
    char autos[CARD_DIR_MAX];
    (void)card_folder_use((bench != NULL) ? bench->user_logs : NULL,
                          CARD_USER_LOGS, user, sizeof(user));
    (void)card_folder_use((bench != NULL) ? bench->auto_logs : NULL,
                          CARD_AUTO_LOGS, autos, sizeof(autos));
    return fm_path_within(path, user) || fm_path_within(path, autos);
}

fm_place_t fm_place_of(const fm_bench_t *bench, const char *dir,
                       const char *name, bool is_dir)
{
    char path[FM_PATH_SIZE];
    if (name == NULL || name[0] == '\0'
        || fm_path_join(dir, name, is_dir, path, sizeof(path))
               != FM_PATH_OK) {
        return FM_PLACE_FREE;
    }
    if (is_dir) {
        return fixed_dir(bench, path) ? FM_PLACE_FIXED : FM_PLACE_FREE;
    }
    if (fm_same_path(dir, CARD_KST_CONF)
        && (same_folded(name, JOURNAL_TXT) || same_folded(name, JOURNAL_NEW))) {
        return FM_PLACE_LOCKED;
    }
    for (size_t i = 0u; i < sizeof(k_fault) / sizeof(k_fault[0]); ++i) {
        if (fm_same_path(path, k_fault[i])) {
            return FM_PLACE_FAULT;
        }
    }
    return FM_PLACE_FREE;
}

bool fm_busy(const fm_bench_t *bench, const char *dir, const char *name)
{
    if (bench == NULL || bench->marks == NULL || dir == NULL
        || name == NULL) {
        return false;
    }
    return log_marks_busy(bench->marks, dir, name);
}

/**
 * @brief What the fault log's place lets @p op do.
 * @param bench  the bench; may be NULL
 * @param op     the operation
 * @return FM_OK, FM_ERR_BUSY while the link is down, or FM_ERR_FIXED
 */
static fm_err_t fault_allows(const fm_bench_t *bench, fm_op_t op)
{
    switch (op) {
    case FM_OP_OPEN:
    case FM_OP_INFO:
        return FM_OK;
    case FM_OP_COPY:
    case FM_OP_DELETE:
        /* Written while the link is down, a line every 5 s. */
        return (bench != NULL && bench->link_up) ? FM_OK : FM_ERR_BUSY;
    case FM_OP_EDIT:
    case FM_OP_MOVE:
    case FM_OP_RENAME:
    case FM_OP_NEW:
    case FM_OP_SAVE:
    case FM_OP_COUNT:
    default:
        return FM_ERR_FIXED;
    }
}

fm_err_t fm_allowed(const fm_bench_t *bench, fm_op_t op, const char *dir,
                    const char *name, bool is_dir)
{
    if (dir == NULL || name == NULL || (unsigned)op >= FM_OP_COUNT) {
        return FM_ERR_ARG;
    }
    if (op == FM_OP_NEW) {
        /* NEW makes something in a folder: the folder is @p name in @p dir,
         * or @p dir itself for "". */
        if (!is_dir && name[0] != '\0') {
            return FM_ERR_ARG;
        }
        return fm_gate(op, bench);
    }
    if (name[0] == '\0') {
        return FM_ERR_ARG;
    }
    if (is_dir && (op == FM_OP_COPY || op == FM_OP_EDIT
                   || op == FM_OP_SAVE)) {
        return FM_ERR_ARG;
    }
    const fm_err_t gate = fm_gate(op, bench);
    if (gate != FM_OK) {
        return gate;
    }
    switch (fm_place_of(bench, dir, name, is_dir)) {
    case FM_PLACE_FIXED:
        if (op == FM_OP_MOVE || op == FM_OP_RENAME || op == FM_OP_DELETE) {
            return FM_ERR_FIXED;
        }
        break;
    case FM_PLACE_LOCKED:
        if (op != FM_OP_INFO) {
            return FM_ERR_LOCKED;
        }
        break;
    case FM_PLACE_FAULT: {
        const fm_err_t f = fault_allows(bench, op);
        if (f != FM_OK) {
            return f;
        }
        break;
    }
    case FM_PLACE_FREE:
    default:
        break;
    }
    if (!is_dir && op != FM_OP_INFO && fm_busy(bench, dir, name)) {
        return FM_ERR_BUSY;
    }
    return FM_OK;
}

fm_err_t fm_target_check(const fm_bench_t *bench, const char *dir,
                         const char *name, bool typed)
{
    if (dir == NULL || name == NULL) {
        return FM_ERR_ARG;
    }
    if (typed) {
        if (fm_name_check(name) != FM_NAME_OK) {
            return FM_ERR_NAME;
        }
    } else if (strchr(name, '/') != NULL || strcmp(name, ".") == 0
               || strcmp(name, "..") == 0) {
        /* Carried over from the card: any characters; fm_name_new() below
         * holds the length. */
        return FM_ERR_NAME;
    }
    /* The ".NEW" a copy or a save writes first has to be listed and its
     * path has to fit; the name's own path is shorter. */
    char tmp[FM_NAME_BUF];
    char path[FM_PATH_SIZE];
    if (!fm_name_new(name, tmp, sizeof(tmp))
        || fm_path_join(dir, tmp, false, path, sizeof(path)) != FM_PATH_OK) {
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
        return FM_OK;
    }
}

fm_kind_t fm_kind_of(const char *dir, const char *name, bool is_dir)
{
    if (is_dir) {
        return FM_KIND_FOLDER;
    }
    if (name == NULL) {
        return FM_KIND_OTHER;
    }
    if (dir != NULL) {
        char path[FM_PATH_SIZE];
        if (fm_path_join(dir, name, false, path, sizeof(path))
            == FM_PATH_OK) {
            for (size_t i = 0u; i < sizeof(k_fault) / sizeof(k_fault[0]);
                 ++i) {
                if (fm_same_path(path, k_fault[i])) {
                    return FM_KIND_FAULT;
                }
            }
        }
    }
    if (log_run_number(name) >= 0) {
        return FM_KIND_RUN;
    }
    if (log_report_number(name) >= 0) {
        return FM_KIND_REPORT;
    }
    if (ends_in(name, ".txt")) {
        return fm_same_path(dir, CARD_KST_CONF) ? FM_KIND_KST : FM_KIND_TEXT;
    }
    if (ends_in(name, ".json")) {
        return fm_same_path(dir, CARD_ESC_CONF) ? FM_KIND_PROFILE
                                                : FM_KIND_JSON;
    }
    if (ends_in(name, ".csv")) {
        return FM_KIND_CSV;
    }
    return FM_KIND_OTHER;
}

bool fm_read_at_start(const char *dir, const char *name)
{
    return name != NULL && fm_same_path(dir, CARD_ESC_CONF)
           && ends_in(name, ".json");
}
