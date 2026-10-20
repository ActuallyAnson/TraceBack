#!/usr/bin/env python3
"""Development evaluation of hybrid 2 (eval/transfer/PLAN.md) against the raw-logs baseline on DEVELOPMENT data only (RCAEval RE3-OB and OTel batches 1-6), cached model answers, no API calls.
Prints localization, clean-window alarms, wrong-at-high, per dataset."""
import json, pathlib, subprocess, sys
ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
CACHE = ROOT / "eval/llm_cache"
SETS = [("OTel b1", "eval/real/v1", "eval/real/labels"), ("OTel b2", "eval/real2/data", "eval/real2/labels"), ("OTel b3", "eval/real3/data", "eval/real3/labels"), ("OTel b4", "eval/real4/data", "eval/real4/labels"),
        ("OTel b5", "eval/real5/data", "eval/real5/labels"), ("OTel b6", "eval/real6/data", "eval/real6/labels"), ("RCAEval OB", "eval/transfer/dev/data", "eval/transfer/dev/labels")]


def run(exe, d, extra=()):
    p = subprocess.run([str(ROOT / "build" / exe), str(d), "--cache", str(CACHE), *extra], capture_output=True, text=True)
    if p.returncode:
        raise SystemExit(f"{exe} {d}: {p.stderr[:200]}")
    return json.loads(p.stdout)


def ok(lab, svc):
    rc = lab["root_cause"]
    return svc in ({rc["service"]} | ({rc["edge"]["from"], rc["edge"]["to"]} if rc.get("edge") else set()))


tot = {k: [0, 0, 0, 0, 0] for k in ("bl", "hy2")}  # faults ok, faults n, clean alarms, clean n, wrong at high
for name, d, labdir in SETS:
    line = [name]
    for key, exe in (("bl", "tb_reason"), ("hy2", "tb_hybrid2")):
        f = c = a = w = n = 0
        for lp in sorted((ROOT / labdir).glob("*.json")):
            lab = json.loads(lp.read_text())
            res = run(exe, ROOT / d / "incidents" / lab["id"], ["--system", "baseline"] if key == "bl" else [])
            rc = res["answer"]["root_cause"] if res.get("answer") else {"service": "none"}
            conf = res["answer"]["confidence"] if res.get("answer") else "low"
            if lab["kind"] == "fault":
                n += 1; hit = ok(lab, rc["service"]); f += hit; w += (not hit) and conf == "high"
            else:
                c += 1; a += conf in ("medium", "high")
        t = tot[key]; t[0] += f; t[1] += n; t[2] += a; t[3] += c; t[4] += w
        line.append(f"{key}: {f}/{n} faults, {a}/{c} clean alarms, {w} wrong@high")
    print(" | ".join(line))
for key, t in tot.items():
    print(f"TOTAL {key}: faults {t[0]}/{t[1]}, clean alarms {t[2]}/{t[3]}, wrong at high {t[4]}")
