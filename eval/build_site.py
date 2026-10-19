#!/usr/bin/env python3
"""Build the static showcase site data (docs/data/*.json) from saved runs.

Every number on the site comes from here: per-incident answers are read from runs/results (cached model
answers), the analyzer is re-run, verification is re-computed, and summaries use the same judge() as score.py.
Run `make site` after `make data` and the evaluation runs.
"""
import json, pathlib, re, subprocess, sys
ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "eval"))
import score  # noqa: E402

OUT = ROOT / "docs" / "data"
PRICE = None


def sh(args):
    return subprocess.run(args, capture_output=True, text=True, check=True).stdout


def fmt_t(sec):
    return f"{sec:+.3f}s"


def records_for(iid, ids, offsets, ops_off, t0):
    """id -> one-line rendering on the aligned clock (same idea as the report)."""
    want = set(ids)
    d = ROOT / "runs/incidents" / iid
    out = {}
    for fn, kind in (("logs.jsonl", "log"), ("spans.jsonl", "span"), ("metrics.jsonl", "metric"), ("events.jsonl", "ops")):
        for line in (d / fn).read_text().splitlines():
            j = json.loads(line)
            if j["id"] not in want:
                continue
            svc = j.get("service", "")
            ts = j.get("ts_us", j.get("start_us", 0))
            t = (ts - (ops_off if kind == "ops" else offsets.get(svc, 0)) - t0) / 1e6
            if kind == "log":
                s = f'{fmt_t(t)} log {svc} {j["level"]}' + (f' event={j["event"]}' if j.get("event") else "") + f' "{j["msg"]}"' + (f' {json.dumps(j["f"], separators=(",", ":"))}' if j.get("f") else "")
            elif kind == "span":
                s = f'{fmt_t(t)} span {svc} {j["kind"]} {j["op"]}' + (f' -> {j["peer"]}' if j.get("peer") else "") + f' status={j["status"]} dur={j["dur_us"]//1000}ms' + (f' queue={j["queue_us"]//1000}ms' if j.get("queue_us") else "") + (f' error={j["error"]}' if j.get("error") else "")
            elif kind == "metric":
                s = f'{fmt_t(t)} metric {svc} req={j["req"]} err5xx={j["err5xx"]} p99={j["p99_ms"]:.0f}ms queue_max={j["queue_max"]} busy_max={j["busy_max"]} workers_limit={j["workers_limit"]} pool={j["pool_in_use_max"]} boots={j["boots"]}'
            else:
                s = f'{fmt_t(t)} ops {j["type"]} {svc} {json.dumps(j.get("detail", {}), separators=(",", ":"))}'
            out[j["id"]] = s
    return out


def norm(c):
    return c.strip("[] ")


def cites_of(ans):
    ids = []
    if not ans:
        return ids
    for part in ans.get("causal_chain", []) + ans.get("alternatives", []):
        ids += [norm(c) for c in part.get("cites", [])]
    return ids


def per_incident(iid, split):
    label = json.loads((ROOT / f"runs/labels/{iid}.json").read_text())
    rep_dir = ROOT / "runs/site_tmp" / iid
    env = dict(**__import__("os").environ)
    sh([str(ROOT / "build/tb_investigate"), str(ROOT / "runs/incidents" / iid), "--replay", str(ROOT / "runs/replay" / f"{iid}.json"),
        "--cache", str(ROOT / "runs/llm_cache"), "--out", str(rep_dir)])
    rep = json.loads((rep_dir / "report.json").read_text())
    an = rep["analyzer"]
    t0 = rep["t0_us"]
    offsets = {k: int(v) for k, v in an["clock"]["offsets_us"].items()}
    ops_off = int(an["clock"]["ops_clock_offset_us"])
    base_path = ROOT / "runs/results/baseline" / f"{iid}.json"
    base = json.loads(base_path.read_text()) if base_path.exists() else None

    tb_top1, tb_svc, _ = score.judge(label, score.llm_as_report(rep))
    base_top1 = score.judge(label, score.llm_as_report(base))[0] if base else False
    an_top1 = score.judge(label, an)[0]

    meta_start = json.loads((ROOT / "runs/incidents" / iid / "meta.json").read_text())["window_start_us"]
    series = {}
    for line in (ROOT / "runs/incidents" / iid / "metrics.jsonl").read_text().splitlines():
        m = json.loads(line)
        t = (m["ts_us"] - offsets.get(m["service"], 0) - t0) / 1e6
        series.setdefault(m["service"], []).append(dict(t=round(t, 2), req=m["req"], err=m["err5xx"], p99=round(m["p99_ms"], 1), q=m["queue_max"], busy=m["busy_max"], wl=m["workers_limit"], pool=m["pool_in_use_max"], boots=m["boots"]))
    for v in series.values():
        v.sort(key=lambda x: x["t"])

    need = set(cites_of(rep["answer"])) | set(cites_of(base["answer"] if base else None))
    for h in an["hypotheses"][:6]:
        need |= {e["id"] for e in h["evidence"]}
    for e in an["timeline"]:
        need.add(e["id"])
    recs = records_for(iid, need, offsets, ops_off, t0)

    hyps = []
    for h in an["hypotheses"][:6]:
        hyps.append(dict(rank=h["rank"], category=h["category"], service=h["service"], edge=h["edge"], onset=(h["onset_us"] - t0) / 1e6 if h["onset_us"] else None,
                         score=h["score"], strength=h["strength"], coverage=h["coverage"], summary=h["summary"], features=h["features"], evidence=h["evidence"]))
    comp = {c["service"]: c["offset_us"] / 1e6 for c in label["complications"]}
    truth = dict(type=label["root_cause"]["type"], service=label["root_cause"]["service"], edge=label["root_cause"].get("edge"), decoys=label["decoys"], complications=label["complications"])
    sym = an["symptom"]
    detail = dict(
        id=iid, split=split, group=label["group"], truth=truth,
        symptom=dict(traces=sym["traces"], failed=sym["failed_traces"], onset=(sym["sustained_onset_us"] - t0) / 1e6 if sym["failed_traces"] else None, before=sym["failures_before_burst"]),
        graph=an["graph"], hypotheses=hyps, ruled_out=[dict(change=r["change"], service=r["service"], reason=r["reason"], t=(r["t_us"] - t0) / 1e6) for r in an["ruled_out"]],
        timeline=[dict(t=e["t_rel_s"], id=e["id"], kind=e["kind"], service=e["service"], text=e["text"]) for e in an["timeline"]],
        clock=dict(reference=an["clock"]["reference_service"], offsets={k: v / 1e6 for k, v in offsets.items()}, injected=comp, ops_calibrated=an["clock"]["ops_clock_calibrated"], samples=an["clock"]["edge_samples"]),
        series=series, records=recs,
        traceback=dict(answer=rep["answer"], accepted=rep["accepted"], llm_confidence=rep["answer"]["confidence"], final_confidence=rep["final_confidence"], verification=rep["verification"],
                       correct=tb_top1, tokens=[rep["meta"]["prompt_tokens"], rep["meta"]["completion_tokens"]], attempts=len(rep["meta"]["attempts"]), cost=rep["meta"]["cost_usd"], latency=rep["meta"]["latency_s"]),
        baseline=(dict(answer=base["answer"], accepted=base["accepted"], correct=base_top1, tokens=[base["meta"]["prompt_tokens"], base["meta"]["completion_tokens"]], attempts=len(base["meta"]["attempts"]), cost=base["meta"]["cost_usd"], latency=base["meta"]["latency_s"],
                       truncated=base["meta"].get("baseline", {}).get("truncated"), kept=base["meta"].get("baseline", {}).get("kept_fraction")) if base else None),
        analyzer=dict(correct=an_top1, confidence=an["confidence"]),
    )
    (OUT / f"{iid}.json").write_text(json.dumps(detail, separators=(",", ":")))
    return dict(id=iid, split=split, group=label["group"], type=label["root_cause"]["type"], service=label["root_cause"]["service"],
                failed=sym["failed_traces"], traces=sym["traces"], tb_ok=tb_top1, base_ok=base_top1, an_ok=an_top1,
                tb_conf=rep["final_confidence"], base_conf=(base["answer"]["confidence"] if base and base["answer"] else "none"), vstatus=rep["verification"]["status"],
                tb_cat=rep["answer"]["root_cause"]["category"], tb_svc=rep["answer"]["root_cause"]["service"])


def summary(rows_by_sys):
    out = {}
    for split, ids in rows_by_sys.items():
        out[split] = {}
        for system, rows in ids.items():
            n = len(rows)
            high = [r for r in rows if r["conf"] == "high"]
            out[split][system] = dict(n=n, top1=sum(r["ok"] for r in rows) / n, fc=sum(1 for r in high if not r["ok"]) / n,
                                      high=len(high), prec_high=(sum(r["ok"] for r in high) / len(high)) if high else None,
                                      cost=sum(r.get("cost", 0) for r in rows) / n, latency=sum(r.get("lat", 0) for r in rows) / n)
    return out


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    manifest = [json.loads(l) for l in (ROOT / "eval/split.json").read_text().splitlines() if l.strip()]
    index = []
    per = {}
    for m in manifest:
        iid = m["id"]
        if not (ROOT / "runs/results/traceback" / f"{iid}.json").exists():
            print("skip (no traceback result):", iid)
            continue
        r = per_incident(iid, m["split"])
        index.append(r)
        per[iid] = json.loads((OUT / f"{iid}.json").read_text())
        print(iid, "ok")
    rows = {"dev": {"analyzer": [], "baseline": [], "traceback": [], "traceback_unverified": []}, "test": {"analyzer": [], "baseline": [], "traceback": [], "traceback_unverified": []}}
    for r in index:
        d = per[r["id"]]
        s = r["split"]
        rows[s]["analyzer"].append(dict(ok=d["analyzer"]["correct"], conf=d["analyzer"]["confidence"], lat=0.05))
        if d["baseline"]:
            rows[s]["baseline"].append(dict(ok=d["baseline"]["correct"], conf=d["baseline"]["answer"]["confidence"] if d["baseline"]["answer"] else "none", cost=d["baseline"]["cost"], lat=d["baseline"]["latency"]))
        tb = d["traceback"]
        rows[s]["traceback"].append(dict(ok=tb["correct"], conf=tb["final_confidence"], cost=tb["cost"], lat=tb["latency"]))
        rows[s]["traceback_unverified"].append(dict(ok=tb["correct"], conf=tb["llm_confidence"], cost=tb["cost"], lat=tb["latency"]))
    asrun = None
    p = ROOT / "eval/test_results/baseline.txt"
    if p.exists():
        m = re.search(r"^ALL\s+n=\s*(\d+) top1=\s*([\d.]+)%", p.read_text(), re.M)
        asrun = float(m.group(2)) / 100 if m else None
    abl = {}
    for name, f in (("baseline_plus_principles", "baseline_plus.txt"), ("baseline_ultra_truncated_only", "baseline_ultra.txt")):
        p = ROOT / "eval/test_results" / f
        if p.exists():
            m = re.search(r"^ALL\s+n=\s*(\d+) top1=\s*([\d.]+)%.*?false-confidence=\s*([\d.]+)%", p.read_text(), re.M)
            if m:
                abl[name] = dict(n=int(m.group(1)), top1=float(m.group(2)) / 100, fc=float(m.group(3)) / 100)
    trunc = {s: sum(1 for r in index if r["split"] == s and per[r["id"]]["baseline"] and per[r["id"]]["baseline"]["truncated"]) for s in ("dev", "test")}
    (OUT / "index.json").write_text(json.dumps(dict(incidents=index, summary=summary(rows), baseline_asrun_test_top1=asrun, ablations=abl, truncated_baseline=trunc,
                                                    model="nvidia/nemotron-3-super-120b-a12b"), separators=(",", ":")))
    print("incidents:", len(index))


if __name__ == "__main__":
    main()
