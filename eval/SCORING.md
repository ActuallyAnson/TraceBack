# Scoring protocol (pre-registered)

Committed before the first analyzer run. Changing it after seeing results requires a
new commit that says so, and the test-set numbers must then be reported both ways.

## Vocabulary
The analyzer uses its own categories. The label-to-category map lives only in
`eval/score.py`; the analyzer never reads labels or this map.

| Analyzer category | Counts as correct for label type | Service that must match |
|---|---|---|
| dependency_slow | slow_dependency | the slow service |
| retry_amplification | retry_storm | the retrying caller (label service) |
| pool_exhaustion | connection_pool_exhaustion | the exhausted service |
| deploy_regression | bad_deploy | the deployed service |
| config_regression | config_change | the service whose config changed |
| process_crash | crash_restart | the crashed service |
| capacity_saturation | capacity_reduction | the throttled service |
| internal_errors | masked_downstream_error | the service the errors originate in |
| edge_unreachable, unexplained | (none) | n/a |

## Metrics (per system)
- **Root-cause accuracy (top-1)**: top hypothesis has a mapped category AND the right service.
- **Service localization (top-1)**: top hypothesis names the labeled service, any category.
- **Top-3 accuracy**: the correct (category, service) appears in the top 3.
- **Unseen-type incidents** (label types with no row above): only service localization is scored;
  category accuracy is reported as n/a. They are reported separately from catalog types.
  For faults that live on a call edge (network partition, DNS failure) the label names the edge, and a
  top-1 hypothesis naming either endpoint of that edge counts as localized. (Amendment, committed after the
  analyzer freeze and before any variant or test result existed.)
- **Confident**: confidence label `high`. **False-confidence rate** = incidents where the
  system said `high` and top-1 was wrong, divided by all incidents. Also reported: precision when `high`.
- **Evidence validity** (LLM systems), three numbers, all computed by the scorer:
  1. *Existence*: share of cited IDs that exist in the incident (primary; objective, same for both LLM systems).
  2. *Relevance*: share of cited records that belong to the labeled root-cause service or an endpoint of its
     edge. Uses labels, computed identically for both systems. A rough proxy: symptom records in other services
     are legitimately citable, so this is not expected to reach 100%.
  3. *Analyzer support* (full Traceback only): share of cited IDs inside the analyzer's `support_ids` for the
     hypothesis matching the answer, or in any hypothesis's support set if the answer matches none. n/a for the baseline.
  Citation retries: the answer after retries is scored; the first-attempt invalid rate is also reported.
- **Latency / cost** per incident from the run log (tokens x published price, wall clock). Model for both LLM
  systems: `nvidia/nemotron-3-super-120b-a12b`, temperature 0, same output format and category list. Prices come
  from the Token Factory `/v1/models` listing at run time ($0.30 / $0.90 per million input / output tokens when
  written). The baseline prompt is `prompts/baseline_system.txt` plus the rendered telemetry; it was written once
  before any LLM result existed and is not tuned on test.
- **Baseline input**: warn/error/fatal logs and lifecycle logs (service_started, config_changed), all service
  metrics and all ops events, logs ordered by raw timestamp and grouped by trace id, raw (unaligned) clocks. No spans
  and no analyzer output. If the estimated token count exceeds the budget, warn/error logs are subsampled evenly
  in time; the share of truncated incidents is reported.

## Process rules
- Tuning uses `dev` only. `eval/score.py --split test` needs `--confirm-test` and appends a line
  to `eval/test_runs.log` (git commit, time, system). The log is committed.
- Unseen variants are added to the test split only, after the analyzer rule commit is recorded in
  `eval/RULES_FREEZE`.
- Failures are reported, not filtered.
