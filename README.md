# Traceback

Incident investigator for a small multi-service backend. Given logs, events and
traces from a failed or slow request, it reconstructs the timeline, ranks likely
root causes with event-ID evidence, and separates confirmed facts from hypotheses.

Status: work in progress (Nebius x NVIDIA Global AI Hackathon entry).
Design is deterministic-first: the LLM only ranks, explains and verifies
hypotheses produced by a non-LLM analysis layer. Results are added once the
evaluation harness has produced them; no numbers are claimed before then.

License: MIT.

## Evaluation data: a simulator (what that does and doesn't prove)

Evaluation incidents come from a deterministic discrete-event simulator of four
services (gateway, orders, inventory, payments) in `src/sim/`. It emits only
structured JSON logs, spans, metrics and ops events. Ground truth goes to a
separate `runs/labels/` directory that the analyzer never reads, and the
simulator shares no code with the analyzer.

Seeded fault types: slow dependency, retry storm, connection-pool exhaustion,
bad deploy, config change, crash/restart, capacity reduction, and a masked
downstream error (symptom in orders/gateway, cause in inventory). 48 incidents:
each type plain (x4), with injected clock skew (x1), and with a harmless decoy
fault present (x1). The data includes background noise, dropped log lines and
spans, missing trace IDs and absent event codes.

Each incident is only kept if the symptom is visible and removing the true cause
makes it disappear (a counterfactual rerun), so labels are causally correct.

16 incidents are frozen as the held-out test set (2 per fault type) in
`eval/split.json` with content hashes; `make verify` fails if regenerated data
drifts. More unseen fault variants will be added to the test set after the
analyzer rules are written.

What this proves: the analysis pipeline handles the failure patterns, noise and
skew modelled here, measured against exact ground truth. What it does not prove:
that it works on real production telemetry, whose failure modes, log formats and
noise are broader than this model.

## Build

    make build     # cmake build
    make test      # unit tests + frozen-split check
    make data      # regenerate runs/incidents and runs/labels
