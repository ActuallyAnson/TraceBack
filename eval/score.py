#!/usr/bin/env python3
"""Evaluation harness. Scoring follows eval/SCORING.md (pre-registered).

The analyzer is run as a subprocess on runs/incidents/<id>/ only. Labels are read here,
never passed to the analyzer. Default split is dev; test requires --confirm-test and is logged.
"""
import argparse, collections, datetime, json, pathlib, subprocess, sys, time

ROOT = pathlib.Path(__file__).resolve().parent.parent
# label type -> analyzer categories that count as correct (see SCORING.md)
MAP = {
    "slow_dependency": {"dependency_slow"},
    "retry_storm": {"retry_amplification"},
    "connection_pool_exhaustion": {"pool_exhaustion"},
    "bad_deploy": {"deploy_regression"},
    "config_change": {"config_regression"},
    "crash_restart": {"process_crash"},
    "capacity_reduction": {"capacity_saturation"},
    "masked_downstream_error": {"internal_errors"},
}


def load_split(split):
    rows = [json.loads(l) for l in (ROOT / "eval/split.json").read_text().splitlines() if l.strip()]
    return [r["id"] for r in rows if split == "all" or r["split"] == split]


def run_analyzer(iid):
    t = time.time()
    out = subprocess.run([str(ROOT / "build/tb_analyze"), str(ROOT / "runs/incidents" / iid)], capture_output=True, text=True, check=True)
    return json.loads(out.stdout), time.time() - t


def judge(label, rep):
    """Return (correct_top1, service_top1, correct_top3) for one analyzer report."""
    rc = label["root_cause"]
    ok_cat = MAP.get(rc["type"])
    hyps = rep["hypotheses"]
    edge = rc.get("edge")
    names = {rc["service"]} | ({edge["from"], edge["to"]} if edge else set())  # edge faults: either endpoint localizes
    def loc(h):
        return h["service"] in names or bool(h.get("edge") and {h["edge"]["from"], h["edge"]["to"]} & names and edge)
    def right(h):
        if ok_cat is None:          # unseen type: only localization is scored (SCORING.md)
            return loc(h)
        return h["category"] in ok_cat and h["service"] == rc["service"]
    top = hyps[0]
    return right(top), loc(top), any(right(h) for h in hyps[:3])


def summarize(rows, title):
    n = len(rows)
    if not n:
        return
    acc = sum(r["top1"] for r in rows) / n
    loc = sum(r["svc"] for r in rows) / n
    t3 = sum(r["top3"] for r in rows) / n
    high = [r for r in rows if r["conf"] == "high"]
    fc = sum(1 for r in high if not r["top1"]) / n
    prec = (sum(r["top1"] for r in high) / len(high)) if high else float("nan")
    print(f"{title:28s} n={n:3d} top1={acc:5.1%} svc={loc:5.1%} top3={t3:5.1%} "
          f"high-conf={len(high):2d} precision@high={prec:5.1%} false-confidence={fc:5.1%}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--split", default="dev", choices=["dev", "test", "all"])
    ap.add_argument("--confirm-test", action="store_true", help="required to score the held-out test split")
    ap.add_argument("--system", default="analyzer", choices=["analyzer"])
    ap.add_argument("--verbose", action="store_true")
    a = ap.parse_args()
    if a.split != "dev" and not a.confirm_test:
        sys.exit("refusing to score %r without --confirm-test (held-out data; every run is logged)" % a.split)
    if a.split != "dev":
        head = subprocess.run(["git", "rev-parse", "--short", "HEAD"], capture_output=True, text=True, cwd=ROOT).stdout.strip()
        dirty = bool(subprocess.run(["git", "status", "--porcelain", "--", "src", "include", "eval"], capture_output=True, text=True, cwd=ROOT).stdout.strip())
        with open(ROOT / "eval/test_runs.log", "a") as f:
            f.write(f"{datetime.datetime.now(datetime.timezone.utc).isoformat()} split={a.split} system={a.system} commit={head}{'+dirty' if dirty else ''}\n")

    rows = []
    for iid in load_split(a.split):
        label = json.loads((ROOT / "runs/labels" / f"{iid}.json").read_text())
        rep, secs = run_analyzer(iid)
        top1, svc, top3 = judge(label, rep)
        top = rep["hypotheses"][0]
        skew_err = None
        if label["complications"]:
            inj = {c["service"]: c["offset_us"] for c in label["complications"]}
            est = rep["clock"]["offsets_us"]
            # compare pairwise differences; base jitter (<= +-3ms per service) is unknown to both
            svcs = list(est)
            skew_err = max(abs((est[a] - est[b]) - (inj.get(a, 0) - inj.get(b, 0))) for a in svcs for b in svcs) / 1000.0
        rows.append(dict(skew_err=skew_err, id=iid, group=label["group"], type=label["root_cause"]["type"], svc_true=label["root_cause"]["service"],
                         top1=top1, svc=svc, top3=top3, conf=rep["confidence"], top=f'{top["category"]}/{top["service"]}',
                         alts=[f'{h["category"]}/{h["service"]}' for h in rep["hypotheses"][1:3]], secs=secs))
    print(f"system={a.system} split={a.split}")
    summarize(rows, "ALL")
    summarize([r for r in rows if r["group"] != "unseen"], "catalog types (category+svc)")
    summarize([r for r in rows if r["group"] == "unseen"], "unseen types (svc only)")
    for g in ("base", "skew", "double", "unseen"):
        summarize([r for r in rows if r["group"] == g], f"group={g}")
    for t in sorted({r["type"] for r in rows}):
        summarize([r for r in rows if r["type"] == t], f"type={t}")
    errs = [r["skew_err"] for r in rows if r["skew_err"] is not None]
    if errs:
        print(f"clock-offset recovery on {len(errs)} skewed incidents: max error {max(errs):.1f} ms, mean {sum(errs)/len(errs):.1f} ms (injected skews are 400-3000 ms)")
    print(f"mean latency {sum(r['secs'] for r in rows)/len(rows)*1000:.0f} ms/incident, cost $0 (no LLM)")
    wrong = [r for r in rows if not r["top1"]]
    print(f"\nincorrect top-1 ({len(wrong)}):")
    for r in wrong:
        print(f"  {r['id']} [{r['group']}] truth={r['type']}/{r['svc_true']}  top={r['top']} conf={r['conf']}  next={r['alts']}")
    if a.verbose:
        print("\ncorrect:")
        for r in rows:
            if r["top1"]:
                print(f"  {r['id']} [{r['group']}] {r['type']}/{r['svc_true']} conf={r['conf']}")


if __name__ == "__main__":
    main()
