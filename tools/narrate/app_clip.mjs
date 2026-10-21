// Records the real app (docs/index.html) deterministically (virtual time, 30 fps, no audio) while a visible cursor performs the actions in a timeline.json from build_app.py.
// Needs: npm i puppeteer-core, Chrome, ffmpeg, and 'cd docs && python3 -m http.server 8765'. usage: node app_clip.mjs <timeline.json> <out.mp4>
import puppeteer from 'puppeteer-core';
import { spawn } from 'node:child_process';
import { readFileSync } from 'node:fs';
const FPS = 30, TL = JSON.parse(readFileSync(process.argv[2], 'utf8')), OUT = process.argv[3];
const b = await puppeteer.launch({ executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', headless: 'new', defaultViewport: { width: 1920, height: 1080, deviceScaleFactor: 1 }, args: ['--hide-scrollbars', '--disable-gpu', '--run-all-compositor-stages-before-draw', '--disable-new-content-rendering-timeout', '--disable-threaded-animation', '--disable-threaded-scrolling'] });
const p = await b.newPage();
const c = await p.createCDPSession();
await p.evaluateOnNewDocument(() => {
  window.__d = {
    cur: null, x: 1500, y: 900,
    init() {
      if (this.cur) return;
      const s = document.createElement('style'); s.textContent = '#fakecur{position:fixed;left:0;top:0;width:30px;height:30px;margin:-15px 0 0 -15px;border-radius:50%;border:3px solid #2dd4bf;background:rgba(45,212,191,.22);box-shadow:0 0 22px rgba(45,212,191,.7);z-index:99999;pointer-events:none}.fakeping{position:fixed;width:30px;height:30px;margin:-15px 0 0 -15px;border-radius:50%;border:3px solid #2dd4bf;z-index:99998;pointer-events:none;animation:fping .6s ease-out forwards}@keyframes fping{to{transform:scale(3);opacity:0}}';
      document.head.appendChild(s); this.cur = document.createElement('div'); this.cur.id = 'fakecur'; document.body.appendChild(this.cur); this.place(this.x, this.y);
    },
    place(x, y) { this.x = x; this.y = y; this.cur.style.transform = `translate(${x}px,${y}px)`; },
    ease: k => (k < .5 ? 4 * k * k * k : 1 - Math.pow(-2 * k + 2, 3) / 2),
    anim(ms, f) { return new Promise(res => { const t0 = performance.now(); (function s() { const k = Math.min(1, (performance.now() - t0) / ms); f(window.__d.ease(k)); k < 1 ? requestAnimationFrame(s) : res(); })(); }); },
    find(spec) {
      if (/^[#.\[]/.test(spec)) return document.querySelector(spec);
      const txt = spec.replace(/^text=/, '');
      return [...document.querySelectorAll('button,a,summary,[role=tab],.chip,code,li,span')].filter(e => e.offsetParent && e.textContent.trim().includes(txt)).sort((a, b) => a.textContent.length - b.textContent.length)[0];
    },
    async scroll(y, ms = 900) { const y0 = scrollY; await this.anim(ms, e => scrollTo(0, y0 + (y - y0) * e)); },
    async click(spec) {
      this.init(); const el = this.find(spec); if (!el) { console.log('MISSING ' + spec); return; }
      let r = el.getBoundingClientRect();
      if (r.top < 90 || r.bottom > innerHeight - 70) { const y0 = scrollY, to = Math.max(0, y0 + r.top - innerHeight / 2); await this.anim(700, e => scrollTo(0, y0 + (to - y0) * e)); r = el.getBoundingClientRect(); }
      const x0 = this.x, y0 = this.y, x1 = r.left + Math.min(r.width / 2, 60), y1 = r.top + r.height / 2;
      await this.anim(650, e => this.place(x0 + (x1 - x0) * e, y0 + (y1 - y0) * e));
      const ping = document.createElement('div'); ping.className = 'fakeping'; ping.style.left = x1 + 'px'; ping.style.top = y1 + 'px'; document.body.appendChild(ping); setTimeout(() => ping.remove(), 700);
      await new Promise(r => setTimeout(r, 180)); el.click();
    },
  };
});
await p.goto('http://localhost:8765/index.html', { waitUntil: 'networkidle0' });
await p.evaluate(() => document.fonts.ready);
await p.evaluate(() => window.__d.init());
await new Promise(r => setTimeout(r, 600));
const ff = spawn('/opt/homebrew/bin/ffmpeg', ['-y', '-f', 'image2pipe', '-framerate', String(FPS), '-c:v', 'mjpeg', '-i', '-', '-c:v', 'libx264', '-crf', '17', '-pix_fmt', 'yuv420p', '-movflags', '+faststart', OUT], { stdio: ['pipe', 'ignore', 'inherit'] });
await c.send('Emulation.setVirtualTimePolicy', { policy: 'pause' });
const step = (ms) => new Promise(res => { c.once('Emulation.virtualTimeBudgetExpired', res); c.send('Emulation.setVirtualTimePolicy', { policy: 'advance', budget: ms }); });
const acts = [...TL.actions].sort((a, b) => a.t - b.t); let ai = 0;
for (let n = 0; n < TL.length * FPS; n++) {
  const t = n / FPS;
  while (ai < acts.length && acts[ai].t <= t) { const a = acts[ai++]; await p.evaluate((a) => { a.do === 'scroll' ? window.__d.scroll(+a.target, a.ms || 1400) : window.__d.click(a.target); }, a); }
  await step(1000 / FPS);
  const { data } = await c.send('Page.captureScreenshot', { format: 'jpeg', quality: 95 });
  ff.stdin.write(Buffer.from(data, 'base64'));
  if (n % 300 === 0) console.log('t=', t);
}
ff.stdin.end(); await new Promise(r => ff.on('close', r));
await b.close();
