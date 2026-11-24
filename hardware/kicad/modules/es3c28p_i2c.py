"""LCDWIKI ES3C28P, I2C socket P4: a 4-pin JST PH header for it.

The carrier's end of a 1:1 cable from the ES3C28P's 4-pin 1.25mm I2C
(inter-integrated circuit bus) socket P4. The header is a JST PH
B4B-PH-K-S, 2.0mm, top entry (_jst_ph.py); its pins carry the socket's
pins in the socket's order.

Sources (lcdwiki.com/res/ES3C28P/; not in the repository):
    S: the schematic 2.8inch_ESP32-S3_Display_Schematic.pdf, block
    "IIC": P4 pin 1 VCC3V3, pin 2 GND, pin 3 IIC_SCL, pin 4 IIC_SDA;
    SCL and SDA pulled to 3.3V by 4.7kOhm (R29, R30); the 3.3V rail
    comes from an ME6217C33M5G regulator (U3) on the +5 rail.
    D: the outline drawing ES3C28P_Size.pdf: the print beside P4 reads
    3.3V, GND, IO15(SCL), IO16(SDA), with the pin 1 mark at 3.3V.
    T: the specification ES3C28P_ES2N28P_Specification_V1.0.pdf, 4.2:
    SCL is GPIO15, SDA GPIO16; the bus also carries the touch
    controller (FT6336G, address 0x38) and the audio codec (ES8311,
    0x18).
    A tester's photo of a delivered board, 2026-11-20, confirms the pin
    order.

The ES3C28P is the bus controller. 3V3 is an output of its regulator.

SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import _jst_ph  # noqa: E402

NAME = "ES3C28P_I2C"
FP = "ES3C28P_I2C_JST_PH_B4B-PH-K-S"
DESC = ("LCDWIKI ES3C28P I2C socket P4 on a JST PH B4B-PH-K-S header, "
        "2.0mm, pins 1:1. Pin 1 3V3: ES3C28P output, its 3.3V regulator. "
        "Pin 2 GND. Pin 3 SCL: GPIO15, ES3C28P is the controller. Pin 4 "
        "SDA: GPIO16, bidirectional. Both pulled to 3.3V by 4.7k on the "
        "ES3C28P; the bus also carries its touch controller (0x38) and "
        "audio codec (0x18). Pin order from the LCDWIKI schematic and "
        "print, confirmed on a delivered board.")
PINS = (("passive", "3V3"), ("passive", "GND"), ("bidirectional", "SCL"),
        ("bidirectional", "SDA"))

FOOTPRINT = _jst_ph.footprint(FP, DESC, "ES3C28P I2C",
                              [n for _k, n in PINS])
SYMBOL = _jst_ph.symbol(NAME, DESC, FP, "I2C", PINS)
MODEL = _jst_ph.model(len(PINS))
