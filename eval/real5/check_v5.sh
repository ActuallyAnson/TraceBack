#!/bin/sh
cd "$(dirname "$0")/../.." || exit 2
grep -v '^#' eval/real5/V5_HASHES | shasum -a 256 -c --quiet - && echo "analyzer v5 and the batch-5 scenarios are unchanged since the freeze" || { echo "V5 FILE CHANGED"; exit 1; }
