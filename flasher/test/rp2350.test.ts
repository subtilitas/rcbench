import { test } from 'node:test';
import assert from 'node:assert/strict';
import { CMD, IF_CMD_STATUS, IF_RESET, Picoboot, PicobootError } from '../src/picoboot.ts';
import type { UsbDeviceLike } from '../src/picoboot.ts';
import { CHUNK, StepError, planBin, planUf2, runPlan, sectorAlign } from '../src/rp2350.ts';
import { FAMILY, parseUf2 } from '../src/uf2.ts';
import { makeUf2 } from './fixtures.ts';

/** A flash of 4MB behind the Picoboot methods runPlan calls, with a log of the calls. */
class FakeFlash {
  flash = new Uint8Array(4 * 1024 * 1024).fill(0x5a);
  calls: string[] = [];
  failWrite = false;
  corrupt = false;
  private off(addr: number): number { return addr - 0x10000000; }
  async exclusive(): Promise<Uint8Array> { this.calls.push('exclusive'); return new Uint8Array(0); }
  async exitXip(): Promise<Uint8Array> { this.calls.push('exit-xip'); return new Uint8Array(0); }
  async erase(addr: number, size: number): Promise<Uint8Array> {
    assert.equal(addr % 4096, 0);
    assert.equal(size % 4096, 0);
    assert.ok(size <= CHUNK);
    this.calls.push(`erase ${addr.toString(16)}+${size.toString(16)}`);
    this.flash.fill(0xff, this.off(addr), this.off(addr) + size);
    return new Uint8Array(0);
  }
  async write(addr: number, data: Uint8Array): Promise<Uint8Array> {
    if (this.failWrite) throw new PicobootError('write', 'transfer stalled');
    assert.equal(addr % 256, 0);
    for (let i = 0; i < data.length; i++) this.flash[this.off(addr) + i]! &= data[i]!;
    if (this.corrupt) this.flash[this.off(addr)] = 0;
    return new Uint8Array(0);
  }
  async read(addr: number, size: number): Promise<Uint8Array> { return this.flash.slice(this.off(addr), this.off(addr) + size); }
  async reboot(): Promise<Uint8Array> { this.calls.push('reboot'); return new Uint8Array(0); }
}

const asPb = (f: FakeFlash): Picoboot => f as unknown as Picoboot;

test('regions are padded to whole sectors with 0xFF and merged when they share one', () => {
  const out = sectorAlign([
    { addr: 0x10000100, data: new Uint8Array(256).fill(1) },
    { addr: 0x10000300, data: new Uint8Array(256).fill(2) },
    { addr: 0x10003000, data: new Uint8Array(10).fill(3) },
  ]);
  assert.deepEqual(out.map((r) => [r.addr, r.data.length]), [[0x10000000, 4096], [0x10003000, 4096]]);
  assert.equal(out[0]!.data[0], 0xff);
  assert.equal(out[0]!.data[0x100], 1);
  assert.equal(out[0]!.data[0x200], 0xff);
  assert.equal(out[0]!.data[0x300], 2);
});

test('an absolute UF2 image is written where it says, with the extra range erased and read back blank', async () => {
  const uf2 = parseUf2(makeUf2(0x10002000, 40, FAMILY.ABSOLUTE));
  const plan = planUf2(uf2, null, [{ offset: 0x1ff000, size: 0x1fd000 }]);
  const f = new FakeFlash();
  const steps: string[] = [];
  await runPlan(asPb(f), plan, (p) => { if (steps.at(-1) !== p.step) steps.push(p.step); });
  assert.deepEqual(steps, ['erase', 'write', 'verify', 'reboot']);
  assert.deepEqual(f.calls.slice(0, 2), ['exclusive', 'exit-xip']);
  assert.equal(f.calls.at(-1), 'reboot');
  assert.deepEqual(f.flash.subarray(0x2000, 0x2000 + 40 * 256), uf2.regions[0]!.data);
  assert.equal(f.flash[0x1fff], 0x5a, 'the byte before the image is not touched');
  assert.ok(f.flash.subarray(0x1ff000, 0x3fc000).every((b) => b === 0xff));
  assert.equal(f.flash[0x3fc000], 0x5a, 'the store after slot B is not touched');
});

test('an ARM-S image goes into the partition the boot ROM names, shifted from 0x10000000', () => {
  const uf2 = parseUf2(makeUf2(0x10000000, 4, FAMILY.RP2350_ARM_S, true));
  const plan = planUf2(uf2, { index: 1, start: 0x1ff000, end: 0x3fc000 });
  assert.equal(plan.writes[0]!.addr, 0x101ff000);
  const abs = planUf2(uf2, { index: null, start: 0, end: 0x400000 });
  assert.equal(abs.writes[0]!.addr, 0x10000000);
  assert.throws(() => planUf2(uf2, 'refused'), /takes no rp2350-arm-s image/);
  assert.throws(() => planUf2(parseUf2(makeUf2(0x10000000, 64)), { index: 0, start: 0x2000, end: 0x4000 }), /exceeds partition 0/);
  assert.throws(() => planUf2(uf2, null, [{ offset: 100, size: 4096 }]), /not sector aligned/);
});

test('a raw binary goes to the flash start', () => {
  const plan = planBin(new Uint8Array(5000).fill(9));
  assert.deepEqual(plan.writes.map((r) => [r.addr, r.data.length]), [[0x10000000, 8192]]);
  assert.throws(() => planBin(new Uint8Array(0)), /empty/);
});

test('a failed write names the step', async () => {
  const f = new FakeFlash();
  f.failWrite = true;
  await assert.rejects(runPlan(asPb(f), planBin(new Uint8Array(300)), () => undefined),
    (e: unknown) => e instanceof StepError && e.step === 'write' && /stalled/.test(e.message));
  assert.ok(!f.calls.includes('reboot'));
});

test('flash that differs from the image fails the verify step and is not rebooted', async () => {
  const f = new FakeFlash();
  f.corrupt = true;
  await assert.rejects(runPlan(asPb(f), planBin(new Uint8Array(300).fill(7)), () => undefined),
    (e: unknown) => e instanceof StepError && e.step === 'verify' && /0x10000000/.test(e.message));
  assert.ok(!f.calls.includes('reboot'));
});

/** A USB device that records transfers and answers like the boot ROM. */
function fakeUsb(stallOn?: number): { dev: UsbDeviceLike; log: string[] } {
  const log: string[] = [];
  let pendingIn = 0;
  let stalled = false;
  const ok = <T>(x: T): Promise<T> => Promise.resolve(x);
  const dev = {
    vendorId: 0x2e8a, productId: 0x000f, opened: false,
    configuration: { interfaces: [
      { interfaceNumber: 0, alternate: { interfaceClass: 8, endpoints: [] } },
      { interfaceNumber: 1, alternate: { interfaceClass: 0xff, endpoints: [
        { endpointNumber: 3, direction: 'out', type: 'bulk' }, { endpointNumber: 4, direction: 'in', type: 'bulk' }] } },
    ] },
    open: () => { log.push('open'); return ok(undefined); },
    close: () => ok(undefined),
    selectConfiguration: () => ok(undefined),
    claimInterface: (n: number) => { log.push(`claim ${n}`); return ok(undefined); },
    releaseInterface: () => ok(undefined),
    clearHalt: () => ok(undefined),
    controlTransferOut: (s: USBControlTransferParameters) => { log.push(`ctrl-out ${s.request} idx ${s.index}`); stalled = false; return ok({ status: 'ok', bytesWritten: 0 }); },
    controlTransferIn: (s: USBControlTransferParameters) => {
      log.push(`ctrl-in ${s.request}`);
      const b = new Uint8Array(16);
      new DataView(b.buffer).setUint32(4, 4, true);
      return ok({ status: 'ok', data: new DataView(b.buffer) });
    },
    transferOut: (ep: number, data: Uint8Array) => {
      if (data.length === 32 && new DataView(data.buffer, data.byteOffset).getUint32(0, true) === 0x431fd10b) {
        const id = data[8]!;
        log.push(`cmd ${id.toString(16)} ep ${ep}`);
        if (id === stallOn) stalled = true;
        pendingIn = id & 0x80 ? new DataView(data.buffer, data.byteOffset).getUint32(12, true) : 0;
      } else {
        log.push(`out ${data.length}`);
      }
      return ok({ status: stalled ? 'stall' : 'ok', bytesWritten: data.length });
    },
    transferIn: (ep: number, len: number) => {
      const n = pendingIn > 0 ? pendingIn : 0;
      pendingIn = 0;
      log.push(`in ${n} ep ${ep} of ${len}`);
      return ok({ status: stalled ? 'stall' : 'ok', data: new DataView(new ArrayBuffer(n)) });
    },
  };
  return { dev: dev as unknown as UsbDeviceLike, log };
}

test('a transfer: command, data, then a zero-length acknowledgement in the other direction', async () => {
  const { dev, log } = fakeUsb();
  const pb = await Picoboot.open(dev);
  assert.deepEqual(log.slice(0, 3), ['open', 'claim 1', `ctrl-out ${IF_RESET} idx 1`]);
  log.length = 0;
  await pb.write(0x10000000, new Uint8Array(4096));
  assert.deepEqual(log, [`cmd ${CMD.WRITE.toString(16)} ep 3`, 'out 4096', 'in 0 ep 4 of 64']);
  log.length = 0;
  const got = await pb.read(0x10000000, 512);
  assert.equal(got.length, 512);
  assert.deepEqual(log, [`cmd ${CMD.READ.toString(16)} ep 3`, 'in 512 ep 4 of 512', 'out 0']);
  log.length = 0;
  await pb.erase(0x10000000, 4096);
  assert.deepEqual(log, [`cmd ${CMD.FLASH_ERASE.toString(16)} ep 3`, 'in 0 ep 4 of 64']);
});

test('a stall reads the command status, resets the interface and names the status', async () => {
  const { dev, log } = fakeUsb(CMD.FLASH_ERASE);
  const pb = await Picoboot.open(dev);
  log.length = 0;
  await assert.rejects(pb.erase(0x20000000, 4096), (e: unknown) => e instanceof PicobootError && e.status === 'INVALID_ADDRESS');
  assert.ok(log.includes(`ctrl-in ${IF_CMD_STATUS}`));
  assert.ok(log.includes(`ctrl-out ${IF_RESET} idx 1`));
});

test('a device with another USB ID is refused', async () => {
  const { dev } = fakeUsb();
  (dev as { productId: number }).productId = 0x0009;
  await assert.rejects(Picoboot.open(dev), /no RP2350 in BOOTSEL/);
});
