#!/usr/bin/env python3
"""Compute rule-M scores for every case of an extracted RCAEval RE2 archive and write eval/metrics/scores_<tag>.json. Needs the archive (public, md5 in PLAN.md); no model.
usage: scores.py <tag> <extracted archive dir>   e.g.  scores.py ss /tmp/rcaeval/re2ss/RE2-SS"""
import glob, json, pathlib, re, sys
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import metric_rule as M
tag, root = sys.argv[1], pathlib.Path(sys.argv[2])
out = []
for d in sorted(p for p in root.iterdir() if p.is_dir()):
    m = re.match(r"^(.*)_(cpu|delay|disk|loss|mem|socket)$", d.name)
    svc, ft = m.group(1), m.group(2)
    for rep in sorted(p.name for p in d.iterdir() if p.is_dir() and p.name.isdigit()):  # the RE2-OB archive also has a `multi-source-data` directory
        c = d / rep
        inj = int((c / "inject_time.txt").read_text().strip())
        for kind, w0 in (("fault", inj - 40), ("clean", inj - 700)):
            if kind == "clean" and rep != "1":
                continue
            sc = M.window_scores(str(c / "simple_metrics.csv"), w0, w0 + 120)
            top, s = M.answer(sc)
            ranked = sorted(sc.items(), key=lambda kv: -kv[1])
            out.append(dict(id=f"{tag}-{svc}-{ft}-r{rep}" if kind == "fault" else f"{tag}-clean-{svc}-{ft}", kind=kind, truth=svc if kind == "fault" else None, fault_type=ft, answer=top, top_service=ranked[0][0] if ranked else None,
                            top_score=round(ranked[0][1], 2) if ranked else 0, second=[ranked[1][0], round(ranked[1][1], 2)] if len(ranked) > 1 else None))
(pathlib.Path(__file__).resolve().parent / f"scores_{tag}.json").write_text(json.dumps(out, indent=0) + "\n")
print(tag, len(out), "windows")
