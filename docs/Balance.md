# Balancing

<sub>**English** · [Deutsch](Balance-de.md)</sub>

Setting up the balance screen, where the two sensors go, and which sensors
work.

A balance result is a magnitude and an angle. Both come from two sensors: a
vibration sensor and, optionally, an index sensor giving one pulse per
revolution. Two placement errors produce a wrong result with no visible sign on
screen: a vibration sensor on a compliant mount measures a filtered version of
the bearing's motion, and an index mark on a blade produces one pulse per blade
instead of one per revolution.

## Screen setup

![The rotor and where the mass goes](img/balance.png)

Set the blade count (2 to 6) and the rotor type. The screen converts the
measured angle into a blade reference ("between blades two and three") in
addition to the angle.

For a ducted fan the correction is given as an angle on the hub, because a
blade tip inside a duct is not accessible:

![A five-blade fan](img/balance-edf.png)

## Sensor placement on a test rig

![Where the sensors go on a rig](img/balance-rig.png)

- The index mark goes on the motor bell, not on a spinner. The bell rotates
  with the shaft, is rigid, is present with any propeller, and can be observed
  from below where nothing crosses the sensor's line of sight. A sensor aimed
  at a spinner from the front looks across the disc and counts blades.
- The mark is a pen line, not reflective tape. Anything attached to the bell is
  mass on the part being balanced.
- The vibration sensor goes on a rigid part as close to the bearing as
  possible. A compliant mount is a low-pass filter.

## Sensor placement on a complete aircraft

![Where the sensors go on an aircraft](img/balance-aircraft.png)

- The accelerometer goes flat on the firewall, not on the cowl. The cowl is a
  fairing, often rubber-mounted, and moves relative to the motor mount.
- A three-axis sensor mounted flat has two axes in the firewall's plane, across
  the shaft. Use one of those and ignore the third.
- The index mark goes on the bell. A spinner is removed for transport and
  refitted at a different angle, which invalidates the phase reference of the
  previous balance.
- Tie the aircraft down. A free-standing airframe is a spring in series with
  the measurement.

## Sensor selection

Without an index pulse there is no phase reference and balancing uses the
four-run method: a baseline run, then a trial mass at 0°, 120° and 240°. The
method does not measure phase, so a constant sensor delay does not affect it.
It needs bandwidth: at 10,000 rpm (revolutions per minute) the fundamental is
167 Hz, which an IMU (inertial measurement unit) streaming fused orientation at
100 Hz cannot resolve. Use an analogue sensor (piezo or analogue accelerometer)
into the coprocessor's ADC (analogue-to-digital converter).

With an index pulse the number of runs halves, and only the variation of the
sensor delay matters; a constant delay cancels because the trial run measures
the whole chain. An analogue sensor has a delay of about 15 µs, under 1° at
10,000 rpm. A fused IMU has about 5 ms of delay, neither constant nor
specified, which is 300° at 10,000 rpm.

## What the measurement needs

An accelerometer and, optionally, an index sensor on the coprocessor. Neither
is fitted, so the screen runs from the model and carries the MODELLED mark. The
setup, the blade arithmetic and both placement guides work without them.
