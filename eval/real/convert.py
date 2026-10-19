#!/usr/bin/env python3
"""OTLP JSON (OpenTelemetry Demo, collector file exporter) -> Traceback incident directory. Rules: eval/real/PLAN.md.

    convert.py <raw dir with traces.jsonl logs.jsonl> <schedule.json> <out dir>      writes <out>/incidents/<id>/{logs,spans,metrics,events}.jsonl + meta.json

Never reads labels. The analyzer, prompts and reasoner are not touched."""
import collections, json, pathlib, re, sys

DROP = {"load-generator", "frontend-web"}  # traffic source and browser: external clients, not part of the system under investigation
GRPC = {0: 200, 4: 504, 14: 503}
LEVEL = {"TRACE": "info", "DEBUG": "info", "INFO": "info", "WARN": "warn", "ERROR": "error", "FATAL": "fatal"}


def val(v):
    for k in ("stringValue", "intValue", "doubleValue", "boolValue"):
        if k in v:
            return int(v[k]) if k == "intValue" else v[k]
    return json.dumps(v)


def attrs(lst):
    return {a["key"]: val(a["value"]) for a in lst or []}


def service_of(res):
    return attrs(res["attributes"]).get("service.name", "unknown")


def jlines(path):
    """JSON lines; a line cut off by a file still being written (or a rotated tail) is skipped."""
    for line in open(path):
        try:
            yield json.loads(line)
        except ValueError:
            continue


def load_spans(path, lo, hi):
    spans = []
    for doc in jlines(path):
        for rs in doc.get("resourceSpans", []):
            svc = service_of(rs["resource"])
            if svc in DROP:
                continue
            for ss in rs["scopeSpans"]:
                for sp in ss["spans"]:
                    st = int(sp["startTimeUnixNano"]) // 1000
                    if not lo <= st < hi or sp["kind"] not in (2, 3):
                        continue
                    spans.append(dict(svc=svc, kind="server" if sp["kind"] == 2 else "client", trace=sp["traceId"], sid=sp["spanId"], parent=sp.get("parentSpanId", ""),
                                      op=sp["name"], start=st, dur=(int(sp["endTimeUnixNano"]) - int(sp["startTimeUnixNano"])) // 1000,
                                      a=attrs(sp.get("attributes")), code=(sp.get("status") or {}).get("code", 0), msg=(sp.get("status") or {}).get("message", "")))
    return spans


def status_of(s):
    a = s["a"]
    for k in ("http.response.status_code", "http.status_code"):
        if k in a:
            return int(a[k])
    g = a.get("rpc.grpc.status_code", a.get("rpc.response.status_code"))
    if g is not None:
        if isinstance(g, str):
            g = {"OK": 0, "DEADLINE_EXCEEDED": 4, "UNAVAILABLE": 14}.get(g.upper(), 2)
        return GRPC.get(int(g), 500)
    return 500 if s["code"] == 2 else 200


def convert_window(spans_all, logs_path, iid, w0, w1, out):
    spans = [s for s in spans_all if w0 <= s["start"] < w1]
    ids = {s["sid"] for s in spans}
    services = sorted({s["svc"] for s in spans})
    child_svc = {}
    for s in spans:
        if s["kind"] == "server" and s["parent"]:
            child_svc.setdefault(s["parent"], s["svc"])
    d = out / "incidents" / iid
    d.mkdir(parents=True, exist_ok=True)
    rows, n = [], collections.Counter()
    lat, errs, reqs = collections.defaultdict(list), collections.Counter(), collections.Counter()
    for s in sorted(spans, key=lambda s: (s["start"], s["sid"])):
        n[s["svc"]] += 1
        st = status_of(s)
        r = dict(id=f"{s['svc']}-S{n[s['svc']]:05x}", trace_id=s["trace"], span_id=s["sid"], service=s["svc"], kind=s["kind"], op=s["op"], start_us=s["start"], dur_us=s["dur"], status=st)
        if s["parent"] and s["parent"] in ids:
            r["parent_span_id"] = s["parent"]
        if s["kind"] == "client":
            peer = child_svc.get(s["sid"])
            if peer is None:
                blob = " ".join(str(v) for k, v in s["a"].items() if k in ("server.address", "rpc.service", "url.full", "net.peer.name", "peer.service", "http.url", "upstream_cluster", "upstream_cluster.name")).lower()
                toks = set(re.split(r"[\s/:@,]+", blob))
                hits = [x for x in services if x in toks and x != s["svc"]]  # whole-token match only ("valkey-cart" is not "cart"); never a self-call
                peer = hits[0] if len(hits) == 1 else None
            if peer:
                r["peer"] = peer
            r["attempt"] = 1
            if st >= 500 and not s["a"].get("http.response.status_code") and "refused" in (s["msg"] + str(s["a"])).lower():
                r["status"], r["error"] = -1, "conn_refused"
        if r["status"] >= 500 or r["status"] <= 0:
            r["error"] = r.get("error") or (s["msg"][:80] or "error")
        rows.append(r)
        if s["kind"] == "server":
            b = (s["start"] - w0) // 5_000_000
            reqs[(s["svc"], b)] += 1
            lat[(s["svc"], b)].append(s["dur"] / 1000)
            errs[(s["svc"], b)] += st >= 500
    (d / "spans.jsonl").write_text("".join(json.dumps(r, separators=(",", ":")) + "\n" for r in rows))
    mrows, mn = [], collections.Counter()
    for (svc, b) in sorted(reqs, key=lambda k: (k[1], k[0])):
        v = sorted(lat[(svc, b)])
        q = lambda p: v[min(len(v) - 1, int(p * len(v)))]
        mn[svc] += 1
        mrows.append(dict(id=f"{svc}-M{mn[svc]:04x}", ts_us=w0 + (b + 1) * 5_000_000, service=svc, window_s=5, req=reqs[(svc, b)], err5xx=errs[(svc, b)],
                          p50_ms=round(q(0.5), 3), p99_ms=round(q(0.99), 3), queue_max=0, busy_max=0, workers_limit=0, pool_in_use_max=0, boots=1))
    (d / "metrics.jsonl").write_text("".join(json.dumps(r, separators=(",", ":")) + "\n" for r in mrows))
    lrows, ln = [], collections.Counter()
    for doc in jlines(logs_path):
        for rl in doc.get("resourceLogs", []):
            svc = service_of(rl["resource"])
            if svc in DROP:
                continue
            for sl in rl["scopeLogs"]:
                for lr in sl["logRecords"]:
                    ts = int(lr.get("timeUnixNano") or lr.get("observedTimeUnixNano") or 0) // 1000
                    if not w0 <= ts < w1:
                        continue
                    lrows.append((ts, svc, lr))
    out_logs = []
    for ts, svc, lr in sorted(lrows, key=lambda x: x[0]):
        ln[svc] += 1
        body = val(lr["body"]) if "body" in lr else ""
        r = dict(id=f"{svc}-L{ln[svc]:05x}", ts_us=ts, service=svc, level=LEVEL.get(lr.get("severityText", "INFO").upper(), "info"))
        if lr.get("eventName"):
            r["event"] = lr["eventName"]
        r["msg"] = str(body)[:300]
        if lr.get("traceId"):
            r["trace_id"] = lr["traceId"]
        f = attrs(lr.get("attributes"))
        if f:
            r["f"] = {k: v for k, v in f.items() if isinstance(v, (str, int, float, bool))}
        out_logs.append(r)
    (d / "logs.jsonl").write_text("".join(json.dumps(r, separators=(",", ":")) + "\n" for r in out_logs))
    (d / "events.jsonl").write_text("")
    (d / "meta.json").write_text(json.dumps(dict(id=iid, window_start_us=w0, window_end_us=w1, services=services)))
    return len(rows), len(out_logs)


def main():
    raw, sched, out = map(pathlib.Path, sys.argv[1:4])
    sc = json.loads(sched.read_text())
    lo, hi = min(s["start_us"] for s in sc), max(s["end_us"] for s in sc)
    spans = load_spans(raw / "traces.jsonl", lo, hi)
    for s in sc:
        print(s["id"], convert_window(spans, raw / "logs.jsonl", s["id"], s["start_us"], s["start_us"] + 120_000_000, out))


if __name__ == "__main__":
    main()
