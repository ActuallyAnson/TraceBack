#!/bin/sh
cd "$(dirname "$0")/../.." || exit 2
grep -v '^#' eval/real6/V6_HASHES | shasum -a 256 -c --quiet - && echo "analyzer v6 and the batch-6 scenarios are unchanged since the freeze" || { echo "V6 FILE CHANGED"; exit 1; }
