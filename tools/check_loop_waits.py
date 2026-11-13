#!/usr/bin/env python3
"""Hold the IOmcu's code to a main loop without busy waits or blocking output.

A DShot frame leaves the IOmcu from its main loop, every 1000us, and a pass
that waits moves the frame grid (docs/DShot.md).  This check reads the C
files of firmware/iomcu/src and of every shared/ and protocols/ directory
that firmware/iomcu/CMakeLists.txt adds (its rp2350/ folders included), and
fails on each of these in a place the table ALLOWED does not name:

- wait:  `tight_loop_contents()` inside a `while`, `do` or `for` loop;
- spin:  a `while` or `for` loop with an empty body (`while (x);`,
  `while (x) {}`), the tail of a `do` loop excepted;
- clock: a `while`, `do` or `for` loop that reads the microsecond clock
  (`time_us_32()`, `time_us_64()`, `get_absolute_time()`,
  `to_ms_since_boot()`, `to_us_since_boot()`) in its condition or body;
- sleep: a call of `busy_wait_*()` or `sleep_*()`;
- print: a call of `printf()`, `vprintf()`, `puts()`, `fputs()` or
  `putchar()`, or of a macro defined as one of them.

An entry names a file, a function, a kind and, for a print, the start of
the call's first string literal, or, for a spin or a clock loop, the start
of the loop's keyword and condition with white space dropped ("for(;;)").
An entry that matches nothing fails, so the table names only what is in
the tree.

The reading is textual: comments are dropped and string literals blanked,
a function is a `{` at file level after a `)`, and a loop is a `while`,
`do` or `for` before an enclosing `{` or before the statement.  A wait
behind any other macro, and code in a header, is not read.

    tools/check_loop_waits.py            list the findings
    tools/check_loop_waits.py --check    the same; exit 1 on any finding

SPDX-License-Identifier: MIT
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
IOMCU = ROOT / "firmware" / "iomcu"

LOOPS = ("while", "do", "for")
PRINTS = ("printf", "vprintf", "puts", "fputs", "putchar")
CLOCK = re.compile(r"\b(time_us_32|time_us_64|get_absolute_time"
                   r"|to_ms_since_boot|to_us_since_boot)\s*\(")
SLEEP = re.compile(r"\b(busy_wait_\w+|sleep_\w+)\s*\(")
LITERAL = re.compile(r'"(?:[^"\\\n]|\\.)*"')
DEFINE_PRINT = re.compile(
    r"^[ \t]*#[ \t]*define[ \t]+(\w+)[ \t]+(" + "|".join(PRINTS)
    + r")[ \t]*$", re.M)

_CORE1 = "core 1's code; the main loop runs on core 0"
_STORE = ("the store's line; a flash window of about 19ms comes before it, "
          "and flash_gate_busy() holds the window back while anything "
          "drives")
_CONSOLE = ("the bench console build's driver test (IOMCU_CONSOLE=ON, never "
            "released): the operator's command drives the wire bit by bit")

# (file under the repository, function, kind, first literal's start or
# None, reason).
ALLOWED = (
    ("firmware/iomcu/src/main.c", "main", "clock", "for(;;)",
     "the main loop itself: the clock read at its top and its tail"),
    ("firmware/iomcu/src/main.c", "main", "print",
     "rcbench-iomcu: output store sector", _STORE),
    ("firmware/iomcu/src/main.c", "main", "print",
     "rcbench-iomcu: outputs saved", _STORE),
    ("firmware/iomcu/src/main.c", "main", "print",
     "rcbench-iomcu: output store window", _STORE),
    ("firmware/iomcu/src/iomcu_can.c", "can_start", "print",
     "rcbench-iomcu: CAN %s at", "the boot line, before the main loop"),
    ("firmware/iomcu/src/iomcu_can.c", "can_drain", "clock", None,
     "the firmware update's restart only, with arming held: at most 7 "
     "batches of CAN_TXQ_STUCK_US (35,000us)"),
    ("firmware/iomcu/src/iomcu_report.c", "reset_start", "print",
     "rcbench-iomcu: reset %s", "the boot line, before the main loop"),
    ("firmware/iomcu/src/iomcu_report.c", "reset_start", "print",
     "rcbench-iomcu: %s", "the boot line, before the main loop"),
    ("firmware/iomcu/src/iomcu_report.c", "reset_start", "print", "%s",
     "the flash line, before the main loop"),
    ("firmware/iomcu/src/sense_core1.c", "core1_main", "wait", None, _CORE1),
    ("firmware/iomcu/src/sense_core1.c", "core1_main", "clock", None,
     _CORE1),
    ("firmware/iomcu/src/sense_core1.c", "sense_core1_start", "wait", None,
     "boot, before the main loop; SENSE_CORE1_START_MS (100ms) at most"),
    ("firmware/iomcu/src/sense_core1.c", "sense_core1_start", "clock", None,
     "boot, before the main loop; SENSE_CORE1_START_MS (100ms) at most"),
    ("firmware/iomcu/src/tone_core1.c", "tone_core1_quiesce", "wait", None,
     "a TONE rewire waits for core 1's acknowledgement, TONE_CORE1_WAIT_US "
     "(5000us) at most, on a page write; not yet removed from the loop"),
    ("firmware/iomcu/src/tone_core1.c", "tone_core1_quiesce", "clock", None,
     "as its wait above"),
    ("firmware/iomcu/src/update_hw.c", "io_restart", "wait", None,
     "after rom_reboot(), which does not return"),
    ("firmware/iomcu/src/xl2515.c", "xl2515_init", "sleep", None,
     "boot: the controller's reset and mode change, 11ms, before the main "
     "loop"),
    ("firmware/iomcu/src/ewline_console.c", "feed_byte", "sleep", None,
     _CONSOLE),
    ("firmware/iomcu/src/ewline_console.c", "feed_answer", "sleep", None,
     _CONSOLE),
    ("firmware/iomcu/src/iomcu_sense.c", "supply_boot_off", "clock", None,
     "boot, before the main loop: the PD mini read and switched off, "
     "SUPPLY_PAGE_BOOT_OFF_MS (3500ms) at most"),
    ("firmware/iomcu/src/out_pwm.c", "out_pwm_write_stamped", "clock", None,
     "the capture's stamped PWM write waits for the counter to leave the "
     "end of its frame, OUT_PWM_STAMP_WAIT_US (5us) at most"),
    ("firmware/iomcu/src/sense_trace_hw.c", "sense_trace_hw_outputs",
     "clock", None,
     "the trace build: one clock read per PWM slot to stamp a changed pulse, "
     "8 slots, no wait"),
    ("protocols/escwire/rp2350/ew_line.c", "rx_settle", "wait", None,
     "the receive FIFO's last words into DMA, EW_LINE_RX_SETTLE_SPINS (64) "
     "FIFO reads at most, at a transaction's end"),
    ("protocols/hitec/rp2350/hitec_line.c", "rx_settle", "wait", None,
     "the receive FIFO's last words into DMA, RX_SETTLE_SPINS (64) FIFO "
     "reads at most, at a transaction's end"),
    ("protocols/hitec/rp2350/hitec_line.c", "pre_give", "wait", None,
     "the same settle for PRECHARGE's sampler, RX_SETTLE_SPINS (64) FIFO "
     "reads at most"),
)


def strip(text):
    """Drops comments and blanks string and character literals.

    Args:
        text: The C source.

    Returns:
        A pair: the source with comments and the insides of literals
        replaced by blanks of the same length (newlines kept), and the
        source with only the comments blanked.
    """
    token = re.compile(r'/\*.*?\*/|//[^\n]*|"(?:[^"\\\n]|\\.)*"'
                       r"|'(?:[^'\\\n]|\\.)*'", re.S)
    code, literals = [], []
    pos = 0
    for m in token.finditer(text):
        code.append(text[pos:m.start()])
        literals.append(text[pos:m.start()])
        tok = m.group(0)
        blank = re.sub(r"[^\n]", " ", tok)
        if tok.startswith(("/*", "//")):
            code.append(blank)
            literals.append(blank)
        else:
            code.append(tok[0] + blank[1:-1] + tok[-1])
            literals.append(tok)
        pos = m.end()
    code.append(text[pos:])
    literals.append(text[pos:])
    return "".join(code), "".join(literals)


def match_paren(code, at, step):
    """Finds the parenthesis that pairs with the one at @p at.

    Args:
        code: The stripped source.
        at: The index of a "(" (step 1) or ")" (step -1).
        step: 1 to search forward, -1 backward.

    Returns:
        The index of its partner, or -1.
    """
    opener, closer = ("(", ")") if step == 1 else (")", "(")
    depth = 0
    i = at
    while 0 <= i < len(code):
        if code[i] == opener:
            depth += 1
        elif code[i] == closer:
            depth -= 1
            if depth == 0:
                return i
        i += step
    return -1


def skip_back(code, i):
    """Steps back over white space.

    Args:
        code: The stripped source.
        i: An index.

    Returns:
        The index after the last character before @p i that is not white
        space; 0 when there is none.
    """
    while i > 0 and code[i - 1].isspace():
        i -= 1
    return i


def word_before(code, i):
    """The identifier that ends at @p i, after white space.

    Args:
        code: The stripped source.
        i: An index.

    Returns:
        A pair: the identifier, or "", and the index where it starts.
    """
    end = skip_back(code, i)
    m = re.search(r"\w+$", code[max(0, end - 64):end])
    if not m:
        return "", end
    return m.group(0), end - len(m.group(0))


def head_of(code, brace):
    """Names what opens the block at @p brace.

    Args:
        code: The stripped source.
        brace: The index of a "{".

    Returns:
        "do", "else", the word before the "(" that pairs with a ")" right
        before the brace ("while", "for", "if", "switch" or a function's
        name), or "" for anything else (an initializer, a struct).
    """
    end = skip_back(code, brace)
    word, _ = word_before(code, end)
    if word in ("do", "else"):
        return word
    if end == 0 or code[end - 1] != ")":
        return ""
    open_ = match_paren(code, end - 1, -1)
    if open_ < 0:
        return ""
    return word_before(code, open_)[0]


def blocks(code):
    """Pairs every "{" with its "}".

    Args:
        code: The stripped source.

    Returns:
        A list of (open, close, head, depth) for every block, depth 0 at
        file level.
    """
    stack, found = [], []
    for i, c in enumerate(code):
        if c == "{":
            stack.append(i)
        elif c == "}" and stack:
            o = stack.pop()
            found.append((o, i, head_of(code, o), len(stack)))
    return found


def braceless_loop(code, at):
    """Whether the statement at @p at is the body of a loop without braces.

    Args:
        code: The stripped source.
        at: The index where the statement starts.

    Returns:
        True when a `while (...)`, `for (...)` or `do` comes right before
        it, also through `if (...)` statements without braces.
    """
    i = at
    while True:
        end = skip_back(code, i)
        if end == 0:
            return False
        if code[end - 1] == ")":
            o = match_paren(code, end - 1, -1)
            word, start = word_before(code, max(o, 0))
            if word in ("while", "for"):
                return True
            if word == "if":
                i = start
                continue
            return False
        return word_before(code, end)[0] == "do"


class Source:
    """One C file, read once."""

    def __init__(self, text):
        """Strips the file and finds its blocks and functions.

        Args:
            text: The C source.
        """
        self.code, self.literals = strip(text)
        self.blocks = blocks(self.code)
        self.closes = {c: h for _, c, h, _ in self.blocks}
        self.opens = {o: c for o, c, _, _ in self.blocks}
        self.functions = [(h, o, c) for o, c, h, d in self.blocks
                          if d == 0 and h and h not in LOOPS
                          and h not in ("if", "switch", "else")]

    def line(self, at):
        """The 1-based line of index @p at."""
        return self.code.count("\n", 0, at) + 1

    def function(self, at):
        """The function around index @p at, or ""."""
        for name, o, c in self.functions:
            if o < at < c:
                return name
        return ""

    def in_loop(self, at):
        """Whether index @p at sits inside a loop's braces or its body."""
        for o, c, head, _ in self.blocks:
            if o < at < c and head in LOOPS:
                return True
        return braceless_loop(self.code, at)

    def loops(self):
        """Every loop: (keyword index, condition text, body start, end).

        The condition of a `do` loop is its tail's; a loop without braces
        has its single statement as its body.
        """
        out = []
        code = self.code
        for m in re.finditer(r"\b(while|for)\s*\(", code):
            close = match_paren(code, m.end() - 1, 1)
            if close < 0:
                continue
            cond = code[m.end():close]
            # The tail of a do loop is that loop's condition, read below.
            prev = skip_back(code, m.start())
            if (m.group(1) == "while" and prev > 0 and code[prev - 1] == "}"
                    and self.closes.get(prev - 1) == "do"):
                continue
            rest = code[close + 1:]
            body = close + 1 + len(rest) - len(rest.lstrip())
            if code.startswith("{", body):
                end = self.opens[body]
            else:
                end = code.find(";", body)
            out.append((m.start(), cond, body, end))
        for o, c, head, _ in self.blocks:
            if head != "do":
                continue
            tail = re.match(r"\s*while\s*\(", code[c + 1:])
            cond = ""
            if tail:
                p = c + 1 + tail.end() - 1
                cond = code[p + 1:match_paren(code, p, 1)]
            kw = code.rfind("do", 0, o)
            out.append((kw, cond, o, c))
        return out


def findings(text):
    """Every wait, spin, clock loop, sleep and print of one file.

    Args:
        text: The C source.

    Returns:
        A sorted list of (line, function, kind, text): text is a print's
        first literal, a spin or clock loop's keyword and condition with
        white space dropped, or None.
    """
    src = Source(text)
    code = src.code
    out = set()
    for m in re.finditer(r"\btight_loop_contents\s*\(", code):
        if src.function(m.start()) and src.in_loop(m.start()):
            out.add((src.line(m.start()), src.function(m.start()), "wait",
                     None))
    for at, cond, body, end in src.loops():
        fname = src.function(at)
        if not fname:
            continue
        # The loop as an entry names it: its keyword and condition, white
        # space dropped ("for(;;)", "while(busy())").
        word = re.match(r"\w+", code[at:]).group(0)
        head = word + "(" + "".join(cond.split()) + ")"
        inner = code[body:end + 1].strip()
        if inner in (";", "{}") or re.fullmatch(r"\{\s*\}", inner):
            out.add((src.line(at), fname, "spin", head))
        if CLOCK.search(cond) or CLOCK.search(code[body:end + 1]):
            out.add((src.line(at), fname, "clock", head))
    for m in SLEEP.finditer(code):
        if src.function(m.start()):
            out.add((src.line(m.start()), src.function(m.start()), "sleep",
                     None))
    names = set(PRINTS) | {a for a, _ in DEFINE_PRINT.findall(src.literals)}
    call = re.compile(r"\b(" + "|".join(sorted(names)) + r")\s*\(")
    for m in call.finditer(code):
        fname = src.function(m.start())
        if not fname:
            continue
        rest = src.literals[m.end():]
        lit = LITERAL.match(src.literals,
                            m.end() + len(rest) - len(rest.lstrip()))
        out.add((src.line(m.start()), fname, "print",
                 lit.group(0)[1:-1] if lit else None))
    return sorted(out, key=lambda f: (f[0], f[2], f[3] or ""))


def sources(iomcu=IOMCU):
    """The C files the IOmcu build compiles from this repository.

    Args:
        iomcu: The IOmcu project's directory.

    Returns:
        Its src/*.c and every *.c under the shared/ and protocols/
        directories its CMakeLists.txt adds, sorted.
    """
    files = set((iomcu / "src").glob("*.c"))
    cmake = (iomcu / "CMakeLists.txt").read_text()
    for rel in re.findall(r'add_subdirectory\("\$\{CMAKE_CURRENT_LIST_DIR\}'
                          r'/\.\./\.\./((?:shared|protocols)/\w+)"', cmake):
        files |= set((iomcu.parent.parent / rel).rglob("*.c"))
    return sorted(files)


def allowed(table, path, fname, kind, first):
    """The entry of the allow list that covers a finding.

    Args:
        table: The allow list.
        path: The file, relative to the repository.
        fname: The function.
        kind: "wait", "spin", "clock", "sleep" or "print".
        first: A print's first literal, a loop's keyword and condition, or
            None.

    Returns:
        The entry, or None.
    """
    for entry in table:
        f, fn, k, prefix, _ = entry
        if (f, fn, k) != (path, fname, kind):
            continue
        if prefix is None or (first is not None and first.startswith(prefix)):
            return entry
    return None


WHAT = {
    "wait": "tight_loop_contents() in a loop",
    "spin": "a loop with an empty body",
    "clock": "a loop that reads the clock",
    "sleep": "a busy_wait_*() or sleep_*() call",
}


def check(files=None, table=ALLOWED, root=ROOT):
    """Every finding the allow list does not cover, and every stale entry.

    Args:
        files: The C files; None for sources().
        table: The allow list.
        root: The directory paths are given relative to.

    Returns:
        A list of problems in words; empty when the tree passes.
    """
    problems = []
    used = set()
    for path in sources() if files is None else files:
        rel = path.relative_to(root).as_posix()
        for line, fname, kind, first in findings(path.read_text()):
            entry = allowed(table, rel, fname, kind, first)
            if entry is not None:
                used.add(entry)
                continue
            what = WHAT.get(kind) or f'a print "{first or ""}"'
            problems.append(f"{rel}:{line}: {what} in {fname}()")
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
        0 when the tree passes, 1 otherwise, 2 for an unknown argument.
    """
    if any(a != "--check" for a in argv):
        print(__doc__.split("\n\n")[-2])
        return 2
    files = sources()
    problems = check(files)
    for p in problems:
        print(p)
    if problems:
        print(f"{len(problems)} wait(s) in the IOmcu's code")
        return 1
    print(f"{len(files)} files: no wait outside the allow list")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
