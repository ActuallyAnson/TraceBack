#!/bin/sh
cd "$(dirname "$0")/../.." || exit 2
grep -v '^#' eval/metrics/METRICS_HASHES | shasum -a 256 -c --quiet - && echo "rule M, its scorer and the plan are unchanged since the freeze" || { echo "METRICS FILE CHANGED"; exit 1; }
