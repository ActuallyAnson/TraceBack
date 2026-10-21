#!/usr/bin/env python3
"""Build windows from extracted RCAEval RE3 archives with converter v3 (eval/transfer/convert.py); labels come from the case directory names (written by the dataset authors).
usage: build.py <tag> <extracted archive dir> <out dir>        e.g.  build.py ss /tmp/rcaeval/re3ss/RE3-SS eval/transfer/test
Each case directory <service>_<fault>/<repeat>/ gives one fault window (injection -40 s .. +80 s) and, for repeat 1, one clean window (700 s .. 580 s before the injection)."""
import json, pathlib, re, subprocess, sys
tag, src, out = sys.argv[1], pathlib.Path(sys.argv[2]), pathlib.Path(sys.argv[3])
conv = pathlib.Path(__file__).with_name("convert.py")
(out / "labels").mkdir(parents=True, exist_ok=True)
n = 0
for d in sorted(p for p in src.iterdir() if p.is_dir()):
    m = re.match(r"^(.*)_(f\d.*)$", d.name)
    svc, fault = m.group(1), m.group(2)
    for rep in sorted(p.name for p in d.iterdir() if p.is_dir()):
        iid = f"{tag}-{svc}-{fault}-r{rep}"
        subprocess.run([sys.executable, str(conv), str(d / rep), str(out / "data/incidents" / iid)], check=True, capture_output=True)
        (out / "labels" / f"{iid}.json").write_text(json.dumps(dict(id=iid, kind="fault", system=tag, source="RCAEval RE3", case=f"{d.name}/{rep}", fault_type=fault, root_cause=dict(service=svc, edge=None)), indent=1) + "\n")
        n += 1
        if rep == "1":
            cid = f"{tag}-clean-{svc}-{fault}"
            subprocess.run([sys.executable, str(conv), str(d / rep), str(out / "data/incidents" / cid), "--clean"], check=True, capture_output=True)
            (out / "labels" / f"{cid}.json").write_text(json.dumps(dict(id=cid, kind="clean", system=tag, source="RCAEval RE3", case=f"{d.name}/{rep}"), indent=1) + "\n")
            n += 1
print(n, "windows written")
