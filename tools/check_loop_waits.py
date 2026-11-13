#!/usr/bin/env python3
"""Hold the IOmcu's sources to a main loop without busy waits or printf().

A DShot frame leaves the IOmcu from its main loop, every 1000us, and a pass
that waits moves the frame grid (docs/DShot.md).  This check fails when a C
file under firmware/iomcu/src holds

- `tight_loop_contents()` inside a `while` or `do` loop, or
- a call of `printf()`, or of a macro defined as `printf`,

in a place the table ALLOWED below does not name.  An entry of the table that
matches nothing fails as well, so the table names only what is in the tree.

The reading is textual: comments are dropped, string literals are blanked
except where a `printf()`'s first literal is matched, and a function is a
`{` at file level after a `)`.  A loop is a `while (...)` or `do` before an
enclosing `{`, or before the call in the same statement.

    tools/check_loop_waits.py            list the findings
    tools/check_loop_waits.py --check    the same; exit 1 on any finding

SPDX-License-Identifier: MIT
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "firmware" / "iomcu" / "src"

# (file, function, kind, first literal's start or None, reason).  kind is
# "wait" for tight_loop_contents() in a loop, "printf" for a printf() call.
ALLOWED = (
    ("sense_core1.c", "core1_main", "wait", None,
     "core 1's sensor tick, paced on the timer; not the main loop"),
    ("sense_core1.c", "sense_core1_start", "wait", None,
     "boot, before the main loop; SENSE_CORE1_START_MS (100ms) at most"),
    ("tone_core1.c", "tone_core1_quiesce", "wait", None,
     "a TONE rewire waits for core 1's acknowledgement; TONE_CORE1_WAIT_US "
     "(5000us) at most, on a page write, not every pass"),
    ("iomcu_can.c", "can_start", "printf", "rcbench-iomcu: CAN %s at",
     "the boot line, before the main loop"),
    ("iomcu_report.c", "reset_start", "printf", "rcbench-iomcu: reset %s",
     "the boot line, before the main loop"),
    ("iomcu_report.c", "reset_start", "printf", "rcbench-iomcu: %s",
     "the boot line, before the main loop"),
    ("main.c", "main", "printf", "rcbench-iomcu: output store sector",
     "the store's lines; a flash window of about 19ms comes with each, and "
     "flash_gate_busy() holds it back while anything drives"),
    ("main.c", "main", "printf", "rcbench-iomcu: outputs saved",
     "the store's line; as above"),
    ("main.c", "main", "printf", "rcbench-iomcu: output store window",
     "the store's line; as above"),
)

LITERAL = re.compile(r'"(?:[^"\\\n]|\\.)*"')
DEFINE_PRINTF = re.compile(
    r"^[ \t]*#[ \t]*define[ \t]+(\w+)[ \t]+printf[ \t]*$", re.M)


def strip(text):
    """Drops comments and blanks string and character literals.

    Args:
        text: The C source.

    Returns:
        A pair: the source with comments and the insides of literals
        replaced by blanks of the same length (newlines kept), and the
        source with only the comments blanked.
    """
    out = []
    pos = 0
    # One pass, so a quote inside a comment and a comment marker inside a
    # literal are each read as what they are.
    token = re.compile(r'/\*.*?\*/|//[^\n]*|"(?:[^"\\\n]|\\.)*"'
                       r"|'(?:[^'\\\n]|\\.)*'", re.S)
    no_comments = []
    for m in token.finditer(text):
        out.append(text[pos:m.start()])
        no_comments.append(text[pos:m.start()])
        tok = m.group(0)
        blank = re.sub(r"[^\n]", " ", tok)
        if tok.startswith(("/*", "//")):
            out.append(blank)
            no_comments.append(blank)
        else:
            out.append(tok[0] + blank[1:-1] + tok[-1])
            no_comments.append(tok)
        pos = m.end()
    out.append(text[pos:])
    no_comments.append(text[pos:])
    return "".join(out), "".join(no_comments)


def match_back(code, close):
    """Finds the "(" that the ")" at @p close closes.

    Args:
        code: The stripped source.
        close: The index of a ")".

    Returns:
        The index of its "(", or -1.
    """
    depth = 0
    for i in range(close, -1, -1):
        if code[i] == ")":
            depth += 1
        elif code[i] == "(":
            depth -= 1
            if depth == 0:
                return i
    return -1


def head_of(code, brace):
    """Names what opens the block at @p brace.

    Args:
        code: The stripped source.
        brace: The index of a "{".

    Returns:
        "while", "do", "for", "if", "switch", "else", a function's name, or
        "" for anything else (an initializer, a struct).
    """
    before = code[:brace].rstrip()
    if re.search(r"\bdo$", before):
        return "do"
    if re.search(r"\belse$", before):
        return "else"
    if not before.endswith(")"):
        return ""
    open_ = match_back(code, len(before) - 1)
    if open_ < 0:
        return ""
    m = re.search(r"(\w+)\s*$", code[:open_])
    return m.group(1) if m else ""


def blocks(code):
    """Pairs every "{" with its "}".

    Args:
        code: The stripped source.

    Returns:
        A list of (open, close, head) for every block.
    """
    stack = []
    found = []
    for i, c in enumerate(code):
        if c == "{":
            stack.append(i)
        elif c == "}" and stack:
            o = stack.pop()
            found.append((o, i, head_of(code, o)))
    return found


def functions(code, all_blocks):
    """The function bodies at file level.

    Args:
        code: The stripped source.
        all_blocks: blocks(code).

    Returns:
        A list of (name, open, close).
    """
    out = []
    for o, c, head in all_blocks:
        depth = code[:o].count("{") - code[:o].count("}")
        if depth == 0 and head and head not in (
                "while", "do", "for", "if", "switch", "else"):
            out.append((head, o, c))
    return out


def in_loop(code, at, all_blocks, fn_open):
    """Whether position @p at sits inside a while or do loop.

    Args:
        code: The stripped source.
        at: The index of the call.
        all_blocks: blocks(code).
        fn_open: The "{" of the function around @p at.

    Returns:
        True inside a `while (...)` or `do` block, or right after a
        `while (...)` or `do` in the same statement.
    """
    for o, c, head in all_blocks:
        if o < at < c and o > fn_open and head in ("while", "do"):
            return True
    start = max(code.rfind(ch, 0, at) for ch in ";{}")
    return re.search(r"\b(while|do)\b", code[start + 1:at]) is not None


def findings(name, text):
    """Every wait and printf() call of one file.

    Args:
        name: The file's name, as ALLOWED names it.
        text: Its source.

    Returns:
        A list of (line, function, kind, first literal or None).
    """
    code, literals = strip(text)
    aliases = {"printf"} | set(DEFINE_PRINTF.findall(literals))
    all_blocks = blocks(code)
    fns = functions(code, all_blocks)
    out = []

    def where(at):
        for fname, o, c in fns:
            if o < at < c:
                return fname, o
        return "", -1

    for m in re.finditer(r"\btight_loop_contents\s*\(", code):
        fname, fo = where(m.start())
        if fname and in_loop(code, m.start(), all_blocks, fo):
            out.append((code.count("\n", 0, m.start()) + 1, fname, "wait",
                        None))
    call = re.compile(r"\b(" + "|".join(sorted(aliases)) + r")\s*\(")
    for m in call.finditer(code):
        # A #define line names the macro; it is not a call.
        line_start = code.rfind("\n", 0, m.start()) + 1
        if code[line_start:m.start()].lstrip().startswith("#"):
            continue
        fname, _ = where(m.start())
        rest = literals[m.end():]
        lit = LITERAL.match(literals, m.end() + len(rest) - len(rest.lstrip()))
        first = lit.group(0)[1:-1] if lit else None
        out.append((code.count("\n", 0, m.start()) + 1, fname, "printf",
                    first))
    return sorted(out)


def allowed(table, name, fname, kind, first):
    """The entry of the allow list that covers a finding.

    Args:
        table: The allow list.
        name: The file's name.
        fname: The function.
        kind: "wait" or "printf".
        first: The call's first literal, or None.

    Returns:
        The entry, or None.
    """
    for entry in table:
        f, fn, k, prefix, _ = entry
        if (f, fn, k) != (name, fname, kind):
            continue
        if prefix is None or (first is not None and first.startswith(prefix)):
            return entry
    return None


def check(src=SRC, table=ALLOWED):
    """Every finding the allow list does not cover, and every stale entry.

    Args:
        src: The directory of the sources.
        table: The allow list.

    Returns:
        A list of problems in words; empty when the tree passes.
    """
    problems = []
    used = set()
    for path in sorted(src.glob("*.c")):
        for line, fname, kind, first in findings(path.name,
                                                 path.read_text()):
            entry = allowed(table, path.name, fname, kind, first)
            if entry is not None:
                used.add(entry)
                continue
            what = ("tight_loop_contents() in a loop" if kind == "wait"
                    else f'printf() "{first or ""}"')
            problems.append(f"{path.name}:{line}: {what} in "
                            f"{fname or 'file scope'}()")
    for e in table:
        if e not in used:
            problems.append(f"ALLOWED: {e[0]} {e[1]}() {e[2]} "
                            f"{e[3] or ''} matches nothing")
    return problems


def main(argv):
    """Runs the check.

    Args:
        argv: The arguments; --check is accepted and changes nothing.

    Returns:
        0 when the tree passes, 1 otherwise.
    """
    if any(a not in ("--check",) for a in argv):
        print(__doc__.split("\n\n")[-2])
        return 2
    problems = check()
    for p in problems:
        print(p)
    if problems:
        print(f"{len(problems)} wait(s) in the IOmcu sources")
        return 1
    print("no wait in the IOmcu sources outside the allow list")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
