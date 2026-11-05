#!/usr/bin/env python3
"""A mock of the panel's Wi-Fi page for the browser test (test/web).

The server answers as the panel's HTTP (hypertext transfer protocol) server
does for the file manager: the page, the file API of
shared/webaccess/wa_api.c and wa_api_write.c, with its JSON (JavaScript
Object Notation) bodies, headers, status codes and refusals, and the request
guard of wa_guard.c (Host, Origin, X-Rcbench-Upload and body limits).  It
keeps a card in memory and answers one request at a time, as the panel
does.

Nothing the panel sends is copied by hand:

- The page is the file wa_page_dump writes (test/host/wa_page_dump.c), the
  bytes wa_page_main() sends, with the panel's highlighter and the servo
  test's reports as the panel writes them.
- The reason words, their HTTP statuses and their sentences are read from
  wa_api.c and ui_text.def.

What differs from the panel: the port in the Host and Origin headers is the
mock's own, where the panel takes port 80 only; no access code (SETUP's
Browser writes CODE is not mocked); no ZIP of the card; no firmware update;
the card has no free-space limit.

Scripting, under /__mock/ and outside every guard and the request log:

  POST /__mock/reset   JSON {"lang": "en"|"de", "writes": "OPEN"|"HOLD"|"OFF",
                       "window": bool, "armed": bool, "logging": bool,
                       "busy": [path, ...]} sets the state, rebuilds the card
                       and clears the log.
  POST /__mock/put     JSON {"path": P, "text": T}: the panel writes file P.
  GET  /__mock/read?path=P   the bytes of file P; 404 when there is none.
  GET  /__mock/tree    JSON {"dirs": [...], "files": {path: size}}.
  GET  /__mock/log     JSON list of the requests: method, url, status,
                       if_match, if_none_match.
  GET  /__mock/spans   the lines of the report on the card, each as
                       [[class, text], ...] as text_hl_line() classes them.
  GET  /__mock/text?id=ID   JSON {"en": ..., "de": ...} of one ui_text.def
                       entry.

Usage:
  wa_mock_server.py --dump BUILD/wa_page_dump [--port N] [--host ADDR]
                    [--pages DIR]

--dump runs the page dump into a temporary folder; --pages serves a folder
already written.  The server prints "listening on http://ADDR:PORT" once it
accepts connections.

SPDX-License-Identifier: MIT
"""

from __future__ import annotations

import argparse
import http.server
import json
import pathlib
import re
import socketserver
import subprocess
import sys
import tempfile
import threading
import time
import urllib.parse
import zlib

REPO = pathlib.Path(__file__).resolve().parent.parent.parent
WA_API_C = REPO / "shared/webaccess/wa_api.c"
UI_TEXT_DEF = REPO / "shared/text/include/ui_text.def"
VERSION_H = REPO / "shared/link/include/rcbench_version.h"

NAME = "rcbench-1A2B"
PAGE_MAX = 48            # FM_PAGE_MAX
FILE_MAX = 1048576       # TEXT_BUF_FILE_MAX, WA_PUT_BODY_MAX
LINES_MAX = 65536        # TEXT_BUF_ENDS_MAX
FORM_MAX = 1024          # WA_FORM_BODY_MAX
NUL_SCAN = 4096          # bytes a NUL is looked for in
LIST_NAME_MAX = 63       # FM_LIST_NAME_MAX
NAME_MAX = LIST_NAME_MAX - 4   # FM_NAME_MAX: room for ".NEW"
PATH_MAX = 120           # FM_PATH_MAX
DEPTH_MAX = 16           # FM_DEPTH_MAX
CARD_BYTES = 15931539456
UPLOAD_HDR = "X-Rcbench-Upload"

AUTO_LOGS = "BENCH/LOGS"
USER_LOGS = "USER/LOGS"
BENCH_DIRS = (AUTO_LOGS, USER_LOGS, "BENCH/ESC/CONF", "BENCH/KST/CONF",
              "BENCH/HITEC/CONF", "BENCH/ESCW/CONF")
LOCKED_FILES = {"BENCH/KST/CONF": ("KST.TXT", "KST.NEW"),
                "BENCH/HITEC/CONF": ("HITEC.TXT", "HITEC.NEW"),
                "BENCH/ESCW/CONF": ("ESCW.TXT", "ESCW.NEW")}
FAULT_FILES = ("BENCH/RCBENCH.LOG", "BENCH/RCBENCH.OLD", "BENCH/RCBENCH.TMP")
RUN_PREFIXES = ("ATEST", "LIMIT", "SERVO", "MOTOR", "SUPPL", "RUDDR",
                "BENCH", "ESC")

REASONS = {400: "Bad Request", 403: "Forbidden", 404: "Not Found",
           409: "Conflict", 412: "Precondition Failed",
           413: "Content Too Large", 415: "Unsupported Media Type",
           421: "Misdirected Request", 422: "Unprocessable Content",
           423: "Locked", 500: "Internal Server Error",
           503: "Service Unavailable", 507: "Insufficient Storage"}


# ------------------------------------------------------------ the sources

def c_string(literals: str) -> str:
    """Join and unescape adjacent C string literals.

    Args:
        literals: One or more C string literals, each in double quotes.

    Returns:
        The text they make.
    """
    out = []
    for lit in re.findall(r'"((?:[^"\\]|\\.)*)"', literals):
        out.append(re.sub(r"\\(.)", lambda m: {"n": "\n", "t": "\t"}.get(
            m.group(1), m.group(1)), lit))
    return "".join(out)


def read_texts(path: pathlib.Path = UI_TEXT_DEF) -> dict[str, tuple[str, str]]:
    """The English and German text of every entry of ui_text.def.

    Args:
        path: The table.

    Returns:
        A map from an entry's identifier to its English and German text.
    """
    src = re.sub(r"/\*.*?\*/", "", path.read_text(encoding="utf-8"),
                 flags=re.DOTALL)
    lit = r'((?:"(?:[^"\\]|\\.)*"\s*)+)'
    rx = re.compile(r"UI_(?:WEB_)?TEXT\(\s*(\w+)\s*,\s*(?:\d+\s*,\s*)?" +
                    lit + r",\s*" + lit + r"\)")
    return {m.group(1): (c_string(m.group(2)), c_string(m.group(3)))
            for m in rx.finditer(src)}


def read_reasons(path: pathlib.Path = WA_API_C) -> dict[str, tuple[int, str]]:
    """The reason words of wa_api.c: each one's HTTP status and text id.

    Args:
        path: wa_api.c.

    Returns:
        A map from a reason word to its status and the identifier of its
        sentence ("" for none).
    """
    rx = re.compile(
        r'\[WA_API_\w+\]\s*=\s*\{\s*"(\w*)",\s*(\d+)u,\s*(\w+)\s*\}')
    out = {}
    for m in rx.finditer(path.read_text(encoding="utf-8")):
        out[m.group(1)] = (int(m.group(2)),
                           "" if m.group(3) == "P_COUNT" else m.group(3))
    return out


# --------------------------------------------------------------- the rules

def fold(name: str) -> str:
    """A name with case folded as FAT (File Allocation Table) compares.

    Args:
        name: A file name.

    Returns:
        The name with a to z and the umlauts a, o and u in upper case.
    """
    return "".join(
        c.upper() if ("a" <= c <= "z" or c in "äöü") else c for c in name)


def within(outer: str, inner: str) -> bool:
    """Whether path @p inner is path @p outer or lies in it (fm_path_within).

    Args:
        outer: A folder's path; "" is the root.
        inner: Another path.

    Returns:
        True when @p inner is @p outer or below it.
    """
    if outer == "":
        return True
    o, i = fold(outer), fold(inner)
    return i == o or i.startswith(o + "/")


def name_ok(name: str) -> bool:
    """The panel keyboard's rule for a new name (fm_name_check).

    Args:
        name: The name.

    Returns:
        True when the card takes it.
    """
    if name == "" or len(name.encode()) > NAME_MAX:
        return False
    for c in name:
        if c in "äöüÄÖÜß":
            continue
        if not " " <= c <= "~" or c in '"*/:<>?\\|':
            return False
    if name[0] in ". " or name[-1] in ". ":
        return False
    stem = name.split(".")[0].lower()
    return stem not in ("con", "prn", "aux", "nul") and not (
        len(stem) == 4 and stem[:3] in ("com", "lpt")
        and stem[3] in "123456789")


def plain_ok(path: str) -> bool:
    """Whether every name of @p path is one the card keeps (fm_path_plain).

    Args:
        path: A path from the root.

    Returns:
        True when no name has a character FAT refuses or one outside ASCII,
        as fm_path_plain() holds it.
    """
    for seg in path.split("/"):
        if seg == "" or seg[0] == " " or seg[-1] in " .":
            return False
        for i, c in enumerate(seg):
            if not " " <= c < "\x7f":
                return False
            if c in '"*:<>?\\|':
                return False
            if c == "~" and seg[i + 1:i + 2].isdigit():
                return False
    return True


def kind_of(dirpath: str, name: str, is_dir: bool) -> str:
    """The kind word of an entry (fm_kind_of).

    Args:
        dirpath: The folder.
        name: The entry's name.
        is_dir: A folder.

    Returns:
        FOLDER, RUN, REPORT, KST, PROFILE, FAULT, CSV, JSON, TEXT or OTHER.
    """
    if is_dir:
        return "FOLDER"
    path = f"{dirpath}/{name}" if dirpath else name
    if any(fold(path) == fold(f) for f in FAULT_FILES):
        return "FAULT"
    up = name.upper()
    for pre in RUN_PREFIXES:
        m = re.fullmatch(re.escape(pre) + r"(\d{3})\.(CSV|TXT)", up)
        if m and int(m.group(1)) >= 1:
            return "RUN" if m.group(2) == "CSV" else "REPORT"
    low = name.lower()
    if low.endswith(".txt"):
        return "KST" if fold(dirpath) == "BENCH/KST/CONF" else "TEXT"
    if low.endswith(".json"):
        return "PROFILE" if fold(dirpath) == "BENCH/ESC/CONF" else "JSON"
    return "CSV" if low.endswith(".csv") else "OTHER"


def eol_word(data: bytes) -> str:
    """The X-Rcbench-Eol word of a file.

    Args:
        data: The file's bytes.

    Returns:
        "mixed" for two line end kinds or more, else "crlf", "cr" or "lf".
    """
    crlf = data.count(b"\r\n")
    cr = data.count(b"\r") - crlf
    lf = data.count(b"\n") - crlf
    kinds = [k for k, n in (("lf", lf), ("crlf", crlf), ("cr", cr)) if n]
    if len(kinds) > 1:
        return "mixed"
    return kinds[0] if kinds else "lf"


def ends_of(data: bytes) -> int:
    """The number of line ends of a file, CRLF counted once.

    Args:
        data: The file's bytes.

    Returns:
        The count.
    """
    crlf = data.count(b"\r\n")
    return data.count(b"\n") + data.count(b"\r") - crlf


# --------------------------------------------------------------- the card

class Refused(Exception):
    """A refusal: the API's reason word and, for a CHANGED, the ETag found."""

    def __init__(self, word: str, etag: str | None = None) -> None:
        """A refusal.

        Args:
            word: The API's reason word.
            etag: The ETag a CHANGED found, 8 hex digits; None for none.
        """
        super().__init__(word)
        self.word = word
        self.etag = etag


class Card:
    """A card in memory: folders and files with their bytes."""

    def __init__(self) -> None:
        """An empty card: the root only."""
        self.dirs: set[str] = {""}
        self.files: dict[str, bytes] = {}
        self.mtime: dict[str, float] = {}

    def find(self, path: str) -> tuple[str, bool] | None:
        """The entry a path names, case folded as FAT finds it.

        Args:
            path: A path from the root.

        Returns:
            The path as the card spells it and whether it is a folder; None
            when no entry has the name.
        """
        want = fold(path)
        for p in self.files:
            if fold(p) == want:
                return p, False
        for p in self.dirs:
            if fold(p) == want:
                return p, True
        return None

    def children(self, dirpath: str) -> list[tuple[str, bool]]:
        """The entries of a folder, unsorted.

        Args:
            dirpath: The folder.

        Returns:
            Each entry's name and whether it is a folder.
        """
        out = []
        for p in list(self.files) + [d for d in self.dirs if d]:
            head, _, tail = p.rpartition("/")
            if head == dirpath:
                out.append((tail, p in self.dirs))
        return out

    def listing(self, dirpath: str,
                busy: frozenset[str] = frozenset()) -> tuple[list[dict], int]:
        """The listed entries of a folder in the pane's order.

        Args:
            dirpath: The folder.
            busy: The paths of the files the logger writes.

        Returns:
            The entries as the list answer gives them, folders first and
            then by name with case folded, and the count of entries left
            out: names that start with "." or are over 63 bytes.
        """
        ents = []
        unlisted = 0
        for name, is_dir in self.children(dirpath):
            if name.startswith(".") or len(name.encode()) > LIST_NAME_MAX:
                unlisted += 1
                continue
            path = f"{dirpath}/{name}" if dirpath else name
            ents.append({"n": name, "d": is_dir,
                         "s": 0 if is_dir else len(self.files[path]),
                         "k": kind_of(dirpath, name, is_dir),
                         "p": place_of(dirpath, name, is_dir),
                         "b": not is_dir and path in busy})
        ents.sort(key=rank)
        return ents, unlisted

    def write(self, path: str, data: bytes) -> None:
        """Make or replace a file.

        Args:
            path: The file's path.
            data: Its bytes.
        """
        found = self.find(path)
        key = found[0] if found else path
        self.files[key] = data
        self.mtime[key] = time.time()

    def remove(self, path: str) -> None:
        """Remove a file or an empty folder.

        Args:
            path: The entry's path as the card spells it.
        """
        self.files.pop(path, None)
        self.dirs.discard(path)
        self.mtime.pop(path, None)

    def move(self, src: str, dst: str) -> None:
        """Rename or move a file, or a folder with everything in it.

        Args:
            src: The entry's path as the card spells it.
            dst: Where it goes.
        """
        for table in (self.files, self.mtime):
            for old in [k for k in table if within(src, k)]:
                table[dst + old[len(src):]] = table.pop(old)
        for old in [d for d in self.dirs if within(src, d)]:
            self.dirs.discard(old)
            self.dirs.add(dst + old[len(src):])


def rank(e: dict) -> tuple:
    """The pane's order of an entry (fm_tree_rank).

    Args:
        e: A list entry.

    Returns:
        A sort key: folders first, then the folded name, then the bytes.
    """
    return (not e["d"], fold(e["n"]), e["n"])


def place_of(dirpath: str, name: str, is_dir: bool) -> str:
    """The place of an entry (fm_place_of).

    Args:
        dirpath: The folder.
        name: The entry's name.
        is_dir: A folder.

    Returns:
        FIXED, LOCKED, FAULT or FREE.
    """
    path = f"{dirpath}/{name}" if dirpath else name
    if is_dir:
        if path and any(within(path, d) for d in BENCH_DIRS):
            return "FIXED"
        return "FREE"
    if fold(dirpath) in LOCKED_FILES and any(
            fold(name) == fold(n) for n in LOCKED_FILES[fold(dirpath)]):
        return "LOCKED"
    if any(fold(path) == fold(f) for f in FAULT_FILES):
        return "FAULT"
    return "FREE"


# ---------------------------------------------------------------- the state

class State:
    """What the mock holds between requests."""

    def __init__(self, pages: pathlib.Path) -> None:
        """The state at its defaults, the card as the tests start from it.

        Args:
            pages: The folder wa_page_dump wrote.
        """
        self.pages = pages
        self.spans = json.loads((pages / "spans.json").read_text("utf-8"))
        self.texts = read_texts()
        self.reasons = read_reasons()
        self.lang = "en"
        self.writes = "OPEN"
        self.window = False
        self.armed = False
        self.logging = False
        self.busy: set[str] = set()
        self.mount = 1
        self.log: list[dict] = []
        self.card = Card()
        self.reset({})

    def report_name(self, how: str) -> str:
        """The dump's report file of the current language.

        Args:
            how: "pass" or "fail".

        Returns:
            The file's name.
        """
        return f"report_{self.lang}_{how}.txt"

    def fixture(self) -> Card:
        """The card the tests start from.

        Returns:
            The card: the bench's folders, a root file, and the User log
            folder with 49 entries: a folder, two servo test reports, a text
            file and 45 run logs.
        """
        c = Card()
        for d in ("BENCH", "BENCH/LOGS", "BENCH/ESC", "BENCH/ESC/CONF",
                  "BENCH/KST", "BENCH/KST/CONF", "BENCH/HITEC",
                  "BENCH/HITEC/CONF", "BENCH/ESCW", "BENCH/ESCW/CONF",
                  "USER", USER_LOGS, USER_LOGS + "/OLD"):
            c.dirs.add(d)
        stamp = 1790000000.0
        files = {
            "HELLO.TXT": b"Hello from the card.\n",
            "BENCH/RCBENCH.LOG": b"boot 1\nboot 2\n",
            "BENCH/KST/CONF/KST.TXT": b"journal\n",
            USER_LOGS + "/OLD/OLD001.CSV": b"t,v\n0,1\n",
            USER_LOGS + "/NOTES.TXT":
                b"Pack A: 6S, 4000mAh\nFirst flight fine.\nSecond line.\n",
            USER_LOGS + "/ATEST001.TXT":
                (self.pages / self.report_name("pass")).read_bytes(),
            USER_LOGS + "/ATEST002.TXT":
                (self.pages / self.report_name("fail")).read_bytes(),
        }
        for n in range(3, 48):
            files[f"{USER_LOGS}/LOG{n:03d}.CSV"] = (
                b"t_ms,volt,amp\n" + b"".join(
                    f"{k * 100},{6.0 - k * 0.01:.2f},{0.5 + k * 0.01:.2f}\n"
                    .encode() for k in range(5)))
        for path, data in files.items():
            c.files[path] = data
            c.mtime[path] = stamp
        return c

    def manifest(self, lang: str) -> str:
        """The card as wa_page_dump reads it.

        Args:
            lang: "en" or "de": the language of the reports on the card.

        Returns:
            One line per entry: the path, a tab, the size; a folder's path
            ends in "/".
        """
        self.lang = lang
        c = self.fixture()
        rows = [f"{d}/\t0" for d in sorted(c.dirs) if d]
        rows += [f"{p}\t{len(b)}" for p, b in sorted(c.files.items())]
        return "\n".join(rows) + "\n"

    def reset(self, opts: dict) -> None:
        """Set the state from a reset request and rebuild the card.

        Args:
            opts: The request's JSON object; absent keys take the defaults.
        """
        self.lang = opts.get("lang", "en")
        self.writes = opts.get("writes", "OPEN")
        self.window = bool(opts.get("window", False))
        self.armed = bool(opts.get("armed", False))
        self.logging = bool(opts.get("logging", False))
        self.busy = set(opts.get("busy", []))
        self.log = []
        self.card = self.fixture()

    def text(self, tid: str, de: bool) -> str:
        """A text of ui_text.def in a language.

        Args:
            tid: The entry's identifier.
            de: German.

        Returns:
            The text; "" for an identifier the table lacks or "".
        """
        t = self.texts.get(tid)
        return "" if t is None else t[1 if de else 0]

    def error(self, word: str) -> tuple[int, str, str]:
        """The status, word and sentence of a reason.

        Args:
            word: The reason word; "" is no reason.

        Returns:
            The HTTP status, the word and the sentence in the panel's
            language.
        """
        status, tid = self.reasons[word]
        return status, word, self.text(tid, self.lang == "de") if tid else ""

    def gate(self) -> str:
        """The refusal of a write now (fm_gate).

        Returns:
            "ARMED", "LOGGING" or "".
        """
        return "ARMED" if self.armed else ("LOGGING" if self.logging else "")

    def may_write(self) -> str:
        """The browser-write refusal of a write now (wa_api_may_write).

        Returns:
            "WRITES_OFF", "WRITES_HOLD" or "".
        """
        if self.writes == "OFF":
            return "WRITES_OFF"
        if self.writes == "HOLD" and not self.window:
            return "WRITES_HOLD"
        return ""

    def status(self) -> dict:
        """The body of /api/status (wa_api_status_json).

        Returns:
            The JSON object.
        """
        shut = self.may_write()
        if shut != "WRITES_OFF" and self.gate():
            shut = self.gate()
        used = sum(len(b) for b in self.card.files.values())
        return {
            "name": NAME, "version": self.version(), "lang": self.lang,
            "writes": "closed" if shut else "open", "why": shut,
            "why_text": self.error(shut)[2] if shut else "",
            "mode": self.writes,
            "card": {"mounted": True, "mount": self.mount, "name": "RCBENCH",
                     "total": CARD_BYTES, "free": CARD_BYTES - used},
            "page": PAGE_MAX, "file_max": FILE_MAX, "lines_max": LINES_MAX,
            "user_logs": USER_LOGS}

    def version(self) -> str:
        """The firmware version the page is built with.

        Returns:
            "MAJOR.MINOR.PATCH" of rcbench_version.h.
        """
        src = VERSION_H.read_text(encoding="utf-8")
        return ".".join(
            re.search(rf"RCBENCH_VERSION_{part} (\d+)", src).group(1)
            for part in ("MAJOR", "MINOR", "PATCH"))


# -------------------------------------------------------------- the guard

def host_ok(host: str | None, ip: str, port: int) -> bool:
    """Whether a Host header names the panel (wa_host_ok).

    Args:
        host: The header's value, or None.
        ip: The address the request came in on.
        port: The port that stands for the panel's port 80.

    Returns:
        True for the panel's name, its name with ".local" or ".local.", or
        its address, each with the port or without.
    """
    if not host:
        return False
    name, colon, p = host.partition(":")
    if colon and p != str(port):
        return False
    return fold(name) in (fold(NAME), fold(NAME + ".local"),
                          fold(NAME + ".local."), ip)


def origin_ok(origin: str | None, referer: str | None, ip: str,
              port: int) -> bool:
    """Whether a write comes from the panel's own page (wa_origin_ok).

    Args:
        origin: The Origin header, or None.
        referer: The Referer header, or None.
        ip: The address the request came in on.
        port: The port that stands for the panel's port 80.

    Returns:
        True when Origin, or without it Referer, names the panel over http.
    """
    url = origin if origin is not None else referer
    if url is None or not url.lower().startswith("http://"):
        return False
    rest = url[7:]
    if origin is None:
        rest = re.split(r"[/?#]", rest, maxsplit=1)[0]
    return host_ok(rest, ip, port)


# --------------------------------------------------------------- the server

def query_of(text: str) -> dict[str, str]:
    """The first value of each key of a query string or a form.

    Args:
        text: The text, without "?".

    Returns:
        A map from a key to its value, percent-decoded; "+" stays a plus.
    """
    out: dict[str, str] = {}
    for part in text.split("&"):
        key, _, val = part.partition("=")
        if key and key not in out:
            out[key] = val
    return out


def decode_path(raw: str) -> str:
    """A percent-encoded path as wa_path_decode() and fm_path_check() read it.

    Args:
        raw: The value from the query.

    Returns:
        The path.

    Raises:
        Refused: NAME for a "%2F", a NUL, bytes that are no UTF-8, a name
            of "." or "..", an empty name, a path over 120 bytes or more
            than 16 levels deep.
    """
    if re.search(r"%2f", raw, re.IGNORECASE):
        raise Refused("NAME")
    try:
        path = urllib.parse.unquote(raw, errors="strict")
    except UnicodeDecodeError:
        raise Refused("NAME") from None
    if path == "":
        return ""
    segs = path.split("/")
    if (len(path.encode()) > PATH_MAX or len(segs) > DEPTH_MAX
            or any(s in ("", ".", "..") or "\x00" in s for s in segs)):
        raise Refused("NAME")
    return path


class Handler(http.server.BaseHTTPRequestHandler):
    """One connection of the mock."""

    protocol_version = "HTTP/1.1"
    server_version = "rcbench-mock"
    lock = threading.Lock()

    def log_message(self, fmt: str, *args: object) -> None:
        """Keep the server quiet; the request log is /__mock/log.

        Args:
            fmt: The format.
            *args: Its values.
        """

    @property
    def st(self) -> State:
        """The server's state."""
        return self.server.state  # type: ignore[attr-defined]

    def ip(self) -> str:
        """The address this request came in on."""
        return self.connection.getsockname()[0]

    def port(self) -> int:
        """The mock's port, which stands for the panel's port 80."""
        return self.server.server_address[1]

    def hdr(self, name: str) -> str | None:
        """A request header.

        Args:
            name: The header's name.

        Returns:
            Its value, or None.
        """
        return self.headers.get(name)

    def send(self, status: int, body: bytes = b"", ctype: str | None = None,
             headers: dict[str, str] | None = None, close: bool = False,
             drain: int = 0) -> None:
        """Send one answer.

        Args:
            status: The HTTP status.
            body: The body.
            ctype: Content-Type; none when None.
            headers: More headers.
            close: End the connection with the answer.
            drain: Request body bytes to read and drop after the answer.
        """
        self.send_response(status, REASONS.get(status, "OK"))
        if ctype:
            self.send_header("Content-Type", ctype)
        for k, v in (headers or {}).items():
            self.send_header(k, v)
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(body)))
        if close:
            self.send_header("Connection", "close")
            self.close_connection = True
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)
        self.wfile.flush()
        if drain:
            self.rfile.read(min(drain, 2 * FILE_MAX))
        self.logged = status

    def json(self, status: int, obj: object, headers: dict | None = None,
             close: bool = False, drain: int = 0) -> None:
        """Send a JSON answer.

        Args:
            status: The HTTP status.
            obj: The body.
            headers: More headers.
            close: End the connection with the answer.
            drain: Request body bytes to read and drop after the answer.
        """
        body = json.dumps(obj, separators=(",", ":"),
                          ensure_ascii=False).encode()
        self.send(status, body, "application/json; charset=utf-8", headers,
                  close, drain)

    def refuse(self, word: str, found: str | None = None, close: bool = False,
               drain: int = 0) -> None:
        """Send a refusal: {"error": WORD, "text": ...}.

        Args:
            word: The reason word.
            found: The ETag a CHANGED found, 8 hex digits.
            close: End the connection with the answer.
            drain: Request body bytes to read and drop after the answer.
        """
        status, w, text = self.st.error(word)
        body = {"error": w, "text": text}
        if found is not None:
            body["etag"] = found
        self.json(status, body, close=close, drain=drain)

    # ---- the guard

    def guard(self, kind: str) -> str:
        """The request guard (wa_guard).

        Args:
            kind: "read", "put" or "form".

        Returns:
            The reason word of the refusal, or "".
        """
        body = int(self.hdr("Content-Length") or 0)
        if not host_ok(self.hdr("Host"), self.ip(), self.port()):
            return "HOST"
        if kind == "read":
            return "BODY" if body > 0 else ""
        if kind == "put" and body > FILE_MAX:
            return "TOO_LARGE"
        if kind == "form" and body > FORM_MAX:
            return "FORM_TOO_LARGE"
        if not origin_ok(self.hdr("Origin"), self.hdr("Referer"), self.ip(),
                         self.port()):
            return "ORIGIN"
        return "" if self.hdr(UPLOAD_HDR) == "1" else "ORIGIN"

    # ---- dispatch

    def route(self) -> None:
        """Answer one request, one at a time, and log it."""
        with Handler.lock:
            self.route_locked()

    def route_locked(self) -> None:
        """Dispatch the request on its method and path."""
        self.logged = 0
        url = urllib.parse.urlsplit(self.path)
        path, query = url.path, url.query
        if path.startswith("/__mock/"):
            self.script(path, query)
            return
        try:
            self.serve(path, query)
        finally:
            self.st.log.append({
                "method": self.command, "url": self.path,
                "status": self.logged, "if_match": self.hdr("If-Match"),
                "if_none_match": self.hdr("If-None-Match")})

    do_GET = do_PUT = do_POST = route

    def serve(self, path: str, query: str) -> None:
        """Answer a request of the panel's own routes.

        Args:
            path: The URL's path.
            query: Its query string.
        """
        gets = {"/api/status": self.api_status, "/api/list": self.api_list,
                "/api/info": self.api_info, "/api/file": self.api_file}
        forms = {"/api/mkdir": self.api_mkdir, "/api/rename": self.api_rename,
                 "/api/delete": self.api_delete}
        if path == "/" and self.command == "GET":
            self.page()
        elif path.startswith("/card/") and self.command == "GET":
            self.download(path[len("/card/"):])
        elif path in gets and self.command == "GET":
            word = self.guard("read")
            if word:
                self.refuse(word, close=True)
            else:
                self.guarded(lambda: gets[path](query))
        elif path == "/api/file" and self.command == "PUT":
            self.api_put(query)
        elif path in forms and self.command == "POST":
            self.form(forms[path])
        else:
            self.send(404 if path not in gets and path not in forms
                      else 405, close=True)

    def guarded(self, fn) -> None:  # type: ignore[no-untyped-def]
        """Run an API call and answer its refusal.

        Args:
            fn: The call; it answers itself or raises Refused.
        """
        try:
            fn()
        except Refused as r:
            self.refuse(r.word, r.etag)

    def page(self) -> None:
        """GET /: the page the dump wrote, as the panel sends it."""
        if not host_ok(self.hdr("Host"), self.ip(), self.port()):
            self.send(421, close=True)
            return
        page = self.st.pages / f"page_{self.st.lang}.html"
        self.send(200, page.read_bytes(), "text/html; charset=utf-8")

    def download(self, raw: str) -> None:
        """GET /card/PATH: a file as a download.

        Args:
            raw: The percent-encoded path.
        """
        if self.guard("read"):
            self.send(421, close=True)
            return
        found = self.st.card.find(urllib.parse.unquote(raw))
        if not found or found[1]:
            self.send(404)
            return
        name = found[0].rpartition("/")[2]
        self.send(200, self.st.card.files[found[0]],
                  "application/octet-stream",
                  {"Content-Disposition": "attachment; filename*=UTF-8''"
                   + urllib.parse.quote(name)})

    # ---- the reads

    def mount_of(self, q: dict[str, str]) -> None:
        """Hold the request's mount number to the card in the slot.

        Args:
            q: The query or form.

        Raises:
            Refused: REQUEST for a mount that is not a number, CARD_GONE for
                another card than the one in the slot.
        """
        if "mount" not in q:
            return
        if not re.fullmatch(r"[0-9]{1,10}", q["mount"]):
            raise Refused("REQUEST")
        if int(q["mount"]) != self.st.mount:
            raise Refused("CARD_GONE")

    def api_status(self, query: str) -> None:
        """GET /api/status.

        Args:
            query: The query string.
        """
        self.json(200, self.st.status())

    def api_list(self, query: str) -> None:
        """GET /api/list: one page of a folder (wa_api_list).

        Args:
            query: The query string.
        """
        q = query_of(query)
        path = decode_path(q.get("path", ""))
        go = q.get("go", "first")
        if go not in ("first", "next", "prev", "last", "same"):
            raise Refused("REQUEST")
        if q.get("key_dir", "0") not in ("0", "1"):
            raise Refused("REQUEST")
        key = None
        if go in ("next", "prev", "same"):
            name = urllib.parse.unquote(q.get("key", ""))
            if name == "" or "/" in name or len(name.encode()) > LIST_NAME_MAX:
                raise Refused("REQUEST")
            key = (q.get("key_dir") != "1", fold(name), name)
        found = self.st.card.find(path) if path else ("", True)
        if not found or not found[1]:
            raise Refused("NOT_FOUND")
        ents, unlisted = self.st.card.listing(found[0],
                                              frozenset(self.st.busy))
        ranks = [rank(e) for e in ents]
        total = len(ents)

        def after(k: tuple, right: bool) -> int:
            return sum(1 for r in ranks if (r <= k if right else r < k))

        def take(go_: str, k: tuple | None) -> tuple[int, int]:
            if go_ == "next" and k:
                lo = after(k, True)
            elif go_ == "prev" and k:
                hi = after(k, False)
                return max(0, hi - PAGE_MAX), hi
            elif go_ == "same" and k:
                lo = after(k, False)
            elif go_ == "last" or go_ == "prev":
                return max(0, total - PAGE_MAX), total
            else:
                lo = 0
            return lo, min(total, lo + PAGE_MAX)

        lo, hi = take(go, key)
        if hi == lo and total > 0:
            # Nothing on the page's side of its key: the first or last page.
            lo, hi = take("first" if go == "prev" and key else "last", None)
        page = ents[lo:hi]
        self.json(200, {"path": found[0], "mount": self.st.mount,
                        "ahead": lo, "total": total, "unlisted": unlisted,
                        "prev": lo > 0, "next": lo + len(page) < total,
                        "entries": page})

    def entry(self, q: dict[str, str]) -> tuple[str, bool, str]:
        """The file or folder a read names.

        Args:
            q: The query.

        Returns:
            The path as the card spells it, whether it is a folder and its
            folder.

        Raises:
            Refused: NAME, REQUEST, CARD_GONE or NOT_FOUND.
        """
        path = decode_path(q.get("path", ""))
        if q.get("dir", "0") not in ("0", "1") or path == "":
            raise Refused("REQUEST")
        self.mount_of(q)
        found = self.st.card.find(path)
        if not found or found[1] != (q.get("dir") == "1"):
            raise Refused("NOT_FOUND")
        return found[0], found[1], found[0].rpartition("/")[0]

    def api_info(self, query: str) -> None:
        """GET /api/info (wa_api_info).

        Args:
            query: The query string.
        """
        q = query_of(query)
        if q.get("etag", "0") not in ("0", "1"):
            raise Refused("REQUEST")
        path, is_dir, _ = self.entry(q)
        card = self.st.card
        stamp = int(card.mtime.get(path, 0))
        size = (sum(len(card.files[f"{path}/{n}"]) for n, d in
                    card.children(path) if not d) if is_dir
                else len(card.files[path]))
        kids = len(card.listing(path)[0]) if is_dir else 0
        body = {"path": path, "d": is_dir, "mount": self.st.mount, "s": size,
                "t": stamp, "entries": kids,
                "unlisted": card.listing(path)[1] if is_dir else 0,
                "time": time.strftime("%Y-%m-%d %H:%M", time.gmtime(stamp))
                if stamp >= 946684800 else ""}
        if q.get("etag") == "1" and not is_dir:
            body["etag"] = ("%08x" % zlib.crc32(card.files[path])
                            if size <= FILE_MAX else "")
        self.json(200, body)

    def api_file(self, query: str) -> None:
        """GET /api/file: a text file whole (wa_api_file).

        Args:
            query: The query string.
        """
        q = query_of(query)
        q["dir"] = "0"
        path, _, folder = self.entry(q)
        name = path.rpartition("/")[2]
        if place_of(folder, name, False) == "LOCKED":
            raise Refused("LOCKED")
        if path in self.st.busy:
            raise Refused("BUSY")
        data = self.st.card.files[path]
        if len(data) > FILE_MAX:
            raise Refused("TOO_BIG")
        if b"\x00" in data[:NUL_SCAN]:
            raise Refused("NOT_TEXT")
        if ends_of(data) > LINES_MAX:
            raise Refused("TOO_MANY_LINES")
        lines = ends_of(data) + (0 if data == b"" or data[-1:] in (
            b"\n", b"\r") else 1)
        self.send(200, data, "text/plain; charset=utf-8", {
            "ETag": '"%08x"' % zlib.crc32(data),
            "X-Rcbench-Eol": eol_word(data),
            "X-Rcbench-Bom": "1" if data.startswith(b"\xef\xbb\xbf") else "0",
            "X-Rcbench-Lines": str(lines)})

    # ---- the writes

    def write_guard(self, kind: str) -> str:
        """The guard, then the browser-write setting.

        Args:
            kind: "put" or "form".

        Returns:
            The reason word of the refusal, or "".
        """
        return self.guard(kind) or self.st.may_write()

    def job_rules(self, path: str, is_dir: bool, op: str) -> None:
        """The gate and the rules of one entry (fm_allowed).

        Args:
            path: The entry's path as the card spells it, or where it will
                stand.
            is_dir: A folder.
            op: "save", "rename", "move", "delete" or "new".

        Raises:
            Refused: ARMED, LOGGING, FIXED, LOCKED or BUSY.
        """
        gate = self.st.gate()
        if gate:
            raise Refused(gate)
        folder, _, name = path.rpartition("/")
        place = place_of(folder, name, is_dir)
        if op != "new":
            if place == "FIXED" and op in ("rename", "move"):
                raise Refused("FIXED")
            if place == "LOCKED":
                raise Refused("LOCKED")
            if place == "FAULT" and op in ("save", "rename", "move"):
                raise Refused("FIXED")
            if path in self.st.busy and not is_dir:
                raise Refused("BUSY")

    def canon(self, path: str) -> str:
        """The path with its folder spelled as the card spells it.

        Args:
            path: A path from the root.

        Returns:
            The path with the stored spelling of its folder.

        Raises:
            Refused: NOT_FOUND when the folder is not there.
        """
        folder, _, name = path.rpartition("/")
        if folder == "":
            return path
        found = self.st.card.find(folder)
        if not found or not found[1]:
            raise Refused("NOT_FOUND")
        return f"{found[0]}/{name}"

    def target_rules(self, path: str, is_dir: bool) -> None:
        """The rules of a destination (fm_target_check, target_path).

        Args:
            path: Where the entry will stand.
            is_dir: A folder.

        Raises:
            Refused: NAME when the name or the path with the ".NEW" a write
                makes first is over its limit, LOCKED for a record the
                programmers keep, FIXED for the fault log: the name and the
                ".NEW" each.
        """
        if is_dir:
            return
        folder, _, name = path.rpartition("/")
        if (len(name.encode()) > NAME_MAX
                or len((path + ".NEW").encode()) > PATH_MAX):
            raise Refused("NAME")
        for n in (name, name + ".NEW"):
            place = place_of(folder, n, False)
            if place == "LOCKED":
                raise Refused("LOCKED")
            if place == "FAULT":
                raise Refused("FIXED")

    def put_state(self, q: dict[str, str]) -> tuple[str, str, str | None]:
        """What a PUT asks before its body (put_args, the check).

        Args:
            q: The query.

        Returns:
            The file's path, the mode ("any", "sum" or "absent") and, for
            "sum", the wanted ETag.

        Raises:
            Refused: The reason the request is refused for.
        """
        im, inm = self.hdr("If-Match"), self.hdr("If-None-Match")
        path = decode_path(q.get("path", ""))
        if path == "" or not plain_ok(path):
            raise Refused("NAME" if path else "REQUEST")
        self.mount_of(q)
        mode, want = "any", None
        if im is not None and inm is not None:
            raise Refused("REQUEST")
        if im is not None:
            m = re.fullmatch(r'"([0-9a-fA-F]{8})"', im)
            if not m:
                raise Refused("REQUEST")
            mode, want = "sum", m.group(1).lower()
        elif inm is not None:
            if inm != "*":
                raise Refused("REQUEST")
            mode = "absent"
        if mode != "sum" and not name_ok(path.rpartition("/")[2]):
            raise Refused("NAME")
        self.job_rules(path, False, "save")
        card = self.st.card
        path = self.canon(path)
        self.target_rules(path, False)
        found = card.find(path)
        if mode == "absent" and found:
            raise Refused("EXISTS")
        if found and found[1]:
            raise Refused("ARG")
        if mode == "sum":
            have = "%08x" % zlib.crc32(card.files[found[0]]) if found else None
            if have != want:
                raise Refused("CHANGED", have)
        return (found[0] if found else path), mode, want

    def api_put(self, query: str) -> None:
        """PUT /api/file: the body as a file (wa_api_put_check, wa_api_put).

        Args:
            query: The query string.
        """
        n = int(self.hdr("Content-Length") or 0)
        try:
            word = self.write_guard("put")
            if word:
                raise Refused(word)
            q = query_of(query)
            self.put_state(q)               # before the body is read
        except Refused as r:
            self.refuse(r.word, r.etag, close=True, drain=n)
            return
        data = self.rfile.read(n)
        try:
            if ends_of(data) > LINES_MAX:
                raise Refused("TOO_MANY_LINES")
            if b"\x00" in data[:NUL_SCAN]:
                raise Refused("NOT_TEXT")
            path, _, _ = self.put_state(q)  # and again at the write
        except Refused as r:
            self.refuse(r.word, r.etag)
            return
        card = self.st.card
        created = card.find(path) is None
        card.write(path, data)
        folder, _, name = path.rpartition("/")
        restart = (fold(folder) == "BENCH/ESC/CONF"
                   and name.lower().endswith(".json"))
        tag = "%08x" % zlib.crc32(data)
        self.json(201 if created else 200,
                  {"path": path, "created": created, "etag": tag,
                   "note": "ESC_NEXT_START" if restart else ""},
                  {"ETag": f'"{tag}"'})

    def form(self, fn) -> None:  # type: ignore[no-untyped-def]
        """A POST of the file API: guard, setting, body, write.

        Args:
            fn: The write; it takes the form and answers or raises Refused.
        """
        n = int(self.hdr("Content-Length") or 0)
        word = self.write_guard("form")
        if word:
            self.refuse(word, close=True, drain=n)
            return
        q = query_of(self.rfile.read(n).decode("utf-8", "replace"))
        self.guarded(lambda: fn(q))

    def api_mkdir(self, q: dict[str, str]) -> None:
        """POST /api/mkdir.

        Args:
            q: The form.
        """
        path = decode_path(q.get("path", ""))
        if path == "" or not plain_ok(path):
            raise Refused("NAME" if path else "REQUEST")
        self.mount_of(q)
        self.job_rules(path, True, "new")
        if not name_ok(path.rpartition("/")[2]):
            raise Refused("NAME")
        card = self.st.card
        path = self.canon(path)
        if card.find(path):
            raise Refused("EXISTS")
        card.dirs.add(path)
        card.mtime[path] = time.time()
        self.json(201, {"path": path, "created": True, "note": ""})

    def api_rename(self, q: dict[str, str]) -> None:
        """POST /api/rename: a rename, or a move into another folder.

        Args:
            q: The form.
        """
        src = decode_path(q.get("from", ""))
        dst = decode_path(q.get("to", ""))
        if (src == "" or dst == "" or not plain_ok(src) or not plain_ok(dst)
                or q.get("dir", "0") not in ("0", "1")):
            raise Refused("REQUEST" if "" in (src, dst) else "NAME")
        self.mount_of(q)
        is_dir = q.get("dir") == "1"
        sdir, _, sname = src.rpartition("/")
        ddir, _, dname = dst.rpartition("/")
        card = self.st.card
        if sdir != ddir and sname != dname:
            raise Refused("REQUEST")
        op = "rename" if sdir == ddir else "move"
        found = card.find(src)
        if not found or found[1] != is_dir:
            raise Refused("NOT_FOUND")
        self.job_rules(found[0], is_dir, op)
        if op == "rename":
            if not name_ok(dname):
                raise Refused("NAME")
            dst = self.canon(dst)
            clash = card.find(dst)
            if clash and clash[0] != found[0]:
                raise Refused("EXISTS")
            if fold(sname) == fold(dname) and sname == dname:
                raise Refused("SAME")
        else:
            target = card.find(ddir) if ddir else ("", True)
            if not target or not target[1]:
                raise Refused("NOT_FOUND")
            dst = (target[0] + "/" if target[0] else "") + sname
            if is_dir and within(found[0], target[0]):
                raise Refused("SAME")
            if is_dir and card.children(found[0]):
                raise Refused("NOT_EMPTY")
            if card.find(dst):
                raise Refused("EXISTS")
        self.target_rules(dst, is_dir)
        card.move(found[0], dst)
        self.json(200, {"path": dst, "created": False, "note": ""})

    def api_delete(self, q: dict[str, str]) -> None:
        """POST /api/delete: a file, with its report, or an empty folder.

        Args:
            q: The form.
        """
        path = decode_path(q.get("path", ""))
        if path == "" or not plain_ok(path) or q.get("dir", "0") not in (
                "0", "1"):
            raise Refused("NAME" if path else "REQUEST")
        self.mount_of(q)
        is_dir = q.get("dir") == "1"
        card = self.st.card
        found = card.find(path)
        if not found or found[1] != is_dir:
            raise Refused("NOT_FOUND")
        self.job_rules(found[0], is_dir, "delete")
        if is_dir and card.children(found[0]):
            raise Refused("NOT_EMPTY")
        folder, _, name = found[0].rpartition("/")
        card.remove(found[0])
        if not is_dir and kind_of(folder, name, False) == "RUN":
            report = card.find(f"{folder}/{name.rpartition('.')[0]}.TXT"
                               if folder else f"{name.rpartition('.')[0]}.TXT")
            if report and not report[1]:
                card.remove(report[0])
        self.json(200, {"path": folder, "created": False, "note": ""})

    # ---- the script

    def script(self, path: str, query: str) -> None:
        """The /__mock/ routes: see the file's head.

        Args:
            path: The URL's path.
            query: Its query string.
        """
        st = self.st
        n = int(self.hdr("Content-Length") or 0)
        body = json.loads(self.rfile.read(n) or b"{}") if n else {}
        if path == "/__mock/ready":
            self.json(200, {"ready": True})
        elif path == "/__mock/reset":
            st.reset(body)
            self.json(200, {})
        elif path == "/__mock/put":
            st.card.write(body["path"], body["text"].encode())
            self.json(200, {})
        elif path == "/__mock/read":
            found = st.card.find(
                urllib.parse.unquote(query_of(query).get("path", "")))
            if found and not found[1]:
                self.send(200, st.card.files[found[0]], "text/plain")
            else:
                self.send(404)
        elif path == "/__mock/tree":
            self.json(200, {"dirs": sorted(d for d in st.card.dirs if d),
                            "files": {p: len(b) for p, b in
                                      sorted(st.card.files.items())}})
        elif path == "/__mock/log":
            self.json(200, st.log)
        elif path == "/__mock/spans":
            self.json(200, st.spans[st.report_name(
                query_of(query).get("how", "pass"))])
        elif path == "/__mock/text":
            t = st.texts.get(query_of(query).get("id", ""))
            self.json(200 if t else 404, {"en": t[0], "de": t[1]} if t else {})
        else:
            self.send(404)


class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
    """The mock's server: a thread a connection, a lock a request."""

    daemon_threads = True
    allow_reuse_address = True
    state: State


def make_server(pages: pathlib.Path, host: str = "127.0.0.1",
                port: int = 0) -> Server:
    """A server over a folder of dump files.

    Args:
        pages: The folder wa_page_dump wrote.
        host: The address to listen on.
        port: The port; 0 takes a free one.

    Returns:
        The server, not yet serving.
    """
    srv = Server((host, port), Handler)
    srv.state = State(pages)
    return srv


def dump(binary: pathlib.Path, out: pathlib.Path) -> None:
    """Run wa_page_dump into a folder, with the card the tests start from.

    Args:
        binary: The program.
        out: The folder.

    Raises:
        subprocess.CalledProcessError: The program failed.
    """
    # The reports first: the card holds them, and the page counts the card.
    subprocess.run([str(binary), str(out)], check=True,
                   stdout=subprocess.DEVNULL)
    state = State(out)
    cards = []
    for lang in ("en", "de"):
        card = out / f"card_{lang}.txt"
        card.write_text(state.manifest(lang), encoding="utf-8")
        cards.append(str(card))
    subprocess.run([str(binary), str(out), *cards], check=True,
                   stdout=subprocess.DEVNULL)


def main(argv: list[str]) -> int:
    """Serve until interrupted.

    Args:
        argv: The program's arguments.

    Returns:
        0 after an interrupt, 2 for a wrong call.
    """
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--dump", type=pathlib.Path, help="wa_page_dump binary")
    ap.add_argument("--pages", type=pathlib.Path, help="folder of dump files")
    ap.add_argument("--port", type=int, default=0)
    ap.add_argument("--host", default="127.0.0.1")
    args = ap.parse_args(argv)
    if (args.dump is None) == (args.pages is None):
        ap.error("give --dump or --pages")
    with tempfile.TemporaryDirectory() as tmp:
        pages = args.pages
        if pages is None:
            pages = pathlib.Path(tmp)
            dump(args.dump, pages)
        srv = make_server(pages, args.host, args.port)
        print(f"listening on http://{args.host}:{srv.server_address[1]}",
              flush=True)
        try:
            srv.serve_forever()
        except KeyboardInterrupt:
            pass
        finally:
            srv.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
