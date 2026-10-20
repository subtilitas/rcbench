/*
 * A card taken out, swapped or put in while the panel runs
 * (firmware/panel/components/storage), over the slot of
 * storage_stub/storage_fake.c.
 *
 * Under test: storage_check() -- LOGS opening, RESCAN, a run's start --
 * finds the card in the slot now with no restart.  A mounted card that no
 * longer answers is let go and the slot mounted again, with no E line from
 * anyone; a card held open is let go at its last release, and no new hold
 * is given meanwhile; a walk that finds the card gone mounts the slot once
 * more and says so; a read error on a card that answers is not taken for a
 * removal, and costs one mount, not a loop.  The lock is free after every
 * call.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "storage_fake.h"

/* --------------------------------------------------------------- helpers */

/* A file on the host for storage_fopen(): the stub's mount point is a name
 * only, so the file is opened where the suite runs. */
#define HOST_FILE "test_storage_swap.tmp"

/* The SK64G of the station: exFAT, 61002 MB, its own label. */
static void sk64g_in(void)
{
    card_in(FS_EXFAT, 61002ull * 1024u * 1024u);
    g_label = "SK64G";
    storage_deinit();
    g_bus_up = false;
    CHECK_EQ(storage_init(), ESP_OK);
    CHECK_STR_EQ(storage_card_name(), "SK64G");
    g_log_n = 0u;
    g_mounts = 0u;
    g_unmounts = 0u;
    g_bus_inits = 1u;               /* the start-up's */
}

/* The card in the slot taken out. */
static void pull(void)
{
    g_card = false;
    g_live = false;
}

/* Another card in the slot, which nothing has started yet. */
static void swap_to(FRESULT fs, BYTE type, const char *label)
{
    g_card = true;
    g_live = false;
    g_card_fs = fs;
    g_fs_type = type;
    g_label = label;
}

static unsigned walk_big(seen_t *v)
{
    memset(v, 0, sizeof(*v));
    return (unsigned)storage_walk("BIG", NULL, see, v, NULL);
}

/* --------------------------------------------------------------- cases */

TEST_CASE(a_card_pulled_while_mounted_is_found_gone_on_the_next_check)
{
    sk64g_in();
    pull();
    CHECK_EQ(storage_check(), CARD_STATE_NONE);
    CHECK(!storage_mounted());
    CHECK_STR_EQ(storage_card_name(), "");
    CHECK_STR_EQ(storage_status(), "no card");
    CHECK_EQ(g_unmounts, 1u);
    CHECK(!g_cs);
    /* The look was one status command, with the check off again after. */
    CHECK_EQ(g_status_asks, 1u);
    CHECK(!g_status_check);
    /* No E line from anyone, the tags back where they were, and the two
     * I lines of storage.c: the card gone and the slot empty. */
    CHECK_EQ(lines_at(ESP_LOG_ERROR), 0u);
    CHECK_EQ(g_disk_tag_level, ESP_LOG_INFO);
    CHECK_EQ(g_cmd_tag_level, ESP_LOG_INFO);
    CHECK_EQ(g_mount_tag_level, ESP_LOG_INFO);
    CHECK_EQ(lines_at(ESP_LOG_INFO), 2u);
    CHECK(strstr(g_log[0].text, "card gone") != NULL);
    CHECK(strstr(last_line()->text, "no card") != NULL);
    CHECK(!g_lock_taken);
    CHECK_EQ(g_lock_misuse, 0u);
}

TEST_CASE(rescan_twice_after_a_pull_reads_the_empty_slot_twice)
{
    sk64g_in();
    pull();
    CHECK_EQ(storage_check(), CARD_STATE_NONE);
    CHECK_EQ(storage_check(), CARD_STATE_NONE);
    CHECK_EQ(g_unmounts, 1u);
    CHECK_EQ(g_mounts, 0u);
    /* Gone once, then one "no card" a try. */
    CHECK_EQ(lines_at(ESP_LOG_ERROR), 0u);
    unsigned gone = 0u;
    unsigned none = 0u;
    for (unsigned i = 0u; i < g_log_n; ++i) {
        gone += (strstr(g_log[i].text, "card gone") != NULL) ? 1u : 0u;
        none += (strstr(g_log[i].text, "no card") != NULL) ? 1u : 0u;
    }
    CHECK_EQ(gone, 1u);
    CHECK_EQ(none, 2u);
    CHECK_EQ(g_bus_inits, 1u);
    CHECK(!g_lock_taken);
}

TEST_CASE(a_swap_to_a_card_with_no_fat_volume_says_so)
{
    sk64g_in();
    swap_to(FR_NO_FILESYSTEM, 0u, "");
    for (unsigned k = 1u; k <= 2u; ++k) {          /* RESCAN, twice */
        CHECK_EQ(storage_check(), CARD_STATE_NO_FAT);
        CHECK(!storage_mounted());
        CHECK_STR_EQ(storage_card_name(), "");
        CHECK_STR_EQ(storage_status(), "no FAT volume");
    }
    CHECK_EQ(g_unmounts, 1u);
    CHECK_EQ(lines_at(ESP_LOG_ERROR), 0u);
    CHECK_EQ(lines_at(ESP_LOG_WARN), 2u);           /* one per try */
    CHECK(probe_left_nothing());
    CHECK(!g_lock_taken);
}

TEST_CASE(a_swap_to_a_fat32_or_exfat_card_mounts_it_by_its_own_name)
{
    static const struct {
        BYTE type;
        const char *label;
        const char *line;
    } k[] = {
        { FS_FAT32, "BENCH2", "BENCH2 mounted, FAT32, 61002 MB" },
        { FS_EXFAT, "BENCH3", "BENCH3 mounted, exFAT, 61002 MB" },
    };
    for (size_t i = 0u; i < sizeof(k) / sizeof(k[0]); ++i) {
        sk64g_in();
        swap_to(FR_OK, k[i].type, k[i].label);
        CHECK_EQ(storage_check(), CARD_STATE_MOUNTED);
        CHECK(storage_mounted());
        CHECK_STR_EQ(storage_card_name(), k[i].label);
        CHECK_STR_EQ(last_line()->text, k[i].line);
        CHECK_EQ(g_unmounts, 1u);
        CHECK_EQ(g_mounts, 1u);
        CHECK(g_cs);
        CHECK_EQ(lines_at(ESP_LOG_ERROR), 0u);
        /* The list is the new card's. */
        seen_t v;
        CHECK_EQ(walk_big(&v), (unsigned)VOL_N);
        /* A card that answers is looked at and kept. */
        CHECK_EQ(storage_check(), CARD_STATE_MOUNTED);
        CHECK_EQ(g_unmounts, 1u);
        CHECK_EQ(g_mounts, 1u);
    }
    CHECK(!g_lock_taken);
}

TEST_CASE(a_card_put_into_the_empty_slot_is_mounted_by_the_next_check)
{
    slot_empty();
    CHECK_EQ(storage_init(), ESP_ERR_NOT_FOUND);
    CHECK_EQ(storage_check(), CARD_STATE_NONE);
    g_card = true;
    CHECK_EQ(storage_check(), CARD_STATE_MOUNTED);
    CHECK_STR_EQ(storage_card_name(), "UNITEST");
    CHECK_EQ(g_unmounts, 0u);
    CHECK_EQ(g_bus_inits, 1u);
    CHECK_EQ(lines_at(ESP_LOG_ERROR), 0u);
    CHECK(!g_lock_taken);
}

TEST_CASE(a_card_pulled_during_a_run_is_let_go_at_the_close)
{
    sk64g_in();
    FILE *f = storage_fopen(HOST_FILE, "w");
    CHECK(f != NULL);
    pull();
    /* The run's start of another task, or the logger's look after a write
     * failed: gone, and not let go under the open file. */
    CHECK_EQ(storage_check(), CARD_STATE_NONE);
    CHECK_EQ(g_unmounts, 0u);
    CHECK(!storage_mounted());
    CHECK_STR_EQ(storage_card_name(), "");
    /* No new hold, no walk, no stat on the volume that is gone. */
    CHECK(!storage_hold());
    CHECK(storage_fopen(HOST_FILE, "r") == NULL);
    seen_t v;
    CHECK_EQ((int)walk_big(&v), -1);
    storage_stat_t st;
    CHECK_EQ(storage_stat(STORAGE_MOUNT_POINT "/BIG/HUGE.CSV", &st),
             ESP_ERR_INVALID_STATE);
    /* storage_init() does not mount over it either. */
    CHECK_EQ(storage_init(), ESP_ERR_INVALID_STATE);
    CHECK_EQ(g_mounts, 0u);
    /* The close lets it go; the next check finds what is in the slot. */
    CHECK_EQ(storage_fclose(f), 0);
    CHECK_EQ(g_unmounts, 1u);
    CHECK(!g_cs);
    swap_to(FR_OK, FS_FAT32, "BENCH2");
    CHECK_EQ(storage_check(), CARD_STATE_MOUNTED);
    CHECK_STR_EQ(storage_card_name(), "BENCH2");
    CHECK_EQ(lines_at(ESP_LOG_ERROR), 0u);
    CHECK(!g_lock_taken);
    CHECK_EQ(g_lock_misuse, 0u);
    (void)remove(HOST_FILE);
}

TEST_CASE(holds_are_counted_and_the_last_release_lets_go)
{
    sk64g_in();
    CHECK(storage_hold());
    CHECK(storage_hold());
    pull();
    CHECK_EQ(storage_check(), CARD_STATE_NONE);
    storage_release();
    CHECK_EQ(g_unmounts, 0u);
    storage_release();
    CHECK_EQ(g_unmounts, 1u);
    /* A release too many is no hold below zero. */
    storage_release();
    g_card = true;
    CHECK_EQ(storage_check(), CARD_STATE_MOUNTED);
    CHECK(storage_hold());
    storage_release();
    CHECK(storage_mounted());
    /* Nothing mounted: no hold, and the closes of nothing are refused. */
    slot_empty();
    CHECK(!storage_hold());
    CHECK(storage_fopen(HOST_FILE, "w") == NULL);
    CHECK_EQ(storage_fclose(NULL), EOF);
    CHECK(!g_lock_taken);
    CHECK_EQ(g_lock_misuse, 0u);
}

TEST_CASE(a_walk_on_a_swapped_card_mounts_the_new_one_and_says_so)
{
    sk64g_in();
    seen_t v;
    CHECK_EQ(walk_big(&v), (unsigned)VOL_N);     /* the volume was read */
    swap_to(FR_OK, FS_FAT32, "BENCH2");
    /* No look first: the walk finds it, the slot is mounted again once. */
    CHECK_EQ(storage_walk("BIG", NULL, see, &v, NULL), STORAGE_WALK_REMOUNTED);
    CHECK_EQ(g_unmounts, 1u);
    CHECK_EQ(g_mounts, 1u);
    CHECK_STR_EQ(storage_card_name(), "BENCH2");
    CHECK_EQ(walk_big(&v), (unsigned)VOL_N);
    /* Pulled: the walk lets it go and the empty slot is -1, no card. */
    pull();
    CHECK_EQ((int)walk_big(&v), -1);
    CHECK_EQ(storage_card_state(), CARD_STATE_NONE);
    CHECK_EQ(g_unmounts, 2u);
    CHECK(!g_lock_taken);
}

TEST_CASE(a_read_error_that_is_not_a_removal_keeps_the_card)
{
    /* FatFs's own error, or a name it refuses: the card is kept. */
    static const FRESULT k_kept[] = { FR_INT_ERR, FR_DENIED, FR_TIMEOUT };
    for (size_t i = 0u; i < sizeof(k_kept) / sizeof(k_kept[0]); ++i) {
        sk64g_in();
        g_read_fail = k_kept[i];
        seen_t v;
        CHECK_EQ((int)walk_big(&v), -1);
        storage_stat_t st;
        CHECK_EQ(storage_stat(STORAGE_MOUNT_POINT "/BIG/HUGE.CSV", &st),
                 ESP_FAIL);
        CHECK(storage_mounted());
        CHECK_EQ(storage_card_state(), CARD_STATE_MOUNTED);
        CHECK_EQ(g_unmounts, 0u);
        CHECK_EQ(g_mounts, 0u);
    }

    /* A card that answers and fails its reads as a pulled one does: one
     * mount again, then unreadable, and no third. */
    sk64g_in();
    seen_t v;
    CHECK_EQ(walk_big(&v), (unsigned)VOL_N);
    g_read_fail = FR_DISK_ERR;
    CHECK_EQ(storage_check(), CARD_STATE_MOUNTED);   /* it answers */
    CHECK_EQ(storage_walk("BIG", NULL, see, &v, NULL), STORAGE_WALK_REMOUNTED);
    CHECK_EQ(g_mounts, 1u);
    CHECK_EQ((int)walk_big(&v), -1);
    CHECK_EQ(g_mounts, 1u);
    CHECK_EQ(g_unmounts, 2u);
    CHECK(!storage_mounted());
    CHECK_EQ(storage_card_state(), CARD_STATE_UNREADABLE);
    CHECK_STR_EQ(storage_status(), "FatFs 1");
    /* RESCAN: one mount, one failed read, unreadable again. */
    CHECK_EQ(storage_check(), CARD_STATE_MOUNTED);
    CHECK_EQ((int)walk_big(&v), -1);
    CHECK_EQ(g_mounts, 2u);
    CHECK_EQ(storage_card_state(), CARD_STATE_UNREADABLE);
    /* Read whole again: kept. */
    g_read_fail = FR_OK;
    CHECK_EQ(storage_check(), CARD_STATE_MOUNTED);
    CHECK_EQ(walk_big(&v), (unsigned)VOL_N);
    CHECK(!g_lock_taken);
    CHECK_EQ(g_lock_misuse, 0u);
}

TEST_CASE(a_stat_that_finds_the_card_gone_lets_it_go_without_a_mount)
{
    sk64g_in();
    storage_stat_t st;
    CHECK_EQ(storage_stat(STORAGE_MOUNT_POINT "/BIG/HUGE.CSV", &st), ESP_OK);
    pull();
    CHECK_EQ(storage_stat(STORAGE_MOUNT_POINT "/BIG/HUGE.CSV", &st), ESP_FAIL);
    CHECK_EQ(g_unmounts, 1u);
    CHECK_EQ(g_mounts, 0u);
    CHECK_EQ(storage_card_state(), CARD_STATE_NONE);
    CHECK(!g_lock_taken);
}

TEST_CASE(a_check_before_the_first_mount_and_after_the_end_is_safe)
{
    slot_empty();
    /* storage_deinit() with a hold left: the end, nothing waited for. */
    g_card = true;
    CHECK_EQ(storage_check(), CARD_STATE_MOUNTED);
    CHECK(storage_hold());
    storage_deinit();
    CHECK(!storage_mounted());
    CHECK_EQ(storage_card_state(), CARD_STATE_NONE);
    CHECK_EQ(storage_check(), CARD_STATE_MOUNTED);
    CHECK(storage_hold());
    storage_release();
    CHECK(!g_lock_taken);
    CHECK_EQ(g_lock_misuse, 0u);
}

int main(void)
{
    RUN(a_card_pulled_while_mounted_is_found_gone_on_the_next_check);
    RUN(rescan_twice_after_a_pull_reads_the_empty_slot_twice);
    RUN(a_swap_to_a_card_with_no_fat_volume_says_so);
    RUN(a_swap_to_a_fat32_or_exfat_card_mounts_it_by_its_own_name);
    RUN(a_card_put_into_the_empty_slot_is_mounted_by_the_next_check);
    RUN(a_card_pulled_during_a_run_is_let_go_at_the_close);
    RUN(holds_are_counted_and_the_last_release_lets_go);
    RUN(a_walk_on_a_swapped_card_mounts_the_new_one_and_says_so);
    RUN(a_read_error_that_is_not_a_removal_keeps_the_card);
    RUN(a_stat_that_finds_the_card_gone_lets_it_go_without_a_mount);
    RUN(a_check_before_the_first_mount_and_after_the_end_is_safe);
    return test_summary("storage_swap");
}
