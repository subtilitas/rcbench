/**
 * @file
 * @brief The console report every 3s: the flash store, the running slot
 * and why this start happened, the CAN bus and the current monitors as the
 * pages show them.
 *
 * See iomcu.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "pico.h"

#include "boot/bootrom_constants.h"
#include "hardware/structs/powman.h"
#include "hardware/structs/watchdog.h"
#include "pico/bootrom.h"

#include "iomcu.h"

#include "reset_cause.h"
#include "sense_limit.h"
#include "sense_map.h"
#include "update_hw.h"
#include "xl2515.h"
#if IOMCU_CONSOLE
#include "console_hw.h"
#endif

/**
 * @brief This file's report lines.
 *
 * The bench console build sends them through the console's output: none lands
 * inside a console line that leaves in pieces, and none waits for the host.
 * Every other build prints them, and printf() waits for the host for up to 500
 * ms (PICO_STDIO_USB_STDOUT_TIMEOUT_US).
 */
#if IOMCU_CONSOLE
#define IOMCU_SAY console_hw_say
#else
#define IOMCU_SAY printf
#endif

/* reset_cause.h holds the SDK's bit values without the SDK. */
_Static_assert(RESET_CHIP_HAD_POR == POWMAN_CHIP_RESET_HAD_POR_BITS, "POR");
_Static_assert(RESET_CHIP_HAD_BOR == POWMAN_CHIP_RESET_HAD_BOR_BITS, "BOR");
_Static_assert(RESET_CHIP_HAD_RUN_LOW == POWMAN_CHIP_RESET_HAD_RUN_LOW_BITS,
               "RUN_LOW");
_Static_assert(RESET_CHIP_HAD_DP_RESET_REQ
                   == POWMAN_CHIP_RESET_HAD_DP_RESET_REQ_BITS, "DP_RESET_REQ");
_Static_assert(RESET_CHIP_HAD_RESCUE == POWMAN_CHIP_RESET_HAD_RESCUE_BITS,
               "RESCUE");
_Static_assert(RESET_CHIP_HAD_WATCHDOG_RESET_POWMAN_ASYNC
                   == POWMAN_CHIP_RESET_HAD_WATCHDOG_RESET_POWMAN_ASYNC_BITS,
               "WATCHDOG_RESET_POWMAN_ASYNC");
_Static_assert(RESET_CHIP_HAD_WATCHDOG_RESET_POWMAN
                   == POWMAN_CHIP_RESET_HAD_WATCHDOG_RESET_POWMAN_BITS,
               "WATCHDOG_RESET_POWMAN");
_Static_assert(RESET_CHIP_HAD_WATCHDOG_RESET_SWCORE
                   == POWMAN_CHIP_RESET_HAD_WATCHDOG_RESET_SWCORE_BITS,
               "WATCHDOG_RESET_SWCORE");
_Static_assert(RESET_CHIP_HAD_SWCORE_PD == POWMAN_CHIP_RESET_HAD_SWCORE_PD_BITS,
               "SWCORE_PD");
_Static_assert(RESET_CHIP_HAD_GLITCH_DETECT
                   == POWMAN_CHIP_RESET_HAD_GLITCH_DETECT_BITS, "GLITCH_DETECT");
_Static_assert(RESET_CHIP_HAD_HZD_SYS_RESET_REQ
                   == POWMAN_CHIP_RESET_HAD_HZD_SYS_RESET_REQ_BITS,
               "HZD_SYS_RESET_REQ");
_Static_assert(RESET_CHIP_HAD_WATCHDOG_RESET_PSM
                   == POWMAN_CHIP_RESET_HAD_WATCHDOG_RESET_PSM_BITS,
               "WATCHDOG_RESET_PSM");
_Static_assert(RESET_WD_REASON_TIMER == WATCHDOG_REASON_TIMER_BITS, "TIMER");
_Static_assert(RESET_WD_REASON_FORCE == WATCHDOG_REASON_FORCE_BITS, "FORCE");
_Static_assert(RESET_BOOT_TYPE_NORMAL == BOOT_TYPE_NORMAL, "NORMAL");
_Static_assert(RESET_BOOT_TYPE_BOOTSEL == BOOT_TYPE_BOOTSEL, "BOOTSEL");
_Static_assert(RESET_BOOT_TYPE_RAM_IMAGE == BOOT_TYPE_RAM_IMAGE, "RAM_IMAGE");
_Static_assert(RESET_BOOT_TYPE_FLASH_UPDATE == BOOT_TYPE_FLASH_UPDATE,
               "FLASH_UPDATE");
_Static_assert(RESET_BOOT_TYPE_PC_SP == BOOT_TYPE_PC_SP, "PC_SP");
_Static_assert(RESET_BOOT_TYPE_CHAINED_FLAG == BOOT_TYPE_CHAINED_FLAG,
               "CHAINED_FLAG");
_Static_assert(RESET_PICOTOOL_P0 == (uint32_t)BOOT_PARTITION_NONE,
               "picotool's p0 is BOOT_PARTITION_NONE");

/** @brief The registers as reset_start() read them. */
static reset_regs_t s_reset;

void reset_start(void)
{
    /* CHIP_RESET's HAD_* bits are RO (datasheet table 489) and change only
     * at a chip-level reset, so they are read and not cleared; reason
     * decides whether they belong to this start (reset_cause.h). */
    s_reset.chip_reset  = powman_hw->chip_reset;
    s_reset.wd_reason   = watchdog_hw->reason;
    s_reset.wd_scratch2 = watchdog_hw->scratch[2];
    s_reset.wd_scratch3 = watchdog_hw->scratch[3];
    /* Free for the application outside a one-shot boot, and written by the
     * next reboot() of the boot ROM: zeroed, so a later reset that writes
     * neither is not read as picotool's again. */
    watchdog_hw->scratch[2] = 0u;
    watchdog_hw->scratch[3] = 0u;
    boot_info_t info;
    memset(&info, 0, sizeof(info));
    s_reset.boot_ok = rom_get_boot_info(&info) != 0;
    s_reset.boot_word = info.boot_word;
    s_reset.reboot_params[0] = info.reboot_params[0];
    s_reset.reboot_params[1] = info.reboot_params[1];

    char text[RESET_CAUSE_FULL_MAX];
    (void)reset_cause_full(&s_reset, text, sizeof(text));
    /* Before the console exists, like can_start()'s line; can_report()
     * repeats it every 3s. */
    printf("rcbench-iomcu: reset %s\n", text);
}

#if IOMCU_CONSOLE
void reset_say(void)
{
    char why[RESET_CAUSE_FULL_MAX];

    (void)reset_cause_full(&s_reset, why, sizeof(why));
    IOMCU_SAY("rcbench-iomcu: reset %s\n", why);
}
#endif

/**
 * @brief The current monitors as the pages show them, with can_report()
 * every 3s, while either part is enabled: what a bench session reads before
 * the panel has a screen for them.
 *
 * The registers as published, in their own
 * units: FLAGS, PRESENT and the IDs in hex, the reset counts of the INA228
 * and the INA3221, BENCH's 10mV and 10mA, CH1's
 * mA and mV, times in 0.1ms; and the INA3221 map with each channel's mA.
 */
static void sense_report(void)
{
    if (!sense_page_enabled(&s_sense)) {
        return;
    }
    sense_sync();
    const uint16_t *n = s_sense.sense;
    const uint16_t *v = s_sense.servo;
    const uint16_t *b = s_state.bench;
    IOMCU_SAY("rcbench-iomcu: sense flags 0x%04X present 0x%04X ids 0x%04X "
              "0x%04X errors %u resets %u %u | bench %u cV %u cA %u mAh %u dWh "
              "flags 0x%02X | temp %d dC\n",
              n[LINK_SN_FLAGS], n[LINK_SN_PRESENT], n[LINK_SN_I228_ID],
              n[LINK_SN_I3221_ID], n[LINK_SN_ERRORS],
              (unsigned)LINK_SN_RESETS_I228(n[LINK_SN_RESETS]),
              (unsigned)LINK_SN_RESETS_I3221(n[LINK_SN_RESETS]),
              b[LINK_BN_VOLTAGE_CV],
              b[LINK_BN_CURRENT_CA], b[LINK_BN_CHARGE_MAH],
              b[LINK_BN_ENERGY_DWH],
              b[LINK_BN_FLAGS], (int)(int16_t)n[LINK_SN_I228_TEMP_DC]);
    IOMCU_SAY("rcbench-iomcu: sense window %u CH1 %d mA %u mV "
              "ch_flags 0x%02X | capture state %u seq %u move %u arrive %u\n",
              v[LINK_SS_WINDOW], (int)(int16_t)v[LINK_SS_CH_MEAN_MA],
              v[LINK_SS_CH_MEAN_MV], v[LINK_SS_CH_FLAGS], v[LINK_SS_CAP_STATE],
              v[LINK_SS_CAP_SEQ], v[LINK_SS_CAP_MOVE_T],
              v[LINK_SS_CAP_ARRIVE_T]);
    /* The INA3221 map: fitted parts, FAST, its flags (bit 8 in force, bit
     * 9 derived from SENSE) and each channel's mean mA, 0 to 11. */
    uint16_t m[LINK_SM_COUNT];
    sense_map_read(&s_sense, 0u, (uint8_t)LINK_SM_COUNT, m);
    int16_t ma[LINK_SM_CHANNELS];
    for (unsigned c = 0; c < LINK_SM_CHANNELS; ++c) {
        ma[c] = (int16_t)s_sense.xwin[c / LINK_SX_PER_PAGE]
                    [LINK_SX_E(c % LINK_SX_PER_PAGE, LINK_SX_E_MEAN_MA)];
    }
    /* And SENSE_LIMIT's OVER and OVER_COUNT, read without clearing. */
    uint16_t over[2];
    sense_limit_peek(&s_sense, (uint8_t)LINK_SL_OVER, 2u, over);
    IOMCU_SAY("rcbench-iomcu: sense map fit 0x%X fast %u flags 0x%04X | mA "
              "%d %d %d %d %d %d %d %d %d %d %d %d | over 0x%03X count %u\n",
              m[LINK_SM_ENABLE], m[LINK_SM_FAST], m[LINK_SM_FLAGS], ma[0],
              ma[1], ma[2], ma[3], ma[4], ma[5], ma[6], ma[7], ma[8], ma[9],
              ma[10], ma[11], over[0], over[1]);
}

/*
 * Repeated every 3s rather than printed at boot alone.  USB (Universal
 * Serial Bus) CDC (communications device class) does not exist until a host
 * enumerates it, so anything printed before the terminal opens is lost.
 */
void can_report(uint32_t now)
{
    static uint32_t last;
    if (now - last < 3000u && last != 0u) {
        return;
    }
    last = now;

    if (out_store_is_off()) {
        IOMCU_SAY("rcbench-iomcu: flash store OFF -- core 1 did not "
                  "register for the flash lock-out; set-ups run from RAM and "
                  "are lost at restart\n");
    }
    /* Which slot runs: a bench without the partition table takes its
     * next image over USB only. */
    if (update_hw_slot() < 0) {
        IOMCU_SAY("rcbench-iomcu: no partition table -- firmware updates over "
                  "CAN need one USB load of rcbench-iomcu-install.uf2\n");
    } else {
        IOMCU_SAY("rcbench-iomcu: running from slot %c%s\n",
                  update_hw_slot() == 0 ? 'A' : 'B',
                  update_hw_on_trial() ? ", on trial until the panel buys it"
                                       : "");
    }
    /* Why this start happened, with every raw value: the start-up line
     * leaves before a host has the port open, so it is repeated here. */
    char why[RESET_CAUSE_FULL_MAX];
    (void)reset_cause_full(&s_reset, why, sizeof(why));
    IOMCU_SAY("rcbench-iomcu: reset %s\n", why);
    if (!s_can_up) {
        IOMCU_SAY("rcbench-iomcu: CAN did not answer on SPI -- module fitted? "
                  "wiring on GP9-12?\n");
        sense_report();
        return;
    }
    uint8_t tec = 0, rec = 0, eflg = 0;
    xl2515_errors(&tec, &rec, &eflg);
    /*
     * Requests first, because it is the number that answers "is the other
     * end talking to me at all".  Echoes are self-test traffic and are zero
     * in ordinary use, which reads as nothing arriving when this line carried
     * them alone.  A request count standing still while the panel says NO
     * LINK means the panel has stopped transmitting; one that climbs while
     * the panel says NO LINK means the answers are not getting back.
     */
    IOMCU_SAY("rcbench-iomcu: CAN up, %u bit/s, %lu requests served, "
              "%lu self-test echoes, tx_err %u rx_err %u eflg 0x%02X\n",
              (unsigned)IOMCU_CAN_BITRATE, (unsigned long)s_frames,
              (unsigned long)s_can_echoes, tec, rec, eflg);
    /*
     * Said in words: an overflow is the one thing that explains a frame
     * going missing while the bus reports no error.  The part has two
     * receive buffers, so anything that stops this loop for two frame times
     * costs a frame, and a printf to a USB host that has stopped reading is
     * such a thing.
     */
    if (s_can_overflows > 0u) {
        IOMCU_SAY("rcbench-iomcu: CAN receive buffers overran %lu time(s) -- "
                  "frames arrived with nowhere to put them; not a bus fault\n",
                  (unsigned long)s_can_overflows);
    }
    sense_report();
}

