#!/usr/bin/env python3
"""Hold every module under protocols/ to the rule that lets it leave.

A module under protocols/ is built in other pico-sdk projects, C and C++,
without the rest of this repository (CONTRIBUTING.md, "Where code goes").
This tool reads each module and fails when one of these does not hold:

- the module has a README.md, a CMakeLists.txt, a header under include/
  and a C file beside them;
- a core file, which is every C file, header and PIO (programmable
  input/output) program outside the module's rp2350/ folder, includes only
  C standard headers and headers of its own core;
- a file under rp2350/ includes only those, the module's own driver
  headers, the header pioasm generates from a .pio file of the module, and
  pico-sdk headers: `pico.h`, `pico/...` and `hardware/...`;
- a header under rp2350/ includes no pico-sdk header, so the driver's
  interface is read without the SDK (software development kit);
- every header under include/ and rp2350/ carries `extern "C"` and
  compiles by itself as C11 and as C++17, with `-Wall -Wextra -Werror`;
- an include of a module file names no path with a `..` component and no
  absolute path, also when the file it reaches has the name of a header of the
  module.  The scan reads `#include`, `#include_next`, `#import` and their `%:`
  digraph forms, joins line continuations first (a backslash, then optional
  spaces, tabs, form feeds or vertical tabs, then a newline), except inside a
  raw string literal, and keeps string, character and raw string literals and
  numbers out of comment removal.  A string or character literal without its
  closing quote runs to the line end, as GCC lexes it; a backslash before that
  line end escapes nothing.  A file is read as UTF-8 bytes with an optional
  byte order mark; CRLF, LF and a lone CR end a line, and form feed and
  vertical tab count as white space in a directive.  An operand that is neither
  `<...>` nor `"..."` (a macro include) fails, because the tool does not expand
  macros.  The name `__has_include` or `__has_include_next` outside a literal
  fails for the same reason, as a call and also as the body of a macro, which
  GCC evaluates as the call.  It passes as the operand of `#ifdef`, `#ifndef`,
  `#elifdef` and `#elifndef`, and as the operand of a `defined` term
  (`defined NAME`, `defined(NAME)`) on an `#if` or `#elif` line that holds
  nothing but such terms, white space, `(`, `)`, `!`, `&` and `|`.  The
  line is the directive's line as GCC reads it: a comment or raw string
  literal across a line break continues it.  Any other token on that line
  fails it (`#if X || defined(__has_include)`),
  because a macro can take `defined` into an argument and paste it to
  another name, and so does a `defined` joined to the identifier before it
  (`$defined`).  A token after the operand fails the include as well.  An
  `#include_next` fails unless it names a C standard header or, in a C file
  under rp2350/, a pico-sdk header: it searches the include path after the
  directory of the file, so it reaches the next file of that name outside the
  module.  A line of only `#` or `%:` fails, because a
  comment across a line break can hide the include that follows.  A trigraph
  (`??` followed by one of `= / ' ( ) ! < > -`) anywhere in the C, also in a
  comment or a literal (in a .pio file, in the C written, as below), fails: ISO
  C11 and C17 replace it before the file is read, so `??/` is a backslash that
  joins lines and escapes quotes, `??'` is a `^` that removes a quote, and
  `??=` is a `#`.  A file with a NUL byte fails, so no NUL takes part in a line
  continuation or a directive.  A file is read under the literal rules of every
  language it can be compiled as, and an include found under any of them
  counts: ISO C11 and C17 (no raw strings, no digit separators, no delimited or
  named universal character names), C++11 (raw strings), gnu11, gnu17 and
  gnu++11 (raw strings and delimited and named universal character names), C23
  (digit separators), C++14 to C++20 (raw strings and digit separators), and
  gnu23, C++23 and gnu++14 or later (all three).  A raw string literal
  (`R"x(...)x"`) is one token, with the line continuations inside it kept, and
  a number is one preprocessing number, through digit separators (`1'000`, but
  not a `'` before a `.`, a `$`, a universal character name or a character
  outside ASCII), exponent signs (`1e-x`), `$`, universal character names
  (`\\u00e9`, `\\U000000E9`, and in gnu11, gnu17, gnu23, gnu++11 or later and
  C++23 also `\\u{e9}` and `\\N{LATIN SMALL LETTER E WITH ACUTE}`) and the
  characters outside ASCII that GCC 13 takes into an identifier (`1$'a` in
  C23): the ranges of ISO C11 Annex D.1, EXTENDED_RANGES below.  A number
  starts at an ASCII digit (0 to 9) after any character but a letter, a digit,
  `_`, `$` and a character of these ranges, and in gnu11, gnu17, gnu23, gnu++11
  or later and C++23 not after a delimited or named universal character name,
  which starts an identifier (`é1`, `€1`, `\\u{e9}1`).  A decimal digit outside
  ASCII starts an identifier (`٠1`, U+0660).  A character outside ASCII and
  outside the ranges ends a number and a name, and a number or raw string
  literal starts after it, as in C (`×1'a`, `ⸯR"(...)"`, `ⸯ__has_include`).
  The C of a .pio file is the text pioasm 2.3.0 writes into the header it
  generates: the lines of each `% c-sdk {` block after the first `.program`, in
  order.  The blocks are found as pioasm's lexer finds them, on the file as
  written: a `;` or `//` comment runs to the line end, a `;` inside a `/* */`
  comment ends nothing, the rest of a `.lang_opt` line is one string, a block
  name is trimmed of spaces and tabs and an empty one is c-sdk, a lone CR is a
  character of its line, and a block ends only at a line of `%}` in column 0
  with nothing but spaces, tabs and CRs after it.  CRLF and a lone CR end a
  line of the C written;
- the module's README.md has a heading with "Interface" in it; a heading
  with "Trademark" in it, in any case, when the text names a brand listed
  in BRANDS below; and, for a module without an rp2350/ folder, the
  sentence "no pin driver".  The Interface heading and "no pin driver" are
  read outside fenced code blocks and HTML comments.  A brand is matched
  anywhere in the file, in any case, in paths, addresses, code blocks and
  comments too, and also as part of an identifier, file name or number
  (`kst_session_t`, `hitec_x.c`, `www.futaba.com`, `2kst`).  A brand next
  to a letter is part of a longer word and does not match: `KSTART`, and
  also a CamelCase identifier such as `KstSession`;
- the module's CMake files name no ESP-IDF (Espressif Internet-of-Things
  Development Framework) requirement and no `rcbench_` library of another
  module;
- a limit a module shares with the project is held equal by a
  `_Static_assert` in the caller (CONTRIBUTING.md, "Where code goes"): a
  `_Static_assert` (or `static_assert`) outside protocols/ that names a macro
  or enumerator of a module's public headers is listed in ASSERTED below by
  file and by condition, and each listed condition is still asserted in its
  file.  Conditions compare with whitespace collapsed and the message
  dropped.  The list is checked in both directions, so a new assertion is
  recorded and a removed one fails, also when its file keeps another.  The
  form is a table of the assertions that exist, read from the callers, and
  not a search for expressions that mix a module macro with a LINK_ macro:
  the table fails on a removed assertion and on a new one that is not
  recorded, and the search would flag every comparison that is not a pinned
  limit.

Current limitation: the CMake include directories and link targets of a
module are not checked.  A module that adds a project directory with
`target_include_directories` or links a project library other than an
`rcbench_` one passes, and a header found there can shadow a module header
of the same name.  Only `REQUIRES` and `rcbench_` names are read.

Current limitation: a `__has_include` that a macro pastes together with `##`
(`#define CAT(a, b) a##b`, then `#if CAT(__has_, include)("../x.h")`) passes,
and GCC 13 evaluates it as the call.

The SPDX (Software Package Data Exchange) line of the .c and .h files under
protocols/ is held by tools/check_docs.py.

    python3 tools/check_protocols.py            # the modules, then the check
    python3 tools/check_protocols.py --check    # the check alone

Both forms exit with 1 when a rule is broken.  CI (continuous integration)
runs `--check`.

SPDX-License-Identifier: MIT
"""

from __future__ import annotations

import bisect
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

REPO = pathlib.Path(__file__).resolve().parent.parent
PROTOCOLS = REPO / "protocols"

# The folder of a module that holds its RP2350 pin driver.
DRIVER = "rp2350"

# The headers of ISO C11, clause 7.1.2.
C_STANDARD = frozenset("""
    assert.h complex.h ctype.h errno.h fenv.h float.h inttypes.h iso646.h
    limits.h locale.h math.h setjmp.h signal.h stdalign.h stdarg.h
    stdatomic.h stdbool.h stddef.h stdint.h stdio.h stdlib.h stdnoreturn.h
    string.h tgmath.h threads.h time.h uchar.h wchar.h wctype.h
""".split())

# What a pin driver may include from the pico-sdk.
SDK_RE = re.compile(r"^(pico\.h|pico/.+\.h|hardware/.+\.h)$")

# A directive that includes a file: `#` or its digraph `%:`.
# White space between the tokens of a directive: space, tab, form feed and
# vertical tab.
WS = r"[ \t\f\v]"
DIRECTIVE = (r"^" + WS + r"*(?:#|%:)" + WS
             + r"*(?:include_next|include|import)\b")
# An include directive: its word (group 1) and its operand (group 2).
INCLUDE_RE = re.compile(r"^" + WS + r"*(?:#|%:)" + WS
                        + r"*(include_next|include|import)\b" + WS
                        + r"*(.*)$", re.M)
# A line with nothing but `#` or `%:` (a null directive, or a directive
# whose name a comment across a line break has cut off).
BARE_DIRECTIVE_RE = re.compile(r"^" + WS + r"*(?:#|%:)" + WS + r"*$")
# A trigraph, which ISO C11 and C17 replace anywhere in the file, also in a
# comment, before lines are joined: `??/` is a backslash, which joins two
# lines before a newline and escapes a quote in a literal, `??'` is a `^`,
# which removes a quote, and `??=` is a `#`.
TRIGRAPH_RE = re.compile(r"\?\?[=/'()!<>-]")
# The characters outside ASCII that GCC 13 takes into an identifier or a
# preprocessing number: the ranges of ISO C11 Annex D.1, the same list as
# C++11 Annex E.1.  Derived with GCC 13.3: `#ifdef a<ch>` for every code
# point from U+0080 to U+10FFFF but the surrogates, with -Wno-normalized
# and -Wno-bidi-chars, in c11, gnu11, c17, gnu17, c2x, gnu2x, c++14,
# c++17, c++20, c++23, gnu++14, gnu++17 and gnu++23.  Every mode accepts the
# same 971,622 characters and gives a diagnostic for every other.  In C, a
# character outside these ranges is a token of its own, so it ends a number
# and a number or raw string can start after it; C++ reads it into the
# identifier and stops with an error.  Python's `\w` matches 133,485,
# 137,873 and 142,877 characters outside ASCII under Unicode 14.0, 15.0 and
# 16.0 (Python 3.11, 3.12 and 3.14); under each, all but U+2E2F (`ⸯ`) are
# in these ranges.
EXTENDED_RANGES = (
    (0xA8, 0xA8), (0xAA, 0xAA), (0xAD, 0xAD), (0xAF, 0xAF), (0xB2, 0xB5),
    (0xB7, 0xBA), (0xBC, 0xBE), (0xC0, 0xD6), (0xD8, 0xF6), (0xF8, 0x167F),
    (0x1681, 0x180D), (0x180F, 0x1FFF), (0x200B, 0x200D), (0x202A, 0x202E),
    (0x203F, 0x2040), (0x2054, 0x2054), (0x2060, 0x218F), (0x2460, 0x24FF),
    (0x2776, 0x2793), (0x2C00, 0x2DFF), (0x2E80, 0x2FFF), (0x3004, 0x3007),
    (0x3021, 0x302F), (0x3031, 0xD7FF), (0xF900, 0xFDCF), (0xFDF0, 0xFE44),
    (0xFE47, 0xFFFD),
    *((plane, plane + 0xFFFD) for plane in range(0x10000, 0xF0000, 0x10000)))
# One character of a GCC identifier as it stands in the text: a letter,
# digit, `_`, `$` or a character of EXTENDED_RANGES.
ID_CHAR = ("[A-Za-z0-9_$" + "".join("\\U%08x-\\U%08x" % r
                                    for r in EXTENDED_RANGES) + "]")
# The opening of a raw string literal, `R"x(...)x"`: C++11 and the GNU
# dialects of C.  Inside it, from its first `"` on, line continuations are
# not joined, so its body is matched on the text as written.
RAW_OPEN_RE = r'(?<!' + ID_CHAR + r')(?:u8|[uUL])?R(?=")'
RAW_BODY_RE = re.compile(
    r'"(?P<delim>[^()\\ \t\f\v\n]{0,16})\(.*?\)(?P=delim)"', re.S)
# A string or character literal.  One without its closing quote runs to the
# line end, as GCC lexes it, so a `/*` after it on the line opens nothing.
# A backslash escapes any character but a newline: a backslash before the
# line end ends the literal there.
STRING_LITERAL = r'"(?:\\[^\n]|[^"\\\n])*"?'
CHAR_LITERAL = r"'(?:\\[^\n]|[^'\\\n])*'?"
# A character that continues a GCC identifier or preprocessing number: a
# letter, digit, `_`, `$`, a character of EXTENDED_RANGES and a universal
# character name (`\u00e9`, `\U000000e9`).
IDENTIFIER_CHAR = (r"(?:" + ID_CHAR + r"|\\u[0-9A-Fa-f]{4}"
                   r"|\\U[0-9A-Fa-f]{8})")
# The delimited (`\u{e9}`) and named (`\N{LATIN SMALL LETTER E WITH
# ACUTE}`) universal character names, which continue a number only in
# gnu11, gnu17, gnu23, gnu++11 or later and C++23.  C11, C17, C23 and
# C++14 to C++20 end the number before the `\`.
DELIMITED_UCN = r"|\\u\{[0-9A-Fa-f]+\}|\\N\{[A-Za-z0-9 _-]+\}"
# An identifier from a delimited or named universal character name on, in
# gnu11, gnu17, gnu23, gnu++11 or later and C++23: kept as text, so no
# number starts at a digit after the `}` (`\u{e9}1'a'` is an identifier
# and a character literal).
DELIMITED_WORD = (r"(?:\\u\{[0-9A-Fa-f]+\}|\\N\{[A-Za-z0-9 _-]+\})(?:"
                  + IDENTIFIER_CHAR + DELIMITED_UCN + ")*")


def number_re(separators: bool, delimited: bool = False) -> str:
    """A preprocessing number: an ASCII digit, after an optional `.`, then
    digits, letters, `_`, `.`, an exponent sign after e, E, p or P, `$`,
    universal character names and characters of EXTENDED_RANGES, as GCC's
    lex_number reads it; with digit separators (C23, C++14) also a `'`
    before a digit, letter or `_`; with @p delimited also delimited and
    named universal character names.  It starts at an ASCII digit after no
    letter, digit, `_`, `$` or character of EXTENDED_RANGES; a decimal digit
    outside ASCII starts an identifier, as in GCC.  A `'` before a `.`, a
    `$`, a universal character name or a character outside ASCII ends the
    number, as GCC lexes it: `1'.x'` is `1` and the character literal
    `'.x'`, and in C23 `1$'a'` is the number `1$'a`, then a `'` that opens
    a character literal."""
    sep = "'?" if separators else ""
    return (r"(?<!" + ID_CHAR + r")\.?[0-9](?:[eEpP][+-]|" + sep
            + r"[A-Za-z0-9_]|\.|"
            + IDENTIFIER_CHAR + (DELIMITED_UCN if delimited else "") + ")*")


# The literal rules of the languages a module file is compiled as, as (raw
# strings, digit separators, delimited and named universal character names
# in a number and an identifier): ISO C11 and C17 have none, C++11 has
# raw strings, gnu11, gnu17 and gnu++11 have raw strings and delimited
# names, C23 has digit separators, C++14 to C++20 have raw strings and
# digit separators, and gnu23, C++23 and gnu++14 on have all three.  A file
# is read under each and the includes found are joined, so an include that
# one of them leaves outside a comment is found.
LITERAL_VIEWS = ((False, False, False), (True, False, False),
                 (True, False, True), (False, True, False),
                 (True, True, False), (True, True, True))


def source_token_re(raw: bool, separators: bool,
                    delimited: bool = False) -> re.Pattern:
    """One token that the comment remover keeps or drops: the operand of an
    include directive, and string, character and raw string literals and
    numbers, are kept whole, so a `//` or `/*` inside them starts no comment
    and a digit separator opens no character literal."""
    return re.compile(
        "(?P<directive>" + DIRECTIVE + WS + r'*(?:<[^>\n]*>|"[^"\n]*"))'
        + ("|(?P<raw>" + RAW_OPEN_RE + ")" if raw else "")
        + ("|(?P<word>" + DELIMITED_WORD + ")" if delimited else "")
        + "|(?P<literal>" + number_re(separators, delimited) + "|"
        + STRING_LITERAL + "|" + CHAR_LITERAL + ")"
        r"|(?P<comment>/\*.*?\*/|//[^\n]*)", re.M | re.S)


SOURCE_TOKEN_RES = tuple(source_token_re(*view) for view in LITERAL_VIEWS)
OPERAND_RE = re.compile(r'^(?:<([^>\n]*)>|"([^"\n]*)")\s*$')
# The name `__has_include` or `__has_include_next` anywhere outside a
# literal: a call, and also a macro that stands for it (`#define P
# __has_include`, then `#if P("x.h")`), which GCC evaluates as the call.
HAS_INCLUDE_RE = re.compile(r"(?<!" + ID_CHAR + r")__has_include(?:_next)?(?!"
                            + ID_CHAR + ")")
# What tests the name as a macro and includes nothing: `#ifdef`, `#ifndef`,
# `#elifdef` and `#elifndef` before it on its line, and a `defined NAME` or
# `defined(NAME)` term of an `#if` or `#elif` line whose other text is only
# such terms, white space, `(`, `)`, `!`, `&` and `|`.  The line holds no
# other identifier, so no macro on it takes `defined` into an argument or
# pastes it to another name, and no `$` or other identifier character joins
# it to the name before it.  The line is the line GCC reads: a comment or
# raw string literal across a line break continues it, so the body of a
# `#define` stays one line also where a comment cuts it
# (`DROP( /*` newline `*/ #if ) defined __has_include`), and an `#if`
# after a comment that starts its line and ends on the next is a directive
# (`/*` newline `*/ #if defined(__has_include)`).
IF_LINE_RE = re.compile(r"^" + WS + r"*(?:#|%:)" + WS + r"*(?:el)?if\b(.*)$")
DEFINED_TERM_RE = re.compile(
    r"\bdefined" + WS + r"*(?:\(" + WS + r"*[A-Za-z_]\w*" + WS + r"*\)"
    + r"|" + WS + r"+[A-Za-z_]\w*)")
LOGIC_ONLY_RE = re.compile(r"[ \t\f\v()!&|]*")
IFDEF_BEFORE_RE = re.compile(r"^" + WS + r"*(?:#|%:)" + WS
                             + r"*(?:ifdef|ifndef|elifdef|elifndef)" + WS
                             + r"+\Z")
# A line continuation: a backslash, then spaces, tabs, form feeds or
# vertical tabs, then a newline, as GCC and Clang join it.
CONTINUATION_RE = re.compile(r"\\[ \t\f\v]*\r?\n")
# A line end: CRLF, LF and a lone CR, which GCC reads as a newline.
NEWLINE_RE = re.compile(r"\r\n?")
# pioasm 2.3.0 (pico-sdk tools/pioasm/lexer.ll) outside a code block: a `;`
# or `//` comment to the line end, a `/* */` comment, the rest of a
# `.lang_opt` line, and the start of a code block, `%`, a name of anything
# but `%` and newline, up to the last `{` of the line.  The name is trimmed
# of spaces and tabs; an empty one is c-sdk.  A block ends at a line of `%}`
# in column 0 followed by spaces, tabs and carriage returns only.
PIO_BLOCK_RE = re.compile(r"%([^%\n]+)\{")
PIO_BLOCK_END_RE = re.compile(r"%\}[ \t\r]*")
PIO_DIRECTIVE_RE = re.compile(r"\.(lang_opt|program)(?![A-Za-z0-9_])", re.I)
C_COMMENT_RE = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)
SOURCE_SUFFIXES = (".c", ".h", ".pio")

# Brand names that a module README names; a README that names one carries a
# section whose heading has "Trademark" in it.
BRANDS = ("Futaba", "S.BUS", "KST", "Chaservo", "Hitec", "YGE", "OpenYGE",
          "WeAct", "DShot", "Hobbywing", "Spektrum", "Graupner", "Jeti",
          "FrSky", "FlySky")
# A brand is found in any case, with a version suffix (DShot300, S.BUS2),
# inside an identifier, file name, path or address (kst_session_t,
# hitec_x.c, www.futaba.com, 2kst), and not next to a letter: KSTART is no
# brand, and neither is a brand joined to a word without a separator, as
# in the CamelCase identifier KstSession.
BRAND_RE = re.compile(r"(?<![A-Za-z])(?:%s)(?![A-Za-z])"
                      % "|".join(re.escape(b) for b in BRANDS), re.I)
HTML_COMMENT_RE = re.compile(r"<!--.*?(?:-->|\Z)", re.S)
EMPHASIS_RE = re.compile(r"[*_`]")
LINK_RE = re.compile(r"\[([^\]]*)\]\([^)]*\)")
# An ATX heading's text: an optional closing sequence of `#` is no part of it.
HEADING_RE = re.compile(
    r"^[ ]{0,3}#{1,6}[ \t]+(.*?)(?:[ \t]+#+)?[ \t]*$", re.M)
FENCE_RE = re.compile(r"^[ ]{0,3}(`{3,}|~{3,})")
# A backtick fence takes no backtick in its info string (CommonMark).
BACKTICK_FENCE_RE = re.compile(r"^[ ]{0,3}`{3,}[^`\n]*$")
NO_DRIVER_RE = re.compile(r"\bno\s+pin\s+driver\b", re.I)

CMAKE_COMMENT_RE = re.compile(r"#[^\n]*")
REQUIRES_RE = re.compile(r"\b(?:PRIV_)?REQUIRES\b")
LIBRARY_RE = re.compile(r"\brcbench_\w+")

# The caller-side `_Static_assert` files of each module: the files outside
# protocols/ with an assertion that names a macro or enumerator of the
# module's public headers.  Paths are relative to the repository root.
ASSERTED = {
    "dshot": {
        "firmware/iomcu/src/outputs_hw.c": (
            "OUT_DSHOT_MAX_PINS >= OUT_MAX_SLOTS",
        ),
    },
    "kst": {
        "shared/bench/kst_link.c": (
            "KST_REG_COUNT == 2u * LINK_KS_IMAGE_REGS",
        ),
        "shared/outputs/kst_port.c": (
            "KST_SES_OK == 0 && KST_SES_BUSY == 1",
            "KST_SES_ERR_NO_SERVO == 4 && KST_SES_ERR_NOT_IN_MODE == 5 "
            "&& KST_SES_ERR_READ == 6 && KST_SES_ERR_NO_BACKUP == 7 "
            "&& KST_SES_ERR_FINGERPRINT == 8 && KST_SES_ERR_LOCKED == 9 "
            "&& KST_SES_ERR_BAD_PLAN == 10 "
            "&& KST_SES_ERR_UNCONFIRMED == 11 "
            "&& KST_SES_ERR_CHANGED == 12 "
            "&& KST_SES_ERR_WRITE_FAILED == 13 "
            "&& KST_SES_ERR_ROLLED_BACK == 14 && KST_SES_ERR_TORN == 15 "
            "&& KST_SES_ERR_UNINTENDED == 16 && KST_SES_ERR_VERIFY == 17 "
            "&& KST_SES_ERR_DRIVER == 18 && KST_SES_ERR_ABORTED == 19",
            "KST_F_COUNT <= 64",
            "KST_REG_COUNT == 2u * LINK_KS_IMAGE_REGS",
            "LINK_KST_RESULT_SUPPLY > KST_SES_ERR_NO_PLAN",
        ),
    },
    "ppm": {
        "firmware/iomcu/src/outputs_hw.c": (
            "OUT_PPM_MAX_PINS >= OUT_MAX_SLOTS",
            "PPM_CHANNEL_MIN_US == OUT_FLOOR_US "
            "&& PPM_CHANNEL_MAX_US == OUT_CEILING_US",
        ),
    },
}
CALLER_DIRS = ("shared", "firmware")
STATIC_ASSERT_RE = re.compile(
    r"\b(?:_Static_assert|static_assert)\s*\((.*?)\)\s*;", re.S)
MESSAGE_RE = re.compile(r',\s*"[^"]*"(?:\s*"[^"]*")*\s*$')
MACRO_RE = re.compile(r"^[ \t]*#[ \t]*define[ \t]+([A-Za-z_]\w*)", re.M)
ENUM_BODY_RE = re.compile(r"\benum\b[^{;]*\{(.*?)\}", re.S)
ENUMERATOR_RE = re.compile(r"(?:^|,)\s*([A-Za-z_]\w*)\s*(?:=[^,]*)?", re.S)

WARNINGS = ("-Wall", "-Wextra", "-Werror")
LANGUAGES = (("C11", "c", "-std=c11"), ("C++17", "c++", "-std=c++17"))


def join_lines(text: str) -> tuple[str, list[int], list[int], list[int]]:
    """@p text with its line continuations joined; for each continuation
    removed, its index in @p text, its index in the joined text, and the
    number of characters removed up to and including it."""
    starts, at, removed, total, out, last = [], [], [], 0, [], 0
    for m in CONTINUATION_RE.finditer(text):
        out.append(text[last:m.start()])
        starts.append(m.start())
        at.append(m.start() - total)
        total += m.end() - m.start()
        removed.append(total)
        last = m.end()
    out.append(text[last:])
    return "".join(out), starts, at, removed


def blank_source(text: str, token_re: re.Pattern) -> tuple[str, str]:
    """@p text with line continuations joined and comments blanked, read
    with the literal rules of @p token_re: once with literals left whole and
    the newlines of comments and raw string literals kept, so an include
    operand stays on its line, and once with each literal and include
    operand replaced by `""` and each comment by spaces, without the
    newlines inside them, for the search of `__has_include`.  The second
    text is split into lines as GCC reads a directive: a comment or raw
    string literal across a line break continues the line.  A raw string
    literal is matched on @p text as written, from its `"` on, because a
    line continuation inside it is not joined."""
    joined, starts, at, removed = join_lines(text)

    def to_text(j: int) -> int:
        k = bisect.bisect_right(at, j)
        return j + (removed[k - 1] if k else 0)

    def to_joined(t: int) -> int:
        k = bisect.bisect_left(starts, t)
        return t - (removed[k - 1] if k else 0)

    kept, bare, pos = [], [], 0
    while True:
        m = token_re.search(joined, pos)
        if m is None:
            break
        kept.append(joined[pos:m.start()])
        bare.append(joined[pos:m.start()])
        end, token = m.end(), m.group(0)
        if m.lastgroup == "raw":
            body = RAW_BODY_RE.match(text, to_text(m.end()))
            if body is None:
                # No end: no raw string here, `R` is an identifier.
                kept.append(token)
                bare.append(token)
                pos = end
                continue
            end = to_joined(body.end())
            token = joined[m.start():end]
        if m.lastgroup == "comment":
            blank = re.sub(r"[^\n]", " ", token)
            kept.append(blank)
            bare.append(blank.replace("\n", " "))
        elif m.lastgroup == "word":
            kept.append(token)
            bare.append(token)
        elif m.lastgroup == "directive":
            kept.append(token)
            bare.append(re.sub(r"[^\n]", " ", token))
        else:
            kept.append(token)
            bare.append('""')
        pos = end
    kept.append(joined[pos:])
    bare.append(joined[pos:])
    return "".join(kept), "".join(bare)


def pio_c_code(text: str) -> str:
    """The C that pioasm writes from the .pio @p text into the header it
    generates with `-o c-sdk`: the lines of each `% c-sdk {` block after the
    first `.program`, in order, without its blank lines, each block ended
    by a newline.  The code blocks are found as pioasm 2.3.0 lexes the file
    (PIO_BLOCK_RE): a `;` inside a `/* */` comment ends nothing, a `/*` in a
    `;` comment opens nothing, and a block of another language and a block
    before the first program are not written."""
    blocks, program, i, n = [], False, 0, len(text)
    while i < n:
        if text.startswith((";", "//"), i):
            i = text.find("\n", i)
        elif text.startswith("/*", i):
            i = text.find("*/", i + 2)
            i = -1 if i < 0 else i + 2
        elif text[i] == "%" and PIO_BLOCK_RE.match(text, i):
            start = PIO_BLOCK_RE.match(text, i)
            name = start.group(1).strip(" \t") or "c-sdk"
            lines, i = [], start.end()
            while i <= n:
                eol = text.find("\n", i)
                eol = n if eol < 0 else eol
                line = text[i:eol]
                if PIO_BLOCK_END_RE.fullmatch(line):
                    break
                if line.strip(" \t\r"):
                    lines.append(line + "\n")
                i = eol + 1
            else:
                eol = n
            i = eol
            if program and name == "c-sdk":
                blocks.append("".join(lines) + "\n")
        elif text[i] == "." and PIO_DIRECTIVE_RE.match(text, i):
            word = PIO_DIRECTIVE_RE.match(text, i)
            if word.group(1).lower() == "lang_opt":
                i = text.find("\n", i)
            else:
                program = True
                i = word.end()
        else:
            i += 1
        if i < 0:
            break
    return "".join(blocks)


def view_includes(text: str, token_re: re.Pattern) -> list[tuple[str, str]]:
    """includes() of @p text under the literal rules of @p token_re."""
    kept, bare = blank_source(text, token_re)
    found = []
    for line in kept.split("\n"):
        if BARE_DIRECTIVE_RE.match(line):
            found.append(("#", line.strip()))
            continue
        hit = INCLUDE_RE.match(line)
        if hit:
            operand = hit.group(2).strip()
            op = OPERAND_RE.match(operand)
            if op and hit.group(1) == "include_next":
                found.append(("next", operand))
            elif op:
                found.append(("<", op.group(1)) if op.group(1) is not None
                             else ('"', op.group(2)))
            else:
                found.append(("?", operand))
    for use in HAS_INCLUDE_RE.finditer(bare):
        if not tested_as_macro(bare, use.start()):
            found.append(("?", use.group(0)))
    return found


def tested_as_macro(bare: str, start: int) -> bool:
    """True when the name `__has_include` at @p start of @p bare (the text
    with comments blanked and literals replaced by `""`) is the operand of
    `#ifdef`, `#ifndef`, `#elifdef` or `#elifndef`, or of a `defined` term
    on an `#if` or `#elif` line that holds nothing but such terms, white
    space, `(`, `)`, `!`, `&` and `|`."""
    line_start = bare.rfind("\n", 0, start) + 1
    if IFDEF_BEFORE_RE.match(bare[line_start:start]):
        return True
    line_end = bare.find("\n", start)
    line = bare[line_start:None if line_end < 0 else line_end]
    cond = IF_LINE_RE.match(line)
    if cond is None:
        return False
    at = start - line_start
    if not any(term.start() < at < term.end()
               for term in DEFINED_TERM_RE.finditer(line)):
        return False
    rest = HAS_INCLUDE_RE.sub(" ", DEFINED_TERM_RE.sub(" ", cond.group(1)))
    return LOGIC_ONLY_RE.fullmatch(rest) is not None


def includes(text: str, pio: bool = False) -> list[tuple[str, str]]:
    """(form, name) for every include directive of @p text outside a C
    comment: form is `<` or `"`, or `?` when the operand is neither (a
    macro) and name is the operand as written; for `#include_next` with a
    `<...>` or `"..."` operand, form is `next` and name is the operand with
    its delimiters.  A line of only `#` or `%:`
    is form `#`, each trigraph of the C is form `??` with the trigraph as
    name, a NUL byte anywhere in @p text is form `NUL` with the number of
    the line of the first one (CRLF, LF and a lone CR end a line), and the
    name `__has_include` or `__has_include_next` outside a literal, except
    as the operand of `#ifdef`, `#ifndef`, `#elifdef` and `#elifndef` and
    of a `defined` term on an `#if` or `#elif` line of nothing but such
    terms and `( ) ! & |` (tested_as_macro()), is form `?`.  The text is
    read under each set of literal rules in LITERAL_VIEWS, and an entry
    found under any of them is listed, in the order of the first view that
    finds it, as often as the view that finds it most often.  @p pio is
    true for the text of a .pio file, whose C is read as pioasm writes it
    into the generated header (pio_c_code()).  CRLF and a lone CR end a
    line of C, after pio_c_code(), because pioasm keeps a CR as a
    character of the line."""
    found: list[tuple[str, str]] = []
    if "\0" in text:
        before = NEWLINE_RE.sub("\n", text[:text.index("\0")])
        found.append(("NUL", str(before.count("\n") + 1)))
    if pio:
        text = pio_c_code(text)
    text = NEWLINE_RE.sub("\n", text)
    for token_re in SOURCE_TOKEN_RES:
        seen: dict[tuple[str, str], int] = {}
        for entry in view_includes(text, token_re):
            seen[entry] = seen.get(entry, 0) + 1
            if seen[entry] > found.count(entry):
                found.append(entry)
    found.extend(("??", tri) for tri in TRIGRAPH_RE.findall(text))
    return found


def source_includes(path: pathlib.Path) -> list[tuple[str, str]]:
    """includes() of the file at @p path: its bytes as UTF-8 with an
    optional byte order mark, a .pio file read as pioasm reads it."""
    return includes(path.read_bytes().decode("utf-8-sig"),
                    path.suffix == ".pio")


def modules(root: pathlib.Path = PROTOCOLS) -> list[pathlib.Path]:
    if not root.is_dir():
        return []
    return sorted(p for p in root.iterdir() if p.is_dir())


def is_driver(path: pathlib.Path, module: pathlib.Path) -> bool:
    return DRIVER in path.relative_to(module).parts


def sources(module: pathlib.Path) -> list[pathlib.Path]:
    """Every C file, header and PIO program of @p module."""
    return sorted(p for p in module.rglob("*")
                  if p.is_file() and p.suffix in SOURCE_SUFFIXES)


def public_headers(module: pathlib.Path) -> list[pathlib.Path]:
    """The headers another project includes: the core's under include/ and
    the driver's under rp2350/."""
    return sorted([*(module / "include").glob("*.h"),
                   *(module / DRIVER).glob("*.h")])


def include_problems(module: pathlib.Path) -> list[str]:
    """What @p module includes that it may not, one line per include."""
    files = sources(module)
    core = {p.name for p in files
            if p.suffix == ".h" and not is_driver(p, module)}
    driver = {p.name for p in files
              if p.suffix == ".h" and is_driver(p, module)}
    generated = {p.name + ".h" for p in files if p.suffix == ".pio"}
    problems = []
    for path in files:
        rel = path.relative_to(module.parent).as_posix()
        in_driver = is_driver(path, module)
        for form, name in source_includes(path):
            if form == "#":
                problems.append(
                    f"{rel} has a line of only {name}: a comment across "
                    "a line break can hide the include that follows")
                continue
            if form == "??":
                problems.append(
                    f"{rel} has the trigraph {name}, which ISO C11 and C17 "
                    "replace and the check does not read")
                continue
            if form == "NUL":
                problems.append(
                    f"{rel} has a NUL byte on line {name}, which GCC reads "
                    "as white space and the check does not read")
                continue
            if form == "?":
                problems.append(
                    f"{rel} includes {name}: an include the check cannot "
                    "resolve (a macro or __has_include)")
                continue
            if form == "next":
                inner = name[1:-1]
                if not (inner in C_STANDARD or (
                        in_driver and SDK_RE.match(inner) is not None)):
                    problems.append(
                        f"{rel} has #include_next {name}: it includes the "
                        "next file of that name on the search path, outside "
                        "the module")
                    continue
                form, name = name[0], inner
            if name in C_STANDARD:
                continue
            parts = re.split(r"[/\\]", name)
            if ".." in parts or name.startswith(("/", "\\")):
                problems.append(
                    f"{rel} includes {name}: a path with a .. component "
                    "or an absolute path")
                continue
            if form == '"' and name in core:
                continue
            sdk = SDK_RE.match(name) is not None
            if in_driver and path.suffix == ".h" and sdk:
                problems.append(
                    f"{rel} includes {name}: a driver's header is read "
                    "without the pico-sdk")
                continue
            if in_driver and (sdk or (form == '"' and name in driver)
                              or (form == '"' and name in generated)):
                continue
            if not in_driver and form == '"' and name in driver:
                problems.append(
                    f"{rel} includes {name}: a core file does not depend "
                    f"on the pin driver in {DRIVER}/")
                continue
            if not in_driver and sdk:
                problems.append(
                    f"{rel} includes {name}: a pico-sdk header belongs in "
                    f"{DRIVER}/")
                continue
            problems.append(
                f"{rel} includes {name}, which is neither a C standard "
                f"header nor a header of {module.name}/")
    return problems


def layout_problems(module: pathlib.Path) -> list[str]:
    """What @p module lacks of a module's parts."""
    name = module.relative_to(module.parent.parent).as_posix()
    problems = []
    for part in ("README.md", "CMakeLists.txt"):
        if not (module / part).is_file():
            problems.append(f"{name}/ has no {part}")
    if not list((module / "include").glob("*.h")):
        problems.append(f"{name}/ has no header under include/")
    if not [p for p in module.rglob("*.c") if not is_driver(p, module)]:
        problems.append(f"{name}/ has no C file outside {DRIVER}/")
    if (module / DRIVER).is_dir():
        for suffix, what in ((".pio", "PIO program"), (".c", "C file"),
                             (".h", "header")):
            if not list((module / DRIVER).glob("*" + suffix)):
                problems.append(f"{name}/{DRIVER}/ has no {what}")
    return problems


def prose(text: str) -> str:
    """@p text without its HTML comments and fenced code blocks: a
    `# comment` in a CMake or shell example is no heading.  Comments go
    first, so a fence marker inside one opens nothing.  A fence closes on a
    fence of the same character, at least as long, or at the end of the
    text.  A backtick fence takes no backtick in its info string.

    Not supported: a `<!--` inside inline code or a fenced block drops the
    text up to the next `-->` or the end; a fence indented 4 or more spaces
    in a list item is not a fence here.  The first fails by demanding
    headings, the second counts a heading inside it."""
    text = HTML_COMMENT_RE.sub(
        lambda m: re.sub(r"[^\n]", "", m.group(0)), text)
    out, fence = [], ""
    for line in text.splitlines(keepends=True):
        m = FENCE_RE.match(line)
        if m and m.group(1)[0] == "`" and not fence \
                and not BACKTICK_FENCE_RE.match(line):
            m = None
        if fence:
            if (m and m.group(1)[0] == fence[0]
                    and len(m.group(1)) >= len(fence)
                    and not line[m.end():].strip()):
                fence = ""
        elif m:
            fence = m.group(1)
        else:
            out.append(line)
    return "".join(out)


def readme_problems(module: pathlib.Path) -> list[str]:
    """What the README of @p module lacks: an Interface heading, a
    heading with "Trademark" where a brand is named, and for a module without a
    pin driver the statement that there is none."""
    path = module / "README.md"
    if not path.is_file():
        return []
    text = path.read_text(encoding="utf-8")
    rel = path.relative_to(module.parent).as_posix()
    shown = prose(text)
    headings = [EMPHASIS_RE.sub("", LINK_RE.sub(r"\1", h)).strip().lower()
                for h in HEADING_RE.findall(shown)]
    problems = []
    if not any("interface" in h for h in headings):
        problems.append(f'{rel} has no heading with "Interface"')
    brands = sorted(set(BRAND_RE.findall(text)))
    if brands and not any("trademark" in h for h in headings):
        problems.append(f'{rel} names {", ".join(brands)} and has no '
                        'heading with "Trademark"')
    if not (module / DRIVER).is_dir() and not NO_DRIVER_RE.search(shown):
        problems.append(f'{rel} does not say "no pin driver": the module '
                        f"has no {DRIVER}/ folder")
    return problems


def cmake_problems(module: pathlib.Path) -> list[str]:
    """What the module's CMake files take from outside the module."""
    problems = []
    for path in sorted(module.rglob("CMakeLists.txt")):
        rel = path.relative_to(module.parent).as_posix()
        text = CMAKE_COMMENT_RE.sub("", path.read_text(encoding="utf-8"))
        if REQUIRES_RE.search(text):
            problems.append(f"{rel} carries REQUIRES: a module needs no "
                            "other component")
        own = "rcbench_" + module.name
        for lib in sorted(set(LIBRARY_RE.findall(text))):
            if lib != own and not lib.startswith(own + "_"):
                problems.append(f"{rel} names {lib}, a library of another "
                                "module")
    return problems


def header_problems(module: pathlib.Path, cc: str, cxx: str) -> list[str]:
    """Every public header of @p module that lacks `extern "C"` or does not
    compile by itself, as C and as C++."""
    problems = []
    dirs = [d for d in (module / "include", module / DRIVER) if d.is_dir()]
    flags = [arg for d in dirs for arg in ("-I", str(d))]
    with tempfile.TemporaryDirectory(prefix="rcbench-proto-") as tmp:
        for header in public_headers(module):
            rel = header.relative_to(module.parent).as_posix()
            if 'extern "C"' not in header.read_text(encoding="utf-8"):
                problems.append(f'{rel} has no extern "C" guard')
            # Twice: a header without an include guard fails here.
            unit = '#include "%s"\n#include "%s"\n' % (header.name,
                                                       header.name)
            for language, lang_flag, std in LANGUAGES:
                src = pathlib.Path(tmp) / "unit.txt"
                src.write_text(unit, encoding="utf-8")
                compiler = cc if lang_flag == "c" else cxx
                run = subprocess.run(
                    [compiler, "-x", lang_flag, std, "-fsyntax-only",
                     *WARNINGS, *flags, str(src)],
                    capture_output=True, text=True)
                if run.returncode != 0:
                    first = (run.stderr.strip().splitlines() or ["?"])
                    wanted = [line for line in first if "error" in line]
                    problems.append(
                        f"{rel} does not compile as {language}: "
                        + (wanted or first)[0].replace(tmp + "/", ""))
    return problems


def compilers() -> tuple[str, str]:
    cc = shutil.which("cc") or shutil.which("gcc")
    cxx = shutil.which("c++") or shutil.which("g++")
    if cc is None or cxx is None:
        sys.exit("a C and a C++ compiler are required on PATH")
    return cc, cxx


def public_names(module: pathlib.Path) -> set[str]:
    """The macros and enumerators the public headers of @p module define."""
    names: set[str] = set()
    for header in public_headers(module):
        text = C_COMMENT_RE.sub(" ", header.read_text())
        names.update(MACRO_RE.findall(text))
        for body in ENUM_BODY_RE.findall(text):
            names.update(ENUMERATOR_RE.findall(body))
    return names


def caller_files(repo: pathlib.Path) -> list[pathlib.Path]:
    """The C files and headers of the project outside protocols/ and outside
    build folders."""
    found = []
    for name in CALLER_DIRS:
        for path in (repo / name).rglob("*"):
            rel = path.relative_to(repo).parts
            if path.is_file() and path.suffix in (".c", ".h") and not any(
                    part.startswith(("build", "managed_components"))
                    for part in rel):
                found.append(path)
    return sorted(found)


def condition(body: str) -> str:
    """The condition of a static assertion's argument text: the message
    dropped, whitespace collapsed."""
    return " ".join(MESSAGE_RE.sub("", body).split())


def assertion_problems(root: pathlib.Path,
                       asserted: dict[str, dict[str, tuple[str, ...]]]
                       ) -> list[str]:
    """Where the callers' static assertions on a module's constants differ
    from @p asserted, per module, file and condition."""
    repo = root.parent
    names = {m.name: public_names(m) for m in modules(root)}
    seen: dict[str, dict[str, set[str]]] = {n: {} for n in names}
    for path in caller_files(repo):
        text = C_COMMENT_RE.sub(" ", path.read_text(errors="replace"))
        rel = path.relative_to(repo).as_posix()
        for body in STATIC_ASSERT_RE.findall(text):
            words = set(re.findall(r"[A-Za-z_]\w*", body))
            for module, own in names.items():
                if words & own:
                    seen[module].setdefault(rel, set()).add(condition(body))
    problems = []
    for module in sorted(names):
        want = {f: set(c) for f, c in asserted.get(module, {}).items()}
        for rel in sorted(set(seen[module]) | set(want)):
            have = seen[module].get(rel, set())
            for cond in sorted(have - want.get(rel, set())):
                problems.append(
                    "%s: %s holds a static assertion on the module's "
                    "constants that ASSERTED in tools/check_protocols.py "
                    "does not list: %s" % (module, rel, cond))
            for cond in sorted(want.get(rel, set()) - have):
                problems.append(
                    "%s: %s is listed in ASSERTED and has no static "
                    "assertion of: %s" % (module, rel, cond))
    return problems


def check(root: pathlib.Path = PROTOCOLS) -> list[str]:
    """Every broken rule under @p root, one line each."""
    cc, cxx = compilers()
    problems = []
    for module in modules(root):
        problems += layout_problems(module)
        problems += include_problems(module)
        problems += readme_problems(module)
        problems += cmake_problems(module)
        problems += header_problems(module, cc, cxx)
    problems += assertion_problems(root, ASSERTED if root == PROTOCOLS
                                   else {})
    return problems


def describe(root: pathlib.Path = PROTOCOLS) -> str:
    """One line per module: its core files, driver files and headers."""
    lines = []
    for module in modules(root):
        files = sources(module)
        drv = [p for p in files if is_driver(p, module)]
        lines.append("%-12s %2d core files, %d driver files, %d public "
                     "headers" % (module.name + "/", len(files) - len(drv),
                                  len(drv), len(public_headers(module))))
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    args = sys.argv[1:] if argv is None else argv
    if args not in ([], ["--check"]):
        sys.exit(__doc__)
    found = modules(PROTOCOLS)
    if not found:
        sys.exit(f"no module under {PROTOCOLS}; the check cannot pass")
    if not args:
        print(describe(PROTOCOLS))
        print()
    problems = check(PROTOCOLS)
    for line in problems:
        print(line)
    if problems:
        print(f"\n{len(problems)} broken rule(s) under protocols/",
              file=sys.stderr)
        return 1
    print(f"{len(found)} modules under protocols/ hold the rule")
    return 0


if __name__ == "__main__":
    sys.exit(main())
