"""LCDWIKI ES3C28P, 4-GPIO socket P3: a 4-pin JST PH header for it.

The carrier's end of a 1:1 cable from the ES3C28P's 4-pin 1.25mm
"Expanded IO" socket P3. The header is a JST PH B4B-PH-K-S, 2.0mm, top
entry (_jst_ph.py); its pins carry the socket's pins in the socket's
order.

Sources (lcdwiki.com/res/ES3C28P/; not in the repository):
    S: the schematic 2.8inch_ESP32-S3_Display_Schematic.pdf, block
    "Expanded IO": P3 pin 1 IO2, pin 2 IO3, pin 3 IO14, pin 4 IO21, each
    straight to the ESP32-S3, with no resistor.
    D: the outline drawing ES3C28P_Size.pdf: the print beside P3 reads
    IO2, IO3, IO14, IO21, with the pin 1 mark at IO2.

The socket has no power pin and no GND pin: the carrier's ground
reaches the ES3C28P through ES3C28P_PWR or another socket. GPIO3 is one
of the ESP32-S3's strapping pins (the JTAG source), read at reset.

SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import _jst_ph  # noqa: E402

NAME = "ES3C28P_GPIO"
FP = "ES3C28P_GPIO_JST_PH_B4B-PH-K-S"
DESC = ("LCDWIKI ES3C28P 4-GPIO socket P3 (Expanded IO) on a JST PH "
        "B4B-PH-K-S header, 2.0mm, pins 1:1. Pin 1 IO2, pin 2 IO3, pin 3 "
        "IO14, pin 4 IO21: ESP32-S3 GPIOs, bidirectional, 3.3V logic, no "
        "resistor on the ES3C28P. No GND pin. GPIO3 is a strapping pin. "
        "Pin order from the LCDWIKI schematic and print.")
PINS = (("bidirectional", "IO2"), ("bidirectional", "IO3"),
        ("bidirectional", "IO14"), ("bidirectional", "IO21"))

FOOTPRINT = _jst_ph.footprint(FP, DESC, "ES3C28P GPIO",
                              [n for _k, n in PINS])
SYMBOL = _jst_ph.symbol(NAME, DESC, FP, "GPIO", PINS)
MODEL = _jst_ph.model(len(PINS))
