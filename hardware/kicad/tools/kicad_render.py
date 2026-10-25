#!/usr/bin/env python3
"""Load the library with kicad-cli and render it to PNG.

KiCad 10's kicad-cli renders every footprint and symbol to SVG, and cairosvg
turns them into PNG, for a look and as a load test: kicad-cli refuses a file
it cannot parse.

Usage: kicad_render.py <out_dir>
Environment: KICAD_CLI, the kicad-cli to run (default: kicad-cli on PATH).
Needs cairosvg for the PNGs; without it the SVGs stay.
"""

import os
import subprocess
import sys
from pathlib import Path

KICAD = Path(__file__).resolve().parent.parent


def run(args):
    """Run a command and stop the program when it fails.

    Args:
        args: The command and its arguments.

    Returns:
        The command's output, stdout and stderr joined.
    """
    r = subprocess.run(args, capture_output=True, text=True)
    out = (r.stdout + r.stderr).strip()
    if r.returncode != 0:
        print(out)
        sys.exit(f"FAILED: {' '.join(args[:4])}")
    return out


def main():
    """Render the library into the directory given as the first argument."""
    out = Path(sys.argv[1])
    cli = os.environ.get("KICAD_CLI", "kicad-cli")
    (out / "fp").mkdir(parents=True, exist_ok=True)
    (out / "sym").mkdir(parents=True, exist_ok=True)
    run([cli, "fp", "export", "svg", str(KICAD / "rcbench_modules.pretty"),
         "-o", str(out / "fp")])
    run([cli, "sym", "export", "svg", str(KICAD / "rcbench_modules.kicad_sym"),
         "-o", str(out / "sym")])
    try:
        import cairosvg
    except ImportError:
        print("cairosvg missing: SVGs only")
        return
    n = 0
    for svg in sorted(out.glob("*/*.svg")):
        cairosvg.svg2png(url=str(svg), write_to=str(svg.with_suffix(".png")),
                         output_width=1000, background_color="white")
        n += 1
    print(f"{n} renders in {out}")


if __name__ == "__main__":
    main()
