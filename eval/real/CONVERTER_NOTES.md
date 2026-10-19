# What changed in the converter after seeing real data (written before any system ran)

1. Client-span peer was first matched by substring in address attributes; `valkey-cart` (the cart's store, not an instrumented service) matched `cart` and produced a bogus `cart → cart` edge on a warm-up window. Fixed to a whole-token match and never a self-call. Client spans to uninstrumented stores (postgres, valkey) therefore carry no `peer`.
Found on warm-up traffic captured before the scenario runs, not on a scored run.

2. **Found after the first scored run (converter v0), before the second.** The first run showed almost no failed entry requests even for faults that return HTTP 5xx at the entry (e.g. `paymentUnreachable`: 15 entry responses with status 500, 2 failed traces seen by the analyzer).
   Cause: the demo's load generator sends a whole user session as **one OTLP trace**, so many entry requests share one `trace_id`, while our analyzer treats a trace as one request and reads only its first entry span.
   Converter v1 re-keys every kept span to one trace per entry request (`trace_id` = the root span of its ancestor chain; log records follow through their span id). Nothing else changed.
   This was found by inspecting the data (status counts per entry span in the raw files), not by tuning to a system's answer, but it *was* done after seeing the v0 scores. Both runs are reported; the v0 result is kept and shown.
3. **Not changed (a limitation, reported):** the demo's frontend turns a failed checkout into HTTP **422**, not 5xx. The analyzer only counts entry status >= 500 as failure, so `paymentFailure` and `cartFailure` remain invisible at the entry by its definition. Mapping 4xx to failures would be a post-hoc rule chosen to fit the data and was not done.
