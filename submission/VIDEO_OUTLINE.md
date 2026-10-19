# Demo video outline (target 2:40, hard limit 3:00)

Record your screen (browser + one terminal) with your own voice. No music, no logos. Keep the site zoomed so text is readable.
Use the incident **inc-026** (opens by default): the raw-logs baseline got it wrong, Traceback got it right and the replay verified it.
Everything below works offline; nothing needs a live API call.

| Time | Screen | Say (adapt in your own words) |
|---|---|---|
| 0:00–0:15 | Site, Investigate view, inc-026 | "When a request fails across services, the error shows up in one place and the cause is somewhere else. Traceback finds the cause and shows its evidence." |
| 0:15–0:35 | Step 1 Telemetry | "This is what the system starts from: thousands of log lines and spans, noisy, with no labels." |
| 0:35–0:55 | Step 2 Clocks, toggle raw vs aligned | "Each service's clock disagrees. Traceback estimates the offsets only from parent/child span pairs, so the events line up." |
| 0:55–1:15 | Steps 3–4 Graph, candidates | "Deterministic rules turn this into ranked candidate causes, each with the exact records behind it. No model yet." |
| 1:15–1:50 | Step 5 LLM, then switch to the baseline | "Now NVIDIA Nemotron, served on Nebius Token Factory, ranks and explains. Every claim cites record IDs. For comparison, the same model reading raw logs gets this wrong and says it is highly confident." (Show claims, click a chip to open a record.) |
| 1:50–2:05 | Step 6 Citations | "Each cited ID is checked against the data. Invented IDs are rejected and the model is asked again." |
| 2:05–2:30 | Step 7 Replay, click a wrong candidate | "The verifier replays the incident with the suspected cause switched off: the failures vanish. Click a wrong candidate and the incident persists. This only lowers confidence, it never changes the answer." |
| 2:30–2:50 | Scoreboard tab; terminal `make demo` | "On a held-out test set it is as accurate as the best system, was never confidently wrong in this run, and costs about a tenth per incident. The gaps in accuracy are within one incident, so we don't claim more. One command reproduces every number; it is all open source." |

Must show: the project working, and how Token Factory and Nemotron are used (say the model name and show the scoreboard cost column).
Upload to YouTube as **Public**. Check the duration reads under 3:00.
