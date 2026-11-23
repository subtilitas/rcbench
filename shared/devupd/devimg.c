/**
 * @file devimg.c
 * @brief The device images fetched on demand (devimg.h).
 *
 * SPDX-License-Identifier: MIT
 */
#include "devimg.h"

#include <stdio.h>
#include <string.h>

#include "bleota.h"
#include "devupd.h"
#include "fw_c6.h"
#include "rcnode.h"

/** @brief One image's fixed facts. */
typedef struct {
    const char *stem;    /**< the file name before the version */
    uint32_t    slot;    /**< the device's slot, bytes */
    uint16_t    chip;    /**< the ESP chip ID it must name */
    const char *project; /**< the project it must name */
    uint8_t     kind;    /**< BLEOTA_KIND_*, 0 for the C6 */
} devimg_fact_t;

/** @brief The images' facts, by devimg_id_t. */
static const devimg_fact_t k_fact[DEVIMG_COUNT] = {
    { "rcbench-c6-", FW_C6_SLOT_BYTES, FW_CHIP_ESP32C6, FW_C6_PROJECT, 0u },
    { "rcbench-dial-", DEVUPD_SLOT_DIAL, DEVUPD_CHIP_ESP32S3,
      DEVUPD_PROJECT_DIAL, BLEOTA_KIND_DIAL },
    { "rcbench-node-pwr-", DEVUPD_SLOT_NODE, FW_CHIP_ESP32C6,
      DEVUPD_PROJECT_NODE, BLEOTA_KIND_NODE_POWER },
    { "rcbench-node-bal-", DEVUPD_SLOT_NODE, FW_CHIP_ESP32C6,
      DEVUPD_PROJECT_NODE, BLEOTA_KIND_NODE_BAL },
    { "rcbench-node-display-", DEVUPD_SLOT_NODE, DEVUPD_CHIP_ESP32S3,
      DEVUPD_PROJECT_NODE, BLEOTA_KIND_NODE_POWER_S3 },
};

_Static_assert(DEVIMG_DIAL == BLEOTA_KIND_DIAL
                   && DEVIMG_PNODE == BLEOTA_KIND_NODE_POWER
                   && DEVIMG_BAL == BLEOTA_KIND_NODE_BAL
                   && DEVIMG_PNODE_S3 == BLEOTA_KIND_NODE_POWER_S3
                   && DEVIMG_COUNT == BLEOTA_KIND_MAX + 1,
               "a wireless device's image number is its BLEOTA_KIND_*");

devimg_id_t devimg_pnode_image(uint8_t board)
{
    switch (board) {
    case 0u:
    case RCNODE_BOARD_C6_ZERO:
        return DEVIMG_PNODE;
    case RCNODE_BOARD_ES3C28P:
        return DEVIMG_PNODE_S3;
    default:
        return DEVIMG_COUNT;
    }
}

uint8_t devimg_kind(devimg_id_t id)
{
    return (unsigned)id < DEVIMG_COUNT ? k_fact[id].kind : 0u;
}

devimg_err_t devimg_fits(devimg_id_t id, uint8_t board, const uint8_t *img,
                         uint32_t n)
{
    if ((unsigned)id >= DEVIMG_COUNT || board == 0u
        || devimg_pnode_image(board) != id) {
        return DEVIMG_E_ID;
    }
    const devimg_fact_t *f = &k_fact[id];
    if (img == NULL || n == 0u
        || bleota_head_check(img, n, f->chip, f->kind) != BLEOTA_OK) {
        return DEVIMG_E_ROLE;
    }
    return DEVIMG_OK;
}

bool devimg_name(devimg_id_t id, fw_version_t v, char *out, size_t n)
{
    if ((unsigned)id >= DEVIMG_COUNT || out == NULL || n == 0u) {
        return false;
    }
    const int k = snprintf(out, n, "%s%u.%u.%u.bin", k_fact[id].stem,
                           (unsigned)v.major, (unsigned)v.minor,
                           (unsigned)v.patch);
    return k > 0 && (size_t)k < n;
}

bool devimg_card_path(devimg_id_t id, fw_version_t v, char *out, size_t n)
{
    char name[DEVIMG_NAME_MAX];
    if (!devimg_name(id, v, name, sizeof(name)) || out == NULL) {
        return false;
    }
    const int k = snprintf(out, n, "%s/%s", DEVIMG_DIR, name);
    return k > 0 && (size_t)k < n;
}

uint32_t devimg_slot(devimg_id_t id)
{
    return (unsigned)id < DEVIMG_COUNT ? k_fact[id].slot : 0u;
}

devimg_err_t devimg_verify(devimg_id_t id, const uint8_t *img, uint32_t n,
                           fw_version_t want, const uint8_t *sha_want,
                           fw_image_info_t *info,
                           uint8_t sha[FW_SHA256_BYTES], fw_err_t *why)
{
    if ((unsigned)id >= DEVIMG_COUNT) {
        return DEVIMG_E_ID;
    }
    const devimg_fact_t *f = &k_fact[id];
    if (img == NULL || n == 0u || n > f->slot) {
        return DEVIMG_E_SIZE;
    }
    uint8_t got[FW_SHA256_BYTES];
    fw_sha256(img, n, got);
    if (sha != NULL) {
        memcpy(sha, got, sizeof(got));
    }
    if (sha_want != NULL && memcmp(got, sha_want, sizeof(got)) != 0) {
        return DEVIMG_E_HASH;
    }
    fw_image_info_t mine;
    fw_image_info_t *out = (info != NULL) ? info : &mine;
    const fw_err_t e = (id == DEVIMG_C6)
                           ? fw_c6_image_check(img, n, out)
                           : fw_image_check_as(img, n, f->slot, f->chip,
                                               f->project, out);
    if (why != NULL) {
        *why = e;
    }
    if (e != FW_OK) {
        return DEVIMG_E_CHECK;
    }
    if (f->kind != 0u
        && bleota_head_check(img, n, f->chip, f->kind) != BLEOTA_OK) {
        /* The two node roles share chip and project: the role in the
         * head's .rodata_custom_desc tells them apart. */
        return DEVIMG_E_ROLE;
    }
    if (fw_version_cmp(out->version, want) != 0) {
        return DEVIMG_E_VERSION;
    }
    return DEVIMG_OK;
}

void devimg_init(devimg_slot_t *s)
{
    memset(s, 0, sizeof(*s));
}

devimg_src_t devimg_src(const devimg_slot_t *s, bool have_net)
{
    if (s->st == DEVIMG_READY) {
        return DEVIMG_SRC_RAM;
    }
    return (!have_net || s->net_failed) ? DEVIMG_SRC_CARD : DEVIMG_SRC_NET;
}

bool devimg_busy(const devimg_slot_t *s)
{
    return s->st == DEVIMG_FETCH || s->st == DEVIMG_CARD
           || s->st == DEVIMG_VERIFY;
}

bool devimg_pending(const devimg_slot_t *s)
{
    return devimg_busy(s) || s->owed;
}

bool devimg_begin(devimg_slot_t *s, devimg_src_t src)
{
    if (devimg_busy(s) || s->st == DEVIMG_READY || src == DEVIMG_SRC_RAM) {
        return false;
    }
    s->st = (src == DEVIMG_SRC_CARD) ? DEVIMG_CARD : DEVIMG_FETCH;
    s->from_card = src == DEVIMG_SRC_CARD;
    s->owed = true;
    s->owed_ms = 0u;
    s->code[0] = '\0';
    return true;
}

void devimg_fetched(devimg_slot_t *s)
{
    if (s->st == DEVIMG_FETCH) {
        s->st = DEVIMG_VERIFY;
    }
}

void devimg_ready(devimg_slot_t *s, uint32_t now_ms)
{
    if (s->st == DEVIMG_CARD || s->st == DEVIMG_VERIFY) {
        s->st = DEVIMG_READY;
        s->owed_ms = now_ms;
        s->net_failed = false;
    }
}

void devimg_failed(devimg_slot_t *s, char kind, unsigned n)
{
    if (!devimg_busy(s)) {
        return;
    }
    s->st = DEVIMG_IDLE;
    s->owed = false;
    s->net_failed = !s->from_card;
    const unsigned k = (n > 99u) ? 99u : n;
    s->code[0] = kind;
    s->code[1] = (char)('0' + k / 10u);
    s->code[2] = (char)('0' + k % 10u);
    s->code[3] = '\0';
}

bool devimg_cancel(devimg_slot_t *s)
{
    const bool was = s->owed;
    s->owed = false;
    return was;
}

devimg_owed_t devimg_owed_step(devimg_slot_t *s, bool open, bool wait,
                               uint32_t now_ms)
{
    if (!s->owed) {
        return DEVIMG_OWED_NONE;
    }
    if (devimg_busy(s)) {
        return DEVIMG_OWED_WAIT;
    }
    if (s->st != DEVIMG_READY) {
        s->owed = false;
        return DEVIMG_OWED_NONE;
    }
    if (open) {
        s->owed = false;
        return DEVIMG_OWED_FIRE;
    }
    if (wait && (uint32_t)(now_ms - s->owed_ms) < DEVIMG_OWED_MS) {
        return DEVIMG_OWED_WAIT;
    }
    s->owed = false;
    return DEVIMG_OWED_DROP;
}

bool devimg_card_sig(bool present, const uint8_t *sig, uint32_t sig_n,
                     fw_sig_t *verdict)
{
    if (!present) {
        *verdict = FW_SIG_NONE;
        return false;
    }
    if (sig == NULL || sig_n < FW_SIG_MIN || sig_n > FW_SIG_MAX) {
        *verdict = FW_SIG_FORM;
        return false;
    }
    return true;
}

devimg_err_t devimg_sig_policy(fw_sig_t s, bool enforce)
{
    return (enforce && !fw_sig_signed(s)) ? DEVIMG_E_SIG : DEVIMG_OK;
}
