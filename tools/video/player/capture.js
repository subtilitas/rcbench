// Captures the player's frames with Playwright's Chromium.
//
//   node capture.js <url> video <out.mp4> <fps> [<first> <end>]
//       frames piped to ffmpeg; first and end bound a part of the video
//   node capture.js <url> stills <dir> <t,t,...>  one PNG per time in s
//
// SPDX-License-Identifier: MIT
const {chromium} = require('playwright');
const {spawn} = require('child_process');
const path = require('path');

(async () => {
  const [url, mode, out, arg, first, end] = process.argv.slice(2);
  // WebGL runs on SwiftShader, a software rasteriser, unless
  // RCBENCH_VIDEO_GL holds Chromium flags of its own, separated by
  // spaces: "--use-angle=vulkan --enable-features=Vulkan" for a GPU
  // (graphics processing unit) with a Vulkan driver.
  const gl = process.env.RCBENCH_VIDEO_GL
    ? process.env.RCBENCH_VIDEO_GL.split(/\s+/).filter(Boolean)
    : ['--use-angle=swiftshader', '--enable-unsafe-swiftshader'];
  const browser = await chromium.launch({args: [
    ...gl, '--ignore-gpu-blocklist', '--enable-webgl']});
  const page0 = await browser.newPage();
  console.log('WebGL: ' + await page0.evaluate(() => {
    const c = document.createElement('canvas').getContext('webgl2');
    const e = c && c.getExtension('WEBGL_debug_renderer_info');
    return e ? c.getParameter(e.UNMASKED_RENDERER_WEBGL) : 'none';
  }));
  await page0.close();
  const page = await browser.newPage(
    {viewport: {width: 1920, height: 1080}, deviceScaleFactor: 1});
  let failed = false;
  page.on('pageerror', e => { failed = true; console.error(e.message); });
  page.on('requestfailed', r => console.error('failed', r.url()));
  await page.goto(url);
  await page.waitForFunction('window.READY === true || false', null,
                             {timeout: 600000}).catch(() => { failed = true; });
  if (failed || !(await page.evaluate('window.READY === true'))) {
    console.error('the player did not load the storyboard');
    await browser.close();
    process.exit(1);
  }
  const total = await page.evaluate('window.TOTAL');
  if (mode === 'stills') {
    for (const t of arg.split(',')) {
      await page.evaluate(x => window.renderAt(x), +t);
      await page.screenshot({path: path.join(out, `still-${t}.png`)});
    }
  } else {
    const fps = +arg || 30, n = Math.round(total * fps);
    const ff = spawn('ffmpeg', ['-v', 'error', '-y', '-f', 'image2pipe',
      '-framerate', String(fps), '-c:v', 'mjpeg', '-i', '-',
      '-c:v', 'libx264', '-preset', 'medium', '-crf', '17',
      '-pix_fmt', 'yuv420p', '-movflags', '+faststart', out],
      {stdio: ['pipe', 'inherit', 'inherit']});
    const last = end === undefined ? n : Math.min(n, +end);
    for (let i = +(first || 0); i < last; i++) {
      await page.evaluate(x => window.renderAt(x), i / fps);
      const buf = await page.screenshot({type: 'jpeg', quality: 96});
      if (!ff.stdin.write(buf)) {
        await new Promise(r => ff.stdin.once('drain', r));
      }
      if (i % 600 === 0) console.log(`frame ${i} of ${n}`);
    }
    ff.stdin.end();
    const code = await new Promise(r => ff.on('close', r));
    if (code !== 0) process.exitCode = 1;
  }
  await browser.close();
})();
