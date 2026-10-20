#!/usr/bin/env python3
"""Ablation of hybrid 2 (offline, from cached model answers; no API call, nothing changes): which part of the method produces the gain?
Variants, all computed from the same cached baseline answer and the same measurements on the frozen test windows:
  A  baseline                       the model's top answer and its own confidence
  B  + confidence cap only          same answer; confidence capped by the measurements (low if the answer's service has no measured anomaly, else medium unless it is the measured origin/dominant burst)
  C  + drop unsupported proposals   first model proposal that is measured anomalous (symptoms allowed), else no answer
  D  + drop symptoms                first model proposal that is a measured cause, else no answer
  E  hybrid 2                       D plus the measurement-only fallback and the confidence rule (the frozen method)
Usage: ablation.py [--model ultra]"""
import json, pathlib, subprocess, sys
ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
ULTRA = "--model" in sys.argv
EXTRA = ["--model", "nvidia/Nemotron-3-Ultra-550b-a55b", "--budget", "900000", "--price-in", "0.000001", "--price-out", "0.000003"] if ULTRA else []
CACHE = str(ROOT / "eval/llm_cache")
RANK = {"low": 0, "medium": 1, "high": 2}
cap = lambda a, b: a if RANK[a] <= RANK[b] else b


def run(exe, d, extra):
    p = subprocess.run([str(ROOT / "build" / exe), str(d), "--cache", CACHE, *EXTRA, *extra], capture_output=True, text=True)
    if p.returncode:
        sys.exit(f"{exe} {d}: {p.stderr.strip()[:200]}")
    return json.loads(p.stdout)


def evaluate(name, base, labdir):
    rows = []
    for lp in sorted((ROOT / labdir).glob("*.json")):
        lab = json.loads(lp.read_text()); d = ROOT / base / "incidents" / lab["id"]
        bl = run("tb_reason", d, ["--system", "baseline"])
        hy = run("tb_hybrid2m", d, [])
        ans = bl.get("answer") or {}
        top = ans.get("root_cause", {}).get("service", "none"); conf = ans.get("confidence", "low")
        props = hy["hybrid"]["proposals"]
        out = {"A": (top, conf)}
        supp = {p["service"]: p for p in props}
        t = supp.get(top)
        out["B"] = (top, "low" if not (t and t["supported"]) else cap(conf, "medium" if not t["cause"] else "high"))
        first = next((p for p in props if p["supported"]), None); out["C"] = (first["service"], cap(conf, "medium")) if first else ("none", "low")
        first = next((p for p in props if p["cause"]), None); out["D"] = (first["service"], cap(conf, "medium")) if first else ("none", "low")
        a = hy.get("answer") or {}
        out["E"] = (a.get("root_cause", {}).get("service", "none"), a.get("confidence", "low"))
        rows.append((lab, out))
    return rows


def table(title, rows):
    print(f"\n{title}")
    print("| Variant | Faults localized | Clean-window alarms | Wrong at high | Wrong at medium or high |\n|---|---:|---:|---:|---:|")
    names = {"A": "A baseline", "B": "B + confidence cap only", "C": "C + drop unsupported proposals", "D": "D + drop symptoms", "E": "E hybrid 2 (frozen)"}
    for k in "ABCDE":
        f = [(l, o[k]) for l, o in rows if l["kind"] == "fault"]; c = [(l, o[k]) for l, o in rows if l["kind"] == "clean"]
        hit = [x for x in f if x[1][0] == x[0]["root_cause"]["service"]]; wrong = [x for x in f if x not in hit]
        print(f"| {names[k]} | {len(hit)}/{len(f)} | {sum(x[1][1] in ('medium', 'high') for x in c)}/{len(c)} | {sum(x[1][1] == 'high' for x in wrong)} | {sum(x[1][1] in ('medium', 'high') for x in wrong)} |")


print("# Ablation of hybrid 2" + (" (Ultra model)" if ULTRA else " (Super model)"))
table("RCAEval RE3 Sock Shop + Train Ticket (60 faults, 18 clean)", evaluate("re3", "eval/transfer/test/data", "eval/transfer/test/labels"))
if not ULTRA:
    table("RCAEval RE2 Sock Shop resource faults (90 faults, 30 clean)", evaluate("re2", "eval/transfer2/test/data", "eval/transfer2/test/labels"))
