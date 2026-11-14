/**
 * @file panel_blec.c
 * @brief The Bluetooth LE clients' task (panel_blec.h).
 *
 * The task owns the hub.  Each client's calls are direct, by owner, so the
 * stack check follows every chain from the task's entry.
 *
 * SPDX-License-Identifier: MIT
 */
#include "panel_blec.h"

#include <stdbool.h>
#include <stdint.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bleuart_hub.h"
#include "panel.h"
#include "panel_ble.h"
#include "panel_bal.h"
#include "panel_devupd.h"
#include "panel_dial.h"
#include "panel_imu.h"
#include "panel_pnode.h"

/** @brief The console lines' tag. */
static const char *TAG = "blec";

/** @brief The task's poll, ms: every client is stepped at least this
 *         often. */
#define BLEC_POLL_MS  5
/** @brief Host stack events taken in one poll at most: the whole queue. */
#define BLEC_EVTS_MAX PANEL_BLE_QUEUE_LEN
/** @brief Actions taken from one client in one poll at most. */
#define BLEC_ACTS_MAX 16

_Static_assert(pdMS_TO_TICKS(BLEC_POLL_MS) >= 1,
               "the poll blocks for a tick at least: the task never spins");

/** @brief The hub, the task's own. */
static bleu_hub_t s_hub;
/** @brief The task was started. */
static bool s_started;

/**
 * @brief An event to the client @p owner.
 * @param owner The client.
 * @param e     The event.
 * @param now   The clock.
 */
static void client_event(uint8_t owner, const bleu_evt_t *e, uint32_t now)
{
    if (owner == PANEL_BLEC_IMU) {
        panel_imu_blec_event(e, now);
    } else if (owner == PANEL_BLEC_DIAL) {
        panel_dial_blec_event(e, now);
    } else if (owner == PANEL_BLEC_PNODE) {
        panel_pnode_blec_event(e, now);
    } else if (owner == PANEL_BLEC_BAL) {
        panel_bal_blec_event(e, now);
    } else if (owner == PANEL_BLEC_UPD) {
        panel_devupd_blec_event(e, now);
    }
}

/**
 * @brief The next action of the client @p owner.
 * @param owner The client.
 * @param now   The clock.
 * @param out   Receives the action.
 * @return true when @p out holds one.
 */
static bool client_step(uint8_t owner, uint32_t now, bleu_act_t *out)
{
    if (owner == PANEL_BLEC_IMU) {
        return panel_imu_blec_step(now, out);
    }
    if (owner == PANEL_BLEC_DIAL) {
        return panel_dial_blec_step(now, out);
    }
    if (owner == PANEL_BLEC_PNODE) {
        return panel_pnode_blec_step(now, out);
    }
    if (owner == PANEL_BLEC_BAL) {
        return panel_bal_blec_step(now, out);
    }
    if (owner == PANEL_BLEC_UPD) {
        return panel_devupd_blec_step(now, out);
    }
    return false;
}

/**
 * @brief The hub's events to their clients and its actions to NimBLE; a
 *        write without response is answered at once (bleu_hub_written()).
 * @param now The clock.
 */
static void hand_out(uint32_t now)
{
    uint8_t owner;
    bleu_evt_t e;
    bleu_act_t a;
    bool more = true;
    while (more) {
        more = false;
        while (bleu_hub_next_event(&s_hub, &owner, &e)) {
            client_event(owner, &e, now);
        }
        while (bleu_hub_next_action(&s_hub, &a)) {
            int written = 0;
            if (panel_ble_do(&a, &written)) {
                /* A write without response is answered here, at once:
                 * its WRITE_DONE goes to the client before the next. */
                bleu_hub_written(&s_hub, &a, written, now);
                more = true;
            }
        }
    }
}

/**
 * @brief The task: the clients' messages, the events, the hub's timers,
 *        the actions and the views, every BLEC_POLL_MS.
 * @param arg Unused.
 */
static void blec_task(void *arg)
{
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(BLEC_POLL_MS));
        const uint32_t now = now_ms();
        panel_imu_blec_poll(now);
        panel_dial_blec_poll(now);
        panel_pnode_blec_poll(now);
        panel_bal_blec_poll(now);
        panel_devupd_blec_poll(now);
        bleu_evt_t e;
        for (unsigned i = 0; i < BLEC_EVTS_MAX && panel_ble_get(&e); ++i) {
            bleu_hub_event(&s_hub, &e, now);
            hand_out(now);
        }
        bleu_hub_tick(&s_hub, now);
        hand_out(now);
        for (uint8_t owner = 0; owner < BLEU_HUB_CLIENTS; ++owner) {
            bleu_act_t a;
            for (int i = 0; i < BLEC_ACTS_MAX && client_step(owner, now, &a);
                 ++i) {
                bleu_hub_action(&s_hub, owner, &a, now);
                hand_out(now);
            }
        }
        panel_devupd_blec_chunks(now);
        panel_imu_blec_publish(now);
        panel_dial_blec_publish(now);
        panel_pnode_blec_publish(now);
        panel_bal_blec_publish(now);
        panel_devupd_blec_publish(now);
    }
}

void panel_blec_start(void)
{
    if (s_started) {
        return;
    }
    s_started = true;
    bleu_hub_init(&s_hub);
    (void)bleu_hub_add(&s_hub, PANEL_BLEC_IMU);
    (void)bleu_hub_add(&s_hub, PANEL_BLEC_DIAL);
    (void)bleu_hub_add(&s_hub, PANEL_BLEC_PNODE);
    (void)bleu_hub_add(&s_hub, PANEL_BLEC_BAL);
    (void)bleu_hub_add(&s_hub, PANEL_BLEC_UPD);
    /*
     * Core 0 beside the renderer, at the knob's priority 2: polls of 5ms,
     * nothing with a deadline.  Stack 4096 bytes: the clients and the hub
     * are static; the deepest chain is a NimBLE call of panel_ble_do() or
     * the IMU's reading line, tools/stack_check.py holds it.
     */
    if (xTaskCreatePinnedToCore(blec_task, "blec", 4096, NULL, 2, NULL, 0)
        != pdPASS) {
        ESP_LOGE(TAG, "task not created; Bluetooth LE clients off");
    }
}
