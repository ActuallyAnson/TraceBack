#!/usr/bin/env python3
"""Evaluation harness. Scoring follows eval/SCORING.md (pre-registered).

The analyzer is run as a subprocess on runs/incidents/<id>/ only. Labels are read here,
never passed to the analyzer. Default split is dev; test requires --confirm-test and is logged.
"""
import argparse, collections, concurrent.futures, datetime, json, os, pathlib, subprocess, sys, time

ROOT = pathlib.Path(__file__).resolve().parent.parent
DATA = ROOT / "runs"                       # topology 1 data root; --topology2 points it at runs/topo2
SPLIT_FILE = ROOT / "eval/split.json"
RUN_LOG = ROOT / "eval/test_runs.log"
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
    rows = [json.loads(l) for l in SPLIT_FILE.read_text().splitlines() if l.strip()]
    return [r["id"] for r in rows if split == "all" or r["split"] == split]


def run_analyzer(iid):
    t = time.time()
    out = subprocess.run([str(ROOT / "build/tb_analyze"), str(DATA / "incidents" / iid)], capture_output=True, text=True, check=True)
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



# Variants: extra tb_reason arguments and a results-directory suffix. "default" is the main comparison.
VARIANTS = {
    "default": [],
    "plus": ["--prompt", "baseline_plus"],
    "ultra": ["--model", "nvidia/Nemotron-3-Ultra-550b-a55b", "--budget", "900000", "--price-in", "0.000001", "--price-out", "0.000003"],
}


def baseline_truncated(iid):
    p = subprocess.run([str(ROOT / "build/tb_reason"), str(DATA / "incidents" / iid), "--system", "baseline", "--dry-run", "yes"], capture_output=True, text=True, check=True)
    return json.loads(p.stdout)["meta"]["baseline"]["truncated"]


def run_llm(iid, system, variant="default"):
    """Run tb_reason on one incident; raw result cached to runs/results/<system>/<id>.json."""
    out_dir = DATA / "results" / (system if variant == "default" else f"{system}_{variant}")
    out_dir.mkdir(parents=True, exist_ok=True)
    cache = ROOT / "eval/llm_cache"
    t = time.time()
    p = subprocess.run([str(ROOT / "build/tb_reason"), str(DATA / "incidents" / iid), "--system", system, "--cache", str(cache)] + VARIANTS[variant],
                       capture_output=True, text=True)
    if p.returncode != 0:
        return {"incident_id": iid, "system": system, "answer": None, "accepted": False, "error": p.stderr.strip()[:300], "meta": {"attempts": []}}
    res = json.loads(p.stdout)
    (out_dir / f"{iid}.json").write_text(json.dumps(res, indent=1))
    return res


def record_index(iid):
    """id -> service for every record in the incident (scorer-side, used for existence/relevance)."""
    idx = {}
    d = DATA / "incidents" / iid
    for fn in ("logs.jsonl", "spans.jsonl", "metrics.jsonl", "events.jsonl"):
        for line in (d / fn).read_text().splitlines():
            if line.strip():
                j = json.loads(line)
                idx[j["id"]] = j.get("service", "")
    return idx


def llm_as_report(res):
    """View an LLM answer as a ranked hypothesis list so judge() can score all systems alike."""
    a = res.get("answer")
    if not a:
        return {"hypotheses": [{"category": "none", "service": "none"}], "confidence": "none"}
    hyps = [{"category": a["root_cause"]["category"], "service": a["root_cause"]["service"]}]
    for alt in a.get("alternatives", []):
        hyps.append({"category": alt.get("category", ""), "service": alt.get("service", "")})
    return {"hypotheses": hyps, "confidence": a.get("confidence", "low")}


def cite_stats(res, label, idx):
    a = res.get("answer")
    if not a:
        return None
    rc = label["root_cause"]
    names = {rc["service"]} | ({rc["edge"]["from"], rc["edge"]["to"]} if rc.get("edge") else set())
    cites = []
    for part in (a.get("causal_chain", []) + a.get("alternatives", [])):
        for c in part.get("cites", []):
            cites.append(c.strip("[] "))
    exist = [c for c in cites if c in idx]
    relevant = [c for c in exist if idx[c] in names]
    sup = None
    if res.get("analyzer_support"):
        ans = (a["root_cause"]["category"], a["root_cause"]["service"])
        sets = [set(h["support_ids"]) for h in res["analyzer_support"] if (h["category"], h["service"]) == ans]
        allowed = sets[0] if sets else set().union(*[set(h["support_ids"]) for h in res["analyzer_support"]])
        sup = sum(1 for c in exist if c in allowed)
    first = (res["meta"]["attempts"] or [{}])[0]
    return dict(total=len(cites), exist=len(exist), relevant=len(relevant), support=sup,
                first_total=first.get("cites_total", 0), first_invalid=len(first.get("cites_invalid", [])))


def run_verify(iid, res):
    """Counterfactual replay of the LLM's answer (category, service). Returns the verifier's status."""
    a = res.get("answer")
    if not a:
        return "inconclusive"
    hyp = f'{a["root_cause"]["category"]}:{a["root_cause"]["service"]}'
    p = subprocess.run([str(ROOT / "build/tb_verify"), str(DATA / "incidents" / iid), "--replay", str(DATA / "replay" / f"{iid}.json"), "--hyp", hyp],
                       capture_output=True, text=True)
    if p.returncode != 0:
        return "inconclusive"
    return json.loads(p.stdout)[0]["status"]


def final_conf(conf, status):
    """Pre-registered policy (SCORING.md): failed -> low; inconclusive caps at medium; passed keeps it."""
    if status == "failed":
        return "low"
    if status == "inconclusive" and conf == "high":
        return "medium"
    return conf


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
    ap.add_argument("--system", default="analyzer", choices=["analyzer", "baseline", "traceback"])
    ap.add_argument("--workers", type=int, default=3)
    ap.add_argument("--topology2", action="store_true", help="score the second-topology generalization incidents (eval/topology2)")
    ap.add_argument("--variant", default="default", choices=list(VARIANTS), help="baseline ablations; see SCORING.md")
    ap.add_argument("--verbose", action="store_true")
    a = ap.parse_args()
    global DATA, SPLIT_FILE, RUN_LOG
    if a.topology2:
        DATA, SPLIT_FILE, RUN_LOG = ROOT / "runs/topo2", ROOT / "eval/topology2/manifest.json", ROOT / "eval/topology2/runs.log"
        a.split = a.split if a.split != "dev" else "test"
    if a.split != "dev" and not a.confirm_test:
        sys.exit("refusing to score %r without --confirm-test (held-out data; every run is logged)" % a.split)
    if a.split != "dev":
        head = subprocess.run(["git", "rev-parse", "--short", "HEAD"], capture_output=True, text=True, cwd=ROOT).stdout.strip()
        dirty = bool(subprocess.run(["git", "status", "--porcelain", "--", "src", "include", "eval"], capture_output=True, text=True, cwd=ROOT).stdout.strip())
        with open(RUN_LOG, "a") as f:
            f.write(f"{datetime.datetime.now(datetime.timezone.utc).isoformat()} split={a.split} system={a.system} variant={a.variant} commit={head}{'+dirty' if dirty else ''}\n")

    ids = load_split(a.split)
    if a.variant != "default" and a.system != "baseline":
        sys.exit("variants apply to the baseline only")
    if a.variant == "ultra":  # sensitivity check: only incidents the main-model baseline had to truncate
        ids = [i for i in ids if baseline_truncated(i)]
    labels = {i: json.loads((DATA / "labels" / f"{i}.json").read_text()) for i in ids}
    llm_res = {}
    if a.system in ("baseline", "traceback"):
        with concurrent.futures.ThreadPoolExecutor(max_workers=a.workers) as ex:
            for iid, res in zip(ids, ex.map(lambda i: run_llm(i, a.system, a.variant), ids)):
                llm_res[iid] = res
    rows = []
    for iid in ids:
        label = labels[iid]
        skew_err = None
        res = llm_res.get(iid)
        if a.system == "analyzer":
            rep, secs = run_analyzer(iid)
        else:
            rep, secs = llm_as_report(res), res["meta"].get("latency_s", 0.0)
        top1, svc, top3 = judge(label, rep)
        top = rep["hypotheses"][0]
        if a.system == "analyzer" and label["complications"]:
            inj = {c["service"]: c["offset_us"] for c in label["complications"]}
            est = rep["clock"]["offsets_us"]
            names = list(est)
            skew_err = max(abs((est[x] - est[y]) - (inj.get(x, 0) - inj.get(y, 0))) for x in names for y in names) / 1000.0
        row = dict(skew_err=skew_err, id=iid, group=label["group"], type=label["root_cause"]["type"], svc_true=label["root_cause"]["service"],
                   top1=top1, svc=svc, top3=top3, conf=rep["confidence"], top=f'{top["category"]}/{top["service"]}',
                   alts=[f'{h["category"]}/{h["service"]}' for h in rep["hypotheses"][1:3]], secs=secs)
        if res is not None and a.system == "traceback":
            row["vstatus"] = run_verify(iid, res)
            row["conf_full"] = final_conf(row["conf"], row["vstatus"])
        if res is not None:
            m = res["meta"]
            row.update(cost=m.get("cost_usd", 0.0), ptok=m.get("prompt_tokens", 0), ctok=m.get("completion_tokens", 0), accepted=res.get("accepted", False),
                       attempts=len(m["attempts"]), cites=cite_stats(res, label, record_index(iid)),
                       truncated=m.get("baseline", {}).get("truncated"), kept=m.get("baseline", {}).get("kept_fraction"))
        rows.append(row)
    print(f"system={a.system} variant={a.variant} split={a.split} n_incidents={len(rows)}")
    summarize(rows, "ALL")
    summarize([r for r in rows if r["type"] in MAP], "mapped types (category+svc)")
    summarize([r for r in rows if r["type"] not in MAP], "unmapped types (svc only)")
    for g in sorted({r["group"] for r in rows}):
        summarize([r for r in rows if r["group"] == g], f"group={g}")
    for t in sorted({r["type"] for r in rows}):
        summarize([r for r in rows if r["type"] == t], f"type={t}")
    errs = [r["skew_err"] for r in rows if r["skew_err"] is not None]
    if errs:
        print(f"clock-offset recovery on {len(errs)} skewed incidents: max error {max(errs):.1f} ms, mean {sum(errs)/len(errs):.1f} ms (injected skews are 400-3000 ms)")
    if a.system == "traceback":
        print("\n--- FULL TRACEBACK = analyzer + LLM + verifier (confidence per pre-registered policy; answers unchanged) ---")
        full = [dict(r, conf=r["conf_full"]) for r in rows]
        summarize(full, "ALL (with verifier)")
        for g in sorted({r["group"] for r in rows}):
            summarize([r for r in full if r["group"] == g], f"group={g} (with verifier)")
        cnt = collections.Counter((r["top1"], r["vstatus"]) for r in rows)
        for ok in (True, False):
            tot = sum(v for (o, _), v in cnt.items() if o == ok)
            if tot:
                print(f"  answers that are {'CORRECT' if ok else 'WRONG  '} (n={tot}): " + ", ".join(f"{st} {cnt[(ok, st)]}" for st in ("passed", "failed", "inconclusive")))
    if a.system == "analyzer":
        print(f"mean latency {sum(r['secs'] for r in rows)/len(rows)*1000:.0f} ms/incident, cost $0 (no LLM)")
    else:
        n = len(rows)
        cs = [r["cites"] for r in rows if r.get("cites")]
        tot = sum(c["total"] for c in cs) or 1
        print(f"answers accepted: {sum(r['accepted'] for r in rows)}/{n}; mean attempts {sum(r['attempts'] for r in rows)/n:.2f}")
        print(f"citation existence: {sum(c['exist'] for c in cs)/tot:.1%} of {tot} cites exist; first-attempt invalid rate "
              f"{sum(c['first_invalid'] for c in cs)/max(1, sum(c['first_total'] for c in cs)):.1%}; relevance (cited record in root-cause service/edge): {sum(c['relevant'] for c in cs)/tot:.1%}")
        sup = [c for c in cs if c["support"] is not None]
        if sup:
            print(f"analyzer-support: {sum(c['support'] for c in sup)/max(1, sum(c['total'] for c in sup)):.1%} of cites are in the analyzer's support set")
        print(f"mean prompt/completion tokens {sum(r['ptok'] for r in rows)/n:.0f}/{sum(r['ctok'] for r in rows)/n:.0f}; "
              f"mean cost ${sum(r['cost'] for r in rows)/n:.4f}/incident (total ${sum(r['cost'] for r in rows):.2f}); mean latency {sum(r['secs'] for r in rows)/n:.1f} s")
        tr = [r for r in rows if r.get("truncated") is not None]
        if tr:
            print(f"baseline input truncated for {sum(1 for r in tr if r['truncated'])}/{len(tr)} incidents")
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
