// Captures the web pages a video shows, as PNG files of 1280 x 800 CSS
// pixels at twice that, into ../stills/, with the places of the controls
// a pointer goes to in ../stills/web.json.
//
//   node tools/video/web/capture.js
//
// github-*: the releases page of subtilitas/rcbench and the assets of its
// newest release.
// esptool-*: the ESP Web Tool at esptool.spacehuhn.com, the page as it is
// served, driven through Connect, a file, Program and its question.  No board is on
// this computer: the page's serial library, esptool-js, is replaced by a
// stand-in that answers as an ESP32-P4 and reports a write's progress, and
// navigator.serial hands out a port object.  Every pixel is drawn by the
// page's own code.
//
// SPDX-License-Identifier: MIT
const fs = require('fs');
const path = require('path');
const {chromium} = require('playwright');

const OUT = path.join(__dirname, '..', 'stills');
const IMAGE = 'rcbench-panel-merged.bin';
const spots = {};

const STAND_IN = `
  const wait = ms => new Promise(r => setTimeout(r, ms));
  export class Transport {
    constructor(port) { this.port = port; }
    setDeviceLostCallback() {}
    async disconnect() {}
  }
  export class ESPLoader {
    constructor(options) {
      this.transport = options.transport;
      this.terminal = options.terminal;
      this.chip = {CHIP_NAME: 'ESP32-P4'};
    }
    async main() { await wait(300); return 'ESP32-P4'; }
    async writeFlash(options) {
      const total = options.fileArray[0].data.length;
      for (let k = 0; k <= 10; k++) {
        await new Promise(r => { window.__step = r; });
        options.reportProgress(0, total * k / 10, total);
      }
    }
  }`;

const centre = async (page, name, locator) => {
  const b = await locator.boundingBox();
  spots[name] = [Math.round(b.x + b.width / 2), Math.round(b.y + b.height / 2)];
};

(async () => {
  const browser = await chromium.launch();
  const context = await browser.newContext(
    {viewport: {width: 1280, height: 800}, deviceScaleFactor: 2});
  const shot = (page, name) => page.screenshot(
    {path: path.join(OUT, `web-${name}.png`)});

  // The releases page and the newest release's assets.
  let page = await context.newPage();
  await page.goto('https://github.com/subtilitas/rcbench/releases',
                  {waitUntil: 'networkidle'});
  await shot(page, 'github-releases');
  const tag = await page.locator('a[href*="/releases/tag/"]').first()
    .getAttribute('href');
  await page.goto('https://github.com' + tag, {waitUntil: 'networkidle'});
  const assets = page.locator('a[href*="/releases/download/"]');
  await assets.first().waitFor();
  // The list of assets, its heading near the top of the picture.
  await page.setViewportSize({width: 1280, height: 800});
  await assets.first().evaluate(e => window.scrollTo(
    0, e.getBoundingClientRect().top + window.scrollY - 110));
  await page.waitForTimeout(400);
  await shot(page, 'github-assets');
  spots.version = tag.split('/').pop();
  spots.assets = [];
  for (const a of await assets.all()) {
    const name = (await a.getAttribute('href')).split('/').pop();
    spots.assets.push(name);
    await centre(page, 'github:' + name, a);
  }
  await page.close();

  // The ESP Web Tool with the stand-in for its serial library.
  page = await context.newPage();
  await page.addInitScript(() => {
    Object.defineProperty(navigator, 'serial', {value: {
      requestPort: async () => ({}), getPorts: async () => [],
      addEventListener() {}, removeEventListener() {}}});
  });
  await page.route('**/esptool-js@*/bundle.js', route => route.fulfill(
    {contentType: 'text/javascript', body: STAND_IN}));
  await page.goto('https://esptool.spacehuhn.com/',
                  {waitUntil: 'networkidle'});
  const connect = page.getByRole('button', {name: /^connect$/i});
  await centre(page, 'esptool:connect', connect);
  await shot(page, 'esptool-home');
  await connect.click();
  const program = page.getByRole('button', {name: /program/i});
  await program.waitFor();
  await page.waitForTimeout(3800);          // the toast "Connected" leaves
  await shot(page, 'esptool-connected');
  const input = page.locator('input[type=file]').first();
  await centre(page, 'esptool:file', page.locator('label:has(input[type=file])').first());
  await input.setInputFiles({name: IMAGE, mimeType: 'application/octet-stream',
                             buffer: Buffer.alloc(4096, 0xff)});
  await page.waitForTimeout(500);
  await centre(page, 'esptool:program', program);
  await shot(page, 'esptool-file');
  await program.click();
  // The page asks before it writes.
  const go = page.getByRole('button', {name: /continue/i});
  await go.waitFor();
  await page.waitForTimeout(400);
  await centre(page, 'esptool:continue', go);
  await shot(page, 'esptool-confirm');
  await go.click();
  for (let k = 0; k <= 10; k++) {
    await page.waitForFunction(() => window.__step);
    await page.evaluate(() => { const r = window.__step; window.__step = null; r(); });
    await page.waitForTimeout(250);
    if (k % 2 === 0) await shot(page, `esptool-write-${k * 10}`);
  }
  await page.waitForTimeout(700);
  await shot(page, 'esptool-done');
  await browser.close();
  fs.writeFileSync(path.join(OUT, 'web.json'),
                   JSON.stringify(spots, null, 1) + '\n');
  console.log(JSON.stringify(spots));
})().catch(e => { console.error(e); process.exit(1); });
