"""LCDWIKI ES3C28P, 5V supply: a 2-pin JST PH header.

The carrier's end of a cable to a USB-C plug in the ES3C28P's USB-C
socket: 5V to VBUS, GND to GND. The ES3C28P has no 1.25mm socket for
a 5V supply; the header is a JST PH B2B-PH-K-S, 2.0mm, top entry
(_jst_ph.py), pin 1 5V, pin 2 GND, the order of the UART socket's first
two pins.

Source: the schematic 2.8inch_ESP32-S3_Display_Schematic.pdf
(lcdwiki.com/res/ES3C28P/; not in the repository). The board's +5 rail
has three feeds:

- USB-C VBUS through a Schottky diode (D8, B5819W), which blocks a
  feed back into VBUS;
- the battery socket JP1 (BAT+, GND) through Q3 (SL2305, P-channel
  MOSFET, gate on VBUS with 100kOhm to GND): Q3 conducts while VBUS is
  absent and blocks while VBUS is up;
- the UART socket's pin 1 through Q5 (SL2305, gate on GND), which
  conducts both ways once the rail or the pin rises past its gate
  threshold.

The +5 rail feeds the 3.3V regulator (ME6217C33M5G, input 2.0V to
6.5V), the audio amplifier and the RGB LED. The charger (TP4054) runs
from VBUS and charges a cell on JP1.

The carrier feeds VBUS. With VBUS up, Q3 keeps a cell on JP1 off the
rail and the charger alone charges it. 5V on the UART socket's pin 1
instead reaches a cell on JP1 through Q3 without the charger, because
VBUS is then absent; and 5V on JP1 would stand on the charger's BAT
pin and the battery divider. The cost of VBUS is the diode's drop,
about 0.3V to 0.4V at 0.3A (B5819W class figure, not measured), so the
regulator sees about 4.6V.

SPDX-License-Identifier: CC-BY-NC-SA-4.0
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import _jst_ph  # noqa: E402

NAME = "ES3C28P_PWR"
FP = "ES3C28P_PWR_JST_PH_B2B-PH-K-S"
DESC = ("LCDWIKI ES3C28P 5V supply on a JST PH B2B-PH-K-S header, 2.0mm, "
        "for a cable to a USB-C plug in the ES3C28P's USB-C socket. Pin 1 "
        "5V: ES3C28P input, VBUS, into its +5 rail through a Schottky "
        "diode (D8); with VBUS up the battery socket stays off the rail. "
        "Pin 2 GND. From the LCDWIKI schematic.")
PINS = (("passive", "5V"), ("passive", "GND"))

FOOTPRINT = _jst_ph.footprint(FP, DESC, "ES3C28P PWR",
                              [n for _k, n in PINS])
SYMBOL = _jst_ph.symbol(NAME, DESC, FP, "PWR", PINS)
MODEL = _jst_ph.model(len(PINS))
