# Linkage fix and a fourth real batch: pre-registered plan

Branch `linkage-fix` (stacked on `hybrid-llm-first`). Written and frozen before the batch exists.

## Why
`eval/diagnose/FINDING.md`: (a) the converter dropped internal spans, so the subtree below them lost its parent link and the failing service sat in a detached trace (~35% of traces rootless in real runs vs 4% in the simulator); (b) analyzer v3 promotes a 4xx span only if it is the direct parent of a span it promoted itself, so the frontend's HTTP 422 spans hid failures from the origin walk.
Batches 1 to 3 were examined in the diagnosis and are not used as a test. Their numbers stay as they are.

## The fixes (general, not keyed on any service, flag or run)
- **Converter v3** (`eval/real4/convert.py`, a copy of `eval/real/convert.py`; the old one stays frozen): a span whose parent was dropped (internal span or unexported service) is re-parented to its nearest retained ancestor, before re-keying. Nothing else changes (`--err-flag` as before).
- **Analyzer v4** (`src/analyzer_v4/`, new files; v1 to v3 untouched): v3 plus S3: a 4xx *server* span that has a failing descendant is itself a failure (post-order, so a chain of such spans is promoted). No other change.
- `tb_reason --rules v4` runs Full Traceback with analyzer v4 and the v2 prompts and override rule. This adds a branch to `src/llm/reasoner.{cpp,hpp}`; `V2_HASHES` is re-frozen for those two files only, as in the v3 iteration, and `make v2-check` plus the unit tests show the v2 path is unchanged.

## Checks before any data
- All 162 simulator incidents (dev, fresh, v1, topology 2): analyzer v4 vs v3. Result: **identical ranking, categories, services and confidence on all 162; the report differs in 1 of 162 (`f1-14`: 6 spans with HTTP 429 promoted, top score 0.849 to 0.850).** Disclosed; no decision changes.
- Converter v3 on the raw batch-3 telemetry (structure only, nothing scored): m10 traces 8,644 to 5,659, rootless traces 2,998 to 13.

## Batch 4 (`scenarios.json`)
13 runs, same protocol, demo commit, load and window as before: 10 faults (repeats of flags that earlier batches missed or hit, plus `paymentFailure` 75% and `cartFailure` 75%) and 3 clean runs. Labels from the flags, locked by commit before any system runs. No run is dropped afterwards.

## Systems (once, batch 4 only)
Raw-logs baseline; analyzer v2 (reference); analyzer v3 (shows what the converter fix alone does); analyzer v4; Full Traceback v2; Full Traceback v4; the frozen hybrid (its analyzer layer is v3; no change to it). Localization scoring and clean-run alarms as before; stated and capped confidence as in `eval/real3/PLAN.md`. No tuning after the results; a further change needs a fifth batch. No significance claims; the README keeps all earlier numbers.
