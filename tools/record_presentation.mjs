// Renders docs/present.html to an mp4 deterministically (virtual time, 30 fps, no audio). Needs: npm i puppeteer-core, Chrome, ffmpeg, and 'cd docs && python3 -m http.server 8765'.
// usage: node tools/record_presentation.mjs [max seconds] [out.mp4]
import puppeteer from 'puppeteer-core';
import { spawn } from 'node:child_process';
const FPS = 30, MAXSEC = +process.argv[2] || 200, OUT = process.argv[3] || 'submission/traceback_presentation.mp4';
const b = await puppeteer.launch({ executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', headless: 'new', defaultViewport: { width: 1920, height: 1080, deviceScaleFactor: 1 }, args: ['--hide-scrollbars', '--disable-gpu', '--run-all-compositor-stages-before-draw', '--disable-new-content-rendering-timeout', '--disable-threaded-animation', '--disable-threaded-scrolling'] });
const p = await b.newPage();
const c = await p.createCDPSession();
await p.goto(process.env.PRES_URL || 'http://localhost:8765/present.html?clean', { waitUntil: 'load' });
await p.evaluate(() => document.fonts.ready);
await new Promise(r => setTimeout(r, 800));
const ff = spawn('/opt/homebrew/bin/ffmpeg', ['-y', '-f', 'image2pipe', '-framerate', String(FPS), '-c:v', 'mjpeg', '-i', '-', '-c:v', 'libx264', '-crf', '17', '-pix_fmt', 'yuv420p', '-movflags', '+faststart', OUT], { stdio: ['pipe', 'ignore', 'inherit'] });
await c.send('Emulation.setVirtualTimePolicy', { policy: 'pause' });
await p.evaluate(() => { document.getElementById('play').click(); });
const step = (ms) => new Promise(res => { c.once('Emulation.virtualTimeBudgetExpired', res); c.send('Emulation.setVirtualTimePolicy', { policy: 'advance', budget: ms }); });
let n = 0, endAt = -1;
for (; n < MAXSEC * FPS; n++) {
  await step(1000 / FPS);
  const { data } = await c.send('Page.captureScreenshot', { format: 'jpeg', quality: 95 });
  ff.stdin.write(Buffer.from(data, 'base64'));
  if (n % 30 === 0) {
    const done = await p.evaluate(() => document.getElementById('play').textContent.includes('Play'));
    if (done && n > 60) { if (endAt < 0) endAt = n; }
    if (endAt >= 0 && n - endAt > 75) break;
    if (n % 300 === 0) console.log('t=', n / FPS);
  }
}
{ const st = await p.evaluate(() => window.__starts); console.log('STARTS', JSON.stringify(st.map(x => +((x - st[0]) / 1000).toFixed(2)))); }
ff.stdin.end(); await new Promise(r => ff.on('close', r));
console.log('frames', n, 'seconds', n / FPS);
await b.close();
