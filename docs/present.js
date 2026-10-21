'use strict';
// Presentation: every scene is an async function that builds its DOM and reveals it on a timeline. All incident figures come from present-data.js (generated from docs/data/*.json).
const P = PD['inc-026'], Q = PD['inc-003'];
const stage = document.getElementById('stage');
let tok = 0, cur = -1, playing = false;
const URLQ = new URLSearchParams(location.search), HOLD = URLQ.has('hold') ? +URLQ.get('hold') : 2200, MINDUR = (URLQ.get('dur') || '').split(',').map(Number), ONLY = URLQ.has('only');  // ?only plays just the starting scene (#N); ?dur=s,s,... floors each scene's length (seconds) so narration fits; ?hold=ms between scenes
const esc = s => String(s).replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
const lab = s => String(s).replace(/_/g, ' ');
const pct = (x, d = 1) => (x * 100).toFixed(d) + '%';
class Stop extends Error {}
const sleep = ms => { const my = tok; return new Promise((ok, no) => setTimeout(() => (my === tok ? ok() : no(new Stop())), ms)); };
const rv = (sc, sel, delay = 0) => { const els = typeof sel === 'string' ? [...sc.querySelectorAll(sel)] : [sel]; els.forEach((e, i) => setTimeout(() => e.classList.add('in'), delay + i * 160)); return els.length; };
async function type(el, text, cps = 70) { const my = tok; for (let i = 1; i <= text.length; i++) { if (my !== tok) return; el.innerHTML = esc(text.slice(0, i)) + '<span class="caret"></span>'; if (i % 3 === 0) await sleep(1000 / cps * 3); } el.innerHTML = esc(text); }
function count(el, to, dec = 0, ms = 1400, suffix = '') { const my = tok, t0 = performance.now(); (function f() { if (my !== tok) return; const k = Math.max(0, Math.min(1, (performance.now() - t0) / ms)), e = 1 - Math.pow(1 - k, 3); el.textContent = (to * e).toFixed(dec) + suffix; if (k < 1) requestAnimationFrame(f); })(); }
const rate = m => (m.req ? m.err / m.req : 0);
function chartPath(ser, W, H, ymax, tmin, tmax) {
  const x = t => 20 + (t - tmin) / (tmax - tmin) * (W - 40), y = v => H - 20 - Math.min(1, v / ymax) * (H - 40);
  return { d: ser.map((m, i) => (i ? 'L' : 'M') + x(m.t).toFixed(1) + ' ' + y(rate(m)).toFixed(1)).join(' '), x, y };
}

const scenes = [
  { name: 'Open', dur: 7, async run(sc) {
    sc.classList.add('title');
    sc.innerHTML = `<svg class="logo rv z" viewBox="0 0 32 32"><rect width="32" height="32" rx="7" fill="#2dd4bf"/><path d="M7 20l5-8 4 6 5-9 4 7" fill="none" stroke="#04201d" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"/></svg>
      <h1 class="rv">Traceback</h1><div class="tag rv" style="max-width:1650px">An incident investigator whose answers <b>you can check</b>, not just trust.</div>
      <div class="chip hi rv" style="margin-top:44px;font-size:24px">Nemotron on Nebius Token Factory · Best Apps and Agents</div>
      <svg class="pulse" viewBox="0 0 1920 200" preserveAspectRatio="none"><path pathLength="1" d="M0 120 L420 120 L470 118 L520 124 L600 120 L660 40 L720 170 L790 20 L860 150 L930 110 L1100 118 L1400 120 L1920 119"/></svg>`;
    await sleep(300); rv(sc, '.logo'); await sleep(400); rv(sc, 'h1'); await sleep(500); rv(sc, '.tag'); sc.querySelector('.pulse').classList.add('go'); await sleep(900); rv(sc, '.chip'); await sleep(3200);
  } },
  { name: 'The outage', dur: 16, async run(sc) {
    const svcs = Object.keys(P.series), all = svcs.flatMap(s => P.series[s].map(m => m.t)), tmin = Math.min(...all), tmax = Math.max(...all);
    const gw = P.series.gateway, ymax = Math.max(0.05, ...gw.map(rate)), c = chartPath(gw, 1000, 420, ymax, tmin, tmax);
    const pk = Object.fromEntries(svcs.map(s => [s, Math.max(0, ...P.series[s].map(rate))])), mx = Math.max(...Object.values(pk)), loud = svcs.find(s => pk[s] === mx);
    const sy = P.symptom, ox = c.x(tmin + (sy.onset - 0));
    sc.innerHTML = `<div class="kicker rv">A simulated outage</div><h2 class="rv">${sy.failed} of ${sy.traces.toLocaleString()} checkout requests are failing.</h2>
      <div class="row mt grow"><div class="card grow rv l" style="flex:1.5"><div class="mono" style="color:var(--mut);font-size:22px;margin-bottom:8px">entry failure rate · 5 s buckets</div>
        <svg viewBox="0 0 1000 420" width="100%"><g stroke="#223432"><line x1="20" x2="980" y1="400" y2="400"/><line x1="20" x2="980" y1="210" y2="210" stroke-dasharray="4 8"/></g>
        <line x1="${ox}" x2="${ox}" y1="10" y2="400" stroke="#fbbf24" stroke-dasharray="6 6"/><text x="${ox - 12}" y="34" text-anchor="end" fill="#fbbf24" font-size="20">failures begin</text>
        <path class="draw" pathLength="1" d="${c.d}" fill="none" stroke="#f87171" stroke-width="5" stroke-linejoin="round" stroke-linecap="round"/></svg>
        <div class="mono" style="color:var(--mut);font-size:21px;margin-top:6px">${P.counts.logs.toLocaleString()} log lines · ${P.counts.spans.toLocaleString()} spans · ${svcs.length} services</div></div>
        <div class="col rv r" style="flex:1">${svcs.map(s => `<div class="svc" data-s="${s}"><b>${s}</b><div class="meter"><i data-w="${mx ? pk[s] / mx * 100 : 0}"></i></div><span class="pk">${pct(pk[s], 0)} 5xx</span></div>`).join('')}</div></div>
      <div class="q rv">The loudest dashboard says <span class="badc">${loud}</span>. Where would you look?</div>`;
    rv(sc, '.kicker'); rv(sc, 'h2', 150); await sleep(700); rv(sc, '.card'); sc.querySelector('.draw').classList.add('go'); await sleep(900); rv(sc, '.col.r');
    await sleep(500); sc.querySelectorAll('.meter i').forEach(i => (i.style.width = i.dataset.w + '%')); await sleep(1700);
    sc.querySelector(`.svc[data-s="${loud}"]`).classList.add('loud'); await sleep(500); rv(sc, '.q'); await sleep(3600);
  } },
  { name: 'The obvious tool', dur: 20, async run(sc) {
    const b = P.baseline, r = b.root, t = P.truth;
    sc.innerHTML = `<div class="kicker rv">The obvious tool</div><h2 class="rv">Paste the logs into the same model.</h2>
      <div class="row mt grow"><div class="term rv l" style="flex:1.3"><div class="tb">raw logs → nvidia/nemotron-3-super-120b-a12b</div><pre id="tp"></pre></div>
      <div class="col rv r" style="flex:1"><div class="mono" style="color:var(--mut);font-size:22px">The model's answer</div>
        <div class="card" style="margin-top:12px"><div class="mono" style="color:var(--mut);font-size:22px">service</div><div style="font-size:64px;font-weight:800" id="bs">…</div><div style="margin-top:8px">${esc(lab(r.category))}</div><div class="chip warn" style="margin-top:14px;font-size:26px" id="bc" hidden>confidence: ${esc(b.confidence)}</div></div>
        <div class="verdict" id="vd"></div></div></div>`;
    rv(sc, '.kicker'); rv(sc, 'h2', 150); await sleep(600); rv(sc, '.term'); rv(sc, '.col.r', 300); await sleep(700);
    const ci = b.raw.indexOf('"causal_chain"'), body = ci > 0 ? b.raw.slice(0, ci) + '"causal_chain": [ … the reply continues … ]\n}' : b.raw.slice(0, 420);
    await type(sc.querySelector('#tp'), body, 90); sc.querySelector('#bs').textContent = r.service; const bc = sc.querySelector('#bc'); bc.hidden = false; await sleep(1200);
    sc.querySelector('#vd').innerHTML = `<div class="stamp ${b.correct ? 'ok' : 'bad'} rv z" id="st">${b.correct ? 'CORRECT' : 'WRONG'}</div><div class="rv" id="tr" style="font-size:30px">The simulator knows the truth:<br><b>${esc(t.service)}</b> · ${esc(lab(t.type))}</div>`;
    await sleep(100); rv(sc, '#st'); await sleep(700); rv(sc, '#tr'); await sleep(3800);
  } },
  { name: 'How Traceback works', dur: 16, async run(sc) {
    const st = [['Clock alignment', 'Each service’s clock offset, from causal span pairs.'], ['Correlation', 'Spans become traces; the call graph is derived.'], ['Ranked candidates', 'Deterministic rules, each tied to evidence IDs.'], ['Model explains', 'Nemotron ranks and explains the candidates only.'], ['Citation check', 'Every cited record must exist, or it is asked again.'], ['Counterfactual replay', 'Re-run with the suspected cause switched off.']];
    sc.innerHTML = `<div class="kicker rv">Traceback</div><h2 class="rv">Measure first. Let the model explain second.</h2><div class="sub rv">The model never sees the raw firehose, only candidates and the records behind them.</div>
      <div class="pipe">${st.map((s, i) => `${i ? '<div class="arrow">›</div>' : ''}<div class="node rv"><div class="n">0${i + 1}</div>${i === 3 ? '<span class="chip hi llm">LLM</span>' : ''}<h3>${s[0]}</h3><p>${s[1]}</p></div>`).join('')}</div>
      <div class="note rv">Stages 1, 2, 3, 5 and 6 use no model at all.</div>`;
    rv(sc, '.kicker'); rv(sc, 'h2', 150); rv(sc, '.sub', 300); await sleep(900);
    const nodes = [...sc.querySelectorAll('.node')], ars = [...sc.querySelectorAll('.arrow')];
    for (let i = 0; i < nodes.length; i++) { nodes[i].classList.add('in', 'lit'); if (ars[i - 1]) ars[i - 1].classList.add('lit'); await sleep(1250); }
    rv(sc, '.note'); await sleep(2600);
  } },
  { name: 'The evidence', dur: 18, async run(sc) {
    const A = P.traceback, ch = A.chain.slice(0, 3), hy = P.hypotheses.slice(0, 4);
    sc.innerHTML = `<div class="kicker rv">Same outage, Traceback’s answer</div><h2 class="rv">${esc(A.root.service)} · ${esc(lab(A.root.category))}. Every claim cites a record.</h2>
      <div class="row mt grow"><div class="col rv l" style="flex:.9"><div class="mono" style="color:var(--mut);font-size:22px;margin-bottom:16px">ranked candidates (deterministic)</div>
        ${hy.map(h => `<div class="hy"><div class="lab"><span>${h.rank}. ${esc(h.service)} · ${esc(lab(h.category))}</span><span>${h.score.toFixed(2)}</span></div><div class="bar"><i data-w="${h.score * 100 / hy[0].score}"></i></div></div>`).join('')}</div>
        <div class="col" style="flex:1.2">${ch.map(s => { const id = s.cites[0], rec = P.records[id] || ''; return `<div class="step rv r"><div class="cl">${esc(s.claim)}</div><div>${s.cites.map(x => `<span class="chip cite">${esc(x)}</span>`).join('')}</div><div class="rec">${esc(rec.slice(0, 150))}${rec.length > 150 ? '…' : ''}</div></div>`; }).join('')}</div></div>`;
    rv(sc, '.kicker'); rv(sc, 'h2', 150); await sleep(700); rv(sc, '.col.l'); await sleep(500); sc.querySelectorAll('.hy .bar i').forEach(i => (i.style.width = i.dataset.w + '%')); await sleep(1500);
    for (const s of sc.querySelectorAll('.step')) { s.classList.add('in'); await sleep(1800); } await sleep(2600);
  } },
  { name: 'The replay', dur: 17, async run(sc) {
    const A = P.traceback, v = A.verification, wrong = Object.entries(P.replays).filter(([k, x]) => x.status !== 'passed').slice(0, 2);
    sc.innerHTML = `<div class="kicker rv">Counterfactual replay</div><h2 class="rv">Switch the cause off. Do the failures vanish?</h2>
      <div class="row mt grow"><div class="card rv l" style="flex:1.3"><div class="mono" style="color:var(--accent);font-size:24px">${esc(A.root.category)}:${esc(A.root.service)}</div>
        <div class="rbar"><div class="lab"><span>cause on (observed)</span><span id="a">${pct(v.rate_observed)}</span></div><div class="track"><i style="background:var(--bad)" data-w="${Math.min(100, v.rate_observed * 100 / Math.max(v.rate_observed, 0.0001))}"></i></div></div>
        <div class="rbar"><div class="lab"><span>cause switched off (${esc(v.matched.join(', '))})</span><span id="d">${pct(v.rate_disabled)}</span></div><div class="track"><i style="background:var(--ok)" data-w="${Math.max(2, v.rate_disabled * 100 / Math.max(v.rate_observed, 0.0001))}"></i></div></div>
        <div class="verdict"><div class="stamp ok rv z" id="ps">${esc(v.status.toUpperCase())}</div><div style="font-size:28px">${esc(v.reason)}</div></div></div>
        <div class="col rv r" style="flex:1"><div class="mono" style="color:var(--mut);font-size:22px;margin-bottom:14px">the other candidates</div>
          ${wrong.map(([k, x]) => `<div class="card" style="margin-bottom:16px"><div class="mono" style="font-size:23px">${esc(k)}</div><span class="chip warn">${esc(x.status)}</span></div>`).join('')}
          <div class="card" style="margin-top:6px"><div class="mono" style="color:var(--mut);font-size:22px">final confidence</div><div style="font-size:54px;font-weight:800" class="good">${esc(A.confidence.toUpperCase())}</div><div class="sub" style="font-size:24px">An answer the replay can’t reproduce can’t say high.</div></div></div></div>
      <div class="note rv">The replay needs the simulator, so on real telemetry it can’t run.</div>`;
    rv(sc, '.kicker'); rv(sc, 'h2', 150); await sleep(800); rv(sc, '.card'); rv(sc, '.col.r', 200); await sleep(900);
    const tr = sc.querySelectorAll('.track i'); tr[0].style.width = tr[0].dataset.w + '%'; await sleep(1300); tr[1].style.width = tr[1].dataset.w + '%'; await sleep(1900); rv(sc, '#ps'); rv(sc, '.note', 300); await sleep(4200);
  } },
  { name: 'When it is wrong', dur: 17, async run(sc) {
    const A = Q.traceback, an = Q.hypotheses[0], pass = Object.entries(Q.replays).find(([k, x]) => x.status === 'passed');
    sc.innerHTML = `<div class="kicker rv">It is not always right</div><h2 class="rv">Here Traceback is wrong, and says so.</h2>
      <div class="row mt"><div class="card rv l" style="flex:1"><div class="mono" style="color:var(--mut);font-size:22px">what the simulator injected</div><div style="font-size:54px;font-weight:800">${esc(Q.truth.service)}</div><div>${esc(lab(Q.truth.type))}</div></div>
        <div class="card rv" style="flex:1"><div class="mono" style="color:var(--mut);font-size:22px">the analyzer’s first candidate</div><div style="font-size:54px;font-weight:800" class="good">${esc(an.service)}</div><div>${esc(lab(an.category))} · replay <b>${pass ? esc(pass[1].status) : '—'}</b></div></div>
        <div class="card rv r" style="flex:1"><div class="mono" style="color:var(--mut);font-size:22px">what the model chose</div><div style="font-size:54px;font-weight:800" class="badc">${esc(A.root.service)}</div><div>${esc(lab(A.root.category))} · replay <b>${esc(A.verification.status)}</b></div></div></div>
      <div class="verdict"><div class="stamp warn rv z" id="sw">CONFIDENCE: ${esc(A.confidence.toUpperCase())}</div><div class="rv" id="sl" style="font-size:32px;max-width:1000px">The replay couldn’t confirm the model’s pick, so it never reached high. <b>It tells you when not to trust it.</b></div></div>`;
    rv(sc, '.kicker'); rv(sc, 'h2', 150); await sleep(900); rv(sc, '.card'); await sleep(3200); rv(sc, '#sw'); await sleep(900); rv(sc, '#sl'); await sleep(4600);
  } },
  { name: 'Simulator scoreboard', dur: 14, async run(sc) {
    const S = [['95.8', 'Traceback finds the cause', '68.8', 'raw-logs baseline', 1, '%'], ['0', 'confidently wrong (Traceback)', '29.2', 'baseline', 1, '%']];
    sc.innerHTML = `<div class="kicker rv">48 fresh, locked simulated incidents</div><h2 class="rv">On the simulators, calibration is the difference.</h2>
      <div class="row" style="margin-top:90px">${S.map(s => `<div class="stat rv"><div class="num ${s[0] === '0' ? 'good' : 'good'}" data-to="${s[0]}" data-d="${s[0] === '0' ? 0 : 1}">0</div><div class="cap">${s[1]}</div><div class="vs" style="margin-top:28px">vs <b style="color:var(--bad)" data-to2="${s[2]}">${s[2]}${s[5]}</b> ${s[3]}</div></div>`).join('')}
        <div class="stat rv"><div class="num good">~1/10</div><div class="cap">the model cost per incident</div><div class="vs" style="margin-top:28px">vs the raw-logs baseline</div></div></div>
      <div class="note rv">Simulators we wrote, with faults we injected. This shows the method, not production accuracy.</div>`;
    rv(sc, '.kicker'); rv(sc, 'h2', 150); await sleep(800); rv(sc, '.stat'); await sleep(500);
    sc.querySelectorAll('.num[data-to]').forEach(e => count(e, +e.dataset.to, +e.dataset.d, 1500, '%')); await sleep(1900); rv(sc, '.note'); await sleep(4200);
  } },
  { name: 'Real telemetry', dur: 24, async run(sc) {
    const rows = [['Code faults, unseen systems (60 cases)', 'baseline 14 of 60', 'hybrid 2 <b class="good">22 of 60</b>', 'wrong at high: 19 → 1'], ['Same, larger Ultra model', 'baseline 19 of 60', 'hybrid 2 <b class="good">38 of 60</b>', 'wrong at high: 8 → 0'], ['Resource faults, model-based (90 cases)', 'baseline 2 of 90', 'hybrid 2 <b class="warnc">5 of 90</b>', 'a real weakness'], ['Resource faults, metric rule, no model', 'Online Boutique <b class="good">62/90</b>', 'Train Ticket <b class="warnc">35/90</b>', 'delay/loss weak; false alarms'], ['Combining the two', 'hybrid 2 alone 22 of 60', 'combined <b class="badc">18 of 60</b>', 'did not help; not used']];
    sc.innerHTML = `<div class="kicker rv">Real benchmark data (RCAEval)</div><h2 class="rv">What held up, and what didn’t.</h2>
      <div class="card mt rv" style="padding:14px 26px"><table class="t"><tbody>${rows.map(r => `<tr class="rv"><td style="width:42%">${r[0]}</td><td>${r[1]}</td><td>${r[2]}</td><td class="mono" style="color:var(--mut);font-size:23px">${r[3]}</td></tr>`).join('')}</tbody></table></div>
      <div class="note rv">Injected faults on demo applications. The hand-written analyzer does not generalize (0 of 30). Every wrong answer is listed in the repository.</div>`;
    rv(sc, '.kicker'); rv(sc, 'h2', 150); await sleep(700); rv(sc, '.card'); await sleep(500);
    for (const r of sc.querySelectorAll('tr')) { r.classList.add('in'); await sleep(2100); } rv(sc, '.note'); await sleep(4200);
  } },
  { name: 'Check it yourself', dur: 11, async run(sc) {
    sc.classList.add('title');
    sc.innerHTML = `<div class="kicker rv">Reproduce everything offline</div><h1 class="rv" style="font-size:96px">An answer you can check<br>beats one you have to trust.</h1>
      <div class="term rv" style="margin-top:50px;min-width:1100px"><div class="tb">no API key needed</div><pre style="min-height:0"><span class="k">$</span> make results-check <span class="k">&amp;&amp;</span> make transfer-check <span class="k">&amp;&amp;</span> make metrics-check</pre></div>
      <div class="tag rv" style="font-size:34px">github.com/ActuallyAnson/TraceBack</div>`;
    rv(sc, '.kicker'); await sleep(500); rv(sc, 'h1'); await sleep(1400); rv(sc, '.term'); await sleep(900); rv(sc, '.tag'); await sleep(5200);
  } },
];

// chapters + player
const chap = document.getElementById('chapters'), playBtn = document.getElementById('play');
scenes.forEach((s, i) => { const d = document.createElement('div'); d.className = 'seg'; d.title = s.name; d.innerHTML = '<i></i>'; d.onclick = () => go(i); chap.appendChild(d); });
function paintChapters(i) { [...chap.children].forEach((d, k) => { d.classList.toggle('done', k < i); const f = d.firstChild; f.style.transition = 'none'; f.style.width = k < i ? '100%' : '0'; }); const f = chap.children[i].firstChild; requestAnimationFrame(() => { f.style.transition = `width ${scenes[i].dur}s linear`; f.style.width = '100%'; }); }
async function go(i) {
  if (i < 0 || i >= scenes.length) { playing = false; playBtn.textContent = '▶ Play'; return; }
  const my = ++tok; cur = i;
  const old = [...stage.children]; old.forEach(c => c.classList.remove('on')); setTimeout(() => old.forEach(c => c.remove()), 700);
  const sc = document.createElement('section'); sc.className = 'scene'; stage.appendChild(sc); paintChapters(i);
  requestAnimationFrame(() => sc.classList.add('on'));
  const t0 = performance.now(); (window.__starts = window.__starts || [])[i] = t0;
  try { await scenes[i].run(sc); const need = (MINDUR[i] || 0) * 1000 - (performance.now() - t0); if (need > 0) await sleep(need); if (playing && !ONLY) { await sleep(HOLD); go(i + 1); } else { playBtn.textContent = '▶ Play'; } } catch (e) { if (!(e instanceof Stop)) throw e; }
}
function toggle() { playing = !playing; playBtn.textContent = playing ? '❚❚ Pause' : '▶ Play'; if (playing && (cur < 0 || cur >= scenes.length - 1 && false)) go(0); else if (playing) go(Math.max(0, cur)); else { tok++; } }
function fit() { const s = Math.min(innerWidth / 1920, innerHeight / 1080); stage.style.transform = `scale(${s})`; }
addEventListener('resize', fit); fit();
playBtn.onclick = toggle; document.getElementById('next').onclick = () => go(cur + 1); document.getElementById('prev').onclick = () => go(cur - 1);
addEventListener('keydown', e => {
  if (e.key === ' ') { e.preventDefault(); toggle(); } else if (e.key === 'ArrowRight') go(cur + 1); else if (e.key === 'ArrowLeft') go(cur - 1);
  else if (e.key === 'h' || e.key === 'H') { document.getElementById('hud').classList.toggle('hide'); document.body.classList.toggle('nocursor'); }
  else if (e.key === 'f' || e.key === 'F') { document.fullscreenElement ? document.exitFullscreen() : document.documentElement.requestFullscreen(); }
  else if (/^[1-9]$/.test(e.key)) go(+e.key - 1);
});
if (location.search.includes('still')) document.body.classList.add('still');
go(Math.max(0, (parseInt(location.hash.slice(1), 10) || 1) - 1));
if (location.search.includes('auto')) toggle();  // ?auto starts playing at once (handy when recording)
if (location.search.includes('clean')) { document.getElementById('hud').classList.add('hide'); document.body.classList.add('nocursor'); }  // ?clean hides the controls for recording
