#!/usr/bin/env python3
"""Hold HOTSPOT PASSWORD and WEB CODE to the code that may touch them.

Both are kept in NVS (non-volatile storage) in plain form, and the panel never
writes them to a log, the console, a file or an answer a browser can read.
This names every source that may hold such a value, and fails on

- a use of a name that holds one in any other source of firmware/panel or
  shared/, the file API and the page among them;
- a log or console call whose arguments name one, in any source.

Usage: check_wifi_secrets.py [--root DIR]

SPDX-License-Identifier: MIT
"""

import argparse
import pathlib
import re
import sys

# The names that hold a value: the settings', the session's and the view's.
NAMES = re.compile(
    r"\b(?:ap_pass|web_code|kept_pass|kept_code|save_kept)\b"
    r"|\bw(?:\.|->)(?:pass|code)\b"
    r"|\bconf\.(?:ap_pass|web_code|pass)\b"
)

# What may hold one, and why.
ALLOWED = {
    "shared/webaccess/include/wa_session.h": "declares them",
    "shared/webaccess/wa_session.c": "the rule, the load and the code check",
    "shared/ui/include/wifi_screen.h": "the view the screen draws",
    "shared/ui/wifi_screen.c": "draws them; takes the typed value",
    "shared/ui/wifi_kept.c": "draws them",
    "firmware/panel/main/panel_update.c": "hands them to the screen",
    "firmware/panel/components/wifi_access/wifi_access.c":
        "the store, the radio's configuration, the cookie",
}

# Calls that reach a log, the console or a file.
LOG_CALL = re.compile(
    r"\b(?:ESP_LOG[EWIDV]|ESP_EARLY_LOG[EWIDV]|ESP_DRAM_LOG[EWIDV]|printf"
    r"|fprintf|puts|fputs|fwrite|log_line|fault_log_\w+|panel_log\w*)\s*\("
)


def statements(text):
    """Split C source into statements with the line each starts on.

    Args:
        text: The source.

    Yields:
        Tuples of (line number, statement text).
    """
    line = 1
    start = 1
    buf = []
    for ch in text:
        if not buf and ch.isspace():
            line += ch == "\n"
            start = line
            continue
        buf.append(ch)
        if ch == "\n":
            line += 1
        if ch in ";{}":
            yield start, "".join(buf)
            buf = []
            start = line
    if buf:
        yield start, "".join(buf)


def strip_comments(text):
    """Blank C comments, keeping the line count.

    Args:
        text: The source.

    Returns:
        The source without its comments.
    """
    def blank(match):
        return re.sub(r"[^\n]", " ", match.group(0))
    return re.sub(r"/\*.*?\*/|//[^\n]*", blank, text, flags=re.S)


def check_file(rel, text):
    """The failures of one source.

    Args:
        rel: Its path from the root, with forward slashes.
        text: Its text.

    Returns:
        A list of lines to print.
    """
    bad = []
    for start, stmt in statements(strip_comments(text)):
        name = NAMES.search(stmt)
        if name is None:
            continue
        if rel not in ALLOWED:
            bad.append(f"{rel}:{start}: {name.group(0)} is used outside the "
                       "sources that may hold a kept value")
        elif LOG_CALL.search(stmt):
            bad.append(f"{rel}:{start}: a log or console call names "
                       f"{name.group(0)}")
    return bad


def main(argv=None):
    """Check every C source under firmware/panel and shared.

    Args:
        argv: The arguments; None is the command line's.

    Returns:
        0 when every source holds the rule, 1 otherwise.
    """
    parser = argparse.ArgumentParser(description=__doc__)
    default = pathlib.Path(__file__).resolve().parent.parent
    parser.add_argument("--root", type=pathlib.Path, default=default)
    args = parser.parse_args(argv)
    bad = []
    seen = set()
    for top in ("firmware/panel", "shared"):
        for path in sorted((args.root / top).rglob("*")):
            if path.suffix not in (".c", ".h") or "build" in path.parts \
                    or "managed_components" in path.parts:
                continue
            rel = path.relative_to(args.root).as_posix()
            seen.add(rel)
            bad += check_file(rel, path.read_text(encoding="utf-8",
                                                  errors="replace"))
    for rel in sorted(set(ALLOWED) - seen):
        bad.append(f"{rel}: listed as allowed, not in the tree")
    for line in bad:
        print(line)
    print(f"check_wifi_secrets: {len(seen)} sources, {len(bad)} failure(s)")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
