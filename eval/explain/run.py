#!/usr/bin/env python3
"""Explanation-quality scoring driver (eval/explain/PLAN.md).

    run.py --set dev --sheet     judge dev-v2 claims and write the blind hand-labeling sheet (judge validation)
    run.py --set dev --validate  compare eval/explain/hand_labels.json with the judge's verdicts
    run.py --set fresh           score the 48 fresh incidents; writes eval/explain/RESULTS.md and rows.json
    run.py --set fresh --check   fail if RESULTS.md differs from the regenerated one (offline: needs eval/llm_cache and eval/explain/judge_cache)
"""
import concurrent.futures, hashlib, json, os, pathlib, sys, urllib.request
HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(ROOT / "eval")); sys.path.insert(0, str(ROOT / "eval/v2")); sys.path.insert(0, str(HERE))
import score, report, lib  # noqa: E402

MODEL = "nvidia/nemotron-3-super-120b-a12b"
URL = "https://api.tokenfactory.nebius.com/v1/chat/completions"
JCACHE = HERE / "judge_cache"
PROMPT = (HERE / "judge_prompt.txt").read_text()
TOPOS = [("t1", "tb_sim"), ("t2", "tb_sim2")]
SYS = [("bl", "Raw logs → LLM (baseline)"), ("tb1", "Full Traceback v1"), ("tb2", "Full Traceback v2"), ("tpl", "Template (analyzer v2 only, no LLM)")]


def judge_call(claim, rec_lines):
    body = "STATEMENT:\n" + claim + "\n\nQUOTED RECORDS:\n" + ("\n".join(l[:500] for l in rec_lines) if rec_lines else "(none)")
    key = hashlib.sha256((MODEL + PROMPT + body).encode()).hexdigest()[:20]
    f = JCACHE / f"{key}.json"
    if f.exists():
        return json.loads(f.read_text())
    if not rec_lines:
        out = dict(verdict="unsupported", why="no records quoted", cost=0.0)
    else:
        req = dict(model=MODEL, temperature=0, max_tokens=16000, messages=[{"role": "system", "content": PROMPT}, {"role": "user", "content": body}])
        r = urllib.request.Request(URL, json.dumps(req).encode(), {"Content-Type": "application/json", "Authorization": "Bearer " + os.environ["NEBIUS_API_KEY"]})
        resp = json.load(urllib.request.urlopen(r, timeout=180))
        txt = resp["choices"][0]["message"]["content"]
        try:
            j = json.loads(txt[txt.index("{"):txt.rindex("}") + 1])
            assert j["verdict"] in ("supported", "partial", "unsupported")
        except Exception:
            j = dict(verdict="unsupported", why="unparseable judge output")
        u = resp.get("usage", {})
        out = dict(verdict=j["verdict"], why=j.get("why", ""), tokens=[u.get("prompt_tokens", 0), u.get("completion_tokens", 0)])
    JCACHE.mkdir(exist_ok=True)
    f.write_text(json.dumps(out, sort_keys=True))
    return out


def explanations(setname):
    """yield dicts: t, iid, lab, idx, {sys: answer-or-template}, correct{sys}"""
    out = []
    for t, sim in TOPOS:
        if setname == "fresh":
            data = report.prepare(t, sim)
            labdir = ROOT / "eval/v2/fresh" / t / "labels"
        else:
            data = ROOT / "runs/v2dev" / t
            labdir = data / "labels"
        score.DATA = data
        for iid in sorted(p.name for p in (data / "incidents").iterdir()):
            lab = json.loads((labdir / f"{iid}.json").read_text())
            idx = lib.records(data, iid)
            rep2 = report.analyzer("tb_analyze2", data, iid)
            e = dict(t=t, iid=iid, lab=lab, idx=idx, ans={}, ok={})
            for key, system, rules in (("bl", "baseline", "v1"), ("tb1", "traceback", "v1"), ("tb2", "traceback", "v2")):
                res = report.llm(data, iid, system, rules)
                e["ans"][key] = res.get("answer")
                e["ok"][key] = score.judge(lab, score.llm_as_report(res))[0]
            e["ans"]["tpl"] = lib.template(t, rep2, data, iid)
            e["ok"]["tpl"] = score.judge(lab, rep2)[0]
            out.append(e)
    return out


def judge_all(exps):
    jobs = []
    for e in exps:
        for k, a in e["ans"].items():
            if a:
                for i, st in enumerate(a.get("causal_chain", [])):
                    lines = [e["idx"][c.strip("[] ")][2] for c in st.get("cites", []) if c.strip("[] ") in e["idx"]]
                    jobs.append((e, k, i, st.get("claim", ""), lines))
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as ex:
        res = list(ex.map(lambda j: judge_call(j[3], j[4]), jobs))
    for (e, k, i, _, _), r in zip(jobs, res):
        e.setdefault("verd", {}).setdefault(k, {})[i] = r["verdict"]
    return jobs, res


def sheet(exps):
    jobs, res = judge_all(exps)
    pick = sorted(range(len(jobs)), key=lambda n: hashlib.sha256(jobs[n][3].encode()).hexdigest())
    sup = [n for n in pick if res[n]["verdict"] == "supported"][:15]
    bad = [n for n in pick if res[n]["verdict"] != "supported"][:15]
    chosen = sorted(sup + bad, key=lambda n: hashlib.sha256(("x" + jobs[n][3]).encode()).hexdigest())  # shuffle so the sheet reveals nothing
    L = ["# Blind sheet: label each claim supported / partial / unsupported by the quoted records only", ""]
    keymap = []
    for q, n in enumerate(chosen, 1):
        e, k, i, claim, lines = jobs[n]
        L += [f"## Q{q}", f"STATEMENT: {claim}", "RECORDS:"] + [f"- {l[:500]}" for l in lines] + [""]
        keymap.append(dict(q=q, iid=e["iid"], sys=k, step=i, judge=res[n]["verdict"], why=res[n]["why"]))
    (HERE / "validation_sheet.md").write_text("\n".join(L))
    (HERE / "validation_key.json").write_text(json.dumps(keymap, indent=1))
    print(f"{len(jobs)} dev claims judged; sheet of {len(chosen)} written (judge verdicts are in validation_key.json — do not read before labeling)")


def validate():
    key = json.loads((HERE / "validation_key.json").read_text())
    hand = json.loads((HERE / "hand_labels.json").read_text())
    agree = sum(hand[str(k["q"])] == k["judge"] for k in key)
    bin_agree = sum((hand[str(k["q"])] == "supported") == (k["judge"] == "supported") for k in key)
    L = ["# Judge validation (dev-v2 claims, not the fresh set)", "",
         f"{len(key)} claims: 15 the judge called supported and {len(key) - 15} it called partial/unsupported (as available), shuffled; labeled by hand blind to the judge's verdict.",
         "**Labeler: the assistant (Claude) that wrote the plan, not an independent human.** The user can re-label `validation_sheet.md` and compare with `validation_key.json`.", "",
         f"- exact agreement on 3 classes: {agree}/{len(key)}", f"- agreement on supported vs. not supported: {bin_agree}/{len(key)}", "",
         "| Q | hand | judge | judge's reason |", "|---|---|---|---|"]
    for k in key:
        L.append(f"| {k['q']} | {hand[str(k['q'])]} | {k['judge']} | {k['why'][:120]} |")
    (HERE / "JUDGE_VALIDATION.md").write_text("\n".join(L) + "\n")
    print("\n".join(L[:8]))


def metrics(exps, sel=None):
    R = {}
    for k, _ in SYS:
        rows = [e for e in exps if e["ans"][k] and (sel is None or e["ok"][k])]
        m = [lib.mechanical(e["t"], e["lab"], e["idx"], e["ans"][k], e["ok"][k]) for e in rows]
        for e, x in zip(rows, m):
            v = e["verd"][k]
            x["claims"] = len(v)
            x["unsup"] = sum(1 for s in v.values() if s == "unsupported")
            x["part"] = sum(1 for s in v.values() if s == "partial")
        R[k] = (rows, m)
    return R


def pc(a, b):
    return "–" if not b else f"{100 * a / b:.1f}% ({a}/{b})"


def table(exps, sel=None):
    R = metrics(exps, sel)
    L = ["| System | n | Cited records in the true chain (a1) | Decoy records cited (a2) | Chain services cited (a3) | Order correct (b) | First step at the origin (b2) | Root cause + mechanism stated (c2) | Claims unsupported by citations (d) | Explanations with ≥1 unsupported | Partial claims |",
         "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|"]
    for k, name in SYS:
        rows, m = R[k]
        n = len(m)
        if not n:
            L.append(f"| {name} | 0 | – | – | – | – | – | – | – | – | – |")
            continue
        od = [x["ordered"] for x in m if x["ordered"] is not None]
        fo = [x["first_origin"] for x in m if x["first_origin"] is not None]
        L.append(f"| {name} | {n} | {pc(sum(x['chain'] for x in m), sum(x['exist'] for x in m))} | {sum(x['decoy'] for x in m)} | {100 * sum(x['cov'] for x in m) / n:.1f}% | {pc(sum(od), len(od))} | {pc(sum(fo), len(fo))} | {pc(sum(x['c2'] for x in m), n)} | {pc(sum(x['unsup'] for x in m), sum(x['claims'] for x in m))} | {pc(sum(1 for x in m if x['unsup']), n)} | {sum(x['part'] for x in m)} |")
    return L


def worse(exps):
    R = metrics(exps)
    tp = {e["iid"]: x for e, x in zip(*R["tpl"])}
    L = []
    for k, name in SYS[:3]:
        rows, m = R[k]
        by = {"chain precision lower": [], "order wrong where template's is right": [], "mechanism/root not stated where template's is": [], "more unsupported claims": []}
        for e, x in zip(rows, m):
            t = tp[e["iid"]]
            r1 = x["chain"] / x["exist"] if x["exist"] else None
            r0 = t["chain"] / t["exist"] if t["exist"] else None
            if r1 is not None and r0 is not None and r1 < r0 - 1e-9:
                by["chain precision lower"].append(f"{e['iid']} ({r1:.2f} vs {r0:.2f})")
            if t["ordered"] and x["ordered"] is False:
                by["order wrong where template's is right"].append(e["iid"])
            if t["c2"] and not x["c2"]:
                by["mechanism/root not stated where template's is"].append(e["iid"] + ("" if e["ok"][k] else " (wrong answer)"))
            if x["unsup"] > t["unsup"]:
                by["more unsupported claims"].append(f"{e['iid']} ({x['unsup']} vs {t['unsup']})")
        L += [f"- **{name}**"] + [f"  - {d}: {len(v)} — {', '.join(v) if v else 'none'}" for d, v in by.items()]
    return L


def build(exps):
    L = ["<!-- EXPLAIN:START (generated by eval/explain/run.py; do not edit by hand) -->", "",
         f"**Explanation quality on the fresh v2 sets, n={len(exps)} (locked at `67e9729`); scoring defined in `eval/explain/PLAN.md` before any scoring.** "
         "(a) cited records in the true causal chain, (b) order, (c) root cause and mechanism stated, (d) claims not supported by their own citations (judge model, validated on a small hand-checked sample: `eval/explain/JUDGE_VALIDATION.md`).", "", "All incidents:", ""]
    L += table(exps)
    L += ["", "Only incidents where that system's top answer was correct:", ""] + table(exps, True)
    for t, title in (("t1", "Topology 1"), ("t2", "Topology 2")):
        L += ["", f"{title} only:", ""] + table([e for e in exps if e["t"] == t])
    L += ["", "Every case where an LLM explanation is worse than the template, by dimension:", ""] + worse(exps)
    L += ["", "<!-- EXPLAIN:END -->"]
    return "\n".join(L)


def main():
    setname = sys.argv[sys.argv.index("--set") + 1]
    if "--validate" in sys.argv:
        return validate()
    exps = explanations(setname)
    if "--sheet" in sys.argv:
        return sheet(exps)
    judge_all(exps)
    block = build(exps)
    readme = ROOT / "README.md"
    txt = readme.read_text()
    a, b = txt.find("<!-- EXPLAIN:START"), txt.find("<!-- EXPLAIN:END -->")
    end = b + len("<!-- EXPLAIN:END -->")
    head = "# Explanation-quality results on the fresh sets (generated)\n\n"
    if "--check" in sys.argv:
        if (HERE / "RESULTS.md").read_text() != head + block + "\n" or a < 0 or txt[a:end] != block:
            sys.exit("explanation-quality results or README block out of date; run `make explain`")
        print("explanation-quality results match")
        return
    (HERE / "RESULTS.md").write_text(head + block + "\n")
    if a >= 0:
        readme.write_text(txt[:a] + block + txt[end:])
    print(block)


if __name__ == "__main__":
    main()
