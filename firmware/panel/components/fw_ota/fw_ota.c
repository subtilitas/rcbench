/*
 * The application slots over esp_ota_ops.
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

typedef struct {
    const esp_partition_t *part;
    esp_ota_handle_t       h;
    bool                   open;
    uint32_t               next;    /* esp_ota_write() appends: the offset */
} ota_ctx_t;

static ota_ctx_t s_ctx;

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

static void ota_abort(void *ctx)
{
    ota_ctx_t *c = ctx;
    if (c->open) {
        (void)esp_ota_abort(c->h);
        c->open = false;
    }
}

static const fw_flash_t k_flash = {
    ota_begin, ota_write, ota_finish, ota_abort, &s_ctx,
};

const fw_flash_t *fw_ota_flash(void) { return &k_flash; }

/* ------------------------------------------------------- the first start */

#define NOTE_MAGIC 0x52435550u   /* "RCUP" */

typedef struct {
    uint32_t magic;
    char     version[FW_TEXT_MAX];
    uint32_t check;
} restart_note_t;

/* Kept across esp_restart() and a panic; a power cut clears it. */
static RTC_NOINIT_ATTR restart_note_t s_note;

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

fw_boot_t fw_ota_boot_check(bool ui_up, char *other, uint32_t other_cap)
{
    if (other_cap > 0u) {
        other[0] = '\0';
    }
    const bool noted = (s_note.magic == NOTE_MAGIC
                        && s_note.check == note_check(&s_note));
    char noted_ver[FW_TEXT_MAX];
    snprintf(noted_ver, sizeof(noted_ver), "%s", noted ? s_note.version : "");
    s_note.magic = 0u;

    const esp_partition_t *run = esp_ota_get_running_partition();
    esp_ota_img_states_t st = ESP_OTA_IMG_UNDEFINED;
    if (run != NULL && esp_ota_get_state_partition(run, &st) == ESP_OK
        && st == ESP_OTA_IMG_PENDING_VERIFY) {
        if (!ui_up) {
            ESP_LOGE(TAG, "display or touch did not come up: back to the "
                          "build before this one");
            (void)esp_ota_mark_app_invalid_rollback_and_reboot();
        }
        const esp_err_t e = esp_ota_mark_app_valid_cancel_rollback();
        if (e != ESP_OK) {
            ESP_LOGE(TAG, "keeping this build: %s", esp_err_to_name(e));
        }
        return FW_BOOT_UPDATED;
    }
    /*
     * A start after a written build that did not stay: the bootloader has
     * marked that slot invalid and started this one.  The slot stays marked
     * until the next update, so the note from before the restart is what
     * makes this report once.
     */
    if (noted && esp_ota_get_last_invalid_partition() != NULL) {
        if (other_cap > 0u) {
            snprintf(other, other_cap, "%s", noted_ver);
        }
        return FW_BOOT_ROLLED_BACK;
    }
    return FW_BOOT_NORMAL;
}
