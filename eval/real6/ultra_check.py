#!/usr/bin/env python3
"""One-off model-size check on batch 6 (not tuned, run once): the raw-logs baseline and Full Traceback v6 with the larger Nemotron Ultra model, same locked data, same prompts.
Answers are cached under eval/llm_cache (the model name is part of the cache key). Needs NEBIUS_API_KEY for the first run. Prints a table; changes no README block."""
import json, pathlib, sys
HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(ROOT / "eval")); sys.path.insert(0, str(ROOT / "eval/v2"))
import score, report  # noqa: E402
ULTRA = ["--model", "nvidia/Nemotron-3-Ultra-550b-a55b", "--budget", "900000", "--price-in", "0.000001", "--price-out", "0.000003"]
rows = []; cost = 0.0
for lp in sorted((HERE / "labels").glob("*.json")):
    lab = json.loads(lp.read_text()); iid = lab["id"]; d = HERE / "data/incidents" / iid
    r = dict(id=iid, lab=lab)
    for k, extra in (("bl", ["--system", "baseline"]), ("tb", ["--system", "traceback", "--rules", "v6"])):
        p = report.sh([ROOT / "build/tb_reason", d, "--cache", report.CACHE] + extra + ULTRA)
        if p.returncode: sys.exit(f"{iid} {k}: {p.stderr.strip()[:300]}")
        res = json.loads(p.stdout); cost += res["meta"]["cost_usd"]
        top = score.llm_as_report(res)["hypotheses"][0]; r[k] = (top["category"], top["service"], score.llm_as_report(res)["confidence"])
    rows.append(r)
def ok(lab, svc):
    rc = lab["root_cause"]; return svc in ({rc["service"]} | ({rc["edge"]["from"], rc["edge"]["to"]} if rc.get("edge") else set()))
for k, name in (("bl", "baseline (Ultra)"), ("tb", "Full Traceback v6 (Ultra)")):
    f = [r for r in rows if r["lab"]["kind"] == "fault"]; c = [r for r in rows if r["lab"]["kind"] == "clean"]
    print(f"{name}: faults {sum(ok(r['lab'], r[k][1]) for r in f)}/{len(f)}, clean alarms {sum(r[k][2] in ('medium', 'high') for r in c)}/{len(c)}, wrong at high {sum(1 for r in f if not ok(r['lab'], r[k][1]) and r[k][2] == 'high')}")
for r in rows:
    lab = r["lab"]; t = "clean" if lab["kind"] == "clean" else lab["root_cause"]["service"]
    print(f"{r['id']} truth={t}: baseline {'✓' if lab['kind'] == 'fault' and ok(lab, r['bl'][1]) else ('✗' if lab['kind'] == 'fault' else '·')} {r['bl'][0]}/{r['bl'][1]} ({r['bl'][2]}) | FT v6 {'✓' if lab['kind'] == 'fault' and ok(lab, r['tb'][1]) else ('✗' if lab['kind'] == 'fault' else '·')} {r['tb'][0]}/{r['tb'][1]} ({r['tb'][2]})")
print(f"model cost (repo price constants for Ultra): ${cost:.2f}")
