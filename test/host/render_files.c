/**
 * @file render_files.c
 * @brief FILES' views for tools/render_ui.py: the screen over a card in
 *        memory behind the host card task (files_host_io.h), posed by taps
 *        through the router as a person reaches each state.
 *
 * The card: BENCH with its fault log, a hidden file beside it and its
 * LOGS, ESC and KST folders, USER/LOGS with a test's CSV and report and a
 * limit run's report, and in the root a note, a CSV, a JSON file, a
 * picture and a hidden file.
 *
 * SPDX-License-Identifier: MIT
 */
#include "render_files.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "files_host_io.h"
#include "files_screen.h"
#include "files_spots.h"
#include "fm_ops.h"
#include "ui_layout.h"
#include "ui_screen.h"
#include "ui_textkey.h"

/** @brief The card task the views run on. */
static fh_t s_fh;

/** @brief Time passes until the card task has answered: 12 frames of 16ms. */
static void settle(void)
{
    for (int i = 0; i < 12; ++i) {
        ui_router_tick(0.016f);
    }
}

/** @brief A tap at (@p x, @p y) in FILES' coordinates, then the answers. */
static void ftap(int x, int y)
{
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = (int16_t)x,
                                   .y = (int16_t)(UI_BAND_H + y),
                                   .strength = 40 } };
    ui_router_event(&e);
    e.type = TOUCH_EVENT_UP;
    ui_router_event(&e);
    settle();
}

/** @brief A tap on footer button @p b. */
static void foot(int b)
{
    ftap(FS_SPOT_FOOT_CX(b), FS_SPOT_FOOT_CY);
}

/**
 * @brief Select @p name on pane @p i: a tap makes the pane the active one
 *        and selects the row.  Exits when the row is not on screen, so a
 *        view cannot come to show the row beside the one it names.
 */
static void pick(int i, const char *name)
{
    const fm_tree_t *t = files_screen_pane(i);
    for (int r = 0; r < t->held && r < FS_SPOT_ROWS; ++r) {
        if (strcmp(t->page[r].name, name) == 0) {
            if (t->selected != r || files_screen_active() != i) {
                ftap(FS_SPOT_ROW_CX(i), FS_SPOT_ROW_CY(r));
            }
            if (t->selected != r) {
                ftap(FS_SPOT_ROW_CX(i), FS_SPOT_ROW_CY(r));
            }
            if (t->selected == r) {
                return;
            }
        }
    }
    fprintf(stderr, "render_files: no row %s on pane %d\n", name, i);
    exit(2);
}

/** @brief Pane @p i into folder @p name: selected, then tapped again. */
static void enter_dir(int i, const char *name)
{
    pick(i, name);
    ftap(FS_SPOT_ROW_CX(i), FS_SPOT_ROW_CY(files_screen_pane(i)->selected));
}

/** @brief Key @p key of the name keyboard, on FILES' keyboard area. */
static void key(int k)
{
    ui_textkey_t kb;
    memset(&kb, 0, sizeof(kb));
    ui_textkey_open_file(&kb, gfx_rect_make(8, 4, 1008, 488), "", "");
    const gfx_rect_t r = ui_textkey_key_rect(&kb, k);
    ftap(r.x + r.w / 2, r.y + r.h / 2);
}

/** @brief Type @p text, small letters, digits and spaces only. */
static void type(const char *text)
{
    static const char lower[] = "1234567890" "qwertyuiop" "asdfghjkl-"
                                " zxcvbnm";
    for (const char *p = text; *p != '\0'; ++p) {
        if (*p == ' ') {
            key(UI_TK_SPACE);
            continue;
        }
        const char *at = strchr(lower + 1, *p);
        if (at == NULL) {
            fprintf(stderr, "render_files: no key %c\n", *p);
            exit(2);
        }
        key((int)(at - lower));
    }
}

/** @brief The card every view starts from. */
static void card(void)
{
    fk_card_t *c = &s_fh.card;
    fk_dir(c, "BENCH/LOGS");
    fk_dir(c, "BENCH/ESC/CONF");
    fk_dir(c, "BENCH/KST/CONF");
    fk_dir(c, "USER/LOGS");
    fk_file(c, "BENCH/LOGS/SERVO001.CSV", NULL, 48213u);
    fk_file(c, "BENCH/LOGS/MOTOR002.CSV", NULL, 1310720u);
    fk_file(c, "BENCH/RCBENCH.LOG", NULL, 812u);
    fk_file(c, "BENCH/._RCBENCH.LOG", NULL, 4096u);
    fk_file(c, "BENCH/ESC/CONF/YGE-135.JSON", NULL, 2140u);
    fk_file(c, "BENCH/KST/CONF/KST.TXT", NULL, 1200u);
    fk_file(c, "BENCH/KST/CONF/MG90S.TXT", NULL, 1873u);
    fk_file(c, "USER/LOGS/ATEST005.CSV", NULL, 15104u);
    fk_file(c, "USER/LOGS/ATEST005.TXT", NULL, 2210u);
    fk_file(c, "USER/LOGS/LIMIT006.TXT", NULL, 1873u);
    fk_file(c, "NOTES.TXT", NULL, 512u);
    fk_file(c, "THRUST.CSV", NULL, 7340032u);
    fk_file(c, "WIRING.JPG", NULL, 3355443u);
    fk_file(c, "SETUP.JSON", NULL, 914u);
    fk_file(c, ".DS_Store", NULL, 6148u);
    /* Every entry's time as stat() answers: 2024-05-17 12:34:56. */
    c->mtime = 1715949296;
}

/** @brief Whether @p view is @p name. */
static bool is(const char *view, const char *name)
{
    return strcmp(view, name) == 0;
}

void render_files_prepare(const char *view)
{
    fh_init(&s_fh);
    const bool nocard = is(view, "files-nocard");
    if (!nocard) {
        card();
    } else {
        fk_pull(&s_fh.card);
        s_fh.state = CARD_STATE_NONE;
    }
    if (is(view, "files-nofolder")) {
        fk_free(&s_fh.card);
        fk_init(&s_fh.card);
        fk_file(&s_fh.card, "NOTES.TXT", NULL, 512u);
    }
    if (is(view, "files-paged")) {
        char path[32];
        for (int i = 0; i < 120; ++i) {
            snprintf(path, sizeof(path), "SWEEPS/SWEEP%03d.CSV", i);
            fk_file(&s_fh.card, path, NULL, 20480u + (uint64_t)i * 512u);
        }
    }
    if (is(view, "files-empty") || is(view, "files-delete-folder")) {
        fk_dir(&s_fh.card, "OLD");
    }
    if (is(view, "files-hidden")) {
        fk_file(&s_fh.card, "OLD/.DS_Store", NULL, 6148u);
    }
    s_fh.armed = is(view, "files-armed");
    s_fh.volume = nocard ? NULL : "RCBENCH";
    if (is(view, "files-logging")) {
        s_fh.marks.test = 5u;       /* USER/LOGS/ATEST005.CSV is open */
    }
    s_fh.defer = is(view, "files-reading");
    const files_io_t io = fh_files_io(&s_fh);
    files_screen_set_io(&io);
}

bool render_files_armed(const char *view)
{
    return is(view, "files-armed");
}

void render_files_pose(const char *view)
{
    settle();
    if (is(view, "files-reading") || is(view, "files-nocard")
        || is(view, "files-nofolder")) {
        return;                       /* as the screen opens */
    }
    if (is(view, "files") || is(view, "files-armed")
        || is(view, "files-new") || is(view, "files-info")
        || is(view, "files-rename") || is(view, "files-logging")
        || is(view, "files-name")) {
        pick(0, "NOTES.TXT");
    }
    if (is(view, "files-logging")) {
        foot(FS_SPOT_RENAME);       /* refused: the gate's sentence */
    } else if (is(view, "files-new") || is(view, "files-name")
               || is(view, "files-made")) {
        foot(FS_SPOT_NEW);
        if (!is(view, "files-new")) {
            const bool folder = is(view, "files-made");
            ftap(FS_SPOT_NEW_CX, FS_SPOT_NEW_CY(folder ? 0 : 1));
            type(folder ? "sweeps 2" : "bench notes");
            if (folder) {
                key(UI_TK_OK);
            }
        }
    } else if (is(view, "files-info")) {
        foot(FS_SPOT_INFO);
    } else if (is(view, "files-info-folder")) {
        pick(0, "BENCH");
        foot(FS_SPOT_INFO);
    } else if (is(view, "files-rename")) {
        foot(FS_SPOT_RENAME);
    } else if (is(view, "files-delete")) {
        pick(1, "ATEST005.CSV");
        foot(FS_SPOT_DELETE);
    } else if (is(view, "files-delete-folder")) {
        pick(0, "OLD");
        foot(FS_SPOT_DELETE);
    } else if (is(view, "files-empty") || is(view, "files-hidden")) {
        enter_dir(0, "OLD");
    } else if (is(view, "files-copy")) {
        enter_dir(0, "BENCH");
        enter_dir(0, "LOGS");
        pick(0, "MOTOR002.CSV");
        s_fh.defer = true;
        s_fh.fixed_progress = true;
        s_fh.done = 48u * FM_CHUNK;   /* 786432B of 1310720B */
        s_fh.total = 1310720u;
        foot(FS_SPOT_COPY);
        settle();
        settle();                    /* past the box's 0.25s */
    } else if (is(view, "files-paged")) {
        enter_dir(0, "SWEEPS");
        ftap(FS_SPOT_NEXT_CX(0), FS_SPOT_HEAD_CY);
        pick(0, "SWEEP050.CSV");
    } else if (is(view, "files-conf")) {
        enter_dir(0, "BENCH");
        enter_dir(0, "ESC");
        enter_dir(0, "CONF");
        ftap(FS_SPOT_UP_CX(1), FS_SPOT_HEAD_CY);    /* USER */
        ftap(FS_SPOT_UP_CX(1), FS_SPOT_HEAD_CY);    /* the root */
        enter_dir(1, "BENCH");
        enter_dir(1, "KST");
        enter_dir(1, "CONF");
        pick(1, "KST.TXT");
    } else if (is(view, "files-bench")) {
        enter_dir(0, "BENCH");
        ftap(FS_SPOT_UP_CX(1), FS_SPOT_HEAD_CY);
        ftap(FS_SPOT_UP_CX(1), FS_SPOT_HEAD_CY);
        enter_dir(1, "BENCH");
        enter_dir(1, "LOGS");
        pick(0, "RCBENCH.LOG");
    } else if (is(view, "files-exists")) {
        fk_file(&s_fh.card, "USER/LOGS/NOTES.TXT", NULL, 512u);
        foot(FS_SPOT_RESCAN);
        pick(0, "NOTES.TXT");
        foot(FS_SPOT_COPY);
    } else if (is(view, "files-busy")) {
        enter_dir(0, "BENCH");
        pick(0, "RCBENCH.LOG");
        s_fh.link_up = false;
        foot(FS_SPOT_COPY);
    } else if (is(view, "files-fixed")) {
        pick(0, "BENCH");
        foot(FS_SPOT_RENAME);
    } else if (is(view, "files-locked")) {
        enter_dir(0, "BENCH");
        enter_dir(0, "KST");
        enter_dir(0, "CONF");
        pick(0, "KST.TXT");
        foot(FS_SPOT_DELETE);
    }
}
