# vibe: the balance measurement

Pure C, no SDK (software development kit). Two halves:

- The balance node's signal processing, for an ESP32-C6: its ADC
  (analogue-to-digital converter) frames split into channels and the scale
  from the ADC's calibration (`vibe_frame`), the index pulse found in its
  channel with the mark's polarity (`vibe_edge`), and the vibration channel
  demodulated against it (`vibe_demod`). Integer only.
- The panel's corrections from measured runs (`vibe_solve`): two runs with
  an index, four runs without one. Single-precision float.

The balance node's image (`firmware/node`, `node_bal.c`) compiles
`vibe_frame`, `vibe_edge` and `vibe_demod`. The host suite holds the module:
`test_vibe` (28 cases), `test_vibe_frame` (18 cases) and `test_vibe_solve`
(10 cases), on synthetic signals from `test/host/vibe_synth.h`. The report
is a plain C struct (`vibe_report_t`); the radio packet that carries it is
`RCNODE_T_BAL_REPORT` in `protocols/rcnode`.

## Signal path

| Stage | Part | Figure |
| --- | --- | --- |
| Vibration | ADXL335 or ADXL337 breakout, X and Y filter capacitors changed from 0.1µF to 4.7nF | bandwidth 1,060Hz; 330mV/g at 3.3V; mid-rail 1.65V |
| Index | TCRT5000 module with LM393 comparator, its digital output D0 into the ADC | one mark per revolution on the motor bell |
| ADC | ESP32-C6 ADC1, continuous mode, 3 channels: vibration, index, battery | 10,040Hz a channel, 30,120 conversions a second; the driver's limit is 611 to 83,333 a second (`SOC_ADC_SAMPLE_FREQ_THRES_LOW/HIGH`, ESP-IDF v6.1) |
| Scale | 12-bit, 12dB attenuation, 0 to 3.3V (ESP32-C6 datasheet v1.6, table 5-6) | the chip's calibration curve at counts 1000 and 3000 (`vibe_cfg_scale()`); nominally 1g = 410 counts, 2,441µg a count |

`vibe_cfg_default()` keeps 10kHz, a 0 to 3.1V range (1g = 436 counts) and
a DC window of 1.0V to 2.3V; the host tests run on it. The node sets its
rate, window and scale over it.

The channels share one ADC and one clock, so the index lands at a known
sample of the vibration channel. With two channels the index channel is
converted half a sample after the vibration channel, and
`edge_shift_q8 = 128` puts its edges on the vibration channel's clock. The
balance node converts three, and its index is a third of a sample later:
`edge_shift_q8 = 85`. Left out, the phase reads (shift) / (samples a
revolution) of a turn late: 2° at 10,000rpm for a third of a sample.

## Angles

All angles count in degrees from the index mark, in the order points pass
the index sensor: a point at 30° passes the sensor 30° of rotation after
the mark does. That is against the direction of rotation.

- The 1x phase (`phase_cdeg[VIBE_H_1X]`) is the rotation from the index edge
  to the peak of the 1x component.
- A mass moved by 30° in this sense moves the phase by 30° in the same
  sense, so the influence model V = C U holds without a sign change. The
  trial position and the correction are in the same sense.
- Counted with the rotation instead, both the trial position and the
  correction come out mirrored. This is why the sense is fixed here.
- The BALANCE screen (`shared/ui/balance_screen.c`) draws angles clockwise
  from the mark at the top and numbers the blades in the same sense; it
  takes the angle and the blade count only, so it fits either sense. Seen
  as drawn, the rotor turns counter-clockwise.
- Without an index the angles count from a mark chosen on the rotor, in the
  same sense.

## Demodulator

```c
vibe_cfg_t cfg;
vibe_cfg_default(&cfg);          /* 10kHz, 250ms windows, 1g = 436 counts */
cfg.edge_shift_q8 = 128;         /* index channel half a sample later */
cfg.blades = 3;
vibe_demod_init(&d, &cfg, on_report, ctx);
vibe_edge_init(&ix, 1200, 2200, true);

/* per ADC frame: n samples of each channel */
uint32_t edges[16];
size_t ne = vibe_edge_scan(&ix, index_samples, n, edges, 16);
vibe_demod_feed(&d, vib_samples, n, edges, ne);
```

- A revolution is the span from one index edge to the next. Each sample gets
  the rotor angle interpolated between the two edges. The sums of
  x cos(h theta), x sin(h theta), cos(h theta) and sin(h theta) for
  h = 1, 2 and the blade count remove each revolution's DC exactly.
- A window (2,500 samples, 250ms) adds the sums of the revolutions that
  closed in it. The magnitude and phase come from the window's sums, so the
  error of a sample grid that does not divide a revolution falls to the
  order of 1 / (samples in the window).
- A revolution counts when it holds every sample of its span, lasts 10 to
  4,096 samples (146 to 60,000rpm at 10kHz), and its period is within 1/8 of
  the last counted one. Two periods in a row lock the period at the start;
  three in a row lock a new one. A missing pulse costs one revolution, an
  extra pulse two.
- No edge for 4,096 samples sets `VIBE_F_INDEX_LOST` and drops the lock.
- The blade-pass is left out of a revolution where it reaches half the
  sample rate (`VIBE_F_BLADE_OK` clear): 6 blades above 50,000rpm at 10kHz.
- The quality figure is |mean 1x vector| / mean |1x vector| over the last
  16 counted revolutions, 0 to 100%. A steady imbalance reads 99 to 100%;
  noise alone read 16% in the test.
- Positions are Q8 (1/256 sample) and wrap at 2^24 samples, 1,677s at 10kHz.
- A revolution counts in the window that is open when its closing edge is
  taken. With `vibe_demod_feed()`, or each edge handed in before its
  samples, that is the window holding the first sample after the edge.
- `vibe_demod_edge()` and `vibe_demod_push()` can also be called apart. An
  edge later than its samples splits the samples already pushed and is taken
  at once. If the window holding the edge has closed by then, the revolution
  counts in the window open then: an edge at sample 2,490 handed in at
  2,505 counts in window 1, not window 0. At most 16 edges wait ahead of
  their samples.
- After a lost index the next edge is not held to the last one before the
  loss, so the index comes back after a pause of any length.
- A new blade count (`vibe_demod_set_blades()`) clears the window's
  blade-pass sums: that window reports the new count's blade-pass only and
  carries `VIBE_F_RESTARTED`.

### No-index mode

`vibe_demod_set_rpm(&d, rpm_x10)` with the rpm from the ESC telemetry or the
phase tap. The module places its own unit boundaries every G turns, G the
fewest whole turns that hold 256 samples (5 at 10,000rpm, 1 at 300rpm). The
magnitude is the root mean square of the units' magnitudes; the phase reads 0
(`VIBE_F_NO_INDEX`). An rpm off by 1% lowers the 1x by 0.86%; the quality
figure falls to 25%, which is how a wrong rpm shows. Switching between the
modes drops the open revolution, the lock and the quality history. The
window of the switch reports the new mode's revolutions, rpm and magnitudes
only and carries `VIBE_F_RESTARTED`; its rms and DC cover the whole window.

## The node's stream

`vibe_frame.h`:

```c
static const uint8_t chans[3] = { 0, 1, 3 };   /* vibration, index, battery */
vibe_frame_init(&f, chans, 3);
vibe_split_t r = vibe_frame_split(&f, frame, len, out, 100);
/* r.rows rows in out[0..2]; r.broke: conversions were lost */
```

- A frame is the driver's bytes: 4 bytes a conversion, little-endian, the
  raw count in bits 0 to 11 and the channel in bits 13 to 16 (ESP-IDF v6.1,
  `adc_digi_output_data_t` type2 for the ESP32-C6). The node checks this
  layout against the driver's own struct at start.
- A row is one round of the pattern. A round cut by the end of a frame
  finishes in the next one.
- Words before the pattern's first channel are skipped. A word of another
  channel than the one due is a break: the split stops in front of it with
  the rows before it, the open round is dropped, and the next call skips to
  the pattern's first channel. The caller restarts the demodulator there,
  because the sample clock has a gap.
- A full driver pool drops whole frames, whole rounds the order cannot
  show. `vibe_frame_whole()` takes the driver's overflow count before and
  after a read and the count when the stream started: a read is whole when
  the three are equal. The node restarts the stream on any other read.
- `vibe_frame_time()` dates a conversion from the newest frame the driver
  finished: its time less the conversions after the one asked for, so a
  backlog in the pool does not make a report late.
- `vibe_cfg_scale()` takes two points of the ADC's calibration curve
  (`vibe_adc_cal_t`), the sensor's sensitivity in µV/g and the DC window in
  mV, and sets `ug_per_count_q16`, `dc_min` and `dc_max`.
  `vibe_cal_counts()` turns a threshold in mV into counts, `vibe_cal_mv()`
  the report's DC back into mV. `vibe_adc_cal_nominal()` is the curve of a
  chip without calibration: 0 to 3300mV over 4096 counts.

### The index's polarity

A dark line on a bright bell and a bright line on a dark bell give marks of
opposite levels at a reflective sensor's comparator. `vibe_index_scan()`
runs a comparator for each polarity on every sample and takes the edges of
the one whose mark is the shorter level, weighed over each 2,500 samples
(`VIBE_INDEX_SPAN`). A 3mm line on a 30mm bell is 3% of a turn. The edge is
always the start of the line, the side that passes the sensor first. A
change of polarity takes effect from the next block.

## Solvers

| Method | Input | Output |
| --- | --- | --- |
| `vibe_solve_two_run()` | baseline vector, trial vector, trial mass and angle | correction mass and angle, C (magnitude and lag) |
| `vibe_solve_four_run()` | baseline magnitude, magnitudes with the trial at 0, 120 and 240° | correction mass and angle, the fitted baseline, the fit's rms residual |

- The correction cancels the measured unbalance at the trial radius, with the
  trial mass removed.
- Two runs: C = (V1 - V0) / (m at its angle), correction = -V0 / C. A constant
  sensor delay and the sensor's direction are part of C and cancel. All runs
  are at one rpm.
- Four runs: the closed form of the three-circle construction starts a
  Gauss-Newton least-squares fit of the baseline's magnitude and angle and the
  trial's effect to all four magnitudes. The correction is m M / t at
  d + 180°.
- `VIBE_SOLVE_WEAK`: the trial changed the vibration by under 20%. The
  correction is given.
- `VIBE_SOLVE_NO_EFFECT`: the trial runs read as the baseline did. No
  correction.
- `VIBE_SOLVE_INCONSISTENT`: the four-run fit's rms residual exceeds 10% of
  the mean magnitude, or the trial runs differ from the baseline but the fit
  finds no trial effect (1, 2, 2 and 2 is such a set). No correction.
- `VIBE_SOLVE_BAD_INPUT`: no correction.

## Accuracy on synthetic signals

The worst window of each run after the first (after the third at 300rpm),
12-bit quantisation included.
These are host-test figures from synthetic signals; nothing is measured on a
sensor.

| Case | Magnitude | Phase | rpm |
| --- | --- | --- | --- |
| 167Hz, 400mg at 265°, index through the comparator | 0.052% | 0.10° | 0.004% |
| 300rpm, exact edges | 0.015% | 0.00° | 0.000% |
| 3,000rpm, exact edges | 0.029% | 0.00° | 0.000% |
| 10,000rpm, exact edges | 0.035% | 0.04° | 0.000% |
| 60,000rpm, exact edges | 0.005% | 0.10° | 0.000% |
| 400mg with 5mg rms noise, 10,000rpm | 0.070% | 0.03° | |
| 10mg with 5mg rms noise, 7,300 and 10,020rpm | 0.24mg | 2.0° | |
| 5mg rms noise alone | 1x 0.17mg, rms 5.15mg, quality 16% | | |
| 2x, 100mg at 30° beside 400mg 1x | 0.18% | 0.01° | |
| Blade-pass, 3 x 167Hz, 50mg | 0.40% | 0.01° | |
| Index jitter ±1 sample, 3,000rpm | 0.083% | 0.45° | 0.083% |
| Index jitter ±1 sample, 10,000rpm | 0.27% | 1.25° a window, 0.02° over 8 | |
| No index, rpm exact, 10,000rpm | 0.011% | | |
| No index, rpm 1% off | 0.86% | | |
| Two runs end to end, 0.35g at 265° | 0.35g | 265.01° | |
| Four runs end to end, 0.35g at 265° | 0.3501g | 264.97° | |
| Four runs, every magnitude ±1% | 2.8% | 1.5° | |

A 10mg sine without noise reads 2.2% low: 4.4 counts quantised with no
dither. The ADC's own noise dithers it on a real channel; how much is not
measured.

## Arithmetic budget

- The ESP32-C6 core is RV32IMAC (`-march=rv32imac_zicsr_zifencei`,
  `components/soc/project_include.cmake` in ESP-IDF v6.1): no floating-point
  unit, single or double. `vibe_demod.c` and `vibe_edge.c` use no float and
  no libm. Compiled for it at -O2 their only helper calls are `__divdi3` and
  `__udivdi3` (64-bit division, once per revolution and per window),
  `memmove` and `memset`.
- The panel's ESP32-P4 is RV32IMAFC: single precision in hardware.
  `vibe_solve.c` uses float only.
- Per vibration sample, from the RV32IMAC compile at -O2: 15 instructions
  plus 44 for each harmonic (2 without a blade count, 3 with), and 27 to
  store the sample and add it to the rms sums: 174 instructions with the
  blade-pass, 130 without. The comparator takes about 17 a sample of the
  index channel. At -Og the harmonic loop is 55 instructions, at -Os 47.
- At 10kHz that is about 1.9 million instructions a second, 1.2% of a 160MHz
  core at one instruction a cycle. Cycles are not measured on a C6. Once per
  revolution and per window come the 64-bit divisions and the square roots;
  their cycles are not measured either.
- `vibe_solve.c` compiled for the P4 (RV32IMAFC, -O2) calls `atan2f`,
  `cosf`, `fmodf`, `hypotf`, `sinf` and `sqrtf` and no double-precision
  helper.
- RAM: 8,768 bytes of state (`vibe_demod_t`, 8,192 of them the revolution
  buffer) and a 2,048-byte cosine table filled once by CORDIC (coordinate
  rotation digital computer) at the first `vibe_demod_init()`.

## Not measured

- The C6 ADC's rate as the node gets it: 10,040Hz a channel is ESP-IDF
  v6.1's formula (`adc_hal.c`); the node's console line counts it.
- The C6 ADC's effective bits and noise at 10kHz a channel, with the radio
  on and off.
- The phase repeatability of the in-stream index on a real rotor; the
  station can check the index timing with a pulse from the Pi on the index
  pin and the LA2016, not a signal on the vibration input.
- The ADXL335's delay at 4.7nF: about 150µs from the filter, constant at one
  rpm, so it cancels in the two-run method; it is not compensated here.
- The TCRT5000 module's edge and the mark's contrast on a motor bell.
