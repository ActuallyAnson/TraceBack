# Demo video outline (target 2:40, hard limit 3:00)

The checked-in cut (`submission/traceback_demo.mp4`, not tracked; ~2:58) is a real screen recording of the live UI with a synthetic Kokoro `af_heart` narration, so say so when uploading and check the Devpost rules on AI-generated voice. To redo it with your own voice, record the browser and read the script below. No music, no logos. Keep the site zoomed so text is readable. The cut also shows the Second topology tab (14 incidents, fan-out diagram).
Use the **The outage** tab (opens by default). Run `make live` before recording so the label reads SIMULATED · RUN LIVE, or record the hosted site where it reads RECORDED REPLAY: either is fine, but keep the label visible and say which it is.
Tour pick 1 is inc-026 (baseline wrong, Traceback right); pick 2 is inc-003 (Traceback wrong but uncertain). Do not skip the second: it is the honest part.
Everything works offline; nothing needs a live API call.

| Time | Screen | Say (adapt in your own words) |
|---|---|---|
| 0:00–0:20 | **The outage** tab, press "Trigger an outage" (tour pick 1, inc-026). Keep the "SIMULATED" label in frame | "This is a simulated outage in a four-service backend. Checkout failures jump, and the loudest dashboard is payments. Where would you look?" |
| 0:20–0:45 | Act 2: the raw-logs answer, open "the model's actual reply, verbatim", then press "Check against ground truth" | "The obvious tool: paste the logs into the same Nemotron model on Nebius Token Factory. It answers payments, high confidence, with a neat explanation. The simulator knows the truth: it was a retry storm in orders. Confident and wrong." |
| 0:45–1:25 | Act 3: Traceback's answer and its five stages; click "Walk through the evidence step by step", show Clocks, Candidates and a cited record chip | "Traceback does deterministic analysis first: clock alignment, correlation, ranked candidates with evidence IDs. The model only ranks and explains, and every claim cites a record that has to exist." |
| 1:25–1:50 | Replay step, click a wrong candidate | "Then it replays the incident with the suspected cause switched off. The failures vanish for the right cause and persist for the wrong one. An answer the replay can't reproduce can't say high confidence." |
| 1:50–2:25 | Back to **The outage**, "Next outage in the tour" (inc-003) | "It is not always right. Here Traceback is wrong: the analyzer had the right answer and the model overruled it. But the replay was inconclusive, so it reported medium confidence instead of high. That is the point: it tells you when not to trust it." |
| 2:25–2:50 | Scoreboard; mention `make live` | "On the second topology, the raw-logs baseline and Traceback tie on accuracy. What differs: zero confidently wrong answers versus about one in five, about a thirteenth of the cost, and every citation valid. This is a simulator, so it shows the method, not production accuracy. One command reproduces every number." |

Must show: the project working, and how Token Factory and Nemotron are used (say the model name and show the scoreboard cost column).
Upload to YouTube as **Public**. Check the duration reads under 3:00.
