# Accuracy improvements (v2): pre-registered plan

Committed before any fresh incident was generated and before any system was run on one.
Time-box: 6 hours of work from 2026-10-19 20:25 (+08), then stop and report whatever state exists.

## 1. v1 is frozen
Git tag `v1-frozen`. Every v1 number, the locked splits (`eval/split.json`, `eval/topology2/manifest.json`), the scoring protocol,
the v1 analyzer and the v1 prompts are listed by hash in `eval/v2/V1_HASHES` and `eval/topology2/FROZEN_HASHES`; `sh eval/v2/check_v1.sh` fails if any changed.
v2 lives in new files (`src/analyzer_v2/`, `prompts_v2/`, a `--rules v2` flag) so v1 can be re-run exactly. Locked test sets are never re-scored with v2.

## 2. Failure modes found in v1, and the proposed fix for each
All come from v1 results we already published (`eval/RESULTS.md`, `eval/topology2/RESULTS.md`). Fixes are general rules over causal order and
evidence; none mentions a scenario name, incident id or seed.

| # | What went wrong in v1 (where it was seen) | Fix (v2) |
|---|---|---|
| F1 | A harmless deploy was read as the regression: a deploy hypothesis scored 0.92 on timing alone while explaining only 4-24% of the failed requests (analyzer, topology-2 decoys: a deploy on the fire-and-forget service and a deploy on the entry service). | **Explanatory gate.** Attribute every failed request to the service/edge where its failure originates (the failing leaf of its call tree). A hypothesis that originates under 10% of failed requests while another strong hypothesis originates at least 50% is demoted (x0.3). Applies to every category, not only deploys. |
| F2 | Trigger vs amplifier (retry storm): models named the overloaded callee (the victim) instead of the retrying caller (LLM stage and raw-logs baseline, topology 2). The v1 analyzer ranked it correctly in both topologies. | No analyzer rule change (the analyzer was right). Covered by the reasoner rule in F3, and measured on new storm variants in the fresh set. |
| F3 | The LLM stage overruled a correct analyzer answer (once in topology 1, twice in topology 2). The v1 prompt asked it to overturn only on contradicting records, but nothing enforced that. | **Reasoner rule.** The model may differ from the analyzer's top candidate only by citing record ids that contradict it (existing ids, not part of that candidate's own supporting evidence). Otherwise it must confirm. The check is mechanical: an unjustified override gets one corrective retry and is then replaced by the analyzer's top candidate, flagged `override_rejected`. |
| F4 | Pool exhaustion blamed when the pool filled because the service became slow (raw-logs baseline, topology 2). v1's analyzer only compares magnitudes. | **Causal ordering.** If a slowdown hypothesis on the same service begins at least 1.5 s before the pool-exhaustion burst, the pool hypothesis is demoted (x0.35): the pool filled because requests got slow. |
| F5 | One unseen-type incident (slow subset of requests) was localized to the caller (analyzer, topology 1). Not in the list of targeted fixes; F1's gate may or may not help. Reported, not specifically targeted. | none targeted |

The reasoner prompt for v2 (`prompts_v2/`) is written from these rules and the field descriptions, not from v1 test incidents.

## 3. How v2 is developed without touching held-out data
v2 is developed on a separate **dev-v2** set: same generators, a different seed, and without the new retry-storm variant family.
It is not reported as evidence. The fresh set below is generated from another seed, committed and locked first, and never looked at while the fixes are written.

## 4. Fresh set (committed and locked before any system runs on it)
Two topologies, 24 incidents each, new seeds, own ids (`f1-xx`, `f2-xx`), ground-truth label files committed with the manifests:
- 8 plain incidents (one per catalog fault type) and 3 skewed-clock incidents;
- 3 new retry-storm variants (different callee, weaker and stronger trigger burst) for the trigger-vs-amplifier case;
- 8 decoy incidents (at least 6 required): harmless deploys (including on the entry service, and on the fire-and-forget service in topology 2), mild latency, cosmetic errors;
- 2 incidents of fault types outside the catalog.
Same validity rule as before (symptom visible; removing the true cause removes it). The git commit that contains the manifests and labels is recorded in `eval/v2/FRESH_LOCK`.

## 5. Systems, run once each on the fresh set
Raw-logs baseline; analyzer v1; analyzer v2; full Traceback v1 (v1 analyzer + v1 prompt + verifier); full Traceback v2 (v2 analyzer + v2 prompt + override rule + verifier).
Same model (`nvidia/nemotron-3-super-120b-a12b`), temperature 0, same scoring (`eval/SCORING.md`), same verifier and confidence policy.

## 6. Reporting
Side by side: top-1, confidently wrong, citation validity, cost per incident, decoy-case accuracy separately; every wrong answer listed.
If v2 does not improve, that is reported as is. Nothing is changed after seeing fresh-set results; a second iteration would need another fresh set.
v2's result is evidence about these two simulated topologies only.
