#!/usr/bin/env python3
"""Hold each IOmcu board's PIO programs and DMA channels to the RP2350.

The RP2350 has 3 PIO (programmable input/output) blocks, each with 4 state
machines and 32 instruction words, and 16 DMA (direct memory access)
channels.  The tool reads the length of every program from the .pio files
under protocols/, as pioasm 2.3.0 counts it: one word an instruction or
`.word`, at most 32, `.origin` a fixed offset given before the first
instruction, `.wrap_target` and `.wrap` inside the program.  It loads each
board's programs into a model of the chip, in the order the board starts
them, and reports per block the words used and free and the state
machines used, and the DMA channels used.

The model places a program as pico-sdk 2.3.0 does
(pio_claim_free_sm_and_add_program_for_gpio_range(), hardware_pio/pio.c):
the blocks are searched from PIO2 down to PIO0, a block's lowest free
state machine is taken, and the program goes at the highest free offset
that holds it, or at its `.origin`.  A bidirectional DShot output takes an
adjacent pair of state machines, n and (n + 1) modulo 4, in the first
block from PIO2 down with such a pair and room for both programs
(claim_pair() in protocols/dshot/rp2350/out_dshot.c).  A program a board
places in a named block goes there or nowhere.  A use that does not fit
is refused and leaves the chip as it was, as the drivers leave it.

Every block runs at one GPIO base for the whole run: 0 when the board's
PIO pins all lie below GPIO32, 16 when they lie in GPIO16 to GPIO47.  A
board whose PIO pins fit neither window fails.

The boards:

- board 1, the RP2350-CAN module wired by hand, and board 2, the rbmod
  carrier: the drivers of firmware/iomcu in the order main.c starts them
  (output slots, the PD mini UART, the phase tap, the programming ports),
  then a programming session.  Each mix in the table names the document
  or code that promises or refuses it.
- board 3, the remote servo node: its uses are a table here, its
  programs the tree's.  PIO0 holds a fixed load from boot: MIRROR
  (copies input k to output k, 1 word) and CAPTURE (pushes every edge of
  the 9 inputs and 9 outputs, 5 words, 2 chained DMA channels), both in
  protocols/passthru/rp2350/pass.pio, and the
  PD mini UART, all pinned to PIO0 before any slot binds; the UART link
  to the display board takes 1 DMA channel.  PIO1 and PIO2 take the
  throttle slots and a programming session's line.  Besides the table,
  every order of up to 9 plain DShot, bidirectional DShot and PPM
  (pulse-position modulation) slots runs with each programming session,
  the session opened after the slots and before them, against
  rsn_allows().  A PPM slot places as a plain DShot slot does (ppm_tx and
  dshot_tx are both 4 words without an origin, the tool checks it), so
  the sweep runs each order once and adds the PPM slots' DMA channels.
  The capture ring's event rate of each mix without a session is held
  against the 42ms flash window; a window that laps the ring is a gap in
  the capture figures, not an error.  During a programming session the
  capture guarantee does not hold: the session's pin carries the line's
  own edges (the Hitec precharge sampler 1,843,200 a second), the
  measured figures (pulse width, frame period, missed frames, servo
  windows) are marked invalid for the session, and pass-through goes on,
  because MIRROR is a state machine of its own and uses no ring.  The
  ring use during each session is reported apart and not counted against
  the guarantee.

The tool fails when a mix that a board's table allows does not fit, and
when a mix the table refuses fits: the table is then wrong the other way.
Board 3's table refuses some mixes for reasons outside the chip model,
each a named rule in RSN_RULES; a refused mix that fits must break one of
them.  The slots the model fits beside each session's line in its own
block are pinned in RSN_SESSION_SPARE, so a program whose length changes
fails the check.

Not modelled: a program unloaded and loaded again (a slot unbound and
bound anew leaves a hole of its own size, which the model never sees),
and pass 2 of the SDK's search, which moves the GPIO base of an empty
block.

    tools/pio_budget.py              the budget of every board
    tools/pio_budget.py --board 3    one board
    tools/pio_budget.py --check      only the failures; exit 1 on one

SPDX-License-Identifier: MIT
"""

from __future__ import annotations

import argparse
import dataclasses
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent

NUM_PIOS = 3     #: PIO blocks of the RP2350
NUM_SMS = 4      #: state machines in a block
PIO_WORDS = 32   #: instruction words in a block
NUM_DMA = 16     #: DMA channels


class BudgetError(Exception):
    """A source is not in a shape this tool reads, or a board is malformed."""


@dataclasses.dataclass(frozen=True)
class Program:
    """One PIO program as pioasm assembles it.

    Attributes:
        name: The name after `.program`.
        length: Instruction words.
        origin: The offset `.origin` fixes, or -1 for none.
        wrap_target: The offset of `.wrap_target`, 0 without one.
        wrap: The offset of the last instruction before `.wrap`, the last
            instruction without one.
        source: The file it is read from, or what defines it.
    """

    name: str
    length: int
    origin: int
    wrap_target: int
    wrap: int
    source: str


# ------------------------------------------------------------------ reading

# The directives pioasm 2.3.0 knows (tools/pioasm/lexer.ll).
_DIRECTIVES = {"program", "origin", "define", "wrap_target", "wrap", "word",
               "side_set", "pio_version", "in", "out", "set", "clock_div",
               "fifo", "mov_status", "lang_opt"}
# A label: `name:`, `public name:` or `*name:`, an instruction after it.
_LABEL_RE = re.compile(r"(?:public\s+|\*\s*)?[A-Za-z_]\w*\s*:(.*)\Z",
                       re.I | re.S)
_BLOCK_RE = re.compile(r"%([^%\n]+)\{")
_BLOCK_END_RE = re.compile(r"%\}[ \t\r]*")
_LANG_OPT_RE = re.compile(r"\.lang_opt(?![A-Za-z0-9_])", re.I)
_INT_RE = re.compile(r"0x[0-9a-f]+|0b[01]+|\d+", re.I)
_DIRECTIVE_RE = re.compile(r"\.(\w*)(.*)\Z", re.S)


def _clean(text: str) -> str:
    """Blanks comments, code blocks and `.lang_opt` values, keeping lines.

    A `;` or `//` comment runs to the line end, a `/* */` comment becomes a
    space and its newlines, a `% name {` block up to a line of `%}` goes
    with its lines, and the rest of a `.lang_opt` line is dropped: pioasm
    lexes it as one string.

    Args:
        text: A .pio file.

    Returns:
        The text with as many lines, holding directives, labels and
        instructions only.

    Raises:
        BudgetError: A `/*` comment or a code block is not closed.
    """
    out, i, n = [], 0, len(text)
    while i < n:
        if text.startswith((";", "//"), i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            if j < 0:
                raise BudgetError("a /* comment is not closed")
            out.append(" " + "\n" * text.count("\n", i, j))
            i = j + 2
        elif text[i] == "%" and _BLOCK_RE.match(text, i):
            i = _BLOCK_RE.match(text, i).end()
            while True:
                eol = text.find("\n", i)
                if eol < 0:
                    raise BudgetError("a code block is not closed")
                out.append("\n")
                if _BLOCK_END_RE.fullmatch(text[i:eol]):
                    i = eol + 1
                    break
                i = eol + 1
        elif text[i] == "." and _LANG_OPT_RE.match(text, i):
            out.append(".lang_opt")
            j = text.find("\n", i)
            i = n if j < 0 else j
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def _value(word: str, defines: dict[str, int], where: str) -> int:
    """An integer operand: a literal, `(literal)` or a `.define`d name.

    Args:
        word: The operand as written.
        defines: The names defined so far with an integer value.
        where: File and line, for the error.

    Returns:
        The value.

    Raises:
        BudgetError: The operand is an expression this tool does not read.
    """
    word = word.strip()
    while word.startswith("(") and word.endswith(")"):
        word = word[1:-1].strip()
    if _INT_RE.fullmatch(word):
        return int(word, 0)
    if word in defines:
        return defines[word]
    raise BudgetError(f"{where}: cannot read the value {word!r}")


@dataclasses.dataclass
class _Draft:
    """A program while its lines are read."""

    name: str
    count: int = 0
    origin: int = -1
    wrap_target: int | None = None
    wrap: int | None = None


def _finish(d: _Draft, source: str) -> Program:
    """The program a draft becomes at its end, held to pioasm's rules.

    Args:
        d: The draft.
        source: The file, for the error.

    Returns:
        The program.

    Raises:
        BudgetError: No instruction, a `.wrap_target` after the last one,
            or an `.origin` that leaves the program no room.
    """
    if d.count == 0:
        raise BudgetError(f"{source}: program {d.name} has no instruction")
    if d.wrap_target is not None and d.wrap_target >= d.count:
        raise BudgetError(f"{source}: .wrap_target of {d.name} is after "
                          "the last instruction")
    if d.origin >= 0 and d.origin + d.count > PIO_WORDS:
        raise BudgetError(f"{source}: .origin {d.origin} of {d.name} leaves "
                          f"no room for {d.count} words")
    return Program(d.name, d.count, d.origin,
                   0 if d.wrap_target is None else d.wrap_target,
                   d.count - 1 if d.wrap is None else d.wrap, source)


def _directive(d: _Draft | None, word: str, rest: str,
               defines: dict[str, int], where: str) -> None:
    """Applies one directive other than `.program` to the draft.

    Args:
        d: The program being read, None before the first `.program`.
        word: The directive's name in lower case, without the dot.
        rest: The text after it.
        defines: The integer `.define`s, extended here.
        where: File and line, for the error.

    Raises:
        BudgetError: An unknown directive, or one that pioasm refuses
            where it stands.
    """
    if word not in _DIRECTIVES:
        raise BudgetError(f"{where}: unknown directive .{word}")
    if word == "define":
        parts = rest.split()
        if parts and parts[0].lower() == "public":
            parts = parts[1:]
        if len(parts) == 2 and _INT_RE.fullmatch(parts[1]):
            defines[parts[0]] = int(parts[1], 0)
        return
    if d is None:
        raise BudgetError(f"{where}: .{word} before .program")
    if word == "origin":
        if d.count:
            raise BudgetError(f"{where}: .origin after an instruction")
        d.origin = _value(rest, defines, where)
    elif word == "wrap_target":
        if d.wrap_target is not None:
            raise BudgetError(f"{where}: a second .wrap_target")
        d.wrap_target = d.count
    elif word == "wrap":
        if d.wrap is not None:
            raise BudgetError(f"{where}: a second .wrap")
        if d.count == 0:
            raise BudgetError(f"{where}: .wrap before the first instruction")
        d.wrap = d.count - 1
    elif word == "word":
        _instruction(d, where)


def _instruction(d: _Draft | None, where: str) -> None:
    """Counts one instruction word into the draft.

    Args:
        d: The program being read.
        where: File and line, for the error.

    Raises:
        BudgetError: No program yet, or the 33rd word.
    """
    if d is None:
        raise BudgetError(f"{where}: an instruction before .program")
    if d.count >= PIO_WORDS:
        raise BudgetError(f"{where}: {d.name} is over {PIO_WORDS} words")
    d.count += 1


def parse_pio(text: str, source: str = "<text>") -> list[Program]:
    """Reads every program of a .pio file.

    Args:
        text: The file.
        source: Its name, for the programs and the errors.

    Returns:
        The programs in file order.

    Raises:
        BudgetError: The file breaks a rule pioasm holds it to, or holds
            an `.origin` this tool cannot read.
    """
    programs: list[Program] = []
    defines: dict[str, int] = {}
    d: _Draft | None = None
    for lineno, raw in enumerate(_clean(text).split("\n"), 1):
        line = raw.strip()
        if not line:
            continue
        where = f"{source}:{lineno}"
        if line.startswith("."):
            word, rest = _DIRECTIVE_RE.match(line).groups()
            word = word.lower()
            if word == "program":
                if d is not None:
                    programs.append(_finish(d, source))
                d = _Draft(rest.strip())
            else:
                _directive(d, word, rest, defines, where)
            continue
        label = _LABEL_RE.match(line)
        if label is None or label.group(1).strip():
            _instruction(d, where)
    if d is not None:
        programs.append(_finish(d, source))
    return programs


def load_programs(root: pathlib.Path = ROOT) -> dict[str, Program]:
    """Every program the IOmcu loads, by name.

    The .pio files under protocols/*/rp2350/, and the Hitec precharge
    sampler, which hitec_pre_program() writes at run time: its length is
    HITEC_PRE_PROGRAM_MAX, the most it writes.

    Args:
        root: The repository.

    Returns:
        The programs by name.

    Raises:
        BudgetError: A file the parser refuses, a name defined twice, or
            no HITEC_PRE_PROGRAM_MAX.
    """
    progs: dict[str, Program] = {}
    for path in sorted((root / "protocols").glob("*/rp2350/*.pio")):
        rel = path.relative_to(root).as_posix()
        for p in parse_pio(path.read_text(encoding="utf-8"), rel):
            if p.name in progs:
                raise BudgetError(f"{rel}: {p.name} is also in "
                                  f"{progs[p.name].source}")
            progs[p.name] = p
    hdr = "protocols/hitec/include/hitec_pre.h"
    m = re.search(r"#define\s+HITEC_PRE_PROGRAM_MAX\s+(\d+)u?\b",
                  (root / hdr).read_text(encoding="utf-8"))
    if m is None:
        raise BudgetError(f"{hdr}: no HITEC_PRE_PROGRAM_MAX")
    n = int(m.group(1))
    progs["hitec_pre"] = Program("hitec_pre", n, -1, 0, n - 1,
                                 hdr + " HITEC_PRE_PROGRAM_MAX")
    return progs


# -------------------------------------------------------------------- model

@dataclasses.dataclass
class Chip:
    """The PIO blocks and DMA channels as the uses so far leave them.

    Attributes:
        used: Per block, a bit per instruction word taken.
        sms: Per block, the program on each state machine, or None.
        dma: DMA channels claimed.
    """

    used: list[int] = dataclasses.field(
        default_factory=lambda: [0] * NUM_PIOS)
    sms: list[list[str | None]] = dataclasses.field(
        default_factory=lambda: [[None] * NUM_SMS for _ in range(NUM_PIOS)])
    dma: int = 0

    def copy(self) -> Chip:
        """An independent copy.

        Returns:
            The copy.
        """
        return Chip(list(self.used), [list(s) for s in self.sms], self.dma)

    def words(self, block: int) -> int:
        """Instruction words taken in a block.

        Args:
            block: 0 to 2.

        Returns:
            0 to 32.
        """
        return bin(self.used[block]).count("1")

    def sms_used(self, block: int) -> int:
        """State machines claimed in a block.

        Args:
            block: 0 to 2.

        Returns:
            0 to 4.
        """
        return sum(s is not None for s in self.sms[block])


def _offset(used: int, prog: Program) -> int | None:
    """Where the SDK loads a program in a block (find_offset_for_program()).

    Args:
        used: The block's taken words.
        prog: The program.

    Returns:
        Its `.origin` when that range is free, else the highest free offset
        that holds it; None when there is none.
    """
    mask = (1 << prog.length) - 1
    if prog.origin >= 0:
        return None if used & (mask << prog.origin) else prog.origin
    for at in range(PIO_WORDS - prog.length, -1, -1):
        if not used & (mask << at):
            return at
    return None


def _add(chip: Chip, block: int, prog: Program) -> bool:
    """Claims a block's lowest free state machine and loads a program.

    Args:
        chip: Changed on success.
        block: 0 to 2.
        prog: The program.

    Returns:
        False, with the chip unchanged, when the block has no free state
        machine or no room.
    """
    sms = chip.sms[block]
    if None not in sms:
        return False
    at = _offset(chip.used[block], prog)
    if at is None:
        return False
    sms[sms.index(None)] = prog.name
    chip.used[block] |= ((1 << prog.length) - 1) << at
    return True


def _pair(chip: Chip, tx: Program, rx: Program) -> bool:
    """Claims an adjacent pair and loads both programs (claim_pair()).

    Args:
        chip: Changed on success.
        tx: The transmitter, on state machine n.
        rx: The receiver, on state machine (n + 1) modulo 4.

    Returns:
        False, with the chip unchanged, when no block has a free pair and
        room for both.
    """
    for block in reversed(range(NUM_PIOS)):
        sms = chip.sms[block]
        for t in range(NUM_SMS):
            r = (t + 1) % NUM_SMS
            if sms[t] is not None or sms[r] is not None:
                continue
            at_tx = _offset(chip.used[block], tx)
            if at_tx is None:
                break
            used = chip.used[block] | (((1 << tx.length) - 1) << at_tx)
            at_rx = _offset(used, rx)
            if at_rx is None:
                break
            chip.used[block] = used | (((1 << rx.length) - 1) << at_rx)
            sms[t], sms[r] = tx.name, rx.name
            return True
    return False


@dataclasses.dataclass(frozen=True)
class Use:
    """One driver instance: the programs it loads and the DMA it claims.

    Attributes:
        label: What it is, for the report.
        steps: ("sm", program) loads through the SDK's search, ("pair",
            transmitter, receiver) through claim_pair(), ("fixed", program,
            block) into that block only.
        dma: DMA channels it claims.
        ref: The code it is read from, or the fact it stands for.
    """

    label: str
    steps: tuple[tuple, ...] = ()
    dma: int = 0
    ref: str = ""


def apply(chip: Chip, use: Use, progs: dict[str, Program]) -> Chip | None:
    """Places one use, all or nothing.

    Args:
        chip: Not changed.
        use: The use.
        progs: The programs by name.

    Returns:
        The chip after the use, or None when it does not fit.

    Raises:
        BudgetError: A step names a program that is not known.
    """
    nxt = chip.copy()
    for step in use.steps:
        try:
            ps = [progs[name] for name in step[1:3] if isinstance(name, str)]
        except KeyError as e:
            raise BudgetError(f"{use.label}: no program {e.args[0]}")
        if step[0] == "sm":
            ok = any(_add(nxt, b, ps[0]) for b in reversed(range(NUM_PIOS)))
        elif step[0] == "pair":
            ok = _pair(nxt, ps[0], ps[1])
        else:
            ok = _add(nxt, step[2], ps[0])
        if not ok:
            return None
    nxt.dma += use.dma
    return nxt if nxt.dma <= NUM_DMA else None


@dataclasses.dataclass(frozen=True)
class Mix:
    """A configuration in a board's table.

    Attributes:
        name: What it holds.
        uses: Its uses after the board's boot uses, in the order they bind.
        fits: True when the board allows it, False when it refuses it.
        ref: The document or code that says so, or the reason.
    """

    name: str
    uses: tuple[Use, ...]
    fits: bool
    ref: str


@dataclasses.dataclass(frozen=True)
class Board:
    """A board: its PIO pins, what it loads at boot and its table.

    Attributes:
        number: The IDENTITY board value.
        name: Its name.
        gpio: The lowest and highest GPIO a PIO program of it touches.
        boot: Uses loaded before any mix, in order.
        mixes: The table.
        planned: Programs not in a .pio file yet: name to (words, ref).
        sweep: True to run every slot order with every session.
    """

    number: int
    name: str
    gpio: tuple[int, int]
    boot: tuple[Use, ...] = ()
    mixes: tuple[Mix, ...] = ()
    planned: tuple[tuple[str, int, str], ...] = ()
    sweep: bool = False

    def base(self) -> int:
        """The GPIO base every block runs at.

        Returns:
            0 or 16.

        Raises:
            BudgetError: The PIO pins fit neither window.
        """
        lo, hi = self.gpio
        if hi < PIO_WORDS:
            return 0
        if lo >= 16 and hi < 48:
            return 16
        raise BudgetError(f"board {self.number}: GPIO{lo} to GPIO{hi} is "
                          "in no 32-pin PIO window")


@dataclasses.dataclass
class Outcome:
    """The chip after a mix, and the uses it refused.

    Attributes:
        chip: The blocks and channels at the end.
        refused: Labels of the uses that did not fit.
    """

    chip: Chip
    refused: list[str]

    @property
    def fits(self) -> bool:
        """True when every use fits."""
        return not self.refused


def board_programs(board: Board,
                   progs: dict[str, Program]) -> dict[str, Program]:
    """The programs a board can load: the files' and its planned ones.

    A planned program that a .pio file defines by the same name is taken
    from the file.

    Args:
        board: The board.
        progs: The programs read from the files.

    Returns:
        The programs by name.
    """
    out = dict(progs)
    for name, words, ref in board.planned:
        if name not in out:
            out[name] = Program(name, words, -1, 0, words - 1,
                                "planned, " + ref)
    return out


def run(board: Board, uses: tuple[Use, ...],
        progs: dict[str, Program]) -> Outcome:
    """Loads a board's boot uses, then a mix's, in order.

    Args:
        board: The board.
        uses: The mix's uses.
        progs: The board's programs.

    Returns:
        The outcome.
    """
    board.base()
    chip, refused = Chip(), []
    for use in board.boot + tuple(uses):
        nxt = apply(chip, use, progs)
        if nxt is None:
            refused.append(use.label)
        else:
            chip = nxt
    return Outcome(chip, refused)


# ------------------------------------------------------------------- boards

#: Plain DShot: one 4-word program per pin, no DMA (out_dshot.c).
DSHOT = Use("plain DShot", (("sm", "dshot_tx"),), 0,
            "protocols/dshot/rp2350/out_dshot.c")
#: Bidirectional DShot: a pair of state machines, no DMA (out_dshot.c).
BIDIR = Use("bidirectional DShot", (("pair", "dshot_bidir_tx", "dshot_rx"),),
            0, "protocols/dshot/rp2350/out_dshot.c")
#: PPM: data and control DMA channels (out_ppm.c).
PPM = Use("PPM", (("sm", "ppm_tx"),), 2, "protocols/ppm/rp2350/out_ppm.c")
#: A servo or MOTOR PWM output: a PWM slice, no PIO (out_pwm.c).
SERVO = Use("servo PWM", (), 0, "firmware/iomcu/src/out_pwm.c")
#: The PD mini UART: two state machines, no DMA (pd_uart.c).
PD_UART = Use("PD mini UART", (("sm", "pd_uart_tx"), ("sm", "pd_uart_rx")),
              0, "protocols/pdmini/rp2350/pd_uart.c")
#: The phase tap's capture: one state machine, one DMA channel.
TAP = Use("phase tap", (("sm", "tone_cap"),), 1,
          "protocols/phase_tap/rp2350/tone_cap.c")
#: A KST session's line: transmit and receive DMA.
KST = Use("KST session", (("sm", "kst_line"),), 2,
          "protocols/kst/rp2350/kst_line.c")
#: An ESC wire session's line: transmit and receive DMA.
ESCWIRE = Use("ESC wire session", (("sm", "ew_line"),), 2,
              "protocols/escwire/rp2350/ew_line.c")
#: A Hitec session's line without the precharge sampler.
HITEC = Use("Hitec session", (("sm", "hitec_line"),), 2,
            "protocols/hitec/rp2350/hitec_line.c")
#: A Hitec D session with the precharge sampler: 2 programs, 3 DMA.
HITEC_PRE = Use("Hitec D session with precharge",
                (("sm", "hitec_line"), ("sm", "hitec_pre")), 3,
                "protocols/hitec/rp2350/hitec_line.c, pre_claim()")
#: The telemetry wire of programming for YGE ESCs: transmit and receive DMA.
YGE = Use("YGE session", (("sm", "oy_line"),), 2,
          "protocols/openyge/rp2350/oy_line.c")
#: Every programming session.
SESSIONS = (KST, ESCWIRE, HITEC, HITEC_PRE, YGE)


def _x(use: Use, n: int) -> tuple[Use, ...]:
    """N copies of a use, for a mix."""
    return (use,) * n


_MAIN = "firmware/iomcu/src/main.c, the boot order"

BOARD_RP2350_CAN = Board(
    1, "RP2350-CAN module wired by hand", (0, 29),
    mixes=tuple(
        Mix(f"PD mini UART, phase tap, {s.label}", (PD_UART, TAP, s), True,
            _MAIN) for s in SESSIONS) + (
        Mix("6 bidirectional DShot slots", _x(BIDIR, 6), True,
            "docs/Link.md, PIO budget: 3 blocks hold 6 bidirectional pins"),
        Mix("2 bidirectional DShot slots, 2 plain DShot ports, Hitec D "
            "session with precharge",
            _x(BIDIR, 2) + _x(DSHOT, 2) + (HITEC_PRE,), True,
            "docs/Link.md, PIO budget: 8 of 12 state machines"),
        Mix("6 bidirectional DShot slots, PD mini UART",
            _x(BIDIR, 6) + (PD_UART,), False,
            "docs/Link.md, BIND WHY 13: the slots leave the UART no block"),
        Mix("8 PPM slots, phase tap", _x(PPM, 8) + (TAP,), False,
            "docs/Link.md, TONE: BUSY, no DMA channel left by the outputs"),
    ))

BOARD_RBMOD = Board(
    2, "rbmod carrier", (0, 29),
    mixes=tuple(
        Mix(f"4 servo PWM slots, PD mini UART, phase tap, {s.label}",
            _x(SERVO, 4) + (PD_UART, TAP, s), True, _MAIN)
        for s in SESSIONS) + (
        Mix("4 bidirectional DShot slots, PD mini UART",
            _x(BIDIR, 4) + (PD_UART,), True, _MAIN),
        Mix("4 plain DShot slots, PD mini UART, KST session",
            _x(DSHOT, 4) + (PD_UART, KST), True, _MAIN),
        Mix("4 bidirectional DShot slots, PD mini UART, phase tap",
            _x(BIDIR, 4) + (PD_UART, TAP), False,
            _MAIN + ": the outputs bind and the tap is left out"),
        Mix("4 plain DShot slots, PD mini UART, phase tap, KST session",
            _x(DSHOT, 4) + (PD_UART, TAP, KST), False,
            "the tap's 22 words leave no block 24 free words"),
    ))

# Board 3.
RSN_MIRROR = Use("MIRROR", (("fixed", "pass_mirror", 0),), 0,
                 "copies input k to output k, pinned to PIO0 at boot")
RSN_CAPTURE = Use("CAPTURE", (("fixed", "pass_capture", 0),), 2,
                  "every edge of 18 pins, 2 chained DMA channels, PIO0")
RSN_PD_UART = Use("PD mini UART", (("fixed", "pd_uart_tx", 0),
                                   ("fixed", "pd_uart_rx", 0)), 0,
                  "pinned to PIO0 at boot")
RSN_LINK_RX = Use("UART link receive", (), 1,
                  "UART0 RX into a 1KiB ring by 1 DMA channel")

#: PPM slots board 3 allows: 3 fixed DMA channels and 2 a PPM slot stay
#: within 16.
RSN_PPM_MAX = 6
#: Output pins of board 3, GPIO16 to GPIO32, even.
RSN_OUTPUTS = 9
_SLOTS = "PIO1 and PIO2 for slots"
_ONE = "a session's line takes one block, the other is for slots"
_PRE = "the precharge sampler takes a state machine of the slots' block"


def _packs(blocks: list[int], singles: int, pairs: int) -> bool:
    """True when the slots go into blocks of the given free state machines.

    Args:
        blocks: Free state machines per block.
        singles: Slots of 1 state machine.
        pairs: Slots of 2 state machines in one block.

    Returns:
        True when a placement exists.
    """
    if pairs == 0:
        return singles <= sum(blocks)
    return any(cap >= 2 and _packs(blocks[:i] + [cap - 2] + blocks[i + 1:],
                                   singles, pairs - 1)
               for i, cap in enumerate(blocks))


def _slot_blocks(session: Use | None) -> list[int]:
    """Free state machines per block that board 3 gives its slots.

    Args:
        session: The programming session, or None.

    Returns:
        [4, 4] without a session (PIO1 and PIO2), [3] with a Hitec D
        session with precharge (the block of the sampler), else [4].
    """
    if session is None:
        return [4, 4]
    return [3] if session is HITEC_PRE else [4]


@dataclasses.dataclass(frozen=True)
class Rule:
    """A refusal of board 3's table that the chip model does not make.

    Attributes:
        name: Its name in the report.
        text: What it refuses and why.
        breaks: (session, singles, pairs, ppm) -> True when refused.
    """

    name: str
    text: str
    breaks: object


#: Board 3's named rules.  A mix the table refuses that fits the model
#: must break one of them, or the tool fails.
RSN_RULES = (
    Rule("SESSION_BLOCK",
         "a session's line holds its whole block; slots go in the other "
         "block, in the 3 state machines the Hitec precharge sampler "
         "leaves there",
         lambda s, n1, n2, q: s is not None
         and not _packs(_slot_blocks(s), n1, n2)),
    Rule("PPM_MAX",
         "at most 6 PPM slots: 3 fixed DMA channels and 2 a PPM slot",
         lambda s, n1, n2, q: q > RSN_PPM_MAX),
    Rule("SLOT_PINS",
         "9 output pins, and a session takes one of them",
         lambda s, n1, n2, q: n1 + n2 > RSN_OUTPUTS - (s is not None)),
)

#: Plain DShot slots the model fits beside each session's line in the
#: line's own block, which SESSION_BLOCK refuses: a KST line (24 words)
#: leaves 8 words and 3 state machines, an ESC wire line (25) 7 words,
#: a Hitec line (30) 2 words.  The tool fails when the model differs.
RSN_SESSION_SPARE = {"KST session": 2, "ESC wire session": 1,
                     "Hitec session": 0, "Hitec D session with precharge": 0,
                     "YGE session": 1}


def rsn_broken(session: Use | None, singles: int, pairs: int,
               ppm: int) -> list[str]:
    """The named rules a board 3 mix breaks.

    Args:
        session: The programming session, or None.
        singles: Plain DShot and PPM slots.
        pairs: Bidirectional DShot slots.
        ppm: How many of the singles are PPM.

    Returns:
        The names, in RSN_RULES order.
    """
    return [r.name for r in RSN_RULES
            if r.breaks(session, singles, pairs, ppm)]


def rsn_allows(session: Use | None, singles: int, pairs: int,
               ppm: int) -> bool:
    """True when board 3's table allows a mix.

    PIO0 holds the fixed load; PIO1 and PIO2 give the slots 4 state
    machines each, and RSN_RULES narrow that.

    Args:
        session: The programming session, or None.
        singles: Plain DShot and PPM slots.
        pairs: Bidirectional DShot slots.
        ppm: How many of the singles are PPM.

    Returns:
        True when allowed.
    """
    return (_packs([4, 4], singles, pairs)
            and not rsn_broken(session, singles, pairs, ppm))


def session_spare(board: Board, session: Use,
                  progs: dict[str, Program]) -> int:
    """Plain DShot slots the model fits in a session line's own block.

    Args:
        board: Board 3.
        session: The session.
        progs: Its programs.

    Returns:
        The slots placed in the line's block before one goes elsewhere.
    """
    chip = apply(run(board, (), progs).chip, session, progs)
    block = next(b for b in range(NUM_PIOS)
                 if progs[session.steps[0][1]].name in chip.sms[b])
    n = 0
    while True:
        nxt = apply(chip, DSHOT, progs)
        if nxt is None or nxt.sms_used(block) == chip.sms_used(block):
            return n
        chip, n = nxt, n + 1


def _rsn_mixes() -> tuple[Mix, ...]:
    """Board 3's table: the limits rsn_allows() states, and the refusals.

    Returns:
        The mixes.
    """
    rows = [
        Mix("boot only", (), True, "PIO0: 18 words, 4 state machines"),
        Mix("8 plain DShot", _x(DSHOT, 8), True,
            _SLOTS + ": 4 plain each"),
        Mix("4 bidirectional DShot", _x(BIDIR, 4), True,
            _SLOTS + ": 2 bidirectional each"),
        Mix("4 bidirectional DShot, 5 servo PWM", _x(BIDIR, 4)
            + _x(SERVO, 5), True, "the capture's worst case"),
    ]
    for s in (KST, ESCWIRE, HITEC):
        rows += [
            Mix(f"4 plain DShot, {s.label}", _x(DSHOT, 4) + (s,), True,
                _ONE),
            Mix(f"2 bidirectional DShot, {s.label}", _x(BIDIR, 2) + (s,),
                True, _ONE),
            Mix(f"1 bidirectional and 2 plain DShot, {s.label}",
                (BIDIR,) + _x(DSHOT, 2) + (s,), True, _ONE),
        ]
    rows += [
        Mix("3 plain DShot, Hitec D session with precharge",
            _x(DSHOT, 3) + (HITEC_PRE,), True, _PRE),
        Mix("1 bidirectional and 1 plain DShot, Hitec D session with "
            "precharge", (BIDIR, DSHOT, HITEC_PRE), True,
            _PRE),
        Mix("4 plain DShot, Hitec D session with precharge",
            _x(DSHOT, 4) + (HITEC_PRE,), False,
            _PRE),
        Mix("2 bidirectional DShot, Hitec D session with precharge",
            _x(BIDIR, 2) + (HITEC_PRE,), False,
            _PRE),
        Mix("6 PPM", _x(PPM, 6), True, "RSN_PPM_MAX: 15 of 16 DMA channels"),
        Mix("7 PPM", _x(PPM, 7), False, "RSN_PPM_MAX: 17 of 16 DMA channels"),
    ]
    return tuple(rows)


BOARD_RSN = Board(
    3, "remote servo node", (16, 35),
    boot=(RSN_MIRROR, RSN_CAPTURE, RSN_PD_UART, RSN_LINK_RX),
    mixes=_rsn_mixes(),
    sweep=True)

BOARDS = (BOARD_RP2350_CAN, BOARD_RBMOD, BOARD_RSN)


# ---------------------------------------------------------------- the sweep

#: The capture ring of board 3: 64KiB of 8-byte events.
RING_EVENTS = 64 * 1024 // 8
#: The longest flash window, in ms (docs/Safety.md).
FLASH_WINDOW_MS = 42
#: Receiver inputs of board 3, each captured.
RSN_INPUTS = 9
#: The fastest servo frame rate the capture is sized for, Hz.
PWM_HZ = 560


def _defines(root: pathlib.Path, header: str,
             names: tuple[str, ...]) -> dict[str, int]:
    """Integer #defines of a header.

    Args:
        root: The repository.
        header: The header, relative to it.
        names: The macros to read.

    Returns:
        Each macro's value.

    Raises:
        BudgetError: A macro is missing.
    """
    text = (root / header).read_text(encoding="utf-8")
    val = {}
    for name in names:
        m = re.search(r"#define\s+%s\s+(\d+)u?\b" % name, text)
        if m is None:
            raise BudgetError(f"{header}: no {name}")
        val[name] = int(m.group(1))
    return val


@dataclasses.dataclass(frozen=True)
class Rates:
    """Capture events a second of one output of each PIO kind.

    Attributes:
        plain: A plain DShot output.
        bidir: A bidirectional DShot output.
        ppm: A PPM output.
    """

    plain: int
    bidir: int
    ppm: int


def dshot_events(root: pathlib.Path = ROOT) -> tuple[int, int]:
    """Events a second of a plain and a bidirectional DShot output.

    Every bit of a frame is a rise and a fall (dshot.pio), so a frame of
    DSHOT_FRAME_BITS bits is 2 edges a bit; a bidirectional output adds up
    to DSHOT_TELEM_BITS edges of the reply (protocols/dshot/include/
    dshot.h).  Frames go out at OUT_RENDER_DSHOT_HZ
    (shared/outputs/include/out_render.h).

    These are the edges on the pad.  Whether CAPTURE sees them is
    input_cleared()'s question: pio_gpio_init() is gpio_set_function(),
    which sets the pad's input enable (pico-sdk 2.3.0 gpio.c), and CAPTURE
    cannot leave the output pins out by its window: it reads one run of
    pins from its IN base, the outputs sit between the inputs (output k on
    GPIO14 + 2k, input k one above), and PIO has no instruction that masks
    a read.  gpio_set_input_enabled(pin, false) after pio_gpio_init()
    makes a plain DShot or PPM pad read 0 to every state machine, which
    takes its events out of the ring and leaves its output untouched; a
    bidirectional DShot pad keeps its input, because its receiver reads
    the reply on it.

    Args:
        root: The repository.

    Returns:
        (plain, bidirectional) events a second.

    Raises:
        BudgetError: A constant is missing.
    """
    bits = _defines(root, "protocols/dshot/include/dshot.h",
                    ("DSHOT_FRAME_BITS", "DSHOT_TELEM_BITS"))
    hz = _defines(root, "shared/outputs/include/out_render.h",
                  ("OUT_RENDER_DSHOT_HZ",))["OUT_RENDER_DSHOT_HZ"]
    frame = 2 * bits["DSHOT_FRAME_BITS"]
    return frame * hz, (frame + bits["DSHOT_TELEM_BITS"]) * hz


@dataclasses.dataclass(frozen=True)
class Traffic:
    """The edges a programming session puts on its pin.

    Attributes:
        rate: Edges a second at most, while it sends or receives.
        burst_us: The longest transaction, us.
        what: How the figures come about.
    """

    rate: int
    burst_us: int
    what: str

    @property
    def events(self) -> int:
        """Edges of the longest transaction at the rate."""
        return self.rate * self.burst_us // 1000000


def session_traffic(root: pathlib.Path = ROOT) -> dict[str, Traffic]:
    """Each session's edges on its pin, from the protocol headers.

    A line that sends bits or half-cells changes level at most once a bit
    or half-cell.  The Hitec precharge sampler drives the pad high and
    releases it HITEC_PRE_SAMPLES_PER_BIT times a bit, and a servo that
    holds the line low, as it does at rest, pulls it down after every
    precharge: 2 edges a sample for the whole window.  pre_take() keeps
    the pad's input enabled, so CAPTURE sees them.

    Args:
        root: The repository.

    Returns:
        Traffic by session label.

    Raises:
        BudgetError: A constant is missing.
    """
    kst = _defines(root, "protocols/kst/include/kst_wire.h",
                   ("KST_HALF_CELL_NS", "KST_FRAME_MAX_HALF_CELLS",
                    "KST_WRITE_WINDOW_NS"))
    ew = _defines(root, "protocols/escwire/include/ew_wire.h",
                  ("EW_BIT_TEXT_NS", "EW_RX_TEXT_MAX"))
    hw = _defines(root, "protocols/hitec/include/hitec_wire.h",
                  ("HITEC_D_BIT_RATE", "HITEC_D_WINDOW_US"))
    pre = _defines(root, "protocols/hitec/include/hitec_pre.h",
                   ("HITEC_PRE_SAMPLES_PER_BIT", "HITEC_PRE_WINDOW_MAX_US"))
    half = kst["KST_HALF_CELL_NS"]
    return {
        KST.label: Traffic(
            1000000000 // half,
            (kst["KST_FRAME_MAX_HALF_CELLS"] * half
             + kst["KST_WRITE_WINDOW_NS"]) // 1000,
            "1 edge a half-cell; the longest frame and a write's reply "
            "window"),
        ESCWIRE.label: Traffic(
            1000000000 // ew["EW_BIT_TEXT_NS"],
            ew["EW_RX_TEXT_MAX"] * 10 * ew["EW_BIT_TEXT_NS"] // 1000,
            "1 edge a bit at 38400 baud; the longest text answer"),
        HITEC.label: Traffic(
            hw["HITEC_D_BIT_RATE"], hw["HITEC_D_WINDOW_US"],
            "1 edge a bit at the D series rate; its receive window"),
        HITEC_PRE.label: Traffic(
            2 * pre["HITEC_PRE_SAMPLES_PER_BIT"] * hw["HITEC_D_BIT_RATE"],
            pre["HITEC_PRE_WINDOW_MAX_US"],
            "2 edges a precharge sample; the longest sampled window"),
    }


#: The drivers whose pads the capture would see, by kind.
DRIVER_SOURCES = {"plain": "protocols/dshot/rp2350/out_dshot.c",
                  "ppm": "protocols/ppm/rp2350/out_ppm.c"}
_INPUT_OFF_RE = re.compile(r"gpio_set_input_enabled\(\s*pin\s*,\s*false\s*\)")


def input_cleared(root: pathlib.Path, kind: str) -> bool:
    """Whether a driver clears its pad's input enable while its block drives.

    Read from the driver's source: a call of
    gpio_set_input_enabled(pin, false), which follows pio_gpio_init()
    before the state machine runs.

    Args:
        root: The repository.
        kind: "plain" or "ppm", a key of DRIVER_SOURCES.

    Returns:
        True when the source makes the call.

    Raises:
        BudgetError: The source is missing.
    """
    path = root / DRIVER_SOURCES[kind]
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as e:
        raise BudgetError(f"{DRIVER_SOURCES[kind]}: {e}") from e
    return _INPUT_OFF_RE.search(text) is not None


def capture_rates(root: pathlib.Path = ROOT) -> Rates:
    """The capture rates of the tree's DShot and PPM drivers.

    A plain DShot or PPM output whose driver clears its pad's input enable
    (input_cleared()) puts no event in the ring: its rate is 0.

    Args:
        root: The repository.

    Returns:
        The rates.
    """
    plain, bidir = dshot_events(root)
    ppm = ppm_events(root)
    return Rates(0 if input_cleared(root, "plain") else plain, bidir,
                 0 if input_cleared(root, "ppm") else ppm)


def ppm_events(root: pathlib.Path = ROOT) -> int:
    """Events a second of a PPM output at its shortest frame.

    The figure is the upper bound by protocols/ppm/include/ppm.h: 2 edges
    a run and PPM_MAX_CHANNELS + 1 runs a frame, in a frame of
    PPM_MAX_CHANNELS channels at PPM_CHANNEL_MIN_US and a gap of
    PPM_SYNC_MIN_US.

    Args:
        root: The repository.

    Returns:
        Events a second, rounded down.

    Raises:
        BudgetError: A constant is missing.
    """
    val = _defines(root, "protocols/ppm/include/ppm.h",
                   ("PPM_MAX_CHANNELS", "PPM_CHANNEL_MIN_US",
                    "PPM_SYNC_MIN_US"))
    n = val["PPM_MAX_CHANNELS"]
    frame_us = n * val["PPM_CHANNEL_MIN_US"] + val["PPM_SYNC_MIN_US"]
    return 2 * (n + 1) * 1000000 // frame_us


def capture_rate(plain: int, pairs: int, ppm: int, rates: Rates) -> int:
    """Board 3's capture events a second for a mix.

    Args:
        plain: Plain DShot outputs.
        pairs: Bidirectional DShot outputs.
        ppm: PPM outputs.
        rates: Events a second of one output of each kind.

    Returns:
        The 9 inputs and every output at its fastest.
    """
    pwm = RSN_OUTPUTS - plain - pairs - ppm
    return (RSN_INPUTS * PWM_HZ * 2 + pwm * PWM_HZ * 2
            + plain * rates.plain + pairs * rates.bidir + ppm * rates.ppm)


def ring_ms(rate: int) -> float:
    """How long the ring holds at an event rate.

    Args:
        rate: Events a second.

    Returns:
        ms.
    """
    return RING_EVENTS * 1000.0 / rate if rate else float("inf")


@dataclasses.dataclass
class Sweep:
    """Board 3's sweep.

    Attributes:
        runs: Mixes run.
        allowed: Mixes the table allows.
        failures: Allowed mixes that do not fit, described.
        refused_fit: Mixes the table refuses that fit all the same.
        worst_rate: The highest capture rate of an allowed mix.
        worst_mix: Its outputs, described.
        lapping: The outputs of every allowed mix whose ring holds less
            than a flash window, described.
        worst_quiet: The highest rate with plain DShot and PPM pads that
            read 0 (their input enable cleared).
        lapping_quiet: lapping with those pads read 0.
        explained: Refused mixes that fit, per named rule they break.
        session_worst: Per session label, the highest capture rate of an
            allowed mix during its transactions, its own pin included.
    """

    runs: int = 0
    allowed: int = 0
    failures: list[str] = dataclasses.field(default_factory=list)
    refused_fit: int = 0
    worst_rate: int = 0
    worst_mix: str = ""
    lapping: list[str] = dataclasses.field(default_factory=list)
    worst_quiet: int = 0
    lapping_quiet: list[str] = dataclasses.field(default_factory=list)
    explained: dict[str, int] = dataclasses.field(default_factory=dict)
    session_worst: dict[str, int] = dataclasses.field(default_factory=dict)


def _outputs(plain: int, pairs: int, ppm: int) -> str:
    """Board 3's PIO outputs of a mix, the PWM outputs left out.

    Args:
        plain: Plain DShot outputs.
        pairs: Bidirectional DShot outputs.
        ppm: PPM outputs.

    Returns:
        For example "4 bidirectional" or "6 plain + 1 PPM".
    """
    parts = [f"{n} {what}" for n, what in ((pairs, "bidirectional"),
                                           (plain, "plain"), (ppm, "PPM"))
             if n]
    return " + ".join(parts) or "no DShot or PPM"


def _walk(chip: Chip, seq: str, ok: bool, limit: int, visit,
          progs: dict[str, Program]) -> None:
    """Visits every order of plain (P) and bidirectional (B) slots.

    Args:
        chip: The chip before the next slot.
        seq: The slots so far.
        ok: False once a slot was refused.
        limit: The most slots.
        visit: Called with (chip, seq, ok) at every node.
        progs: The programs.
    """
    visit(chip, seq, ok)
    if len(seq) < limit:
        for kind, use in (("P", DSHOT), ("B", BIDIR)):
            nxt = apply(chip, use, progs)
            _walk(nxt or chip, seq + kind, ok and nxt is not None, limit,
                  visit, progs)


def sweep(board: Board, progs: dict[str, Program], rates: Rates,
          traffic: dict[str, Traffic] | None = None) -> Sweep:
    """Runs every slot order with every session on board 3.

    Args:
        board: Board 3.
        progs: Its programs.
        rates: Capture events a second of one output of each kind.
        traffic: Each session's edges on its pin; none counted without.

    Returns:
        The sweep.

    Raises:
        BudgetError: ppm_tx and dshot_tx no longer place alike.
    """
    a, b = progs["ppm_tx"], progs["dshot_tx"]
    if (a.length, a.origin, PPM.steps[0][0]) != (b.length, b.origin, "sm"):
        raise BudgetError("ppm_tx and dshot_tx differ: the sweep's PPM "
                          "slots no longer place as plain DShot slots")
    records = []
    boot = run(board, (), progs)

    def slots_first(chip, seq, ok):
        records.append((None, "", seq, ok, chip.dma))
        if len(seq) < RSN_OUTPUTS:
            for s in SESSIONS:
                nxt = apply(chip, s, progs)
                records.append((s, "after", seq, ok and nxt is not None,
                                chip.dma + s.dma))

    _walk(boot.chip, "", boot.fits, RSN_OUTPUTS, slots_first, progs)
    for s in SESSIONS:
        start = apply(boot.chip, s, progs)
        _walk(start or boot.chip, "", boot.fits and start is not None,
              RSN_OUTPUTS - 1,
              lambda chip, seq, ok, s=s: records.append(
                  (s, "before", seq, ok, chip.dma)), progs)

    out = Sweep()
    for session, order, seq, ok, dma in records:
        singles, pairs = seq.count("P"), seq.count("B")
        for ppm in range(singles + 1):
            out.runs += 1
            fits = ok and dma + ppm * PPM.dma <= NUM_DMA
            what = (f"{singles - ppm} plain, {pairs} bidirectional, {ppm} "
                    f"PPM in the order {seq or '-'}")
            if session is not None:
                what += f", {session.label} opened {order} the slots"
            if not rsn_allows(session, singles, pairs, ppm):
                if fits:
                    out.refused_fit += 1
                    broken = rsn_broken(session, singles, pairs, ppm)
                    for name in broken:
                        out.explained[name] = out.explained.get(name, 0) + 1
                    if not broken:
                        out.failures.append("refused for no named rule, "
                                            "fits: " + what)
                continue
            out.allowed += 1
            if not fits:
                out.failures.append("allowed, does not fit: " + what)
            rate = capture_rate(singles - ppm, pairs, ppm, rates)
            if session is not None:
                # The session's pin is one of the 9 outputs: its edges in
                # place of a PWM output's.
                rate += ((traffic or {}).get(session.label, Traffic(
                    2 * PWM_HZ, 0, "")).rate - 2 * PWM_HZ)
                key = session.label
                out.session_worst[key] = max(out.session_worst.get(key, 0),
                                             rate)
                continue
            outputs = _outputs(singles - ppm, pairs, ppm)
            quiet = capture_rate(singles - ppm, pairs, ppm,
                                 Rates(0, rates.bidir, 0))
            for r, laps in ((rate, out.lapping), (quiet, out.lapping_quiet)):
                if ring_ms(r) < FLASH_WINDOW_MS and outputs not in laps:
                    laps.append(outputs)
            if rate > out.worst_rate:
                out.worst_rate, out.worst_mix = rate, outputs
            out.worst_quiet = max(out.worst_quiet, quiet)
    for laps in (out.lapping, out.lapping_quiet):
        laps.sort()
    return out


# ------------------------------------------------------------------- report

def _row(chip: Chip) -> str:
    """The block and DMA columns of a report line."""
    cells = [f"{chip.words(b):2d}/{PIO_WORDS - chip.words(b):2d} "
             f"{chip.sms_used(b)}" for b in range(NUM_PIOS)]
    return "  ".join(cells) + f"  {chip.dma:2d}/{NUM_DMA}"


def report(board: Board, progs: dict[str, Program],
           rates: Rates,
           traffic: dict[str, Traffic] | None = None
           ) -> tuple[list[str], list[str]]:
    """The budget of one board.

    Args:
        board: The board.
        progs: The programs read from the files.
        rates: Capture events a second of one output of each kind.
        traffic: Each session's edges on its pin.

    Returns:
        The report's lines and the failures.
    """
    bp = board_programs(board, progs)
    lines = [f"Board {board.number}, {board.name}: GPIO{board.gpio[0]} to "
             f"GPIO{board.gpio[1]}, every block at base {board.base()}"]
    uses = board.boot + tuple(u for m in board.mixes for u in m.uses)
    uses += SESSIONS if board.sweep else ()
    names = []
    for use in uses:
        for step in use.steps:
            for name in step[1:]:
                if isinstance(name, str) and name not in names:
                    names.append(name)
    lines.append("  programs: " + ", ".join(
        f"{n} {bp[n].length}"
        + (" (planned)" if bp[n].source.startswith("planned") else "")
        for n in names))
    lines.append("  words used/free and state machines per block, DMA "
                 "channels:")
    lines.append("  PIO0    PIO1    PIO2    DMA    result    mix")
    failures = []
    for mix in board.mixes:
        out = run(board, mix.uses, bp)
        verdict = "fits" if out.fits else "refused"
        want = "fits" if mix.fits else "refused"
        line = f"  {_row(out.chip)}  {verdict:8s}  {mix.name}"
        if out.refused:
            line += f" [refused: {', '.join(out.refused)}]"
        if out.fits != mix.fits:
            line += f"  FAIL: the table says {want}"
            failures.append(f"board {board.number}: {mix.name}: {verdict}, "
                            f"the table says {want} ({mix.ref})")
        lines.append(line)
    if board.sweep:
        sw = sweep(board, bp, rates, traffic)
        lines.append(f"  sweep: {sw.runs} mixes run, {sw.allowed} allowed by "
                     f"the table; {sw.refused_fit} the table refuses fit "
                     f"the model, each refused by a named rule: "
                     + ", ".join(f"{r.name} {sw.explained.get(r.name, 0)}"
                                 for r in RSN_RULES))
        for r in RSN_RULES:
            lines.append(f"    {r.name}: {r.text}")
        for session in SESSIONS:
            got = session_spare(board, session, bp)
            want = RSN_SESSION_SPARE[session.label]
            if got != want:
                failures.append(
                    f"board {board.number}: {session.label}: {got} plain "
                    f"DShot fit beside the line in its block, "
                    f"RSN_SESSION_SPARE says {want}")
        seen = {k: ("read 0 to it (input enable cleared by the driver)"
                    if v == 0 else "are captured")
                for k, v in (("plain", rates.plain), ("ppm", rates.ppm))}
        lines.append(f"  capture: plain DShot pads {seen['plain']}; PPM "
                     f"pads {seen['ppm']}")
        lines.append(f"  capture: {sw.worst_rate:,} events/s at most "
                     f"({sw.worst_mix}): the ring holds "
                     f"{ring_ms(sw.worst_rate):.1f}ms against a "
                     f"{FLASH_WINDOW_MS}ms flash window; it covers the "
                     f"window below "
                     f"{RING_EVENTS * 1000 // FLASH_WINDOW_MS:,} events/s")
        lines.append(f"  a flash window can lap the ring with "
                     f"{len(sw.lapping)} output mixes (a gap in the "
                     f"capture figures, not an error): "
                     + ("; ".join(sw.lapping) or "none"))
        lines.append(f"  with plain DShot and PPM pads at input enable "
                     f"off: {sw.worst_quiet:,} events/s at most, "
                     f"{ring_ms(sw.worst_quiet):.1f}ms; laps with "
                     + ("; ".join(sw.lapping_quiet) or "none"))
        lines.append("  during a programming session the capture figures "
                     "are not guaranteed and are marked invalid; "
                     "pass-through (MIRROR) does not use the ring:")
        for session in SESSIONS:
            t = (traffic or {}).get(session.label)
            if t is None:
                continue
            worst = sw.session_worst.get(session.label, 0)
            lines.append(
                f"    {session.label}: {t.rate:,} edges/s on its pin, "
                f"transactions up to {t.burst_us / 1000:.1f}ms, "
                f"{t.events:,} edges ({t.what}); with the busiest allowed "
                f"slots {worst:,} events/s, the ring holds "
                f"{ring_ms(worst):.1f}ms")
        failures += [f"board {board.number}: {f}" for f in sw.failures]
    return lines, failures


def main(argv: list[str] | None = None) -> int:
    """Prints the budget of every board; exits 1 on a failure."""
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--board", type=int, choices=[b.number for b in BOARDS],
                    help="report this board only")
    ap.add_argument("--check", action="store_true",
                    help="print the failures only")
    args = ap.parse_args(argv)
    try:
        progs = load_programs()
        rates = capture_rates()
        traffic = session_traffic()
        failures = []
        for board in BOARDS:
            if args.board not in (None, board.number):
                continue
            lines, bad = report(board, progs, rates, traffic)
            failures += bad
            if not args.check:
                print("\n".join(lines) + "\n")
    except BudgetError as e:
        print(f"pio_budget: {e}", file=sys.stderr)
        return 1
    for f in failures:
        print(f)
    if failures:
        print(f"{len(failures)} mix(es) against the table", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
