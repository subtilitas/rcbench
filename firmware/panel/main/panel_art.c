/*
 * The board's drawing: fetched from the IOmcu a slice of a poll at a
 * time on the control task, kept in flash a sector a pass on a safe bench,
 * and published for the picker on the UI task.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdatomic.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "arming.h"
#include "art_fetch.h"
#include "art_store.h"
#include "flash_gate.h"
#include "link_host.h"
#include "link_pages.h"
#include "outputs.h"
#include "panel.h"
#include "picker_screen.h"

static const char *TAG = "rcbench";

/*
 * Fetching the board's drawing, a slice of a poll at a time.
 *
 * Up to LINK_DRAW_MAX_BYTES (5328 bytes) over a link that carries sixty-two
 * bytes a transaction is up to 86 round trips.  Doing them in a loop would
 * stop the control task for the length of the transfer, so a bounded slice
 * of each poll goes to it and the bench keeps its cadence.
 */
#define ART_SLICE_MS 15u

const art_flash_t        *s_artflash;
static art_fetch_t        s_artfetch;
static uint8_t           *s_artbuf;
static uint16_t           s_artboard;
bool                      s_artbusy;

/*
 * Writing it down: one sector a control pass, and only on a safe bench.
 *
 * A drawing of up to 5328 bytes is up to 3 sector erases (the header's and
 * two of payload) and 2 writes and the header.  Auto-suspend
 * is off, so each one stalls both cores, this task with them, for its length
 * -- tens of milliseconds an erase.  No other task can take that stall off
 * this one.  So the keep runs here, a step a pass (art_keep_service()), and
 * a step is taken only when flash_gate_open() says so: disarmed, no arm
 * waiting, the supply off and no ON queued.  The gate is read on the same
 * task, in the same pass, that applies an arm and an ON, so neither can come
 * in between the check and the sector.
 *
 * An arm or an ON pauses the keep between two sectors; it holds its buffer
 * and carries on from the same step on the next safe pass.  Nothing is
 * findable for the board until the header, written last.
 *
 * A flash operation closes the cache for its length; the display's
 * scan-out reads PSRAM by DMA and its interrupt runs regardless
 * (CONFIG_LCD_DSI_ISR_CACHE_SAFE).
 */
static uint8_t     *s_keepbuf;      /* the keeper's, once handed over */
static art_entry_t  s_keepentry;
static art_keep_t   s_keep;         /* control task only */
atomic_bool         s_keeping;      /* set and cleared by the control task */
static bool         s_keep_waited;  /* the wait is logged once a keep */

/*
 * The drawing the picker may show, published by the control task and copied
 * out by the UI task.
 *
 * Emptied on every link-up edge before anything else is asked, so a drawing
 * outlives neither the IOmcu it came from nor a reflash of it.  Filled
 * only with bytes that match the ARTWORK page read on that link-up: fetched
 * and checked against its CRC and hash, or read from flash under an entry
 * whose every field equals the page (art_store_read_same()).  The board ID
 * alone never selects a kept drawing.
 *
 * s_draw_gen counts publications; the UI task re-asks when it moves, so an
 * open picker follows a link-up instead of waiting for its next visit.
 */
SemaphoreHandle_t        s_draw_lock;
uint8_t                 *s_drawpub;        /* LINK_DRAW_MAX_BYTES, PSRAM */
static uint32_t          s_drawpub_bytes;  /* 0: nothing to show; lock */
static uint16_t          s_drawpub_board;  /* lock */
static atomic_uint       s_draw_gen;
uint8_t                 *s_drawshow;       /* the UI task's copy, PSRAM */
static unsigned          s_draw_seen;      /* UI task only */

/* ------------------------------------------------------ the board drawing */

/* Publish @p bytes of @p src for @p board, or nothing for a NULL @p src. */
static void draw_publish(uint16_t board, const uint8_t *src, uint32_t bytes)
{
    if (s_draw_lock == NULL || s_drawpub == NULL) {
        return;
    }
    xSemaphoreTake(s_draw_lock, portMAX_DELAY);
    if (src != NULL && bytes != 0u && bytes <= LINK_DRAW_MAX_BYTES) {
        memcpy(s_drawpub, src, bytes);
        s_drawpub_bytes = bytes;
        s_drawpub_board = board;
    } else {
        s_drawpub_bytes = 0u;
    }
    xSemaphoreGive(s_draw_lock);
    atomic_fetch_add(&s_draw_gen, 1u);
}

/*
 * The picker asking for a board's drawing: on the way into the screen, and
 * from the UI loop whenever a link-up edge published or withdrew one.  UI
 * task only.  The copy is the picker's until the next call, and the picker
 * checks it against the board's catalogue before showing it.
 */
void drawing_for_picker(uint16_t board)
{
    uint32_t bytes = 0u;
    if (s_draw_lock != NULL && s_drawpub != NULL && s_drawshow != NULL) {
        xSemaphoreTake(s_draw_lock, portMAX_DELAY);
        if (s_drawpub_bytes != 0u && s_drawpub_board == board) {
            memcpy(s_drawshow, s_drawpub, s_drawpub_bytes);
            bytes = s_drawpub_bytes;
        }
        xSemaphoreGive(s_draw_lock);
    }
    if (bytes == 0u) {
        (void)picker_screen_set_drawing(NULL, 0u, NULL);
        return;
    }
    const char *why = "";
    if (!picker_screen_set_drawing(s_drawshow, bytes, &why)) {
        ESP_LOGW(TAG, "hardware %u's drawing is not shown: %s",
                 (unsigned)board, why);
    }
}

/* A publication since the picker last asked: ask again. UI task only. */
void drawing_follow(void)
{
    const unsigned gen = atomic_load(&s_draw_gen);
    if (gen != s_draw_seen) {
        s_draw_seen = gen;
        drawing_for_picker(picker_screen_binding()->board);
    }
}

static void art_keep_end(void)
{
    heap_caps_free(s_keepbuf);
    s_keepbuf = NULL;
    atomic_store(&s_keeping, false);
}

/*
 * One step of the keep, from the control task after the pass has applied
 * its arm, disarm and supply commands.  At most one sector, and none unless
 * the bench is disarmed with no arm waiting and the supply off and not
 * coming on.
 */
void art_keep_service(void)
{
    if (!atomic_load(&s_keeping)) {
        return;
    }
    const bool open = flash_gate_open(
        arming_link_needed(&s_arm, outputs_armed(&s_out)),
        supply_live_or_coming());
    switch (art_keep_step(&s_keep, open)) {
    case ART_KEEP_WAITING:
        if (!s_keep_waited) {
            s_keep_waited = true;
            ESP_LOGI(TAG, "keeping hardware %u's drawing waits for a "
                          "bench in SAFE with the supply off",
                     (unsigned)s_keepentry.board);
        }
        break;
    case ART_KEEP_DONE:
        ESP_LOGI(TAG, "hardware %u's drawing kept",
                 (unsigned)s_keepentry.board);
        art_keep_end();
        break;
    case ART_KEEP_FAILED:
        ESP_LOGW(TAG, "hardware %u's drawing could not be kept; it will be "
                      "fetched again", (unsigned)s_keepentry.board);
        art_keep_end();
        break;
    case ART_KEEP_IDLE:
        art_keep_end();             /* not reached: s_keeping means busy */
        break;
    default:
        break;
    }
}

void art_stop(const char *why)
{
    if (s_artbuf != NULL) {
        heap_caps_free(s_artbuf);
        s_artbuf = NULL;
    }
    if (s_artbusy && why != NULL) {
        ESP_LOGW(TAG, "gave up on hardware %u's drawing: %s",
                 (unsigned)s_artboard, why);
    }
    s_artbusy = false;
}

/*
 * How the fetch reaches the far end.  The sequence itself is in
 * shared/artwork, where the suite runs it against a device in memory and can
 * stop the link at a chosen transaction; this is the two calls it needs.
 */
static bool art_link_read(void *ctx, uint8_t page, uint8_t count,
                          uint16_t *regs)
{
    (void)ctx;
    link_msg_t r;
    if (!poll_page(&s_host, page, count, &r) || r.op != LINK_OP_DATA) {
        return false;
    }
    for (uint8_t i = 0; i < count; ++i) {
        regs[i] = r.regs[i];
    }
    return true;
}

static bool art_link_write(void *ctx, uint8_t page, uint8_t reg,
                           uint16_t value)
{
    (void)ctx;
    link_msg_t ack;
    return write_regs(&s_host, page, reg, 1u, &value, &ack)
           && ack.op != LINK_OP_NACK;
}

static const art_transport_t s_art_link = {
    art_link_read, art_link_write, NULL,
};

/*
 * On the link-up edge: withdraw the last drawing, ask what this one is, and
 * show the kept copy only when it is this drawing -- else fetch it.
 *
 * Two transactions and at most one flash read here; a fetch happens a slice
 * of a poll at a time below.  An IOmcu older than 4.20 serves a
 * photograph or nothing, and is drawn from its shape.
 */
void art_begin(uint16_t board)
{
    art_stop(NULL);
    draw_publish(board, NULL, 0u);
    s_artboard = board;

    if (s_far_minor < LINK_MINOR_DRAWING) {
        ESP_LOGI(TAG, "hardware %u serves no board drawing (protocol 4.%u); "
                      "PICK A PIN draws its shape", (unsigned)board,
                 (unsigned)s_far_minor);
        return;
    }
    uint16_t meta[LINK_AW_COUNT];
    uint32_t bytes = 0u;
    const char *why = "";
    if (!art_fetch_meta(&s_art_link, meta, &bytes, &why)) {
        /* Three different things end here -- no drawing, no page, and a page
         * that disagrees with itself -- and only the last is a fault. The
         * line says which rather than reporting all three as the first. */
        ESP_LOGI(TAG, "no drawing from hardware %u: %s", (unsigned)board,
                 why);
        return;
    }
    art_entry_t key;
    if (!art_fetch_key(meta, board, &key)) {
        return;         /* not reached: art_fetch_meta() checked the page */
    }

    s_artbuf = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
    if (s_artbuf == NULL) {
        ESP_LOGW(TAG, "no room for a %u byte drawing", (unsigned)bytes);
        return;
    }
    if (s_artflash != NULL && !atomic_load(&s_keeping)
        && art_store_read_same(s_artflash, &key, s_artbuf, bytes)) {
        draw_publish(board, s_artbuf, bytes);
        ESP_LOGI(TAG, "hardware %u's drawing is kept: %u bytes, hash %08lx",
                 (unsigned)board, (unsigned)bytes, (unsigned long)key.hash);
        art_stop(NULL);
        return;
    }
    if (!art_fetch_begin(&s_artfetch, meta, s_artbuf, bytes)) {
        ESP_LOGW(TAG, "hardware %u: %s", (unsigned)board,
                 art_fetch_why(&s_artfetch));
        art_stop(NULL);
        return;
    }
    s_artbusy = true;
    ESP_LOGI(TAG, "fetching hardware %u's drawing: %u bytes, hash %08lx",
             (unsigned)board, (unsigned)bytes, (unsigned long)key.hash);
}

/* It arrived and it is the drawing it claimed.  Show it, then keep it. */
static void art_finish(void)
{
    s_artbusy = false;
    const uint32_t bytes = art_fetch_bytes(&s_artfetch);
    draw_publish(s_artboard, s_artbuf, bytes);
    if (s_artflash == NULL || atomic_load(&s_keeping)) {
        ESP_LOGW(TAG, "hardware %u's drawing arrived; it is not kept, so it "
                      "will be fetched again", (unsigned)s_artboard);
        art_stop(NULL);
        return;
    }
    s_keepentry.board  = s_artboard;
    s_keepentry.format = art_fetch_format(&s_artfetch);
    s_keepentry.width  = art_fetch_width(&s_artfetch);
    s_keepentry.height = art_fetch_height(&s_artfetch);
    s_keepentry.crc    = art_fetch_crc(&s_artfetch);
    s_keepentry.hash   = art_fetch_hash(&s_artfetch);
    s_keepentry.bytes  = bytes;
    /*
     * Hand the buffer over and stop owning it.  Nothing is written here:
     * art_keep_service() takes it a sector a pass, on a safe bench.  The
     * fetch checked the payload's CRC and hash; the keep's read-back checks
     * the flash against them before the header goes in.
     */
    s_keepbuf = s_artbuf;
    s_artbuf  = NULL;
    if (!art_keep_begin(&s_keep, s_artflash, s_keepentry.board, &s_keepentry,
                        s_keepbuf)) {
        ESP_LOGW(TAG, "hardware %u's drawing does not fit a slot; it will "
                      "be fetched again", (unsigned)s_artboard);
        heap_caps_free(s_keepbuf);
        s_keepbuf = NULL;
        return;
    }
    s_keep_waited = false;
    atomic_store(&s_keeping, true);
}

/*
 * One bounded slice of the fetch.
 *
 * A block at a time until the slice is spent: this task also beats the
 * safety line, and its ceiling is 150ms.  The sequence does exactly the
 * blocks it is given, so how much of a poll goes to a drawing is decided
 * here and nowhere else.
 */
void art_slice(void)
{
    const uint32_t began = now_ms();
    while (s_artbusy && (uint32_t)(now_ms() - began) < ART_SLICE_MS) {
        switch (art_fetch_step(&s_artfetch, &s_art_link, 1u)) {
        case ART_FETCH_RUNNING:
            break;
        case ART_FETCH_DONE:
            art_finish();
            return;
        default:
            art_stop(art_fetch_why(&s_artfetch));
            return;
        }
    }
}
