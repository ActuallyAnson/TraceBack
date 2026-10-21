# Explanation quality: pre-registered scoring plan

Committed before any explanation was scored. Time-box: 2 hours from 2026-10-19 23:08 (+08), then stop and report what exists.
Nothing from v1 or v2 (results, locked sets, prompts, analyzers, video script) is modified. This track only *reads* the fresh v2 sets
(48 incidents, locked at `67e9729`) and the committed model answers in `eval/llm_cache`. No new reasoner or baseline calls are made on them.

## Question
Does the model's written causal chain match the true causal chain, and does it add anything over a mechanical write-up?

## Systems (explanations scored)
| Key | Explanation |
|---|---|
| `bl` | Raw-logs baseline's `causal_chain` (v1 prompts, as run in v2 evaluation) |
| `tb1` | Full Traceback v1's `causal_chain` |
| `tb2` | Full Traceback v2's `causal_chain` (after the override rule; the forced analyzer answer is scored as the model wrote it) |
| `tpl` | **Control.** Template explanation from analyzer v2 alone, no LLM (`eval/explain/template.py`; fixed rules, below) |

## Ground truth for the causal chain (from the label files and the fixed call graph only)
Call graph (from the simulators' definitions): topology 1: `inventory→orders→gateway`, `payments→orders→gateway`;
topology 2: `billing|shipping|notifier→broker→ingest→api`. Let *root* = `root_cause.service`.
- **Chain services and depth.** depth 0: the root service (for an edge fault: both endpoints; for a retry storm: the `trigger_service`, the overloaded callee, is depth 0 and the retrying root is depth 1);
  each caller further up toward the entry service is one deeper. The entry service is the symptom.
- **Chain window.** a record belongs to the chain if its service is a chain service and its timestamp is in
  `[onset − 10 s − |skew|, end + 10 s + |skew|]`, where onset/end are the label's and |skew| is that service's injected clock offset (0 if none).
  Records of a decoy's service that are not chain services are *decoy records*.

## Metrics
**(a) Are the cited events the right ones.**
- a1 *chain precision*: cited, existing record ids that belong to the chain / all cited existing ids (per explanation; pooled as a micro-average).
- a2 *decoy citations*: count of cited ids that are decoy records.
- a3 *chain coverage*: chain services with at least one cited chain record / all chain services.

**(b) Is the order right.** Each step (item of `causal_chain`) gets a depth = the minimum depth of its cited chain records' services (steps with none are unplaced).
Order is *correct* if no placed step comes after a deeper placed step (non-decreasing depth along the list, ties allowed); the prompt asks for earliest cause first.
Reported over explanations with at least two placed steps; plus b2: the first placed step is at depth 0 (the origin).

**(c) Root cause and mechanism.**
- c1: top answer is correct under the existing scorer (`score.judge`), reported for context only.
- c2 *true root cause and mechanism stated*: c1 holds AND the root-cause summary or some claim (a) names the root service, (b) for a retry storm also names the trigger service,
  and (c) contains a mechanism term for the true fault type (case-insensitive substring):
  retry_storm `retr`; connection_pool_exhaustion `pool`; bad_deploy `deploy`; config_change `config`; crash_restart/crash_loop `crash|restart|refus`;
  capacity_reduction `worker|capacity|cpu|saturat`; slow_dependency `slow|latenc`; network_partition `partition|unreachable|dropped|lost|unresponsive|cannot reach`;
  masked_downstream_error `mask|swallow|hidden|downstream`.
  The table is fixed here and not changed after scoring; its leniency (a keyword can match a wrong mechanism) is a stated limit.

**(d) Claims not supported by their citations.** A *judge model* (same Nebius model, temperature 0, its own prompt `eval/explain/judge_prompt.txt`, different from the reasoner's)
sees one claim plus the raw text of only the records that claim cites, never the ground truth, never the other claims, and answers `supported | partial | unsupported`.
It is a check of claim-vs-citation support, not of truth. Reported: unsupported claims / all claims, and explanations with at least one unsupported claim (`partial` counted separately, not as unsupported).
*Validation:* before scoring the fresh set, the judge is run on the explanations for the separate dev-v2 incidents; 30 claims (15 judged supported, 15 judged unsupported/partial where available,
chosen by sorting on a hash of claim text) are labeled by hand *blind to the judge's verdict*, and agreement is reported in `eval/explain/JUDGE_VALIDATION.md`.
The hand-labeler is the assistant that wrote this plan, not an independent human, which is stated there. The judge prompt is frozen (hash recorded) before the fresh run.

## The template control
Input: analyzer v2's report plus the incident's record files. Fixed rules (no incident-specific logic, no label access):
1. Root step: the top hypothesis' `summary`, citing its first 2 `evidence` ids.
2. For every service on the call path from the top hypothesis' service up to the entry service (path taken from the report's `graph`), one step "`<svc>` reported errors while its dependency `<child>` was failing",
   citing that service's first 2 error-level log/span records at or after the hypothesis onset (clock-aligned as the report's `clock`). Steps ordered root first.
3. Confidence = the analyzer's.
It is written to be generic; it is not tuned to the metrics (it is developed on dev-v2 only).

## Reporting
Per-system table of a1, a3, b, c2, d, pooled and per topology, plus the same restricted to incidents where that system's answer was correct (so the comparison is not just about who got the root cause right).
Every incident where an LLM explanation is worse than the template, listed per dimension (lower a1, order incorrect while template correct, c2 fails while template passes, more unsupported claims).
No prompt, template or metric changes after the fresh scores are seen. If a change is needed, a new fresh set must be generated and locked first.
If the LLM adds no value beyond the template, the README says so.
