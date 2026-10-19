#!/usr/bin/env python3
"""Synthetic checks that the two analyzer-v3 rules fire (and that v2 does not do the same). No real or simulator data involved.
S2: entry requests answered 422 with the error flag set beneath them are failures in v3 only.
S1: a hub service (5 callers) that is slow for everyone is demoted as a root-cause candidate in v3, not in v2."""
import json, pathlib, random, subprocess, sys, tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
random.seed(7)
W0 = 1_700_000_000_000_000


def build(d, hub):
    d.mkdir(parents=True)
    spans, n = [], [0]
    def sid():
        n[0] += 1
        return f"{n[0]:016x}"
    callers = ["a", "b", "c", "d", "e"]
    for k in range(3000):
        t = W0 + k * 40_000  # 120 s
        bad = t > W0 + 40_000_000
        tid = sid()
        root = sid()
        c = callers[k % 5]
        entry_status = 200
        if not hub and bad and k % 4 == 0:
            entry_status = 422
        if hub and bad:
            entry_status = 504
        spans.append(dict(id=f"web-S{k:05d}", trace_id=tid, span_id=root, service="web", kind="server", op="GET /x", start_us=t, dur_us=(1_000_000 if hub and bad else 60_000), status=entry_status))
        cs = sid(); ss = sid()
        spans.append(dict(id=f"web-C{k:05d}", trace_id=tid, span_id=cs, parent_span_id=root, service="web", kind="client", op="call", peer=c, start_us=t + 1000, dur_us=50_000, status=200 if entry_status == 200 else (0 if hub else 422), attempt=1, **({"err": True} if entry_status != 200 and not hub else {})))
        spans.append(dict(id=f"{c}-S{k:05d}", trace_id=tid, span_id=ss, parent_span_id=cs, service=c, kind="server", op="work", start_us=t + 2000, dur_us=40_000, status=200 if entry_status == 200 else (504 if hub else 500), **({"err": True} if entry_status != 200 and not hub else {})))
        if hub:
            hc, hs = sid(), sid()
            slow = 900_000 if bad else 8_000
            spans.append(dict(id=f"{c}-H{k:05d}", trace_id=tid, span_id=hc, parent_span_id=ss, service=c, kind="client", op="flag", peer="hub", start_us=t + 3000, dur_us=slow, status=200, attempt=1))
            spans.append(dict(id=f"hub-S{k:05d}", trace_id=tid, span_id=hs, parent_span_id=hc, service="hub", kind="server", op="eval", start_us=t + 3500, dur_us=slow - 1000, status=200))
    (d / "spans.jsonl").write_text("".join(json.dumps(s, separators=(",", ":")) + "\n" for s in spans))
    (d / "logs.jsonl").write_text("")
    (d / "events.jsonl").write_text("")
    (d / "metrics.jsonl").write_text("")
    (d / "meta.json").write_text(json.dumps(dict(id="synth", window_start_us=W0, window_end_us=W0 + 120_000_000, services=["web", "a", "b", "c", "d", "e"] + (["hub"] if hub else []))))


def run(exe, d):
    return json.loads(subprocess.run([str(ROOT / "build" / exe), str(d)], capture_output=True, text=True, check=True).stdout)


ok = True
with tempfile.TemporaryDirectory() as tmp:
    s2 = pathlib.Path(tmp) / "s2"; build(s2, False)
    a2, a3 = run("tb_analyze2", s2), run("tb_analyze3", s2)
    f2, f3 = a2["symptom"]["failed_traces"], a3["symptom"]["failed_traces"]
    print(f"S2: failed entry requests v2={f2} v3={f3}"); ok &= (f2 == 0 and f3 > 100)
    s1 = pathlib.Path(tmp) / "s1"; build(s1, True)
    a2, a3 = run("tb_analyze2", s1), run("tb_analyze3", s1)
    h2 = [(h["category"], h["service"]) for h in a2["hypotheses"][:2]]
    h3 = [(h["category"], h["service"]) for h in a3["hypotheses"][:2]]
    notes = [h["features"].get("v3_adjustments") for h in a3["hypotheses"] if h["features"].get("v3_adjustments")]
    print("S1: v2 top", h2, "| v3 top", h3, "| notes", notes[:1])
    ok &= bool(notes) and h2[0][1] == "hub" and h3[0][1] != "hub"
print("synthetic checks:", "OK" if ok else "FAILED")
sys.exit(0 if ok else 1)
