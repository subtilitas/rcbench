/*
 * SPDX-License-Identifier: MIT
 */

#include "kst_store.h"

#include <string.h>

#define PRESET      "KST"
#define PRESET_LEN  3u
#define EXT_LEN     (sizeof(KST_STORE_EXT) - 1u)
#define NUMBER_BYTES ((KST_STORE_LAST / 8) + 1)

static char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

/* Order of two names without case; the case decides between names that
 * are equal without it. */
static int name_cmp(const char *a, const char *b)
{
    size_t i = 0;

    while (a[i] != '\0' && lower(a[i]) == lower(b[i])) {
        i++;
    }
    if (lower(a[i]) != lower(b[i])) {
        return (unsigned char)lower(a[i]) < (unsigned char)lower(b[i]) ? -1 : 1;
    }
    return strcmp(a, b);
}

static bool same_name(const char *a, const char *b)
{
    size_t i = 0;

    while (a[i] != '\0' && lower(a[i]) == lower(b[i])) {
        i++;
    }
    return a[i] == b[i];
}

/* Whether the first @p n characters of @p s are a device name. */
static bool reserved(const char *s, size_t n)
{
    static const char *const fixed[] = {"con", "prn", "aux", "nul"};
    static const char *const numbered[] = {"com", "lpt"};
    char word[4];

    if (n != 3u && n != 4u) {
        return false;
    }
    for (size_t i = 0; i < 3u; ++i) {
        word[i] = lower(s[i]);
    }
    word[3] = '\0';
    for (size_t i = 0; i < 4u; ++i) {
        if (n == 3u && strcmp(word, fixed[i]) == 0) {
            return true;
        }
    }
    for (size_t i = 0; i < 2u; ++i) {
        if (n == 4u && s[3] >= '1' && s[3] <= '9' &&
            strcmp(word, numbered[i]) == 0) {
            return true;
        }
    }
    return false;
}

/* Whether the first @p n characters of @p s are KST_STORE_RECORD. */
static bool record(const char *s, size_t n)
{
    return n == PRESET_LEN && lower(s[0]) == 'k' && lower(s[1]) == 's' &&
           lower(s[2]) == 't';
}

static kst_store_name_t check(const char *base, size_t n)
{
    size_t stem = 0;

    if (n == 0u) {
        return KST_STORE_NAME_EMPTY;
    }
    if (n > KST_STORE_BASE_MAX) {
        return KST_STORE_NAME_TOO_LONG;
    }
    for (size_t i = 0; i < n; ++i) {
        const unsigned char c = (unsigned char)base[i];

        if (c < 0x20u || c > 0x7Eu || strchr("\"*/:<>?\\|", c) != NULL) {
            return KST_STORE_NAME_BAD_CHAR;
        }
    }
    if (base[0] == '.' || base[0] == ' ' || base[n - 1u] == '.' ||
        base[n - 1u] == ' ') {
        return KST_STORE_NAME_EDGE;
    }
    while (stem < n && base[stem] != '.') {
        stem++;
    }
    return reserved(base, stem) || record(base, n) ? KST_STORE_NAME_RESERVED
                                                   : KST_STORE_NAME_OK;
}

kst_store_name_t kst_store_name_check(const char *base)
{
    return base == NULL ? KST_STORE_NAME_EMPTY : check(base, strlen(base));
}

bool kst_store_file_name(const char *base, char *out, size_t out_size)
{
    size_t n;

    if (out == NULL || out_size == 0u) {
        return false;
    }
    out[0] = '\0';
    if (out_size < KST_STORE_NAME_MAX ||
        kst_store_name_check(base) != KST_STORE_NAME_OK) {
        return false;
    }
    n = strlen(base);
    memcpy(out, base, n);
    memcpy(out + n, KST_STORE_EXT, EXT_LEN + 1u);
    return true;
}

/* Length of the base name in a file name with the extension; 0 when the
 * name does not end with it. */
static size_t base_len(const char *name)
{
    const size_t n = strlen(name);

    if (n <= EXT_LEN || !same_name(name + n - EXT_LEN, KST_STORE_EXT)) {
        return 0;
    }
    return n - EXT_LEN;
}

/* The number in "KSTnnn.TXT", in any case; 0 for any other name. */
static int preset_number(const char *name)
{
    int number = 0;

    if (base_len(name) != PRESET_LEN + 3u ||
        lower(name[0]) != 'k' || lower(name[1]) != 's' ||
        lower(name[2]) != 't') {
        return 0;
    }
    for (size_t i = PRESET_LEN; i < PRESET_LEN + 3u; ++i) {
        if (name[i] < '0' || name[i] > '9') {
            return 0;
        }
        number = number * 10 + (name[i] - '0');
    }
    return number;
}

static void mark_number(const char *name, bool is_dir, void *visit_ctx)
{
    uint8_t *taken = visit_ctx;
    const int number = preset_number(name);

    (void)is_dir;
    taken[number / 8] |= (uint8_t)(1u << (number % 8));
}

kst_store_t kst_store_preset(const kst_store_io_t *io, char *base,
                             size_t base_size)
{
    uint8_t taken[NUMBER_BYTES];
    int highest = 0;
    int number = 0;

    if (io == NULL || base == NULL || base_size < KST_STORE_PRESET_MAX) {
        return KST_STORE_ARG;
    }
    base[0] = '\0';
    if (!io->mounted(io->ctx)) {
        return KST_STORE_NO_CARD;
    }
    memset(taken, 0, sizeof(taken));
    if (!io->walk(io->ctx, mark_number, taken)) {
        return KST_STORE_LIST_ERROR;
    }
    for (int n = KST_STORE_FIRST; n <= KST_STORE_LAST; ++n) {
        if ((taken[n / 8] & (1u << (n % 8))) != 0u) {
            highest = n;
        } else if (number == 0) {
            number = n;
        }
    }
    if (highest < KST_STORE_LAST) {
        number = highest + 1;
    }
    if (number == 0) {
        return KST_STORE_NO_NUMBER;
    }
    memcpy(base, PRESET, PRESET_LEN);
    base[3] = (char)('0' + number / 100);
    base[4] = (char)('0' + number / 10 % 10);
    base[5] = (char)('0' + number % 10);
    base[6] = '\0';
    return KST_STORE_OK;
}

typedef struct {
    const char *name;
    bool found;
} find_t;

static void find_name(const char *name, bool is_dir, void *visit_ctx)
{
    find_t *find = visit_ctx;

    (void)is_dir;
    if (same_name(name, find->name)) {
        find->found = true;
    }
}

kst_store_t kst_store_save(const kst_store_io_t *io, const char *base,
                           const char *text, size_t len,
                           kst_store_name_t *why)
{
    char name[KST_STORE_NAME_MAX];
    find_t find = {name, false};

    if (why != NULL) {
        *why = KST_STORE_NAME_OK;
    }
    if (io == NULL || text == NULL) {
        return KST_STORE_ARG;
    }
    if (!kst_store_file_name(base, name, sizeof(name))) {
        if (why != NULL) {
            *why = kst_store_name_check(base);
        }
        return KST_STORE_BAD_NAME;
    }
    if (!io->mounted(io->ctx)) {
        return KST_STORE_NO_CARD;
    }
    if (!io->walk(io->ctx, find_name, &find)) {
        return KST_STORE_LIST_ERROR;
    }
    if (find.found) {
        return KST_STORE_EXISTS;
    }
    switch (io->create(io->ctx, name, text, len)) {
    case KST_STORE_IO_OK:
        return KST_STORE_OK;
    case KST_STORE_IO_EXISTS:
        return KST_STORE_EXISTS;
    case KST_STORE_IO_FULL:
        return KST_STORE_CARD_FULL;
    case KST_STORE_IO_DENIED:
        return KST_STORE_DENIED;
    default:
        return KST_STORE_WRITE_ERROR;
    }
}

/* Whether a name that is no proposed one stands in front of the proposed
 * names: at or before "KST000.TXT" in the order without case. */
static bool before_presets(const char *name)
{
    static const char first[] = "kst000" ".txt";
    size_t i = 0;

    while (name[i] != '\0' && lower(name[i]) == first[i]) {
        i++;
    }
    return (unsigned char)lower(name[i]) <= (unsigned char)first[i];
}

/* Order of two file names in a list: by name without case, with the
 * proposed names in one run where "KST000.TXT" would stand, the highest
 * number first. */
static int list_cmp(const char *a, const char *b)
{
    const int na = preset_number(a);
    const int nb = preset_number(b);

    if (na != 0 && nb != 0 && na != nb) {
        return na > nb ? -1 : 1;
    }
    if (na != 0 && nb == 0) {
        return before_presets(b) ? 1 : -1;
    }
    if (na == 0 && nb != 0) {
        return before_presets(a) ? -1 : 1;
    }
    return name_cmp(a, b);
}

typedef struct {
    kst_store_entry_t *entries;
    const char *after;      /* NULL: from the first file on */
    size_t cap;
    size_t count;
    size_t total;
    size_t skipped;
} list_t;

static void list_name(const char *name, bool is_dir, void *visit_ctx)
{
    list_t *list = visit_ctx;
    const size_t base = base_len(name);
    size_t at;

    /* The record of a write under way is no servo file and no stray one. */
    if (is_dir || base == 0u || record(name, base)) {
        return;
    }
    if (check(name, base) != KST_STORE_NAME_OK) {
        list->skipped++;
        return;
    }
    list->total++;
    if (list->after != NULL && list_cmp(name, list->after) <= 0) {
        return;
    }
    at = list->count;
    while (at > 0u && list_cmp(name, list->entries[at - 1u].name) < 0) {
        at--;
    }
    if (at == list->cap) {
        return;
    }
    if (list->count < list->cap) {
        list->count++;
    }
    memmove(&list->entries[at + 1u], &list->entries[at],
            (list->count - at - 1u) * sizeof(list->entries[0]));
    /* A checked base name and the extension fit with the terminator. */
    memcpy(list->entries[at].name, name, base + EXT_LEN + 1u);
}

kst_store_t kst_store_list(const kst_store_io_t *io,
                           kst_store_entry_t *entries, size_t cap,
                           size_t *count, size_t *total, size_t *skipped)
{
    return kst_store_list_after(io, NULL, entries, cap, count, total,
                                skipped);
}

kst_store_t kst_store_list_after(const kst_store_io_t *io, const char *after,
                                 kst_store_entry_t *entries, size_t cap,
                                 size_t *count, size_t *total,
                                 size_t *skipped)
{
    list_t list = {entries, after, cap, 0, 0, 0};
    bool read_through;

    if (io == NULL || (entries == NULL && cap > 0u)) {
        return KST_STORE_ARG;
    }
    if (!io->mounted(io->ctx)) {
        return KST_STORE_NO_CARD;
    }
    read_through = io->walk(io->ctx, list_name, &list);
    if (count != NULL) {
        *count = list.count;
    }
    if (total != NULL) {
        *total = list.total;
    }
    if (skipped != NULL) {
        *skipped = list.skipped;
    }
    return read_through ? KST_STORE_OK : KST_STORE_LIST_ERROR;
}

kst_store_t kst_store_load(const kst_store_io_t *io, const char *name,
                           char *buf, size_t cap, size_t *len)
{
    int32_t size;

    if (io == NULL || name == NULL || buf == NULL || len == NULL) {
        return KST_STORE_ARG;
    }
    *len = 0;
    if (check(name, base_len(name)) != KST_STORE_NAME_OK) {
        return KST_STORE_BAD_NAME;
    }
    if (!io->mounted(io->ctx)) {
        return KST_STORE_NO_CARD;
    }
    size = io->size(io->ctx, name);
    if (size < 0) {
        return KST_STORE_NOT_FOUND;
    }
    if ((size_t)size > cap) {
        return KST_STORE_TOO_LARGE;
    }
    if (io->read(io->ctx, name, buf, cap) != size) {
        return KST_STORE_READ_ERROR;
    }
    *len = (size_t)size;
    return KST_STORE_OK;
}
