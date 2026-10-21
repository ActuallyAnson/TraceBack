# Log evidence (analyzer v6) and a sixth real batch: pre-registered plan

Branch `log-evidence` (stacked on `candidate-gen`). Written and frozen before batch 6 exists.

## Why
The explanation offered for the analyzers trailing the raw-logs baseline on real telemetry: the baseline reads what the logs say (a service logging "Payment request failed"), the analyzer never reads logs for attribution. This iteration tests that explanation with one log-evidence rule. If it is wrong, the result will show it.

## Development data (disclosed)
Batches 1 to 5 are development data for v6 (analyzer only, no model calls). The simulator dev-v2 sets guard against regressions; `dev_check.py` also prints a label-free v5-vs-v6 diff on every simulator incident. No rule was chosen from the simulator fresh sets, the v1 test split, topology 2 or batch 6.

## Analyzer v6 (`src/analyzer_v6/`, new files; v1 to v5 untouched)
v5, then, unless v5 promoted a failure origin (that measured result stands):
- **R-rule:** a `retry_amplification` hypothesis whose median gap between attempts is negative (attempts overlap in time: concurrent identical calls, not retries) has its strength multiplied by 0.3. This is the false alarm that fired on every clean real run.
- **L-rule:** per service, excess warning/error/fatal logs = those after the quiet first quarter of the window minus the baseline count tripled (services that are not in the incident's service list are ignored). With at least 3 excess logs a service is a candidate; a service that calls (transitively) another candidate is a symptom and dropped. The candidate with the most excess logs is ranked first if the current top candidate is symptom-level (same definition as v5) and not that service, by promoting its existing hypothesis or adding an `internal_errors` hypothesis with its log lines as evidence. No message text is read; nothing is keyed on service names.
Development history: v6 with the L-rule only: real dev batch 2 6/10, batch 3 4/10, batch 5 4/10, clean alarms unchanged; adding the R-rule removed the clean-run alarms (batch 4 3/3 to 0/3, batch 5 3/3 to 1/3) but cost one fault in batch 5 (3/10 instead of 4/10). The composition with the R-rule is frozen. Simulator dev-v2: 40/42, same as v5. v6 differs from v5 in 6 of 162 simulator incidents (inc-049, inc-050, fan-11, fan-12, fan-13, f2-04); the effect on them is measured once, at the end, and reported whatever it is.
`tb_reason --rules v6` runs Full Traceback with analyzer v6, the v2 prompts and the override rule; `V2_HASHES` is re-frozen for `src/llm/reasoner.{cpp,hpp}` only (fourth time; v2-check and the unit tests show the v2 path unchanged).

## Batch 6 (`scenarios.json`)
13 runs, same protocol, demo commit, load, window and converter v3 as batches 4 and 5: 10 faults (repeats, `paymentFailure` 50%, `cartFailure` 25%, `emailMemoryLeak` 1000x) and 3 clean runs. Labels from the flags, locked by commit before any system runs. No run is dropped.

## Systems (once)
Raw-logs baseline; analyzer v5 and Full Traceback v5 (previous); analyzer v6 and Full Traceback v6. Strict localization, clean-run alarms, stated and capped confidence as before. Then the simulator fresh sets, the v1 test split and topology 2 with analyzer v6 vs v5, and Full Traceback v6 on the fresh sets. No tuning after results. No significance claims.
