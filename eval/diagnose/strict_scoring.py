#!/usr/bin/env python3
"""Review finding: eval/real/evaluate.py:correct() counts an answer as localized if the ANSWER's own edge touches the true service, but the pre-registered rule
(eval/real/PLAN.md line 27) is: the answer's service is the label's root service or an endpoint of the LABEL's edge. This script rescores every real batch
from the cached outputs under the pre-registered (strict) rule and prints both, side by side. It changes no file.   Offline (cache only)."""
import importlib.util, json, pathlib, sys
ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "eval")); sys.path.insert(0, str(ROOT / "eval/v2"))


def load(p, name):
    s = importlib.util.spec_from_file_location(name, ROOT / p); m = importlib.util.module_from_spec(s); s.loader.exec_module(m); return m


def strict(lab, v):
    rc = lab["root_cause"]
    return v["svc"] in ({rc["service"]} | ({rc["edge"]["from"], rc["edge"]["to"]} if rc.get("edge") else set()))


def lenient(lab, v):
    rc = lab["root_cause"]
    names = {rc["service"]} | ({rc["edge"]["from"], rc["edge"]["to"]} if rc.get("edge") else set())
    return v["svc"] in names or bool(v["edge"] and {v["edge"]["from"], v["edge"]["to"]} & names)


r1, r2, r3, r4 = (load(f"eval/{d}/evaluate.py", n) for d, n in (("real", "r1"), ("real2", "r2"), ("real3", "r3"), ("real4", "r4")))
batches = [("batch 1 (converter v1)", r1.run("v1"), [k for k in ("bl", "an1", "an2", "tb2") if True]), ("batch 2", r2.run2(), [k for k, _ in r2.SYS]),
           ("batch 3", r3.run3(), [k for k, _ in r3.SYS]), ("batch 4", r4.run3(), [k for k, _ in r4.SYS])]
for name, rows, keys in batches:
    f = [r for r in rows if r["lab"]["kind"] == "fault"]
    print(f"\n{name}: {len(f)} fault runs   (pre-registered strict rule / rule as implemented)")
    for k in keys:
        if k not in f[0]: continue
        s = [r["id"] for r in f if strict(r["lab"], r[k])]; l = [r["id"] for r in f if lenient(r["lab"], r[k])]
        print(f"  {k:4s} strict {len(s)}/{len(f)}  implemented {len(l)}/{len(f)}" + ("" if s == l else f"   counted only by the implemented rule: {sorted(set(l) - set(s))}"))
