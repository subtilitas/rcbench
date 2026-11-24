# passthru

This module passes up to 9 receiver signals through to 9 servo outputs on
an RP2350B and measures every pulse on both sides: width, frame period,
missed frames, glitches and lost signals. It is the remote servo node's
pass-through (board 3 of rcbench).

The module has two parts.

| Part | Files | Depends on |
|---|---|---|
| Core | `pass_decode.c`, `pass_ring.c`, `include/pass_decode.h`, `include/pass_ring.h` | C11 standard headers |
| RP2350 pin driver | `rp2350/pass.pio`, `rp2350/pass_cap.c`, `rp2350/pass_cap.h` | the core, pico-sdk |

- `pass_decode.c` turns pin states with microsecond stamps into pulse
  figures.
- `pass_ring.c` reads the capture's DMA (direct memory access) ring into
  the decoder and finds a lap.
- `pass.pio` holds two PIO (programmable input/output) programs: MIRROR
  copies each input to its output, CAPTURE pushes the state of all 18 pins
  on every change.

## Constraints

- Pins are fixed: output k on GPIO 16 + 2k, input k on GPIO 17 + 2k,
  k = 0 to 8. The part is an RP2350B: the pins pass GPIO 31.
- Both programs run in PIO0 at GPIO base 16: 2 state machines and 6 of the
  block's 32 words. `pass_cap_start()` sets the base, so PIO0 holds no
  program before it.
- MIRROR copies every cycle, 6.7ns at 150MHz. An edge reaches the output
  pad 2 to 3 cycles after it reaches the input pin (13.3 to 20ns: the
  input synchroniser and one sample), before the carrier's buffers.
- An output pad carries MIRROR's copy only while its function is PIO0
  (`pass_cap_route()`). A pad driven by a PWM slice or another PIO block
  ignores MIRROR.
- CAPTURE samples every 2 cycles (13.3ns). A change takes 5 cycles to push;
  a pin that moves again within them is seen at the next sample. The RX
  FIFO (first in, first out) holds 4 states; a full FIFO drops a state and
  sets FDEBUG.RXSTALL (`pass_cap_stalled()`).
- 2 chained DMA channels write each state and then the 32-bit microsecond
  timer into two rings of 8192 words (`PASS_RING_EVENTS`), 64KiB in all,
  each aligned to its 32KiB size. The ring holds 42ms of events up to
  195,000 events/s.
- Stamps have 1us resolution and wrap every 71.6 minutes.
- Decoder rules: a high of 400us to 2600us is a pulse, anything else a
  glitch; a gap over 1.5 periods counts round(gap / period) - 1 missed
  frames; no pulse for 3 periods or 100ms, whichever is longer, is a lost
  signal. A bidirectional DShot output is skipped (`pass_dec_skip()`).
- A ring lapped by the DMA is a measurement gap: the reader drops what is
  in the ring and restarts the decoder at the newest state.
- `pass_cap_*()` are called from one core.

## Interface

Core, `pass_decode.h` and `pass_ring.h`:

```c
void         pass_dec_init(pass_dec_t *d, uint32_t now_us);
void         pass_dec_skip(pass_dec_t *d, uint32_t mask, uint32_t now_us);
void         pass_dec_event(pass_dec_t *d, uint32_t state, uint32_t stamp_us);
void         pass_dec_now(pass_dec_t *d, uint32_t now_us);
void         pass_dec_restart(pass_dec_t *d, uint32_t level, uint32_t now_us);
void         pass_ring_init(pass_ring_t *r);
pass_drain_t pass_ring_drain(pass_ring_t *r, const volatile uint32_t *states,
                             const volatile uint32_t *stamps, uint32_t head,
                             uint32_t now_us, pass_dec_t *d);
```

Driver, `pass_cap.h`:

```c
bool                     pass_cap_start(void);
bool                     pass_cap_running(void);
const volatile uint32_t *pass_cap_states(void);
const volatile uint32_t *pass_cap_stamps(void);
uint32_t                 pass_cap_head(void);
bool                     pass_cap_stalled(void);
void                     pass_cap_route(unsigned k, bool mirror);
```

One pass:

```c
uint32_t head = pass_cap_head();          /* before the clock */
uint32_t now  = time_us_32();
pass_drain_t r = pass_ring_drain(&ring, pass_cap_states(),
                                 pass_cap_stamps(), head, now, &dec);
bool stalled = pass_cap_stalled();
/* dec.pin[PASS_IN_BIT(k)].width_us is input k's last pulse */
```

## Use in another pico-sdk project

```cmake
add_subdirectory(protocols/passthru)
target_link_libraries(my_target PRIVATE
    rcbench_passthru rcbench_passthru_rp2350)
```

`add_subdirectory()` comes after `pico_sdk_init()`. The driver is an
INTERFACE library: `pass_cap.c` compiles in the target that links it, and
`pass.pio.h` is generated in the build tree. All three headers compile from
C++.

A host build links `rcbench_passthru` alone; the folder `rp2350/` is added
only where `pico_generate_pio_header` exists. The host suite runs the
decoder (`test_pass_decode`), the ring (`test_pass_ring`) and both PIO
programs in a model of the state machine (`test_pass_pio`).

## Trademarks

DShot is the name of a protocol; it is used here only to say which outputs
the decoder skips. The project claims no right in the name.
The full notice is in the repository's
[README](../../README.md#legal-notice-and-disclaimer) and in the section
"Trademarks" of [NOTICE](../../NOTICE).
