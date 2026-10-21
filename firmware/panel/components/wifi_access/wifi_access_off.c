/**
 * @file wifi_access_off.c
 * @brief The panel's Wi-Fi in a build without a radio (RCBENCH_PANEL_WIFI
 *        OFF, firmware/panel/CMakeLists.txt).
 *
 * The radio on the 7-inch ESP32-P4 board is an ESP32-C6, reached through
 * Espressif's esp_wifi_remote, which this build does not carry.
 *
 * wifi_access_init() fails, so the panel runs without Wi-Fi: WI-FI says
 * the radio is not available, and UPDATES checks for no release.  The
 * other calls are made only after an init that worked (wifi_access.h);
 * here each answers as a radio that is off and holds nothing.
 *
 * SPDX-License-Identifier: MIT
 */

#include "wifi_access.h"

#include <string.h>

#include "esp_log.h"

/**
 * @brief Refuse: this build has no radio.
 * @return ESP_ERR_NOT_SUPPORTED.
 */
esp_err_t wifi_access_init(uint32_t slot, const fw_image_info_t *running)
{
    (void)slot;
    (void)running;
    ESP_LOGW("wifi", "this build has no radio (RCBENCH_PANEL_WIFI off)");
    return ESP_ERR_NOT_SUPPORTED;
}

/** @brief Nothing to translate. */
void wifi_access_set_german(bool de)
{
    (void)de;
}

/**
 * @brief Refuse every start.
 * @return @p gate when it says why not, else WA_OFF_FAILED.
 */
wa_off_t wifi_access_start(wa_mode_t mode, wa_off_t gate)
{
    (void)mode;
    return (gate != WA_OFF_NONE) ? gate : WA_OFF_FAILED;
}

/** @brief Nothing to close. */
void wifi_access_close(wa_off_t why)
{
    (void)why;
}

/**
 * @brief Nothing runs.
 * @return false.
 */
bool wifi_access_tick(wa_off_t gate, bool stop, bool may_write)
{
    (void)gate;
    (void)stop;
    (void)may_write;
    return false;
}

/** @brief A radio that is down and holds nothing. */
void wifi_access_get(wifi_access_state_t *out)
{
    memset(out, 0, sizeof(*out));
    wa_init(&out->w);
    out->down = true;
}

/**
 * @brief The radio is always down.
 * @return true.
 */
bool wifi_access_down(void)
{
    return true;
}

/**
 * @brief Keep no network.
 * @return false.
 */
bool wifi_access_save_net(const char *ssid, const char *pass)
{
    (void)ssid;
    (void)pass;
    return false;
}

/** @brief Nothing to forget. */
void wifi_access_forget(void)
{
}

/** @brief No access code to set. */
void wifi_access_set_code(bool on)
{
    (void)on;
}

/** @brief No image to discard. */
void wifi_access_discard(void)
{
}

/**
 * @brief No image arrives.
 * @return false.
 */
bool wifi_access_take(const uint8_t **img, uint32_t *bytes,
                      fw_image_info_t *info)
{
    (void)img;
    (void)bytes;
    (void)info;
    return false;
}

/** @brief No image to release. */
void wifi_access_release(void)
{
}

/**
 * @brief No fetch without a radio.
 * @return WA_GOT_RADIO.
 */
wa_got_t wifi_access_fetch_start(wa_job_t job, fw_version_t want,
                                 wa_channel_t ch, wa_why_t gate)
{
    (void)job;
    (void)want;
    (void)ch;
    (void)gate;
    return WA_GOT_RADIO;
}

/** @brief No fetch runs. */
void wifi_access_fetch_tick(wa_why_t abort)
{
    (void)abort;
}

/** @brief A fetch that never started. */
void wifi_access_fetch_get(wa_fetch_t *out)
{
    memset(out, 0, sizeof(*out));
}

/**
 * @brief No home network.
 * @return false, with @p ssid empty.
 */
bool wifi_access_home(char *ssid, size_t n)
{
    if (ssid != NULL && n > 0u) {
        ssid[0] = '\0';
    }
    return false;
}
