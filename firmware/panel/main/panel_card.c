/*
 * The card's files outside the run log: the ESC profiles read at start-up,
 * the log viewer's list, open and delete, and the servo programmer's record
 * and file requests, which the logger task serves (panel_log.c).
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>          /* fsync(), unlink() */

#include "esp_heap_caps.h"
#include "esp_log.h"

#include "card_tree.h"
#include "esc_profile.h"
#include "kst_card.h"
#include "kst_files.h"
#include "kst_panel.h"
#include "kst_prog.h"
#include "log_name.h"
#include "log_select.h"
#include "log_take.h"
#include "log_viewer_screen.h"
#include "panel.h"
#include "storage.h"

static const char *TAG = "rcbench";

/* ------------------------------------------------- ESC profiles on the card */

/*
 * Every ESC (electronic speed controller) profile is compiled in.  A file
 * in /ESC/ on the card replaces the built-in profile with the same id, or
 * adds one with a new id: a corrected default or an ESC the build does not
 * know needs a file copied to the card, not a firmware release.  Read once,
 * at start-up; a card changed later is read at the next start.
 */
#define ESC_CARD_DIR "ESC"
/* Names are collected first and read after the walk, so no file is open
 * while the directory is.  Twice the registry's room: a card with more files
 * than that is refused past the first ones, and the log says how many. */
#define ESC_CARD_MAX (2u * ESC_PROFILE_MAX_OVERRIDES)

typedef struct {
    char     name[ESC_CARD_MAX][STORAGE_NAME_MAX];
    unsigned held;
    unsigned files;
} esc_card_t;

static void esc_card_take(const storage_entry_t *entry, void *ctx)
{
    esc_card_t *c = (esc_card_t *)ctx;
    if (entry->is_dir) {
        return;
    }
    ++c->files;
    if (c->held < ESC_CARD_MAX) {
        memcpy(c->name[c->held], entry->name, STORAGE_NAME_MAX);
        c->name[c->held][STORAGE_NAME_MAX - 1u] = '\0';
        ++c->held;
    }
}

void esc_profiles_load(void)
{
    esc_profiles_clear_overrides();
    if (!storage_mounted()) {
        return;
    }
    /* 4 KiB of names: static, off the start-up task's stack. */
    static esc_card_t c;
    memset(&c, 0, sizeof(c));
    if (storage_walk(ESC_CARD_DIR, ".json", esc_card_take, &c, NULL) < 0) {
        return;                         /* no /ESC/: nothing to add */
    }
    /* One byte past the limit, so a file that fills it is seen to be
     * larger rather than parsed cut off. */
    char *buf = heap_caps_malloc(ESC_PROFILE_MAX_BYTES + 1u,
                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buf == NULL) {
        ESP_LOGW(TAG, "ESC profiles: no memory to read the card");
        return;
    }
    unsigned added = 0;
    for (unsigned i = 0; i < c.held; ++i) {
        char path[STORAGE_NAME_MAX + 32];
        storage_path(ESC_CARD_DIR, c.name[i], path, sizeof(path));
        FILE *f = fopen(path, "rb");
        if (f == NULL) {
            ESP_LOGW(TAG, "ESC profiles: %s will not open", c.name[i]);
            continue;
        }
        const size_t len = fread(buf, 1, ESC_PROFILE_MAX_BYTES + 1u, f);
        /* A short read is the end of the file or a card fault; parsing what
         * a fault left could accept a file that is not what the card holds. */
        const bool read_failed = (ferror(f) != 0);
        fclose(f);
        if (read_failed) {
            ESP_LOGW(TAG, "ESC profiles: %s refused: read error", c.name[i]);
            continue;
        }
        esc_profile_t p;
        void *block = NULL;
        char err[96];
        if (!esc_profile_parse(buf, len, &p, &block, err, sizeof(err))) {
            ESP_LOGW(TAG, "ESC profiles: %s refused: %s", c.name[i], err);
            continue;
        }
        if (!esc_profile_file_is(c.name[i], p.id)) {
            ESP_LOGW(TAG, "ESC profiles: %s refused: its id is %s", c.name[i],
                     p.id);
            free(block);
            continue;
        }
        const bool replaces = (esc_profiles_find(p.id) != NULL);
        const char *why = NULL;
        if (!esc_profiles_override_why(&p, block, &why)) {
            ESP_LOGW(TAG, "ESC profiles: %s refused: %s", c.name[i], why);
            continue;
        }
        ++added;
        ESP_LOGI(TAG, "ESC profiles: %s %s %s", c.name[i],
                 replaces ? "replaces" : "adds", p.id);
    }
    heap_caps_free(buf);
    if (c.files > c.held) {
        ESP_LOGW(TAG, "ESC profiles: %u files in /%s, the first %u read",
                 c.files, ESC_CARD_DIR, c.held);
    }
    ESP_LOGI(TAG, "ESC profiles: %u built in, %u read from the card, %u "
             "offered", (unsigned)esc_profiles_builtin_count, added,
             (unsigned)esc_profiles_count());
}


/* ------------------------------------------------------- the card, listed */

/*
 * The log viewer's side of the SD card.
 *
 * The viewer knows about names, sizes and a rewindable source; it knows
 * nothing about a mount point or a suffix filter, and it is built by the host
 * suite against a fake card.  This is the only place the two meet, and
 * without it the viewer has no list function at all -- which it reports as no
 * card, whatever is actually mounted.
 */
/*
 * What the viewer can actually open.  Every file it lists is handed to
 * log_csv_analyse(), and nothing in the tree decodes a Betaflight blackbox
 * log, so offering .bfl here would list files that fail to open.  The screen
 * says the same thing; the two are kept together deliberately.
 */
#define CARD_SUFFIXES ".csv"

/* The file the viewer currently has open, so close() has something to close.
 * One at a time: the viewer opens a log, reads it and closes it before it
 * opens another. */
static FILE *s_card_file;

/*
 * The run the logger has open, as a run number, or 0 for none.
 *
 * Written by the runlog task and read by the task that renders.  A run whose
 * file is still open is not a run this viewer can read: FAT keeps a file's
 * length in its directory entry and f_close is what writes it, so the size
 * the walk sees is the size at the last commit and the rows since then are
 * not in it.  The viewer would show a run that is still growing as a
 * finished one, and the operator has no way to tell the two apart.
 *
 * So it is left out of the list until the logger closes it, which is one
 * pass of the logger after the queue drains.  A stalled card makes that
 * window long, which is exactly when the difference matters.
 */
atomic_uint s_log_open_run;

/*
 * The servo test's CSV while the logger writes it, as a run number, or 0:
 * held back from the viewer for the reason s_log_open_run is.  A bench run
 * and a test run are open at once, so it is a second marker.
 */
atomic_uint s_test_open_run;

/*
 * A test's report, ATESTnnn.TXT or LIMITnnn.TXT, from its open to its
 * close, as a run number, or 0: the automatic test's after its CSV, and the
 * limit run's, which has no CSV.  An open report is an open run
 * (run_open_now()).
 */
atomic_uint s_test_open_txt;

/*
 * The folder the viewer lists, opens in and deletes from: a path from the
 * card's root, set by the viewer before each call (log_viewer_io_t.folder).
 * The task that renders' own.
 */
static char s_card_dir[CARD_DIR_MAX];

/* Whether @p name in the viewer's folder is a file the logger is still
 * writing, by the marks of that folder (log_marks_busy()). */
static bool card_busy(const char *name)
{
    const log_marks_t marks = {
        .run_dir  = CARD_AUTO_LOGS,
        .run      = atomic_load(&s_log_open_run),
        .test_dir = CARD_USER_LOGS,
        .test     = atomic_load(&s_test_open_run),
        .txt      = atomic_load(&s_test_open_txt),
    };
    return log_marks_busy(&marks, s_card_dir, name);
}

static void card_folder(const char *dir, void *ctx)
{
    (void)ctx;
    snprintf(s_card_dir, sizeof(s_card_dir), "%s", (dir != NULL) ? dir : "");
}

static unsigned card_written(log_viewer_tab_t *tab, void *ctx)
{
    (void)ctx;
    const unsigned n = atomic_load(&s_card_written);
    if (tab != NULL) {
        *tab = (log_viewer_tab_t)atomic_load(&s_card_written_tab);
    }
    return n;
}

/*
 * One directory entry on its way into the viewer's list.
 *
 * The card takes LOG_RUN_LAST runs and a page of the list holds
 * LOG_VIEWER_MAX_FILES of them, so which of them arrive is a decision rather
 * than a side effect of where the read stopped: every entry is offered to
 * log_select_page_offer(), which holds the page the viewer asked for.
 */
typedef struct {
    log_select_page_t sel;
    int files;      /* what the card holds that this viewer could open */
} card_pick_t;

static void card_take(const storage_entry_t *entry, void *ctx)
{
    card_pick_t *pick = (card_pick_t *)ctx;
    /*
     * Files only.  storage_walk() offers directories whatever the suffix
     * filter says, because a filter is about names and a directory has no
     * extension to match -- but this viewer cannot enter one, so a directory
     * row is a row that says "is a folder" and nothing else.  Letting them
     * compete for a bounded list means 48 folders sorted early can fill it
     * and leave the card's only log unreachable.
     */
    if (entry->is_dir) {
        return;
    }
    /* And not the run the logger still has open; see s_log_open_run.  Not
     * counted either: a card whose only file is that run has nothing this
     * viewer can show, which is what an empty list says. */
    if (card_busy(entry->name)) {
        return;
    }
    ++pick->files;
    /* Field by field rather than a block copy of the structure: the two
     * agree today and neither owns the other's layout.  The name is copied
     * whole and terminated by hand, so one that filled its array without a
     * terminator ends here rather than running off the end. */
    _Static_assert(sizeof(((log_viewer_file_t *)0)->name)
                       == sizeof(entry->name),
                   "the viewer's name field and the card's are one size");
    log_viewer_file_t f;
    memcpy(f.name, entry->name, sizeof(f.name));
    f.name[sizeof(f.name) - 1u] = '\0';
    f.size   = entry->size;
    f.is_dir = entry->is_dir;
    log_select_page_offer(&pick->sel, &f);
}

static int card_list(log_viewer_file_t *out, int max_entries,
                     log_viewer_page_t *page, void *ctx)
{
    (void)ctx;
    /*
     * A card put in after the panel booted is mounted here, on the way past.
     * The only other storage_init() runs in the splash sequence, so without
     * this the RESCAN button that the empty screen tells the operator to
     * press could never find a card and a reboot would be the only way in.
     * Mounting when something is already mounted returns at once.
     */
    if (!storage_mounted()) {
        (void)storage_init();
    }
    /*
     * No card is -1 and an empty card is 0, and the viewer says different
     * things about them.  storage_walk() cannot open the root of a volume
     * that is not mounted, so the two already arrive apart; asking
     * storage_mounted() first makes that true by construction rather than by
     * how a failure happened to surface.
     */
    if (!storage_mounted()) {
        return -1;
    }
    card_pick_t pick;
    pick.files = 0;
    log_select_page_begin(&pick.sel, out, max_entries, page);
    /*
     * No tick: this runs on the task that renders, which has no safety line
     * to hold and nothing to pump.  One walk per page, PREV and NEXT
     * included: the page is held and the rest of the card is counted, so a
     * page of a full card costs what the first page of it does.
     */
    const int total = storage_walk(s_card_dir, CARD_SUFFIXES, card_take,
                                   &pick, NULL);
    if (total == STORAGE_WALK_NO_DIR) {
        /* A folder the logger has not made yet: an empty list, with the
         * empty folder's text, and not a card that is gone. */
        (void)log_select_page_end(&pick.sel, page);
        return 0;
    }
    if (total < 0) {
        /*
         * Mounted, and yet its root will not open: the card it was mounted
         * from has been taken out or swapped.  Nothing clears that flag, so
         * the mount stays stale and a replacement card is not found until
         * the panel restarts.
         *
         * Not unmounted from here.  This runs on the task that renders, and
         * the runlog task writes the run log on the same volume: unmounting
         * under an open handle frees the SPI bus beneath a write from another
         * task.  Putting that right means one task owning the card's
         * lifetime, which is a change of its own; see STATUS.md.
         */
        return -1;
    }
    /* Held in rank order while the card is read and drawn in it: the newest
     * run at the top. */
    (void)log_select_page_end(&pick.sel, page);
    /*
     * Files, not entries.  The number goes to the tab that says how much of
     * the card is on the list, and counting folders there would say a card
     * holds more than the list can ever show while the list is complete.
     */
    return pick.files;     /* what the card holds; page->held were written */
}

static bool card_open(const char *name, log_source_t *src, void *ctx)
{
    (void)ctx;
    if (name == NULL || src == NULL) {
        return false;
    }
    /* Whatever was open is closed first.  A viewer that opened a second log
     * without closing the first would leak the handle, and there are few. */
    if (s_card_file != NULL) {
        fclose(s_card_file);
        s_card_file = NULL;
    }
    /*
     * Refused if the logger has that run open, even though card_list() left
     * it out: a name can reach here from a list taken before the run started
     * -- the screen holds its list until it is opened again or RESCAN.
     */
    if (card_busy(name)) {
        ESP_LOGW(TAG, "%s is still being written", name);
        return false;
    }
    char path[STORAGE_NAME_MAX + sizeof(STORAGE_MOUNT_POINT) + 2];
    storage_path(s_card_dir, name, path, sizeof(path));
    s_card_file = fopen(path, "rb");
    if (s_card_file == NULL) {
        /* Listed and then gone, or unreadable.  The viewer says so; there is
         * nothing here to retry. */
        ESP_LOGW(TAG, "could not open %s", path);
        return false;
    }
    log_source_stdio(src, s_card_file);
    return true;
}

static void card_close(void *ctx)
{
    (void)ctx;
    if (s_card_file != NULL) {
        fclose(s_card_file);
        s_card_file = NULL;
    }
}

static const char *card_volume(void *ctx)
{
    (void)ctx;
    return storage_card_name();
}

/*
 * Delete a run the operator confirmed on the screen.
 *
 * Refused for the run the logger has open, for the reason card_open() refuses
 * it: the name can come from a list taken before the run started.  Deleting
 * it would pull the directory entry out from under the logger's handle, and
 * the logger would go on writing a run nobody can find.
 *
 * And refused for anything that is not a plain name in the viewer's folder:
 * the viewer only ever lists those, so a separator here is a caller defect
 * and not a path to follow.
 */
static bool card_remove(const char *name, void *ctx)
{
    (void)ctx;
    if (name == NULL || name[0] == '\0' || strchr(name, '/') != NULL
        || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        return false;
    }
    if (card_busy(name)) {
        ESP_LOGW(TAG, "%s is still being written", name);
        return false;
    }
    /* The viewer closes what it reads before it lists again, so nothing is
     * open here; closed anyway, because FAT will not delete an open file. */
    if (s_card_file != NULL) {
        fclose(s_card_file);
        s_card_file = NULL;
    }
    char path[STORAGE_NAME_MAX + sizeof(STORAGE_MOUNT_POINT) + 2];
    storage_path(s_card_dir, name, path, sizeof(path));
    if (remove(path) != 0) {
        ESP_LOGW(TAG, "could not delete %s", path);
        return false;
    }
    ESP_LOGI(TAG, "deleted %s", path);
    /* A test's report goes with its run: left behind, it would be the only
     * file carrying the number.  The same prefix as the run's, so
     * ATESTnnn.CSV takes ATESTnnn.TXT, and in the root a servo test's
     * SERVOnnn.CSV or BENCHnnn.CSV of firmware 0.21 and older takes its
     * report. */
    char txt[LOG_RUN_NAME_MAX];
    log_name_report_of(txt, sizeof(txt), name);
    if (txt[0] != '\0') {
        storage_path(s_card_dir, txt, path, sizeof(path));
        if (remove(path) == 0) {
            ESP_LOGI(TAG, "deleted %s", path);
        }
    }
    return true;
}

const log_viewer_io_t k_card_io = {
    .list   = card_list,
    .open   = card_open,
    .close  = card_close,
    .volume = card_volume,
    .remove = card_remove,
    .folder = card_folder,
    .written = card_written,
    .ctx    = NULL,
};

/*
 * The programmer's record, KST.TXT in the card's directory KST beside the
 * servo files (kst_store.h keeps the name free): a write under way with the
 * servo's values before it (kst_prog.h).  The render loop fills
 * s_kst_file and counts s_kst_file_sent up; this task writes it, says in
 * s_kst_file_ok whether the card took it whole, and counts
 * s_kst_file_done up.  One file at a time: the render loop fills the
 * buffer only while the two counts are equal.
 *
 * The text goes to KST.NEW and takes the name KST.TXT once the card holds
 * it whole, so that a write the card or the supply ends leaves one of the
 * two whole: KST.TXT as it was, or KST.NEW complete.  FAT has no rename
 * over a file, so KST.TXT is removed between the two; kst_record_load()
 * takes a whole KST.NEW before KST.TXT.
 */
#define KST_FILE_NAME "KST.TXT"
#define KST_FILE_NEW  "KST.NEW"
char        s_kst_file[KST_PROG_FILE_MAX];
static char        s_kst_path[64];
static char        s_kst_path_new[64];
size_t      s_kst_file_len;
atomic_uint s_kst_file_sent;
atomic_uint s_kst_file_done;
atomic_bool s_kst_file_ok;

void kst_record_write(void)
{
    const unsigned sent = atomic_load(&s_kst_file_sent);
    if (sent == atomic_load(&s_kst_file_done)) {
        return;
    }
    bool ok = false;
    if (storage_mounted() && kst_files_dir()) {
        kst_files_path(KST_FILE_NAME, s_kst_path, sizeof(s_kst_path));
        kst_files_path(KST_FILE_NEW, s_kst_path_new, sizeof(s_kst_path_new));
        FILE *f = fopen(s_kst_path_new, "w");
        if (f != NULL) {
            ok = fwrite(s_kst_file, 1, s_kst_file_len, f) == s_kst_file_len;
            ok = (fflush(f) == 0) && ok;
            ok = (fsync(fileno(f)) == 0) && ok;
            ok = (fclose(f) == 0) && ok;
        }
        if (ok) {
            (void)unlink(s_kst_path);   /* fails where there is none */
            ok = rename(s_kst_path_new, s_kst_path) == 0;
        }
    }
    atomic_store(&s_kst_file_ok, ok);
    atomic_store(&s_kst_file_done, sent);
}

/* @p path into s_kst_file.  False for no file; @p len is 0 for a file that
 * does not read. */
static bool kst_record_read(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return false;
    }
    const size_t n = fread(s_kst_file, 1, sizeof(s_kst_file) - 1u, f);
    const bool read_failed = (ferror(f) != 0);
    fclose(f);
    s_kst_file[n] = '\0';
    *len = read_failed ? 0u : n;
    return true;
}

/* s_kst_file to the model: at the start, or from a card mounted later,
 * where a record the model holds already stands. */
static bool kst_record_use(bool late, size_t len)
{
    return late ? kst_panel_file_late(s_kst_file, len)
                : kst_panel_file_load(s_kst_file, len);
}

/*
 * The file as the card holds it: before the tasks start, and on the render
 * task for a card mounted later, while no file of the model is out.  A whole
 * KST.NEW is the text of a write that did not reach its name: it is used
 * and takes the name.  One that is not whole is removed.  No file is no
 * record; a KST.TXT that does not read or does not parse is said on the
 * programmer's page and not used.
 */
void kst_record_load(bool late)
{
    if (!storage_mounted()) {
        return;
    }
    size_t len = 0;
    kst_files_path(KST_FILE_NAME, s_kst_path, sizeof(s_kst_path));
    kst_files_path(KST_FILE_NEW, s_kst_path_new, sizeof(s_kst_path_new));
    if (kst_record_read(s_kst_path_new, &len)) {
        if (kst_prog_file_whole(s_kst_file, len)) {
            /* From a card mounted later it takes the name only as the
             * record the model uses.  One the model refuses, for a record
             * it holds, stays as it is: the model's record is written
             * over it. */
            if (!kst_record_use(late, len) && late) {
                ESP_LOGW(TAG, "%s is not used", KST_FILE_NEW);
                return;
            }
            (void)unlink(s_kst_path);
            if (rename(s_kst_path_new, s_kst_path) != 0) {
                ESP_LOGW(TAG, "%s keeps its name", KST_FILE_NEW);
            }
            return;
        }
        (void)unlink(s_kst_path_new);
    }
    if (!kst_record_read(s_kst_path, &len)) {
        return;
    }
    if (!kst_record_use(late, len)) {
        ESP_LOGW(TAG, "%s is not used", KST_FILE_NAME);
    }
}

/*
 * A servo file request (kst_prog.h): list, load or save in the directory
 * KST.  The render loop fills s_kst_card_req and counts s_kst_card_sent
 * up; this task runs it on the card, leaves the answer in s_kst_card_done
 * and counts s_kst_card_answered up.  One request at a time: the render
 * loop takes the next from the model only while the two counts are equal,
 * and reads the answer only after they are equal again.
 */
kst_prog_card_req_t  s_kst_card_req;
kst_prog_card_done_t s_kst_card_done;
atomic_uint          s_kst_card_sent;
atomic_uint          s_kst_card_answered;

void kst_card_serve(void)
{
    const unsigned sent = atomic_load(&s_kst_card_sent);
    if (sent == atomic_load(&s_kst_card_answered)) {
        return;
    }
    kst_card_run(kst_files_io(), &s_kst_card_req, &s_kst_card_done);
    atomic_store(&s_kst_card_answered, sent);
}
