/**
 * @file test_files_changed.c
 * @brief The editor's SAVE against a file changed on the card since it was
 *        read: the CRC-32 (cyclic redundancy check) the LOAD and each SAVE
 *        take, the compare before a byte is written, the question STAY or
 *        READ AGAIN, and the sum the Wi-Fi page's ETag shares.
 *
 * Cases:
 * - the_sum_is_the_api_etag_crc32
 * - a_browser_write_between_open_and_save_fails_the_save
 * - stay_keeps_the_text_and_a_second_save_is_refused_again
 * - read_again_reads_the_file_the_browser_wrote
 * - a_save_after_a_save_compares_with_what_it_wrote
 * - a_file_removed_since_the_open_fails_the_save
 * - crlf_and_a_bom_are_summed_as_the_card_holds_them
 * - an_open_question_is_kept_when_the_save_finds_a_change
 * - the_header_shows_the_size_as_read_and_as_saved
 *
 * SPDX-License-Identifier: MIT
 */
#include "fm_crc.h"
#include "wa_zip.h"

#include "files_edit_rig.h"

/** @brief A short text file. */
static const char k_note[] =
    "first line\n"
    "second line\n";

/** @brief What the Wi-Fi page writes over it. */
static const char k_web[] =
    "web line\n"
    "second line\n";

/**
 * @brief A write of the Wi-Fi page: the file at @p path replaced by
 *        @p len bytes of @p text, outside FILES' jobs.
 * @param path  the file
 * @param text  the bytes
 * @param len   their count
 */
static void browser_writes(const char *path, const char *text, size_t len)
{
    const fm_io_t io = fk_io(&g_h.card);
    CHECK_EQ(io.unlink(io.ctx, path), FM_OK);
    put_n(path, text, len);
}

/** @brief The note FILES writes for FM_ERR_CHANGED on @p name. */
static const char *changed(const char *name)
{
    static char out[256];
    files_err_text(FM_ERR_CHANGED, name, out, sizeof(out));
    return out;
}

/*
 * The panel's sum is the API's ETag: CRC-32 as ZIP takes it, the same in
 * pieces as in one, and wa_crc32() gives the same.
 */
TEST_CASE(the_sum_is_the_api_etag_crc32)
{
    CHECK_EQ(fm_crc32(0u, "123456789", 9u), 0xCBF43926u);
    CHECK_EQ(fm_crc32(0u, NULL, 0u), 0u);
    static uint8_t buf[70000];
    for (size_t i = 0u; i < sizeof(buf); ++i) {
        buf[i] = (uint8_t)(i * 131u + (i >> 9));
    }
    const uint32_t whole = fm_crc32(0u, buf, sizeof(buf));
    uint32_t parts = 0u;
    for (size_t at = 0u; at < sizeof(buf); at += 16384u) {
        const size_t n = (sizeof(buf) - at < 16384u) ? sizeof(buf) - at
                                                     : 16384u;
        parts = fm_crc32(parts, buf + at, n);
    }
    CHECK_EQ(parts, whole);
    CHECK_EQ(wa_crc32(0u, buf, sizeof(buf)), whole);
}

/*
 * The browser writes the file after the editor read it: SAVE writes
 * nothing, no ".NEW" is left, the browser's bytes stay, and the editor
 * says why and asks STAY or READ AGAIN with its text kept.
 */
TEST_CASE(a_browser_write_between_open_and_save_fails_the_save)
{
    open_text("NOTE.TXT", k_note);
    edit();
    ed_type("x");
    ed_key(UI_TK_DONE);
    browser_writes("USER/LOGS/NOTE.TXT", k_web, strlen(k_web));
    const long writes = g_h.card.calls[FK_WRITE];
    save();
    CHECK(!text_view_saving());
    CHECK_EQ(g_h.card.calls[FK_WRITE], writes);
    CHECK(card_holds("USER/LOGS/NOTE.TXT", k_web, strlen(k_web)));
    CHECK(fk_get(&g_h.card, "USER/LOGS/NOTE.TXT.NEW") == NULL);
    CHECK(text_view_dirty());
    CHECK_EQ(text_view_ask(), TEXT_VIEW_ASK_CHANGED);
    CHECK_STR_EQ(note(), changed("NOTE.TXT"));
    CHECK_STR_EQ(row_at(0), "xfirst line");
}

/*
 * STAY closes the question and keeps the text; SAVE again is refused
 * again, the browser's file untouched.
 */
TEST_CASE(stay_keeps_the_text_and_a_second_save_is_refused_again)
{
    open_text("NOTE.TXT", k_note);
    edit();
    ed_type("x");
    ed_key(UI_TK_DONE);
    browser_writes("USER/LOGS/NOTE.TXT", k_web, strlen(k_web));
    save();
    NEED(text_view_ask() == TEXT_VIEW_ASK_CHANGED);
    tap(FS_SPOT_TV_STAY_CX, FS_SPOT_TV_Q_CY);
    CHECK_EQ(text_view_ask(), TEXT_VIEW_ASK_NONE);
    CHECK(text_view_dirty());
    CHECK_STR_EQ(row_at(0), "xfirst line");
    save();
    CHECK_EQ(text_view_ask(), TEXT_VIEW_ASK_CHANGED);
    CHECK(card_holds("USER/LOGS/NOTE.TXT", k_web, strlen(k_web)));
}

/*
 * READ AGAIN drops the editor's text and reads the file from the card:
 * the viewer shows the browser's text, no question, nothing dirty; EDIT
 * and SAVE then write over the browser's version.
 */
TEST_CASE(read_again_reads_the_file_the_browser_wrote)
{
    open_text("NOTE.TXT", k_note);
    edit();
    ed_type("x");
    ed_key(UI_TK_DONE);
    browser_writes("USER/LOGS/NOTE.TXT", k_web, strlen(k_web));
    save();
    NEED(text_view_ask() == TEXT_VIEW_ASK_CHANGED);
    tap(FS_SPOT_TV_DISCARD_CX, FS_SPOT_TV_Q_CY);
    settle();
    draw();
    CHECK_EQ(files_screen_view(), FILES_VIEW_TEXT);
    CHECK_EQ(text_view_state(), TEXT_VIEW_SHOWN);
    CHECK_EQ(text_view_ask(), TEXT_VIEW_ASK_NONE);
    CHECK(!text_view_dirty());
    CHECK_STR_EQ(text_view_name(), "NOTE.TXT");
    CHECK_STR_EQ(row_at(0), "web line");
    edit();
    ed_type("y");
    ed_key(UI_TK_DONE);
    save();
    CHECK_STR_EQ(note(), said(TX_ED_SAVED, "NOTE.TXT"));
    CHECK(card_holds("USER/LOGS/NOTE.TXT", "yweb line\nsecond line\n", 22u));
}

/*
 * Two saves in a row: the second compares with the bytes the first wrote,
 * not with the file as first read, and writes.
 */
TEST_CASE(a_save_after_a_save_compares_with_what_it_wrote)
{
    open_text("NOTE.TXT", k_note);
    edit();
    ed_type("a");
    ed_key(UI_TK_DONE);
    save();
    CHECK_STR_EQ(note(), said(TX_ED_SAVED, "NOTE.TXT"));
    foot(FS_SPOT_TV_KEYS_CX);
    ed_type("b");
    ed_key(UI_TK_DONE);
    save();
    CHECK_STR_EQ(note(), said(TX_ED_SAVED, "NOTE.TXT"));
    CHECK_EQ(text_view_ask(), TEXT_VIEW_ASK_NONE);
    CHECK(card_holds("USER/LOGS/NOTE.TXT", "abfirst line\nsecond line\n",
                     25u));
}

/*
 * The file deleted since the open: SAVE makes no new file under the name
 * and asks as for a change.
 */
TEST_CASE(a_file_removed_since_the_open_fails_the_save)
{
    open_text("NOTE.TXT", k_note);
    edit();
    ed_type("x");
    ed_key(UI_TK_DONE);
    const fm_io_t io = fk_io(&g_h.card);
    CHECK_EQ(io.unlink(io.ctx, "USER/LOGS/NOTE.TXT"), FM_OK);
    save();
    CHECK(fk_get(&g_h.card, "USER/LOGS/NOTE.TXT") == NULL);
    CHECK(fk_get(&g_h.card, "USER/LOGS/NOTE.TXT.NEW") == NULL);
    CHECK_EQ(text_view_ask(), TEXT_VIEW_ASK_CHANGED);
}

/*
 * A file with a BOM (byte order mark) and CR LF line ends: its sum is
 * taken over the bytes as the card holds them, so an untouched file saves
 * and keeps both, and a browser write of the same text with LF ends is a
 * change.
 */
TEST_CASE(crlf_and_a_bom_are_summed_as_the_card_holds_them)
{
    static const char k_crlf[] = "\xEF\xBB\xBF" "one\r\ntwo\r\n";
    open_text("CRLF.TXT", k_crlf);
    edit();
    ed_type("z");
    ed_key(UI_TK_DONE);
    save();
    CHECK_STR_EQ(note(), said(TX_ED_SAVED, "CRLF.TXT"));
    CHECK(card_holds("USER/LOGS/CRLF.TXT", "\xEF\xBB\xBF" "zone\r\ntwo\r\n",
                     14u));
    foot(FS_SPOT_TV_KEYS_CX);
    ed_type("q");
    ed_key(UI_TK_DONE);
    browser_writes("USER/LOGS/CRLF.TXT", "\xEF\xBB\xBF" "zone\ntwo\n", 12u);
    save();
    CHECK_EQ(text_view_ask(), TEXT_VIEW_ASK_CHANGED);
    CHECK(card_holds("USER/LOGS/CRLF.TXT", "\xEF\xBB\xBF" "zone\ntwo\n",
                     12u));
}

/*
 * A question asked while the save ran (another open, held) stays the one
 * that shows when the save finds the change: its DISCARD still discards
 * and the held open goes on.
 */
TEST_CASE(an_open_question_is_kept_when_the_save_finds_a_change)
{
    open_text("NOTE.TXT", k_note);
    put_n("USER/LOGS/OTHER.TXT", k_web, strlen(k_web));
    edit();
    ed_type("x");
    ed_key(UI_TK_DONE);
    browser_writes("USER/LOGS/NOTE.TXT", k_web, strlen(k_web));
    g_h.defer = true;
    save();
    CHECK(text_view_saving());
    CHECK_EQ(files_screen_open_text(CARD_USER_LOGS, "OTHER.TXT",
                                    g_h.card.mount, strlen(k_web), false),
             FM_OK);
    settle();
    CHECK_EQ(text_view_ask(), TEXT_VIEW_ASK_OPEN);
    CHECK(fh_run(&g_h));
    g_h.defer = false;
    settle();
    CHECK(!text_view_saving());
    CHECK_EQ(text_view_ask(), TEXT_VIEW_ASK_OPEN);
    CHECK_STR_EQ(note(), changed("NOTE.TXT"));
    tap(FS_SPOT_TV_DISCARD_CX, FS_SPOT_TV_Q_CY);
    settle();
    CHECK_STR_EQ(text_view_name(), "OTHER.TXT");
}

/*
 * The header's size follows the file: as read when it opens (the listed
 * size handed in may be another), as written after each SAVE, and as read
 * again after READ AGAIN.
 */
TEST_CASE(the_header_shows_the_size_as_read_and_as_saved)
{
    open_text("NOTE.TXT", k_note);
    CHECK_EQ(text_view_size(), (uint64_t)strlen(k_note));
    edit();
    ed_type("abcd");
    ed_key(UI_TK_DONE);
    save();
    CHECK_STR_EQ(note(), said(TX_ED_SAVED, "NOTE.TXT"));
    CHECK_EQ(text_view_size(), (uint64_t)strlen(k_note) + 4u);
    foot(FS_SPOT_TV_KEYS_CX);
    ed_key(UI_TK_DEL);
    ed_key(UI_TK_DONE);
    save();
    CHECK_EQ(text_view_size(), (uint64_t)strlen(k_note) + 3u);
    /* Opened with a listed size that is stale: the header shows the
     * bytes read. */
    fresh_bare();
    card_default();
    put_n("USER/LOGS/NOTE.TXT", k_web, strlen(k_web));
    ui_router_goto(SCREEN_FILES);
    settle();
    CHECK_EQ(files_screen_open_text(CARD_USER_LOGS, "NOTE.TXT",
                                    g_h.card.mount, 1u, false), FM_OK);
    settle();
    CHECK_EQ(text_view_size(), (uint64_t)strlen(k_web));
    /* READ AGAIN: the size of the file the browser wrote. */
    edit();
    ed_type("x");
    ed_key(UI_TK_DONE);
    browser_writes("USER/LOGS/NOTE.TXT", "w\n", 2u);
    save();
    NEED(text_view_ask() == TEXT_VIEW_ASK_CHANGED);
    CHECK_EQ(text_view_size(), (uint64_t)strlen(k_web));
    tap(FS_SPOT_TV_DISCARD_CX, FS_SPOT_TV_Q_CY);
    settle();
    CHECK_EQ(text_view_size(), 2u);
}

int main(void)
{
    RUN(the_sum_is_the_api_etag_crc32);
    RUN(a_browser_write_between_open_and_save_fails_the_save);
    RUN(stay_keeps_the_text_and_a_second_save_is_refused_again);
    RUN(read_again_reads_the_file_the_browser_wrote);
    RUN(a_save_after_a_save_compares_with_what_it_wrote);
    RUN(a_file_removed_since_the_open_fails_the_save);
    RUN(crlf_and_a_bom_are_summed_as_the_card_holds_them);
    RUN(an_open_question_is_kept_when_the_save_finds_a_change);
    RUN(the_header_shows_the_size_as_read_and_as_saved);
    return test_summary("files_changed");
}
