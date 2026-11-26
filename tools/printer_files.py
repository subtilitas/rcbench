#!/usr/bin/env python3
"""Read and check label printer files (protocols/printers/*.prn).

A printer file is read in two places: by prn_parse() in
protocols/prnengine on the panel, and here, for CI and the release.  This
reader follows the same grammar and refuses a file with the same code and
line as the C reader; test/host/prn_parity.py holds the two to that on
changed files.  The format is protocols/printers/README.md.

    tools/printer_files.py --check FILE...   parse each file and hold the
                                             files of record to the repo's
                                             rules: an MIT licence line and
                                             SPDX line, a source, the id as
                                             the file name
    tools/printer_files.py --dump FILE...    print each file's result as
                                             test/host/prn_parse_dump does

Exit status: 0, or 1 when a file is refused or breaks a rule.

SPDX-License-Identifier: MIT
"""

from __future__ import annotations

import argparse
import pathlib
import sys

from printer_spec import (
    C_CHECK, C_CHUNK, C_PAGE, C_ROWS, CHUNK_MAX, EDGES, FILE_MAX,
    LINE_BYTES, LINE_MAX, LINES_MAX, REQUIRED, RELINK_VERDICT, RX_MAX,
    TOKENS, Refused, Spec, ck_width, fixed_len, hexbytes, new_send,
    num_text, ranges_ok, text, word,
)
from printer_steps import StepLines


class Reader(StepLines):
    """The reader's state (pp_t)."""

    def __init__(self, fname: str | None) -> None:
        """Start a file.

        Args:
            fname: its name for the id rule, or None.
        """
        self.s = Spec()
        self.fname = fname
        self.line = ""
        self.toks: list[tuple[int, str]] = []
        self.lineno = 0
        self.seen: set[str] = set()
        self.sub: set[str] = set()
        self.in_step = False
        self.step_lineno = 0
        self.relink_line: dict[int, int] = {}
        self.rsource_line = 0
        self.nmatch_kind = [0, 0, 0, 0]

    # -- helpers --------------------------------------------------------

    @property
    def nt(self) -> int:
        """Tokens of the line."""
        return len(self.toks)

    def t(self, i: int) -> str:
        """Token @p i, or "" past the last.

        Args:
            i: the token.

        Returns:
            Its text.
        """
        return self.toks[i][1] if i < len(self.toks) else ""

    def is_(self, i: int, w: str) -> bool:
        """Whether token @p i is a word.

        Args:
            i: the token.
            w: the word.

        Returns:
            True when equal.
        """
        return i < self.nt and self.toks[i][1] == w

    def num(self, i: int, lo: int, hi: int) -> int:
        """Token @p i as a number.

        Args:
            i: the token.
            lo: the least value.
            hi: the largest.

        Returns:
            The value.

        Raises:
            Refused: E07.
        """
        if i >= self.nt:
            raise Refused(7)
        return num_text(self.t(i), lo, hi)

    def once(self, k: str) -> None:
        """Mark a key seen.

        Args:
            k: the key.

        Raises:
            Refused: E08 when seen before.
        """
        if k in self.seen:
            raise Refused(8)
        self.seen.add(k)

    def once_n(self, k: str, nt: int, at_least: bool = False) -> None:
        """Mark a key seen and hold the line to a token count.

        Args:
            k: the key.
            nt: the count.
            at_least: the count is a least count.

        Raises:
            Refused: E08, then E07.
        """
        self.once(k)
        if (self.nt < nt) if at_least else (self.nt != nt):
            raise Refused(7)

    def has(self, k: str) -> bool:
        """Whether a key was seen.

        Args:
            k: the key.

        Returns:
            True when seen.
        """
        return k in self.seen

    def rest(self, i: int) -> str:
        """The text from token @p i to the last token's end.

        Args:
            i: the first token.

        Returns:
            The text.
        """
        at = self.toks[i][0]
        last_at, last = self.toks[-1]
        return self.line[at:last_at + len(last)]

    def uuid(self, i: int) -> bytes:
        """Token @p i as a UUID.

        Args:
            i: the token.

        Returns:
            2 or 16 bytes.

        Raises:
            Refused: E07.
        """
        t = self.t(i)
        if len(t) == 4:
            b = hexbytes(t, 2)
            if b is None:
                raise Refused(7)
            return b
        if len(t) != 36 or t[8] != "-" or t[13] != "-" or t[18] != "-" \
                or t[23] != "-":
            raise Refused(7)
        out = b""
        for at, n in ((0, 8), (9, 4), (14, 4), (19, 4), (24, 12)):
            b = hexbytes(t[at:at + n], 16 - len(out))
            if b is None or len(b) != n // 2:
                raise Refused(7)
            out += b
        return out

    # -- top-level keys -------------------------------------------------

    def identity(self) -> bool:
        """The keys id, name, version, licence, source.

        Returns:
            False for another key.
        """
        s = self.s
        if self.is_(0, "id"):
            self.once_n("id", 2)
            s.id = text(self.t(1), 23, 0)
            if self.fname is not None and self.fname != s.id + ".prn":
                raise Refused(17)
        elif self.is_(0, "name"):
            self.once_n("name", 2, True)
            s.name = text(self.rest(1), 23, 1)
        elif self.is_(0, "version"):
            self.once_n("version", 2)
            s.version = self.num(1, 1, 65535)
        elif self.is_(0, "licence"):
            self.once_n("licence", 2)
            s.licence = text(self.t(1), 23, 2)
        elif self.is_(0, "source"):
            if self.nt != 2:
                raise Refused(7)
            if len(s.source) >= 3:
                raise Refused(30)
            s.source.append(text(self.t(1), 100, 1))
        else:
            return False
        return True

    def match(self) -> None:
        """A match line."""
        kinds = ["oui", "name", "iname", "svc"]
        s = self.s
        if self.nt < 3:
            raise Refused(6 if self.nt == 2 and word(self.t(1), kinds) < 0
                          else 7)
        kind = word(self.t(1), kinds)
        if kind < 0:
            raise Refused(6)
        cap = 2 if kind == 3 else 4
        if self.nmatch_kind[kind] >= cap or len(s.match) >= 14:
            raise Refused(30)
        if kind == 0:
            t = self.t(2)
            parts = [hexbytes(t[k:k + 2], 1) for k in (0, 3, 6)]
            if self.nt != 3 or len(t) != 8 or t[2] != ":" or t[5] != ":" \
                    or None in parts:
                raise Refused(7)
            b = b"".join(parts)
        elif kind == 3:
            if self.nt != 3:
                raise Refused(7)
            b = self.uuid(2)
        else:
            b = text(self.rest(2), 23, 1).encode()
        self.nmatch_kind[kind] += 1
        s.match.append((kind, b))

    def gatt(self) -> bool:
        """The keys service, chr, cccd, mtu_min, conn.

        Returns:
            False for another key.
        """
        s = self.s
        if self.is_(0, "service"):
            self.once_n("service", 2)
            s.service = self.uuid(1)
        elif self.is_(0, "chr"):
            if self.nt < 4:
                raise Refused(7)
            r = word(self.t(1), ["cmd", "data", "rx", "other"])
            if r < 0:
                raise Refused(7)
            if s.chr[r][0]:
                raise Refused(8)
            if r == 0:
                self.seen.add("cmd")
            uuid = self.uuid(2)
            s.chr[r][2] = uuid
            props = 0
            for i in range(3, self.nt):
                k = word(self.t(i), ["write", "write_nr", "notify"])
                if k < 0 or props & (1 << k):
                    raise Refused(7)
                props |= 1 << k
            s.chr[r][1] = props
            s.chr[r][0] = True
        elif self.is_(0, "cccd"):
            self.once_n("cccd", 2)
            if not self.is_(1, "next") and not self.is_(1, "discover"):
                raise Refused(7)
            s.cccd_next = self.is_(1, "next")
        elif self.is_(0, "mtu_min"):
            self.once_n("mtu", 2)
            s.mtu_min = self.num(1, 23, 247)
        elif self.is_(0, "conn"):
            self.once_n("conn", 5)
            a = self.num(1, 6, 3200)
            b = self.num(2, a, 3200)
            c = self.num(3, 0, 499)
            d = self.num(4, 10, 3200)
            if d * 4 <= (1 + c) * b:
                raise Refused(7)
            s.conn = [a, b, c, d]
        else:
            return False
        return True

    def head(self) -> bool:
        """The keys head_dots, dots_mm, line, density.

        Returns:
            False for another key.
        """
        s = self.s
        if self.is_(0, "head_dots"):
            self.once_n("head", 2)
            s.head_dots = self.num(1, 8, 1024)
        elif self.is_(0, "dots_mm"):
            self.once_n("dots", 2)
            s.dots_mm = self.num(1, 8, 8)
        elif self.is_(0, "line"):
            self.once_n("line", 5)
            if not self.has("head"):
                raise Refused(9)
            s.line_bytes = self.num(1, (s.head_dots + 7) // 8, LINE_BYTES)
            if self.t(2) not in ("lsb", "msb") or self.t(3) not in ("1", "0") \
                    or self.t(4) not in ("no", "yes"):
                raise Refused(7)
            s.line_msb = self.is_(2, "msb")
            s.line_black0 = self.is_(3, "0")
            s.line_reverse = self.is_(4, "yes")
        elif self.is_(0, "density"):
            self.once_n("density", 2)
            s.density = self.num(1, 0, 255)
        else:
            return False
        return True

    def label(self) -> None:
        """The label keys."""
        s = self.s
        if self.nt < 2:
            raise Refused(6)
        if self.is_(1, "feed") or self.is_(1, "dot0"):
            feed = self.is_(1, "feed")
            self.once_n("feed" if feed else "dot0", 3)
            edge = word(self.t(2), EDGES)
            if edge < 0:
                raise Refused(7)
            if feed:
                s.feed = edge
            else:
                s.dot0 = edge
            if self.has("feed") and self.has("dot0") \
                    and (s.feed < 2) == (s.dot0 < 2):
                raise Refused(21)
        elif self.is_(1, "align"):
            self.once_n("align", 3)
            if self.is_(2, "center"):
                s.align_center = True
                return
            if not self.has("head"):
                raise Refused(9)
            s.align_dot = self.num(2, 0, s.head_dots - 1)
        elif self.is_(1, "margin"):
            self.once_n("margin", 4)
            s.mlead = self.num(2, 0, 64)
            s.mtrail = self.num(3, 0, 64)
        else:
            raise Refused(6)

    def roll(self) -> None:
        """The roll keys."""
        s = self.s
        if self.is_(1, "source"):
            self.once_n("rsource", 3)
            if self.t(2) not in ("record", "fixed"):
                raise Refused(7)
            s.roll_fixed = self.is_(2, "fixed")
            self.rsource_line = self.lineno
            return
        fixed = self.is_(1, "fixed")
        if not fixed and not self.is_(1, "limits"):
            raise Refused(6)
        if not fixed:
            self.once("rlimits")
        if self.nt != (5 if fixed else 6):
            raise Refused(7)
        if not self.has("head") or not self.has("dots"):
            raise Refused(9)
        wmax = s.head_dots // s.dots_mm
        if fixed:
            if len(s.roll) >= 8:
                raise Refused(30)
            s.roll.append((self.num(2, 10, wmax), self.num(3, 10, 200),
                           self.num(4, 0, 20)))
            return
        a = self.num(2, 10, wmax)
        b = self.num(3, a, wmax)
        c = self.num(4, 10, 200)
        d = self.num(5, c, 200)
        s.roll_lim = [a, b, c, d]

    def page(self) -> None:
        """The page keys."""
        s = self.s
        if self.is_(1, "size"):
            self.once_n("psize", 3)
            s.page_size = self.num(2, 64, 8192)
        elif self.is_(1, "lines"):
            self.once_n("plines", 3)
            if not self.has("psize") or not self.has("line"):
                raise Refused(9)
            s.page_lines = self.num(2, 1, 1024)
        elif self.is_(1, "head"):
            self.once_n("phead", 3, True)
            if not self.has("plines"):
                raise Refused(9)
            s.page_head = self.tokens(2, C_PAGE, 32)
            n = fixed_len(s, s.page_head)
            if not ranges_ok(s, s.page_head, n, n):
                raise Refused(12)
            if n + s.page_lines * s.line_bytes > s.page_size:
                raise Refused(7)
        elif self.is_(1, "flags"):
            self.once_n("pflags", 4)
            a, b = hexbytes(self.t(2), 1), hexbytes(self.t(3), 1)
            if a is None or b is None:
                raise Refused(7)
            s.page_flags = [a[0], b[0]]
        else:
            raise Refused(6)

    def frames(self) -> bool:
        """The keys compress, rows, chunk.

        Returns:
            False for another key.
        """
        s = self.s
        if self.is_(0, "compress"):
            self.once_n("compress", 2, True)
            if self.is_(1, "none") and self.nt == 2:
                return True
            if not self.is_(1, "lzma_lit") or self.nt != 3:
                raise Refused(7)
            v = self.num(2, 4096, 65536)
            if self.has("rows"):
                raise Refused(23)
            s.lzma = True
            s.lzma_dict = v
            return True
        if self.is_(0, "rows"):
            self.once_n("rows", 2, True)
            if self.has("csize") or s.lzma:
                raise Refused(23)
            if not self.has("line"):
                raise Refused(9)
            s.frame = self.tokens(1, C_ROWS, 40)
            n = fixed_len(s, s.frame) + s.line_bytes
            if not ranges_ok(s, s.frame, n, n):
                raise Refused(12)
            return True
        if not self.is_(0, "chunk"):
            return False
        if self.is_(1, "size"):
            self.once_n("csize", 3)
            if self.has("rows"):
                raise Refused(23)
            s.chunk_size = self.num(2, 1, CHUNK_MAX)
        elif self.is_(1, "pad"):
            self.once_n("cpad", 3)
            if self.is_(2, "no"):
                return True
            s.chunk_pad = True
            b = hexbytes(self.t(2), 1)
            if b is None:
                raise Refused(7)
            s.chunk_pad_byte = b[0]
        elif self.is_(1, "max"):
            self.once_n("cmax", 3)
            s.chunk_max = self.num(2, 1, 255)
        elif self.is_(1, "frame"):
            self.once_n("cframe", 3, True)
            if not self.has("csize") or not self.has("cpad"):
                raise Refused(9)
            s.frame = self.tokens(2, C_CHUNK, 40)
            fixed = fixed_len(s, s.frame)
            lo = fixed + (s.chunk_size if s.chunk_pad else 1)
            if not ranges_ok(s, s.frame, lo, fixed + s.chunk_size):
                raise Refused(12)
        else:
            raise Refused(6)
        return True

    def writes(self) -> bool:
        """The keys write and table.

        Returns:
            False for another key.
        """
        s = self.s
        if self.is_(0, "write"):
            cmd = self.is_(1, "cmd")
            if not cmd and not self.is_(1, "data"):
                raise Refused(6)
            self.once_n("wcmd" if cmd else "wdata", 3 if cmd else 5)
            if self.t(2) not in ("req", "nr"):
                raise Refused(7)
            if cmd:
                s.cmd_nr = self.is_(2, "nr")
                return True
            if not self.has("mtu"):
                raise Refused(9)
            s.data_nr = self.is_(2, "nr")
            frag = self.num(3, 16, 244)
            gap = self.num(4, 0, 500)
            if frag + 3 > s.mtu_min:
                raise Refused(18)
            s.frag, s.gap_ms = frag, gap
            return True
        if not self.is_(0, "table"):
            return False
        if not self.is_(1, "speed"):
            raise Refused(6)
        self.once_n("speed", 6, True)
        if self.t(2) not in ("mean_page", "stream_len"):
            raise Refused(7)
        s.speed_stream = self.is_(2, "stream_len")
        pairs = self.nt - 5
        if not self.is_(self.nt - 2, "else"):
            raise Refused(7)
        if pairs > 12:
            raise Refused(30)
        out: list[tuple[int, int]] = []
        for i in range(pairs):
            t = self.t(3 + i)
            if ":" not in t:
                raise Refused(7)
            a, b = t.split(":", 1)
            above = num_text(a, 0, 0xFFFFFFFF)
            value = num_text(b, 0, 65535)
            if out and above >= out[-1][0]:
                raise Refused(19)
            out.append((above, value))
        s.speed_else = self.num(self.nt - 1, 0, 65535)
        s.speed = out
        return True

    def rx(self) -> None:
        """The rx keys."""
        s = self.s
        if self.is_(1, "join"):
            self.once("rxjoin")
            if self.is_(2, "notify") and self.nt == 5:
                a = self.num(3, 1, 40)
                b = self.num(4, a, 40)
                s.join, s.rx_min, s.rx_max = 1, a, b
                return
            if not self.is_(2, "magic_len") or self.nt != 9:
                raise Refused(7)
            magic = hexbytes(self.t(3), 4) or b""
            enc = word(self.t(5), ["u8", "le16", "be16"])
            if not magic or enc < 0:
                raise Refused(7)
            off = self.num(4, 0, 8)
            add = self.num(6, 0, 16)
            lo = self.num(7, 4, RX_MAX)
            hi = self.num(8, lo, RX_MAX)
            if lo < len(magic) or lo < off + (1 if enc == 0 else 2):
                raise Refused(7)
            s.join, s.magic, s.len_off, s.len_enc = 0, magic, off, enc
            s.len_add, s.rx_min, s.rx_max = add, lo, hi
        elif self.is_(1, "tail"):
            self.once_n("rxtail", 3)
            tail = hexbytes(self.t(2), 4)
            if tail is None:
                raise Refused(7)
            s.tail = tail
        elif self.is_(1, "check"):
            self.once_n("rxcheck", 3)
            if not self.has("rxjoin"):
                raise Refused(9)
            seq = self.tokens(2, C_CHECK, 1)
            if not ranges_ok(s, seq, s.rx_min, s.rx_max):
                raise Refused(12)
            ck = s.tok[seq[0]]
            for n in (s.rx_min, s.rx_max):
                to = n + ck[3] if ck[3] < 0 else ck[3]
                if to + ck_width(ck[1]) >= n:
                    raise Refused(12)
            s.rx_check = ck
            del s.tok[seq[0]:]
        elif self.is_(1, "key"):
            self.once_n("rxkey", 3)
            if not self.has("rxjoin"):
                raise Refused(9)
            v = self.num(2, 0, 15)
            if v >= s.rx_min:
                raise Refused(7)
            s.key_off = v
        else:
            raise Refused(6)

    def times(self) -> bool:
        """The keys relink_ms and answer_ms.

        Returns:
            False for another key.
        """
        relink = self.is_(0, "relink_ms")
        if not relink and not self.is_(0, "answer_ms"):
            return False
        self.once_n("relinkms" if relink else "answerms", 2)
        if relink:
            self.s.relink_ms = self.num(1, 1000, 30000)
        else:
            self.s.answer_ms = self.num(1, 100, 60000)
        return True

    # -- the file -------------------------------------------------------

    def finish(self) -> None:
        """The checks at `end`."""
        s = self.s
        for k in REQUIRED:
            if not self.has(k):
                raise Refused(9)
        if not self.has("cframe") and not self.has("rows"):
            raise Refused(9)
        pg = sum(self.has(k) for k in ("psize", "plines", "phead", "pflags"))
        ck = sum(self.has(k) for k in ("csize", "cpad", "cmax", "cframe"))
        if pg not in (0, 4) or ck not in (0, 4):
            raise Refused(9)
        relinks = any(st["relink_name"] for st in s.step)
        if relinks and not self.has("relinkms"):
            raise Refused(9)
        if not s.match:
            raise Refused(20)
        if not self.has("answerms"):
            s.answer_ms = 1000
        for i, st in enumerate(s.step):
            if not st["relink_name"] or st["relink"] == RELINK_VERDICT:
                continue
            k = self.step_find(st["relink_name"])
            if k < 0 or k not in s.print:
                self.lineno = self.relink_line[i]
                raise Refused(25)
            st["relink"] = k
        fields = s.field_any(11) >= 0 and s.field_any(12) >= 0
        if (not s.roll) if s.roll_fixed else not fields:
            self.lineno = self.rsource_line
            raise Refused(27)

    def one_line(self, state: dict) -> None:
        """One line of the file.

        Args:
            state: header and ended flags.
        """
        line = self.line
        indented = line.startswith("  ") or line.startswith("\t")
        body = line.split("#", 1)[0]
        toks: list[tuple[int, str]] = []
        i = 0
        while True:
            while i < len(body) and body[i] in " \t":
                i += 1
            if i >= len(body):
                break
            if len(toks) == TOKENS:
                raise Refused(7)
            j = i
            while j < len(body) and body[j] not in " \t":
                j += 1
            toks.append((i, body[i:j]))
            i = j
        self.toks = toks
        if not toks:
            return
        if state["ended"]:
            raise Refused(16)
        if not state["header"]:
            if [t for _, t in toks] != ["rcbench", "printer,", "format", "1"]:
                raise Refused(5)
            state["header"] = True
            return
        if indented:
            if not self.in_step:
                raise Refused(6)
            self.step_line()
            return
        if self.in_step:
            self.close_step()
        k = self.t(0)
        if k == "end":
            if self.nt != 1:
                raise Refused(7)
            state["ended"] = True
            self.finish()
            return
        if k == "match":
            self.match()
        elif k == "label":
            self.label()
        elif k == "roll":
            self.roll()
        elif k == "page":
            self.page()
        elif k == "rx":
            self.rx()
        elif not (self.identity() or self.gatt() or self.head()
                  or self.frames() or self.writes() or self.times()
                  or self.ext()):
            raise Refused(6)


def parse(data: bytes, fname: str | None) -> tuple[Spec | None, int, int]:
    """Read a printer file.

    Args:
        data: its bytes.
        fname: its name for the id rule, or None.

    Returns:
        The spec or None, the code (0 when taken) and the line.
    """
    r = Reader(fname)
    if not data or len(data) > FILE_MAX:
        return None, 1, 0
    state = {"header": False, "ended": False}
    pos, n = 0, len(data)
    try:
        while pos < n:
            r.lineno += 1
            eol = data.find(b"\n", pos)
            eol = n if eol < 0 else eol
            end = eol
            if end > pos and eol < n and data[end - 1] == 0x0D:
                end -= 1
            if r.lineno > LINES_MAX:
                raise Refused(4)
            raw = data[pos:end]
            if any(c != 0x09 and not 0x20 <= c <= 0x7E for c in raw):
                raise Refused(2)
            if len(raw) > LINE_MAX:
                raise Refused(3)
            r.line = raw.decode("ascii")
            pos = eol + 1
            r.one_line(state)
    except Refused as e:
        return None, e.code, r.lineno
    if not state["header"]:
        return None, 5, 0
    if not state["ended"]:
        return None, 16, r.lineno
    return r.s, 0, 0


def hx(t: str) -> str:
    """A text as the C dump prints it."""
    return "'" + t.encode().hex() + "' "


def hb(b: bytes) -> str:
    """Bytes as the C dump prints them."""
    return "h" + b.hex() + " "


def seq_dump(s: Spec, seq: tuple[int, int]) -> str:
    """A token run as the C dump prints it."""
    toks = s.tok[seq[0]:seq[0] + seq[1]]
    return "[" + "".join(f"{a}.{b}.{c}.{d}," for a, b, c, d in toks) + "] "


def send_dump(d: dict) -> str:
    """A send as the C dump prints it."""
    out = f"S{d['tmpl']}:" + ":".join(
        f"{d['n'][i]}/{d['var'][i]}" for i in range(4))
    return out + " "


def cond_dump(c: tuple) -> str:
    """A condition as the C dump prints it."""
    field, op, n, var, err = c
    return f"C{field}:{op}:{n}:{var}:{err} "


def dump(s: Spec) -> str:
    """The spec as test/host/prn_parse_dump prints it.

    Args:
        s: the spec.

    Returns:
        The line, without its newline.
    """
    o = [hx(s.id), hx(s.name), hx(s.licence),
         f"v{s.version} src{len(s.source)} "]
    o += [hx(t) for t in s.source]
    o.append(f"m{len(s.match)} ")
    o += [f"{k}:" + hb(b) for k, b in s.match]
    o.append(hb(s.service))
    o += [f"c{int(u)}:{p}:" + hb(b) for u, p, b in s.chr]
    o.append(f"{int(s.cccd_next)} {s.mtu_min} {s.conn[0]} {s.conn[1]} "
             f"{s.conn[2]} {s.conn[3]}|")
    o.append(f"{s.head_dots} {s.dots_mm} {s.line_bytes} {int(s.line_msb)} "
             f"{int(s.line_black0)} {int(s.line_reverse)} {s.feed} {s.dot0} "
             f"{int(s.align_center)} {s.align_dot} {s.mlead} {s.mtrail}|")
    o.append(f"{int(s.roll_fixed)} {len(s.roll)} ")
    o += [f"{w}.{h}.{g} " for w, h, g in s.roll]
    o.append(f"{s.roll_lim[0]} {s.roll_lim[1]} {s.roll_lim[2]} "
             f"{s.roll_lim[3]} {s.density}|")
    o.append(f"{s.page_size} {s.page_lines} ")
    o.append(seq_dump(s, s.page_head))
    o.append(f"{s.page_flags[0]} {s.page_flags[1]} {int(s.lzma)} "
             f"{s.lzma_dict} {s.chunk_size} {int(s.chunk_pad)} "
             f"{s.chunk_pad_byte} {s.chunk_max} ")
    o.append(seq_dump(s, s.frame))
    o.append(f"{int(s.cmd_nr)} {int(s.data_nr)} {s.frag} {s.gap_ms} "
             f"{int(s.speed_stream)} {len(s.speed)} ")
    o += [f"{a}:{v} " for a, v in s.speed]
    o.append(f"e{s.speed_else}|T{len(s.tmpl)} ")
    for name, seq in s.tmpl:
        o.append(hx(name))
        o.append(seq_dump(s, seq))
    o.append(f"J{s.join} ")
    o.append(hb(s.magic))
    o.append(f"{s.len_off} {s.len_enc} {s.len_add} {s.rx_min} {s.rx_max} ")
    o.append(hb(s.tail))
    ck = s.rx_check or (0, 0, 0, 0)
    o.append(f"{int(s.rx_check is not None)} {ck[0]}.{ck[1]}.{ck[2]}.{ck[3]} "
             f"k{s.key_off}|A{len(s.answer)} ")
    o += [f"{k}:{m} " for k, m in s.answer]
    o.append(f"F{len(s.field)} ")
    o += [f"{a}.{b}.{c}.{d}.{e} " for a, b, c, d, e in s.field]
    o.append(f"E{len(s.error)} ")
    o += [f"{f}:{c} " for f, c in s.error]
    o.append(f"|P{len(s.step)} ")
    for st in s.step:
        o.append(hx(st["name"]))
        o.append(hx(st["relink_name"]))
        o.append(f"k{st['kind']} ")
        o.append(send_dump(st["send"]))
        o.append(f"{st['wait']} {st['poll']} {st['within']} {st['delay']} "
                 f"{int(st['has_expect'])}:{st['expect']} {st['tries']} "
                 f"{st['else']} {int(st['flags'])} {int(st['roll'])} "
                 f"{st['relink']} ")
        o += [cond_dump(c) for c in st["until"]]
        o.append("/ ")
        o += [cond_dump(c) for c in st["check"]]
        o.append("/ ")
        o += [f"{v}={f} " for v, f in st["save"]]
        o.append("; ")
    o.append("Q" + "".join(f"{k}," for k in s.query))
    o.append(" R" + "".join(f"{k}," for k in s.print))
    o.append(f" a{int(s.abort is not None)} ")
    o.append(send_dump(s.abort or new_send()))
    o.append(f"{s.abort_from} {s.abort_to} {s.relink_ms} {s.answer_ms}|"
             f"V{s.verdict} {s.vd_field} {s.vd_key} {s.vd_reads} "
             f"{s.vd_gap_ms} ")
    o.append(send_dump(s.vd_send))
    o += [f"{v}," for v in s.result]
    return "".join(o)


def dump_line(path: pathlib.Path) -> str:
    """The line test/host/prn_parse_dump prints for a file.

    Args:
        path: the file.

    Returns:
        The line, without its newline.
    """
    try:
        data = path.read_bytes()[:FILE_MAX + 2]
    except OSError:
        return f"{path}\tBAD\tE00 0"
    spec, code, line = parse(data, path.name)
    if spec is None:
        return f"{path}\tBAD\tE{code:02d} {line}"
    return f"{path}\tOK\t{dump(spec)}"


def check_file(path: pathlib.Path) -> list[str]:
    """A file of record's problems.

    Args:
        path: the file.

    Returns:
        One line per problem; empty when it holds.
    """
    data = path.read_bytes()
    spec, code, line = parse(data, path.name)
    if spec is None:
        return [f"{path}: E{code:02d} line {line}"]
    out = []
    if spec.licence != "MIT":
        out.append(f"{path}: licence {spec.licence or 'missing'}, not MIT")
    if b"\n# SPDX-License-Identifier: MIT\n" not in data:
        out.append(f"{path}: no line '# SPDX-License-Identifier: MIT'")
    if not spec.source:
        out.append(f"{path}: no source line")
    return out


def main(argv: list[str] | None = None) -> int:
    """Check or dump printer files.

    Args:
        argv: the arguments, or None for sys.argv.

    Returns:
        0, or 1 when a file is refused or breaks a rule.
    """
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    mode = ap.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true",
                      help="parse and hold to the rules of record")
    mode.add_argument("--dump", action="store_true",
                      help="print each result as prn_parse_dump does")
    ap.add_argument("files", nargs="+", type=pathlib.Path)
    args = ap.parse_args(argv)
    if args.dump:
        for path in args.files:
            print(dump_line(path))
        return 0
    bad = []
    for path in args.files:
        bad += check_file(path)
    for line in bad:
        print(line, file=sys.stderr)
    if not bad:
        print(f"{len(args.files)} printer file(s) hold")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
