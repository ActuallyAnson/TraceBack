#!/usr/bin/env python3
"""Dump hybrid 2's final answer per window from the cached model answers (offline; a cache miss would call the model, so it fails without a key instead).
usage: h2dump.py <incidents base> <labels dir> <out json> [ultra]"""
import json, pathlib, subprocess, sys
ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
base, labs, out = ROOT / sys.argv[1], ROOT / sys.argv[2], pathlib.Path(sys.argv[3])
extra = ["--model", "nvidia/Nemotron-3-Ultra-550b-a55b", "--budget", "900000", "--price-in", "0.000001", "--price-out", "0.000003"] if "ultra" in sys.argv[4:] else []
res = {}
for lp in sorted(labs.glob("*.json")):
    i = json.loads(lp.read_text())["id"]
    p = subprocess.run([str(ROOT / "build/tb_hybrid2m"), str(base / "incidents" / i), "--cache", str(ROOT / "eval/llm_cache"), *extra], capture_output=True, text=True)
    if p.returncode:
        sys.exit(f"{i}: {p.stderr.strip()[:200]}")
    r = json.loads(p.stdout); a = r.get("answer") or {}
    res[i] = dict(svc=a.get("root_cause", {}).get("service", "none"), conf=a.get("confidence", "low"), src=r["hybrid"]["source"])
out.write_text(json.dumps(res, indent=0, sort_keys=True) + "\n"); print(len(res))
