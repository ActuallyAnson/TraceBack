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
- **Confident**: confidence label `high`. **False-confidence rate** = incidents where the
  system said `high` and top-1 was wrong, divided by all incidents. Also reported: precision when `high`.
- **Evidence validity** (LLM systems): share of cited IDs that exist in the incident, and share that
  are also in the evidence set the analyzer attached to the cited hypothesis ("support").
- **Latency / cost** per incident from the run log (tokens x published price, wall clock).

## Process rules
- Tuning uses `dev` only. `eval/score.py --split test` needs `--confirm-test` and appends a line
  to `eval/test_runs.log` (git commit, time, system). The log is committed.
- Unseen variants are added to the test split only, after the analyzer rule commit is recorded in
  `eval/RULES_FREEZE`.
- Failures are reported, not filtered.
