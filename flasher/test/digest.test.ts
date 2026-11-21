import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { md5Hex, sha256Hex } from '../src/digest.ts';
import { padTo4 } from '../src/esp.ts';

test('MD5 matches the reference for the RFC 1321 vectors and for lengths around a block', () => {
  assert.equal(md5Hex(new Uint8Array(0)), 'd41d8cd98f00b204e9800998ecf8427e');
  assert.equal(md5Hex(new TextEncoder().encode('abc')), '900150983cd24fb0d6963f7d28e17f72');
  for (const n of [55, 56, 63, 64, 65, 1000, 70001]) {
    const b = new Uint8Array(n).map((_, i) => (i * 13) & 0xff);
    assert.equal(md5Hex(b), createHash('md5').update(b).digest('hex'), `length ${n}`);
  }
});

test('SHA-256 is the Web Crypto digest in hex', async () => {
  const b = new Uint8Array(3000).map((_, i) => i & 0xff);
  assert.equal(await sha256Hex(b), createHash('sha256').update(b).digest('hex'));
});

test('an image is padded to a multiple of 4 bytes with 0xFF, as esptool-js pads it', () => {
  assert.deepEqual([...padTo4(Uint8Array.of(1, 2, 3, 4, 5))], [1, 2, 3, 4, 5, 0xff, 0xff, 0xff]);
  const even = new Uint8Array(8);
  assert.equal(padTo4(even), even);
});
