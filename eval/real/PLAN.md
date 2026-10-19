# Real telemetry check: pre-registered plan

Committed before any telemetry was collected for scoring and before any system was run on it. Time-box: 4 hours from 2026-10-20 00:10 (+08).
Branch `real-telemetry`. The analyzers (v1 and v2), the prompts (`prompts/`, `prompts_v2/`) and the reasoner stay frozen: `sh eval/topology2/check_frozen.sh` and `sh eval/v2/check_v2.sh` must pass.

## Source
OpenTelemetry Demo (https://github.com/open-telemetry/opentelemetry-demo, commit `85c2cb2`, image version 3.1.0), run locally with Docker Compose in its own core layout
(`compose.yaml`, observability stack off, no Kafka services), with its OpenTelemetry Collector additionally writing raw OTLP JSON for traces, logs and metrics to files.
Faults are the demo's own feature flags (`src/flagd/demo.flagd.json`), switched on through flagd. Load: the demo's Locust load generator, `LOCUST_USERS=25` for every run (same for clean runs).

## Runs (fixed order, `scenarios.json`)
10 runs of a 120 s window each: 40 s clean baseline, then the flag is switched on (or nothing happens in a clean run), 80 s of fault; flags back off and 15 s settle between runs.
7 fault runs (paymentFailure 100%, paymentUnreachable, productCatalogFailure, cartFailure 100%, adFailure, intlShippingSlowdown 10 s, productCatalogLockContention) and 3 clean runs.
Ground truth is `labels/<id>.json`, generated from `scenarios.json` and the flip timestamp the runner records; written before any system runs.

## Converter (`eval/real/convert.py`), rules fixed before the run
Maps OTLP JSON to our four files without looking at labels:
- spans: OTel SERVER → `server`, CLIENT → `client`; INTERNAL/PRODUCER/CONSUMER dropped. Services `load-generator` and `frontend-web` (the traffic source and the browser) are external clients and dropped; a span whose parent is not in the kept set becomes a root, so `frontend-proxy` is the entry. `peer` of a client span = the service of the server span whose parent is that client span; if none exists, the service whose DNS name or address appears in the span's address attributes. `status`: HTTP status code if present; gRPC `OK` → 200, `UNAVAILABLE` → 503, `DEADLINE_EXCEEDED` → 504, other gRPC codes → 500; ERROR span status with no code → 500; connection errors → -1 with `error: conn_refused`.
- logs: OTLP log records; severity → level (`info|warn|error|fatal`), body → `msg`, eventName → `event`, attributes → `f`, trace id kept.
- metrics: computed per service per 5 s window from server spans (`req`, `err5xx`, `p50_ms`, `p99_ms`). Fields the demo does not export in this form (`queue_max`, `busy_max`, `workers_limit`, `pool_in_use_max`) are 0 and `boots` is 1: the analyzer therefore cannot see capacity or pool signals in real data.
- events: none. Flag changes are not given to the analyzer as ops events.
- windows: cut to the runner's `[start, start+120 s]`.
What had to change in the converter after seeing the first data is recorded in `CONVERTER_NOTES.md` (before any scoring) and nothing changes after systems run.

## Scoring (no tuning)
Systems: raw-logs baseline, analyzer v2, Full Traceback v2 (existing binaries and prompts; same model and temperature 0). Analyzer v1 is added because it is free.
- Fault runs: the top answer is correct if its service is the label's root service or an endpoint of the label's edge (localization only; the demo's faults have no entry in the category map).
  The category of the answer is reported, not scored.
- Clean runs: no root cause exists. Reported: whether the system gave "high" or "medium" confidence (false alarm); and what the top answer was.
- 7 fault + 3 clean incidents: no significance claims. Every wrong answer is listed.
