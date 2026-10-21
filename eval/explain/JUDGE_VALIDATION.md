# Judge validation (dev-v2 claims, not the fresh set)

30 claims: 15 the judge called supported and 15 it called partial/unsupported (as available), shuffled; labeled by hand blind to the judge's verdict.
**Labeler: the assistant (Claude) that wrote the plan, not an independent human.** The user can re-label `validation_sheet.md` and compare with `validation_key.json`.

- exact agreement on 3 classes: 25/30
- agreement on supported vs. not supported: 26/30

| Q | hand | judge | judge's reason |
|---|---|---|---|
| 1 | supported | partial | The record shows the broker's workers_limit set to 1 at the given timestamp, confirming the limit value, but it does not |
| 2 | supported | supported | The record shows workers_limit=1 and a non‑zero queue_max=3, indicating the broker had limited worker capacity and reque |
| 3 | supported | supported | The record shows workers_limit=1 and queue_max=15, indicating requests queued due to insufficient workers. |
| 4 | supported | supported | The record shows workers_limit=1 and queue_max=8, indicating limited worker capacity caused requests to queue. |
| 5 | supported | partial | The record confirms an API call to ingest failed with the error 'dependency ingest returned 500', but it does not indica |
| 6 | partial | partial | The records confirm that the Api service logged an upstream_error with message 'delivery step failed' and a delivery_fai |
| 7 | supported | supported | The broker log entry shows a retry being scheduled with a backoff of exactly 100 ms, matching the claim about short back |
| 8 | partial | partial | The record confirms Ingest logged an upstream_error with message 'delivery step failed' and status 504, but it does not  |
| 9 | partial | partial | The record shows a broker retry to billing (attempt 2) but does not provide evidence of repeated retries, failures or la |
| 10 | partial | partial | The quoted record confirms the payments service's workers_limit is now 1, but it does not provide any information about  |
| 11 | supported | supported | Each quoted span shows the orders service attempting a client call to inventory and receiving error 'conn_refused', conf |
| 12 | supported | supported | The quoted records show an API upstream_timeout error with HTTP status 504 (a 5xx error) and aggregated 5xx error counts |
| 13 | supported | supported | The logs show the gateway recorded an upstream_error from orders with status 500 and, using the same trace ID, later log |
| 14 | supported | supported | Both records share the same trace_id, showing the request that caused an error in shipping also produced a 500 error at  |
| 15 | supported | supported | The broker log entry explicitly records an upstream_error caused by the shipping service returning status 500. |
| 16 | partial | partial | The record confirms the broker's current workers_limit is 1, but it does not show the nominal value of 24 or indicate a  |
| 17 | supported | supported | The record shows the ingest service logged an upstream error where the broker returned a 503 status. |
| 18 | partial | partial | The records show Orders' client calls to Payments timing out after approximately 1.2 seconds, but they do not provide ev |
| 19 | supported | supported | The quoted record shows the payments service emitted an error event 'temporarily_unavailable' with status 503, matching  |
| 20 | partial | supported | The gateway record shows a high count of 5xx errors (62 out of 91 requests) and elevated latency (p50≈238 ms, p99≈303 ms |
| 21 | supported | supported | The gateway log shows an upstream_error with status 503 and message 'order step failed', indicating that the order servi |
| 22 | supported | partial | The record confirms the billing service experienced an OOM kill (crash), but it does not explicitly state that this stop |
| 23 | partial | partial | The record confirms the inventory service returned HTTP 500 due to an internal error, but it does not specify that the e |
| 24 | supported | supported | The logs record billing's DB connection pool exhausted (pool_size=0) and, via matching trace_ids, link those events to c |
| 25 | unsupported | partial | The records show Orders making repeated calls to Payments that returned connection‑refused errors with incrementing atte |
| 26 | supported | supported | The quoted records show a broker configuration change that set timeout.shipping_ms from 1000 to 7. |
| 27 | partial | partial | The records show billing charge durations but provide no baseline for comparison or evidence of timeouts. |
| 28 | partial | partial | Records show billing charge durations around 2 seconds but provide no baseline for comparison or evidence of caller time |
| 29 | partial | partial | The records show billing charge durations but give no baseline for comparison or evidence that callers timed out. |
| 30 | partial | partial | The records confirm that the ingest service experienced an upstream_timeout error, but they do not show that this timeou |
judge_prompt.txt sha256: 331ba95f8d5b2549  (frozen before the fresh run)
