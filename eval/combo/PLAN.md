# Model proposals plus metric evidence (rule C4). Pre-registered plan

Branch `combo` (stacked on `ablation`). Written and frozen before the combination was applied to any test window. Cost: $0 (cached model answers only; no model call).

## Why
Hybrid 2 (model proposals + log measurements) is calibrated but blind to resource faults (RE2-SS: 5 of 90). Rule M (metric z-scores, no model) finds resource faults (RE2-SS 66 of 90, RE2-OB 62, RE2-TT 35) but has no way to see code-level faults that leave metrics flat, and no calibration. Do the two cover each other?

## Rule C4 (`combine.py`, frozen by `COMBO_HASHES`)
Inputs per window: hybrid 2's final answer (service, confidence) and rule M's per-service scores (`metric_rule.py` unchanged, TAU = 10).
1. If hybrid 2 answers with a service whose own rule-M score is at least TAU: that service.
2. Else if rule M answers (top score >= TAU): rule M's service.
3. Else hybrid 2's answer, if any. Else no answer.
Confidence: **high** if both sources name the same service; **medium** if a single source answers (hybrid 2's own low stays low); **low** if no answer. No model call is added; the rule is not tuned on any test window.

## Development (RE2-SS only, where hybrid 2's answers are cached and rule M was developed; all variants recorded)
H (hybrid 2): 5/90, clean alarms 19/30. M: 66/90, 4/30. C2 (hybrid 2 else M): 22/90. C3 (M else hybrid 2): 66/90, 20/30. **C4: 67/90, 20/30, wrong at high 1.** C4 is chosen as the principled variant (metrics corroborate or replace the model's choice) before looking at any RE3 window. The 20 clean alarms are hybrid 2's own false alarms where rule M is silent; no variant fixes that without tuning on the dev windows.

## Test (applied once)
RCAEval **RE3-SS** and **RE3-TT** (the 60 fault cases and 18 clean windows of `eval/transfer/`), cached model answers from the first transfer test (Super) and the Ultra size check. Rule M scores come from `simple_metrics.csv` of the same windows (never looked at before; rule M was never tuned on RE3). Honest limits: the RE3 windows' *model answers* were already opened for hybrid 2 and the ablation, so only rule M and the combination are new; the combination was designed on RE2-SS; the RE2-OB / RE2-TT archives have no raw logs, so the model has nothing to read there and cannot be combined; faults are injected on demo apps; repeats are not independent. Metrics: faults localized (strict), clean alarms (medium or high), wrong at high, wrong at medium or high, no answer; every wrong answer. Other variants are printed for context only. No change after the results; no significance claims.
