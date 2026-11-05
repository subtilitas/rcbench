/*
 * The console's boot line: the boot ROM's boot type, the partition booted,
 * the diagnostic source and the diagnostic word, from the boot word
 * 0xttppbbdd and boot_info_t.boot_diagnostic.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "boot_diag.h"

/** @brief A boot word: flags @p tbyb, partition @p part, type, source. */
static uint32_t word(uint8_t tbyb, int8_t part, uint8_t type, int8_t source)
{
    return ((uint32_t)tbyb << 24) | ((uint32_t)(uint8_t)part << 16)
           | ((uint32_t)type << 8) | (uint32_t)(uint8_t)source;
}

/** @brief Boot info present with @p boot_word and @p diag. */
static reset_regs_t regs(uint32_t boot_word, uint32_t diag)
{
    reset_regs_t r;
    memset(&r, 0, sizeof(r));
    r.boot_ok = true;
    r.boot_word = boot_word;
    r.boot_diagnostic = diag;
    return r;
}

TEST_CASE(the_failed_power_on_of_2026_11_04_reads_as_the_station_saw_it)
{
    char buf[BOOT_DIAG_TEXT_MAX];
    const reset_regs_t r = regs(word(0u, BOOT_DIAG_PARTITION_NONE,
                                     RESET_BOOT_TYPE_NORMAL,
                                     BOOT_DIAG_PARTITION_SLOT0),
                                0x00010355u);
    CHECK_EQ(boot_diag_text(&r, buf, sizeof(buf)), strlen(buf));
    CHECK_STR_EQ(buf, "boot type NORMAL partition none source slot 0 "
                      "diagnostic 0x00010355");
}

TEST_CASE(a_boot_of_slot_b_names_partition_1)
{
    char buf[BOOT_DIAG_TEXT_MAX];
    const reset_regs_t r = regs(word(0x01u, 1, RESET_BOOT_TYPE_FLASH_UPDATE,
                                     BOOT_DIAG_PARTITION_NONE), 0u);
    (void)boot_diag_text(&r, buf, sizeof(buf));
    CHECK_STR_EQ(buf, "boot type FLASH_UPDATE partition 1 source none "
                      "diagnostic 0x00000000");
}

TEST_CASE(a_diagnostic_reboot_names_its_source_partition)
{
    char buf[BOOT_DIAG_TEXT_MAX];
    const reset_regs_t r = regs(word(0u, 0, RESET_BOOT_TYPE_NORMAL, 0),
                                0x50715071u);
    (void)boot_diag_text(&r, buf, sizeof(buf));
    CHECK_STR_EQ(buf, "boot type NORMAL partition 0 source 0 "
                      "diagnostic 0x50715071");
}

TEST_CASE(every_named_type_and_code_has_its_word)
{
    char buf[BOOT_DIAG_TEXT_MAX];
    reset_regs_t r = regs(word(0u, BOOT_DIAG_PARTITION_SLOT1,
                               RESET_BOOT_TYPE_BOOTSEL,
                               BOOT_DIAG_PARTITION_WINDOW), 1u);
    (void)boot_diag_text(&r, buf, sizeof(buf));
    CHECK_STR_EQ(buf, "boot type BOOTSEL partition slot 1 source window "
                      "diagnostic 0x00000001");
    r.boot_word = word(0u, 15, RESET_BOOT_TYPE_RAM_IMAGE, 15);
    (void)boot_diag_text(&r, buf, sizeof(buf));
    CHECK_STR_EQ(buf, "boot type RAM_IMAGE partition 15 source 15 "
                      "diagnostic 0x00000001");
    r.boot_word = word(0u, 0, RESET_BOOT_TYPE_PC_SP, 0);
    (void)boot_diag_text(&r, buf, sizeof(buf));
    CHECK_STR_EQ(buf, "boot type PC_SP partition 0 source 0 "
                      "diagnostic 0x00000001");
}

TEST_CASE(a_chained_boot_carries_the_chained_mark)
{
    char buf[BOOT_DIAG_TEXT_MAX];
    const reset_regs_t r = regs(word(0u, 0, RESET_BOOT_TYPE_NORMAL
                                                | RESET_BOOT_TYPE_CHAINED_FLAG,
                                     BOOT_DIAG_PARTITION_SLOT0), 0u);
    (void)boot_diag_text(&r, buf, sizeof(buf));
    CHECK_STR_EQ(buf, "boot type NORMAL+CHAINED partition 0 source slot 0 "
                      "diagnostic 0x00000000");
}

TEST_CASE(an_unknown_type_and_partition_print_in_hex)
{
    char buf[BOOT_DIAG_TEXT_MAX];
    /* 16 is one past the 16 partitions; -5 no code at all. */
    const reset_regs_t r = regs(word(0u, 16, 0x07u, -5), 0u);
    (void)boot_diag_text(&r, buf, sizeof(buf));
    CHECK_STR_EQ(buf, "boot type 0x07 partition 0x10 source 0xFB "
                      "diagnostic 0x00000000");
}

TEST_CASE(no_boot_info_reads_none)
{
    char buf[BOOT_DIAG_TEXT_MAX];
    reset_regs_t r = regs(0xFFFFFFFFu, 0xFFFFFFFFu);
    r.boot_ok = false;
    CHECK_EQ(boot_diag_text(&r, buf, sizeof(buf)), strlen(buf));
    CHECK_STR_EQ(buf, "boot info none");
    CHECK_EQ(boot_diag_text(NULL, buf, sizeof(buf)), strlen(buf));
    CHECK_STR_EQ(buf, "boot info none");
}

TEST_CASE(the_longest_line_fits_and_a_short_buffer_cuts_it)
{
    char buf[BOOT_DIAG_TEXT_MAX];
    const reset_regs_t r = regs(word(0xFFu, BOOT_DIAG_PARTITION_SLOT0,
                                     RESET_BOOT_TYPE_FLASH_UPDATE
                                         | RESET_BOOT_TYPE_CHAINED_FLAG,
                                     BOOT_DIAG_PARTITION_SLOT1),
                                0xFFFFFFFFu);
    const size_t len = boot_diag_text(&r, buf, sizeof(buf));
    CHECK_EQ(len, strlen(buf));
    CHECK(len < BOOT_DIAG_TEXT_MAX);
    char small[10];
    CHECK_EQ(boot_diag_text(&r, small, sizeof(small)), len);
    CHECK_STR_EQ(small, "boot type");
}

int main(void)
{
    RUN(the_failed_power_on_of_2026_11_04_reads_as_the_station_saw_it);
    RUN(a_boot_of_slot_b_names_partition_1);
    RUN(a_diagnostic_reboot_names_its_source_partition);
    RUN(every_named_type_and_code_has_its_word);
    RUN(a_chained_boot_carries_the_chained_mark);
    RUN(an_unknown_type_and_partition_print_in_hex);
    RUN(no_boot_info_reads_none);
    RUN(the_longest_line_fits_and_a_short_buffer_cuts_it);
    return test_summary("boot_diag");
}
