#!/usr/bin/env python3
"""Development check for analyzer v6 (eval/real6/PLAN.md): v6 vs v5 on every simulator incident (label-free diff), simulator dev-v2 accuracy, and v5/v6 on real batches 1-5 (development data, strict rule). No model calls."""
import glob, json, pathlib, subprocess, sys
ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "eval")); import score
run = lambda exe, d: json.loads(subprocess.run([str(ROOT / "build" / exe), str(d)], capture_output=True, text=True, check=True).stdout)
n = diff = 0
for p in sorted(glob.glob(str(ROOT / "runs/**/incidents/*"), recursive=True)):
    if "/demo/" in p or not pathlib.Path(p, "spans.jsonl").exists(): continue
    a, b = run("tb_analyze5", p), run("tb_analyze6", p)
    k = lambda r: ([(h["category"], h["service"], h["score"]) for h in r["hypotheses"]], r["confidence"])
    n += 1
    if k(a) != k(b): diff += 1; print("DIFF", pathlib.Path(p).name)
print(f"simulator: {n} incidents, v6 differs from v5 on {diff}")
for exe in ("tb_analyze5", "tb_analyze6"):
    ok = tot = 0
    for t in ("t1", "t2"):
        for lp in sorted(glob.glob(str(ROOT / f"runs/v2dev/{t}/labels/*.json"))):
            lab = json.loads(open(lp).read()); tot += 1
            ok += score.judge(lab, run(exe, ROOT / f"runs/v2dev/{t}/incidents/{lab['id']}"))[0]
    print(f"simulator dev-v2 {exe}: {ok}/{tot}")
for name, d, labdir in (("batch 1", "eval/real/v1", "eval/real/labels"), ("batch 2", "eval/real2/data", "eval/real2/labels"), ("batch 3", "eval/real3/data", "eval/real3/labels"), ("batch 4", "eval/real4/data", "eval/real4/labels"), ("batch 5", "eval/real5/data", "eval/real5/labels")):
    ok5 = ok6 = nf = al5 = al6 = nc = 0; rows = []
    for lp in sorted(pathlib.Path(ROOT / labdir).glob("*.json")):
        lab = json.loads(lp.read_text()); iid = lab["id"]
        r5, r6 = run("tb_analyze5", ROOT / d / "incidents" / iid), run("tb_analyze6", ROOT / d / "incidents" / iid)
        t6 = r6["hypotheses"][0]
        if lab["kind"] == "fault":
            rc = lab["root_cause"]; names = {rc["service"]} | ({rc["edge"]["from"], rc["edge"]["to"]} if rc.get("edge") else set())
            nf += 1; ok5 += r5["hypotheses"][0]["service"] in names; ok6 += t6["service"] in names
            rows.append(f"{iid}:{'✓' if t6['service'] in names else '✗'}{t6['service']}")
        else:
            nc += 1; al5 += r5["confidence"] in ("medium", "high"); al6 += r6["confidence"] in ("medium", "high")
    print(f"{name}: faults v5 {ok5}/{nf} v6 {ok6}/{nf}; clean alarms v5 {al5}/{nc} v6 {al6}/{nc}   " + " ".join(rows))
