// Fake Web Serial and WebUSB for the browser tests, installed before the page
// script runs. window.__fakeConfig picks what is attached:
//
//   esp: { chipId }        an ESP32 boot ROM on a serial port, USB-Serial-JTAG
//                          IDs 303A:1001. chipId 18 is an ESP32-P4, 9 an
//                          ESP32-S3, 13 an ESP32-C6. It answers SYNC, the
//                          register reads, GET_SECURITY_INFO, the stub
//                          upload (with "OHAI"), the baud change, the
//                          compressed flash write and SPI_FLASH_MD5.
//   iomcu: true            an IOmcu with rcbench firmware on a serial port,
//                          IDs 2E8A:0009; opened at 1200 baud it leaves for
//                          BOOTSEL.
//   bootsel: true          the RP2350 boot ROM (2E8A:000F) present from the
//                          start.
//   none: true             the browser has neither interface.
//
// window.__fake holds the state: the flash of each device, the commands, and
// flashHash(kind, start, length) for the checks.
(() => {
  const cfg = window.__fakeConfig || {};
  if (cfg.none) {
    delete Navigator.prototype.serial;
    delete Navigator.prototype.usb;
    return;
  }
  const state = { espFlash: new Uint8Array(1 << 22).fill(0xff), rpFlash: new Uint8Array(1 << 22).fill(0x5a),
    espOps: [], rpCmds: [], bootsel: !!cfg.bootsel, touched: 0 };
  window.__fake = state;
  state.flashHash = async (kind, start, length) => {
    const f = kind === 'esp' ? state.espFlash : state.rpFlash;
    const d = await crypto.subtle.digest('SHA-256', f.slice(start, start + length));
    return Array.from(new Uint8Array(d), (b) => b.toString(16).padStart(2, '0')).join('');
  };

  // ---- ESP boot ROM over SLIP ----
  const u32 = (b, o) => (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0;
  const le = (v) => [v & 0xff, (v >>> 8) & 0xff, (v >>> 16) & 0xff, (v >>> 24) & 0xff];
  const slip = (bytes) => {
    const out = [0xc0];
    for (const b of bytes) {
      if (b === 0xc0) out.push(0xdb, 0xdc);
      else if (b === 0xdb) out.push(0xdb, 0xdd);
      else out.push(b);
    }
    out.push(0xc0);
    return new Uint8Array(out);
  };

  function espPort() {
    let ctrl = null;
    let inFrame = false;
    let esc = false;
    let frame = [];
    let stub = false;
    const regs = new Map(Object.entries(cfg.esp ? cfg.esp.regs || {} : {}).map(([k, v]) => [Number(k), v]));
    let defl = null;
    const send = (bytes) => { if (ctrl) ctrl.enqueue(slip(bytes)); };
    const reply = (op, val, data) => {
      const status = stub ? [0, 0] : [0, 0, 0, 0];
      const body = [...data, ...status];
      send([0x01, op, body.length & 0xff, body.length >> 8, ...le(val), ...body]);
    };
    async function handle(p) {
      const op = p[1];
      const data = p.slice(8);
      state.espOps.push(op);
      switch (op) {
        case 0x08:
          for (let i = 0; i < 8; i++) reply(op, stub ? 0 : 0x12345678, []);
          return;
        case 0x14: {
          const d = new Array(20).fill(0);
          d.splice(12, 4, ...le(cfg.esp.chipId));
          reply(op, 0, d);
          return;
        }
        case 0x0a: reply(op, regs.get(u32(data, 0)) || 0, []); return;
        // Register writes are not kept: a read gives 0, so the SPI command
        // of the flash ID read reads as done at once.
        case 0x09: reply(op, 0, []); return;
        case 0x06:
          reply(op, 0, []);
          stub = true;
          setTimeout(() => send([...'OHAI'].map((c) => c.charCodeAt(0))), 5);
          return;
        case 0x10: defl = { size: u32(data, 0), offset: u32(data, 12), parts: [] }; reply(op, 0, []); return;
        case 0x11: defl.parts.push(data.slice(16)); reply(op, 0, []); return;
        case 0x12: {
          const blob = new Blob(defl.parts);
          const out = new Uint8Array(await new Response(blob.stream().pipeThrough(new DecompressionStream('deflate'))).arrayBuffer());
          state.espFlash.set(out, defl.offset);
          reply(op, 0, []);
          return;
        }
        case 0x13: {
          const addr = u32(data, 0);
          const size = u32(data, 4);
          const hex = await window.__fakeMd5(Array.from(state.espFlash.subarray(addr, addr + size)));
          const raw = hex.match(/../g).map((h) => parseInt(h, 16));
          reply(op, 0, raw);
          return;
        }
        default: reply(op, 0, []);
      }
    }
    function feed(chunk) {
      for (const b of chunk) {
        if (b === 0xc0) {
          if (inFrame && frame.length) { const p = Uint8Array.from(frame); void handle(p); }
          inFrame = true;
          frame = [];
          continue;
        }
        if (!inFrame) continue;
        if (esc) { frame.push(b === 0xdc ? 0xc0 : 0xdb); esc = false; } else if (b === 0xdb) esc = true; else frame.push(b);
      }
    }
    const port = {
      readable: null,
      writable: null,
      getInfo: () => ({ usbVendorId: 0x303a, usbProductId: 0x1001 }),
      async open() {
        port.readable = new ReadableStream({ start(c) { ctrl = c; }, cancel() { ctrl = null; } });
        port.writable = new WritableStream({ write(chunk) { feed(chunk); } });
      },
      async close() { ctrl = null; port.readable = null; port.writable = null; },
      async setSignals() { stub = false; },
      async setBaudRate() {},
      addEventListener() {},
      removeEventListener() {},
    };
    return port;
  }

  function iomcuPort() {
    return {
      readable: null, writable: null,
      getInfo: () => ({ usbVendorId: 0x2e8a, usbProductId: 0x0009 }),
      async open(opts) { if (opts.baudRate === 1200) { state.touched++; setTimeout(() => { state.bootsel = true; }, 50); } },
      async close() { throw new DOMException('The device has been lost.', 'NetworkError'); },
      async setSignals() {},
      addEventListener() {}, removeEventListener() {},
    };
  }

  const ports = [];
  if (cfg.esp) ports.push(espPort());
  if (cfg.iomcu) ports.push(iomcuPort());
  Object.defineProperty(navigator, 'serial', { configurable: true, value: {
    async requestPort(opts) {
      const f = opts && opts.filters;
      const hit = ports.find((p) => !f || f.some((x) => x.usbVendorId === undefined || x.usbVendorId === p.getInfo().usbVendorId));
      if (!hit) throw new DOMException('No port selected by the user.', 'NotFoundError');
      return hit;
    },
    async getPorts() { return []; },
    addEventListener() {}, removeEventListener() {},
  } });

  // ---- RP2350 boot ROM, PICOBOOT ----
  const FLASH = 0x10000000;
  function bootrom() {
    let pending = null;
    let lastStatus = 0;
    const ok = (x) => Promise.resolve(x);
    const offset = (a) => a - FLASH;
    const inRange = (a, n) => a >= FLASH && a + n <= FLASH + state.rpFlash.length;
    const dev = {
      vendorId: 0x2e8a, productId: 0x000f, productName: 'RP2350 Boot', opened: false,
      configuration: { interfaces: [
        { interfaceNumber: 0, alternate: { interfaceClass: 8, endpoints: [] } },
        { interfaceNumber: 1, alternate: { interfaceClass: 0xff, endpoints: [
          { endpointNumber: 3, direction: 'out', type: 'bulk' }, { endpointNumber: 4, direction: 'in', type: 'bulk' }] } },
      ] },
      open() { dev.opened = true; return ok(); },
      close() { dev.opened = false; return ok(); },
      selectConfiguration: () => ok(),
      claimInterface: () => ok(),
      releaseInterface: () => ok(),
      clearHalt: () => ok(),
      controlTransferOut: () => { pending = null; return ok({ status: 'ok', bytesWritten: 0 }); },
      controlTransferIn: () => {
        const b = new Uint8Array(16);
        b.set(le(lastStatus), 4);
        return ok({ status: 'ok', data: new DataView(b.buffer) });
      },
      transferOut(ep, data) {
        const d = new Uint8Array(data.buffer || data, data.byteOffset || 0, data.byteLength);
        if (pending && pending.phase === 'data') {
          const { addr } = pending;
          for (let i = 0; i < d.length; i++) state.rpFlash[offset(addr) + i] &= d[i];
          pending.phase = 'ack';
          return ok({ status: 'ok', bytesWritten: d.length });
        }
        if (pending && pending.phase === 'ack-out') { pending = null; return ok({ status: 'ok', bytesWritten: 0 }); }
        if (d.length !== 32 || u32(d, 0) !== 0x431fd10b) return ok({ status: 'stall', bytesWritten: 0 });
        const id = d[8];
        const a0 = u32(d, 16);
        const a1 = u32(d, 20);
        const len = u32(d, 12);
        state.rpCmds.push(id);
        lastStatus = 0;
        if ((id === 0x03 || id === 0x05 || id === 0x84) && !inRange(a0, a1)) {
          lastStatus = 4;
          return ok({ status: 'stall', bytesWritten: 0 });
        }
        if (id === 0x03) {
          state.rpFlash.fill(0xff, offset(a0), offset(a0) + a1);
          pending = { phase: 'ack' };
        } else if (id === 0x05) pending = { phase: 'data', addr: a0 };
        else if (id === 0x84) pending = { phase: 'in', data: state.rpFlash.slice(offset(a0), offset(a0) + a1) };
        else if (id === 0x8b) {
          const b = new Uint8Array(len);
          b.set([...le(3), ...le(0xff), ...le(0)], 0);
          pending = { phase: 'in', data: b };
        } else if (id === 0x0a) { pending = { phase: 'ack' }; state.rebooted = true; } else pending = { phase: 'ack' };
        return ok({ status: 'ok', bytesWritten: 32 });
      },
      transferIn(ep, len) {
        if (pending && pending.phase === 'in') {
          const data = pending.data;
          pending = { phase: 'ack-out' };
          return ok({ status: 'ok', data: new DataView(data.buffer, data.byteOffset, data.byteLength) });
        }
        pending = null;
        return ok({ status: 'ok', data: new DataView(new ArrayBuffer(0)) });
      },
    };
    return dev;
  }
  const rom = bootrom();
  Object.defineProperty(navigator, 'usb', { configurable: true, value: {
    async getDevices() { return []; },
    async requestDevice() {
      if (!state.bootsel) throw new DOMException('No device selected.', 'NotFoundError');
      return rom;
    },
    addEventListener() {}, removeEventListener() {},
  } });
})();
