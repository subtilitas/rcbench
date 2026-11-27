/**
 * @file
 * @brief The Guition JC3636K518C's I2C parts: the CST816S touch controller
 *        and the DRV2605 haptic driver on one bus, SDA GPIO11, SCL GPIO12,
 *        400kHz.  They are the K518's dial_touch_* and dial_buzz_* calls.
 *
 * Touch: the CST816S answers at 0x15, at 0x2A on the boards C7 lists.  Its
 * reset (GPIO10) is held low 10ms, then 50ms pass before the probe.  Its
 * own sleep is switched off (register 0xFE = 1), since a sleeping CST816S
 * gives no I2C answer until a finger wakes it.  One read takes registers
 * 0x02 to 0x06: the finger count and the point, 0 to 359 on each axis.  A
 * read that fails counts as no finger.
 *
 * Vibration: the DRV2605 at 0x5A drives an LRA (linear resonant actuator)
 * from its ROM library 6, internal trigger.  The probe goes to 0x5A; with
 * no answer GPIO38 goes high (the enable of V1.0 boards) and the probe
 * runs again after 2ms; with still none GPIO38 goes back to an input and
 * the K518 does not vibrate.  The effects (dk518_input.h): a detent
 * "Sharp Tick 3" (26); PRESS "Sharp Click 100%" (4); HOLD, an update
 * accepted and any tone "Strong Click 100%" (1); the 5s warning "Buzz 1
 * 100%" (47), whose length is the ROM's; the forget "Double Click 100%"
 * (10) twice.  No auto-calibration runs: the part's defaults drive the
 * LRA.  Its strength, the LRA's resonance and the touch axes' orientation
 * are not run on the device yet.
 *
 * The bus is ESP-IDF's I2C master driver, which serialises the transfers
 * of the input task (touch, detent tick, cues), the face task and NimBLE's
 * host task (tones).
 *
 * SPDX-License-Identifier: MIT
 */

#include "dial.h"
#include "dk518_input.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/** @brief The bus clock, Hz. */
#define BUS_HZ 400000u
/** @brief One transfer's timeout, ms. */
#define BUS_TIMEOUT_MS 10
/** @brief The CST816S's addresses. @{ */
#define TOUCH_ADDR     0x15u
#define TOUCH_ADDR_ALT 0x2Au /**< the address C7 lists */
/** @} */
/** @brief CST816S registers. @{ */
#define TOUCH_REG_FINGERS  0x02u
#define TOUCH_REG_NO_SLEEP 0xFEu /**< DisAutoSleep: 1 keeps it awake */
/** @} */
/** @brief The CST816S's reset pulse and start, ms. @{ */
#define TOUCH_RESET_MS 10u
#define TOUCH_START_MS 50u /**< from the reset to the probe */
/** @} */
/** @brief The DRV2605's address. */
#define HAPTIC_ADDR 0x5Au
/** @brief DRV2605 registers. @{ */
#define DRV_MODE     0x01u /**< 0: internal trigger, out of standby */
#define DRV_RTP      0x02u /**< real-time playback input */
#define DRV_LIBRARY  0x03u /**< the ROM library */
#define DRV_SEQ0     0x04u /**< the waveform sequence, 8 slots */
#define DRV_GO       0x0Cu /**< 1: play the sequence */
#define DRV_FEEDBACK 0x1Au /**< bit 7: LRA */
/** @} */
/** @brief The library of LRA effects. */
#define DRV_LIB_LRA 6u
/** @brief FEEDBACK's reset value 0x36 with N_ERM_LRA set. */
#define DRV_FEEDBACK_LRA 0xB6u
/** @brief The wait after the enable goes high, ms. */
#define HAPTIC_EN_MS 2u

/** @brief The log's tag. */
static const char *TAG = "k518io";

/** @brief The bus, made by the first part that needs it. */
static i2c_master_bus_handle_t s_bus;
/** @brief The touch controller; NULL when it did not answer. */
static i2c_master_dev_handle_t s_touch;
/** @brief The haptic driver; NULL when it did not answer. */
static i2c_master_dev_handle_t s_haptic;

/** @brief Start the bus once.  @return true when it runs */
static bool bus(void)
{
    if (s_bus != NULL) {
        return true;
    }
    const i2c_master_bus_config_t cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = DIAL_PIN_I2C_SDA,
        .scl_io_num = DIAL_PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    if (i2c_new_master_bus(&cfg, &s_bus) != ESP_OK) {
        ESP_LOGE(TAG, "the I2C bus did not start");
        s_bus = NULL;
        return false;
    }
    return true;
}

/**
 * @brief Add a part at an address that answers.
 * @param addr its address
 * @param out  its handle
 * @return true when it answered and was added
 */
static bool add(uint8_t addr, i2c_master_dev_handle_t *out)
{
    const i2c_device_config_t dev = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = BUS_HZ,
    };
    return i2c_master_probe(s_bus, addr, BUS_TIMEOUT_MS) == ESP_OK
           && i2c_master_bus_add_device(s_bus, &dev, out) == ESP_OK;
}

/**
 * @brief Write one register.
 * @param d   the part
 * @param reg the register
 * @param v   its value
 * @return true when the part took it
 */
static bool put(i2c_master_dev_handle_t d, uint8_t reg, uint8_t v)
{
    const uint8_t b[2] = { reg, v };
    return i2c_master_transmit(d, b, sizeof b, BUS_TIMEOUT_MS) == ESP_OK;
}

/* --- touch ----------------------------------------------------------- */

bool dial_touch_init(void)
{
    const gpio_config_t rst = {
        .pin_bit_mask = 1ULL << DIAL_PIN_TOUCH_RST,
        .mode = GPIO_MODE_OUTPUT,
    };
    if (!bus()) {
        return false;
    }
    gpio_config(&rst);
    gpio_set_level(DIAL_PIN_TOUCH_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(TOUCH_RESET_MS));
    gpio_set_level(DIAL_PIN_TOUCH_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(TOUCH_START_MS));
    if (!add(TOUCH_ADDR, &s_touch) && !add(TOUCH_ADDR_ALT, &s_touch)) {
        ESP_LOGW(TAG, "no CST816S at 0x%02X or 0x%02X",
                 (unsigned)TOUCH_ADDR, (unsigned)TOUCH_ADDR_ALT);
        s_touch = NULL;
        return false;
    }
    if (!put(s_touch, TOUCH_REG_NO_SLEEP, 1u)) {
        ESP_LOGW(TAG, "the CST816S kept its sleep");
    }
    return true;
}

bool dial_k518_touch(uint16_t *x, uint16_t *y)
{
    const uint8_t reg = TOUCH_REG_FINGERS;
    uint8_t d[5];
    if (s_touch == NULL
        || i2c_master_transmit_receive(s_touch, &reg, 1, d, sizeof d,
                                       BUS_TIMEOUT_MS) != ESP_OK
        || (d[0] & 0x0Fu) == 0u) {
        return false;
    }
    const unsigned px = ((unsigned)(d[1] & 0x0Fu) << 8) | d[2];
    const unsigned py = ((unsigned)(d[3] & 0x0Fu) << 8) | d[4];
    *x = (uint16_t)(px >= DK518_TOUCH_SIZE ? DK518_TOUCH_SIZE - 1u : px);
    *y = (uint16_t)(py >= DK518_TOUCH_SIZE ? DK518_TOUCH_SIZE - 1u : py);
    return true;
}

bool dial_touch_read(uint8_t *x, uint8_t *y)
{
    uint16_t nx;
    uint16_t ny;
    if (!dial_k518_touch(&nx, &ny)) {
        return false;
    }
    *x = dk518_touch_scale(nx);
    *y = dk518_touch_scale(ny);
    return true;
}

/* --- vibration ------------------------------------------------------- */

/**
 * @brief Play up to two ROM effects in a row.
 * @param a the first effect
 * @param b the second, 0 for none
 */
static void play(uint8_t a, uint8_t b)
{
    if (s_haptic == NULL) {
        return;
    }
    (void)put(s_haptic, DRV_GO, 0u);
    (void)put(s_haptic, DRV_SEQ0, a);
    (void)put(s_haptic, DRV_SEQ0 + 1u, b);
    if (b != 0u) {
        (void)put(s_haptic, DRV_SEQ0 + 2u, 0u);
    }
    (void)put(s_haptic, DRV_GO, 1u);
}

void dial_buzz_init(void)
{
    if (!bus()) {
        return;
    }
    if (!add(HAPTIC_ADDR, &s_haptic)) {
        const gpio_config_t en = {
            .pin_bit_mask = 1ULL << DIAL_PIN_HAPTIC_EN,
            .mode = GPIO_MODE_OUTPUT,
        };
        gpio_config(&en);
        gpio_set_level(DIAL_PIN_HAPTIC_EN, 1);
        esp_rom_delay_us(HAPTIC_EN_MS * 1000u);
        if (!add(HAPTIC_ADDR, &s_haptic)) {
            gpio_reset_pin(DIAL_PIN_HAPTIC_EN);
            ESP_LOGW(TAG, "no DRV2605 at 0x%02X: no vibration",
                     (unsigned)HAPTIC_ADDR);
            s_haptic = NULL;
            return;
        }
    }
    if (!put(s_haptic, DRV_MODE, 0u) || !put(s_haptic, DRV_RTP, 0u)
        || !put(s_haptic, DRV_FEEDBACK, DRV_FEEDBACK_LRA)
        || !put(s_haptic, DRV_LIBRARY, DRV_LIB_LRA)) {
        ESP_LOGW(TAG, "the DRV2605 did not take its set-up");
    }
}

void dial_buzz_click(void)
{
    play(DK518_FX_TICK, 0u);
}

void dial_buzz_tone(uint32_t hz, uint32_t ms)
{
    (void)hz;
    (void)ms;
    play(DK518_FX_STRONG, 0u);
}

void dial_buzz_cue(dial_cue_t cue)
{
    switch (cue) {
    case DIAL_CUE_PRESS:
        play(DK518_FX_PRESS, 0u);
        break;
    case DIAL_CUE_WARN:
        play(DK518_FX_BUZZ, 0u);
        break;
    case DIAL_CUE_FORGET:
        play(DK518_FX_DOUBLE, DK518_FX_DOUBLE);
        break;
    case DIAL_CUE_HOLD:
    case DIAL_CUE_ACCEPT:
    default:
        play(DK518_FX_STRONG, 0u);
        break;
    }
}
