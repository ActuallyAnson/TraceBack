# Code review, 2026-10-20 (branch `linkage-fix`)

Method: sanitizer build (`-fsanitize=address,undefined`) of every target, all 10 ctest tests plus every analyzer on all 162 simulator incidents and four real runs; malformed-input probes; a read of `llm.cpp`, `load.cpp`, `reasoner.cpp`, `hybrid.cpp`, `tools/live_server.py`, the evaluation scripts and the README against the generated numbers; secret scan of the tree and history; reproduction from a clean clone with no API key.

## Bugs found
| # | Where | Finding | Status |
|---|---|---|---|
| 1 | `eval/real/evaluate.py` `correct()` | Implemented a more lenient rule than pre-registered (`eval/real/PLAN.md` line 27): it also accepted an answer whose *own* edge touched the true service. Effect: the first batch under the first converter reported analyzers at 2/7; the pre-registered rule gives **1/7** (r09). Batches 1 (converter v1), 2 and 3 are unchanged; on batch 4 it would have given the analyzers 2/10 instead of 0/10. | **Fixed**; the README block and prose are corrected and say so |
| 2 | `src/hybrid/hybrid.cpp` merge | When the analyzer lists the same (category, service) twice (different edges), the later, lower-ranked entry overwrote the top candidate's rank, so the hybrid believed the analyzer had no top candidate and fell to its last-resort branch. Happened in 13 of 13 runs of batch 4 (never on batch 3 or the simulator fresh sets: outputs identical before and after). The hybrid's 4/10 on batch 4 as first run comes from that branch, not from the pre-registered rule. | **Fixed**; both versions are reported (`--legacy-merge`) |
| 3 | `src/llm/llm.cpp` | A reply with `"content": null` (a reasoning model that spends its whole budget thinking) made `value("content","")` throw, aborting the incident with an unrelated type error; an empty reply could also have been cached as a success. | **Fixed**: null/empty content is an error, never cached |
| 4 | `src/llm/llm.cpp` | Cache files were written in place, so a concurrent reader could parse half a file, treat it as a miss and pay for the call again. | **Fixed**: write to a temp file, then rename |
| 5 | `src/analyzer/load.cpp` | One malformed line in any `.jsonl` aborts the analysis with `parse error at line 1, column 2` and no file name or line number; a missing `services` key throws. Real pipelines produce partial lines (the collector file did). | Not fixed: the file is in the frozen v1 set. Needs a re-freeze or a wrapper that validates input |
| 6 | `src/analyzer/correlate.cpp` | The lost-parent repair only re-attaches *server* spans, and only inside one trace; client spans orphaned by a dropped parent stay detached. This is why the converter bug mattered. | Not fixed (frozen); the converter now avoids creating such orphans |
| 7 | `eval/topology2/check_frozen.sh` | Writes to a fixed `/tmp/tb_frozen_now.txt`: two concurrent runs race. Harmless in practice. | Not fixed |
| 8 | `tools/live_server.py` | A timed-out live model call leaves a daemon thread running. Binds to 127.0.0.1 and whitelists incident ids, so no injection path. | Not fixed |

Clean results: no ASan/UBSan finding on any test or any of 162 simulator incidents; no secret in the tree or git history (`NEBIUS_API_KEY=...` appears only as a placeholder in README); no analyzer rule names a service; the analyzer returns a valid empty report for an empty incident.

## Weaknesses that matter for the submission (not bugs)
1. **Real telemetry is the open problem, and it is not solved.** After the linkage fix the failing service is reachable from the failed entry request (batch 4: the payment failures sit inside the failed entry traces), but all analyzers still answer `retry_amplification/frontend` on every run, including the clean ones. That rule fires on ordinary real traffic. This is candidate generation, not scoring (see README, batch 4).
2. **The replay verifier needs the simulator.** On real telemetry it is inconclusive by construction, so the central "checkable" claim holds only on simulated incidents. The README says so; the video should too.
3. **No CI.** A workflow was not added because I cannot run it here and an unverified red badge is worse than none; the offline `make *-check` targets are the CI (all pass from a clean clone).
4. **The README is long (~800 lines) and its opening claims predate v2.** A judge reads the first screen: it should state the v2 fresh-set result (Full Traceback v2 95.8% top-1, 0% confidently wrong, baseline 68.8% and 29.2%), and the real-telemetry result, before anything else. A status block was added; the rest still needs trimming.
5. **Repository weight:** 85 MB of history, 72 MB of it real-telemetry tarballs. Fine for GitHub, heavy for a judge cloning on a laptop.
6. The converter has no unit test of its own beyond `eval/real4/synthetic_check.py` (added today; it is in ctest as `real4_synthetic`).

## Suggested next steps, in order of value
1. Decide the video story around the honest picture (calibrated on simulators; real telemetry partly working; the verifier limited to simulation).
2. If time remains: a candidate generator that proposes a service when it originates failed entry requests and the callers' error spans (the origin measure now works on real traces: batch 4 `p10` has recommendation at 0.82), tested on a fresh fifth batch. About 3 hours.
3. Trim the README to a one-screen summary plus links.
