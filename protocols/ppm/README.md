# ppm

PPM (pulse-position modulation) out: up to 8 channels on one pin.

The module has two parts.

| Part | Files | Depends on |
|---|---|---|
| Core | `ppm.c`, `include/ppm.h` | C11 standard headers |
| RP2350 pin driver | `rp2350/ppm.pio`, `rp2350/out_ppm.c`, `rp2350/out_ppm.h` | the core, pico-sdk |

## Constraints

- At most 8 channels per frame (`PPM_MAX_CHANNELS`).
- A channel is 400 us to 2500 us, both ends included
  (`PPM_CHANNEL_MIN_US`, `PPM_CHANNEL_MAX_US`). A width outside the range is
  refused, not clamped.
- The mark is 300 us and the frame 22.5 ms by default.
- A frame whose sync gap is under 3000 us (`PPM_SYNC_MIN_US`) is refused.
  8 channels at 2500 us do not fit a 22.5 ms frame.
- The state machine counts in microseconds: its clock divider is the system
  clock in MHz.
- The driver takes 1 PIO (programmable input/output) state machine and 2 DMA
  (direct memory access) channels per pin. The frame repeats without the
  processor.
- A channel written during a frame takes effect in the next frame.
- The polarity is positive. Negative shift is a pad inversion the caller
  sets.
- `OUT_PPM_MAX_PINS` pins are bound at one time, 8 unless the build defines
  another number.

## Interface

Core, `ppm.h`:

```c
size_t   ppm_frame(const uint16_t *channel_us, uint8_t channels,
                   const ppm_cfg_t *cfg, uint16_t *runs, size_t max_runs);
uint32_t ppm_min_frame_us(uint8_t channels, const ppm_cfg_t *cfg);
```

`ppm_frame()` writes mark, space, mark, space and so on, in microseconds,
and returns the number of runs or 0 for a frame that cannot be built.

Driver, `out_ppm.h`:

```c
bool out_ppm_bind(uint8_t pin, uint8_t channels, uint16_t rate_hz);
bool out_ppm_write(uint8_t pin, const uint16_t *channel_us, uint8_t channels);
void out_ppm_stop(uint8_t pin);
void out_ppm_release(uint8_t pin);
```

Nothing is emitted between `out_ppm_bind()` and the first `out_ppm_write()`.
A refused write stops the output and leaves the pin low.

## Use in another pico-sdk project

```cmake
add_subdirectory(protocols/ppm)
target_link_libraries(my_target PRIVATE rcbench_ppm rcbench_ppm_rp2350)
# Optional: another number of pins.
target_compile_definitions(my_target PRIVATE OUT_PPM_MAX_PINS=2u)
```

`add_subdirectory()` comes after `pico_sdk_init()`. The driver is an
INTERFACE library: `out_ppm.c` compiles in the target that links it, and
`ppm.pio.h` is generated in the build tree. Both headers compile from C++.

A host build links `rcbench_ppm` alone; the folder `rp2350/` is added only
where `pico_generate_pio_header` exists.
