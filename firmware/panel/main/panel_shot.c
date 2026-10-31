/**
 * @file panel_shot.c
 * @brief The screenshot on the panel: one picture in PSRAM (pseudo-static
 *        RAM), a task that writes it, and the facts of the gate.
 *
 * The task "shot" runs at priority 0 on core 0, below the render loop, for
 * one job and ends: it costs internal RAM only while it runs, SHOT_STACK for
 * its stack and FM_CHUNK for the card's buffer.  The encoder's state,
 * sizeof(png_enc_t), is in PSRAM.  It holds the card's job lock
 * (panel_files_lock()) for the whole job, waits for it at most 2000ms, and
 * answers FM_ERR_BUSY after that; the Wi-Fi page's writes wait the same.
 *
 * Each save logs its bytes on the card, its time and the stack it left, and
 * each capture its copy time.  Measured on a panel: 38,388 to 77,840B a PNG
 * over 8 files, a save in 5.3 to 6.3s alone and in 0.40 to 1.00s with a Wi-Fi
 * page write waiting for the lock (the cause of the difference is not
 * traced), the copy in 25.8 to 31.6ms (median 28.3ms over 54 copies) and the
 * lowest unused stack of the task 5,116B of 7,168B.  30 shots in a row ended
 * with no error and no restart.  Not run on a panel: no card in the slot, a
 * second contact during a hold, pixel equality with a rendered frame.
 *
 * SPDX-License-Identifier: MIT
 */
#include "panel_shot.h"

#include <stdatomic.h>
#include <stdio.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "fm_ops.h"
#include "panel.h"
#include "shot_file.h"
#include "shot_gate.h"
#include "storage.h"
#include "ui_screen.h"
#include "ui_shot.h"

/** @brief The log tag of the panel's lines. */
static const char *TAG = "rcbench";

/** @brief The task's stack, bytes: cardjob's 6,656 and the file's paths. */
#define SHOT_STACK 7168
/** @brief The task's priority: below the render loop. */
#define SHOT_PRIO  0
/** @brief The task's core: the render loop's. */
#define SHOT_CORE  0
/** @brief The longest wait for the card's job lock, ms. */
#define SHOT_LOCK_MS 2000u

/** @brief Where the job stands. */
enum { JOB_IDLE = 0, JOB_RUNNING, JOB_ANSWERED };

/** @brief JOB_IDLE, JOB_RUNNING or JOB_ANSWERED. */
static atomic_int  s_state;
/** @brief How far a save has come, 0 to 100. */
static atomic_int  s_percent;
/** @brief The popup asked the save to stop. */
static atomic_bool s_cancel;
/** @brief What the task is asked for. */
static ui_shot_job_t s_job;
/** @brief The picture of a save. */
static const gfx_color_t *s_pixels;
/** @brief The name of a save, without its extension: the task's own copy. */
static char s_base[SHOT_BASE_MAX + 1u];
/** @brief What the job came to. */
static fm_err_t s_err;
/** @brief The next number, from UI_SHOT_JOB_NEXT. */
static unsigned s_number;

/**
 * @brief shot_save_t's cancelled().
 * @param ctx  unused
 * @return the popup asked it to stop
 */
static bool save_cancelled(void *ctx)
{
    (void)ctx;
    return atomic_load(&s_cancel);
}

/**
 * @brief shot_save_t's progress().
 * @param done   rows done
 * @param total  rows in all
 * @param ctx    unused
 */
static void save_progress(int done, int total, void *ctx)
{
    (void)ctx;
    atomic_store(&s_percent, done * 100 / total);
}

/**
 * @brief One save: the encoder's state in PSRAM, the chunk in internal RAM,
 *        either missing an honest answer.
 * @return what it came to
 */
static fm_err_t run_save(void)
{
    png_enc_t *enc = heap_caps_malloc(sizeof(*enc), MALLOC_CAP_SPIRAM
                                                        | MALLOC_CAP_8BIT);
    uint8_t *chunk = heap_caps_malloc(FM_CHUNK, MALLOC_CAP_INTERNAL
                                                    | MALLOC_CAP_DMA
                                                    | MALLOC_CAP_8BIT);
    fm_err_t e = SHOT_ERR_MEMORY;
    if (enc != NULL && chunk != NULL) {
        const shot_save_t save = {
            .io = panel_files_card(), .pixels = s_pixels, .w = UI_PANEL_W,
            .h = UI_PANEL_H, .base = s_base, .enc = enc, .chunk = chunk,
            .chunk_size = FM_CHUNK, .cancelled = save_cancelled,
            .progress = save_progress, .ctx = NULL,
        };
        char path[FM_PATH_SIZE];
        const int64_t t0 = esp_timer_get_time();
        e = shot_file_save(&save, path, sizeof(path));
        ESP_LOGI(TAG, "screenshot: %s in %lldms: %d; shot stack %uB unused",
                 (e == FM_OK) ? path : s_base,
                 (long long)((esp_timer_get_time() - t0) / 1000), (int)e,
                 (unsigned)uxTaskGetStackHighWaterMark(NULL));
    } else {
        ESP_LOGW(TAG, "screenshot: no memory for the encoder or the chunk");
    }
    heap_caps_free(enc);
    heap_caps_free(chunk);
    return e;
}

/**
 * @brief The task: one job, then gone.
 * @param arg  unused
 */
static void shot_task(void *arg)
{
    (void)arg;
    fm_err_t e = FM_ERR_BUSY;
    if (panel_files_lock(SHOT_LOCK_MS)) {
        (void)storage_check();
        e = (s_job == UI_SHOT_JOB_SAVE)
                ? run_save() : shot_file_next(panel_files_card(), &s_number);
        panel_files_unlock(false);
    }
    s_err = e;
    atomic_store(&s_state, JOB_ANSWERED);
    vTaskDelete(NULL);
}

/**
 * @brief ui_shot_io_t's alloc().
 * @param n    bytes
 * @param ctx  unused
 * @return PSRAM, or NULL
 */
static void *io_alloc(size_t n, void *ctx)
{
    (void)ctx;
    return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

/**
 * @brief ui_shot_io_t's release().
 * @param p    the memory
 * @param ctx  unused
 */
static void io_release(void *p, void *ctx)
{
    (void)ctx;
    heap_caps_free(p);
}

/**
 * @brief ui_shot_io_t's start().
 * @param job     what is asked
 * @param pixels  the picture
 * @param base    the name
 * @param ctx     unused
 * @return false with a job under way or no RAM for the task
 */
static bool io_start(ui_shot_job_t job, const gfx_color_t *pixels,
                     const char *base, void *ctx)
{
    (void)ctx;
    if (atomic_load(&s_state) != JOB_IDLE) {
        return false;
    }
    s_job = job;
    s_pixels = pixels;
    (void)snprintf(s_base, sizeof(s_base), "%s", (base != NULL) ? base : "");
    s_number = 1u;
    atomic_store(&s_percent, 0);
    atomic_store(&s_cancel, false);
    atomic_store(&s_state, JOB_RUNNING);
    if (xTaskCreatePinnedToCore(shot_task, "shot", SHOT_STACK, NULL, SHOT_PRIO,
                                NULL, SHOT_CORE) != pdPASS) {
        atomic_store(&s_state, JOB_IDLE);
        ESP_LOGW(TAG, "screenshot: no internal RAM for its task");
        return false;
    }
    return true;
}

/**
 * @brief ui_shot_io_t's poll().
 * @param err     takes the answer
 * @param number  takes the number
 * @param ctx     unused
 * @return true once per job ended
 */
static bool io_poll(fm_err_t *err, unsigned *number, void *ctx)
{
    (void)ctx;
    if (atomic_load(&s_state) != JOB_ANSWERED) {
        return false;
    }
    *err = s_err;
    *number = s_number;
    atomic_store(&s_state, JOB_IDLE);
    return true;
}

/**
 * @brief ui_shot_io_t's percent().
 * @param ctx  unused
 * @return the save's progress
 */
static int io_percent(void *ctx)
{
    (void)ctx;
    return atomic_load(&s_percent);
}

/**
 * @brief ui_shot_io_t's cancel().
 * @param ctx  unused
 */
static void io_cancel(void *ctx)
{
    (void)ctx;
    atomic_store(&s_cancel, true);
}

/**
 * @brief ui_shot_io_t's now_us().
 * @param ctx  unused
 * @return the clock
 */
static uint32_t io_now_us(void *ctx)
{
    (void)ctx;
    return (uint32_t)esp_timer_get_time();
}

/**
 * @brief ui_shot_io_t's copied().
 * @param us   the copy's time
 * @param ctx  unused
 */
static void io_copied(uint32_t us, void *ctx)
{
    (void)ctx;
    ESP_LOGI(TAG, "screenshot: frame copied in %uus", (unsigned)us);
}

/** @brief The panel's side of the screenshot. */
static const ui_shot_io_t k_io = {
    .alloc = io_alloc, .release = io_release, .start = io_start,
    .poll = io_poll, .percent = io_percent, .cancel = io_cancel,
    .now_us = io_now_us, .copied = io_copied, .ctx = NULL,
};

void panel_shot_init(void)
{
    ui_shot_bind(&k_io);
}

void panel_shot_frame(const panel_shot_in_t *in)
{
    const unsigned drive = atomic_load(&s_drive);
    const shot_bench_t b = {
        .read = true,
        .link_up = in->link_up,
        .simulated = in->simulated,
        .armed = in->armed,
        .arm_wanted = in->arm_wanted,
        .stop_request = atomic_load(&s_stop_request),
        .supply_on = in->supply_now,
        .supply_sample = in->supply_seen,
        .supply_coming = supply_live_or_coming(),
        .prog5v_on = in->psup_now,
        .prog5v_coming = supplies_live_or_coming(),
        .hitec_holds = ((drive >> DRIVE_HELD_SHIFT) & 0xFu) != 0u,
        .card_job_out =
            atomic_load(&s_kst_file_sent) != atomic_load(&s_kst_file_done)
            || atomic_load(&s_kst_card_sent)
                   != atomic_load(&s_kst_card_answered)
            || atomic_load(&s_hitec_file_sent)
                   != atomic_load(&s_hitec_file_done)
            || atomic_load(&s_hitec_card_sent)
                   != atomic_load(&s_hitec_card_answered),
        .testing = atomic_load(&s_servo_testing),
        .cu_active = in->cu_active,
        .update_busy = panel_update_busy(),
        .faults = in->faults,
        .fault_log_out = log_fault_busy(),
        .run_open = panel_run_open(),
    };
    shot_facts_t f;
    shot_facts_read(&b, &f);
    ui_router_shot_set_idle(shot_gate_idle(&f));
}
