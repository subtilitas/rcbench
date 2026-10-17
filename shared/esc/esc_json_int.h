/*
 * The tokens of esc_json_lex.c and the functions esc_json.c reads them with.
 * Private to shared/esc/: no other file includes it.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef ESC_JSON_INT_H
#define ESC_JSON_INT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { T_OBJ, T_ARR, T_STR, T_NUM, T_TRUE, T_FALSE, T_NULL } ttype_t;

typedef struct {
    ttype_t  type;
    uint32_t start;     /* first byte; for a string, after the quote      */
    uint32_t end;       /* one past the last byte; before the quote       */
    uint32_t size;      /* members of an object, elements of an array     */
    uint32_t next;      /* the token after this value and its descendants */
} tok_t;

/* Why the text was refused, beyond its not being JSON. */
typedef enum {
    LEX_SYNTAX = 0,
    LEX_DUP,            /* a key an object already had            */
    LEX_NUL,            /* \u0000, which would end a C string     */
    LEX_SURROGATE,      /* half a surrogate pair                  */
    LEX_UTF8,           /* bytes that are not UTF-8               */
    LEX_MEMBERS,        /* an object with more than 64 members    */
} lex_why_t;

typedef struct {
    const char *s;
    uint32_t    len;
    uint32_t    pos;
    tok_t      *tok;    /* NULL: count only */
    uint32_t    n;
    uint32_t    cap;
    lex_why_t   why;    /* what stopped the read, past a syntax error */
} lex_t;

/* Cut lexer @p l's text into tokens.  With l->tok NULL it only counts them
 * into l->n; with l->tok set to l->cap tokens it stores them and refuses a
 * key an object already had.  False on a refusal, with l->pos and l->why
 * saying where and why. */
bool esc_json_lex_all(lex_t *l);

/* Why the lexer refused, as the text after "%s near byte". */
const char *esc_json_lex_why_text(lex_why_t why);

/* The UTF-8 bytes of the character at s[*i], into @p out; *i moves past it.
 * Only for text esc_json_lex_all() accepted.  Returns the byte count, 1 to 4. */
unsigned esc_json_decode_unit(const char *s, uint32_t *i, uint8_t out[4]);

/* Whether a string token's text decodes to the plain @p lit. */
bool esc_json_text_is(const char *s, const tok_t *a, const char *lit);

#endif /* ESC_JSON_INT_H */
