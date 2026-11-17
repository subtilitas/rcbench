# Handover

Open to-dos for the owner. A new session reads this page after
[STATUS.md](STATUS.md) and names each open item to the owner at the start.
An item leaves this page when it is done; the result goes into the page it
names.

## Owner to-dos

### 1. Rev 2 data of the rbmod carrier

The repository holds no board data of the rbmod carrier: no `.kicad_pcb`,
no `.kicad_sch`, no netlist, for rev 1 or rev 2.
[docs/Carrier.md](docs/Carrier.md) describes rev 1 and names rev 2 only as
planned. The pin map of `rcbench-iomcu-rbmod` and the panel's carrier
views depend on it.

Needed, in order of preference:

1. The rev 2 KiCad files (schematic and board) or a netlist, in
   `hardware/kicad/rbmod/` under CC BY-NC-SA 4.0. The pin table, connectors
   and parts of `docs/Carrier.md` are then derived from them and checked
   against the firmware.
2. Otherwise the changes from rev 1, answering each question:
   - Which GPIOs changed? PROGP03 is expected on GPIO 15 (rework item 1).
   - Is the pull-down on M7's EN on the board? Its value?
   - Is the 5.6V zener on M7's output on the board? Which part?
   - Where is the 25V 1500uF electrolytic capacitor, on which net?
   - Is the divider 100kOhm / 10kOhm from +15V to GPIO 26 fitted?
   - Does J6 stay pads for wires to the USB-C plug board?
   - Are any connectors added or removed?

### 2. Carrier facts to enter into docs/Carrier.md and docs/Carrier-de.md

Stated by the owner on 2026-11-16, not yet in the docs. Enter them with
item 1:

| Item | Value |
| --- | --- |
| J3 (heartbeat), J9 (tap) | JST PH 2.0, 2-pin |
| J4 (I2C 2), J5 (sensor I2C), J7 (panel supply) | JST PH 2.0, 4-pin |
| J8 (PD mini output to the servo rails) | not fitted; the cable is soldered to the pads |
| M7, M8 | MINI560 buck modules, 5V out, 7 to 20V in, EN pad |
| PD mini | WeAct PD Power Mini Buck V1 and V1.1 both work |
| Panel cables (12-pin and 2 x 4-pin PH 2.0) | supplied with the display board |
| Filter capacitor | 25V, 1500uF electrolytic; net unknown (item 1) |
| Angled 3 x 4 pin headers for J1 and J2 | supplied with the group-order board |

The kicad spec `hardware/kicad/modules/dcdc_buck_mini.py` describes a
30 x 18mm module with input 6 to 20V. Whether the MINI560 fits that
footprint is not checked.

### 3. M5Stack Dial: hardware state in STATUS.md

[STATUS.md, M5Stack Dial](STATUS.md#m5stack-dial) records the Dial as
"built, not run with a Dial". The owner reports it running on hardware:
servo moved from the Dial, live current, voltage, power and angle on its
display, updates over Bluetooth LE. The owner names which functions ran, and
the section is corrected to that.

### 4. ELECROW ESP32 Rotary Display 1.28 inch as a Dial alternative

Listed by the owner as an alternative to the M5Stack Dial. Branch
`dial/crow128` holds a board choice of `firmware/dial`
(`main/dial_board.h`, `sdkconfig.crow128`): update kind 5, INFO hardware
byte 2. It builds with ESP-IDF v6.1; it has not run on the board. Open
before it merges:

- Run it on the board: display orientation (no X mirror), touch mapping,
  encoder counts a detent (2 per the EC3501 datasheet, 4 in the vendor's
  example) and direction, the CST816D's start.
- The panel carries the CrowPanel's image and picks the Dial row's image
  by the bound knob's INFO hardware byte (`panel_devupd.c`); a panel
  built before the branch refuses INFO hardware 2, so it does not bind.
- CI (`dial.yml`, a second build), the release assets,
  `tools/c6_image.py` (role `dial-crow`), docs/Dial.md and Dial-de.md,
  STATUS.md, CHANGELOG.md.

### 5. Group order of rev 2 boards

Deadline 2026-11-29. Each order holds one bare board and the angled servo
pin headers. A build video for rev 2 is announced; it does not exist yet.
