/**
 * @file fw_ota.c
 * @brief The application slots over esp_ota_ops.
 *
 * SPDX-License-Identifier: MIT
 */

#include "fw_ota.h"

#include <stdio.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"

/** @brief The console tag. */
static const char *TAG = "fw_ota";

uint32_t fw_ota_slot_bytes(void)
{
    const esp_partition_t *p = esp_ota_get_next_update_partition(NULL);
    return (p != NULL) ? (uint32_t)p->size : 0u;
}

bool fw_ota_running(fw_image_info_t *out)
{
    memset(out, 0, sizeof(*out));
    const esp_app_desc_t *d = esp_app_get_description();
    snprintf(out->version_text, sizeof(out->version_text), "%s", d->version);
    memcpy(out->elf_sha, d->app_elf_sha256, sizeof(out->elf_sha));
    return fw_version_parse(d->version, strnlen(d->version,
                                                 sizeof(d->version)),
                            &out->version);
}

/* ------------------------------------------------------------- the writer */

/** @brief One write into the slot that is not running. */
typedef struct {
    const esp_partition_t *part;
    esp_ota_handle_t       h;
    bool                   open;
    uint32_t               next;    /* esp_ota_write() appends: the offset */
} ota_ctx_t;

/** @brief The write under way; one at a time. */
static ota_ctx_t s_ctx;

/**
 * @brief Open the slot that is not running for @p bytes.
 * @param ctx   The ota_ctx_t.
 * @param bytes The image's size.
 * @return False when the slot is missing, too small or will not open.
 */
static bool ota_begin(void *ctx, uint32_t bytes)
{
    ota_ctx_t *c = ctx;
    c->part = esp_ota_get_next_update_partition(NULL);
    if (c->part == NULL || bytes > c->part->size) {
        return false;
    }
    /* Sequential: each sector is erased as the write reaches it, so the
     * first erase is the first write's. */
    const esp_err_t e = esp_ota_begin(c->part, OTA_WITH_SEQUENTIAL_WRITES,
                                      &c->h);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin: %s", esp_err_to_name(e));
        return false;
    }
    c->open = true;
    c->next = 0u;
    return true;
}

/**
 * @brief Write the next piece, at the offset the last one ended.
 * @param ctx The ota_ctx_t.
 * @param off The piece's offset in the image.
 * @param src The bytes.
 * @param len Bytes at @p src.
 * @return False when the write is not open, out of order or fails.
 */
static bool ota_write(void *ctx, uint32_t off, const uint8_t *src,
                      uint32_t len)
{
    ota_ctx_t *c = ctx;
    if (!c->open || off != c->next) {
        return false;
    }
    const esp_err_t e = esp_ota_write(c->h, src, len);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_write at %lu: %s", (unsigned long)off,
                 esp_err_to_name(e));
        return false;
    }
    c->next += len;
    return true;
}

/**
 * @brief Check the written slot and name it in otadata.
 * @param ctx The ota_ctx_t.
 * @return False when the check or the otadata write fails.
 */
static bool ota_finish(void *ctx)
{
    ota_ctx_t *c = ctx;
    if (!c->open) {
        return false;
    }
    /* esp_ota_end() reads the slot back and checks the image and its
     * appended SHA-256; only then does otadata name it. */
    c->open = false;
    esp_err_t e = esp_ota_end(c->h);
    if (e == ESP_OK) {
        e = esp_ota_set_boot_partition(c->part);
    }
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "finish: %s", esp_err_to_name(e));
        return false;
    }
    return true;
}

/**
 * @brief Drop a write under way.
 * @param ctx The ota_ctx_t.
 */
static void ota_abort(void *ctx)
{
    ota_ctx_t *c = ctx;
    if (c->open) {
        (void)esp_ota_abort(c->h);
        c->open = false;
    }
}

/** @brief The writer fw_install_begin() drives. */
static const fw_flash_t k_flash = {
    ota_begin, ota_write, ota_finish, ota_abort, &s_ctx,
};

const fw_flash_t *fw_ota_flash(void) { return &k_flash; }

/* ------------------------------------------------------- the first start */

/** @brief A note that holds a version: "RCUP". */
#define NOTE_MAGIC 0x52435550u

/** @brief The note fw_ota_note_restart() leaves for the next start. */
typedef struct {
    uint32_t magic;
    char     version[FW_TEXT_MAX];
    uint32_t check;
} restart_note_t;

/** @brief Kept across esp_restart() and a panic; a power cut clears it. */
static RTC_NOINIT_ATTR restart_note_t s_note;

/**
 * @brief FNV-1a (Fowler-Noll-Vo) over the note's version text.
 * @param n The note.
 * @return The hash.
 */
static uint32_t note_check(const restart_note_t *n)
{
    uint32_t h = 2166136261u;
    const uint8_t *p = (const uint8_t *)n->version;
    for (size_t i = 0; i < sizeof(n->version); ++i) {
        h = (h ^ p[i]) * 16777619u;
    }
    return h;
}

void fw_ota_note_restart(const char *version)
{
    memset(&s_note, 0, sizeof(s_note));
    snprintf(s_note.version, sizeof(s_note.version), "%s", version);
    s_note.check = note_check(&s_note);
    s_note.magic = NOTE_MAGIC;
}

/**
 * @brief Whether the running build is pending verify.
 * @param ctx Unused.
 * @return True on a trial start.
 */
static bool boot_pending(void *ctx)
{
    (void)ctx;
    const esp_partition_t *run = esp_ota_get_running_partition();
    esp_ota_img_states_t st = ESP_OTA_IMG_UNDEFINED;
    return run != NULL && esp_ota_get_state_partition(run, &st) == ESP_OK
           && st == ESP_OTA_IMG_PENDING_VERIFY;
}

/**
 * @brief Mark the running build invalid and restart into the one before.
 * @param ctx Unused.
 */
static void boot_mark_invalid(void *ctx)
{
    (void)ctx;
    ESP_LOGE(TAG, "display or touch did not come up: back to the build "
                  "before this one");
    const esp_err_t e = esp_ota_mark_app_invalid_rollback_and_reboot();
    ESP_LOGE(TAG, "marking this build invalid: %s; restarting unconfirmed",
             esp_err_to_name(e));
}

/**
 * @brief Keep the running build: otadata says valid.
 * @param ctx Unused.
 * @return False when otadata could not be written.
 */
static bool boot_mark_valid(void *ctx)
{
    (void)ctx;
    const esp_err_t e = esp_ota_mark_app_valid_cancel_rollback();
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "keeping this build: %s; the next start goes back",
                 esp_err_to_name(e));
        return false;
    }
    return true;
}

/**
 * @brief Restart.
 * @param ctx Unused.
 */
static void boot_restart(void *ctx)
{
    (void)ctx;
    esp_restart();
}

/**
 * @brief Whether a slot is marked invalid by a start that went back.
 * @param ctx Unused.
 * @return True when one is.
 */
static bool boot_last_invalid(void *ctx)
{
    (void)ctx;
    return esp_ota_get_last_invalid_partition() != NULL;
}

bool fw_ota_trial_pending(void)
{
    return boot_pending(NULL);
}

/** @brief The calls fw_boot_decide() makes. */
static const fw_boot_ops_t k_boot = {
    boot_pending, boot_mark_invalid, boot_mark_valid, boot_restart,
    boot_last_invalid, NULL,
};

fw_boot_t fw_ota_boot_check(bool ui_up, char *other, uint32_t other_cap)
{
    if (other_cap > 0u) {
        other[0] = '\0';
    }
    const bool noted = (s_note.magic == NOTE_MAGIC
                        && s_note.check == note_check(&s_note));
    char noted_ver[FW_TEXT_MAX];
    snprintf(noted_ver, sizeof(noted_ver), "%s", noted ? s_note.version : "");
    /*
     * The note stays through the decision: a trial that goes back restarts
     * inside it, and the build before reads the note to say so.
     */
    const fw_boot_t r = fw_boot_decide(&k_boot, ui_up, noted);
    s_note.magic = 0u;
    if (r == FW_BOOT_ROLLED_BACK && other_cap > 0u) {
        snprintf(other, other_cap, "%s", noted_ver);
    }
    return r;
}
