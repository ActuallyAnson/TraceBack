# Test-split runs: what each file is

All runs are logged in `eval/test_runs.log` (commit hash recorded; the `+dirty` suffix on the first runs
came from the results directory being created before the run, not from source changes).

- `analyzer.txt`, `traceback.txt`, `baseline.txt`: main comparison, run once on the locked test split.
- `baseline_plus.txt`, `baseline_ultra.txt`: ablations defined in `eval/SCORING.md` before any test run.
- `baseline_after_crashfix.txt`: **disclosure.** In the original baseline run, `tb_reason` crashed on inc-024
  when the model returned a JSON `null` where a string was expected (a tool bug: it should have been sent back
  as a retry). That incident was scored as unanswered, giving 88.5% top-1 in `baseline.txt`. After making the
  reply parser robust, the incident was re-run once; the model answered correctly (`process_crash/payments`),
  giving 92.3% in `baseline_after_crashfix.txt`. All other incidents were served from the response cache
  (same prompts, same answers). Both numbers are reported. The model is not fully deterministic at temperature 0,
  so a fresh end-to-end run can differ slightly from the cached answers used here.
- Traceback's prompt and the analyzer were not changed after this point.
