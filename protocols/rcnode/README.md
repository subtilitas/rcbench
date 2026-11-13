# rcnode

This module is the Bluetooth LE (Low Energy) protocol of the rcbench
sensor nodes: the power node, an ESP32-C6 that reads an INA228 on the
ESC's (electronic speed controller's) XT60 power path, and the balance
node, an ESP32-C6 with a vibration sensor and an index sensor. A node
sends finished readings. The node firmware (`firmware/node`) encodes the
packets, the panel decodes them, and the host suite holds both to one
layout.

| Part | Files | Depends on |
|---|---|---|
| Core | `rcnode.c`, `rcnode_win.c`, `rcnode_adv.c`, `include/rcnode.h` | C11 standard headers |
| RP2350 pin driver | none | |

The module has no pin driver. A node is a GATT (generic attribute profile)
server on its own Bluetooth stack. The caller moves the bytes.

## Constraints

- Every packet is little-endian and starts with a 6-byte header: `type`
  (1 byte), `seq` (1 byte, counts the packets of one characteristic, wraps
  at 256) and `t_node` (4 bytes, the node's clock in µs, wraps at 2^32µs,
  71.6min).
- `rcnode_t_diff()` gives the signed time between two `t_node` values
  across the wrap. It is correct while the two lie less than 2^31µs
  (35.8min) apart. `rcnode_seq_lost()` gives the packets lost between two
  `seq` values: 0 to 255.
- A packet's length is fixed by its type. The decoder refuses another
  length, an unknown type and a field outside its range. It keeps flag bits
  that are not defined.
- All packets are 20 bytes or fewer, which fits the default ATT (attribute
  protocol) MTU (maximum transmission unit) of 23, except the balance
  report: 24 bytes, which needs an MTU of 27 or more. The node asks for an
  MTU of 247.
- The service and characteristic UUIDs (universally unique identifiers)
  share the base `72636265-6E63-6800-XXXX-000000000000`. The first 7 bytes
  spell "rcbench" in ASCII.

| XXXX | Characteristic | Properties | Content |
|---|---|---|---|
| 0100 | the node service | | |
| 0101 | STREAM | NOTIFY | power: windows, accumulators, run extremes; balance: reports |
| 0102 | STATUS | READ, NOTIFY | `RCNODE_T_STATUS`, notified every 1000ms |
| 0103 | CONTROL | WRITE, WRITE WITHOUT RESPONSE | 1 to 3 bytes, below |
| 0104 | INFO | READ | `RCNODE_T_INFO` |

### Packets

| Type | Name | Characteristic | Length | Rate |
|---|---|---|---|---|
| 0x01 | `RCNODE_T_PWR_WIN` | STREAM | 20 bytes | every 20ms |
| 0x02 | `RCNODE_T_PWR_ACC` | STREAM | 20 bytes | every 500ms |
| 0x03 | `RCNODE_T_PWR_RUN` | STREAM | 18 bytes | every 500ms |
| 0x20 | `RCNODE_T_BAL_REPORT` | STREAM | 24 bytes | every 250ms |
| 0x30 | `RCNODE_T_STATUS` | STATUS | 20 bytes | every 1000ms and on a read |
| 0x31 | `RCNODE_T_INFO` | INFO | 20 bytes | on a read |

The byte tables of each packet are in `include/rcnode.h`, beside its
struct.

- **Power window.** Flags, the sample count, the mean and the highest
  current (µA, signed 32 bits), the mean and the lowest bus voltage (2mV
  steps, 16 bits, 0 to 131.07V). A current sample at either end of the
  INA228's 20-bit range is a clip: it is counted and flagged, and it is not
  part of the mean or the highest value. A window without a current value
  carries the highest current `RCNODE_I_NONE` (INT32_MIN). A window without a
  sample carries the lowest voltage `RCNODE_V_NONE` (65535). The flag
  `RCNODE_WF_PACK` is set when the mean voltage is 5.0V or more.
- **Accumulators.** CHARGE (40 bits, two's complement) and ENERGY (40 bits,
  unsigned) exactly as the INA228 holds them, the die temperature in 0.01°C
  steps, and `epoch`. The node never clears the accumulators. The part
  clears them at its own reset and at each set-up the node writes, and
  `epoch` counts those set-ups (wraps at 256). The panel takes the
  difference from the first packet of a run with `rcnode_charge_delta()`
  and `rcnode_energy_delta()`, both across the 40-bit wrap, and converts
  the steps with the calibration INFO names.
- **Run extremes.** The tag of the last RUN write, the highest current and
  the lowest voltage of every window since, and the number of windows. A
  radio gap loses no peak.
- **Balance report.** Flags, quality (%, 0 to 100), rpm (1rpm steps), the
  1x, 2x and blade-pass components as a magnitude (mg, or ADC counts with
  `RCNODE_BF_COUNTS`) and a phase (0.1° steps from the index edge in the
  direction of rotation, 0 to 3599), and the broadband rms (root mean
  square) in mg. The decoder refuses a phase of 3600 or more and a quality
  over 100. No node sends it yet: the balance role is a stub.
- **Status.** Flags (battery low under 3.5V, no battery, sensor online,
  pack present, held awake by the console, STREAM subscribed), the cell
  voltage in mV, the firmware version as 3 bytes, the sensor's state
  (`rcnode_sensor_t`), and three 16-bit counters that wrap: failed bus
  transactions, sensor set-ups after a reset or a loss, and STREAM packets
  not sent.
- **Info.** The protocol version (1), the role, the board (1 = Waveshare
  ESP32-C6-Zero), the address and its type, and for the power node the
  INA228's address, ADCRANGE and shunt in µΩ.

### Control

| Byte 0 | Operation | Bytes | Value | Roles |
|---|---|---|---|---|
| 0x01 | RUN | 2 | a tag, 0 to 255; the run extremes restart | both |
| 0x02 | SLEEP | 1 | | both |
| 0x03 | OFF | 1 | | both |
| 0x04 | BLADES | 2 | 1 to 32 | balance |
| 0x05 | RPM | 3 | 16 bits, 0 to 60000; 0 is the index mode | balance |

The decoder refuses an unknown operation, another length than the
operation's, and a value out of range. `rcnode_ctl_for_role()` says which
role takes which operation.

### Advertising

- Legacy advertising, 29 bytes: flags 0x06, the complete name
  `rcbench-pwr-XXXX` or `rcbench-bal-XXXX` (the two address bytes printed
  last, upper-case hex), and manufacturer data: company 0xFFFF (the value
  left for tests and for no registered company), `r`, `c`, the protocol
  version and the role.
- Scan response, 18 bytes: the service UUID in a complete list of 128-bit
  UUIDs.
- `rcnode_adv_match()` takes a report's advertising or scan response. The
  manufacturer data names the role first, then the name. The service UUID
  alone matches with role 0. A structure whose length runs past the end
  refuses the report.

### The power node's windows

`rcnode_win_add()` takes one sample at a time with its time on a 64-bit
clock and closes a 20ms window when a sample of a later window arrives.
Windows are aligned to the node's clock: window k runs from k × 20000µs to
(k + 1) × 20000µs, and the closed window's `t_node` is its end cut to 32
bits. Windows without a sample between two are not sent. `rcnode_run_take()`
folds each closed window into the run extremes.

- No function allocates, blocks or keeps a global.

## Interface

`rcnode.h`:

```c
void         rcnode_uuid(uint16_t id, uint8_t out[16]);
size_t       rcnode_len(uint8_t type);
size_t       rcnode_enc_pwr_win(const rcnode_pwr_win_t *p, uint8_t *out, size_t cap);
size_t       rcnode_enc_pwr_acc(const rcnode_pwr_acc_t *p, uint8_t *out, size_t cap);
size_t       rcnode_enc_pwr_run(const rcnode_pwr_run_t *p, uint8_t *out, size_t cap);
size_t       rcnode_enc_bal_report(const rcnode_bal_report_t *p, uint8_t *out, size_t cap);
size_t       rcnode_enc_status(const rcnode_status_t *p, uint8_t *out, size_t cap);
size_t       rcnode_enc_info(const rcnode_info_t *p, uint8_t *out, size_t cap);
rcnode_err_t rcnode_decode(const uint8_t *b, size_t n, rcnode_pkt_t *out);
size_t       rcnode_enc_ctl(const rcnode_ctl_t *c, uint8_t *out, size_t cap);
rcnode_err_t rcnode_dec_ctl(const uint8_t *b, size_t n, rcnode_ctl_t *out);
bool         rcnode_ctl_for_role(uint8_t role, uint8_t op);
uint8_t      rcnode_seq_lost(uint8_t prev, uint8_t now);
int32_t      rcnode_t_diff(uint32_t later, uint32_t earlier);
int64_t      rcnode_sext40(uint64_t raw);
uint64_t     rcnode_energy_delta(uint64_t prev, uint64_t now);
int64_t      rcnode_charge_delta(uint64_t prev, uint64_t now);
uint16_t     rcnode_v_field(int32_t uv);
void         rcnode_win_init(rcnode_win_t *w);
bool         rcnode_win_add(rcnode_win_t *w, uint64_t t_us, const rcnode_sample_t *s,
                            uint8_t keep_flags, rcnode_pwr_win_t *out);
void         rcnode_run_mark(rcnode_run_t *r, uint8_t tag);
void         rcnode_run_take(rcnode_run_t *r, const rcnode_pwr_win_t *w);
bool         rcnode_name(uint8_t role, const uint8_t addr[6], char out[RCNODE_NAME_MAX]);
size_t       rcnode_adv_build(uint8_t role, const uint8_t addr[6], uint8_t *out, size_t cap);
size_t       rcnode_rsp_build(uint8_t *out, size_t cap);
bool         rcnode_adv_match(const uint8_t *ad, size_t len, uint8_t *role);
```

## Use in another project

```cmake
add_subdirectory(protocols/rcnode)
target_link_libraries(my_target PRIVATE rcbench_rcnode)
```

Under ESP-IDF (Espressif Internet-of-Things Development Framework) the
folder is a component: name it in `EXTRA_COMPONENT_DIRS` and add `rcnode`
to a component's `REQUIRES`. The header compiles from C++.
