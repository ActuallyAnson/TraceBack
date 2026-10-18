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
