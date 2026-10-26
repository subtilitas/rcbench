/**
 * @file
 * @brief The card's side of the Hitec programmer's servo files.  See
 *        hitec_card_run.h.
 *
 * Hitec is a name or trademark of its owner; it is used here only to say
 * which servos this module works with.
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_card_run.h"

#include <string.h>

_Static_assert(HITEC_CARD_NAME_MAX == SERVO_STORE_BASE_MAX + 1,
               "a name of hitec_card.h holds a base name of the store");
_Static_assert(SERVO_STORE_PRESET_MAX <= HITEC_CARD_NAME_MAX,
               "a proposed name fits a name of hitec_card.h");

/** @brief The length of the extension the store adds. */
#define EXT_LEN (sizeof(SERVO_STORE_EXT) - 1u)

/* One request at a time: the text of a file and the folder's names. */
/** @brief A file's text, one byte past the longest so a larger one shows. */
static char s_text[HITEC_CARD_FILE_MAX + 1u];
/** @brief The names of one walk. */
static servo_store_entry_t s_list[HITEC_CARD_LIST_MAX];

/**
 * @brief A result of the store as hitec_card.h names it.
 *
 * @param r    The store's result.
 * @param save true for a save, false for a list or a load.
 * @return hitec_card_done_result_t.
 */
static uint8_t store_result(servo_store_t r, bool save)
{
    switch (r) {
    case SERVO_STORE_OK:
        return HITEC_CARD_DONE_OK;
    case SERVO_STORE_NO_CARD:
        return HITEC_CARD_DONE_NO_CARD;
    case SERVO_STORE_NO_NUMBER:
    case SERVO_STORE_CARD_FULL:
        return HITEC_CARD_DONE_FULL;
    case SERVO_STORE_BAD_NAME:
        return HITEC_CARD_DONE_NAME;
    case SERVO_STORE_EXISTS:
        return HITEC_CARD_DONE_EXISTS;
    case SERVO_STORE_DENIED:
        return HITEC_CARD_DONE_DENIED;
    default:
        return save ? HITEC_CARD_DONE_WRITE : HITEC_CARD_DONE_READ;
    }
}

_Static_assert(SERVO_STORE_NAME_MAX - 1u - EXT_LEN < HITEC_CARD_NAME_MAX,
               "the base name of a listed file fits a name of hitec_card.h");

/**
 * @brief The base name of a file name the store lists.
 *
 * @param file The file name, with the extension: at most
 *             SERVO_STORE_NAME_MAX - 1 characters.
 * @param out  Receives the base name, terminated.
 */
static void base_of(const char *file, char *out)
{
    size_t n = strlen(file);

    n = n > EXT_LEN ? n - EXT_LEN : 0u;
    memcpy(out, file, n);
    out[n] = '\0';
}

/**
 * @brief The save: the text of the image to a new file.
 *
 * @param io   The store.
 * @param req  The request.
 * @param done The answer.
 */
static void save(const servo_store_io_t *io, const hitec_card_req_t *req,
                 hitec_card_done_t *done)
{
    char base[HITEC_CARD_NAME_MAX];
    servo_store_t r = SERVO_STORE_OK;
    const size_t len = hitec_card_file_text(&req->image, s_text,
                                            sizeof(s_text));

    if (len == 0u) {
        done->result = HITEC_CARD_DONE_WRITE;
        return;
    }
    if (req->preset) {
        r = servo_store_preset(io, base, sizeof(base));
    } else {
        memcpy(base, req->name, sizeof(base));
        base[sizeof(base) - 1u] = '\0';
    }
    if (r == SERVO_STORE_OK) {
        r = servo_store_save(io, base, s_text, len, NULL);
    }
    done->result = store_result(r, true);
    if (r == SERVO_STORE_OK) {
        memcpy(done->name, base, sizeof(done->name));
    }
}

/**
 * @brief The model line of the file @p name; "" for one that does not read
 *        or parse.
 *
 * @param io   The store.
 * @param name The file name, with the extension.
 * @param out  Receives the model line's value.
 */
static void model_of(const servo_store_io_t *io, const char *name,
                     char out[HITEC_CARD_MODEL_MAX + 1u])
{
    hitec_image_t img;
    size_t len = 0;

    out[0] = '\0';
    if (servo_store_load(io, name, s_text, sizeof(s_text), &len)
            != SERVO_STORE_OK
        || hitec_card_file_parse(s_text, len, &img, out, NULL)
               != HITEC_CARD_OK) {
        out[0] = '\0';
    }
}

/**
 * @brief The list: the files from req->first on, a walk per
 *        HITEC_CARD_LIST_MAX files in front of them.
 *
 * @param io   The store.
 * @param req  The request.
 * @param done The answer.
 */
static void list(const servo_store_io_t *io, const hitec_card_req_t *req,
                 hitec_card_done_t *done)
{
    char after[SERVO_STORE_NAME_MAX];
    const char *from = NULL;
    size_t skip = req->first;
    size_t count = 0;
    size_t total = 0;
    size_t at = 0;
    servo_store_t r;

    for (;;) {
        const size_t want = skip + HITEC_CARD_FILES;
        const size_t cap = want < HITEC_CARD_LIST_MAX ? want
                                                      : HITEC_CARD_LIST_MAX;

        size_t past;

        r = servo_store_list_after(io, from, s_list, cap, &count, &total,
                                   NULL);
        /* The page is in this walk's names, or the folder ends in it. */
        if (r != SERVO_STORE_OK || want <= cap || count < cap) {
            break;
        }
        /* Behind HITEC_CARD_LIST_MAX names: the next walk starts after the
         * names in front of the page, up to a whole walk of them.  want
         * is above cap here, so skip is HITEC_CARD_LIST_MAX -
         * HITEC_CARD_FILES or more and past is 1 or more. */
        past = skip < count ? skip : count;
        skip -= past;
        memcpy(after, s_list[past - 1u].name, sizeof(after));
        from = after;
    }
    done->result = store_result(r, false);
    if (r != SERVO_STORE_OK) {
        return;
    }
    done->total = total < 0xFFFFu ? (uint16_t)total : 0xFFFFu;
    for (at = skip; at < count && done->count < HITEC_CARD_FILES; ++at) {
        base_of(s_list[at].name, done->names[done->count]);
        model_of(io, s_list[at].name, done->models[done->count]);
        done->count++;
    }
    if (servo_store_preset(io, done->name, sizeof(done->name))
        != SERVO_STORE_OK) {
        done->name[0] = '\0';
    }
}

/**
 * @brief The load: the file read and parsed.
 *
 * @param io   The store.
 * @param req  The request.
 * @param done The answer.
 */
static void load(const servo_store_io_t *io, const hitec_card_req_t *req,
                 hitec_card_done_t *done)
{
    char file[SERVO_STORE_NAME_MAX];
    char base[HITEC_CARD_NAME_MAX];
    unsigned line = 0;
    size_t len = 0;
    servo_store_t r;

    memcpy(base, req->name, sizeof(base));
    base[sizeof(base) - 1u] = '\0';
    if (!servo_store_file_name(io->stem, base, file, sizeof(file))) {
        done->result = HITEC_CARD_DONE_READ;
        return;
    }
    r = servo_store_load(io, file, s_text, HITEC_CARD_FILE_MAX, &len);
    if (r == SERVO_STORE_TOO_LARGE) {
        done->result = HITEC_CARD_DONE_BAD;
        done->parse = (uint8_t)HITEC_CARD_ERR_HEADER;
        return;
    }
    done->result = store_result(r, false);
    if (r != SERVO_STORE_OK) {
        return;
    }
    done->parse = (uint8_t)hitec_card_file_parse(s_text, len, &done->image,
                                                 done->model, &line);
    done->line = (uint16_t)line;
    if (done->parse != (uint8_t)HITEC_CARD_OK) {
        done->result = HITEC_CARD_DONE_BAD;
    }
}

void hitec_card_run(const servo_store_io_t *io, const hitec_card_req_t *req,
                    hitec_card_done_t *done)
{
    memset(done, 0, sizeof(*done));
    done->op = req->op;
    done->id = req->id;
    switch (req->op) {
    case HITEC_CARD_SAVE:
        save(io, req, done);
        break;
    case HITEC_CARD_LIST:
        list(io, req, done);
        break;
    case HITEC_CARD_LOAD:
        load(io, req, done);
        break;
    default:
        done->result = HITEC_CARD_DONE_READ;
        break;
    }
}
