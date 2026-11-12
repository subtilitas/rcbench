#!/usr/bin/env python3
"""Compare an ESP-IDF sdkconfig with the defaults files it is built from.

ESP-IDF reads `sdkconfig.defaults` only when it generates `sdkconfig`.  An
`sdkconfig` already in the project folder takes precedence, so a local
build that keeps an old one is not the build CI makes: CI starts from a
checkout without one.  This tool reads every setting the defaults files
give and compares it with the sdkconfig.

    tools/check_sdkconfig.py --check              firmware/panel/sdkconfig
                                                  against the panel's
                                                  defaults files
    tools/check_sdkconfig.py --check --sdkconfig BUILD/sdkconfig
                                                  a generated one, as
                                                  `idf.py -D SDKCONFIG=...`
                                                  writes it
    --project DIR                                 another ESP-IDF project
                                                  (default firmware/panel)

The defaults files are `sdkconfig.defaults` and, when the sdkconfig names
its target, `sdkconfig.defaults.<target>`; a later file overrides an earlier
one, as in ESP-IDF.  A boolean set to `n` matches `# CONFIG_X is not set`
and a missing line: Kconfig leaves out an option whose dependencies are
off.  Any other value has to be there and equal; hex numbers compare by
value.  Options the defaults do not name are not compared: their values
come from the Kconfig files of the ESP-IDF release, which this tool does
not read.

Exit status: 0 when every setting matches, or when the project holds no
sdkconfig and none is named; 1 when a setting differs or is missing, or a
named sdkconfig does not exist.  The fix for a local file is to
delete it, or to build with `-D SDKCONFIG=<build folder>/sdkconfig` so that
every build generates its own.

SPDX-License-Identifier: MIT
"""

import argparse
import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
DEFAULT_PROJECT = REPO / "firmware" / "panel"

_SET_RE = re.compile(r"^(CONFIG_[A-Za-z0-9_]+)=(.*)$")
_NOT_SET_RE = re.compile(r"^# (CONFIG_[A-Za-z0-9_]+) is not set$")
_HEX_RE = re.compile(r"^0[xX][0-9a-fA-F]+$")


def read_config(path: pathlib.Path) -> dict[str, str]:
    """Read the settings of an sdkconfig or a defaults file.

    Args:
        path: The file.

    Returns:
        Each option's name with its value as written; `n` for a line
        `# CONFIG_X is not set`.  A later line wins over an earlier one.
    """
    settings: dict[str, str] = {}
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        m = _SET_RE.match(line)
        if m:
            settings[m.group(1)] = m.group(2).strip()
            continue
        m = _NOT_SET_RE.match(line)
        if m:
            settings[m.group(1)] = "n"
    return settings


def defaults_files(project: pathlib.Path, target: str | None
                   ) -> list[pathlib.Path]:
    """Find the defaults files ESP-IDF reads for a project.

    Args:
        project: The project folder.
        target: The chip, as CONFIG_IDF_TARGET names it, or None.

    Returns:
        `sdkconfig.defaults`, then `sdkconfig.defaults.<target>` where it
        exists.
    """
    files = [project / "sdkconfig.defaults"]
    if target:
        extra = project / f"sdkconfig.defaults.{target}"
        if extra.is_file():
            files.append(extra)
    return [f for f in files if f.is_file()]


def same_value(want: str, have: str) -> bool:
    """Compare two values of one option.

    Args:
        want: The value the defaults give.
        have: The value the sdkconfig holds.

    Returns:
        True when they are equal, hex numbers compared by value.
    """
    if _HEX_RE.match(want) and _HEX_RE.match(have):
        return int(want, 16) == int(have, 16)
    return want == have


def compare(defaults: dict[str, str], config: dict[str, str]) -> list[str]:
    """List the settings of the defaults that the sdkconfig does not hold.

    Args:
        defaults: The settings of the defaults files, merged.
        config: The settings of the sdkconfig.

    Returns:
        One line per option that differs or is missing, sorted by name.
    """
    problems = []
    for name in sorted(defaults):
        want = defaults[name]
        have = config.get(name)
        if want == "n":
            if have not in (None, "n"):
                problems.append(f"{name}: defaults n, sdkconfig {have}")
        elif have is None:
            problems.append(f"{name}: defaults {want}, not in the sdkconfig")
        elif not same_value(want, have):
            problems.append(f"{name}: defaults {want}, sdkconfig {have}")
    return problems


def main(argv: list[str] | None = None) -> int:
    """Run the comparison.

    Args:
        argv: The arguments; sys.argv[1:] when None.

    Returns:
        0 when the sdkconfig holds every default or does not exist, 1 when
        it differs.
    """
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--check", action="store_true", required=True,
                        help="fail when the sdkconfig differs")
    parser.add_argument("--project", type=pathlib.Path,
                        default=DEFAULT_PROJECT,
                        help="the ESP-IDF project folder")
    parser.add_argument("--sdkconfig", type=pathlib.Path,
                        help="the sdkconfig to compare (default: the "
                             "project's own)")
    args = parser.parse_args(argv)
    project = args.project.resolve()
    sdkconfig = args.sdkconfig or project / "sdkconfig"

    if not sdkconfig.is_file():
        if args.sdkconfig is not None:
            print(f"check_sdkconfig: no {sdkconfig}", file=sys.stderr)
            return 1
        print(f"check_sdkconfig: no {sdkconfig}; the build generates it "
              "from the defaults")
        return 0
    config = read_config(sdkconfig)
    target = config.get("CONFIG_IDF_TARGET", "").strip('"') or None
    files = defaults_files(project, target)
    if not files:
        print(f"check_sdkconfig: no sdkconfig.defaults in {project}",
              file=sys.stderr)
        return 1
    defaults: dict[str, str] = {}
    for f in files:
        defaults.update(read_config(f))

    problems = compare(defaults, config)
    names = ", ".join(f.name for f in files)
    if problems:
        print(f"check_sdkconfig: {sdkconfig} differs from {names} in "
              f"{len(problems)} setting(s):")
        print("".join(f"  {p}\n" for p in problems), end="")
        print("Delete it, or build with -D SDKCONFIG=<build folder>/sdkconfig,"
              " so that ESP-IDF generates it from the defaults.")
        return 1
    print(f"check_sdkconfig: {sdkconfig} holds the {len(defaults)} "
          f"setting(s) of {names}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
