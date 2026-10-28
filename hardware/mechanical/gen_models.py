#!/usr/bin/env python3
"""Write the STEP models of the mechanical parts from their specs.

The specs are hardware/mechanical/*.py other than this file; each defines
``parts(colors)``, a dict of file stem to solids as
hardware/kicad/tools/step_gen.py's ``write`` takes them. The models go
to hardware/mechanical/models/<stem>.step.

Usage:
    python3 hardware/mechanical/gen_models.py           write the files
    python3 hardware/mechanical/gen_models.py --check   exit 1 when a file
                                                        differs from its
                                                        spec
"""

import importlib.util
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
MODELS = HERE / "models"
sys.path.insert(0, str(HERE.parent / "kicad" / "tools"))
import step_gen  # noqa: E402


def load_specs():
    """Load every spec module in hardware/mechanical/.

    Returns:
        The modules, sorted by file name.
    """
    specs = []
    for path in sorted(HERE.glob("*.py")):
        if path.name == Path(__file__).name or path.name.startswith("_"):
            continue
        spec = importlib.util.spec_from_file_location(path.stem, path)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        specs.append(mod)
    return specs


def outputs():
    """Build every model in memory.

    Returns:
        A dict of path to (file text, solids).
    """
    out = {}
    for spec in load_specs():
        for stem, items in spec.parts(step_gen.COLORS).items():
            out[MODELS / f"{stem}.step"] = (step_gen.write(stem, items),
                                            items)
    return out


def main(argv):
    """Write or check every model.

    Args:
        argv: Command-line arguments without the program name.

    Returns:
        0 when every file is current or written, 1 for a stale file in check
        mode.
    """
    check = "--check" in argv
    stale = []
    for path, (text, _items) in outputs().items():
        old = path.read_text() if path.exists() else None
        if old == text:
            continue
        if check:
            stale.append(path.relative_to(HERE.parent.parent))
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)
            print(f"wrote {path.relative_to(HERE.parent.parent)}")
    for p in stale:
        print(f"STALE {p}: run hardware/mechanical/gen_models.py")
    return 1 if stale else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
