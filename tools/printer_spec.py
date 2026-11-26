"""The definitions both halves of the printer file reader share.

The word lists, limits and token kinds of the format, the reader's
refusal, the parsed spec (prn_spec_t of protocols/prnengine) and the
checks of a token's text.  tools/printer_files.py is the tool; this module
and printer_steps.py are its parts.

SPDX-License-Identifier: MIT
"""

from __future__ import annotations

FILE_MAX = 8192
LINE_MAX = 120
LINES_MAX = 256
TOKENS = 42
TOKENS_POOL = 384
CHUNK_MAX = 1024
LINE_BYTES = 128
RX_MAX = 128

FIELDS = [
    "busy", "printing", "buf_full", "cover", "no_label", "label_end",
    "label_err", "label_mode", "paper_out", "battery_low", "head_hot",
    "roll_w", "roll_h", "roll_gap", "roll_type", "left", "uid", "count",
    "page_index", "result", "battery", "x0", "x1", "x2", "x3",
]
VARS = [
    "stream_len", "pages", "frames", "lines", "line_bytes", "head_dots",
    "density", "speed", "mlead", "mtrail", "roll_w", "roll_h", "roll_gap",
    "pflags", "idx", "total", "line_no", "lay_w", "lay_h", "c0", "c1",
    "uid0", "w0", "h0", "v0", "v1", "v2", "v3",
]
V_PFLAGS, V_IDX, V_TOTAL, V_LINE_NO, V_C0 = 14, 15, 16, 17, 20
ENCS = ["u8", "le16", "be16", "le32", "be32"]
CKS = ["sum8", "sum16le", "sum16be", "xor8", "crc8", "crc16ccitt_le",
       "crc16ccitt_be", "crc32le", "svpage"]
CK_SVPAGE = 8
CODES = ["", "COVER", "NO_LABEL", "LABEL", "BATTERY", "HOT", "BUSY",
         "SILENT", "", "", "NOT_PRINTED", "ROLL"]
SILENT = 7
FKINDS = ["bit", "u8", "le16", "be16", "le32", "be32", "bytes"]
EDGES = ["top", "bottom", "left", "right"]
T_BYTE, T_ARG, T_VAR, T_LEN, T_CK, T_DATA = range(6)
C_TMPL, C_PAGE, C_CHUNK, C_ROWS, C_CHECK, C_STEP = range(6)
S_SEND, S_DATA, S_WAIT = range(3)
RELINK_VERDICT, RELINK_NONE = 0xFE, 0xFF
OP_SET, OP_CLEAR, OP_EQ_N, OP_NE_N, OP_EQ_V, OP_EQ_V1, OP_NE_V = range(7)

# once-keys, as pp_key_t in prn_parse_int.h
KEYS = ("id name version licence service cccd mtu conn head dots line feed "
        "dot0 align margin rsource rlimits density psize plines phead "
        "pflags compress csize cpad cmax cframe rows wcmd wdata speed "
        "rxjoin rxtail rxcheck rxkey query print abort relinkms answerms "
        "verdict cmd").split()
REQUIRED = ("id name version service cmd mtu conn head dots line feed dot0 "
            "align margin rsource rlimits wcmd wdata rxjoin rxkey query "
            "print verdict").split()
SUBS = ("send data wait expect poll until within tries else flags relink "
        "roll delay").split()


class Refused(Exception):
    """A file the reader refuses: its code."""

    def __init__(self, code: int) -> None:
        """Keep the code.

        Args:
            code: E01 to E30 as a number.
        """
        super().__init__(code)
        self.code = code


def enc_width(enc: int) -> int:
    """Bytes of an encoding.

    Args:
        enc: the index in ENCS.

    Returns:
        1, 2 or 4.
    """
    return (1, 2, 2, 4, 4)[enc]


def ck_width(kind: int) -> int:
    """Bytes of a checksum kind.

    Args:
        kind: the index in CKS.

    Returns:
        1, 2 or 4.
    """
    return (1, 2, 2, 1, 1, 2, 2, 4, 2)[kind]


def num_text(t: str, lo: int, hi: int) -> int:
    """Read a number: decimal or 0x hexadecimal, at most 10 characters.

    Args:
        t: the text.
        lo: the least value.
        hi: the largest value.

    Returns:
        The value.

    Raises:
        Refused: E07.
    """
    if not t or len(t) > 10:
        raise Refused(7)
    if len(t) > 2 and t[0] == "0" and t[1] in "xX":
        digits = t[2:]
        if any(c not in "0123456789abcdefABCDEF" for c in digits):
            raise Refused(7)
        x = int(digits, 16)
    else:
        if any(c not in "0123456789" for c in t):
            raise Refused(7)
        x = int(t)
    if x < lo or x > hi:
        raise Refused(7)
    return x


def hexbytes(t: str, most: int) -> bytes | None:
    """Read two hexadecimal digits a byte.

    Args:
        t: the text.
        most: the most bytes.

    Returns:
        The bytes, or None when the text is not 2 to 2 x most digits.
    """
    if len(t) < 2 or len(t) % 2 or len(t) // 2 > most:
        return None
    if any(c not in "0123456789abcdefABCDEF" for c in t):
        return None
    return bytes.fromhex(t)


def word(t: str, words: list[str]) -> int:
    """The index of a word in a list.

    Args:
        t: the text.
        words: the list.

    Returns:
        The index, or -1; an empty text matches nothing.
    """
    if not t:
        return -1
    return words.index(t) if t in words else -1


def var_in(var: int, ctx: int) -> bool:
    """Whether a variable may appear in a context.

    Args:
        var: the variable, from 1; 0 for none.
        ctx: a C_* context.

    Returns:
        True when it may.
    """
    if var == V_PFLAGS:
        return ctx == C_PAGE
    if var in (V_IDX, V_TOTAL):
        return ctx == C_CHUNK
    if var == V_LINE_NO:
        return ctx == C_ROWS
    return var > 0


def offset(t: str) -> int | None:
    """A signed offset, -128 to 127.

    Args:
        t: the text.

    Returns:
        The offset, or None.
    """
    digits = t[1:] if t.startswith("-") else t
    if not 1 <= len(digits) <= 3 or any(c not in "0123456789" for c in digits):
        return None
    x = -int(digits) if t.startswith("-") else int(digits)
    return x if -128 <= x <= 127 else None


class Spec:
    """A parsed printer file: the fields of prn_spec_t."""

    def __init__(self) -> None:
        """An empty spec, as memset() leaves prn_spec_t."""
        self.id = self.name = self.licence = ""
        self.source: list[str] = []
        self.version = 0
        self.match: list[tuple[int, bytes]] = []
        self.service = b""
        self.chr = [[False, 0, b""] for _ in range(4)]
        self.cccd_next = False
        self.mtu_min = 0
        self.conn = [0, 0, 0, 0]
        self.head_dots = self.dots_mm = self.line_bytes = 0
        self.line_msb = self.line_black0 = self.line_reverse = False
        self.feed = self.dot0 = 0
        self.align_center = False
        self.align_dot = self.mlead = self.mtrail = 0
        self.roll_fixed = False
        self.roll: list[tuple[int, int, int]] = []
        self.roll_lim = [0, 0, 0, 0]
        self.density = 0
        self.page_size = self.page_lines = 0
        self.page_head = (0, 0)
        self.page_flags = [0, 0]
        self.lzma = False
        self.lzma_dict = 0
        self.chunk_size = 0
        self.chunk_pad = False
        self.chunk_pad_byte = self.chunk_max = 0
        self.frame = (0, 0)
        self.cmd_nr = self.data_nr = False
        self.frag = self.gap_ms = 0
        self.speed_stream = False
        self.speed: list[tuple[int, int]] = []
        self.speed_else = 0
        self.tmpl: list[tuple[str, tuple[int, int]]] = []
        self.tok: list[tuple[int, int, int, int]] = []
        self.join = 0
        self.magic = b""
        self.len_off = self.len_enc = self.len_add = 0
        self.rx_min = self.rx_max = 0
        self.tail = b""
        self.rx_check: tuple[int, int, int, int] | None = None
        self.key_off = 0
        self.answer: list[tuple[int, int]] = []
        self.field: list[tuple[int, int, int, int, int]] = []
        self.error: list[tuple[int, int]] = []
        self.step: list[dict] = []
        self.query: list[int] = []
        self.print: list[int] = []
        self.abort: dict | None = None
        self.abort_from = self.abort_to = 0
        self.relink_ms = self.answer_ms = 0
        self.verdict = 0
        self.vd_field = self.vd_key = self.vd_reads = self.vd_gap_ms = 0
        self.vd_send = new_send()
        self.result: list[int] = []

    def field_find(self, key: int, name: int) -> int:
        """The field line of a name under a key.

        Args:
            key: the answer's key.
            name: the index in FIELDS.

        Returns:
            Its index, or -1.
        """
        for i, f in enumerate(self.field):
            if f[0] == key and f[1] == name:
                return i
        return -1

    def field_any(self, name: int) -> int:
        """The first field line of a name.

        Args:
            name: the index in FIELDS.

        Returns:
            Its index, or -1.
        """
        for i, f in enumerate(self.field):
            if f[1] == name:
                return i
        return -1


def new_send() -> dict:
    """An empty send.

    Returns:
        The template index, the numbers and the variables.
    """
    return {"tmpl": 0, "n": [0, 0, 0, 0], "var": [0, 0, 0, 0]}


def new_step(name: str) -> dict:
    """A step as the reader opens it.

    Args:
        name: its name.

    Returns:
        The fields of prn_step_t.
    """
    return {"name": name, "relink_name": "", "send": new_send(),
            "until": [], "check": [], "save": [], "wait": 0, "poll": 0,
            "within": 0, "delay": 0, "kind": 0, "expect": 0,
            "has_expect": False, "tries": 1, "else": SILENT, "flags": False,
            "roll": False, "relink": RELINK_NONE}


def text(t: str, most: int, kind: int) -> str:
    """Check a text's characters.

    Args:
        t: the text.
        most: its largest length.
        kind: 0 id (a-z 0-9 -), 1 printable, 2 licence (A-Z a-z 0-9 . + -).

    Returns:
        The text.

    Raises:
        Refused: E29.
    """
    if not t or len(t) > most:
        raise Refused(29)
    for c in t:
        if kind == 0:
            ok = c in "abcdefghijklmnopqrstuvwxyz0123456789-"
        elif kind == 1:
            ok = " " <= c <= "~"
        else:
            ok = c.isascii() and (c.isalnum() or c in ".+-")
        if not ok:
            raise Refused(29)
    return t


def name_tok(t: str) -> str:
    """A template's or step's name.

    Args:
        t: the text.

    Returns:
        The name.

    Raises:
        Refused: E29.
    """
    if not t or len(t) > 11 or any(
            c not in "abcdefghijklmnopqrstuvwxyz0123456789_" for c in t):
        raise Refused(29)
    return t


def code_tok(t: str) -> int:
    """A code a file may name.

    Args:
        t: the text.

    Returns:
        The prn_err_t value.

    Raises:
        Refused: E07.
    """
    k = word(t, CODES)
    if k <= 0:
        raise Refused(7)
    return k


def placeholder(t: str, ctx: int) -> tuple[int, int, int, int]:
    """One `{...}` token.

    Args:
        t: its text inside the braces.
        ctx: a C_* context.

    Returns:
        The token (type, x, a, b).

    Raises:
        Refused: E11, E22.
    """
    part = t.split(":")
    if len(part) > 4:
        raise Refused(11)
    if len(part) == 1 and part[0] == "data":
        if ctx not in (C_CHUNK, C_ROWS):
            raise Refused(22)
        return (T_DATA, 0, 0, 0)
    lk = word(part[0], ["len", "ck"])
    if lk == 1 and len(part) == 3 and ctx == C_PAGE \
            and word(part[1], CKS) == CK_SVPAGE and part[2] == "le16":
        return (T_CK, CK_SVPAGE, 0, 0)
    if lk >= 0:
        k = word(part[1], ENCS if lk == 0 else CKS) if len(part) > 1 else -1
        a = offset(part[2]) if len(part) == 4 else None
        b = offset(part[3]) if len(part) == 4 else None
        if len(part) != 4 or k < 0 or (lk == 1 and k == CK_SVPAGE) \
                or a is None or b is None:
            raise Refused(11)
        return (T_LEN if lk == 0 else T_CK, k, a, b)
    if len(part) != 2 or ctx == C_CHECK:
        raise Refused(11)
    enc = word(part[1], ENCS)
    arg = word(part[0], ["a", "p0", "p1", "p2"])
    if enc < 0:
        raise Refused(11)
    if arg >= 0:
        if ctx != C_TMPL:
            raise Refused(11)
        return (T_ARG, enc, arg, 0)
    var = word(part[0], VARS) + 1
    if not var_in(var, ctx):
        raise Refused(11)
    return (T_VAR, enc, var, 0)


def fixed_len(s: Spec, seq: tuple[int, int]) -> int:
    """Bytes of a run without its data token.

    Args:
        s: the spec.
        seq: the run.

    Returns:
        The bytes.
    """
    n = 0
    for typ, x, _, _ in s.tok[seq[0]:seq[0] + seq[1]]:
        if typ == T_BYTE:
            n += 1
        elif typ == T_CK:
            n += ck_width(x)
        elif typ != T_DATA:
            n += enc_width(x)
    return n


def range_in(tok: tuple[int, int, int, int], n: int) -> bool:
    """Whether a range lies inside a frame and its length fits.

    Args:
        tok: the token.
        n: the frame's length.

    Returns:
        True when it does.
    """
    typ, x, a, b = tok
    f = n + a if a < 0 else a
    e = n + b if b < 0 else b
    if f < 0 or e < f or e >= n:
        return False
    if typ == T_LEN:
        w = enc_width(x)
        return w >= 4 or e - f + 1 < (1 << (8 * w))
    return True


def ranges_ok(s: Spec, seq: tuple[int, int], lo: int, hi: int) -> bool:
    """Whether a run's ranges lie inside frames of @p lo and @p hi bytes.

    Args:
        s: the spec.
        seq: the run.
        lo: the shortest frame.
        hi: the longest.

    Returns:
        True when they do.
    """
    for tok in s.tok[seq[0]:seq[0] + seq[1]]:
        if (tok[0] == T_LEN or (tok[0] == T_CK and tok[1] != CK_SVPAGE)) \
                and (not range_in(tok, lo) or not range_in(tok, hi)):
            return False
    return True
