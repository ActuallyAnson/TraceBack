# Candidate generation (analyzer v5) and a fifth real batch: pre-registered plan

Branch `candidate-gen` (stacked on `linkage-fix`). Written and frozen before batch 5 exists.

## Why
Batch 4 (`eval/real4/`) showed the linkage fix works (the failing service sits inside the failed entry requests) but every analyzer still answers a symptom (`retry_amplification/frontend`) and never proposes, or ranks first, the service where the failures originate. The remaining defect is candidate generation and ranking.

## Development data (disclosed)
Batches 1 to 4 have been examined repeatedly and are used here as **development data** for v5 (analyzer only, no model calls); they are not a test. The simulator dev-v2 sets (42) guard against regressions. Nothing from the simulator fresh sets, the v1 test split or batch 5 was used to choose a rule, though `dev_check.py` also reports v5 vs v4 on every simulator incident (a label-free diff).
Development history, all recorded: (a) add a service no candidate names when it owns a share of failing leaf spans, no gate: simulator unchanged (0 of 162 differ), real dev 3/10 on batches 2 to 4; (b) subtract the baseline-period noise and promote a *named* service as well: real dev 4 to 6 of 10 but **simulator dev 28/42** (41/42 for v4): rejected; (c) restrict (b) to symptom-level top candidates (`dependency_slow`, `internal_errors`, `edge_unreachable`): 36/42; (d) also treat a `retry_amplification` top whose attempts overlap in time (negative median gap, i.e. concurrent calls, not retries) as symptom-level: **simulator dev 40/42** (d2-15 newly wrong; d2-04 was already wrong), real dev: batch 1 5/7, batch 2 4/10, batch 3 2/10, batch 4 5/10; clean-run alarms unchanged from v4 (the false alarm comes from the v1 retry rule and is not addressed). (d) is frozen.

## Analyzer v5 (`src/analyzer_v5/`, new files; v1 to v4 untouched)
v4 plus the O-rule: failures have an origin (a failing span none of whose children fail; a failed call originates at its callee). The first quarter of the window is the quiet baseline; per service, excess failing leaves = leaves after the baseline minus the baseline count tripled. The service with the largest excess is the origin candidate if it has at least 5 excess leaves and at least 20% of all excess. If the analyzer's top candidate is symptom-level (as defined in (d)) and is not that service, the origin candidate is ranked first: an existing hypothesis for that service is promoted (annotated), otherwise a new `internal_errors` hypothesis with its failing leaf spans as evidence is added. Nothing is keyed on service names, flags or incident ids. v5 differs from v4 on 2 of 162 simulator incidents (both in dev-v2: d1-17 still correct, d2-15 newly wrong).
`tb_reason --rules v5` runs Full Traceback with analyzer v5, the v2 prompts and the override rule; `V2_HASHES` is re-frozen for `src/llm/reasoner.{cpp,hpp}` only (third time; `make v2-check` and the unit tests show the v2 path unchanged).

## Batch 5 (`scenarios.json`)
13 runs, same protocol, demo commit, load, window and converter v3 (`--err-flag`) as batch 4: 10 faults (repeats, plus new variants `paymentFailure` 90% and 10%, `cartFailure` 90%) and 3 clean runs. Labels from the flags, locked by commit before any system runs. No run is dropped afterwards.

## Systems (once, batch 5 only)
Raw-logs baseline; analyzer v4 (the previous best, for before/after); analyzer v5; Full Traceback v4; Full Traceback v5. Strict localization rule (`eval/real/PLAN.md`), clean-run alarms (medium or high), stated and capped confidence as in batch 3 and 4. No tuning after the results. Then: the simulator fresh sets with Full Traceback v5 and analyzer v5 next to v4 (label-aware, run once, regressions reported). No significance claims; all earlier numbers stay.
