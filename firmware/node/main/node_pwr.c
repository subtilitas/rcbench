/**
 * @file node_pwr.c
 * @brief The power role: an INA228 board on the node's I2C
 *        (Inter-Integrated Circuit) bus at any of 0x40 to 0x4F, the 20ms
 *        windows and the 500ms accumulators.
 *
 * The driver and the register codec are shared/sense's ina228.c and
 * sense_bus.c, the IOmcu's own, on ESP-IDF's i2c_master here; the search
 * of the address and the shunt setting are node_pwr_sens.c's.  The set-up
 * is the bench's: INA228_ADC_BENCH (bus and shunt 540µs, temperature 50µs,
 * averaging 1, a result every 1130µs), ADCRANGE 1: 204.8A on the MATEK
 * I2C-INA-BM's 200µΩ, the default.
 *
 * The shunt is kept in NVS (non-volatile storage), namespace "pwr", key
 * "shunt_uohm".  A SHUNT write (protocols/rcnode) is stored there, goes
 * into INFO at once and reaches `pwr` at its next tick, which sets the
 * part up again with it.  The address found is kept in RTC (real-time
 * clock) memory through deep sleep for the timer wake's check.
 *
 * The `pwr` task runs on a 1130µs timer: CURRENT and VBUS, two 3-byte
 * reads, into the window (protocols/rcnode, rcnode_win_add()).  Every 40ms
 * ADC_CONFIG is read back (ina228_verify()); a part found reset is set up
 * again 1000ms on and counted.  Every 500ms DIAG_ALRT, then CHARGE and
 * ENERGY raw, then ADC_CONFIG again, then DIETEMP are read, and the
 * accumulators and the run extremes go out; CHARGE and ENERGY go out only
 * when the read-back after them matches.  The node never clears the
 * accumulators; the part's own reset and each set-up do, and epoch counts
 * the set-ups.
 *
 * Pins: the board's (node_board.h), 400kHz, the pads' own pull-ups off.
 * The C6-Zero: SDA GPIO19, SCL GPIO18, 2.2kΩ pull-ups to 3.3V on the node.
 * The ES3C28P: SDA IO16, SCL IO15, the board's 4.7kΩ pull-ups; the touch
 * controller shares the bus (node_disp.c), and each closed window also
 * goes to the screen (node_disp_window()).
 *
 * SPDX-License-Identifier: MIT
 */
#include "node.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "nvs.h"

#include "esp_attr.h"
#include "ina228.h"
#include "node_pwr_sens.h"
#include "sense_bus.h"

/** @brief The bus and the part. @{ */
#define PWR_SDA  NODE_PIN_SDA  /**< SDA */
#define PWR_SCL  NODE_PIN_SCL  /**< SCL */
#define PWR_HZ     400000u  /**< the bus clock */
#define PWR_XFER_MS      5    /**< one transaction's timeout */
_Static_assert(pdMS_TO_TICKS(PWR_XFER_MS) >= 1,
               "an I2C timeout under one tick ends every transaction");
/** @} */

/** @brief The schedule. @{ */
#define PWR_TICK_US    1130u   /**< INA228_ADC_BENCH's cycle */
#define PWR_VERIFY_US 40000u   /**< ADC_CONFIG read back */
/** @} */

static i2c_master_bus_handle_t s_bus;   /**< the I2C bus */
static i2c_master_dev_handle_t s_dev;   /**< the INA228, or the address asked */
static uint8_t     s_dev_addr;          /**< the address s_dev is set to */
static sense_bus_t s_sbus;              /**< shared/sense's view of the bus */
static psens_t     s_sns;               /**< the search, the shunt, the driver */
/** @brief The address found, kept through deep sleep; 0 none. */
RTC_DATA_ATTR static uint8_t s_rtc_addr;
static TaskHandle_t s_task;             /**< `pwr` */
static esp_timer_handle_t s_timer;      /**< the 1130µs tick */
static bool s_set_up_once;              /**< the part has been set up since the start */

static rcnode_win_t s_win;              /**< the window being filled */
static rcnode_run_t s_run;              /**< the extremes since RUN */
static rcnode_pwr_acc_t s_acc;          /**< the last accumulators */
static uint8_t  s_wflags;               /**< RCNODE_WF_ONLINE and RCNODE_WF_RUN */
static uint8_t  s_diag_of;              /**< RCNODE_AF_*_OF since the last packet */

static atomic_int  s_run_tag = -1;      /**< a RUN tag to take, 1 to 255; -1 none */
static atomic_bool s_online;            /**< the part is online */
static atomic_bool s_pack;              /**< the last window's mean is 5.0V or more */
static atomic_uint s_resets;            /**< set-ups after the first */
static atomic_uint s_windows;           /**< windows closed, for the console */
static atomic_int  s_last_i;            /**< the last window's mean, µA */
static atomic_int  s_last_v;            /**< the last window's mean, 2mV steps */
static atomic_uint s_last_n;            /**< the last window's samples */
static atomic_uint s_shunt_req;         /**< a SHUNT for `pwr`, µΩ; 0 none */
static atomic_uint s_shunt;             /**< the shunt for INFO, µΩ */
static atomic_uint s_addr;              /**< the address for INFO, 0 none */

/**
 * @brief An ESP-IDF error as a sensor bus error.
 *
 * @param e The error.
 * @return SENSE_OK, SENSE_TIMEOUT, or SENSE_NACK for every other failure.
 */
static sense_err_t sense_of(esp_err_t e)
{
    if (e == ESP_OK) {
        return SENSE_OK;
    }
    return e == ESP_ERR_TIMEOUT ? SENSE_TIMEOUT : SENSE_NACK;
}

/**
 * @brief The device handle pointed at @p addr: one handle serves the
 *        search's addresses and the part found.
 *
 * @param addr 0x40 to 0x4F.
 * @return SENSE_OK, or SENSE_BAD_ARG for another address or a failed change.
 */
static sense_err_t dev_at(uint8_t addr)
{
    if (addr < INA228_ADDR_MIN || addr > INA228_ADDR_MAX) {
        return SENSE_BAD_ARG;
    }
    if (addr != s_dev_addr) {
        if (i2c_master_device_change_address(s_dev, addr, PWR_XFER_MS)
            != ESP_OK) {
            return SENSE_BAD_ARG;
        }
        s_dev_addr = addr;
    }
    return SENSE_OK;
}

/**
 * @brief sense_i2c_t's read: the register address, then @p n bytes.
 *
 * @param ctx Unused.
 * @param addr The part's address, 0x40 to 0x4F.
 * @param reg The register.
 * @param buf Receives the bytes.
 * @param n Their count.
 * @return The bus error.
 */
static sense_err_t bus_read(void *ctx, uint8_t addr, uint8_t reg,
                            uint8_t *buf, size_t n)
{
    (void)ctx;
    const sense_err_t e = dev_at(addr);
    if (e != SENSE_OK) {
        return e;
    }
    return sense_of(i2c_master_transmit_receive(s_dev, &reg, 1, buf, n,
                                                PWR_XFER_MS));
}

/**
 * @brief sense_i2c_t's write: the register address, then @p n bytes.
 *
 * @param ctx Unused.
 * @param addr The part's address, 0x40 to 0x4F.
 * @param reg The register.
 * @param buf The bytes, 2 at most.
 * @param n Their count.
 * @return The bus error.
 */
static sense_err_t bus_write(void *ctx, uint8_t addr, uint8_t reg,
                             const uint8_t *buf, size_t n)
{
    (void)ctx;
    uint8_t b[3];
    if (n > 2u || dev_at(addr) != SENSE_OK) {
        return SENSE_BAD_ARG;
    }
    b[0] = reg;
    memcpy(b + 1, buf, n);
    return sense_of(i2c_master_transmit(s_dev, b, n + 1u, PWR_XFER_MS));
}

/**
 * @brief The bus and the device handle.
 *
 * @return true when both are up.
 */
static bool bus_up(void)
{
    if (s_dev != NULL) {
        return true;
    }
    i2c_master_bus_config_t bc;
    memset(&bc, 0, sizeof bc);
    bc.i2c_port          = -1;
    bc.sda_io_num        = PWR_SDA;
    bc.scl_io_num        = PWR_SCL;
    bc.clk_source        = I2C_CLK_SRC_DEFAULT;
    bc.glitch_ignore_cnt = 7;
    bc.flags.enable_internal_pullup = 0;
    if (i2c_new_master_bus(&bc, &s_bus) != ESP_OK) {
        return false;
    }
    i2c_device_config_t dc;
    memset(&dc, 0, sizeof dc);
    dc.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dc.device_address  = INA228_ADDR_MIN;
    dc.scl_speed_hz    = PWR_HZ;
    if (i2c_master_bus_add_device(s_bus, &dc, &s_dev) != ESP_OK) {
        s_dev = NULL;
        return false;
    }
    s_dev_addr = INA228_ADDR_MIN;
    static const sense_i2c_t io = { bus_read, bus_write, NULL };
    sense_bus_init(&s_sbus, &io);
    return true;
}

/**
 * @brief A 40-bit register, raw.
 *
 * @param reg INA228_CHARGE or INA228_ENERGY.
 * @param raw Receives the value.
 * @return true when the read succeeded.
 */
static bool read_raw40(uint8_t reg, uint64_t *raw)
{
    uint8_t b[5];
    if (sense_part_read(&s_sns.ina.part, reg, b, sizeof b) != SENSE_OK) {
        return false;
    }
    *raw = sense_be40(b);
    return true;
}

/**
 * @brief The 500ms reads and the two packets.
 *
 * @param t_us The node's clock.
 */
static void slow_step(uint64_t t_us)
{
    s_acc.flags = 0u;
    if (atomic_load(&s_online)) {
        /* DIAG_ALRT first: reading CHARGE and ENERGY clears their
         * overflow flags (Table 7-16). */
        uint16_t diag = 0u;
        if (ina228_read_diag(&s_sns.ina, &diag) == SENSE_OK) {
            if ((diag & INA228_DIAG_CHARGEOF) != 0u) {
                s_diag_of |= RCNODE_AF_CHARGE_OF;
            }
            if ((diag & INA228_DIAG_ENERGYOF) != 0u) {
                s_diag_of |= RCNODE_AF_ENERGY_OF;
            }
        }
        uint64_t charge = 0u;
        uint64_t energy = 0u;
        const bool charge_ok = read_raw40(INA228_CHARGE, &charge);
        const bool energy_ok = read_raw40(INA228_ENERGY, &energy);
        /* ADC_CONFIG read back after the two: a part that reset before or
         * between the reads answers with its reset value, and its zeros are
         * not sent under the old epoch.  A read back that fails confirms
         * nothing either.  Both values are dropped then. */
        bool lost = false;
        if (ina228_verify(&s_sns.ina, &lost) != SENSE_OK || lost) {
            if (lost) {
                atomic_store(&s_online, false);
            }
        } else {
            if (charge_ok) {
                s_acc.charge = charge;
                s_acc.flags |= RCNODE_AF_CHARGE_OK;
            }
            if (energy_ok) {
                s_acc.energy = energy;
                s_acc.flags |= RCNODE_AF_ENERGY_OK;
            }
        }
        int32_t mdegc = 0;
        if (ina228_read_dietemp(&s_sns.ina, &mdegc) == SENSE_OK) {
            s_acc.temp = (int16_t)(mdegc / 10);
            s_acc.flags |= RCNODE_AF_TEMP_OK;
        }
        s_acc.flags |= RCNODE_AF_ONLINE;
    }
    s_acc.flags |= s_diag_of;
    s_diag_of = 0u;

    uint8_t b[RCNODE_PKT_MAX];
    s_acc.hdr.seq    = node_ble_stream_seq();
    s_acc.hdr.t_node = (uint32_t)t_us;
    size_t n = rcnode_enc_pwr_acc(&s_acc, b, sizeof b);
    node_ble_stream(b, n);

    rcnode_pwr_run_t r;
    memset(&r, 0, sizeof r);
    r.hdr.seq    = node_ble_stream_seq();
    r.hdr.t_node = (uint32_t)t_us;
    r.tag     = s_run.tag;
    r.flags   = s_run.flags;
    r.i_max   = s_run.i_max;
    r.v_min   = s_run.v_min;
    r.windows = s_run.windows;
    n = rcnode_enc_pwr_run(&r, b, sizeof b);
    node_ble_stream(b, n);
}

/**
 * @brief One sample: the probe when it is due, then CURRENT and VBUS.
 *
 * @param now_ms The node's ms clock.
 * @param s Receives the sample.
 */
static void sample_step(uint32_t now_ms, rcnode_sample_t *s)
{
    memset(s, 0, sizeof *s);
    if (sense_bus_recovery_due(&s_sbus, now_ms)) {
        (void)i2c_master_bus_reset(s_bus);
        sense_bus_recovered(&s_sbus, now_ms);
    }
    const unsigned req = atomic_exchange(&s_shunt_req, 0u);
    if (req != 0u) {
        (void)psens_set_shunt(&s_sns, req);
    }
    const bool was = atomic_load(&s_online);
    const bool now = psens_step(&s_sns, now_ms);
    const bool rearm = now && psens_take_rearm(&s_sns);
    if ((now && !was) || rearm) {
        /* Each set-up writes RSTACC: the accumulators restart from 0.  A
         * shunt change's set-up is no reset of the part. */
        if (s_set_up_once && !rearm) {
            atomic_fetch_add(&s_resets, 1u);
        }
        s_set_up_once = true;
        ++s_acc.epoch;
        s_acc.charge = 0u;
        s_acc.energy = 0u;
    }
    atomic_store(&s_addr, psens_addr(&s_sns));
    if (psens_addr(&s_sns) != 0u) {
        s_rtc_addr = psens_addr(&s_sns);
    }
    atomic_store(&s_online, now);
    if (!now) {
        return;
    }
    sense_value_t i;
    int32_t v = 0;
    if (ina228_read_current(&s_sns.ina, &i) != SENSE_OK
        || ina228_read_vbus(&s_sns.ina, &v) != SENSE_OK) {
        return;
    }
    s->ok   = true;
    s->clip = (int8_t)i.clip;
    s->i_ua = i.value;
    s->v_uv = v;
}

/**
 * @brief `pwr`: the sampler.
 *
 * @param arg Unused.
 */
static void pwr_task(void *arg)
{
    (void)arg;
    rcnode_win_init(&s_win);
    rcnode_run_mark(&s_run, 0u);
    uint64_t next_verify = 0u;
    uint64_t next_slow   = 0u;
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        const uint64_t t = node_us();
        const uint32_t now_ms = (uint32_t)(t / 1000u);

        const int tag = atomic_exchange(&s_run_tag, -1);

        rcnode_sample_t s;
        sample_step(now_ms, &s);
        s_wflags = (uint8_t)((s_wflags & RCNODE_WF_RUN)
                             | (atomic_load(&s_online) ? RCNODE_WF_ONLINE : 0u));
        rcnode_pwr_win_t w;
        const bool closed = rcnode_win_add(&s_win, t, &s, s_wflags, &w);
        /* The closed window first, then the marker: the window is from
         * before the RUN write. */
        rcnode_run_step(&s_run, closed ? &w : NULL, tag);
        if (tag > 0) {
            s_wflags |= RCNODE_WF_RUN;
        }
        if (closed) {
            atomic_store(&s_pack, (w.flags & RCNODE_WF_PACK) != 0u);
            atomic_store(&s_last_i, w.i_mean);
            atomic_store(&s_last_v, w.v_mean);
            atomic_store(&s_last_n, w.n);
            atomic_fetch_add(&s_windows, 1u);
            uint8_t b[RCNODE_PKT_MAX];
            w.hdr.seq = node_ble_stream_seq();
            const size_t n = rcnode_enc_pwr_win(&w, b, sizeof b);
            node_ble_stream(b, n);
#if NODE_BOARD_DISPLAY
            node_disp_window(&w);
#endif
        }
        if (atomic_load(&s_online) && t >= next_verify) {
            next_verify = t + PWR_VERIFY_US;
            bool lost = false;
            (void)ina228_verify(&s_sns.ina, &lost);
            if (lost) {
                atomic_store(&s_online, false);
            }
        }
        if (t >= next_slow) {
            next_slow = t + (uint64_t)RCNODE_ACC_MS * 1000u;
            slow_step(t);
        }
    }
}

/**
 * @brief The 1130µs timer: wakes `pwr`.
 *
 * @param arg Unused.
 */
static void tick(void *arg)
{
    (void)arg;
    xTaskNotifyGive(s_task);
}

/**
 * @brief psens_store_t's read: the shunt from NVS.
 *
 * @param ctx Unused.
 * @param uohm Receives it.
 * @return false when none is stored or NVS failed.
 */
static bool nvs_load(void *ctx, uint16_t *uohm)
{
    (void)ctx;
    nvs_handle_t h;
    if (nvs_open("pwr", NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    const bool ok = nvs_get_u16(h, "shunt_uohm", uohm) == ESP_OK;
    nvs_close(h);
    return ok;
}

/**
 * @brief psens_store_t's write: the shunt to NVS, committed.
 *
 * @param ctx Unused.
 * @param uohm The shunt.
 * @return false when NVS failed.
 */
static bool nvs_save(void *ctx, uint16_t uohm)
{
    (void)ctx;
    nvs_handle_t h;
    if (nvs_open("pwr", NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    const bool ok = nvs_set_u16(h, "shunt_uohm", uohm) == ESP_OK
                    && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    return ok;
}

/** @brief The shunt's store. */
static const psens_store_t k_store = { nvs_load, nvs_save, NULL };

/** @brief Start the bus, the driver, `pwr` and its timer. */
static void pwr_start(void)
{
    if (!bus_up()) {
        ESP_LOGE(NODE_TAG, "pwr: I2C bus failed on SDA %d SCL %d", PWR_SDA,
                 PWR_SCL);
        return;
    }
    const uint16_t uohm = psens_shunt_load(&k_store);
    psens_init(&s_sns, &s_sbus, uohm);
    atomic_store(&s_shunt, uohm);
    ESP_LOGI(NODE_TAG, "pwr: shunt %uuOhm, INA228 searched at 0x40 to 0x4F",
             (unsigned)uohm);
    xTaskCreate(pwr_task, "pwr", 4096, NULL, configMAX_PRIORITIES - 2, &s_task);
    const esp_timer_create_args_t ta = {
        .callback = tick,
        .name     = "pwr_tick",
    };
    if (esp_timer_create(&ta, &s_timer) != ESP_OK
        || esp_timer_start_periodic(s_timer, PWR_TICK_US) != ESP_OK) {
        ESP_LOGE(NODE_TAG, "pwr: timer failed");
    }
}

#if NODE_BOARD_DISPLAY
i2c_master_bus_handle_t node_pwr_bus(void)
{
    return s_dev != NULL ? s_bus : NULL;
}
#endif

/**
 * @brief RUN: the tag goes to `pwr`, which restarts the extremes.  LANG:
 *        the screen's language on a board with a display; ignored on one
 *        without.  SHUNT: INFO's value at once, then NVS, then `pwr`
 *        sets the part up with it at its next tick.
 *
 * @param c The write.
 */
static void pwr_control(const rcnode_ctl_t *c)
{
    if (c->op == RCNODE_OP_RUN) {
        atomic_store(&s_run_tag, (int)c->tag);
    }
    if (c->op == RCNODE_OP_SHUNT && psens_shunt_ok(c->shunt_uohm)) {
        atomic_store(&s_shunt, c->shunt_uohm);
        if (!psens_shunt_save(&k_store, c->shunt_uohm)) {
            ESP_LOGW(NODE_TAG, "pwr: shunt %uuOhm not stored, kept until a restart",
                     (unsigned)c->shunt_uohm);
        }
        atomic_store(&s_shunt_req, c->shunt_uohm);
    }
#if NODE_BOARD_DISPLAY
    if (c->op == RCNODE_OP_LANG) {
        node_disp_lang(c->lang);
    }
#endif
}

/**
 * @brief The part's state in the status's terms.
 *
 * @param st The driver's state.
 * @return The rcnode_sensor_t.
 */
static uint8_t sensor_word(sense_state_t st)
{
    switch (st) {
    case SENSE_PART_UNPROBED: return RCNODE_SENSOR_UNPROBED;
    case SENSE_PART_ABSENT:   return RCNODE_SENSOR_ABSENT;
    case SENSE_PART_WRONG_ID: return RCNODE_SENSOR_WRONG_ID;
    case SENSE_PART_REFUSED:  return RCNODE_SENSOR_REFUSED;
    case SENSE_PART_ONLINE:   return RCNODE_SENSOR_ONLINE;
    case SENSE_PART_OFFLINE:  return RCNODE_SENSOR_OFFLINE;
    default:                  return RCNODE_SENSOR_NONE;
    }
}

/**
 * @brief The role's status fields.
 *
 * @param s The status.
 */
static void pwr_status(rcnode_status_t *s)
{
    s->sensor = sensor_word(psens_state(&s_sns));
    s->errors = s_sbus.errors;
    s->resets = (uint16_t)atomic_load(&s_resets);
    if (atomic_load(&s_online)) {
        s->flags |= RCNODE_ST_SENSOR;
    }
    if (atomic_load(&s_pack)) {
        s->flags |= RCNODE_ST_PACK;
    }
}

/**
 * @brief The role's part of INFO.
 *
 * @param i The info.
 */
static void pwr_info(rcnode_info_t *i)
{
    i->i228_addr  = (uint8_t)atomic_load(&s_addr);
    i->adcrange   = 1u;     /* every shunt runs at ADCRANGE 1 (node_pwr_sens.h) */
    i->shunt_uohm = (uint16_t)atomic_load(&s_shunt);
}

/**
 * @brief The role's part of the console line.
 *
 * @param buf The buffer.
 * @param n Its size.
 */
static void pwr_line(char *buf, size_t n)
{
    static unsigned s_prev;
    const unsigned w = atomic_load(&s_windows);
    const int i_ua = atomic_load(&s_last_i);
    const int v = atomic_load(&s_last_v);
    const int i_ma = i_ua / 1000;
    const unsigned a = atomic_load(&s_addr) & 0xFFu;
    char at[16] = "";
    if (a != 0u) {
        snprintf(at, sizeof at, " at 0x%02X", a);
    }
    snprintf(buf, n, "%u win/s, n %u, %d.%02dV %s%d.%03dA, part %s%s, "
             "%uuOhm, epoch %u",
             w - s_prev, atomic_load(&s_last_n), v / 500, (v % 500) / 5,
             i_ua < 0 ? "-" : "", (i_ma < 0 ? -i_ma : i_ma) / 1000,
             (i_ma < 0 ? -i_ma : i_ma) % 1000,
             atomic_load(&s_online) ? "online" : "not answering", at,
             atomic_load(&s_shunt), (unsigned)s_acc.epoch);
    s_prev = w;
}

/** @brief Whether a pack is on the XT60.  @return true when one is. */
static bool pwr_busy(void)
{
    return atomic_load(&s_pack);
}

/** @brief The sampler stopped, then ADC_CONFIG to shutdown: the part's
 *         lowest current. */
static void pwr_sleep(void)
{
    if (s_timer != NULL) {
        (void)esp_timer_stop(s_timer);
        vTaskDelay(pdMS_TO_TICKS(10));      /* a sample under way ends */
    }
    if (s_dev != NULL) {
        /* The part online now, else the kept address once it still holds
         * an INA228: nothing else in 0x40 to 0x4F is written. */
        const uint8_t online = atomic_load(&s_online) ? psens_addr(&s_sns) : 0u;
        (void)psens_shut_down(&s_sbus, online, s_rtc_addr);
    }
}

/**
 * @brief The address for the timer wake's check: the one kept through
 *        deep sleep when it still holds an INA228, else the first of 0x40
 *        to 0x4F that does.
 *
 * @return The address, 0 for none.
 */
static uint8_t wake_addr(void)
{
    if (s_rtc_addr != 0u && psens_is_ina228(&s_sbus, s_rtc_addr, NULL)) {
        return s_rtc_addr;
    }
    for (uint8_t a = INA228_ADDR_MIN; a <= INA228_ADDR_MAX; ++a) {
        if (psens_is_ina228(&s_sbus, a, NULL)) {
            return a;
        }
    }
    return 0u;
}

/**
 * @brief The timer wake's check: one triggered bus voltage conversion
 *        (mode 1h, 540µs), the read, the part back to shutdown.
 *
 * @param vbus_uv Receives the bus voltage, µV.
 * @return true when the INA228 answered.
 */
static bool pwr_wake(int32_t *vbus_uv)
{
    bool answered = false;
    *vbus_uv = 0;
    if (bus_up()) {
        const uint8_t a = wake_addr();
        s_rtc_addr = a;
        const uint16_t trig = ina228_adc_config(0x1u, 4, 4, 0, 0);
        uint8_t b[3];
        if (a != 0u
            && sense_bus_write16(&s_sbus, a, INA228_ADC_CONFIG, trig) == SENSE_OK) {
            esp_rom_delay_us(1500);
            if (sense_bus_read(&s_sbus, a, INA228_VBUS, b, sizeof b) == SENSE_OK) {
                answered = true;
                *vbus_uv = ina228_vbus_uv(ina228_code20(sense_be24(b)));
            }
        }
        pwr_sleep();
    }
    return answered;
}

const node_role_t node_role = {
    .role    = RCNODE_ROLE_POWER,
    .word    = "pwr",
    .timer_wake = true,
    .start   = pwr_start,
    .control = pwr_control,
    .status  = pwr_status,
    .info    = pwr_info,
    .line    = pwr_line,
    .busy    = pwr_busy,
    .sleep   = pwr_sleep,
    .wake    = pwr_wake,
};
