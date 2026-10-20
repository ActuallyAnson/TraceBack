#!/usr/bin/env python3
"""Model-size check on the first transfer test (RE3-SS + RE3-TT; run once, not tuned): the raw-logs baseline and hybrid 2 with the larger Nemotron Ultra model.
Same frozen windows, labels and hybrid 2; only the model changes. Prints a table; changes no README block."""
import json, pathlib, subprocess, sys
HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent
ULTRA = ["--model", "nvidia/Nemotron-3-Ultra-550b-a55b", "--budget", "900000", "--price-in", "0.000001", "--price-out", "0.000003", "--cache", str(ROOT / "eval/llm_cache")]
def run(exe, d, extra=()):
    p = subprocess.run([str(ROOT / "build" / exe), str(d), *extra, *ULTRA], capture_output=True, text=True)
    if p.returncode: sys.exit(f"{exe} {d}: {p.stderr.strip()[:300]}")
    return json.loads(p.stdout)
rows = []; cost = 0.0
for lp in sorted((HERE / "test/labels").glob("*.json")):
    lab = json.loads(lp.read_text()); d = HERE / "test/data/incidents" / lab["id"]; r = dict(lab=lab)
    b = run("tb_reason", d, ["--system", "baseline"]); cost += b["meta"]["cost_usd"]
    h = run("tb_hybrid2m", d)
    for k, res in (("bl", b), ("hy", h)):
        a = res.get("answer") or {}
        r[k] = (a.get("root_cause", {}).get("service", "none"), a.get("confidence", "low"))
    rows.append(r)
for k, name in (("bl", "baseline (Ultra)"), ("hy", "hybrid 2 (Ultra)")):
    f = [r for r in rows if r["lab"]["kind"] == "fault"]; c = [r for r in rows if r["lab"]["kind"] == "clean"]
    hit = [r for r in f if r[k][0] == r["lab"]["root_cause"]["service"]]; wrong = [r for r in f if r not in hit]
    print(f"{name}: faults {len(hit)}/{len(f)}, clean alarms {sum(r[k][1] in ('medium', 'high') for r in c)}/{len(c)}, wrong at high {sum(r[k][1] == 'high' for r in wrong)}, wrong at medium/high {sum(r[k][1] in ('medium', 'high') for r in wrong)}, no answer {sum(r[k][0] == 'none' for r in rows)}")
print(f"model cost (baseline calls, repo price constants for Ultra): ${cost:.2f}")
