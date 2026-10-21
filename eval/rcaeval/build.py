#!/usr/bin/env python3
"""Build the RCAEval RE3-OB test set from the extracted Zenodo archive: converts 30 fault windows and 10 clean windows, writes labels from the directory names.
usage: build.py <extracted RE3-OB directory> <out dir (data/ and labels/ are created under it)>   Run before any system."""
import json, pathlib, subprocess, sys
src, out = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
conv = pathlib.Path(__file__).with_name("convert.py")
(out / "labels").mkdir(parents=True, exist_ok=True)
n = 0
for d in sorted(p for p in src.iterdir() if p.is_dir()):
    svc, fault = d.name.rsplit("_", 1)
    for rep in sorted(p.name for p in d.iterdir() if p.is_dir()):
        iid = f"{svc.replace('service', '')}-{fault}-r{rep}"
        subprocess.run([sys.executable, str(conv), str(d / rep), str(out / "data/incidents" / iid)], check=True, capture_output=True)
        (out / "labels" / f"{iid}.json").write_text(json.dumps(dict(id=iid, kind="fault", source="RCAEval RE3-OB", case=f"{d.name}/{rep}", fault_type=fault, root_cause=dict(service=svc, edge=None)), indent=1) + "\n")
        n += 1
        if rep == "1":
            cid = f"clean-{svc.replace('service', '')}-{fault}"
            subprocess.run([sys.executable, str(conv), str(d / rep), str(out / "data/incidents" / cid), "--clean"], check=True, capture_output=True)
            (out / "labels" / f"{cid}.json").write_text(json.dumps(dict(id=cid, kind="clean", source="RCAEval RE3-OB", case=f"{d.name}/{rep}"), indent=1) + "\n")
            n += 1
print(n, "windows written")
