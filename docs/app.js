/* Traceback showcase: static, dependency-free. All data comes from data/*.json built by eval/build_site.py.
   Model answers are recorded from real runs; nothing here calls a model live. */
'use strict';
const $ = (s, r = document) => r.querySelector(s);
const $$ = (s, r = document) => [...r.querySelectorAll(s)];
const esc = s => String(s ?? '').replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
const pct = (x, d = 1) => (x * 100).toFixed(d) + '%';
const usd = x => (x < 0.01 ? '$' + x.toFixed(4) : '$' + x.toFixed(3));
const catLabel = c => esc(String(c || '').replace(/_/g, ' '));
const SVC = ['gateway', 'orders', 'inventory', 'payments'];
const reduce = matchMedia('(prefers-reduced-motion: reduce)').matches;
const q = new URLSearchParams(location.search);
const STEPS = [
  ['tel', 'Telemetry', 'Telemetry'], ['clk', 'Clock alignment', 'Clocks'], ['cor', 'Correlate & graph', 'Correlate'], ['cand', 'Candidates', 'Candidates'],
  ['llm', 'LLM reasoning', 'LLM'], ['cite', 'Citation check', 'Citations'], ['rep', 'Replay verdict', 'Replay'], ['fin', 'Report', 'Report'],
];
const SYS = [
  { key: 'baseline', label: 'Raw logs → LLM', sub: 'baseline', color: 'var(--c-base)' },
  { key: 'analyzer', label: 'Analyzer only', sub: 'no LLM', color: 'var(--c-an)' },
  { key: 'traceback_unverified', label: 'Analyzer + LLM', sub: 'no verifier', color: 'color-mix(in srgb,var(--c-tb) 60%,var(--surface))' },
  { key: 'traceback', label: 'Full Traceback', sub: 'analyzer + LLM + verifier', color: 'var(--c-tb)' },
];
const state = {
  idx: null, view: 'out', split: 'test', fSplit: 'all', fOut: 'all', id: null, step: Math.max(0, Math.min(7, (+q.get('step') || 1) - 1)),
  reveal: q.get('reveal') === '1', who: 'tb', clockOrder: 'raw', cursor: 0, playing: false, timer: null, replaySel: null, hint: true, cache: {},
};
let TL = null; // timeline geometry for the scrubber

async function getJSON(url) { const r = await fetch(url); if (!r.ok) throw new Error(url + ' ' + r.status); return r.json(); }
const D = () => state.cache[state.id];

/* ================= routing / views ================= */
function showView(v) {
  state.view = v;
  $$('.view').forEach(x => x.classList.toggle('on', x.id === 'view-' + v));
  $$('.views button').forEach(b => { const on = b.dataset.view === v; b.classList.toggle('on', on); b.setAttribute('aria-selected', on); });
  if (v !== 'inv') stopPlay();
}
function setHash() {
  const h = state.view === 'inv' ? `#${state.id}/${state.step + 1}` : state.view === 'out' ? '#out' : '#' + state.view;
  if (location.hash !== h) history.replaceState(null, '', location.pathname + location.search + h);
}
function applyHash() {
  const h = location.hash.slice(1);
  if (h === 'score' || h === 'how' || h === 'out' || h === '') { showView(h === '' ? 'out' : h); return null; }
  const m = h.match(/^(inc-\d+)(?:\/(\d))?$/);
  showView('inv');
  if (m && state.idx.incidents.some(r => r.id === m[1])) return { id: m[1], step: m[2] ? +m[2] - 1 : state.step };
  return null;
}

/* ================= scoreboard ================= */
function renderStats() {
  const s = state.idx.summary.test, tb = s.traceback, bl = s.baseline;
  const cards = [
    { big: pct(tb.top1), lab: 'root-cause accuracy on held-out incidents', vs: `Raw-logs baseline: <b>${pct(bl.top1)}</b> · analyzer only: <b>${pct(s.analyzer.top1)}</b>` },
    { big: pct(tb.fc), lab: 'confidently wrong (“high” confidence, wrong answer)', vs: `Raw-logs baseline: <b>${pct(bl.fc)}</b>` },
    { big: usd(tb.cost), lab: 'model cost per incident', vs: `Baseline: <b>${usd(bl.cost)}</b> (${(bl.cost / tb.cost).toFixed(0)}× more)` },
  ];
  $('#stats').innerHTML = cards.map(c => `<div class="stat"><div class="big">${c.big}</div><div class="lab">${c.lab}</div><div class="vs">${c.vs}</div></div>`).join('');
  $('#stats-note').textContent = `Held-out test split: ${tb.n} locked incidents (16 from the fault catalog, 10 of fault types the analyzer was never written for). The accuracy gap is one incident and is not statistically meaningful; the differences that hold up are false confidence, cost and checkable evidence.`;
  $('#model').textContent = state.idx.model;
}
function renderResults() {
  const s = state.idx.summary[state.split];
  $('#results-sub').textContent = state.split === 'test'
    ? `${s.traceback.n} incidents, locked before any model ran on them. This is the headline result.`
    : `${s.traceback.n} incidents used while developing the rules and prompts, so these numbers are optimistic.`;
  const bars = (key, max) => SYS.map(sy => { const v = s[sy.key][key];
    return `<div class="bar-row"><span>${esc(sy.label)}<br><small class="hint" style="margin:0">${esc(sy.sub)}</small></span><div class="track"><div class="fill" style="width:${Math.max(0, v / max * 100)}%;background:${sy.color}"></div></div><span class="val">${pct(v)}</span></div>`; }).join('');
  $('#chart-acc').innerHTML = bars('top1', 1);
  $('#chart-fc').innerHTML = bars('fc', 0.3);
  const row = sy => { const v = s[sy.key];
    return `<tr class="${sy.key === 'traceback' ? 'hl' : ''}"><td><b>${esc(sy.label)}</b> <span class="hint">${esc(sy.sub)}</span></td><td class="num">${pct(v.top1)}</td><td class="num">${v.prec_high == null ? '–' : pct(v.prec_high)} <span class="hint">(${v.high} high)</span></td><td class="num">${pct(v.fc)}</td><td class="num">${v.cost ? usd(v.cost) : '$0'}</td><td class="num">${v.latency < 1 ? '< 0.1 s' : v.latency.toFixed(1) + ' s'}</td></tr>`; };
  $('#results-table').innerHTML = `<thead><tr><th>System</th><th class="num">Top-1</th><th class="num">Precision when “high”</th><th class="num">Confidently wrong</th><th class="num">Cost / incident</th><th class="num">Latency</th></tr></thead><tbody>${SYS.map(row).join('')}</tbody>`;
  const a = state.idx.ablations, n = [];
  if (state.split === 'test') {
    if (state.idx.baseline_asrun_test_top1 != null) n.push(`Baseline as first run: ${pct(state.idx.baseline_asrun_test_top1)}. One incident crashed my reply parser (a tool bug); after fixing it and re-running that incident the baseline scores ${pct(s.baseline.top1)}. Both are in <code>eval/test_results/NOTES.md</code>.`);
    if (a.baseline_plus_principles) n.push(`Giving the baseline the same prompt principles as Traceback: ${pct(a.baseline_plus_principles.top1)} top-1, ${pct(a.baseline_plus_principles.fc)} confidently wrong.`);
    if (a.baseline_ultra_truncated_only) n.push(`Baseline on a 1M-context model for the ${a.baseline_ultra_truncated_only.n} incidents it had to truncate: ${pct(a.baseline_ultra_truncated_only.top1)} correct. Same as the main baseline on those incidents; thin evidence.`);
  }
  n.push(`Baseline input exceeded the model's context window and was sampled by trace for ${state.idx.truncated_baseline[state.split]} of ${s.baseline.n} incidents.`);
  n.push('“Unseen” fault types are scored on locating the right service only. Verification can only lower confidence, never change an answer.');
  $('#results-notes').innerHTML = n.map(x => `<li>${x}</li>`).join('');
  $$('[data-split]').forEach(b => { const on = b.dataset.split === state.split; b.classList.toggle('on', on); b.setAttribute('aria-pressed', on); });
}

/* ================= incident list ================= */
function listItems() {
  return state.idx.incidents.filter(r => (state.fSplit === 'all' || r.split === state.fSplit) &&
    (state.fOut === 'all' || (state.fOut === 'wrong' && !r.tb_ok) || (state.fOut === 'base-wrong' && !r.base_ok)));
}
function renderList() {
  const items = listItems();
  $('#side-count').textContent = items.length + ' shown';
  $('#inc-list').innerHTML = items.length ? items.map(r => `<li tabindex="0" data-id="${r.id}" class="${r.id === state.id ? 'sel' : ''}" role="button" aria-label="Incident ${r.id}">
    <span class="id">${r.id}</span><span class="pill">${r.group}</span><span class="pill ${r.split === 'test' ? 'acc' : ''}">${r.split}</span>
    <span class="tags"><span class="dot ${r.tb_ok ? 'ok' : 'bad'}" title="Traceback ${r.tb_ok ? 'correct' : 'wrong'}"></span><span class="dot ${r.base_ok ? 'ok' : 'bad'}" title="Baseline ${r.base_ok ? 'correct' : 'wrong'}"></span></span></li>`).join('')
    : '<li class="empty" style="cursor:default">No incidents match.</li>';
}

/* ================= selecting an incident ================= */
async function select(id, step) {
  stopPlay();
  state.id = id;
  if (step != null) state.step = Math.max(0, Math.min(7, step));
  renderList();
  $('#pane').innerHTML = '<p class="empty">Loading…</p>';
  if (!state.cache[id]) state.cache[id] = await getJSON(`data/${id}.json`);
  const d = D();
  state.replaySel = d.replays[d.answer_spec] ? d.answer_spec : Object.keys(d.replays)[0];
  state.cursor = d.symptom.onset ?? 0;
  setHash();
  renderHead(); renderStepper(); renderPane(); renderScrub();
}
const okPill = ok => state.reveal ? `<span class="pill ${ok ? 'ok' : 'bad'}">${ok ? '✓ matches ground truth' : '✗ does not match ground truth'}</span>` : '';
const confPill = c => `<span class="pill ${c === 'high' ? 'ok' : c === 'low' ? 'bad' : 'warn'}">${esc(c)} confidence</span>`;

function truthBox(d) {
  const t = d.truth, edge = t.edge ? ` on the edge ${esc(t.edge.from)} → ${esc(t.edge.to)}` : '';
  return `<div class="truth"><b>Ground truth (never shown to the analyzer, the model or the verifier):</b> <code>${esc(t.type)}</code> in <b>${esc(t.service)}</b>${edge}.${t.decoys.length ? `<br>Decoys present: ${t.decoys.map(x => `<code>${esc(x.type)}</code> in ${esc(x.service)}`).join(', ')}.` : ''}${t.complications.length ? `<br>Injected clock skew: ${t.complications.map(x => `${esc(x.service)} ${(x.offset_us / 1e6).toFixed(2)} s`).join(', ')}.` : ''}</div>`;
}
function renderHead() {
  const d = D(), sy = d.symptom;
  $('#work-head').innerHTML = `<h2>${d.id}</h2><span class="pill">${d.group}</span><span class="pill ${d.split === 'test' ? 'acc' : ''}">${d.split} split</span>
    <span class="pill bad">${sy.failed} of ${sy.traces} requests failed (${pct(sy.failed / sy.traces)})</span>
    <button class="btn" id="reveal">${state.reveal ? 'Hide' : 'Reveal'} ground truth</button>
    <a class="btn" href="reports/${d.id}.html" target="_blank" rel="noopener">Open full report ↗</a>${state.reveal ? truthBox(d) : ''}`;
  $('#work-head').querySelector('#reveal').onclick = () => { state.reveal = !state.reveal; renderHead(); renderPane(); };
}
function renderStepper() {
  $('#stepper').innerHTML = STEPS.map(([k, t, short], i) => `<button class="step ${i === state.step ? 'on' : i < state.step ? 'done' : ''}" role="tab" aria-selected="${i === state.step}" data-step="${i}" title="${t}"><span class="n">${i < state.step ? '✓' : i + 1}</span>${short}</button>`).join('');
  $('#play').textContent = state.playing ? '❚❚ Pause' : '▶ Play walkthrough';
  $('#play').classList.toggle('playing', state.playing);
  const on = $('.step.on'); if (on && on.scrollIntoView) on.scrollIntoView({ block: 'nearest', inline: 'center', behavior: reduce ? 'auto' : 'smooth' });
}
function go(i, user = true) {
  if (!state.id) return;
  if (user) stopPlay();
  state.step = Math.max(0, Math.min(7, i));
  const d = D();
  if (state.step === 3 && d.hypotheses[0]?.onset != null) setCursor(d.hypotheses[0].onset); else setCursor(d.symptom.onset ?? 0);
  setHash(); renderStepper(); renderPane(); renderScrub(true);
}
function startPlay() {
  if (state.playing) return;
  if (state.step >= 7) go(0, false);
  state.playing = true; renderStepper();
  state.timer = setInterval(() => { if (state.step >= 7) { stopPlay(); return; } go(state.step + 1, false); }, 5200);
}
function stopPlay() { if (state.timer) clearInterval(state.timer); state.timer = null; if (state.playing) { state.playing = false; renderStepper(); } }

/* ================= pane ================= */
function chipRow(ids, d) {
  return `<div class="chips">${ids.map(raw => { const id = String(raw).replace(/^[\s\[]+|[\s\]]+$/g, ''); const known = d.records[id] != null;
    return `<button class="chip ${known ? '' : 'unknown'}" data-rec="${esc(id)}" aria-expanded="false" title="${known ? 'Show record' : 'Record text not included on this page'}">${esc(id)}</button>`; }).join('')}</div><div class="recs"></div>`;
}
function renderPane() {
  const d = D(), k = STEPS[state.step][0];
  const f = { tel: stepTel, clk: stepClk, cor: stepCor, cand: stepCand, llm: stepLlm, cite: stepCite, rep: stepRep, fin: stepFin }[k];
  const pane = $('#pane');
  pane.innerHTML = `<h3 class="step-title">${state.step + 1} · ${STEPS[state.step][1]}</h3>${f(d)}`;
  afterPane();
}
function afterPane() {
  const pane = $('#pane');
  requestAnimationFrame(() => $$('.meter>i[data-w]', pane).forEach(i => { i.style.width = i.dataset.w + '%'; }));
  $$('[data-count]', pane).forEach(el => {
    const end = +el.dataset.count;
    if (reduce) { el.textContent = end.toLocaleString(); return; }
    const t0 = performance.now();
    const tick = t => { const p = Math.min(1, (t - t0) / 800); el.textContent = Math.round(end * (1 - Math.pow(1 - p, 3))).toLocaleString(); if (p < 1) requestAnimationFrame(tick); };
    requestAnimationFrame(tick);
  });
  const d = D();
  $$('[data-order]', pane).forEach(b => b.onclick = () => { state.clockOrder = b.dataset.order; renderPane(); });
  $$('.node', pane).forEach(n => { const act = () => showNode(n.dataset.svc); n.onclick = act; n.onkeydown = e => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); act(); } }; });
  $$('.row[data-h]', pane).forEach(r => r.onclick = () => toggleCand(r.dataset.h));
  $$('[data-who]', pane).forEach(b => b.onclick = () => { state.who = b.dataset.who; renderPane(); });
  $$('[data-rsel]', pane).forEach(b => b.onclick = () => { state.replaySel = b.dataset.rsel; renderPane(); });
  $$('[data-next-inc]', pane).forEach(b => b.onclick = () => { const l = listItems(), i = l.findIndex(r => r.id === state.id); select(l[(i + 1) % l.length].id, 0); });
  $$('[data-go]', pane).forEach(b => b.onclick = () => go(+b.dataset.go));
  if (state.step === 2) showNode(state.selNode || (d.hypotheses[0] && d.hypotheses[0].service) || 'gateway');
}

/* ---- 1 telemetry ---- */
function stepTel(d) {
  const c = d.counts;
  const hint = state.hint ? `<div class="hintbar"><span>New here? Press <b>▶ Play walkthrough</b> or use the <b>←</b> <b>→</b> keys to step through an investigation. Every step shows real output. Pick other incidents on the left; use the <b>timeline slider</b> at the bottom to scrub through time.</span><button id="hint-x" aria-label="Dismiss">×</button></div>` : '';
  return `${hint}<p class="step-sub">The system starts from nothing but what the services emitted: structured logs, request spans, metrics and deploy/config events. It is given no labels and no hint about what went wrong.</p>
    <div class="counters"><div class="counter"><b data-count="${c.logs}">0</b><span>log lines (${c.warn_error.toLocaleString()} warn/error)</span></div><div class="counter"><b data-count="${c.spans}">0</b><span>spans</span></div><div class="counter"><b data-count="${c.metrics}">0</b><span>metric samples</span></div><div class="counter"><b data-count="${c.ops}">0</b><span>deploy / config events</span></div></div>
    <div class="grid2" style="margin-top:14px"><div class="card"><h3>What it actually looks like</h3><pre class="raw">${d.samples.map(esc).join('\n\n')}</pre></div>
    <div class="card"><h3>Why it’s hard</h3><ul style="margin:0;padding-left:18px;color:var(--mut)"><li>${d.symptom.failed} of ${d.symptom.traces} requests failed, but the failing service is often <b>not</b> the broken one.</li><li>Each service stamps its <b>own clock</b>, and they disagree.</li><li>Logs are noisy: unrelated errors, lost lines, missing trace IDs.</li><li>${d.symptom.before ? `${d.symptom.before} isolated failures happen <b>before</b> the real outage starts.` : 'Background errors occur even in healthy periods.'}</li></ul></div></div>`;
}

/* ---- 2 clocks ---- */
function stepClk(d) {
  const ev = d.timeline.filter(e => e.raw != null);
  const byRaw = [...ev].sort((a, b) => a.raw - b.raw).map(e => e.id), byT = [...ev].sort((a, b) => a.t - b.t).map(e => e.id);
  const order = state.clockOrder === 'raw' ? byRaw : byT;
  const rows = order.map(id => ev.find(e => e.id === id)).map(e => `<tr class="${byRaw.indexOf(e.id) !== byT.indexOf(e.id) ? 'moved' : ''}"><td>${esc(e.service)}</td><td>${esc(e.text)}</td><td class="t">${e.raw.toFixed(2)}s</td><td class="t">${e.t.toFixed(2)}s</td></tr>`).join('');
  const c = d.clock, off = SVC.filter(s => c.offsets[s] != null).map(s => [s, c.offsets[s]]);
  const mx = Math.max(0.05, ...off.map(([, v]) => Math.abs(v)));
  const bars = off.map(([s, v]) => `<div class="bar2"><span>${s}</span><div class="meter"><i data-w="${Math.abs(v) / mx * 100}" style="background:${s === c.reference ? 'var(--c-base)' : 'var(--c-an)'}"></i></div><span>${(v * 1000).toFixed(0)} ms</span></div>`).join('');
  const inj = state.reveal && Object.keys(c.injected).length ? `<p class="hint">Injected skew (revealed): ${Object.entries(c.injected).map(([s, v]) => `${esc(s)} ${v.toFixed(2)} s`).join(', ')}. Differences are recovered to within a few milliseconds.</p>` : '';
  const pairs = Object.entries(c.samples).map(([k, n]) => `${esc(k)}: ${n.toLocaleString()}`).join(' · ');
  return `<p class="step-sub">Each service writes timestamps from its own clock. Traceback estimates the offsets from nothing but parent/child span pairs: a callee’s span must start a fraction of a millisecond after its caller’s. Highlighted rows change position once the clocks are aligned.</p>
    <div class="grid2"><div class="card"><h3>Key events</h3><div class="seg small" style="margin-bottom:8px"><button data-order="raw" class="${state.clockOrder === 'raw' ? 'on' : ''}">Order by raw clocks</button><button data-order="aligned" class="${state.clockOrder === 'aligned' ? 'on' : ''}">Order by aligned clocks</button></div>
      <div class="table-wrap"><table class="evt-table"><thead><tr><th>service</th><th>event</th><th>raw time</th><th>aligned</th></tr></thead><tbody>${rows || '<tr><td colspan=4>no events</td></tr>'}</tbody></table></div></div>
    <div class="card"><h3>Estimated clock offsets <span class="hint">vs ${esc(c.reference)}</span></h3>${bars}<p class="hint">Span pairs used: ${pairs}</p>${inj}</div></div>`;
}

/* ---- 3 correlate + graph ---- */
function graphSVG(d) {
  const pos = { gateway: [80, 115], orders: [290, 115], inventory: [520, 42], payments: [520, 188] }, top = d.hypotheses[0];
  let g = '';
  const maxCalls = Math.max(...d.graph.map(e => e.calls));
  d.graph.forEach(e => { const a = pos[e.caller], b = pos[e.callee]; if (!a || !b) return;
    const fr = e.failed / e.calls, w = 1.5 + 5 * e.calls / maxCalls, col = fr > 0.02 ? `rgba(var(--heat),${Math.min(1, 0.35 + fr * 1.2).toFixed(2)})` : 'var(--line)';
    g += `<line x1="${a[0] + 50}" y1="${a[1]}" x2="${b[0] - 50}" y2="${b[1]}" stroke="${col}" stroke-width="${w.toFixed(1)}" marker-end="url(#ar)"/><text x="${(a[0] + b[0]) / 2}" y="${(a[1] + b[1]) / 2 - 9}" text-anchor="middle" style="font-size:13px">${pct(fr, 0)} failed${e.retried_attempts ? ' · ' + e.retried_attempts + ' retries' : ''}</text>`; });
  Object.entries(pos).forEach(([s, [px, py]]) => { const hy = d.hypotheses.filter(h => h.service === s || (h.edge && (h.edge.from === s || h.edge.to === s)));
    g += `<g class="node" data-svc="${s}" role="button" tabindex="0" aria-label="${s}: show details"><rect x="${px - 50}" y="${py - 22}" width="100" height="44" rx="10" fill="var(--surface)" stroke="${top && top.service === s ? 'var(--c-tb)' : 'var(--line)'}" stroke-width="${top && top.service === s ? 3 : 1.5}"/><text class="lane-label" x="${px}" y="${py + 5}" text-anchor="middle" style="font-size:14px">${s}</text>${hy.length ? `<text x="${px}" y="${py - 28}" text-anchor="middle" style="fill:var(--c-tb);font-weight:700;font-size:13px">${hy.slice(0, 3).map(h => 'H' + h.rank).join(' ')}</text>` : ''}</g>`; });
  return `<svg viewBox="0 0 640 235" role="img" aria-label="Dependency graph with failure rates" class="graph"><defs><marker id="ar" viewBox="0 0 10 10" refX="8" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path d="M0 0L10 5L0 10z" fill="var(--mut)"/></marker></defs>${g}</svg>`;
}
function stepCor(d) {
  return `<p class="step-sub">Spans are regrouped into ${d.symptom.traces.toLocaleString()} traces on the aligned clock (lost parent spans are re-attached by time containment). The dependency graph comes from the calls themselves. <b>Click a service</b> to inspect it.</p>
    <div class="grid2"><div class="card"><h3>Dependency graph <span class="hint">edge width = traffic, red = failures</span></h3>${graphSVG(d)}</div><div class="card" id="node-panel"></div></div>`;
}
function showNode(s) {
  state.selNode = s;
  const d = D(), ser = d.series[s] || [];
  $$('.node rect', $('#pane')).forEach(r => r.setAttribute('stroke-width', r.parentElement.dataset.svc === s ? 4 : (d.hypotheses[0] && r.parentElement.dataset.svc === d.hypotheses[0].service ? 3 : 1.5)));
  const peakErr = Math.max(0, ...ser.map(m => m.req ? m.err / m.req : 0)), peakP99 = Math.max(0, ...ser.map(m => m.p99));
  const wl = ser.map(m => m.wl), minWl = wl.length ? Math.min(...wl) : 0, maxWl = wl.length ? Math.max(...wl) : 0;
  const hs = d.hypotheses.filter(h => h.service === s);
  $('#node-panel').innerHTML = `<h3>${esc(s)}</h3><dl class="kv"><dt>peak 5xx rate</dt><dd>${pct(peakErr, 0)}</dd><dt>peak p99 latency</dt><dd>${peakP99.toFixed(0)} ms</dd><dt>max queue</dt><dd>${Math.max(0, ...ser.map(m => m.q))}</dd><dt>worker limit</dt><dd>${minWl === maxWl ? maxWl : `${maxWl} → ${minWl}`}</dd><dt>max pool in use</dt><dd>${Math.max(0, ...ser.map(m => m.pool))}</dd><dt>restarts</dt><dd>${ser.length ? Math.max(...ser.map(m => m.boots)) - Math.min(...ser.map(m => m.boots)) : 0}${ser.length && Math.max(...ser.map(m => m.boots)) > Math.min(...ser.map(m => m.boots)) ? ' (boot counter increased)' : ''}</dd></dl>
    <p style="margin:10px 0 0">${hs.length ? 'Candidate causes involving this service: ' + hs.map(h => `<span class="pill acc">H${h.rank} ${catLabel(h.category)}</span>`).join(' ') : '<span class="hint">No candidate cause points at this service.</span>'}</p>`;
}

/* ---- 4 candidates ---- */
function stepCand(d) {
  const rows = d.hypotheses.map(h => { const spec = `${h.category}:${h.service}`, chosen = spec === d.answer_spec;
    return `<div class="row" data-h="${h.rank}" tabindex="0" role="button" aria-expanded="false"><b>H${h.rank}</b><span><code>${catLabel(h.category)}</code><br>${esc(h.service)}${h.edge ? ` <span class="hint">(${esc(h.edge.from)} → ${esc(h.edge.to)})</span>` : ''}${chosen ? '<br><span class="pill acc">chosen by the model</span>' : ''}</span>
      <span><div class="meter"><i data-w="${h.score * 100}"></i></div><span class="hint" style="margin:0">score ${h.score} · touches ${pct(h.coverage, 0)} of failures</span></span><span>${esc(h.summary)}</span></div><div class="cand-detail" id="cd-${h.rank}" hidden></div>`; }).join('');
  const ro = d.ruled_out.length ? `<div class="card" style="margin-top:14px"><h3>Ruled out</h3><ul style="margin:0;padding-left:18px">${d.ruled_out.map(r => `<li>${esc(r.change)} on <b>${esc(r.service)}</b> at ${r.t.toFixed(1)} s: ${esc(r.reason)}</li>`).join('')}</ul></div>` : '';
  return `<p class="step-sub">Deterministic rules turn the correlated data into ranked candidate causes, each with the exact evidence records behind it. No language model is involved yet. <b>Click a candidate</b> to see what it saw.</p><div class="card rows">${rows}</div>${ro}`;
}
function toggleCand(rank) {
  const d = D(), box = $('#cd-' + rank), row = $(`.row[data-h="${rank}"]`);
  const open = !box.hidden;
  $$('.cand-detail').forEach(b => { b.hidden = true; }); $$('.row[data-h]').forEach(r => { r.classList.remove('sel'); r.setAttribute('aria-expanded', 'false'); });
  if (open) return;
  const h = d.hypotheses.find(x => x.rank === +rank);
  box.innerHTML = `<b>What it saw</b><pre>${esc(JSON.stringify(h.features, null, 1))}</pre><b>Evidence records</b>${chipRow(h.evidence.map(e => e.id), d)}`;
  box.hidden = false; row.classList.add('sel'); row.setAttribute('aria-expanded', 'true');
  if (h.onset != null) setCursor(h.onset);
}

/* ---- 5 LLM ---- */
function claimList(a, d, delay = true) {
  return `<ol class="chain">${a.causal_chain.map((c, i) => `<li class="claim" style="animation-delay:${delay ? 250 + i * 330 : 0}ms"><span class="pill ${c.status === 'confirmed' ? 'ok' : 'warn'}">${c.status}</span> ${esc(c.claim)}${chipRow(c.cites, d)}</li>`).join('')}</ol>`;
}
function stepLlm(d) {
  const tb = d.traceback, bl = d.baseline, isTb = state.who === 'tb' || !bl;
  const r = isTb ? tb : bl, a = r.answer;
  const seg = `<div class="seg" style="margin-bottom:12px"><button data-who="tb" class="${isTb ? 'on' : ''}">Full Traceback</button><button data-who="base" class="${!isTb ? 'on' : ''}">Raw-logs baseline</button></div>`;
  if (!a) return `${seg}<p class="empty">No answer produced.</p>`;
  const saw = isTb ? `the analysis layer’s ${d.hypotheses.length} candidate causes, the dependency graph, the ruled-out changes and ${Object.keys(d.records).length} evidence records (${tb.tokens[0].toLocaleString()} tokens)`
    : `every warn/error log, all metrics and events, grouped by trace with raw clocks (${bl.tokens[0].toLocaleString()} tokens${bl.attempts > 1 ? ` over ${bl.attempts} attempts` : ''}${bl.truncated ? `; too large for the context window, so ${pct(bl.kept, 0)} of trace groups were sampled` : ''})`;
  const conf = isTb ? tb.final_confidence : a.confidence;
  return `<p class="step-sub">Nemotron (on Nebius Token Factory) is asked to rank, explain and check. Same model and output format for both systems; only the input differs. These answers are recorded from real runs.</p>${seg}
    <div class="card"><div class="rc"><span class="pill acc">Root cause</span><span class="cat">${catLabel(a.root_cause.category)}</span><span>in <b>${esc(a.root_cause.service)}</b></span>${confPill(conf)}${okPill(r.correct)}</div>
      <p style="margin:10px 0 4px">${esc(a.root_cause.summary)}</p><p class="hint" style="margin:0">It saw: ${saw}. ${usd(r.cost)} · ${r.latency.toFixed(1)} s</p></div>
    <div class="card" style="margin-top:14px"><h3>Causal chain <span class="hint">earliest cause → visible symptom; click an ID to read the record</span></h3>${claimList(a, d)}</div>
    ${(a.alternatives || []).length ? `<div class="card" style="margin-top:14px"><h3>Alternatives it considered</h3><ul style="margin:0;padding-left:18px">${a.alternatives.map(c => `<li><code>${catLabel(c.category)}</code> in ${esc(c.service)}: ${esc(c.why_less_likely)}</li>`).join('')}</ul></div>` : ''}`;
}

/* ---- 6 citation check ---- */
function stepCite(d) {
  const cs = d.cite_summary, flags = d.cite_flags, ids = Object.keys(flags).sort();
  const rows = ids.map(id => { const f = flags[id];
    return `<div class="row" style="grid-template-columns:auto 56px 70px 1fr;cursor:default"><span class="chip" style="cursor:default">${esc(id)}</span><span class="pill ${f.exists ? 'ok' : 'bad'}">${f.exists ? '✓ exists' : '✗ missing'}</span><span class="pill ${f.supported ? 'ok' : ''}" title="Is this record in the evidence set the analysis layer attached to the chosen candidate?">${f.supported ? '✓ evidence' : 'other'}</span><span class="mono" style="font-size:12px;word-break:break-word">${esc(d.records[id] ?? '(record text not included on this page)')}</span></div>`; }).join('');
  return `<p class="step-sub">Every claim must cite record IDs. Each cited ID is looked up in the incident’s data; if any does not exist, the answer is rejected and the model is asked again. Models do invent IDs sometimes, so this is a hard check, not a formality.</p>
    <div class="counters"><div class="counter"><b data-count="${cs.total}">0</b><span>citations (${cs.unique} unique)</span></div><div class="counter"><b>${ids.every(i => flags[i].exists) ? '100%' : pct(ids.filter(i => flags[i].exists).length / ids.length, 0)}</b><span>cited IDs exist</span></div><div class="counter"><b>${cs.attempts}</b><span>model attempt${cs.attempts > 1 ? 's' : ''}</span></div><div class="counter"><b>${cs.first_invalid}</b><span>invalid IDs on the first attempt</span></div></div>
    <div class="card rows" style="margin-top:14px"><h3>The cited records <span class="hint">${cs.supported} of ${cs.unique} are in the analysis layer’s evidence set; the rest are symptom records the model looked up on its own</span></h3>${rows}</div>`;
}

/* ---- 7 replay ---- */
function lineChart(d, rp) {
  const W = 700, H = 210, L = 44, R = 14, T = 12, B = 28, n = rp.series_all.length;
  const x = i => L + (i + 0.5) / n * (W - L - R);
  const gw = d.series.gateway || [];
  const obs = Array(n).fill(null);
  gw.forEach(m => { const i = Math.min(n - 1, Math.max(0, Math.round((m.t) / 5) - 1)); if (m.req) obs[i] = m.err / m.req; });
  const mx = Math.max(0.1, ...rp.series_all, ...rp.series_disabled, ...obs.filter(v => v != null)) * 1.1;
  const y = v => T + (1 - v / mx) * (H - T - B);
  const path = arr => arr.map((v, i) => v == null ? '' : `${i && arr[i - 1] != null ? 'L' : 'M'}${x(i).toFixed(1)} ${y(v).toFixed(1)}`).join('');
  let g = '';
  for (let k = 0; k <= 4; k++) { const v = mx * k / 4; g += `<line x1="${L}" x2="${W - R}" y1="${y(v)}" y2="${y(v)}" stroke="var(--line)"/><text x="${L - 6}" y="${y(v) + 4}" text-anchor="end">${(v * 100).toFixed(0)}%</text>`; }
  for (let t = 0; t <= 120; t += 20) g += `<text x="${L + t / 120 * (W - L - R)}" y="${H - 8}" text-anchor="middle">${t}s</text>`;
  g += `<path d="${path(obs)}" fill="none" stroke="var(--c-base)" stroke-width="5" opacity=".35"/><path d="${path(rp.series_all)}" fill="none" stroke="rgb(var(--heat))" stroke-width="2.5"/><path d="${path(rp.series_disabled)}" fill="none" stroke="var(--c-tb)" stroke-width="2.5" stroke-dasharray="${rp.series_disabled.length ? '0' : '4'}"/>`;
  return `<svg viewBox="0 0 ${W} ${H}" role="img" aria-label="Failure rate over time: observed, replay with everything enabled, replay with the candidate disabled" style="width:100%">${g}</svg>
    <div class="legend"><span><i style="background:var(--c-base);opacity:.5"></i>observed incident</span><span><i style="background:rgb(var(--heat))"></i>replay, everything enabled</span><span><i style="background:var(--c-tb)"></i>replay, candidate’s conditions disabled</span></div>`;
}
function stepRep(d) {
  const specs = d.hypotheses.map(h => ({ spec: `${h.category}:${h.service}`, h }));
  if (!specs.some(s => s.spec === d.answer_spec)) specs.push({ spec: d.answer_spec, h: null });
  const sel = specs.some(s => s.spec === state.replaySel) ? state.replaySel : d.answer_spec, rp = d.replays[sel];
  const chips = specs.map(s => `<button class="chip ${s.spec === sel ? '' : ''}" data-rsel="${esc(s.spec)}" aria-expanded="${s.spec === sel}">${s.h ? 'H' + s.h.rank + ' ' : 'answer '}${esc(s.spec.replace(':', ' · ').replace(/_/g, ' '))}${s.spec === d.answer_spec ? ' ★' : ''}</button>`).join('');
  const cls = rp.status === 'passed' ? 'ok' : rp.status === 'failed' ? 'bad' : 'warn';
  const label = { passed: 'Replay passed', failed: 'Replay failed', inconclusive: 'Inconclusive' }[rp.status];
  const msg = rp.status === 'passed' ? `With ${esc(rp.matched.join(', '))} disabled the peak failure rate falls from ${pct(rp.rate_all)} to ${pct(rp.rate_disabled)}: this candidate is a <b>sufficient cause</b>.`
    : rp.status === 'failed' ? `With ${esc(rp.matched.join(', '))} disabled the incident <b>persists</b> (${pct(rp.rate_disabled)} vs ${pct(rp.rate_all)}): switching this off does not fix it.`
    : esc(rp.reason) + '.';
  return `<p class="step-sub">The verifier re-runs the incident with the conditions behind a hypothesis switched off. If the failures vanish, that hypothesis is a real cause. <b>Click any candidate</b> to test it. ★ marks the model’s answer. The replay is told the environment’s switches, not which one matters.</p>
    <div class="card"><div class="chips" style="margin:0 0 12px">${chips}</div><div class="rc" style="margin-bottom:6px"><span class="pill ${cls}">${label}</span><span>${msg}</span></div>${rp.series_all.length ? lineChart(d, rp) : '<p class="hint">No replay was run, so there is no curve to show.</p>'}</div>
    <p class="hint">Verification never changes the answer; it only adjusts confidence: passed keeps it, failed forces “low”, inconclusive caps it at “medium”.</p>`;
}

/* ---- 8 report ---- */
function stepFin(d) {
  const tb = d.traceback, a = tb.answer, bl = d.baseline;
  const adj = tb.final_confidence !== tb.llm_confidence ? `The model said “${esc(tb.llm_confidence)}”; replay verification (${esc(tb.verification.status)}) adjusted it to “${esc(tb.final_confidence)}”.` : `Replay verification (${esc(tb.verification.status)}) kept the model’s “${esc(tb.llm_confidence)}”.`;
  return `<p class="step-sub">The investigation ends in a report a human can audit line by line.</p>
    <div class="card"><div class="rc"><span class="pill acc">Root cause</span><span class="cat">${catLabel(a.root_cause.category)}</span><span>in <b>${esc(a.root_cause.service)}</b></span>${confPill(tb.final_confidence)}${okPill(tb.correct)}</div>
      <p style="margin:10px 0 4px">${esc(a.root_cause.summary)}</p><p class="hint" style="margin:0">${adj}</p>
      <dl class="kv" style="margin-top:10px"><dt>claims</dt><dd>${a.causal_chain.length} (${a.causal_chain.filter(c => c.status === 'confirmed').length} confirmed), every cited ID checked</dd><dt>cost / time</dt><dd>${usd(tb.cost)} · ${tb.latency.toFixed(1)} s</dd>${bl ? `<dt>baseline</dt><dd>${catLabel(bl.answer?.root_cause.category)} in ${esc(bl.answer?.root_cause.service)} · ${usd(bl.cost)}${state.reveal ? ' · ' + (bl.correct ? '✓' : '✗') : ''}</dd>` : ''}</dl>
      <p style="margin:14px 0 0;display:flex;gap:10px;flex-wrap:wrap"><a class="btn" href="reports/${d.id}.html" target="_blank" rel="noopener">Open the full report ↗</a><button class="btn" data-go="0">↺ Replay the walkthrough</button><button class="btn" data-next-inc="1">Next incident →</button></p></div>`;
}

/* ================= scrubber / timeline ================= */
function timelineSVG(d, tmin, tmax) {
  const lanes = SVC.filter(s => d.series[s]);
  const W = 1000, L = 92, R = 14, T = 30, LH = 34, H = T + lanes.length * LH + 26;
  TL = { W, L, R, tmin, tmax, T, H, lanes: lanes.length, LH };
  const x = t => L + (t - tmin) / (tmax - tmin) * (W - L - R);
  let g = '';
  for (let t = Math.ceil(tmin / 10) * 10; t <= tmax; t += 10) g += `<line x1="${x(t)}" x2="${x(t)}" y1="${T - 4}" y2="${H - 22}" stroke="var(--line)"/><text x="${x(t)}" y="${H - 8}" text-anchor="middle">${t}s</text>`;
  lanes.forEach((s, i) => { const y = T + i * LH;
    g += `<text class="lane-label" x="${L - 10}" y="${y + 21}" text-anchor="end">${s}</text><rect x="${L}" y="${y + 3}" width="${W - L - R}" height="${LH - 6}" rx="4" fill="var(--surface2)"/>`;
    d.series[s].forEach(m => { const rate = m.req ? m.err / m.req : 0;
      if (rate > 0.005) g += `<rect x="${x(m.t - 5)}" y="${y + 3}" width="${Math.max(1, x(m.t) - x(m.t - 5) - 1)}" height="${LH - 6}" rx="2" fill="rgba(var(--heat),${Math.min(1, 0.15 + rate * 1.4).toFixed(2)})"><title>${s} ${m.t.toFixed(0)}s: ${m.err}/${m.req} 5xx, p99 ${m.p99} ms, queue ${m.q}, workers ${m.busy}/${m.wl}, pool ${m.pool}</title></rect>`; });
    const p99s = d.series[s].map(m => m.p99).sort((a, b) => a - b), med = p99s[Math.floor(p99s.length / 2)] || 1;
    d.series[s].forEach(m => { const r = m.p99 / Math.max(med, 5);
      if (m.req && r > 3 && m.p99 > 100) g += `<rect x="${x(m.t - 5)}" y="${y + LH - 11}" width="${Math.max(1, x(m.t) - x(m.t - 5) - 1)}" height="7" rx="2" fill="var(--warn)" opacity="${Math.min(1, 0.35 + r / 30).toFixed(2)}"><title>${s} ${m.t.toFixed(0)}s: p99 ${m.p99} ms vs typical ${med.toFixed(0)} ms</title></rect>`; });
    const ts = d.series[s].map(m => m.t);
    for (let k = 1; k < ts.length; k++) if (ts[k] - ts[k - 1] > 7.5) g += `<rect x="${x(ts[k - 1])}" y="${y + 3}" width="${x(ts[k]) - x(ts[k - 1])}" height="${LH - 6}" fill="none" stroke="var(--warn)" stroke-dasharray="4 3" rx="3"><title>${s}: no metrics for ${(ts[k] - ts[k - 1]).toFixed(0)} s (stopped?)</title></rect>`;
  });
  const lane = s => Math.max(0, lanes.indexOf(s));
  d.hypotheses.slice(0, 3).forEach(h => { if (h.onset == null) return; const y = T + lane(h.service) * LH;
    g += `<path d="M${x(h.onset) - 5} ${y + LH - 2} l5 -9 l5 9z" fill="var(--c-tb)"><title>H${h.rank} ${h.category} onset ${h.onset.toFixed(1)}s</title></path><text x="${x(h.onset)}" y="${y + 9}" text-anchor="middle" style="fill:var(--c-tb);font-weight:700">H${h.rank}</text>`; });
  d.timeline.forEach(e => { if (e.kind === 'anomaly_onset' || e.kind === 'metric') return;
    const col = e.kind === 'symptom_onset' ? 'var(--bad)' : e.kind.endsWith('_change') ? 'var(--warn)' : 'var(--c-tb)';
    g += `<line x1="${x(e.t)}" x2="${x(e.t)}" y1="${T - 4}" y2="${H - 22}" stroke="${col}" stroke-width="2" ${e.kind === 'symptom_onset' ? 'stroke-dasharray="5 3"' : ''}/><circle cx="${x(e.t)}" cy="${T - 10}" r="5" fill="${col}"><title>${esc(e.text)} (${e.t.toFixed(1)} s)</title></circle>`; });
  g += `<line id="cur" x1="${x(state.cursor)}" x2="${x(state.cursor)}" y1="${T - 12}" y2="${H - 22}" stroke="var(--fg)" stroke-width="1.5"/><circle id="cur-dot" cx="${x(state.cursor)}" cy="${T - 14}" r="4.5" fill="var(--fg)"/>`;
  return `<svg id="tl" viewBox="0 0 ${W} ${H}" role="img" aria-label="Timeline of per-service error rate with change events and candidate onsets; click to move the time cursor" style="min-width:640px;width:100%;cursor:crosshair">${g}</svg>`;
}
function renderScrub(keep) {
  const d = D(), all = SVC.flatMap(s => (d.series[s] || []).map(m => m.t));
  const tmin = Math.min(-2, ...all.map(t => t - 5)), tmax = Math.max(...all) + 1;
  const box = $('#scrub');
  box.innerHTML = `<h3>Timeline <span class="hint">drag the slider or click the chart; red = 5xx share, amber strip = unusually slow, per service per 5 s</span></h3>
    <div class="tl-wrap">${timelineSVG(d, tmin, tmax)}</div>
    <div class="legend"><span><i style="background:rgba(var(--heat),.8)"></i>5xx rate</span><span><i style="background:var(--warn);height:6px;vertical-align:1px"></i>p99 far above normal</span><span><i style="background:var(--bad)"></i>symptom onset</span><span><i style="background:var(--warn)"></i>deploy / config / restart</span><span><i style="background:var(--c-tb)"></i>candidate onset</span><span><i style="border:1px dashed var(--warn)"></i>metrics silent</span></div>
    <input type="range" id="cursor" min="${tmin.toFixed(1)}" max="${tmax.toFixed(1)}" step="0.5" value="${state.cursor}" aria-label="Time cursor, seconds">
    <div id="readout" class="readout"></div><div id="near"></div>`;
  $('#cursor').oninput = e => setCursor(+e.target.value, false);
  $('#tl').onclick = e => { const r = e.currentTarget.getBoundingClientRect(), vx = (e.clientX - r.left) / r.width * TL.W, t = TL.tmin + (vx - TL.L) / (TL.W - TL.L - TL.R) * (TL.tmax - TL.tmin); setCursor(Math.max(TL.tmin, Math.min(TL.tmax, t))); };
  updateReadout();
}
function setCursor(t, moveSlider = true) {
  state.cursor = t;
  if (!D() || !TL) return;
  const x = TL.L + (t - TL.tmin) / (TL.tmax - TL.tmin) * (TL.W - TL.L - TL.R);
  const c = $('#cur'), dot = $('#cur-dot'); if (c) { c.setAttribute('x1', x); c.setAttribute('x2', x); dot.setAttribute('cx', x); }
  if (moveSlider && $('#cursor')) $('#cursor').value = t;
  updateReadout();
}
function updateReadout() {
  const d = D(), t = state.cursor; if (!d || !$('#readout')) return;
  $('#readout').innerHTML = SVC.filter(s => d.series[s]).map(s => {
    const ser = d.series[s]; let m = ser.find(x => x.t - 5 <= t && t <= x.t);
    if (!m) return `<div class="ro bad"><b>${s} @ ${t.toFixed(0)}s</b><span>no metrics (service silent?)</span></div>`;
    const er = m.req ? m.err / m.req : 0, bad = er > 0.05;
    return `<div class="ro ${bad ? 'bad' : ''}"><b>${s} @ ${t.toFixed(0)}s</b><span>${m.req} req · ${pct(er, 0)} 5xx<br>p99 ${m.p99} ms · queue ${m.q}<br>workers ${m.busy}/${m.wl} · pool ${m.pool}</span></div>`; }).join('');
  const near = d.timeline.filter(e => Math.abs(e.t - t) <= 6);
  $('#near').innerHTML = near.length ? `<div class="chips" style="margin-top:8px"><span class="hint">events within ±6 s:</span>${near.map(e => `<button class="chip" data-rec="${esc(e.id)}" aria-expanded="false" title="${esc(e.text)}">${e.t.toFixed(1)}s ${esc(e.service)}: ${esc(e.kind.replace(/_/g, ' '))}</button>`).join('')}</div><div class="recs"></div>` : '';
}

/* ================= architecture ================= */
function renderArch() {
  const box = (x, y, w, t1, t2, kind, step) => `<g role="button" tabindex="0" data-archstep="${step}" aria-label="${t1}: open step ${step + 1}"><rect x="${x}" y="${y}" width="${w}" height="76" rx="12" fill="var(--surface)" stroke="${kind === 'llm' ? 'var(--c-an)' : kind === 'ver' ? 'var(--warn)' : 'var(--c-tb)'}" stroke-width="2"/><text x="${x + w / 2}" y="${y + 31}" text-anchor="middle" class="lane-label" style="font-size:13px">${t1}</text><text x="${x + w / 2}" y="${y + 52}" text-anchor="middle" style="font-size:11.5px">${t2}</text></g>`;
  const ar = (x1, y1, x2, y2) => `<line x1="${x1}" y1="${y1}" x2="${x2}" y2="${y2}" stroke="var(--mut)" stroke-width="2" marker-end="url(#ah)"/>`;
  $('#arch').innerHTML = `<svg viewBox="0 0 1000 310" role="img" aria-label="Pipeline architecture"><defs><marker id="ah" viewBox="0 0 10 10" refX="8" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0L10 5L0 10z" fill="var(--mut)"/></marker></defs>
    ${box(20, 20, 210, 'Telemetry', 'logs · spans · metrics · deploys', 'det', 0)}${ar(230, 58, 262, 58)}${box(265, 20, 210, 'Clock alignment', 'offsets from causal span pairs', 'det', 1)}${ar(475, 58, 507, 58)}
    ${box(510, 20, 210, 'Correlator + graph', 'traces, ordering, dependencies', 'det', 2)}${ar(720, 58, 752, 58)}${box(755, 20, 225, 'Hypothesis rules', 'ranked candidates + evidence IDs', 'det', 3)}${ar(867, 96, 867, 154)}
    ${box(755, 158, 225, 'LLM reasoner', 'Nemotron on Nebius: rank, explain, cite', 'llm', 4)}${ar(755, 196, 723, 196)}${box(510, 158, 210, 'Citation validator', 'reject unknown IDs, ask again', 'det', 5)}${ar(510, 196, 478, 196)}
    ${box(265, 158, 210, 'Replay verifier', 'disable suspected cause, re-run', 'ver', 6)}${ar(265, 196, 233, 196)}${box(20, 158, 210, 'Incident report', 'timeline · chain · evidence · verdict', 'det', 7)}
    <g transform="translate(20 272)"><rect width="14" height="14" rx="3" fill="none" stroke="var(--c-tb)" stroke-width="2"/><text x="22" y="12">deterministic code (C++)</text><rect x="220" width="14" height="14" rx="3" fill="none" stroke="var(--c-an)" stroke-width="2"/><text x="242" y="12">language model</text><rect x="380" width="14" height="14" rx="3" fill="none" stroke="var(--warn)" stroke-width="2"/><text x="402" y="12">counterfactual check</text></g></svg>`;
  $$('[data-archstep]').forEach(g => { const act = () => { location.hash = `#${state.id}/${+g.dataset.archstep + 1}`; applyAndSelect(); }; g.onclick = act; g.onkeydown = e => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); act(); } }; });
}

/* ================= wiring ================= */
async function applyAndSelect() { const r = applyHash(); if (r) await select(r.id, r.step); else if (state.view === 'inv' && !state.id) await select(defaultPick(), state.step); else if (state.view === 'inv' && state.id) { setHash(); } }
function defaultPick() {
  const L = state.idx.incidents;
  return (L.find(r => r.split === 'test' && !r.base_ok && r.tb_ok && r.group === 'double') || L.find(r => r.split === 'test' && !r.base_ok && r.tb_ok) || L.find(r => r.split === 'test')).id;
}
function wire() {
  $$('.views button, [data-view]').forEach(b => b.addEventListener('click', e => { if (b.tagName === 'A') e.preventDefault(); const v = b.dataset.view; showView(v); if (v === 'inv' && !state.id) applyAndSelect(); setHash(); window.scrollTo({ top: 0 }); }));
  $$('#view-score [data-split]').forEach(b => b.onclick = () => { state.split = b.dataset.split; renderResults(); });
  $$('#f-split button').forEach(b => b.onclick = () => { state.fSplit = b.dataset.v; $$('#f-split button').forEach(x => x.classList.toggle('on', x === b)); renderList(); });
  $$('#f-out button').forEach(b => b.onclick = () => { state.fOut = b.dataset.v; $$('#f-out button').forEach(x => x.classList.toggle('on', x === b)); renderList(); });
  $('#inc-list').addEventListener('click', e => { const li = e.target.closest('li[data-id]'); if (li) select(li.dataset.id, 0); });
  $('#inc-list').addEventListener('keydown', e => { if ((e.key === 'Enter' || e.key === ' ') && e.target.dataset.id) { e.preventDefault(); select(e.target.dataset.id, 0); } });
  $('#stepper').addEventListener('click', e => { const b = e.target.closest('.step'); if (b) go(+b.dataset.step); });
  $('#prev').onclick = () => go(state.step - 1); $('#next').onclick = () => go(state.step + 1);
  $('#play').onclick = () => state.playing ? stopPlay() : startPlay();
  document.addEventListener('click', e => {
    const b = e.target.closest('.chip[data-rec]'); if (b) {
      const d = D(), box = b.parentElement.nextElementSibling; if (!box || !box.classList.contains('recs')) return;
      const id = b.dataset.rec, open = b.getAttribute('aria-expanded') === 'true'; b.setAttribute('aria-expanded', !open);
      const ex = [...box.children].find(x => x.dataset.for === id);
      if (open) { ex && ex.remove(); return; }
      box.insertAdjacentHTML('beforeend', `<div class="rec" data-for="${esc(id)}"><b>${esc(id)}</b> ${esc(d.records[id] ?? 'record text is not included on this page (the id exists in the incident data)')}</div>`);
    }
    if (e.target.id === 'hint-x') { state.hint = false; renderPane(); }
    if (e.target.id === 'theme') { const r = document.documentElement, dark = r.dataset.theme ? r.dataset.theme === 'dark' : matchMedia('(prefers-color-scheme: dark)').matches; r.dataset.theme = dark ? 'light' : 'dark'; if (state.id && state.view === 'inv') { renderPane(); renderScrub(); } }
  });
  document.addEventListener('keydown', e => {
    if (state.view !== 'inv' || /INPUT|TEXTAREA|SELECT/.test(e.target.tagName)) return;
    if (e.key === 'ArrowRight') { e.preventDefault(); go(state.step + 1); }
    else if (e.key === 'ArrowLeft') { e.preventDefault(); go(state.step - 1); }
    else if (e.key === ' ' && e.target === document.body) { e.preventDefault(); state.playing ? stopPlay() : startPlay(); }
  });
  window.addEventListener('hashchange', () => { const r = applyHash(); if (r && (r.id !== state.id || r.step !== state.step)) select(r.id, r.step); });
}

(async function init() {
  if (q.get('theme')) document.documentElement.dataset.theme = q.get('theme');
  try { state.idx = await getJSON('data/index.json'); }
  catch (e) { $('#pane').innerHTML = '<p class="empty">Could not load data/index.json. Serve this folder over HTTP (for example <code>make serve</code>).</p>'; return; }
  renderStats(); renderResults(); renderArch(); wire();
  const r = applyHash();
  if (state.view === 'inv') await select(r ? r.id : defaultPick(), r ? r.step : state.step);
  if (state.view === 'out' && !location.hash) { /* landing: the story */ }
})();
