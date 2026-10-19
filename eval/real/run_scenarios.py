#!/usr/bin/env python3
"""Drive the running OpenTelemetry Demo through eval/real/scenarios.json (see PLAN.md) and record the window timestamps.
Usage: run_scenarios.py <otel-demo checkout> <out schedule.json> [scenarios.json]"""
import json, pathlib, sys, time

demo = pathlib.Path(sys.argv[1])
flagfile = demo / "src/flagd/demo.flagd.json"
sched = []


def set_flag(flag, variant):
    d = json.loads(flagfile.read_text())
    d["flags"][flag]["defaultVariant"] = variant
    with open(flagfile, "r+") as f:  # in place: flagd watches the bind-mounted file
        f.seek(0); f.write(json.dumps(d, indent=2)); f.truncate()


def all_off():
    d = json.loads(flagfile.read_text())
    for k, v in d["flags"].items():
        if "off" in v["variants"]:
            v["defaultVariant"] = "off"
    with open(flagfile, "r+") as f:
        f.seek(0); f.write(json.dumps(d, indent=2)); f.truncate()


now_us = lambda: int(time.time() * 1e6)
for sc in json.loads((pathlib.Path(sys.argv[3]) if len(sys.argv) > 3 else pathlib.Path(__file__).parent / "scenarios.json").read_text()):
    all_off(); time.sleep(15)
    start = now_us(); time.sleep(40)
    flip = None
    if sc["kind"] == "fault":
        set_flag(sc["flag"], sc["variant"]); flip = now_us()
    time.sleep(80)
    end = now_us()
    sched.append(dict(id=sc["id"], start_us=start, flip_us=flip, end_us=end))
    all_off()
    pathlib.Path(sys.argv[2]).write_text(json.dumps(sched, indent=1))
    print(sc["id"], "done", flush=True)
