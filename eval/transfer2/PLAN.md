# Larger transfer test: RCAEval RE2 Sock Shop (resource and network faults). Pre-registered plan

Branch `transfer2` (stacked on `transfer`). Written and frozen before any system was run on the test data. Nothing is changed in hybrid 2, its converter or its plan (`eval/transfer/TRANSFER_HASHES` still passes).

## Why
The first transfer test (`eval/transfer/`) had 60 fault cases from two systems and code-level faults. This one adds a **larger and different fault class**: RCAEval RE2-SS (Sock Shop, `RE2-SS.zip`, md5 `bd747a8fc7c5be00c613e13fbf9dd74b`, CC BY 4.0): 30 case directories (carts, catalogue, orders, payment, user x cpu, delay, disk, loss, mem, socket) x 3 repeats = **90 fault cases**, plus **30 clean windows** (repeat 1 of each directory, 700 to 580 s before the injection). Telemetry: logs and resource metrics only (no traces). Labels: the service in the directory name, written by the dataset authors.

## What is different from the first transfer test, and the expectation
Resource faults (CPU stress, memory, disk, network delay/loss, sockets) often produce **no error lines** in the logs of the faulty service. Hybrid 2 only trusts measured log bursts (and the model's reading of the logs), so I expect many "no answer" results here, which count as misses. This is a test of whether the calibration advantage holds where the localization advantage may not. Sock Shop was part of the first test's test set (code-level faults), so it is not a new application.

## Systems (once, after the lock)
Raw-logs baseline and hybrid 2 exactly as frozen (same model `nemotron-3-super`, temperature 0; hybrid 2 reuses the baseline's call). Metrics: faults localized (strict), clean-window alarms (medium or high), wrong at high, wrong at medium or high, no-answer rate; per fault type and pooled; every wrong answer listed. No change after the results; no significance claims; the 3 repeats of a fault are not independent. Expected cost about $2.5 (Super model).
