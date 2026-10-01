"""Check an IO board pin map against the RP2350's pin functions.

Usage: pinmap_check.py PINMAP.json [--sdk PICO_SDK_PATH]

PINMAP.json holds one object per coprocessor:

    {"chips": [{"chip": "main", "pins": [
        {"gpio": 2, "signal": "link CAN SCLK", "function": "SPI0_SCLK",
         "bus": "can_spi", "external": false}, ...]}]}

"function" is a name from io_bank0.h's FUNCSEL values (SPI0_SCLK, UART1_TX,
I2C0_SDA, PWM_A_3, ...), or one of PIO0, PIO1, PIO2, SIO or ADC. "external"
marks a signal that reaches a connector. A PIO pin carries "protocol"
(for example i2c, spi, uart, dshot, quadrature or input), and a pin of a
shared bus carries "bus". The checks:

- each GPIO is 0 to 47 and used once a chip;
- a named function exists on that GPIO in the pico-sdk's io_bank0.h;
- every pin is an object with a GPIO number, a signal and a function;
- every SPI, UART or I2C pin, and every PIO pin whose protocol is one of
  them, names its bus; the pins of one bus agree on the instance, a PIO
  block counting as an instance; an I2C bus has 2 pins;
- every PIO pin names its protocol;
- a PIO block's pins fit one 32-pin window, GPIO 0 to 31 or 16 to 47;
- ADC is on GPIO 40 to 47, the RP2350B's ADC inputs;
- an external signal is on GPIO 0 to 39, the fault-tolerant pins;
- the map has exactly one main and one measurement chip, each with pins.

It prints one line per failure and the pin count per chip, and exits 1
on any failure.
"""
import argparse
import json
import os
import re
import sys

REGS = "src/rp2350/hardware_regs/include/hardware/regs/io_bank0.h"
# GPIO 0 to 39 are fault tolerant (IOBoard.md, Pin ratings).
FT_LAST = 39
ADC_PINS = range(40, 48)
PIO_WINDOWS = ((0, 31), (16, 47))
CHIPS = ("main", "measurement")


def funcsel(sdk):
    text = open(os.path.join(sdk, REGS), encoding="utf-8").read()
    table = {}
    pattern = (r"#define IO_BANK0_GPIO(\d+)_CTRL_FUNCSEL_VALUE_(\w+)"
               r" _u\(0x[0-9a-f]+\)")
    for m in re.finditer(pattern, text):
        table.setdefault(int(m.group(1)), set()).add(m.group(2))
    return table


def instance(function):
    m = re.match(r"(SPI\d|UART\d|I2C\d)_", function)
    return m.group(1) if m else None


SERIAL = ("i2c", "spi", "uart")


def check_chip(chip, table):
    fails = []
    name = chip.get("chip", "?")
    seen = {}
    pio = {}
    raw = chip.get("pins", [])
    raw = raw if isinstance(raw, list) else []
    pins = [p for p in raw if isinstance(p, dict)]
    if len(pins) != len(raw):
        fails.append(f"{name}: a pin entry is not an object")
    for pin in pins:
        if not pin.get("signal") or not pin.get("function"):
            fails.append(f"{name} GPIO{pin.get('gpio')}: a pin names no "
                         f"signal or no function")
        gpio, fn = pin.get("gpio"), pin.get("function", "")
        sig = pin.get("signal", "?")
        where = f"{name} GPIO{gpio} ({sig})"
        if (not isinstance(gpio, int) or isinstance(gpio, bool)
                or not 0 <= gpio <= 47):
            fails.append(f"{where}: no such GPIO")
            continue
        if gpio in seen:
            fails.append(f"{where}: also used by {seen[gpio]}")
        seen[gpio] = sig
        if fn in ("PIO0", "PIO1", "PIO2"):
            pio.setdefault(fn, []).append(gpio)
            if not pin.get("protocol"):
                fails.append(f"{where}: a PIO pin names no protocol")
        elif fn == "ADC":
            if gpio not in ADC_PINS:
                fails.append(f"{where}: ADC needs GPIO 40 to 47")
        elif fn != "SIO" and fn not in table.get(gpio, set()):
            fails.append(f"{where}: {fn} is not a function of GPIO{gpio}")
        if pin.get("external") and gpio > FT_LAST:
            fails.append(f"{where}: an external signal needs a fault-tolerant "
                         f"pin, GPIO 0 to {FT_LAST}")
    for block, gpios in pio.items():
        lo_pin, hi_pin = min(gpios), max(gpios)
        if not any(lo <= lo_pin and hi_pin <= hi for lo, hi in PIO_WINDOWS):
            fails.append(f"{name} {block}: GPIO {lo_pin} to {hi_pin} do not "
                         f"fit one window, 0 to 31 or 16 to 47")
    groups = {}
    i2c = {}
    for pin in pins:
        fn = pin.get("function", "")
        where = f"{name} GPIO{pin.get('gpio')} ({pin.get('signal', '?')})"
        if fn in ("PIO0", "PIO1", "PIO2"):
            inst, proto = fn, pin.get("protocol", "")
        else:
            inst = instance(fn)
            proto = inst[:-1].lower() if inst else ""
        if proto not in SERIAL:
            continue
        if not pin.get("bus"):
            fails.append(f"{where}: {fn} {proto} names no bus")
            continue
        groups.setdefault(pin["bus"], set()).add(inst)
        if proto == "i2c":
            i2c[pin["bus"]] = i2c.get(pin["bus"], 0) + 1
    for bus, insts in groups.items():
        if len(insts) > 1:
            fails.append(f"{name} bus {bus}: mixes {', '.join(sorted(insts))}")
    for bus, count in i2c.items():
        if count != 2:
            fails.append(f"{name} bus {bus}: an I2C bus has 2 pins, not "
                         f"{count}")
    return fails, len(seen)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("pinmap")
    ap.add_argument("--sdk", default=os.environ.get(
        "PICO_SDK_PATH", os.path.expanduser("~/toolchains/pico-sdk")))
    args = ap.parse_args()
    table = funcsel(args.sdk)
    doc = json.load(open(args.pinmap, encoding="utf-8"))
    total = []
    chips = doc.get("chips") if isinstance(doc, dict) else None
    if not isinstance(chips, list):
        total.append("the map has no chips")
        chips = []
    if not all(isinstance(c, dict) for c in chips):
        total.append("a chip entry is not an object")
        chips = [c for c in chips if isinstance(c, dict)]
    names = [c.get("chip") for c in chips]
    for want in CHIPS:
        if names.count(want) != 1:
            total.append(f"the map has {names.count(want)} {want} chip(s), "
                         f"not 1")
    for extra in sorted(set(names) - set(CHIPS), key=str):
        total.append(f"the map has an unknown chip: {extra}")
    for chip in chips:
        if not chip.get("pins"):
            total.append(f"{chip.get('chip', '?')}: no pins")
        fails, used = check_chip(chip, table)
        total += fails
        label = chip.get("chip", "?")
        print(f"{label}: {used} of 48 GPIO used, {48 - used} free")
    for line in total:
        print("FAIL " + line)
    print(f"{len(total)} failure(s)")
    return 1 if total else 0


if __name__ == "__main__":
    sys.exit(main())
