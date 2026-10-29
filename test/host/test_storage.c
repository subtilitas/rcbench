/*
 * The panel's SD card mount (firmware/panel/components/storage/storage.c)
 * over the ESP-IDF calls of storage_stub/esp_fake.h.
 *
 * Under test: a mount tried again with no card -- the splash, RESCAN, an
 * opening of LOGS -- switches the card's supply on once and logs the
 * missing card once per try at I level, with no E line from storage.c or
 * from ESP-IDF's mount; the mount's own log tag is back at its level after
 * each try.  The mount is SDMMC slot 0, 4 data lines, with the supply's
 * handle on its host.  A card put in later mounts; a supply that failed to
 * come up is tried again; a supply switched off by storage_deinit() is set
 * up again; a mount that fails for another reason is said at W level.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "greatest.h"

#include "storage_fake.h"

/* --------------------------------------------------------------- cases */

TEST_CASE(a_mount_tried_again_with_no_card_sets_the_supply_up_once)
{
    slot_empty();
    for (unsigned k = 1u; k <= 3u; ++k) {
        CHECK_EQ(storage_init(), ESP_ERR_NOT_FOUND);
        CHECK(!storage_mounted());
        CHECK_STR_EQ(storage_status(), "no card");
        CHECK_EQ(g_pwr_news, 1u);
        CHECK(g_pwr_on);
        CHECK_EQ(g_mount_slot, 0);
        CHECK_EQ(g_mount_width, 4);
        /* One line a try, at I level, and no E line from anyone. */
        CHECK_EQ(lines_at(ESP_LOG_ERROR), 0u);
        CHECK_EQ(lines_at(ESP_LOG_INFO), k);
        CHECK_STR_EQ(g_log[k - 1u].tag, "storage");
        CHECK(strstr(g_log[k - 1u].text, "no card") != NULL);
        /* The mount's tag is back where it was. */
        CHECK_EQ(g_mount_tag_level, ESP_LOG_INFO);
    }
}

TEST_CASE(a_card_put_in_later_mounts_on_the_supply_already_on)
{
    slot_empty();
    CHECK_EQ(storage_init(), ESP_ERR_NOT_FOUND);
    g_card = true;
    CHECK_EQ(storage_init(), ESP_OK);
    CHECK(storage_mounted());
    CHECK_STR_EQ(name_now(), "UNITEST");
    CHECK_EQ(g_pwr_news, 1u);
    CHECK_EQ(lines_at(ESP_LOG_ERROR), 0u);
    /* Mounted: another call does nothing. */
    CHECK_EQ(storage_init(), ESP_OK);
    CHECK_EQ(g_pwr_news, 1u);
}

TEST_CASE(a_supply_switched_off_or_failed_is_set_up_again)
{
    slot_empty();
    g_pwr_fail = ESP_ERR_NO_MEM;
    CHECK_EQ(storage_init(), ESP_ERR_NO_MEM);
    CHECK_STR_EQ(storage_status(), "LDO ESP_ERR_NO_MEM");
    CHECK(!g_pwr_on);
    CHECK_EQ(storage_init(), ESP_ERR_NOT_FOUND);
    CHECK_EQ(g_pwr_news, 2u);
    /* Switched off: the next try sets it up again, and only once. */
    storage_deinit();
    CHECK(!g_pwr_on);
    CHECK_EQ(storage_init(), ESP_ERR_NOT_FOUND);
    CHECK_EQ(storage_init(), ESP_ERR_NOT_FOUND);
    CHECK_EQ(g_pwr_news, 3u);
    CHECK(g_pwr_on);
    CHECK_EQ(lines_at(ESP_LOG_ERROR), 0u);
    /* And the deinit of a slot never tried switches nothing. */
    slot_empty();
    storage_deinit();
    CHECK_EQ(g_pwr_news, 0u);
    CHECK(!g_pwr_on);
}

TEST_CASE(a_card_that_will_not_mount_is_said_at_w_level)
{
    slot_empty();
    g_card = true;
    g_card_fs = FR_DISK_ERR;
    CHECK_EQ(storage_init(), ESP_FAIL);
    CHECK(!storage_mounted());
    CHECK_EQ(storage_card_state(), CARD_STATE_UNREADABLE);
    CHECK_STR_EQ(storage_status(), "FatFs 1");
    CHECK_EQ(lines_at(ESP_LOG_ERROR), 0u);
    CHECK_EQ(lines_at(ESP_LOG_WARN), 1u);
    CHECK_STR_EQ(g_log[0].tag, "storage");
    CHECK(strstr(g_log[0].text, "FatFs 1") != NULL);
    CHECK_EQ(g_mount_tag_level, ESP_LOG_INFO);
    CHECK(probe_left_nothing());
    /* A tag set quieter or louder before is left as it was found. */
    g_mount_tag_level = ESP_LOG_DEBUG;
    CHECK_EQ(storage_init(), ESP_FAIL);
    CHECK_EQ(g_mount_tag_level, ESP_LOG_DEBUG);
}

/* The card the station mounted: SDHC by its CSD, one MBR partition of
 * 124,964,864 sectors formatted exFAT, "SK64G mounted, 61002 MB". */
TEST_CASE(an_exfat_card_mounts_and_its_line_names_the_format)
{
    card_in(FS_EXFAT, 61002ull * 1024u * 1024u);
    CHECK(storage_mounted());
    CHECK_EQ(lines_at(ESP_LOG_ERROR), 0u);
    const log_line_t *l = last_line();
    CHECK_STR_EQ(l->tag, "storage");
    CHECK_STR_EQ(l->text, "UNITEST mounted, exFAT, 61002 MB");
    CHECK(storage_total_bytes() == 61002ull * 1024u * 1024u);
}

TEST_CASE(every_fat_format_is_named_and_an_unread_one_says_so)
{
    static const struct {
        BYTE type;
        const char *line;
    } k[] = {
        { FS_FAT12, "UNITEST mounted, FAT12, 16384 MB" },
        { FS_FAT16, "UNITEST mounted, FAT16, 16384 MB" },
        { FS_FAT32, "UNITEST mounted, FAT32, 16384 MB" },
        { FS_EXFAT, "UNITEST mounted, exFAT, 16384 MB" },
        { 0u,       "UNITEST mounted, format unread, 16384 MB" },
    };
    for (size_t i = 0u; i < sizeof(k) / sizeof(k[0]); ++i) {
        card_in(k[i].type, 16ull * 1024u * 1024u * 1024u);
        CHECK_STR_EQ(last_line()->text, k[i].line);
    }
    slot_empty();
    g_card = true;
    g_getfree = FR_DISK_ERR;
    CHECK_EQ(storage_init(), ESP_OK);
    CHECK_STR_EQ(last_line()->text, "UNITEST mounted, format unread, 16384 MB");
}

TEST_CASE(a_walk_hands_on_sizes_past_4_gib_whole)
{
    card_in(FS_EXFAT, 64ull * 1000u * 1000u * 1000u);
    seen_t v = { 0 };
    CHECK_EQ(storage_walk("BIG", NULL, see, &v, NULL), (int)VOL_N);
    CHECK_EQ(v.n, (unsigned)VOL_N);
    CHECK_STR_EQ(v.got[0].name, "EDGE1.CSV");
    CHECK(v.got[0].size == 0xFFFFFFFFull);          /* 2^32 - 1 */
    CHECK(v.got[1].size == 0x100000000ull);         /* 2^32 */
    CHECK(v.got[2].size == 0x200000000ull);         /* 2^33 */
    CHECK(!v.got[2].is_dir);
    CHECK(v.got[3].is_dir);
    CHECK(v.got[3].size == 0u);
    /* The suffix filter passes the large files as any other. */
    memset(&v, 0, sizeof(v));
    CHECK_EQ(storage_walk("BIG", ".csv", see, &v, NULL), (int)VOL_N);
    CHECK(v.got[2].size == 0x200000000ull);
    /* A folder the volume does not have. */
    CHECK_EQ(storage_walk("NONE", NULL, see, &v, NULL), STORAGE_WALK_NO_DIR);
}

/*
 * Each entry of a walk carries FAT's stamp as the directory read returned
 * it, and storage_stamp_mtime() reads it as storage_stat() and
 * storage_dir_read() read the same file's: the LOGS list and FILES INFO
 * show one time.  A stamp of 1980, which FatFs writes for a clock not set,
 * and a stamp of 0 read below 1981-01-01 12:00 UTC (WALL_STAMP_FLOOR_S,
 * 347198400) in every zone from UTC-12 to UTC+14.
 */
TEST_CASE(a_walk_hands_on_each_entrys_stamp)
{
    card_in(FS_EXFAT, 64ull * 1000u * 1000u * 1000u);
    seen_t v = { 0 };
    CHECK_EQ(storage_walk("BIG", NULL, see, &v, NULL), (int)VOL_N);
    for (unsigned i = 0u; i < v.n; ++i) {
        CHECK_EQ(v.got[i].stamp,
                 ((uint32_t)VOL_FDATE << 16) | (uint32_t)VOL_FTIME);
        CHECK(storage_stamp_mtime(v.got[i].stamp) == vol_mtime());
    }
    storage_stat_t st;
    CHECK_EQ(storage_stat(STORAGE_MOUNT_POINT "/BIG/EDGE1.CSV", &st), ESP_OK);
    CHECK(storage_stamp_mtime(v.got[0].stamp) == st.mtime);
    /* 1980-01-01 00:00 and 1980-12-31 23:58 (year 0 in bits 15..9 of
     * the date), and no stamp at all. */
    static const uint32_t k_unset[] = {
        (uint32_t)((1u << 5) | 1u) << 16,
        ((uint32_t)((12u << 5) | 31u) << 16) | ((23u << 11) | (58u << 5)),
        0u,
    };
    static const char *const k_zones[] = { "UTC0", "<-12>12", "<+14>-14" };
    for (size_t z = 0u; z < sizeof(k_zones) / sizeof(k_zones[0]); ++z) {
        (void)setenv("TZ", k_zones[z], 1);
        tzset();
        for (size_t i = 0u; i < sizeof(k_unset) / sizeof(k_unset[0]); ++i) {
            CHECK(storage_stamp_mtime(k_unset[i]) < 347198400LL);
        }
    }
    (void)setenv("TZ", "UTC0", 1);
    tzset();
    /* 2026-10-26 09:41:00 in UTC. */
    const uint32_t now = ((uint32_t)((46u << 9) | (10u << 5) | 26u) << 16)
                         | ((9u << 11) | (41u << 5));
    CHECK(storage_stamp_mtime(now) == 1793007660LL);
}

TEST_CASE(a_counted_walk_counts_the_names_it_leaves_out)
{
    card_in(FS_EXFAT, 64ull * 1000u * 1000u * 1000u);
    seen_t v = { 0 };
    unsigned left = 99u;
    /* .DS_Store and a name of 70 characters are left out and counted;
     * the file manager calls such a folder not empty. */
    CHECK_EQ(storage_walk_counted("HID", NULL, see, &v, NULL, &left), 1);
    CHECK_EQ(left, 2u);
    CHECK_EQ(v.n, 1u);
    CHECK_STR_EQ(v.got[0].name, "KEEP.TXT");
    /* What the suffix filter leaves out is not counted: the long name is a
     * ".TXT", the dot name is counted before any suffix. */
    memset(&v, 0, sizeof(v));
    left = 99u;
    CHECK_EQ(storage_walk_counted("HID", ".csv", see, &v, NULL, &left), 0);
    CHECK_EQ(left, 1u);
    left = 99u;
    CHECK_EQ(storage_walk_counted("BIG", NULL, see, &v, NULL, &left),
             (int)VOL_N);
    CHECK_EQ(left, 0u);
    CHECK_EQ(storage_walk_counted("HID", NULL, NULL, NULL, NULL, NULL), 1);
    /* Nothing mounted: nothing read, nothing counted. */
    slot_empty();
    left = 99u;
    CHECK_EQ(storage_walk_counted("HID", NULL, see, &v, NULL, &left), -1);
    CHECK_EQ(left, 0u);
}

TEST_CASE(a_folder_read_an_entry_at_a_time_has_each_size_and_time)
{
    card_in(FS_EXFAT, 64ull * 1000u * 1000u * 1000u);
    storage_dir_t *d = storage_dir_open(STORAGE_MOUNT_POINT "/BIG");
    CHECK(d != NULL);
    char name[STORAGE_NAME_MAX];
    storage_stat_t st;
    for (size_t i = 0u; i < VOL_N; ++i) {
        memset(&st, 0xA5, sizeof(st));
        CHECK_EQ(storage_dir_read(d, name, sizeof(name), &st), 1);
        CHECK_STR_EQ(name, k_vol[i].name);
        CHECK(st.size == (k_vol[i].dir ? 0u : k_vol[i].size));
        CHECK_EQ(st.is_dir, k_vol[i].dir);
        CHECK(st.mtime == vol_mtime());
    }
    CHECK_EQ(storage_dir_read(d, name, sizeof(name), &st), 0);
    CHECK_EQ(storage_dir_read(d, name, sizeof(name), &st), 0);
    storage_dir_close(d);
    CHECK(storage_mounted());
    /* A name that does not fit: -1, not a cut name. */
    d = storage_dir_open(STORAGE_MOUNT_POINT "/BIG");
    CHECK(d != NULL);
    char small[9];                       /* "EDGE1.CSV" is 9 bytes */
    CHECK_EQ(storage_dir_read(d, small, sizeof(small), &st), -1);
    char fits[10];
    CHECK_EQ(storage_dir_read(d, fits, sizeof(fits), &st), 1);
    CHECK_STR_EQ(fits, "EDGE2.CSV");
    storage_dir_close(d);
    storage_dir_close(NULL);
    CHECK(storage_mounted());
    /* Not under the mount point, not there, nothing mounted: no folder,
     * and a folder not there is no failed read. */
    CHECK(storage_dir_open("/other/BIG") == NULL);
    CHECK(storage_dir_open(STORAGE_MOUNT_POINT "x/BIG") == NULL);
    CHECK(storage_dir_open(NULL) == NULL);
    CHECK(storage_dir_open(STORAGE_MOUNT_POINT "/NONE") == NULL);
    CHECK(storage_mounted());
    slot_empty();
    CHECK(storage_dir_open(STORAGE_MOUNT_POINT "/BIG") == NULL);
}

TEST_CASE(a_card_pulled_under_a_folder_read_is_let_go_at_the_close)
{
    card_in(FS_EXFAT, 64ull * 1000u * 1000u * 1000u);
    g_pull_after = 2;
    g_card = false;
    storage_dir_t *d = storage_dir_open(STORAGE_MOUNT_POINT "/BIG");
    CHECK(d != NULL);
    char name[STORAGE_NAME_MAX];
    storage_stat_t st;
    CHECK_EQ(storage_dir_read(d, name, sizeof(name), &st), 1);
    CHECK_EQ(storage_dir_read(d, name, sizeof(name), &st), 1);
    CHECK_EQ(storage_dir_read(d, name, sizeof(name), &st), -1);
    /* Held until the close; then the card is found gone and let go. */
    storage_dir_close(d);
    CHECK(!storage_mounted());
    CHECK_EQ(storage_card_state(), CARD_STATE_NONE);
    CHECK(!g_lock_taken);
}

TEST_CASE(a_stat_reads_sizes_past_4_gib_whole)
{
    card_in(FS_EXFAT, 64ull * 1000u * 1000u * 1000u);
    static const struct {
        const char *path;
        uint64_t size;
    } k[] = {
        { STORAGE_MOUNT_POINT "/BIG/EDGE1.CSV", 0xFFFFFFFFull },
        { STORAGE_MOUNT_POINT "/BIG/EDGE2.CSV", 0x100000000ull },
        { STORAGE_MOUNT_POINT "/BIG/HUGE.CSV",  0x200000000ull },
    };
    for (size_t i = 0u; i < sizeof(k) / sizeof(k[0]); ++i) {
        storage_stat_t st;
        memset(&st, 0xA5, sizeof(st));
        CHECK_EQ(storage_stat(k[i].path, &st), ESP_OK);
        CHECK(st.size == k[i].size);
        CHECK(!st.is_dir);
        CHECK(st.mtime == vol_mtime());
    }
    storage_stat_t st;
    CHECK_EQ(storage_stat(STORAGE_MOUNT_POINT "/BIG/SUB", &st), ESP_OK);
    CHECK(st.is_dir);
    CHECK(st.size == 0u);
    CHECK_EQ(storage_stat(STORAGE_MOUNT_POINT "/BIG", &st), ESP_OK);
    CHECK(st.is_dir);
}

TEST_CASE(a_stat_of_what_it_cannot_reach_says_which)
{
    storage_stat_t st;
    slot_empty();
    /* Nothing mounted. */
    CHECK_EQ(storage_stat(STORAGE_MOUNT_POINT "/BIG/HUGE.CSV", &st),
             ESP_ERR_INVALID_STATE);
    card_in(FS_EXFAT, 64ull * 1000u * 1000u * 1000u);
    /* Not under the mount point, or another name that starts like it. */
    CHECK_EQ(storage_stat("/spiffs/HUGE.CSV", &st), ESP_ERR_INVALID_STATE);
    CHECK_EQ(storage_stat(STORAGE_MOUNT_POINT "X/BIG/HUGE.CSV", &st),
             ESP_ERR_INVALID_STATE);
    CHECK_EQ(storage_stat(NULL, &st), ESP_ERR_INVALID_STATE);
    CHECK_EQ(storage_stat(STORAGE_MOUNT_POINT "/BIG/HUGE.CSV", NULL),
             ESP_ERR_INVALID_STATE);
    /* The root, which f_stat() does not take, is a folder. */
    CHECK_EQ(storage_stat(STORAGE_MOUNT_POINT, &st), ESP_OK);
    CHECK(st.is_dir);
    CHECK_EQ(storage_stat(STORAGE_MOUNT_POINT "/", &st), ESP_OK);
    CHECK(st.is_dir);
    /* No such file, no such folder. */
    CHECK_EQ(storage_stat(STORAGE_MOUNT_POINT "/BIG/NONE.CSV", &st),
             ESP_ERR_NOT_FOUND);
    CHECK_EQ(storage_stat(STORAGE_MOUNT_POINT "/NONE/HUGE.CSV", &st),
             ESP_ERR_NOT_FOUND);
    /* A read FatFs fails on its own: said at W level, and no size; the
     * card is kept (test_storage_swap holds a card gone). */
    const unsigned w = lines_at(ESP_LOG_WARN);
    g_stat_fail = FR_INT_ERR;
    CHECK_EQ(storage_stat(STORAGE_MOUNT_POINT "/BIG/HUGE.CSV", &st), ESP_FAIL);
    CHECK_EQ(lines_at(ESP_LOG_WARN), w + 1u);
    CHECK(strstr(last_line()->text, "0:/BIG/HUGE.CSV") != NULL);
}

TEST_CASE(an_empty_slot_and_a_slot_never_tried_read_as_no_card)
{
    slot_empty();
    CHECK_EQ(storage_card_state(), CARD_STATE_NONE);
    CHECK_STR_EQ(storage_status(), "");
    CHECK_EQ(storage_init(), ESP_ERR_NOT_FOUND);
    CHECK_EQ(storage_card_state(), CARD_STATE_NONE);
    /* No card: nothing to look at twice.  The mount starts the slot once,
     * and its failure stops it once. */
    CHECK_EQ(g_dev_adds, 1u);
    CHECK_EQ(g_dev_removes, 1u);
    CHECK(probe_left_nothing());
}

TEST_CASE(a_card_with_no_fat_volume_is_said_so)
{
    slot_empty();
    g_card = true;
    g_card_fs = FR_NO_FILESYSTEM;           /* exFAT, NTFS or unformatted */
    CHECK_EQ(storage_init(), ESP_FAIL);
    CHECK(!storage_mounted());
    CHECK_EQ(storage_card_state(), CARD_STATE_NO_FAT);
    CHECK_STR_EQ(storage_status(), "no FAT volume");
    CHECK_STR_EQ(name_now(), "");
    /* One W line from storage.c that names it, and no E line at all. */
    CHECK_EQ(lines_at(ESP_LOG_ERROR), 0u);
    CHECK_EQ(lines_at(ESP_LOG_WARN), 1u);
    CHECK_STR_EQ(last_line()->tag, "storage");
    CHECK(strstr(last_line()->text, "no FAT volume") != NULL);
    CHECK(strstr(last_line()->text, "format it FAT32 or exFAT") != NULL);
    CHECK(strstr(last_line()->text, "FatFs 13") != NULL);
    CHECK(strstr(last_line()->text, "FAT32") != NULL);
    /* The second look ran once, on a slot set up for it, and let go: 1
     * start and 1 stop for the mount, 1 of each for the look. */
    CHECK_EQ(g_dev_adds, 2u);
    CHECK_EQ(g_dev_removes, 2u);
    CHECK(g_slot_in_probe);
    CHECK(probe_left_nothing());
    CHECK_EQ(g_mount_tag_level, ESP_LOG_INFO);
}

TEST_CASE(a_card_with_no_fat_volume_tried_again_says_it_each_time)
{
    slot_empty();
    g_card = true;
    g_card_fs = FR_NO_FILESYSTEM;
    for (unsigned k = 1u; k <= 3u; ++k) {
        CHECK_EQ(storage_init(), ESP_FAIL);
        CHECK_EQ(storage_card_state(), CARD_STATE_NO_FAT);
        CHECK_EQ(lines_at(ESP_LOG_WARN), k);
        CHECK_EQ(g_dev_adds, 2u * k);
        CHECK(probe_left_nothing());
    }
    CHECK_EQ(g_pwr_news, 1u);
    CHECK_EQ(lines_at(ESP_LOG_ERROR), 0u);
}

TEST_CASE(a_card_with_no_fat_volume_replaced_by_a_fat_one_mounts)
{
    slot_empty();
    g_card = true;
    g_card_fs = FR_NO_FILESYSTEM;
    CHECK_EQ(storage_init(), ESP_FAIL);
    CHECK_EQ(storage_card_state(), CARD_STATE_NO_FAT);
    /* Out: no card. */
    g_card = false;
    CHECK_EQ(storage_init(), ESP_ERR_NOT_FOUND);
    CHECK_EQ(storage_card_state(), CARD_STATE_NONE);
    CHECK_STR_EQ(storage_status(), "no card");
    /* A FAT32 card in: mounted, the old words gone. */
    g_card = true;
    g_card_fs = FR_OK;
    CHECK_EQ(storage_init(), ESP_OK);
    CHECK(storage_mounted());
    CHECK_EQ(storage_card_state(), CARD_STATE_MOUNTED);
    CHECK_STR_EQ(storage_status(), "");
    CHECK_STR_EQ(name_now(), "UNITEST");
    CHECK_EQ(g_dev_adds, 4u);       /* 2 with the look, then 1 per try */
    CHECK_EQ(g_host_slots, 2);      /* slot 0 and the C6's slot 1 */
    CHECK_EQ(g_pwr_news, 1u);
    /* Unmounted: nothing known of the slot. */
    storage_deinit();
    CHECK_EQ(storage_card_state(), CARD_STATE_NONE);
}

TEST_CASE(a_card_swapped_without_a_pull_mounts_on_the_next_try)
{
    slot_empty();
    g_card = true;
    g_card_fs = FR_NO_FILESYSTEM;
    CHECK_EQ(storage_init(), ESP_FAIL);
    g_card_fs = FR_OK;                      /* formatted on a computer */
    CHECK_EQ(storage_init(), ESP_OK);
    CHECK_EQ(storage_card_state(), CARD_STATE_MOUNTED);
    CHECK(g_pwr_on);
    CHECK_EQ(g_pwr_news, 1u);
}

TEST_CASE(a_card_that_does_not_start_is_unreadable_without_a_second_look)
{
    slot_empty();
    g_card = true;
    g_card_start = ESP_ERR_INVALID_CRC;
    CHECK_EQ(storage_init(), ESP_ERR_INVALID_CRC);
    CHECK_EQ(storage_card_state(), CARD_STATE_UNREADABLE);
    CHECK_STR_EQ(storage_status(), "ESP_ERR_INVALID_CRC");
    CHECK_EQ(g_dev_adds, 1u);       /* the mount's, and none for a look */
    CHECK_EQ(lines_at(ESP_LOG_WARN), 1u);
    CHECK_EQ(lines_at(ESP_LOG_ERROR), 0u);
    CHECK(probe_left_nothing());
}

TEST_CASE(a_second_look_that_cannot_ask_fatfs_leaves_it_unreadable)
{
    /* No device for the look. */
    slot_empty();
    g_card = true;
    g_card_fs = FR_NO_FILESYSTEM;
    g_dev_fail = ESP_ERR_NO_MEM;
    g_dev_fail_after = 1u;      /* the mount's start works */
    CHECK_EQ(storage_init(), ESP_FAIL);
    CHECK_EQ(storage_card_state(), CARD_STATE_UNREADABLE);
    CHECK_STR_EQ(storage_status(), "ESP_FAIL");
    CHECK(strstr(last_line()->text, "card check: ESP_ERR_NO_MEM") != NULL);
    CHECK(probe_left_nothing());
    /* A slot that did not come up is not let go.  The one stop is the
     * mount's. */
    CHECK_EQ(g_dev_adds, 2u);
    CHECK_EQ(g_dev_removes, 1u);

    /* The card pulled between the mount and the look. */
    slot_empty();
    g_card = true;
    g_card_fs = FR_NO_FILESYSTEM;
    g_probe_card_gone = true;
    CHECK_EQ(storage_init(), ESP_FAIL);
    CHECK_EQ(storage_card_state(), CARD_STATE_UNREADABLE);
    CHECK(strstr(last_line()->text, "card check: ESP_ERR_TIMEOUT") != NULL);
    CHECK(probe_left_nothing());
    /* And the next try finds the empty slot. */
    g_probe_card_gone = false;
    CHECK_EQ(storage_init(), ESP_ERR_NOT_FOUND);
    CHECK_EQ(storage_card_state(), CARD_STATE_NONE);

    /* No free drive for FatFs. */
    slot_empty();
    g_card = true;
    g_card_fs = FR_NO_FILESYSTEM;
    g_drives_full = true;
    CHECK_EQ(storage_init(), ESP_FAIL);
    CHECK_EQ(storage_card_state(), CARD_STATE_UNREADABLE);
    CHECK(strstr(last_line()->text, "card check: ESP_ERR_NOT_FOUND") != NULL);
    CHECK(probe_left_nothing());
}

TEST_CASE(the_second_look_decides_what_the_card_is)
{
    /* The mount failed, the look found a FAT volume after all: a card that
     * reads now and then is unreadable, not one without FAT. */
    slot_empty();
    g_card = true;
    g_card_fs = FR_DISK_ERR;
    g_probe_fs = FR_OK;
    CHECK_EQ(storage_init(), ESP_FAIL);
    CHECK_EQ(storage_card_state(), CARD_STATE_UNREADABLE);
    CHECK_STR_EQ(storage_status(), "FatFs 0");
    CHECK(probe_left_nothing());
    /* A read error at the mount, no FAT at the look: what FatFs said last. */
    g_probe_fs = FR_NO_FILESYSTEM;
    CHECK_EQ(storage_init(), ESP_FAIL);
    CHECK_EQ(storage_card_state(), CARD_STATE_NO_FAT);
}

TEST_CASE(a_mount_starts_slot_0_once_on_the_host_the_c6_holds)
{
    /* A slot 0 started before the mount would make the mount's own start
     * answer ESP_ERR_INVALID_STATE, and no card would ever mount. */
    slot_empty();
    g_card = true;
    CHECK_EQ(storage_init(), ESP_OK);
    CHECK(storage_mounted());
    CHECK_EQ(g_dev_adds, 1u);
    CHECK_EQ(g_dev_removes, 0u);
    CHECK_EQ(g_host_slots, 2);      /* slot 0 and the C6's slot 1 */
    storage_deinit();
    CHECK_EQ(g_host_slots, 1);
    CHECK_EQ(g_host_stops, 0u);
}

TEST_CASE(a_slot_that_does_not_start_leaves_the_c6_link_up)
{
    slot_empty();
    g_card = true;
    g_dev_fail = ESP_ERR_INVALID_STATE;
    CHECK_EQ(storage_init(), ESP_ERR_INVALID_STATE);
    CHECK(!storage_mounted());
    CHECK_EQ(storage_card_state(), CARD_STATE_UNREADABLE);
    CHECK_STR_EQ(storage_status(), "ESP_ERR_INVALID_STATE");
    CHECK_EQ(lines_at(ESP_LOG_WARN), 1u);
    CHECK(strstr(last_line()->text, "mount failed") != NULL);
    CHECK_EQ(g_mount_slot, 0);
    /* The mount's stop of a slot that never started has no handle to stop:
     * the count stays at the C6's slot 1 and the controller stays up. */
    CHECK_EQ(g_dev_adds, 1u);
    CHECK_EQ(g_dev_removes, 1u);
    CHECK_EQ(g_host_slots, 1);
    CHECK_EQ(g_host_stops, 0u);
    CHECK(probe_left_nothing());
    /* The next try starts it and mounts. */
    CHECK_EQ(storage_init(), ESP_OK);
    CHECK(storage_mounted());
    CHECK_EQ(g_host_slots, 2);
    storage_deinit();
    CHECK_EQ(g_host_slots, 1);
    CHECK_EQ(g_host_stops, 0u);
}

TEST_CASE(a_supply_fault_is_unreadable_and_said)
{
    slot_empty();
    g_card = true;
    g_pwr_fail = ESP_ERR_NO_MEM;
    CHECK_EQ(storage_init(), ESP_ERR_NO_MEM);
    CHECK_EQ(storage_card_state(), CARD_STATE_UNREADABLE);
    CHECK_EQ(lines_at(ESP_LOG_WARN), 1u);
    CHECK(strstr(last_line()->text, "LDO ESP_ERR_NO_MEM") != NULL);
    /* Nothing was mounted without a supply. */
    CHECK_EQ(g_mounts, 0u);
    /* The next try reaches the card. */
    CHECK_EQ(storage_init(), ESP_OK);
    CHECK_EQ(storage_card_state(), CARD_STATE_MOUNTED);
}

TEST_CASE(the_status_stays_inside_its_field)
{
    /* 47 characters and the terminator, whatever ESP-IDF calls the fault,
     * and the start of the name kept. */
    slot_empty();
    g_pwr_fail = ESP_ERR_LONG_NAME;
    CHECK_EQ(storage_init(), ESP_ERR_LONG_NAME);
    CHECK_EQ(strlen(storage_status()), 47u);
    CHECK(strncmp(storage_status(), "LDO ESP_ERR_A_NAME", 18) == 0);

    slot_empty();
    g_card = true;
    g_card_start = ESP_ERR_LONG_NAME;
    CHECK_EQ(storage_init(), ESP_ERR_LONG_NAME);
    CHECK_EQ(strlen(storage_status()), 47u);
    CHECK_EQ(storage_card_state(), CARD_STATE_UNREADABLE);

    /* Every other answer is short, and an answer replaces the one before
     * whole: no tail of a longer one. */
    g_card_start = ESP_OK;
    g_card_fs = FR_NO_FILESYSTEM;
    CHECK_EQ(storage_init(), ESP_FAIL);
    CHECK_STR_EQ(storage_status(), "no FAT volume");
    g_card = false;
    CHECK_EQ(storage_init(), ESP_ERR_NOT_FOUND);
    CHECK_STR_EQ(storage_status(), "no card");
}

int main(void)
{
    RUN(a_mount_tried_again_with_no_card_sets_the_supply_up_once);
    RUN(a_card_put_in_later_mounts_on_the_supply_already_on);
    RUN(a_supply_switched_off_or_failed_is_set_up_again);
    RUN(a_card_that_will_not_mount_is_said_at_w_level);
    RUN(an_empty_slot_and_a_slot_never_tried_read_as_no_card);
    RUN(a_card_with_no_fat_volume_is_said_so);
    RUN(a_card_with_no_fat_volume_tried_again_says_it_each_time);
    RUN(a_card_with_no_fat_volume_replaced_by_a_fat_one_mounts);
    RUN(a_card_swapped_without_a_pull_mounts_on_the_next_try);
    RUN(a_card_that_does_not_start_is_unreadable_without_a_second_look);
    RUN(a_second_look_that_cannot_ask_fatfs_leaves_it_unreadable);
    RUN(the_second_look_decides_what_the_card_is);
    RUN(a_mount_starts_slot_0_once_on_the_host_the_c6_holds);
    RUN(a_slot_that_does_not_start_leaves_the_c6_link_up);
    RUN(a_supply_fault_is_unreadable_and_said);
    RUN(the_status_stays_inside_its_field);
    RUN(an_exfat_card_mounts_and_its_line_names_the_format);
    RUN(every_fat_format_is_named_and_an_unread_one_says_so);
    RUN(a_walk_hands_on_sizes_past_4_gib_whole);
    RUN(a_walk_hands_on_each_entrys_stamp);
    RUN(a_counted_walk_counts_the_names_it_leaves_out);
    RUN(a_stat_reads_sizes_past_4_gib_whole);
    RUN(a_folder_read_an_entry_at_a_time_has_each_size_and_time);
    RUN(a_card_pulled_under_a_folder_read_is_let_go_at_the_close);
    RUN(a_stat_of_what_it_cannot_reach_says_which);
    return test_summary("storage");
}
