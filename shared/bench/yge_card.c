/**
 * @file
 * @brief The card's side of programming for YGE ESCs.  See yge_card.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "yge_card.h"

#include <stdio.h>
#include <string.h>

_Static_assert(SERVO_STORE_PRESET_MAX <= YGE_CARD_NAME_MAX,
               "a proposed name fits a name of yge_card.h");

/** @brief The length of the extension the store adds. */
#define EXT_LEN (sizeof(SERVO_STORE_EXT) - 1u)
/** @brief Names one walk holds: the list's pages behind it walk again. */
#define LIST_MAX 64u

/** @brief The names of one walk. */
static servo_store_entry_t s_list[LIST_MAX];

/**
 * @brief A result of the store as yge_card.h names it.
 * @param r    The store's result.
 * @param save true for a save.
 * @return A yge_card_result_t.
 */
static uint8_t store_result(servo_store_t r, bool save)
{
    switch (r) {
    case SERVO_STORE_OK:
        return YGE_CARD_DONE_OK;
    case SERVO_STORE_NO_CARD:
        return YGE_CARD_DONE_NO_CARD;
    case SERVO_STORE_NO_NUMBER:
    case SERVO_STORE_CARD_FULL:
        return YGE_CARD_DONE_FULL;
    case SERVO_STORE_DENIED:
        return YGE_CARD_DONE_DENIED;
    case SERVO_STORE_NOT_FOUND:
        return YGE_CARD_DONE_MISSING;
    case SERVO_STORE_TOO_LARGE:
        return YGE_CARD_DONE_LARGE;
    default:
        return save ? YGE_CARD_DONE_WRITE : YGE_CARD_DONE_READ;
    }
}

uint8_t yge_card_prog_result(uint8_t result)
{
    switch (result) {
    case YGE_CARD_DONE_OK:
        return YGE_PROG_CARD_OK;
    case YGE_CARD_DONE_NO_CARD:
        return YGE_PROG_CARD_NO_CARD;
    case YGE_CARD_DONE_FULL:
        return YGE_PROG_CARD_FULL;
    case YGE_CARD_DONE_READ:
    case YGE_CARD_DONE_MISSING:
        return YGE_PROG_CARD_UNREADABLE;
    default:
        return YGE_PROG_CARD_WRITE;
    }
}

/**
 * @brief The save: @c text to a new file under the proposed name, with the
 *        type code as its tag.
 * @param io   The store.
 * @param req  The request.
 * @param done The answer.
 */
static void save(const servo_store_io_t *io, const yge_card_req_t *req,
                 yge_card_done_t *done)
{
    servo_store_t r = servo_store_preset(io, done->name, sizeof(done->name));

    if (r == SERVO_STORE_OK && req->tag[0] != '\0') {
        const size_t n = strlen(done->name);

        (void)snprintf(done->name + n, sizeof(done->name) - n, "-%.*s",
                       (int)(YGE_CARD_TAG_MAX - 1u), req->tag);
    }
    if (r == SERVO_STORE_OK) {
        r = servo_store_save(io, done->name, req->text,
                             req->len < sizeof(req->text) ? req->len
                                                          : sizeof(req->text),
                             NULL);
    }
    done->result = store_result(r, true);
    if (r != SERVO_STORE_OK) {
        done->name[0] = '\0';
    }
}

/**
 * @brief The list: the names from @c first on, a walk per LIST_MAX names in
 *        front of them.
 * @param io   The store.
 * @param req  The request.
 * @param done The answer.
 */
static void list(const servo_store_io_t *io, const yge_card_req_t *req,
                 yge_card_done_t *done)
{
    char after[SERVO_STORE_NAME_MAX];
    const char *from = NULL;
    size_t skip = req->first;
    size_t count = 0u;
    size_t total = 0u;
    servo_store_t r;

    for (;;) {
        const size_t want = skip + YGE_CARD_FILES;
        const size_t cap = want < LIST_MAX ? want : LIST_MAX;

        r = servo_store_list_after(io, from, s_list, cap, &count, &total, NULL);
        if (r != SERVO_STORE_OK || want <= cap || count < cap) {
            break;
        }
        /* want is above cap: skip is 1 or more, and past with it. */
        const size_t past = skip < count ? skip : count;

        skip -= past;
        memcpy(after, s_list[past - 1u].name, sizeof(after));
        from = after;
    }
    done->result = store_result(r, false);
    if (r != SERVO_STORE_OK) {
        return;
    }
    done->total = total < 0xFFFFu ? (uint16_t)total : 0xFFFFu;
    for (size_t at = skip; at < count && done->count < YGE_CARD_FILES; ++at) {
        const size_t n = strlen(s_list[at].name);
        const size_t base = n > EXT_LEN ? n - EXT_LEN : 0u;

        memcpy(done->names[done->count], s_list[at].name, base);
        done->names[done->count][base] = '\0';
        done->count++;
    }
}

/**
 * @brief The load: the text of the file, terminated.
 * @param io   The store.
 * @param req  The request.
 * @param done The answer.
 */
static void load(const servo_store_io_t *io, const yge_card_req_t *req,
                 yge_card_done_t *done)
{
    char file[SERVO_STORE_NAME_MAX];
    char base[YGE_CARD_NAME_MAX];
    size_t len = 0u;

    memcpy(base, req->name, sizeof(base));
    base[sizeof(base) - 1u] = '\0';
    if (!servo_store_file_name(io->stem, base, file, sizeof(file))) {
        done->result = YGE_CARD_DONE_MISSING;
        return;
    }
    const servo_store_t r = servo_store_load(io, file, done->text, OY_FILE_MAX,
                                             &len);

    done->result = store_result(r, false);
    if (r == SERVO_STORE_OK) {
        done->text[len] = '\0';
        done->len = len;
    }
}

void yge_card_run(const servo_store_io_t *io, const yge_card_req_t *req,
                  yge_card_done_t *done)
{
    done->op = req->op;
    done->id = req->id;
    done->result = YGE_CARD_DONE_OK;
    done->count = 0u;
    done->total = 0u;
    done->len = 0u;
    done->name[0] = '\0';
    done->text[0] = '\0';
    switch (req->op) {
    case YGE_CARD_SAVE:
        save(io, req, done);
        break;
    case YGE_CARD_LIST:
        list(io, req, done);
        break;
    case YGE_CARD_LOAD:
        load(io, req, done);
        break;
    default:
        done->result = YGE_CARD_DONE_READ;
        break;
    }
}
