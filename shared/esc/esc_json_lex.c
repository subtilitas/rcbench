/**
 * @file
 * @brief The first stage of reading an ESC profile from JSON: the text cut into
 *        tokens.
 *
 * See esc_json.c for the second stage, and esc_json_int.h for the four
 * functions this file gives it.
 *
 * One token per value, each with the index one past its last descendant, so
 * a reader skips a subtree in one step and never recurses to find a key.
 * The reader runs this twice over the same text: once to count the tokens,
 * once into an array of that size, where the duplicate-key check also runs.
 * Every string is checked here, including those in fields the panel never
 * reads.
 *
 * The rules are tools/gen_esc_profiles.py's.  The host suite parses every
 * profile of record and compares it with the generated table, so a rule
 * that differs between the two fails there.
 *
 * SPDX-License-Identifier: MIT
 */

#include "esc_json_int.h"

#include <string.h>

/**
 * @brief Nesting deeper than any profile needs (five levels) is refused rather
 *        than followed, so a hostile file cannot run the stack down.
 */
#define MAX_DEPTH 16

/**
 * @brief No profile object has more than 14 members.
 *
 * The duplicate check compares each key with those before it, so this bounds it
 * at 64 x 63 / 2 comparisons an object rather than letting a 64 KiB file of
 * short keys ask for tens of millions.
 */
#define MAX_MEMBERS 64u

/* ------------------------------------------------------------ the */

/* ------------------------------------------------------------ the */

/* ------------------------------------------------------------ the */

/**
 * @brief Skip white space.
 *
 * Skips space, tab, LF and CR.
 *
 * @param l  The lexer state; its position moves past the white space.
 */
static void ws(lex_t *l)
{
    while (l->pos < l->len) {
        const char c = l->s[l->pos];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
            return;
        }
        l->pos++;
    }
}

/**
 * @brief Add a token that starts at a byte.
 *
 * Stores the token when the lexer has a token array; counts it always.
 *
 * @param l      The lexer state.
 * @param t      Type of the token.
 * @param start  First byte of the token.
 * @return Index of the token, or -1 when the array is full.
 */
static int64_t add(lex_t *l, ttype_t t, uint32_t start)
{
    if (l->tok != NULL) {
        if (l->n >= l->cap) {
            return -1;
        }
        l->tok[l->n] = (tok_t){ t, start, start, 0u, 0u };
    }
    return (int64_t)l->n++;
}

/**
 * @brief Set the end, size and next link of a token.
 *
 * Does nothing when the lexer only counts tokens.
 *
 * @param l     The lexer state.
 * @param i     Index of the token.
 * @param end   One past the last byte of the token.
 * @param size  Members of an object, elements of an array, or 0.
 */
static void close_tok(lex_t *l, int64_t i, uint32_t end, uint32_t size)
{
    if (l->tok != NULL) {
        l->tok[i].end  = end;
        l->tok[i].size = size;
        l->tok[i].next = l->n;
    }
}

static bool lex_value(lex_t *l, int depth);

/**
 * @brief Value of one hexadecimal digit.
 *
 * @param c  The digit character.
 * @return 0 to 15, or -1 when @p c is not a hexadecimal digit.
 */
static int hexval(char c)
{
    if (c >= '0' && c <= '9') { return c - '0'; }
    if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
    if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
    return -1;
}

/**
 * @brief The four hex digits at @p s, or -1.
 *
 * @param s  The text; four digits are read.
 * @return The 16-bit value, or -1 when a digit is not hexadecimal.
 */
static int32_t hex4(const char *s)
{
    int32_t v = 0;
    for (int k = 0; k < 4; ++k) {
        const int h = hexval(s[k]);
        if (h < 0) {
            return -1;
        }
        v = (v << 4) | h;
    }
    return v;
}

/**
 * @brief The length of the well-formed UTF-8 sequence at @p s, or 0: no stray
 *        continuation byte, no overlong form, no encoded surrogate, nothing
 *        past U+10FFFF.
 *
 * What Python's UTF-8 decoder takes, so a file the generator would refuse to
 * read is refused here too.
 *
 * @param s      The bytes, starting at the lead byte.
 * @param avail  Bytes available from @p s.
 * @return The sequence length, 2 to 4, or 0 when it is not well formed.
 */
static uint32_t utf8_len(const unsigned char *s, uint32_t avail)
{
    const unsigned char c = s[0];
    uint32_t n;
    unsigned char lo = 0x80u, hi = 0xBFu;      /* the second byte's range */
    if (c >= 0xC2u && c <= 0xDFu) {
        n = 2u;
    } else if (c >= 0xE0u && c <= 0xEFu) {
        n = 3u;
        if (c == 0xE0u) { lo = 0xA0u; }         /* not overlong */
        if (c == 0xEDu) { hi = 0x9Fu; }         /* not a surrogate */
    } else if (c >= 0xF0u && c <= 0xF4u) {
        n = 4u;
        if (c == 0xF0u) { lo = 0x90u; }         /* not overlong */
        if (c == 0xF4u) { hi = 0x8Fu; }         /* not past U+10FFFF */
    } else {
        return 0u;
    }
    if (avail < n || s[1] < lo || s[1] > hi) {
        return 0u;
    }
    for (uint32_t k = 2u; k < n; ++k) {
        if (s[k] < 0x80u || s[k] > 0xBFu) {
            return 0u;
        }
    }
    return n;
}

/**
 * @brief A string, with every escape checked here, in the one pass that sees
 *        every string -- including those in fields the panel never reads.
 *        \u0000 is refused (it would end a C string early), and so is half a
 *        surrogate pair: json.loads() keeps a lone half, which no UTF-8 can
 *        carry.
 *
 * What passes is what esc_json_decode_unit() decodes without looking again.
 *
 * @param l  The lexer state, at the opening quote; moves past the closing
 *           quote on success.
 * @return true when the string is well formed.
 */
static bool lex_string(lex_t *l)
{
    const uint32_t start = ++l->pos;            /* past the quote */
    while (l->pos < l->len) {
        const unsigned char c = (unsigned char)l->s[l->pos];
        if (c == '"') {
            const int64_t i = add(l, T_STR, start);
            if (i < 0) {
                return false;
            }
            close_tok(l, i, l->pos, 0u);
            l->pos++;
            return true;
        }
        if (c < 0x20u) {
            return false;                       /* raw control character */
        }
        if (c >= 0x80u) {
            const uint32_t n = utf8_len((const unsigned char *)l->s + l->pos,
                                        l->len - l->pos);
            if (n == 0u) {
                l->why = LEX_UTF8;
                return false;
            }
            l->pos += n;
            continue;
        }
        if (c != '\\') {
            l->pos++;
            continue;
        }
        if (l->pos + 1u >= l->len) {
            return false;
        }
        const char e = l->s[l->pos + 1u];
        if (e != 'u') {
            if (strchr("\"\\/bfnrt", e) == NULL || e == '\0') {
                return false;
            }
            l->pos += 2u;
            continue;
        }
        if (l->len - l->pos < 6u) {
            return false;
        }
        const int32_t cp = hex4(l->s + l->pos + 2u);
        if (cp < 0) {
            return false;
        }
        if (cp == 0) {
            l->why = LEX_NUL;
            return false;
        }
        if (cp >= 0xDC00 && cp <= 0xDFFF) {
            l->why = LEX_SURROGATE;             /* a low half first */
            return false;
        }
        if (cp >= 0xD800 && cp <= 0xDBFF) {
            const int32_t lo = (l->len - l->pos >= 12u
                                && l->s[l->pos + 6u] == '\\'
                                && l->s[l->pos + 7u] == 'u')
                                   ? hex4(l->s + l->pos + 8u) : -1;
            if (lo < 0xDC00 || lo > 0xDFFF) {
                l->why = LEX_SURROGATE;         /* a high half alone */
                return false;
            }
            l->pos += 6u;
        }
        l->pos += 6u;
    }
    return false;                               /* no closing quote */
}

/*
 * The UTF-8 bytes of the character at s[*i], into @p out; *i moves past
 * it.
 *
 * A raw byte is itself (UTF-8 in the file passes through a byte at a time); an
 * escape is decoded, a surrogate pair into one four-byte character.  Only for
 * text lex_string() accepted.
 */
unsigned esc_json_decode_unit(const char *s, uint32_t *i, uint8_t out[4])
{
    const char c = s[*i];
    if (c != '\\') {
        out[0] = (uint8_t)c;
        *i += 1u;
        return 1u;
    }
    const char e = s[*i + 1u];
    *i += 2u;
    switch (e) {
    case 'b': out[0] = '\b'; return 1u;
    case 'f': out[0] = '\f'; return 1u;
    case 'n': out[0] = '\n'; return 1u;
    case 'r': out[0] = '\r'; return 1u;
    case 't': out[0] = '\t'; return 1u;
    case 'u': break;
    default:  out[0] = (uint8_t)e; return 1u;  /* \" \\ \/ */
    }
    uint32_t cp = (uint32_t)hex4(s + *i);
    *i += 4u;
    if (cp >= 0xD800u && cp <= 0xDBFFu) {
        const uint32_t lo = (uint32_t)hex4(s + *i + 2u);
        *i += 6u;
        cp = 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u);
    }
    if (cp < 0x80u) {
        out[0] = (uint8_t)cp;
        return 1u;
    }
    if (cp < 0x800u) {
        out[0] = (uint8_t)(0xC0u | (cp >> 6));
        out[1] = (uint8_t)(0x80u | (cp & 0x3Fu));
        return 2u;
    }
    if (cp < 0x10000u) {
        out[0] = (uint8_t)(0xE0u | (cp >> 12));
        out[1] = (uint8_t)(0x80u | ((cp >> 6) & 0x3Fu));
        out[2] = (uint8_t)(0x80u | (cp & 0x3Fu));
        return 3u;
    }
    out[0] = (uint8_t)(0xF0u | (cp >> 18));
    out[1] = (uint8_t)(0x80u | ((cp >> 12) & 0x3Fu));
    out[2] = (uint8_t)(0x80u | ((cp >> 6) & 0x3Fu));
    out[3] = (uint8_t)(0x80u | (cp & 0x3Fu));
    return 4u;
}

/**
 * @brief Whether two string tokens' texts decode to the same bytes, so "schema"
 *        and "sch\u0065ma" are one key, as json.loads() has them.
 *
 * The two decoded streams are compared byte by byte, not unit by unit: a raw
 * "\xC3\xA9" comes a byte at a time and "\u00e9" as both bytes at once, and
 * they are the same character.
 *
 * @param s  The text both tokens point into.
 * @param a  The first string token.
 * @param b  The second string token.
 * @return true when the decoded texts are equal.
 */
static bool same_text(const char *s, const tok_t *a, const tok_t *b)
{
    uint32_t i = a->start, j = b->start;
    uint8_t x[4], y[4];
    unsigned xn = 0, xi = 0, yn = 0, yi = 0;
    for (;;) {
        if (xi == xn && i < a->end) {
            xn = esc_json_decode_unit(s, &i, x);
            xi = 0;
        }
        if (yi == yn && j < b->end) {
            yn = esc_json_decode_unit(s, &j, y);
            yi = 0;
        }
        const bool x_done = (xi == xn);
        const bool y_done = (yi == yn);
        if (x_done || y_done) {
            return x_done && y_done;
        }
        if (x[xi++] != y[yi++]) {
            return false;
        }
    }
}

/*
 * Whether a string token's text decodes to the plain @p lit.
 */
bool esc_json_text_is(const char *s, const tok_t *a, const char *lit)
{
    uint32_t i = a->start;
    size_t k = 0;
    const size_t n = strlen(lit);
    while (i < a->end) {
        uint8_t x[4];
        const unsigned m = esc_json_decode_unit(s, &i, x);
        if (k + m > n || memcmp(x, lit + k, m) != 0) {
            return false;
        }
        k += m;
    }
    return k == n;
}

/**
 * @brief Whether a character is an ASCII decimal digit.
 *
 * @param c  The character.
 * @return true for '0' to '9'.
 */
static bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

/**
 * @brief -? (0 | [1-9][0-9]*) (. [0-9]+)? ([eE] [+-]? [0-9]+)?, as JSON has it
 *        and as the generator's json.loads() takes it, so a file one refuses
 *        the other refuses too, whichever field the number sits in.
 *
 * @param l  The lexer state, at the first character of the number.
 * @return true when a well-formed number was lexed.
 */
static bool lex_number(lex_t *l)
{
    const uint32_t start = l->pos;
    uint32_t i = l->pos;
    const char *s = l->s;
    if (i < l->len && s[i] == '-') {
        i++;
    }
    if (i < l->len && s[i] == '0') {
        i++;
    } else if (i < l->len && s[i] >= '1' && s[i] <= '9') {
        while (i < l->len && is_digit(s[i])) {
            i++;
        }
    } else {
        return false;
    }
    if (i < l->len && s[i] == '.') {
        i++;
        if (i >= l->len || !is_digit(s[i])) {
            return false;
        }
        while (i < l->len && is_digit(s[i])) {
            i++;
        }
    }
    if (i < l->len && (s[i] == 'e' || s[i] == 'E')) {
        i++;
        if (i < l->len && (s[i] == '+' || s[i] == '-')) {
            i++;
        }
        if (i >= l->len || !is_digit(s[i])) {
            return false;
        }
        while (i < l->len && is_digit(s[i])) {
            i++;
        }
    }
    l->pos = i;
    const int64_t t = add(l, T_NUM, start);
    if (t < 0) {
        return false;
    }
    close_tok(l, t, l->pos, 0u);
    return true;
}

/**
 * @brief Lex a literal word: true, false or null.
 *
 * @param l  The lexer state.
 * @param w  The word to match.
 * @param t  The token type for the word.
 * @return true when the word matched.
 */
static bool lex_word(lex_t *l, const char *w, ttype_t t)
{
    const size_t n = strlen(w);
    if (l->len - l->pos < n || memcmp(l->s + l->pos, w, n) != 0) {
        return false;
    }
    const int64_t i = add(l, t, l->pos);
    if (i < 0) {
        return false;
    }
    l->pos += (uint32_t)n;
    close_tok(l, i, l->pos, 0u);
    return true;
}

/**
 * @brief Whether the key just read is one of the @p members before it in the
 *        object at token @p obj.  json.loads() keeps the last of two and a
 *        reader that stops at the first keeps the first, so a file with a key
 *        twice is refused rather than read two ways.
 *
 * Only once the tokens exist: the counting pass has nothing to compare.
 *
 * @param l        The lexer state; the key is its newest token.
 * @param obj      Index of the object token.
 * @param members  Members of the object read before this key.
 * @return true when an earlier member has the same key.
 */
static bool key_seen(const lex_t *l, uint32_t obj, uint32_t members)
{
    const tok_t *k = &l->tok[l->n - 1u];
    uint32_t i = obj + 1u;
    for (uint32_t m = 0; m < members; ++m) {
        if (same_text(l->s, &l->tok[i], k)) {
            return true;
        }
        i = l->tok[i + 1u].next;
    }
    return false;
}

/**
 * @brief Lex an object or an array.
 *
 * @param l      The lexer state, at the opening bracket.
 * @param depth  Nesting depth of this container.
 * @param obj    true for an object, false for an array.
 * @return true when the container was lexed.
 */
static bool lex_container(lex_t *l, int depth, bool obj)
{
    const int64_t i = add(l, obj ? T_OBJ : T_ARR, l->pos);
    if (i < 0) {
        return false;
    }
    const char close = obj ? '}' : ']';
    uint32_t size = 0;
    l->pos++;
    ws(l);
    if (l->pos < l->len && l->s[l->pos] == close) {
        l->pos++;
        close_tok(l, i, l->pos, 0u);
        return true;
    }
    for (;;) {
        if (obj) {
            if (size >= MAX_MEMBERS) {
                l->why = LEX_MEMBERS;
                return false;
            }
            ws(l);
            if (l->pos >= l->len || l->s[l->pos] != '"' || !lex_string(l)) {
                return false;
            }
            if (l->tok != NULL && key_seen(l, (uint32_t)i, size)) {
                l->why = LEX_DUP;
                return false;
            }
            ws(l);
            if (l->pos >= l->len || l->s[l->pos] != ':') {
                return false;
            }
            l->pos++;
        }
        if (!lex_value(l, depth + 1)) {
            return false;
        }
        size++;
        ws(l);
        if (l->pos >= l->len) {
            return false;
        }
        if (l->s[l->pos] == ',') {
            l->pos++;
            continue;
        }
        if (l->s[l->pos] == close) {
            l->pos++;
            close_tok(l, i, l->pos, size);
            return true;
        }
        return false;
    }
}

/**
 * @brief Lex one value of any type.
 *
 * @param l      The lexer state.
 * @param depth  Nesting depth of the value.
 * @return true when the value was lexed.
 */
static bool lex_value(lex_t *l, int depth)
{
    if (depth > MAX_DEPTH) {
        return false;
    }
    ws(l);
    if (l->pos >= l->len) {
        return false;
    }
    switch (l->s[l->pos]) {
    case '{': return lex_container(l, depth, true);
    case '[': return lex_container(l, depth, false);
    case '"': return lex_string(l);
    case 't': return lex_word(l, "true", T_TRUE);
    case 'f': return lex_word(l, "false", T_FALSE);
    case 'n': return lex_word(l, "null", T_NULL);
    default:  return lex_number(l);
    }
}

/* The whole text is one value with nothing after it but white space. */
bool esc_json_lex_all(lex_t *l)
{
    l->pos = 0;
    l->n = 0;
    if (!lex_value(l, 0)) {
        return false;
    }
    ws(l);
    return l->pos == l->len;
}

const char *esc_json_lex_why_text(lex_why_t why)
{
    switch (why) {
    case LEX_DUP:       return "a key twice";
    case LEX_NUL:       return "a string holds \\u0000";
    case LEX_SURROGATE: return "half a surrogate pair";
    case LEX_UTF8:      return "not UTF-8";
    case LEX_MEMBERS:   return "an object with more than 64 members";
    case LEX_SYNTAX:
    default:            return "not JSON";
    }
}
