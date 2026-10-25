#!/usr/bin/env python3
"""Reads the tables of a C source as written.

String literals, brace lists, designated initialisers and the comments
between their entries, and macros expanded with the C preprocessor.
tools/merge_texts.py reads the interface's words with it.

SPDX-License-Identifier: MIT
"""

from __future__ import annotations

import re
import shutil
import subprocess


class TableError(Exception):
    """A source is not in a shape this module reads."""


def skip_space(s: str, i: int) -> int:
    """Skips white space, comments and backslash-newlines.

    Args:
        s: The C source.
        i: The index to start at.

    Returns:
        The index of the first character after them.

    Raises:
        TableError: A comment is not closed.
    """
    n = len(s)
    while i < n:
        if s[i] in " \t\r\n":
            i += 1
        elif s.startswith("\\\n", i):
            i += 2
        elif s.startswith("/*", i):
            end = s.find("*/", i + 2)
            if end < 0:
                raise TableError("an unterminated comment")
            i = end + 2
        elif s.startswith("//", i):
            end = s.find("\n", i)
            i = n if end < 0 else end
        else:
            break
    return i


def string_end(s: str, i: int) -> int:
    """Finds the end of a string literal.

    Args:
        s: The C source.
        i: The index of the literal's opening quote.

    Returns:
        The index after its closing quote.

    Raises:
        TableError: The literal ends at a line break or at the end of s.
    """
    j = i + 1
    while j < len(s):
        if s[j] == "\\":
            j += 2
            continue
        if s[j] == '"':
            return j + 1
        if s[j] == "\n":
            break
        j += 1
    raise TableError("an unterminated string literal")


def literal_run(s: str, i: int) -> tuple[list[str], int]:
    """Reads adjacent string literals as written.

    Comments and line breaks between them are allowed.

    Args:
        s: The C source.
        i: The index to start at; white space before the first is skipped.

    Returns:
        The literals, quotes included, and the index after the last one; no
        literals and i when none starts there.
    """
    parts: list[str] = []
    j = skip_space(s, i)
    while j < len(s) and s[j] == '"':
        end = string_end(s, j)
        parts.append(s[j:end])
        i = end
        j = skip_space(s, end)
    return parts, i


def code_mask(s: str) -> list[bool]:
    """Marks which characters of a C source are code.

    Args:
        s: The C source.

    Returns:
        For every index, False inside a string, a character constant or a
        comment and True elsewhere.
    """
    mask = [True] * len(s)
    i = 0
    n = len(s)
    while i < n:
        if s.startswith("/*", i):
            end = s.find("*/", i + 2)
            end = n if end < 0 else end + 2
        elif s.startswith("//", i):
            end = s.find("\n", i)
            end = n if end < 0 else end
        elif s[i] == '"':
            end = string_end(s, i)
        elif s[i] == "'":
            end = i + 1
            while end < n and s[end] != "'":
                end += 2 if s[end] == "\\" else 1
            end += 1
        else:
            i += 1
            continue
        for k in range(i, min(end, n)):
            mask[k] = False
        i = end
    return mask


def matching(s: str, mask: list[bool], i: int) -> int:
    """Finds the bracket that closes another.

    Args:
        s: The C source.
        mask: code_mask(s).
        i: The index of an opening (, [ or {.

    Returns:
        The index of its closing bracket.

    Raises:
        TableError: The brackets do not pair up.
    """
    pairs = {"{": "}", "(": ")", "[": "]"}
    stack = [pairs[s[i]]]
    j = i + 1
    while j < len(s):
        c = s[j]
        if mask[j]:
            if c in pairs:
                stack.append(pairs[c])
            elif c in ")]}":
                if c != stack.pop():
                    raise TableError("unbalanced brackets")
                if not stack:
                    return j
        j += 1
    raise TableError("an unclosed bracket")


def elements(s: str, mask: list[bool], open_at: int) -> list[tuple[int, int]]:
    """Splits a brace list at its top-level commas.

    Args:
        s: The C source.
        mask: code_mask(s).
        open_at: The index of the list's opening brace.

    Returns:
        The (start, end) of every element, without the comma after it.

    Raises:
        TableError: The brackets do not pair up.
    """
    close = matching(s, mask, open_at)
    out = []
    start = open_at + 1
    depth = 0
    for j in range(open_at + 1, close):
        if not mask[j]:
            continue
        c = s[j]
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        elif c == "," and depth == 0:
            out.append((start, j))
            start = j + 1
    if s[start:close].strip(" \t\r\n\\"):
        out.append((start, close))
    return out


def table_body(src: str, name: str) -> tuple[int, int]:
    """Finds the initialiser of an array.

    Args:
        src: The C source.
        name: The array's name.

    Returns:
        The (start, end) of the text between its braces.

    Raises:
        TableError: No such array, or its braces do not pair up.
    """
    m = re.search(rf"\b{re.escape(name)}\s*\[[^\]]*\]\s*=\s*\{{", src)
    if m is None:
        raise TableError(f"no array {name}")
    open_at = m.end() - 1
    return open_at + 1, matching(src, code_mask(src), open_at)


def join(parts: list[str]) -> list[str]:
    """Writes adjacent literals as one where that is safe.

    Args:
        parts: The literals, quotes included.

    Returns:
        One literal with their contents, or parts as they are when one holds
        a backslash, where an escape could run across a joint.
    """
    if len(parts) > 1 and not any("\\" in p for p in parts):
        return ['"' + "".join(p[1:-1] for p in parts) + '"']
    return parts


# -------------------------------------------------------- table entries


def entries(body: str) -> list[tuple]:
    """Reads a designated initialiser's items in order.

    Args:
        body: The text between the initialiser's braces.

    Returns:
        ("comment", text), ("pp", line), ("entry", id, literals, comments)
        for `[ID] = "..."` with the comments before it, ("ref", id, name) for
        `[ID] = name` and ("macro", text) for anything else on a line of its
        own, such as a macro that writes entries.

    Raises:
        TableError: An entry is neither a string nor a name.
    """
    out: list[tuple] = []
    pending: list[str] = []
    i = 0
    n = len(body)
    while True:
        i = skip_ws_only(body, i)
        if i >= n:
            break
        if body.startswith("/*", i):
            end = body.index("*/", i) + 2
            pending.append(body[i:end])
            i = end
            continue
        if body[i] == "#":
            end = body.find("\n", i)
            end = n if end < 0 else end
            out.extend(("comment", c) for c in pending)
            pending = []
            out.append(("pp", body[i:end].strip()))
            i = end
            continue
        m = re.compile(r"\[\s*(\w+)\s*\]\s*=\s*").match(body, i)
        if m is not None:
            ident = m.group(1)
            j = m.end()
            if body[j] == '"':
                parts, j = literal_run(body, j)
                out.append(("entry", ident, parts, pending))
            else:
                r = re.compile(r"(\w+)").match(body, j)
                if r is None:
                    raise TableError(f"[{ident}] = is neither a string "
                                       "nor a name")
                out.extend(("comment", c) for c in pending)
                out.append(("ref", ident, r.group(1)))
                j = r.end()
            pending = []
            j = skip_space(body, j)
            if j < n and body[j] == ",":
                j += 1
            i = j
            continue
        end = body.find("\n", i)
        end = n if end < 0 else end
        out.extend(("comment", c) for c in pending)
        pending = []
        out.append(("macro", body[i:end].strip()))
        i = end
    out.extend(("comment", c) for c in pending)
    return out


def skip_ws_only(s: str, i: int) -> int:
    """Skips white space, not comments.

    Args:
        s: The text.
        i: The index to start at.

    Returns:
        The index of the first character that is no white space.
    """
    while i < len(s) and s[i] in " \t\r\n":
        i += 1
    return i


def dedent_comment(c: str) -> str:
    """Moves a comment to column 0, its later lines one space in.

    Args:
        c: The comment, as written.

    Returns:
        The comment with its indent taken out.
    """
    lines = c.split("\n")
    return "\n".join([lines[0].strip()] + [" " + ln.strip()
                                           for ln in lines[1:]])


def defines(src: str, skip: set[str]) -> str:
    """Collects the #define lines of a source.

    Args:
        src: The C source.
        skip: Names of macros to leave out.

    Returns:
        Every other #define, continuation lines included, one after another.
    """
    out = []
    for m in re.finditer(r"^#define\s+(\w+)(?:.*\\\n)*.*$", src, re.M):
        if m.group(1) not in skip:
            out.append(m.group(0))
    return "\n".join(out)


def preprocess(macros: str, text: str) -> str:
    """Expands macros with the C preprocessor.

    Args:
        macros: #define lines to expand by.
        text: The text to expand.

    Returns:
        text with every macro expanded.

    Raises:
        TableError: No C compiler is on PATH, or the preprocessor fails.
    """
    cc = shutil.which("cc") or shutil.which("gcc")
    if cc is None:
        raise TableError("no C compiler on PATH")
    run = subprocess.run([cc, "-E", "-P", "-x", "c", "-"],
                         input=macros + "\n@@BEGIN@@\n" + text,
                         capture_output=True, text=True, encoding="utf-8")
    if run.returncode != 0:
        raise TableError("the preprocessor failed: " + run.stderr)
    return run.stdout.split("@@BEGIN@@", 1)[1]


def array_items(body: str) -> list[list[str]]:
    """Reads a plain array's initialiser.

    Args:
        body: The text between the array's braces.

    Returns:
        The literals of each element, one run of adjacent literals each.

    Raises:
        TableError: An element is not a string.
    """
    out = []
    i = 0
    while True:
        parts, j = literal_run(body, i)
        if not parts:
            break
        out.append(parts)
        j = skip_space(body, j)
        if j < len(body) and body[j] == ",":
            j += 1
        i = j
    if body[i:].strip():
        raise TableError("an option array holds more than strings")
    return out
