#!/usr/bin/env python3
"""v2 evaluation on the fresh sets (eval/v2/PLAN.md): five systems, two topologies, one table.

    python3 eval/v2/report.py            run everything (model answers come from eval/llm_cache; a cache miss calls the model and needs
                                         NEBIUS_API_KEY), write eval/v2/RESULTS.md and refresh the README block
    python3 eval/v2/report.py --record   same, and append one line per system to eval/v2/runs.log (the first, real run)
    python3 eval/v2/report.py --check    fail if the README block differs from the regenerated one (offline, no key)

Systems: raw-logs baseline, analyzer v1 (frozen), analyzer v2, full Traceback v1 (v1 analyzer + v1 prompt + verifier),
full Traceback v2 (v2 analyzer + v2 prompt + override rule + verifier). Same model, temperature 0, scoring as eval/SCORING.md.
"""
import concurrent.futures, datetime, json, pathlib, subprocess, sys
ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "eval"))
import score  # noqa: E402

CACHE = ROOT / "eval/llm_cache"
TOPOS = [("t1", "tb_sim", "Topology 1 (4 services)"), ("t2", "tb_sim2", "Topology 2 (6-service fan-out)")]
pc = lambda x: "–" if x is None else f"{x * 100:.1f}%"
usd = lambda x: "$0" if not x else (f"${x:.4f}" if x < 0.01 else f"${x:.3f}")


def sh(args, **kw):
    return subprocess.run([str(a) for a in args], capture_output=True, text=True, **kw)


def prepare(t, sim):
    """Regenerate the fresh set deterministically and check it against the locked manifest and the committed labels."""
    data = ROOT / "runs/v2fresh" / t
    man = ROOT / "eval/v2/fresh" / f"manifest_{t}.json"
    p = sh([ROOT / "build" / sim, "verify", "--set", "v2fresh", "--manifest", man])
    if p.returncode:
        sys.exit(f"fresh set {t} no longer matches its locked manifest: {p.stderr.strip()}")
    p = sh([ROOT / "build" / sim, "generate", "--set", "v2fresh", "--out", data])
    if p.returncode:
        sys.exit(f"cannot generate fresh set {t}: {p.stderr.strip()}")
    for lab in sorted((ROOT / "eval/v2/fresh" / t / "labels").glob("*.json")):
        if (data / "labels" / lab.name).read_text() != lab.read_text():
            sys.exit(f"label {lab.name} differs from the committed ground truth")
    return data


def analyzer(exe, data, iid):
    return json.loads(sh([ROOT / "build" / exe, data / "incidents" / iid], check=True).stdout)


def llm(data, iid, system, rules="v1"):
    args = [ROOT / "build/tb_reason", data / "incidents" / iid, "--system", system, "--cache", CACHE]
    if rules == "v2":
        args += ["--rules", "v2"]
    p = sh(args)
    if p.returncode:
        sys.exit(f"tb_reason failed on {iid} ({system} {rules}): {p.stderr.strip()[:300]}")
    return json.loads(p.stdout)


def evaluate(t, sim):
    data = prepare(t, sim)
    score.DATA = data
    ids = sorted(p.name for p in (data / "incidents").iterdir())
    labels = {i: json.loads((ROOT / "eval/v2/fresh" / t / "labels" / f"{i}.json").read_text()) for i in ids}
    jobs = [(i, s) for i in ids for s in ("bl", "tb1", "tb2")]
    spec = {"bl": ("baseline", "v1"), "tb1": ("traceback", "v1"), "tb2": ("traceback", "v2")}
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as ex:
        res = dict(zip(jobs, ex.map(lambda j: llm(data, j[0], *spec[j[1]]), jobs)))
    rows = []
    for iid in ids:
        lab = labels[iid]
        r = {"id": iid, "t": t, "group": lab["group"], "label": lab}
        for key, exe in (("an1", "tb_analyze"), ("an2", "tb_analyze2")):
            rep = analyzer(exe, data, iid)
            top = rep["hypotheses"][0]
            r[key] = dict(ok=score.judge(lab, rep)[0], cat=top["category"], svc=top["service"], conf=rep["confidence"])
        idx = score.record_index(iid)
        for key in ("bl", "tb1", "tb2"):
            res_ = res[(iid, key)]
            rep = score.llm_as_report(res_)
            top = rep["hypotheses"][0]
            d = dict(ok=score.judge(lab, rep)[0], cat=top["category"], svc=top["service"], conf=rep["confidence"],
                     cost=res_["meta"]["cost_usd"], accepted=res_.get("accepted", False), cites=score.cite_stats(res_, lab, idx),
                     attempts=len(res_["meta"]["attempts"]), rejected="override_rejected" in res_,
                     overrode=bool(res_.get("analyzer_top")) and (top["category"], top["service"]) != (res_["analyzer_top"]["category"], res_["analyzer_top"]["service"]))
            if key != "bl":
                d["replay"] = score.run_verify(iid, res_)
                d["final"] = score.final_conf(d["conf"], d["replay"])
            r[key] = d
        rows.append(r)
    return rows


def agg(rows, key, conf_key="conf"):
    n = len(rows)
    if not n:
        return None
    high = [r for r in rows if r[key][conf_key] == "high"]
    cs = [r[key]["cites"] for r in rows if r[key].get("cites")]
    tot = sum(c["total"] for c in cs)
    return dict(n=n, top1=sum(r[key]["ok"] for r in rows) / n, high=len(high),
                prec=(sum(r[key]["ok"] for r in high) / len(high)) if high else None,
                fc=sum(1 for r in high if not r[key]["ok"]) / n,
                cites=(sum(c["exist"] for c in cs) / tot) if tot else None,
                cost=(sum(r[key].get("cost", 0) for r in rows) / n) if key in ("bl", "tb1", "tb2") else 0.0)


SYSTEMS = [
    ("Raw logs → LLM (baseline)", "bl", "conf"),
    ("Analyzer v1 (frozen)", "an1", "conf"),
    ("Analyzer v2", "an2", "conf"),
    ("Full Traceback v1", "tb1", "final"),
    ("**Full Traceback v2**", "tb2", "final"),
]


def table(rows):
    L = ["| System | Top-1 | Precision when “high” | Confidently wrong | Citations that exist | Decoy cases top-1 | Cost / incident |", "|---|---:|---:|---:|---:|---:|---:|"]
    dec = [r for r in rows if r["group"] == "decoy"]
    for name, key, ck in SYSTEMS:
        a, d = agg(rows, key, ck), agg(dec, key, ck)
        L.append(f"| {name} | {pc(a['top1'])} | {'–' if a['prec'] is None else pc(a['prec']) + ' (' + str(a['high']) + ')'} | {pc(a['fc'])} | {pc(a['cites'])} | {pc(d['top1'])} ({sum(r[key]['ok'] for r in dec)}/{len(dec)}) | {usd(a['cost'])} |")
    return L


def build(all_rows):
    mark = lambda ok: "✓" if ok else "✗"
    L = ["<!-- V2:START (generated by eval/v2/report.py; do not edit by hand) -->", "",
         f"**v2 on the fresh sets: {len(all_rows)} incidents ({sum(1 for r in all_rows if r['t'] == 't1')} per topology), generated and locked before any system ran on them** "
         "(`eval/v2/FRESH_LOCK`). Both topologies pooled:", ""]
    L += table(all_rows)
    for t, _, title in TOPOS:
        rows = [r for r in all_rows if r["t"] == t]
        L += ["", f"{title}, n={len(rows)}:", ""] + table(rows)
    L += ["", "Top-1 by kind of incident (Analyzer v1 / Analyzer v2 / Full Traceback v1 / Full Traceback v2 / baseline):", "",
          "| Group (n) | Baseline | Analyzer v1 | Analyzer v2 | Full Traceback v1 | Full Traceback v2 |", "|---|---:|---:|---:|---:|---:|"]
    for g in ("plain", "skew", "storm_variant", "decoy", "unseen"):
        rs = [r for r in all_rows if r["group"] == g]
        f = lambda k: pc(sum(r[k]["ok"] for r in rs) / len(rs))
        L.append(f"| {g} ({len(rs)}) | {f('bl')} | {f('an1')} | {f('an2')} | {f('tb1')} | {f('tb2')} |")
    ov = [r for r in all_rows if r["tb2"]["overrode"] or r["tb2"]["rejected"]]
    L += ["", f"Override rule in Full Traceback v2: the model differed from the analyzer's top candidate in {sum(1 for r in all_rows if r['tb2']['overrode'] or r['tb2']['rejected'])} of {len(all_rows)} incidents "
          f"({sum(1 for r in all_rows if r['tb2']['rejected'])} of those answers were replaced by the analyzer's choice for lacking contradicting records); "
          f"in v1 the model differed in {sum(1 for r in all_rows if r['tb1']['overrode'])}.", ""]
    L += ["Every incident (✓ = top-1 correct; the answer is shown for every ✗; for the two Full Traceback systems: confidence after the verifier, replay verdict on the model's answer):", "",
          "| Incident | Truth (service) | Baseline | Analyzer v1 | Analyzer v2 | Full Traceback v1 | Full Traceback v2 |", "|---|---|---|---|---|---|---|"]
    for r in all_rows:
        rc = r["label"]["root_cause"]
        edge = f" {rc['edge']['from']}→{rc['edge']['to']}" if rc.get("edge") else ""
        trig = f"; trigger {rc['trigger_service']}" if rc.get("trigger_service") else ""
        cell = lambda k: mark(r[k]["ok"]) if r[k]["ok"] else f"✗ {r[k]['cat']}/{r[k]['svc']}"
        full = lambda k: f"{cell(k)} ({r[k]['final']}; replay {r[k]['replay']})"
        L.append(f"| {r['id']} ({r['group']}) | {rc['type']} ({rc['service']}{edge}{trig}) | {cell('bl')} | {cell('an1')} | {cell('an2')} | {full('tb1')} | {full('tb2')} |")
    wrong = {name: [r["id"] for r in all_rows if not r[k]["ok"]] for name, k, _ in SYSTEMS}
    L += ["", "Wrong answers per system (all of them): " + "; ".join(f"{n.replace('**', '')}: {', '.join(v) if v else 'none'}" for n, v in wrong.items()) + ".", "",
          "<!-- V2:END -->"]
    return "\n".join(L)


def main():
    rows = []
    for t, sim, _ in TOPOS:
        rows += evaluate(t, sim)
    block = build(rows)
    readme = ROOT / "README.md"
    txt = readme.read_text()
    a, b = txt.find("<!-- V2:START"), txt.find("<!-- V2:END -->")
    if "--check" in sys.argv:
        if a < 0 or b < 0 or txt[a:b + len("<!-- V2:END -->")] != block:
            sys.exit("README.md v2 block is missing or out of date; run `make v2`")
        print("README v2 block matches the generated results")
        return
    if "--record" in sys.argv:
        head = sh(["git", "rev-parse", "--short", "HEAD"], cwd=ROOT).stdout.strip()
        dirty = bool(sh(["git", "status", "--porcelain", "--", "src", "include", "prompts", "prompts_v2", "eval/v2"], cwd=ROOT).stdout.strip())
        now = datetime.datetime.now(datetime.timezone.utc).isoformat()
        with open(ROOT / "eval/v2/runs.log", "a") as f:
            for name, key, _ in SYSTEMS:
                f.write(f"{now} set=v2fresh system={name.replace('**', '').replace(' ', '_')} n={len(rows)} commit={head}{'+dirty' if dirty else ''}\n")
    (ROOT / "eval/v2/RESULTS.md").write_text("# v2 results on the fresh sets (generated)\n\n" + block + "\n")
    if a >= 0 and b >= 0:
        readme.write_text(txt[:a] + block + txt[b + len("<!-- V2:END -->"):])
    print(block)


if __name__ == "__main__":
    main()
