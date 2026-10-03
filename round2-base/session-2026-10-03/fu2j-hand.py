import json, sys
S = sys.argv[1]
d = json.load(open(S + "/fu2j-base.json"))
R = ("the owner's rulings of 2026-10-03 on FU-2P56 (Raised by P1, V324 to "
     "V333; V53, V159, V160)")
hand = [
 {"category": "R3", "function": "switched-node freewheel diode (negative clamp of the ESC-side node, V54)",
  "requirement": "No part: FU-2A's P3 named it a missed function and the re-rank returned no candidate. The diode clamps the ESC-side switched node when the pack switch opens (V54). It carries 300 A onboard and 600 A external for the 0.5 uH discharge, 22.5 mJ and 90 mJ, and stands 67.2 V reverse with the switch closed (F15). The turn-off qualification of IPT015N10N5ATMA1 assumes about 1 V across it, VDS up to 86 V with the node under 85 V. Read its forward voltage at 300 A, its single-pulse current and energy ratings and its reverse rating, and state the onboard turn-off margin with it. The 600 A external path is not known (V324): state the diode's figures there as reported, not as pass or fail. The ESC pack node is held by its bus capacitance with damping, a stage 2 requirement, not a clamp part (V324)",
  "source": "FU-2A/016-P3-R3.json missed functions; FU-2A/012-P2-R3.json 'onboard switch turn-off'; V54, V270, V324",
  "reason": R},
 {"category": "R3", "function": "pack overvoltage and transient clamp; external pack switch module",
  "requirement": "No part, by V324: the bus capacitance with damping holds the ESC pack node under 85 V, a stage 2 requirement with FU-2A's figures (6 x 330 uF at 35 to 52 mOhm, a peak under 82 V, 45 mJ at 1 uH, the loop inductance assumed). The 600 A external path and the external module are not known. This run searches neither a clamp part nor a module; it states the stage 2 requirement as FU-2A's figures give it",
  "source": "V270, V324; FU-2A/012-P2-R3.json 'ESC pack node clamp level', 'external pack switch module'",
  "reason": R},
 {"category": "R3", "function": "latch clock Schmitt buffer (between the clear push button's RC debounce and the latch clock)",
  "requirement": "FU-2A's re-rank and its P4 datasheet verifier name SN74AUP1G17DBVR for it, a second placement beside the Q9 trigger-input I_off buffer (2 a board, need 10), but FU-2A's merge left the function without a part. Qualify it under its own function name by that part number",
  "source": "FU-2A/017-rerank-R3.json figure 'latch clock Schmitt buffer (P3's missed function)'; FU-2A/019-P4-datasheet-R3.json",
  "reason": R},
 {"category": "R3", "function": "onboard pack switch driver; external module driver output",
  "requirement": "The drivers take the latch output alone; no coprocessor signal reaches them (V53). A motor overcurrent or over-temperature opens the ESC pack switches through the coprocessor's failsafe latch and the panel's heartbeat: the latched coprocessor refuses the panel's control writes, the panel stops its heartbeat, and the monostable opens the switches, within the 500 ms stop deadline (V159, V160)",
  "source": "V53, V159, V160, V288; IOBoard.md, Not defeatable",
  "reason": R},
 {"category": "R3", "function": "inverter on an active-low output enable",
  "requirement": "R4's enable inverter, SN74LVC1G04DBVR, also drives the external CAN transceiver's STB from the enable node, so the TCAN1472VDRQ1 is in normal mode while the enable node is high and in standby when it falls (V326). The placement count across R3 and R4 takes a fifth SN74LVC1G04DBVR a board as the upper bound: need 25, gate 125",
  "source": "V326; FU-2A/003-P2-R2.json 'external port loop delay'; TCAN1472-Q1 datasheet, STB",
  "reason": R},
 {"category": "R3", "function": "monostable; timing network (C and R of the monostable)",
  "requirement": "The drift of k over 3.3 V +/-3 % and 0 to 50 degC is not known: no stocked 74HC423 states k against temperature. The window budget keeps +/-3 % as an assumption, and the corner sweep of Monostable.md step 11 tests the window at 0 and 50 degC (V36). The timing network's resistor and capacitor are stage 2",
  "source": "V36; FU-2A/012-P2-R3.json 'timing network'",
  "reason": R},
 {"category": "R3", "function": "every R3 function",
  "requirement": "FU-2A verified: 74HC423BQ,115 (monostable), 74AUP1G32GW,125 (OR gate), TPS3703A7330DSERQ1 (reset supervisor), 74LVC1G175GW,125 (latch), 434153017835 (latch clear control), SN74AUP1G14DBVR (trigger-path inverter), SN74AUP1G17DBVR (trigger input I_off buffer), BSC014N04LSATMA1 (servo rail gate), IPT015N10N5ATMA1 (onboard pack switch MOSFETs, pack reverse-polarity protection), TPSI3052DWZR (onboard pack switch driver, external module driver output, servo rail gate driver), CSD19537Q3T (pack precharge), SN74LVC1G04DBVR (inverter on an active-low output enable). Requalify them. FU-2P56 names IPT015N10N5ATMA1 and CSD19537Q3T on the ESC pack node, held under 85 V by the bus capacitance (V324). The timing network, the heartbeat-node resistors, links and pull-downs, and the test points (V333) are stage 2",
  "source": "selection.json R3 (run FU-2A); FU-2P56 conflicts; V324, V333",
  "reason": R},
 {"category": "R9", "function": "V+ OR element (new)",
  "requirement": "Feeds the ADBMS1818's V+ from each balance-lead tap that can be the pack top, so V+ stays at or above C18 - 0.3 V and C18 at or under V+ + 5.5 V for 1 to 16 cells up to 67.2 V, with no cell-count setting (V193, V327). The LTM8068EY#PBF's fixed 18 V misses the first above 18.0 V of pack and breaks the second above 23.2 V. Requirements (V327, FU-2D): the pack-side branch drops at most 0.3 V at 1.4 mA, the ADBMS1818's highest V+ current; the supply-side branch blocks up to 67.2 V less the supply voltage, and the pack-side branch blocks the supply into the pack; both rated with V188's 85 V in mind; with the board off, V+ draws 6.1 uA typical and 18 uA maximum from the pack top through it, a draw V190 does not count. State the placements a board and the stock gate at that count",
  "source": "FU-2D/003-P2-R9.json reports 'V+ feed from the highest tap', 'V+ OR element ratings', 'monitor with the board unpowered'; ADBMS1818 Rev. B Tables 3 and 11; V188, V190, V193, V327; FU-2P56 conflicts",
  "reason": R},
 {"category": "R9", "function": "every R9 function",
  "requirement": "FU-2D verified: ADBMS1818ASWAZ-RL (Cell monitor), ISO7741DWR (cell monitor isolation barrier), LTM8068EY#PBF (cell monitor pack-side supply). Requalify them. FU-2P56 names ADBMS1818ASWAZ-RL on the ESC pack node, held under 85 V by the bus capacitance (V324). The balance-lead input network, its connector and its connector protection, and the unused-input tie, are stage 2",
  "source": "selection.json R9 (run FU-2D); FU-2P56 conflicts; V324, V327",
  "reason": R},
]
d["items"].extend(hand)
json.dump(d, open(sys.argv[2], "w"), indent=1, ensure_ascii=False)
print(len(d["items"]))
