import { test } from 'node:test';
import assert from 'node:assert/strict';
import { ESP_BAUD, ROM_BAUD, padTo4, stubBaud } from '../src/esp.ts';

test('the USB Serial/JTAG 303a:1001 stays at 115200 baud, a USB-to-UART bridge gets 460800', () => {
  assert.equal(stubBaud({ usbVendorId: 0x303a, usbProductId: 0x1001 }), ROM_BAUD);
  assert.equal(ROM_BAUD, 115200);
  assert.equal(stubBaud({ usbVendorId: 0x1a86, usbProductId: 0x55d3 }), ESP_BAUD);
  assert.equal(stubBaud({ usbVendorId: 0x10c4, usbProductId: 0xea60 }), 460800);
  assert.equal(stubBaud({ usbVendorId: 0x303a, usbProductId: 0x4001 }), ESP_BAUD);
  assert.equal(stubBaud({}), ESP_BAUD);
});

test('a chunk is padded to 4 bytes with 0xFF, as esptool-js pads it', () => {
  assert.deepEqual(Array.from(padTo4(Uint8Array.of(1, 2, 3, 4, 5))), [1, 2, 3, 4, 5, 0xff, 0xff, 0xff]);
  const even = Uint8Array.of(1, 2, 3, 4);
  assert.equal(padTo4(even), even);
});
