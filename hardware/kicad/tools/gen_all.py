#!/usr/bin/env python3
"""Write the rcbench KiCad library from the module specs.

The specs are hardware/kicad/modules/*.py; the output is
hardware/kicad/rcbench_modules.pretty/ and rcbench_modules.kicad_sym.

Usage:
    python3 hardware/kicad/tools/gen_all.py           write the files
    python3 hardware/kicad/tools/gen_all.py --check   exit 1 when a file
                                                      differs from its spec

UUIDs derive from the module name and the item's place in it, so the output
is the same on every run.
"""

import importlib.util
import sys
import uuid
from pathlib import Path

HERE = Path(__file__).resolve().parent
KICAD = HERE.parent
LIB = "rcbench_modules"
NS = uuid.UUID("6f1d3c52-2a4e-4b8e-9a63-0c3d7e2b51a4")

sys.path.insert(0, str(HERE))
import kicad_gen  # noqa: E402


def load_specs():
    """Load every spec module in hardware/kicad/modules/.

    Returns:
        The modules, sorted by file name; names starting with "_" are skipped.
    """
    specs = []
    for path in sorted((KICAD / "modules").glob("*.py")):
        if path.name.startswith("_"):
            continue
        spec = importlib.util.spec_from_file_location(path.stem, path)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        specs.append(mod)
    return specs


def uid_for(name):
    """Return a UUID source that counts from 0 for one module.

    Args:
        name: The module name the UUIDs derive from.

    Returns:
        A function returning the next UUID as text on each call.
    """
    count = iter(range(1 << 30))
    return lambda: str(uuid.uuid5(NS, f"{name}:{next(count)}"))


def outputs():
    """Build every output file in memory.

    Returns:
        A dict of path to file text: one per footprint and the symbol library.
    """
    out = {}
    symbols = []
    for mod in load_specs():
        fp = mod.FOOTPRINT
        out[KICAD / f"{LIB}.pretty" / f"{fp['name']}.kicad_mod"] = \
            kicad_gen.footprint(fp, uid_for(fp["name"]))
        symbols.append(mod.SYMBOL)
    out[KICAD / f"{LIB}.kicad_sym"] = kicad_gen.symbol_library(symbols, LIB)
    return out


def main(argv):
    """Write or check every output file.

    Args:
        argv: Command-line arguments without the program name.

    Returns:
        0 when every file is current or written, 1 for a stale file in check
        mode.
    """
    check = "--check" in argv
    stale = []
    for path, text in outputs().items():
        old = path.read_text() if path.exists() else None
        if old == text:
            continue
        if check:
            stale.append(path.relative_to(KICAD.parent.parent))
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)
            print(f"wrote {path.relative_to(KICAD.parent.parent)}")
    if check and stale:
        for p in stale:
            print(f"STALE {p}: run hardware/kicad/tools/gen_all.py")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
