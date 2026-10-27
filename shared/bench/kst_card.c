/**
 * @file
 * @brief The card's side of the servo programmer's files.
 *
 * See kst_card.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "kst_card.h"

#include <string.h>

#include "kst_file.h"
#include "kst_limits.h"

_Static_assert(KST_PROG_NAME_MAX == KST_STORE_BASE_MAX + 1,
               "a name of kst_prog.h holds a base name of the store");
_Static_assert(KST_STORE_PRESET_MAX <= KST_PROG_NAME_MAX,
               "a preset name fits a name of kst_prog.h");

/* One request at a time: the text of a file, what it reads as, what it
 * comes to on the servo, and the directory. */
static char s_text[KST_FILE_MAX];
static kst_file_t s_file;
static kst_file_load_t s_load;
static kst_store_entry_t s_list[KST_CARD_LIST_MAX];

/**
 * @brief The servo type as the number the screen prints.
 *
 * @param type The servo type.
 * @return 6 or 8; 0 for any other type.
 */
static uint8_t type_no(kst_type_t type)
{
    return type == KST_TYPE_V6 ? 6u : type == KST_TYPE_V8 ? 8u : 0u;
}

unsigned kst_card_type(const kst_image_t *img)
{
    return type_no(kst_type(img));
}

/**
 * @brief The type the screen numbers 6 or 8 as kst_file.h names it.
 *
 * @param no 6, 8, or any other value for none.
 * @return KST_TYPE_V6, KST_TYPE_V8, or KST_TYPE_NONE.
 */
static kst_type_t type_of_no(unsigned no)
{
    return no == 6u ? KST_TYPE_V6 : no == 8u ? KST_TYPE_V8 : KST_TYPE_NONE;
}

/**
 * @brief A result of the store as kst_prog.h names it.
 *
 * @param r    The store result.
 * @param save true for a save, false for a list or a load.
 * @return The card result, as kst_prog_card_result_t.
 */
static uint8_t store_result(kst_store_t r, bool save)
{
    switch (r) {
    case KST_STORE_OK:
        return KST_PROG_CARD_OK;
    case KST_STORE_NO_CARD:
        return KST_PROG_CARD_NO_CARD;
    case KST_STORE_NO_NUMBER:
    case KST_STORE_CARD_FULL:
        return KST_PROG_CARD_FULL;
    case KST_STORE_BAD_NAME:
        return KST_PROG_CARD_NAME;
    case KST_STORE_EXISTS:
        return KST_PROG_CARD_EXISTS;
    case KST_STORE_TOO_LARGE:
        return KST_PROG_CARD_BAD;       /* larger than any servo file */
    case KST_STORE_DENIED:
        return KST_PROG_CARD_DENIED;
    default:
        return save ? KST_PROG_CARD_WRITE_ERR : KST_PROG_CARD_READ_ERR;
    }
}

/**
 * @brief The base name of a file name the store lists.
 *
 * @param file The file name the store lists.
 * @param out  Receives the base name, at most KST_PROG_NAME_MAX - 1 characters.
 */
static void base_of(const char *file, char *out)
{
    const size_t len = strlen(file);
    const size_t ext = strlen(KST_STORE_EXT);
    const size_t n = len > ext ? len - ext : 0u;

    memcpy(out, file, n < KST_PROG_NAME_MAX ? n : KST_PROG_NAME_MAX - 1u);
    out[n < KST_PROG_NAME_MAX ? n : KST_PROG_NAME_MAX - 1u] = '\0';
}

/**
 * @brief Carries out a save request.
 *
 * @param io   The card store.
 * @param req  The request.
 * @param done Receives the answer.
 */
static void save(const kst_store_io_t *io, const kst_prog_card_req_t *req,
                 kst_prog_card_done_t *done)
{
    char base[KST_PROG_NAME_MAX];
    kst_store_t r = KST_STORE_OK;

    memcpy(base, req->name, sizeof(base));
    base[sizeof(base) - 1u] = '\0';
    if (req->preset) {
        r = kst_store_preset(io, base, sizeof(base));
    }
    if (r == KST_STORE_OK) {
        const size_t len = kst_file_format(&req->image, type_of_no(req->type),
                                           s_text, sizeof(s_text));
        r = len != 0u ? kst_store_save(io, base, s_text, len, NULL)
                      : KST_STORE_WRITE_ERROR;
    }
    done->result = store_result(r, true);
    if (r == KST_STORE_OK) {
        memcpy(done->name, base, sizeof(done->name));
    }
}

/**
 * @brief The servo type the file @p name states; 0 for none and for a file that
 *        does not read as a servo file.
 *
 * @param io   The card store.
 * @param name The file name.
 * @return The type number, or 0 for none or for a file that is not a servo
 *         file.
 */
static uint8_t type_of(const kst_store_io_t *io, const char *name)
{
    size_t len = 0;

    if (kst_store_load(io, name, s_text, sizeof(s_text), &len) != KST_STORE_OK
        || kst_file_parse(s_text, len, &s_file, NULL) != KST_FILE_OK) {
        return 0u;
    }
    return type_no(s_file.type);
}

/**
 * @brief Carries out a list request.
 *
 * @param io   The card store.
 * @param req  The request.
 * @param done Receives the answer.
 */
static void list(const kst_store_io_t *io, const kst_prog_card_req_t *req,
                 kst_prog_card_done_t *done)
{
    char behind[KST_STORE_NAME_MAX];
    size_t first = req->first;
    size_t count = 0;
    size_t total = 0;
    kst_store_t r = kst_store_list(io, s_list, KST_CARD_LIST_MAX, &count,
                                   &total, NULL);

    /* A full buffer that does not hold the whole page: the directory has
     * more files.  Go on behind the last file in front of the page, or
     * behind the buffer's last one. */
    while (r == KST_STORE_OK && count == KST_CARD_LIST_MAX &&
           first + KST_PROG_FILES_MAX > count) {
        const size_t skip = first < count ? first : count;

        memcpy(behind, s_list[skip - 1u].name, sizeof(behind));
        first -= skip;
        r = kst_store_list_after(io, behind, s_list, KST_CARD_LIST_MAX,
                                 &count, NULL, NULL);
    }
    done->result = store_result(r, false);
    if (r != KST_STORE_OK) {
        return;
    }
    done->total = total < 0xFFFFu ? (uint16_t)total : 0xFFFFu;
    for (size_t i = first;
         i < count && done->count < KST_PROG_FILES_MAX; ++i) {
        base_of(s_list[i].name, done->names[done->count]);
        done->types[done->count] = type_of(io, s_list[i].name);
        done->count++;
    }
    if (kst_store_preset(io, done->name, sizeof(done->name))
        != KST_STORE_OK) {
        done->name[0] = '\0';
    }
}

/**
 * @brief The lowest rule in a set of rules.
 *
 * @param set The set of rules, one bit each.
 * @return The index of the lowest rule set, or KST_RULE_COUNT for none.
 */
static unsigned lowest(kst_rules_t set)
{
    unsigned n = 0;

    while (n < (unsigned)KST_RULE_COUNT && ((set >> n) & 1u) == 0u) {
        n++;
    }
    return n;
}

/**
 * @brief Carries out a load request.
 *
 * @param io   The card store.
 * @param req  The request.
 * @param done Receives the answer.
 */
static void load(const kst_store_io_t *io, const kst_prog_card_req_t *req,
                 kst_prog_card_done_t *done)
{
    char name[KST_STORE_NAME_MAX];
    size_t len = 0;
    unsigned line = 0;
    kst_store_t r = KST_STORE_BAD_NAME;
    kst_file_err_t e;

    if (kst_store_file_name(req->name, name, sizeof(name))) {
        r = kst_store_load(io, name, s_text, sizeof(s_text), &len);
    }
    if (r != KST_STORE_OK) {
        /* No file of that name any more reads as a card that does not. */
        done->result = r == KST_STORE_BAD_NAME ? KST_PROG_CARD_READ_ERR
                                               : store_result(r, false);
        return;
    }
    e = kst_file_parse(s_text, len, &s_file, &line);
    if (e != KST_FILE_OK) {
        done->result = e == KST_FILE_ERR_CHECKSUM ? KST_PROG_CARD_CHECKSUM
                     : e == KST_FILE_ERR_NEWER    ? KST_PROG_CARD_NEWER
                                                  : KST_PROG_CARD_BAD;
        done->line = (uint16_t)line;
        return;
    }
    done->kind = s_file.kind == KST_FILE_VERIFIED ? KST_PROG_FILE_VERIFIED
               : s_file.kind == KST_FILE_EDITED_TYPED ? KST_PROG_FILE_EDITED
                                                      : KST_PROG_FILE_UNTYPED;
    done->type = type_no(s_file.type);
    switch (kst_file_load(&s_file, &req->image, &req->image,
                          type_of_no(req->type), &s_load)) {
    case KST_FILE_LOAD_OK:
        done->image = s_load.target;
        done->skipped = s_load.skipped;
        done->named = s_load.named;
        done->fields = s_load.fields;
        break;
    case KST_FILE_LOAD_TYPE_MISMATCH:
        done->result = KST_PROG_CARD_TYPE;
        done->servo_type = type_no(s_load.servo_type);
        break;
    case KST_FILE_LOAD_HARD_LIMIT:
        if (s_load.hard_line != 0u && s_load.hard_field < KST_F_COUNT) {
            done->result = KST_PROG_CARD_VALUE;
            done->field = (uint8_t)s_load.hard_field;
            done->line = (uint16_t)s_load.hard_line;
        } else {
            done->result = KST_PROG_CARD_LIMIT;
            done->rule = (uint8_t)lowest(s_load.rules & KST_RULES_HARD);
        }
        break;
    default:
        done->result = KST_PROG_CARD_BAD;
        break;
    }
}

void kst_card_run(const kst_store_io_t *io, const kst_prog_card_req_t *req,
                  kst_prog_card_done_t *done)
{
    memset(done, 0, sizeof(*done));
    done->id = req->id;
    done->field = 0xFFu;
    switch (req->op) {
    case KST_PROG_CARD_SAVE:
        save(io, req, done);
        break;
    case KST_PROG_CARD_LIST:
        list(io, req, done);
        break;
    case KST_PROG_CARD_LOAD:
        load(io, req, done);
        break;
    default:
        done->result = KST_PROG_CARD_READ_ERR;
        break;
    }
}
