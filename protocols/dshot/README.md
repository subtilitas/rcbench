# dshot

DShot out to an ESC (electronic speed controller) and the telemetry burst
back on the same wire.

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
- The driver has no frame rate of its own. One call sends one frame. A frame
  asked for while the previous one is still in the queue is dropped.
- The rate is given in kbit/s. A rate of 0 is refused.
- A plain pin takes 1 PIO (programmable input/output) state machine. A
  bidirectional pin takes 2 in one PIO block, the receiver directly above
  the transmitter. A bind that cannot get that pair is refused.
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
```

`dshot_edt_*()` schedules the command that asks for extended telemetry:
one ask every 500 ms at zero throttle, at most 10 asks per run.

Driver, `out_dshot.h`:

```c
bool out_dshot_bind(uint8_t pin, uint16_t rate_kbit, bool bidirectional);
void out_dshot_send(uint8_t pin, uint16_t value, bool telemetry);
bool out_dshot_poll(uint8_t pin, bool edt, dshot_telem_t *out);
void out_dshot_stop(uint8_t pin);
void out_dshot_release(uint8_t pin);
```

`out_dshot_stop()` sends nothing further. An ESC stops on the silence after
its own timeout.

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
