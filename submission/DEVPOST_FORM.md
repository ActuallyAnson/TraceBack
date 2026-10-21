# Devpost form: paste-ready answers

Fields you must decide yourself are marked 🧑 (country, submitter type, age/employee checkboxes, ratings, IRL city). The 1-10 ratings are
suggestions from what the repo measured; change them if your experience differed. Nothing here claims more than the repo shows.

## General info
- **Project name:** Traceback
- **Elevator pitch** (178 chars): Root-cause analysis you can check: measured evidence, cited records, and a replay with the suspected cause switched off. Nemotron explains the candidates; it never guesses alone.
- **Thumbnail / gallery:** `submission/media/` (3:2 JPG/PNG, each under 5 MB). Use `00_thumbnail.png` as the thumbnail.

## About the project (Markdown; paste everything below the line)
---
## Inspiration
When a request fails across several services, the error shows up in one place and the cause is in another. Pasting
logs into a chatbot gives a fluent guess with nothing to verify. We wanted an investigator whose every claim points
to a real record, and whose conclusion is tested by asking: *if this were the cause, would switching it off make the
failure go away?*

## What it does
Traceback takes the logs, spans, metrics and deploy events from a failed multi-service request and:
1. aligns the services' clocks (estimated only from causal span pairs, so clock skew can't scramble the timeline),
2. correlates spans into traces and builds the dependency graph,
3. produces ranked candidate causes with the evidence records behind each (deterministic rules, no model),
4. asks **NVIDIA Nemotron on Nebius Token Factory** to rank, explain and check those candidates, outputting a causal chain with a
   confidence label and *confirmed vs uncertain* per claim, where every claim must cite real record IDs (unknown IDs are rejected and the model is asked again),
5. **replays the incident with the suspected cause switched off** and reports whether the symptom disappears, which can lower confidence but never changes the answer.

It is not a log summarizer: the analysis layer works without any model, and a baseline that feeds raw logs to the same model is measured against it.

## How we built it
C++20 core (simulator, clock estimator, correlator, graph, rules, libcurl Token Factory client, verifier), CMake, a vendored single-header
JSON parser, Python (standard library) for the evaluation harness, and a static, dependency-free web UI. Evaluation uses a
deterministic simulator of a four-service backend with background noise, lost lines, clock skew, misleading symptoms and decoy faults;
58 labeled incidents, a held-out test split locked before any model ran on it, and 6 fault types added to the test split only after the rules were frozen.
Scoring rules were committed before the first run.

## Results (every number is regenerated from saved runs; see the repo README)
**Simulators we wrote (valid for those simulators only).** On the first locked test set (26 incidents) the accuracy gaps are within one incident; what holds up is calibration and cost: Full Traceback was never confidently wrong (raw-logs baseline 7.7%) at about 1/13 of the cost. On two later, harder locked sets (48 incidents across two simulated backends) Full Traceback finds the cause in 95.8% against 68.8% for the raw-logs baseline, with 0% confidently wrong against 29.2%, at about a tenth of the cost.

**Real telemetry (public RCAEval benchmark and the OpenTelemetry Demo; injected faults on demo applications, so not production).** This is where it got honest:
- Our hand-written analyzer does **not** generalize: 0 of 30 on RCAEval Online Boutique, where the raw-logs baseline gets 16.
- Letting the model propose causes and checking them against measured evidence ("hybrid") helped on code faults in unseen systems: baseline 14 of 60, hybrid 22 of 60, with confident wrong answers cut from 19 to 1. With the larger Nemotron Ultra model: 19 vs 38 of 60.
- Resource faults are hard for the model (baseline 2 of 90, hybrid 5 of 90), but a simple per-service metric rule with no model and no cost gets 62 of 90 on Online Boutique and 35 of 90 on Train Ticket (both unseen), with weak network-fault detection and false alarms on 20 of 30 Train Ticket clean windows.
- We also tried combining the two on unseen systems. It did not help (18 of 60 combined vs 22 of 60 hybrid alone), and we report that too.
- The replay verifier needs the simulator, so on real data it checks nothing.

Every wrong answer is listed in the repo, and each test was pre-registered (plan, frozen hashes, locked commit, one run).

## Generalization check on a second simulated topology
We built an event-driven fan-out topology with a message broker (14 incidents) with the analyzer and prompts frozen. Accuracy dropped for every system and Full Traceback has no accuracy advantage there: it ties the raw-logs baseline at 78.6%. With the replay verifier **0% of answers were confidently wrong versus 21.4% for the baseline**, cost was about 1/13 of the baseline's, and every cited ID was valid. The analyzer failed on both decoy incidents; we report it rather than tune it in.

## Challenges
- Models invent IDs and latch onto the first error they see: we added hard citation validation with retry, and a symptom-onset field that ignores isolated early noise.
- Short incidents were invisible to a whole-window failure rate in the verifier; we switched to a peak 30-second rate.
- Keeping evaluation honest: we found and disclosed a simulator bug, relocked before any test scoring, and report a tool crash and its fix.

## Accomplishments we're proud of
Every reported number is regenerated by one command from saved model answers (offline, no API key), and the interactive showcase lets you
open on a failing system, show the obvious approach going wrong, and include a case where Traceback itself is wrong and says it is unsure.

## What we learned
Deterministic structure plus a model that is forced to cite evidence and a replay that checks it gives answers you can verify and that are rarely confidently wrong, at a fraction of the cost of reading raw telemetry. Accuracy on a simulator is not accuracy on real systems: hand-written rules did not transfer, model proposals checked against measured evidence did better, and a larger model helped most. Replay verification is only possible in a controllable environment.

## What's next
Better network delay/loss detection on real data, a validated way to choose between model proposals and metric rules (our router idea is post hoc and unproven), adapters for real log schemas, and replay against canary/feature-flag rollbacks.

## Built with
C++20, CMake, libcurl, nlohmann/json, Python, vanilla JavaScript, NVIDIA Nemotron 3 Super (`nvidia/nemotron-3-super-120b-a12b`), Nebius Token Factory.

---

## Built with (tags, max 25)
C++20, CMake, libcurl, nlohmann/json, Python, JavaScript, HTML, CSS, NVIDIA Nemotron, Nemotron 3 Super, Nemotron 3 Ultra, Nebius Token Factory, OpenTelemetry, RCAEval, GitHub Pages, Kokoro TTS, ffmpeg

## "Try it out" links
- Code: https://github.com/ActuallyAnson/TraceBack
- Live demo: https://actuallyanson.github.io/TraceBack/
- Presentation page: https://actuallyanson.github.io/TraceBack/present.html (works only after the `combo` branch is merged to `main`)

## Demo video URL
Your YouTube link for `submission/traceback_narrated.mp4` (Public, 2:48).

## Additional info
- **Upload a file:** optional; skip it, or zip `submission/` text files if you want. Limit 35 MB.
- **Submitter type / organization / country / Canada province:** 🧑 (organization: "N/A" if individual; province: "N/A" unless you live in Canada)
- **Track:** Best Apps and Agents
- **New or existing before Aug 26, 2026?** New. The repository was created Oct 19, 2026; nothing pre-existing.
- **Public code repository URL:** https://github.com/ActuallyAnson/TraceBack (MIT license; README has setup, `make demo` and how Nemotron and Token Factory are used)
- **Working demo URL:** https://actuallyanson.github.io/TraceBack/

**Which model(s) did you use, and why that size/variant?**
`nvidia/nemotron-3-super-120b-a12b` for every headline result: it is cheap (about $0.30 / $0.90 per million input / output tokens), has a 262k-token context that lets the raw-logs baseline see real telemetry, and follows a strict JSON output format with cited record IDs. I also ran the larger `nvidia/Nemotron-3-Ultra-550b-a55b` as a size check on 60 real-benchmark code faults: the hybrid went from 22 to 38 of 60 correct (baseline 14 to 19), at roughly five times the cost per call, so I report both and did not switch the main results to it.

**Rate Nemotron's output quality for your use case (1-10) and what worked or fell short.** 🧑 Suggested: 7.
Worked: structured causal explanations from compact evidence, strict JSON, cited IDs that existed on the first try almost every time, about 14 s for a 4k-token analysis. Fell short: it can anchor on the first anomaly it sees, overrule a correct deterministic analysis, cite whole log lines when the ID rule is not spelled out, and on real resource faults it was weak (2 of 90 correct for the raw-logs baseline, 5 of 90 for the hybrid). Very large prompts (about 700k tokens on Ultra) were slow and often rejected.

**Fine-tune, prompt-engineer, or out of the box?**
Out of the box, no fine-tuning. Prompt engineering plus structure around it: the model only sees ranked candidates and the records behind them, outputs a fixed JSON format, every cited ID is validated (unknown IDs are rejected and it is asked again), and a replay can lower the confidence but never changes the answer. Prompts and rules were frozen before each locked test.

**How did Nemotron's performance compare to other models you've used?**
I did not benchmark other model families in this project, so I can't claim a comparison. The only comparison I measured is Nemotron 3 Super against the larger Nemotron 3 Ultra (see above), and the same Super model with raw logs against Super with the Traceback structure.

**Which Nebius platform capabilities were most valuable, and how?**
Token Factory's OpenAI-compatible API only. A plain POST from C++ with libcurl worked immediately; `GET /v1/models?verbose=true` gave prices and context lengths for cost accounting; the 262k context made a fair raw-logs baseline possible; usage fields (including reasoning tokens) let me report cost per incident. I did not use Nebius GPU instances, Serverless jobs or endpoints, or any deployment or scaling setup.

**How likely are you to recommend running Nemotron on Nebius (1-10)?** 🧑 Suggested: 8. Easy API, clear pricing and fast for short prompts; docs did not list the NVIDIA model IDs publicly, temperature 0 was not fully repeatable, and 700k-token prompts were slow.

**Experience running Nemotron inference on Nebius vs previous environments (1-10)?** 🧑 Suggested: 8. Same reasons; no hardware to manage, but I have no like-for-like local run to compare against.

**What additional features or improvements would have helped?**
A public page listing current Nemotron model IDs with prices and context; an honored `seed` parameter for repeatable runs; documentation of prompt caching and cached-token discounts; a way to cap or separately budget reasoning tokens (they share the `max_tokens` budget and are billed); and practical guidance for very large prompts. Details are in `submission/FEEDBACK.md`.

**What do you most hope to see from the Nemotron team next?**
Reliable behavior at very long context (our 700k-token runs failed to parse or cite correctly in 3 of 5 cases), less anchoring on the first anomaly, better handling of numeric telemetry (resource faults were the weakest area), and a controllable reasoning budget.

**Did you use Tavily?** No.

**Builders & Brews IRL city:** 🧑 (leave blank if you did not attend an event)

**Age of majority / not an employee of the promotion entities:** 🧑 tick only if true.

**Which AI tools did you leverage?**
Nemotron on Nebius Token Factory at runtime (the product itself). Claude Code (Anthropic) assisted with writing and reviewing code, the evaluation scripts, the web UI and the video tooling; I directed the design and the pre-registered tests and checked the results. Kokoro (open-source TTS) for the synthetic voice-over; Chrome and ffmpeg to render the video.

**Level of learning from the project:** 🧑 edit to your own words. Draft: High. I learned how easily a fluent model answer can be confidently wrong, that hand-written rules do not transfer to real telemetry, that pre-registering plans and reporting negative results changes what you build, and how to make every claim reproducible offline.

**Did you gain AI value you can use in your career?** 🧑 edit to your own words. Draft: Yes. Evaluation design (locked test sets, frozen hashes, honest reporting), grounding model answers in checkable evidence, and cost and calibration accounting for LLM systems.
