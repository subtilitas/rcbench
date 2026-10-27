/**
 * @file
 * @brief Servo configuration files on the card for a programmer named by a
 *        stem.  See servo_store.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_store.h"

#include <string.h>

/** @brief Length of the file extension, characters. */
#define EXT_LEN     (sizeof(SERVO_STORE_EXT) - 1u)
/** @brief Bytes of a bit set that holds one flag per number 0 to
 *         SERVO_STORE_LAST. */
#define NUMBER_BYTES ((SERVO_STORE_LAST / 8) + 1)

/**
 * @brief Fold an ASCII capital letter to lower case.
 *
 * @param c  The character.
 * @return The lower-case letter for A to Z; every other character unchanged.
 */
static char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

/**
 * @brief Order of two names without case; the case decides between names
 *        that are equal without it.
 *
 * @param a  The first name.
 * @param b  The second name.
 * @return Negative, zero or positive as @p a sorts before, with or after
 *         @p b.
 */
static int name_cmp(const char *a, const char *b)
{
    size_t i = 0;

    while (a[i] != '\0' && lower(a[i]) == lower(b[i])) {
        i++;
    }
    if (lower(a[i]) != lower(b[i])) {
        return (unsigned char)lower(a[i]) < (unsigned char)lower(b[i]) ? -1
                                                                       : 1;
    }
    return strcmp(a, b);
}

/**
 * @brief Whether two names are equal without case.
 *
 * @param a  The first name.
 * @param b  The second name.
 * @return true when the names match without regard to case.
 */
static bool same_name(const char *a, const char *b)
{
    size_t i = 0;

    while (a[i] != '\0' && lower(a[i]) == lower(b[i])) {
        i++;
    }
    return a[i] == b[i];
}

/**
 * @brief The length of a stem: 1 to SERVO_STORE_STEM_MAX letters A to Z.
 *
 * @param stem  The stem.
 * @return Its length; 0 for NULL or a stem that is not such letters.
 */
static size_t stem_len(const char *stem)
{
    size_t n = 0;

    if (stem == NULL) {
        return 0;
    }
    while (stem[n] != '\0') {
        if (stem[n] < 'A' || stem[n] > 'Z' || n == SERVO_STORE_STEM_MAX) {
            return 0;
        }
        n++;
    }
    return n;
}

/**
 * @brief Whether the first @p n characters of @p s, in any case, are the
 *        first @p n of @p stem.
 *
 * @param s     The name.
 * @param stem  The stem, upper case.
 * @param n     Characters to compare.
 * @return true when they match.
 */
static bool stem_at(const char *s, const char *stem, size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        if (lower(s[i]) != lower(stem[i])) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Whether the first @p n characters of @p s are a device name.
 *
 * @param s  The name.
 * @param n  Number of characters to test: 3 or 4.
 * @return true for CON, PRN, AUX, NUL, COM1 to COM9 and LPT1 to LPT9.
 */
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
        if (n == 4u && s[3] >= '1' && s[3] <= '9'
            && strcmp(word, numbered[i]) == 0) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Whether a base name of @p n characters is the record: the stem
 *        alone, in any case.
 *
 * @param stem  The stem.
 * @param s     The base name.
 * @param n     Its length.
 * @return true for the record.
 */
static bool record(const char *stem, const char *s, size_t n)
{
    return n == stem_len(stem) && stem_at(s, stem, n);
}

/**
 * @brief Check a base name of known length.
 *
 * @param stem  The stem.
 * @param base  The base name.
 * @param n     Length of the base name, characters.
 * @return SERVO_STORE_NAME_OK, or the first rule the name breaks.
 */
static servo_store_name_t check(const char *stem, const char *base, size_t n)
{
    size_t dot = 0;

    if (n == 0u) {
        return SERVO_STORE_NAME_EMPTY;
    }
    if (n > SERVO_STORE_BASE_MAX) {
        return SERVO_STORE_NAME_TOO_LONG;
    }
    for (size_t i = 0; i < n; ++i) {
        const unsigned char c = (unsigned char)base[i];

        if (c < 0x20u || c > 0x7Eu || strchr("\"*/:<>?\\|", c) != NULL) {
            return SERVO_STORE_NAME_BAD_CHAR;
        }
    }
    if (base[0] == '.' || base[0] == ' ' || base[n - 1u] == '.'
        || base[n - 1u] == ' ') {
        return SERVO_STORE_NAME_EDGE;
    }
    while (dot < n && base[dot] != '.') {
        dot++;
    }
    return reserved(base, dot) || record(stem, base, n)
               ? SERVO_STORE_NAME_RESERVED
               : SERVO_STORE_NAME_OK;
}

servo_store_name_t servo_store_name_check(const char *stem, const char *base)
{
    return base == NULL ? SERVO_STORE_NAME_EMPTY
                        : check(stem, base, strlen(base));
}

bool servo_store_file_name(const char *stem, const char *base, char *out,
                           size_t out_size)
{
    size_t n;

    if (out == NULL || out_size == 0u) {
        return false;
    }
    out[0] = '\0';
    if (out_size < SERVO_STORE_NAME_MAX
        || servo_store_name_check(stem, base) != SERVO_STORE_NAME_OK) {
        return false;
    }
    n = strlen(base);
    memcpy(out, base, n);
    memcpy(out + n, SERVO_STORE_EXT, EXT_LEN + 1u);
    return true;
}

/**
 * @brief Length of the base name in a file name with the extension; 0 when
 *        the name does not end with it.
 *
 * @param name  The file name, with the extension.
 * @return The length of the base name, characters, or 0.
 */
static size_t base_len(const char *name)
{
    const size_t n = strlen(name);

    if (n <= EXT_LEN || !same_name(name + n - EXT_LEN, SERVO_STORE_EXT)) {
        return 0;
    }
    return n - EXT_LEN;
}

/**
 * @brief The number in "<stem>nnn.TXT", in any case; 0 for any other name.
 *
 * @param stem  The stem.
 * @param name  The file name.
 * @return The number 1 to 999, or 0.
 */
static int preset_number(const char *stem, const char *name)
{
    const size_t s = stem_len(stem);
    int number = 0;

    if (s == 0u || base_len(name) != s + 3u || !stem_at(name, stem, s)) {
        return 0;
    }
    for (size_t i = s; i < s + 3u; ++i) {
        if (name[i] < '0' || name[i] > '9') {
            return 0;
        }
        number = number * 10 + (name[i] - '0');
    }
    return number;
}

/** @brief A walk that marks the proposed numbers taken. */
typedef struct {
    const char *stem;              /**< the stem               */
    uint8_t taken[NUMBER_BYTES];   /**< bit n: number n taken  */
} mark_t;

/**
 * @brief Set the bit of a directory entry's proposed number.
 *
 * @param name       The entry name.
 * @param is_dir     Whether the entry is a directory; ignored.
 * @param visit_ctx  The mark_t.
 */
static void mark_number(const char *name, bool is_dir, void *visit_ctx)
{
    mark_t *m = visit_ctx;
    const int number = preset_number(m->stem, name);

    (void)is_dir;
    m->taken[number / 8] |= (uint8_t)(1u << (number % 8));
}

int servo_store_number(const char *stem, const char *file)
{
    return file != NULL ? preset_number(stem, file) : 0;
}

servo_store_t servo_store_preset(const servo_store_io_t *io, char *base,
                                 size_t base_size)
{
    mark_t m;
    int highest = 0;
    int number = 0;
    size_t s;

    if (io == NULL || base == NULL || base_size < SERVO_STORE_PRESET_MAX
        || stem_len(io->stem) == 0u) {
        return SERVO_STORE_ARG;
    }
    base[0] = '\0';
    if (!io->mounted(io->ctx)) {
        return SERVO_STORE_NO_CARD;
    }
    memset(&m, 0, sizeof(m));
    m.stem = io->stem;
    if (!io->walk(io->ctx, mark_number, &m)) {
        return SERVO_STORE_LIST_ERROR;
    }
    for (int n = SERVO_STORE_FIRST; n <= SERVO_STORE_LAST; ++n) {
        if ((m.taken[n / 8] & (1u << (n % 8))) != 0u) {
            highest = n;
        } else if (number == 0) {
            number = n;
        }
    }
    if (highest < SERVO_STORE_LAST) {
        number = highest + 1;
    }
    if (number == 0) {
        return SERVO_STORE_NO_NUMBER;
    }
    s = stem_len(io->stem);
    memcpy(base, io->stem, s);
    base[s] = (char)('0' + number / 100);
    base[s + 1u] = (char)('0' + number / 10 % 10);
    base[s + 2u] = (char)('0' + number % 10);
    base[s + 3u] = '\0';
    return SERVO_STORE_OK;
}

/** @brief State of a search for one file name. */
typedef struct {
    const char *name;   /**< the name searched for              */
    bool found;         /**< a name equal to it was seen        */
} find_t;

/**
 * @brief Set found when a directory entry has the searched name.
 *
 * @param name       The entry name.
 * @param is_dir     Whether the entry is a directory; ignored.
 * @param visit_ctx  The find_t state.
 */
static void find_name(const char *name, bool is_dir, void *visit_ctx)
{
    find_t *find = visit_ctx;

    (void)is_dir;
    if (same_name(name, find->name)) {
        find->found = true;
    }
}

servo_store_t servo_store_save(const servo_store_io_t *io, const char *base,
                               const char *text, size_t len,
                               servo_store_name_t *why)
{
    char name[SERVO_STORE_NAME_MAX];
    find_t find = {name, false};

    if (why != NULL) {
        *why = SERVO_STORE_NAME_OK;
    }
    if (io == NULL || text == NULL || stem_len(io->stem) == 0u) {
        return SERVO_STORE_ARG;
    }
    if (!servo_store_file_name(io->stem, base, name, sizeof(name))) {
        if (why != NULL) {
            *why = servo_store_name_check(io->stem, base);
        }
        return SERVO_STORE_BAD_NAME;
    }
    if (!io->mounted(io->ctx)) {
        return SERVO_STORE_NO_CARD;
    }
    if (!io->walk(io->ctx, find_name, &find)) {
        return SERVO_STORE_LIST_ERROR;
    }
    if (find.found) {
        return SERVO_STORE_EXISTS;
    }
    switch (io->create(io->ctx, name, text, len)) {
    case SERVO_STORE_IO_OK:
        return SERVO_STORE_OK;
    case SERVO_STORE_IO_EXISTS:
        return SERVO_STORE_EXISTS;
    case SERVO_STORE_IO_FULL:
        return SERVO_STORE_CARD_FULL;
    case SERVO_STORE_IO_DENIED:
        return SERVO_STORE_DENIED;
    default:
        return SERVO_STORE_WRITE_ERROR;
    }
}

/**
 * @brief Whether a name that is no proposed one stands in front of the
 *        proposed names: at or before "<stem>000.txt" in the order without
 *        case.
 *
 * @param stem  The stem.
 * @param name  The file name.
 * @return true when the name sorts at or before it.
 */
static bool before_presets(const char *stem, const char *name)
{
    char first[SERVO_STORE_STEM_MAX + 3u + EXT_LEN + 1u];
    const size_t s = stem_len(stem);
    size_t i = 0;

    for (size_t k = 0; k < s; ++k) {
        first[k] = lower(stem[k]);
    }
    memcpy(first + s, "000.txt", 8u);
    while (name[i] != '\0' && lower(name[i]) == first[i]) {
        i++;
    }
    return (unsigned char)lower(name[i]) <= (unsigned char)first[i];
}

/**
 * @brief Order of two file names in a list: by name without case, with the
 *        proposed names in one run where "<stem>000.TXT" would stand, the
 *        highest number first.
 *
 * @param stem  The stem.
 * @param a     The first file name.
 * @param b     The second file name.
 * @return Negative, zero or positive as @p a sorts before, with or after
 *         @p b.
 */
static int list_cmp(const char *stem, const char *a, const char *b)
{
    const int na = preset_number(stem, a);
    const int nb = preset_number(stem, b);

    if (na != 0 && nb != 0 && na != nb) {
        return na > nb ? -1 : 1;
    }
    if (na != 0 && nb == 0) {
        return before_presets(stem, b) ? 1 : -1;
    }
    if (na == 0 && nb != 0) {
        return before_presets(stem, a) ? -1 : 1;
    }
    return name_cmp(a, b);
}

/** @brief State of one list: the sorted entries kept so far. */
typedef struct {
    const char *stem;              /**< the stem                         */
    servo_store_entry_t *entries;  /**< the list being built             */
    const char *after;             /**< NULL: from the first file on     */
    size_t cap;                    /**< capacity of entries              */
    size_t count;                  /**< entries kept                     */
    size_t total;                  /**< qualifying files seen            */
    size_t skipped;                /**< files with a bad base name       */
} list_t;

/**
 * @brief Insert one directory entry into the sorted list.
 *
 * @param name       The entry name.
 * @param is_dir     Whether the entry is a directory; directories are
 *                   skipped.
 * @param visit_ctx  The list_t state.
 */
static void list_name(const char *name, bool is_dir, void *visit_ctx)
{
    list_t *list = visit_ctx;
    const size_t base = base_len(name);
    size_t at;

    /* The record of a write under way is no servo file and no stray one. */
    if (is_dir || base == 0u || record(list->stem, name, base)) {
        return;
    }
    if (check(list->stem, name, base) != SERVO_STORE_NAME_OK) {
        list->skipped++;
        return;
    }
    list->total++;
    if (list->after != NULL && list_cmp(list->stem, name, list->after) <= 0) {
        return;
    }
    at = list->count;
    while (at > 0u
           && list_cmp(list->stem, name, list->entries[at - 1u].name) < 0) {
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

servo_store_t servo_store_list_after(const servo_store_io_t *io,
                                     const char *after,
                                     servo_store_entry_t *entries, size_t cap,
                                     size_t *count, size_t *total,
                                     size_t *skipped)
{
    list_t list;
    bool read_through;

    if (io == NULL || (entries == NULL && cap > 0u)
        || stem_len(io->stem) == 0u) {
        return SERVO_STORE_ARG;
    }
    if (!io->mounted(io->ctx)) {
        return SERVO_STORE_NO_CARD;
    }
    memset(&list, 0, sizeof(list));
    list.stem = io->stem;
    list.entries = entries;
    list.after = after;
    list.cap = cap;
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
    return read_through ? SERVO_STORE_OK : SERVO_STORE_LIST_ERROR;
}

servo_store_t servo_store_load(const servo_store_io_t *io, const char *name,
                               char *buf, size_t cap, size_t *len)
{
    int32_t size;

    if (io == NULL || name == NULL || buf == NULL || len == NULL) {
        return SERVO_STORE_ARG;
    }
    *len = 0;
    if (check(io->stem, name, base_len(name)) != SERVO_STORE_NAME_OK) {
        return SERVO_STORE_BAD_NAME;
    }
    if (!io->mounted(io->ctx)) {
        return SERVO_STORE_NO_CARD;
    }
    size = io->size(io->ctx, name);
    if (size < 0) {
        return SERVO_STORE_NOT_FOUND;
    }
    if ((size_t)size > cap) {
        return SERVO_STORE_TOO_LARGE;
    }
    if (io->read(io->ctx, name, buf, cap) != size) {
        return SERVO_STORE_READ_ERROR;
    }
    *len = (size_t)size;
    return SERVO_STORE_OK;
}
