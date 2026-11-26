/**
 * @file
 * @brief A scan's look-up on the card (servo_look.h) and its answer to
 *        the scan flow (scan_flow_answer()): the newest run's DUT NAME
 *        from a file the bench writes, and the cases that leave DUT NAME.
 *
 * The failures this is about: a known servo filed under the name typed
 * for the servo before it; a run other than the newest naming the servo;
 * a first data row cut by the read giving a cut name; an answer that
 * lands after another scan or under a run and changes DUT NAME there.
 *
 * The card is a list in memory; the hook answers as the panel's does
 * (panel_servo.c): unknown at once, the card's answer later.
 *
 * SPDX-License-Identifier: MIT
 */
#include "nav_rig.h"

#include "log_info.h"
#include "run_info.h"
#include "scan_flow.h"
#include "servo_active.h"
#include "servo_look.h"
#include "servo_test.h"

/* ------------------------------------------------------------ the card */

/** @brief One file or folder of the card in memory. */
typedef struct {
    const char *path;   /**< from the card's root          */
    uint32_t    stamp;  /**< FAT stamp                      */
    const char *text;   /**< the content; NULL for a folder */
} mem_file_t;

/** @brief The card: its entries, and a folder that does not read. */
static mem_file_t  s_files[8];
static int         s_count;
static const char *s_broken;
static int         s_reads;

/** @brief No card entries, no broken folder. */
static void card_clear(void)
{
    s_count = 0;
    s_broken = NULL;
    s_reads = 0;
}

/** @brief Add an entry. */
static void card_add(const char *path, uint32_t stamp, const char *text)
{
    if (s_count >= (int)(sizeof(s_files) / sizeof(s_files[0]))) {
        T_FAIL("card full at %s", path);
        return;
    }
    s_files[s_count++] = (mem_file_t){ path, stamp, text };
}

/** @brief servo_look_io_t's list over the card in memory. */
static int mem_list(void *ctx, const char *dir, servo_look_visit_fn visit,
                    void *walk)
{
    (void)ctx;
    if (s_broken != NULL && strcmp(dir, s_broken) == 0) {
        return -1;
    }
    const size_t k = strlen(dir);
    bool there = false;
    int n = 0;
    for (int i = 0; i < s_count; ++i) {
        const char *p = s_files[i].path;
        if (strcmp(p, dir) == 0) {
            there = true;
        } else if (strncmp(p, dir, k) == 0 && p[k] == '/') {
            there = true;
            visit(walk, p + k + 1u, s_files[i].text == NULL, s_files[i].stamp);
            ++n;
        }
    }
    return there ? n : SERVO_LOOK_LIST_NO_DIR;
}

/** @brief servo_look_io_t's read over the card in memory. */
static size_t mem_read(void *ctx, const char *path, char *buf, size_t n)
{
    (void)ctx;
    ++s_reads;
    for (int i = 0; i < s_count; ++i) {
        if (s_files[i].text != NULL && strcmp(s_files[i].path, path) == 0) {
            const size_t len = strlen(s_files[i].text);
            const size_t got = len < n ? len : n;
            memcpy(buf, s_files[i].text, got);
            return got;
        }
    }
    return 0u;
}

/** @brief The card's io. */
static const servo_look_io_t k_io = { .list = mem_list, .read = mem_read };

/** @brief The room for the read. */
static char s_buf[SERVO_LOOK_READ];

/** @brief servo_look() of @p id into @p name. */
static servo_look_t look_up(const char *id, char *name, size_t n)
{
    return servo_look(&k_io, id, name, n, s_buf, sizeof(s_buf));
}

/** @brief A CSV as AUTOMATIC TEST writes it: its header, and a first row
 *         whose info cell carries DUT NAME @p dut (run_info_servo()). */
static void test_csv(char *out, size_t n, const char *dut)
{
    settings_set_text(SET_TEXT_DUT_NAME, dut);
    char cell[LOG_INFO_MAX];
    run_info_servo(cell, sizeof(cell), "KST X10", "INA3221", "", "X");
    const char *head = servo_test_csv_header();
    int cols = 1;
    for (const char *c = head; *c != '\0'; ++c) {
        cols += (*c == ';') ? 1 : 0;
    }
    size_t at = (size_t)snprintf(out, n, "%s\n", head);
    for (int i = 0; i < cols - 1 && at < n; ++i) {
        at += (size_t)snprintf(out + at, n - at, "0.25;");
    }
    (void)snprintf(out + at, n - at, "%s\n0.50;\n", cell);
}

/* ------------------------------------------------------------- the flow */

/** @brief The id the hook was asked last. */
static char s_asked[32];

/** @brief The hook as the panel's: no answer at once. */
static scan_card_t later(const char *id, char *name, size_t n)
{
    (void)name;
    (void)n;
    (void)snprintf(s_asked, sizeof(s_asked), "%s", id);
    return SCAN_CARD_UNKNOWN;
}

/** @brief The logger task's part: the look-up of the id asked, handed to
 *         the flow as panel_servo_frame() hands it. */
static bool answer(void)
{
    char name[LOG_INFO_TEXT_MAX];
    const servo_look_t r = look_up(s_asked, name, sizeof(name));
    return scan_flow_answer(s_asked, scan_flow_card_of(r), name);
}

/** @brief A case's start: the menu up, no servo, the panel's hook. */
static uint32_t start(void)
{
    static uint32_t t = 7000000u;
    settings_set_store(NULL);
    settings_init();
    fresh();
    to_overview();
    scan_flow_reset();
    (void)servo_active_clear(false);
    ui_router_set_alert(NULL);
    card_clear();
    s_asked[0] = '\0';
    scan_flow_set_look(later);
    t += 100000u;
    return t;
}

/** @brief The alert band's text, "" for none. */
static const char *notice(void)
{
    const char *a = ui_router_alert();
    return a != NULL ? a : "";
}

/* ----------------------------------------------------------- the look */

TEST_CASE(the_newest_run_by_stamp_names_the_servo)
{
    (void)start();
    static char a[1024], b[1024], c[1024];
    test_csv(a, sizeof(a), "OLDER");
    test_csv(b, sizeof(b), "NEWEST");
    test_csv(c, sizeof(c), "HIGHER NUMBER");
    card_add("Servos/KST_X10-0000A", 0u, NULL);
    card_add("Servos/KST_X10-0000A/automatic test_000.csv", 0x5B7A0000u, a);
    card_add("Servos/KST_X10-0000A/limit run_001.csv", 0x5B7B0000u, b);
    card_add("Servos/KST_X10-0000A/automatic test_007.csv", 0x5B790000u, c);
    /* A report is no run: its name is not read. */
    card_add("Servos/KST_X10-0000A/limit run_002.txt", 0x5B7C0000u, "x");
    char name[LOG_INFO_TEXT_MAX];
    CHECK_EQ(look_up("KST_X10-0000A", name, sizeof(name)), SERVO_LOOK_RUNS);
    CHECK_STR_EQ(name, "NEWEST");
    CHECK_EQ(s_reads, 1);
}

TEST_CASE(equal_stamps_go_to_the_higher_number)
{
    (void)start();
    static char a[1024], b[1024];
    test_csv(a, sizeof(a), "RUN 3");
    test_csv(b, sizeof(b), "RUN 12");
    card_add("Servos/KST_X10-0000A/servo run_003.csv", 0x00210000u, a);
    card_add("Servos/KST_X10-0000A/servo run_012.csv", 0x00210000u, b);
    char name[LOG_INFO_TEXT_MAX];
    CHECK_EQ(look_up("KST_X10-0000A", name, sizeof(name)), SERVO_LOOK_RUNS);
    CHECK_STR_EQ(name, "RUN 12");
}

TEST_CASE(no_folder_no_runs_and_a_folder_that_does_not_read)
{
    (void)start();
    char name[LOG_INFO_TEXT_MAX] = "X";
    CHECK_EQ(look_up("KST_X10-0000A", name, sizeof(name)),
             SERVO_LOOK_NO_FOLDER);
    CHECK_STR_EQ(name, "");
    card_add("Servos/KST_X10-0000B", 0u, NULL);
    card_add("Servos/KST_X10-0000B/notes.csv", 1u, "info\nx\n");
    CHECK_EQ(look_up("KST_X10-0000B", name, sizeof(name)), SERVO_LOOK_NO_RUNS);
    s_broken = "Servos/KST_X10-0000B";
    CHECK_EQ(look_up("KST_X10-0000B", name, sizeof(name)),
             SERVO_LOOK_UNREADABLE);
    CHECK_EQ(look_up("not an id", name, sizeof(name)), SERVO_LOOK_NO_FOLDER);
    CHECK_EQ(s_reads, 0);
}

TEST_CASE(a_row_the_read_cuts_gives_no_name)
{
    /* The row ends 1 byte past the read: no name, never a cut one. */
    char text[SERVO_LOOK_READ + 64u];
    const char *head = "time (s);info\n";
    const size_t h = strlen(head);
    memcpy(text, head, h);
    const char *cell = ";dut=LEFT AILERON\n";
    const size_t pad_n = SERVO_LOOK_READ + 1u - h - strlen(cell);
    memset(text + h, '1', pad_n);
    memcpy(text + h + pad_n, cell, strlen(cell) + 1u);
    char name[LOG_INFO_TEXT_MAX];
    CHECK(!servo_look_info(text, SERVO_LOOK_READ, name, sizeof(name)));
    CHECK_STR_EQ(name, "");
    /* One byte more and it ends inside. */
    CHECK(servo_look_info(text, SERVO_LOOK_READ + 1u, name, sizeof(name)));
    CHECK_STR_EQ(name, "LEFT AILERON");
}

TEST_CASE(the_info_column_is_found_by_its_header_and_blank_rows_skipped)
{
    char name[LOG_INFO_TEXT_MAX];
    const char *t = "a;  info ;b\r\n;;\r\n1; dut=WING&note=N ;2\r\n";
    CHECK(servo_look_info(t, strlen(t), name, sizeof(name)));
    CHECK_STR_EQ(name, "WING");
    const char *none = "a;b\n1;2\n";
    CHECK(!servo_look_info(none, strlen(none), name, sizeof(name)));
    const char *short_row = "a;info\n1\n";
    CHECK(!servo_look_info(short_row, strlen(short_row), name, sizeof(name)));
    const char *no_row = "a;info\n";
    CHECK(!servo_look_info(no_row, strlen(no_row), name, sizeof(name)));
    CHECK(!servo_look_info("a;info", 6u, name, sizeof(name)));
    CHECK(!servo_look_info(NULL, 0u, name, sizeof(name)));
}

/* ---------------------------------------------------------- the answer */

TEST_CASE(a_known_ids_scan_sets_dut_name_from_the_card)
{
    const uint32_t t = start();
    static char csv[1024];
    test_csv(csv, sizeof(csv), "LEFT AILERON");
    card_add("Servos/KST_X10-0000A/automatic test_000.csv", 0x5B7A0000u, csv);
    settings_set_text(SET_TEXT_DUT_NAME, "OLD NAME");
    CHECK(scan_flow_line("KST_X10-0000A", false, t));
    CHECK_STR_EQ(s_asked, "KST_X10-0000A");
    /* Before the answer: the id alone, DUT NAME as it was. */
    CHECK_STR_EQ(notice(), "SERVO KST_X10-0000A");
    CHECK_STR_EQ(settings_text(SET_TEXT_DUT_NAME), "OLD NAME");
    CHECK(answer());
    CHECK_STR_EQ(notice(), "SERVO KST_X10-0000A  LEFT AILERON");
    CHECK_STR_EQ(settings_text(SET_TEXT_DUT_NAME), "LEFT AILERON");
    /* Each answer is taken once. */
    CHECK(!answer());
}

TEST_CASE(an_unknown_ids_scan_leaves_dut_name)
{
    const uint32_t t = start();
    settings_set_text(SET_TEXT_DUT_NAME, "OLD NAME");
    CHECK(scan_flow_line("KST_X10-0000A", false, t));
    CHECK(answer());
    CHECK_STR_EQ(notice(), "SERVO KST_X10-0000A  NEW ON THIS CARD");
    CHECK_STR_EQ(settings_text(SET_TEXT_DUT_NAME), "OLD NAME");
    /* A folder NEW ID made, no run in it yet: new as well. */
    card_add("Servos/KST_X10-0000B", 0u, NULL);
    CHECK(scan_flow_line("KST_X10-0000B", false, t + 10u));
    CHECK(answer());
    CHECK_STR_EQ(notice(), "SERVO KST_X10-0000B  NEW ON THIS CARD");
    CHECK_STR_EQ(settings_text(SET_TEXT_DUT_NAME), "OLD NAME");
    /* Runs without a name, and a folder that does not read: the id alone. */
    card_add("Servos/KST_X10-0000B/servo run_000.csv", 1u, "a;info\n1;\n");
    CHECK(scan_flow_line("KST_X10-0000C", false, t + 20u));
    s_broken = "Servos/KST_X10-0000C";
    CHECK(!answer());
    CHECK_STR_EQ(notice(), "SERVO KST_X10-0000C");
    CHECK(scan_flow_line("KST_X10-0000B", false, t + 30u));
    CHECK(answer());
    CHECK_STR_EQ(notice(), "SERVO KST_X10-0000B");
    CHECK_STR_EQ(settings_text(SET_TEXT_DUT_NAME), "OLD NAME");
}

TEST_CASE(a_late_answer_after_another_scan_or_under_a_run_is_dropped)
{
    const uint32_t t = start();
    static char csv[1024];
    test_csv(csv, sizeof(csv), "LEFT AILERON");
    card_add("Servos/KST_X10-0000A/limit run_000.csv", 5u, csv);
    settings_set_text(SET_TEXT_DUT_NAME, "OLD NAME");
    CHECK(scan_flow_line("KST_X10-0000A", false, t));
    CHECK(scan_flow_line("KST_X10-0000B", false, t + 10u));
    /* The answer for A comes after B was set: dropped. */
    CHECK(!scan_flow_answer("KST_X10-0000A", SCAN_CARD_KNOWN, "LEFT AILERON"));
    CHECK_STR_EQ(settings_text(SET_TEXT_DUT_NAME), "OLD NAME");
    /* A run starts before the answer: DUT NAME stays under the run. */
    CHECK(scan_flow_line("KST_X10-0000A", false, t + 20u));
    ui_bench_status_t st = *ui_router_status();
    st.armed = true;
    st.armed_roles = UI_ARMED_SERVO;
    ui_router_set_status(&st);
    CHECK(!answer());
    CHECK_STR_EQ(settings_text(SET_TEXT_DUT_NAME), "OLD NAME");
    st.armed = false;
    st.armed_roles = 0u;
    ui_router_set_status(&st);
    /* A refused or repeated line keeps the answer due. */
    CHECK(scan_flow_line("KST_X10-0000B", false, t + 30u));
    CHECK(scan_flow_line("KST_X10-0000B", false, t + 40u));
    CHECK_EQ(scan_flow_last(), SERVO_SCAN_REPEAT);
    CHECK(scan_flow_line("bad", false, t + 50u));
    CHECK(scan_flow_answer("KST_X10-0000B", SCAN_CARD_NEW, NULL));
    CHECK_STR_EQ(notice(), "SERVO KST_X10-0000B  NEW ON THIS CARD");
    /* No card at once: nothing is due. */
    CHECK(!scan_flow_answer("KST_X10-0000B", SCAN_CARD_NEW, ""));
    CHECK(!scan_flow_answer(NULL, SCAN_CARD_NEW, ""));
    CHECK_EQ(scan_flow_card_of(SERVO_LOOK_UNREADABLE), SCAN_CARD_UNKNOWN);
}

int main(void)
{
    RUN(the_newest_run_by_stamp_names_the_servo);
    RUN(equal_stamps_go_to_the_higher_number);
    RUN(no_folder_no_runs_and_a_folder_that_does_not_read);
    RUN(a_row_the_read_cuts_gives_no_name);
    RUN(the_info_column_is_found_by_its_header_and_blank_rows_skipped);
    RUN(a_known_ids_scan_sets_dut_name_from_the_card);
    RUN(an_unknown_ids_scan_leaves_dut_name);
    RUN(a_late_answer_after_another_scan_or_under_a_run_is_dropped);
    return test_summary("scan_look");
}
