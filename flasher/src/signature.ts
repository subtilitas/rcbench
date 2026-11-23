// The release signature of an image, checked in the browser with Web Crypto
// by the panel's rules (shared/fwupdate/fw_sign.c, fw_sig.h).
//
// A signature is `<image>.sig`: ECDSA (Elliptic Curve Digital Signature
// Algorithm) on curve P-256 over the SHA-256 of the whole file, in the DER
// (Distinguished Encoding Rules) form `openssl dgst -sha256 -sign` writes,
// 8 to 72 bytes. The form is held to the panel's check before Web Crypto
// sees it: a SEQUENCE of two INTEGERs of 1 to 33 bytes each, short lengths,
// nothing after it. Web Crypto takes the two integers as 32 bytes each
// (IEEE P1363), so the DER form is turned into that.

/** The shortest DER signature of P-256: 2 + 2 x (2 + 1) bytes. */
export const SIG_MIN = 8;
/** The longest: 2 + 2 x (2 + 33) bytes. */
export const SIG_MAX = 72;

const DER_SEQ = 0x30;
const DER_INT = 0x02;
/** The longest integer of P-256: 32 bytes and a leading zero. */
const DER_INT_MAX = 33;
const P256_BYTES = 32;

/**
 * The verdict, as the panel's fw_sig_t names it.
 *
 * none: no signature (none came, or 0 bytes); ok: it verifies under the key;
 * form: not a DER signature of P-256, or the wrong size; key: the key is not
 * a P-256 public key; bad: well formed, and it does not verify.
 */
export type SigVerdict = 'none' | 'ok' | 'form' | 'key' | 'bad';

/** What the page shows: signed, no signature, or a signature that does not match. */
export type SigShown = 'signed' | 'none' | 'mismatch';

/** The shown state of a verdict: form, key and bad all read "does not match". */
export function sigShown(v: SigVerdict): SigShown {
  return v === 'ok' ? 'signed' : v === 'none' ? 'none' : 'mismatch';
}

/** One INTEGER at sig[at]: its value bytes, or null for a bad tag or length. */
function derInt(sig: Uint8Array, at: number): { value: Uint8Array; next: number } | null {
  if (at + 2 > sig.length || sig[at] !== DER_INT) return null;
  const len = sig[at + 1]!;
  if (len === 0 || len > DER_INT_MAX || at + 2 + len > sig.length) return null;
  return { value: sig.subarray(at + 2, at + 2 + len), next: at + 2 + len };
}

/** Whether the bytes have the DER form of an ECDSA P-256 signature (fw_sig_der_ok()). */
export function derOk(sig: Uint8Array): boolean {
  return derParts(sig) !== null;
}

function derParts(sig: Uint8Array): [Uint8Array, Uint8Array] | null {
  if (sig.length < SIG_MIN || sig.length > SIG_MAX || sig[0] !== DER_SEQ || sig[1]! + 2 !== sig.length) return null;
  const r = derInt(sig, 2);
  if (!r) return null;
  const s = derInt(sig, r.next);
  if (!s || s.next !== sig.length) return null;
  return [r.value, s.value];
}

/** An unsigned big-endian integer as 32 bytes; null when it needs more. */
function fixed32(v: Uint8Array): Uint8Array | null {
  let i = 0;
  while (i < v.length && v[i] === 0) i++;
  const digits = v.subarray(i);
  if (digits.length > P256_BYTES) return null;
  const out = new Uint8Array(P256_BYTES);
  out.set(digits, P256_BYTES - digits.length);
  return out;
}

/**
 * The DER signature as r and s of 32 bytes each, as Web Crypto takes it.
 * Null for a form derOk() refuses or an integer above 32 significant bytes,
 * which no P-256 signature verifies with.
 */
export function derToRaw(sig: Uint8Array): Uint8Array | null {
  const parts = derParts(sig);
  if (!parts) return null;
  const r = fixed32(parts[0]);
  const s = fixed32(parts[1]);
  if (!r || !s) return null;
  const out = new Uint8Array(2 * P256_BYTES);
  out.set(r, 0);
  out.set(s, P256_BYTES);
  return out;
}

/** The DER bytes of the SubjectPublicKeyInfo in PEM text; null without one. */
function pemBody(pem: string): Uint8Array | null {
  const m = /-----BEGIN PUBLIC KEY-----([A-Za-z0-9+/=\s]+)-----END PUBLIC KEY-----/.exec(pem);
  if (!m) return null;
  try {
    return Uint8Array.from(atob(m[1]!.replace(/\s+/g, '')), (c) => c.charCodeAt(0));
  } catch {
    return null;
  }
}

/** The P-256 public key of PEM text, for verification; null for any other text. */
export async function importKey(pem: string): Promise<CryptoKey | null> {
  const der = pemBody(pem);
  if (!der) return null;
  try {
    return await crypto.subtle.importKey('spki', der as Uint8Array<ArrayBuffer>, { name: 'ECDSA', namedCurve: 'P-256' }, false, ['verify']);
  } catch {
    return null;
  }
}

/**
 * The verdict on `sig` over the whole `image` under the public key in `pem`,
 * in the panel's order (fw_sig_verify()): none for no signature or 0 bytes,
 * form for a size outside 8 to 72 bytes or a form that is not a DER
 * signature, key for a key that is not a P-256 public key, then ok or bad.
 */
export async function verifySignature(pem: string, image: Uint8Array, sig: Uint8Array | null): Promise<SigVerdict> {
  if (!sig || sig.length === 0) return 'none';
  if (!derOk(sig)) return 'form';
  const key = await importKey(pem);
  if (!key) return 'key';
  const raw = derToRaw(sig);
  if (!raw) return 'bad';
  try {
    const ok = await crypto.subtle.verify({ name: 'ECDSA', hash: 'SHA-256' }, key, raw as Uint8Array<ArrayBuffer>, image as Uint8Array<ArrayBuffer>);
    return ok ? 'ok' : 'bad';
  } catch {
    return 'bad';
  }
}
