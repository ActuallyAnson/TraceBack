#!/bin/sh
# Fails if analyzer v4, the v3 converter or the batch-4 scenarios changed since the freeze.
cd "$(dirname "$0")/../.." || exit 2
grep -v '^#' eval/real4/V4_HASHES | shasum -a 256 -c --quiet - && echo "analyzer v4, converter v3 and the batch-4 scenarios are unchanged since the freeze" || { echo "V4 FILE CHANGED"; exit 1; }
