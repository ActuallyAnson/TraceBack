#!/usr/bin/env python3
"""Write eval/real/labels/<id>.json from scenarios.json and the runner's schedule (ground truth = the flag that was switched on). Run before any system."""
import json, pathlib, sys
here = pathlib.Path(sys.argv[2]) if len(sys.argv) > 2 else pathlib.Path(__file__).parent  # directory holding scenarios.json and labels/
sched = {s["id"]: s for s in json.loads(pathlib.Path(sys.argv[1]).read_text())}
for sc in json.loads((here / "scenarios.json").read_text()):
    s = sched[sc["id"]]
    lab = dict(id=sc["id"], source="opentelemetry-demo 85c2cb2 (3.1.0)", window_start_us=s["start_us"], window_end_us=s["start_us"] + 120_000_000, kind=sc["kind"])
    if sc["kind"] == "fault":
        lab.update(flag=sc["flag"], variant=sc["variant"], flip_us=s["flip_us"],
                   root_cause=dict(service=sc["root_service"], edge=sc["edge"], summary=sc["summary"], onset_us=s["flip_us"]))
    (here / "labels" / f"{sc['id']}.json").write_text(json.dumps(lab, indent=1) + "\n")
print("labels written")
