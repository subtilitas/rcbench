# witmotion

The serial protocol of the WitMotion BWT901CL inertial sensor and its
family: the 11-byte frames decoded to units and the 5-byte commands encoded.
The module parses and encodes. It decides nothing: which frames to ask for,
when to send a command and what a reading means belong to the caller.

| Part | Files | Depends on |
|---|---|---|
| Core | `witmotion.c`, `include/witmotion.h` | C11 standard headers |
| RP2350 pin driver | none | |

The module has no pin driver. The sensor's link is a UART (universal
asynchronous receiver-transmitter) at 115200 baud, 8N1, fixed on the
BWT901CL; the caller supplies the received bytes one at a time and sends the
built commands.

## Constraints

- A frame is 11 bytes: 0x55, a type byte, four signed 16-bit words
  little-endian, and the low byte of the sum of the first 10 bytes
  (`WIT_FRAME_LEN`).
- Types decoded: 0x51 acceleration, 0x52 angular rate, 0x53 angle, 0x54
  magnetic field. Types 0x50 (time) and 0x59 (quaternion) are named
  (`WIT_T_TIME`, `WIT_T_QUAT`) and handed out as raw words; no decoder
  exists for them.
- Scales: acceleration word / 32768 x 16g with 1g = 9.80665m/s²; angular
  rate word / 32768 x 2000°/s; angle word / 32768 x 180° (32767 is
  179.995°, -32768 is -180°); magnetic field 0.15µT per LSB (least
  significant bit); temperature word / 100°C.
- The fourth word of the 0x52 frame is decoded as the battery voltage,
  unsigned, word / 100V (420 is 4.20V). That is the BWT901CL's use of the
  word; other parts of the family put a firmware version there, and the
  decoder cannot tell them apart. A caller hides a value above 6.0V.
- The fourth word of the 0x53 frame is returned as the firmware version
  word, unsigned, not interpreted.
- The parser holds at most one frame in flight (11 bytes, no allocation).
  A byte that is not 0x55 between frames is dropped and counted in
  `resyncs`. A frame whose sum fails is dropped, counted in `bad_sum`, and
  the parser looks for the next 0x55 among the bytes it holds, so a frame
  that began inside the dropped one is kept. The bytes skipped on the way
  count in `resyncs`.
- A sum is 8 bits: 1 in 256 random 11-byte windows that start with 0x55
  passes it. The parser cannot tell such a window from a frame.
- A command is 5 bytes: 0xFF, 0xAA, a register, the value low byte, the value
  high byte (`WIT_CMD_LEN`). The unlock 0xFF 0xAA 0x69 0x88 0xB5 is sent
  first on the newer firmware and is harmless on the older one; its effect
  on the BWT901CL is not measured.
- RATE codes are 0x01 (0.2Hz) to 0x0B (200Hz). A RATE change takes effect
  after SAVE and a power cycle of the sensor. A code outside the table is
  refused and nothing is written.
- CALSW modes are 0 (exit), 1 (accelerometer: keep the sensor still and
  level, about 3s) and 2 (magnetic). A mode above 2 is refused.
- RSW is a bit mask of the frames the sensor sends. The default is 0x1E
  (0x51 to 0x54).
- Values use `float`.
- No function allocates, blocks or keeps a global.

## Interface

`witmotion.h`:

```c
void     wit_parser_init(wit_parser_t *p);
bool     wit_parser_feed(wit_parser_t *p, uint8_t b, wit_frame_t *out);
bool     wit_decode_acc(const wit_frame_t *f, wit_acc_t *out);
bool     wit_decode_gyro(const wit_frame_t *f, wit_gyro_t *out);
bool     wit_decode_angle(const wit_frame_t *f, wit_angle_t *out);
bool     wit_decode_mag(const wit_frame_t *f, wit_mag_t *out);
size_t   wit_cmd(uint8_t out[WIT_CMD_LEN], uint8_t reg, uint16_t value);
size_t   wit_cmd_unlock(uint8_t out[WIT_CMD_LEN]);
size_t   wit_cmd_save(uint8_t out[WIT_CMD_LEN]);
size_t   wit_cmd_rate(uint8_t out[WIT_CMD_LEN], uint8_t code);
size_t   wit_cmd_rsw(uint8_t out[WIT_CMD_LEN], uint16_t mask);
size_t   wit_cmd_calsw(uint8_t out[WIT_CMD_LEN], uint8_t mode);
size_t   wit_cmd_axis6(uint8_t out[WIT_CMD_LEN], bool six);
uint16_t wit_rate_dhz(uint8_t code);
```

`wit_parser_feed()` returns true when `out` holds a frame whose sum holds.
A NULL pointer makes it return false and take no byte. A decoder returns
false for a frame of another type or a NULL pointer. A command encoder
returns `WIT_CMD_LEN` and fills the buffer, or returns 0 and leaves it
untouched. `wit_rate_dhz()` gives the output rate of a code in 0.1Hz (2 for
0x01, 2000 for 0x0B) and 0 for a code outside the table.

## Use in another pico-sdk project

```cmake
add_subdirectory(protocols/witmotion)
target_link_libraries(my_target PRIVATE rcbench_witmotion)
```

The header compiles from C++.

## Trademarks

WitMotion is a name or trademark of its owner; it is used here only to say
which sensors this module decodes. The project is not affiliated with it.
The frame layout and the command bytes come from the sensor's published
datasheet and manual; no vendor code is included. The full notice is in the
repository's [README](../../README.md#legal-notice-and-disclaimer) and in
the section "Trademarks" of [NOTICE](../../NOTICE).

## Tests

`test_witmotion` runs the core on host: frames built byte by byte, the ends of
the 16-bit range, a bad sum, a frame cut short by the next one, 22000 bytes of
seeded noise compared with a reference scanner, every command's bytes and
the refusals. The sensor itself is not on the bench; the decoding of its
output is not measured against a sensor.
