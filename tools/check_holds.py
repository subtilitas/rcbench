#!/usr/bin/env python3
"""Hold every 2s hold of the panel to its flash, in the source.

A hold flashes as it fires: ui_hold_tick() starts the flash, the screen
steps it with ui_hold_flash_step() every frame and draws the button with
ui_hold_button(), ui_hold_button_fill() or ui_hold_ghost()
(shared/ui/ui_widgets.c).  A hold that is begun and never stepped keeps
its first phase, white, on the glass; one that is never drawn through
those calls shows no flash at all.

The check: every ui_hold_begin(&X, ...) in shared/ui/*.c names a hold X.
Its path after the root, `arm` of `s.arm`, `page.hold` of `hp.page.hold`,
must appear as the last part of the hold argument of a ui_hold_flash_step()
call and of a ui_hold_button(), ui_hold_button_fill() or ui_hold_ghost()
call in a file of the same module: the files that share the name's first
word (servo_*.c, kst_*.c, prog5v_*.c).  A function of the module that
takes a `ui_hold_t *` and hands it to one of those calls counts as that
call (upd_screen.c's hold_fill(), servo_screen_sweep.c's flash_frame()), and
so does a function that hands it to such a function.
ui_widgets.c, which defines the calls, is not checked.  A hold drawn by
another module is an entry in EXCEPTIONS below, with the reason.

    tools/check_holds.py            each hold, its draw and its step
    tools/check_holds.py --check    and exit 1 when one lacks either

SPDX-License-Identifier: MIT
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
UI = ROOT / "shared" / "ui"

#: Holds drawn outside their module, by (file of the begin, path): why.
EXCEPTIONS = {
    ("stop_hold.c", "s_hold"): "STOP is drawn by ui_band.c's stop_body() "
                               "from stop_hold_flash_ms(), its own red and "
                               "frame around the flash",
}

DRAWS = ("ui_hold_button", "ui_hold_button_fill", "ui_hold_ghost")
STEP = "ui_hold_flash_step"


def strip_comments(text: str) -> str:
    """Returns C source without its comments and string literals.

    Args:
        text: The source.

    Returns:
        The source, comments and strings blanked, lines kept.
    """
    out = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"),
                 text, flags=re.S)
    out = re.sub(r"//[^\n]*", "", out)
    return re.sub(r'"(?:\\.|[^"\\])*"', '""', out)


def calls(text: str, name: str) -> list[str]:
    """Returns the argument lists of every call of @p name in @p text.

    Args:
        text: C source without comments.
        name: The function.

    Returns:
        Each call's arguments, as written between its parentheses.
    """
    found = []
    for m in re.finditer(r"\b" + re.escape(name) + r"\s*\(", text):
        depth, i = 1, m.end()
        while i < len(text) and depth:
            depth += {"(": 1, ")": -1}.get(text[i], 0)
            i += 1
        found.append(text[m.end():i - 1])
    return found


def path_of(arg: str) -> str:
    """Returns the hold's path after its root: `page.hold` of `&hp.page.hold`.

    Args:
        arg: The hold argument as written.

    Returns:
        The path after the root, or the name itself for a plain variable.
    """
    a = arg.strip().lstrip("&").strip()
    parts = re.split(r"\.|->", a)
    return ".".join(parts[1:]) if len(parts) > 1 else parts[0]


def holds_arg(args: str, path: str) -> bool:
    """Returns whether a call's arguments name the hold @p path.

    Args:
        args: The call's arguments.
        path: The hold's path after its root.

    Returns:
        True when an argument ends in the path, after a `.` or `->`, or is
        a copy named as the path's last part.
    """
    tail = re.escape(path).replace(r"\.", r"(?:\.|->)")
    if re.search(r"(?:\.|->)" + tail + r"\b", args):
        return True
    return re.search(r"&\s*" + tail + r"\b", args) is not None


def wrappers(text: str, names: tuple[str, ...]) -> tuple[str, ...]:
    """Returns the functions of @p text that hand a hold to one of @p names.

    Args:
        text: C source without comments.
        names: The calls.

    Returns:
        The names of the functions that take a `ui_hold_t *` and pass it
        to one of @p names.
    """
    found = []
    for m in re.finditer(r"\b(\w+)\s*\(([^()]*\bui_hold_t\s*\*\s*(\w+)"
                         r"[^()]*)\)\s*\{", text):
        fn, param = m.group(1), m.group(3)
        depth, i = 1, m.end()
        while i < len(text) and depth:
            depth += {"{": 1, "}": -1}.get(text[i], 0)
            i += 1
        body = text[m.end():i]
        if any(re.search(r"\b" + re.escape(param) + r"\b", a)
               for n in names for a in calls(body, n)):
            found.append(fn)
    return tuple(found)


def closure(texts: list[str], names: tuple[str, ...]) -> tuple[str, ...]:
    """Returns @p names and every wrapper of them, wrappers of wrappers too.

    Args:
        texts: The module's sources without comments.
        names: The calls.

    Returns:
        The calls and their wrappers.
    """
    known = tuple(names)
    while True:
        more = tuple(w for t in texts for w in wrappers(t, known)
                     if w not in known)
        if not more:
            return known
        known += tuple(dict.fromkeys(more))


def module(name: str) -> str:
    """Returns a file's module: the first word of its name."""
    return name.split("_", 1)[0]


def check(ui: pathlib.Path = UI) -> list[str]:
    """Returns one line per hold that lacks its draw or its step.

    Args:
        ui: The directory of the screens' sources.

    Returns:
        The problems, empty when every hold flashes.
    """
    sources = {p.name: strip_comments(p.read_text(encoding="utf-8"))
               for p in sorted(ui.glob("*.c")) if p.name != "ui_widgets.c"}
    problems = []
    for name, text in sources.items():
        for args in calls(text, "ui_hold_begin"):
            hold = args.split(",", 1)[0]
            path = path_of(hold)
            if (name, path) in EXCEPTIONS:
                continue
            mates = [t for n, t in sources.items()
                     if module(n) == module(name)]
            draws = closure(mates, DRAWS)
            steps = closure(mates, (STEP,))
            drawn = any(holds_arg(a, path) for t in mates
                        for d in draws for a in calls(t, d))
            stepped = any(holds_arg(a, path) for t in mates
                          for d in steps for a in calls(t, d))
            if not drawn:
                problems.append(f"{name}: {hold.strip()} is drawn by none "
                                f"of {', '.join(DRAWS)}")
            if not stepped:
                problems.append(f"{name}: {hold.strip()} is not stepped by "
                                f"{STEP}")
    return problems


def main(argv: list[str] | None = None) -> int:
    """Prints the problems; with --check, fails on one."""
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--check", action="store_true",
                    help="exit 1 when a hold lacks its draw or its step")
    args = ap.parse_args(argv)
    problems = check()
    for p in problems:
        print(p)
    if args.check and problems:
        print(f"{len(problems)} hold(s) without their flash", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
