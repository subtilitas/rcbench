/**
 * @file render_files.c
 * @brief FILES' views for tools/render_ui.py: the screen over a card in
 *        memory behind the host card task (files_host_io.h), posed by taps
 *        through the router as a person reaches each state.
 *
 * The card: BENCH with its fault log, a hidden file beside it and its
 * LOGS, ESC and KST folders, USER/LOGS with a test's CSV and report and a
 * limit run's report, and in the root a note, a CSV, a JSON file, a
 * picture and a hidden file.  The text viewer's views and logs-report put
 * text in the files they open: the report as servo_report.c writes it in
 * the language rendered, a limit run's report, a servo file as
 * kst_file_format() writes it, and an ESC profile.
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
#include "kst_file.h"
#include "kst_sim.h"
#include "servo_test.h"
#include "settings.h"
#include "ui_layout.h"
#include "ui_screen.h"
#include "ui_text.h"
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
 * @brief A drag of pane @p i's list up by @p rows rows.
 * @param i     the pane
 * @param rows  the rows
 */
static void drag_up(int i, int rows)
{
    const int x = FS_SPOT_ROW_CX(i);
    const int y0 = UI_BAND_H + FS_SPOT_ROW_CY(FS_SPOT_ROWS - 1);
    touch_event_t e = { .type = TOUCH_EVENT_DOWN,
                        .point = { .id = 1, .x = (int16_t)x, .y = (int16_t)y0,
                                   .strength = 40 } };
    ui_router_event(&e);
    e.type = TOUCH_EVENT_MOVE;
    e.point.y = (int16_t)(y0 - rows * 48 - 10);
    ui_router_event(&e);
    e.type = TOUCH_EVENT_UP;
    ui_router_event(&e);
}

/**
 * @brief Select @p name on pane @p i: a tap makes the pane the active one
 *        and selects the row, dragged on screen first.  Exits when the page
 *        does not hold the row, so a view cannot come to show the row beside
 *        the one it names.
 */
static void pick(int i, const char *name)
{
    const fm_tree_t *t = files_screen_pane(i);
    for (int r = 0; r < t->held; ++r) {
        if (strcmp(t->page[r].name, name) == 0) {
            const int past = r - (files_screen_scroll(i) + FS_SPOT_ROWS - 1);
            if (past > 0) {
                drag_up(i, past);
            }
            const int on = r - files_screen_scroll(i);
            if (t->selected != r || files_screen_active() != i) {
                ftap(FS_SPOT_ROW_CX(i), FS_SPOT_ROW_CY(on));
            }
            if (t->selected != r) {
                ftap(FS_SPOT_ROW_CX(i), FS_SPOT_ROW_CY(on));
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
    ftap(FS_SPOT_ROW_CX(i), FS_SPOT_ROW_CY(files_screen_pane(i)->selected
                                           - files_screen_scroll(i)));
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

/* LIMIT006.TXT as a limit run writes it, in English. */
static const char k_limit_report[] =
    "RCBENCH SERVO LIMIT REPORT\n"
    "Result:         PASS\n"
    "Device:         KST X20-830\n"
    "Firmware:       rcbench 0.22.0\n"
    "Mode:           search only, nothing written to the servo\n"
    "Output:         S1, 333Hz\n"
    "Pulse range:    centre 1500us, 900-2100us\n"
    "Searched:       from 1500us out to 900us and 2100us\n"
    "Current:        INA3221, 50ms windows, shunt 100.0m\xCE\xA9\n"
    "Search rule:    10us steps, settle 120ms, mean of 80ms; bound above "
    "2.0 x free and +0.15A\n"
    "Protections:    a sample at 4.0A, above 2.5A for 300ms, no window "
    "for 500ms, 60s a search\n"
    "Duration:       38.4s\n"
    "\n"
    "ENDS (the search before programming)\n"
    "Low end:        bound at 960us, endpoint 970us; free 0.112A, bound "
    "0.391A\n"
    "High end:       free to 2100us; free 0.108A\n"
    "AS5600:         not fitted, no angles\n"
    "\n"
    "PROGRAMMING (LEFT ANGLE and RIGHT ANGLE, unit not known)\n"
    "Not run: search only.\n"
    "\n"
    "Bench: the ends found are not applied to PULSE MIN and PULSE MAX.\n";

/* YGE-135.JSON: the head of an ESC profile. */
static const char k_profile[] =
    "{\n"
    "  \"id\": \"yge-135\",\n"
    "  \"brand\": \"YGE\",\n"
    "  \"family\": \"135 LVT\",\n"
    "  \"models\": [\n"
    "    {\n"
    "      \"name\": \"YGE 135 LVT\",\n"
    "      \"cells_min\": 3,\n"
    "      \"cells_max\": 6,\n"
    "      \"v_min_mv\": 9000,\n"
    "      \"v_max_mv\": 26000,\n"
    "      \"current_a\": 135,\n"
    "      \"bec\": \"switching\",\n"
    "      \"firmware_note\": null,\n"
    "      \"telemetry\": true\n"
    "    }\n"
    "  ]\n"
    "}\n";

/**
 * @brief A mean of @p n readings of @p a each.
 * @param a  the reading
 * @param n  how many
 * @return the mean
 */
static servo_test_mean_t mean_of(float a, uint32_t n)
{
    const servo_test_mean_t m = { a * (float)n, n };
    return m;
}

/**
 * @brief ATEST005.TXT as servo_report.c writes it in the language
 *        rendered: an MG90S on the PD mini at 4.8V, 6.0V and 7.4V, 12
 *        moves a step, its idle current over the limit at 7.4V.
 * @param out  takes the report
 * @param n    bytes at @p out
 * @return its bytes
 */
static size_t servo_report(char *out, size_t n)
{
    static servo_test_t r;
    servo_test_init(&r);
    const ui_language_t *lt = ui_text_table(ui_text_language());
    servo_test_cfg_t *g = &r.cfg;
    g->text = (lt != NULL) ? lt->servo : NULL;
    static const float volts[3] = { 4.8f, 6.0f, 7.4f };
    static const float idle[3] = { 0.012f, 0.018f, 0.121f };
    g->step_count = 3u;
    g->i_limit = 2.0f;
    g->centre_us = 1500u;
    g->end_lo_us = 1000u;
    g->end_hi_us = 2000u;
    g->settle_ms = 300u;
    g->dwell_ms = 500u;
    g->by_moves = true;
    g->moves = 12u;
    g->idle_max_a = 0.10f;
    g->hold_max_a = 0.50f;
    g->travel_max_ms = 400u;
    g->stall_a = 1.20f;
    g->report = true;
    snprintf(g->dut, sizeof(g->dut), "%s", "MG90S");
    snprintf(g->type, sizeof(g->type), "%s", "STANDARD PWM");
    g->min_us = 1000u;
    g->max_us = 2000u;
    g->frame_hz = 50u;
    g->travel_deg = 45u;
    g->range_pct = 100u;
    servo_test_meter_pdmini(&g->meter);
    snprintf(g->firmware, sizeof(g->firmware), "%s", "0.23.0");
    r.step_count = 3u;
    for (unsigned k = 0u; k < 3u; ++k) {
        g->steps_v[k] = volts[k];
        servo_test_step_t *s = &r.steps[k];
        s->set_v = volts[k];
        s->begun = true;
        s->done = true;
        s->v = mean_of(volts[k] - 0.02f, 40u);
        s->v_min = volts[k] - 0.09f;
        s->idle = mean_of(idle[k], 20u);
        s->move_a = 0.040f + 0.005f * (float)k;
        s->move = mean_of(0.210f + 0.030f * (float)k, 60u);
        s->move_peak_a = 0.480f + 0.060f * (float)k;
        s->peak_a = s->move_peak_a;
        s->hold[0] = mean_of(0.015f, 20u);
        s->hold[1] = mean_of(0.030f, 20u);
        s->travels = 12u;
        s->moves = 12u;
        s->travel_max_ms = 220u - 30u * k;
        s->travel_sum_ms = 12u * (180u - 25u * k);
        s->moved = true;
    }
    r.start_ms = 0u;
    r.end_ms = 61200u;
    r.rows = 612u;
    r.readings = 586u;          /* one every 104ms, the PD mini's rate */
    r.module_samples = 590u;
    r.first_ms = 100u;
    r.last_ms = 61000u;
    size_t used = 0u;
    char line[SERVO_TEST_LINE_MAX];
    for (unsigned k = 0u; servo_report_line(&r, k, line, sizeof(line));
         ++k) {
        const int w = snprintf(out + used, n - used, "%s\n", line);
        if (w < 0 || (size_t)w >= n - used) {
            break;
        }
        used += (size_t)w;
    }
    return used;
}

/** @brief Put @p text at @p path on the card. */
static void put(const char *path, const char *text, size_t len)
{
    fk_file(&s_fh.card, path, (const uint8_t *)text, len);
}

/**
 * @brief The card every view starts from.
 * @param texts  the viewer's files hold text: the report, the limit
 *               run's report, the servo file and the profile
 */
static void card(bool texts)
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
    fk_file(c, "BENCH/KST/CONF/KST.TXT", NULL, 1200u);
    fk_file(c, "USER/LOGS/ATEST005.CSV", NULL, 15104u);
    if (texts) {
        static char report[8192];
        put("USER/LOGS/ATEST005.TXT", report,
            servo_report(report, sizeof(report)));
        put("USER/LOGS/LIMIT006.TXT", k_limit_report,
            sizeof(k_limit_report) - 1u);
        static char kst[KST_FILE_MAX];
        const kst_image_t img = sim_bench_image();
        put("BENCH/KST/CONF/MG90S.TXT", kst,
            kst_file_format(&img, KST_TYPE_V6, kst, sizeof(kst)));
        put("BENCH/ESC/CONF/YGE-135.JSON", k_profile, sizeof(k_profile) - 1u);
    } else {
        fk_file(c, "BENCH/ESC/CONF/YGE-135.JSON", NULL, 2140u);
        fk_file(c, "BENCH/KST/CONF/MG90S.TXT", NULL, 1873u);
        fk_file(c, "USER/LOGS/ATEST005.TXT", NULL, 2210u);
        fk_file(c, "USER/LOGS/LIMIT006.TXT", NULL, 1873u);
    }
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
        card(strncmp(view, "files-view", 10) == 0
             || strncmp(view, "files-edit", 10) == 0
             || is(view, "logs-report"));
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
    s_fh.armed = render_files_armed(view);
    s_fh.volume = nocard ? NULL : "RCBENCH";
    if (is(view, "files-logging")) {
        s_fh.marks.test = 5u;       /* USER/LOGS/ATEST005.CSV is open */
    }
    if (is(view, "files-view-binary")) {
        put("DATA.TXT", "rcbench\0\1\2", 10u);
    } else if (is(view, "files-view-mixed")) {
        static const char mixed[] =
            "Notes from the bench, written on two computers.\r\n"
            "This line ends with CR LF.\r\n"
            "This one ends with LF alone.\n";
        put("MIXED.TXT", mixed, sizeof(mixed) - 1u);
    } else if (is(view, "files-view-empty")) {
        put("EMPTY.TXT", "", 0u);
    }
    s_fh.defer = is(view, "files-reading");
    const files_io_t io = fh_files_io(&s_fh);
    files_screen_set_io(&io);
}

bool render_files_armed(const char *view)
{
    return is(view, "files-armed") || is(view, "files-edit-armed");
}

void render_files_settle(void)
{
    settle();
}

/**
 * @brief Tap FILES into a view of the text viewer.
 * @param view  the view's name, "files-view..."
 */
static void pose_viewer(const char *view)
{
    if (is(view, "files-view-kst") || is(view, "files-view-json")) {
        enter_dir(0, "BENCH");
        enter_dir(0, is(view, "files-view-kst") ? "KST" : "ESC");
        enter_dir(0, "CONF");
        pick(0, is(view, "files-view-kst") ? "MG90S.TXT" : "YGE-135.JSON");
    } else if (is(view, "files-view-binary")) {
        pick(0, "DATA.TXT");
    } else if (is(view, "files-view-mixed")) {
        pick(0, "MIXED.TXT");
    } else if (is(view, "files-view-empty")) {
        pick(0, "EMPTY.TXT");
    } else if (is(view, "files-view-toolarge")) {
        pick(0, "WIRING.JPG");      /* 3.2MB, no file LOGS opens */
    } else {
        pick(1, "ATEST005.TXT");
    }
    s_fh.defer = is(view, "files-view-reading");
    foot(FS_SPOT_OPEN);
    if (is(view, "files-view-small")) {
        ftap(FS_SPOT_TV_SMALL_CX, FS_SPOT_TV_FOOT_CY);
        ftap(FS_SPOT_TV_PGDN_CX, FS_SPOT_TV_FOOT_CY);
    }
}

/** @brief A key of the editor's keyboard, on its area under 8 rows. */
static void ed_key(int k)
{
    ui_textkey_t kb;
    memset(&kb, 0, sizeof(kb));
    ui_textkey_open_editor(&kb, gfx_rect_make(FS_SPOT_TV_KB_X,
                                              FS_SPOT_TV_KB_Y,
                                              FS_SPOT_TV_KB_W,
                                              FS_SPOT_TV_KB_H));
    const gfx_rect_t r = ui_textkey_key_rect(&kb, k);
    ftap(r.x + r.w / 2, r.y + r.h / 2);
}

/**
 * @brief Type @p text on the editor's keyboard: every printable ASCII
 *        character, spaces and line feeds, the layers switched as needed
 *        and left on the small letters.
 * @param text  the characters
 */
static void ed_type(const char *text)
{
    ui_textkey_layer_t layer = UI_TK_LOWER;
    ui_textkey_layer_t want = UI_TK_LOWER;
    for (const char *p = text; *p != '\0'; ++p) {
        if (*p == ' ' || *p == '\n') {
            ed_key(*p == ' ' ? UI_TK_SPACE : UI_TK_ENTER);
            continue;
        }
        const int at = ui_textkey_editor_key(*p, &want);
        if (at < 0) {
            fprintf(stderr, "render_files: no key %c\n", *p);
            exit(2);
        }
        for (int k = ui_textkey_editor_step(layer, want, &layer); k >= 0;
             k = ui_textkey_editor_step(layer, want, &layer)) {
            ed_key(k);
        }
        ed_key(at);
    }
    for (int k = ui_textkey_editor_step(layer, UI_TK_LOWER, &layer); k >= 0;
         k = ui_textkey_editor_step(layer, UI_TK_LOWER, &layer)) {
        ed_key(k);
    }
}

/**
 * @brief Open @p name of @p dir in the viewer as OPEN does, and EDIT.
 * @param dir   the folder
 * @param name  the file
 */
static void edit_file(const char *dir, const char *name)
{
    char path[160];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    const fk_node_t *n = fk_get(&s_fh.card, path);
    (void)files_screen_open_text(dir, name, s_fh.card.mount,
                                 (n != NULL) ? n->size : 0u, false);
    settle();
    ftap(FS_SPOT_TV_EDIT_CX, FS_SPOT_TV_FOOT_CY);
}

/** @brief A tap on the editor's footer button whose middle is @p cx. */
static void ed_foot(int cx)
{
    ftap(cx, FS_SPOT_TV_FOOT_CY);
}

/**
 * @brief Tap FILES into a view of the text editor.
 * @param view  the view's name, "files-edit..."
 */
static void pose_editor(const char *view)
{
    const bool de = ui_text_language() == UI_LANG_DE;
    if (is(view, "files-edit-kst")) {
        edit_file("BENCH/KST/CONF", "MG90S.TXT");
        ftap(FS_SPOT_TV_TEXT_X + 90 * 10, FS_SPOT_TV_TEXT_Y + 5);
        ed_type(de ? "\n# Ersatzservo, Ruder links" : "\n# spare servo, left aileron");
        ed_key(UI_TK_DONE);
        ed_foot(FS_SPOT_TV_SAVE_CX);
        return;
    }
    if (is(view, "files-edit-esc")) {
        edit_file("BENCH/ESC/CONF", "YGE-135.JSON");
        ed_type(" ");
        ed_key(UI_TK_DONE);
        ed_foot(FS_SPOT_TV_SAVE_CX);
        return;
    }
    if (is(view, "files-edit-nul")) {
        /* A NUL at 4,096: the first byte deleted brings it into the 4kB
         * an open tests, and SAVE refuses. */
        static char nul[4100];
        for (size_t i = 0; i < 4096u; ++i) {
            nul[i] = (i % 40u == 39u) ? '\n' : 'x';
        }
        memcpy(nul + 4096, "\0yz\n", 4u);
        put("USER/LOGS/NUL.TXT", nul, sizeof(nul));
        edit_file("USER/LOGS", "NUL.TXT");
        ed_key(UI_TK_RIGHT);
        ed_key(UI_TK_DEL);
        ed_key(UI_TK_DONE);
        ed_foot(FS_SPOT_TV_SAVE_CX);
        return;
    }
    if (is(view, "files-edit-full") || is(view, "files-edit-lines")) {
        static char big[1048576];
        const bool full = is(view, "files-edit-full");
        const size_t n = full ? sizeof(big) : 65536u;
        for (size_t i = 0; i < n; ++i) {
            big[i] = (!full || i % 64u == 63u) ? '\n' : 'a';
        }
        put("USER/LOGS/BIG.TXT", big, n);
        edit_file("USER/LOGS", "BIG.TXT");
        ed_type(full ? "q" : "\n");
        return;
    }
    /* files-edit-see25 and files-edit-see50: Keyboard see-through at
     * 25% and 50%, the keyboard open. */
    const bool see = strncmp(view, "files-edit-see", 14) == 0;
    if (see) {
        settings_set(SET_KB_SEE, is(view, "files-edit-see25") ? 1.0f : 2.0f);
    }
    edit_file("USER/LOGS", "ATEST005.TXT");
    /* After the result line: a line of the operator's own. */
    ftap(FS_SPOT_TV_TEXT_X + 90 * 10, FS_SPOT_TV_TEXT_Y + FS_SPOT_TV_PITCH + 5);
    ed_type(de ? "\nNotiz: Horn vor dem Lauf neu montiert."
               : "\nNote: horn refitted before the run.");
    if (is(view, "files-edit") || see) {
        return;                         /* the keyboard open */
    }
    if (is(view, "files-edit-marks")) {
        ed_key(UI_TK_LAYER);            /* the digits and the first marks */
        return;
    }
    ed_key(UI_TK_DONE);
    if (is(view, "files-edit-unsaved")) {
        (void)files_screen()->back();   /* the home tag: the question */
    } else if (is(view, "files-edit-revert")) {
        ed_foot(FS_SPOT_TV_REVERT_CX);
    } else if (is(view, "files-edit-open")) {
        (void)files_screen_open_text("", "NOTES.TXT", s_fh.card.mount, 512u,
                                     false);
    } else if (is(view, "files-edit-saving")) {
        s_fh.defer = true;
        ed_foot(FS_SPOT_TV_SAVE_CX);
    } else if (is(view, "files-edit-card")) {
        fk_fail(&s_fh.card, FK_WRITE, 1, FM_ERR_CARD_GONE);
        ed_foot(FS_SPOT_TV_SAVE_CX);
    } else if (is(view, "files-edit-saved")) {
        ed_foot(FS_SPOT_TV_SAVE_CX);
    } else if (is(view, "files-edit-changed")) {
        /* The Wi-Fi page wrote the report since the editor read it: SAVE
         * finds it changed and asks STAY, READ AGAIN or OVERWRITE. */
        const fm_io_t io = fk_io(&s_fh.card);
        (void)io.unlink(io.ctx, "USER/LOGS/ATEST005.TXT");
        put("USER/LOGS/ATEST005.TXT", "web\n", 4u);
        ed_foot(FS_SPOT_TV_SAVE_CX);
    } else if (is(view, "files-edit-nochange")) {
        ed_foot(FS_SPOT_TV_REVERT_CX);
        ftap(FS_SPOT_TV_DISCARD_CX, FS_SPOT_TV_Q_CY);
        ed_foot(FS_SPOT_TV_SAVE_CX);
    }
    /* files-edit-armed: the text unsaved, SAVE shut by the gate. */
}

void render_files_pose(const char *view)
{
    settle();
    if (strncmp(view, "files-view", 10) == 0) {
        pose_viewer(view);
        return;
    }
    if (strncmp(view, "files-edit", 10) == 0) {
        pose_editor(view);
        return;
    }
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
