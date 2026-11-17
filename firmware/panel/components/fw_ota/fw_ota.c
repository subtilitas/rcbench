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
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "nvs.h"

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
    const esp_partition_t *part;  /**< The slot that is not running. */
    esp_ota_handle_t       h;  /**< The handle esp_ota_begin() returned. */
    bool                   open;  /**< Whether the handle is open. */
    uint32_t               next;    /**< esp_ota_write() appends: the offset */
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

/*
 * The restart note and the run note live in NVS (non-volatile storage),
 * namespace NOTE_NS, keys "restart_note" (FW_NOTE_BYTES) and "run_note"
 * (FW_RUN_BYTES): blobs that a restart, a panic and a power cut keep, at
 * no address the build's layout decides.  settings_nvs_store() brings NVS
 * up in bring_up() before the start check runs; without it no note is
 * written and each reads as none.
 */

/** @brief The notes' NVS namespace. */
#define NOTE_NS  "fw_ota"

/** @brief One note's NVS key and the console words about it. */
typedef struct {
    const char *key;    /**< its NVS key, up to 15 characters        */
    const char *name;   /**< its name in a console line              */
    const char *lost;   /**< what a refused write costs              */
    const char *stale;  /**< what a refused erase costs              */
} note_key_t;

/** @brief The restart note (fw_boot.h). */
static note_key_t k_restart_key = {
    "restart_note", "restart note",
    "a start that goes back is not reported",
    "the next start may report it again",
};

/** @brief The run note (fw_run.h), beside it. */
static note_key_t k_run_key = {
    "run_note", "run note",
    "the next start finds no update run",
    "its openings are spent at the next starts",
};

/**
 * @brief Read a note's blob.
 * @param ctx The note_key_t.
 * @param buf Where it goes.
 * @param cap Bytes at @p buf.
 * @return Its bytes; 0 when there is none or NVS will not open.
 */
static uint32_t note_load(void *ctx, uint8_t *buf, uint32_t cap)
{
    const note_key_t *k = ctx;
    nvs_handle_t h;
    if (nvs_open(NOTE_NS, NVS_READONLY, &h) != ESP_OK) {
        return 0u;   /* no namespace yet: nothing was ever noted */
    }
    size_t len = 0u;
    esp_err_t e = nvs_get_blob(h, k->key, NULL, &len);
    if (e == ESP_OK && len > 0u && len <= cap) {
        e = nvs_get_blob(h, k->key, buf, &len);
    }
    nvs_close(h);
    if (e == ESP_ERR_NVS_NOT_FOUND) {
        return 0u;
    }
    if (e != ESP_OK) {
        /* There is an entry it cannot read: one that reads as no note. */
        ESP_LOGW(TAG, "%s unreadable: %s", k->name, esp_err_to_name(e));
        return UINT32_MAX;
    }
    return (uint32_t)len;
}

/**
 * @brief Write a note's blob and commit it.
 * @param ctx The note_key_t.
 * @param buf The entry.
 * @param len Bytes at @p buf.
 * @return False when NVS refused.
 */
static bool note_save(void *ctx, const uint8_t *buf, uint32_t len)
{
    const note_key_t *k = ctx;
    nvs_handle_t h;
    esp_err_t e = nvs_open(NOTE_NS, NVS_READWRITE, &h);
    if (e == ESP_OK) {
        e = nvs_set_blob(h, k->key, buf, len);
        if (e == ESP_OK) {
            e = nvs_commit(h);
        }
        nvs_close(h);
    }
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "%s not kept: %s; %s", k->name, esp_err_to_name(e),
                 k->lost);
        return false;
    }
    return true;
}

/**
 * @brief Erase a note's blob and commit.
 * @param ctx The note_key_t.
 * @return False when NVS refused.
 */
static bool note_erase(void *ctx)
{
    const note_key_t *k = ctx;
    nvs_handle_t h;
    esp_err_t e = nvs_open(NOTE_NS, NVS_READWRITE, &h);
    if (e == ESP_OK) {
        e = nvs_erase_key(h, k->key);
        if (e == ESP_OK || e == ESP_ERR_NVS_NOT_FOUND) {
            e = nvs_commit(h);
        }
        nvs_close(h);
    }
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "%s not erased: %s; %s", k->name, esp_err_to_name(e),
                 k->stale);
        return false;
    }
    return true;
}

/** @brief The store fw_note_write() and fw_boot_settle() use. */
static const fw_note_ops_t k_note = {
    note_load, note_save, note_erase, &k_restart_key,
};

/** @brief The store fw_run.h's calls use. */
static const fw_run_ops_t k_run_ops = {
    note_load, note_save, note_erase, &k_run_key,
};

/** @brief This start's view of the run; app_main's task only. */
static fw_run_t s_run;

void fw_ota_note_restart(const char *version)
{
    (void)fw_note_write(&k_note, version);
}

/* ---------------------------------------------------------- the run note */

/**
 * @brief The console line after a note was or was not written.
 * @param ok     Whether the run was noted.
 * @param expect The version it names.
 * @return @p ok.
 */
static bool said_begin(bool ok, const char *expect)
{
    if (ok) {
        ESP_LOGI(TAG, "run note: %s, %u openings", expect,
                 (unsigned)FW_RUN_OPENS);
    } else {
        ESP_LOGW(TAG, "run note: none for '%s'; the next start finds no "
                      "update run", expect != NULL ? expect : "");
    }
    return ok;
}

bool fw_ota_run_begin(const char *expect)
{
    return said_begin(fw_run_begin(&k_run_ops, &s_run, expect), expect);
}

bool fw_ota_run_installed(fw_install_status_t st, const char *version)
{
    const bool ok = fw_run_after_install(&k_run_ops, &s_run, st, version);
    /* An install that did not end is no run: nothing written, nothing
     * said. */
    return (st == FW_INSTALL_DONE) ? said_begin(ok, version) : ok;
}

/**
 * @brief Settle the run note once per start, with its console line.
 * @param running The running build's version.
 */
static void run_settle(const char *running)
{
    if (s_run.settled) {
        return;
    }
    switch (fw_run_settle(&k_run_ops, &s_run, running)) {
    case FW_RUN_STANDS:
        ESP_LOGI(TAG, "run note: %s, an update run stands, openings left "
                      "%u", s_run.expect, (unsigned)s_run.opens_left);
        break;
    case FW_RUN_FOREIGN:
        ESP_LOGW(TAG, "run note: unreadable, erased; no update run");
        break;
    case FW_RUN_MISMATCH:
        ESP_LOGW(TAG, "run note: %s expected, %s runs; the update run ends",
                 s_run.expect, running);
        break;
    case FW_RUN_UNSPENT:
        ESP_LOGW(TAG, "run note: %s, opening not spent; no update run",
                 s_run.expect);
        break;
    case FW_RUN_NONE:
    default:
        break;
    }
}

bool fw_ota_run_active(void)
{
    return s_run.active;
}

bool fw_ota_run_settled(void)
{
    return s_run.settled;
}

void fw_ota_run_end(void)
{
    const bool was = s_run.active;
    if (fw_run_end(&k_run_ops, &s_run) && was) {
        ESP_LOGI(TAG, "run note: erased; the update run ends");
    }
}

/** @brief This start's note, filled by fw_boot_settle() before any
 *         call of k_boot. */
static fw_note_seen_t s_seen;

/** @brief The slot the bootloader marked invalid, for the console line. */
static const char *s_invalid_label = "none";

/**
 * @brief The one console line of the start check.
 * @param result What the check found, by name.
 */
static void say(const char *result)
{
    const char *note = (s_seen.state == FW_NOTE_FOUND)     ? s_seen.version
                       : (s_seen.state == FW_NOTE_FOREIGN) ? "unreadable"
                                                           : "none";
    ESP_LOGI(TAG, "start check: note %s, last invalid slot %s, result %s",
             note, s_invalid_label, result);
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
    say("GOING_BACK");
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

/**
 * @brief The console name of a result.
 * @param r The result.
 * @return Its name.
 */
static const char *boot_name(fw_boot_t r)
{
    switch (r) {
    case FW_BOOT_UPDATED:     return "UPDATED";
    case FW_BOOT_ROLLED_BACK: return "ROLLED_BACK";
    case FW_BOOT_UNCONFIRMED: return "UNCONFIRMED";
    case FW_BOOT_NORMAL:
    default:                  return "NORMAL";
    }
}

fw_boot_t fw_ota_boot_check(bool ui_up, char *other, uint32_t other_cap)
{
    if (other_cap > 0u) {
        other[0] = '\0';
    }
    const esp_partition_t *bad = esp_ota_get_last_invalid_partition();
    s_invalid_label = (bad != NULL) ? bad->label : "none";
    /* A trial that goes back says its line in boot_mark_invalid() and
     * restarts there or in boot_restart(): it does not return here. */
    const fw_boot_t r = fw_boot_settle(&k_boot, &k_note, ui_up, &s_seen);
    say(boot_name(r));
    if (r == FW_BOOT_ROLLED_BACK && other_cap > 0u) {
        snprintf(other, other_cap, "%s", s_seen.version);
    }
    /* After the start is settled: a trial that goes back never gets here,
     * and the build before reads the note at its own start. */
    run_settle(esp_app_get_description()->version);
    return r;
}
