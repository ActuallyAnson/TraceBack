/* "The outage": failure-first opening. Shares globals with app.js (state, $, esc, pct, usd, catLabel, SVC, select, showView).
   Act 1 an outage happens, Act 2 the obvious tool (raw logs -> LLM) answers, Act 3 the checkable investigation.
   Provenance is always on screen: the simulation is either run live by `make live` or a recorded replay (static site);
   model answers are recorded from real runs unless the viewer opts into a live call (which has a time limit and a recorded fallback). */
'use strict';
const TOUR = [
  { id: 'inc-026', why: 'picked to show a case where the raw-logs answer fails and Traceback gets it right' },
  { id: 'inc-003', why: 'picked to show a case where Traceback itself is wrong' },
];
const ST = { health: null, tour: 0, mode: 'tour', id: null, d: null, prov: null, act: 0, revealed: false, busy: false, err: '', tally: [], liveModel: false, picked: '' };

async function probeHealth() {
  try {
    const c = new AbortController(); const t = setTimeout(() => c.abort(), 1500);
    const r = await fetch('/api/health', { signal: c.signal, cache: 'no-store' }); clearTimeout(t);
    if (r.ok) { const j = await r.json(); if (j && j.live) return j; }
  } catch (e) { /* static hosting */ }
  return null;
}
const isLive = () => !!ST.health;
function badge() {
  return isLive()
    ? `<span class="prov live" title="The simulator, analyzer and verifier run on this machine when you press the button.">● SIMULATED · RUN LIVE</span>`
    : `<span class="prov rec" title="This page is static. The incident was simulated earlier; nothing runs in your browser. Run make live to execute the pipeline yourself.">◌ SIMULATED · RECORDED REPLAY</span>`;
}
function provLine() {
  const p = ST.prov; if (!p) return '';
  const model = { recorded: 'recorded (replayed from the committed response cache)', live: 'called live just now', 'recorded-fallback': 'RECORDED FALLBACK (the live call did not complete)' }[p.model] || p.model;
  return `<p class="provline"><b>Where this came from.</b> Simulation: ${esc(p.simulation)}${p.sim_ms != null ? ` (${p.sim_ms} ms)` : ''}. Model answers: ${esc(model)}.${p.note ? ' ' + esc(p.note) + '.' : ''} <b>Why this outage:</b> ${esc(ST.picked)}.</p>`;
}

function spark(ser, key, W, H, ymax, onset, tmin, tmax) {
  const x = t => 4 + (t - tmin) / Math.max(1e-9, tmax - tmin) * (W - 8), y = v => H - 4 - Math.min(1, v / ymax) * (H - 10);
  const pts = ser.map(m => `${x(m.t).toFixed(1)},${y(key(m)).toFixed(1)}`).join(' ');
  const ox = onset != null ? `<line x1="${x(onset)}" x2="${x(onset)}" y1="2" y2="${H - 2}" stroke="var(--warn)" stroke-dasharray="3 3"/>` : '';
  return `<svg viewBox="0 0 ${W} ${H}" width="100%" role="img" aria-hidden="true"><rect x="0" y="0" width="${W}" height="${H}" rx="6" fill="var(--surface2)"/>${ox}<polyline points="${pts}" fill="none" stroke="var(--bad)" stroke-width="2" stroke-linejoin="round"/></svg>`;
}
function loudest(d) {
  let best = null;
  SVC.forEach(s => { const ser = d.series[s] || []; const pk = Math.max(0, ...ser.map(m => m.req ? m.err / m.req : 0)); if (!best || pk > best.pk) best = { s, pk }; });
  return best;
}
function act1(d) {
  const all = SVC.flatMap(s => (d.series[s] || []).map(m => m.t)), tmin = Math.min(...all), tmax = Math.max(...all), sy = d.symptom;
  const gw = d.series.gateway || [];
  const rate = m => m.req ? m.err / m.req : 0;
  const ymaxG = Math.max(0.05, ...gw.map(rate));
  const loud = loudest(d);
  const minis = SVC.filter(s => d.series[s]).map(s => {
    const pk = Math.max(0, ...d.series[s].map(m => m.req ? m.err / m.req : 0));
    return `<div class="mini"><div class="mini-h"><b>${s}</b><span class="hint">peak 5xx rate ${pct(pk, 0)}</span></div>${spark(d.series[s], m => (m.req ? m.err / m.req : 0), 260, 54, Math.max(0.05, ymaxG), sy.onset, tmin, tmax)}</div>`;
  }).join('');
  return `<div class="act-body"><p class="act-lede">${sy.failed} of ${sy.traces} checkout requests failed (${pct(sy.failed / sy.traces)}). Failures begin at +${(sy.onset ?? 0).toFixed(0)} s. Four services, ${d.counts.logs.toLocaleString()} log lines and ${d.counts.spans.toLocaleString()} spans in the window.</p>
    <div class="card"><h3>What the on-call engineer sees</h3><div class="big-spark">${spark(gw, rate, 900, 110, ymaxG, sy.onset, tmin, tmax)}</div><p class="hint" style="margin:6px 0 10px">Entry (gateway) failure rate, 5-second buckets. Dashed line: start of the sustained failure burst.</p>
    <div class="minis">${minis}</div>
    <p class="hint" style="margin:10px 0 0">Loudest service by 5xx rate: <b>${esc(loud.s)}</b>. Is that where the problem is? That is the question.</p></div></div>`;
}
function rawBlock(d) {
  const b = d.baseline;
  if (!b || !b.answer) return '<p class="empty">The baseline produced no valid answer for this incident.</p>';
  const a = b.answer;
  return `<div class="act-body"><p class="act-lede">The obvious move: put the logs in a language model and ask. Same model, same output format as Traceback, raw time-sorted logs grouped by trace${b.truncated ? ' (truncated to fit the context window here)' : ''}.</p>
    <div class="card"><div class="rc"><span class="pill acc">Raw-logs answer</span><span class="cat">${catLabel(a.root_cause.category)}</span><span>in <b>${esc(a.root_cause.service)}</b></span>${confPill(a.confidence)}</div>
      <p style="margin:10px 0 4px">${esc(a.root_cause.summary)}</p>
      <p class="hint" style="margin:0">${b.tokens[0].toLocaleString()} prompt tokens · ${usd(b.cost)} · ${b.latency.toFixed(0)} s${b.first_invalid ? ` · its first reply cited ${b.first_invalid} record ID${b.first_invalid > 1 ? 's' : ''} that do not exist; the harness rejected it and asked again` : ''}</p>
      <details style="margin-top:10px"><summary>The model’s actual reply, verbatim (recorded output, unedited)</summary><pre class="raw">${esc(b.raw || JSON.stringify(a, null, 2))}</pre></details></div></div>`;
}
function verdictBox(d) {
  const t = d.truth, b = d.baseline, bl = b && b.answer;
  const edge = t.edge ? ` on the edge ${esc(t.edge.from)} → ${esc(t.edge.to)}` : '';
  const loud = loudest(d);
  let line;
  if (!bl) line = 'The baseline gave no valid answer.';
  else if (b.correct) line = `The raw-logs answer was <b>correct</b> here, so this outage does not show the baseline failing.`;
  else {
    const sameSvc = bl.root_cause.service === t.service;
    line = `The raw-logs answer was <b>wrong</b> (${sameSvc ? 'right service, wrong kind of fault' : 'wrong service'}) and said “${esc(bl.confidence)}” confidence.`;
  }
  return `<div class="truth"><b>Ground truth</b> (known to the simulator; never shown to the analyzer, the model or the verifier): <code>${esc(t.type)}</code> in <b>${esc(t.service)}</b>${edge}.${t.decoys.length ? ` Decoys: ${t.decoys.map(x => `<code>${esc(x.type)}</code> in ${esc(x.service)}`).join(', ')}.` : ''}<br>${line}<br>The loudest service on the dashboard was <b>${esc(loud.s)}</b>${loud.s === t.service ? ', which was the right place to look.' : ', not the root cause.'}</div>`;
}
function act3(d) {
  const tb = d.traceback, a = tb.answer, an = d.analyzer, v = tb.verification, cs = d.cite_summary;
  const top = d.hypotheses[0], inj = Object.entries(d.clock.injected || {}).filter(([, x]) => Math.abs(x) > 0.05);
  const clockTxt = inj.length ? `Clock skew injected and recovered: ${inj.map(([s, x]) => `${esc(s)} ${x.toFixed(2)} s (estimated ${(d.clock.offsets[s] ?? 0).toFixed(2)} s)`).join(', ')}.` : 'No clock skew in this incident.';
  const rep = d.replays[d.answer_spec];
  const repTxt = v.status === 'passed' ? `Replay passed: switching the suspected cause off drops the peak failure rate from ${pct(v.rate_observed, 0)} to ${pct(v.rate_disabled, 0)}.`
    : v.status === 'failed' ? `Replay failed: switching the suspected cause off still leaves ${pct(v.rate_disabled, 0)} failures, so the answer was rejected.`
    : `Replay inconclusive: ${esc(v.reason)}. An unverified answer cannot be “high” confidence.`;
  let honest = '';
  if (!tb.correct) {
    honest = `<div class="honest"><b>Traceback got this one wrong, and it said it wasn’t sure.</b> ${an.correct ? `The deterministic analyzer’s top candidate was actually right (${esc(an.confidence)} confidence), and the language model overruled it. ` : 'Neither the analyzer’s top candidate nor the model’s answer matched the truth. '}${v.status === 'passed' ? 'The replay of the wrong answer passed, which is a miss the verifier cannot catch here. ' : `The replay was ${esc(v.status)}, so the final confidence was ${esc(tb.final_confidence)}, not high. `}Wrong answers that look unsure are what a human can act on safely; this is the uncertainty moment, not a hidden failure.</div>`;
  } else if (tb.final_confidence !== 'high') {
    honest = `<div class="honest ok"><b>Correct, with calibrated confidence.</b> Final confidence is ${esc(tb.final_confidence)} because the replay was ${esc(v.status)}.</div>`;
  }
  const t = state.idx.summary.test;
  return `<div class="act-body"><p class="act-lede">Same incident, different process: deterministic analysis first, then the model may only rank and explain, every claim must cite real records, and the suspected cause is switched off in a replay.</p>
    <div class="card"><div class="rc"><span class="pill acc">Traceback answer</span><span class="cat">${catLabel(a.root_cause.category)}</span><span>in <b>${esc(a.root_cause.service)}</b></span>${confPill(tb.final_confidence)}${okPill(tb.correct)}</div>
      <p style="margin:10px 0 4px">${esc(a.root_cause.summary)}</p>
      <ol class="stages">
        <li><b>Clocks.</b> ${clockTxt}</li>
        <li><b>Analyzer (no LLM).</b> Top candidate: ${catLabel(top.category)} in ${esc(top.service)} (strength ${top.strength.toFixed(2)}, covers ${pct(top.coverage, 0)} of failed traces)${state.reveal ? ' · ' + (an.correct ? '✓' : '✗') : ''}.</li>
        <li><b>Citations.</b> ${cs.unique} distinct records cited; ${cs.first_invalid ? `${cs.first_invalid} did not exist on the first try and were rejected` : 'all existed on the first try'}${cs.attempts > 1 ? ` (${cs.attempts} attempts)` : ''}; ${cs.supported} of ${cs.unique} appear in the analyzer’s own evidence for that hypothesis.</li>
        <li><b>Replay.</b> ${repTxt}</li>
        <li><b>Final.</b> Model said “${esc(tb.llm_confidence)}”, verification ${esc(v.status)}, shipped as “${esc(tb.final_confidence)}” · ${usd(tb.cost)} · ${tb.latency.toFixed(0)} s.</li></ol>
      ${honest}</div>
    <p class="hint">Across all ${t.traceback.n} held-out incidents: Traceback ${pct(t.traceback.top1)} correct and ${pct(t.traceback.fc)} confidently wrong; raw-logs baseline ${pct(t.baseline.top1)} and ${pct(t.baseline.fc)}, at about ${(t.baseline.cost / t.traceback.cost).toFixed(0)}× the cost. The accuracy difference is within noise; the checkable, calibrated part is the point. This outage is one draw, not the average.</p></div>`;
}
function tallyLine() {
  const T = ST.tally; if (!T.length) return '';
  const n = T.length, c = f => T.filter(f).length;
  return `<p class="tally">This session, ${n} outage${n > 1 ? 's' : ''} (including the ones that didn’t go well): raw-logs answer correct ${c(x => x.bl)} of ${n}, confidently wrong ${c(x => !x.bl && x.blConf === 'high')}; Traceback correct ${c(x => x.tb)} of ${n}, confidently wrong ${c(x => !x.tb && x.tbConf === 'high')}.</p>`;
}

function actCard(n, title, body, state_) {
  return `<section class="act ${state_}" aria-label="Act ${n}"><div class="act-head"><span class="act-n">${n}</span><h2>${title}</h2></div>${body}</section>`;
}
function renderStory() {
  const el = $('#story'); if (!el) return;
  const d = ST.d;
  const tourPick = TOUR[ST.tour % TOUR.length];
  const nextLabel = ST.mode === 'tour' ? (d ? 'Next outage in the tour' : 'Trigger an outage') : (d ? 'Another random outage' : 'Trigger an outage');
  const liveOpt = isLive() ? `<label class="chk"><input type="checkbox" id="live-model" ${ST.liveModel ? 'checked' : ''} ${ST.health.key ? '' : 'disabled'}> Call the model live${ST.health.key ? ` (limit ${ST.health.model_timeout_s} s, falls back to the recorded answer)` : ' (needs NEBIUS_API_KEY; not set)'}</label>` : '';
  let h = `<div class="story-top"><div><p class="eyebrow">Traceback · incident investigation you can check</p><h1>Something is failing in production. Who do you trust?</h1>
    <p class="lede">A small multi-service backend starts failing. First the obvious approach, logs into a language model. Then the same incident through Traceback: deterministic analysis, cited evidence, and a replay that switches the suspected cause off.</p></div>
    <div class="story-ctl">${badge()}<div class="seg small" role="group" aria-label="Which outage" id="mode-seg"><button data-m="tour" class="${ST.mode === 'tour' ? 'on' : ''}">Guided tour</button><button data-m="random" class="${ST.mode === 'random' ? 'on' : ''}">Random held-out outage</button></div>
    <button class="primary" id="trigger" ${ST.busy ? 'disabled' : ''}>${ST.busy ? 'Running…' : (ST.mode === 'tour' ? `▶ ${nextLabel}` : `▶ ${nextLabel}`)}</button>${liveOpt}
    <p class="hint" style="margin:0">${ST.mode === 'tour' ? `Tour pick ${(ST.tour % TOUR.length) + 1} of ${TOUR.length}: ${esc(tourPick.why)}. The tour is chosen, not random.` : 'Uniformly random from the 26 held-out test incidents. Nothing is filtered for success.'}</p></div></div>`;
  if (ST.err) h += `<p class="story-err" role="alert">${esc(ST.err)}</p>`;
  if (!d) { h += `<div class="card empty-card"><p>Press <b>Trigger an outage</b>. ${isLive() ? 'The simulator will generate fresh telemetry on this machine and run the full pipeline.' : 'This static site replays recorded simulated incidents; <code>make live</code> runs the pipeline for real.'}</p></div>`; el.innerHTML = h; wireStory(); return; }
  h += provLine();
  h += actCard(1, `${esc(d.id)}: something breaks`, act1(d), 'on');
  if (ST.act >= 1) h += actCard(2, 'The obvious approach: logs into an LLM', rawBlock(d) + (ST.revealed ? verdictBox(d) : '') + (ST.act === 1 ? '<p class="act-actions"><button class="btn" id="check">Check it against the ground truth</button></p>' : ''), 'on');
  else h += `<p class="act-actions"><button class="btn" id="to2">Ask the obvious tool →</button></p>`;
  if (ST.act >= 3) h += actCard(3, 'Now use Traceback', act3(d), 'on') + `<p class="act-actions"><button class="primary" id="walk">Walk through the evidence step by step →</button><button class="btn" id="again">${ST.mode === 'tour' ? 'Next outage in the tour' : 'Another random outage'}</button></p>` + tallyLine();
  else if (ST.act === 2 || (ST.act === 1 && ST.revealed)) h += `<p class="act-actions"><button class="primary" id="to3">Now run the same incident through Traceback →</button></p>`;
  el.innerHTML = h; wireStory();
}
function wireStory() {
  const el = $('#story');
  $('#trigger')?.addEventListener('click', trigger);
  $$('#mode-seg button', el).forEach(b => b.onclick = () => { ST.mode = b.dataset.m; renderStory(); });
  $('#live-model')?.addEventListener('change', e => { ST.liveModel = e.target.checked; });
  $('#to2')?.addEventListener('click', () => { ST.act = 1; renderStory(); scrollAct(2); });
  $('#check')?.addEventListener('click', () => { ST.revealed = true; ST.act = 2; record(); renderStory(); scrollAct(2); });
  $('#to3')?.addEventListener('click', () => { ST.act = 3; state.reveal = true; renderStory(); scrollAct(3); });
  $('#walk')?.addEventListener('click', () => { state.cache[ST.id] = ST.d; state.reveal = true; showView('inv'); select(ST.id, 0); window.scrollTo({ top: 0 }); });
  $('#again')?.addEventListener('click', trigger);
}
function scrollAct(n) { const a = $$('.act')[n - 1]; if (a && a.scrollIntoView) a.scrollIntoView({ block: 'start', behavior: reduce ? 'auto' : 'smooth' }); }
function record() {
  const d = ST.d; if (!d || ST.tally.some(x => x.id === d.id + ':' + ST.runN)) return;
  ST.tally.push({ id: d.id + ':' + ST.runN, bl: !!(d.baseline && d.baseline.correct), blConf: d.baseline && d.baseline.answer ? d.baseline.answer.confidence : 'none', tb: d.traceback.correct, tbConf: d.traceback.final_confidence });
}
async function trigger() {
  if (ST.busy) return;
  const L = state.idx.incidents.filter(r => r.split === 'test');
  const id = ST.mode === 'tour' ? TOUR[ST.tour++ % TOUR.length].id : L[Math.floor(Math.random() * L.length)].id;
  ST.busy = true; ST.err = ''; ST.runN = (ST.runN || 0) + 1; ST.act = 0; ST.revealed = false; state.reveal = false; renderStory();
  let d, prov;
  try {
    if (isLive()) {
      const c = new AbortController(); const t = setTimeout(() => c.abort(), (ST.health.model_timeout_s * 2 + 30) * 1000);
      const r = await fetch(`/api/run?inc=${id}&model=${ST.liveModel ? 'live' : 'recorded'}`, { signal: c.signal, cache: 'no-store' }); clearTimeout(t);
      if (!r.ok) throw new Error('server returned ' + r.status);
      d = await r.json(); prov = d.provenance;
    }
  } catch (e) { ST.err = `The live run failed (${e.message}); showing the recorded replay instead.`; d = null; }
  if (!d) {
    try { d = state.cache[id] || await getJSON(`data/${id}.json`); } catch (e) { ST.busy = false; ST.err = 'Could not load incident data.'; renderStory(); return; }
    prov = { simulation: 'recorded replay of a simulated incident (not executed in your browser)', model: 'recorded', note: isLive() ? 'live run unavailable, so this is the recorded replay' : 'static site: run make live to execute the pipeline yourself' };
  }
  state.cache[id] = d; ST.id = id; ST.picked = ST.mode === 'tour' ? TOUR.find(x => x.id === id).why : 'random draw from the held-out test set, not filtered'; ST.d = d; ST.prov = prov; ST.busy = false; ST.act = 0;
  renderStory();
}
(async function initStory() {
  ST.health = await probeHealth();
  const wait = setInterval(() => { if (typeof state !== 'undefined' && state.idx) { clearInterval(wait); renderStory(); } }, 50);
})();
