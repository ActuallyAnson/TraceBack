# Lock history

Every change to `eval/split.json` after its first commit, with the reason.

1. **Relock 1 (before any analyzer existed).** Closed timestamp-grid clock-skew leaks in the
   simulator (boot logs at t=0, 5s metric ticks on a fixed grid, whole-second fault onsets).
   All 48 incidents regenerated. Commit "Close timestamp-grid clock-skew leaks...".
2. **Relock 2 (simulator ground-truth bug; no test result had been computed).** While examining a
   failing dev incident, I found the retry-storm trigger burst was applied to `orders` instead of
   `payments`, so incidents labeled `retry_storm` were not storms. Fixed the target service.
   Only the 6 retry-storm incidents changed (inc-020, -022, -042, -044 dev; inc-005, -026 test);
   the other 42 content hashes are unchanged (checked by diffing manifests). The two test incidents were
   regenerated because their labels were wrong, not because of any analyzer result: the test split
   has never been scored (`eval/test_runs.log` is empty at this point).
3. **Append (unseen variants, test split only).** Added inc-049..inc-058 after the analyzer freeze
   (`eval/RULES_FREEZE`) and before any LLM code existed. The 48 original lines are unchanged (checked
   by `tb_sim lock --append`, which refuses if any locked line differs). Types not in the original catalog:
   network_partition (x2), dns_failure (x2), crash_loop (x2), silent_regression (x2), slow_subset_requests,
   gradual_degradation. Several are look-alikes of catalog faults with different symptoms (crash_loop,
   silent_regression, slow_subset_requests, gradual_degradation). Not scored by anything yet; the
   analyzer was not run on them before locking.
