/**
 * @file node_bal.c
 * @brief The balance role: an ADXL335 or ADXL337 axis and the TCRT5000
 *        module's comparator output in one ADC (analogue-to-digital
 *        converter) stream, shared/vibe's demodulator on it, and an
 *        RCNODE_T_BAL_REPORT every RCNODE_BAL_MS on STREAM.
 *
 * The stream.  ADC1 in continuous (DMA, direct memory access) mode
 * converts a pattern of three channels in turn: GPIO0 (channel 0, the
 * accelerometer axis), GPIO1 (channel 1, the TCRT5000 module's D0) and
 * GPIO3 (channel 3, the battery divider), all at 12dB attenuation, 12 bits.
 * One trigger clock drives the three, so the index lands at a known sample
 * of the vibration channel: a third of a sample after it
 * (vibe_cfg_t::edge_shift_q8 = 85).  The battery is in the pattern because
 * a one-shot read of ADC1 fails while continuous mode holds the unit
 * (ESP-IDF v6.1, adc_oneshot_read() returns ESP_ERR_TIMEOUT).
 *
 * The rate.  ESP-IDF v6.1 sets the ESP32-C6's trigger interval to
 * 2,500,000 / sample_freq_hz, cut to a whole number (adc_hal.c: PLL_F80M
 * divided by 16, two cycles a conversion).  30,000 asked gives an
 * interval of 83: 30,120 conversions a second, 10,040Hz a channel.  The
 * driver takes 611 to 83,333 a second (SOC_ADC_SAMPLE_FREQ_THRES_LOW and
 * _HIGH), 27,777Hz a channel for three; the datasheet names 100kSPS (kilo
 * samples per second).  The console line counts the rate the node gets.
 *
 * The index.  D0 is a full-swing step, its threshold set by the module's
 * potentiometer, so the node's thresholds are fixed at 1100mV and 2200mV.
 * vibe_index_scan() takes the level the channel spends less time at as the
 * mark, so a dark line on a bright bell and a bright line on a dark bell
 * both work, and the edge is the start of the line.
 *
 * The scale.  The curve-fitting calibration of channel 0, read at counts
 * 1000 and 3000, and BAL_UV_PER_G give vibe_cfg_t's µg a count and the DC
 * window (vibe_cfg_scale()).  Without the calibration the nominal 0 to
 * 3300mV over 4096 counts stands in, and the console says so.
 *
 * Breaks.  A conversion out of the pattern's order (vibe_frame_split())
 * restarts the demodulator and the index there.  A full driver pool loses
 * whole frames, which the splitter cannot see: the overflow callback
 * counts a generation, read before and after each read, and a read that
 * is not whole (vibe_frame_whole()) is dropped, the stream stopped, its
 * pool emptied and the stream and the demodulator started again.  Either
 * way the open report is lost, STATUS counts the event in `errors` and the
 * restart in `resets`.
 *
 * Time.  The frame-done callback keeps the bytes the driver finished and
 * when; a report's t_node is its window's last vibration sample, dated
 * back from there (vibe_frame_time()), so a backlog in the pool does not
 * make it late.
 *
 * The TCRT5000 module's supply comes from GPIO20, high while the role runs
 * and low and held through deep sleep before a sleep: its infrared LED is
 * the node's largest load.
 *
 * The battery: from the stream while it runs; a one-shot read of ADC1
 * when it does not (the stream did not start, or stopped).
 *
 * Sleep: the node sleeps when no central is connected and the console does
 * not hold it awake for NODE_IDLE_MS; a turning rotor does not hold it.
 * Asleep, it sleeps until the RESET key.
 *
 * SPDX-License-Identifier: MIT
 */
#include "node.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_continuous.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/soc_caps.h"

#include "node_policy.h"
#include "vibe_demod.h"
#include "vibe_edge.h"
#include "vibe_frame.h"

/* The report's flags are the demodulator's, bit for bit (rcnode.h). */
_Static_assert(RCNODE_BF_VALID == VIBE_F_VALID, "RCNODE_BF_VALID");
_Static_assert(RCNODE_BF_INDEX_SEEN == VIBE_F_INDEX_SEEN, "RCNODE_BF_INDEX_SEEN");
_Static_assert(RCNODE_BF_INDEX_STEADY == VIBE_F_INDEX_STEADY, "RCNODE_BF_INDEX_STEADY");
_Static_assert(RCNODE_BF_INDEX_LOST == VIBE_F_INDEX_LOST, "RCNODE_BF_INDEX_LOST");
_Static_assert(RCNODE_BF_NO_INDEX == VIBE_F_NO_INDEX, "RCNODE_BF_NO_INDEX");
_Static_assert(RCNODE_BF_CLIPPED == VIBE_F_CLIPPED, "RCNODE_BF_CLIPPED");
_Static_assert(RCNODE_BF_DC_OK == VIBE_F_DC_OK, "RCNODE_BF_DC_OK");
_Static_assert(RCNODE_BF_BLADE_OK == VIBE_F_BLADE_OK, "RCNODE_BF_BLADE_OK");
_Static_assert(RCNODE_BF_QUALITY == VIBE_F_QUALITY, "RCNODE_BF_QUALITY");
_Static_assert(RCNODE_BF_EDGE_DROPPED == VIBE_F_EDGE_DROPPED, "RCNODE_BF_EDGE_DROPPED");
_Static_assert(RCNODE_BF_RPM_RANGE == VIBE_F_RPM_RANGE, "RCNODE_BF_RPM_RANGE");
_Static_assert(RCNODE_BF_RESTARTED == VIBE_F_RESTARTED, "RCNODE_BF_RESTARTED");
_Static_assert(RCNODE_BLADES_MAX == VIBE_BLADES_MAX, "RCNODE_BLADES_MAX");

/** @brief The demodulator's flags that travel: bits 0 to 11. */
#define BAL_VIBE_FLAGS 0x0FFFu
_Static_assert((BAL_VIBE_FLAGS & RCNODE_BF_COUNTS) == 0u, "RCNODE_BF_COUNTS is the node's own");

/** @brief Pins. @{ */
#define BAL_VIB_GPIO   0   /**< ADC1 channel 0: the accelerometer's X or Y */
#define BAL_IDX_GPIO   1   /**< ADC1 channel 1: the TCRT5000 module's D0 */
#define BAL_BAT_GPIO   3   /**< ADC1 channel 3: the battery divider */
#define BAL_IR_GPIO   20   /**< the TCRT5000 module's VCC */
/** @} */

/** @brief The stream. @{ */
#define BAL_NCH          3u      /**< entries in the pattern */
#define BAL_REQ_HZ   30000u      /**< conversions a second asked */
#define BAL_TRIG_HZ 2500000u     /**< ESP-IDF v6.1's trigger clock on the ESP32-C6 */
#define BAL_INTERVAL (BAL_TRIG_HZ / BAL_REQ_HZ)            /**< 83 */
#define BAL_FS_HZ   (BAL_TRIG_HZ / (BAL_INTERVAL * BAL_NCH)) /**< 10,040Hz a channel */
#define BAL_ROWS       100u      /**< rounds a DMA frame: 10ms */
#define BAL_FRAME_BYTES (BAL_ROWS * BAL_NCH * VIBE_FRAME_WORD) /**< 1,200 */
#define BAL_POOL_FRAMES  8u      /**< frames the driver holds: 80ms */
#define BAL_EDGES       16u      /**< index edges a frame, at most: 60,000rpm gives 10 */
#define BAL_READ_MS    100u      /**< a read's timeout */
#define BAL_NS_PER_CONV (BAL_INTERVAL * (1000000000u / BAL_TRIG_HZ)) /**< 33,200ns */
#define BAL_BAT_ONE_N   16u      /**< one-shot battery samples averaged */
/** @} */

_Static_assert(BAL_REQ_HZ >= SOC_ADC_SAMPLE_FREQ_THRES_LOW
               && BAL_REQ_HZ <= SOC_ADC_SAMPLE_FREQ_THRES_HIGH, "rate");
_Static_assert(BAL_NCH <= SOC_ADC_PATT_LEN_MAX && BAL_NCH <= VIBE_FRAME_CH_MAX, "pattern");
_Static_assert(SOC_ADC_DIGI_RESULT_BYTES == VIBE_FRAME_WORD, "word");
_Static_assert(sizeof(adc_digi_output_data_t) == VIBE_FRAME_WORD, "word");
_Static_assert(BAL_FRAME_BYTES % SOC_ADC_DIGI_DATA_BYTES_PER_CONV == 0u, "frame");

/** @brief The accelerometer and the thresholds, mV. @{ */
#define BAL_UV_PER_G  330000u  /**< ADXL335/337: 300mV/g at 3.0V, ratiometric, so 330mV/g at 3.3V */
#define BAL_DC_MIN_MV   1000u  /**< VIBE_F_DC_OK from */
#define BAL_DC_MAX_MV   2300u  /**< to */
#define BAL_IDX_LO_MV   1100u  /**< the index comparator's lower threshold */
#define BAL_IDX_HI_MV   2200u  /**< its upper threshold */
/** @} */

/** @brief The blade count before a BLADES write. */
#define BAL_BLADES_START 2u

static adc_continuous_handle_t s_adc;       /**< the stream */
static adc_cali_handle_t s_cali_vib;        /**< channel 0's calibration */
static adc_cali_handle_t s_cali_bat;        /**< channel 3's calibration */
static vibe_adc_cal_t s_cal;                /**< channel 0's two points */
static bool s_cal_nominal;                  /**< s_cal is the nominal curve */
static TaskHandle_t s_task;                 /**< `bal` */
static uint8_t s_chans[BAL_NCH];            /**< the pattern's channels */
static adc_channel_t s_bat_chan;            /**< the battery's channel */
static adc_oneshot_unit_handle_t s_one;     /**< ADC1 one-shot, while no stream runs */

/* The `bal` task's own state. */
static uint8_t s_buf[BAL_FRAME_BYTES];      /**< one read */
static int16_t s_vib[BAL_ROWS];             /**< the vibration channel's samples */
static int16_t s_idx[BAL_ROWS];             /**< the index channel's */
static int16_t s_bat[BAL_ROWS];             /**< the battery's */
static int16_t *const s_out[BAL_NCH] = { s_vib, s_idx, s_bat };  /**< the split's rows */
static vibe_frame_t  s_fr;                  /**< the splitter */
static vibe_index_t  s_ix;                  /**< the index comparator */
static vibe_demod_t  s_d;                   /**< the demodulator */
static vibe_cfg_t    s_cfg;                 /**< its settings */
static uint32_t s_samples;                  /**< samples pushed since the demodulator started */
static uint64_t s_rows_us;                  /**< when the last vibration sample fed was converted */
static uint32_t s_taken;                    /**< bytes read since the stream started, wraps */
static uint32_t s_gen_seen;                 /**< the overflow generation when the stream started */
static uint8_t  s_blades_now;               /**< the blade count in use */
static uint16_t s_rpm_now;                  /**< the rpm in use, 0 index mode */
static bool     s_online_once;              /**< the DC was in range once */

/* Between the tasks. */
static atomic_uint s_blades = BAL_BLADES_START; /**< the blade count written */
static atomic_uint s_rpm;                   /**< the rpm written, 0 index mode */
static atomic_bool s_stop;                  /**< the `bal` task is to stop the stream */
static atomic_bool s_stopped;               /**< it has */
static atomic_uint s_ovf_gen;               /**< full pools counted by the driver's callback */
static atomic_bool s_streaming;             /**< the stream runs: the battery comes from it */
static portMUX_TYPE s_done_mux = portMUX_INITIALIZER_UNLOCKED; /**< guards the two below */
static uint32_t s_done_bytes;               /**< bytes the driver finished since the stream started */
static uint64_t s_done_us;                  /**< when it finished the last frame */
static atomic_bool s_low_mark;              /**< the index's mark is the low level */
static atomic_uint s_errors;                /**< breaks and full pools */
static atomic_uint s_resets;                /**< demodulator restarts */
static atomic_uint s_rows;                  /**< rounds taken, for the console's rate */
static atomic_uint s_reports;               /**< reports sent */
static atomic_int  s_sensor = RCNODE_SENSOR_UNPROBED; /**< rcnode_sensor_t */
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED; /**< guards the two below */
static uint32_t s_bat_sum;                  /**< battery counts since the last read */
static uint32_t s_bat_n;                    /**< how many */
static portMUX_TYPE s_last_mux = portMUX_INITIALIZER_UNLOCKED; /**< guards s_last */
static vibe_report_t s_last;                /**< the last report, for the console */

/**
 * @brief The driver's pool was full: frames are lost.  ISR (interrupt
 *        service routine) context.
 *
 * @param h The handle.
 * @param ev The event.
 * @param ctx Unused.
 * @return false: no task to wake.
 */
static bool on_pool_ovf(adc_continuous_handle_t h, const adc_continuous_evt_data_t *ev,
                        void *ctx)
{
    (void)h;
    (void)ev;
    (void)ctx;
    atomic_fetch_add(&s_ovf_gen, 1u);
    return false;
}

/**
 * @brief The driver finished a frame: its bytes and the time.  ISR context.
 *
 * @param h The handle.
 * @param ev The frame.
 * @param ctx Unused.
 * @return false: no task to wake.
 */
static bool on_conv_done(adc_continuous_handle_t h, const adc_continuous_evt_data_t *ev,
                         void *ctx)
{
    (void)h;
    (void)ctx;
    const uint64_t now = (uint64_t)esp_timer_get_time();
    portENTER_CRITICAL_ISR(&s_done_mux);
    s_done_bytes += ev->size;
    s_done_us = now;
    portEXIT_CRITICAL_ISR(&s_done_mux);
    return false;
}

/**
 * @brief Converts µg to the packet's mg, rounded, held to 16 bits.
 *
 * @param ug The value, µg.
 * @return mg.
 */
static uint16_t to_mg(uint32_t ug)
{
    const uint32_t mg = (ug + 500u) / 1000u;
    return mg > UINT16_MAX ? (uint16_t)UINT16_MAX : (uint16_t)mg;
}

/**
 * @brief A window closed: the report goes out on STREAM.  Runs in `bal`,
 *        inside vibe_demod_feed().
 *
 * @param r The demodulator's report.
 * @param ctx Unused.
 */
static void on_report(const vibe_report_t *r, void *ctx)
{
    (void)ctx;
    rcnode_bal_report_t p;
    memset(&p, 0, sizeof p);
    /* The window ended before the last sample fed. */
    const uint32_t back = s_samples - r->end_sample;
    const uint64_t back_us = (uint64_t)back * 1000000u / BAL_FS_HZ;
    p.hdr.seq    = node_ble_stream_seq();
    p.hdr.t_node = (uint32_t)(s_rows_us - back_us);
    p.flags      = (uint16_t)(r->flags & BAL_VIBE_FLAGS);
    p.quality    = (uint8_t)(r->quality_pct > 100u ? 100u : r->quality_pct);
    p.revs       = r->revs;
    const uint32_t rpm = (r->rpm_x10 + 5u) / 10u;
    p.rpm        = rpm > UINT16_MAX ? (uint16_t)UINT16_MAX : (uint16_t)rpm;
    p.spread_pm  = r->spread_pm;
    p.mag_1x     = to_mg(r->mag_ug[VIBE_H_1X]);
    p.phase_1x   = (uint16_t)(((uint32_t)r->phase_cdeg[VIBE_H_1X] + 5u) / 10u % 3600u);
    p.mag_2x     = to_mg(r->mag_ug[VIBE_H_2X]);
    p.mag_blade  = to_mg(r->mag_ug[VIBE_H_BLADE]);
    p.rms        = to_mg(r->rms_ug);
    uint8_t b[RCNODE_PKT_MAX];
    const size_t n = rcnode_enc_bal_report(&p, b, sizeof b);
    node_ble_stream(b, n);
    atomic_fetch_add(&s_reports, 1u);

    if ((r->flags & VIBE_F_DC_OK) != 0u) {
        s_online_once = true;
        atomic_store(&s_sensor, RCNODE_SENSOR_ONLINE);
    } else {
        atomic_store(&s_sensor, s_online_once ? RCNODE_SENSOR_OFFLINE
                                              : RCNODE_SENSOR_ABSENT);
    }
    taskENTER_CRITICAL(&s_last_mux);
    s_last = *r;
    taskEXIT_CRITICAL(&s_last_mux);
}

/**
 * @brief Starts the demodulator and the index at sample 0, with the blade
 *        count and the rpm in use.
 */
static void demod_start(void)
{
    s_cfg.blades = s_blades_now;
    vibe_demod_init(&s_d, &s_cfg, on_report, NULL);
    if (s_rpm_now != 0u) {
        vibe_demod_set_rpm(&s_d, (uint32_t)s_rpm_now * 10u);
    }
    vibe_index_init(&s_ix, (int16_t)vibe_cal_counts(&s_cal, BAL_IDX_LO_MV, s_cfg.adc_max),
                    (int16_t)vibe_cal_counts(&s_cal, BAL_IDX_HI_MV, s_cfg.adc_max));
    s_samples = 0u;
}

/** @brief A gap in the sample clock: count it and start again. */
static void demod_restart(void)
{
    atomic_fetch_add(&s_errors, 1u);
    atomic_fetch_add(&s_resets, 1u);
    demod_start();
}

/** @brief Takes the BLADES and RPM written since the last frame. */
static void take_control(void)
{
    const uint8_t blades = (uint8_t)atomic_load(&s_blades);
    if (blades != s_blades_now) {
        s_blades_now = blades;
        vibe_demod_set_blades(&s_d, blades);
    }
    const uint16_t rpm = (uint16_t)atomic_load(&s_rpm);
    if (rpm != s_rpm_now) {
        s_rpm_now = rpm;
        vibe_demod_set_rpm(&s_d, (uint32_t)rpm * 10u);
    }
}

/**
 * @brief One frame's rows: the battery summed, the index found, the
 *        vibration demodulated.
 *
 * @param rows Rows in s_vib, s_idx and s_bat.
 */
static void take_rows(size_t rows)
{
    uint32_t sum = 0u;
    for (size_t k = 0; k < rows; ++k) {
        sum += (uint32_t)s_bat[k];
    }
    taskENTER_CRITICAL(&s_mux);
    s_bat_sum += sum;
    s_bat_n += (uint32_t)rows;
    taskEXIT_CRITICAL(&s_mux);

    uint32_t e[BAL_EDGES];
    const size_t ne = vibe_index_scan(&s_ix, s_idx, rows, e, BAL_EDGES);
    atomic_store(&s_low_mark, s_ix.low_mark);
    s_samples += (uint32_t)rows;
    vibe_demod_feed(&s_d, s_vib, rows, e, ne);
    atomic_fetch_add(&s_rows, (unsigned)rows);
}

/**
 * @brief Frames were lost in the driver's pool: the stream stopped, the
 *        pool emptied, the stream, the splitter and the demodulator
 *        started again on a clock without a gap.
 *
 * @return false when the stream does not start again.
 */
static bool stream_restart(void)
{
    (void)adc_continuous_stop(s_adc);
    (void)adc_continuous_flush_pool(s_adc);
    taskENTER_CRITICAL(&s_done_mux);
    s_done_bytes = 0u;
    taskEXIT_CRITICAL(&s_done_mux);
    s_taken = 0u;
    s_gen_seen = atomic_load(&s_ovf_gen);
    vibe_frame_init(&s_fr, s_chans, BAL_NCH);
    demod_restart();
    return adc_continuous_start(s_adc) == ESP_OK;
}

/**
 * @brief When the last vibration sample of the rows split so far was
 *        converted.
 *
 * @param done_us When the driver finished its newest frame.
 * @param done_bytes The bytes it had finished then.
 * @param pos Bytes since the stream started to the end of the split.
 * @return µs.
 */
static uint64_t rows_time(uint64_t done_us, uint32_t done_bytes, uint32_t pos)
{
    /* The open round's words come after the last row; the vibration
     * sample is the row's first conversion. */
    const uint32_t open = s_fr.synced ? s_fr.at : 0u;
    const uint32_t vib_end = pos - VIBE_FRAME_WORD * (open + BAL_NCH - 1u);
    return vibe_frame_time(done_us, done_bytes, vib_end, BAL_NS_PER_CONV);
}

/**
 * @brief `bal`: reads the stream, a frame at a time, until s_stop.
 *
 * @param arg Unused.
 */
static void bal_task(void *arg)
{
    (void)arg;
    s_blades_now = (uint8_t)atomic_load(&s_blades);
    demod_start();
    while (!atomic_load(&s_stop)) {
        uint32_t len = 0u;
        const uint32_t g0 = atomic_load(&s_ovf_gen);
        const esp_err_t e = adc_continuous_read(s_adc, s_buf, sizeof s_buf, &len,
                                                BAL_READ_MS);
        const uint32_t g1 = atomic_load(&s_ovf_gen);
        if (!vibe_frame_whole(s_gen_seen, g0, g1)) {
            /* A frame was lost before this read's data or during it. */
            if (!stream_restart()) {
                ESP_LOGE(NODE_TAG, "bal: the ADC stream did not start again");
                atomic_store(&s_sensor, RCNODE_SENSOR_REFUSED);
                break;
            }
            continue;
        }
        if (e == ESP_ERR_TIMEOUT) {
            continue;           /* nothing in 100ms */
        }
        if (e != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(10));    /* the `node` task runs below */
            continue;
        }
        taskENTER_CRITICAL(&s_done_mux);
        const uint32_t done_bytes = s_done_bytes;
        const uint64_t done_us = s_done_us;
        taskEXIT_CRITICAL(&s_done_mux);
        take_control();
        size_t at = 0u;
        while (at < len) {
            const vibe_split_t r = vibe_frame_split(&s_fr, s_buf + at, len - at,
                                                    s_out, BAL_ROWS);
            at += r.used;
            if (r.rows > 0u) {
                s_rows_us = rows_time(done_us, done_bytes, s_taken + (uint32_t)at);
                take_rows(r.rows);
            }
            if (r.broke) {
                demod_restart();
            } else if (r.used == 0u) {
                break;          /* bytes past the last whole word */
            }
        }
        s_taken += len;
    }
    (void)adc_continuous_stop(s_adc);
    atomic_store(&s_streaming, false);
    atomic_store(&s_stopped, true);
    vTaskSuspend(NULL);
}

/**
 * @brief A curve-fitting calibration for one channel at 12dB.
 *
 * @param chan The channel.
 * @return The handle, NULL without the chip's calibration.
 */
static adc_cali_handle_t cali_for(adc_channel_t chan)
{
    const adc_cali_curve_fitting_config_t kc = {
        .unit_id  = ADC_UNIT_1,
        .chan     = chan,
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    adc_cali_handle_t h = NULL;
    if (adc_cali_create_scheme_curve_fitting(&kc, &h) != ESP_OK) {
        return NULL;
    }
    return h;
}

/**
 * @brief The two points of channel 0's curve, or the nominal curve.
 */
static void read_scale(void)
{
    int lo = 0;
    int hi = 0;
    if (s_cali_vib != NULL
        && adc_cali_raw_to_voltage(s_cali_vib, (int)VIBE_CAL_RAW_LO, &lo) == ESP_OK
        && adc_cali_raw_to_voltage(s_cali_vib, (int)VIBE_CAL_RAW_HI, &hi) == ESP_OK
        && lo > 0 && hi > lo && hi <= (int)UINT16_MAX) {
        s_cal.raw_lo = (uint16_t)VIBE_CAL_RAW_LO;
        s_cal.raw_hi = (uint16_t)VIBE_CAL_RAW_HI;
        s_cal.mv_lo  = (uint16_t)lo;
        s_cal.mv_hi  = (uint16_t)hi;
        s_cal_nominal = false;
    } else {
        vibe_adc_cal_nominal(&s_cal);
        s_cal_nominal = true;
    }
    vibe_cfg_default(&s_cfg);
    s_cfg.fs_hz = BAL_FS_HZ;
    s_cfg.window = BAL_FS_HZ * RCNODE_BAL_MS / 1000u;
    s_cfg.edge_shift_q8 = 256 / (int32_t)BAL_NCH;    /* the index is entry 1 */
    (void)vibe_cfg_scale(&s_cfg, &s_cal, BAL_UV_PER_G, BAL_DC_MIN_MV, BAL_DC_MAX_MV);
}

/**
 * @brief Whether vibe_frame_word() reads the driver's word as the driver
 *        writes it.
 *
 * @return true when the raw count and the channel match.
 */
static bool word_layout_ok(void)
{
    adc_digi_output_data_t w;
    memset(&w, 0, sizeof w);
    w.type2.data = 0xABCu;
    w.type2.channel = 5u;
    uint8_t b[VIBE_FRAME_WORD];
    memcpy(b, &w, sizeof b);
    uint8_t ch = 0u;
    return vibe_frame_word(b, &ch) == 0xABCu && ch == 5u;
}

/**
 * @brief Sets up the pattern and starts the stream.
 *
 * @return true when the stream runs.
 */
static bool stream_open(void)
{
    static const int gpios[BAL_NCH] = { BAL_VIB_GPIO, BAL_IDX_GPIO, BAL_BAT_GPIO };
    adc_digi_pattern_config_t pat[BAL_NCH];
    for (unsigned k = 0; k < BAL_NCH; ++k) {
        adc_unit_t unit;
        adc_channel_t chan;
        if (adc_continuous_io_to_channel(gpios[k], &unit, &chan) != ESP_OK
            || unit != ADC_UNIT_1) {
            ESP_LOGE(NODE_TAG, "bal: GPIO%d has no ADC1 channel", gpios[k]);
            return false;
        }
        pat[k].atten = ADC_ATTEN_DB_12;
        pat[k].channel = (uint8_t)chan;
        pat[k].unit = ADC_UNIT_1;
        pat[k].bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;
        s_chans[k] = (uint8_t)chan;
    }
    vibe_frame_init(&s_fr, s_chans, BAL_NCH);
    /* A full pool drops the frames it holds and keeps the newest; the
     * generation check restarts the stream either way. */
    const adc_continuous_handle_cfg_t hc = {
        .max_store_buf_size = BAL_FRAME_BYTES * BAL_POOL_FRAMES,
        .conv_frame_size    = BAL_FRAME_BYTES,
        .flags = { .flush_pool = 1 },
    };
    if (adc_continuous_new_handle(&hc, &s_adc) != ESP_OK) {
        s_adc = NULL;
        return false;
    }
    const adc_continuous_config_t cc = {
        .pattern_num    = BAL_NCH,
        .adc_pattern    = pat,
        .sample_freq_hz = BAL_REQ_HZ,
        .conv_mode      = ADC_CONV_SINGLE_UNIT_1,
    };
    const adc_continuous_evt_cbs_t cbs = {
        .on_conv_done = on_conv_done,
        .on_pool_ovf  = on_pool_ovf,
    };
    s_gen_seen = atomic_load(&s_ovf_gen);
    return adc_continuous_config(s_adc, &cc) == ESP_OK
           && adc_continuous_register_event_callbacks(s_adc, &cbs, NULL) == ESP_OK
           && adc_continuous_start(s_adc) == ESP_OK;
}

/**
 * @brief Starts the stream; on a failure frees the driver, so the battery's
 *        one-shot read can take ADC1.
 *
 * @return true when the stream runs.
 */
static bool stream_start(void)
{
    if (stream_open()) {
        atomic_store(&s_streaming, true);
        return true;
    }
    if (s_adc != NULL) {
        (void)adc_continuous_stop(s_adc);
        (void)adc_continuous_deinit(s_adc);
        s_adc = NULL;
    }
    return false;
}

/** @brief The TCRT5000 module's supply, the scale and the stream. */
static void bal_start(void)
{
    const gpio_config_t io = {
        .pin_bit_mask = 1ull << BAL_IR_GPIO,
        .mode = GPIO_MODE_OUTPUT,
    };
    /* Low first, so the pad leaves the hold of the last sleep at the
     * level it was held at. */
    (void)gpio_set_level(BAL_IR_GPIO, 0);
    (void)gpio_config(&io);
    (void)gpio_set_drive_capability(BAL_IR_GPIO, GPIO_DRIVE_CAP_3);
    (void)gpio_hold_dis(BAL_IR_GPIO);
    (void)gpio_set_level(BAL_IR_GPIO, 1);

    s_cali_vib = cali_for(ADC_CHANNEL_0);
    read_scale();
    if (!word_layout_ok()) {
        ESP_LOGE(NODE_TAG, "bal: the ADC's word layout differs from vibe_frame.h");
        atomic_store(&s_sensor, RCNODE_SENSOR_REFUSED);
        return;
    }
    if (!stream_start()) {
        ESP_LOGE(NODE_TAG, "bal: the ADC stream did not start");
        atomic_store(&s_sensor, RCNODE_SENSOR_REFUSED);
        return;
    }
    ESP_LOGI(NODE_TAG, "bal: ADC %uHz a channel, 1g = %u counts, DC %u to %u, %s",
             (unsigned)BAL_FS_HZ,
             (unsigned)(1000000ull * 65536u / (s_cfg.ug_per_count_q16 ? s_cfg.ug_per_count_q16 : 1u)),
             (unsigned)s_cfg.dc_min, (unsigned)s_cfg.dc_max,
             s_cal_nominal ? "nominal curve, no calibration" : "calibrated");
    if (xTaskCreate(bal_task, "bal", 4096, NULL, 5, &s_task) != pdPASS) {
        s_task = NULL;
        ESP_LOGE(NODE_TAG, "bal: task not started");
        atomic_store(&s_sensor, RCNODE_SENSOR_REFUSED);
        atomic_store(&s_streaming, false);
        (void)adc_continuous_stop(s_adc);
        (void)adc_continuous_deinit(s_adc);
        s_adc = NULL;
    }
}

/**
 * @brief BLADES and RPM for the demodulator; RUN changes nothing here.
 *
 * @param c The write.
 */
static void bal_control(const rcnode_ctl_t *c)
{
    if (c->op == RCNODE_OP_BLADES) {
        atomic_store(&s_blades, c->blades);
    } else if (c->op == RCNODE_OP_RPM) {
        atomic_store(&s_rpm, c->rpm);
    }
}

/**
 * @brief The sensor's state from the last report's DC, and the counters.
 *
 * @param s The status.
 */
static void bal_status(rcnode_status_t *s)
{
    s->sensor  = (uint8_t)atomic_load(&s_sensor);
    s->errors  = (uint16_t)atomic_load(&s_errors);
    s->resets  = (uint16_t)atomic_load(&s_resets);
    if (s->sensor == RCNODE_SENSOR_ONLINE) {
        s->flags |= RCNODE_ST_SENSOR;
    }
}

/**
 * @brief No role part in INFO: the 4 bytes stay 0.
 *
 * @param i The info.
 */
static void bal_info(rcnode_info_t *i)
{
    (void)i;
}

/**
 * @brief The role's part of the console line: reports and rounds in the
 *        last second, then the last report.
 *
 * @param buf The buffer.
 * @param n Its size.
 */
static void bal_line(char *buf, size_t n)
{
    static unsigned s_prev_rows;
    static unsigned s_prev_reps;
    const unsigned rows = atomic_load(&s_rows);
    const unsigned reps = atomic_load(&s_reports);
    vibe_report_t r;
    taskENTER_CRITICAL(&s_last_mux);
    r = s_last;
    taskEXIT_CRITICAL(&s_last_mux);
    const unsigned ph = r.phase_cdeg[VIBE_H_1X];
    snprintf(buf, n, "%u rep/s, adc %u/s, %urpm %u.%u%%, 1x %umg %u.%02udeg q%u%%, "
             "2x %umg, blade %umg (%u), rms %umg, revs %u, dc %umV, flags 0x%04x, "
             "mark %s%s",
             reps - s_prev_reps, rows - s_prev_rows, (unsigned)((r.rpm_x10 + 5u) / 10u),
             (unsigned)(r.spread_pm / 10u), (unsigned)(r.spread_pm % 10u),
             (unsigned)to_mg(r.mag_ug[VIBE_H_1X]), ph / 100u, ph % 100u,
             (unsigned)r.quality_pct, (unsigned)to_mg(r.mag_ug[VIBE_H_2X]),
             (unsigned)to_mg(r.mag_ug[VIBE_H_BLADE]), atomic_load(&s_blades),
             (unsigned)to_mg(r.rms_ug), (unsigned)r.revs,
             (unsigned)vibe_cal_mv(&s_cal, r.dc_x16), (unsigned)r.flags,
             atomic_load(&s_low_mark) ? "low" : "high", s_cal_nominal ? ", nominal scale" : "");
    s_prev_rows = rows;
    s_prev_reps = reps;
}

/** @brief Nothing holds a balance node awake but the link and the console.
 *  @return false. */
static bool bal_busy(void)
{
    return false;
}

/** @brief The stream stopped, then the TCRT5000 module's supply off and
 *  held off through deep sleep. */
static void bal_sleep(void)
{
    if (s_task != NULL) {
        atomic_store(&s_stop, true);
        for (unsigned k = 0; k < 30u && !atomic_load(&s_stopped); ++k) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    (void)gpio_set_level(BAL_IR_GPIO, 0);
    (void)gpio_hold_en(BAL_IR_GPIO);
}

/**
 * @brief Not called: a balance node has no timer wake.
 *
 * @param vbus_uv Receives 0.
 * @return false.
 */
static bool bal_wake(int32_t *vbus_uv)
{
    *vbus_uv = 0;
    return false;
}

/* ------------------------------------------------------------ battery */

void node_bat_init(void)
{
    adc_unit_t unit;
    if (adc_continuous_io_to_channel(BAL_BAT_GPIO, &unit, &s_bat_chan) != ESP_OK) {
        ESP_LOGE(NODE_TAG, "bat: GPIO%d has no ADC channel", BAL_BAT_GPIO);
        return;
    }
    s_cali_bat = cali_for(s_bat_chan);
    if (s_cali_bat == NULL) {
        ESP_LOGW(NODE_TAG, "bat: no ADC calibration");
    }
}

/**
 * @brief The battery without the stream: a one-shot read of ADC1, as the
 *        power node reads it, BAL_BAT_ONE_N samples averaged.
 *
 * @return The pin, mV; -1 when the read fails.
 */
static int bat_oneshot_mv(void)
{
    if (s_one == NULL) {
        const adc_oneshot_unit_init_cfg_t uc = { .unit_id = ADC_UNIT_1 };
        if (adc_oneshot_new_unit(&uc, &s_one) != ESP_OK) {
            s_one = NULL;
            return -1;
        }
        const adc_oneshot_chan_cfg_t cc = {
            .atten    = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_12,
        };
        if (adc_oneshot_config_channel(s_one, s_bat_chan, &cc) != ESP_OK) {
            return -1;
        }
    }
    int sum = 0;
    for (unsigned k = 0; k < BAL_BAT_ONE_N; ++k) {
        int mv = 0;
        if (adc_oneshot_get_calibrated_result(s_one, s_cali_bat, s_bat_chan, &mv) != ESP_OK) {
            return -1;
        }
        sum += mv < 0 ? 0 : mv;
    }
    return sum / (int)BAL_BAT_ONE_N;
}

uint16_t node_bat_read(void)
{
    if (!atomic_load(&s_streaming)) {
        if (s_cali_bat == NULL) {
            return NODE_BAT_ERR;
        }
        const int mv = bat_oneshot_mv();
        return mv < 0 ? NODE_BAT_ERR : node_bat_mv((uint32_t)mv);
    }
    taskENTER_CRITICAL(&s_mux);
    const uint32_t sum = s_bat_sum;
    const uint32_t cnt = s_bat_n;
    s_bat_sum = 0u;
    s_bat_n = 0u;
    taskEXIT_CRITICAL(&s_mux);
    int mv = 0;
    if (s_cali_bat == NULL || cnt == 0u
        || adc_cali_raw_to_voltage(s_cali_bat, (int)((sum + cnt / 2u) / cnt), &mv) != ESP_OK) {
        return NODE_BAT_ERR;
    }
    return node_bat_mv((uint32_t)(mv < 0 ? 0 : mv));
}

const node_role_t node_role = {
    .role    = RCNODE_ROLE_BALANCE,
    .word    = "bal",
    .timer_wake = false,
    .start   = bal_start,
    .control = bal_control,
    .status  = bal_status,
    .info    = bal_info,
    .line    = bal_line,
    .busy    = bal_busy,
    .sleep   = bal_sleep,
    .wake    = bal_wake,
};
