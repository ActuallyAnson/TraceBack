# Topology-2 run notes

- Plan, incident list, no-tuning rules and the architecture-description sentence: `PLAN.md` (committed before any incident existed).
- Frozen files: `FROZEN_HASHES`; `check_frozen.sh` confirms the analyzer and prompts are byte-identical. It passed before and after the runs.
- Scoring runs are logged in `runs.log` (analyzer, Traceback, baseline, once each, 14 incidents). The `+dirty` suffix on those log lines comes from
  uncommitted result files and the generated results directory, not from source changes (the frozen check above is the authoritative test).
- Raw outputs of the original scoring runs: `analyzer.txt`, `traceback.txt`, `baseline.txt`. `RESULTS.md` is regenerated from the cached model answers by `report.py`.
- Nothing was re-run or changed after seeing results. No crashes occurred.
- One input difference by design: each incident's `meta.json` carries the architecture sentence from `PLAN.md`; the reasoner substitutes it for the first paragraph of the system prompt.
- The verifier needed topology-2 support (its replay environment), which is infrastructure, not a rule change.
