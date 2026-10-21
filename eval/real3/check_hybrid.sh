#!/bin/sh
# Fails if the hybrid's sources changed since the freeze (v1, v2 and v3 have their own checks).
cd "$(dirname "$0")/../.." || exit 2
grep -v '^#' eval/real3/HYBRID_HASHES | shasum -a 256 -c --quiet - && echo "the hybrid is unchanged since its freeze" || { echo "HYBRID FILE CHANGED"; exit 1; }
