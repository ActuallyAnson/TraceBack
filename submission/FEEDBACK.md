# Feedback on Nebius Token Factory, AI Cloud and NVIDIA models (draft from real use)

Context: one developer, Oct 2026, C++ client with libcurl, roughly 230 chat-completion calls (about 17M prompt tokens) during development and
evaluation, including a few runs of the larger-context Ultra model. Model used for all reported results: `nvidia/nemotron-3-super-120b-a12b`.
Nebius AI Cloud compute (Serverless Jobs/Endpoints, DevPods) was **not** used; the project is a Token Factory API consumer, and this feedback is only about what I used.

## What worked well
- **OpenAI-compatible endpoint.** A plain POST to `/v1/chat/completions` with a Bearer key worked immediately; no SDK was needed in C++.
- **`GET /v1/models?verbose=true` returns prices and context lengths.** That was the only place I found the exact Nemotron model IDs with pricing and context size,
  and it made cost accounting trivial. It needs a key, though (see below).
- **Context windows and price made a strong baseline possible.** A 262k-token window at $0.30/$0.90 per million input/output tokens let me feed raw telemetry to the same model as a fair
  comparison, and keep iterating on prompts for cents per run.
- **Nemotron 3 Super followed a strict JSON output format** well: across the evaluation runs, cited record IDs almost always existed on the first attempt.
- **Latency** for the structured-analysis task (about 4k prompt tokens, about 2.7k completion tokens including reasoning) was around 14 s; for 100–200k-token prompts, around 20 s.
- Cost visibility: usage fields (prompt/completion tokens, reasoning tokens) are returned per call.

## Rough edges and suggestions
1. **Docs did not list the NVIDIA models.** The inference/model overview pages named only model *types*; the list-of-models example showed Llama IDs. I had to call the API with a key to learn the Nemotron IDs. A public page that lists current Nemotron model IDs, context lengths and prices (with the model-selection guidance the hackathon page gives: Ultra for reasoning, Super/Nano for fast calls) would save time.
2. **Old docs URLs redirect** (`docs.nebius.com/studio/...` → `docs.tokenfactory.nebius.com`); some search results still point to the old paths.
3. **Reasoning tokens are billed and share the `max_tokens` budget.** The reasoning arrives in a separate `reasoning`/`reasoning_content` field and counts toward `completion_tokens` (about 2.7k tokens for a short JSON answer). It is easy to truncate the visible answer with a small `max_tokens`. A docs note, and perhaps a separate `max_reasoning_tokens` or a way to disable reasoning for extraction tasks, would help.
4. **Not fully deterministic at temperature 0.** The same request occasionally returned a different answer on a later call, which matters for reproducible evaluation; I worked around it by caching responses. A `seed` parameter that is honored (if supported) should be documented.
5. **Very large prompts are slow and brittle.** With roughly 700k-token prompts on the 1M-context Ultra model, 3 of 5 answers were rejected (malformed or invalid citations) and a call took minutes; the cost per incident was about $0.76. Guidance on practical prompt sizes for long-context models would help.
6. **Prompt-cache fields exist but are undocumented in what I read** (`prompt_cache_hit_tokens`, `cached_tokens`); retries re-send the same large prefix, so clear guidance on whether and how cached tokens are discounted would help cost planning.
7. Hackathon page: the rules and resources were easy to find; a short "what model IDs are eligible" list on the Devpost page would remove the guesswork.

## NVIDIA Nemotron observations
- Strong at structured causal explanations from compact evidence. It sometimes **anchors on the first anomaly** it sees (an isolated early error) and then rejects the true cause as "starting later"; adding explicit background-noise guidance to the prompt fixed most of it.
- With raw logs and no explicit ID rule it **cited whole log lines instead of IDs** until the output instructions defined what an ID is.
- It can **overrule a correct deterministic analysis**; on my dev set the LLM stage lowered accuracy relative to the analyzer alone until the prompt was adjusted, which is why verification matters.
