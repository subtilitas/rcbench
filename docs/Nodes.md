# The sensor nodes

<sub>**English** · [Deutsch](Nodes-de.md)</sub>

A sensor node is an ESP32-C6 board on its own LiPo cell that sends finished
readings over Bluetooth LE (Low Energy, BLE). It has no wire to the bench.
The power node reads the INA228 of a MATEK I2C-INA-BM on the ESC's
(electronic speed controller's) XT60 power path. The balance node reads an
accelerometer axis and an index sensor and sends a balance report every
250ms.

The power node is a measurement, not a protection. No limit or trip of the
bench reads it, as none reads the wired INA228. With INA228 SOURCE NODE a
run ends on the node's dropouts as on any measurement sensor's
([The panel](#the-panel)).

Waveshare, MATEK, GERUI, Texas Instruments, Analog Devices and Vishay are
names of their owners, and
Bluetooth is a trademark of its owner. rcbench is independent: it is not
affiliated with, endorsed by or sponsored by them. The names say which
parts the node is built from.
[Legal notice and disclaimer](../README.md#legal-notice-and-disclaimer) holds
the project's notice.

| Part | Where |
| --- | --- |
| the packets, the UUIDs (universally unique identifiers), the control writes, the advertising, the 20ms windows | `protocols/rcnode`, no pin driver |
| start-up, the once-a-second step, sleep and wake | `firmware/node/main/node_main.c` |
| the battery and sleep decisions, under the host suite | `firmware/node/main/node_policy.c` |
| the GATT (generic attribute profile) server and the advertising on NimBLE | `firmware/node/main/node_ble.c` |
| the power role: the INA228, the windows, the accumulators | `firmware/node/main/node_pwr.c` |
| the INA228 driver, the IOmcu's own | `shared/sense/ina228.c`, `shared/sense/sense_bus.c` |
| the balance role: the ADC stream, the index, the report, the battery | `firmware/node/main/node_bal.c` |
| the ADC frames, the scale, the index and the demodulator, under the host suite | `shared/vibe` |
| the power node's battery reading | `firmware/node/main/node_bat.c` |
| the console | `firmware/node/main/node_console.c` |
| the panel's central for the power node: scan, bind, INFO's check, the RUN marker, under the host suite | `shared/ui/pnode_ctl.c` |
| the panel's readings, totals, peaks and dropout rule, under the host suite | `shared/ui/pnode_meter.c` |
| SETUP > WIRELESS' POWER NODE entry | `shared/ui/wireless_pnode.c` |
| the panel's `blec` task client and the control task's glue | `firmware/panel/main/panel_pnode.c` |

## The power node's parts

| Part | Use |
| --- | --- |
| Waveshare ESP32-C6-Zero | ESP32-C6FH4 with 4MB flash in the package, USB-C (USB Serial/JTAG), LDO (low-dropout regulator) from the 5V pin to 3.3V: ME6217C33M5G on Waveshare's schematic, ME6217C33M6G on the seller's listing; the part on the delivered boards is not confirmed, WS2812 RGB LED on GPIO8, BOOT key on GPIO9, RESET key |
| GERUI CD42 charge and boost module | charges the cell from VIN (4.5V to 5V) and boosts it to 5V on OUT |
| a 1S LiPo cell | on the module's BAT |
| MATEK I2C-INA-BM | INA228, 200µΩ shunt, address 0x45, XT60 in and out |
| 2 × 200kΩ, 1 × 100nF | the battery divider |
| 2 × 2.2kΩ | I2C (Inter-Integrated Circuit) pull-ups to 3.3V |

The board's USB VBUS reaches its 5V pin through a B5819WS Schottky diode
(the board's schematic). With the module's OUT on the 5V pin, nothing feeds
back into a USB cable.

## Wiring

| From | To | Note |
| --- | --- | --- |
| cell + | CD42 BAT+ | |
| cell − | CD42 BAT− | |
| CD42 OUT 5V | ESP32-C6-Zero 5V | the board's LDO makes 3.3V |
| CD42 OUT GND | ESP32-C6-Zero GND | |
| CD42 BAT+ | 200kΩ to GPIO3 | the top of the divider |
| GPIO3 | 200kΩ to GND, 100nF to GND | the bottom of the divider; GPIO3 is ADC1 channel 3 |
| ESP32-C6-Zero GPIO19 | MATEK SDA | 2.2kΩ from SDA to 3V3 |
| ESP32-C6-Zero GPIO18 | MATEK SCL | 2.2kΩ from SCL to 3V3 |
| ESP32-C6-Zero 5V | MATEK 5V | the INA228's supply |
| ESP32-C6-Zero GND | MATEK G | the node's only ground |

The lead to the MATEK is 10cm or shorter. The bus runs at 400kHz. The
firmware switches the pads' own pull-ups off; the bus runs on the 2.2kΩ
pull-ups and the module's own.

The divider draws 10.5µA from the cell at 4.2V, also while the node is
asleep or off.

**Before the first connect.** The rail the MATEK's I2C pull-ups go to is not
known. Power the MATEK alone from 5V, leave SDA and SCL open, and measure
each against G: both must read 3.3V or less. A higher reading needs a level
shifter; the ESP32-C6's pins take 3.3V.

### The ground rule

The node's GND is the MATEK's G, and G is the pack's negative. The cell's
negative is the node's GND too. The node has no other wire: the measured
path is separate from the bench.

- No USB cable on the node while a pack is on the XT60. The cable's ground
  is then the pack's negative, and a laptop or the panel on the same USB tree
  closes a loop through the ESC's return.
- Charge the node with the XT60 unplugged, or from a power bank with nothing
  else on it.
- The node's case insulates the board from the shunt side of the module.

## The CD42 module

The GERUI "5V 2A Laderegler" carries a chip marked CD42 and a 4-LED charge
indicator (25%, 50%, 75%, 100%).

| Terminal | Use |
| --- | --- |
| VIN | 4.5V to 5V from a source of 2A to 3A, to charge |
| BAT | the 3.7V cell |
| OUT | 5V, boosted from the cell |
| KEY | a button |

Not known, and measured on the first module:

- the light-load auto-shutdown threshold: the load current under which the
  module switches OUT off;
- the module's quiescent current at the cell;
- whether KEY or a load starts the boost.

**What the sleep policy assumes.** The module keeps OUT at 5V while the node
sleeps. The sleep load on OUT is the ESP32-C6 in deep sleep (7µA, the chip's
figure), the board's LDO and WS2812 at idle (not measured), the MATEK's own
regulator if it has one (not known) and the INA228 shut down (5µA). If the
module switches OUT off under this load, the node goes dark at its first
sleep: the 10s timer never runs, the RESET key does nothing, and only what
starts the module's boost again (KEY, or a charger on VIN, not known which)
brings the node back.

**How to measure it.**

1. A meter in series in OUT: the current with the node awake, advertising,
   and connected.
2. On the console, `sleep`. A meter on OUT for 2min: OUT stays at 5V, or it
   drops.
3. The LA2016 (logic analyser) on SCL: a bus voltage read every 10s shows the
   node waking on its timer.
4. The threshold: OUT loaded with resistors, the node off OUT. The lowest
   current that holds OUT on for 5min.
5. The quiescent current: a meter in the cell's lead, OUT without a load.

If OUT switches off in sleep, the choices are a bleed resistor on OUT that
holds the load above the threshold (it costs the cell that current), or a
slide switch in the cell's lead and no sleep. The choice waits for the
figures.

## Sleep and wake

| Condition | What the node does |
| --- | --- |
| no link, no pack on the XT60 and no `stay on` for 120s | deep sleep with a 10s timer; the balance node without a timer |
| SLEEP written to CONTROL, or `sleep` on the console | the same, at once |
| a timer wake | the INA228 converts the bus voltage once (540µs), the node reads it and shuts the part down; 5.0V or more starts the node in full, less sleeps again |
| a cell between 1.0V and 3.3V, 3 readings in a row, counted across timer wakes | off: deep sleep without a timer |
| OFF written to CONTROL, or `off` on the console | off, at once |
| the RESET key, a power cycle, a flash | a full start |

The count of low readings is kept in RTC (real-time clock) memory through
deep sleep, so one low reading at a timer wake switches nothing off; a
good reading clears the count. A divider reading under 1.0V is no cell,
the board on USB alone, and switches nothing off. Under 3.5V STATUS
reports the battery low. A failed battery read (the ADC or its
calibration fails) sets STATUS flag 0x0040 with a cell voltage of 0, prints
`bat error` on the console, and neither counts towards the switch-off nor
clears the count. Before a
sleep the node ends the link, waits 200ms, and writes the INA228's
ADC_CONFIG to shutdown.

The BOOT key is GPIO9, which cannot wake the ESP32-C6 from deep sleep (only
GPIO0 to GPIO7 can). The RESET key is the wake button. The balance node has
no sensor that tells it to wake: asleep, it sleeps until the RESET key.

The balance node has no pack. It is idle with no link and no `stay on`; a
turning rotor or an index pulse does not hold it awake, because without a
link nobody takes its reports. After 120s idle it stops the ADC stream,
switches the TCRT5000 module's supply (GPIO20) off and sleeps until the
RESET key.

A timer wake loads the image without checking it again
(`CONFIG_BOOTLOADER_SKIP_VALIDATE_IN_DEEP_SLEEP`). The time a timer wake
takes and the node's mean current asleep are not measured. The plan's
estimate is 100µA: 30ms awake at 30mA every 10s, plus the current asleep.

## The INA228

The node writes the bench's own set-up (`INA228_ADC_BENCH`): bus and shunt
voltage 540µs each, temperature 50µs, averaging 1, a result every 1130µs.
ADCRANGE 1 (±40.96mV) for 204.8A on the 200µΩ shunt: 390.625µA a step.
The driver is the IOmcu's.

| Every | Read | Into |
| --- | --- | --- |
| 1130µs | CURRENT and VBUS | the 20ms window |
| 20ms | | a window packet: mean and highest current, mean and lowest voltage, the sample count, clip flags |
| 40ms | ADC_CONFIG, against the value written | a part found reset is set up again 1000ms on and counted |
| 500ms | DIAG_ALRT, then CHARGE and ENERGY (raw, 40 bits; the reads clear their overflow flags), DIETEMP | an accumulator packet, and the run extremes since the last RUN write |

A window holds 17 or 18 samples. A current sample at either end of the
20-bit range is a clip: counted and flagged, never a value. The node never
clears the accumulators. The part clears them at its own reset and at
every set-up, and the accumulator packet's epoch counts the set-ups. After
CHARGE and ENERGY the node reads ADC_CONFIG back once more. When it does
not match, the part reset before or between the reads: both values are
dropped, their flags in the packet are clear, and the part is set up again.
The panel treats a fall in ENERGY under the same epoch as a restart too
(`rcnode_acc_restarted()`). The packets are in
[protocols/rcnode](../protocols/rcnode/README.md).

**What a set-up again loses.** Every set-up writes RSTACC, so CHARGE and
ENERGY counted since the last accumulator packet the panel took are gone:

| Cause | Lost charge and energy |
| --- | --- |
| the part reset itself | from the last good accumulator packet to the set-up: up to 500ms to the next read, 40ms to the read-back that catches the reset, and 1000ms to the set-up, 1.54s at most |
| 3 failed reads in a row with no reset (the bus, not the part) | from the last good accumulator packet to the set-up: up to 500ms, the time the reads fail, and 1000ms |
| the part does not answer the set-up | the set-up is tried again every 1000ms, and each try adds 1000ms |

The window packets carry current and voltage through such a gap; the
panel can count the gap from them.

## The balance node

The balance node samples one accelerometer axis and an index sensor in one
ADC (analogue-to-digital converter) stream, demodulates the vibration
against the index on the node (shared/vibe), and sends a balance report
every 250ms. Its sensors and its cell are its own: it touches no pack, and
the ground rule of the power node does not apply to it. Where the sensors
go on a rig or an aircraft is in [Balancing](Balance.md).

### Parts

| Part | Use |
| --- | --- |
| Waveshare ESP32-C6-Zero | as the power node |
| GERUI CD42 module, a 1S LiPo cell, 2 × 200kΩ and 1 × 100nF | as the power node: power and the battery divider |
| ADXL335 or ADXL337 breakout (Analog Devices), 3 axes, ±3g | the vibration; one axis is used |
| 2 × 4.7nF ceramic capacitors, 0603 or the breakout's size | replace the X and Y filter capacitors |
| TCRT5000 module with an LM393 comparator (TCRT5000 by Vishay) | the index: one pulse per revolution from a line on the motor bell |

### Wiring

| From | To | Note |
| --- | --- | --- |
| cell, CD42, divider to GPIO3 | as the power node | the battery: GPIO3 is ADC1 channel 3 |
| ESP32-C6-Zero 3V3 | ADXL VCC | the accelerometer's supply, the board's LDO |
| ESP32-C6-Zero GND | ADXL GND | |
| ESP32-C6-Zero GPIO0 | ADXL X or Y | ADC1 channel 0: the axis in the mount's plane, across the shaft |
| ADXL Z | not connected | |
| ESP32-C6-Zero GPIO20 | TCRT5000 module VCC | 3.3V while the node runs, off before a sleep |
| ESP32-C6-Zero GND | TCRT5000 module GND | |
| ESP32-C6-Zero GPIO1 | TCRT5000 module D0 | ADC1 channel 1: the comparator's output |
| TCRT5000 module A0 | not connected | |

The sensor leads are twisted with their ground and 30cm or shorter.

**Pins.** GPIO0 and GPIO1 are ADC1 channels 0 and 1 and no strapping pins:
the ESP32-C6's strapping pins are GPIO4, GPIO5, GPIO8, GPIO9 and GPIO15
(its datasheet v1.6, table 3-1). GPIO0 and GPIO1 are also the pins of an
external 32kHz crystal. The ESP32-C6-Zero carries none (its schematic shows
the 40MHz crystal only), and the firmware runs the slow clock on the
internal RC oscillator (`CONFIG_RTC_CLK_SRC_INT_RC`). GPIO20 is on the
board's header, neither an ADC channel nor a strapping pin. The node
holds it low through deep sleep (`gpio_hold_en()`) and releases the hold at
start.

**The module's supply from GPIO20.** The TCRT5000's infrared LED is the
node's largest sensor load. A pin at drive strength 3 sources up to 40mA at
2.64V or more (datasheet v1.6, table 5-4). The node drives GPIO20 high at
start and low before a sleep, so the LED is dark while the node sleeps. The
module's current depends on its LED resistor and is not measured. The
module must not run from 5V: D0 then reaches 5V, and the ESP32-C6's pins
take 3.3V.

**The accelerometer on 3V3.** The ADXL's output is ratiometric: its
sensitivity and its zero-g level follow its supply. The board's 3V3 is the
cleanest supply on the node. A breakout with its own regulator (the GY-61
carries one) puts the ADXL on that regulator's output, which is not
measured. The ADXL draws 350µA, also while the node sleeps.

### The accelerometer's filter capacitors

The ADXL335 and ADXL337 set each axis's bandwidth with one capacitor from
the output pin to GND, against an internal 32kΩ ±15%: bandwidth =
1 / (2π × 32kΩ × C). Common breakouts fit 0.1µF on each axis: 50Hz. At
10,000rpm the 1x is 167Hz, and 50Hz passes 29% of it.

Change the X and Y capacitors to 4.7nF: 1,058Hz, 920Hz to 1,245Hz over the
resistor's tolerance. 4.7nF is the smallest value the datasheet
recommends; the X and Y axes reach 1,600Hz. The Z capacitor can stay.

The breakout documentation found names no positions for the capacitors.
On Analog Devices' EVAL-ADXL335Z, C2, C3 and C4 sit at XOUT, YOUT and
ZOUT. On another breakout, find each one with a meter: the capacitor with
one end on the X (or Y) pin of the header and the other on GND.

At 4.7nF the filter delays the signal by about 150µs and passes 98.8% of
167Hz, 9° late. Both are constant at one rpm, so they cancel in the
two-run and four-run methods. The node does not compensate them: the
magnitude it reports at 167Hz is 1.2% low, its phase 9° late.

### The TCRT5000 module

The module's comparator compares the phototransistor with the threshold its
potentiometer sets. Its D0 LED lights at one level of D0. Set the
potentiometer with the rotor turned by hand: D0 changes as the line passes
the sensor, and only there.

The node reads D0 in its ADC stream with thresholds of 1100mV and 2200mV.
It takes as the mark the level D0 spends less time at, weighed over each
250ms, so a dark line on a bright bell and a bright line on a dark bell
both work. The index edge is the start of the line: the edge that passes
the sensor first.

The TCRT5000's datasheet gives 2.5mm as its distance of the highest output.
The line's contrast on a motor bell and the module's edge are not measured.

### Placement

- The line on the motor bell goes in line with blade 1, as
  [Balancing](Balance.md) says.
- The TCRT5000 module faces the bell's outer surface, 2mm to 3mm from it.
- The ADXL goes on a rigid part close to the bearing, the axis on GPIO0 in
  the plane of the mount and across the shaft.
- The breakout is mounted with its ground isolated from the rig's metal
  (nylon screws or an insulating pad), so the node's ground has no path to
  the bench.

### What the node measures

| Stage | Figure |
| --- | --- |
| ADC | ADC1 in continuous mode, a pattern of 3 channels: GPIO0, GPIO1, GPIO3, 12dB attenuation, 12 bits |
| Rate | 30,000 conversions a second asked; ESP-IDF v6.1 sets 30,120, 10,040Hz a channel. The driver takes 611 to 83,333 a second, 27,777Hz a channel for 3; the ESP32-C6's datasheet names 100,000 |
| Index | in the stream, a third of a sample after the vibration channel. A sample is 100µs, 6° of a revolution at 10,000rpm; a report sums its revolutions, and ±1 sample of jitter reads 1.25° in a report at 10,000rpm in shared/vibe's test |
| Scale | the chip's calibration curve of channel 0 at counts 1000 and 3000; nominally 0 to 3300mV over 4096 counts, 805µV a count, 2.44mg a count at 330mV/g |
| Sensitivity | 330mV/g: the datasheets' 300mV/g at 3.0V (270 to 330mV/g), scaled to 3.3V |
| Sensor in range | the DC of the axis between 1000mV and 2300mV sets `RCNODE_BF_DC_OK`: 1650mV ± 165mV of zero-g tolerance and ± 330mV of gravity on a tilted axis, with 150mV to spare |
| Report | every 2,510 samples, 250ms: rpm, period spread, the 1x magnitude and phase, the 2x and blade-pass magnitudes, rms, quality, revolutions, flags (`RCNODE_T_BAL_REPORT`, [protocols/rcnode](../protocols/rcnode/README.md)) |

The battery is the third channel of the stream: a one-shot read of ADC1
fails while continuous mode holds the unit. STATUS takes the mean of a
second's battery samples. While the stream does not run (it did not start,
or it could not start again), the node reads the battery one-shot, 16
samples averaged, as the power node does.

A report's `t_node` is the time its window's last vibration sample was
converted. The node dates it from the newest frame the driver finished,
so a backlog of frames in the driver's pool does not make it late.

The phase counts in degrees from the index edge, in the order points pass
the index sensor: against the direction of rotation, as the BALANCE screen
counts. The edge is the start of the line, half the line's width before
its middle: 5.7° for a 3mm line on a 30mm bell. The correction of the
two-run method is free of this offset, because the trial run measures it
too.

**Control.** BLADES sets the blade count for the blade-pass (2 until the
first write). RPM with a value of 1 to 60000 switches to the no-index mode:
the node demodulates at that rpm, the phase reads 0 and the report carries
`RCNODE_BF_NO_INDEX`. RPM 0 returns to the index. RUN is taken and changes
nothing on the balance node.

**STATUS.** The sensor state is `RCNODE_SENSOR_UNPROBED` (1) before the
first report, `RCNODE_SENSOR_ONLINE` (5) while the last report's DC was in
range, `RCNODE_SENSOR_ABSENT` (2) before it ever was,
`RCNODE_SENSOR_OFFLINE` (6) after it was, and `RCNODE_SENSOR_REFUSED` (4)
when the ADC stream did not start. `errors` counts two events, and
`resets` the restarts they cause; each loses the open report:

- a conversion out of the pattern's order: the demodulator starts again
  there;
- a full driver pool. The node fell more than 8 frames (80ms) behind, and
  the driver dropped the frames it held. Lost frames are whole rounds of
  the pattern, so the order does not show them. The node counts the
  driver's overflows, drops a read that one came before or during, stops
  the stream, empties the pool and starts the stream and the demodulator
  again. The samples up to the restart are lost, about 80ms or more.

**Calibration.** Nothing on the node is calibrated against a known
vibration. The ADC's error after calibration is ±40mV at 12dB (datasheet
v1.6, table 5-6), its effective bits at 10kHz are not measured, and the
ADXL's sensitivity is ±10% and follows its supply. The absolute g value
carries these; the two-run and four-run corrections use ratios and do not.
A shaker or a known imbalance on the owner's rig measures the scale.

## Bluetooth LE

The node advertises as `rcbench-pwr-XXXX` or `rcbench-bal-XXXX`, the last two bytes of its address
in hex, every 100ms while no link stands. The advertising carries the node
service's UUID and the manufacturer data with the role; the scan response
carries the name. One central at a
time, no pairing, no bonding. The node asks for an ATT (attribute protocol)
MTU (maximum transmission unit) of 247 and for no connection parameters of
its own.

| Characteristic | Properties | Content |
| --- | --- | --- |
| STREAM | NOTIFY | power: windows every 20ms, accumulators and run extremes every 500ms; balance: a report every 250ms; both: the STATUS packet every 1000ms |
| STATUS | READ, NOTIFY | battery, flags, firmware version, counters, every 1000ms |
| CONTROL | WRITE, WRITE WITHOUT RESPONSE | RUN with a tag of 1 to 255, SLEEP, OFF; BLADES (1 to 12) and RPM on the balance node |
| INFO | READ | protocol, role, board, address, the INA228's set-up (0 on the balance node) |

A write the role does not take, or of another length, is refused with ATT
error 0x13 or 0x0D.

STREAM packets wait in a queue of 32 for the sender. A full queue, a stack
without a buffer, or a packet longer than the link's MTU less 3 bytes
drops the packet and counts it in STATUS's `dropped`. The stack would cut
a longer notification without an error; the power node's packets are 20
bytes, which fit the smallest MTU of 23, and the balance report is 24
bytes, which needs 27.

## The panel

The panel connects to one power node as the third client of its Bluetooth
LE hub, beside the IMU and the Dial (owner 2 of `protocols/bleuart`'s hub).
SETUP > WIRELESS > POWER NODE holds its rows
([Screens](Screens.md#wireless-the-power-node)). The first Power node ON
starts Bluetooth LE until the panel restarts. The balance node is the
fourth client, owner 3
([Balancing](Balance.md#measuring-with-the-balance-node)).

- **Scan and bind.** SCAN FOR NODE scans 10s and lists the devices whose
  advertising names the node service and the power role in its
  manufacturer data (`rcnode_adv_match()`, the name only where no
  manufacturer data came). A balance node is not listed, named or not. A tap binds one by its address, kept in SETUP
  (`pnode_ble`, `pnode_name`). A bound node is connected again after each
  drop without a scan, 1000ms after the drop and on the hub's backoff
  after a failed connect. A scan while ARMED is refused, and a scan that
  runs is stopped at the switch to ARMED.
- **The link.** The panel subscribes to STREAM and reads INFO once after
  each subscribe. A node whose INFO names another protocol than 1, another
  role than power, or an INA228 set-up that `ina228_calibrate()` does not
  give for that shunt at that range's full scale sends nothing the panel
  takes. The central subscribes to one characteristic, so the node sends
  its STATUS packet on STREAM as well; the battery row reads it.
- **The RUN marker.** At each switch to ARMED the panel writes RUN with the
  run's tag, 1 to 255, to CONTROL. A link that is down then gets the write
  when it comes back within the run. A link that comes back within the run
  to a node whose first RUN packet carries another tag, a node that started
  again, gets the write again.

INA228 SOURCE (WIRED, NODE; WIRED by default) says whose INA228 feeds MOTOR
& ESC. It changes only while SAFE: while ARMED its row has no keys and
`settings_set()` and RESET leave it (`settings_safe_only()`). With NODE,
after each read of the IOmcu's BENCH page:

| Figure | From the node |
| --- | --- |
| voltage, current, power | the mean of the windows with the INA228 online that came since the last read, every 50ms; the last window's when none came; none once the last is older than 300ms |
| a clipped current | a window whose every current sample sat at an end reads that end, the full scale at INFO's ADCRANGE and shunt (204.8A for the MATEK), marked clipped; a window with some clipped samples reads the mean of the rest, marked clipped |
| lowest voltage, highest current, highest power | since the switch to ARMED: the windows' extremes and the node's RUN extremes under the run's tag, which hold the windows the radio lost |
| charge, energy | differences of the 40-bit accumulators with CHARGE_OK and ENERGY_OK, from the last packet before the switch to ARMED, up to 500ms before it; an epoch change or a fall in ENERGY adds the new counts from 0 (`rcnode_acc_restarted()`); the steps become µC and mJ with `ina228_charge_uc()` and `ina228_energy_mj()` |
| the ESC's telemetry | BENCH's own voltage and current, which are the ESC's while the IOmcu's INA228 is not the source, move to the ESC's figures |

A run without an accumulator packet in the 500ms before the switch to
ARMED has no node totals: the panel counts charge and energy from the
readings. Totals older than 1500ms lose their mark, and the panel counts
from the readings while they are absent.

**The switch to ARMED.** With NODE, an ARM of MOTOR & ESC or ESC STICK,
whose runs read BENCH, is refused while POWER NODE is OFF or no reading
came in the last 100ms. The band reads `Cannot switch to ARMED: no power
node readings. Set INA228 source WIRED or connect the node.` The refusal
sits with the panel's other arm refusals (`drain_commands()`), so every
path that arms the throttle meets it. SERVO's and OUTPUTS' arms read no
INA228 and are not refused for the node.

**Dropouts.** A run counts on the node when it reads BENCH, MOTOR & ESC or
ESC STICK, with NODE. A SERVO or OUTPUTS run counts none. In such a run, no
reading for more than 100ms opens a dropout dated from the last reading,
not before the run's start, and the next reading closes it. The rule is
the one of every measurement sensor (`servo_gap.h`): a dropout of up to
300ms rides, once in 10s; a longer one, or a second one that starts within
10s of the first's start, is an error: SAFE and every output off, with
`POWER NODE READINGS LOST WHILE ARMED: SWITCHED TO SAFE, ALL OUTPUTS OFF`
in the band. While WIRED the node's link ends nothing.

The dropout times are the `blec` task's processing times: a window counts
when the task takes its notification, every 5ms, not when the node closed
it. The delay from the node's window to the task is not measured.

The panel's console prints the link's changes and, once a second while
linked, a line tagged `node`:

```
I (52120) node: 50 windows in 1000ms, 2214 readings, 55 packets, lost 0 bad 0 unread 0, restarts 0, cell 3920mV, runs 1, 0 events dropped
```

The bench log names the source: `source` reads `node` on a row whose INA228
figures are the node's, and `ina voltage` and `ina current` hold them
([Screens](Screens.md#table-and-what-the-esc-says)).

## Update over Bluetooth LE

A node takes a new application image over BLE from a central, through the
update service of `protocols/bleota`
([README](https://github.com/subtilitas/rcbench/blob/main/protocols/bleota/README.md)).
The panel carries both roles' images, checks them at start and sends them
from UPDATES' wireless devices' page: POWER NODE FIRMWARE and BALANCE NODE
FIRMWARE ([Screens: The sensor nodes](Screens.md#the-sensor-nodes)). The
row's 2s hold ends the node's own link (`pnode_ctl_hold()`,
`bal_ctl_hold()`); that end and the link's return count as no loss, so the
band shows no `POWER NODE LOST`. After the update the node's client
connects again from SETUP's values, and the row reads the node's version
from its first STATUS packet on the new link. A balance node that
installed is a restart for the balance runs: INFO's clock finds it and
the runs clear ([Balancing](Balance.md#measuring-with-the-balance-node));
an update without an install keeps them.

1. The central connects and sends BEGIN: the kind (power node or balance
   node), the size, the version and the image's SHA-256.
2. The node checks the kind against its role, the size against its slot of
   1,572,864 bytes (0x180000) and the version. A power node refuses a
   balance image and the reverse. Nothing is erased yet.
3. The node waits 30s for its BOOT key (GPIO9) held 1s. The node shows
   nothing; the WS2812 is not driven, and the central tells the user to
   press BOOT. No hold in 30s refuses. A key already down when the window
   opens counts only after it is released.
4. Held: the node takes the image's first 296 bytes and checks that they
   name the ESP32-C6 and the node's own role (`DEVOTA_ROLE`, after
   ESP-IDF's application descriptor). An image of the other role or
   another device is refused before anything is written. The node then
   erases and writes the slot it does not run from.
5. END: the node compares the SHA-256 of every byte it wrote with BEGIN's,
   lets ESP-IDF check the image, sets the boot slot and restarts 500ms
   later.

A refusal, a lost link, 10s without a write or a SHA-256 that differs ends
the transfer: the node closes the slot and keeps running its image. A new
transfer starts from byte 0. The exception is the install after END: the
boot slot may already be set when the link drops or an ABORT comes. The
node then finishes the install and restarts into the new image if it
passed ESP-IDF's check.

While a transfer or an install runs, the node does not sleep, refuses
SLEEP and OFF from CONTROL and the console, and switches itself off only
for a cell under 3.0V (`NODE_UPD_OFF_MV`), 3 readings in a row, in place of
3.3V. A cell that empty ends the update with the node; the image it ran
stays.

The new image starts on trial (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`) and
marks itself valid at its first connection, which it takes only with its
BLE stack running.

| Start of an image on trial | Result |
| --- | --- |
| a connection | kept |
| a timer wake from deep sleep | the same image, still on trial: the wake loads the image the start before it chose and does not read the trial state (`CONFIG_BOOTLOADER_SKIP_VALIDATE_IN_DEEP_SLEEP`) |
| the RESET key, a power cycle, a panic, before a connection | the image before it |

So a power node that sleeps before its first connection stays on trial
across its 10s wakes, and a full start then goes back to the old image.
The balance node sleeps without a timer and wakes only on RESET, a full
start.

The trial and the rollback are the bootloader's: a node flashed over USB
with an image older than this update firmware has a bootloader without
them. It takes updates over the air, but with no trial and no rollback,
until it is flashed once over USB with a release that carries the
update.

Security: no pairing, no encryption and no signature. Integrity rests on
the SHA-256 in BEGIN. Any central in range can send BEGIN; the held BOOT
key is what accepts it ([SECURITY.md](../SECURITY.md)).

The device side runs on the task `ota`, priority 3, every 10ms, with a
stack of 8,192 bytes from the heap at start, the size of ESP-IDF's OTA
examples; its depth is not measured. Its static RAM is 3,623 bytes:
the window's ring of 2,048 bytes, a write buffer of 1,024 bytes and the
state. The power node's image is 646,240 bytes, 41.1% of a slot; the
balance node's 642,672 bytes, 40.9%.

## Building and flashing

The role is chosen at build time (`main/Kconfig.projbuild`). A board is
wired for one sensor, so the role is part of the image, not a stored
setting that a full flash erase loses.

```sh
. ~/esp/esp-idf-v6.1/export.sh
cd firmware/node
idf.py -B build-pwr -D SDKCONFIG=build-pwr/sdkconfig build
idf.py -B build-pwr merge-bin -o rcbench-node-pwr-merged.bin
```

The balance node:

```sh
idf.py -B build-bal -D SDKCONFIG=build-bal/sdkconfig \
  -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.balance" build
idf.py -B build-bal merge-bin -o rcbench-node-bal-merged.bin
```

A release carries, for each role (`pwr`, `bal`),
`rcbench-node-<role>-<version>.bin`, the application, and
`rcbench-node-<role>-merged-<version>.bin`, which goes to 0x0. The CI
(continuous integration) job "Node image" builds both roles.

To flash, take the pack off the XT60 first (the ground rule), then connect
the node's USB-C:

```sh
python -m esptool --chip esp32c6 -p /dev/ttyACM0 -b 460800 \
  write-flash 0x0 rcbench-node-pwr-merged-<version>.bin
```

The balance node takes `rcbench-node-bal-merged-<version>.bin` the same
way.

A node asleep does not answer on USB. Press RESET to start it, or hold BOOT
and press RESET for the ROM's download mode.

## The console

USB Serial/JTAG on the board's USB-C. A line every second:

```text
I (5123) node: pwr 50 win/s, n 18, 22.18V 14.300A, part online, epoch 1; link up -58dBm, sub 1, bat 3.92V, errors 0, resets 0, dropped 0
```

| Field | Meaning |
| --- | --- |
| `win/s` | windows closed in the last second, 50 when every window has a sample |
| `n` | samples in the last window |
| the volts and amperes | the last window's means |
| `part` | `online`, or `not answering` |
| `epoch` | set-ups of the INA228 since the start |
| `link` | `none`, `up` and the RSSI (received signal strength indication) |
| `sub` | 1 when STREAM is subscribed |
| `bat` | the cell; under 1.00V without a cell; `error` when the read failed |
| `errors`, `resets`, `dropped` | STATUS's counters |

The balance node's line:

```text
I (5123) node: bal 4 rep/s, adc 10040/s, 8420rpm 0.3%, 1x 412mg 265.00deg q91%, 2x 38mg, blade 120mg (3), rms 530mg, revs 35, dc 1652mV, flags 0x01c7, mark high; link up -58dBm, sub 1, bat 3.92V, errors 0, resets 0, dropped 0
```

| Field | Meaning |
| --- | --- |
| `rep/s` | reports in the last second, 4 |
| `adc` | rounds of the pattern in the last second: the rate a channel gets |
| the rpm and the percentage | the last report's rpm and period spread |
| `1x`, `q` | the 1x magnitude and phase, the quality |
| `2x`, `blade`, `rms` | the 2x and blade-pass magnitudes (the blade count in brackets), the rms |
| `revs` | revolutions in the last report |
| `dc` | the axis's DC: 1650mV with the ADXL at rest on 3.3V |
| `flags` | the report's flags, `VIBE_F_*` (shared/vibe) |
| `mark` | `high` or `low`: the level of D0 the node takes as the line |
| `nominal scale` | shown when the chip has no ADC calibration and the nominal curve stands in |

At start the node prints the rate, the counts of 1g and the DC window:
`bal: ADC 10040Hz a channel, 1g = 409 counts, DC 1241 to 2855, calibrated`.

| Command | Does |
| --- | --- |
| `status` | the name, the firmware, the role and the last status in full |
| `stay on`, `stay off` | holds the node awake, or lets it sleep again; until a restart |
| `sleep` | deep sleep at the next second, with the 10s check on the power node |
| `off` | off, at the next second |

The start prints `rcbench node <version>, role pwr, <n> timer wakes before
this start`.

## Current limitations

- The panel connects to the power node and to the balance node (SETUP >
  WIRELESS, [Balancing](Balance.md#measuring-with-the-balance-node)).
- The panel shows no signal strength of a linked node. STATUS carries
  none; the found list shows the scan's dBm.
- The node's readings reach MOTOR & ESC only while the IOmcu link is up:
  with the link down, the panel's model runs.
- The latency from a node's window to the panel's read is not measured.
- No node has run on hardware: the boards are ordered, not delivered. The
  pins, the wake, the currents and the CD42's behaviour are not measured.
- The CD42's light-load auto-shutdown threshold is not known (above).
- The balance node's rate, scale, noise and index timing are not measured:
  the figures above are the datasheets' and ESP-IDF's. The station can
  check the index with a pulse from the Pi on GPIO1 and the LA2016; a
  signal on the vibration input needs a generator the station does not
  have.
- The balance node does not compensate the ADXL's filter: at 167Hz its
  magnitude reads 1.2% low and its phase 9° late (above).
- A lost conversion loses the open balance report; a full driver pool loses
  it and at least 80ms of samples (`errors`, `resets` in STATUS).
- The balance node holds its ADXL on 3V3 while it sleeps: 350µA.
- The WS2812 is not driven. Its current at idle is not measured.
- The panel does not yet send a node its image: the node's side of the
  update over BLE exists, the panel's rows come with its node clients. The
  update has not run on a node; its rate and a rollback are not measured.
- A node checks an image's kind, chip and segments, not its origin: a
  central in range can offer any image of the node's role, older ones
  included, and a 1s hold of BOOT installs it.
- Windows without a sample between two windows are not sent: a stalled
  sampler shows as a jump in `t_node` with no jump in `seq`.
