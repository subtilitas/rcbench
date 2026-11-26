// Capture the player's 2D layer (captions, chips, labels, rings, end card)
// with a transparent background, one PNG per frame: OUT/NNNNN.png, NNNNN =
// frame index + 1 (Blender's numbering).  "bg" mode writes the background
// alone (#bg and #grid) to OUT.
//   node overlay_capture.js URL frames OUT FIRST END     (FIRST..END-1, 0-based)
//   node overlay_capture.js URL bg OUT.png
//   node overlay_capture.js URL list OUT I,I,...         (0-based frame indices)
const {chromium} = require('playwright');
const path = require('path');
const fs = require('fs');
(async () => {
  const [url, mode, out, first, end] = process.argv.slice(2);
  const browser = await chromium.launch({args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist', '--enable-webgl']});
  const page = await browser.newPage({viewport: {width: 1920, height: 1080}, deviceScaleFactor: 1});
  page.on('pageerror', e => console.error('pageerror', e.message));
  await page.goto(url);
  await page.waitForFunction('window.READY === true', null, {timeout: 150000});
  if (mode === 'bg') {
    await page.addStyleTag({content: 'body>*:not(#bg):not(#grid){display:none!important}#bg,#grid{display:block!important}html,body{background:#070b14!important}'});
    await page.screenshot({path: out});
  } else {
    fs.mkdirSync(out, {recursive: true});
    const todo = mode === 'list' ? first.split(',').map(Number)
      : Array.from({length: +end - +first}, (_, k) => +first + k);
    for (const i of todo) {
      const dst = path.join(out, String(i + 1).padStart(5, '0') + '.png');
      if (fs.existsSync(dst) && fs.statSync(dst).size > 0) continue;
      await page.evaluate(x => window.renderAt(x), i / 30);
      await page.screenshot({path: dst + '.tmp', type: 'png', omitBackground: true});
      fs.renameSync(dst + '.tmp', dst);
    }
  }
  await browser.close();
})();
// SPDX-License-Identifier: MIT
