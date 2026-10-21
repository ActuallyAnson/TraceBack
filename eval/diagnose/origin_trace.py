#!/usr/bin/env python3
"""Read-only diagnostic (no analyzer rule is changed): replicate correlate() + analyzer v3's error-flag normalisation + v2's origin_share on a converted
incident and print the intermediate values.   usage: origin_trace.py <incident dir> [service]"""
import collections, json, pathlib, sys
d = pathlib.Path(sys.argv[1]); focus = sys.argv[2] if len(sys.argv) > 2 else None
sp = [json.loads(l) for l in open(d / "spans.jsonl") if l.strip()]
for s in sp: s.setdefault("parent_span_id", "")
# S2 (analyzer v3): 4xx + own error flag -> 500; a 4xx span that is the direct parent of such a span -> 500
promoted = set()
for s in sp:
    if s.get("err") and 400 <= s["status"] < 500: s["status"] = 500; promoted.add(s["span_id"])
par_prom = {s["parent_span_id"] for s in sp if s["span_id"] in promoted and s["parent_span_id"]}
for s in sp:
    if 400 <= s["status"] < 500 and s["span_id"] in par_prom: s["status"] = 500
by_span = {s["span_id"]: i for i, s in enumerate(sp)}
is_fail = lambda s: s["status"] >= 500 if s["kind"] == "server" else (s["status"] <= 0 or s["status"] >= 500)
traces = collections.defaultdict(list)
for i, s in enumerate(sp): traces[s["trace_id"]].append(i)
children = collections.defaultdict(list); root = {}
for t, idx in traces.items():
    idx.sort(key=lambda i: sp[i]["start_us"]); root[t] = -1
    for i in idx:
        s = sp[i]
        if not s["parent_span_id"]:
            if s["kind"] == "server" and root[t] < 0: root[t] = i
            continue
        j = by_span.get(s["parent_span_id"])
        if j is not None and sp[j]["trace_id"] == s["trace_id"]: children[j].append(i)
        # (virtual-parent attachment omitted: it only applies to server spans whose parent is lost)
n_traces = len(traces); with_root = sum(1 for t in traces if root[t] >= 0)
failed = [t for t in traces if root[t] >= 0 and is_fail(sp[root[t]])]
print(f"spans {len(sp)}; traces {n_traces}; traces with a root server span {with_root}; failed entry requests (root server span >=500): {len(failed)}")
print("failing spans by service/kind:", dict(collections.Counter((s["service"], s["kind"]) for s in sp if is_fail(s))))
count = collections.Counter()
for t in failed:
    origins = set(); st = [root[t]]
    while st:
        i = st.pop(); cf = False
        for c in children[i]:
            if is_fail(sp[c]): cf = True; st.append(c)
        if cf: continue
        s = sp[i]; origins.add(s["service"] if s["kind"] == "server" or not s.get("peer") else s["peer"])
    for o in origins: count[o] += 1
print("origin numerators:", dict(count), "denominator:", len(failed), "-> shares", {k: round(v / max(len(failed), 1), 3) for k, v in count.items()})
if focus:
    fs = [s for s in sp if s["service"] == focus and is_fail(s)]
    reach = 0; where = collections.Counter()
    for s in fs:
        t = s["trace_id"]; has_root = root[t] >= 0
        top = s; chain = []
        while top["parent_span_id"] and top["parent_span_id"] in by_span: top = sp[by_span[top["parent_span_id"]]]
        where[(top["service"], top["kind"], top["op"][:32], "root-server" if has_root else "NO root server span in trace")] += 1
        reach += (t in failed)
    print(f"{focus}: {len(fs)} failing spans; in a trace counted as a failed entry request: {reach}")
    for k, v in where.items(): print("  top ancestor reachable via parent links:", k, v)
