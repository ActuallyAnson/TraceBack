#!/usr/bin/env python3
"""Development check for analyzer v5 (eval/real5/PLAN.md): (1) v5 vs v4 on every simulator incident, (2) analyzers v4/v5 on real batches 1-4 (development data, strict rule), no model calls."""
import glob, json, pathlib, subprocess, sys
ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
run = lambda exe, d: json.loads(subprocess.run([str(ROOT / "build" / exe), str(d)], capture_output=True, text=True, check=True).stdout)
n = diff = 0
for p in sorted(glob.glob(str(ROOT / "runs/**/incidents/*"), recursive=True)):
    if "/demo/" in p or not pathlib.Path(p, "spans.jsonl").exists(): continue
    a, b = run("tb_analyze4", p), run("tb_analyze5", p)
    k = lambda r: ([(h["category"], h["service"], h["score"]) for h in r["hypotheses"]], r["confidence"])
    n += 1
    if k(a) != k(b): diff += 1; print("DIFF", p)
print(f"simulator: {n} incidents, v5 differs from v4 on {diff}")
for name, d, labdir in (("batch 1", "eval/real/v1", "eval/real/labels"), ("batch 2", "eval/real2/data", "eval/real2/labels"), ("batch 3", "eval/real3/data", "eval/real3/labels"), ("batch 4", "eval/real4/data", "eval/real4/labels")):
    ok4 = ok5 = nf = alarm5 = nc = 0; rows = []
    for lp in sorted(pathlib.Path(ROOT / labdir).glob("*.json")):
        lab = json.loads(lp.read_text()); iid = lab["id"]
        r4, r5 = run("tb_analyze4", ROOT / d / "incidents" / iid), run("tb_analyze5", ROOT / d / "incidents" / iid)
        t4, t5 = r4["hypotheses"][0], r5["hypotheses"][0]
        if lab["kind"] == "fault":
            rc = lab["root_cause"]; names = {rc["service"]} | ({rc["edge"]["from"], rc["edge"]["to"]} if rc.get("edge") else set())
            nf += 1; ok4 += t4["service"] in names; ok5 += t5["service"] in names
            rows.append(f"{iid}:{'✓' if t5['service'] in names else '✗'} {t5['category']}/{t5['service']}({r5['confidence']})")
        else:
            nc += 1; alarm5 += r5["confidence"] in ("medium", "high"); rows.append(f"{iid}:clean {t5['category']}/{t5['service']}({r5['confidence']})")
    print(f"{name}: faults v4 {ok4}/{nf}  v5 {ok5}/{nf}; clean alarms v5 {alarm5}/{nc}\n   " + "  ".join(rows))
