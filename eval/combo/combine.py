#!/usr/bin/env python3
"""Combination of hybrid 2 (model proposals + log measurements) with rule M (metric z-scores). Offline: reads the cached h2_*.json and mscores_*.json; no model call.
Confidence: high = both sources name the same service; medium = a single source answers (hybrid 2 keeps its own confidence, capped at medium); low = no answer.
Variants (all recorded, developed on RE2-SS only):
  H   hybrid 2 alone                 M   rule M alone (answer if the top z >= TAU)
  C2  hybrid 2, else M               C3  M, else hybrid 2
  C4  hybrid 2's service if its own metric z >= TAU, else M, else hybrid 2
usage: combine.py <dev|test>"""
import json, pathlib, sys
HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent
TAU = 10.0
RANK = {"low": 0, "medium": 1, "high": 2}


def mtop(z):
    if not z:
        return None
    s = max(z, key=z.get)
    return s if z[s] >= TAU else None


def combine(v, h, z):
    hs = h["svc"] if h["svc"] != "none" else None
    m = mtop(z)
    if v == "H": pick = hs
    elif v == "M": pick = m
    elif v == "C2": pick = hs or m
    elif v == "C3": pick = m or hs
    elif v == "C4": pick = hs if (hs and z.get(hs, 0) >= TAU) else (m or hs)
    if pick is None:
        return "none", "low"
    if v in ("H",): return pick, h["conf"]
    if v in ("M",): return pick, "medium"
    agree = pick == hs and pick == m
    if agree: return pick, "high"
    return pick, "medium" if pick != hs or RANK[h["conf"]] >= 1 else "low"


def load(tag):
    labs = {}
    for d in ("transfer", "transfer2"):
        for lp in (ROOT / "eval" / d / "test/labels").glob("*.json"):
            lab = json.loads(lp.read_text()); labs[lab["id"]] = lab
    return labs


def score(sets, v, labs):
    f = c = hit = alarm = whi = wmh = na = 0
    for h, ms in sets:
        for i, hh in h.items():
            lab = labs[i]; a, conf = combine(v, hh, ms[i]); na += a == "none"
            if lab["kind"] == "clean":
                c += 1; alarm += conf in ("medium", "high")
            else:
                f += 1
                if a == lab["root_cause"]["service"]: hit += 1
                else: whi += conf == "high"; wmh += conf in ("medium", "high")
    return f"{hit}/{f}", f"{alarm}/{c}", whi, wmh, na


def main():
    labs = load(None); mode = sys.argv[1]
    J = lambda n: json.loads((HERE / n).read_text())
    if mode == "dev":
        groups = [("RE2-SS (development)", [(J("h2_re2ss.json"), J("mscores_re2ss.json"))])]
    else:
        groups = [("RE3-SS+TT, Super", [(J("h2_re3ss.json"), J("mscores_re3ss.json")), (J("h2_re3tt.json"), J("mscores_re3tt.json"))]),
                  ("RE3-SS+TT, Ultra", [(J("h2u_re3ss.json"), J("mscores_re3ss.json")), (J("h2u_re3tt.json"), J("mscores_re3tt.json"))])]
    for name, sets in groups:
        print(f"\n{name}\n| Variant | Faults localized | Clean alarms | Wrong at high | Wrong at medium or high | No answer |\n|---|---:|---:|---:|---:|---:|")
        for v in ("H", "M", "C2", "C3", "C4"):
            print(f"| {v} | " + " | ".join(str(x) for x in score(sets, v, labs)) + " |")


main()
