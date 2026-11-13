/**
 * @file
 * @brief The console report every 3s: the flash store, the running slot
 * and why this start happened, the flash's read set-up, the CAN bus, the
 * current monitors as the pages show them, and the main loop's pass times
 * with the XIP cache's misses.
 *
 * The lines are shared/bench/report_lines.c's, and report_steps.c runs
 * them: one snapshot at the due pass, then at most one line per pass, and
 * only with REPORT_LINE_MAX bytes of room in the transport, so no line
 * waits for the host.  This file holds the snapshot and the transport.
 *
 * See iomcu.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "pico.h"

#include "boot/bootrom_constants.h"
#include "hardware/regs/qmi.h"
#include "hardware/structs/powman.h"
#include "hardware/structs/qmi.h"
#include "hardware/structs/watchdog.h"
#include "hardware/structs/xip.h"
#include "hardware/timer.h"
#include "pico/bootrom.h"
#include "pico/stdio.h"
#include "pico/stdio_usb.h"
#include "tusb.h"

#include "iomcu.h"

#include "boot_diag.h"
#include "report_lines.h"
#include "report_steps.h"
#include "reset_cause.h"
#include "sense_limit.h"
#include "sense_map.h"
#include "update_hw.h"
#include "xl2515.h"
#if IOMCU_CONSOLE
#include "console_hw.h"
#endif

/**
 * @brief The longest a printf() waits for room in the USB transmit buffer
 * after the host last took bytes, in us.
 *
 * The report does not use printf(); the boot lines and the store's lines
 * (main.c) do.  The main loop samples the heartbeat, and 150ms without an
 * edge switches every output off: a USB host that holds the port open and
 * stops reading holds such a printf() for this long at most, not the
 * SDK's 500,000us.  firmware/iomcu/CMakeLists.txt sets
 * PICO_STDIO_USB_STDOUT_TIMEOUT_US and says why; the assert below holds the
 * two equal.
 */
#define IOMCU_USB_STDOUT_WAIT_US 1000u

/**
 * @brief The USB transmit buffer, in bytes: CFG_TUD_CDC_TX_BUFSIZE.
 *
 * The report at its widest, REPORT_BYTES_MAX (1,639 bytes) and one "\r" a
 * line, fits whole: a host that read the last report finds room for every
 * line of this one, one line a pass.
 */
#define IOMCU_USB_TX_BUFFER_BYTES 2048u

_Static_assert(PICO_STDIO_USB_STDOUT_TIMEOUT_US == IOMCU_USB_STDOUT_WAIT_US,
               "a stalled USB host must not hold the main loop");
_Static_assert(CFG_TUD_CDC_TX_BUFSIZE == IOMCU_USB_TX_BUFFER_BYTES,
               "the 3s report must fit the USB transmit buffer");
_Static_assert(REPORT_BYTES_MAX + REPORT_LINES <= IOMCU_USB_TX_BUFFER_BYTES,
               "the 3s report must fit the USB transmit buffer");
_Static_assert(LINK_SM_CHANNELS == REPORT_MAP_CHANNELS,
               "the map line prints every INA3221 channel");

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
/** @brief Why this start happened, as reset_start() read it. */
static char s_why[RESET_CAUSE_FULL_MAX];
/**
 * @brief The boot ROM's record of this boot: boot type, partition booted,
 * diagnostic source and word (boot_diag.h).
 */
static char s_boot[BOOT_DIAG_TEXT_MAX];

/**
 * @brief The flash's read set-up into the snapshot: QMI M0's RCMD, RFMT and
 * TIMING, and M1's with whether they differ from their reset values.
 *
 * The image carries no XIP setup of its own (PICO_EMBED_XIP_SETUP 0), so
 * these are what the boot ROM left: the read command, its width and the
 * clock divisor every flash read runs at.  The image runs from SRAM, so
 * only the output store and an update's checks read the flash.
 *
 * @param s  The snapshot.
 */
static void qmi_snap(report_snap_t *s)
{
    s->qmi_m0.rcmd   = qmi_hw->m[0].rcmd;
    s->qmi_m0.rfmt   = qmi_hw->m[0].rfmt;
    s->qmi_m0.timing = qmi_hw->m[0].timing;
    s->qmi_m1.rcmd   = qmi_hw->m[1].rcmd;
    s->qmi_m1.rfmt   = qmi_hw->m[1].rfmt;
    s->qmi_m1.timing = qmi_hw->m[1].timing;
    s->qmi_m1_set = s->qmi_m1.rcmd != QMI_M1_RCMD_RESET
                    || s->qmi_m1.rfmt != QMI_M1_RFMT_RESET
                    || s->qmi_m1.timing != QMI_M1_TIMING_RESET;
}

/**
 * @brief The XIP cache's misses since the last call, and its counters
 * cleared.
 *
 * CTR_ACC counts every XIP access and CTR_HIT those the cache served; both
 * are 32-bit, saturating, and cleared by any write.  They count the
 * accesses of both cores and of DMA alike, with no means to tell them
 * apart, and an uncached access counts as a miss.  The accesses between
 * the reads and the clear, a few dozen at most, are lost or counted as
 * misses.  At 150MHz the two cores make at most 300,000,000 accesses a
 * second, so 3s stays under the 4,294,967,295 a counter saturates at.
 *
 * @return CTR_ACC - CTR_HIT, or 0 when CTR_HIT is the larger.
 */
static uint32_t xip_take_misses(void)
{
    const uint32_t hit = xip_ctrl_hw->ctr_hit;
    const uint32_t acc = xip_ctrl_hw->ctr_acc;
    xip_ctrl_hw->ctr_hit = 0u;
    xip_ctrl_hw->ctr_acc = 0u;
    return acc >= hit ? acc - hit : 0u;
}

/**
 * @brief The flash line, formatted from the registers now.
 *
 * @param line  Receives the line.
 * @return The bytes @p line holds, or 0.
 */
static size_t flash_say_line(char line[REPORT_LINE_MAX])
{
    report_snap_t q;
    memset(&q, 0, sizeof(q));
    qmi_snap(&q);
    return report_line(&q, REPORT_L_FLASH, line, REPORT_LINE_MAX);
}

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
    s_reset.boot_diagnostic = info.boot_diagnostic;
    s_reset.reboot_params[0] = info.reboot_params[0];
    s_reset.reboot_params[1] = info.reboot_params[1];

    (void)reset_cause_full(&s_reset, s_why, sizeof(s_why));
    (void)boot_diag_text(&s_reset, s_boot, sizeof(s_boot));
    /* Before the console exists, like can_start()'s line; the report
     * repeats both every 3s. */
    printf("rcbench-iomcu: reset %s\n", s_why);
    printf("rcbench-iomcu: %s\n", s_boot);
    char line[REPORT_LINE_MAX];
    if (flash_say_line(line) > 0u) {
        printf("%s", line);
    }
}

#if IOMCU_CONSOLE
void reset_say(void)
{
    console_hw_say("rcbench-iomcu: reset %s\n", s_why);
    console_hw_say("rcbench-iomcu: %s\n", s_boot);
    char line[REPORT_LINE_MAX];
    if (flash_say_line(line) > 0u) {
        console_hw_say("%s", line);
    }
}
#endif

/**
 * @brief The current monitors as the pages show them, into the snapshot:
 * FLAGS, PRESENT and the IDs, the reset counts of the INA228 and the
 * INA3221, BENCH's 10mV and 10mA, CH1's mA and mV, times in 0.1ms, the
 * INA3221 map with each channel's mA, and SENSE_LIMIT's OVER and
 * OVER_COUNT read without clearing.
 *
 * @param s  The snapshot; its sense_on is already set.
 */
static void sense_snap(report_snap_t *s)
{
    sense_sync();
    const uint16_t *n = s_sense.sense;
    const uint16_t *v = s_sense.servo;
    const uint16_t *b = s_state.bench;
    s->sn_flags     = n[LINK_SN_FLAGS];
    s->sn_present   = n[LINK_SN_PRESENT];
    s->i228_id      = n[LINK_SN_I228_ID];
    s->i3221_id     = n[LINK_SN_I3221_ID];
    s->sn_errors    = n[LINK_SN_ERRORS];
    s->i228_resets  = LINK_SN_RESETS_I228(n[LINK_SN_RESETS]);
    s->i3221_resets = LINK_SN_RESETS_I3221(n[LINK_SN_RESETS]);
    s->temp_dc      = (int16_t)n[LINK_SN_I228_TEMP_DC];
    s->bn_cv        = b[LINK_BN_VOLTAGE_CV];
    s->bn_ca        = b[LINK_BN_CURRENT_CA];
    s->bn_mah       = b[LINK_BN_CHARGE_MAH];
    s->bn_dwh       = b[LINK_BN_ENERGY_DWH];
    s->bn_flags     = b[LINK_BN_FLAGS];
    s->ss_window    = v[LINK_SS_WINDOW];
    s->ch1_ma       = (int16_t)v[LINK_SS_CH_MEAN_MA];
    s->ch1_mv       = v[LINK_SS_CH_MEAN_MV];
    s->ch_flags     = v[LINK_SS_CH_FLAGS];
    s->cap_state    = v[LINK_SS_CAP_STATE];
    s->cap_seq      = v[LINK_SS_CAP_SEQ];
    s->cap_move     = v[LINK_SS_CAP_MOVE_T];
    s->cap_arrive   = v[LINK_SS_CAP_ARRIVE_T];
    /* The INA3221 map: fitted parts, FAST, its flags (bit 8 in force, bit
     * 9 derived from SENSE) and each channel's mean mA, 0 to 11. */
    uint16_t m[LINK_SM_COUNT];
    sense_map_read(&s_sense, 0u, (uint8_t)LINK_SM_COUNT, m);
    s->map_fit   = m[LINK_SM_ENABLE];
    s->map_fast  = m[LINK_SM_FAST];
    s->map_flags = m[LINK_SM_FLAGS];
    for (unsigned c = 0; c < LINK_SM_CHANNELS; ++c) {
        s->map_ma[c] = (int16_t)s_sense.xwin[c / LINK_SX_PER_PAGE]
                           [LINK_SX_E(c % LINK_SX_PER_PAGE, LINK_SX_E_MEAN_MA)];
    }
    uint16_t over[2];
    sense_limit_peek(&s_sense, (uint8_t)LINK_SL_OVER, 2u, over);
    s->over       = over[0];
    s->over_count = over[1];
}

/**
 * @brief report_io_t's snapshot: every value the report prints, at the
 * pass it is due, so one report shows one instant.  The loop figures, the
 * XIP cache's counters and the transmit queue's peak are read and cleared
 * here.
 *
 * @param ctx  Unused.
 * @param s    Receives the values.
 */
static void io_snap(void *ctx, report_snap_t *s)
{
    (void)ctx;
    memset(s, 0, sizeof(*s));
    s->store_off = out_store_is_off();
    s->slot      = (int8_t)update_hw_slot();
    s->on_trial  = update_hw_on_trial();
    s->reset     = s_why;
    s->boot      = s_boot;
    qmi_snap(s);
    s->can_up    = s_can_up;
    if (s_can_up) {
        s->bitrate  = IOMCU_CAN_BITRATE;
        s->requests = s_frames;
        s->echoes   = s_can_echoes;
        xl2515_errors(&s->tec, &s->rec, &s->eflg);
        s->tx_drop  = s_can_txq.dropped;
        s->txq_max  = can_txq_take_peak(&s_can_txq);
        s->overruns = s_can_overflows;
    }
    s->sense_on = sense_page_enabled(&s_sense);
    if (s->sense_on) {
        sense_snap(s);
    }
    pass_stats_take(&s_pass, &s->loop);
    s->xip_miss = xip_take_misses();
}

/**
 * @brief report_io_t's host: the bench console build always takes lines
 * (the USB port or the panel's CONSOLE page reads them); every other build
 * while a USB host has the port open.
 *
 * @param ctx  Unused.
 * @return true when the report is to be written.
 */
static bool io_host(void *ctx)
{
    (void)ctx;
#if IOMCU_CONSOLE
    return true;
#else
    return stdio_usb_connected();
#endif
}

/**
 * @brief report_io_t's room: the bench console's output never waits, so
 * it takes a line of REPORT_LINE_MAX bytes on any pass; the USB port has
 * what its transmit buffer has free now.
 *
 * @param ctx  Unused.
 * @return Bytes a line may take without a wait.
 */
static size_t io_room(void *ctx)
{
    (void)ctx;
#if IOMCU_CONSOLE
    return REPORT_LINE_MAX;
#else
    return stdio_usb_connected() ? (size_t)tud_cdc_write_available() : 0u;
#endif
}

/**
 * @brief report_io_t's output: through the bench console's output in that
 * build, so no report line lands inside a console line; else to USB
 * stdio with "\n" sent as "\r\n", as printf() sends it.  The room check
 * before it means the write finds room for the whole line.
 *
 * @param ctx   Unused.
 * @param line  The line, terminated.
 * @param n     Its length.
 */
static void io_put(void *ctx, const char *line, size_t n)
{
    (void)ctx;
#if IOMCU_CONSOLE
    (void)n;
    console_hw_say("%s", line);
#else
    (void)stdio_put_string(line, (int)n, false, true);
#endif
}

/** @brief The report's transport and snapshot. */
static const report_io_t k_report_io = {
    io_host, io_room, io_snap, io_put, NULL,
};

/**
 * @brief The report's progress: the line due next, the snapshot.  Zero at
 * start, which is report_steps_init()'s state: the first pass is due.
 */
static report_steps_t s_report;

void can_report(uint32_t now)
{
    const uint32_t t0 = time_us_32();
    (void)report_steps_pass(&s_report, &k_report_io, now);
    pass_stats_section(&s_pass, PASS_SEC_REPORT, t0, time_us_32());
}
