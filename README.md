# Traceback

Incident investigator for a small multi-service backend. Given logs, events and
traces from a failed or slow request, it reconstructs the timeline, ranks likely
root causes with event-ID evidence, and separates confirmed facts from hypotheses.

Status: work in progress (Nebius x NVIDIA Global AI Hackathon entry).
Design is deterministic-first: the LLM only ranks, explains and verifies
hypotheses produced by a non-LLM analysis layer. Results are added once the
evaluation harness has produced them; no numbers are claimed before then.

License: MIT.
