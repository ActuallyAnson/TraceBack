"""Explanation-quality scoring (eval/explain/PLAN.md). Mechanical checks against the label files; the judge model is in run.py."""
import json, pathlib, re, subprocess

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
PARENT = {  # call graph of the two simulators: child -> its caller
    "t1": {"inventory": "orders", "payments": "orders", "orders": "gateway"},
    "t2": {"billing": "broker", "shipping": "broker", "notifier": "broker", "broker": "ingest", "ingest": "api"},
}
ENTRY = {"t1": "gateway", "t2": "api"}
MECH = {
    "retry_storm": "retr", "connection_pool_exhaustion": "pool", "bad_deploy": "deploy", "config_change": "config",
    "crash_restart": "crash|restart|refus", "crash_loop": "crash|restart|refus", "capacity_reduction": "worker|capacity|cpu|saturat",
    "slow_dependency": "slow|latenc", "network_partition": "partition|unreachable|dropped|lost|unresponsive|cannot reach",
    "masked_downstream_error": "mask|swallow|hidden|downstream",
}


def records(data, iid):
    """id -> (service, ts_us, raw line)"""
    idx = {}
    for fn in ("logs.jsonl", "spans.jsonl", "metrics.jsonl", "events.jsonl"):
        for line in (data / "incidents" / iid / fn).read_text().splitlines():
            if line.strip():
                j = json.loads(line)
                idx[j["id"]] = (j.get("service", ""), j.get("ts_us", j.get("start_us", 0)), line)
    return idx


def depths(t, rc):
    seed = {}
    if rc.get("edge"):
        seed = {rc["edge"]["from"]: 0, rc["edge"]["to"]: 0}
    elif rc.get("trigger_service"):
        seed = {rc["trigger_service"]: 0, rc["service"]: 1}
    else:
        seed = {rc["service"]: 0}
    d = dict(seed)
    for s in list(seed):
        cur = s
        while cur in PARENT[t]:
            p = PARENT[t][cur]
            d.setdefault(p, d[cur] + 1)
            cur = p
    return d


def chain_info(t, lab):
    rc = lab["root_cause"]
    dep = depths(t, rc)
    skew = {c["service"]: abs(c["offset_us"]) for c in lab.get("complications", []) if c.get("type") == "clock_skew"}
    decoy_svcs = {d["service"] for d in lab.get("decoys", [])} - set(dep)
    return dep, skew, decoy_svcs, rc


def in_chain(rec, dep, skew, rc):
    svc, ts, _ = rec
    if svc not in dep:
        return False
    lo = rc["onset_us"] - 10_000_000 - skew.get(svc, 0)
    hi = rc["end_us"] + 10_000_000 + skew.get(svc, 0)
    return lo <= ts <= hi


def explanation_of(ans):
    """(root_cause dict, steps) from an LLM answer or a template."""
    if not ans:
        return None
    return ans.get("root_cause", {}), ans.get("causal_chain", [])


def mechanical(t, lab, idx, ans, correct):
    dep, skew, decoy_svcs, rc = chain_info(t, lab)
    got = explanation_of(ans)
    out = dict(cited=0, exist=0, chain=0, decoy=0, cov=None, placed=0, ordered=None, first_origin=None, c2=False, steps=0)
    if not got:
        return out
    root, steps = got
    out["steps"] = len(steps)
    cited, step_depths, seen_chain_svcs = [], [], set()
    for st in steps:
        ids = [c.strip("[] ") for c in st.get("cites", [])]
        cited += ids
        ds = []
        for c in ids:
            if c in idx and in_chain(idx[c], dep, skew, rc):
                ds.append(dep[idx[c][0]])
                seen_chain_svcs.add(idx[c][0])
        step_depths.append(min(ds) if ds else None)
    out["cited"] = len(cited)
    ex = [c for c in cited if c in idx]
    out["exist"] = len(ex)
    out["chain"] = sum(1 for c in ex if in_chain(idx[c], dep, skew, rc))
    out["decoy"] = sum(1 for c in ex if idx[c][0] in decoy_svcs)
    out["cov"] = len(seen_chain_svcs) / len(dep)
    placed = [d for d in step_depths if d is not None]
    out["placed"] = len(placed)
    if len(placed) >= 2:
        out["ordered"] = all(a <= b for a, b in zip(placed, placed[1:]))
    if placed:
        out["first_origin"] = placed[0] == 0
    text = " ".join([root.get("summary", "")] + [s.get("claim", "") for s in steps]).lower()
    needs = [rc["service"]] + ([rc["trigger_service"]] if rc.get("trigger_service") else [])
    mech = re.search(MECH[rc["type"]], text) is not None
    out["c2"] = bool(correct and mech and all(n in text for n in needs))
    return out


# ---- template control (no LLM, no labels) ----
def template(t, rep, data, iid):
    top = rep["hypotheses"][0]
    off = rep["clock"]["offsets_us"]
    svc = top["edge"]["from"] if top.get("edge") else top["service"]
    callers = {}
    for g in sorted(rep["graph"], key=lambda g: -g["failed"]):
        callers.setdefault(g["callee"], g["caller"])
    path, cur = [svc], svc
    while cur in callers and callers[cur] not in path:
        cur = callers[cur]
        path.append(cur)
    errs = {}
    d = data / "incidents" / iid
    for line in (d / "logs.jsonl").read_text().splitlines():
        j = json.loads(line)
        if j["level"] == "error":
            errs.setdefault(j["service"], []).append((j["ts_us"] - off.get(j["service"], 0), j["id"]))
    if not any(errs.values()):
        for line in (d / "spans.jsonl").read_text().splitlines():
            j = json.loads(line)
            if isinstance(j["status"], int) and (j["status"] >= 500 or j["status"] == 0):
                errs.setdefault(j["service"], []).append((j["start_us"] - off.get(j["service"], 0), j["id"]))
    steps = [dict(claim=top["summary"], status="confirmed", cites=[e["id"] for e in top["evidence"][:2]])]
    for child, s in zip(path, path[1:]):
        after = sorted(e for e in errs.get(s, []) if e[0] >= top["onset_us"])[:2]
        steps.append(dict(claim=f"{s} reported errors while its dependency {child} was failing", status="confirmed", cites=[i for _, i in after]))
    return dict(root_cause=dict(category=top["category"], service=top["service"], summary=top["summary"]), confidence=rep["confidence"], causal_chain=steps)
