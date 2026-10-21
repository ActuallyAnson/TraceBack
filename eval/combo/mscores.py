#!/usr/bin/env python3
"""Full rule-M per-service scores (eval/metrics/metric_rule.py, unchanged) for every window of a transfer label set. Needs the public archive; no model.
usage: mscores.py <labels dir> <extracted archive dir> <out json>"""
import json, pathlib, sys
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent / "metrics"))
import metric_rule as M
labs, root, out = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]), pathlib.Path(sys.argv[3])
res = {}
for lp in sorted(labs.glob("*.json")):
    lab = json.loads(lp.read_text())
    c = root / lab["case"]
    inj = int((c / "inject_time.txt").read_text().strip())
    w0 = inj - 40 if lab["kind"] == "fault" else inj - 700
    res[lab["id"]] = {k: round(v, 3) for k, v in M.window_scores(str(c / "simple_metrics.csv"), w0, w0 + 120).items()}
out.write_text(json.dumps(res, indent=0, sort_keys=True) + "\n")
print(len(res), "windows")
