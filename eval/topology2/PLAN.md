# Generalization check: a second simulated topology (pre-registered)

Committed before any topology-2 incident was generated or scored.

## Question
Does the pipeline built and tuned on one request-chain topology still work, with **no changes to the analyzer
rules or the prompts**, on a different architecture: an event-driven fan-out with a message broker?

## Topology 2 (6 services, different call pattern)
    client -> api -> ingest -> broker --+--> billing   (awaited, parallel; has a DB connection pool)
                                        +--> shipping  (awaited, parallel)
                                        +--> notifier  (fire-and-forget, result ignored)
`broker` waits for billing and shipping and fails the request if either fails after retries; notifier failures never
affect the user, so notifier errors are a built-in misleading symptom. Same observable schema as topology 1
(structured logs, spans, metrics, ops events), same noise model (lost lines/spans, missing trace ids, clock offsets).
The simulator is a separate implementation (`src/sim2/`); topology-1 code and its locked data are untouched.

## Incidents (14, all test-only; ids `fan-01`..`fan-14`)
slow dependency (billing), slow dependency (billing) + decoy deploy on notifier (errors that never reach users),
bad deploy (shipping), bad deploy (billing) + clock skew, crash/restart (billing), pool exhaustion (billing),
config change (broker -> shipping timeout), capacity reduction (broker), retry storm (broker amplifies a billing burst),
masked downstream error (shipping, subset of requests), network partition (broker -> shipping),
network partition (broker -> billing) + decoy deploy, silent regression (ingest), crash loop (shipping).
Every incident is kept only if the symptom is visible and removing the true cause makes it disappear (same check as topology 1).
Seeds, parameters and the list above come from `src/sim2/` and are locked in `eval/topology2/manifest.json` with content hashes
before any system is run on them.

## Rules
1. **No tuning.** `src/analyzer/*`, `include/tb/analyzer.hpp` and `prompts/*.txt` must be byte-identical to
   `eval/topology2/FROZEN_HASHES` when results are reported (checked by `eval/topology2/check_frozen.sh`).
2. The only supplied difference is **input data**: each topology-2 incident's `meta.json` carries a one-sentence architecture
   description, which the reasoner substitutes for the first paragraph of the system prompt (that paragraph describes
   topology 1's four services). Topology-1 incidents have no description, so their prompts and cached results are unchanged.
   The description is written once, here, before results:
   > a small event-driven backend with six services: api (entry point), ingest, broker (a message broker), billing, shipping and notifier. Api calls ingest; ingest publishes each order to the broker; the broker delivers it to billing and shipping in parallel and waits for both, and also notifies notifier without waiting.
3. Same model, same output format, same category list, same scoring (`eval/SCORING.md`), same confidence policy.
   Category mapping from topology 1 applies; fault types with no category (network partition, crash loop, silent regression)
   are scored on service localization only, exactly as before.
4. Systems: analyzer only, raw-logs baseline, analyzer + LLM, Full Traceback (+ counterfactual verifier on a topology-2 replay environment).
5. Every run is appended to `eval/topology2/runs.log`. **All incidents are reported, including failures.** n=14 is small: no significance claims.
6. If something breaks for an infrastructure reason (a crash, not a wrong answer), fix and disclose both numbers, as in `eval/test_results/NOTES.md`.
7. Merge to `main` only if a clean clone builds, passes the tests and regenerates the results table without an API key.
