#!/usr/bin/env python3
"""Build windows from the extracted RCAEval RE2 Sock Shop archive with the frozen transfer converter (eval/transfer/convert.py). Labels = the service before the fault type in the case directory name
(`carts_cpu` -> carts), written by the dataset authors. Each case <service>_<fault>/<repeat>/ gives a fault window (injection -40 s .. +80 s) and, for repeat 1, a clean window (700 to 580 s before).
usage: build.py <tag> <extracted archive dir> <out dir>"""
import json, pathlib, re, subprocess, sys
tag, src, out = sys.argv[1], pathlib.Path(sys.argv[2]), pathlib.Path(sys.argv[3])
conv = pathlib.Path(__file__).resolve().parent.parent / "transfer/convert.py"
(out / "labels").mkdir(parents=True, exist_ok=True)
n = 0
for d in sorted(p for p in src.iterdir() if p.is_dir()):
    m = re.match(r"^(.*)_(cpu|delay|disk|loss|mem|socket)$", d.name)
    svc, fault = m.group(1), m.group(2)
    for rep in sorted(p.name for p in d.iterdir() if p.is_dir()):
        iid = f"{tag}-{svc}-{fault}-r{rep}"
        subprocess.run([sys.executable, str(conv), str(d / rep), str(out / "data/incidents" / iid)], check=True, capture_output=True)
        (out / "labels" / f"{iid}.json").write_text(json.dumps(dict(id=iid, kind="fault", system=tag, source="RCAEval RE2", case=f"{d.name}/{rep}", fault_type=fault, root_cause=dict(service=svc, edge=None)), indent=1) + "\n")
        n += 1
        if rep == "1":
            cid = f"{tag}-clean-{svc}-{fault}"
            subprocess.run([sys.executable, str(conv), str(d / rep), str(out / "data/incidents" / cid), "--clean"], check=True, capture_output=True)
            (out / "labels" / f"{cid}.json").write_text(json.dumps(dict(id=cid, kind="clean", system=tag, source="RCAEval RE2", case=f"{d.name}/{rep}"), indent=1) + "\n")
            n += 1
print(n, "windows written")
