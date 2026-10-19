/* Traceback showcase: static, dependency-free. All data comes from data/*.json built by eval/build_site.py. */
'use strict';
const $ = (s, r = document) => r.querySelector(s);
const $$ = (s, r = document) => [...r.querySelectorAll(s)];
const esc = s => String(s ?? '').replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
const pct = (x, d = 1) => (x * 100).toFixed(d) + '%';
const usd = x => (x < 0.01 ? '$' + x.toFixed(4) : '$' + x.toFixed(3));
const SVC = ['gateway', 'orders', 'inventory', 'payments'];
const SYS = [
  { key: 'baseline', label: 'Raw logs → LLM', sub: 'baseline', color: 'var(--c-base)' },
  { key: 'analyzer', label: 'Analyzer only', sub: 'no LLM', color: 'var(--c-an)' },
  { key: 'traceback_unverified', label: 'Analyzer + LLM', sub: 'no verifier', color: 'color-mix(in srgb,var(--c-tb) 60%,var(--surface))' },
  { key: 'traceback', label: 'Full Traceback', sub: 'analyzer + LLM + verifier', color: 'var(--c-tb)' },
];
const q = new URLSearchParams(location.search);
const state = { idx: null, split: 'test', fSplit: 'all', fOut: 'all', id: null, tab: ['inv', 'ana', 'cmp'].includes(q.get('tab')) ? q.get('tab') : 'inv', reveal: q.get('reveal') === '1', cache: {} };

if (q.get('theme')) document.documentElement.dataset.theme = q.get('theme');
document.addEventListener('click', e => { if (e.target.id === 'theme') { const r = document.documentElement, dark = r.dataset.theme ? r.dataset.theme === 'dark' : matchMedia('(prefers-color-scheme: dark)').matches; r.dataset.theme = dark ? 'light' : 'dark'; if (state.id) renderDetail(); } });
async function getJSON(url) { const r = await fetch(url); if (!r.ok) throw new Error(url + ' ' + r.status); return r.json(); }

/* ---------------- results ---------------- */
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
  const bars = (key, max) => SYS.map(sy => {
    const v = s[sy.key][key];
    return `<div class="bar-row"><span>${esc(sy.label)}<br><small class="hint" style="margin:0">${esc(sy.sub)}</small></span><div class="track"><div class="fill" style="width:${Math.max(0, v / max * 100)}%;background:${sy.color}"></div></div><span class="val">${pct(v)}</span></div>`;
  }).join('');
  $('#chart-acc').innerHTML = bars('top1', 1);
  $('#chart-fc').innerHTML = bars('fc', 0.3);
  const row = sy => {
    const v = s[sy.key];
    return `<tr class="${sy.key === 'traceback' ? 'hl' : ''}"><td><b>${esc(sy.label)}</b> <span class="hint">${esc(sy.sub)}</span></td><td class="num">${pct(v.top1)}</td><td class="num">${v.prec_high == null ? '–' : pct(v.prec_high)} <span class="hint">(${v.high} high)</span></td><td class="num">${pct(v.fc)}</td><td class="num">${v.cost ? usd(v.cost) : '$0'}</td><td class="num">${v.latency < 1 ? '< 0.1 s' : v.latency.toFixed(1) + ' s'}</td></tr>`;
  };
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

/* ---------------- explorer list ---------------- */
function listItems() {
  return state.idx.incidents.filter(r =>
    (state.fSplit === 'all' || r.split === state.fSplit) &&
    (state.fOut === 'all' || (state.fOut === 'wrong' && !r.tb_ok) || (state.fOut === 'base-wrong' && !r.base_ok)));
}
function renderList() {
  const items = listItems();
  $('#inc-list').innerHTML = items.length ? items.map(r => `<li tabindex="0" data-id="${r.id}" class="${r.id === state.id ? 'sel' : ''}" role="button" aria-label="Incident ${r.id}">
    <span class="id">${r.id}</span><span class="pill">${r.group}</span><span class="pill ${r.split === 'test' ? 'acc' : ''}">${r.split}</span>
    <span class="tags"><span class="dot ${r.tb_ok ? 'ok' : 'bad'}" title="Traceback ${r.tb_ok ? 'correct' : 'wrong'}"></span><span class="dot ${r.base_ok ? 'ok' : 'bad'}" title="Baseline ${r.base_ok ? 'correct' : 'wrong'}"></span></span></li>`).join('')
    : '<li class="empty">No incidents match.</li>';
  $('#inc-list').insertAdjacentHTML('afterbegin', '<li class="empty" style="font-size:12px;cursor:default;padding:2px 10px">Dots: left = Traceback, right = baseline</li>');
}

/* ---------------- detail ---------------- */
async function select(id, push = true) {
  state.id = id;
  if (push) history.replaceState(null, '', '#' + id);
  renderList();
  $('#detail').innerHTML = '<p class="empty">Loading…</p>';
  if (!state.cache[id]) state.cache[id] = await getJSON(`data/${id}.json`);
  renderDetail();
}
const catLabel = c => esc(String(c || '').replace(/_/g, ' '));
const confPill = c => `<span class="pill ${c === 'high' ? 'ok' : c === 'low' ? 'bad' : 'warn'}">${esc(c)} confidence</span>`;
const okPill = (ok) => state.reveal ? `<span class="pill ${ok ? 'ok' : 'bad'}">${ok ? '✓ matches ground truth' : '✗ does not match ground truth'}</span>` : '';

function renderDetail() {
  const d = state.cache[state.id];
  const sy = d.symptom;
  const tabs = [['inv', 'Investigation'], ['ana', 'Analysis layer'], ['cmp', 'Baseline vs Traceback']];
  $('#detail').innerHTML = `
    <div class="d-head"><h3>${d.id}</h3><span class="pill">${d.group}</span><span class="pill ${d.split === 'test' ? 'acc' : ''}">${d.split} split</span>
      <span class="pill bad">${sy.failed} of ${sy.traces} requests failed (${pct(sy.failed / sy.traces)})</span>
      <button class="reveal-btn" id="reveal">${state.reveal ? 'Hide' : 'Reveal'} ground truth</button></div>
    ${state.reveal ? truthBox(d) : ''}
    <div class="tabs" role="tablist">${tabs.map(([k, l]) => `<button role="tab" data-tab="${k}" class="${state.tab === k ? 'on' : ''}" aria-selected="${state.tab === k}">${l}</button>`).join('')}</div>
    <div id="tab-body" class="stack"></div>`;
  $('#reveal').onclick = () => { state.reveal = !state.reveal; renderDetail(); };
  $$('[data-tab]').forEach(b => b.onclick = () => { state.tab = b.dataset.tab; renderDetail(); });
  const body = $('#tab-body');
  body.innerHTML = state.tab === 'inv' ? tabInvestigation(d) : state.tab === 'ana' ? tabAnalysis(d) : tabCompare(d);
  wireChips(body, d);
}

function truthBox(d) {
  const t = d.truth;
  const edge = t.edge ? ` on the edge ${esc(t.edge.from)} → ${esc(t.edge.to)}` : '';
  return `<div class="truth"><b>Ground truth (never shown to the analyzer, the model or the verifier):</b> <code>${esc(t.type)}</code> in <b>${esc(t.service)}</b>${edge}.
    ${t.decoys.length ? `<br>Decoys present: ${t.decoys.map(x => `<code>${esc(x.type)}</code> in ${esc(x.service)}`).join(', ')}.` : ''}
    ${t.complications.length ? `<br>Injected clock skew: ${t.complications.map(x => `${esc(x.service)} ${(x.offset_us / 1e6).toFixed(2)} s`).join(', ')}.` : ''}</div>`;
}

function chipRow(ids, d) {
  return `<div class="chips">${ids.map(raw => { const id = raw.replace(/^[\s\[]+|[\s\]]+$/g, ''); const known = d.records[id] != null;
    return `<button class="chip ${known ? '' : 'unknown'}" data-rec="${esc(id)}" aria-expanded="false" title="${known ? 'Show record' : 'Record not shown on this page'}">${esc(id)}</button>`; }).join('')}</div><div class="recs"></div>`;
}
function wireChips(root, d) {
  $$('.chip', root).forEach(b => b.onclick = () => {
    const box = b.closest('li, .card, .claim')?.querySelector('.recs') || b.parentElement.nextElementSibling;
    const id = b.dataset.rec, open = b.getAttribute('aria-expanded') === 'true';
    b.setAttribute('aria-expanded', !open);
    let el = box.querySelector(`[data-for="${CSS.escape(id)}"]`);
    if (open) { el?.remove(); return; }
    box.insertAdjacentHTML('beforeend', `<div class="rec" data-for="${esc(id)}"><b>${esc(id)}</b> ${esc(d.records[id] ?? 'record text is not included in this page (the id exists in the incident data)')}</div>`);
  });
}

function verifyBlock(v) {
  const cls = v.status === 'passed' ? 'ok' : v.status === 'failed' ? 'bad' : 'warn';
  const label = { passed: 'Replay verification passed', failed: 'Replay verification failed', inconclusive: 'Replay inconclusive' }[v.status];
  const bars = v.rate_all_enabled > 0 ? `<div class="bar2"><span>observed</span><div class="meter"><i style="width:${Math.min(100, v.rate_observed * 250)}%"></i></div><span>${pct(v.rate_observed)}</span></div>
    <div class="bar2"><span>replay, all on</span><div class="meter"><i style="width:${Math.min(100, v.rate_all_enabled * 250)}%"></i></div><span>${pct(v.rate_all_enabled)}</span></div>
    ${v.matched.length ? `<div class="bar2"><span>${esc(v.matched.join(', '))} off</span><div class="meter"><i style="width:${Math.min(100, v.rate_disabled * 250)}%;background:var(--${cls})"></i></div><span>${pct(v.rate_disabled)}</span></div>` : ''}` : '';
  return `<div class="card"><h3>Counterfactual replay <span class="pill ${cls}">${label}</span></h3><p style="margin:6px 0;color:var(--mut);font-size:14px">${esc(v.reason)}.</p>${bars}<p class="hint" style="margin:6px 0 0">Peak 30-second failure rate of entry requests. The replay was told the environment’s controllable conditions, not which one matters.</p></div>`;
}

function tabInvestigation(d) {
  const tb = d.traceback, a = tb.answer, rc = a.root_cause;
  const conf = tb.final_confidence;
  const chain = a.causal_chain.map(c => `<li class="claim"><span class="pill ${c.status === 'confirmed' ? 'ok' : 'warn'}">${c.status}</span> ${esc(c.claim)}${chipRow(c.cites, d)}</li>`).join('');
  const alts = (a.alternatives || []).map(c => `<li><code>${catLabel(c.category)}</code> in ${esc(c.service)}: ${esc(c.why_less_likely)}${chipRow(c.cites || [], d)}</li>`).join('');
  const ro = d.ruled_out.map(r => `<li>${esc(r.change)} on <b>${esc(r.service)}</b> at ${r.t.toFixed(1)} s: ${esc(r.reason)}</li>`).join('');
  return `
    <div class="card"><div class="rc"><span class="pill acc">Root cause</span><span class="cat">${catLabel(rc.category)}</span><span>in <b>${esc(rc.service)}</b></span>${confPill(conf)}${okPill(tb.correct)}</div>
      <p style="margin:10px 0 0">${esc(rc.summary)}</p>
      <p class="hint" style="margin:6px 0 0">${conf !== tb.llm_confidence ? `The model said “${esc(tb.llm_confidence)}”; replay verification adjusted it to “${esc(conf)}”. ` : ''}${tb.tokens[0].toLocaleString()} prompt + ${tb.tokens[1].toLocaleString()} completion tokens · ${usd(tb.cost)} · ${tb.latency.toFixed(1)} s</p></div>
    <div class="verdict">${verifyBlock(tb.verification)}<div class="card"><h3>Citations</h3><p style="margin:6px 0;color:var(--mut);font-size:14px">Every claim below cites record IDs. Each ID was checked against the incident’s records; the model is asked again if any does not exist. Click an ID to read the record.</p><p class="pill ${tb.accepted ? 'ok' : 'bad'}">${tb.accepted ? 'all cited IDs exist' : 'some cited IDs missing'}</p></div></div>
    <div class="card"><h3>Timeline <span class="hint">clock-aligned; red = share of 5xx, amber strip = unusually slow, per service per 5 s</span></h3><div class="tl-wrap">${timelineSVG(d)}</div>${timelineLegend()}<div class="chips" style="margin-top:8px">${d.timeline.map(e => `<button class="chip" data-rec="${esc(e.id)}" aria-expanded="false" title="${esc(e.text)}">${e.t.toFixed(1)}s ${esc(e.service)}: ${esc(e.kind.replace(/_/g, ' '))}</button>`).join('')}</div><div class="recs"></div></div>
    <div class="card"><h3>Causal chain</h3><ol class="chain">${chain}</ol></div>
    ${alts ? `<div class="card"><h3>Alternatives considered</h3><ul>${alts}</ul></div>` : ''}
    ${ro ? `<div class="card"><h3>Ruled out</h3><ul>${ro}</ul></div>` : ''}`;
}

/* ---------------- timeline SVG ---------------- */
function timelineSVG(d) {
  const lanes = SVC.filter(s => d.series[s]);
  const all = lanes.flatMap(s => d.series[s].map(m => m.t));
  const tmin = Math.min(-2, ...all.map(t => t - 5)), tmax = Math.max(...all) + 1;
  const W = 1000, L = 92, R = 14, T = 30, LH = 34, H = T + lanes.length * LH + 26;
  const x = t => L + (t - tmin) / (tmax - tmin) * (W - L - R);
  let g = '';
  for (let t = Math.ceil(tmin / 10) * 10; t <= tmax; t += 10) g += `<line x1="${x(t)}" x2="${x(t)}" y1="${T - 4}" y2="${H - 22}" stroke="var(--line)"/><text x="${x(t)}" y="${H - 8}" text-anchor="middle">${t}s</text>`;
  lanes.forEach((s, i) => {
    const y = T + i * LH;
    g += `<text class="lane-label" x="${L - 10}" y="${y + 21}" text-anchor="end">${s}</text><rect x="${L}" y="${y + 3}" width="${W - L - R}" height="${LH - 6}" rx="4" fill="var(--surface2)"/>`;
    d.series[s].forEach(m => {
      const rate = m.req ? m.err / m.req : 0;
      if (rate > 0.005) g += `<rect x="${x(m.t - 5)}" y="${y + 3}" width="${Math.max(1, x(m.t) - x(m.t - 5) - 1)}" height="${LH - 6}" rx="2" fill="rgba(var(--heat),${Math.min(1, 0.15 + rate * 1.4).toFixed(2)})"><title>${s} ${m.t.toFixed(0)}s: ${m.err}/${m.req} 5xx, p99 ${m.p99} ms, queue ${m.q}, workers ${m.busy}/${m.wl}, pool ${m.pool}</title></rect>`;
    });
    // latency strip: p99 far above this service's own typical p99 (slow service without many 5xx)
    const p99s = d.series[s].map(m => m.p99).sort((a, b) => a - b), med = p99s[Math.floor(p99s.length / 2)] || 1;
    d.series[s].forEach(m => { const r = m.p99 / Math.max(med, 5);
      if (m.req && r > 3 && m.p99 > 100) g += `<rect x="${x(m.t - 5)}" y="${y + LH - 11}" width="${Math.max(1, x(m.t) - x(m.t - 5) - 1)}" height="7" rx="2" fill="var(--warn)" opacity="${Math.min(1, 0.35 + r / 30).toFixed(2)}"><title>${s} ${m.t.toFixed(0)}s: p99 ${m.p99} ms vs typical ${med.toFixed(0)} ms</title></rect>`; });
    // metrics gap marker (service silent)
    const ts = d.series[s].map(m => m.t);
    for (let k = 1; k < ts.length; k++) if (ts[k] - ts[k - 1] > 7.5) g += `<rect x="${x(ts[k - 1])}" y="${y + 3}" width="${x(ts[k]) - x(ts[k - 1])}" height="${LH - 6}" fill="none" stroke="var(--warn)" stroke-dasharray="4 3" rx="3"><title>${s}: no metrics for ${(ts[k] - ts[k - 1]).toFixed(0)} s (stopped?)</title></rect>`;
  });
  const lane = s => Math.max(0, lanes.indexOf(s));
  d.hypotheses.slice(0, 3).forEach((h, i) => { if (h.onset == null) return; const y = T + lane(h.service) * LH;
    g += `<path d="M${x(h.onset) - 5} ${y + LH - 2} l5 -9 l5 9z" fill="var(--c-tb)"><title>H${h.rank} ${h.category} onset ${h.onset.toFixed(1)}s</title></path><text x="${x(h.onset)}" y="${y + 9}" text-anchor="middle" style="fill:var(--c-tb);font-weight:700">H${h.rank}</text>`; });
  d.timeline.forEach(e => { const col = e.kind === 'symptom_onset' ? 'var(--bad)' : e.kind.endsWith('_change') ? 'var(--warn)' : 'var(--c-tb)';
    if (e.kind === 'anomaly_onset' || e.kind === 'metric') return;
    g += `<line x1="${x(e.t)}" x2="${x(e.t)}" y1="${T - 4}" y2="${H - 22}" stroke="${col}" stroke-width="2" ${e.kind === 'symptom_onset' ? 'stroke-dasharray="5 3"' : ''}/><circle cx="${x(e.t)}" cy="${T - 10}" r="5" fill="${col}"><title>${esc(e.text)} (${e.t.toFixed(1)} s)</title></circle>`; });
  return `<svg viewBox="0 0 ${W} ${H}" role="img" aria-label="Timeline of per-service error rate with change events and hypothesis onsets" style="min-width:640px;width:100%">${g}</svg>`;
}
const timelineLegend = () => `<div class="legend"><span><i style="background:rgba(var(--heat),.8)"></i>5xx rate</span><span><i style="background:var(--warn);height:6px;vertical-align:1px"></i>p99 latency far above normal</span><span><i style="background:var(--bad)"></i>symptom onset</span><span><i style="background:var(--warn)"></i>deploy / config / restart</span><span><i style="background:var(--c-tb)"></i>candidate onset (H1…)</span><span><i style="border:1px dashed var(--warn)"></i>metrics silent</span></div>`;

/* ---------------- analysis tab ---------------- */
function graphSVG(d) {
  const pos = { gateway: [80, 115], orders: [290, 115], inventory: [520, 42], payments: [520, 188] };
  const top = d.hypotheses[0];
  let g = '';
  const maxCalls = Math.max(...d.graph.map(e => e.calls));
  d.graph.forEach(e => {
    const a = pos[e.caller], b = pos[e.callee]; if (!a || !b) return;
    const fr = e.failed / e.calls, w = 1.5 + 5 * e.calls / maxCalls, col = fr > 0.02 ? `rgba(var(--heat),${Math.min(1, 0.35 + fr * 1.2).toFixed(2)})` : 'var(--line)';
    g += `<line x1="${a[0] + 50}" y1="${a[1]}" x2="${b[0] - 50}" y2="${b[1]}" stroke="${col}" stroke-width="${w.toFixed(1)}" marker-end="url(#ar)"/><text x="${(a[0] + b[0]) / 2}" y="${(a[1] + b[1]) / 2 - 9}" text-anchor="middle" style="font-size:13px">${pct(fr, 0)} failed${e.retried_attempts ? ' · ' + e.retried_attempts + ' retries' : ''}</text>`;
  });
  Object.entries(pos).forEach(([s, [px, py]]) => {
    const hyps = d.hypotheses.filter(h => h.service === s || (h.edge && (h.edge.from === s || h.edge.to === s)));
    const isTop = top && (top.service === s);
    g += `<rect x="${px - 50}" y="${py - 22}" width="100" height="44" rx="10" fill="var(--surface)" stroke="${isTop ? 'var(--c-tb)' : 'var(--line)'}" stroke-width="${isTop ? 3 : 1.5}"/><text class="lane-label" x="${px}" y="${py + 5}" text-anchor="middle" style="font-size:14px">${s}</text>${hyps.length ? `<text x="${px}" y="${py - 28}" text-anchor="middle" style="fill:var(--c-tb);font-weight:700;font-size:13px">${hyps.slice(0, 3).map(h => 'H' + h.rank).join(' ')}</text>` : ''}`;
  });
  return `<svg viewBox="0 0 640 235" role="img" aria-label="Dependency graph with failure rates" class="graph"><defs><marker id="ar" viewBox="0 0 10 10" refX="8" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path d="M0 0L10 5L0 10z" fill="var(--mut)"/></marker></defs>${g}</svg>`;
}

function tabAnalysis(d) {
  const rows = d.hypotheses.map(h => `<tr><td>H${h.rank}</td><td><code>${catLabel(h.category)}</code><br>${esc(h.service)}${h.edge ? ` <span class="hint">(${esc(h.edge.from)} → ${esc(h.edge.to)})</span>` : ''}</td>
    <td style="min-width:120px"><div class="meter"><i style="width:${h.score * 100}%"></i></div><span class="hint">score ${h.score} · covers ${pct(h.coverage, 0)} of failures</span></td><td>${esc(h.summary)}
    <details><summary>features and evidence</summary><pre>${esc(JSON.stringify(h.features, null, 1))}</pre>${chipRow(h.evidence.map(e => e.id), d)}</details></td></tr>`).join('');
  const c = d.clock, off = SVC.filter(s => c.offsets[s] != null).map(s => [s, c.offsets[s]]);
  const maxOff = Math.max(0.05, ...off.map(([, v]) => Math.abs(v)));
  const clock = off.map(([s, v]) => `<div class="bar2"><span>${s}</span><div class="meter"><i style="width:${Math.abs(v) / maxOff * 100}%;background:${s === c.reference ? 'var(--c-base)' : 'var(--c-an)'}"></i></div><span>${(v * 1000).toFixed(0)} ms</span></div>`).join('');
  const inj = state.reveal && Object.keys(c.injected).length ? `<p class="hint">Injected skew (revealed): ${Object.entries(c.injected).map(([s, v]) => `${s} ${v.toFixed(2)} s`).join(', ')}. Differences between services are recovered to within a few milliseconds.</p>` : '';
  return `<div class="stack"><div class="card"><h3>Dependency graph <span class="hint">from client spans</span></h3>${graphSVG(d)}</div>
    <div class="card"><h3>Clock alignment <span class="hint">offset vs ${esc(c.reference)}, from causal span pairs</span></h3>${clock}<p class="hint">Logs from each service use their own clock. The offsets above are estimated only from parent/child span pairs (no labels, no metadata) and used to order events across services.</p>${inj}</div></div>
    <div class="card"><h3>Candidate causes <span class="hint">deterministic rules; no language model</span></h3><div class="table-wrap"><table><thead><tr><th>#</th><th>Candidate</th><th>Strength</th><th>What it saw</th></tr></thead><tbody>${rows}</tbody></table></div></div>`;
}

/* ---------------- compare tab ---------------- */
function sysCard(title, sub, r, d, extra) {
  if (!r || !r.answer) return `<div class="card"><h3>${title}</h3><p class="empty">No answer produced.</p></div>`;
  const a = r.answer, rc = a.root_cause, conf = r.final_confidence || a.confidence;
  const confirmed = a.causal_chain.filter(c => c.status === 'confirmed').length;
  const cites = a.causal_chain.reduce((n, c) => n + c.cites.length, 0);
  return `<div class="card"><h3>${title} <span class="hint">${sub}</span></h3>
    <div class="rc" style="margin:8px 0"><span class="cat">${catLabel(rc.category)}</span><span>in <b>${esc(rc.service)}</b></span></div>
    <div class="rc" style="margin-bottom:8px">${confPill(conf)}${okPill(r.correct)}</div>
    <p style="margin:6px 0">${esc(rc.summary)}</p>
    <dl class="kv"><dt>claims</dt><dd>${a.causal_chain.length} (${confirmed} confirmed) · ${cites} citations</dd><dt>input</dt><dd>${r.tokens[0].toLocaleString()} tokens${r.attempts > 1 ? ` across ${r.attempts} attempts (earlier answers cited unknown IDs or were malformed)` : ''}</dd><dt>cost</dt><dd>${usd(r.cost)}</dd><dt>latency</dt><dd>${r.latency.toFixed(1)} s</dd>${extra || ''}</dl>
    <details><summary>causal chain</summary><ol class="chain">${a.causal_chain.map(c => `<li class="claim"><span class="pill ${c.status === 'confirmed' ? 'ok' : 'warn'}">${c.status}</span> ${esc(c.claim)}${chipRow(c.cites, d)}</li>`).join('')}</ol></details></div>`;
}
function tabCompare(d) {
  const b = d.baseline, tb = d.traceback;
  const bx = b ? `<dt>input</dt><dd>${b.truncated ? `sampled: ${pct(b.kept, 0)} of trace groups fit the context window` : 'complete (warn/error logs, metrics, events)'}</dd>` : '';
  return `<p class="sub" style="margin:0">Same model, same output format. The baseline reads the raw warn/error logs, metrics and events (grouped by trace, raw clocks). Traceback reads the analysis layer’s candidates and evidence.</p>
    <div class="two">${sysCard('Raw logs → LLM', 'baseline', b && { ...b, correct: b.correct }, d, bx)}${sysCard('Full Traceback', 'analyzer + LLM + verifier', { ...tb, correct: tb.correct }, d, `<dt>verification</dt><dd>${esc(tb.verification.status)}</dd>`)}</div>
    <p class="hint">${state.reveal ? '' : 'Reveal the ground truth (button above) to mark each answer right or wrong.'}</p>`;
}

/* ---------------- architecture ---------------- */
function renderArch() {
  const box = (x, y, w, t1, t2, kind) => `<g><rect x="${x}" y="${y}" width="${w}" height="76" rx="12" fill="var(--surface)" stroke="${kind === 'llm' ? 'var(--c-an)' : kind === 'ver' ? 'var(--warn)' : 'var(--c-tb)'}" stroke-width="2"/><text x="${x + w / 2}" y="${y + 31}" text-anchor="middle" class="lane-label" style="font-size:13px">${t1}</text><text x="${x + w / 2}" y="${y + 52}" text-anchor="middle" style="font-size:11.5px">${t2}</text></g>`;
  const ar = (x1, y1, x2, y2) => `<line x1="${x1}" y1="${y1}" x2="${x2}" y2="${y2}" stroke="var(--mut)" stroke-width="2" marker-end="url(#ah)"/>`;
  $('#arch').innerHTML = `<svg viewBox="0 0 1000 330" role="img" aria-label="Pipeline architecture"><defs><marker id="ah" viewBox="0 0 10 10" refX="8" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0L10 5L0 10z" fill="var(--mut)"/></marker></defs>
    ${box(20, 20, 210, 'Telemetry', 'logs · spans · metrics · deploys', 'det')}${ar(230, 58, 262, 58)}
    ${box(265, 20, 210, 'Clock alignment', 'offsets from causal span pairs', 'det')}${ar(475, 58, 507, 58)}
    ${box(510, 20, 210, 'Correlator + graph', 'traces, ordering, dependencies', 'det')}${ar(720, 58, 752, 58)}
    ${box(755, 20, 225, 'Hypothesis rules', 'ranked candidates + evidence IDs', 'det')}
    ${ar(867, 96, 867, 154)}
    ${box(755, 158, 225, 'LLM reasoner', 'Nemotron on Nebius: rank, explain, cite', 'llm')}${ar(755, 196, 723, 196)}
    ${box(510, 158, 210, 'Citation validator', 'reject unknown IDs, ask again', 'det')}${ar(510, 196, 478, 196)}
    ${box(265, 158, 210, 'Replay verifier', 'disable suspected cause, re-run', 'ver')}${ar(265, 196, 233, 196)}
    ${box(20, 158, 210, 'Incident report', 'timeline · chain · evidence · verdict', 'det')}
    <g transform="translate(20 270)"><rect width="14" height="14" rx="3" fill="none" stroke="var(--c-tb)" stroke-width="2"/><text x="22" y="12">deterministic code (C++)</text><rect x="220" width="14" height="14" rx="3" fill="none" stroke="var(--c-an)" stroke-width="2"/><text x="242" y="12">language model</text><rect x="380" width="14" height="14" rx="3" fill="none" stroke="var(--warn)" stroke-width="2"/><text x="402" y="12">counterfactual check</text></g></svg>`;
}

/* ---------------- wiring ---------------- */
function wire() {
  $$('#results [data-split]').forEach(b => b.onclick = () => { state.split = b.dataset.split; renderResults(); });
  $$('#f-split button').forEach(b => b.onclick = () => { state.fSplit = b.dataset.v; $$('#f-split button').forEach(x => x.classList.toggle('on', x === b)); renderList(); });
  $$('#f-out button').forEach(b => b.onclick = () => { state.fOut = b.dataset.v; $$('#f-out button').forEach(x => x.classList.toggle('on', x === b)); renderList(); });
  $('#inc-list').addEventListener('click', e => { const li = e.target.closest('li[data-id]'); if (li) select(li.dataset.id); });
  $('#inc-list').addEventListener('keydown', e => { if ((e.key === 'Enter' || e.key === ' ') && e.target.dataset.id) { e.preventDefault(); select(e.target.dataset.id); } });
}

(async function init() {
  try {
    state.idx = await getJSON('data/index.json');
  } catch (e) { $('#detail').innerHTML = '<p class="empty">Could not load data/index.json. Serve this folder over HTTP (for example <code>python3 -m http.server -d docs</code>).</p>'; return; }
  renderStats(); renderResults(); renderArch(); wire();
  const want = location.hash.slice(1);
  const pick = state.idx.incidents.find(r => r.id === want)
    || state.idx.incidents.find(r => r.split === 'test' && !r.base_ok && r.tb_ok && r.group === 'double')
    || state.idx.incidents.find(r => r.split === 'test' && !r.base_ok && r.tb_ok)
    || state.idx.incidents.find(r => r.split === 'test');
  await select(pick.id, !!want);
})();
