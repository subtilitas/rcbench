"""LCDWIKI ES3C28P, UART socket P2: a 4-pin JST PH header for it.

The carrier's end of a 1:1 cable from the ES3C28P's 4-pin 1.25mm UART
(universal asynchronous receiver-transmitter) socket P2. The header is
a JST PH B4B-PH-K-S, 2.0mm, top entry (_jst_ph.py); its pins carry the
socket's pins in the socket's order.

Sources (lcdwiki.com/res/ES3C28P/; not in the repository):
    S: the schematic 2.8inch_ESP32-S3_Display_Schematic.pdf, block
    "UART": P2 pin 4 RXD0 through 100Ohm (R38), pin 3 TXD0 through
    100Ohm (R39), pin 2 GND, pin 1 to the drain of Q5 (SL2305, P-channel
    MOSFET), whose source is the board's +5 rail and whose gate lies on
    GND through 10kOhm (R40).
    D: the outline drawing ES3C28P_Size.pdf: the print beside P2 reads
    RXD, TXD, GND, 5V, with the pin 1 mark at 5V.
    T: the specification ES3C28P_ES2N28P_Specification_V1.0.pdf, 4.2:
    RXD0 is GPIO44 (U0RXD), TXD0 GPIO43 (U0TXD).

Pin 1, 5V, is the board's +5 rail through Q5. Q5's gate lies on GND,
so it conducts both ways once the rail or the pin rises past its gate
threshold (SL2305 threshold not looked up): the pin gives out the rail
while the board runs, and 5V put on the pin feeds the rail. S also
shows how the rail is fed: USB-C VBUS through a Schottky diode (D8,
B5819W), and the battery socket JP1 through Q3 (SL2305, gate on VBUS),
which conducts while VBUS is absent. 5V on this pin with no USB-C
therefore reaches a cell on JP1 through Q3 without the charger. The
carrier powers the ES3C28P through ES3C28P_PWR and leaves this pin
open.

SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import _jst_ph  # noqa: E402

NAME = "ES3C28P_UART"
FP = "ES3C28P_UART_JST_PH_B4B-PH-K-S"
DESC = ("LCDWIKI ES3C28P UART socket P2 on a JST PH B4B-PH-K-S header, "
        "2.0mm, pins 1:1. Pin 1 5V: the ES3C28P's +5 rail through its "
        "MOSFET Q5, out while the board runs, in when driven; with no "
        "USB-C VBUS, 5V driven here reaches a cell on the battery socket; "
        "leave open, power through ES3C28P_PWR. Pin 2 GND. Pin 3 TXD: "
        "ES3C28P output, GPIO43 (U0TXD) through 100 Ohm. Pin 4 RXD: "
        "ES3C28P input, GPIO44 (U0RXD) through 100 Ohm. UART0, 3.3V "
        "logic. From the LCDWIKI schematic and outline drawing.")
PINS = (("passive", "5V"), ("passive", "GND"), ("output", "TXD"),
        ("input", "RXD"))

FOOTPRINT = _jst_ph.footprint(FP, DESC, "ES3C28P UART",
                              [n for _k, n in PINS])
SYMBOL = _jst_ph.symbol(NAME, DESC, FP, "UART", PINS)
MODEL = _jst_ph.model(len(PINS))
