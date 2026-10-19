#!/usr/bin/env python3
"""Copy exactly the model responses the reported results depend on into eval/llm_cache (committed).

For every incident and both LLM systems (baseline, Full Traceback) this runs tb_reason with a read-only
fallback cache (default runs/llm_cache) and fails if any call would have gone to the API. After export,
`make results` and `make demo` reproduce all numbers offline, with no API key.
"""
import json, pathlib, subprocess, sys
ROOT = pathlib.Path(__file__).resolve().parent.parent
fallback = sys.argv[1] if len(sys.argv) > 1 else str(ROOT / "runs/llm_cache")
ids = [json.loads(l)["id"] for l in (ROOT / "eval/split.json").read_text().splitlines() if l.strip()]
bad = 0
for iid in ids:
    for system in ("traceback", "baseline"):
        p = subprocess.run([str(ROOT / "build/tb_reason"), str(ROOT / "runs/incidents" / iid), "--system", system,
                            "--cache", str(ROOT / "eval/llm_cache"), "--cache-fallback", fallback], capture_output=True, text=True)
        if p.returncode != 0:
            print("FAILED", iid, system, p.stderr[:200]); bad += 1; continue
        res = json.loads(p.stdout)
        live = [a for a in res["meta"]["attempts"] if not a.get("cached", False)]
        if live:
            print("LIVE CALL (not in cache)", iid, system); bad += 1
print("exported for", len(ids), "incidents x 2 systems;", "problems:", bad)
sys.exit(1 if bad else 0)
