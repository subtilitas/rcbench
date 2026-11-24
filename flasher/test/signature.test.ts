import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createHash, generateKeyPairSync, sign } from 'node:crypto';
import { readFileSync } from 'node:fs';
import { RELEASE_KEY_PEM } from '../src/release_key.ts';
import { SIG_MAX, SIG_MIN, derOk, derToRaw, importKey, sigShown, verifySignature } from '../src/signature.ts';

// A TEST key pair, made for this run: the release key's private half is
// never used here.
const pair = generateKeyPairSync('ec', { namedCurve: 'P-256' });
const PEM = pair.publicKey.export({ type: 'spki', format: 'pem' }).toString();
const IMAGE = Uint8Array.from({ length: 70_000 }, (_, i) => (i * 31 + 7) & 0xff);
// `openssl dgst -sha256 -sign` writes this form: DER, SHA-256 over the whole file.
const SIG = new Uint8Array(sign('sha256', IMAGE, { key: pair.privateKey, dsaEncoding: 'der' }));

const flip = (b: Uint8Array, at: number): Uint8Array => { const c = b.slice(); c[at]! ^= 0x01; return c; };

/** A DER signature of the two integers as given, with no shortening. */
function der(r: number[], s: number[]): Uint8Array {
  const body = [0x02, r.length, ...r, 0x02, s.length, ...s];
  return Uint8Array.from([0x30, body.length, ...body]);
}

test('a signature of the image under the key is signed', async () => {
  assert.equal(await verifySignature(PEM, IMAGE, SIG), 'ok');
  assert.equal(sigShown('ok'), 'signed');
});

test('one flipped byte of the image does not match', async () => {
  for (const at of [0, 35_000, IMAGE.length - 1]) {
    assert.equal(await verifySignature(PEM, flip(IMAGE, at), SIG), 'bad', `byte ${at}`);
  }
  assert.equal(sigShown('bad'), 'mismatch');
});

test('one flipped byte in the last integer of the signature does not match', async () => {
  assert.equal(await verifySignature(PEM, IMAGE, flip(SIG, SIG.length - 1)), 'bad');
});

test('the signature of another image does not match', async () => {
  const other = new Uint8Array(sign('sha256', IMAGE.subarray(1), pair.privateKey));
  assert.equal(await verifySignature(PEM, IMAGE, other), 'bad');
  assert.equal(await verifySignature(PEM, IMAGE.subarray(0, 1000), SIG), 'bad');
});

test('a signature under another key does not match, the release key included', async () => {
  const other = generateKeyPairSync('ec', { namedCurve: 'P-256' });
  const otherPem = other.publicKey.export({ type: 'spki', format: 'pem' }).toString();
  assert.equal(await verifySignature(otherPem, IMAGE, SIG), 'bad');
  assert.equal(await verifySignature(RELEASE_KEY_PEM, IMAGE, SIG), 'bad');
});

test('no signature and 0 bytes are no signature', async () => {
  assert.equal(await verifySignature(PEM, IMAGE, null), 'none');
  assert.equal(await verifySignature(PEM, IMAGE, new Uint8Array(0)), 'none');
  assert.equal(sigShown('none'), 'none');
});

test('a malformed signature is refused by form before the key', async () => {
  const one = [1];
  const cases: [string, Uint8Array][] = [
    ['7 bytes', Uint8Array.from([0x30, 0x05, 0x02, 0x01, 0x01, 0x02, 0x00])],
    ['73 bytes', Uint8Array.from([...SIG, ...new Uint8Array(SIG_MAX + 1 - SIG.length)])],
    ['SET, not SEQUENCE', Uint8Array.from([0x31, ...SIG.subarray(1)])],
    ['length byte one short', Uint8Array.from([0x30, SIG[1]! - 1, ...SIG.subarray(2)])],
    ['a byte after it', Uint8Array.from([...SIG, 0x00])],
    ['an INTEGER of 0 bytes', der([], [1, 2, 3])],
    ['an INTEGER of 34 bytes', der(Array(34).fill(1), one)],
    ['BIT STRING for s', Uint8Array.from([0x30, 0x06, 0x02, 0x01, 0x01, 0x03, 0x01, 0x01])],
    ['a third INTEGER', Uint8Array.from([0x30, 0x09, 0x02, 0x01, 0x01, 0x02, 0x01, 0x01, 0x02, 0x01, 0x01])],
    ['text', new TextEncoder().encode('not a signature at all')],
  ];
  for (const [what, sig] of cases) {
    assert.equal(derOk(sig), false, what);
    assert.equal(await verifySignature(PEM, IMAGE, sig), 'form', what);
    // The form is checked before the key, as on the panel.
    assert.equal(await verifySignature('no key', IMAGE, sig), 'form', what);
  }
  assert.equal(sigShown('form'), 'mismatch');
});

test('the size limits are 8 and 72 bytes', () => {
  assert.equal(SIG_MIN, 8);
  assert.equal(SIG_MAX, 72);
  assert.equal(derOk(der([1], [1])), true);
  assert.equal(der([1], [1]).length, SIG_MIN);
  const longest = der([0, ...Array(32).fill(0x80)], [0, ...Array(32).fill(0x80)]);
  assert.equal(longest.length, SIG_MAX);
  assert.equal(derOk(longest), true);
});

test('a well-formed signature with integers above the curve does not match', async () => {
  const big = der(Array(33).fill(0xff), [1]);
  assert.equal(derOk(big), true);
  assert.equal(derToRaw(big), null);
  assert.equal(await verifySignature(PEM, IMAGE, big), 'bad');
  assert.equal(await verifySignature(PEM, IMAGE, der([0], [0])), 'bad');
});

test('integers padded with leading zeros to 33 bytes verify, as on the panel', async () => {
  const raw = derToRaw(SIG)!;
  const pad = (b: Uint8Array): number[] => [0, ...b];
  const padded = der(pad(raw.subarray(0, 32)), pad(raw.subarray(32)));
  assert.equal(padded.length, SIG_MAX);
  assert.equal(derOk(padded), true);
  assert.deepEqual(derToRaw(padded), raw);
  assert.equal(await verifySignature(PEM, IMAGE, padded), 'ok');
});

test('a key that is not a P-256 public key is the key verdict', async () => {
  const p384 = generateKeyPairSync('ec', { namedCurve: 'P-384' }).publicKey.export({ type: 'spki', format: 'pem' }).toString();
  for (const pem of ['', 'no key', p384, '-----BEGIN PUBLIC KEY-----\n!!!!\n-----END PUBLIC KEY-----\n']) {
    assert.equal(await importKey(pem), null);
    assert.equal(await verifySignature(pem, IMAGE, SIG), 'key');
  }
  assert.equal(sigShown('key'), 'mismatch');
});

test('the embedded key is the panel key, a P-256 public key with its fingerprint', async () => {
  const file = readFileSync(new URL('../../firmware/panel/main/keys/release_signing_pub.pem', import.meta.url), 'utf8');
  assert.equal(RELEASE_KEY_PEM, file);
  assert.ok(await importKey(RELEASE_KEY_PEM));
  const spki = Buffer.from(RELEASE_KEY_PEM.replace(/-----[A-Z ]+-----|\s/g, ''), 'base64');
  assert.equal(createHash('sha256').update(spki).digest('hex'), 'cf9320e466c7961fca210b1d56f49b3f89c1ddc14a92f15bd62778f880f7eda2');
});
