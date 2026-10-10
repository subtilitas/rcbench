# openyge

The OpenYGE ESC (electronic speed controller) protocol: framing, CRC (cyclic
redundancy check), telemetry, status and the parameter table.

| Part | Files | Depends on |
|---|---|---|
| Core | `openyge_frame.c`, `openyge_status.c`, `openyge_params.c`, `include/openyge.h` | C11 standard headers |
| RP2350 pin driver | none | |

The module has no pin driver. The wire is a half-duplex UART (universal
asynchronous receiver-transmitter) at 115200 baud, 8N1; the caller supplies
the received bytes and sends the built ones. The specification is
`docs/OpenYGE.md` in the rcbench repository.

## Constraints

- A frame starts with the sync byte 0xA5 and is 6 to 140 bytes long. The
  header is 6 bytes in version 3 and 4 bytes before it.
- The CRC is CRC-16/XMODEM: polynomial 0x1021, seed 0x0000, no reflection,
  no final XOR. It covers the frame without its last 2 bytes and is stored
  there little-endian. The check value over `"123456789"` is 0x31C3
  (`OPENYGE_CRC_CHECK`).
- A telemetry payload is 26 bytes, a control payload 4 bytes.
- Every field is assembled byte by byte. No struct is cast over a buffer.
- The parameter table holds 64 indices (`OPENYGE_MAX_PARAMS`). A table is
  readable only when every index below its count has been seen.
- The module builds a parameter write request. It does not decide to send
  one. The parameter indices are not confirmed against an ESC.
- Telemetry values use `float`.

## Interface

`openyge.h`:

```c
uint16_t openyge_crc(uint16_t crc, const void *data, size_t len);
void     openyge_decoder_reset(openyge_decoder_t *d);
bool     openyge_decode_byte(openyge_decoder_t *d, uint8_t byte,
                             openyge_frame_t *out);
bool     openyge_telemetry_parse(const openyge_frame_t *f,
                                 openyge_telemetry_t *out);
size_t   openyge_encode(uint8_t *out, size_t cap, uint8_t type,
                        uint8_t device, uint8_t seq, const void *payload,
                        size_t payload_len);
size_t   openyge_build_telemetry_request(uint8_t *out, size_t cap,
                                         uint8_t device, uint8_t seq);
void     openyge_status_decode(uint8_t status1, openyge_status_t *out);
void     openyge_params_observe(openyge_params_t *p, uint16_t index,
                                uint16_t value);
bool     openyge_motor_rpm(const openyge_params_t *p, uint32_t erpm,
                           float *motor_rpm);
```

`openyge_decode_byte()` returns true when `out` holds a frame whose CRC
holds. The decoder counts frames, CRC errors, resynchronisations and frames
rejected for impossible contents.

## Use in another pico-sdk project

```cmake
add_subdirectory(protocols/openyge)
target_link_libraries(my_target PRIVATE rcbench_openyge)
```

The header compiles from C++.
