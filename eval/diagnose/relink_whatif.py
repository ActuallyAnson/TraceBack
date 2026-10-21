#!/usr/bin/env python3
"""What-if for the diagnosed linkage cause, on ONE incident, in memory only (no file written, no analyzer or converter changed, no system scored):
re-parent every span whose parent was dropped to its nearest retained ancestor (walking the raw OTLP parents), then recompute the entry-request failures and origin shares.
usage: relink_whatif.py <incident dir> <raw traces.jsonl>"""
import collections, json, pathlib, sys
d = pathlib.Path(sys.argv[1])
raw = {}
for l in open(sys.argv[2]):
    try: doc = json.loads(l)
    except Exception: continue
    for rs in doc.get("resourceSpans", []):
        for ss in rs["scopeSpans"]:
            for sp in ss["spans"]: raw[sp["spanId"]] = sp.get("parentSpanId", "")
sp = [json.loads(l) for l in open(d / "spans.jsonl") if l.strip()]
for s in sp: s.setdefault("parent_span_id", "")
ids = {s["span_id"] for s in sp}
relinked = 0
for s in sp:
    if s["parent_span_id"]: continue
    p = raw.get(s["span_id"], ""); n = 0
    while p and p not in ids and n < 64: p, n = raw.get(p, ""), n + 1
    if p in ids: s["parent_span_id"] = p; relinked += 1
byid = {s["span_id"]: s for s in sp}
def root_of(s):
    n = 0
    while s["parent_span_id"] in byid and n < 64: s, n = byid[s["parent_span_id"]], n + 1
    return s["span_id"]
for s in sp: s["trace_id"] = root_of(s)
json.dump(sp, open("/dev/null", "w"))
# reuse the replica on the in-memory spans
src = open(pathlib.Path(__file__).with_name("origin_trace.py")).read()
src = src.replace('sp = [json.loads(l) for l in open(d / "spans.jsonl") if l.strip()]', 'sp = SP').replace("d = pathlib.Path(sys.argv[1]); focus = sys.argv[2] if len(sys.argv) > 2 else None", "focus = FOCUS")
print("re-parented spans:", relinked)
exec(src, {"SP": sp, "FOCUS": "payment", "json": json, "collections": collections, "pathlib": pathlib, "sys": sys})
