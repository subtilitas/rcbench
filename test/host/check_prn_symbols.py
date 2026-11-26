#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Hold the printer engine to the functions it may call.

A printer file from the SD card drives protocols/prnengine.  The engine
reaches nothing outside its own buffers because it calls nothing outside
them: no output, arming, safety, radio or file code, and of the C library
only memcpy(), memset(), memcmp() and strlen().  This reads the symbols the
library's objects leave undefined (`nm -u`), drops the ones another object
of the library defines, and fails on any other name.

The compiler inserts calls of its own: the stack protector's
`__stack_chk_fail`, and in the sanitizer and coverage builds the
`__asan_`, `__ubsan_`, `__sanitizer_`, `__gcov_` and `__llvm_` hooks.  They
are allowed by name and prefix.

usage: check_prn_symbols.py <librcbench_prnengine.a> [--nm NM]
"""
import argparse
import subprocess
import sys

ALLOWED = {"memcpy", "memset", "memcmp", "strlen", "__stack_chk_fail"}
PREFIXES = ("__asan_", "__ubsan_", "__sanitizer_", "__gcov_", "__llvm_",
            "__tsan_")


def symbols(nm: str, lib: str, flag: str) -> set[str]:
    """The symbol names nm lists for a flag.

    Args:
        nm: the nm program.
        lib: the library.
        flag: --undefined-only or --defined-only.

    Returns:
        The names.
    """
    out = subprocess.run([nm, flag, lib], capture_output=True, text=True,
                         check=True).stdout
    names = set()
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 2 and not line.endswith(":"):
            names.add(parts[-1])
    return names


def main() -> int:
    """Check the library.

    Returns:
        0, or 1 when it calls a function outside the allowed ones.
    """
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("lib")
    ap.add_argument("--nm", default="nm")
    args = ap.parse_args()
    called = symbols(args.nm, args.lib, "--undefined-only")
    own = symbols(args.nm, args.lib, "--defined-only")
    outside = sorted(name for name in called - own
                     if name not in ALLOWED and not name.startswith(PREFIXES))
    for name in outside:
        print(f"{args.lib} calls {name}, which is not allowed")
    if not outside:
        print(f"{args.lib}: only {', '.join(sorted(ALLOWED & called))}")
    return 1 if outside else 0


if __name__ == "__main__":
    sys.exit(main())
