# Transfer to unseen real systems: model proposals + measured evidence (hybrid 2). Pre-registered plan

Branch `transfer` (stacked on `rcaeval`). Written and frozen before any system was run on the test data.

## Question
Earlier results: on real telemetry the hand-written analyzer rules do not transfer (0 of 30 on RCAEval Online Boutique), while the raw-logs baseline does well but raises false alarms on clean windows and is often wrong at "high" confidence. Can a design that keeps the model's strength (reading the evidence and proposing candidates) and adds only **system-agnostic measurements** to check and cap it transfer to systems it has never seen?

## Method (hybrid 2, `src/hybrid2/`, new files)
1. **Proposals:** the model's answer to the raw-logs baseline request (root cause plus alternatives, first 3 distinct, with the record ids it cited), validated as in `tb_hybrid` (service exists, cites exist, no repair).
2. **Measurements, no rule about any service, application or message text:** (a) *log burst*: warning/error/fatal lines per service in the window after its first quarter minus three times its count in that first quarter, at least 3; (b) *failure origin*: analyzer v5's measured origin service, when there are spans with failures. A service is **supported** if either holds. A supported service that (transitively, by the call graph from spans, when there are spans) calls another supported service is a **symptom** and is dropped; the rest are **causes**.
3. **Choice:** the model's first proposal that is a cause. Otherwise the cause with the largest log burst ("measurement only"). Otherwise **no answer** (confidence low): nothing is measurably anomalous.
4. **Confidence:** the model's confidence capped by what is measured: "high" only if the chosen service is the measured failure origin or owns at least half of the causes' log bursts, otherwise "medium"; a proposal that is not the model's top answer, or a measurement-only answer, is at most "medium"; no answer is "low". The model cannot raise confidence.
Replay verification is impossible on these data (no replay environment).

## Data
- **Development (used to design and tune steps 2 to 4, and the converter's log-severity rule):** RCAEval RE3-OB (30 cases + 10 clean windows) and OTel Demo batches 1 to 6 (57 fault runs, 18 clean).
- **Test (never used for any design decision):** RCAEval **RE3-SS** (Sock Shop; logs and metrics only, no traces) and **RE3-TT** (Train Ticket; traces without status codes, Java logs): **60 fault cases and 18 clean windows**, all of them, none dropped. Labels come from the case directory names (service before `_f`), written by the dataset authors. Both are different applications, languages and log formats from the development data.
- Converter `eval/transfer/convert.py` (frozen with `TRANSFER_HASHES`): 120 s windows (injection -40 s .. +80 s; clean: 700 to 580 s before), log severity from keywords/exception class names/HTTP 5xx access lines (the dataset exports none), spans only when `traces.csv` exists.
- Honest limits: injected faults on demo applications; repeats of the same fault are not independent; the development data for the severity rule includes seeing that `AttributeError` was missed on OB.

## Development record (all recorded)
First version of the converter missed exception class names (`AttributeError`) as errors; fixed (regex) before the test windows were built or run. Dev results with the frozen method: baseline vs hybrid 2 on dev data: faults 50/87 vs 52/87; clean-window alarms 25/28 vs 1/28; wrong at high 15 vs 3. RCAEval OB alone: 19/30 vs 18/30.

## Test run (once, after the lock)
Systems: raw-logs baseline and hybrid 2 (same model `nemotron-3-super`, temperature 0; hybrid 2 reuses the baseline's call, so it adds no cost). Metrics: faults localized (strict: the answer's service equals the label), clean-window alarms (medium or high), wrong at high, no-answer rate; per system (SS, TT) and pooled; every wrong answer listed. No change after the results. No significance claims. Cost expected under $3 (Super model).
