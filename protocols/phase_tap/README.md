# phase_tap

The edges of one ESC (electronic speed controller) motor phase, captured on
one input pin and turned into beeps with their pitch.

The module has two parts.

| Part | Files | Depends on |
|---|---|---|
| Core | `tone.c`, `edge_ring.c`, `tone_svc.c`, `include/tone.h`, `include/edge_ring.h`, `include/tone_svc.h` | C11 standard headers |
| RP2350 pin driver | `rp2350/tone_cap.pio`, `rp2350/tone_cap.c`, `rp2350/tone_cap.h` | the core, pico-sdk |

- `tone.c` is the detector: edges in, windows and beeps out.
- `edge_ring.c` reads the words the capture leaves in its ring and extends
  their 31-bit counts to 64 bits.
- `tone_svc.c` is one pass: ring to detector to finished beeps.

## Constraints

- The capture is built for a 150 MHz system clock and is refused at any
  other (`TONE_SVC_SYS_HZ`).
- One tick is 4 system clocks, 26.7 ns. A word carries 31 bits of the tick
  count, which wraps every 57.3 s. The caller's estimate of the present
  tick has to be within 28.6 s of the truth.
- The capture hold-off is 8000 ns (`TONE_SVC_HOLD_NS`). A rise starts a
  burst only after a low of at least twice the hold-off.
- The ring is 4096 words (`TONE_RING_WORDS`). A reader the DMA (direct
  memory access) channel has lapped discards the ring and reports an
  overrun.
- A pass is made every 1 ms and takes at most 16 finished beeps
  (`TONE_SVC_BEEPS`).
- The detector's limits: a window of 1 ms to 100 ms, a silence of at most
  1 s to end a beep, a lowest tone of at least 50 Hz.
- The driver takes 1 PIO (programmable input/output) state machine and 1 DMA
  channel, and serves 1 pin. The pin is an input with its pull-down on.
- The input sees the phase through a series resistor of 4.7 kOhm and a
  clamp. The module does not drive the pin.
- `tone_cap_start()` and `tone_cap_stop()` are called from one core, the
  ring reader from the other. A capture is paused and the reader's
  acknowledgement awaited before it is stopped.

## Interface

Core, `tone_svc.h`:

```c
void     tone_svc_init(tone_svc_t *s);
uint64_t tone_svc_ticks(uint64_t us);
size_t   tone_svc_step(tone_svc_t *s, const tone_cmd_t *cmd,
                       const uint32_t *ring, uint32_t wr, uint64_t now,
                       bool fifo_overrun, tone_rec_t *rec,
                       tone_status_t *st);
```

`tone.h` and `edge_ring.h` are the detector and the ring reader by
themselves, for a caller with its own pass.

Driver, `tone_cap.h`:

```c
bool            tone_cap_start(uint8_t pin);
bool            tone_cap_pause(void);
void            tone_cap_resume(void);
void            tone_cap_stop(void);
uint64_t        tone_cap_start_us(void);
const uint32_t *tone_cap_ring(void);
uint32_t        tone_cap_wr(void);
bool            tone_cap_stalled(void);
```

One pass, every 1 ms on the reading core:

```c
bool   stalled = tone_cap_stalled();          /* before the position */
uint32_t wr    = tone_cap_wr();
uint64_t now   = tone_svc_ticks(time_us_64() - tone_cap_start_us());
size_t n = tone_svc_step(&svc, &cmd, tone_cap_ring(), wr, now, stalled,
                         rec, &status);
```

## Use in another pico-sdk project

```cmake
add_subdirectory(protocols/phase_tap)
target_link_libraries(my_target PRIVATE
    rcbench_phase_tap rcbench_phase_tap_rp2350)
```

`add_subdirectory()` comes after `pico_sdk_init()`. The driver is an
INTERFACE library: `tone_cap.c` compiles in the target that links it, and
`tone_cap.pio.h` is generated in the build tree. All four headers compile
from C++.

A host build links `rcbench_phase_tap` alone; the folder `rp2350/` is added
only where `pico_generate_pio_header` exists.
