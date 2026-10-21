# YouTube upload text

**Title:** Traceback: root-cause analysis you can check (Nebius x NVIDIA hackathon)

**Visibility:** Public. **Length:** 2:48. **Captions:** upload `traceback_narrated.srt`.

**Description (paste):**

Traceback is an incident investigator for small multi-service backends. It measures first (clock alignment, trace correlation, ranked candidate causes tied to evidence IDs), then asks NVIDIA Nemotron on Nebius Token Factory to rank and explain only those candidates, checks that every citation exists, and replays the incident with the suspected cause switched off.

This video shows the real app on a simulated outage (a recorded replay of saved model answers, not a live call), a case where Traceback is wrong and says it is unsure, and the results, including what did not generalize on real benchmark data (RCAEval): the hand-written analyzer scored 0 of 30 there.

Everything reproduces offline with no API key (`make results-check`). Simulator results are valid for our simulators only; real-data tests use injected faults on demo applications.

The voice-over is synthetic (Kokoro, open source).

Code: https://github.com/ActuallyAnson/TraceBack
Live demo: https://actuallyanson.github.io/TraceBack/
Model: nvidia/nemotron-3-super-120b-a12b on Nebius Token Factory

**Devpost form fields:** track = Best Apps and Agents; description = `DEVPOST_DESCRIPTION.md`; feedback = `FEEDBACK.md`; repo, demo and video URLs as above.
