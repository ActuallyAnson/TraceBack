# Public benchmark check: RCAEval RE3-OB (Online Boutique, code-level faults). Pre-registered plan

Branch `rcaeval` (stacked on `model-check`). Written and frozen before any system was run on the test data.

## Why
All earlier real-telemetry batches come from one demo application driven by our own scenarios. RCAEval (Pham et al., arXiv 2412.17015, CC BY 4.0, Zenodo record 14590730) is an independently built benchmark: other people injected the faults, recorded traces, logs and metrics, and wrote the root-cause labels. This is a transfer test: the analyzer (v6) and the prompts were developed on the OTel Demo and the simulators and are **not changed** here.
Honest limits: injected faults on a demo application again; one system (Online Boutique); 30 cases that are 10 distinct faults repeated 3 times, so they are not 30 independent samples.

## Data
`RE3-OB.zip` (190 MB, md5 `96947589084348f9d12cba370313458e`): 10 fault directories (`adservice_f3/f4/f5`, `cartservice_f1`, `currencyservice_f1`, `emailservice_f1..f5`) x 3 repeats = **30 fault cases**; per case `traces.csv`, `logs.csv`, `inject_time.txt`, resource metrics. **Test set = all 30 cases, none dropped.** The root-cause label is the service named by the directory (`emailservice_f1` -> `emailservice`), written by the dataset authors, extracted by `build.py` before any system runs.
Additionally **10 clean windows** (repeat 1 of each fault directory, the window 700 to 580 s before the injection) for false alarms.
Development data for the converter: `RE3-TT.zip` (Train Ticket, same CSV schema; its traces carry no status codes, so it checks the mechanics only). `RE3-SS` (Sock Shop) has no `traces.csv` and is unused. Nothing from RE3-OB was used to design the converter beyond reading the column names and the status-code values in four cases while writing it; no analyzer was run on RE3-OB before the lock.

## Converter (`convert.py`, rules in its docstring, frozen by `RCA_HASHES`)
120 s window: injection -40 s .. +80 s (same shape as the OTel runs). Server/client kinds inferred from the parent's service, internal spans dropped and children re-parented to the nearest retained ancestor, gRPC codes mapped to HTTP-like statuses, root spans failed if any descendant failed, log severity from message keywords (the dataset exports none), metrics derived from spans, no resource metrics, no ops events.

## Systems (once, after the lock)
Raw-logs baseline; analyzer v2 (the pre-real-telemetry analyzer); analyzer v6; Full Traceback v6 (analyzer v6, v2 prompts, override rule); same model (`nemotron-3-super`), temperature 0. Replay verification is impossible (no replay environment), so the verifier policy for real data applies (a "high" becomes "medium"), shown as in the OTel batches.
Scoring: strict localization (answer's service equals the label's service); clean-window alarms (medium or high). Every wrong answer is listed. No tuning after results; no significance claims.
Cost cap about $1.5 (Super model).
