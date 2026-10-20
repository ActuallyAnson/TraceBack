#!/usr/bin/env python3
"""Hybrid vs raw-logs baseline vs Full Traceback v2 on the simulator sets (eval/real3/PLAN.md).

    sim_eval.py --set dev      development sets (v2dev); the only place the hybrid's rule may be examined while it is being written
    sim_eval.py --set fresh    the locked fresh sets, once, after the hybrid is frozen
Model answers come from eval/llm_cache (baseline and v2 were scored there already); a miss would call the model and needs NEBIUS_API_KEY.
"""
import concurrent.futures, json, pathlib, subprocess, sys
HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(ROOT / "eval")); sys.path.insert(0, str(ROOT / "eval/v2"))
import score, report  # noqa: E402

TOPOS = [("t1", "tb_sim"), ("t2", "tb_sim2")]


def prepare_dev(t, sim):
    data = ROOT / "runs/v2dev" / t
    if not (data / "incidents").exists():
        report.sh([ROOT / "build" / sim, "generate", "--set", "v2dev", "--out", data])
    return data


def hybrid(data, iid):
    p = report.sh([ROOT / "build/tb_hybrid", data / "incidents" / iid, "--cache", report.CACHE])
    if p.returncode:
        sys.exit(f"tb_hybrid failed on {iid}: {p.stderr.strip()[:300]}")
    return json.loads(p.stdout)


def evaluate(which):
    rows = []
    for t, sim in TOPOS:
        if which == "fresh":
            data = report.prepare(t, sim)
            labdir = ROOT / "eval/v2/fresh" / t / "labels"
        else:
            data = prepare_dev(t, sim)
            labdir = data / "labels"
        score.DATA = data
        ids = sorted(p.name for p in (data / "incidents").iterdir())
        idx_of = {}
        with concurrent.futures.ThreadPoolExecutor(max_workers=3) as ex:
            hy = dict(zip(ids, ex.map(lambda i: hybrid(data, i), ids)))
            bl = dict(zip(ids, ex.map(lambda i: report.llm(data, i, "baseline", "v1"), ids)))
            tb = dict(zip(ids, ex.map(lambda i: report.llm(data, i, "traceback", "v2"), ids)))
        for iid in ids:
            lab = json.loads((labdir / f"{iid}.json").read_text())
            r = dict(id=iid, t=t, group=lab["group"], label=lab)
            for key, res in (("bl", bl[iid]), ("tb2", tb[iid]), ("hy", hy[iid])):
                rep_ = score.llm_as_report(res)
                top = rep_["hypotheses"][0]
                d = dict(ok=score.judge(lab, rep_)[0], loc=score.judge(lab, rep_)[1], cat=top["category"], svc=top["service"], conf=rep_["confidence"])
                if key != "bl":
                    d["replay"] = score.run_verify(iid, res)
                    d["final"] = score.final_conf(d["conf"], d["replay"])
                else:
                    d["final"] = d["conf"]
                if key == "hy":
                    d["hy"] = res["hybrid"]
                r[key] = d
            rows.append(r)
    return rows


def summ(rows, k, ck):
    n = len(rows)
    high = [r for r in rows if r[k][ck] == "high"]
    return dict(n=n, top1=sum(r[k]["ok"] for r in rows), loc=sum(r[k]["loc"] for r in rows), high=len(high), high_ok=sum(r[k]["ok"] for r in high),
                fc=sum(1 for r in high if not r[k]["ok"]), wrong_mh=sum(1 for r in rows if not r[k]["ok"] and r[k][ck] in ("medium", "high")))


if __name__ == "__main__":
    which = sys.argv[sys.argv.index("--set") + 1]
    rows = evaluate(which)
    for name, k, ck in (("baseline", "bl", "final"), ("tb2", "tb2", "final"), ("hybrid", "hy", "final")):
        print(name, summ(rows, k, ck))
    print("hybrid wrong:", [(r["id"], r["hy"]["cat"], r["hy"]["svc"], r["hy"]["final"], r["label"]["root_cause"]["type"], r["label"]["root_cause"]["service"]) for r in rows if not r["hy"]["ok"]])
    print("tb2 wrong:", [r["id"] for r in rows if not r["tb2"]["ok"]])
    print("by group:", {g: (sum(r["hy"]["ok"] for r in rows if r["group"] == g), sum(r["tb2"]["ok"] for r in rows if r["group"] == g), sum(1 for r in rows if r["group"] == g)) for g in sorted({r["group"] for r in rows})})
