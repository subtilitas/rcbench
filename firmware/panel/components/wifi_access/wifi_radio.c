/**
 * @file wifi_radio.c
 * @brief The radio under esp_wifi: the board's ESP32-C6, reached over SDIO
 *        (Secure Digital input/output) through esp_wifi_remote and
 *        esp_hosted.
 *
 * See wifi_radio.h.
 *
 * The link: SDIO slot 1, 4 data lines at 40 MHz, the pins of board_pins.h
 * (BOARD_C6_*), which esp_hosted reads from sdkconfig.defaults; the checks
 * below fail the build when the two differ.  esp_hosted sets up its 6 tasks
 * and the SDMMC (SD/MMC host) controller before app_main(); it resets and
 * talks to the C6 from the radio's first start on (wifi_radio_link()).
 * Between sessions the radio is off (esp_wifi_deinit() on the C6); the C6
 * stays powered and is not reset again until the panel restarts.
 *
 * SPDX-License-Identifier: MIT
 */

#include "wifi_radio.h"

#include <stdatomic.h>

#include "board_pins.h"
#include "esp_event.h"
#include "esp_hosted.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

_Static_assert(CONFIG_ESP_HOSTED_SDIO_SLOT == BOARD_C6_SLOT, "C6 slot");
_Static_assert(CONFIG_ESP_HOSTED_SDIO_BUS_WIDTH == BOARD_C6_WIDTH, "C6 width");
_Static_assert(CONFIG_ESP_HOSTED_SDIO_CLOCK_FREQ_KHZ == BOARD_C6_FREQ_KHZ,
               "C6 clock");
_Static_assert(CONFIG_ESP_HOSTED_SDIO_PIN_CLK == BOARD_C6_PIN_CLK, "C6 CLK");
_Static_assert(CONFIG_ESP_HOSTED_SDIO_PIN_CMD == BOARD_C6_PIN_CMD, "C6 CMD");
_Static_assert(CONFIG_ESP_HOSTED_SDIO_PIN_D0 == BOARD_C6_PIN_D0, "C6 D0");
_Static_assert(CONFIG_ESP_HOSTED_SDIO_PIN_D1 == BOARD_C6_PIN_D1, "C6 D1");
_Static_assert(CONFIG_ESP_HOSTED_SDIO_PIN_D2 == BOARD_C6_PIN_D2, "C6 D2");
_Static_assert(CONFIG_ESP_HOSTED_SDIO_PIN_D3 == BOARD_C6_PIN_D3, "C6 D3");
_Static_assert(CONFIG_ESP_HOSTED_SDIO_GPIO_RESET_SLAVE == BOARD_C6_PIN_EN,
               "C6 EN");
_Static_assert(BOARD_C6_SLOT != BOARD_SD_SLOT, "the SD card's slot");

static const char *TAG = "wifi";

static atomic_bool s_failed;   /**< the link has failed since start-up */
static bool        s_once;     /**< the worker's: handler and version done */

/**
 * @brief The linker's name for ESP-IDF's xTaskCreatePinnedToCore().
 */
BaseType_t __real_xTaskCreatePinnedToCore(TaskFunction_t fn, const char *name,
                                          const uint32_t stack, void *arg,
                                          UBaseType_t prio,
                                          TaskHandle_t *handle,
                                          const BaseType_t core);

/**
 * @brief Create a task on core 0 when no core is given.
 *
 * Core 1 runs the control task, which owns STOP and the heartbeat, and the
 * run log task below it; nothing of the radio's runs there.  esp_hosted
 * creates its 6 tasks (SDIO receive buffer, read, process, write; RPC
 * (remote procedure call) receive and send) at priority 23 with no core
 * given, which on its own lets them take core 1 from the control task at
 * priority 10.  The link (CMakeLists.txt, --wrap) sends every
 * xTaskCreatePinnedToCore() of the image through here: a task created with
 * no core (tskNO_AFFINITY) runs on core 0, and a task created for a given
 * core stays on it.
 *
 * @param fn     The task's function.
 * @param name   Its name.
 * @param stack  Its stack in bytes.
 * @param arg    Its argument.
 * @param prio   Its priority.
 * @param handle Receives its handle; may be NULL.
 * @param core   0, 1 or tskNO_AFFINITY.
 * @return pdPASS, or what ESP-IDF returns for a task it could not create.
 */
BaseType_t __wrap_xTaskCreatePinnedToCore(TaskFunction_t fn, const char *name,
                                          const uint32_t stack, void *arg,
                                          UBaseType_t prio,
                                          TaskHandle_t *handle,
                                          const BaseType_t core)
{
    return __real_xTaskCreatePinnedToCore(fn, name, stack, arg, prio, handle,
                                          core == tskNO_AFFINITY ? 0 : core);
}

void wifi_radio_mac(uint8_t mac[6])
{
    (void)esp_read_mac(mac, ESP_MAC_BASE);
}

/**
 * @brief esp_hosted's events: a link that failed for good.
 *
 * esp_hosted posts ESP_HOSTED_EVENT_TRANSPORT_FAILURE when the SDIO
 * transfers to the C6 have failed for good.  With
 * CONFIG_ESP_HOSTED_TRANSPORT_RESTART_ON_FAILURE off it does not restart
 * the panel; the worker sees the flag, takes the radio down and fails the
 * session, and later starts fail at wifi_radio_link().
 *
 * @param arg  Unused.
 * @param base ESP_HOSTED_EVENT.
 * @param id   The event.
 * @param data Unused.
 */
static void on_hosted(void *arg, esp_event_base_t base, int32_t id,
                      void *data)
{
    (void)arg;
    (void)base;
    (void)data;
    if (id == ESP_HOSTED_EVENT_TRANSPORT_FAILURE) {
        atomic_store(&s_failed, true);
        ESP_LOGE(TAG, "C6 link failed: Wi-Fi is off until the panel restarts");
    }
}

/**
 * @brief Once, at the first start: the failure handler and the C6's version.
 *
 * The first start: the C6 is reset, enumerated on SDIO and sends its start
 * event (esp_hosted logs a version mismatch there).  A C6 firmware that
 * predates the version request answers it with an error or not at all; the
 * request then ends after esp_hosted's response time-out, 5 s, once.
 */
static void link_once(void)
{
    if (esp_event_handler_register(ESP_HOSTED_EVENT,
                                   ESP_HOSTED_EVENT_TRANSPORT_FAILURE,
                                   on_hosted, NULL) != ESP_OK) {
        ESP_LOGW(TAG, "C6: no handler for a link failure");
    }
    esp_hosted_coprocessor_fwver_t v = { 0 };
    if (esp_hosted_get_coprocessor_fwversion(&v) == ESP_OK) {
        ESP_LOGI(TAG, "C6: esp_hosted firmware %u.%u.%u, host %u.%u.%u",
                 (unsigned)v.major1, (unsigned)v.minor1, (unsigned)v.patch1,
                 (unsigned)ESP_HOSTED_VERSION_MAJOR_1,
                 (unsigned)ESP_HOSTED_VERSION_MINOR_1,
                 (unsigned)ESP_HOSTED_VERSION_PATCH_1);
    } else {
        ESP_LOGW(TAG, "C6: firmware version not reported, host %u.%u.%u",
                 (unsigned)ESP_HOSTED_VERSION_MAJOR_1,
                 (unsigned)ESP_HOSTED_VERSION_MINOR_1,
                 (unsigned)ESP_HOSTED_VERSION_PATCH_1);
    }
}

esp_err_t wifi_radio_link(void)
{
    if (atomic_load(&s_failed)) {
        return ESP_FAIL;
    }
    const int e = esp_hosted_connect_to_slave();
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "C6: no SDIO link (%d)", e);
        return ESP_FAIL;
    }
    if (!s_once) {
        s_once = true;
        link_once();
    }
    return atomic_load(&s_failed) ? ESP_FAIL : ESP_OK;
}

bool wifi_radio_failed(void)
{
    return atomic_load(&s_failed);
}
