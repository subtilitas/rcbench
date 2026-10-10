# sbus

Futaba S.BUS: a decoder for 16 channels in a frame of 25 bytes.

| Part | Files | Depends on |
|---|---|---|
| Core | `sbus.c`, `include/sbus.h` | C11 standard headers |
| RP2350 pin driver | none | |

The module has no pin driver. The wire is an inverted 8E2 UART (universal
asynchronous receiver-transmitter) at 100 kbaud; a PIO (programmable
input/output) receiver that produces the bytes is not written.

## Constraints

- A frame is 25 bytes: the header 0x0F, 22 bytes of 16 channels at 11 bits
  each, a flag byte and a footer.
- S.BUS has no checksum. The header value occurs inside channel data, so the
  decoder frames on silence: a gap of 2000 us or more (`SBUS_GAP_US`) starts
  a frame. The caller passes a timestamp in microseconds with every byte.
- Bytes inside a frame are 120 us apart. Frames are at least 4 ms apart.
- A raw channel value is 0 to 2047. 172 maps to 1000 us and 1811 to 2000 us.
- The flags `failsafe` and `frame_lost` are decoded and not acted on. A
  caller that drives anything from S.BUS treats `failsafe` as a stop.

## Interface

`sbus.h`:

```c
void     sbus_decoder_reset(sbus_decoder_t *d);
bool     sbus_decode_byte(sbus_decoder_t *d, uint8_t byte, uint32_t now_us,
                          sbus_frame_t *out);
uint16_t sbus_to_us(uint16_t raw);
```

`sbus_decode_byte()` returns true when `out` holds a frame. The decoder
counts frames, bytes discarded while looking for a boundary, and frames with
a wrong footer.

## Use in another pico-sdk project

```cmake
add_subdirectory(protocols/sbus)
target_link_libraries(my_target PRIVATE rcbench_sbus)
```

The header compiles from C++. The caller supplies the bytes and their
arrival times.
