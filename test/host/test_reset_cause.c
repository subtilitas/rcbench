/*
 * Why the IOmcu last started: one case per pattern of POWMAN CHIP_RESET,
 * WATCHDOG REASON, the boot ROM's boot word and WATCHDOG SCRATCH2/3, and
 * the text the console prints.  A watchdog system reset leaves CHIP_RESET
 * as the last chip-level reset set it, so HAD_POR is still set after a
 * reboot() on a board that was powered up.
 *
 * The boot word is 0xttppbbdd: tt TBYB and update flags, pp the partition
 * booted, bb the boot type, dd the diagnostic partition.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "greatest.h"

#include "reset_cause.h"

/* Slot B's start in flash as this firmware's update reboots into it:
 * XIP_BASE + 2036KiB. */
#define SLOT_B_XIP 0x101FD000u

/** @brief A boot word for partition @p part and boot type @p type. */
static uint32_t boot_word(uint8_t tbyb, int8_t part, uint8_t type)
{
    return ((uint32_t)tbyb << 24) | ((uint32_t)(uint8_t)part << 16)
           | ((uint32_t)type << 8) | 0xFFu;
}

/** @brief A start with only CHIP_RESET set and a NORMAL boot of slot A. */
static reset_regs_t chip_only(uint32_t chip_reset)
{
    reset_regs_t r;
    memset(&r, 0, sizeof(r));
    r.chip_reset = chip_reset;
    r.boot_ok = true;
    r.boot_word = boot_word(0u, 0, RESET_BOOT_TYPE_NORMAL);
    return r;
}

/** @brief A watchdog reset (PSM, REASON @p reason) and the boot after it. */
static reset_regs_t watchdog(uint32_t reason, uint8_t type, uint32_t p0,
                             uint32_t p1)
{
    reset_regs_t r = chip_only(RESET_CHIP_HAD_WATCHDOG_RESET_PSM);
    r.wd_reason = reason;
    r.boot_word = boot_word(0u, 1, type);
    r.wd_scratch2 = p0;
    r.wd_scratch3 = p1;
    return r;
}

TEST_CASE(power_on_reads_por)
{
    const reset_regs_t r = chip_only(RESET_CHIP_HAD_POR);
    CHECK_EQ(reset_cause_decode(&r), RESET_CAUSE_POR);
    CHECK_STR_EQ(reset_cause_word(reset_cause_decode(&r)), "POR");
}

TEST_CASE(brown_out_reads_bor)
{
    const reset_regs_t r = chip_only(RESET_CHIP_HAD_BOR);
    CHECK_EQ(reset_cause_decode(&r), RESET_CAUSE_BOR);
}

TEST_CASE(run_pin_low_reads_run_pin)
{
    const reset_regs_t r = chip_only(RESET_CHIP_HAD_RUN_LOW);
    CHECK_EQ(reset_cause_decode(&r), RESET_CAUSE_RUN_PIN);
    CHECK_STR_EQ(reset_cause_word(RESET_CAUSE_RUN_PIN), "RUN_PIN");
}

TEST_CASE(glitch_debugger_and_power_down_read_their_words)
{
    reset_regs_t r = chip_only(RESET_CHIP_HAD_GLITCH_DETECT);
    CHECK_EQ(reset_cause_decode(&r), RESET_CAUSE_GLITCH);
    r = chip_only(RESET_CHIP_HAD_DP_RESET_REQ);
    CHECK_EQ(reset_cause_decode(&r), RESET_CAUSE_DEBUGGER);
    r = chip_only(RESET_CHIP_HAD_RESCUE);
    CHECK_EQ(reset_cause_decode(&r), RESET_CAUSE_DEBUGGER);
    r = chip_only(RESET_CHIP_HAD_HZD_SYS_RESET_REQ);
    CHECK_EQ(reset_cause_decode(&r), RESET_CAUSE_DEBUGGER);
    r = chip_only(RESET_CHIP_HAD_SWCORE_PD);
    CHECK_EQ(reset_cause_decode(&r), RESET_CAUSE_POWER_DOWN);
}

/*
 * picotool -f: the reset interface reboots into BOOTSEL (p0 0, p1
 * 0xFFFFFFFF, no LED), the command runs, and picotool's reboot NORMAL with
 * p0 = -1 and p1 = 0 brings the application back.  The boot ROM's reboot()
 * runs the watchdog in timer mode.
 */
TEST_CASE(watchdog_after_a_bootsel_session_reads_bootsel_reboot)
{
    const reset_regs_t r = watchdog(RESET_WD_REASON_TIMER,
                                    RESET_BOOT_TYPE_NORMAL, RESET_PICOTOOL_P0,
                                    RESET_PICOTOOL_P1);
    CHECK_EQ(reset_cause_decode(&r), RESET_CAUSE_BOOTSEL_REBOOT);
    CHECK_STR_EQ(reset_cause_word(RESET_CAUSE_BOOTSEL_REBOOT),
                 "BOOTSEL_REBOOT");
    /* The BOOTSEL entry's own parameters do not match: only the reboot out
     * of BOOTSEL leaves them. */
    const reset_regs_t entry = watchdog(RESET_WD_REASON_TIMER,
                                        RESET_BOOT_TYPE_NORMAL, 0u,
                                        0xFFFFFFFFu);
    CHECK_EQ(reset_cause_decode(&entry), RESET_CAUSE_WATCHDOG);
    /* A boot word of type BOOTSEL. */
    const reset_regs_t sel = watchdog(RESET_WD_REASON_TIMER,
                                      RESET_BOOT_TYPE_BOOTSEL, 0u, 0u);
    CHECK_EQ(reset_cause_decode(&sel), RESET_CAUSE_BOOTSEL_REBOOT);
}

/* This firmware's update over CAN: reboot(FLASH_UPDATE, p0 = the slot). */
TEST_CASE(flash_update_reboot_reads_flash_update)
{
    reset_regs_t r = watchdog(RESET_WD_REASON_TIMER,
                              RESET_BOOT_TYPE_FLASH_UPDATE, SLOT_B_XIP, 0u);
    r.boot_word = boot_word(0x01u, 1, RESET_BOOT_TYPE_FLASH_UPDATE);
    r.reboot_params[0] = SLOT_B_XIP;
    CHECK_EQ(reset_cause_decode(&r), RESET_CAUSE_FLASH_UPDATE);
    /* The chained flag does not hide the type. */
    r.boot_word = boot_word(0x01u, 1, (uint8_t)(RESET_BOOT_TYPE_FLASH_UPDATE
                                                | RESET_BOOT_TYPE_CHAINED_FLAG));
    CHECK_EQ(reset_cause_boot_type(&r), (int)RESET_BOOT_TYPE_FLASH_UPDATE);
    CHECK_EQ(reset_cause_decode(&r), RESET_CAUSE_FLASH_UPDATE);
    /* picotool load -x into SRAM. */
    const reset_regs_t ram = watchdog(RESET_WD_REASON_TIMER,
                                      RESET_BOOT_TYPE_RAM_IMAGE, 0x20000000u,
                                      0x82000u);
    CHECK_EQ(reset_cause_decode(&ram), RESET_CAUSE_RAM_IMAGE);
}

/*
 * The boot ROM's 16.7s TBYB timer: an update not bought.  The watchdog
 * counts down, the boot is NORMAL, and SCRATCH2 still holds the update's
 * p0.
 */
TEST_CASE(tbyb_timer_reads_watchdog)
{
    const reset_regs_t r = watchdog(RESET_WD_REASON_TIMER,
                                    RESET_BOOT_TYPE_NORMAL, SLOT_B_XIP, 0u);
    CHECK_EQ(reset_cause_decode(&r), RESET_CAUSE_WATCHDOG);
    CHECK_STR_EQ(reset_cause_word(RESET_CAUSE_WATCHDOG), "WATCHDOG");
    /* REASON alone, no CHIP_RESET bit, is a watchdog reset too. */
    reset_regs_t bare = r;
    bare.chip_reset = 0u;
    CHECK_EQ(reset_cause_decode(&bare), RESET_CAUSE_WATCHDOG);
    /* A CHIP_RESET watchdog bit with REASON cleared, each of the four. */
    const uint32_t bits[] = { RESET_CHIP_HAD_WATCHDOG_RESET_POWMAN_ASYNC,
                              RESET_CHIP_HAD_WATCHDOG_RESET_POWMAN,
                              RESET_CHIP_HAD_WATCHDOG_RESET_SWCORE,
                              RESET_CHIP_HAD_WATCHDOG_RESET_PSM };
    for (size_t i = 0; i < sizeof(bits) / sizeof(bits[0]); ++i) {
        reset_regs_t w = chip_only(bits[i]);
        CHECK_EQ(reset_cause_decode(&w), RESET_CAUSE_WATCHDOG);
    }
}

TEST_CASE(forced_watchdog_reads_sw_reset)
{
    const reset_regs_t r = watchdog(RESET_WD_REASON_FORCE,
                                    RESET_BOOT_TYPE_NORMAL, 0u, 0u);
    CHECK_EQ(reset_cause_decode(&r), RESET_CAUSE_SW_RESET);
    /* Without boot info the reason alone decides. */
    reset_regs_t blind = r;
    blind.boot_ok = false;
    CHECK_EQ(reset_cause_boot_type(&blind), -1);
    CHECK_EQ(reset_cause_decode(&blind), RESET_CAUSE_SW_RESET);
    blind.wd_reason = RESET_WD_REASON_TIMER;
    blind.wd_scratch2 = RESET_PICOTOOL_P0;
    CHECK_EQ(reset_cause_decode(&blind), RESET_CAUSE_WATCHDOG);
}

TEST_CASE(nothing_set_reads_unknown)
{
    const reset_regs_t r = chip_only(0u);
    CHECK_EQ(reset_cause_decode(&r), RESET_CAUSE_UNKNOWN);
    /* The flags that are not causes: DOUBLE_TAP and RESCUE_FLAG. */
    const reset_regs_t flags = chip_only(0x00000011u);
    CHECK_EQ(reset_cause_decode(&flags), RESET_CAUSE_UNKNOWN);
    /* REASON bits outside TIMER and FORCE are not a watchdog reset. */
    reset_regs_t high = chip_only(0u);
    high.wd_reason = 0x4u;
    CHECK_EQ(reset_cause_decode(&high), RESET_CAUSE_UNKNOWN);
    CHECK_EQ(reset_cause_decode(NULL), RESET_CAUSE_UNKNOWN);
    CHECK_EQ(reset_cause_boot_type(NULL), -1);
    CHECK_STR_EQ(reset_cause_word(RESET_CAUSE_UNKNOWN), "UNKNOWN");
    CHECK_STR_EQ(reset_cause_word(RESET_CAUSE_COUNT), "UNKNOWN");
    CHECK_STR_EQ(reset_cause_word((reset_cause_t)-1), "UNKNOWN");
}

/* Two causes at once: the earlier in the order wins. */
TEST_CASE(two_bits_at_once_take_the_first_in_order)
{
    reset_regs_t r = chip_only(RESET_CHIP_HAD_POR | RESET_CHIP_HAD_RUN_LOW);
    CHECK_EQ(reset_cause_decode(&r), RESET_CAUSE_POR);
    r = chip_only(RESET_CHIP_HAD_BOR | RESET_CHIP_HAD_RUN_LOW);
    CHECK_EQ(reset_cause_decode(&r), RESET_CAUSE_BOR);
    r = chip_only(RESET_CHIP_HAD_RUN_LOW | RESET_CHIP_HAD_WATCHDOG_RESET_PSM);
    CHECK_EQ(reset_cause_decode(&r), RESET_CAUSE_RUN_PIN);
    /* With REASON set the watchdog decides, whatever CHIP_RESET holds. */
    r.wd_reason = RESET_WD_REASON_TIMER;
    CHECK_EQ(reset_cause_decode(&r), RESET_CAUSE_WATCHDOG);
    r = chip_only(RESET_CHIP_HAD_GLITCH_DETECT | RESET_CHIP_HAD_DP_RESET_REQ);
    CHECK_EQ(reset_cause_decode(&r), RESET_CAUSE_GLITCH);
    r = chip_only(RESET_CHIP_HAD_DP_RESET_REQ | RESET_CHIP_HAD_SWCORE_PD);
    CHECK_EQ(reset_cause_decode(&r), RESET_CAUSE_DEBUGGER);
    r = watchdog(RESET_WD_REASON_TIMER | RESET_WD_REASON_FORCE,
                 RESET_BOOT_TYPE_NORMAL, 0u, 0u);
    CHECK_EQ(reset_cause_decode(&r), RESET_CAUSE_SW_RESET);
}

/* Every word is at most 14 characters, and every cause has one. */
TEST_CASE(every_cause_has_a_short_word)
{
    for (int c = 0; c < (int)RESET_CAUSE_COUNT; ++c) {
        const char *w = reset_cause_word((reset_cause_t)c);
        CHECK(w != NULL);
        CHECK(strlen(w) >= 3u && strlen(w) <= 14u);
    }
}

/*
 * The station's pattern: powered up, then the update over CAN.  reboot()
 * resets the system through PSM WDSEL, not the chip, so CHIP_RESET still
 * holds HAD_POR from the power-up, REASON reads TIMER, and the boot is
 * FLASH_UPDATE.
 */
TEST_CASE(a_sticky_por_after_a_flash_update_reads_flash_update)
{
    reset_regs_t r = chip_only(RESET_CHIP_HAD_POR);
    r.wd_reason = RESET_WD_REASON_TIMER;
    r.boot_word = boot_word(0x01u, 1, RESET_BOOT_TYPE_FLASH_UPDATE);
    r.reboot_params[0] = SLOT_B_XIP;
    r.wd_scratch2 = SLOT_B_XIP;
    CHECK_EQ(reset_cause_decode(&r), RESET_CAUSE_FLASH_UPDATE);
    r.wd_reason = RESET_WD_REASON_FORCE;
    CHECK_EQ(reset_cause_decode(&r), RESET_CAUSE_FLASH_UPDATE);
    /* The boot type alone decides, REASON or not. */
    r.wd_reason = 0u;
    CHECK_EQ(reset_cause_decode(&r), RESET_CAUSE_FLASH_UPDATE);
}

/* HAD_POR from the power-up, then the TBYB timer: a NORMAL boot. */
TEST_CASE(a_sticky_por_with_a_watchdog_timer_reads_watchdog)
{
    reset_regs_t r = chip_only(RESET_CHIP_HAD_POR);
    r.wd_reason = RESET_WD_REASON_TIMER;
    r.wd_scratch2 = SLOT_B_XIP;
    CHECK_EQ(reset_cause_decode(&r), RESET_CAUSE_WATCHDOG);
    r.wd_reason = RESET_WD_REASON_FORCE;
    CHECK_EQ(reset_cause_decode(&r), RESET_CAUSE_SW_RESET);
}

/* HAD_POR from the power-up, then picotool -f and its reboot out of
 * BOOTSEL. */
TEST_CASE(a_sticky_por_with_picotool_scratch_reads_bootsel_reboot)
{
    reset_regs_t r = chip_only(RESET_CHIP_HAD_POR);
    r.wd_reason = RESET_WD_REASON_TIMER;
    r.wd_scratch2 = RESET_PICOTOOL_P0;
    r.wd_scratch3 = RESET_PICOTOOL_P1;
    CHECK_EQ(reset_cause_decode(&r), RESET_CAUSE_BOOTSEL_REBOOT);
    /* picotool's values with REASON 0 cannot follow a reboot(): a
     * chip-level reset clears the scratch registers, and HAD_POR names it. */
    r.wd_reason = 0u;
    CHECK_EQ(reset_cause_decode(&r), RESET_CAUSE_POR);
}

/* A fresh power-up: REASON 0, the scratch registers 0. */
TEST_CASE(a_fresh_por_reads_por)
{
    const reset_regs_t r = chip_only(RESET_CHIP_HAD_POR);
    CHECK_EQ(r.wd_reason, 0u);
    CHECK_EQ(reset_cause_decode(&r), RESET_CAUSE_POR);
}

TEST_CASE(the_full_form_prints_every_raw_value)
{
    char buf[RESET_CAUSE_FULL_MAX];
    reset_regs_t r = watchdog(RESET_WD_REASON_TIMER,
                              RESET_BOOT_TYPE_FLASH_UPDATE, SLOT_B_XIP, 0u);
    r.boot_word = boot_word(0x01u, 1, RESET_BOOT_TYPE_FLASH_UPDATE);
    r.reboot_params[0] = SLOT_B_XIP;
    CHECK_EQ(reset_cause_full(&r, buf, sizeof(buf)), strlen(buf));
    CHECK_STR_EQ(buf, "FLASH_UPDATE (chip_reset 0x10000000 reason 0x1 boot "
                      "0x010104FF params 0x101FD000 0x00000000 scratch "
                      "0x101FD000 0x00000000)");
    /* Without boot info. */
    r.boot_ok = false;
    CHECK_EQ(reset_cause_full(&r, buf, sizeof(buf)), strlen(buf));
    CHECK_STR_EQ(buf, "WATCHDOG (chip_reset 0x10000000 reason 0x1 boot none "
                      "scratch 0x101FD000 0x00000000)");
    /* The longest: the longest word and every field at 8 digits. */
    reset_regs_t big;
    memset(&big, 0xFF, sizeof(big));
    big.boot_ok = true;
    big.chip_reset = 0u;
    big.wd_reason = 0xFFFFFFFFu;
    big.boot_word = 0xFFFF00FFu;
    big.wd_scratch2 = RESET_PICOTOOL_P0;
    big.wd_scratch3 = RESET_PICOTOOL_P1;
    CHECK_EQ(reset_cause_decode(&big), RESET_CAUSE_BOOTSEL_REBOOT);
    const size_t len = reset_cause_full(&big, buf, sizeof(buf));
    CHECK_EQ(len, strlen(buf));
    CHECK(len < RESET_CAUSE_FULL_MAX);
    CHECK_EQ(reset_cause_full(NULL, buf, sizeof(buf)), strlen(buf));
    CHECK_STR_EQ(buf, "UNKNOWN (chip_reset 0x00000000 reason 0x0 boot none "
                      "scratch 0x00000000 0x00000000)");
}

int main(void)
{
    RUN(power_on_reads_por);
    RUN(brown_out_reads_bor);
    RUN(run_pin_low_reads_run_pin);
    RUN(glitch_debugger_and_power_down_read_their_words);
    RUN(watchdog_after_a_bootsel_session_reads_bootsel_reboot);
    RUN(flash_update_reboot_reads_flash_update);
    RUN(tbyb_timer_reads_watchdog);
    RUN(forced_watchdog_reads_sw_reset);
    RUN(nothing_set_reads_unknown);
    RUN(two_bits_at_once_take_the_first_in_order);
    RUN(every_cause_has_a_short_word);
    RUN(a_sticky_por_after_a_flash_update_reads_flash_update);
    RUN(a_sticky_por_with_a_watchdog_timer_reads_watchdog);
    RUN(a_sticky_por_with_picotool_scratch_reads_bootsel_reboot);
    RUN(a_fresh_por_reads_por);
    RUN(the_full_form_prints_every_raw_value);
    return test_summary("reset_cause");
}
