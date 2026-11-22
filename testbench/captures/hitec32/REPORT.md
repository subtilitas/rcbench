# hitec32: Hitec D485HW register 0x32 read on GPIO 13, setups 1, 2 and 3

Plan from RCBench 261122 (rcbench-b4). Taken 2026-11-22 on the rcbench station (Raspberry Pi 5): setups 1 and 3 at 18:43-18:56 CET, setup 2 at 19:58-20:04 CET.

## Rig

| Item | Value |
| --- | --- |
| Panel | P4 C1:09, firmware 0.23.18 (SETUP > UPDATES, CURRENT) |
| IOmcu | Waveshare RP2350-CAN on the Osoyoo carrier, USB serial DB7C6B3C4B5A1331, firmware 0.23.18 (SETUP > UPDATES), slot B |
| C6 radio | 2.13.0 |
| Servo | Hitec D485HW, D series, PROG PORT 1 GPIO 13. Panel: "D series servo, model 34485, ID 0" |
| Supply | PD mini, HITEC CONNECT sets 5.0 V and 1000 mA. SUPPLY set points 5.00 V / 2.00 A |
| Series resistor at the pad | none recorded |
| Pull-up on the signal wire | setups 1 and 3: none fitted (Julian, via RCBench 261122). Setup 2: 2.2 kOhm from the signal wire to servo + (Julian: "2.2k is fitted", 19:5x) |
| Analyser | Kingst LA2016, /opt/sigrok/bin/sigrok-cli (libsigrok 0.6.0-git-0bc24877) |
| Channels | CH11 = GPIO 13, CH0 = servo supply + |
| Sample rate | 200 MHz (metadata "samplerate=200 MHz"). The rate itself is not checked against a reference |
| Threshold | preset 1.4 V (`voltage_threshold=1.4-1.4`) |

The sigrok-cli arguments are those of `testbench/host/capture.sh` at main 69fe42cdb, run directly. The script's flat-channel check was not run: it prints one line per sample and channel (2 x 10^9 here). Edges were checked by VCD export.

## Before the setups

- An RTS pulse on the panel's USB-JTAG port (18:12:58) attached the touch injector. Julian then reset the bench (devices back 18:20).
- FAULT 01 (IOmcu failsafe) stayed latched. SUPPLY OUTPUT ON was refused: "Supply refused to switch on: IOmcu failsafe latched. Hold ARM, then DISARM."
- Cleared at 18:42:25 by ARM on SERVO > LIVE and an immediate DISARM, as Julian approved. During the ARM the PD mini was on 2.886 s (CH0) at 5.00 V, 0.029 A, with 131 PWM pulses at 50 Hz on CH11 (`armclear.sr`). After it: SAFE, FAULT 01 gone, rail 0.06 V.

## CH11 idle level with the supply on

`idle-tester-on-1.4v.sr`: 2.0 s at 10 MHz, 18:54. Port in tester mode after LEAVE, PD mini on at 5.00 V, SAFE.

- CH0 high for the whole capture.
- CH11 toggles for the first 0.81 ms: high fraction 0.59, low segments 0.30 us median.
- CH11 then stays low for the remaining 2.015 s.

The idle level is below 1.4 V. Not measured: the idle voltage.

## What CH11 shows of a D series read without a pull-up

- **Request:** the D series line is open drain (`protocols/hitec/README.md`, Line levels). A 0 bit and the start bit are released, and a released line with no pull-up stays low. So the master's request never crosses 1.4 V and is not in any capture of these setups. The request bytes below come from the protocol, not from the capture.
- **Answer, PRECHARGE:** the answer shows only through the master's precharge pulses: 100 ns driven high every 1.0865 us (measured period), at 7.99 samples a bit.
  - Line up (released): each pulse reads 0.26-0.29 us above 1.4 V.
  - Servo pulling low: a pulse reads 0.00-0.02 us, or does not appear.
  - Decoder `~/la/hitec32/hspk.py` reads one sample a pulse (above 0.15 us = high), takes a 3-sample majority at each bit centre, and decodes inverted 8N1 at 115 200 bit/s.
- **Answer, PULL-UP:** no precharge and no pull-up, so CH11 has 0 edges.

## Setup 1: PRECHARGE, no pull-up

Hitec D receive was already PRECHARGE at the start, not PULL-UP as recorded on 2026-10-21. Panel after CONNECT: "Port 1 in programming mode. PWM off. 4.99V, 23mA", SERVO D485HW.

| Read | File | Length | Answers with a good sum |
| --- | --- | --- | --- |
| 1, CONNECT | `s1-precharge-1.4v-20261122T174325Z.sr` (.vcd beside it) | 10.0 s | 22 of 22 receive windows (plus one opening window at 4307.8 ms with no answer) |
| 2, READ ALL | `s1-precharge-r2-1.4v-20261122T174721Z.sr` | 5.0 s | 21 of 21 |
| 3, READ ALL | `s1r3-1.4v-20261122T174817Z.sr` | 5.0 s | 21 of 21 |

Register 0x32, all three reads:

| Read | Receive window opens (capture ms) | Answer start after it | Answer end (capture ms) | Answer bytes |
| --- | --- | --- | --- | --- |
| 1 | 5885.3251 | 6.205 ms | 5892.2242 | `69 00 32 02 00 00 34 00` |
| 2 | 2604.7207 | 6.392 ms | 2611.8068 | `69 00 32 02 00 00 34 00` |
| 3 | 2622.6003 | 6.198 ms | 2629.4920 | `69 00 32 02 00 00 34 00` |

- Answer: header 0x69, ID 0x00, address 0x32, 0x02, value 0x0000 (lo 00, hi 00), sum 0x34 (= 00+32+02+00+00, correct), then the trailing 0x00.
- Request, from the protocol, not seen: `96 00 32 00 32` (sum = 00+32+00).
- Panel result: the session read the servo (D485HW, ID 0). The ID 0x32 value 0 matches "ID 0".

Kept window, read 1: `win/s1-0x32-win.sr` and `win/s1-0x32-win.csv` (`sigrok-cli -O csv`, 200 MHz, 1 890 060 rows, columns CH0, CH11).

- Window 5883.7739 to 5893.2242 ms of read 1, 9.450 ms long.
- The request start in it is estimated, not seen: window open minus 0.434 ms (5 bytes at 86.8 us) minus 0.117 ms (the RP2350 lead, README). That puts the request at 5884.774 ms, 1.000 ms into the window.
- In window time: the receive window opens at 1.551 ms, the answer starts at 7.756 ms and ends at 8.450 ms, and the window ends 1.000 ms after that.
- The answer starts 6.205 ms after the receive window opens. Across all reads it starts 5.76 to 6.44 ms after the window opens.
- Not measured: the time from the request's last stop bit to the window open. The README gives 5.9-6.5 ms from the request end, so the window opens close to it.

## Setup 3: PULL-UP, no pull-up

`s3-pullup-nopullup-1.4v-20261122T175456Z.sr`, 10.0 s.

- Panel: "No D series servo answers. Check plug, receive mode and series." and "Port 1 is held. The servo is not in programming mode". SERVO NOT READ.
- CH11: 0 edges in 10.0 s.
- CH0, the session's supply cycle: on until 4385.1 ms, off 4385.1-4705.0 ms, on 4705.0-6030 ms, off from 6042.3 ms on. The ON edges chatter for several ms.
- No read of 0x32 took place: the session failed before the register reads. No request or answer is visible.
- Window kept for comparison: `win/s3-win.sr` and `.csv`, 5590.0-5599.4503 ms, the same length as setup 1's window. It is placed where setup 1's first read opened (886 ms after power-on). CH11 is 0 in every sample.

## Setup 2: PULL-UP, 2.2 kOhm fitted

### CH11 idle level

- SUPPLY page, 5.00 V / 2.00 A, OUTPUT ON, 19:58:59 (`s2-idle-supply-on-1.4v.sr`, 10 MHz, 9.0 s). The panel read 5.00 V, 0.03 A, CV.
- CH0 rises at 4.206 s and stays high to the end.
- CH11 stays low for all 9.0 s, 0 edges. In tester mode on the SUPPLY page the pad holds the line low against the pull-up; the pad state is inferred, not measured.
- In the HITEC session (port open, input with pull-up) CH11 idles **high** between transactions: about 96 ms high between one read's release and the next lead.

### Reads

Hitec D receive set to PULL-UP (it was PRECHARGE). Panel after CONNECT: "Port 1 in programming mode. PWM off. 4.99V, 24mA", "D series servo, model 34485, ID 0". Band SAFE PROG 1.

Decoder `~/la/hitec32/hdec.py`: inverted 8N1 at 115 200 bit/s, start bit high, a 1 bit low. With the pull-up the request and the answer are plain levels. The decoder lists one extra byte after each request (00, 80, F8, FC or C0): that is the release of the line to the pull-up after the last stop bit, not a byte on the wire.

| Read | File | Length | Answers with a good sum |
| --- | --- | --- | --- |
| 1, CONNECT | `s2-pullup-2k2-1.4v-20261122T190003Z.sr` (.vcd beside it) | 10.0 s | 22 of 22 |
| 2, READ ALL | `s2-pullup-2k2-r2-1.4v-20261122T190126Z.sr` | 5.0 s | 21 of 21 |
| 3, READ ALL | `s2-pullup-2k2-r3-1.4v-20261122T190158Z.sr` | 5.0 s | 21 of 21 |

Register 0x32:

| Read | Request start bit (capture ms) | Request bytes | Answer start (capture ms) | Answer bytes |
| --- | --- | --- | --- | --- |
| 1 | 5871.5169 | `96 00 32 00 32` | 5878.3093 | `69 00 32 02 00 00 34 00` |
| 2 | 2644.9090 | `96 00 32 00 32` | 2651.7742 | `69 00 32 02 00 00 34 00` |
| 3 | 2612.5984 | `96 00 32 00 32` | 2619.3746 | `69 00 32 02 00 00 34 00` |

Read 1, on the wire:

- Lead: driven low 117.27 us from 5871.3997 ms.
- Request: 5 bytes back to back from 5871.5169 ms. The last stop bit ends at 5871.9509 ms (5 x 86.81 us).
- Answer: starts 6.358 ms after the request's last stop bit. Reads 2 and 3: 6.431 and 6.342 ms.
- Answer bytes: 8 back to back, the last one 0x00, ending at 5879.0037 ms.
- After the answer the line stays low until 5879.1004 ms, 107 us after the trailing 0x00's start-and-data bits end. Then it is released high.

Kept window, read 1: `win/s2-0x32-win.sr`, `win/s2-0x32-win.csv` (`sigrok-cli -O csv`, 200 MHz, 1 920 800 rows, columns CH0, CH11), `win/s2-0x32-win.vcd`.

- Capture 5870.3997 to 5880.0037 ms, 9.604 ms long: 1.000 ms before the lead to 1.000 ms after the end of the answer's last byte.
- In window time:
  - lead 1.000 ms;
  - request start bit 1.117 ms;
  - request end 1.551 ms;
  - answer 7.910-8.604 ms;
  - release 8.701 ms.
- Re-decoding the cut file gives `96 00 32 00 32` and `69 00 32 02 00 00 34 00`.

End of setup 2: LEAVE; the set-point question answered NO; Hitec D receive set back to PRECHARGE. Leaving the HITEC page switched the PD mini off: SUPPLY read MODE OFF, 0.00 V.

## Extra, outside the plan

`s1x06-0.6v-20261122T174850Z.sr`: READ ALL at the 0.6 V preset, to see whether the request shows below 1.4 V.

- sigrok-cli hit the 180 s timeout (rc 124). The file holds 0.86 s with 22.4 x 10^6 CH11 edges: the line crosses 0.6 V continuously.
- Not decoded and not used. Whether the request is readable at 0.6, 0.9 or 1.2 V is not checked.

## End state (camera 20:03:51, after setup 2)

- HOME, band LINK / INJECT / SAFE, no FAULT.
- The 2.2 kOhm pull-up is still fitted (Julian's hardware; not removed by the station).
- Supply off: INA 0x40 CH1 0.00 V. SUPPLY set points 5.00 V / 2.00 A.
- HITEC session left (LEAVE), Hitec D receive PRECHARGE (restored to its value at the start).
- Screen rotation NORMAL, theme DARK.

## Not covered

- The 200 MHz time base is not checked against a reference. A 6 s capture read 6.0458 s long.
- Line voltages are not measured: the analyser shows only above or below 1.4 V.
- The request is not visible in any capture at 1.4 V without a pull-up (setups 1 and 3). With the 2.2 kOhm it is (setup 2).
- Edge times with the pull-up depend on the RC rise through 2.2 kOhm; the rise time is not measured.
- Only one servo, one supply voltage (5.0 V), one temperature (room, not measured).
