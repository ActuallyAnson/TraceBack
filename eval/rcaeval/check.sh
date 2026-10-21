#!/bin/sh
cd "$(dirname "$0")/../.." || exit 2
grep -v '^#' eval/rcaeval/RCA_HASHES | shasum -a 256 -c --quiet - && echo "the RCAEval converter, builder and plan are unchanged since the freeze" || { echo "RCAEVAL FILE CHANGED"; exit 1; }
