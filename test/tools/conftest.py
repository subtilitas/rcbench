"""The tools are scripts in tools/, not a package: put them on the path."""

import pathlib
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(REPO / "tools"))
