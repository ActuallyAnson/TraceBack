# Metric evidence (rule M): resource and network faults without a model. Pre-registered plan

Branch `ablation` (stacked on `model-check2`). Written and frozen before the test archives were opened. Cost: $0 (no model call anywhere).

## Why
The larger transfer test (`eval/transfer2/`, RCAEval RE2 Sock Shop, 90 resource and network faults) showed that neither the raw-logs baseline (2 of 90) nor hybrid 2 (5 of 90) works when the evidence is in resource metrics, which the dataset ships (`simple_metrics.csv`: per-service cpu, mem, socket, diskio, latency, workload, error at 1 s resolution) and nothing here read.

## Rule M (`metric_rule.py`, frozen by `METRICS_HASHES`)
Per 120 s window (injection -40 s .. +80 s, the same windows as every earlier test; the first quarter is the quiet baseline): for every numeric column, z = |mean(rest) - mean(baseline)| / max(std(baseline), 5% of |mean(baseline)|, 5% of the column's range, 1e-9). A service scores the maximum z over its columns (`<service>_<kind>`); the answer is the top-scoring service if its score reaches TAU = 10, otherwise no answer. No model, no logs, no traces, no service names, no knowledge of the injection time beyond the window cut. The range term bounds z at 20.

## Development (RE2-SS only; all variants recorded)
- v0: z with a std/5%-of-mean floor only: 57 of 90 localized, but scores up to 1e13 from near-constant counters and clean windows indistinguishable.
- v1 (frozen): add the 5%-of-range floor: **69 of 90 localized at argmax; at TAU = 10: 66 of 90 localized, 83 of 90 answered, 4 of 30 clean windows alarmed.** (A range floor of 10% scored 61; rejected.) TAU = 10 was chosen from the clean-window and fault score distributions on RE2-SS (clean top scores range 2 to 16; fault top scores median 12).
- RE2-SS is therefore **development data** for rule M. Honest limit: its clean windows (700 to 580 s before the injection) are not guaranteed quiet; they overlap the fault score range, so the false-alarm rate is a property of this dataset's pre-injection period as much as of the rule.

## Test (never opened before the freeze)
RCAEval **RE2-OB** (Online Boutique, `RE2-OB.zip`, md5 `b9e23f8842c404b396ffd2becff15de4`) and **RE2-TT** (Train Ticket, `RE2-TT.zip`, md5 `a7fbcd1ada406067dcc50771ae398408`), all cases, none dropped, plus one clean window per case directory. Labels: the service named by the case directory (written by the dataset authors). RE2-OB is a system the log-based methods were developed on, but the metrics were never looked at; RE2-TT is an unseen system.
Metrics reported: faults localized (strict), answered, clean-window alarms (an answer on a clean window), per fault type, every wrong answer. No change after the results; no significance claims; repeats of one fault are not independent.
Also reported for context (no new run): the earlier model-based results on RE2-SS (baseline 2 of 90, hybrid 2 5 of 90).

## Amendment 1 (found while scoring the first test archive, before any test result was seen)
The scorer crashed on RE2-OB (`ValueError`): some rows of its `simple_metrics.csv` have an empty timestamp. `metric_rule.py` now skips rows whose time is not a number. This is a bug fix, not a change of the rule: the development scores on RE2-SS are byte-identical before and after (`scores_ss.json`). `METRICS_HASHES` re-frozen for `metric_rule.py` and this plan; nothing about TAU or the formula changed.

## Amendment 2 (also before any test result was seen)
The RE2-OB archive has an extra `multi-source-data` directory next to the repeat directories 1, 2 and 3; the scorer now only reads directories whose name is a number. RE2-SS scores are byte-identical. `METRICS_HASHES` re-frozen again; the rule is untouched.
