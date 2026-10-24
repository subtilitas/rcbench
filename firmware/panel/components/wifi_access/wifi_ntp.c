/**
 * @file
 * @brief The clock set from the home network.  See wifi_ntp.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "wifi_ntp.h"

#include <stdint.h>
#include <sys/time.h>

#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "wall_clock.h"

/** @brief The console tag of the clock's lines. */
static const char *TAG = "clock";

/* The answer, written by the TCP/IP task's callback, read by the worker. */
/** @brief Guards the answer between the TCP/IP task and the worker. */
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
/** @brief An answer has set the clock since the start. */
static bool    s_answered;
/** @brief The time the answer set, microseconds since 1970 UTC. */
static int64_t s_new_us;
/** @brief The old clock at the answer, microseconds since 1970 UTC. */
static int64_t s_old_us;

/* The worker's own. */
/** @brief This join's request (wall_clock.h). */
static wall_ntp_t s_req;
/** @brief esp_netif_sntp_init() succeeded and no deinit followed. */
static bool    s_running;
/** @brief The clock at the start, microseconds since 1970 UTC. */
static int64_t s_base_wall_us;
/** @brief esp_timer at the start, microseconds since boot. */
static int64_t s_base_mono_us;

/**
 * @brief The system time.
 * @return Microseconds since 1970-01-01 UTC.
 */
static int64_t wall_now_us(void)
{
    struct timeval tv;
    (void)gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000000 + (int64_t)tv.tv_usec;
}

/**
 * @brief The answer, on the TCP/IP task, after lwIP has set the clock: the
 *        old clock is the one at the start carried on by esp_timer, which
 *        the step does not move.
 * @param tv The time the answer set.
 */
static void on_sync(struct timeval *tv)
{
    const int64_t mono = esp_timer_get_time();
    portENTER_CRITICAL(&s_mux);
    if (!s_answered && tv != NULL) {
        s_new_us = (int64_t)tv->tv_sec * 1000000 + (int64_t)tv->tv_usec;
        s_old_us = s_base_wall_us + (mono - s_base_mono_us);
        s_answered = true;
    }
    portEXIT_CRITICAL(&s_mux);
}

/** @brief Start the client, which asks at once (no start-up delay,
 *         CONFIG_LWIP_SNTP_STARTUP_DELAY off). */
static void ntp_start(void)
{
    /* gettimeofday() takes a lock: read outside the critical section.
     * No client runs yet, so nothing reads the base before it is set. */
    const int64_t wall = wall_now_us();
    const int64_t mono = esp_timer_get_time();
    portENTER_CRITICAL(&s_mux);
    s_answered = false;
    s_base_wall_us = wall;
    s_base_mono_us = mono;
    portEXIT_CRITICAL(&s_mux);
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(WALL_NTP_SERVER);
    cfg.wait_for_sync = false;
    cfg.sync_cb = on_sync;
    const esp_err_t e = esp_netif_sntp_init(&cfg);
    if (e == ESP_OK) {
        s_running = true;
        ESP_LOGI(TAG, "ntp: asking %s", WALL_NTP_SERVER);
    } else {
        /* Nothing runs to answer: this join asks no more. */
        s_req.state = WALL_NTP_DONE;
        ESP_LOGW(TAG, "ntp: not started: %s", esp_err_to_name(e));
    }
}

/** @brief Stop the client; nothing when it never started. */
static void ntp_end(void)
{
    if (s_running) {
        esp_netif_sntp_deinit();
        s_running = false;
    }
}

void wifi_ntp_poll(bool joined)
{
    portENTER_CRITICAL(&s_mux);
    const bool answered = s_answered;
    const int64_t new_us = s_new_us;
    const int64_t old_us = s_old_us;
    portEXIT_CRITICAL(&s_mux);
    const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    switch (wall_ntp_step(&s_req, joined, answered, now_ms)) {
    case WALL_NTP_START:
        ntp_start();
        break;
    case WALL_NTP_SET: {
        char line[128];
        ntp_end();
        ESP_LOGI(TAG, "%s", wall_ntp_line(line, sizeof(line),
                                          WALL_NTP_SERVER, new_us, old_us));
        break;
    }
    case WALL_NTP_GAVE_UP:
        ntp_end();
        ESP_LOGW(TAG, "ntp: no answer from %s in %ums; clock unchanged",
                 WALL_NTP_SERVER, (unsigned)WALL_NTP_TIMEOUT_MS);
        break;
    case WALL_NTP_STOP:
        ntp_end();
        ESP_LOGI(TAG, "ntp: join ended before an answer from %s; clock "
                 "unchanged", WALL_NTP_SERVER);
        break;
    case WALL_NTP_NONE:
    default:
        break;
    }
}

void wifi_ntp_stop(void)
{
    wifi_ntp_poll(false);
}
