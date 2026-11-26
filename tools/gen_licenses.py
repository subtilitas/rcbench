#!/usr/bin/env python3
"""Write the third-party table and notices the panel's ABOUT screen shows.

The panel firmware and the IOmcu firmware (which the panel image carries
and installs) link code that other people wrote.  This file is the one
list of it: each component with its version, its licence as an SPDX
(Software Package Data Exchange) identifier, its copyright lines and the
firmware it is linked into.  From the list it writes

- shared/ui/about_gen.c: the table and the notice texts as rows of at most
  CELLS (92) cells, which SETUP > PANEL > About rcbench shows
  (about_screen.h);
- the table in docs/Licenses.md and docs/Licenses-de.md, between the
  marker lines `<!-- gen_licenses: begin -->` and `<!-- gen_licenses:
  end -->`, and the table of figures between `<!-- gen_licenses: figures
  begin -->` and `<!-- gen_licenses: figures end -->`: the components,
  the references, the licence texts and the rows, counted from the list.

It also holds NOTICE to the list: its section on the panel build's
fetched components names every component firmware/panel/dependencies.lock
fetches, marks those the image does not link "(not linked)" and the others
not, and its list of references names every reference here.

The notice texts are copies of the licence files in third_party/notices/,
taken from the ESP-IDF tree, the toolchains, the pico-sdk and the managed
components by --refresh; a text that sits in this repository (qrcodegen's
LICENSE, the header of protocols/pdmini/rp2350/pd_uart.pio) is read where
it is.  third_party/notices/tlsf.txt has no file to come from: ESP-IDF's
copy of TLSF carries only its SPDX lines, so the text is the BSD-3-Clause
template with the copyright line of tlsf.c.

What a component is was read off the link maps of the two images
(rcbench-panel.map, rcbench-iomcu.elf.map): every archive member the
linker took, traced to its source file and that file's licence header.

    tools/gen_licenses.py              write about_gen.c and the doc tables
    tools/gen_licenses.py --check      exit 1 when either differs from what
                                       the list writes, or when NOTICE's
                                       list of fetched components names a
                                       linked one the list does not, or
                                       misses one it has
    tools/gen_licenses.py --refresh    copy the notice texts anew from
                                       their origins (needs the toolchains)
    tools/gen_licenses.py --sources    exit 1 when a stored notice differs
                                       from its origin; an origin that is
                                       not on this machine is named and
                                       skipped; NOTICE is checked as with
                                       --check

The roots of the origins: IDF_PATH (~/esp/esp-idf-v6.1), PICO_SDK_PATH
(~/toolchains/pico-sdk), RCBENCH_RISCV_TC (the riscv32-esp-elf toolchain
ESP-IDF v6.1 installs), RCBENCH_ARM_TC
(~/toolchains/arm-gnu-toolchain-14.2.rel1-x86_64-arm-none-eabi) and
/usr/share/doc/fonts-dejavu-core.

The screen's copy keeps the faces' characters (printable ASCII, °, Ω and
the German letters): any other character is written as its ASCII
decomposition, © as (c), and what has none as '?'.  A tab moves to the
next multiple of 8 cells.  A row longer than CELLS breaks at its last
space within CELLS and the rest keeps the row's indent.

SPDX-License-Identifier: MIT
"""

from __future__ import annotations

import argparse
import os
import re
import sys
import unicodedata
from dataclasses import dataclass, field
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
NOTICE = REPO / "NOTICE"
LOCK = REPO / "firmware" / "panel" / "dependencies.lock"
# The heading of NOTICE's list of references.
REFS_SECTION = "Design and protocol references"
# The heading of NOTICE's section on the panel build's fetched components.
PANEL_FETCHES = "Components the ESP32-P4 panel build fetches"
NOTICES = REPO / "third_party" / "notices"
OUT_C = REPO / "shared" / "ui" / "about_gen.c"
DOCS = {"en": REPO / "docs" / "Licenses.md",
        "de": REPO / "docs" / "Licenses-de.md"}
BEGIN = "<!-- gen_licenses: begin -->"
END = "<!-- gen_licenses: end -->"
FIG_BEGIN = "<!-- gen_licenses: figures begin -->"
FIG_END = "<!-- gen_licenses: figures end -->"

# Cells of a row on the screen: 920px of the 10 x 20 face.
CELLS = 92

# Row styles, one character each in about_gen_style[] (about_gen.h).
NAME = "N"      # a component's name and version
DIM = "D"       # its licence and copyright lines
HEAD = "S"      # a notice block's heading
TEXT = "T"      # a notice text's row
BLANK = "B"     # an empty row

HOME = Path.home()
ROOTS = {
    "idf": Path(os.environ.get("IDF_PATH", HOME / "esp" / "esp-idf-v6.1")),
    "pico": Path(os.environ.get("PICO_SDK_PATH",
                                HOME / "toolchains" / "pico-sdk")),
    "riscv": Path(os.environ.get(
        "RCBENCH_RISCV_TC",
        HOME / ".espressif" / "tools" / "riscv32-esp-elf"
        / "esp-15.2.0_20251204" / "riscv32-esp-elf")),
    "arm": Path(os.environ.get(
        "RCBENCH_ARM_TC",
        HOME / "toolchains"
        / "arm-gnu-toolchain-14.2.rel1-x86_64-arm-none-eabi")),
    "hosted": REPO / "firmware" / "panel" / "managed_components"
    / "espressif__esp_hosted",
    "fonts": Path("/usr/share/doc/fonts-dejavu-core"),
    "common": Path("/usr/share/common-licenses"),
    "repo": REPO,
}


@dataclass
class Notice:
    """A licence text: where it comes from and how it is cut out.

    root and path name the origin; first and last its lines (1-based,
    inclusive; 0 for the whole file); strip the comment marks taken off
    each line: "c" for ` * ` and `/* */`, "slash" for `// `, "semi" for
    `; `, "fatfs" for `/ `, "deb" for a Debian copyright file's leading
    space and its `.` lines.  stored: the text lives in
    third_party/notices/<key>.txt; else it is read from the origin, which
    is in this repository.
    """
    key: str
    root: str | None
    path: str = ""
    first: int = 0
    last: int = 0
    strip: str = ""
    stored: bool = True


@dataclass
class Component:
    """A component linked into a firmware image."""
    name: str
    version: str
    spdx: str
    copyright: list[str]
    firmware: str                 # "panel" or "iomcu"
    notices: list[str] = field(default_factory=list)
    registry: str = ""            # its name in the component registry, as
                                  # NOTICE's list of fetched components has it


@dataclass
class Reference:
    """A design or protocol reference: read, no code taken."""
    name: str
    spdx: str
    copyright: list[str]
    use: str          # what it is used for, in docs/Licenses.md
    use_de: str       # the same in docs/Licenses-de.md
    notice_key: str   # how NOTICE's list of references names it


NOTICE_LIST = [
    Notice("apache-2.0", "idf", "LICENSE"),
    Notice("freertos", "idf",
           "components/freertos/FreeRTOS-Kernel/LICENSE.md"),
    Notice("nimble-notice", "idf", "components/bt/host/nimble/nimble/NOTICE"),
    Notice("nimble-bundled", "idf",
           "components/bt/host/nimble/nimble/LICENSE", 203, 227),
    Notice("nimble-os-mbuf", "idf",
           "components/bt/host/nimble/nimble/porting/nimble/src/os_mbuf.c",
           1, 33, "c"),
    Notice("lwip", "idf", "components/lwip/lwip/COPYING"),
    Notice("fatfs", "idf", "components/fatfs/src/ff.c", 5, 17, "fatfs"),
    Notice("http-parser", "idf", "components/http_parser/LICENSE.txt",
           0, 0, "c"),
    Notice("tlsf", None),
    Notice("picolibc", "riscv", "share/licenses/picolibc/COPYING.picolibc"),
    Notice("esp-libc-sifive", "idf",
           "components/esp_libc/src/port/riscv/memcpy.c", 8, 16, "c"),
    Notice("sdmmc", "idf", "components/sdmmc/sdmmc_common.c", 2, 15, "c"),
    Notice("ubsan", "idf", "components/esp_system/ubsan.c", 1, 25, "slash"),
    Notice("protobuf-c", "hosted", "common/protobuf-c/LICENSE"),
    Notice("gcc-runtime", "riscv", "share/licenses/gcc/COPYING.RUNTIME"),
    Notice("mpl-2.0", "common", "MPL-2.0"),
    Notice("qrcodegen", "repo", "third_party/qrcodegen/LICENSE",
           stored=False),
    Notice("bitstream-vera", "fonts", "copyright", 8, 51, "deb"),
    Notice("pico-sdk", "pico", "LICENSE.TXT"),
    Notice("tinyusb", "pico", "lib/tinyusb/LICENSE"),
    Notice("stdio-usb-descriptors", "pico",
           "src/rp2_common/pico_stdio_usb/stdio_usb_descriptors.c",
           2, 26, "c"),
    Notice("printf", "pico", "src/rp2_common/pico_printf/printf.c",
           2, 23, "slash"),
    Notice("pd-uart", "repo", "protocols/pdmini/rp2350/pd_uart.pio",
           11, 35, "semi", stored=False),
    Notice("newlib", "arm", "license.txt", 8721, 10639),
]

ESPRESSIF = "Copyright 2015-2026 Espressif Systems (Shanghai) CO LTD"
RPI = "Copyright (c) 2020 Raspberry Pi (Trading) Ltd."

COMPONENTS = [
    # ---- the panel firmware (ESP32-P4, ESP-IDF v6.1) ----
    Component("ESP-IDF (drivers, esp_lcd, HTTP server, esp-tls, NVS, VFS)",
              "6.1.0",
              "Apache-2.0", [ESPRESSIF], "panel", ["apache-2.0"]),
    Component("FreeRTOS Kernel (ESP-IDF SMP)", "10.5.1", "MIT",
              ["Copyright (C) 2021 Amazon.com, Inc. or its affiliates."],
              "panel", ["freertos"]),
    Component("Apache Mynewt NimBLE (host)", "1.6.0", "Apache-2.0",
              ["Copyright 2015-2022 The Apache Software Foundation",
               "Modifications Copyright 2017-2022 Espressif Systems "
               "(Shanghai) CO., LTD.",
               "os_mbuf.c: Copyright (c) 1982, 1986, 1988, 1991, 1993 "
               "The Regents of the University of California."],
              "panel", ["nimble-notice", "nimble-bundled", "nimble-os-mbuf",
                        "apache-2.0"]),
    Component("lwIP", "2.2.0", "BSD-3-Clause",
              ["Copyright (c) 2001-2004 Swedish Institute of Computer "
               "Science.",
               "Copyright (c) 2002-2003, Adam Dunkels.",
               "Copyright (c) 2001-2004 Leon Woestenberg, "
               "Axon Digital Design B.V.",
               "Copyright (c) 2002 CITEL Technologies Ltd.",
               "Copyright (c) 2010 Inico Technologies Ltd.",
               "Copyright (c) 2007-2009 Frédéric Bernon, Simon Goldschmidt"],
              "panel", ["lwip"]),
    Component("FatFs", "R0.16", "LicenseRef-scancode-fatfs",
              ["Copyright (C) 2025, ChaN, all right reserved."],
              "panel", ["fatfs"]),
    Component("Mbed TLS and TF-PSA-Crypto", "4.1.0, 1.1.0",
              "Apache-2.0 OR GPL-2.0-or-later",
              ["Copyright The Mbed TLS Contributors"], "panel",
              ["apache-2.0"]),
    Component("http_parser", "2.7.0", "MIT",
              ["Copyright Joyent, Inc. and other Node contributors.",
               "Based on src/http/ngx_http_parse.c from NGINX, copyright "
               "Igor Sysoev"], "panel", ["http-parser"]),
    Component("TLSF memory allocator (ESP-IDF heap)", "ESP-IDF 6.1.0",
              "BSD-3-Clause", ["Copyright (c) 2006-2016 Matthew Conte"],
              "panel", ["tlsf"]),
    Component("picolibc (C library)", "1.8.10",
              "LicenseRef-picolibc-COPYING",
              ["Copyright 2019 Keith Packard; 2020 The Newlib Project; "
               "the further holders its COPYING.picolibc names"],
              "panel", ["picolibc"]),
    Component("esp_libc string routines (memcpy, strcmp, strcpy from "
              "SiFive; memmove, memcmp, strncmp, strncpy from newlib)",
              "ESP-IDF 6.1.0", "BSD-2-Clause-FreeBSD AND Apache-2.0",
              ["Copyright (c) 2017 SiFive Inc.",
               "Copyright (c) 1994-2009 Red Hat, Inc."],
              "panel", ["esp-libc-sifive"]),
    Component("SD/MMC driver (ESP-IDF sdmmc, from OpenBSD)", "ESP-IDF 6.1.0",
              "ISC", ["Copyright (c) 2006 Uwe Stuehler"], "panel", ["sdmmc"]),
    Component("UBSan handlers (ESP-IDF esp_system, from HelenOS)",
              "ESP-IDF 6.1.0", "BSD-2-Clause",
              ["Copyright (c) 2016, Linaro Limited"], "panel", ["ubsan"]),
    Component("ESP-Hosted-MCU (link to the ESP32-C6)", "2.13.0",
              "Apache-2.0", [ESPRESSIF], "panel", ["apache-2.0"],
              "espressif/esp_hosted"),
    Component("protobuf-c (in ESP-Hosted-MCU)", "1.4.1", "BSD-2-Clause",
              ["Copyright (c) 2008-2022, Dave Benson and the protobuf-c "
               "authors."], "panel", ["protobuf-c"]),
    Component("Mozilla CA root certificates (ESP-IDF certificate bundle, "
              "common authorities)", "Mozilla data of 2026-02-11", "MPL-2.0",
              ["components/mbedtls/esp_crt_bundle/cacrt_all.pem of ESP-IDF "
               "v6.1 (Mozilla's certdata.txt of 2026-02-11, converted by "
               "mk-ca-bundle.pl 1.32), filtered by cmn_crt_authorities.csv, "
               "and cacrt_local.pem of the same folder",
               "Source: https://curl.se/docs/caextract.html"], "panel",
              ["mpl-2.0"]),
    Component("GCC runtime libraries (libgcc, libstdc++)", "15.2.0",
              "GPL-3.0-or-later WITH GCC-exception-3.1",
              ["Copyright (C) Free Software Foundation, Inc."], "panel",
              ["gcc-runtime"]),
    Component("QR Code generator library (qrcodegen)", "1.8.0", "MIT",
              ["Copyright (c) Project Nayuki"], "panel", ["qrcodegen"]),
    Component("DejaVu Sans Mono (glyph bitmaps of tools/gen_font.py)",
              "2.37", "Bitstream-Vera",
              ["Copyright (c) 2003 by Bitstream, Inc. All Rights Reserved.",
               "DejaVu changes are in public domain."], "panel",
              ["bitstream-vera"]),
    # ---- the IOmcu firmware (RP2350, pico-sdk 2.3.0) ----
    Component("Raspberry Pi Pico SDK", "2.3.0", "BSD-3-Clause",
              ["Copyright 2020 (c) 2020 Raspberry Pi (Trading) Ltd."],
              "iomcu", ["pico-sdk"]),
    Component("TinyUSB (in the Pico SDK)", "0.18.0", "MIT",
              ["Copyright (c) 2018, hathach (tinyusb.org)",
               RPI + " (the RP2040 port)"], "iomcu", ["tinyusb"]),
    Component("USB descriptors of pico_stdio_usb (from MicroPython)",
              "Pico SDK 2.3.0", "MIT",
              [RPI, "Copyright (c) 2019 Damien P. George"], "iomcu",
              ["stdio-usb-descriptors"]),
    Component("printf of pico_printf", "Pico SDK 2.3.0", "MIT",
              ["(c) Marco Paland (info@paland.com) 2014-2019, "
               "PALANDesign Hannover, Germany"], "iomcu", ["printf"]),
    Component("uart_tx and uart_rx PIO programs of pico-examples "
              "(pd_uart.pio)", "pico-examples", "BSD-3-Clause", [RPI],
              "iomcu", ["pd-uart"]),
    Component("newlib (C library of the arm-none-eabi toolchain)", "4.4.0",
              "LicenseRef-newlib-COPYING",
              ["Copyright (c) 1994-2009 Red Hat, Inc.; the further holders "
               "its COPYING.NEWLIB names"], "iomcu", ["newlib"]),
    Component("GCC runtime library (libgcc)", "14.2.1",
              "GPL-3.0-or-later WITH GCC-exception-3.1",
              ["Copyright (C) Free Software Foundation, Inc."], "iomcu",
              ["gcc-runtime"]),
]

REFERENCES = [
    Reference("LZMA specification and LzmaEnc.c of the LZMA SDK",
              "LicenseRef-LZMA-SDK-public-domain", ["Igor Pavlov"],
              "the literal-only encoder of protocols/supvan/lzlit.c and "
              "tools/lzlit.py",
              "der Literal-Encoder von protocols/supvan/lzlit.c und "
              "tools/lzlit.py", "LZMA SDK"),
    Reference("heeen/supvan-cups", "MIT", ["Copyright (c) 2026 Florian Häne"],
              "the print protocol facts of protocols/supvan",
              "die Fakten des Druckprotokolls von protocols/supvan",
              "heeen/supvan-cups"),
]

# The doc tables' words, per language.
DOC_WORDS = {
    "en": {"head": "| Component | Version | Licence (SPDX) | Copyright "
                   "| Firmware |",
           "panel": "panel", "iomcu": "IOmcu",
           "refs": "| Reference | Licence (SPDX) | Copyright | Used for |"},
    "de": {"head": "| Komponente | Version | Lizenz (SPDX) | Copyright "
                   "| Firmware |",
           "panel": "Panel", "iomcu": "IOmcu",
           "refs": "| Referenz | Lizenz (SPDX) | Copyright | Verwendet für |"},
}

# The figures table's head and labels, per language, in doc_figures()'s
# order.
FIGURE_WORDS = {
    "en": ["| Figure | Value |",
           "Third-party components, panel firmware",
           "Third-party components, IOmcu firmware",
           "Design and protocol references",
           "Licence texts: stored in `third_party/notices/` or read in place",
           "Rows ABOUT shows from this list",
           "Of them, rows of the licence texts",
           "Cells in a row, at most"],
    "de": ["| Größe | Wert |",
           "Fremdkomponenten, Panel-Firmware",
           "Fremdkomponenten, IOmcu-Firmware",
           "Design- und Protokollreferenzen",
           "Lizenztexte: in `third_party/notices/` gespeichert oder an Ort "
           "und Stelle gelesen",
           "Zeilen, die ABOUT aus dieser Liste zeigt",
           "Davon Zeilen der Lizenztexte",
           "Zeichen einer Zeile, höchstens"],
}

# The parts of parts(), as about_gen.h numbers them.
ABOUT_GEN_NOTICES = 3

# The faces' characters beyond printable ASCII (ui_text.def).
FACE_EXTRA = set("°ΩÄÖÜäöüß")
# Characters with no ASCII decomposition, and what stands for them.
SPELLED = {"©": "(c)", "®": "(R)", "“": '"', "”": '"', "‘": "'", "’": "'",
           "–": "-", "—": "-", "…": "...", "•": "*", " ": " ",
           "ø": "o", "Ø": "O", "æ": "ae", "Æ": "AE", "ł": "l", "Ł": "L",
           "đ": "d", "ð": "d", "þ": "th", "ı": "i"}


# ------------------------------------------------------------ the texts

def strip_line(line: str, how: str) -> str:
    """One line of an origin with its comment marks taken off."""
    s = line.rstrip("\n").rstrip()
    if how == "c":
        t = s.lstrip()
        for mark in ("/*", "*/", "*"):
            if t.startswith(mark):
                t = t[len(mark):]
                break
        if t.endswith("*/"):
            t = t[:-2]
        return t[1:].rstrip() if t.startswith(" ") else t.rstrip()
    if how == "slash":
        return s[3:] if s.startswith("// ") else s.lstrip("/")
    if how == "semi":
        return s[2:] if s.startswith("; ") else s.lstrip(";")
    if how == "fatfs":
        return s[2:] if s.startswith("/ ") else s.lstrip("/")
    if how == "deb":
        t = s[1:] if s.startswith(" ") else s
        return "" if t == "." else t
    return s


def extract(n: Notice) -> str:
    """The notice cut out of its origin, as it is stored."""
    origin = ROOTS[n.root] / n.path
    lines = origin.read_text(encoding="utf-8", errors="replace").splitlines()
    if n.first:
        lines = lines[n.first - 1:n.last]
    out = [strip_line(x, n.strip) for x in lines]
    while out and not out[0].strip():
        out.pop(0)
    while out and not out[-1].strip():
        out.pop()
    return "\n".join(out) + "\n"


def notice_path(n: Notice) -> Path:
    """Where a stored notice lives."""
    return NOTICES / f"{n.key}.txt"


def notice_text(n: Notice) -> str:
    """The notice as the generator reads it."""
    if n.stored:
        return notice_path(n).read_text(encoding="utf-8")
    return extract(n)


def face(s: str) -> str:
    """@p s in the faces' characters, tabs expanded."""
    out = []
    col = 0
    for ch in s:
        if ch == "\t":
            pad = 8 - col % 8
            out.append(" " * pad)
            col += pad
            continue
        if " " <= ch <= "~" or ch in FACE_EXTRA:
            rep = ch
        elif ch in SPELLED:
            rep = SPELLED[ch]
        else:
            dec = unicodedata.normalize("NFKD", ch)
            rep = "".join(c for c in dec if " " <= c <= "~") or "?"
        out.append(rep)
        col += len(rep)
    return "".join(out)


def wrap(s: str, cells: int = CELLS) -> list[str]:
    """@p s in rows of at most @p cells, broken at spaces."""
    s = face(s).rstrip()
    if len(s) <= cells:
        return [s]
    indent = len(s) - len(s.lstrip(" "))
    indent = indent if indent < cells // 2 else 0
    rows = []
    while len(s) > cells:
        cut = s.rfind(" ", indent + 1, cells + 1)
        if cut <= indent:
            cut = cells
        rows.append(s[:cut].rstrip())
        s = " " * indent + s[cut:].lstrip(" ")
    rows.append(s)
    return rows


# ------------------------------------------------------------ the rows

def table_rows(firmware: str) -> list[tuple[str, str]]:
    """The screen's table of one firmware's components."""
    rows: list[tuple[str, str]] = []
    for c in (c for c in COMPONENTS if c.firmware == firmware):
        rows += [(NAME, r) for r in wrap(f"{c.name} {c.version}")]
        rows += [(DIM, r) for r in wrap("  " + c.spdx)]
        for line in c.copyright:
            rows += [(DIM, r) for r in wrap("  " + line)]
        rows.append((BLANK, ""))
    return rows


def reference_rows() -> list[tuple[str, str]]:
    """The screen's rows of the references: name, licence and copyright.

    What each is used for is prose and stays in the docs' table.
    """
    rows: list[tuple[str, str]] = []
    for r in REFERENCES:
        rows += [(NAME, x) for x in wrap(r.name)]
        rows += [(DIM, x) for x in wrap("  " + r.spdx)]
        for line in r.copyright:
            rows += [(DIM, x) for x in wrap("  " + line)]
        rows.append((BLANK, ""))
    return rows


def notice_rows() -> list[tuple[str, str]]:
    """The notice texts, the Apache License 2.0 once at the end.

    Per component its heading, its copyright lines and its own texts.
    """
    by_key = {n.key: n for n in NOTICE_LIST}
    shown: set[str] = set()
    rows: list[tuple[str, str]] = []
    apache_users = []
    for c in COMPONENTS:
        if "apache-2.0" in c.notices:
            apache_users.append(c.name.split(" (")[0])
        rows += [(HEAD, r) for r in wrap(f"{c.name} {c.version} ({c.spdx})")]
        for line in c.copyright:
            rows += [(DIM, r) for r in wrap(line)]
        for key in c.notices:
            if key == "apache-2.0" or key in shown:
                continue
            shown.add(key)
            rows.append((BLANK, ""))
            for line in notice_text(by_key[key]).splitlines():
                if not line.strip():
                    rows.append((BLANK, ""))
                    continue
                rows += [(TEXT, r) for r in wrap(line)]
        rows.append((BLANK, ""))
    rows += [(HEAD, r) for r in wrap("Apache License 2.0 (Apache-2.0): "
                                     + ", ".join(apache_users))]
    rows.append((BLANK, ""))
    for line in notice_text(by_key["apache-2.0"]).splitlines():
        if not line.strip():
            rows.append((BLANK, ""))
            continue
        rows += [(TEXT, r) for r in wrap(line)]
    while rows and rows[-1][0] == BLANK:
        rows.pop()
    return rows


def parts() -> list[list[tuple[str, str]]]:
    """The four parts, in the order of about_gen.h's ABOUT_GEN_*."""
    return [table_rows("panel"), table_rows("iomcu"), reference_rows(),
            notice_rows()]


# ------------------------------------------------------------ the C file

def c_string(s: str) -> str:
    """@p s as a C string literal, UTF-8 bytes past ASCII escaped."""
    out = []
    for b in s.encode("utf-8"):
        ch = chr(b)
        if ch in '"\\':
            out.append("\\" + ch)
        elif 0x20 <= b < 0x7F and ch != "?":
            out.append(ch)
        else:
            out.append(f"\\{b:03o}")
    return '"' + "".join(out) + '"'


def render_c() -> str:
    """The text of shared/ui/about_gen.c."""
    ps = parts()
    starts = [0]
    for p in ps:
        starts.append(starts[-1] + len(p))
    total = starts[-1]
    if total >= 65536:
        raise SystemExit("gen_licenses: more rows than a uint16_t counts")
    rows = [r for p in ps for r in p]
    lines = [
        "/**",
        " * @file",
        " * @brief The ABOUT screen's third-party table and notice texts "
        "(about_gen.h).",
        " *",
        " * Generated by tools/gen_licenses.py -- do not edit by hand.",
        " *",
        f" * {total} rows of at most {CELLS} cells in 4 parts: the panel "
        "firmware's",
        " * components, the IOmcu firmware's, the references and the "
        "notice texts.",
        " *",
        " * SPDX-License-Identifier: MIT",
        " */",
        "",
        '#include "about_gen.h"',
        "",
        "/** @brief Each part's first row, and the row count last. */",
        "const uint16_t about_gen_part[ABOUT_GEN_PARTS + 1] = {",
        "    " + ", ".join(f"{s}u" for s in starts),
        "};",
        "",
        "/** @brief Each row's style, one letter a row (about_gen.h). */",
        "const char about_gen_style[] =",
    ]
    styles = "".join(s for s, _ in rows)
    for i in range(0, len(styles), 64):
        lines.append(f'    "{styles[i:i + 64]}"')
    lines[-1] += ";"
    lines += ["", "/** @brief Each row's text. */",
              "const char *const about_gen_text[] = {"]
    lines += [f"    {c_string(t)}," for _, t in rows]
    lines += ["};", ""]
    return "\n".join(lines)


# ------------------------------------------------------------ the docs

def md(s: str) -> str:
    """@p s for a Markdown table cell."""
    return s.replace("|", "\\|")


def doc_table(lang: str) -> str:
    """The table between the markers of the page in @p lang."""
    w = DOC_WORDS[lang]
    out = [w["head"], "| --- | --- | --- | --- | --- |"]
    for c in COMPONENTS:
        out.append(f"| {md(c.name)} | {md(c.version)} | `{c.spdx}` | "
                   f"{md('<br>'.join(c.copyright))} | {w[c.firmware]} |")
    out += ["", w["refs"], "| --- | --- | --- | --- |"]
    for r in REFERENCES:
        use = r.use_de if lang == "de" else r.use
        out.append(f"| {md(r.name)} | `{r.spdx}` | "
                   f"{md('<br>'.join(r.copyright))} | {md(use)} |")
    return "\n".join(out)


def thousands(n: int, lang: str) -> str:
    """@p n with the thousands separator of @p lang."""
    s = f"{n:,}"
    return s.replace(",", ".") if lang == "de" else s


def doc_figures(lang: str) -> str:
    """The figures table of the page in @p lang."""
    ps = parts()
    rows = sum(len(p) for p in ps)
    texts = len(NOTICE_LIST)
    panel = sum(c.firmware == "panel" for c in COMPONENTS)
    iomcu = sum(c.firmware == "iomcu" for c in COMPONENTS)
    words = FIGURE_WORDS[lang]
    values = [panel, iomcu, len(REFERENCES), texts, rows,
              len(ps[ABOUT_GEN_NOTICES]), CELLS]
    out = [words[0], "| --- | --- |"]
    out += [f"| {label} | {thousands(v, lang)} |"
            for label, v in zip(words[1:], values, strict=True)]
    return "\n".join(out)


def put_block(text: str, begin: str, end: str, body: str, name: str) -> str:
    """@p text with the lines between @p begin and @p end renewed."""
    try:
        head, rest = text.split(begin + "\n", 1)
        _, tail = rest.split(end, 1)
    except ValueError:
        raise SystemExit(f"gen_licenses: {name} lacks the marker lines "
                         f"{begin}") from None
    return head + begin + "\n" + body + "\n" + end + tail


def render_doc(lang: str, old: str) -> str:
    """The page with its table and its figures renewed."""
    name = DOCS[lang].name
    text = put_block(old, BEGIN, END, doc_table(lang), name)
    return put_block(text, FIG_BEGIN, FIG_END, doc_figures(lang), name)


# ------------------------------------------------------------ the modes

def outputs() -> dict[Path, str]:
    """Every file the generator writes and its text."""
    out = {OUT_C: render_c()}
    for lang, path in DOCS.items():
        out[path] = render_doc(lang, path.read_text(encoding="utf-8"))
    return out


def section(text: str, heading: str) -> list[str] | None:
    """The lines of NOTICE's section under @p heading, None without one."""
    lines = text.splitlines()
    try:
        start = lines.index(heading)
    except ValueError:
        return None
    end = next((k for k in range(start + 2, len(lines) - 1)
                if lines[k + 1].startswith("---")), len(lines))
    return lines[start:end]


def lock_components(lock: str) -> set[tuple[str, str]]:
    """The registry components dependencies.lock fetches, with versions."""
    out = set()
    name = None
    for line in lock.splitlines():
        m = re.match(r"^  (espressif/[^:\s]+):\s*$", line)
        if m:
            name = m.group(1)
            continue
        m = re.match(r"^    version: '?([^'\s]+)'?\s*$", line)
        if m and name:
            out.add((name, m.group(1)))
            name = None
    return out


def notice_problems(text: str, lock: str) -> list[str]:
    """Where NOTICE disagrees with the list and with dependencies.lock.

    In NOTICE's section on the panel build's components, a line
    `  espressif/<name> <version>  <licence>` that is not marked
    "(not linked)" is a linked component: it must be a component here with
    that registry name and version, and every component here with a
    registry name must be such a line.  Every component dependencies.lock
    fetches must have a line, linked or not, and every line a lock entry.
    Every reference here must start a line of NOTICE's references.
    """
    body = section(text, PANEL_FETCHES)
    if body is None:
        return [f"NOTICE has no section {PANEL_FETCHES!r}"]
    listed_all = set()
    linked = set()
    for line in body:
        m = re.match(r"^  (espressif/\S+) (\S+)\s+(.*)$", line)
        if m:
            listed_all.add((m.group(1), m.group(2)))
            if "(not linked)" not in m.group(3):
                linked.add((m.group(1), m.group(2)))
    listed = {(c.registry, c.version) for c in COMPONENTS if c.registry}
    out = [f"NOTICE lists {n} {v} as linked; the list does not"
           for n, v in sorted(linked - listed)]
    out += [f"NOTICE does not list {n} {v} as linked" for n, v in
            sorted(listed - linked)]
    fetched = lock_components(lock)
    out += [f"NOTICE lacks {n} {v}, which dependencies.lock fetches"
            for n, v in sorted(fetched - listed_all)]
    out += [f"NOTICE lists {n} {v}, which dependencies.lock does not fetch"
            for n, v in sorted(listed_all - fetched)]
    refs = section(text, REFS_SECTION)
    if refs is None:
        return [*out, f"NOTICE has no section {REFS_SECTION!r}"]
    out += [f"NOTICE's references lack {r.notice_key}" for r in REFERENCES
            if not any(x.startswith("  " + r.notice_key + " ")
                       for x in refs)]
    return out


def notice_now() -> list[str]:
    """NOTICE's problems as the tree stands."""
    return notice_problems(NOTICE.read_text(encoding="utf-8"),
                           LOCK.read_text(encoding="utf-8"))


def check() -> int:
    """Compare the files with what the list writes, and NOTICE with it."""
    bad = 0
    for problem in notice_now():
        print(problem)
        bad += 1
    for n in NOTICE_LIST:
        if n.stored and not notice_path(n).exists():
            print(f"{notice_path(n).relative_to(REPO)}: missing")
            bad += 1
    if bad:
        return 1
    for path, text in outputs().items():
        if not path.exists() or path.read_text(encoding="utf-8") != text:
            name = (path.relative_to(REPO) if path.is_relative_to(REPO)
                    else path)
            print(f"{name}: out of date; run tools/gen_licenses.py")
            bad += 1
    return 1 if bad else 0


def sources(refresh: bool) -> int:
    """Hold the stored notices to their origins, or copy them anew."""
    bad = 0
    for problem in notice_now():
        print(problem)
        bad += 1
    for n in NOTICE_LIST:
        if not n.stored or n.root is None:
            continue
        origin = ROOTS[n.root] / n.path
        if not origin.exists():
            print(f"{n.key}: {origin} not found, skipped")
            continue
        text = extract(n)
        if refresh:
            NOTICES.mkdir(parents=True, exist_ok=True)
            notice_path(n).write_text(text, encoding="utf-8")
        elif notice_path(n).read_text(encoding="utf-8") != text:
            print(f"{n.key}: differs from {origin}")
            bad += 1
    return 1 if bad else 0


def main(argv: list[str] | None = None) -> int:
    """Write, check, refresh or hold the notices to their origins."""
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    g = p.add_mutually_exclusive_group()
    g.add_argument("--check", action="store_true",
                   help="exit 1 when a written file is out of date")
    g.add_argument("--refresh", action="store_true",
                   help="copy the notice texts from their origins")
    g.add_argument("--sources", action="store_true",
                   help="exit 1 when a stored notice differs from its origin")
    a = p.parse_args(argv)
    if a.check:
        return check()
    if a.refresh or a.sources:
        return sources(a.refresh)
    for path, text in outputs().items():
        path.write_text(text, encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
