#!/usr/bin/env python3
"""RCAEval RE3 case directory (traces.csv, logs.csv, inject_time.txt) -> Traceback incident directory (eval/rcaeval/PLAN.md). Never reads the directory name as a label.

    convert.py <case dir> <out dir> [--clean]    120 s window: injection-40 s .. injection+80 s (--clean: a pre-injection window 700 s .. 580 s before injection)

Rules (fixed before any system was run on the test data):
* spans: the telemetry exporter's own spans (operation `opentelemetry.proto.collector...`) are dropped. A span is a server span if it has no parent or its parent is in another service. A span whose parent is
  in the same service is a client span if it has a child in another service, or its operation names a service (`pkg.XService/Method`), or it failed; otherwise it is internal and dropped (children are re-parented
  to the nearest retained ancestor).
* peer of a client span: the service of its cross-service child, else the service named by its operation, if that is a service of the incident.
* status: gRPC status code 0 -> 200; 1..99 -> 500 (4 -> 504, 14 -> 503); other numbers as HTTP codes; empty -> 200, except a root span, which is 500 if any retained descendant is >= 500 (the entry service
  turns a failed call into an error page). Spans with a non-zero gRPC code carry `err: true`.
* logs: no severity is exported; level is `error` if the message has an ERROR token or any of error/exception/fail/unable/refused/timeout/panic/fatal, `warn` for WARN/warning, else `info`. No trace id.
* metrics: per service and 5 s window from the server spans (as eval/real/convert.py); no resource metrics; no ops events.
"""
import collections, csv, json, pathlib, re, sys

args = [a for a in sys.argv[1:] if not a.startswith("--")]
case, out = pathlib.Path(args[0]), pathlib.Path(args[1])
CLEAN = "--clean" in sys.argv
csv.field_size_limit(10 ** 9)
inj = int(case.joinpath("inject_time.txt").read_text().strip())
w0 = (inj - 700 if CLEAN else inj - 40) * 1_000_000
w1 = w0 + 120_000_000
ERR = re.compile(r"\berror\b|exception|fail|unable|refused|timeout|timed out|panic|fatal|\bERROR\b", re.I)
WARN = re.compile(r"\bwarn(ing)?\b", re.I)


def code_status(v):
    if v == "":
        return None
    c = float(v)
    if c == 0:
        return 200
    if c >= 100:
        return int(c)
    return {4: 504, 14: 503}.get(int(c), 500)


rows = []
with open(case / "traces.csv", newline="", encoding="utf-8", errors="replace") as f:
    for r in csv.DictReader(f):
        st = int(r["startTime"])
        if not w0 <= st < w1 or "opentelemetry.proto.collector" in r["operationName"]:
            continue
        rows.append(dict(trace=r["traceID"], sid=r["spanID"], parent=r["parentSpanID"], svc=r["serviceName"], op=r["operationName"], start=st, dur=int(float(r["duration"] or 0)), raw=r["statusCode"]))
by = {s["sid"]: s for s in rows}
services = sorted({s["svc"] for s in rows})
lower = {x.lower(): x for x in services}
children = collections.defaultdict(list)
for s in rows:
    if s["parent"] in by:
        children[s["parent"]].append(s)


def named(op):
    m = re.search(r"([A-Za-z0-9_]+?)(?:Service)?/[A-Za-z0-9_]+$", op)
    if not m:
        return None
    base = m.group(1).lower()
    return lower.get(base) or lower.get(base + "service")


for s in rows:
    s["status"] = code_status(s["raw"])
    p = by.get(s["parent"])
    s["kind"] = "server" if p is None or p["svc"] != s["svc"] else "client"
    kids_other = [k for k in children[s["sid"]] if k["svc"] != s["svc"]]
    s["peer"] = kids_other[0]["svc"] if kids_other else (named(s["op"]) if s["kind"] == "client" else None)
    if s["kind"] == "client" and not (kids_other or named(s["op"]) or (s["status"] or 200) >= 500):
        s["kind"] = "internal"
keep = {s["sid"] for s in rows if s["kind"] != "internal"}


def anc(s):
    p, n = s["parent"], 0
    while p and p not in keep and n < 64:
        p, n = by[p]["parent"] if p in by else "", n + 1
    return p if p in keep else ""


for s in rows:
    s["parent2"] = anc(s)
kept = [s for s in rows if s["sid"] in keep]
kids2 = collections.defaultdict(list)
for s in kept:
    if s["parent2"]:
        kids2[s["parent2"]].append(s)


def failing_below(s):
    st = [s]
    while st:
        x = st.pop()
        for k in kids2[x["sid"]]:
            if (k["status"] or 200) >= 500 or failing_below(k):
                return True
    return False


for s in kept:
    if not s["parent2"] and s["status"] is None:
        s["status"] = 500 if failing_below(s) else 200
    elif s["status"] is None:
        s["status"] = 200
root = {}
def rootof(s):
    n = 0
    while s["parent2"] and n < 64:
        s, n = by[s["parent2"]], n + 1
    return s["sid"]
d = out
d.mkdir(parents=True, exist_ok=True)
out_rows, cnt = [], collections.Counter()
reqs, lat, errs = collections.Counter(), collections.defaultdict(list), collections.Counter()
for s in sorted(kept, key=lambda s: (s["start"], s["sid"])):
    cnt[s["svc"]] += 1
    r = dict(id=f"{s['svc']}-S{cnt[s['svc']]:05x}", trace_id=rootof(s), span_id=s["sid"], service=s["svc"], kind=s["kind"], op=s["op"][:120], start_us=s["start"], dur_us=s["dur"], status=s["status"])
    if s["parent2"]:
        r["parent_span_id"] = s["parent2"]
    if s["kind"] == "client":
        if s["peer"] and s["peer"] != s["svc"]:
            r["peer"] = s["peer"]
        r["attempt"] = 1
    if s["raw"] not in ("", "0.0") and 0 < float(s["raw"]) < 100:
        r["err"] = True
    if s["status"] >= 500:
        r["error"] = f"status {s['status']}"
    out_rows.append(r)
    if s["kind"] == "server":
        b = (s["start"] - w0) // 5_000_000
        reqs[(s["svc"], b)] += 1; lat[(s["svc"], b)].append(s["dur"] / 1000); errs[(s["svc"], b)] += s["status"] >= 500
(d / "spans.jsonl").write_text("".join(json.dumps(r, separators=(",", ":")) + "\n" for r in out_rows))
mrows, mn = [], collections.Counter()
for (svc, b) in sorted(reqs, key=lambda k: (k[1], k[0])):
    v = sorted(lat[(svc, b)]); q = lambda p: v[min(len(v) - 1, int(p * len(v)))]
    mn[svc] += 1
    mrows.append(dict(id=f"{svc}-M{mn[svc]:04x}", ts_us=w0 + (b + 1) * 5_000_000, service=svc, window_s=5, req=reqs[(svc, b)], err5xx=errs[(svc, b)], p50_ms=round(q(0.5), 3), p99_ms=round(q(0.99), 3),
                      queue_max=0, busy_max=0, workers_limit=0, pool_in_use_max=0, boots=1))
(d / "metrics.jsonl").write_text("".join(json.dumps(r, separators=(",", ":")) + "\n" for r in mrows))
lrows, ln = [], collections.Counter()
with open(case / "logs.csv", newline="", encoding="utf-8", errors="replace") as f:
    for r in csv.DictReader(f):
        ts = int(r["timestamp"]) // 1000
        if not w0 <= ts < w1:
            continue
        svc, msg = r["container_name"], r["message"]
        ln[svc] += 1
        lvl = "error" if ERR.search(msg) else "warn" if WARN.search(msg) else "info"
        lrows.append(dict(id=f"{svc}-L{ln[svc]:05x}", ts_us=ts, service=svc, level=lvl, msg=msg[:300]))
lrows.sort(key=lambda x: x["ts_us"])
(d / "logs.jsonl").write_text("".join(json.dumps(r, separators=(",", ":")) + "\n" for r in lrows))
(d / "events.jsonl").write_text("")
(d / "meta.json").write_text(json.dumps(dict(id=case.parent.name + "-" + case.name + ("-clean" if CLEAN else ""), window_start_us=w0, window_end_us=w1, services=services)))
print(len(out_rows), "spans", len(lrows), "logs", len(services), "services")
