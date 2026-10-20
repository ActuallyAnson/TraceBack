# Hybrid (LLM-first candidates) on real telemetry: pre-registered plan, part 1 (the batch)

Branch `hybrid-llm-first` (stacked on `real-telemetry-2`). Written before any telemetry of this batch was collected.

## Batch 3
13 runs, same protocol, demo commit, load (`LOCUST_USERS=25`) and converter (`eval/real/convert.py --err-flag`, frozen by `eval/real2/V3_HASHES`) as batch 2: 120 s per run, 40 s clean, then the flag on for 80 s.
`scenarios.json`: 10 fault runs (repeats of earlier flags, new variants, and new flags `adManualGc`, `emailMemoryLeak`) and 3 clean runs. Ground truth comes from the flag and is written by `make_labels.py` before any system is run.
Flags chosen from `demo.flagd.json`; the Kafka flags are not used because the stack runs without Kafka services.
**No run is dropped after the fact**, including runs whose flag produces no visible failure in 80 s (`emailMemoryLeak` may be one); they stay in the denominator as misses or, on a clean-looking run, as correct "nothing found" answers only if the system says so.
Batches 1 and 2 are not used to design or tune anything in this iteration. Part 2 of this plan (the hybrid) is written after this batch is committed and before any system runs on it.
