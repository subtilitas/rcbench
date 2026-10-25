/**
 * @file wifi_radio.c
 * @brief The radio under esp_wifi: the board's ESP32-C6, reached over SDIO
 *        (Secure Digital input/output) through esp_wifi_remote and
 *        esp_hosted.
 *
 * See wifi_radio.h.
 *
 * The link: SDIO slot 1, 4 data lines at 40MHz, the pins of board_pins.h
 * (BOARD_C6_*), which esp_hosted reads from sdkconfig.defaults; the checks
 * below fail the build when the two differ.  esp_hosted sets up its 6 tasks
 * and the SDMMC (SD/MMC host) controller before app_main(); it resets and
 * talks to the C6 from the radio's first start on (wifi_radio_link()).
 * Between sessions the radio is off (esp_wifi_deinit() on the C6); the C6
 * stays powered and is not reset again until the panel restarts.  A link
 * that fails, at the first start or later, holds the C6 in reset until the
 * panel restarts.
 *
 * SPDX-License-Identifier: MIT
 */

#include "wifi_radio.h"

#include <stdatomic.h>

#include "board_pins.h"
#include "driver/gpio.h"
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

/*
 * With this option esp_hosted creates its tasks with xTaskCreateStatic(),
 * which the wrap below does not see: its 6 tasks at priority 23 would run
 * on either core, core 1 with the control task included.
 */
#if CONFIG_ESP_HOSTED_DFLT_TASK_FROM_SPIRAM
#error "CONFIG_ESP_HOSTED_DFLT_TASK_FROM_SPIRAM: esp_hosted's tasks escape the core 0 wrap"
#endif

/**
 * @brief How long wifi_radio_link() waits for esp_hosted's TRANSPORT_UP event
 * after esp_hosted_connect_to_slave() has returned, in ms.  esp_hosted
 * posts the event before its connect returns on a C6 that answered, so the
 * wait covers the event loop's delivery only.
 */
#define WIFI_RADIO_UP_WAIT_MS 1000u

/** @brief The poll period of that wait, in ms. */
#define WIFI_RADIO_UP_POLL_MS 10u

/** @brief Log tag. */
static const char *TAG = "wifi";

static atomic_bool s_failed;   /**< the link has failed since start-up */
static atomic_bool s_up;       /**< esp_hosted reported the link up */
static bool        s_handlers; /**< the worker's: event handlers registered */
static bool        s_once;     /**< the worker's: the version logged */

/**
 * @brief The linker's name for ESP-IDF's xTaskCreatePinnedToCore().
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
 * xTaskCreatePinnedToCore() of the image through here, xTaskCreate()
 * included: a task created with no core (tskNO_AFFINITY) runs on core 0,
 * and a task created for a given core stays on it.  xTaskCreateStatic() and
 * the xTaskCreate*WithCaps() calls do not come through here; esp_hosted
 * uses them only with CONFIG_ESP_HOSTED_DFLT_TASK_FROM_SPIRAM, which the
 * \#error above refuses.
 *
 * Not covered by the wrap: the SD card and the C6 share ESP-IDF's SDMMC
 * request mutex, which inherits priority.  While the run log task on
 * core 1 holds it for a card write and an esp_hosted task waits for it,
 * the run log task runs at 23 on core 1, above the control task.  The
 * control task's longest period under that load is not measured.
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
 * @brief Mark the link failed, once: log it and hold the C6 in reset.
 *
 * esp_hosted's tasks keep running after a failure.  A C6 that holds DAT1
 * (its interrupt line) low would keep esp_hosted's read task, priority 23
 * on core 0, failing and logging in a loop.  EN low holds the C6 in reset;
 * its SDIO pins let go, the 51kOhm pull-ups take DAT1 high, and the read
 * task waits.  The C6 stays in reset until the panel restarts.
 *
 * @param why What failed, for the log.
 */
static void link_failed(const char *why)
{
    if (atomic_exchange(&s_failed, true)) {
        return;
    }
    (void)gpio_set_level(BOARD_C6_PIN_EN, 0);
    (void)gpio_set_direction(BOARD_C6_PIN_EN, GPIO_MODE_OUTPUT);
    ESP_LOGE(TAG, "C6 %s: C6 held in reset, Wi-Fi is off until the panel "
                  "restarts", why);
}

/**
 * @brief esp_hosted's events: the link up, and a link that failed for good.
 *
 * esp_hosted posts ESP_HOSTED_EVENT_TRANSPORT_UP once, when the C6 has
 * sent its start event, and ESP_HOSTED_EVENT_TRANSPORT_FAILURE when the
 * SDIO transfers to the C6 have failed for good.  With
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
    if (id == ESP_HOSTED_EVENT_TRANSPORT_UP) {
        atomic_store(&s_up, true);
    } else if (id == ESP_HOSTED_EVENT_TRANSPORT_FAILURE) {
        link_failed("link failed");
    }
}

/**
 * @brief Register on_hosted() for the two events, before the first connect.
 * @return true when both handlers are registered.
 */
static bool handlers_once(void)
{
    if (s_handlers) {
        return true;
    }
    if (esp_event_handler_register(ESP_HOSTED_EVENT,
                                   ESP_HOSTED_EVENT_TRANSPORT_UP, on_hosted,
                                   NULL) != ESP_OK
        || esp_event_handler_register(ESP_HOSTED_EVENT,
                                      ESP_HOSTED_EVENT_TRANSPORT_FAILURE,
                                      on_hosted, NULL) != ESP_OK) {
        (void)esp_event_handler_unregister(ESP_HOSTED_EVENT,
                                           ESP_HOSTED_EVENT_TRANSPORT_UP,
                                           on_hosted);
        return false;
    }
    s_handlers = true;
    return true;
}

/**
 * @brief Wait for the TRANSPORT_UP event, at most WIFI_RADIO_UP_WAIT_MS.
 * @return true when the link is up.
 */
static bool wait_up(void)
{
    for (uint32_t t = 0; t < WIFI_RADIO_UP_WAIT_MS;
         t += WIFI_RADIO_UP_POLL_MS) {
        if (atomic_load(&s_up)) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(WIFI_RADIO_UP_POLL_MS));
    }
    return atomic_load(&s_up);
}

/**
 * @brief Once, at the first start with the link up: the C6's version.
 *
 * A C6 firmware that predates the version request answers it with an
 * error or not at all; the request then ends after esp_hosted's response
 * time-out, 5s, once.
 */
static void version_once(void)
{
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
    if (!handlers_once()) {
        ESP_LOGE(TAG, "C6: no handler for the link's events");
        return ESP_FAIL;
    }
    /* Its answer is ESP_OK whether or not the C6 came up; the event is
     * what tells. */
    (void)esp_hosted_connect_to_slave();
    if (!wait_up()) {
        link_failed("did not answer");
        return ESP_FAIL;
    }
    if (!s_once) {
        s_once = true;
        version_once();
    }
    return atomic_load(&s_failed) ? ESP_FAIL : ESP_OK;
}

bool wifi_radio_failed(void)
{
    return atomic_load(&s_failed);
}
