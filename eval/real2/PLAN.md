# Real telemetry, second iteration: pre-registered plan

Written before the new batch was collected. Branch `real-telemetry-2` (stacked on `real-telemetry`).

## 1. The first batch stays the frozen "before"
`eval/real/` (10 runs, converter v0 and v1) is not re-scored and not used to tune. Its results (analyzers and Full Traceback v2 localize 1 of 7 faults with converter v1; 2 of 7 with v0; the raw-logs baseline 5 of 7) stay in the README.
Honest limit: the fixes below were chosen *because* of what that batch showed (a shared flag service ranked as the cause; failed checkouts returned as HTTP 422). The new batch is what tests whether they help.

## 2. New batch (`scenarios.json`), collected and locked first
13 runs, same protocol as before (120 s: 40 s clean, 80 s with the flag on; same demo commit `85c2cb2`, same load, `LOCUST_USERS=25`): repeats of earlier flags plus new variants and new flags
(`paymentFailure` 50%, `intlShippingSlowdown` 5 s, `adHighCpu`, `recommendationCacheFailure`), and 3 clean runs. Labels come from the flag, written before any system runs. Data + labels are committed and the commit hash is recorded in `LOCK`.
Converter v2 = converter v1 (trace per entry request) + each span's own error flag carried as `err: true`. Nothing else changes.

## 3. General fixes (analyzer v3, new files only; v1 and v2 stay untouched)
Written from the failure modes, not from scores on the new batch, and not keyed on any service name, flag or incident id:
- **S1, shared infrastructure.** A service with 4 or more distinct caller services is shared infrastructure (a feature-flag or config service, a database proxy). A *dependency_slow* or *internal_errors* hypothesis whose service is shared infrastructure is demoted (x0.3) unless that service originates at least half of the failed entry requests (the origin-share measure from v2). The simulators' services have at most 3 callers, so v3 must equal v2 there (checked on the dev-v2 sets, not the fresh ones).
- **S2, failed entry requests, broadly.** Before analysis, a span whose own error flag is set and whose HTTP status is 4xx is treated as failed (status 500); a 4xx server span whose direct child is such a failed span is failed too. A request the application answered with 4xx while the work beneath it failed counts as a failure. Without error flags (all simulator data) nothing changes.
- **S3, span error flags where present.** The same flag marks candidate evidence: hypotheses and ranking are otherwise v2's.
Full Traceback v3 = analyzer v3 + the v2 prompts + the v2 override rule + (no verifier on real data).

## 4. Runs and reporting
Once, on the new batch only: raw-logs baseline, analyzer v1, analyzer v2, analyzer v3, Full Traceback v2, Full Traceback v3. Same model and settings. Localization scoring as in `eval/real/PLAN.md`; clean runs report false alarms (medium/high confidence).
Reported: before (first batch) and after (new batch) side by side, every wrong answer, API cost, time. No significance claims. No change to any fix after the new results; a further change needs a third batch.
Frozen-hash checks: `check_frozen.sh` (v1 sources and prompts) must pass unchanged; `check_v2.sh` is re-frozen only for `src/llm/reasoner.{cpp,hpp}`, which gain a `v3` branch; the v2 path is shown unchanged because `make v2-check` still reproduces the v2 README block offline from the cache.
