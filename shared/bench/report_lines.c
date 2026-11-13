/**
 * @file
 * @brief The lines of the IOmcu's 3s console report; see report_lines.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "report_lines.h"

#include <stdarg.h>
#include <stdio.h>

/**
 * @brief snprintf() into @p buf, the length clamped to what @p buf holds.
 *
 * @param buf  Receives the text and a terminator.
 * @param cap  Bytes @p buf holds, 1 or more.
 * @param fmt  The format.
 * @return The bytes written, the terminator not counted.
 */
static size_t say(char *buf, size_t cap, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

static size_t say(char *buf, size_t cap, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(buf, cap, fmt, ap);
    va_end(ap);
    if (n < 0) {
        buf[0] = '\0';
        return 0u;
    }
    return (size_t)n < cap ? (size_t)n : cap - 1u;
}

/**
 * @brief A text of the snapshot, or "" for none.
 *
 * @param t  The text, or NULL.
 * @return @p t, or "".
 */
static const char *text(const char *t)
{
    return t != NULL ? t : "";
}

/**
 * @brief The SENSE and BENCH line.
 *
 * @param s    The snapshot.
 * @param buf  Receives the line.
 * @param cap  Bytes @p buf holds.
 * @return The bytes written.
 */
static size_t sense_line(const report_snap_t *s, char *buf, size_t cap)
{
    return say(buf, cap,
               "rcbench-iomcu: sense flags 0x%04X present 0x%04X ids 0x%04X "
               "0x%04X errors %u resets %u %u | bench %u cV %u cA %u mAh %u "
               "dWh flags 0x%02X | temp %d dC\n",
               (unsigned)s->sn_flags, (unsigned)s->sn_present,
               (unsigned)s->i228_id, (unsigned)s->i3221_id,
               (unsigned)s->sn_errors, (unsigned)s->i228_resets,
               (unsigned)s->i3221_resets, (unsigned)s->bn_cv,
               (unsigned)s->bn_ca, (unsigned)s->bn_mah, (unsigned)s->bn_dwh,
               (unsigned)s->bn_flags, (int)s->temp_dc);
}

/**
 * @brief The SERVO_SENSE line: CH1's window and the capture.
 *
 * @param s    The snapshot.
 * @param buf  Receives the line.
 * @param cap  Bytes @p buf holds.
 * @return The bytes written.
 */
static size_t window_line(const report_snap_t *s, char *buf, size_t cap)
{
    return say(buf, cap,
               "rcbench-iomcu: sense window %u CH1 %d mA %u mV "
               "ch_flags 0x%02X | capture state %u seq %u move %u arrive "
               "%u\n",
               (unsigned)s->ss_window, (int)s->ch1_ma, (unsigned)s->ch1_mv,
               (unsigned)s->ch_flags, (unsigned)s->cap_state,
               (unsigned)s->cap_seq, (unsigned)s->cap_move,
               (unsigned)s->cap_arrive);
}

/**
 * @brief The INA3221 map line: fitted parts, FAST, its flags, each
 *        channel's mean mA, 0 to 11, and SENSE_LIMIT's OVER and OVER_COUNT.
 *
 * @param s    The snapshot.
 * @param buf  Receives the line.
 * @param cap  Bytes @p buf holds.
 * @return The bytes written.
 */
static size_t map_line(const report_snap_t *s, char *buf, size_t cap)
{
    const int16_t *ma = s->map_ma;
    return say(buf, cap,
               "rcbench-iomcu: sense map fit 0x%X fast %u flags 0x%04X | mA "
               "%d %d %d %d %d %d %d %d %d %d %d %d | over 0x%03X count %u\n",
               (unsigned)s->map_fit, (unsigned)s->map_fast,
               (unsigned)s->map_flags, (int)ma[0], (int)ma[1], (int)ma[2],
               (int)ma[3], (int)ma[4], (int)ma[5], (int)ma[6], (int)ma[7],
               (int)ma[8], (int)ma[9], (int)ma[10], (int)ma[11],
               (unsigned)s->over, (unsigned)s->over_count);
}

size_t report_line(const report_snap_t *s, unsigned i, char *buf,
                   size_t cap)
{
    if (s == NULL || buf == NULL || cap == 0u) {
        return 0u;
    }
    buf[0] = '\0';
    switch (i) {
    case REPORT_L_STORE_OFF:
        if (!s->store_off) {
            return 0u;
        }
        return say(buf, cap,
                   "rcbench-iomcu: flash store OFF -- core 1 did not "
                   "register for the flash lock-out; set-ups run from RAM "
                   "and are lost at restart\n");
    case REPORT_L_SLOT:
        /* Which slot runs: a bench without the partition table takes its
         * next image over USB only. */
        if (s->slot < 0) {
            return say(buf, cap,
                       "rcbench-iomcu: no partition table -- firmware "
                       "updates over CAN need one USB load of "
                       "rcbench-iomcu-install.uf2\n");
        }
        return say(buf, cap, "rcbench-iomcu: running from slot %c%s\n",
                   s->slot == 0 ? 'A' : 'B',
                   s->on_trial ? ", on trial until the panel buys it" : "");
    case REPORT_L_RESET:
        return say(buf, cap, "rcbench-iomcu: reset %s\n", text(s->reset));
    case REPORT_L_BOOT:
        return say(buf, cap, "rcbench-iomcu: %s\n", text(s->boot));
    case REPORT_L_CAN_DOWN:
        if (s->can_up) {
            return 0u;
        }
        return say(buf, cap,
                   "rcbench-iomcu: CAN did not answer on SPI -- module "
                   "fitted? wiring on GP9-12?\n");
    case REPORT_L_CAN_UP:
        /* Requests first: the number that answers "is the other end talking
         * to me at all". */
        if (!s->can_up) {
            return 0u;
        }
        return say(buf, cap,
                   "rcbench-iomcu: CAN up, %u bit/s, %lu requests served, "
                   "%lu self-test echoes, tx_err %u rx_err %u eflg 0x%02X "
                   "tx_drop %lu txq_max %u\n",
                   (unsigned)s->bitrate, (unsigned long)s->requests,
                   (unsigned long)s->echoes, (unsigned)s->tec,
                   (unsigned)s->rec, (unsigned)s->eflg,
                   (unsigned long)s->tx_drop, (unsigned)s->txq_max);
    case REPORT_L_CAN_OVERRUN:
        /* In words: an overrun is the one thing that explains a frame going
         * missing while the bus reports no error. */
        if (!s->can_up || s->overruns == 0u) {
            return 0u;
        }
        return say(buf, cap,
                   "rcbench-iomcu: CAN receive buffers overran %lu time(s) "
                   "-- frames arrived with nowhere to put them; not a bus "
                   "fault\n",
                   (unsigned long)s->overruns);
    case REPORT_L_SENSE:
        return s->sense_on ? sense_line(s, buf, cap) : 0u;
    case REPORT_L_SENSE_WINDOW:
        return s->sense_on ? window_line(s, buf, cap) : 0u;
    case REPORT_L_SENSE_MAP:
        return s->sense_on ? map_line(s, buf, cap) : 0u;
    case REPORT_L_LOOP:
        return say(buf, cap,
                   "rcbench-iomcu: loop max %lu us, over %uus %lu, can max "
                   "%lu us, report max %lu us\n",
                   (unsigned long)s->loop.max_us,
                   (unsigned)PASS_STATS_OVER_US,
                   (unsigned long)s->loop.over,
                   (unsigned long)s->loop.sec_max_us[PASS_SEC_CAN],
                   (unsigned long)s->loop.sec_max_us[PASS_SEC_REPORT]);
    default:
        return 0u;
    }
}
