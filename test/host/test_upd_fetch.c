/**
 * @file test_upd_fetch.c
 * @brief UPDATES' words for an image fetched on demand (upd_fetch.c): the
 *        control's label in each state the panel reaches, the card's line
 *        for each place the image comes from, the failure lines with their
 *        codes, and the reasons of a card read and of the checks.
 *
 * Cases:
 * - the_label_follows_the_download_the_check_and_the_card_read
 * - a_hold_that_reads_the_card_says_so_and_a_download_keeps_the_rows_label
 * - the_cards_line_names_the_file_and_where_the_hold_takes_it
 * - an_unsigned_image_warns_with_its_reason_and_a_signed_one_does_not
 * - each_failure_line_carries_its_kind_and_code
 * - each_card_answer_has_its_reason
 * - each_check_refusal_has_its_reason
 * - the_words_follow_the_language
 * - a_null_or_empty_buffer_is_left_alone
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "upd_fetch.h"
#include "ui_text.h"

/** @brief A view of @p src and @p run naming @p file. */
static upd_fetch_t view(devimg_src_t src, upd_fetch_run_t run,
                        const char *file)
{
    upd_fetch_t f;
    memset(&f, 0, sizeof(f));
    f.src = (uint8_t)src;
    f.run = (uint8_t)run;
    snprintf(f.file, sizeof(f.file), "%s", file);
    return f;
}

#define DIAL "rcbench-dial-0.23.16.bin"
#define CARD "BENCH/UPDATES/rcbench-dial-0.23.16.bin"

TEST_CASE(the_label_follows_the_download_the_check_and_the_card_read)
{
    ui_text_set_language(UI_LANG_EN);
    char out[64];
    /* The download's share, 0 and 100, and a share past 100 held there. */
    upd_fetch_t f = view(DEVIMG_SRC_NET, UPD_FETCH_NET, DIAL);
    static const struct { uint8_t pct; const char *want; } k_pct[] = {
        { 0u, "DOWNLOADING 0%" },
        { 43u, "DOWNLOADING 43%" },
        { 100u, "DOWNLOADING 100%" },
        { 101u, "DOWNLOADING 100%" },
        { 255u, "DOWNLOADING 100%" },
    };
    for (unsigned i = 0; i < sizeof(k_pct) / sizeof(k_pct[0]); ++i) {
        f.percent = k_pct[i].pct;
        CHECK(upd_fetch_label(&f, out, sizeof(out)));
        CHECK_STR_EQ(out, k_pct[i].want);
    }
    /* The downloaded file's checks (SHA-256, signature, the senders'). */
    f = view(DEVIMG_SRC_NET, UPD_FETCH_CHECK, DIAL);
    CHECK(upd_fetch_label(&f, out, sizeof(out)));
    CHECK_STR_EQ(out, "CHECKING THE FILE");
    /* The card's read and its checks, whatever the next hold's place. */
    f = view(DEVIMG_SRC_NET, UPD_FETCH_CARD, DIAL);
    CHECK(upd_fetch_label(&f, out, sizeof(out)));
    CHECK_STR_EQ(out, "READING THE SD CARD");
    f.src = DEVIMG_SRC_CARD;
    CHECK(upd_fetch_label(&f, out, sizeof(out)));
    CHECK_STR_EQ(out, "READING THE SD CARD");
    /* A run outside the enum is no run. */
    f = view(DEVIMG_SRC_NET, (upd_fetch_run_t)9, DIAL);
    snprintf(out, sizeof(out), "%s", "untouched");
    CHECK(!upd_fetch_label(&f, out, sizeof(out)));
    CHECK_STR_EQ(out, "untouched");
}

TEST_CASE(a_hold_that_reads_the_card_says_so_and_a_download_keeps_the_rows_label)
{
    ui_text_set_language(UI_LANG_EN);
    char out[64];
    /* No home network, or a failed download: the hold reads the card. */
    upd_fetch_t f = view(DEVIMG_SRC_CARD, UPD_FETCH_IDLE, CARD);
    CHECK(upd_fetch_label(&f, out, sizeof(out)));
    CHECK_STR_EQ(out, "HOLD 2s TO UPDATE FROM THE SD CARD");
    /* A hold that downloads, or one whose image is in memory: the row's
     * own label (HOLD 2s TO UPDATE THE DIAL) stands. */
    snprintf(out, sizeof(out), "%s", "row");
    f = view(DEVIMG_SRC_NET, UPD_FETCH_IDLE, DIAL);
    CHECK(!upd_fetch_label(&f, out, sizeof(out)));
    f = view(DEVIMG_SRC_RAM, UPD_FETCH_IDLE, DIAL);
    CHECK(!upd_fetch_label(&f, out, sizeof(out)));
    f.nosig = true;
    CHECK(!upd_fetch_label(&f, out, sizeof(out)));
    CHECK_STR_EQ(out, "row");
    /* All of it zero is a hold that downloads. */
    memset(&f, 0, sizeof(f));
    CHECK(!upd_fetch_label(&f, out, sizeof(out)));
}

TEST_CASE(the_cards_line_names_the_file_and_where_the_hold_takes_it)
{
    ui_text_set_language(UI_LANG_EN);
    char out[160];
    upd_fetch_t f = view(DEVIMG_SRC_NET, UPD_FETCH_IDLE, DIAL);
    upd_fetch_line(&f, out, sizeof(out));
    CHECK_STR_EQ(out, "The hold downloads " DIAL " from GitHub first.");
    CHECK(!upd_fetch_warn(&f));
    f = view(DEVIMG_SRC_CARD, UPD_FETCH_IDLE, CARD);
    upd_fetch_line(&f, out, sizeof(out));
    CHECK_STR_EQ(out, "The hold reads " CARD " from the SD card.");
    CHECK(upd_fetch_warn(&f));
    f = view(DEVIMG_SRC_RAM, UPD_FETCH_IDLE, DIAL);
    upd_fetch_line(&f, out, sizeof(out));
    CHECK_STR_EQ(out, DIAL " is downloaded and checked.");
    CHECK(!upd_fetch_warn(&f));
    /* A place outside the enum reads as a download. */
    f = view((devimg_src_t)7, UPD_FETCH_IDLE, DIAL);
    upd_fetch_line(&f, out, sizeof(out));
    CHECK_STR_EQ(out, "The hold downloads " DIAL " from GitHub first.");
    CHECK(!upd_fetch_warn(&f));
    /* No file: no line. */
    f = view(DEVIMG_SRC_CARD, UPD_FETCH_IDLE, "");
    snprintf(out, sizeof(out), "%s", "stale");
    upd_fetch_line(&f, out, sizeof(out));
    CHECK_STR_EQ(out, "");
    upd_fetch_line(NULL, out, sizeof(out));
    CHECK_STR_EQ(out, "");
    CHECK(!upd_fetch_warn(NULL));
}

TEST_CASE(an_unsigned_image_warns_with_its_reason_and_a_signed_one_does_not)
{
    ui_text_set_language(UI_LANG_EN);
    char out[160];
    /* Signed (or a build that checked none: nosig false). */
    upd_fetch_t f = view(DEVIMG_SRC_RAM, UPD_FETCH_IDLE, DIAL);
    upd_fetch_line(&f, out, sizeof(out));
    CHECK_STR_EQ(out, DIAL " is downloaded and checked.");
    /* No .sig beside it. */
    f.nosig = true;
    upd_fetch_line(&f, out, sizeof(out));
    CHECK_STR_EQ(out, DIAL ": not signed by the rcbench release key. "
                      "The image comes with no signature.");
    CHECK(upd_fetch_warn(&f));
    /* A .sig that does not verify, or is out of form. */
    f.sigbad = true;
    upd_fetch_line(&f, out, sizeof(out));
    CHECK_STR_EQ(out, DIAL ": not signed by the rcbench release key. "
                      "The signature does not match.");
    /* nosig belongs to the image in memory: a hold that downloads or
     * reads the card again shows its own line. */
    f.src = DEVIMG_SRC_NET;
    upd_fetch_line(&f, out, sizeof(out));
    CHECK_STR_EQ(out, "The hold downloads " DIAL " from GitHub first.");
    CHECK(!upd_fetch_warn(&f));
    /* The band's line once it arrives. */
    upd_fetch_nosig_note(DIAL, out, sizeof(out));
    CHECK_STR_EQ(out, DIAL " is not signed by the rcbench release key.");
    upd_fetch_nosig_note(NULL, out, sizeof(out));
    CHECK_STR_EQ(out, " is not signed by the rcbench release key.");
    /* Refused while the build enforces it (V07). */
    upd_fetch_check_text(DEVIMG_E_SIG, NULL, NULL, out, sizeof(out));
    CHECK_STR_EQ(out, "The file is not signed by the rcbench release key.");
}

TEST_CASE(each_failure_line_carries_its_kind_and_code)
{
    ui_text_set_language(UI_LANG_EN);
    char out[160];
    upd_fetch_fail_line(UPD_FETCH_FAIL_NET, "D10", "No connection to GitHub.",
                        out, sizeof(out));
    CHECK_STR_EQ(out, "Download failed (D10): No connection to GitHub.");
    upd_fetch_fail_line(UPD_FETCH_FAIL_NET, "D14",
                        "The SHA-256 checksum does not match SHA256SUMS.",
                        out, sizeof(out));
    CHECK_STR_EQ(out, "Download failed (D14): The SHA-256 checksum does not "
                      "match SHA256SUMS.");
    upd_fetch_fail_line(UPD_FETCH_FAIL_CARD, "S02", "x is not on the SD card.",
                        out, sizeof(out));
    CHECK_STR_EQ(out, "SD card failed (S02): x is not on the SD card.");
    upd_fetch_fail_line(UPD_FETCH_FAIL_CHECK, "V07",
                        "The file is not signed by the rcbench release key.",
                        out, sizeof(out));
    CHECK_STR_EQ(out, "Check failed (V07): The file is not signed by the "
                      "rcbench release key.");
    /* A kind outside the enum is a download's; no code, no reason. */
    upd_fetch_fail_line((upd_fetch_fail_t)9, NULL, NULL, out, sizeof(out));
    CHECK_STR_EQ(out, "Download failed (): ");
    /* Cut at the buffer, terminated. */
    char small[16];
    upd_fetch_fail_line(UPD_FETCH_FAIL_CHECK, "V05", "long reason", small,
                        sizeof(small));
    CHECK_EQ(strlen(small), sizeof(small) - 1u);
    CHECK_EQ(strncmp(small, "Check failed (V", 15), 0);
}

TEST_CASE(each_card_answer_has_its_reason)
{
    ui_text_set_language(UI_LANG_EN);
    static const struct { devimg_card_t e; const char *want; } k[] = {
        { DEVIMG_CARD_NONE,    "No SD card." },
        { DEVIMG_CARD_NO_FILE, CARD " is not on the SD card." },
        { DEVIMG_CARD_READ,    "The SD card could not be read." },
        { DEVIMG_CARD_SIZE,
          "The file is empty or larger than the device's slot." },
        { DEVIMG_CARD_MEMORY,  "No memory for the file." },
        { DEVIMG_CARD_SUMS,    "SHA256SUMS on the SD card is out of form." },
        /* OK reaches no failure line; it and any other value read as a
         * read that failed. */
        { DEVIMG_CARD_OK,      "The SD card could not be read." },
        { (devimg_card_t)42,   "The SD card could not be read." },
    };
    char out[128];
    for (unsigned i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        upd_fetch_card_text(k[i].e, CARD, out, sizeof(out));
        CHECK_STR_EQ(out, k[i].want);
    }
    upd_fetch_card_text(DEVIMG_CARD_NO_FILE, NULL, out, sizeof(out));
    CHECK_STR_EQ(out, " is not on the SD card.");
}

TEST_CASE(each_check_refusal_has_its_reason)
{
    ui_text_set_language(UI_LANG_EN);
    static const struct { devimg_err_t e; const char *want; } k[] = {
        { DEVIMG_E_SIZE,
          "The file is empty or larger than the device's slot." },
        { DEVIMG_E_HASH, "The SHA-256 checksum does not match SHA256SUMS." },
        { DEVIMG_E_ROLE, "The file is the firmware of another device." },
        { DEVIMG_E_VERSION,
          "The file is version 0.23.15; this release has 0.23.16." },
        { DEVIMG_E_SIG, "The file is not signed by the rcbench release key." },
        { DEVIMG_E_CHECK, "The file is no firmware for this device." },
        { DEVIMG_E_ID, "The file is no firmware for this device." },
        { DEVIMG_OK, "The file is no firmware for this device." },
    };
    char out[128];
    for (unsigned i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        upd_fetch_check_text(k[i].e, "0.23.15", "0.23.16", out, sizeof(out));
        CHECK_STR_EQ(out, k[i].want);
    }
    upd_fetch_check_text(DEVIMG_E_VERSION, NULL, NULL, out, sizeof(out));
    CHECK_STR_EQ(out, "The file is version ; this release has .");
}

TEST_CASE(the_words_follow_the_language)
{
    ui_text_set_language(UI_LANG_DE);
    char out[160];
    upd_fetch_t f = view(DEVIMG_SRC_NET, UPD_FETCH_NET, DIAL);
    f.percent = 43u;
    CHECK(upd_fetch_label(&f, out, sizeof(out)));
    CHECK_STR_EQ(out, "DOWNLOAD 43%");
    f = view(DEVIMG_SRC_CARD, UPD_FETCH_IDLE, CARD);
    CHECK(upd_fetch_label(&f, out, sizeof(out)));
    CHECK_STR_EQ(out, "2s HALTEN: UPDATE VON DER SD-KARTE");
    upd_fetch_line(&f, out, sizeof(out));
    CHECK_STR_EQ(out, "Das Halten liest " CARD " von der SD-Karte.");
    f = view(DEVIMG_SRC_RAM, UPD_FETCH_IDLE, DIAL);
    f.nosig = true;
    upd_fetch_line(&f, out, sizeof(out));
    CHECK_STR_EQ(out, DIAL ": nicht vom rcbench-Release-Schlüssel signiert. "
                      "Das Image hat keine Signatur.");
    upd_fetch_fail_line(UPD_FETCH_FAIL_CARD, "S01", "Keine SD-Karte.", out,
                        sizeof(out));
    CHECK_STR_EQ(out, "SD-Karte fehlgeschlagen (S01): Keine SD-Karte.");
    upd_fetch_check_text(DEVIMG_E_VERSION, "0.23.15", "0.23.16", out,
                         sizeof(out));
    CHECK_STR_EQ(out, "Die Datei hat Version 0.23.15, dieses Release 0.23.16.");
    ui_text_set_language(UI_LANG_EN);
}

TEST_CASE(a_null_or_empty_buffer_is_left_alone)
{
    upd_fetch_t f = view(DEVIMG_SRC_CARD, UPD_FETCH_NET, CARD);
    char out[8] = "keep";
    CHECK(!upd_fetch_label(NULL, out, sizeof(out)));
    CHECK(!upd_fetch_label(&f, NULL, sizeof(out)));
    CHECK(!upd_fetch_label(&f, out, 0u));
    CHECK_STR_EQ(out, "keep");
    upd_fetch_line(&f, NULL, 8u);
    upd_fetch_line(&f, out, 0u);
    upd_fetch_nosig_note(DIAL, NULL, 8u);
    upd_fetch_nosig_note(DIAL, out, 0u);
    upd_fetch_fail_line(UPD_FETCH_FAIL_NET, "D10", "r", NULL, 8u);
    upd_fetch_fail_line(UPD_FETCH_FAIL_NET, "D10", "r", out, 0u);
    upd_fetch_card_text(DEVIMG_CARD_NONE, CARD, NULL, 8u);
    upd_fetch_card_text(DEVIMG_CARD_NONE, CARD, out, 0u);
    upd_fetch_check_text(DEVIMG_E_SIZE, NULL, NULL, NULL, 8u);
    upd_fetch_check_text(DEVIMG_E_SIZE, NULL, NULL, out, 0u);
    CHECK_STR_EQ(out, "keep");
}

int main(void)
{
    RUN(the_label_follows_the_download_the_check_and_the_card_read);
    RUN(a_hold_that_reads_the_card_says_so_and_a_download_keeps_the_rows_label);
    RUN(the_cards_line_names_the_file_and_where_the_hold_takes_it);
    RUN(an_unsigned_image_warns_with_its_reason_and_a_signed_one_does_not);
    RUN(each_failure_line_carries_its_kind_and_code);
    RUN(each_card_answer_has_its_reason);
    RUN(each_check_refusal_has_its_reason);
    RUN(the_words_follow_the_language);
    RUN(a_null_or_empty_buffer_is_left_alone);
    return test_summary("upd_fetch");
}
