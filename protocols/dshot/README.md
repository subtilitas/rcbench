# dshot

This module sends DShot frames to an ESC (electronic speed controller)
and receives the telemetry reply on the same wire.

The module has two parts.

| Part | Files | Depends on |
|---|---|---|
| Core | `dshot_frame.c`, `dshot_telem.c`, `dshot_edt.c`, `include/dshot.h` | C11 standard headers |
| RP2350 pin driver | `rp2350/dshot.pio`, `rp2350/out_dshot.c`, `rp2350/out_dshot.h` | the core, pico-sdk |

## Constraints

- A frame is 16 bits: 11 bits of value, 1 telemetry bit, 4 bits of checksum.
- Value 0 stops the motor, 1 to 47 are commands, 48 to 2047 are throttle.
- An ESC acts on a command after 10 repeats (`DSHOT_CMD_REPEATS`).
- A bidirectional reply is 21 line bits at 5/4 of the frame rate, GCR
  (group-coded recording) coded. The driver samples it 5 times per bit
  (`DSHOT_RX_OVERSAMPLE`) into 8 words of 32 bits.
- The caller sets the frame rate: each call sends one frame. A request
  made while the previous frame is still queued is dropped.
- The rate is given in kbit/s. A rate of 0 is refused.
- A plain output uses 1 PIO (programmable input/output) state machine. A
  bidirectional output uses 2 in the same PIO block, with the receiver
  directly above the transmitter. Binding is refused if that pair is
  unavailable.
- `OUT_DSHOT_MAX_PINS` pins are bound at one time, 8 unless the build
  defines another number.
- Not confirmed against an ESC: the extended telemetry frame types and
  their units, and the 5/4 reply rate.

## Interface

Core, `dshot.h`:

```c
uint16_t dshot_frame(uint16_t value, bool telemetry, bool inverted);
uint16_t dshot_throttle(uint16_t command, uint16_t span);
bool     dshot_rx_bits(const uint32_t *samples, size_t words,
                       uint8_t oversample, uint32_t *line);
bool     dshot_telem_decode(uint32_t line, bool edt, dshot_telem_t *out);
uint32_t dshot_rpm(uint32_t erpm, uint8_t pole_pairs);
void     dshot_edt_bind(dshot_edt_t *e, uint32_t now_ms);
bool     dshot_edt_frame(dshot_edt_t *e, bool stopped, uint32_t now_ms);
uint32_t dshot_edt_millivolts(uint16_t count);   /* also _amps, _celsius */
```

`dshot_edt_*()` schedules requests for extended telemetry at zero
throttle. Each request repeats the command 10 times. The next request
starts 500ms after the last repeat: 509ms between starts at 1kHz. A run
sends at most 10 requests.

`dshot_throttle(command, span)` maps a command from 0 to `span` onto 0
(stop) or a throttle value from 48 to 2047: 0 gives 0, 1 gives 48 for a
`span` of 2 or more, `span` gives 2047. A `span` of 1 gives 2047 for a
command of 1. A command above `span` gives 0, as `dshot_frame()` gives 0 for
a value above 2047.

Driver, `out_dshot.h`:

```c
bool out_dshot_bind(uint8_t pin, uint16_t rate_kbit, bool bidirectional);
void out_dshot_send(uint8_t pin, uint16_t value, bool telemetry);
bool out_dshot_poll(uint8_t pin, bool edt, dshot_telem_t *out);
void out_dshot_stop(uint8_t pin);
bool out_dshot_wake(uint8_t pin);
void out_dshot_release(uint8_t pin);
```

`out_dshot_bind()` returns true if the pin is already bound at the
requested rate and mode. The output continues running. A different rate
or mode returns false and also leaves the existing output running. Call
`out_dshot_release()` before binding the pin with different settings.

`out_dshot_stop()` stops sending frames and drives the pin low, in both
modes. The ESC stops after its own signal-loss timeout. A bound pin is low
from the bind to the first frame as well. A bidirectional line high with no
frames on it is the bootloader entry condition of the family S (BLHeli_S),
Bluejay and AM32 ESC firmware.

A bidirectional pin idles high on its pull-up between the frames of a run.
`out_dshot_wake()` takes it from the low rest to that idle and sends nothing;
it returns true when it released the line. Call it one frame period before
the first frame of a run. `out_dshot_send()` on a bidirectional pin still at
its low rest does the same and sends no frame, so no frame starts from a low
line. A plain DShot pin needs no wake: its frames start from its low idle.

`out_dshot_release()` gives the pad back as a plain input with no pull. The
driver does not touch a released pin, so a programmer that holds it sets
its level.

## Use in another pico-sdk project

```cmake
add_subdirectory(protocols/dshot)
target_link_libraries(my_target PRIVATE rcbench_dshot rcbench_dshot_rp2350)
# Optional: another number of pins.
target_compile_definitions(my_target PRIVATE OUT_DSHOT_MAX_PINS=4u)
```

`add_subdirectory()` comes after `pico_sdk_init()`. The driver is an
INTERFACE library: `out_dshot.c` compiles in the target that links it, and
`dshot.pio.h` is generated in the build tree. Both headers compile from C++.

A host build links `rcbench_dshot` alone; the folder `rp2350/` is added only
where `pico_generate_pio_header` exists.

## Trademarks

DShot is the name of a protocol; it is used here only to say which ESCs
this module works with. The project claims no right in the name. No code of
another implementation is included.
The full notice is in the repository's
[README](../../README.md#legal-notice-and-disclaimer) and in the section
"Trademarks" of [NOTICE](../../NOTICE).
