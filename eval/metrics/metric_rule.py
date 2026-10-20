#!/usr/bin/env python3
"""Rule M (eval/metrics/PLAN.md): per-service metric anomaly, no model, no traces, no logs, nothing keyed on service or metric names beyond the `<service>_<kind>` column convention.
The 120 s window is split into a quiet baseline (first quarter) and the rest. For every numeric column, z = |mean(rest) - mean(baseline)| / max(std(baseline), 5% of |mean(baseline)|, 5% of the column's range over the window, 1e-9) (the range term bounds z at 20 and stops near-constant counters from producing astronomical scores).
A service scores the largest z of its columns; the answer is the top service if its score reaches TAU, else no answer."""
import csv, statistics

TAU = 10.0  # fixed on the development data (RE2-SS) before any test data was opened


def window_scores(path, w0, w1):
    rows = list(csv.DictReader(open(path, newline="", encoding="utf-8", errors="replace")))
    if not rows:
        return {}
    t = [int(float(r["time"])) for r in rows]
    split = w0 + (w1 - w0) // 4
    out = {}
    for c in rows[0]:
        if c == "time":
            continue
        try:
            v = [float(r[c]) if r[c] not in ("", "nan") else float("nan") for r in rows]
        except ValueError:
            continue
        base = [x for tt, x in zip(t, v) if w0 <= tt < split and x == x]
        rest = [x for tt, x in zip(t, v) if split <= tt < w1 and x == x]
        if len(base) < 10 or len(rest) < 10:
            continue
        mb = statistics.mean(base)
        rng = max(base + rest) - min(base + rest)
        z = abs(statistics.mean(rest) - mb) / max(statistics.pstdev(base), 0.05 * abs(mb), 0.05 * rng, 1e-9)
        svc = c.rsplit("_", 1)[0]
        out[svc] = max(out.get(svc, 0.0), z)
    return out


def answer(scores, tau=TAU):
    if not scores:
        return None, 0.0
    top = max(scores, key=scores.get)
    return (top, scores[top]) if scores[top] >= tau else (None, scores[top])
