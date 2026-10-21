#!/usr/bin/env python3
"""Synthetic checks for the batch-4 fixes; no real or simulator data.
1. Converter v3 re-parents a span whose parent was an internal span; the frozen converter (v2) leaves it detached.
2. Analyzer v4 counts an entry request answered 4xx as failed when a descendant failed natively (the shape found in the diagnosis); v3 does not."""
import json, pathlib, subprocess, sys, tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
W0 = 1_700_000_000_000_000
ok = True


def attr(k, v):
    return {"key": k, "value": ({"intValue": str(v)} if isinstance(v, int) else {"stringValue": v})}


def otlp_span(tid, sid, parent, name, kind, start, dur, status=None, http=None):
    sp = {"traceId": tid, "spanId": sid, "name": name, "kind": kind, "startTimeUnixNano": str((W0 + start) * 1000), "endTimeUnixNano": str((W0 + start + dur) * 1000), "attributes": []}
    if parent: sp["parentSpanId"] = parent
    if status: sp["status"] = {"code": status}
    if http: sp["attributes"].append(attr("http.response.status_code", http))
    return sp


def doc(svc, spans):
    return {"resourceSpans": [{"resource": {"attributes": [attr("service.name", svc)]}, "scopeSpans": [{"spans": spans}]}]}


def raw_dir(d, n_req=40):
    docs = []
    for k in range(n_req):
        t = k * 1000
        tid = f"{k + 1:032x}"
        # entry (client of a dropped load generator) -> frontend server (422, no flag) -> INTERNAL -> frontend client (500, flagged) -> checkout server (500, flagged)
        docs.append(doc("frontend", [otlp_span(tid, f"{k:04x}0001", "", "POST /api/x", 2, t, 50_000, http=422),
                                     otlp_span(tid, f"{k:04x}0002", f"{k:04x}0001", "api route", 1, t + 100, 49_000),
                                     otlp_span(tid, f"{k:04x}0003", f"{k:04x}0002", "Checkout/Place", 3, t + 200, 40_000, status=2)]))
        docs.append(doc("checkout", [otlp_span(tid, f"{k:04x}0004", f"{k:04x}0003", "Checkout/Place", 2, t + 300, 30_000, status=2, http=500)]))
    d.mkdir(parents=True)
    (d / "traces.jsonl").write_text("".join(json.dumps(x) + "\n" for x in docs))
    (d / "logs.jsonl").write_text("")


def convert(script, raw, out, sched):
    subprocess.run([sys.executable, str(ROOT / script), str(raw), str(sched), str(out), "--err-flag"], check=True, capture_output=True)
    return [json.loads(l) for l in (out / "incidents/s1/spans.jsonl").read_text().splitlines()]


def analyze(exe, d):
    return json.loads(subprocess.run([str(ROOT / "build" / exe), str(d)], capture_output=True, text=True, check=True).stdout)


with tempfile.TemporaryDirectory() as tmp:
    tmp = pathlib.Path(tmp)
    raw = tmp / "raw"; raw_dir(raw)
    sched = tmp / "sched.json"; sched.write_text(json.dumps([{"id": "s1", "start_us": W0, "end_us": W0 + 120_000_000}]))
    old = convert("eval/real/convert.py", raw, tmp / "old", sched)
    new = convert("eval/real4/convert.py", raw, tmp / "new", sched)
    detached = lambda sp: sum(1 for s in sp if s["service"] == "frontend" and s["kind"] == "client" and not s.get("parent_span_id"))
    print(f"converter: detached frontend client spans v2={detached(old)} v3={detached(new)} (of 40)")
    ok &= detached(old) == 40 and detached(new) == 0
    # analyzer: the converted v3 data has frontend 422 (no flag) above natively failing spans
    inc = tmp / "new/incidents/s1"
    (inc / "metrics.jsonl").write_text("")
    for name in ("events.jsonl",): (inc / name).write_text("")
    a3, a4 = analyze("tb_analyze3", inc), analyze("tb_analyze4", inc)
    f3, f4 = a3["symptom"]["failed_traces"], a4["symptom"]["failed_traces"]
    print(f"analyzer: failed entry requests v3={f3} v4={f4} (of 40)")
    ok &= f4 == 40 and f3 < 40
print("synthetic checks:", "OK" if ok else "FAILED")
sys.exit(0 if ok else 1)
