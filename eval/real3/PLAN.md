# Hybrid (LLM-first candidates) on real telemetry: pre-registered plan, part 1 (the batch)

Branch `hybrid-llm-first` (stacked on `real-telemetry-2`). Written before any telemetry of this batch was collected.

## Batch 3
13 runs, same protocol, demo commit, load (`LOCUST_USERS=25`) and converter (`eval/real/convert.py --err-flag`, frozen by `eval/real2/V3_HASHES`) as batch 2: 120 s per run, 40 s clean, then the flag on for 80 s.
`scenarios.json`: 10 fault runs (repeats of earlier flags, new variants, and new flags `adManualGc`, `emailMemoryLeak`) and 3 clean runs. Ground truth comes from the flag and is written by `make_labels.py` before any system is run.
Flags chosen from `demo.flagd.json`; the Kafka flags are not used because the stack runs without Kafka services.
**No run is dropped after the fact**, including runs whose flag produces no visible failure in 80 s (`emailMemoryLeak` may be one); they stay in the denominator as misses or, on a clean-looking run, as correct "nothing found" answers only if the system says so.
Batches 1 and 2 are not used to design or tune anything in this iteration. Part 2 of this plan (the hybrid) is written after this batch is committed and before any system runs on it.

---
# Part 2: the hybrid (written and frozen before any telemetry of batch 3 exists)

## Design: the model proposes, the deterministic layer decides, the model cannot raise confidence
1. **Proposals.** The model reads the same evidence as the raw-logs baseline. Instead of a new prompt, the proposals are the baseline's own answer: its root cause plus its alternatives, first 3 distinct (category, service) pairs, each with the record ids it cited (root cause: the ids of its causal chain). Reason: this is literally "the same evidence", it costs no extra model call, and 507 of the 521 cached baseline answers already carry at least one alternative with a service and citations. Limit: the model was not asked to produce candidates as such.
2. **Validation.** A proposal is rejected (and listed) if its service is not in the incident, it cites no record, or any cited id does not exist. Invalid ids are never repaired.
3. **Merge.** Valid proposals are merged with the analyzer's top 6 candidates (analyzer v3, which equals v2 on all simulator data; same category and service = one candidate).
4. **Evidence, measured the same way for every candidate:** origin share (the share of failed entry requests whose failure originates in the candidate's service, v2's measure) and anomalous share (the share of its cited records, or for the analyzer its evidence records, that are anomalous records about it: warn/error/fatal logs, failing spans or spans with an error, metrics with 5xx, any ops event).
5. **Choice.** The analyzer's top candidate stands unless a proposal for another service beats it on measured evidence: origin share >= 0.5, anomalous share >= 0.5, and the analyzer's top candidate's origin share < 0.2. If several qualify, the largest origin share wins. When the winner's service also has an analyzer candidate, the analyzer's best category for that service is used. If the analyzer has no candidate at all, the proposal with the best evidence is used.
6. **Confidence.** Final = min(ceiling, source confidence). Ceiling: the analyzer's own confidence if the analyzer's top candidate is chosen, otherwise from measured evidence (high needs both measures >= 0.8, medium both >= 0.5, else low; medium at most for a non-top analyzer candidate). Source confidence: the model's confidence if the chosen candidate is the model's top proposal; capped at medium whenever the model's top valid proposal names a different service than the one chosen. Nothing in the model's output can raise the result above the ceiling.
7. **Replay verifier.** Simulator sets: `tb_verify` on the final answer, `final_conf` policy as everywhere (failed -> low, inconclusive caps high at medium). Real telemetry has no replay environment (the verifier replays the simulator), so there the verifier is "inconclusive" by construction: high becomes medium. Both numbers are reported on real data: the system's own confidence and the confidence after that cap, for Full Traceback v2 and the hybrid alike. Because that cap makes "confidently wrong" zero by construction, wrong answers at medium or high confidence are reported as well.

## Development record (simulator dev-v2 sets only; fresh sets, batches 1 and 2 and batch 3 were not looked at)
Three versions were examined on the dev-v2 sets (42 incidents): (a) a weighted evidence score ranking every candidate (0.45 origin + 0.25 anomalous + 0.30 analyzer score): 30/42 top-1; (b) the same with service-only origin and the analyzer's category: 25/42. Both lose to the analyzer because origin share points at the victim whenever the cause sits upstream of the failing call. (c) The frozen version above: 41/42 (analyzer v3 = v2: 41/42; Full Traceback v2: 41/42; baseline: 31/42), with 26 "high" answers, all correct, and the one wrong answer at medium.
The thresholds in step 5 are generic and were chosen from the failure mode seen in earlier iterations (the analyzer's top candidate originates few of the failed requests while another service originates most), not from scores on batches 1 or 2, which were not run through the hybrid. That earlier knowledge is a limit on how independent this design is.

## Runs
Once, after the freeze (`HYBRID_HASHES`): batch 3 with the raw-logs baseline, Full Traceback v2 and the hybrid (analyzer v2 shown for reference); then the hybrid on the simulator fresh sets (both topologies) next to the numbers already in the README. No changes after seeing results; a further change needs a new batch.
Model cost cap for this iteration: about $0.5 (the sim fresh runs reuse cached baseline answers).
