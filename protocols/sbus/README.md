# sbus

This module decodes and encodes Futaba S.BUS frames: 16 channels in 25
bytes.

| Part | Files | Depends on |
|---|---|---|
| Core | `sbus.c`, `include/sbus.h` | C11 standard headers |
| RP2350 pin driver | none | |

The module has no pin driver. The wire is an inverted 8E2 UART (universal
asynchronous receiver-transmitter) at 100 kbaud; a PIO (programmable
input/output) receiver that produces the bytes and a transmitter that sends
them are not written. The core is linked into the panel image and the host
tests, not into the IOmcu image, and no firmware calls it.

## Constraints

- A frame is 25 bytes: the header 0x0F, 22 bytes of 16 channels at 11 bits
  each, a flag byte and a footer. Channel n starts at bit 11n of the 22
  bytes, least significant bit first. The flag byte carries CH17 in bit 0,
  CH18 in bit 1, frame lost in bit 2 and failsafe in bit 3.
- S.BUS has no checksum. The header value occurs inside channel data, so the
  decoder uses silence to find frame boundaries. A gap of 2000us or more
  (`SBUS_GAP_US`) starts a frame. The caller supplies a timestamp in
  microseconds with every byte.
- Bytes inside a frame are 120us apart. Frames are at least 4ms apart.
- A raw channel value is 0 to 2047. 172 maps to 1000us and 1811 to 2000us.
- The encoder writes the footer 0x00 and clears bits 4 to 7 of the flag
  byte. A channel above 2047 does not fit in 11 bits: the encoder refuses
  the frame and leaves the output unwritten rather than clamping or
  masking the value.
- The decoder reports `failsafe` and `frame_lost` without acting on them.
  A caller that drives outputs from S.BUS must treat `failsafe` as a stop.

## Interface

`sbus.h`:

```c
void     sbus_decoder_reset(sbus_decoder_t *d);
bool     sbus_decode_byte(sbus_decoder_t *d, uint8_t byte, uint32_t now_us,
                          sbus_frame_t *out);
uint16_t sbus_to_us(uint16_t raw);
uint16_t sbus_from_us(uint16_t us);
bool     sbus_encode(const sbus_frame_t *f, uint8_t *out);
```

`sbus_decode_byte()` returns true when `out` contains a complete frame.
The decoder counts frames, bytes discarded while seeking a frame boundary,
and frames with an invalid footer.

`sbus_encode()` writes `SBUS_FRAME_BYTES` bytes to `out` and returns true.
It returns false and leaves `out` unchanged when an argument is NULL or a
channel is above 2047. A frame it writes decodes with `sbus_decode_byte()`
to the frame it was given. On false the caller does not send `out`. It
sends a frame with the throttle channel at its stop value and the failsafe
flag set, or it stops sending and lets the device's own failsafe act.

`sbus_to_us()` rounds to the nearest microsecond and clamps to 800us to
2200us. `sbus_from_us()` is its inverse: it rounds to the nearest count,
halves away from 1000us (1500us gives 992), and clamps to 0 to 2047. From
895us to 2144us, `sbus_to_us(sbus_from_us(us))` returns `us`; below 895us
the raw value is 0 and above 2144us it is 2047. The other direction lands
within 1 count, since 2048 raw values share 1250 pulse widths.

Both functions use the line through 172 = 1000us and 1811 = 2000us. A
device that reads 0.625us per count from 880us produces 2011.9us at 1811,
11.9us higher, and 987.5us at 172, 12.5us lower. These figures come from
the formula and are not measured on a device. A caller that holds a
ceiling on the pulse a device produces applies it in raw counts.

## Use in another pico-sdk project

```cmake
add_subdirectory(protocols/sbus)
target_link_libraries(my_target PRIVATE rcbench_sbus)
```

The header compiles from C++. The caller supplies the received bytes and
their arrival times, and sends the encoded bytes on its own UART or PIO
program.

## Trademarks

Futaba and S.BUS are names or trademarks of their respective owners; they
are used here only to say which receivers and devices this module works
with. The
project is not affiliated with them. No vendor code is included.
The full notice is in the repository's
[README](../../README.md#legal-notice-and-disclaimer) and in the section
"Trademarks" of [NOTICE](../../NOTICE).
