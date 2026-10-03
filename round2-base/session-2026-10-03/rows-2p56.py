import re, sys
P = sys.argv[1]
t = open(P).read()
D = "(owner, 2026-10-03)"
new = [
 ("V324", "R3, pack overvoltage and transient clamp",
  "FU-2A found no TVS, active clamp or catalogued capacitor bank that holds the ESC pack node under V270's 85 V at the 300 A onboard and 600 A external turn-off. TVS diodes clamp at 113 to 126 V. A simulated bank of 6 x 330 µF at 35 to 52 mOhm peaks under 82 V, with the loop inductance assumed (45 mJ at 1 µH). What holds the node?",
  f"The bus capacitance with damping, with no clamp part. It is a stage 2 requirement for the 300 A onboard path, from FU-2A's figures: 6 x 330 µF at 35 to 52 mOhm, a peak under 82 V, 45 mJ at 1 µH, the loop inductance stated as assumed. The 600 A external path and the external pack switch module are not known. A pass-2 run of R3 researches the switched-node freewheel diode {D}"),
 ("V325", "R7, Pack charger; Pack overcurrent protection",
  "V294 allows 0.2 V from the cells to the converters at 52.1 A, 3.84 mOhm. The BQ25723RSNR's 5 mOhm battery sense resistor sits in that path and alone drops 0.261 V and 13.6 W at 52.1 A. At the 6.0 V corner the pack current reaches 53.8 A with 6 mOhm in the path, over the BQ40Z80's lower overcurrent edge of 52.21 A. At the 100.68 A short-circuit edge the resistor puts 0.503 V across SRP-SRN against the charger's 0.5 V maximum, for up to 915 µs. Which basis judges the pack path?",
  f"The 6.6 V floor of V294: 47.3 to 50.1 A for 3 to 10 mOhm in the path (FU-2P56), below the lower overcurrent edges of 52.21 A untrimmed and 55.25 A post-trim. The parts stay sized at 6.0 V. Current limitations: the sense resistor drops 0.261 V and dissipates 13.6 W at 52.1 A, over the 0.2 V allowance; at the 100.68 A edge it puts 0.503 V across SRP-SRN, 3 mV over the 0.5 V maximum, for up to 915 µs. Each converter's inductor current limit, ILIM_AVG 7 A minimum and 8 A typical with no maximum stated, bounds the draw. The sense resistor's tolerance is a stage 2 requirement {D}"),
 ("V326", "R2, External CAN transceiver",
  "The TCAN1472VDRQ1 is in standby with STB high and in normal mode with STB low, with an integrated pull-up. The enable node is high when enabled. What drives STB?",
  f"The inverted enable from R4's enable inverter, SN74LVC1G04DBVR: the transceiver is in normal mode while the enable node is high and in standby when it falls. The count books a fifth SN74LVC1G04DBVR a board as the upper bound: need 25, gate 125 {D}"),
 ("V327", "R9, V+ OR element (new)",
  "The ADBMS1818's V+ must stay at or above C18 - 0.3 V, and C18 may not exceed V+ + 5.5 V. The LTM8068EY#PBF's fixed 18 V misses the first above 18.0 V of pack (5 cells at 4.2 V) and breaks the second above 23.2 V (6 cells). What feeds V+ for 1 to 16 cells up to 67.2 V?",
  f"A new function of R9, the V+ OR element, from each tap that can be the pack top, with FU-2D's rows as its requirements: the pack-side branch drops at most 0.3 V at 1.4 mA; the supply-side branch blocks up to 67.2 V less the supply voltage, and the pack-side branch blocks the supply into the pack; both rated with V188's 85 V in mind; with the board off, V+ draws 6.1 µA typical and 18 µA maximum from the pack top through it. A pass-2 run of R9 researches it. The unused-input tie stays with stage 2 {D}"),
 ("V328", "R5, source selection",
  "With neither the DC input nor the pack present, the shared USB-C connector powers the board through the BQ25723RSNR's VSYS (V91, found in FU-2I). A 3 A, 5 V source gives 15 W x 94.7 % = 14.2 W at VSYS. The logic draws 16.5 to 16.9 W at the display's peak and 13.9 to 14.3 W continuous. What applies?",
  f"A current limitation, stated with its numbers: the source gives 14.2 W at VSYS (13.3 W at the 2.8 A edge of the input limit); the 3.3 V rail takes 5.4 to 5.8 W of it; the display's 2.0 A peak is not supported from USB-C alone; a 500 mA default source, 2.5 W, cannot carry the 3.3 V rail {D}"),
 ("V329", "R5, display supply",
  "The display's Type_C2 VBUS joins its 5 V net with only a TVS to ground (V80, found 'No' in FU-2I), so the link's 5 V reaches the VBUS pin of the display's native USB-C socket. What applies?",
  f"A current limitation: no host or charger on the display's native USB-C while the link cable carries 5 V. Flashing the display and reading its console use that socket with the link cable removed {D}"),
 ("V330", "R1, Microcontroller",
  "pico-sdk 2.3.0 places a programming mode's PIO UART on any free state machine and does not reserve a port's freed space. After the stated bind order, switching one port into a programming mode is refused in 12 (mix, port) cases, in 8 of the 68 allowed mixes. What bind order applies?",
  f"A port enters a programming mode with the port released, and programming binds come before ports 1 to 4, the socket and PPM. A mode change that cannot keep that order rebinds every output from scratch with the outputs stopped. The firmware takes this order; the PIO budget is checked against it {D}"),
 ("V331", "R5, power indicator LED; pack disconnect wake contact",
  "IOBoard.md requires a power indicator LED, and V301 an operator wake contact from the pack to the BQ40Z80's PACK pin (6.0 to 8.4 V). No category's inventory names a part for either. Where are they selected?",
  f"In stage 2, with the passives and connectors {D}"),
 ("V332", "R1, BOOT and RESET buttons",
  "IOBoard.md requires BOOT and RESET buttons and relay contacts across each for the bench (Test access). No category's inventory names a part for them. Where are they selected?",
  f"In stage 2, with the access across each button for the bench relays {D}"),
 ("V333", "R3, test points",
  "IOBoard.md requires test points on the listed nodes, a star ground pad and a way to lift one device's supply for the isolation tests. No category's inventory names a part for them. Where are they decided?",
  f"In stage 2 {D}"),
]
edit = {
 "V53": f"Through the heartbeat, as V160 states: no input of the switch driver takes a coprocessor signal (owner, 2026-10-03)",
 "V159": f"500 ms, the stop deadline (V1, V288). FU-2P56 bounds the path of V160 at about 277 ms: 17 ms to the INA228's alert, 20 ms to the action, 50 ms to the panel, 185 ms of the monostable, 5 ms of the switch {D}",
 "V160": f"The coprocessor's firmware, through the heartbeat. Its overcurrent and over-temperature action latches its failsafe; it then refuses (NACKs) the panel's control writes, the panel stops its heartbeat, and the monostable opens the ESC pack switches. No processor, register or firmware enters the path from GPIO6 to a gate. The action is not built in the coprocessor's firmware {D}",
 "V11": f"Not known: the split of the 75 pF between the RP2354B pin and the connector is not stated. The TPD6E05U06RVZR adds 0.47 pF typical, with no maximum stated {D}",
 "V36": f"Not known: no stocked 74HC423 states k against temperature. The window budget keeps ±3 % as an assumption, and the corner sweep of Monostable.md step 11 tests the window at 0 and 50 °C {D}",
 "V60": f"Not known: the programmer drives about 2 V, and no Hitec publication states the servo's input-high threshold {D}",
 "V69": f"Not known for S.BUS and iBUS. Read: 3.3 V for SRXL2, 3.0 to 3.3 V for CRSF, a JETI EX Bus one at 3.0 V or more with no maximum; SUMD states no level {D}",
 "V75": f"Not known. Stage 2 sizes the series resistance and clamps on a short to 8.7 V: 29.8 mA and 245 mW in 275 Ohm, against the RP2354B's 12 mA highest drive {D}",
 "V257": f"980.7 N for the thrust cell (a 100 kg TAS501). The torque cells' capacity is not known until round 3 sets the lever arm (V258) {D}",
 "V259": f"Not known until round 3 sets the lever arm and the ranges (V258) {D}",
 "V260": f"Not known until round 3 sets the lever arm and the ranges (V258) {D}",
}
lines = t.split("\n")
seen = set()
for i, l in enumerate(lines):
    m = re.match(r"^\| (V\d+) \| ", l)
    if m and m.group(1) in edit:
        cells = l.split(" | ")
        assert cells[-1].endswith("|"), l[:60]
        old = cells[-1][:-2].strip()
        assert old == "for research (owner, 2026-09-28)", (m.group(1), old)
        cells[-1] = edit[m.group(1)] + " |"
        lines[i] = " | ".join(cells)
        seen.add(m.group(1))
assert seen == set(edit), set(edit) - seen
last = max(i for i, l in enumerate(lines) if l.startswith("| V323 |"))
rows = [f"| {v} | {c} | {q} | {a} |" for v, c, q, a in new]
for r in rows:
    assert r.count(" | ") == 3, r[:60]
lines[last+1:last+1] = rows
open(P, "w").write("\n".join(lines))
print("ok", len(rows), len(seen))
